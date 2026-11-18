#include "gdc/code_scan.h"
#include "log.h"
#include "snapshot_internal.h"

namespace gdc::adapter {
namespace {

template <class F>
bool bind(const Resolve& r, F& out, std::string_view label) {
  const void* p = r(label);
  if (!p) log::printf("snapshot: %.*s unresolved", static_cast<int>(label.size()), label.data());
  out = reinterpret_cast<F>(const_cast<void*>(p));
  return p != nullptr;
}

bool verify_slots(const Resolve& r, std::string_view cls, bool full) {
  const void* vft = r(std::string(cls) + "::vftable{Object}");
  if (!vft) return false;
  struct {
    std::size_t off;
    const char* fn;
    bool damage;
  } const slots[] = {{kSlotChar, "GetCharAttributes", false},
                     {kSlotSkill, "GetSkillAttributes", false},
                     {kSlotDamage, "GetOffensiveDamageAttributes", true},
                     {kSlotOffMod, "GetOffensiveModifierAttributes", false},
                     {kSlotRetaliation, "GetRetaliationAttributes", true},
                     {kSlotRetMod, "GetRetaliationModifierAttributes", false},
                     {kSlotDefense, "GetDefenseAttributes", false},
                     {kSlotConversion, "GetConversionAttributes", false}};
  bool ok = true;
  for (const auto& s : slots) {
    if (s.damage && !full) continue;  // components: stubs, never call
    const std::string label = std::string(cls) + "::" + s.fn;
    const void* want = r(label);
    const void* have = rd<const void*>(vft, s.off);
    if (!want && !full) continue;
    if (want != have) {
      log::printf("snapshot: %s vftable slot 0x%zX = 0x%016llX, expected %s", std::string(cls).c_str(), s.off,
                  static_cast<unsigned long long>(addr(have)), label.c_str());
      ok = false;
    }
  }
  return ok;
}

bool bind_player(const Resolve& r) {
  bool ok = true;
  auto b = [&](auto& out, std::string_view label) { ok = bind(r, out, label) && ok; };
  b(api.engine_var, "gGameEngine");
  b(api.main_player, "GameEngine::GetMainPlayer");
  b(api.player_name, "Player::GetPlayerName");
  b(api.difficulty, "GameEngine::GetGameDifficulty");
  return ok;
}

// Optional: a miss only leaves its snapshot field empty.
void bind_optional(const Resolve& r) {
  // A string literal would pick std::bind by ADL.
  bind(r, api.difficulty_ramp, std::string_view{"GameEngine::GetDifficultyRamp"});
  bind(r, api.cloud_storage, std::string_view{"GameEngine::GetCloudStorage"});
  bind(r, api.engine2_var, std::string_view{"gEngine"});
  bind(r, api.game_info, std::string_view{"Engine::GetGameInfo"});
  bind(r, api.game_mode, std::string_view{"GameInfo::GetMode"});
  if (HMODULE steam = GetModuleHandleW(L"steam_api64.dll")) {
    api.steam_user = reinterpret_cast<void* (*)()>(GetProcAddress(steam, "SteamAPI_SteamUser_v023"));
    api.steam_id = reinterpret_cast<u64 (*)(void*)>(GetProcAddress(steam, "SteamAPI_ISteamUser_GetSteamID"));
  }
  if (!api.steam_user || !api.steam_id) {
    api.steam_user = nullptr;
    log::write("snapshot: Steam user unresolved; cloud saves bind presets by name only");
  }
}

bool bind_character(const Resolve& r) {
  bool ok = true;
  auto b = [&](auto& out, std::string_view label) { ok = bind(r, out, label) && ok; };
  b(api.om_get, "Singleton<ObjectManager>::Get");
  b(api.object_name, "Object::GetObjectName");
  b(api.object_id, "Object::GetObjectId");
  b(api.ci_item, "Item::classInfo");
  b(api.ci_equipment, "ItemEquipment::classInfo");
  b(api.ci_artifact, "ItemArtifact::classInfo");
  b(api.ci_relic, "ItemRelic::classInfo");
  b(api.ci_enchantment, "ItemEnchantment::classInfo");
  b(api.total_char_attr, "Character::GetTotalCharAttribute");
  b(api.total_char_mod, "Character::GetTotalCharModifier");
  b(api.base_char_attr, "Character::GetBaseCharAttribute");
  b(api.points_spent, "Character::GetPointsSpent");
  b(api.life_limit, "Character::GetLifeLimit");
  b(api.mana_limit, "Character::GetManaLimit");
  b(api.attack_speed, "Character::GetAttackSpeed");
  b(api.cast_speed, "Character::GetSpellCastSpeed");
  b(api.char_level, "Character::GetCharLevel");
  b(api.skill_points, "Character::GetSkillPoints");
  b(api.modifier_points, "Character::GetModifierPoints");
  b(api.devotion_points, "Character::GetDevotionPoints");
  b(api.total_devotion_points, "Character::GetTotalDevotionPoints");
  b(api.all_defense, "Character::GetAllDefenseAttributes");
  b(api.attached_items, "Character::GetAttachedItems");
  b(api.inventory_items, "Character::GetInventoryItems");
  b(api.skill_manager, "Character::GetSkillManager");
  b(api.collect_offensive_modifiers, "SkillManager::CollectAvailableOffensiveModifierAttributes");
  b(api.collect_conversions, "SkillManager::CollectAvailableConversionAttributes");
  b(api.collect_skill_attributes, "SkillManager::CollectAvailableSkillAttributes");
  b(api.run_speed, "Character::GetRunSpeed");
  b(api.defense_cap, "Player::GetDefenseAttributeCap");
  b(api.skill_list, "SkillManager::GetSkillList");
  b(api.item_skill_list, "SkillManager::GetItemSkillList");
  b(api.item_skill_item_id, "Skill::GetItemSkillItemId");
  b(api.devotion_spent, "SkillManager::GetNumDevotionPointsSpent");
  b(api.skill_level, "Skill::GetSkillLevel");
  b(api.augmented_level, "Skill::GetAugmentedLevel");
  b(api.current_level, "Skill::GetCurrentLevel");
  b(api.devotion_level, "Skill::GetDevotionLevel");
  b(api.devotion_max_level, "Skill::GetDevotionMaxLevel");
  b(api.mastery_id, "Skill::GetMasteryId");
  b(api.skill_type, "Skill::GetSkillType");
  b(api.buff_active, "Character::IsSkillBuffActive");
  b(api.buff_skill_name, "Skill::GetBuffSkillName");
  b(api.ci_spell, "SkillActivatedSpell::classInfo");
  b(api.ci_buff_self, "SkillActivatedBuffSelf::classInfo");
  b(api.hit_callback_point, "SkillActivated::GetHitCallbackPoint");
  return ok;
}

// Optional: all four or no pets.
void bind_pets(const Resolve& r) {
  auto opt = [&](auto& out, std::string_view label) { return bind(r, out, label); };
  api.pets_ok = opt(api.pet_pen, "Character::GetPetPen") && opt(api.ci_character, "Character::classInfo") &&
                opt(api.ci_activated, "SkillActivated::classInfo") && opt(api.default_skill_id, "SkillManager::GetDefaultSkillId");
  if (!api.pets_ok) log::write("snapshot: pet getters unresolved; pets not read");
}

bool bind_accumulators(const Resolve& r) {
  bool ok = true;
  auto b = [&](auto& out, std::string_view label) { ok = bind(r, out, label) && ok; };
  b(api.char_acc_ctor, "CharAttributeAccumulator::CharAttributeAccumulator()");
  b(api.char_acc_dtor, "CharAttributeAccumulator::~CharAttributeAccumulator()");
  b(api.combat_acc_ctor, "CombatDisplayAccumulator::CombatDisplayAccumulator(bool)");
  b(api.combat_acc_dtor, "CombatDisplayAccumulator::~CombatDisplayAccumulator()");
  b(api.display_vft, "CombatDisplayAccumulator::vftable");
  b(api.chance_mult, "CombatDisplayAccumulator::GetChanceMultiplier");
  b(api.base_physical_vft, "CombatAttributeDamage_BasePhysical::vftable");
  b(api.base_physical_values, "CombatAttributeDamage_BasePhysical::GetValues");
  b(api.total_defense, "CombatAttributeAccumulator::GetTotalDefenseType");
  b(api.total_defense_mod, "CombatAttributeAccumulator::GetTotalDefenseModifierType");
  b(api.total_reduction, "CombatAttributeAccumulator::GetTotalReduction");
  b(api.total_damage_mod, "CombatAttributeAccumulator::GetTotalDamageModifierType");
  b(api.process_defense, "CombatAttributeAccumulator::ProcessDefense");
  b(api.abs_defense_process, "CombatAttributeAbsDefense::Process");
  b(api.skill_acc_ctor, "SkillAttributeAccumulator::SkillAttributeAccumulator()");
  b(api.skill_acc_dtor, "SkillAttributeAccumulator::~SkillAttributeAccumulator()");
  b(api.conv_acc_ctor, "ConversionAttributeAccumulator::ConversionAttributeAccumulator()");
  b(api.conv_acc_dtor, "ConversionAttributeAccumulator::~ConversionAttributeAccumulator()");
  b(api.skill_passive_vft, "Skill_Passive::vftable");
  b(api.skill_passive_char, "Skill_Passive::CollectPassiveCharAttributes");
  b(api.skill_passive_defense, "Skill_Passive::CollectPassiveDefenseAttributes");
  b(api.ci_skill_modifier, "Skill_Modifier::classInfo");
  b(api.mod_damage, "Skill_Modifier::CollectModifierOffensiveDamageAttributes");
  b(api.mod_offensive_modifiers, "Skill_Modifier::CollectModifierOffensiveModifierAttributes");
  b(api.mod_skill_attributes, "Skill_Modifier::CollectModifierSkillAttributes");
  b(api.mod_conversions, "Skill_Modifier::CollectModifierConversionAttributes");
  b(api.item_set_name, "ItemEquipment::GetItemSetName");
  b(api.get_relic, "ItemEquipment::GetRelic");
  b(api.get_enchantment, "ItemEquipment::GetEnchantment");
  b(api.requirements_met, "ItemEquipment::AreRequirementsMet");
  return ok;
}

// The unexported object-id lookup, found by gdc::scan.
bool bind_object_lookup(const Resolve& r) {
  // Accept only if it first calls Engine.dll's ObjectManager::Get IAT entry.
  const auto* anchor = static_cast<const std::byte*>(r("Character::GetEquipCharAttributes"));
  if (!anchor) {
    log::write("snapshot: Character::GetEquipCharAttributes unresolved");
    return false;
  }
  const auto call = scan::find_object_lookup_call({anchor, 0x100}, addr(anchor));
  if (!call) {
    log::write("snapshot: object lookup call not found in Character::GetEquipCharAttributes");
    return false;
  }
  const void* via_iat = rd<const void*>(reinterpret_cast<const void*>(call->iat_slot));
  if (via_iat != reinterpret_cast<const void*>(api.om_get)) {
    log::printf("snapshot: lookup call's IAT slot 0x%016llX holds 0x%016llX, not Singleton<ObjectManager>::Get",
                static_cast<unsigned long long>(call->iat_slot), static_cast<unsigned long long>(addr(via_iat)));
    return false;
  }
  api.object_from_id = reinterpret_cast<void* (*)(void*, u32)>(call->target);
  log::printf("snapshot: object lookup at 0x%016llX", static_cast<unsigned long long>(call->target));
  return true;
}

bool verify_vtables(const Resolve& r) {
  const bool ok = verify_slots(r, "ItemEquipment", true) && verify_slots(r, "ItemArtifact", true) &&
                  verify_slots(r, "ItemRelic", false) && verify_slots(r, "ItemEnchantment", false);
  if (!ok) return false;
  if (rd<const void*>(api.base_physical_vft, kSlotGetValues) != reinterpret_cast<const void*>(api.base_physical_values)) {
    log::write("snapshot: CombatAttributeDamage_BasePhysical vftable slot 0x110 is not GetValues");
    return false;
  }
  if (rd<const void*>(api.display_vft, kSlotChanceMultiplier) != reinterpret_cast<const void*>(api.chance_mult)) {
    log::write("snapshot: CombatDisplayAccumulator vftable slot 0x18 is not GetChanceMultiplier");
    return false;
  }
  if (rd<const void*>(api.skill_passive_vft, kSlotSkillPassiveChar) != api.skill_passive_char ||
      rd<const void*>(api.skill_passive_vft, kSlotSkillPassiveDefense) != api.skill_passive_defense) {
    log::write("snapshot: Skill_Passive vftable slots 0x408 / 0x438 are not its CollectPassive* overrides");
    return false;
  }
  return true;
}

// One display vtable copy per chance pass.
void build_pass_vtables() {
  float (*const mult[kPasses])(void*, float) = {nullptr, mult_raw, mult_excluded, mult_probe};
  for (int p = kRaw; p < kPasses; ++p) {
    pass_vft[p][0] = rd<const void*>(static_cast<const char*>(api.display_vft) - sizeof(void*));
    for (std::size_t i = 0; i < kDisplayVftSlots; ++i) pass_vft[p][1 + i] = rd<const void*>(api.display_vft, 8 * i);
    pass_vft[p][1 + kSlotChanceMultiplier / 8] = reinterpret_cast<const void*>(mult[p]);
  }
}

// Optional slots: a miss turns one feature off.
void verify_optional_slots(const Resolve& r) {
  const void* ring_vft = r("ArmorJewelry_Ring::vftable{Object}");
  const void* ring_type = r("ArmorJewelry_Ring::GetItemType");
  const void* item_type = r("Item::GetItemType");
  const void* eq_vft = r("ItemEquipment::vftable{Object}");
  api.item_type_ok = ring_vft && ring_type && item_type && rd<const void*>(ring_vft, kSlotItemType) == ring_type &&
                     rd<const void*>(eq_vft, kSlotItemType) == item_type;
  if (!api.item_type_ok) log::write("snapshot: Item::GetItemType slot 0x550 not verified; tooltip block off");
  const void* req[] = {r("ItemEquipment::GetLevelRequirement"), r("ItemEquipment::GetStrengthRequirement"),
                       r("ItemEquipment::GetDexterityRequirement"), r("ItemEquipment::GetIntelligenceRequirement")};
  const std::size_t slots[] = {kSlotReqLevel, kSlotReqAttr[0], kSlotReqAttr[1], kSlotReqAttr[2]};
  api.requirements_ok = eq_vft != nullptr;
  for (int i = 0; i < 4 && api.requirements_ok; ++i) api.requirements_ok = req[i] && rd<const void*>(eq_vft, slots[i]) == req[i];
  if (!api.requirements_ok) log::write("snapshot: requirement getter slots 0x528..0x540 not verified; requirements not read");
}

}  // namespace

bool snapshot_init(const Resolve& r) {
  bool ok = bind_player(r);
  bind_optional(r);
  ok = bind_character(r) && ok;
  bind_pets(r);
  ok = bind_accumulators(r) && ok;
  if (!ok || !bind_object_lookup(r) || !verify_vtables(r)) return false;
  build_pass_vtables();
  log::write("snapshot: item vtable slots verified");
  verify_optional_slots(r);
  api_ready = true;
  return true;
}

}  // namespace gdc::adapter
