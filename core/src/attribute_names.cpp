#include "gdc/attribute_names.h"

#include <array>

namespace gdc::names {
namespace {

// Index = CharAttributeType. Suffix of CharAttributeVal_* (else CharAttributeMod_*).
constexpr std::array<std::string_view, kCharAttributeMax + 1> kChar = {
    "",
    "Strength",
    "Dexterity",
    "Intelligence",
    "Life",
    "Mana",
    "LifeRegen",
    "ManaRegen",
    "ManaLimitReserve",
    "ManaLimitReserveReduction",
    "Constitution",
    "HealIncrease",
    "PercentHealIncrease",
    "TotalSpeed",
    "RunSpeed",
    "AttackSpeed",
    "SpellCastSpeed",
    "BaseAttackSpeed",
    "RunSpeedCap",
    "AttackSpeedCap",
    "SpellCastSpeedCap",
    "OffensiveAbility",
    "OffensiveAbilityModifier",
    "DefensiveAbility",
    "DefensiveAbilityModifier",
    "DefensiveBlockRecoveryReduction",
    "EnergyAbsorptionPercent",
    "DodgePercent",
    "DeflectProjectile",
    "WeaponStrengthReqReduction",
    "WeaponDexterityReqReduction",
    "WeaponIntelligenceReqReduction",
    "MeleeStrengthReqReduction",
    "MeleeDexterityReqReduction",
    "MeleeIntelligenceReqReduction",
    "HuntingStrengthReqReduction",
    "HuntingDexterityReqReduction",
    "HuntingIntelligenceReqReduction",
    "StaffStrengthReqReduction",
    "StaffDexterityReqReduction",
    "StaffIntelligenceReqReduction",
    "Weapon2HStrengthReqReduction",
    "Weapon2HDexterityReqReduction",
    "Weapon2HIntelligenceReqReduction",
    "ShieldStrengthReqReduction",
    "ShieldDexterityReqReduction",
    "ShieldIntelligenceReqReduction",
    "ArmorStrengthReqReduction",
    "ArmorDexterityReqReduction",
    "ArmorIntelligenceReqReduction",
    "JewelryStrengthReqReduction",
    "JewelryDexterityReqReduction",
    "JewelryIntelligenceReqReduction",
    "GlobalReqReduction",
    "LevelReqReduction",
    "IncreasedExperience",
    "IncreasedGold",
    "LightRadius",
};

// Index = CombatAttributeType. Most common class-name suffix for the value (game spelling kept,
// e.g. "LifeLeach").
constexpr std::array<std::string_view, kCombatAttributeMax + 1> kCombat = {
    "BasePhysical",
    "BasePhysical",
    "Physical",
    "PierceRatio",
    "Pierce",
    "Cold",
    "Fire",
    "Poison",
    "Lightning",
    "Life",
    "Chaos",
    "Aether",
    "ManaBurn",
    "Disruption",
    "PercentCurrentLife",
    "Bleeding",
    "TotalSpeed",
    "AttackSpeed",
    "SpellCastSpeed",
    "RunSpeed",
    "LifeLeach",
    "ManaLeach",
    "OffensiveAbility",
    "DefensiveAbility",
    "OffensiveReduction",
    "DefensiveReduction",
    "Fumble",
    "ProjectileFumble",
    "TotalDamageReductionPercent",
    "TotalDamageReductionAbsolute",
    "PhysicalReductionPercent",
    "ElementalReductionPercent",
    "TotalResistanceReductionPercent",
    "TotalResistanceReductionAbsolute",
    "PhysicalResistanceReductionPercent",
    "PhysicalResistanceReductionAbsolute",
    "ElementalResistanceReductionPercent",
    "ElementalResistanceReductionAbsolute",
    "AbsorptionProtection",
    "Absorption",
    "Protection",
    "",
    "Stun",
    "Sleep",
    "Trap",
    "Freeze",
    "Petrify",
    "",
    "Knockdown",
    "",
    "Taunt",
    "Convert",
    "Fear",
    "Confusion",
    "CrowdControl",
    "BlockModifier",
    "BlockAmountModifier",
    "Reflect",
    "Elemental",
    "CritDamageModifier",
    "TotalDamageModifier",
    "DamageMultiplier",
    "ManaBurnRatio",
    "PercentReflectionResistance",
    "DefenseCap_All",
    "DefenseCap_CrowdControl",
};

constexpr std::array<std::string_view, kSkillAttributeMax + 1> kSkill = {
    "", "CooldownReduction", "ManaCostReduction", "ProjectileSpeedModifier", "ComboChargeSpendReduction",
};

constexpr std::array<std::string_view, 3> kDifficulty = {"Normal", "Elite", "Ultimate"};

constexpr std::array<std::string_view, kEquipLocationMax + 1> kLocation = {
    "R Hand", "L Hand", "Neck", "Medal", "Finger", "Finger", "Waist", "Head",
    "Chest",  "Shoulders", "Hands", "Legs", "Feet", "Artifact", "Clothing",
};

template <std::size_t N>
std::string_view at(const std::array<std::string_view, N>& a, int i) {
  return i >= 0 && static_cast<std::size_t>(i) < N ? a[static_cast<std::size_t>(i)] : std::string_view{};
}

}  // namespace

std::string_view skill_attribute(int type) { return at(kSkill, type); }
std::string_view difficulty(int value) { return at(kDifficulty, value); }
std::string_view equip_location(int value) { return at(kLocation, value); }

std::string_view char_attribute(int type) {
  return type >= 0 && type <= kCharAttributeMax ? kChar[static_cast<std::size_t>(type)] : std::string_view{};
}

std::string_view combat_attribute(int type) {
  return type >= 0 && type <= kCombatAttributeMax ? kCombat[static_cast<std::size_t>(type)] : std::string_view{};
}

}  // namespace gdc::names
