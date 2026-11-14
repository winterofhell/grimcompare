#pragma once
// Database field -> engine attribute, for the record fields the swap model reads from skill records.
//   defensive*   DefenseAttributeAbs_* (value) / DefenseAttributeAbsMod_* (modifier): the class's +0x50
//                load field in Game.dll (re/DefenseAttributeFields.tsv, re/tools/defense_fields.py), its
//                CombatAttributeType from re/CombatAttributeType.tsv
//   character*   CharAttributeVal_* (value) / CharAttributeMod_* (modifier): the +0x40 string of the class
//                (re/CharAttributeType.tsv). CharAttributeMod_LifeMultiplier is left out (not a plain %).
// Left out on purpose: durations, caps, the composite Elemental (58) / AllResistance (60) types and
// everything else; tests/test_db_fields.cpp checks the table against the TSVs.
#include <span>
#include <string_view>
#include <utility>

namespace gdc::fields {

enum class Kind { Char, Defense };

struct Target {
  Kind kind;
  int type;       // CharAttributeType or CombatAttributeType
  bool modifier;  // accumulator modifier (%) rather than value
};

// nullptr when `field` is not in the table.
const Target* target(std::string_view field);
std::span<const std::pair<std::string_view, Target>> all();

}  // namespace gdc::fields
