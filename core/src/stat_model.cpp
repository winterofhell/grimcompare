#include "gdc/stat_model.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <optional>
#include <set>
#include <tuple>

#include "gdc/attribute_names.h"
#include "gdc/combat_formulas.h"
#include "gdc/db_fields.h"

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

double item_absorption(const snap::Item& it) {
  if (!it.stats) return 0;
  const auto* r = find(it.stats->defense, kAbsorption);
  return r ? r->defense : 0;
}

const snap::Item* at(const snap::Snapshot& s, int location) {
  for (const auto& it : s.equipped)
    if (it.location == location) return &it;
  return nullptr;
}

std::map<std::string, int> set_counts(const snap::Snapshot& s) {
  std::map<std::string, int> n;
  for (const auto& it : s.equipped)
    if (!it.set.empty()) ++n[it.set];
  return n;
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
};

Hidden hidden(const snap::Item* it, const arz::Database& db) {
  Hidden h;
  if (!it) return h;
  std::set<std::string> records = {it->record};
  if (it->replica)
    for (const std::string* r : {&it->replica->prefix, &it->replica->suffix, &it->replica->modifier,
                                 &it->replica->transmute, &it->replica->relic, &it->replica->relic_bonus,
                                 &it->replica->augment})
      records.insert(*r);
  if (it->relic) records.insert(it->relic->record);
  if (it->augment) records.insert(it->augment->record);
  records.erase("");
  for (const auto& name : records) {
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
    const fields::Target* t = fields::target(key);
    if (!t) {
      if (key.starts_with("defensive") || key.starts_with("character"))
        e.problem = "item skill modifier field " + key + " is not modeled";
      continue;
    }
    if (field.numbers.size() > 1) {
      e.problem = "item skill modifier field " + key + " varies by level";
      continue;
    }
    e.delta[{t->kind, t->type, t->modifier}] += field.numbers[0];
  }
  return e;
}

