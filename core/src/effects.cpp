#include "gdc/effects.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <set>

#include "gdc/sets.h"
#include "gdc/stat_model.h"
#include "gdc/utility.h"

namespace gdc::effects {
namespace {

// Past the end of the per-level array the last value holds.
double at(const arz::Record& r, std::string_view key, std::uint32_t level) {
  const auto it = r.fields.find(key);
  if (it == r.fields.end() || it->second.numbers.empty() || level == 0) return 0;
  const auto& n = it->second.numbers;
  return n[std::min<std::size_t>(level, n.size()) - 1];
}

// Devotion powers keep cooldown and templateAutoCast on the buff record (tier2_32d_skill.dbr).
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
  char buf[48];
  std::snprintf(buf, sizeof buf, "%.*f", decimals, v);
  return buf;
}

// Elemental (58) lands on Cold, Fire, Lightning.
struct Resist {
  std::string_view field;
  int type;
};
constexpr Resist kDebuffFields[] = {
    {"defensivePhysical", 2}, {"defensivePierce", 4}, {"defensiveCold", 5},   {"defensiveFire", 6},
    {"defensivePoison", 7},   {"defensiveLightning", 8}, {"defensiveLife", 9}, {"defensiveChaos", 10},
    {"defensiveAether", 11},  {"defensiveBleeding", 15}, {"defensiveElementalResistance", 58}};
constexpr int kElementalTypes[] = {5, 6, 8};

// `on`: 0 = every resist of the enemy model, 2 = Physical, 58 = Elemental.
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
  else if (type == 58)
    for (int t : kElementalTypes) out[t] += v;
  else
    out[type] += v;
}

// itemSkillLevelEq: a number or "itemLevel/4+1" (303 records in 1.3.1.1), rounded down, using the base item's
// itemLevel. Checked on one item: a03_torso001b (itemLevel 11) "of Balanced Steel" shows Ring of Steel at level 3.
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

struct Builder {
  const snap::Snapshot& s;
  const arz::Database& db;
  const Rates& rates;
  const std::map<std::string, double>& overrides;
  const std::vector<int>& resists;
  Collected out;

  double cdr;

  // Skill::GetCooldownTime 0x4917E0 skips CDR only when Skill+0x2F9 (IsItemSkill 0x30AD10, set by
  // SkillManager::AddItemSkills 0x538BC0 and buff replicas) or Skill+0x2FC (potionModifier) is set. Devotion powers cast
  // via SkillAutoCastController::AppraiseAndExecute 0x523440 -> GetCooldownTime, so they get CDR. Item and set
  // skills (`own` false) keep the DB value.
  double recharge(double cooldown, bool own) const { return own ? cooldown * (1 - cdr) : cooldown; }
  std::string recharge_text(double cooldown, bool own) const {
    return fmt(cooldown, 1) + " s" + (own && cdr > 0 ? " x (1 - " + fmt(cdr * 100, 0) + "%)" : "");
  }

  // Expected time between casts = recharge + 1 / (chance x rate), triggers assumed random.
  void trigger(Effect& e, const arz::Record& skill, double chance, std::string_view trigger, bool own) {
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
    const double cooldown = own_or_buff(skill, "skillCooldownTime", e.level, db);
    e.per_sec = chance > 0 ? 1 / (recharge(cooldown, own) + 1 / (chance * rate)) : 0;
    e.origin = Origin::Derived;
    e.basis = fmt(chance * 100, 0) + "% " + what + " x " + fmt(rate, 2) + "/s" +
              (cooldown > 0 ? ", cooldown " + recharge_text(cooldown, own) : "") + e.basis;
  }

