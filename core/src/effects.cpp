#include "gdc/effects.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>

#include "effects_internal.h"
#include "gdc/attribute_names.h"
#include "gdc/classes.h"
#include "gdc/sets.h"
#include "gdc/stat_model.h"
#include "gdc/utility.h"

namespace gdc::effects {

double own_or_buff(const arz::Record& r, std::string_view key, std::uint32_t level, const arz::Database& db) {
  if (r.fields.contains(key)) return at(r, key, level);
  const arz::Record* b = db.record(r.string("buffSkillName"));
  return b ? at(*b, key, level) : 0;
}
std::string_view own_or_buff(const arz::Record& r, std::string_view key, const arz::Database& db) {
  if (const auto v = r.string(key); !v.empty()) return v;
  const arz::Record* b = db.record(r.string("buffSkillName"));
  return b ? b->string(key) : std::string_view{};
}

std::string fmt(double v, int decimals) {
  char buf[400];  // fits %.*f of any double; to_chars rounds as printf does
  return {buf, std::to_chars(buf, buf + sizeof buf, v, std::chars_format::fixed, decimals).ptr};
}

namespace {

// Elemental (58) lands on Cold, Fire, Lightning.
struct Resist {
  std::string_view field;
  int type;
};
constexpr Resist kDebuffFields[] = {
    {"defensivePhysical", 2}, {"defensivePierce", 4}, {"defensiveCold", 5},   {"defensiveFire", 6},
    {"defensivePoison", 7},   {"defensiveLightning", 8}, {"defensiveLife", 9}, {"defensiveChaos", 10},
    {"defensiveAether", 11},  {"defensiveBleeding", 15}, {"defensiveElementalResistance", 58}};
constexpr int kElementalTypes[] = {combat_attr::kCold, combat_attr::kFire, combat_attr::kLightning};

// `on`: 0 = every resist, 2 = Physical, 58 = Elemental.
struct Offensive {
  std::string_view min, duration;
  int category, on;
};
constexpr Offensive kOffensiveFields[] = {
    {"offensiveTotalResistanceReductionPercentMin", "offensiveTotalResistanceReductionPercentDurationMin", 2, 0},
    {"offensiveTotalResistanceReductionAbsoluteMin", "offensiveTotalResistanceReductionAbsoluteDurationMin", 3, 0},
    {"offensivePhysicalResistanceReductionPercentMin", "offensivePhysicalResistanceReductionPercentDurationMin", 2, 2},
    {"offensivePhysicalResistanceReductionAbsoluteMin", "offensivePhysicalResistanceReductionAbsoluteDurationMin", 3, 2},
    {"offensiveElementalResistanceReductionPercentMin", "offensiveElementalResistanceReductionPercentDurationMin", 2, 58},
    {"offensiveElementalResistanceReductionAbsoluteMin", "offensiveElementalResistanceReductionAbsoluteDurationMin", 3, 58}};

bool debuff(const arz::Record& r) { return r.number("debufSkill") > 0 || r.cls == "SkillBuff_Debuf"; }

bool pet_summon(const arz::Record& r) { return r.cls == "Skill_SpawnPet" || r.cls == "Skill_TargetedSpawnPet"; }

void spread(std::map<int, double>& out, int type, double v, const std::vector<int>& all) {
  if (type == 0)
    for (int t : all) out[t] += v;
  else if (type == combat_attr::kElemental)
    for (int t : kElementalTypes) out[t] += v;
  else
    out[type] += v;
}

// A number or "itemLevel/4+1", floored, from the base item.
std::uint32_t item_skill_level(std::string_view eq, const arz::Record& item, std::string& note) {
  std::uint32_t v = 0;
  const auto [end, ec] = std::from_chars(eq.data(), eq.data() + eq.size(), v);
  if (ec == std::errc{} && end == eq.data() + eq.size()) return v;
  std::string lower(eq);
  for (auto& c : lower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  if (lower == "itemlevel/4+1") {
    note = ", level itemLevel/4+1";
    return static_cast<std::uint32_t>(std::floor(item.number("itemLevel") / 4 + 1));
  }
  return 0;
}

// The preset, a kept-up effect, or uses/s x duration.
void set_uptime(Reduction& r, const Effect& e, double duration, const std::map<std::string, double>& overrides) {
  const auto o = overrides.find(e.record);
  if (o != overrides.end()) {
    r.uptime = o->second, r.origin = Origin::Preset, r.basis = "preset [uptime]";
  } else if (e.maintained) {
    r.uptime = 1, r.origin = e.origin, r.basis = e.basis;
  } else if (e.per_sec > 0 && duration > 0) {
    r.uptime = std::min(1.0, e.per_sec * duration), r.origin = e.origin;
    r.basis = e.basis + ", " + fmt(duration, 1) + " s";
  } else {
    r.uptime = 0, r.origin = Origin::Default;
    r.basis = e.per_sec > 0 ? "no duration" : e.basis;
  }
}

// Passives and modifiers add to their parent.
void learned_skills(Builder& b) {
  for (const auto& sk : b.s.skills) {
    if (sk.level == 0 || sk.current_level == 0 || sk.devotion_max_level != 0) continue;
    if (!sk.record.starts_with("records/skills/playerclass")) continue;
    const arz::Record* r = b.db.record(sk.record);
    // Subclasses too: blackwater3 (RR) belongs to blackwater1.
    if (!r || classes::is_a(r->cls, "Skill_Passive") || classes::is_a(r->cls, "Skill_Modifier")) continue;
    Effect e;
    e.record = e.skill = sk.record;
    e.source = "learned";
    e.level = sk.current_level;
    for (const std::uint32_t id : sk.modifier_ids)
      for (const auto& m : b.s.skills)
        if (m.id == id && m.current_level > 0) e.modifiers.emplace_back(m.record, m.current_level);
    for (const auto& m : b.s.item_skill_modifiers)
      if (m.skill_id == sk.id && !m.modifier_record.empty()) e.modifiers.emplace_back(m.modifier_record, 1);
    if (!(model::triggered_buff(*r) && b.own_trigger(e, *r, 1, true))) b.cast(e, *r, "cast", true);
    b.add(std::move(e));
  }
}

void devotion_powers(Builder& b) {
  for (const auto& sk : b.s.skills) {
    if (sk.devotion_max_level == 0 || sk.current_level == 0) continue;
    const arz::Record* r = b.db.record(sk.record);
    const std::string_view controller = r ? own_or_buff(*r, "templateAutoCast", b.db) : std::string_view{};
    if (controller.empty()) continue;
    Effect e;
    e.record = e.skill = sk.record;
    e.source = sk.record;
    e.level = sk.current_level;
    b.proc(e, *r, controller, true);
    b.add(std::move(e));
  }
}

void item_and_set_skills(Builder& b) {
  for (const auto& it : b.s.equipped)
    for (const auto& name : model::item_records(it))
      if (const arz::Record* rec = b.db.record(name); rec && !rec->string("itemSkillName").empty()) {
        std::string note;
        const arz::Record* base = b.db.record(it.record);
        const std::uint32_t level = item_skill_level(rec->string("itemSkillLevelEq"), base ? *base : *rec, note);
        b.granted(std::string(rec->string("itemSkillName")), level, note, rec->string("itemSkillAutoController"), name);
      }
  for (const auto& [set, n] : sets::counts(b.s))
    if (const arz::Record* rec = b.db.record(set)) {
      const sets::Bonus bonus = sets::bonus(*rec, n);
      if (!bonus.granted_skill.empty())
        b.granted(bonus.granted_skill, static_cast<std::uint32_t>(bonus.granted_level), "",
                  rec->string("itemSkillAutoController"), set);
    }
}

// Highest-only: each source counts while stronger ones are off.
std::vector<std::pair<double, double>> states(std::vector<std::pair<double, double>> v) {
  std::sort(v.rbegin(), v.rend());
  std::vector<std::pair<double, double>> s;
  double none = 1;
  for (const auto& [value, u] : v) {
    s.emplace_back(value, none * u);
    none *= 1 - u;
  }
  s.emplace_back(0, none);
  return s;
}

}  // namespace

// Game.dll 0x4917E0, cooldown x (1 - CDR).
double Builder::recharge(double cooldown, bool own) const { return own ? cooldown * (1 - cdr) : cooldown; }

std::string Builder::recharge_text(double cooldown, bool own) const {
  return fmt(cooldown, 1) + " s" + (own && cdr > 0 ? " x (1 - " + fmt(cdr * 100, 0) + "%)" : "");
}

// Skill::GetSkillModifierData 0x49AF90: profile plus learned and item modifiers.
double Builder::summed(const Effect& e, const arz::Record& skill, std::string_view key) const {
  double v = key == "skillCooldownTime" ? own_or_buff(skill, key, e.level, db) : at(skill, key, e.level);
  for (const auto& [m, lv] : e.modifiers)
    if (const arz::Record* r = db.record(m)) v += at(*r, key, lv);
  return std::max(0.0, v);
}

// Random trigger interval: recharge + 1 / (chance x rate).
void Builder::trigger(Effect& e, const arz::Record& skill, double chance, std::string_view trigger, bool own) {
  double rate = 0;
  std::string what;
  if (trigger == "AttackEnemy") rate = rates.action, what = "on attack";
  else if (trigger == "AttackEnemyCrit") rate = rates.action * rates.crit, what = "on crit";
  else if (trigger == "HitByEnemy") rate = rates.hits_taken, what = "when hit";
  else if (trigger == "HitByMelee") rate = rates.melee_hits, what = "when hit in melee";
  else if (trigger == "HitByProjectile") rate = rates.projectile_hits, what = "when hit by a projectile";
  else if (trigger == "HitByCrit") rate = rates.crit_hits, what = "when critically hit";
  else if (trigger == "OnKill") rate = rates.kills, what = "on kill";
  else if (trigger == "Block") rate = rates.blocks, what = "on block";
  else if (trigger == "LowHealth") rate = rates.low_health, what = "at low health";
  else if (trigger == "WeaponPool") rate = rates.weapon, what = "of weapon attacks";
  else {
    e.basis = "trigger " + std::string(trigger);
    return;
  }
  if (rate <= 0) {
    e.basis = what + ": rate unknown";
    return;
  }
  const double cooldown = summed(e, skill, "skillCooldownTime");
  e.per_sec = chance > 0 ? 1 / (recharge(cooldown, own) + 1 / (chance * rate)) : 0;
  e.origin = Origin::Derived;
  e.basis = fmt(chance * 100, 0) + "% " + what + " x " + fmt(rate, 2) + "/s" +
            (cooldown > 0 ? ", cooldown " + recharge_text(cooldown, own) : "") + e.basis;
}

void Builder::proc(Effect& e, const arz::Record& skill, std::string_view controller_record, bool own) {
  const arz::Record* c = db.record(controller_record);
  if (!c) {
    e.basis = "controller not in the database";
    return;
  }
  const std::string_view type = c->string("triggerType");
  if (type == "OnEquip") {  // base_atself_onequip.dbr: always up
    e.maintained = true;
    e.origin = Origin::Derived;
    e.basis = "on equip: kept up" + e.basis;
    return;
  }
  if (type == "LowHealth") e.life_below = c->number("triggerParam");
  trigger(e, skill, c->number("chanceToRun") / 100, type, own);
}

// skillChanceWeight: percent of weapon attacks ("x% Chance to be Used").
bool Builder::own_trigger(Effect& e, const arz::Record& skill, double chance, bool own) {
  if (classes::is_a(skill.cls, "Skill_PassiveOnLifeBuffSelf")) {
    e.life_below = at(skill, "lifeMonitorPercent", e.level);
    trigger(e, skill, chance, "LowHealth", own);
  } else if (classes::is_a(skill.cls, "Skill_PassiveOnCritBuffSelf")) {
    // SkillManager::OnCriticalAttack 0x535900: the owner's own crits trigger it.
    trigger(e, skill, at(skill, "onHitActivationChance", e.level) / 100, "AttackEnemyCrit", own);
  } else if (classes::is_a(skill.cls, "Skill_PassiveOnHitBuffSelf")) {  // incl. Skill_PassiveOnHitBuffShield
    trigger(e, skill, at(skill, "onHitActivationChance", e.level) / 100, "HitByEnemy", own);
  } else if (skill.cls.starts_with("Skill_WPAttack_")) {
    trigger(e, skill, at(skill, "skillChanceWeight", e.level) / 100, "WeaponPool", own);
  } else {
    return false;
  }
  return true;
}

// Cast whenever ready; nothing left to wait means kept up.
void Builder::cast(Effect& e, const arz::Record& skill, const char* how, bool own) {
  const double cooldown = summed(e, skill, "skillCooldownTime");
  const double wait = recharge(cooldown, own);
  e.origin = Origin::Derived;
  if (wait > 0) {
    e.per_sec = 1 / wait;
    e.basis = std::string(how) + " on cooldown " + recharge_text(cooldown, own) + e.basis;
  } else if (cooldown > 0) {
    e.maintained = true;
    e.basis = std::string(how) + ", cooldown " + recharge_text(cooldown, own) + ": kept up" + e.basis;
  } else {
    e.maintained = true;
    e.basis = std::string(how) + ", no cooldown: kept up" + e.basis;
  }
}

// An item or set skill.
void Builder::granted(const std::string& skill, std::uint32_t level, std::string note, std::string_view controller,
                      const std::string& source) {
  const arz::Record* r = db.record(skill);
  if (!r) return;
  Effect e;
  e.record = e.skill = skill;
  e.source = source;
  e.level = level;
  e.basis = std::move(note);
  if (level == 0) {
    e.basis = "level equation" + e.basis;
    add(std::move(e));
    return;
  }
  const arz::Record* c = db.record(controller);
  if (own_trigger(e, *r, c ? c->number("chanceToRun") / 100 : 1, false)) {
  } else if (controller.empty()) cast(e, *r, "activated", false);
  else proc(e, *r, controller, false);
  add(std::move(e));
}

void Builder::add(Effect e) {
  const arz::Record* rec = db.record(e.record);
  if (!rec) return;
  const arz::Record* buff = db.record(rec->string("buffSkillName"));
  const arz::Record* deb = buff && debuff(*buff) ? buff : debuff(*rec) ? rec : nullptr;
  if (deb) {
    Reduction r;
    r.record = e.record;
    r.category = 1;
    auto take = [&](const arz::Record& x, std::uint32_t lvl) {
      for (const auto& f : kDebuffFields)
        if (const double v = at(x, f.field, lvl); v < 0) spread(r.amount, f.type, -v, resists);
    };
    take(*deb, e.level);
    for (const auto& [m, lvl] : e.modifiers)
      if (const arz::Record* mr = db.record(m)) take(*mr, lvl);
    if (!r.amount.empty()) {
      set_uptime(r, e, at(*deb, "skillActiveDuration", e.level), overrides);
      out.reductions.push_back(std::move(r));
    }
  }
  for (const auto& f : kOffensiveFields) {
    double v = 0, duration = 0;
    for (const arz::Record* x : {rec, buff}) {
      if (!x) continue;
      v = std::max(v, at(*x, f.min, e.level));
      duration = std::max(duration, at(*x, f.duration, e.level));
    }
    for (const auto& [m, lvl] : e.modifiers)
      if (const arz::Record* mr = db.record(m)) v += at(*mr, f.min, lvl);
    if (v <= 0) continue;
    Reduction r;
    r.record = e.record;
    r.category = f.category;
    spread(r.amount, f.on, v, resists);
    set_uptime(r, e, duration, overrides);
    out.reductions.push_back(std::move(r));
  }
  const bool summons = pet_summon(*rec);
  out.effects.push_back(e);
  if (summons) pets(out.effects.back(), *rec);
}

std::string summoned_record(const arz::Record& skill, std::uint32_t level) {
  if (!pet_summon(skill)) return {};
  const auto it = skill.fields.find("spawnObjects");
  if (it == skill.fields.end() || it->second.strings.empty()) return {};
  const auto& v = it->second.strings;  // past the end: last entry
  return v[std::min<std::size_t>(std::max<std::uint32_t>(level, 1), v.size()) - 1];
}

const arz::Record* summoned(const arz::Record& skill, std::uint32_t level, const arz::Database& db) {
  const std::string r = summoned_record(skill, level);
  return r.empty() ? nullptr : db.record(r);
}

Collected collect(const snap::Snapshot& s, const arz::Database& db, const Rates& rates,
                  const std::map<std::string, double>& overrides, const std::vector<int>& resists) {
  Builder b{s, db, rates, overrides, resists, {}, utility::cooldown_reduction(s)};
  learned_skills(b);
  devotion_powers(b);
  item_and_set_skills(b);
  return std::move(b.out);
}

std::map<int, double> resist_multipliers(const std::map<int, double>& enemy, const std::vector<Reduction>& rr) {
  std::map<int, double> out;
  for (const auto& [type, resist] : enemy) {
    double r1 = resist;
    std::vector<std::pair<double, double>> by[4];
    for (const auto& r : rr) {
      const auto it = r.amount.find(type);
      if (it == r.amount.end() || r.uptime <= 0) continue;
      if (r.category == 1) r1 -= it->second * r.uptime;
      else by[r.category].emplace_back(it->second, r.uptime);
    }
    double m = 0;
    for (const auto& [v2, p2] : states(by[2]))
      for (const auto& [v3, p3] : states(by[3])) {
        const double r2 = r1 > 0 ? r1 * (1 - v2 / 100) : r1;
        m += p2 * p3 * (1 - (r2 - v3) / 100);
      }
    out[type] = m;
  }
  return out;
}

}  // namespace gdc::effects