// A skill record's mapped stats at `from` and their change to `to`.
SkillEffect level_effect(const arz::Record& rec, std::uint32_t from, std::uint32_t to) {
  SkillEffect e;
  for (const auto& [key, field] : rec.fields) {
    if (field.kind != arz::Field::Kind::Int && field.kind != arz::Field::Kind::Float) continue;
    const auto a = at_level(field, from), b = at_level(field, to);
    const fields::Target* t = fields::target(key);
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
    e.at_from[{t->kind, t->type, t->modifier}] += *a;
    if (*b != *a) e.delta[{t->kind, t->type, t->modifier}] += *b - *a;
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
    std::erase_if(at_from, [](const auto& kv) { return kv.second == 0; });
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

// Item-granted skill levels and item skill modifiers. GetCurrentLevel (0x4909C0) = level ? min(level +
// augmented, cap) : 0 outside devotions, cap = SkillProfile+0x150 = max(skillUltimateLevel, skillMaxLevel)
// (SkillProfile::LoadProfile 0x545A27). A skill counts only while it contributes (Skill::passive non-empty);
// its stats at the new level come from its record (gdc::fields), a removed / added modifier's from the
// modifier record. Without passive data (schema < 4) passives and toggles are assumed on.
void apply_skill_levels(const snap::Snapshot& s, int location, const Hidden& before, const Hidden& after,
                        const arz::Database& db, Deltas& chr, Deltas& def, Prediction& p) {
  if (before.all != after.all)
    p.warnings.push_back("all-skills grant changes: applied to every skill (the engine's one exclusion is not traced)");
  auto mods_of = [](const Hidden& h, const std::string& skill) {
    std::multiset<std::string> out;
    for (auto it = h.modifiers.lower_bound({skill, std::string{}}); it != h.modifiers.end() && it->first == skill; ++it)
      out.insert(it->second);
    return out;
  };
  bool assumed = false;
  for (const auto& sk : s.skills) {
    if (sk.level == 0 || sk.devotion_max_level != 0) continue;  // unlearned skills stay at 0
    const double d = grant_delta(sk, s, before, after);
    const auto removed = mods_of(before, sk.record), added = mods_of(after, sk.record);
    if (d == 0 && removed == added) continue;
    const auto rec = db.record(sk.record);
    const std::string name = sk.record.substr(sk.record.rfind('/') + 1);
    if (!rec) {
      p.warnings.push_back("skill " + name + ": record not in the database");
      continue;
    }
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
      const auto mrec = ms != s.skills.end() ? db.record(ms->record) : std::nullopt;
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
    if (removed != added) {
      auto& list = p.after.item_skill_modifiers;
      std::erase_if(list, [&](const snap::ItemSkillModifier& m) { return m.skill_id == sk.id && m.location == location; });
      for (const auto& r : added) {
        snap::ItemSkillModifier m;
        m.skill_id = sk.id;
        m.skill_record = sk.record;
        m.modifier_record = r;
        m.location = location;
        list.push_back(std::move(m));
      }
    }
    if (rec->cls == "Skill_Modifier") {  // added through its parent's collector (Skill::AddModifierDefenseAttributes)
      if (to == from) continue;
      const snap::Skill* parent = modifier_parent(s.skills, sk.id);
      const auto prec = parent ? db.record(parent->record) : std::nullopt;
      if (!prec) {
        p.warnings.push_back("skill " + name + ": modifier level change not modeled (" +
                             (s.schema < 5 ? "its parent skill is captured from schema 5)" : "no parent skill)"));
        continue;
      }
      if (!contributes(*parent, *prec, assumed)) continue;
      const SkillEffect e = level_effect(*rec, from, to);
      if (!e.problem.empty()) {
        p.warnings.push_back("skill " + name + ": " + e.problem);
        continue;
      }
      for (const auto& [k, v] : e.delta) {
        const auto [kind, type, modifier] = k;
        auto& d2 = kind == fields::Kind::Char ? chr[type] : def[type];
        (modifier ? d2.second : d2.first) += v;
      }
      snap::Skill& pout = *std::find_if(p.after.skills.begin(), p.after.skills.end(),
                                        [&](const snap::Skill& k) { return k.id == parent->id && k.record == parent->record; });
      if (pout.passive) add_to(*pout.passive, e.delta);
      continue;
    }
    if (!contributes(sk, *rec, assumed)) continue;
    SkillEffect e = level_effect(*rec, from, to);
    engine_check(s, sk, from, attached, e);
    for (const auto& r : removed) add_mod(r, -1, e.delta);
    for (const auto& r : added) add_mod(r, +1, e.delta);
    if (e.problem.empty()) e.problem = problem;
    if (!e.problem.empty()) {
      p.warnings.push_back("skill " + name + ": " + e.problem);
      continue;
    }
    for (const auto& [k, v] : e.delta) {
      const auto [kind, type, modifier] = k;
      auto& d2 = kind == fields::Kind::Char ? chr[type] : def[type];
      (modifier ? d2.second : d2.first) += v;
    }
    if (out.passive) add_to(*out.passive, e.delta);  // keep the engine numbers in step for a following swap
  }
  if (assumed) p.warnings.push_back("skill activity not in snapshot: passives and toggles assumed on");
}

}  // namespace

double armor(const snap::Snapshot& s) {
  double sum = 0;
  for (const auto& it : s.equipped) sum += region_weight(it.location) * item_armor(it);
  const auto* prot = find(s.defense, kProtection);
  return sum * (1 + (prot ? prot->defense_modifier : 0) / 100);
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

Prediction swap(const snap::Snapshot& s, const snap::Item& candidate, int location, const arz::Database* db) {
  Prediction p;
  p.after = s;
  const snap::Item* old = at(s, location);
  if (!candidate.stats) p.warnings.push_back("candidate item has no stats");

  // Character attributes: total = flat * (1 + modifier%), so flat = total / (1 + modifier%).
  std::map<int, std::pair<double, double>> d;  // type -> (value delta, modifier delta)
  auto add_char = [&](const snap::Item* it, double sign) {
    if (it && it->stats)
      for (const auto& c : it->stats->char_attributes) {
        d[c.type].first += sign * c.value;
        d[c.type].second += sign * c.modifier;
      }
  };
  add_char(old, -1);
  add_char(&candidate, +1);
  std::map<int, std::pair<double, double>> dd;  // defense: type -> (value delta, modifier delta)
  if (db) apply_skill_levels(s, location, hidden(old, *db), hidden(&candidate, *db), *db, d, dd, p);
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
  auto add_def = [&](const snap::Item* it, double sign) {
    if (!it || !it->stats) return;
    for (const auto& c : it->stats->defense) {
      if (c.type != kAbsorption) dd[c.type].first += sign * c.defense;  // Absorption: averaged below
      dd[c.type].second += sign * c.defense_modifier;
    }
  };
  add_def(old, -1);
  add_def(&candidate, +1);
  for (const auto& [t, v] : dd) {
    if (v.first == 0 && v.second == 0) continue;
    snap::CombatTotals& r = row(p.after.defense, t);
    r.defense = static_cast<float>(r.defense + v.first);
    r.defense_modifier = static_cast<float>(r.defense_modifier + v.second);
  }
  const double abs_old = old ? item_absorption(*old) : 0, abs_new = item_absorption(candidate);
  if (abs_old != abs_new) {
    // shortcut: one absorption entry per item (true for the fixtures' armor) — revisit when a piece has two.
    int n = 0;
    for (const auto& it : s.equipped) n += item_absorption(it) > 0;
    snap::CombatTotals& r = row(p.after.defense, kAbsorption);
    const int n2 = n - (abs_old > 0) + (abs_new > 0);
    r.defense = n2 > 0 ? static_cast<float>((r.defense * n - abs_old + abs_new) / n2) : 0.0f;
  }
  if (has_processed(s)) {
    const auto* prot = find(p.after.defense, kProtection);
    for (snap::CombatTotals& r : p.after.defense) {
      if (cappable(r.type))
        r.processed = static_cast<float>(std::min(uncapped(r), static_cast<double>(s.defense_cap + r.cap_bonus)));
      else if (r.type == kArmor)  // fixture: 6614 x (1 + 13%) = 7473.82
        r.processed = static_cast<float>(r.defense * (1 + (prot ? prot->defense_modifier : 0) / 100.0));
      else if (r.type == kAbsorption)  // fixtures: 70 x 1.4 = 98, 70 x 1.2 = 84
        r.processed = static_cast<float>(r.defense * (1 + r.defense_modifier / 100.0));
    }
  }

  // Equipped items and sets.
  snap::Item placed = candidate;
  placed.location = location;
  auto& eq = p.after.equipped;
  const auto it = std::find_if(eq.begin(), eq.end(), [&](const snap::Item& x) { return x.location == location; });
  if (it != eq.end()) *it = placed;
  else eq.push_back(placed);
  const auto sb = set_counts(s), sa = set_counts(p.after);
  std::set<std::string> sets;
  for (const auto& [k, _] : sb) sets.insert(k);
  for (const auto& [k, _] : sa) sets.insert(k);
  for (const auto& k : sets) {
    const int b = sb.contains(k) ? sb.at(k) : 0, a = sa.contains(k) ? sa.at(k) : 0;
    if (a != b) p.sets.push_back({k, b, a});
  }
  if (!p.sets.empty()) p.warnings.push_back("set bonuses change (not modeled)");

  if (!db) p.warnings.push_back("no database: skill grants and max resist not checked");
  else if (hidden(old, *db).max_resist != hidden(&candidate, *db).max_resist)
    p.warnings.push_back("max resist changes (not modeled)");
  return p;
}

}  // namespace gdc::model
