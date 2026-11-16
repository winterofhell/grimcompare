#include "gdc/stat_model.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <map>
#include <optional>
#include <set>
#include <string_view>
#include <tuple>

#include "gdc/attribute_names.h"
#include "gdc/classes.h"
#include "gdc/combat_formulas.h"
#include "gdc/db_fields.h"
#include "gdc/sets.h"

namespace gdc::model {
namespace {

constexpr int kPhysique = 1, kCunning = 2, kSpirit = 3, kLife = 4, kEnergy = 5, kRunSpeed = 14;
constexpr int kArmor = 38, kAbsorption = 39, kProtection = 40;

// playerlevels.dbr: lifeIncrement 20, lifeIncrementDexterity 8, lifeIncrementIntelligence 12, manaIncrement 16
// per 8 attribute points. Applies to attribute totals, bonuses included: the poison swap fixture shows
// +12.1 Spirit -> +18.15 Life, +24.2 Energy.
constexpr double kLifePerPhysique = 20.0 / 8, kLifePerCunning = 8.0 / 8, kLifePerSpirit = 12.0 / 8;
constexpr double kEnergyPerSpirit = 16.0 / 8;

// Types with a DefenseAttributeDefenseCap_* class: CombatAttributeAbsDefense::Process caps them at
// defense_cap + cap_bonus.
bool cappable(int t) {
  static const std::set<int> types = {2, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 20, 21, 42, 43, 44, 45, 46};
  return types.contains(t);
}

// combatformulas.dbr combatRegion*Chance by EquipLocations; waist adds its armor whole (fit).
double region_weight(int location) {
  switch (location) {
    case 7: return 0.15;   // Head: combatRegionHeadChance
    case 9: return 0.15;   // Shoulders: combatRegionShouldersChance
    case 8: return 0.26;   // Chest: combatRegionTorsoChance
    case 10: return 0.12;  // Hands: combatRegionArmsChance
    case 11: return 0.20;  // Legs: combatRegionLegsChance
    case 12: return 0.12;  // Feet: combatRegionFeetChance
    case 6: return 1.0;    // fit
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


struct Hidden {
  std::map<std::string, double> skills;      // augmentSkillName*
  std::map<std::string, double> masteries;   // augmentMasteryName*
  double all = 0;                            // augmentAllLevel
  std::map<std::string, double> max_resist;  // defensive*MaxResist
  // b013a_sword has two modifiers on possession1.
  std::multiset<std::pair<std::string, std::string>> modifiers;
  // Set bonuses are not tied to the slot.
  std::multiset<std::pair<std::string, std::string>> set_modifiers;
};

void merge(Hidden& h, const sets::Bonus& b) {
  for (const auto& [k, v] : b.skills) h.skills[k] += v;
  for (const auto& [k, v] : b.masteries) h.masteries[k] += v;
  h.all += b.all;
  h.set_modifiers.insert(b.modifiers.begin(), b.modifiers.end());
  for (const auto& [k, v] : b.max_resist) h.max_resist[k] += v;
}

// One record's part of Hidden, in field order, so hidden() adds exactly as a walk of the fields would.
struct HiddenOp {
  enum Kind { Skill, Mastery, All, Modifier, MaxResist } kind;
  std::string key, value;
  double v = 0;
};

const std::vector<HiddenOp>& hidden_ops(const arz::Record& rec, const arz::Database& db) {
  return db.derived<std::vector<HiddenOp>>(rec, 0, [&] {
    std::vector<HiddenOp> ops;
    for (const auto& [key, field] : rec.fields) {
      auto grant = [&](HiddenOp::Kind kind, std::string_view name_prefix, std::string_view level_prefix) {
        if (!key.starts_with(name_prefix) || field.strings.empty() || field.strings[0].empty()) return;
        const std::string level_key = std::string(level_prefix) + key.substr(name_prefix.size());
        ops.push_back({kind, field.strings[0], {}, rec.number(level_key)});
      };
      grant(HiddenOp::Skill, "augmentSkillName", "augmentSkillLevel");
      grant(HiddenOp::Mastery, "augmentMasteryName", "augmentMasteryLevel");
      if (key == "augmentAllLevel") ops.push_back({HiddenOp::All, {}, {}, rec.number(key)});
      if (key.starts_with("modifierSkillName") && !field.strings.empty() && !field.strings[0].empty()) {
        const std::string_view target = rec.string("modifiedSkillName" + key.substr(17));
        if (!target.empty()) ops.push_back({HiddenOp::Modifier, std::string(target), field.strings[0], 0});
      }
      if (key.ends_with("MaxResist") && rec.number(key) != 0) ops.push_back({HiddenOp::MaxResist, key, {}, rec.number(key)});
    }
    return ops;
  });
}

Hidden hidden(const snap::Item* it, const arz::Database& db) {
  Hidden h;
  if (!it) return h;
  for (const auto& name : item_records(*it)) {
    const auto rec = db.record(name);
    if (!rec) continue;
    for (const auto& op : hidden_ops(*rec, db)) switch (op.kind) {
        case HiddenOp::Skill: h.skills[op.key] += op.v; break;
        case HiddenOp::Mastery: h.masteries[op.key] += op.v; break;
        case HiddenOp::All: h.all += op.v; break;
        case HiddenOp::Modifier: h.modifiers.emplace(op.key, op.value); break;
        case HiddenOp::MaxResist: h.max_resist[op.key] += op.v; break;
      }
  }
  return h;
}

double get(const std::map<std::string, double>& m, const std::string& k) {
  const auto it = m.find(k);
  return it != m.end() ? it->second : 0;
}

// SetSkillLevelAugment 0x5375B0 by record; SetMasteryAugment 0x5373D0 to the mastery and every skill whose
// GetMasteryId (Skill+0x13C) matches; SetAllSkillAugment 0x537230 to every skill but one the caller names
// (not traced, warned in swap()).
// Skill and mastery grants that differ between the sides; a mastery's with the ids of the snapshot's skills of
// that record.
struct Grants {
  std::map<std::string, double> skills;  // after - before
  struct Mastery {
    std::string record;
    std::set<std::uint32_t> ids;
    double delta = 0;
  };
  std::vector<Mastery> masteries;  // record order
};

Grants grants(const snap::Snapshot& s, const Hidden& before, const Hidden& after) {
  Grants out;
  for (const auto* h : {&before, &after})
    for (const auto& [k, _] : h->skills)
      if (const double d = get(after.skills, k) - get(before.skills, k); d != 0) out.skills[k] = d;
  std::set<std::string> records;
  for (const auto* h : {&before, &after})
    for (const auto& [m, _] : h->masteries) records.insert(m);
  for (const auto& m : records)
    if (const double d = get(after.masteries, m) - get(before.masteries, m); d != 0) {
      out.masteries.push_back({m, {}, d});
      for (const auto& other : s.skills)
        if (other.id != 0 && other.record == m) out.masteries.back().ids.insert(other.id);
    }
  return out;
}

// Grants that differ by exactly 0 are left out: adding +0 leaves the sum as it was.
double grant_delta(const snap::Skill& sk, const Grants& g, const Hidden& before, const Hidden& after) {
  const auto it = g.skills.find(sk.record);
  double d = (it != g.skills.end() ? it->second : 0.0) + after.all - before.all;
  for (const auto& m : g.masteries)
    if (sk.record == m.record || m.ids.contains(sk.mastery_id)) d += m.delta;
  return d;
}

std::optional<double> at_level(const arz::Field& f, std::uint32_t level) {
  if (level == 0 || f.numbers.empty()) return 0.0;
  if (f.numbers.size() == 1) return f.numbers[0];
  if (level > f.numbers.size()) return std::nullopt;
  return f.numbers[level - 1];
}

struct SkillEffect {
  Contribution delta;
  Contribution at_from;
  std::string problem;   // non-empty: not modeled, why
};

// Added through the skill's own collector: possession1's captured passive has AttackSpeed modifier 16 = two
// sword1h_b013_possession.dbr (characterAttackSpeedModifier 8, one per dual-wielded b013 sword); its own record
// has none (auto-snapshot 2026-11-14).
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

SkillEffect level_effect(const arz::Record& rec, std::uint32_t from, std::uint32_t to) {
  SkillEffect e;
  for (const auto& [key, field] : rec.fields) {
    if (field.kind != arz::Field::Kind::Int && field.kind != arz::Field::Kind::Float) continue;
    const auto a = at_level(field, from), b = at_level(field, to);
    double scale = 1;
    const fields::Target* t = fields::lookup(key, scale);
    if (!t) {
      // Only defensive* / character* fields reach the sheet values the model predicts.
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

// Engine numbers must equal record + attached modifiers, or a conditional part is in play and the record
// delta can't be trusted.
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
    if (kind == fields::Kind::Damage) continue;
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

struct AllDeltas {
  Deltas chr, def, dmg;
  void add(const Contribution& c, bool damage_only = false) {
    for (const auto& [k, v] : c) {
      const auto [kind, type, modifier] = k;
      if (damage_only && kind != fields::Kind::Damage) continue;
      if (kind == fields::Kind::Damage && (type == 58 || type == 60)) {
        // Items arrive spread already (see swap()).
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

// Skill_AttackBuffRadius-style skills keep damage on their buffSkillName record.
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

// bloodofdreeg1 -> bloodofdreeg1_buff.dbr (SkillBuff_Passive).
const arz::Record& stats_record(const arz::Record& rec, const arz::Database& db) {
  const std::string buff(rec.string("buffSkillName"));
  const arz::Record* b = buff.empty() ? nullptr : db.record(buff);
  return b ? *b : rec;
}

// SkillBuff_Passive (buffSkillName of Skill_BuffRadius / SkillSecondary_BuffSelfDuration / Skill_BuffOther in
// 1.3.1.1) or Skill_BuffSelfDuration (overguard1) with skillActiveDuration. Toggles, auras, debuffs, attacks and
// Skill_PassiveOn*BuffSelf are not timed buffs.
std::optional<BuffTiming> timing(const arz::Record& stats, std::uint32_t level) {
  if (!classes::is_a(stats.cls, "SkillBuff_Passive") && !classes::is_a(stats.cls, "Skill_BuffSelfDuration")) return std::nullopt;
  const double duration = value_at(stats, "skillActiveDuration", level);
  if (duration <= 0) return std::nullopt;
  return BuffTiming{duration, value_at(stats, "skillCooldownTime", level)};
}

Contribution scaled(const Contribution& c, double f) {
  Contribution out;
  for (const auto& [k, v] : c) out[k] = v * f;
  return out;
}

void add_conversion(std::vector<snap::Conversion>& v, int from, int to, double delta) {
  auto at = std::find_if(v.begin(), v.end(), [&](const snap::Conversion& x) { return x.from == from && x.to == to; });
  if (at == v.end()) at = v.insert(v.end(), snap::Conversion{from, to, 0});
  at->percent = static_cast<float>(at->percent + delta);
  if (std::abs(at->percent) < 1e-6) v.erase(at);
}

void drop_zeros(Contribution& c) {
  std::erase_if(c, [](const auto& kv) { return kv.second == 0; });
}

// Fields read at min(level - 1, size - 1), as sets::bonus does.
void add_unmodeled(const arz::Record& r, std::uint32_t level, BuffEffect& out) {
  if (level == 0) return;
  for (const char* name : {"skillCooldownReduction", "skillManaCostReduction"}) {
    const auto it = r.fields.find(name);
    if (it == r.fields.end() || it->second.numbers.empty()) continue;
    const auto& n = it->second.numbers;
    if (const double v = n[std::min<std::size_t>(level - 1, n.size() - 1)]; v != 0) out.unmodeled[name] += v;
  }
}

std::optional<BuffEffect> learned_effect(const snap::Snapshot& s, const snap::Skill& sk, const arz::Database& db) {
  if (!buff_timing(sk, db)) return std::nullopt;
  const arz::Record& stats = stats_record(*db.record(sk.record), db);
  std::vector<std::pair<const arz::Record*, std::uint32_t>> parts{{&stats, sk.current_level}};
  for (const std::uint32_t id : sk.modifier_ids) {
    const auto ms = std::find_if(s.skills.begin(), s.skills.end(), [&](const snap::Skill& k) { return k.id == id; });
    if (ms == s.skills.end()) return std::nullopt;
    if (ms->current_level == 0) continue;
    const arz::Record* mrec = db.record(ms->record);
    if (!mrec) return std::nullopt;
    parts.emplace_back(mrec, ms->current_level);
  }
  for (const auto& m : s.item_skill_modifiers) {
    if (m.skill_id != sk.id || m.modifier_record.empty()) continue;
    const arz::Record* mrec = db.record(m.modifier_record);
    if (!mrec) return std::nullopt;
    parts.emplace_back(mrec, 1);  // skillMaxLevel 1
  }
  BuffEffect out;
  for (const auto& [r, level] : parts) {
    const sets::Bonus b = sets::bonus(*r, static_cast<int>(level));
    for (const auto& [k, v] : b.stats) out.stats[k] += v;
    for (const auto& c : b.conversions) add_conversion(out.conversions, c.from, c.to, c.percent);
    add_unmodeled(*r, level, out);
  }
  drop_zeros(out.stats);
  return out;
}

void add_effect(const BuffEffect& e, double f, AllDeltas& all, std::vector<snap::Conversion>& out) {
  all.add(scaled(e.stats, f));
  for (const auto& c : e.conversions) add_conversion(out, c.from, c.to, f * c.percent);
}

bool add_buff(const snap::Snapshot& s, const snap::Skill& sk, double f, const arz::Database& db, AllDeltas& all,
              std::vector<snap::Conversion>& out) {
  const auto e = learned_effect(s, sk, db);
  if (e) add_effect(*e, f, all, out);
  return e.has_value();
}

// GetCurrentLevel 0x4909C0 = level ? min(level + augmented, cap) : 0 outside devotions; cap = SkillProfile+0x150 =
// max(skillUltimateLevel, skillMaxLevel) (SkillProfile::LoadProfile 0x545A27). Without passive data (schema < 4)
// passives and toggles are assumed on.
void apply_skill_levels(const snap::Snapshot& s, int location, const Hidden& before, const Hidden& after,
                        const arz::Database& db, AllDeltas& out_d, Prediction& p) {
  if (before.all != after.all)
    p.warnings.push_back("all-skills grant changes: applied to every skill (the engine's one exclusion is not traced)");
  auto of = [](const std::multiset<std::pair<std::string, std::string>>& m, const std::string& skill) {
    std::multiset<std::string> out;
    if (m.empty()) return out;
    for (auto it = m.lower_bound({skill, std::string{}}); it != m.end() && it->first == skill; ++it) out.insert(it->second);
    return out;
  };
  std::set<std::string_view, std::less<>> modded;  // skills with a modifier on either side
  for (const auto* m : {&before.modifiers, &before.set_modifiers, &after.modifiers, &after.set_modifiers})
    for (const auto& [k, _] : *m) modded.insert(k);
  auto mods_of = [&](const Hidden& h, const std::string& skill) {
    if (!modded.contains(skill)) return std::multiset<std::string>{};
    std::multiset<std::string> out = of(h.modifiers, skill);
    for (auto& r : of(h.set_modifiers, skill)) out.insert(r);
    return out;
  };
  bool assumed = false;
  std::set<const snap::Skill*> running;
  const Grants granted = grants(s, before, after);
  for (const auto& sk : s.skills) {
    if (sk.devotion_max_level != 0) continue;
    const double d = grant_delta(sk, granted, before, after);
    if (sk.level == 0) {  // GetCurrentLevel stays 0 whatever the bonus
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
    // The mastery bar takes no item bonus: with +1 Occultist (d016_relic) every Occultist skill rose by 1,
    // _classtraining_class03.dbr stayed (no skillMaxLevel / skillUltimateLevel in its record).
    if (rec->cls == "Skill_Mastery") continue;
    // shortcut: Skill_BuffRadius levels taken from the buffSkillName record, assumed to be what GetCurrentLevel's
    // profile (vt+0x578) reads — revisit when a buff skill at its cap disagrees.
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
      // AddModifierDefenseAttributes 0x494970 adds only Skill_Modifier objects; whether Skill_Transmuter is one is
      // not traced, so transmuters with stats are not modeled.
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
    // The slot the engine records for set modifiers is unknown, so match by record.
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
    if (rec->cls == "Skill_Modifier") {  // added through the parent's collector
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
        if (parent->buff_active && buff_timing(*parent, db)) {
          p.inactive.push_back({sk.record, parent->record, from, to, true});
          if (*parent->buff_active) running.insert(parent);
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
      if (&srec != rec) e = level_effect(srec, from, to);
      // The capture sees char / defense collectors only, so a damage-only Skill_Passive counts as on.
      if (has_damage(e.delta) && e.problem.empty() && rec->cls == "Skill_Passive" && removed == added) {
        out_d.add(e.delta, true);
      } else if (sk.buff_active && buff_timing(sk, db)) {
        if (to != from) p.inactive.push_back({sk.record, {}, from, to, true});
        if (*sk.buff_active) running.insert(&sk);
      } else if (triggered_buff(*rec)) {
        if (to != from) p.inactive.push_back({sk.record, {}, from, to, true});
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
    if (out.passive) add_to(*out.passive, e.delta);  // keep engine numbers in step for a following swap
  }
  for (const auto* h : {&before, &after})
    for (const auto& [record, _] : h->skills)
      if (std::none_of(s.skills.begin(), s.skills.end(), [&](const snap::Skill& k) { return k.record == record; }) &&
          std::none_of(p.unused.begin(), p.unused.end(), [&](const SkillLevel& u) { return u.record == record; })) {
        const double b = get(before.skills, record), a = get(after.skills, record);
        if (a != b) p.unused.push_back({record, static_cast<std::uint32_t>(b), static_cast<std::uint32_t>(a)});
      }
  if (assumed) p.warnings.push_back("skill activity not in snapshot: passives and toggles assumed on");
  for (const snap::Skill* sk : running) {
    const auto now = std::find_if(p.after.skills.begin(), p.after.skills.end(),
                                  [&](const snap::Skill& k) { return k.id == sk->id && k.record == sk->record; });
    add_buff(s, *sk, -1, db, out_d, p.after.conversions);
    add_buff(p.after, *now, +1, db, out_d, p.after.conversions);
  }
}


// Excludes gdc::fields mappings, flat damage (scored), level caps and weapon flags.
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

// 0 for an equation ("itemLevel/4+1").
std::uint32_t constant_level(std::string_view eq) {
  std::uint32_t v = 0;
  const auto [end, ec] = std::from_chars(eq.data(), eq.data() + eq.size(), v);
  return ec == std::errc{} && end == eq.data() + eq.size() ? v : 0;
}

// Removed: its captured contribution (schema 6), damage % from its record when on. Added: only a
// Skill_Passive without weapon flags at a constant level. Procs, buffs, pets, weapon-gated passives and level
// equations are reported, not scored.
void apply_granted(const snap::Snapshot& s, const snap::Item* old, const snap::Item& cand, const arz::Database& db,
                   AllDeltas& out_d, Prediction& p) {
  auto grants = [&](const snap::Item& it) {
    std::vector<std::pair<std::string, std::string>> out;
    for (const auto& name : item_records(it))
      if (const auto rec = db.record(name); rec && !rec->string("itemSkillName").empty())
        out.emplace_back(std::string(rec->string("itemSkillName")), std::string(rec->string("itemSkillLevelEq")));
    return out;
  };
  auto kind = [](const arz::Record* r) { return r ? r->cls : std::string("record not in the database"); };
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
      // SkillManager::GetCharAttributes 0x52F920 checks only the level, so a passive that captured nothing adds only
      // damage %.
      const bool on = (k->passive && (!k->passive->char_attributes.empty() || !k->passive->defense.empty())) ||
                      !fields::weapon_gated(*rec);
      const Contribution at = level_effect(*rec, k->current_level, k->current_level).at_from;
      const bool has_char_def = std::any_of(at.begin(), at.end(), [](const auto& kv) {
        return std::get<0>(kv.first) != fields::Kind::Damage && kv.second != 0;
      });
      if (!k->passive) {
        g.why = "capture failed";
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
        g.scored = true;
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
      snap::Skill k;  // score.cpp weapon_flat reads its flat damage
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

// Char attributes: total = flat x (1 + modifier%).
void derive(const snap::Snapshot& s, const arz::Database* db, AllDeltas& all, Prediction& p) {
  Deltas& d = all.chr;
  std::map<int, double> derived;
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

  // Absorption is averaged over entries with positive GetTotalAbsorption (GetTotalDefenseType
  // 0x11A90E..0x11A954); the rest sum.
  for (const auto& [t, v] : all.def) {
    if (v.first == 0 && v.second == 0) continue;
    snap::CombatTotals& r = row(p.after.defense, t);
    r.defense = static_cast<float>(r.defense + v.first);
    r.defense_modifier = static_cast<float>(r.defense_modifier + v.second);
  }
  // Every armor entry carries GameEngine::GetArmorDefensiveAbsorption:
  // BaseProtectionAbsorption::LoadFromTable 0x204420 copies GameEngine+0x1328 (gameengine.dbr
  // armorDefensiveAbsorption, LoadFromDatabase 0x2C8830); AddToAccumulator 0x204BF0 builds it only for armor > 0;
  // GetTotalDefenseType(39) 0x11A880 averages them. So the total is that constant while any armor exists, else 0.
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
      else if (r.type == kArmor)  // fixture: 6614 x 1.13 = 7473.82
        r.processed = static_cast<float>(r.defense * (1 + (prot ? prot->defense_modifier : 0) / 100.0));
      else if (r.type == kAbsorption)  // fixtures: 70 x 1.4 = 98; capped at 100 (Process 0x118370)
        r.processed = static_cast<float>(std::min(100.0, r.defense * (1 + r.defense_modifier / 100.0)));
    }
  }

  for (const auto& [t, v] : all.dmg) {
    if (v.first == 0 && v.second == 0) continue;
    snap::CombatTotals& r = row(p.after.damage_modifiers, t);
    r.damage_modifier = static_cast<float>(r.damage_modifier + v.first);
    r.dot_modifier = static_cast<float>(r.dot_modifier + v.second);
  }
}

}  // namespace

Prediction swap(const snap::Snapshot& s, const snap::Item& candidate, int location, const arz::Database* db) {
  Prediction p;
  p.after = s;
  const snap::Item* old = at(s, location);
  if (!candidate.stats) p.warnings.push_back("candidate item has no stats");

  // Absorption values are averaged by derive(), so only its modifier moves.
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
  // SkillAttributeAccumulator::AddValue 0x51F160 / AddChanceValue / AddModifier / AddChanceModifier sum each array.
  if (s.schema >= 9)
    for (const auto* it : {old, static_cast<const snap::Item*>(&candidate)}) {
      if (!it || !it->stats) continue;
      const float sign = it == old ? -1.0f : 1.0f;
      for (const auto& a : it->stats->skill_attributes) {
        snap::SkillAttribute& t = row(p.after.skill_attributes, a.type);
        t.value += sign * a.value, t.chance_value += sign * a.chance_value;
        t.modifier += sign * a.modifier, t.chance_modifier += sign * a.chance_modifier;
      }
    }

  // Includes the removed item's set.
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
    apply_skill_levels(s, location, hb, ha, *db, all, p);
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
  Prediction p;
  p.after = s;
  AllDeltas all;
  if (!add_buff(s, sk, on ? 1 : -1, db, all, p.after.conversions)) return std::nullopt;
  derive(s, &db, all, p);
  return p;
}

bool self_buff(const arz::Record& r) {
  return classes::is_a(r.cls, "Skill_BuffSelfDuration") || r.cls == "Skill_BuffSelfShield" || r.cls == "Skill_GiveBonus" ||
         triggered_buff(r) || classes::is_a(r.cls, "SkillBuff_Passive");
}

// Engine subclasses included: Skill_PassiveOnCritBuffSelf (playerclass07/passive02), Skill_PassiveOnHitBuffShield.
bool triggered_buff(const arz::Record& r) {
  return classes::is_a(r.cls, "Skill_PassiveOnLifeBuffSelf") || classes::is_a(r.cls, "Skill_PassiveOnHitBuffSelf");
}

std::optional<BuffTiming> buff_timing(const snap::Skill& sk, const arz::Database& db) {
  if (sk.level == 0 || sk.current_level == 0 || sk.devotion_max_level != 0) return std::nullopt;
  if (!sk.record.starts_with("records/skills/playerclass")) return std::nullopt;
  const arz::Record* rec = db.record(sk.record);
  return rec ? timing(stats_record(*rec, db), sk.current_level) : std::nullopt;
}

snap::Snapshot expected(const snap::Snapshot& s, const arz::Database* db) {
  if (!db) return s;
  Prediction p;
  p.after = s;
  AllDeltas all;
  bool any = false;
  for (auto* list : {&p.after.skills, &p.after.item_skills})
    for (snap::Skill& sk : *list) {
      if (!sk.buff_active) continue;  // schema < 8: leave the engine totals
      const bool active = *sk.buff_active;
      if (list == &p.after.skills && buff_timing(sk, *db)) {
        if (active) any |= add_buff(s, sk, -1, *db, all, p.after.conversions);
        sk.buff_active = false;
        continue;
      }
      sk.buff_active.reset();  // idempotent
      if (active && sk.current_level > 0) {
        // Toggles and auras stay in the totals as captured.
        const arz::Record* rec = db->record(sk.record);
        if (!rec) continue;
        if (list == &p.after.skills && sk.devotion_max_level == 0 && !triggered_buff(*rec)) continue;
        const arz::Record& stats = stats_record(*rec, *db);
        if (!self_buff(stats)) continue;
        all.add(scaled(level_effect(stats, sk.current_level, sk.current_level).at_from, -1));
        any = true;
      }
    }
  if (any) derive(s, db, all, p);
  return std::move(p.after);
}

BuffEffect buff_effect(const arz::Record& stats, std::uint32_t level) {
  BuffEffect out{level_effect(stats, level, level).at_from, {}, {}};
  add_unmodeled(stats, level, out);
  drop_zeros(out.stats);
  return out;
}

BuffEffect buff_effect(const snap::Snapshot& s, const snap::Skill& learned, const arz::Database& db) {
  return learned_effect(s, learned, db).value_or(BuffEffect{});
}

void buffed(const snap::Snapshot& s, std::span<const std::pair<const BuffEffect*, double>> buffs,
            const arz::Database& db, snap::Snapshot& out) {
  out.char_attributes = s.char_attributes;
  out.defense = s.defense;
  out.damage_modifiers = s.damage_modifiers;
  out.conversions = s.conversions;
  AllDeltas all;
  bool any = false;
  for (const auto& [e, share] : buffs) {
    if (share == 0 || e->empty()) continue;
    add_effect(*e, share, all, out.conversions);
    any = true;
  }
  if (!any) return;
  Prediction p;
  p.after = std::move(out);
  derive(s, &db, all, p);
  out = std::move(p.after);
}

}  // namespace gdc::model
