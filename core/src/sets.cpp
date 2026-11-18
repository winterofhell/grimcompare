#include "gdc/sets.h"

#include <algorithm>

#include "gdc/attribute_names.h"

namespace gdc::sets {
namespace {

double at(const arz::Field& f, int n) {
  if (n <= 0 || f.numbers.empty()) return 0;
  return f.numbers[std::min<std::size_t>(static_cast<std::size_t>(n), f.numbers.size()) - 1];
}

double at(const arz::Record& r, std::string_view key, int n) {
  const auto it = r.fields.find(key);
  return it == r.fields.end() ? 0 : at(it->second, n);
}

// Template enum names (ERRATA §1) are CombatAttributeType names.
int damage_type_named(std::string_view name) {
  for (int t : {2, 4, 5, 6, 7, 8, 9, 10, 11, 58})
    if (!name.empty() && names::combat_attribute(t) == name) return t;
  return 0;
}

bool numeric(const arz::Field& f) { return f.kind == arz::Field::Kind::Int || f.kind == arz::Field::Kind::Float; }

}  // namespace

double bonus_field(const arz::Record& set, std::string_view key, int n) { return at(set, key, n); }

std::map<std::string, int> counts(const snap::Snapshot& s) {
  std::map<std::string, std::set<std::string>> records;
  for (const auto& it : s.equipped)
    if (!it.set.empty()) records[it.set].insert(it.record);
  std::map<std::string, int> out;
  for (const auto& [set, r] : records) out[set] = static_cast<int>(r.size());
  return out;
}

namespace {

// Numeric fields to stats, plus skill and mastery grants.
void stats_and_grants(const arz::Record& set, int n, Bonus& b) {
  for (const auto& [key, field] : set.fields) {
    if (numeric(field)) {
      const double v = at(field, n);
      if (v == 0) continue;
      double scale;
      if (const fields::Target* t = fields::lookup(key, scale)) {
        b.stats[{t->kind, t->type, t->modifier}] += v * scale;
      } else if (key.ends_with("MaxResist")) {
        b.max_resist[key] += v;
      } else if (key.starts_with("defensive") || key.starts_with("character") || key.starts_with("retaliation") ||
                 (key.starts_with("offensive") && !fields::flat_damage_field(key))) {
        b.unmodeled.push_back(key);
      }
    }
    auto grant = [&](std::map<std::string, double>& out, std::string_view name_prefix, std::string_view level_prefix) {
      if (!key.starts_with(name_prefix) || field.strings.empty() || field.strings[0].empty()) return;
      const double v = at(set, std::string(level_prefix) + key.substr(name_prefix.size()), n);
      if (v != 0) out[field.strings[0]] += v;
    };
    grant(b.skills, "augmentSkillName", "augmentSkillLevel");
    grant(b.masteries, "augmentMasteryName", "augmentMasteryLevel");
  }
}

void skill_modifiers(const arz::Record& set, int n, Bonus& b) {
  const auto control = set.fields.find("itemSkillModifierControl");
  if (control != set.fields.end() && !control->second.numbers.empty() && !(at(control->second, n) >= 1)) return;
  for (const auto& [key, field] : set.fields) {
    if (!key.starts_with("modifierSkillName") || field.strings.empty() || field.strings[0].empty()) continue;
    const std::string_view target = set.string("modifiedSkillName" + key.substr(17));
    if (!target.empty()) b.modifiers.emplace(std::string(target), field.strings[0]);
  }
}

void conversions(const arz::Record& set, int n, Bonus& b) {
  for (const auto& [in, out, pct] : {std::tuple{"conversionInType", "conversionOutType", "conversionPercentage"},
                                     std::tuple{"conversionInType2", "conversionOutType2", "conversionPercentage2"}}) {
    const int from = damage_type_named(set.string(in)), to = damage_type_named(set.string(out));
    const double v = at(set, pct, n);
    if (from && to && v > 0) b.conversions.push_back({from, to, static_cast<float>(v / 100)});
  }
}

void flat_damage(const arz::Record& set, int n, Bonus& b) {
  for (const auto& f : fields::flat_damage()) {
    const double min = at(set, f.min, n);
    if (min == 0) continue;
    snap::Damage d;
    d.type = f.type;
    d.dot = !f.duration.empty();
    d.duration = d.dot ? static_cast<float>(at(set, f.duration, n)) : 0.0f;
    const double max = std::max(min, at(set, f.max, n));
    const double chance = at(set, f.chance, n);
    const double scale = chance > 0 && chance < 100 ? chance / 100 : 1;
    if (scale != 1) {
      d.chance = static_cast<float>(chance);
      d.raw_min = static_cast<float>(min);
      d.raw_max = static_cast<float>(max);
    }
    d.min = static_cast<float>(min * scale);
    d.max = static_cast<float>(max * scale);
    b.flat.push_back(d);
  }
}

}  // namespace

Bonus bonus(const arz::Record& set, int n) {
  Bonus b;
  if (n <= 0) return b;
  stats_and_grants(set, n, b);
  b.all = at(set, "augmentAllLevel", n);
  skill_modifiers(set, n, b);
  if (const auto skill = set.string("itemSkillName"); !skill.empty()) {
    const double level = at(set, "itemSkillLevel", n);
    if (level > 0) {
      b.granted_skill = std::string(skill);
      b.granted_level = level;
    }
  }
  if (!set.string("petBonusName").empty()) b.unmodeled.push_back("petBonusName");
  conversions(set, n, b);
  flat_damage(set, n, b);
  return b;
}

}  // namespace gdc::sets
