#include <algorithm>
#include <cmath>
#include <numeric>

#include "gdc/stat_model.h"
#include "gdc/utility.h"

#include "score_internal.h"

namespace gdc::score {
namespace {

// GameEngine::GetComboChargeMultiplier 0x2D8D20: skillComboChargeMultipliers[charges - 1], the last past the end;
// between whole charges, the line between their entries.
double combo_pct(const arz::Database& db, double charges) {
  const arz::Record* r = db.record("records/game/gameengine.dbr");
  if (!r || charges <= 0) return 0;
  const auto it = r->fields.find("skillComboChargeMultipliers");
  if (it == r->fields.end() || it->second.numbers.empty()) return 0;
  const auto& m = it->second.numbers;
  auto at = [&](double n) { return n < 1 ? 0.0 : m[std::min(static_cast<std::size_t>(n), m.size()) - 1]; };
  const double whole = std::floor(charges);
  return at(whole) + (at(whole + 1) - at(whole)) * (charges - whole);
}

// Combo charges (Onslaught). SkillManager::IncrementComboCharge 0x53F990: one per hit of a skill with
// skillComboChargeLevel, up to that level. Skill::CollectComboMultiplier 0x4993D0: a skill whose cooldown runs
// (GetCooldownTotal 0x491270 > 0) gets DamageScaleInfo 100 (0x7A710C) + GetComboChargeMultiplier(charges) %, then
// Character::SpendComboCharges 0x603F0 spends them unless a roll keeps them (utility::combo_keep).
// Charges at a cooldown skill's use: the builder's uses between two cooldown skill uses, over the spend chance.
// shortcut: the strongest builder at its own rate fills the time between cooldown skill uses; the charge time gate
// (GameEngine+0x1080 / attack speed), the skillComboChargeDuration expiry, procs with cooldowns spending charges and
// the DamageScaleInfo consumer (taken as a multiplier on the whole hit) are not traced — revisit with an in-game
// Onslaught + cooldown skill test.
void combo(const snap::Snapshot& s, const Side& side, const arz::Database& db, std::vector<SkillDamage>& skills, bool explain) {
  double cap = 0, rate = 0, spends = 0;
  for (std::size_t k = 0; k < side.mains.size(); ++k) {
    if (side.mains[k].combo > 0 && skills[k].rate > 0) cap = std::max(cap, side.mains[k].combo), rate = std::max(rate, skills[k].rate);
    if (skills[k].cooldown > 0 && skills[k].per_use > 0) spends += skills[k].uses;
  }
  if (cap <= 0 || spends <= 0) return;
  const double keep = utility::combo_keep(s);
  const double charges = keep < 1 ? std::min(cap, rate / spends / (1 - keep)) : cap;
  const double f = 1 + combo_pct(db, charges) / 100;
  for (auto& d : skills)
    if (d.cooldown > 0 && d.per_use > 0) {
      d.value *= f, d.per_use *= f, d.plain *= f;
      if (explain) d.inputs += " x" + fmt(f, 3) + " combo (" + fmt(charges, 2) + " charges)";
    }
}

// `explain`: build log text; only the shown state needs it.
// `mult`: the state's buff damage multiplier.
Eval eval(const snap::Snapshot& s, double absorbed, const Side& side, const Procs& procs, const Settings& p,
          const arz::Database* db, std::vector<std::string>& notes, bool explain = true,
          std::vector<Converted>* converted = nullptr, double mult = 1) {
  Eval e;
  if (db) {
    std::optional<Converted> own;
    const Converted& conv = converted_for(s, side, p, converted, own, notes);
    for (std::size_t k = 0; k < side.mains.size(); ++k) {
      const auto& m = side.mains[k];
      e.skills.push_back(m.skill ? skill_damage(s, *m.skill, m.raw, side.gear, p, notes, nullptr, &side.resist, explain, &conv.mains[k])
                              : SkillDamage{});
      SkillDamage& d = e.skills.back();
      d.plain = d.per_use;
      if (m.skill && m.charges > 0 && d.per_use > 0) {
        // Every (charges + 1)th use is the skill's own hit (Cadence: two plain hits, then the finale).
        const SkillDamage plain = skill_damage(s, *m.skill, m.plain, side.gear, p, notes, nullptr, &side.resist, false);
        const double cycle = (m.charges * plain.per_use + d.per_use) / (m.charges + 1);
        d.value *= cycle / d.per_use;
        d.per_use = cycle, d.plain = plain.per_use;
        if (explain) d.inputs += " cycle " + fmt(m.charges, 0) + " x " + fmt(plain.per_use) + " plain + 1 = " + fmt(cycle);
      }
      d.value *= mult, d.per_use *= mult, d.plain *= mult;
      if (explain && mult != 1 && !e.skills.back().inputs.empty()) e.skills.back().inputs += " x" + fmt(mult, 3) + " buff mult";
    }
    combo(s, side, *db, e.skills, explain);
    // The trigger rates at this state's speeds (as rates() in score_side.cpp): the main skills' uses/s, the crit
    // chance, the weapon pool skills' uses, whose share the weapon pool attacks take.
    effects::Rates now;
    double replaced = 0;
    for (std::size_t k = 0; k < side.mains.size(); ++k) {
      const Main& m = side.mains[k];
      if (!m.skill) continue;
      const double rate = use_rate(s, *m.skill, m.raw);
      now.action += m.weight * rate;
      const double uses = m.pool * m.pool_share() * rate;
      now.weapon += uses;
      replaced += uses * e.skills[k].plain;
    }
    now.crit = attack(s, p, 0, {}).crit_chance;
    e.extra = extra_damage(s, side, p, explain, conv, mult, now);
    // Weapon pool attacks are counted whole in extra; the weapon pool skill uses they take come off here, so the main
    // skills' values keep their per-use meaning. Only the uses each fires on: a cooldown keeps it under its chance.
    double pooled = 0;
    for (const auto& h : side.hits)
      if (h.pool_chance > 0 && now.weapon > 0) pooled += std::min(h.pool_chance, h.rate(now, side.rates) / now.weapon);
    if (pooled > 0 && replaced > 0) {
      e.extra.per_sec -= pooled * replaced;
      if (explain) e.extra.inputs += " replaced weapon pool uses " + fmt(pooled, 3) + " x " + fmt(replaced) + "/s";
    }
  }
  e.survival = survival(s, procs, absorbed, p, db, explain);
  e.other = other(s, side.utility, p, explain);
  return e;
}

constexpr std::size_t kMaxEnumerated = 4;  // 2^4 buff states at most

// Buffs with unmapped stats change nothing, so get no state.
std::vector<const Buff*> open_buffs(const std::vector<Buff>& learned, const std::vector<Buff>& procs) {
  std::vector<const Buff*> open;
  for (const auto* list : {&learned, &procs})
    for (const auto& b : *list)
      if (b.uptime > 0 && b.uptime < 1 && b.matters() &&
          std::none_of(open.begin(), open.end(), [&](const Buff* o) { return o->record == b.record; }))
        open.push_back(&b);
  std::stable_sort(open.begin(), open.end(), [](const Buff* a, const Buff* b) {
    return a->uptime * (1 - a->uptime) > b->uptime * (1 - b->uptime);
  });
  return open;
}

}  // namespace

Scored score_side(const snap::Snapshot& s, const std::vector<Skill>& skills, const Settings& p, const arz::Database* db,
                  const Amounts& base, std::vector<std::string>& notes, const snap::Snapshot* captured) {
  Scored out;
  if (!db) {
    out.side.utility = utility_of(s, p, db);
    out.states.emplace_back(1.0, eval(s, 1, out.side, out.procs, p, db, notes));
    return out;
  }
  out.side = side(s, skills, p, *db, base, notes, captured);
  out.procs = procs(s, out.side, p, *db);
  const std::vector<Buff> learned = learned_buffs(s, out.side, p, *db);
  if (p.retaliation_damage) add_retaliation(s, p, *db, out.procs, learned, out.side);
  std::vector<const Buff*> open = open_buffs(learned, out.procs.buffs);
  if (open.size() > kMaxEnumerated) open.resize(kMaxEnumerated);
  for (const Buff* b : open) out.buffs += (out.buffs.empty() ? "" : ", ") + stem(b->record) + " up " + fmt(b->uptime, 3) + " (" + b->basis + ")";
  const std::size_t n = std::size_t{1} << open.size();
  std::vector<double> probs(n, 1.0);
  for (std::size_t mask = 0; mask < n; ++mask)
    for (std::size_t i = 0; i < open.size(); ++i) probs[mask] *= mask >> i & 1 ? open[i]->uptime : 1 - open[i]->uptime;
  const std::size_t shown = static_cast<std::size_t>(std::max_element(probs.begin(), probs.end()) - probs.begin());
  snap::Snapshot work = s;
  std::vector<Converted> converted;
  for (std::size_t mask = 0; mask < n; ++mask) {
    std::map<std::string, double> shares;
    for (std::size_t i = 0; i < open.size(); ++i) shares[open[i]->record] = mask >> i & 1;
    double mult = 1;
    const double absorbed = state(s, out.procs, learned, shares, *db, work, &mult);
    out.states.emplace_back(probs[mask], eval(work, absorbed, out.side, out.procs, p, db, notes, mask == shown, &converted, mult));
  }
  return out;
}

// Geometric mean per factor; summed parts are scaled means.
Survival mixed_survival(const Scored& scored) {
  Survival out = scored.shown().survival;
  auto g = [&](auto f) { return std::exp(scored.ln_mean(f)); };
  out.ehp = g([](const Eval& e) { return e.survival.ehp; });
  out.life = g([](const Eval& e) { return e.survival.life; });
  out.hit = g([](const Eval& e) { return e.survival.hit; });
  out.oa_debuff = g([](const Eval& e) { return e.survival.oa_debuff; });
  out.avoid = 1 - g([](const Eval& e) { return 1 - e.survival.avoid; });
  out.block = 1 - g([](const Eval& e) { return 1 - e.survival.block; });
  out.reduction = 1 - g([](const Eval& e) { return 1 - e.survival.reduction; });
  out.cc = 1 - g([](const Eval& e) { return 1 - e.survival.cc; });
  out.procs = 1 - g([](const Eval& e) { return 1 - e.survival.procs; });
  auto total = [](const Eval& e) {
    return std::accumulate(e.survival.taken.begin(), e.survival.taken.end(), 0.0, [](double a, const auto& kv) { return a + kv.second; });
  };
  const double sum = scored.mean(total), scale = sum > 0 ? g(total) / sum : 1;
  for (auto& [type, v] : out.taken) v = scale * scored.mean([&, t = type](const Eval& e) { return e.survival.taken.at(t); });
  for (auto& [name, v] : out.thresholds) v = g([&, n = name](const Eval& e) { return e.survival.thresholds.at(n); });
  return out;
}

Other mixed_other(const Scored& scored) {
  Other out = scored.shown().other;
  out.value = std::exp(scored.ln_mean([](const Eval& e) { return e.other.value; }));
  for (auto& [key, v] : out.terms) {  // mean of both halves, one find per state
    double value = 0, weight = 0;
    for (const auto& [prob, e] : scored.states) {
      const auto it = e.other.terms.find(key);
      const std::pair<double, double> t = it != e.other.terms.end() ? it->second : std::pair{0.0, 0.0};
      value += prob * t.first;
      weight += prob * t.second;
    }
    v = {value, weight};
  }
  return out;
}

}  // namespace gdc::score
