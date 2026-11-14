#include "gdc/hook_targets.h"

#include <cstdio>
#include <unordered_map>

namespace gdc::targets {
namespace {

// Required: the comparison block cannot work without it → mod disables itself if missing.
// Optional: diagnostics or fallbacks; missing → logged only.
// ItemRelic/ItemEnchantment getters are optional until Phase 2 shows whether the
// ItemEquipment getters already include component/augment contributions.
// Trailing comments: RVA in v1.3.1.1 (re/*.exports.tsv), for reference only; never used.
constexpr Target kPlanned[] = {
    {Module::Engine, Kind::Call, false, "version", "Engine::GetVersion",
     "?GetVersion@Engine@GAME@@SAPEBDXZ"},  // 0x79cd0
    {Module::Game, Kind::Data, true, "engine", "gGameEngine",
     "?gGameEngine@GAME@@3PEAVGameEngine@1@EA"},  // 0xa66288
    {Module::Game, Kind::Call, true, "engine", "GameEngine::GetMainPlayer",
     "?GetMainPlayer@GameEngine@GAME@@QEBAPEAVPlayer@2@XZ"},  // 0x2ebb70
    // Snapshot character.name / auto-snapshot folder: returns the u16string's data (Player+0x46C8, 0x3D3100).
    {Module::Game, Kind::Call, true, "engine", "Player::GetPlayerName", "?GetPlayerName@Player@GAME@@QEBAPEBGXZ"},  // 0x3d3100
    {Module::Game, Kind::Call, true, "engine", "GameEngine::GetGameDifficulty",
     "?GetGameDifficulty@GameEngine@GAME@@QEBA?AW4GameDifficulty@2@XZ"},  // 0x2d8490
    {Module::Game, Kind::Detour, true, "tooltip", "ItemEquipment::GetUIDisplayText",
     "?GetUIDisplayText@ItemEquipment@GAME@@UEBAXPEBVCharacter@2@AEAV?$vector@UGameTextLine@GAME@@@mem@@_N@Z"},  // 0x33dce0
    {Module::Game, Kind::Detour, true, "tooltip", "ItemArtifact::GetUIDisplayText",
     "?GetUIDisplayText@ItemArtifact@GAME@@UEBAXPEBVCharacter@2@AEAV?$vector@UGameTextLine@GAME@@@mem@@_N@Z"},  // 0x32a570
    // Tooltip lines are appended with the engine's own (unexported) push_back, found through
    // ItemEquipment::GetUIDisplayText (gdc::scan::line_push_back_call). GameTextLineToString (0x316EB0) is
    // never called or referenced in Game.dll 1.3.1.1, so it is not a draw hook.
    // Item::GenerateSearchText calls GetUIDisplayText (vt+0x458, call at +0xB8) for stash search text: the
    // tooltip detours skip that caller.
    {Module::Game, Kind::Call, false, "tooltip", "Item::GenerateSearchText", "?GenerateSearchText@Item@GAME@@QEAAXXZ"},  // 0x329370
    {Module::Game, Kind::Call, false, "tooltip", "GameTextLine::GameTextLine(u16string)",
     "??0GameTextLine@GAME@@QEAA@W4GameTextClass@1@AEBV?$basic_string@GU?$char_traits@G@std@@V?$allocator@G@2@@std@@_NPEBVGraphicsTexture@1@M@Z"},  // 0x313030
    // Item::GetItemType is virtual slot 0x550 of the Object-subobject vftable (adapter tooltip, checked in
    // snapshot_init against these: each class's slot is a folded `mov eax, imm; ret` stub).
    {Module::Game, Kind::Call, false, "tooltip", "Item::GetItemType", "?GetItemType@Item@GAME@@UEBA?BW4Item_Type@2@XZ"},  // 0x167e0
    {Module::Game, Kind::Call, false, "tooltip", "ArmorJewelry_Ring::GetItemType",
     "?GetItemType@ArmorJewelry_Ring@GAME@@UEBA?BW4Item_Type@2@XZ"},  // 0x2de70
    {Module::Game, Kind::Data, false, "tooltip", "ArmorJewelry_Ring::vftable{Object}",
     "??_7ArmorJewelry_Ring@GAME@@6BObject@1@@"},  // 0x6d7658
    {Module::Game, Kind::Call, true, "item", "ItemEquipment::GetCharAttributes",
     "?GetCharAttributes@ItemEquipment@GAME@@UEBAXAEAVCharAttributeAccumulator@2@@Z"},  // 0x340830
    {Module::Game, Kind::Call, true, "item", "ItemEquipment::GetDefenseAttributes",
     "?GetDefenseAttributes@ItemEquipment@GAME@@UEBAXAEAVCombatAttributeAccumulator@2@@Z"},  // 0x340c30
    {Module::Game, Kind::Call, true, "item", "ItemEquipment::GetOffensiveDamageAttributes",
     "?GetOffensiveDamageAttributes@ItemEquipment@GAME@@UEBAXAEAVCombatAttributeAccumulator@2@M@Z"},  // 0x340950
    {Module::Game, Kind::Call, true, "item", "ItemEquipment::GetOffensiveModifierAttributes",
     "?GetOffensiveModifierAttributes@ItemEquipment@GAME@@UEBAXAEAVCombatAttributeAccumulator@2@@Z"},  // 0x340a40
    {Module::Game, Kind::Call, true, "item", "ItemEquipment::GetRetaliationAttributes",
     "?GetRetaliationAttributes@ItemEquipment@GAME@@UEBAXAEAVCombatAttributeAccumulator@2@M@Z"},  // 0x340ac0
    {Module::Game, Kind::Call, true, "item", "ItemEquipment::GetRetaliationModifierAttributes",
     "?GetRetaliationModifierAttributes@ItemEquipment@GAME@@UEBAXAEAVCombatAttributeAccumulator@2@@Z"},  // 0x340bb0
    {Module::Game, Kind::Call, true, "item", "ItemEquipment::GetConversionAttributes",
     "?GetConversionAttributes@ItemEquipment@GAME@@UEBAXAEAVConversionAttributeAccumulator@2@@Z"},  // 0x340cb0
    {Module::Game, Kind::Call, true, "item", "ItemEquipment::GetSkillAttributes",
     "?GetSkillAttributes@ItemEquipment@GAME@@UEBAXAEAVSkillAttributeAccumulator@2@@Z"},  // 0x3408d0
    {Module::Game, Kind::Call, true, "item", "ItemArtifact::GetCharAttributes",
     "?GetCharAttributes@ItemArtifact@GAME@@UEBAXAEAVCharAttributeAccumulator@2@@Z"},  // 0x32ba20
    {Module::Game, Kind::Call, true, "item", "ItemArtifact::GetDefenseAttributes",
     "?GetDefenseAttributes@ItemArtifact@GAME@@UEBAXAEAVCombatAttributeAccumulator@2@@Z"},  // 0x32bcf0
    {Module::Game, Kind::Call, true, "item", "ItemArtifact::GetOffensiveDamageAttributes",
     "?GetOffensiveDamageAttributes@ItemArtifact@GAME@@UEBAXAEAVCombatAttributeAccumulator@2@M@Z"},  // 0x32bad0
    {Module::Game, Kind::Call, true, "item", "ItemArtifact::GetOffensiveModifierAttributes",
     "?GetOffensiveModifierAttributes@ItemArtifact@GAME@@UEBAXAEAVCombatAttributeAccumulator@2@@Z"},  // 0x32bb80
    {Module::Game, Kind::Call, true, "item", "ItemArtifact::GetRetaliationAttributes",
     "?GetRetaliationAttributes@ItemArtifact@GAME@@UEBAXAEAVCombatAttributeAccumulator@2@M@Z"},  // 0x32bbe0
    {Module::Game, Kind::Call, true, "item", "ItemArtifact::GetRetaliationModifierAttributes",
     "?GetRetaliationModifierAttributes@ItemArtifact@GAME@@UEBAXAEAVCombatAttributeAccumulator@2@@Z"},  // 0x32bc90
    {Module::Game, Kind::Call, true, "item", "ItemArtifact::GetConversionAttributes",
     "?GetConversionAttributes@ItemArtifact@GAME@@UEBAXAEAVConversionAttributeAccumulator@2@@Z"},  // 0x32bd50
    {Module::Game, Kind::Call, true, "item", "ItemArtifact::GetSkillAttributes",
     "?GetSkillAttributes@ItemArtifact@GAME@@UEBAXAEAVSkillAttributeAccumulator@2@@Z"},  // 0x32ba80
    {Module::Game, Kind::Call, false, "item", "ItemRelic::GetCharAttributes",
     "?GetCharAttributes@ItemRelic@GAME@@UEBAXAEAVCharAttributeAccumulator@2@@Z"},  // 0x349c00
    {Module::Game, Kind::Call, false, "item", "ItemRelic::GetDefenseAttributes",
     "?GetDefenseAttributes@ItemRelic@GAME@@UEBAXAEAVCombatAttributeAccumulator@2@@Z"},  // 0x349d30
    {Module::Game, Kind::Call, false, "item", "ItemRelic::GetOffensiveDamageAttributes",
     "?GetOffensiveDamageAttributes@ItemRelic@GAME@@UEBAXIAEAVCombatAttributeAccumulator@2@M@Z"},  // 0x349c50
    {Module::Game, Kind::Call, false, "item", "ItemRelic::GetOffensiveModifierAttributes",
     "?GetOffensiveModifierAttributes@ItemRelic@GAME@@UEBAXAEAVCombatAttributeAccumulator@2@@Z"},  // 0x349c90
    {Module::Game, Kind::Call, false, "item", "ItemRelic::GetRetaliationAttributes",
     "?GetRetaliationAttributes@ItemRelic@GAME@@UEBAXIAEAVCombatAttributeAccumulator@2@M@Z"},  // 0x349cc0
    {Module::Game, Kind::Call, false, "item", "ItemRelic::GetRetaliationModifierAttributes",
     "?GetRetaliationModifierAttributes@ItemRelic@GAME@@UEBAXAEAVCombatAttributeAccumulator@2@@Z"},  // 0x349d00
    {Module::Game, Kind::Call, false, "item", "ItemRelic::GetConversionAttributes",
     "?GetConversionAttributes@ItemRelic@GAME@@UEBAXAEAVConversionAttributeAccumulator@2@@Z"},  // 0x349e00
    {Module::Game, Kind::Call, false, "item", "ItemRelic::GetSkillAttributes",
     "?GetSkillAttributes@ItemRelic@GAME@@UEBAXAEAVSkillAttributeAccumulator@2@@Z"},  // 0x349c30
    {Module::Game, Kind::Call, false, "item", "ItemEnchantment::GetCharAttributes",
     "?GetCharAttributes@ItemEnchantment@GAME@@UEBAXAEAVCharAttributeAccumulator@2@@Z"},  // 0x33af90
    {Module::Game, Kind::Call, false, "item", "ItemEnchantment::GetDefenseAttributes",
     "?GetDefenseAttributes@ItemEnchantment@GAME@@UEBAXAEAVCombatAttributeAccumulator@2@@Z"},  // 0x33b0c0
    {Module::Game, Kind::Call, false, "item", "ItemEnchantment::GetOffensiveDamageAttributes",
     "?GetOffensiveDamageAttributes@ItemEnchantment@GAME@@UEBAXIAEAVCombatAttributeAccumulator@2@M@Z"},  // 0x33afe0
    {Module::Game, Kind::Call, false, "item", "ItemEnchantment::GetOffensiveModifierAttributes",
     "?GetOffensiveModifierAttributes@ItemEnchantment@GAME@@UEBAXAEAVCombatAttributeAccumulator@2@@Z"},  // 0x33b020
    {Module::Game, Kind::Call, false, "item", "ItemEnchantment::GetRetaliationAttributes",
     "?GetRetaliationAttributes@ItemEnchantment@GAME@@UEBAXIAEAVCombatAttributeAccumulator@2@M@Z"},  // 0x33b050
    {Module::Game, Kind::Call, false, "item", "ItemEnchantment::GetRetaliationModifierAttributes",
     "?GetRetaliationModifierAttributes@ItemEnchantment@GAME@@UEBAXAEAVCombatAttributeAccumulator@2@@Z"},  // 0x33b090
    {Module::Game, Kind::Call, false, "item", "ItemEnchantment::GetConversionAttributes",
     "?GetConversionAttributes@ItemEnchantment@GAME@@UEBAXAEAVConversionAttributeAccumulator@2@@Z"},  // 0x33b0f0
    {Module::Game, Kind::Call, false, "item", "ItemEnchantment::GetSkillAttributes",
     "?GetSkillAttributes@ItemEnchantment@GAME@@UEBAXAEAVSkillAttributeAccumulator@2@@Z"},  // 0x33afc0
    {Module::Game, Kind::Call, true, "item", "ItemEquipment::GetLevelRequirement",
     "?GetLevelRequirement@ItemEquipment@GAME@@UEBAIXZ"},  // 0x343ab0
    {Module::Game, Kind::Call, true, "item", "ItemEquipment::GetStrengthRequirement",
     "?GetStrengthRequirement@ItemEquipment@GAME@@UEBAIXZ"},  // 0x343b10
    {Module::Game, Kind::Call, true, "item", "ItemEquipment::GetDexterityRequirement",
     "?GetDexterityRequirement@ItemEquipment@GAME@@UEBAIXZ"},  // 0x343b70
    {Module::Game, Kind::Call, true, "item", "ItemEquipment::GetIntelligenceRequirement",
     "?GetIntelligenceRequirement@ItemEquipment@GAME@@UEBAIXZ"},  // 0x343bd0
    {Module::Game, Kind::Call, true, "item", "ItemEquipment::AreRequirementsMet",
     "?AreRequirementsMet@ItemEquipment@GAME@@UEBA_NPEBVCharacter@2@@Z"},  // 0x343c30
    {Module::Game, Kind::Call, true, "item", "ItemEquipment::GetItemSetName",
     "?GetItemSetName@ItemEquipment@GAME@@QEBAAEBV?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@XZ"},  // 0x344820
    {Module::Game, Kind::Call, true, "item", "ItemEquipment::GetRelic",
     "?GetRelic@ItemEquipment@GAME@@QEBAPEAVItemRelic@2@XZ"},  // 0x3429b0
    {Module::Game, Kind::Call, true, "item", "ItemEquipment::GetEnchantment",
     "?GetEnchantment@ItemEquipment@GAME@@QEBAPEAVItemEnchantment@2@XZ"},  // 0x343260
    {Module::Game, Kind::Call, false, "item", "ItemEquipment::HasPetBonus",
     "?HasPetBonus@ItemEquipment@GAME@@UEBA_NXZ"},  // 0x340e80
    {Module::Game, Kind::Call, true, "accumulator", "CharAttributeAccumulator::CharAttributeAccumulator()",
     "??0CharAttributeAccumulator@GAME@@QEAA@XZ"},  // 0xcf5c0
    {Module::Game, Kind::Call, true, "accumulator", "CharAttributeAccumulator::~CharAttributeAccumulator()",
     "??1CharAttributeAccumulator@GAME@@QEAA@XZ"},  // 0xcf660
    // Deterministic CombatAttributeAccumulator: EvaluateChance always true, chance attributes scaled by
    // chance/100. With `true` it has the flags Skill::CollectWeaponDamage builds inline for the sheet.
    {Module::Game, Kind::Call, true, "accumulator", "CombatDisplayAccumulator::CombatDisplayAccumulator(bool)",
     "??0CombatDisplayAccumulator@GAME@@QEAA@_N@Z"},  // 0x11b980
    {Module::Game, Kind::Call, true, "accumulator", "CombatDisplayAccumulator::~CombatDisplayAccumulator()",
     "??1CombatDisplayAccumulator@GAME@@UEAA@XZ"},  // 0x11b9f0
    // Copied (8 slots, up to CombatAttributeAccumulator::vftable at 0x6d3d38) with slot 0x18 replaced, to
    // read chance-based item stats at 100% / 0% / chance (adapter snapshot.cpp, chance passes).
    {Module::Game, Kind::Data, true, "accumulator", "CombatDisplayAccumulator::vftable",
     "??_7CombatDisplayAccumulator@GAME@@6B@"},  // 0x6d3cf8
    {Module::Game, Kind::Call, true, "accumulator", "CombatDisplayAccumulator::GetChanceMultiplier",
     "?GetChanceMultiplier@CombatDisplayAccumulator@GAME@@UEAAMM@Z"},  // 0x11ba10
    {Module::Game, Kind::Data, true, "accumulator", "CombatAttributeDamage_BasePhysical::vftable",
     "??_7CombatAttributeDamage_BasePhysical@GAME@@6B@"},  // 0x7358e8
    {Module::Game, Kind::Call, true, "accumulator", "CombatAttributeDamage_BasePhysical::GetValues",
     "?GetValues@CombatAttributeDamage_BasePhysical@GAME@@UEAAXAEAM0000@Z"},  // 0x1177b0
    {Module::Game, Kind::Call, true, "accumulator", "ConversionAttributeAccumulator::ConversionAttributeAccumulator()",
     "??0ConversionAttributeAccumulator@GAME@@QEAA@XZ"},  // 0x19fc0
    {Module::Game, Kind::Call, true, "accumulator", "ConversionAttributeAccumulator::~ConversionAttributeAccumulator()",
     "??1ConversionAttributeAccumulator@GAME@@QEAA@XZ"},  // 0x17d6f0
    {Module::Game, Kind::Call, true, "accumulator", "SkillAttributeAccumulator::SkillAttributeAccumulator()",
     "??0SkillAttributeAccumulator@GAME@@QEAA@XZ"},  // 0x51f0b0
    {Module::Game, Kind::Call, true, "accumulator", "SkillAttributeAccumulator::~SkillAttributeAccumulator()",
     "??1SkillAttributeAccumulator@GAME@@UEAA@XZ"},  // 0x51f0f0
    {Module::Game, Kind::Call, true, "character", "Character::GetTotalCharAttribute",
     "?GetTotalCharAttribute@Character@GAME@@QEBAMW4CharAttributeType@2@@Z"},  // 0x69560
    {Module::Game, Kind::Call, true, "character", "Character::GetTotalCharModifier",
     "?GetTotalCharModifier@Character@GAME@@QEBAMW4CharAttributeType@2@@Z"},  // 0x69570
    {Module::Game, Kind::Call, true, "character", "Character::GetBaseCharAttribute",
     "?GetBaseCharAttribute@Character@GAME@@QEBAMW4CharAttributeType@2@@Z"},  // 0x69580
    {Module::Game, Kind::Call, true, "character", "Character::GetPointsSpent",
     "?GetPointsSpent@Character@GAME@@QEBA?BIW4CharAttributeType@2@@Z"},  // 0x609a0
    {Module::Game, Kind::Call, true, "character", "Character::GetLifeLimit",
     "?GetLifeLimit@Character@GAME@@QEBA?BMXZ"},  // 0x6b200
    {Module::Game, Kind::Call, true, "character", "Character::GetManaLimit",
     "?GetManaLimit@Character@GAME@@QEBA?BMXZ"},  // 0x6b150
    {Module::Game, Kind::Call, false, "character", "Character::GetLifeRegenValue",
     "?GetLifeRegenValue@Character@GAME@@QEBAMMMM@Z"},  // 0x69890
    {Module::Game, Kind::Call, false, "character", "Character::GetManaRegenValue",
     "?GetManaRegenValue@Character@GAME@@QEBAMMMMM@Z"},  // 0x69900
    {Module::Game, Kind::Call, true, "character", "Character::GetAttackSpeed",
     "?GetAttackSpeed@Character@GAME@@QEAA?BMXZ"},  // 0x692e0
    {Module::Game, Kind::Call, true, "character", "Character::GetSpellCastSpeed",
     "?GetSpellCastSpeed@Character@GAME@@QEAA?BMXZ"},  // 0x693a0
    {Module::Game, Kind::Call, true, "character", "Character::GetRunSpeed",
     "?GetRunSpeed@Character@GAME@@QEAA?BM_N@Z"},  // 0x69040
    {Module::Game, Kind::Call, true, "character", "Character::GetAllDefenseAttributes",
     "?GetAllDefenseAttributes@Character@GAME@@QEBAXAEAVCombatAttributeAccumulator@2@@Z"},  // 0x69d90
    {Module::Game, Kind::Call, false, "character", "Character::GetPhysicalDamageReductionPercent",
     "?GetPhysicalDamageReductionPercent@Character@GAME@@QEBAMXZ"},  // 0x69450
    {Module::Game, Kind::Call, false, "character", "Character::GetElementalDamageReductionPercent",
     "?GetElementalDamageReductionPercent@Character@GAME@@QEBAMXZ"},  // 0x69470
    {Module::Game, Kind::Call, false, "character", "Character::GetTotalDamageReductionPercent",
     "?GetTotalDamageReductionPercent@Character@GAME@@QEBAMXZ"},  // 0x69490
    {Module::Game, Kind::Call, false, "character", "Character::GetTotalDamageReductionAbsolute",
     "?GetTotalDamageReductionAbsolute@Character@GAME@@QEBAMXZ"},  // 0x694b0
    {Module::Game, Kind::Call, true, "character", "Character::GetEquipCharAttributes",
     "?GetEquipCharAttributes@Character@GAME@@QEBAXAEAVCharAttributeAccumulator@2@@Z"},  // 0x695e0
    {Module::Game, Kind::Call, true, "character", "Character::GetEquipSkillAttributes",
     "?GetEquipSkillAttributes@Character@GAME@@QEBAXAEAVSkillAttributeAccumulator@2@@Z"},  // 0x69910
    {Module::Game, Kind::Call, true, "character", "Character::GetEquipOffensiveModifierAttributes",
     "?GetEquipOffensiveModifierAttributes@Character@GAME@@QEBAXAEAVCombatAttributeAccumulator@2@@Z"},  // 0x69ac0
    {Module::Game, Kind::Call, true, "character", "Character::GetEquipConversionAttributes",
     "?GetEquipConversionAttributes@Character@GAME@@QEBAXAEAVConversionAttributeAccumulator@2@@Z"},  // 0x69ce0
    {Module::Game, Kind::Call, true, "character", "Character::GetEquipRetaliationAttributes",
     "?GetEquipRetaliationAttributes@Character@GAME@@QEBAXAEAVCombatAttributeAccumulator@2@M@Z"},  // 0x69b70
    {Module::Game, Kind::Call, true, "character", "Character::GetEquipRetaliationModifierAttributes",
     "?GetEquipRetaliationModifierAttributes@Character@GAME@@QEBAXAEAVCombatAttributeAccumulator@2@@Z"},  // 0x69c30
    {Module::Game, Kind::Call, true, "character", "Character::GetEquipArmorDamageAttributes",
     "?GetEquipArmorDamageAttributes@Character@GAME@@QEBAXAEAVCombatAttributeAccumulator@2@M@Z"},  // 0x699c0
    {Module::Game, Kind::Call, true, "character", "Character::GetSkillCharAttributes",
     "?GetSkillCharAttributes@Character@GAME@@QEBAXAEAVCharAttributeAccumulator@2@@Z"},  // 0x69780
    {Module::Game, Kind::Call, true, "character", "Character::GetBaseCharAttributes",
     "?GetBaseCharAttributes@Character@GAME@@QEBAXAEAVCharAttributeAccumulator@2@@Z"},  // 0x69590
    {Module::Game, Kind::Call, true, "character", "Character::GetCharLevel",
     "?GetCharLevel@Character@GAME@@QEBA?BIXZ"},  // 0x60610
    {Module::Game, Kind::Call, true, "character", "Character::GetSkillPoints",
     "?GetSkillPoints@Character@GAME@@QEBA?BIXZ"},  // 0x608c0
    {Module::Game, Kind::Call, true, "character", "Character::GetModifierPoints",
     "?GetModifierPoints@Character@GAME@@QEBA?BIXZ"},  // 0x60760
    {Module::Game, Kind::Call, true, "character", "Character::GetDevotionPoints",
     "?GetDevotionPoints@Character@GAME@@QEBA?BIXZ"},  // 0x60770
    {Module::Game, Kind::Call, true, "character", "Character::GetTotalDevotionPoints",
     "?GetTotalDevotionPoints@Character@GAME@@QEBA?BIXZ"},  // 0x60980
    {Module::Game, Kind::Call, true, "character", "Character::GetSkillMasteries",
     "?GetSkillMasteries@Character@GAME@@QEBAXAEAV?$vector@I@mem@@@Z"},  // 0x6bf30
    {Module::Game, Kind::Call, true, "character", "Character::GetNumberOfItemsInSet",
     "?GetNumberOfItemsInSet@Character@GAME@@QEBAHAEBV?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@@Z"},  // 0x6a4c0
    {Module::Game, Kind::Call, false, "character", "Character::GetItemsInSet",
     "?GetItemsInSet@Character@GAME@@QEBA?AV?$vector@V?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@@mem@@AEBV?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@@Z"},  // 0x6a5c0
    {Module::Game, Kind::Call, true, "character", "Character::GetAttachedItems",
     "?GetAttachedItems@Character@GAME@@QEBAAEBV?$vector@VEquipManagerContainer@GAME@@@mem@@XZ"},  // 0x63b90
    {Module::Game, Kind::Call, true, "character", "Character::GetSkillManager",
     "?GetSkillManager@Character@GAME@@QEBAAEBVSkillManager@2@XZ"},  // 0x6da90
    {Module::Game, Kind::Call, true, "character", "Character::GetCombatManager",
     "?GetCombatManager@Character@GAME@@QEBAAEBVCombatManager@2@XZ"},  // 0x463f0
    {Module::Game, Kind::Call, true, "character", "Player::GetDefenseAttributeCap",
     "?GetDefenseAttributeCap@Player@GAME@@UEBAMXZ"},  // 0x3e59f0
    {Module::Game, Kind::Call, true, "character", "Player::GetPlayerCharacterClass",
     "?GetPlayerCharacterClass@Player@GAME@@QEBA?AW4PlayerCharacterClassType@2@XZ"},  // 0x3e13e0
    {Module::Game, Kind::Call, true, "equipment", "CombatManager::GetEquipManager",
     "?GetEquipManager@CombatManager@GAME@@QEBAAEBVEquipManager@2@XZ"},  // 0x8b9a0
    {Module::Game, Kind::Call, true, "equipment", "EquipManager::GetItemId",
     "?GetItemId@EquipManager@GAME@@QEBA?BIW4EquipLocations@2@@Z"},  // 0x2847d0
    {Module::Game, Kind::Call, true, "equipment", "EquipManager::GetItemLocation",
     "?GetItemLocation@EquipManager@GAME@@QEBA?BW4EquipLocations@2@I@Z"},  // 0x284860
    {Module::Game, Kind::Call, true, "equipment", "EquipManager::GetItemAlternate",
     "?GetItemAlternate@EquipManager@GAME@@QEBA?B_NI@Z"},  // 0x2848f0
    {Module::Game, Kind::Call, true, "equipment", "EquipManager::GetHandState",
     "?GetHandState@EquipManager@GAME@@QEBA?BW4EquipManager_HandState@2@XZ"},  // 0x285e50
    {Module::Game, Kind::Call, true, "equipment", "EquipManager::GetWeaponIdLeft",
     "?GetWeaponIdLeft@EquipManager@GAME@@QEBAIXZ"},  // 0x284720
    {Module::Game, Kind::Call, true, "equipment", "EquipManager::GetWeaponIdRight",
     "?GetWeaponIdRight@EquipManager@GAME@@QEBAIXZ"},  // 0x284670
    {Module::Game, Kind::Call, false, "equipment", "EquipManager::LocationToString",
     "?LocationToString@EquipManager@GAME@@AEBA?AV?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@W4EquipLocations@2@@Z"},  // 0x286480
    // Phase 2: snapshots (docs/SNAPSHOT.md).
    {Module::Game, Kind::Detour, true, "tick", "GameEngine::Update",
     "?Update@GameEngine@GAME@@QEAAXH@Z"},  // 0x2d54b0
    {Module::Engine, Kind::Call, true, "object", "Singleton<ObjectManager>::Get",
     "?Get@?$Singleton@VObjectManager@GAME@@@GAME@@SAPEAVObjectManager@2@XZ"},  // 0x1c040
    {Module::Engine, Kind::Call, true, "object", "Object::GetObjectName",
     "?GetObjectName@Object@GAME@@QEBAPEBDXZ"},  // 0x3a760
    {Module::Engine, Kind::Call, true, "object", "Object::GetObjectId",
     "?GetObjectId@Object@GAME@@QEBAIXZ"},  // 0x3a740
    {Module::Game, Kind::Data, true, "object", "Item::classInfo",
     "?classInfo@Item@GAME@@1VRTTI_ClassInfo@2@B"},  // 0xa5c8a0
    {Module::Game, Kind::Data, true, "object", "ItemEquipment::classInfo",
     "?classInfo@ItemEquipment@GAME@@1VRTTI_ClassInfo@2@B"},  // 0xa5c840
    {Module::Game, Kind::Data, true, "object", "ItemArtifact::classInfo",
     "?classInfo@ItemArtifact@GAME@@1VRTTI_ClassInfo@2@B"},  // 0xa5bd90
    {Module::Game, Kind::Data, true, "object", "ItemRelic::classInfo",
     "?classInfo@ItemRelic@GAME@@1VRTTI_ClassInfo@2@B"},  // 0xa5c780
    {Module::Game, Kind::Data, true, "object", "ItemEnchantment::classInfo",
     "?classInfo@ItemEnchantment@GAME@@1VRTTI_ClassInfo@2@B"},  // 0xa5bf40
    {Module::Game, Kind::Data, true, "item", "ItemEquipment::vftable{Object}",
     "??_7ItemEquipment@GAME@@6BObject@1@@"},  // 0x76f718
    {Module::Game, Kind::Data, true, "item", "ItemArtifact::vftable{Object}",
     "??_7ItemArtifact@GAME@@6BObject@1@@"},  // 0x76ab48
    {Module::Game, Kind::Data, true, "item", "ItemRelic::vftable{Object}",
     "??_7ItemRelic@GAME@@6BObject@1@@"},  // 0x771180
    {Module::Game, Kind::Data, true, "item", "ItemEnchantment::vftable{Object}",
     "??_7ItemEnchantment@GAME@@6BObject@1@@"},  // 0x76ee60
    {Module::Game, Kind::Call, true, "accumulator", "CombatAttributeAccumulator::GetTotalDefenseType",
     "?GetTotalDefenseType@CombatAttributeAccumulator@GAME@@QEAAMW4CombatAttributeType@2@@Z"},  // 0x11a880
    {Module::Game, Kind::Call, true, "accumulator", "CombatAttributeAccumulator::GetTotalDefenseModifierType",
     "?GetTotalDefenseModifierType@CombatAttributeAccumulator@GAME@@QEAAMW4CombatAttributeType@2@@Z"},  // 0x11a980
    {Module::Game, Kind::Call, true, "accumulator", "CombatAttributeAccumulator::GetTotalReduction",
     "?GetTotalReduction@CombatAttributeAccumulator@GAME@@QEAAMW4CombatAttributeType@2@@Z"},  // 0x11aa10
    {Module::Game, Kind::Call, true, "accumulator", "CombatAttributeAccumulator::GetTotalDamageModifierType",
     "?GetTotalDamageModifierType@CombatAttributeAccumulator@GAME@@QEAAMPEAVCharacter@2@W4CombatAttributeType@2@W4DamageType@CombatAttribute@2@@Z"},  // 0x11a750
    {Module::Game, Kind::Call, true, "character", "Player::CollectDamageModifiers",
     "?CollectDamageModifiers@Player@GAME@@QEAAXAEAMW4CombatAttributeType@2@W4DamageType@CombatAttribute@2@@Z"},  // 0x3cd7e0
    {Module::Game, Kind::Call, true, "character", "SkillManager::CollectAvailableOffensiveModifierAttributes",
     "?CollectAvailableOffensiveModifierAttributes@SkillManager@GAME@@UEBAXAEAVCombatAttributeAccumulator@2@I@Z"},  // 0x536260
    {Module::Game, Kind::Call, true, "character", "SkillManager::CollectAvailableConversionAttributes",
     "?CollectAvailableConversionAttributes@SkillManager@GAME@@QEBAXAEAVConversionAttributeAccumulator@2@@Z"},  // 0x536400
    {Module::Game, Kind::Call, true, "accumulator", "CombatAttributeAccumulator::ProcessDefense",
     "?ProcessDefense@CombatAttributeAccumulator@GAME@@QEAAXAEBVCharacter@2@AEBUReductionInfo@2@AEBUDamageScaleInfo@2@M@Z"},  // 0x11ab40
    {Module::Game, Kind::Call, true, "accumulator", "CombatAttributeAbsDefense::Process",
     "?Process@CombatAttributeAbsDefense@GAME@@UEAAXAEBVCharacter@2@AEBUReductionInfo@2@AEBUDamageScaleInfo@2@M@Z"},  // 0x115370
    // Per-skill passive contributions (adapter sec_skills): slots 0x408 / 0x438 of every skill vftable,
    // checked against Skill_Passive's. Skill_BuffSelfToggled's overrides sit at the same slots
    // (0x707B80 / 0x707BB0 in its vftable at 0x707778).
    {Module::Game, Kind::Data, true, "skills", "Skill_Passive::vftable", "??_7Skill_Passive@GAME@@6B@"},  // 0x711ba0
    {Module::Game, Kind::Call, true, "skills", "Skill_Passive::CollectPassiveCharAttributes",
     "?CollectPassiveCharAttributes@Skill_Passive@GAME@@UEBAXAEAVCharAttributeAccumulator@2@@Z"},  // 0x5011d0
    {Module::Game, Kind::Call, true, "skills", "Skill_Passive::CollectPassiveDefenseAttributes",
     "?CollectPassiveDefenseAttributes@Skill_Passive@GAME@@UEBAXAEAVCombatAttributeAccumulator@2@@Z"},  // 0x4fb690
    {Module::Game, Kind::Data, true, "skills", "Skill_Modifier::classInfo",
     "?classInfo@Skill_Modifier@GAME@@1VRTTI_ClassInfo@2@B"},  // 0xa5a770
    {Module::Game, Kind::Call, true, "skills", "Skill_Modifier::CollectModifierOffensiveDamageAttributes",
     "?CollectModifierOffensiveDamageAttributes@Skill_Modifier@GAME@@UEBAXAEAVCombatAttributeAccumulator@2@M@Z"},  // 0x4fb320
    {Module::Game, Kind::Call, true, "skills", "Skill_Modifier::CollectModifierOffensiveModifierAttributes",
     "?CollectModifierOffensiveModifierAttributes@Skill_Modifier@GAME@@UEBAXAEAVCombatAttributeAccumulator@2@@Z"},  // 0x4fb3d0
    {Module::Game, Kind::Call, true, "skills", "Skill_Modifier::CollectModifierSkillAttributes",
     "?CollectModifierSkillAttributes@Skill_Modifier@GAME@@UEBAXAEAVSkillAttributeAccumulator@2@@Z"},  // 0x4fb2a0
    {Module::Game, Kind::Call, true, "skills", "Skill_Modifier::CollectModifierConversionAttributes",
     "?CollectModifierConversionAttributes@Skill_Modifier@GAME@@UEBAXAEAVConversionAttributeAccumulator@2@@Z"},  // 0x4fb710
    {Module::Game, Kind::Call, true, "character", "Character::GetInventoryItems",
     "?GetInventoryItems@Character@GAME@@QEBAAEBV?$vector@I@mem@@XZ"},  // 0x63b80
    {Module::Game, Kind::Call, true, "skills", "SkillManager::GetSkillList",
     "?GetSkillList@SkillManager@GAME@@QEBAAEBV?$vector@PEAVSkill@GAME@@@mem@@XZ"},  // 0x8ef90
    {Module::Game, Kind::Call, true, "skills", "SkillManager::GetItemSkillList",
     "?GetItemSkillList@SkillManager@GAME@@QEBAAEBV?$vector@PEAVSkill@GAME@@@mem@@XZ"},  // 0x531730
    {Module::Game, Kind::Call, true, "skills", "Skill::GetItemSkillItemId",
     "?GetItemSkillItemId@Skill@GAME@@QEBAIXZ"},  // 0x31d500
    {Module::Game, Kind::Call, true, "skills", "SkillManager::GetNumDevotionPointsSpent",
     "?GetNumDevotionPointsSpent@SkillManager@GAME@@QEBAIXZ"},  // 0x534d40
    {Module::Game, Kind::Call, true, "skills", "Skill::GetSkillLevel",
     "?GetSkillLevel@Skill@GAME@@QEBA?BIXZ"},  // 0x4909b0
    {Module::Game, Kind::Call, true, "skills", "Skill::GetAugmentedLevel",
     "?GetAugmentedLevel@Skill@GAME@@QEBA?BIXZ"},  // 0x490b30
    {Module::Game, Kind::Call, true, "skills", "Skill::GetCurrentLevel",
     "?GetCurrentLevel@Skill@GAME@@UEBA?BIXZ"},  // 0x4909c0
    {Module::Game, Kind::Call, true, "skills", "Skill::GetDevotionLevel",
     "?GetDevotionLevel@Skill@GAME@@QEBA?BIXZ"},  // 0x490b40
    {Module::Game, Kind::Call, true, "skills", "Skill::GetDevotionMaxLevel",
     "?GetDevotionMaxLevel@Skill@GAME@@QEBA?BIXZ"},  // 0x490b50
    {Module::Game, Kind::Call, true, "skills", "Skill::GetMasteryId",
     "?GetMasteryId@Skill@GAME@@QEBA?BIXZ"},  // 0x31e1e0
    {Module::Game, Kind::Call, true, "skills", "Skill::GetSkillType",
     "?GetSkillType@Skill@GAME@@QEBA?AW4Skill_Type@12@XZ"},  // 0x426730
};

std::unordered_map<std::string_view, const pe::Export*> by_name(std::span<const pe::Export> ex) {
  std::unordered_map<std::string_view, const pe::Export*> m;
  m.reserve(ex.size());
  for (const auto& e : ex) m.emplace(e.name, &e);
  return m;
}

std::unordered_map<std::uint32_t, std::uint32_t> alias_counts(std::span<const pe::Export> ex) {
  std::unordered_map<std::uint32_t, std::uint32_t> m;
  for (const auto& e : ex)
    if (!e.forwarded) ++m[e.rva];
  return m;
}

}  // namespace

std::span<const Target> planned() { return kPlanned; }

Report resolve(std::span<const Target> targets, std::span<const pe::Export> game,
               std::span<const pe::Export> engine) {
  const auto game_names = by_name(game), engine_names = by_name(engine);
  const auto game_alias = alias_counts(game), engine_alias = alias_counts(engine);

  Report r;
  r.items.reserve(targets.size());
  for (const auto& t : targets) {
    const bool is_game = t.module == Module::Game;
    const auto& names = is_game ? game_names : engine_names;
    const auto& alias = is_game ? game_alias : engine_alias;

    Resolution res{&t};
    if (auto it = names.find(t.symbol); it == names.end()) {
      res.problem = Problem::Missing;
    } else if (it->second->forwarded) {
      res.problem = Problem::Forwarded;
    } else {
      res.rva = it->second->rva;
      res.aliases = alias.at(res.rva);
      if (t.kind == Kind::Detour && res.aliases > 1) res.problem = Problem::Folded;
    }
    if (res.problem != Problem::None) ++(t.required ? r.required_failed : r.optional_failed);
    r.items.push_back(res);
  }
  return r;
}

std::string format(const Report& r, std::uint64_t game_base, std::uint64_t engine_base) {
  std::string out;
  char buf[160];
  for (const auto& it : r.items) {
    const Target& t = *it.target;
    const std::uint64_t base = t.module == Module::Game ? game_base : engine_base;
    std::snprintf(buf, sizeof buf, "[%-9s] %s %-6s %-11s ", std::string(to_string(it.problem)).c_str(),
                  t.required ? "req" : "opt", std::string(to_string(t.kind)).c_str(),
                  std::string(t.group).c_str());
    out += buf;
    if (it.problem == Problem::None || it.problem == Problem::Folded) {
      std::snprintf(buf, sizeof buf, "%s+0x%06X = 0x%016llX x%u  ", std::string(to_string(t.module)).c_str(),
                    it.rva, static_cast<unsigned long long>(base + it.rva), it.aliases);
      out += buf;
    } else {
      out += std::string(to_string(t.module)) + "  -  ";
    }
    out += t.label;
    if (it.problem != Problem::None) out += "  {" + std::string(t.symbol) + "}";
    out += '\n';
  }
  std::snprintf(buf, sizeof buf, "targets: %zu, required failed: %zu, optional failed: %zu\n", r.items.size(),
                r.required_failed, r.optional_failed);
  out += buf;
  return out;
}

std::string_view to_string(Module m) { return m == Module::Game ? "Game" : "Engine"; }

std::string_view to_string(Kind k) {
  switch (k) {
    case Kind::Call: return "call";
    case Kind::Detour: return "detour";
    case Kind::Data: return "data";
  }
  return "?";
}

std::string_view to_string(Problem p) {
  switch (p) {
    case Problem::None: return "ok";
    case Problem::Missing: return "MISSING";
    case Problem::Forwarded: return "FORWARDED";
    case Problem::Folded: return "FOLDED";
  }
  return "?";
}

}  // namespace gdc::targets
