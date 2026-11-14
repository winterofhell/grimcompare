#include "gdc/stat_model.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <map>
#include <optional>
#include <set>
#include <tuple>

#include "gdc/attribute_names.h"
#include "gdc/combat_formulas.h"
#include "gdc/db_fields.h"
#include "gdc/sets.h"

namespace gdc::model {
namespace {

constexpr int kPhysique = 1, kCunning = 2, kSpirit = 3, kLife = 4, kEnergy = 5, kRunSpeed = 14;
constexpr int kArmor = 38, kAbsorption = 39, kProtection = 40;  // re/CombatAttributeType.tsv

// records/creatures/pc/playerlevels.dbr: lifeIncrement 20, lifeIncrementDexterity 8,
// lifeIncrementIntelligence 12, manaIncrement 16 per attribute point of strength/dexterity/
// intelligenceIncrement 8. Applied to attribute totals (item and % bonuses included): verified for
// Spirit by the poison swap fixture (+12.1 Spirit -> +18.15 Life, +24.2 Energy before Life %).
constexpr double kLifePerPhysique = 20.0 / 8, kLifePerCunning = 8.0 / 8, kLifePerSpirit = 12.0 / 8;
constexpr double kEnergyPerSpirit = 16.0 / 8;

// Types with a DefenseAttributeDefenseCap_* class (re/CombatAttributeType.tsv): their defense is capped
// at defense_cap + cap_bonus by CombatAttributeAbsDefense::Process.
bool cappable(int t) {
  static const std::set<int> types = {2, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 20, 21, 42, 43, 44, 45, 46};
  return types.contains(t);
}

// combatformulas.dbr combatRegion*Chance (%), by EquipLocations; waist (6) adds its armor whole (fit).
double region_weight(int location) {
  switch (location) {
    case 7: return 0.15;   // Head: combatRegionHeadChance
    case 9: return 0.15;   // Shoulders: combatRegionShouldersChance
    case 8: return 0.26;   // Chest: combatRegionTorsoChance
    case 10: return 0.12;  // Hands: combatRegionArmsChance
    case 11: return 0.20;  // Legs: combatRegionLegsChance
    case 12: return 0.12;  // Feet: combatRegionFeetChance
    case 6: return 1.0;    // Waist
    default: return 0.0;
  }
}

double uncapped(const snap::CombatTotals& r) {
  return r.defense + std::abs(r.defense) * r.defense_modifier / 100;
}

bool has_processed(const snap::Snapshot& s) {
  return std::any_of(s.defense.begin(), s.defense.end(), [](const auto& r) { return r.processed != 0; });
}

template <class Row>
Row& row(std::vector<Row>& v, int type) {
  for (Row& r : v)
    if (r.type == type) return r;
  Row r;
  r.type = type;
  const auto at = std::lower_bound(v.begin(), v.end(), type, [](const Row& x, int t) { return x.type < t; });
  return *v.insert(at, r);
}

const snap::CombatTotals* find(const std::vector<snap::CombatTotals>& v, int type) {
  for (const auto& r : v)
    if (r.type == type) return &r;
  return nullptr;
}

double item_armor(const snap::Item& it) {
  if (!it.stats) return 0;
  const auto* r = find(it.stats->defense, kArmor);
  return r ? r->defense : 0;
}

snap::CombatTotals* find_mut(std::vector<snap::CombatTotals>& v, int type) {
  for (auto& r : v)
    if (r.type == type) return &r;
  return nullptr;
}


const snap::Item* at(const snap::Snapshot& s, int location) {
  for (const auto& it : s.equipped)
    if (it.location == location) return &it;
  return nullptr;
}


// What item stats do not show: skill / mastery grants and max-resist bonuses, summed over the item's
// records (base, affixes, components, completion bonus).
struct Hidden {
  std::map<std::string, double> skills;      // augmentSkillName* record -> levels
  std::map<std::string, double> masteries;   // augmentMasteryName* record -> levels
  double all = 0;                            // augmentAllLevel
  std::map<std::string, double> max_resist;  // defensive*MaxResist field -> value
  // modifiedSkillName<n> -> modifierSkillName<n>: item skill modifiers (b013a_sword: two on possession1)
  std::multiset<std::pair<std::string, std::string>> modifiers;
  // The same from set bonuses at the current piece count (sets::Bonus); they are not tied to the slot.
  std::multiset<std::pair<std::string, std::string>> set_modifiers;
};

// Adds a set bonus's skill parts and max resists (sets.h).
void merge(Hidden& h, const sets::Bonus& b) {
  for (const auto& [k, v] : b.skills) h.skills[k] += v;
  for (const auto& [k, v] : b.masteries) h.masteries[k] += v;
  h.all += b.all;
  h.set_modifiers.insert(b.modifiers.begin(), b.modifiers.end());
  for (const auto& [k, v] : b.max_resist) h.max_resist[k] += v;
}

// The item's records: base, affixes, components, completion bonus.

Hidden hidden(const snap::Item* it, const arz::Database& db) {
  Hidden h;
  if (!it) return h;
  for (const auto& name : item_records(*it)) {
    const auto rec = db.record(name);
    if (!rec) continue;
    for (const auto& [key, field] : rec->fields) {
      auto grant = [&](std::map<std::string, double>& out, std::string_view name_prefix, std::string_view level_prefix) {
        if (!key.starts_with(name_prefix) || field.strings.empty() || field.strings[0].empty()) return;
        const std::string level_key = std::string(level_prefix) + key.substr(name_prefix.size());
        out[field.strings[0]] += rec->number(level_key);
      };
      grant(h.skills, "augmentSkillName", "augmentSkillLevel");
      grant(h.masteries, "augmentMasteryName", "augmentMasteryLevel");
      if (key == "augmentAllLevel") h.all += rec->number(key);
      if (key.starts_with("modifierSkillName") && !field.strings.empty() && !field.strings[0].empty()) {
        const std::string_view target = rec->string("modifiedSkillName" + key.substr(17));
        if (!target.empty()) h.modifiers.emplace(std::string(target), field.strings[0]);
      }
      if (key.ends_with("MaxResist") && rec->number(key) != 0) h.max_resist[key] += rec->number(key);
    }
  }
  return h;
}

double get(const std::map<std::string, double>& m, const std::string& k) {
  const auto it = m.find(k);
  return it != m.end() ? it->second : 0;
}

// Augmented levels the swap adds to skill `sk` (negative: removes). Targets as the engine applies
// grants: SetSkillLevelAugment (0x5375B0) by record; SetMasteryAugment (0x5373D0) to the mastery skill
// itself and every skill whose GetMasteryId (Skill+0x13C) is its id; SetAllSkillAugment (0x537230) to
// every skill but one the caller names (not traced; see the warning in swap()).
double grant_delta(const snap::Skill& sk, const snap::Snapshot& s, const Hidden& before, const Hidden& after) {
  double d = get(after.skills, sk.record) - get(before.skills, sk.record) + after.all - before.all;
  std::set<std::string> masteries;
  for (const auto& [m, _] : before.masteries) masteries.insert(m);
  for (const auto& [m, _] : after.masteries) masteries.insert(m);
  for (const auto& m : masteries) {
    bool hit = sk.record == m;
    for (const auto& other : s.skills)
      hit = hit || (other.record == m && other.id != 0 && sk.mastery_id == other.id);
    if (hit) d += get(after.masteries, m) - get(before.masteries, m);
  }
  return d;
}

// Value of a per-level field at `level` (1-based); arrays are indexed by level - 1, a single value holds
// for every level. nullopt past the end of an array.
std::optional<double> at_level(const arz::Field& f, std::uint32_t level) {
  if (level == 0 || f.numbers.empty()) return 0.0;
  if (f.numbers.size() == 1) return f.numbers[0];
  if (level > f.numbers.size()) return std::nullopt;
  return f.numbers[level - 1];
}

// Contributions of one skill record at a level, keyed by (kind, type, modifier).
using Contribution = std::map<std::tuple<fields::Kind, int, bool>, double>;

struct SkillEffect {
  Contribution delta;    // after - before
  Contribution at_from;  // the record's mapped stats at the old level
  std::string problem;   // non-empty: not modeled, why
};

// What an item skill modifier (Skill_Modifier record, skillMaxLevel 1) adds to its skill. The engine adds
// it through the skill's own collector: possession1's captured passive has AttackSpeed modifier 16 = two
// sword1h_b013_possession.dbr (characterAttackSpeedModifier 8, one per dual-wielded b013 sword), its own
// record has none (auto-snapshot store 2026-11-14, record 20261114-123319-910-0001).
SkillEffect modifier_effect(const std::string& record, const arz::Database& db) {
  SkillEffect e;
  const auto rec = db.record(record);
  if (!rec) {
    e.problem = "item skill modifier " + record + " not in the database";
    return e;
  }
  for (const auto& [key, field] : rec->fields) {
    if (field.kind != arz::Field::Kind::Int && field.kind != arz::Field::Kind::Float) continue;
    const bool any = std::any_of(field.numbers.begin(), field.numbers.end(), [](double v) { return v != 0; });
    if (!any) continue;
    double scale = 1;
    const fields::Target* t = fields::lookup(key, scale);
    if (!t) {
      if (key.starts_with("defensive") || key.starts_with("character"))
        e.problem = "item skill modifier field " + key + " is not modeled";
      continue;
    }
    if (field.numbers.size() > 1) {
      e.problem = "item skill modifier field " + key + " varies by level";
      continue;
    }
    e.delta[{t->kind, t->type, t->modifier}] += field.numbers[0] * scale;
  }
  return e;
}

// A skill record's mapped stats at `from` and their change to `to`.
SkillEffect level_effect(const arz::Record& rec, std::uint32_t from, std::uint32_t to) {
  SkillEffect e;
  for (const auto& [key, field] : rec.fields) {
    if (field.kind != arz::Field::Kind::Int && field.kind != arz::Field::Kind::Float) continue;
    const auto a = at_level(field, from), b = at_level(field, to);
    double scale = 1;
    const fields::Target* t = fields::lookup(key, scale);
    if (!t) {
      // Compared stats come only from defensive* / character* fields; others (offensive*, pet, ...) don't
      // reach the sheet values the model predicts.
      if ((key.starts_with("defensive") || key.starts_with("character")) && a != b)
        e.problem = key + " is not modeled";
      continue;
    }
    if (!a || !b) {
      e.problem = key + " has no value past level " + std::to_string(field.numbers.size());
      continue;
    }
    e.at_from[{t->kind, t->type, t->modifier}] += *a * scale;
    if (*b != *a) e.delta[{t->kind, t->type, t->modifier}] += (*b - *a) * scale;
  }
  return e;
}

// The engine's own numbers for this skill (schema 4) must equal its record at the current level plus what
// is attached to it (`attached`: learned modifiers, item skill modifiers), or something else (a conditional
// part) is in play and the record delta can't be trusted. Sets e.problem.
void engine_check(const snap::Snapshot& s, const snap::Skill& sk, std::uint32_t from, const Contribution& attached,
                  SkillEffect& e) {
  if (e.problem.empty() && sk.passive) {
    Contribution at_from = e.at_from;
    Contribution engine;
    for (const auto& c : sk.passive->char_attributes) {
      if (c.value != 0) engine[{fields::Kind::Char, c.type, false}] += c.value;
      if (c.modifier != 0) engine[{fields::Kind::Char, c.type, true}] += c.modifier;
    }
    for (const auto& c : sk.passive->defense) {
      if (c.defense != 0) engine[{fields::Kind::Defense, c.type, false}] += c.defense;
      if (c.defense_modifier != 0) engine[{fields::Kind::Defense, c.type, true}] += c.defense_modifier;
    }
    for (const auto& [k, v] : attached) at_from[k] += v;
    // The capture holds char / defense collectors only (snap::Skill::passive).
    std::erase_if(at_from, [](const auto& kv) { return kv.second == 0 || std::get<0>(kv.first) == fields::Kind::Damage; });
    const bool same = engine.size() == at_from.size() &&
                      std::all_of(engine.begin(), engine.end(), [&](const auto& kv) {
                        const auto it = at_from.find(kv.first);
                        return it != at_from.end() && std::abs(it->second - kv.second) <= 0.01;
                      });
    if (!same)
      e.problem = "engine values differ from the record at level " + std::to_string(from) +
                  (s.schema < 5 ? " (learned modifiers are captured from schema 5)" : "");
  }
}

// The skill whose collector adds modifier skill `id` (snap::Skill::modifier_ids, schema 5), or null.
const snap::Skill* modifier_parent(const std::vector<snap::Skill>& skills, std::uint32_t id) {
  for (const auto& k : skills)
    if (std::find(k.modifier_ids.begin(), k.modifier_ids.end(), id) != k.modifier_ids.end()) return &k;
  return nullptr;
}

bool contributes(const snap::Skill& sk, const arz::Record& rec, bool& assumed) {
  if (sk.passive) return !sk.passive->char_attributes.empty() || !sk.passive->defense.empty();
  const bool on = rec.cls == "Skill_Passive" || rec.cls == "Skill_BuffSelfToggled";
  assumed = assumed || on;
  return on;
}

void add_to(snap::ItemStats& passive, const Contribution& delta) {
  for (const auto& [k, v] : delta) {
    if (v == 0) continue;
    const auto [kind, type, modifier] = k;
    if (kind == fields::Kind::Damage) continue;  // not part of the captured passive
    if (kind == fields::Kind::Char) {
      auto& c = row(passive.char_attributes, type);
      (modifier ? c.modifier : c.value) += static_cast<float>(v);
    } else {
      auto& c = row(passive.defense, type);
      (modifier ? c.defense_modifier : c.defense) += static_cast<float>(v);
    }
  }
}

using Deltas = std::map<int, std::pair<double, double>>;  // type -> (value delta, modifier delta)

// Char / defense: (value, modifier). Damage: (direct %, duration %).
struct AllDeltas {
  Deltas chr, def, dmg;
  void add(const Contribution& c, bool damage_only = false) {
    for (const auto& [k, v] : c) {
      const auto [kind, type, modifier] = k;
      if (damage_only && kind != fields::Kind::Damage) continue;
      if (kind == fields::Kind::Damage && (type == 58 || type == 60)) {
        // Skill fields: spread like the engine's own rows (items arrive spread already, see swap()).
        for (int t : fields::spread(type, modifier)) (modifier ? dmg[t].second : dmg[t].first) += v;
        if (type == 60 && !modifier) dmg[60].first += v;  // b002e_torso: row 60 direct only
        continue;
      }
      auto& d = kind == fields::Kind::Char ? chr[type] : kind == fields::Kind::Defense ? def[type] : dmg[type];
      (modifier ? d.second : d.first) += v;
    }
  }
};

bool has_damage(const Contribution& c) {
  return std::any_of(c.begin(), c.end(), [](const auto& kv) { return std::get<0>(kv.first) == fields::Kind::Damage && kv.second != 0; });
}

// Whether any damage the skill deals or adds differs between two levels: flat damage, damage %, weapon
// damage %, in the record or its buffSkillName record (where Skill_AttackBuffRadius-style skills keep them).
bool damage_changes(const arz::Record& r, std::uint32_t from, std::uint32_t to, const arz::Database& db) {
  auto differs = [&](const arz::Record& x, std::string_view key) {
    if (key.empty()) return false;
    const auto it = x.fields.find(key);
    return it != x.fields.end() && at_level(it->second, from) != at_level(it->second, to);
  };
  for (const arz::Record* x : {&r, db.record(r.string("buffSkillName"))}) {
    if (!x) continue;
    if (differs(*x, "weaponDamagePct")) return true;
    for (const auto& f : fields::flat_damage())
      if (differs(*x, f.min) || differs(*x, f.max) || differs(*x, f.duration) || differs(*x, f.chance)) return true;
    for (const auto& [key, target] : fields::damage_modifiers())
      if (differs(*x, key)) return true;
  }
  return false;
}

double value_at(const arz::Record& r, std::string_view key, std::uint32_t level) {
  const auto it = r.fields.find(std::string(key));
  if (it == r.fields.end()) return 0;
  return at_level(it->second, level).value_or(it->second.numbers.empty() ? 0 : it->second.numbers.back());
}

// The record holding a skill's stats and timing: its buffSkillName record when it has one (bloodofdreeg1 ->
// bloodofdreeg1_buff.dbr, SkillBuff_Passive), else the skill's own.
const arz::Record& stats_record(const arz::Record& rec, const arz::Database& db) {
  const std::string buff(rec.string("buffSkillName"));
  const arz::Record* b = buff.empty() ? nullptr : db.record(buff);
  return b ? *b : rec;
}

// Uptime of a timed buff the snapshot caught between casts: skillActiveDuration / skillCooldownTime at
// `level` (bloodofdreeg1_buff.dbr: 60 s / 12 s -> 1), capped at 1; no cooldown: 1. 0 when it has no
// duration (a toggle or aura that is off) or is not a self buff: SkillBuff_Passive (the buffSkillName record
// of Skill_BuffRadius / SkillSecondary_BuffSelfDuration / Skill_BuffOther in 1.3.1.1) or Skill_BuffSelfDuration
// (overguard1). Debuffs (SkillBuff_Debuf*: gdc::effects), attacks and triggered buffs
// (Skill_PassiveOn*BuffSelf) stay unapplied.
// Design choice: recast whenever the cooldown allows (docs/MECHANICS.md §4).
// shortcut: preset [uptime] does not reach the stat model — revisit when a buff needs an override.
double buff_uptime(const arz::Record& stats, std::uint32_t level) {
  if (stats.cls != "SkillBuff_Passive" && stats.cls != "Skill_BuffSelfDuration") return 0;
  const double duration = value_at(stats, "skillActiveDuration", level);
  if (duration <= 0) return 0;
  const double cooldown = value_at(stats, "skillCooldownTime", level);
  return cooldown > 0 ? std::min(1.0, duration / cooldown) : 1.0;
}

Contribution scaled(const Contribution& c, double f) {
  Contribution out;
  for (const auto& [k, v] : c) out[k] = v * f;
  return out;
}

// Item-granted skill levels and item skill modifiers. GetCurrentLevel (0x4909C0) = level ? min(level +
// augmented, cap) : 0 outside devotions, cap = SkillProfile+0x150 = max(skillUltimateLevel, skillMaxLevel)
// (SkillProfile::LoadProfile 0x545A27). A skill counts only while it contributes (Skill::passive non-empty);
// its stats at the new level come from its record (gdc::fields), a removed / added modifier's from the
// modifier record. Without passive data (schema < 4) passives and toggles are assumed on.
void apply_skill_levels(const snap::Snapshot& s, int location, const Hidden& before, const Hidden& after,
                        const arz::Database& db, bool expected, AllDeltas& out_d, Prediction& p) {
  if (before.all != after.all)
    p.warnings.push_back("all-skills grant changes: applied to every skill (the engine's one exclusion is not traced)");
  auto of = [](const std::multiset<std::pair<std::string, std::string>>& m, const std::string& skill) {
    std::multiset<std::string> out;
    for (auto it = m.lower_bound({skill, std::string{}}); it != m.end() && it->first == skill; ++it) out.insert(it->second);
    return out;
  };
  auto mods_of = [&](const Hidden& h, const std::string& skill) {
    std::multiset<std::string> out = of(h.modifiers, skill);
    for (auto& r : of(h.set_modifiers, skill)) out.insert(r);
    return out;
  };
  bool assumed = false;
  for (const auto& sk : s.skills) {
    if (sk.devotion_max_level != 0) continue;
    const double d = grant_delta(sk, s, before, after);
    if (sk.level == 0) {  // unlearned: GetCurrentLevel stays 0 whatever the bonus
      if (d != 0) p.unused.push_back({sk.record, sk.augmented_level, static_cast<std::uint32_t>(std::max(0.0, sk.augmented_level + d))});
      continue;
    }
    const auto removed = mods_of(before, sk.record), added = mods_of(after, sk.record);
    if (d == 0 && removed == added) continue;
    const auto rec = db.record(sk.record);
    const std::string name = sk.record.substr(sk.record.rfind('/') + 1);
    if (!rec) {
      p.warnings.push_back("skill " + name + ": record not in the database");
      continue;
    }
    // The mastery bar takes no item bonus: with +1 Occultist added (d016_relic, store 20261114-172933-952
    // records 1 -> 2) every Occultist skill's augmented level rose by 1, _classtraining_class03.dbr's stayed 0
    // (level 50; its record has no skillMaxLevel / skillUltimateLevel).
    if (rec->cls == "Skill_Mastery") continue;
    // Skill_BuffRadius records (bloodofdreeg1) keep the levels in their buffSkillName record.
    // shortcut: assumed to be what GetCurrentLevel's profile (vt+0x578) reads — revisit when a buff skill at its cap disagrees.
    auto cap_of = [](const arz::Record& r) { return std::max(r.number("skillUltimateLevel"), r.number("skillMaxLevel")); };
    double cap = cap_of(*rec);
    if (cap == 0 && !rec->string("buffSkillName").empty())
      if (const auto buff = db.record(rec->string("buffSkillName"))) cap = cap_of(*buff);
    if (cap == 0) {
      p.warnings.push_back("skill " + name + ": no level cap in its record");
      continue;
    }
    const double aug = std::max(0.0, sk.augmented_level + d);
    const auto to = static_cast<std::uint32_t>(std::min(sk.level + aug, cap));
    const std::uint32_t from = sk.current_level;
    if (from != std::min<double>(sk.level + sk.augmented_level, cap)) {
      p.warnings.push_back("skill " + name + ": current level " + std::to_string(from) + " does not follow from level + bonus");
      continue;
    }
    if (to == from && removed == added) continue;
    snap::Skill& out = *std::find_if(p.after.skills.begin(), p.after.skills.end(),
                                     [&](const snap::Skill& k) { return k.id == sk.id && k.record == sk.record; });
    if (to != from) {
      p.skills.push_back({sk.record, from, to});
      out.augmented_level = static_cast<std::uint32_t>(aug);
      out.current_level = to;
    }
    // What the engine adds to the skill now: item skill modifiers and learned modifier skills.
    Contribution attached;
    std::string problem;
    auto add_mod = [&](const std::string& record, double sign, Contribution& into) {
      const SkillEffect m = modifier_effect(record, db);
      if (!m.problem.empty()) problem = m.problem;
      for (const auto& [k, v] : m.delta) into[k] += sign * v;
    };
    for (const auto& m : s.item_skill_modifiers)
      if (m.skill_id == sk.id && !m.modifier_record.empty()) add_mod(m.modifier_record, +1, attached);
    for (const std::uint32_t id : sk.modifier_ids) {
      const auto ms = std::find_if(s.skills.begin(), s.skills.end(), [&](const snap::Skill& k) { return k.id == id; });
      const auto mrec = ms != s.skills.end() ? db.record(ms->record) : nullptr;
      if (!mrec) {
        problem = "modifier skill " + std::to_string(id) + " not in the snapshot or the database";
        continue;
      }
      const SkillEffect m = level_effect(*mrec, ms->current_level, ms->current_level);
      if (!m.problem.empty()) problem = m.problem;
      // AddModifierDefenseAttributes (0x494970) adds only objects that are-a Skill_Modifier; whether
      // Skill_Transmuter is one is not traced, so a transmuter with stats is not modeled.
      const bool any = std::any_of(m.at_from.begin(), m.at_from.end(), [](const auto& kv) { return kv.second != 0; });
      if (mrec->cls != "Skill_Modifier" && any) problem = "linked " + mrec->cls + " " + ms->record + " has stats";
      for (const auto& [k, v] : m.at_from) attached[k] += v;
    }
    auto& list = p.after.item_skill_modifiers;
    auto push = [&](const std::string& r, int loc) {
      snap::ItemSkillModifier m;
      m.skill_id = sk.id;
      m.skill_record = sk.record;
      m.modifier_record = r;
      m.location = loc;
      list.push_back(std::move(m));
    };
    if (const auto item_added = of(after.modifiers, sk.record); of(before.modifiers, sk.record) != item_added) {
      std::erase_if(list, [&](const snap::ItemSkillModifier& m) { return m.skill_id == sk.id && m.location == location; });
      for (const auto& r : item_added) push(r, location);
    }
    // Set modifiers: the slot the engine records for them is not known, so they are matched by record.
    const auto set_before = of(before.set_modifiers, sk.record), set_after = of(after.set_modifiers, sk.record);
    for (const auto& r : std::set<std::string>(set_before.begin(), set_before.end()))
      for (auto n = set_before.count(r); n > set_after.count(r); --n) {
        const auto at = std::find_if(list.begin(), list.end(), [&](const snap::ItemSkillModifier& m) {
          return m.skill_id == sk.id && m.modifier_record == r && m.location != location;
        });
        if (at != list.end()) list.erase(at);
      }
    for (const auto& r : std::set<std::string>(set_after.begin(), set_after.end()))
      for (auto n = set_after.count(r); n > set_before.count(r); --n) push(r, -1);
    if (rec->cls == "Skill_Modifier") {  // added through its parent's collector (Skill::AddModifierDefenseAttributes)
      if (to == from) continue;
      const snap::Skill* parent = modifier_parent(s.skills, sk.id);
      const auto prec = parent ? db.record(parent->record) : nullptr;
      if (!prec) {
        p.warnings.push_back("skill " + name + ": modifier level change not modeled (" +
                             (s.schema < 5 ? "its parent skill is captured from schema 5)" : "no parent skill)"));
        continue;
      }
      const SkillEffect e = level_effect(*rec, from, to);
      if (!contributes(*parent, *prec, assumed)) {
        // A modifier of a timed buff (bloodofdreeg2 of bloodofdreeg1): its change x the buff's uptime.
        const double up = expected ? buff_uptime(stats_record(*prec, db), parent->current_level) : 0;
        if (up > 0 && e.problem.empty()) {
          out_d.add(scaled(e.delta, up));
          p.inactive.push_back({sk.record, parent->record, from, to, up});
        } else if (damage_changes(*rec, from, to, db)) {
          p.inactive.push_back({sk.record, parent->record, from, to});
        }
        continue;
      }
      if (!e.problem.empty()) {
        p.warnings.push_back("skill " + name + ": " + e.problem);
        continue;
      }
      out_d.add(e.delta);
      snap::Skill& pout = *std::find_if(p.after.skills.begin(), p.after.skills.end(),
                                        [&](const snap::Skill& k) { return k.id == parent->id && k.record == parent->record; });
      if (pout.passive) add_to(*pout.passive, e.delta);
      continue;
    }
    SkillEffect e = level_effect(*rec, from, to);
    if (!contributes(sk, *rec, assumed)) {
      const arz::Record& srec = stats_record(*rec, db);
      const double up = expected ? buff_uptime(srec, to) : 0;
      if (&srec != rec) e = level_effect(srec, from, to);
      // The capture sees char / defense collectors only: a Skill_Passive with only damage bonuses is on.
      if (has_damage(e.delta) && e.problem.empty() && rec->cls == "Skill_Passive" && removed == added) {
        out_d.add(e.delta, true);
      } else if (up > 0 && e.problem.empty() && removed == added) {
        // A timed buff between casts: its change at uptime. shortcut: `before` lacks the buff itself, so the
        // ratio is a little high — revisit when snapshots carry the buff's stats while it is down.
        out_d.add(scaled(e.delta, up));
        p.inactive.push_back({sk.record, {}, from, to, up});
      } else if (damage_changes(*rec, from, to, db)) {
        p.inactive.push_back({sk.record, {}, from, to});
      }
      continue;
    }
    engine_check(s, sk, from, attached, e);
    for (const auto& r : removed) add_mod(r, -1, e.delta);
    for (const auto& r : added) add_mod(r, +1, e.delta);
    if (e.problem.empty()) e.problem = problem;
    if (!e.problem.empty()) {
      p.warnings.push_back("skill " + name + ": " + e.problem);
      continue;
    }
    out_d.add(e.delta);
    if (out.passive) add_to(*out.passive, e.delta);  // keep the engine numbers in step for a following swap
  }
  // Skills of masteries the character does not have are not in the snapshot: unused as well.
  for (const auto* h : {&before, &after})
    for (const auto& [record, _] : h->skills)
      if (std::none_of(s.skills.begin(), s.skills.end(), [&](const snap::Skill& k) { return k.record == record; }) &&
          std::none_of(p.unused.begin(), p.unused.end(), [&](const SkillLevel& u) { return u.record == record; })) {
        const double b = get(before.skills, record), a = get(after.skills, record);
        if (a != b) p.unused.push_back({record, static_cast<std::uint32_t>(b), static_cast<std::uint32_t>(a)});
      }
  if (assumed) p.warnings.push_back("skill activity not in snapshot: passives and toggles assumed on");
}


// Non-zero numeric fields of a skill record at `level` that the model does not apply: not mapped by
// gdc::fields, not flat damage (the score reads it), not the level caps or weapon flags. `char_def` false:
// defensive* / character* fields are left out (the caller has the engine's numbers for them).
std::vector<std::string> unapplied(const arz::Record& r, std::uint32_t level, bool char_def) {
  std::vector<std::string> out;
  for (const auto& [key, field] : r.fields) {
    if (field.kind != arz::Field::Kind::Int && field.kind != arz::Field::Kind::Float) continue;
    const auto v = at_level(field, level);
    if (!v || *v == 0) continue;
    double scale;
    if (fields::lookup(key, scale) || fields::flat_damage_field(key)) continue;
    if (key == "skillMaxLevel" || key == "skillUltimateLevel") continue;
    if (fields::weapon_flag(key)) continue;
    if (!char_def && (key.starts_with("defensive") || key.starts_with("character"))) continue;
    out.push_back(key);
  }
  return out;
}

Contribution only(const Contribution& c, fields::Kind kind) {
  Contribution out;
  for (const auto& [k, v] : c)
    if (std::get<0>(k) == kind) out[k] = v;
  return out;
}

// itemSkillLevelEq when it is a plain number (most items: "1", "2"); 0 for an equation ("itemLevel/4+1").
std::uint32_t constant_level(std::string_view eq) {
  std::uint32_t v = 0;
  const auto [end, ec] = std::from_chars(eq.data(), eq.data() + eq.size(), v);
  return ec == std::errc{} && end == eq.data() + eq.size() ? v : 0;
}

// Skills the removed and the added item grant (itemSkillName / itemSkillLevelEq on their records). The engine
// adds an item skill's collectors to the totals like a learned skill's (snapshot.h `item_skills`). Applied:
//   removed: its captured contribution (schema 6) — char / defense exactly, damage % from its record at its
//            level when it is on; a skill that adds nothing now changes nothing.
//   added:   a Skill_Passive without weapon flags at a constant level: its record (gdc::fields).
// Everything else (procs, buffs, pets, weapon-gated passives, level equations) is reported, not scored.
void apply_granted(const snap::Snapshot& s, const snap::Item* old, const snap::Item& cand, const arz::Database& db,
                   AllDeltas& out_d, Prediction& p) {
  auto grants = [&](const snap::Item& it) {
    std::vector<std::pair<std::string, std::string>> out;  // skill record, level equation
    for (const auto& name : item_records(it))
      if (const auto rec = db.record(name); rec && !rec->string("itemSkillName").empty())
        out.emplace_back(std::string(rec->string("itemSkillName")), std::string(rec->string("itemSkillLevelEq")));
    return out;
  };
  auto kind = [](const arz::Record* r) { return r ? r->cls : std::string("record not in the database"); };
  // A grant both items carry (the same base item with other components) changes nothing.
  auto lost = old ? grants(*old) : decltype(grants(cand)){}, gained = grants(cand);
  for (auto it = lost.begin(); it != lost.end();)
    if (const auto g = std::find(gained.begin(), gained.end(), *it); g != gained.end()) {
      gained.erase(g);
      it = lost.erase(it);
    } else {
      ++it;
    }
  for (const auto& [record, eq] : lost) {
    Granted g{record, false, false, {}};
    const auto rec = db.record(record);
    const auto k = std::find_if(s.item_skills.begin(), s.item_skills.end(), [&](const snap::Skill& x) {
      return x.record == record && (old->id == 0 || x.item_id == old->id);
    });
    if (s.schema < 6) g.why = "not in snapshot (schema < 6)";
    else if (k == s.item_skills.end()) g.why = "not in snapshot";
    else if (!rec || rec->cls != "Skill_Passive") g.why = kind(rec);
    else {
      // Without weapon flags the collectors always run (SkillManager::GetCharAttributes 0x52F920 checks only
      // the level), so a passive that captured nothing adds only damage %.
      const bool on = (k->passive && (!k->passive->char_attributes.empty() || !k->passive->defense.empty())) ||
                      !fields::weapon_gated(*rec);
      const Contribution at = level_effect(*rec, k->current_level, k->current_level).at_from;
      const bool has_char_def = std::any_of(at.begin(), at.end(), [](const auto& kv) {
        return std::get<0>(kv.first) != fields::Kind::Damage && kv.second != 0;
      });
      if (!k->passive) {
        g.why = "capture failed";  // the adapter dropped its passive (fault)
      } else if (!on && !has_char_def && has_damage(at)) {
        g.why = "on or off unknown (damage only)";
      } else if (on) {
        Contribution c;
        for (const auto& a : k->passive->char_attributes) {
          c[{fields::Kind::Char, a.type, false}] -= a.value;
          c[{fields::Kind::Char, a.type, true}] -= a.modifier;
        }
        for (const auto& d : k->passive->defense) {
          c[{fields::Kind::Defense, d.type, false}] -= d.defense;
          c[{fields::Kind::Defense, d.type, true}] -= d.defense_modifier;
        }
        for (const auto& [key, v] : only(at, fields::Kind::Damage)) c[key] -= v;
        out_d.add(c);
        const auto rest = unapplied(*rec, k->current_level, false);
        g.scored = rest.empty();
        if (!rest.empty()) g.why = rest.front();
      } else {
        g.scored = true;  // off (weapon type): adds nothing now
      }
    }
    if (k != s.item_skills.end())
      std::erase_if(p.after.item_skills, [&](const snap::Skill& x) { return x.id == k->id; });
    p.granted.push_back(std::move(g));
  }
  for (const auto& [record, eq] : gained) {
    Granted g{record, true, false, {}};
    const auto rec = db.record(record);
    const std::uint32_t level = constant_level(eq);
    if (!rec || rec->cls != "Skill_Passive") g.why = kind(rec);
    else if (fields::weapon_gated(*rec)) g.why = "weapon type";
    else if (!rec->string("petBonusName").empty()) g.why = "pet bonus";
    else if (level == 0) g.why = "level " + eq;
    else if (const auto rest = unapplied(*rec, level, true); !rest.empty()) g.why = rest.front();
    else if (const SkillEffect e = level_effect(*rec, level, level); !e.problem.empty()) g.why = e.problem;
    else {
      out_d.add(e.at_from);
      snap::Skill k;  // so the score sees its flat damage (score.cpp weapon_flat)
      k.record = record;
      k.level = k.current_level = level;
      k.item_id = cand.id;
      add_to(k.passive.emplace(), e.at_from);
      p.after.item_skills.push_back(std::move(k));
      g.scored = true;
    }
    p.granted.push_back(std::move(g));
  }
}

}  // namespace

std::set<std::string> item_records(const snap::Item& it) {
  std::set<std::string> records = {it.record};
  if (it.replica)
    for (const std::string* r : {&it.replica->prefix, &it.replica->suffix, &it.replica->modifier,
                                 &it.replica->transmute, &it.replica->relic, &it.replica->relic_bonus,
                                 &it.replica->augment})
      records.insert(*r);
  if (it.relic) records.insert(it.relic->record);
  if (it.augment) records.insert(it.augment->record);
  records.erase("");
  return records;
}

std::vector<std::pair<double, double>> region_armor(const snap::Snapshot& s) {
  const auto* prot = find(s.defense, kProtection);
  const double scale = 1 + (prot ? prot->defense_modifier : 0) / 100;
  double everywhere = 0;
  for (const auto& it : s.equipped)
    if (it.location == 6) everywhere += item_armor(it);
  std::vector<std::pair<double, double>> out;
  for (const int location : {7, 9, 8, 10, 11, 12}) {
    double a = everywhere;
    for (const auto& it : s.equipped)
      if (it.location == location) a += item_armor(it);
    out.emplace_back(region_weight(location), a * scale);
  }
  return out;
}

double armor(const snap::Snapshot& s) {
  double sum = 0;
  for (const auto& it : s.equipped) sum += region_weight(it.location) * item_armor(it);
  const auto* prot = find(s.defense, kProtection);
  return sum * (1 + (prot ? prot->defense_modifier : 0) / 100);
}

double base_absorption(const snap::Snapshot& s, const arz::Database* db) {
  if (const auto* r = find(s.defense, kAbsorption); r && r->defense > 0) return r->defense;
  const arz::Record* rec = db ? db->record("records/game/gameengine.dbr") : nullptr;
  return rec ? rec->number("armorDefensiveAbsorption") : 0;
}

Sheet sheet(const snap::Snapshot& s) {
  auto total = [&](int t) {
    for (const auto& a : s.char_attributes)
      if (a.type == t) return static_cast<double>(a.total);
    return 0.0;
  };
  Sheet out;
  out.physique = total(kPhysique);
  out.cunning = total(kCunning);
  out.spirit = total(kSpirit);
  out.life = total(kLife);
  out.energy = total(kEnergy);
  out.life_regen = formulas::life_regen(s);
  out.energy_regen = formulas::mana_regen(s);
  out.oa = formulas::offensive_ability(s);
  out.da = formulas::defensive_ability(s);
  out.run_speed = total(kRunSpeed);
  out.armor = armor(s);
  const bool processed = has_processed(s);
  if (const auto* ab = find(s.defense, kAbsorption))
    out.absorption = processed ? ab->processed : std::min(100.0, ab->defense * (1 + ab->defense_modifier / 100.0));
  for (const auto& r : s.defense) {
    if (!cappable(r.type)) continue;
    Resist x;
    x.type = r.type;
    x.raw = r.defense;
    x.uncapped = uncapped(r);
    x.max = s.defense_cap + r.cap_bonus;
    x.shown = processed ? r.processed : x.uncapped;
    out.resists.push_back(x);
  }
  return out;
}

namespace {

// Applies `all` to p.after: char attributes (total = flat x (1 + modifier%), so flat = total / (1 + modifier%);
// Life / Energy from the attribute totals' change), defense rows, Absorption, processed values, damage
// modifiers. `s` is the state before.
void derive(const snap::Snapshot& s, const arz::Database* db, AllDeltas& all, Prediction& p) {
  Deltas& d = all.chr;
  std::map<int, double> derived;  // flat added from attribute totals
  auto apply = [&](int t) {
    const auto [dv, dm] = d[t];
    const double extra = derived[t];
    if (dv == 0 && dm == 0 && extra == 0) return 0.0;
    snap::CharAttribute& a = row(p.after.char_attributes, t);
    const double scale = 1 + a.modifier / 100.0;
    if (scale <= 0) {
      p.warnings.push_back("attribute " + std::string(names::char_attribute(t)) + ": modifier <= -100%");
      return 0.0;
    }
    const double before = a.total;
    a.modifier = static_cast<float>(a.modifier + dm);
    a.total = static_cast<float>((before / scale + dv + extra) * (1 + a.modifier / 100.0));
    return static_cast<double>(a.total) - before;
  };
  const double dp = apply(kPhysique), dc = apply(kCunning), ds = apply(kSpirit);
  derived[kLife] = kLifePerPhysique * dp + kLifePerCunning * dc + kLifePerSpirit * ds;
  derived[kEnergy] = kEnergyPerSpirit * ds;
  std::set<int> rest;
  for (const auto& [t, _] : d) rest.insert(t);
  rest.insert(kLife), rest.insert(kEnergy);
  for (int t : rest)
    if (t != kPhysique && t != kCunning && t != kSpirit) apply(t);

  // Defense rows: sums, except Absorption, which GetTotalDefenseType (0x11A90E..0x11A954) averages over
  // entries with a positive GetTotalAbsorption.
  for (const auto& [t, v] : all.def) {
    if (v.first == 0 && v.second == 0) continue;
    snap::CombatTotals& r = row(p.after.defense, t);
    r.defense = static_cast<float>(r.defense + v.first);
    r.defense_modifier = static_cast<float>(r.defense_modifier + v.second);
  }
  // Absorption (39): every armor entry (CombatAttributeDefense_AbsorptionProtection, items and skills alike)
  // carries GameEngine::GetArmorDefensiveAbsorption (DefenseAttribute_BaseProtectionAbsorption::LoadFromTable
  // 0x204420 copies GameEngine+0x1328, loaded from gameengine.dbr armorDefensiveAbsorption by
  // GameEngine::LoadFromDatabase 0x2C8830; AddToAccumulator 0x204BF0 builds the entry only when its armor
  // > 0), and GetTotalDefenseType(39) (0x11A880) averages the positive ones. So the total is that constant
  // while any armor entry exists, else 0. The snapshot has no type-39 row until an absorption modifier exists.
  if (snap::CombatTotals* r = find_mut(p.after.defense, kAbsorption)) {
    const snap::CombatTotals* a = find(p.after.defense, kArmor);
    if (a && a->defense > 0) {
      const double base = base_absorption(s, db);
      if (base > 0) r->defense = static_cast<float>(base);
      else p.warnings.push_back("armor absorption base unknown (no database)");
    } else {
      r->defense = 0;
    }
  }
  if (has_processed(s)) {
    const auto* prot = find(p.after.defense, kProtection);
    for (snap::CombatTotals& r : p.after.defense) {
      if (cappable(r.type))
        r.processed = static_cast<float>(std::min(uncapped(r), static_cast<double>(s.defense_cap + r.cap_bonus)));
      else if (r.type == kArmor)  // fixture: 6614 x (1 + 13%) = 7473.82
        r.processed = static_cast<float>(r.defense * (1 + (prot ? prot->defense_modifier : 0) / 100.0));
      else if (r.type == kAbsorption)  // fixtures: 70 x 1.4 = 98, 70 x 1.2 = 84; capped at 100 (Process 0x118370)
        r.processed = static_cast<float>(std::min(100.0, r.defense * (1 + r.defense_modifier / 100.0)));
    }
  }

  // Damage modifiers per type (items: as their getter reports them, 58 / 60 already spread; skill fields:
  // spread like the engine, fields::spread).
  for (const auto& [t, v] : all.dmg) {
    if (v.first == 0 && v.second == 0) continue;
    snap::CombatTotals& r = row(p.after.damage_modifiers, t);
    r.damage_modifier = static_cast<float>(r.damage_modifier + v.first);
    r.dot_modifier = static_cast<float>(r.dot_modifier + v.second);
  }
}

void add_conversion(std::vector<snap::Conversion>& v, int from, int to, double delta) {
  auto at = std::find_if(v.begin(), v.end(), [&](const snap::Conversion& x) { return x.from == from && x.to == to; });
  if (at == v.end()) at = v.insert(v.end(), snap::Conversion{from, to, 0});
  at->percent = static_cast<float>(at->percent + delta);
  if (std::abs(at->percent) < 1e-6) v.erase(at);
}

}  // namespace

Prediction swap(const snap::Snapshot& s, const snap::Item& candidate, int location, const arz::Database* db, Mode mode) {
  Prediction p;
  p.after = s;
  const snap::Item* old = at(s, location);
  if (!candidate.stats) p.warnings.push_back("candidate item has no stats");

  // The items' own getter output: char attributes, defense rows (Absorption values are averaged by derive(),
  // so only its modifier moves), damage modifiers as their getter reports them (58 / 60 already spread).
  AllDeltas all;
  auto add_item = [&](const snap::Item* it, double sign) {
    if (!it || !it->stats) return;
    for (const auto& c : it->stats->char_attributes) {
      all.chr[c.type].first += sign * c.value;
      all.chr[c.type].second += sign * c.modifier;
    }
    for (const auto& c : it->stats->defense) {
      if (c.type != kAbsorption) all.def[c.type].first += sign * c.defense;
      all.def[c.type].second += sign * c.defense_modifier;
    }
    for (const auto& c : it->stats->offensive_modifiers) {
      all.dmg[c.type].first += sign * c.damage_modifier;
      all.dmg[c.type].second += sign * c.dot_modifier;
    }
  };
  add_item(old, -1);
  add_item(&candidate, +1);

  // Equipped items and sets. Set bonuses (sets.h) at the old and new piece count of every set whose count
  // changes, including the set the removed item belonged to.
  snap::Item placed = candidate;
  placed.location = location;
  auto& eq = p.after.equipped;
  if (const auto it = std::find_if(eq.begin(), eq.end(), [&](const snap::Item& x) { return x.location == location; });
      it != eq.end())
    *it = placed;
  else
    eq.push_back(placed);
  Hidden hb = db ? hidden(old, *db) : Hidden{}, ha = db ? hidden(&candidate, *db) : Hidden{};
  const auto sb = sets::counts(s), sa = sets::counts(p.after);
  std::set<std::string> names;
  for (const auto& [k, _] : sb) names.insert(k);
  for (const auto& [k, _] : sa) names.insert(k);
  bool unmodeled_sets = false;
  for (const auto& k : names) {
    SetCount c;
    c.set = k;
    c.before = sb.contains(k) ? sb.at(k) : 0;
    c.after = sa.contains(k) ? sa.at(k) : 0;
    if (c.before == c.after) continue;
    const auto rec = db ? db->record(k) : nullptr;
    if (!rec) {
      unmodeled_sets = true;
      p.sets.push_back(std::move(c));
      continue;
    }
    c.modeled = true;
    const sets::Bonus b = sets::bonus(*rec, c.before), a = sets::bonus(*rec, c.after);
    sets::Stats delta = a.stats;
    for (const auto& [key, v] : b.stats) delta[key] -= v;
    all.add(delta);
    merge(hb, b);
    merge(ha, a);
    for (const auto* x : {&b, &a})
      for (const auto& cv : x->conversions) add_conversion(p.after.conversions, cv.from, cv.to, (x == &b ? -1 : 1) * cv.percent);
    if (b.granted_skill != a.granted_skill || b.granted_level != a.granted_level) {
      c.skill_lost = b.granted_skill;
      c.skill_gained = a.granted_skill;
    }
    for (const auto* x : {&b, &a})
      for (const auto& f : x->unmodeled)
        if (std::find(c.unmodeled.begin(), c.unmodeled.end(), f) == c.unmodeled.end() &&
            sets::bonus_field(*rec, f, c.before) != sets::bonus_field(*rec, f, c.after))
          c.unmodeled.push_back(f);
    p.sets.push_back(std::move(c));
  }
  if (unmodeled_sets) p.warnings.push_back("set bonuses change (set record not in the database)");
  if (db) {
    apply_skill_levels(s, location, hb, ha, *db, mode == Mode::Expected, all, p);
    apply_granted(s, old, candidate, *db, all, p);
  }
  derive(s, db, all, p);
  for (const auto* it : {old, static_cast<const snap::Item*>(&candidate)})
    if (it && it->stats)
      for (const auto& c : it->stats->conversions) add_conversion(p.after.conversions, c.from, c.to, (it == old ? -1 : 1) * c.percent);

  if (!db) p.warnings.push_back("no database: skill grants, set bonuses and max resist not checked");
  else if (hb.max_resist != ha.max_resist)
    p.warnings.push_back("max resist changes (not modeled)");
  return p;
}

std::optional<Prediction> toggle_buff(const snap::Snapshot& s, const snap::Skill& sk, bool on, const arz::Database& db) {
  if (sk.level == 0 || sk.current_level == 0 || sk.devotion_max_level != 0) return std::nullopt;
  const arz::Record* rec = db.record(sk.record);
  if (!rec) return std::nullopt;
  const arz::Record& stats = stats_record(*rec, db);
  if (buff_uptime(stats, sk.current_level) <= 0) return std::nullopt;
  Prediction p;
  p.after = s;
  AllDeltas all;
  const double sign = on ? 1 : -1;
  // sets::bonus reads every field at min(level - 1, size - 1), the skill's per-level arrays included.
  auto add = [&](const arz::Record& r, std::uint32_t level) {
    const sets::Bonus b = sets::bonus(r, static_cast<int>(level));
    all.add(scaled(b.stats, sign));
    for (const auto& c : b.conversions) add_conversion(p.after.conversions, c.from, c.to, sign * c.percent);
  };
  add(stats, sk.current_level);
  for (const std::uint32_t id : sk.modifier_ids) {
    const auto ms = std::find_if(s.skills.begin(), s.skills.end(), [&](const snap::Skill& k) { return k.id == id; });
    if (ms == s.skills.end()) return std::nullopt;
    if (ms->current_level == 0) continue;
    const arz::Record* mrec = db.record(ms->record);
    if (!mrec) return std::nullopt;
    add(*mrec, ms->current_level);
  }
  for (const auto& m : s.item_skill_modifiers) {
    if (m.skill_id != sk.id || m.modifier_record.empty()) continue;
    const arz::Record* mrec = db.record(m.modifier_record);
    if (!mrec) return std::nullopt;
    add(*mrec, 1);  // item skill modifiers have one level (skillMaxLevel 1)
  }
  derive(s, &db, all, p);
  return p;
}

}  // namespace gdc::model
