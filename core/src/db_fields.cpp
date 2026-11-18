#include "gdc/db_fields.h"

#include <algorithm>
#include <unordered_map>

namespace gdc::fields {
namespace {

constexpr Kind kChar = Kind::Char, kDefense = Kind::Defense;

// Sorted by field name; generated from the re/*.tsv tables.
constexpr std::pair<std::string_view, Target> kTable[] = {
    {"characterArmorDexterityReqReduction", {kChar, 48, false}},
    {"characterArmorIntelligenceReqReduction", {kChar, 49, false}},
    {"characterArmorStrengthReqReduction", {kChar, 47, false}},
    {"characterAttackSpeed", {kChar, 15, false}},
    {"characterAttackSpeedMaxModifier", {kChar, 19, true}},
    {"characterAttackSpeedModifier", {kChar, 15, true}},
    {"characterBaseAttackSpeed", {kChar, 17, false}},
    {"characterConstitution", {kChar, 10, false}},
    {"characterConstitutionModifier", {kChar, 10, true}},
    {"characterDefensiveAbility", {kChar, 23, false}},
    {"characterDefensiveAbilityModifier", {kChar, 24, false}},
    {"characterDefensiveBlockRecoveryReduction", {kChar, 25, false}},
    {"characterDeflectProjectile", {kChar, 28, false}},
    {"characterDexterity", {kChar, 2, false}},
    {"characterDexterityModifier", {kChar, 2, true}},
    {"characterDodgePercent", {kChar, 27, false}},
    {"characterEnergyAbsorptionPercent", {kChar, 26, false}},
    {"characterGlobalReqReduction", {kChar, 53, false}},
    {"characterHealIncreasePercent", {kChar, 11, true}},
    {"characterHuntingDexterityReqReduction", {kChar, 36, false}},
    {"characterHuntingIntelligenceReqReduction", {kChar, 37, false}},
    {"characterHuntingStrengthReqReduction", {kChar, 35, false}},
    {"characterIncreasedExperience", {kChar, 55, false}},
    {"characterIncreasedGold", {kChar, 56, false}},
    {"characterIntelligence", {kChar, 3, false}},
    {"characterIntelligenceModifier", {kChar, 3, true}},
    {"characterJewelryDexterityReqReduction", {kChar, 51, false}},
    {"characterJewelryIntelligenceReqReduction", {kChar, 52, false}},
    {"characterJewelryStrengthReqReduction", {kChar, 50, false}},
    {"characterLevelReqReduction", {kChar, 54, false}},
    {"characterLife", {kChar, 4, false}},
    {"characterLifeModifier", {kChar, 4, true}},
    {"characterLifeRegen", {kChar, 6, false}},
    {"characterLifeRegenModifier", {kChar, 6, true}},
    {"characterLightRadius", {kChar, 57, false}},
    {"characterMana", {kChar, 5, false}},
    {"characterManaLimitReserve", {kChar, 8, false}},
    {"characterManaLimitReserveModifier", {kChar, 8, true}},
    {"characterManaLimitReserveReduction", {kChar, 9, false}},
    {"characterManaLimitReserveReductionModifier", {kChar, 9, true}},
    {"characterManaModifier", {kChar, 5, true}},
    {"characterManaRegen", {kChar, 7, false}},
    {"characterManaRegenModifier", {kChar, 7, true}},
    {"characterMeleeDexterityReqReduction", {kChar, 33, false}},
    {"characterMeleeIntelligenceReqReduction", {kChar, 34, false}},
    {"characterMeleeStrengthReqReduction", {kChar, 32, false}},
    {"characterOffensiveAbility", {kChar, 21, false}},
    {"characterOffensiveAbilityModifier", {kChar, 22, false}},
    {"characterPercentHealIncreaseModifier", {kChar, 12, true}},
    {"characterRunSpeed", {kChar, 14, false}},
    {"characterRunSpeedMaxModifier", {kChar, 18, true}},
    {"characterRunSpeedModifier", {kChar, 14, true}},
    {"characterShieldDexterityReqReduction", {kChar, 45, false}},
    {"characterShieldIntelligenceReqReduction", {kChar, 46, false}},
    {"characterShieldStrengthReqReduction", {kChar, 44, false}},
    {"characterSpellCastSpeed", {kChar, 16, false}},
    {"characterSpellCastSpeedMaxModifier", {kChar, 20, true}},
    {"characterSpellCastSpeedModifier", {kChar, 16, true}},
    {"characterStaffDexterityReqReduction", {kChar, 39, false}},
    {"characterStaffIntelligenceReqReduction", {kChar, 40, false}},
    {"characterStaffStrengthReqReduction", {kChar, 38, false}},
    {"characterStrength", {kChar, 1, false}},
    {"characterStrengthModifier", {kChar, 1, true}},
    {"characterTotalSpeedModifier", {kChar, 13, true}},
    {"characterWeapon2HDexterityReqReduction", {kChar, 42, false}},
    {"characterWeapon2HIntelligenceReqReduction", {kChar, 43, false}},
    {"characterWeapon2HStrengthReqReduction", {kChar, 41, false}},
    {"characterWeaponDexterityReqReduction", {kChar, 30, false}},
    {"characterWeaponIntelligenceReqReduction", {kChar, 31, false}},
    {"characterWeaponStrengthReqReduction", {kChar, 29, false}},
    {"defensiveAbsorptionModifier", {kDefense, 39, true}},
    {"defensiveAether", {kDefense, 11, false}},
    {"defensiveAetherModifier", {kDefense, 11, true}},
    {"defensiveBleeding", {kDefense, 15, false}},
    {"defensiveBleedingModifier", {kDefense, 15, true}},
    {"defensiveChaos", {kDefense, 10, false}},
    {"defensiveChaosModifier", {kDefense, 10, true}},
    {"defensiveCold", {kDefense, 5, false}},
    {"defensiveColdModifier", {kDefense, 5, true}},
    {"defensiveConfusion", {kDefense, 53, false}},
    {"defensiveConvert", {kDefense, 51, false}},
    {"defensiveCrowdControl", {kDefense, 54, false}},
    {"defensiveDisruption", {kDefense, 13, false}},
    {"defensiveFear", {kDefense, 52, false}},
    {"defensiveFire", {kDefense, 6, false}},
    {"defensiveFireModifier", {kDefense, 6, true}},
    {"defensiveFreeze", {kDefense, 45, false}},
    {"defensiveKnockdown", {kDefense, 48, false}},
    {"defensiveKnockdownModifier", {kDefense, 48, true}},
    {"defensiveLife", {kDefense, 9, false}},
    {"defensiveLifeModifier", {kDefense, 9, true}},
    {"defensiveLightning", {kDefense, 8, false}},
    {"defensiveLightningModifier", {kDefense, 8, true}},
    {"defensiveManaBurn", {kDefense, 12, false}},
    {"defensiveManaBurnRatio", {kDefense, 62, false}},
    {"defensivePercentCurrentLife", {kDefense, 14, false}},
    {"defensivePercentReflectionResistance", {kDefense, 63, false}},
    {"defensivePetrify", {kDefense, 46, false}},
    {"defensivePhysical", {kDefense, 2, false}},
    {"defensivePhysicalModifier", {kDefense, 2, true}},
    {"defensivePierce", {kDefense, 4, false}},
    {"defensivePierceModifier", {kDefense, 4, true}},
    {"defensivePoison", {kDefense, 7, false}},
    {"defensivePoisonModifier", {kDefense, 7, true}},
    {"defensiveProtectionModifier", {kDefense, 40, true}},
    {"defensiveSleep", {kDefense, 43, false}},
    {"defensiveSleepModifier", {kDefense, 43, true}},
    {"defensiveSlowLifeLeach", {kDefense, 20, false}},
    {"defensiveSlowLifeLeachModifier", {kDefense, 20, true}},
    {"defensiveSlowManaLeach", {kDefense, 21, false}},
    {"defensiveSlowManaLeachModifier", {kDefense, 21, true}},
    {"defensiveStun", {kDefense, 42, false}},
    {"defensiveStunModifier", {kDefense, 42, true}},
    {"defensiveTaunt", {kDefense, 50, false}},
    {"defensiveTotalSpeedResistance", {kDefense, 16, false}},
    {"defensiveTrap", {kDefense, 44, false}},
};

constexpr Kind kDamage = Kind::Damage;

// Sorted by field name.
constexpr std::pair<std::string_view, Target> kDamageMods[] = {
    {"offensiveAetherModifier", {kDamage, 11, false}},
    {"offensiveBleedingModifier", {kDamage, 15, false}},
    {"offensiveChaosModifier", {kDamage, 10, false}},
    {"offensiveColdModifier", {kDamage, 5, false}},
    {"offensiveElementalModifier", {kDamage, 58, false}},
    {"offensiveFireModifier", {kDamage, 6, false}},
    {"offensiveLifeModifier", {kDamage, 9, false}},
    {"offensiveLightningModifier", {kDamage, 8, false}},
    {"offensivePhysicalModifier", {kDamage, 2, false}},
    {"offensivePierceModifier", {kDamage, 4, false}},
    {"offensivePoisonModifier", {kDamage, 7, false}},
    {"offensiveSlowAetherModifier", {kDamage, 11, true}},
    {"offensiveSlowBleedingModifier", {kDamage, 15, true}},
    {"offensiveSlowChaosModifier", {kDamage, 10, true}},
    {"offensiveSlowColdModifier", {kDamage, 5, true}},
    {"offensiveSlowFireModifier", {kDamage, 6, true}},
    {"offensiveSlowLifeModifier", {kDamage, 9, true}},
    {"offensiveSlowLightningModifier", {kDamage, 8, true}},
    {"offensiveSlowPhysicalModifier", {kDamage, 2, true}},
    {"offensiveSlowPoisonModifier", {kDamage, 7, true}},
    {"offensiveTotalDamageModifier", {kDamage, 60, false}},
};

constexpr FlatDamage kFlat[] = {
    {"offensivePhysicalMin", "offensivePhysicalMax", "", "offensivePhysicalChance", 2},
    {"offensiveBonusPhysicalMin", "offensiveBonusPhysicalMax", "", "offensiveBonusPhysicalChance", 2},
    {"offensiveSlowPhysicalMin", "offensiveSlowPhysicalMax", "offensiveSlowPhysicalDurationMin", "offensiveSlowPhysicalChance", 2},
    {"offensivePierceMin", "offensivePierceMax", "", "offensivePierceChance", 4},
    {"offensiveBaseColdMin", "offensiveBaseColdMax", "", "offensiveBaseColdChance", 5},
    {"offensiveColdMin", "offensiveColdMax", "", "offensiveColdChance", 5},
    {"offensiveSlowColdMin", "offensiveSlowColdMax", "offensiveSlowColdDurationMin", "offensiveSlowColdChance", 5},
    {"offensiveBaseFireMin", "offensiveBaseFireMax", "", "offensiveBaseFireChance", 6},
    {"offensiveFireMin", "offensiveFireMax", "", "offensiveFireChance", 6},
    {"offensiveSlowFireMin", "offensiveSlowFireMax", "offensiveSlowFireDurationMin", "offensiveSlowFireChance", 6},
    {"offensiveBasePoisonMin", "offensiveBasePoisonMax", "", "offensiveBasePoisonChance", 7},
    {"offensivePoisonMin", "offensivePoisonMax", "", "offensivePoisonChance", 7},
    {"offensiveSlowPoisonMin", "offensiveSlowPoisonMax", "offensiveSlowPoisonDurationMin", "offensiveSlowPoisonChance", 7},
    {"offensiveBaseLightningMin", "offensiveBaseLightningMax", "", "offensiveBaseLightningChance", 8},
    {"offensiveLightningMin", "offensiveLightningMax", "", "offensiveLightningChance", 8},
    {"offensiveSlowLightningMin", "offensiveSlowLightningMax", "offensiveSlowLightningDurationMin", "offensiveSlowLightningChance", 8},
    {"offensiveBaseLifeMin", "offensiveBaseLifeMax", "", "offensiveBaseLifeChance", 9},
    {"offensiveLifeMin", "offensiveLifeMax", "", "offensiveLifeChance", 9},
    {"offensiveSlowLifeMin", "offensiveSlowLifeMax", "offensiveSlowLifeDurationMin", "offensiveSlowLifeChance", 9},
    {"offensiveBaseChaosMin", "offensiveBaseChaosMax", "", "offensiveBaseChaosChance", 10},
    {"offensiveChaosMin", "offensiveChaosMax", "", "offensiveChaosChance", 10},
    {"offensiveSlowChaosMin", "offensiveSlowChaosMax", "offensiveSlowChaosDurationMin", "offensiveSlowChaosChance", 10},
    {"offensiveBaseAetherMin", "offensiveBaseAetherMax", "", "offensiveBaseAetherChance", 11},
    {"offensiveAetherMin", "offensiveAetherMax", "", "offensiveAetherChance", 11},
    {"offensiveSlowAetherMin", "offensiveSlowAetherMax", "offensiveSlowAetherDurationMin", "offensiveSlowAetherChance", 11},
    {"offensiveSlowBleedingMin", "offensiveSlowBleedingMax", "offensiveSlowBleedingDurationMin", "offensiveSlowBleedingChance", 15},
    {"offensiveElementalMin", "offensiveElementalMax", "", "offensiveElementalChance", 58},
};

constexpr int kAllDirect[] = {2, 4, 5, 6, 7, 8, 9, 10, 11};
constexpr int kAllDot[] = {2, 5, 6, 7, 8, 9, 10, 11, 15};
constexpr int kElemental[] = {5, 6, 8};

}  // namespace

const Target* damage_modifier(std::string_view field) {
  const auto it = std::lower_bound(std::begin(kDamageMods), std::end(kDamageMods), field,
                                   [](const auto& e, std::string_view f) { return e.first < f; });
  return it != std::end(kDamageMods) && it->first == field ? &it->second : nullptr;
}

std::span<const std::pair<std::string_view, Target>> damage_modifiers() { return kDamageMods; }

const Target* lookup(std::string_view field, double& scale) {
  static constexpr Target kCrit{Kind::Damage, 59, false};
  // target(), damage_modifier(), then crit; hashed, hot per skill level.
  static const std::unordered_map<std::string_view, std::pair<const Target*, double>> index = [] {
    std::unordered_map<std::string_view, std::pair<const Target*, double>> m;
    for (const auto& [name, t] : kTable) m.emplace(name, std::pair{&t, 1.0});
    for (const auto& [name, t] : kDamageMods) m.emplace(name, std::pair{&t, 1.0});
    m.emplace("offensiveCritDamageModifier", std::pair{&kCrit, 0.01});
    return m;
  }();
  const auto it = index.find(field);
  scale = it != index.end() ? it->second.second : 1;
  return it != index.end() ? it->second.first : nullptr;
}

std::span<const int> spread(int type, bool dot) {
  if (type == 60) return dot ? std::span<const int>(kAllDot) : std::span<const int>(kAllDirect);
  if (type == 58) return kElemental;
  return {};
}

std::span<const FlatDamage> flat_damage() { return kFlat; }

namespace {
constexpr std::string_view kWeaponFlags[] = {
    "Sword", "Axe", "Mace", "Dagger", "Scepter", "Axe2h", "Sword2h", "Mace2h", "Spear2h", "Spear", "Staff",
    "Magical", "Shield", "Offhand", "Ranged1h", "Ranged2h", "dualWieldOnly", "dualRangedOnly",
    "dualRangedOrRanged2hOnly", "dualRangedOrAllRangedOnly", "shieldNoRanged", "unarmedOnly"};
}  // namespace

bool weapon_flag(std::string_view field) {
  return std::find(std::begin(kWeaponFlags), std::end(kWeaponFlags), field) != std::end(kWeaponFlags);
}

bool weapon_gated(const arz::Record& skill) {
  return std::any_of(std::begin(kWeaponFlags), std::end(kWeaponFlags), [&](std::string_view f) { return skill.number(f) != 0; });
}

bool flat_damage_field(std::string_view field) {
  return !field.empty() && std::any_of(std::begin(kFlat), std::end(kFlat), [&](const FlatDamage& f) {
    return field == f.min || field == f.max || field == f.duration || field == f.chance;
  });
}

const Target* target(std::string_view field) {
  const auto it = std::lower_bound(std::begin(kTable), std::end(kTable), field,
                                   [](const auto& e, std::string_view f) { return e.first < f; });
  return it != std::end(kTable) && it->first == field ? &it->second : nullptr;
}

std::span<const std::pair<std::string_view, Target>> all() { return kTable; }

}  // namespace gdc::fields
