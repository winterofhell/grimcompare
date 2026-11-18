#pragma once
// Engine enum names from Game.dll 1.3.1.1 (§13.6).
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
