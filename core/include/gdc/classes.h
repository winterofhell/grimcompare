#pragma once
// Game.dll's own class tree (GAME::RTTI_ClassInfo parents, re/tools/class_tree.py -> re/GameClassTree.tsv), so a
// record's `Class` can be checked as the engine checks its objects (subclasses included).
#include <string_view>

namespace gdc::classes {

// cls == base, or base is one of cls's engine ancestors. Unknown classes are only themselves.
bool is_a(std::string_view cls, std::string_view base);

// Player::CalculateSkillDps 0x3E06D0 picks the speed by class, in this order: SkillActivatedWeapon (0x3E0B3D),
// SkillActivatedWeaponPool (0x3E0B80) -> CalculateAttackSpeed; Skill_AttackRadiusSpin (0x3E0BB7) -> GetAttackSpeed;
// SkillActivatedSpell (0x3E0BEE), SkillActivatedBuffSelf (0x3E0C25) -> GetSpellCastSpeed. kNone: none of them.
enum class Speed { kNone, kAttack, kCast };
Speed speed(std::string_view cls);

}  // namespace gdc::classes
