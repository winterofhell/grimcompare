#pragma once
// From Game.dll 1.3.1.1: CharAttributeType / CombatAttributeType from exported class names (re/*.tsv, checked by
// tests/test_attribute_names.cpp; 41, 47, 49 have none). SkillAttributeType: the +8 store in each
// SkillAttributeVal_* / Mod_* ctor (0x51EE70..0x51F050). GameDifficulty: GetGameDifficultyAsPath 0x2D85B0.
// EquipLocations: EquipManager::LocationToString 0x286480. Skill_Type has no string table.
#include <string_view>

namespace gdc::names {

inline constexpr int kCharAttributeMax = 57;
inline constexpr int kCombatAttributeMax = 65;
inline constexpr int kSkillAttributeMax = 4;
inline constexpr int kEquipLocationMax = 14;

std::string_view char_attribute(int type);
std::string_view combat_attribute(int type);
std::string_view skill_attribute(int type);
std::string_view difficulty(int value);
std::string_view equip_location(int value);

}  // namespace gdc::names