  void proc(Effect& e, const arz::Record& skill, std::string_view controller_record, bool own) {
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

  // skillChanceWeight is read as a percent of weapon attacks ("x% Chance to be Used").
  bool own_trigger(Effect& e, const arz::Record& skill, double chance, bool own) {
    if (skill.cls == "Skill_PassiveOnLifeBuffSelf") {
      e.life_below = at(skill, "lifeMonitorPercent", e.level);
      trigger(e, skill, chance, "LowHealth", own);
    } else if (skill.cls == "Skill_PassiveOnHitBuffSelf" || skill.cls == "Skill_PassiveOnHitBuffShield") {
      trigger(e, skill, at(skill, "onHitActivationChance", e.level) / 100, "HitByEnemy", own);
    } else if (skill.cls.starts_with("Skill_WPAttack_")) {
      trigger(e, skill, at(skill, "skillChanceWeight", e.level) / 100, "WeaponPool", own);
    } else {
      return false;
    }
    return true;
  }

  // Design choice: cast whenever ready; kept up with no cooldown or when CDR takes it all.
  void cast(Effect& e, const arz::Record& skill, const char* how, bool own) {
    const double cooldown = own_or_buff(skill, "skillCooldownTime", e.level, db);
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

  // `summon` by value: add() grows out.effects.
  void pets(const Effect summon, const arz::Record& skill) {
    const arz::Record* pet = summoned(skill, summon.level, db);
    if (!pet) return;
    const double limit = at(skill, "petLimit", summon.level), ttl = at(skill, "spawnObjectsTimeToLive", summon.level);
    double alive = 0;
    std::string how;
    if (summon.maintained) alive = limit, how = "kept at " + fmt(limit, 0);
    else if (summon.per_sec > 0 && ttl > 0) {
      alive = std::min(limit > 0 ? limit : 1e9, summon.per_sec * ttl);
      how = fmt(summon.per_sec, 2) + " summons/s x " + fmt(ttl, 0) + " s, limit " + fmt(limit, 0);
    }
    // Preset only: min/maxSwingPause is the pause between attacks and the animation length is not in the DB.
    const double rate = rates.pet_attacks;
    const Origin origin = Origin::Preset;
    const std::string rate_how = "preset pet_attack_rate";
    std::vector<std::pair<std::string, std::uint32_t>> skills;
    if (const auto a = pet->string("attackSkillName"); !a.empty()) skills.emplace_back(std::string(a), 1);
    for (int i = 1; i <= 17; ++i) {
      const auto name = pet->string("skillName" + std::to_string(i));
      const auto lvl = static_cast<std::uint32_t>(pet->number("skillLevel" + std::to_string(i)));
      if (!name.empty() && lvl > 0 && std::none_of(skills.begin(), skills.end(), [&](const auto& k) { return k.first == name; }))
        skills.emplace_back(std::string(name), lvl);
    }
    for (const auto& [r, lvl] : skills) {
      Effect e;
      e.record = e.skill = r;
      e.source = summon.skill;
      e.level = lvl;
      e.pet = true;
      e.player_scaled = pet->cls == "PetPlayerScaling";
      e.pets = alive;
      // Design choice: once per cooldown per pet.
      const arz::Record* pr = &r == &skills.front().first ? nullptr : db.record(r);
      const double cooldown = pr ? at(*pr, "skillCooldownTime", lvl) : 0;
      if (alive > 0 && cooldown > 0) {
        e.per_sec = alive / cooldown;
        e.origin = Origin::Derived;
        e.basis = fmt(alive, 1) + " pets (" + how + "), cooldown " + fmt(cooldown, 1) + " s";
      } else if (alive > 0 && rate > 0) {
        e.per_sec = alive * rate;
        e.origin = origin;
        e.basis = fmt(alive, 1) + " pets (" + how + ") x " + fmt(rate, 2) + "/s (" + rate_how + ")";
      } else {
        e.basis = alive > 0 ? "pet_attack_rate not set" : "summon rate unknown (" + summon.basis + ")";
      }
      add(std::move(e));
    }
  }

  void add(Effect e) {
    const arz::Record* rec = db.record(e.record);
    if (!rec) return;
    const auto o = overrides.find(e.record);
    const arz::Record* buff = db.record(rec->string("buffSkillName"));
    const arz::Record* deb = buff && debuff(*buff) ? buff : debuff(*rec) ? rec : nullptr;
    auto uptime = [&](Reduction& r, double duration) {
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
    };
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
        uptime(r, at(*deb, "skillActiveDuration", e.level));
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
      uptime(r, duration);
      out.reductions.push_back(std::move(r));
    }
    const bool summons = pet_summon(*rec);
    out.effects.push_back(e);
    if (summons) pets(out.effects.back(), *rec);
  }
};

}  // namespace

