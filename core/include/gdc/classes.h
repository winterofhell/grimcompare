#pragma once
// Game.dll's class tree (re/GameClassTree.tsv), checked as the engine does.
#include <string_view>

namespace gdc::classes {

// cls is base or descends from it; unknown: self only.
bool is_a(std::string_view cls, std::string_view base);

// Speed class, as Player::CalculateSkillDps 0x3E06D0 picks it.
enum class Speed { kNone, kAttack, kCast };
Speed speed(std::string_view cls);

}  // namespace gdc::classes
