#pragma once
// Set bonuses, added outside item getters (§13.17).
#include <map>
#include <set>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "gdc/arz.h"
#include "gdc/db_fields.h"
#include "gdc/snapshot.h"

namespace gdc::sets {

// Character::AddItemToSet 0x6A1A0 holds each equipped member record; the count is std::unique_copy's size
// (0x7B5C0). shortcut: counted as distinct records, though unique_copy only drops adjacent duplicates — revisit if two
// copies of one set ring ever count 2 in game.
std::map<std::string, int> counts(const snap::Snapshot& s);

using Stats = std::map<std::tuple<fields::Kind, int, bool>, double>;

struct Bonus {
  Stats stats;                                   // 58 / 60 unspread
  std::vector<snap::Conversion> conversions;     // fraction, as in snapshots
  std::vector<snap::Damage> flat;                // dot: per second x duration
  std::map<std::string, double> skills;
  std::map<std::string, double> masteries;
  double all = 0;
  std::multiset<std::pair<std::string, std::string>> modifiers;
  std::string granted_skill;                     // while itemSkillLevel > 0
  double granted_level = 0;
  std::map<std::string, double> max_resist;
  std::vector<std::string> unmodeled;            // non-zero fields not modeled
};

Bonus bonus(const arz::Record& set, int n);
double bonus_field(const arz::Record& set, std::string_view key, int n);

}  // namespace gdc::sets