const arz::Record* summoned(const arz::Record& skill, std::uint32_t level, const arz::Database& db) {
  if (!pet_summon(skill)) return nullptr;
  const auto it = skill.fields.find("spawnObjects");
  if (it == skill.fields.end() || it->second.strings.empty()) return nullptr;
  const auto& v = it->second.strings;  // past the end: last entry
  return db.record(v[std::min<std::size_t>(std::max<std::uint32_t>(level, 1), v.size()) - 1]);
}

Collected collect(const snap::Snapshot& s, const arz::Database& db, const Rates& rates,
                  const std::map<std::string, double>& overrides, const std::vector<int>& resists) {
  Builder b{s, db, rates, overrides, resists, {}, utility::cooldown_reduction(s)};
  // Passives and modifiers add to their parent.
  for (const auto& sk : s.skills) {
    if (sk.level == 0 || sk.current_level == 0 || sk.devotion_max_level != 0) continue;
    if (!sk.record.starts_with("records/skills/playerclass")) continue;
    const arz::Record* r = db.record(sk.record);
    if (!r || r->cls == "Skill_Passive" || r->cls == "Skill_Modifier" || r->cls == "Skill_Transmuter" ||
        r->cls == "Skill_Mastery")
      continue;
    Effect e;
    e.record = e.skill = sk.record;
    e.source = "learned";
    e.level = sk.current_level;
    for (const std::uint32_t id : sk.modifier_ids)
      for (const auto& m : s.skills)
        if (m.id == id && m.current_level > 0) e.modifiers.emplace_back(m.record, m.current_level);
    for (const auto& m : s.item_skill_modifiers)
      if (m.skill_id == sk.id && !m.modifier_record.empty()) e.modifiers.emplace_back(m.modifier_record, 1);
    if (!(model::triggered_buff(*r) && b.own_trigger(e, *r, 1, true))) b.cast(e, *r, "cast", true);
    b.add(std::move(e));
  }
  for (const auto& sk : s.skills) {
    if (sk.devotion_max_level == 0 || sk.current_level == 0) continue;
    const arz::Record* r = db.record(sk.record);
    const std::string_view controller = r ? own_or_buff(*r, "templateAutoCast", db) : std::string_view{};
    if (controller.empty()) continue;
    Effect e;
    e.record = e.skill = sk.record;
    e.source = sk.record;
    e.level = sk.current_level;
    b.proc(e, *r, controller, true);
    b.add(std::move(e));
  }
  auto granted = [&](const std::string& skill, std::uint32_t level, std::string note, std::string_view controller,
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
      b.add(std::move(e));
      return;
    }
    const arz::Record* c = db.record(controller);
    if (b.own_trigger(e, *r, c ? c->number("chanceToRun") / 100 : 1, false)) {
    } else if (controller.empty()) b.cast(e, *r, "activated", false);
    else b.proc(e, *r, controller, false);
    b.add(std::move(e));
  };
  for (const auto& it : s.equipped)
    for (const auto& name : model::item_records(it))
      if (const arz::Record* rec = db.record(name); rec && !rec->string("itemSkillName").empty()) {
        std::string note;
        const arz::Record* base = db.record(it.record);
        const std::uint32_t level = item_skill_level(rec->string("itemSkillLevelEq"), base ? *base : *rec, note);
        granted(std::string(rec->string("itemSkillName")), level, note, rec->string("itemSkillAutoController"), name);
      }
  for (const auto& [set, n] : sets::counts(s))
    if (const arz::Record* rec = db.record(set)) {
      const sets::Bonus bonus = sets::bonus(*rec, n);
      if (!bonus.granted_skill.empty())
        granted(bonus.granted_skill, static_cast<std::uint32_t>(bonus.granted_level), "",
                rec->string("itemSkillAutoController"), set);
    }
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
    auto states = [](std::vector<std::pair<double, double>> v) {
      std::sort(v.rbegin(), v.rend());
      std::vector<std::pair<double, double>> s;
      double none = 1;
      for (const auto& [value, u] : v) {
        s.emplace_back(value, none * u);
        none *= 1 - u;
      }
      s.emplace_back(0, none);
      return s;
    };
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
