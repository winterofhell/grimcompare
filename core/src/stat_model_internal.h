#pragma once
// Shared by the stat model sources only.
#include <algorithm>
#include <cmath>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "gdc/attribute_names.h"
#include "gdc/sets.h"
#include "gdc/stat_model.h"

namespace gdc::model {

using char_attr::kPhysique, char_attr::kCunning, char_attr::kSpirit, char_attr::kLife, char_attr::kEnergy,
    char_attr::kRunSpeed;
using combat_attr::kArmor, combat_attr::kAbsorption, combat_attr::kProtection;

// DefenseCap types: Process caps them at defense_cap + cap_bonus.
inline bool cappable(int t) {
  static const std::set<int> types = {2, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 20, 21, 42, 43, 44, 45, 46};
  return types.contains(t);
}

inline double uncapped(const snap::CombatTotals& r) {
  return r.defense + std::abs(r.defense) * r.defense_modifier / 100;
}

inline bool has_processed(const snap::Snapshot& s) {
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

inline const snap::CombatTotals* find(const std::vector<snap::CombatTotals>& v, int type) {
  for (const auto& r : v)
    if (r.type == type) return &r;
  return nullptr;
}

inline snap::CombatTotals* find_mut(std::vector<snap::CombatTotals>& v, int type) {
  for (auto& r : v)
    if (r.type == type) return &r;
  return nullptr;
}

inline const snap::Item* at(const snap::Snapshot& s, int location) {
  for (const auto& it : s.equipped)
    if (it.location == location) return &it;
  return nullptr;
}

inline double get(const std::map<std::string, double>& m, const std::string& k) {
  const auto it = m.find(k);
  return it != m.end() ? it->second : 0;
}

// stat_model_hidden.cpp: grants an item or set adds without stats.
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

void merge(Hidden& h, const sets::Bonus& b);
Hidden hidden(const snap::Item* it, const arz::Database& db);

// Skill and mastery grants that differ between sides.
struct Grants {
  std::map<std::string, double> skills;  // after - before
  struct Mastery {
    std::string record;
    std::set<std::uint32_t> ids;
    double delta = 0;
  };
  std::vector<Mastery> masteries;  // record order
};

Grants grants(const snap::Snapshot& s, const Hidden& before, const Hidden& after);
double grant_delta(const snap::Skill& sk, const Grants& g, const Hidden& before, const Hidden& after);

// stat_model_effects.cpp: what a record or buff adds.
using Deltas = std::map<int, std::pair<double, double>>;  // type -> (value delta, modifier delta)

struct AllDeltas {
  Deltas chr, def, dmg;
  void add(const Contribution& c, bool damage_only = false) {
    for (const auto& [k, v] : c) {
      const auto [kind, type, modifier] = k;
      if (damage_only && kind != fields::Kind::Damage) continue;
      if (kind == fields::Kind::Damage && (type == combat_attr::kElemental || type == combat_attr::kAll)) {
        // Items arrive spread already (see swap()).
        for (int t : fields::spread(type, modifier)) (modifier ? dmg[t].second : dmg[t].first) += v;
        if (type == combat_attr::kAll && !modifier) dmg[combat_attr::kAll].first += v;  // b002e_torso: row 60 direct only
        continue;
      }
      auto& d = kind == fields::Kind::Char ? chr[type] : kind == fields::Kind::Defense ? def[type] : dmg[type];
      (modifier ? d.second : d.first) += v;
    }
  }
};

std::optional<double> at_level(const arz::Field& f, std::uint32_t level);
struct SkillEffect {
  Contribution delta;
  Contribution at_from;
  std::string problem;   // non-empty: not modeled, why
};

SkillEffect modifier_effect(const std::string& record, const arz::Database& db);
SkillEffect level_effect(const arz::Record& rec, std::uint32_t from, std::uint32_t to);
bool has_damage(const Contribution& c);
void add_to(snap::ItemStats& passive, const Contribution& delta);
bool damage_changes(const arz::Record& r, std::uint32_t from, std::uint32_t to, const arz::Database& db);
const arz::Record& stats_record(const arz::Record& rec, const arz::Database& db);
Contribution scaled(const Contribution& c, double f);
void add_conversion(std::vector<snap::Conversion>& v, int from, int to, double delta);
void add_unmodeled(const arz::Record& r, std::uint32_t level, BuffEffect& out);
void drop_zeros(Contribution& c);
void add_effect(const BuffEffect& e, double f, AllDeltas& all, std::vector<snap::Conversion>& out);
bool add_buff(const snap::Snapshot& s, const snap::Skill& sk, double f, const arz::Database& db, AllDeltas& all,
              std::vector<snap::Conversion>& out);

// stat_model_derive.cpp
void derive(const snap::Snapshot& s, const arz::Database* db, AllDeltas& all, Prediction& p);

// stat_model_skills.cpp
void apply_skill_levels(const snap::Snapshot& s, int location, const Hidden& before, const Hidden& after,
                        const arz::Database& db, AllDeltas& out_d, Prediction& p);

// stat_model_granted.cpp
void apply_granted(const snap::Snapshot& s, const snap::Item* old, const snap::Item& cand, const arz::Database& db,
                   AllDeltas& out_d, Prediction& p);

}  // namespace gdc::model
