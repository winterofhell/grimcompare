#pragma once
// Engine enum names from Game.dll 1.3.1.1.
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

namespace gdc {

// CharAttributeType values the code names (re/CharAttributeType.tsv; DB Strength / Dexterity / Intelligence /
// Mana = Physique / Cunning / Spirit / Energy).
namespace char_attr {
inline constexpr int kPhysique = 1, kCunning = 2, kSpirit = 3, kLife = 4, kEnergy = 5,
                     kTotalSpeed = 13, kRunSpeed = 14, kAttackSpeed = 15, kCastSpeed = 16, kOffensiveAbility = 21,
                     kOffensiveAbilityModifier = 22, kBlockRecovery = 25, kDodge = 27, kDeflect = 28,
                     kGlobalReqReduction = 53, kLevelReqReduction = 54, kExperience = 55;
}  // namespace char_attr

// CombatAttributeType values the code names (re/CombatAttributeType.tsv). DB Poison = Acid (its DoT is Poison),
// DB Life = Vitality.
namespace combat_attr {
inline constexpr int kBasePhysical = 1, kPhysical = 2, kPierce = 4, kCold = 5, kFire = 6, kPoison = 7, kLightning = 8,
                     kLife = 9, kChaos = 10, kAether = 11, kBleeding = 15, kTotalSpeed = 16, kLifeLeech = 20,
                     kTotalDamageReduction = 28, kArmor = 38 /* AbsorptionProtection */, kAbsorption = 39,
                     kProtection = 40, kBlockChance = 55, kBlockAmount = 56, kElemental = 58, kCritDamage = 59,
                     kAll = 60 /* TotalDamageModifier */;
}  // namespace combat_attr

// EquipLocation values (names: names::equip_location).
namespace equip {
inline constexpr int kRightHand = 0, kLeftHand = 1, kNeck = 2, kMedal = 3, kFinger1 = 4, kFinger2 = 5, kWaist = 6,
                     kHead = 7, kChest = 8, kShoulders = 9, kHands = 10, kLegs = 11, kFeet = 12, kArtifact = 13;
}  // namespace equip

// Item::GetItemType values, named by the slot ValidateItemType 0x288D30 accepts them in.
namespace item_types {
inline constexpr int kHead = 1, kNeck = 2, kChest = 3, kLegs = 4, kFeet = 5, kRing = 6, kHands = 7, kOffHand = 8,
                     kWeapon = 9, kArtifact = 11, kWaist = 13, kShoulders = 14, kMedal = 15;
}  // namespace item_types

}  // namespace gdc
