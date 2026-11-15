#pragma once
// The engine adds set bonuses outside the items: Character::ContributeMisc*Attributes (0x6A6B0 char, 0x6A980
// defense, 0x6AA80 damage, 0x6ABB0 modifiers, 0x6AEE0 conversions) walk the set map (Character+0x1FD0) with
// n = GetNumberOfItemsInSet (0x6A4C0) and index every field at min(n - 1, size - 1); n = 0 adds nothing
// (CharAttributeMod::AddToAccumulator 0xCCA80). So item getters never contain set bonuses.
// ItemSet::SkillModifierInstall (0x34B660 -> 0x34FB00) adds modifiers only while
// itemSkillModifierControl[min(n - 1, size - 1)] >= 1 (always when empty).
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
