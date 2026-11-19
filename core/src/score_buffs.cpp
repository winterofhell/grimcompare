#include <algorithm>
#include <array>
#include <tuple>

#include "gdc/stat_model.h"

#include "score_internal.h"

namespace gdc::score {
namespace {

// re/CombatAttributeType.tsv 42..53; Min / Max seconds, Chance %.
constexpr std::string_view kControl[] = {"Stun", "Freeze", "Knockdown", "Fear", "Confusion", "Petrify", "Sleep", "Trap", "Convert"};

// An enemy is held once: longest control x its chance.
double held_seconds(const arz::Record* rec, const arz::Record* buff, std::uint32_t lv) {
  double held = 0;
  static const auto kControlFields = [] {  // offensive<Kind>Min / Max / Chance
    std::array<std::array<std::string, 3>, std::size(kControl)> out;
    for (std::size_t i = 0; i < out.size(); ++i)
      for (std::size_t j = 0; j < 3; ++j) out[i][j] = "offensive" + std::string(kControl[i]) + (j == 0 ? "Min" : j == 1 ? "Max" : "Chance");
    return out;
  }();
  for (const arz::Record* r : {rec, buff})
    for (const auto& [fmin, fmax, fchance] : kControlFields) {
      const double min = r ? at_level(*r, fmin, lv) : 0, chance = r ? at_level(*r, fchance, lv) : 0;
      if (min > 0) held = std::max(held, (min + std::max(min, at_level(*r, fmax, lv))) / 2 * (chance > 0 && chance < 100 ? chance / 100 : 1));
    }
  return held;
}

using Modifiers = std::vector<std::pair<std::string, std::uint32_t>>;

// damageAbsorptionPercent / 100 and damageAbsorption / one hit, the skill's plus its modifiers' (Mark of Torment).
std::pair<double, double> absorption(const arz::Record& stats, std::uint32_t lv, const Modifiers& mods,
                                     const arz::Database& db, double hit) {
  double pct = at_level(stats, "damageAbsorptionPercent", lv), flat = at_level(stats, "damageAbsorption", lv);
  for (const auto& [m, ml] : mods)
    if (const arz::Record* r = db.record(m)) {
      pct += at_level(*r, "damageAbsorptionPercent", ml);
      flat += at_level(*r, "damageAbsorption", ml);
    }
  return {std::clamp(pct / 100, 0.0, 1.0), hit > 0 ? std::clamp(flat / hit, 0.0, 1.0) : 0.0};
}

Modifiers linked(const snap::Snapshot& s, const snap::Skill& sk) {
  Modifiers out;
  for (const std::uint32_t id : sk.modifier_ids)
    for (const auto& m : s.skills)
      if (m.id == id && m.current_level > 0) out.emplace_back(m.record, m.current_level);
  for (const auto& m : s.item_skill_modifiers)
    if (m.skill_id == sk.id && !m.modifier_record.empty()) out.emplace_back(m.modifier_record, 1);
  return out;
}

}  // namespace

std::vector<Buff> learned_buffs(const snap::Snapshot& s, const Settings& p, const arz::Database& db) {
  std::vector<Buff> out;
  const double cdr = utility::cooldown_reduction(s);
  for (const auto& sk : s.skills) {
    if (!sk.buff_active || *sk.buff_active) continue;  // unknown, or still in the totals
    const auto t = model::buff_timing(sk, db);
    if (!t) continue;
    Buff b{sk.record, 1, {}, model::buff_effect(s, sk, db)};
    if (const arz::Record* rec = db.record(sk.record)) {
      const arz::Record* buff = db.record(rec->string("buffSkillName"));
      std::tie(b.absorb_pct, b.absorb_hit) = absorption(buff ? *buff : *rec, sk.current_level, linked(s, sk), db, enemy_hit(s, p, false));
    }
    if (const auto o = p.uptime.find(sk.record); !p.recast_buffs && o != p.uptime.end()) {
      b.uptime = o->second;
      b.basis = p.live_buffs ? "live" : "preset [uptime]";
    } else if (t->recharge > 0) {
      const double recharge = t->recharge * (1 - cdr);
      b.uptime = recharge > 0 ? std::min(1.0, t->duration / recharge) : 1.0;
      b.basis = fmt(t->duration, 1) + " s / (" + fmt(t->recharge, 1) + " s x (1 - " + fmt(cdr * 100) + "% cooldown reduction))";
    } else {
      b.basis = fmt(t->duration, 1) + " s, no recharge";
    }
    out.push_back(std::move(b));
  }
  return out;
}

Procs procs(const snap::Snapshot& s, const Side& side, const Settings& p, const arz::Database& db) {
  Procs out;
  out.enemy_oa = effects::shred(side.effects.reductions, effects::kEnemyOA);
  for (const auto& r : side.effects.reductions)
    if (r.category == effects::kEnemyDamage && r.uptime > 0) out.enemy_damage.emplace_back(r.amount.at(0) / 100, r.uptime);
  const double hit = enemy_hit(s, p, false), life = model::sheet(s).life;
  double free = 1;
  for (const auto& e : side.effects.effects) {
    if (e.pet) continue;
    const arz::Record* rec = db.record(e.record);
    // Cast buffs come from learned_buffs(); here self-triggered passives only.
    if (!rec || (e.source == "learned" && !model::triggered_buff(*rec))) continue;
    const arz::Record* buff = db.record(rec->string("buffSkillName"));
    const std::uint32_t lv = std::max<std::uint32_t>(e.level, 1);
    const auto preset_uptime = p.uptime.find(e.record);
    const bool known = e.maintained || e.per_sec > 0 || preset_uptime != p.uptime.end();
    auto up = [&](double duration) { return preset_uptime != p.uptime.end() ? preset_uptime->second : effects::uptime(e, duration); };
    auto seen = [&](const std::string& what) {
      if (known) out.scored.insert(e.record), out.inputs += (out.inputs.empty() ? "" : ", ") + stem(e.record) + " " + what;
      else out.unknown.emplace(e.record, e.basis);
      return known;
    };
    const double held = held_seconds(rec, buff, lv);
    if (held > 0 && seen("cc " + fmt(up(held), 3))) free *= 1 - up(held);
    const arz::Record* stats = buff && model::self_buff(*buff) ? buff : model::self_buff(*rec) ? rec : nullptr;
    if (!stats) continue;
    // Low-health buffs: up while needed, if off cooldown (design choice).
    const double duration = at_level(*stats, "skillActiveDuration", lv);
    const double u = preset_uptime != p.uptime.end() || e.life_below <= 0 || p.low_health_per_sec <= 0
                         ? up(duration)
                         : std::min(1.0, e.life_below / 100) * std::min(1.0, e.per_sec / p.low_health_per_sec);
    if (!seen("buff " + fmt(u, 3))) continue;
    if (duration > 0) {
      const auto [pct, per_hit] = absorption(*stats, lv, e.modifiers, db, hit);
      // MECHANICS §2 rule 7: each record's multiplier compounds (item_torment -25%).
      double mult = std::max(0.0, 1 + at_level(*stats, "offensiveDamageMultModifier", lv) / 100);
      for (const auto& [m, ml] : e.modifiers)
        if (const arz::Record* mr = db.record(m)) mult *= std::max(0.0, 1 + at_level(*mr, "offensiveDamageMultModifier", ml) / 100);
      out.buffs.push_back({e.record, u, preset_uptime != p.uptime.end() ? "preset [uptime]" : fmt(duration, 1) + " s, " + e.basis,
                           db.derived<model::BuffEffect>(*stats, lv, [&] { return model::buff_effect(*stats, lv); }), pct, per_hit,
                           mult});
    } else {
      out.restored += e.per_sec * at_level(*stats, "damageAbsorption", lv);
    }
    out.restored += e.per_sec * (at_level(*stats, "skillLifePercent", lv) / 100 * life + at_level(*stats, "skillLifeBonus", lv));
  }
  out.cc = 1 - free;
  return out;
}

// `shares`: buff record -> uptime in this state; unnamed: own uptime.
double state(const snap::Snapshot& s, const Procs& procs, const std::vector<Buff>& learned,
             const std::map<std::string, double>& shares, const arz::Database& db, snap::Snapshot& out, double* mult) {
  if (mult) *mult = 1;
  std::vector<std::pair<const model::BuffEffect*, double>> buffs;
  double absorbed = 1;
  for (const auto* list : {&procs.buffs, &learned})
    for (const auto& b : *list) {
      const auto it = shares.find(b.record);
      const double sh = it != shares.end() ? it->second : b.uptime;
      buffs.emplace_back(&b.effect, sh);
      absorbed *= (1 - sh * b.absorb_pct) * (1 - sh * b.absorb_hit);
      if (mult) *mult *= 1 + sh * (b.mult - 1);  // a share between 0 and 1: its expected value
    }
  model::buffed(s, buffs, db, out);
  // Floor: one hit size stands for all, avoid fake immunity.
  return std::max(0.1, absorbed);
}

BuffView buff_view(const snap::Snapshot& s, const std::vector<Skill>& skills, const Settings& p, const arz::Database* db) {
  BuffView out;
  if (!db) {
    out.sheet = model::sheet(s);
    return out;
  }
  std::vector<std::string> notes;
  const Side character = side(s, skills, p, *db, {}, notes);
  const Procs triggered = procs(s, character, p, *db);
  const std::vector<Buff> learned = learned_buffs(s, p, *db);
  std::vector<Buff> all = learned;
  for (const auto& b : triggered.buffs)
    if (std::none_of(all.begin(), all.end(), [&](const Buff& o) { return o.record == b.record; })) all.push_back(b);
  std::map<std::string, double> usual;
  for (const auto& b : all) usual[b.record] = b.uptime >= 0.5;
  snap::Snapshot work = s;
  auto sheet = [&](const std::map<std::string, double>& shares) {
    state(s, triggered, learned, shares, *db, work);
    return model::sheet(work);
  };
  out.sheet = sheet(usual);
  for (const auto& b : all) {
    auto other = usual;
    other[b.record] = !(b.uptime >= 0.5);
    const model::Sheet flipped = sheet(other);
    out.buffs.push_back({b.record, b.uptime, b.basis, b.uptime >= 0.5 ? flipped : out.sheet, b.uptime >= 0.5 ? out.sheet : flipped,
                         b.effect, b.absorb_pct, b.absorb_hit});
  }
  return out;
}

}  // namespace gdc::score
