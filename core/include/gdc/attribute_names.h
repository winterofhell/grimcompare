#pragma once
// Display names for the engine's attribute enums. Values come from Game.dll 1.3.1.1:
//   CharAttributeType 1..57    re/CharAttributeType.tsv   (re/tools/char_attribute_types.py)
//   CombatAttributeType 0..65  re/CombatAttributeType.tsv (re/tools/combat_attribute_types.py)
// Each name is part of an exported class name with that value; tests/test_attribute_names.cpp
// checks every entry against the TSVs. Values with no exported class (41, 47, 49) have no name.
//   SkillAttributeType 1..4    the `+8` store in each exported SkillAttributeVal_* / Mod_* default
//                              ctor (0x51EE70..0x51F050); SkillAttributeAccumulator holds 5 slots
//   GameDifficulty 0..2        GameEngine::GetGameDifficultyAsPath (0x2D85B0), GetDifficultyFromName
//   EquipLocations 0..14       EquipManager::LocationToString (0x286480); 4 and 5 are both "Finger"
// Skill::Skill_Type has no string table in Game.dll: no names.
#include <string_view>

namespace gdc::names {

inline constexpr int kCharAttributeMax = 57;
inline constexpr int kCombatAttributeMax = 65;
inline constexpr int kSkillAttributeMax = 4;
inline constexpr int kEquipLocationMax = 14;

// "" when the value has no known name.
std::string_view char_attribute(int type);
std::string_view combat_attribute(int type);
std::string_view skill_attribute(int type);
std::string_view difficulty(int value);
std::string_view equip_location(int value);

}  // namespace gdc::names
