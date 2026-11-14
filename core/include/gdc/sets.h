#pragma once
// Item set bonuses by equipped piece count (PROMPT.md Slice C). The engine adds them outside the items:
// Character::ContributeMisc*Attributes (0x6A6B0 char, 0x6A980 defense, 0x6AA80 offensive damage, 0x6ABB0
// offensive modifiers, 0x6AEE0 conversions, ...) walk the character's set map (Character+0x1FD0), take
// n = Character::GetNumberOfItemsInSet (0x6A4C0) and call the ItemSet's AttributePak getters with level n
// (ItemSet AttributePak vftable 0x6E3958). Those index every field array at min(n - 1, size - 1), level 0
// adds nothing (CharAttributeMod::AddToAccumulator 0xCCA80, DefenseAttribute_BaseProtectionAbsorption::
// AddToAccumulator 0x204BF0). So the item getters (snap::Item::stats) never contain set bonuses.
// Skill parts: ItemSet::SkillAugmentInstall (0x34B540) re-installs the augments and the granted skill
// (itemSkillName) for n; ItemSet::SkillModifierInstall (0x34B660 -> 0x34FB00) adds the set's item skill
// modifiers only while itemSkillModifierControl[min(n - 1, size - 1)] >= 1 (always when the array is empty).
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

// Piece count per set record, as GetNumberOfItemsInSet counts it: the set's member list holds the record of
// every equipped item of that set (Character::AddItemToSet 0x6A1A0) and the count is the size of its
// std::unique_copy (0x7B5C0). shortcut: unique_copy only drops adjacent duplicates; counted here as distinct
// records — revisit if two copies of one set ring ever count 2 in game.
std::map<std::string, int> counts(const snap::Snapshot& s);

using Stats = std::map<std::tuple<fields::Kind, int, bool>, double>;  // gdc::fields target -> value

struct Bonus {
  Stats stats;                                   // char / defense values and modifiers, damage % (58 / 60 unspread)
  std::vector<snap::Conversion> conversions;     // percent as a fraction (snapshot convention)
  std::vector<snap::Damage> flat;                // flat damage (dot: per second x duration, as items report it)
  std::map<std::string, double> skills;          // augmentSkillName<n> -> levels
  std::map<std::string, double> masteries;       // augmentMasteryName<n> -> levels
  double all = 0;                                // augmentAllLevel
  std::multiset<std::pair<std::string, std::string>> modifiers;  // modifiedSkillName<n> -> modifierSkillName<n>
  std::string granted_skill;                     // itemSkillName while itemSkillLevel > 0
  double granted_level = 0;
  std::map<std::string, double> max_resist;      // defensive*MaxResist
  std::vector<std::string> unmodeled;            // non-zero defensive* / character* / retaliation* / pet fields
};

// The set record's bonus at piece count `n` (0 = none).
Bonus bonus(const arz::Record& set, int n);
// One numeric field at piece count `n` (0 when absent or n = 0).
double bonus_field(const arz::Record& set, std::string_view key, int n);

}  // namespace gdc::sets
