#pragma once
// Shared by adapter snapshot sources only.
#include <windows.h>

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "snapshot.h"

namespace gdc::adapter {

using u32 = std::uint32_t;
using u64 = std::uint64_t;

// Item vtable slots, checked in init.
constexpr std::size_t kSlotChar = 0x498, kSlotSkill = 0x4A0, kSlotDamage = 0x4A8, kSlotOffMod = 0x4B0,
                      kSlotRetaliation = 0x4B8, kSlotRetMod = 0x4C0, kSlotDefense = 0x4C8, kSlotConversion = 0x4D0;
// Scale 1.0, as CalculateRetaliationValues 0x3CDE50 passes.
constexpr float kDamageScale = 1.0f;
constexpr std::size_t kReplica = 0x538;
constexpr std::size_t kAccBytes = 0x1000;  // CharAttributeAccumulator writes up to +0x338
constexpr std::size_t kMaxVector = 4096;
constexpr int kDamageDirect = 1, kDamageDuration = 2;
// CombatAttributeDurDamage: the ctor (0x113E10) sets +0x24 and +0x28 to the duration; Process (0x113F30) applies the
// duration % to +0x24 only, GetDuration (0x1142A0) returns +0x28.
constexpr std::size_t kDurProcessed = 0x24;  // CombatAttributeAbsDamage / DurDamage::GetDamageType
// CollectWeaponDamage's inline values: chance stats at expected value.
constexpr bool kDisplayModifyProcs = true;
constexpr std::size_t kSlotGetValues = 0x110;  // CombatAttributeDamage_BasePhysical vftable 0x7358E8
constexpr std::size_t kPthTable = 0x7A8;
// Patched chance multiplier: raw, excluded and effective chance.
constexpr std::size_t kDisplayVftSlots = 8, kSlotChanceMultiplier = 0x18, kGlobalChance = 0x88;
// Skill's slots are the `ret` stub; Skill_Passive's (0x711BA0) override.
constexpr std::size_t kSlotSkillPassiveChar = 0x408, kSlotSkillPassiveDefense = 0x438;
constexpr int kAbsorptionProtection = 38;  // CombatAttributeDefense_AbsorptionProtection::GetType (0x1182E0)
// Skill+0x140: Skill::GetModifiers 0x3C5970, AddModifier 0x48FD00, AddModifierDefenseAttributes 0x494970.
constexpr std::size_t kSkillModifiers = 0x140;
// Hit animation, called as Player::CalculateSkillDps does.
constexpr std::size_t kSlotHitCallbackPoint = 0x6E0;
// DurationDamageManager::GetFixedDamage, called through vt+0x10 by Skill::CollectCombatParameters 0x499570.
constexpr std::size_t kSlotFixedDamage = 0x10;
// GetItemType, slot 0x550 of the Object-subobject vftable.
constexpr std::size_t kSlotItemType = 0x550;
// Requirement getter slots and reduction ids, as AreRequirementsMet 0x343C30.
constexpr std::size_t kSlotReqLevel = 0x528, kSlotReqAttr[3] = {0x530, 0x538, 0x540};
constexpr std::size_t kReqReduction[3][3] = {{0xDF8, 0xDEC, 0xE04}, {0xDFC, 0xDF0, 0xE08}, {0xE00, 0xDF4, 0xE0C}};
// PetPen list layout, as KillExtraPets 0x509890 walks it.
constexpr std::size_t kPetList = 8, kPetNodeId = 0x10, kPetNodeSkill = 0x14, kPetNodeReleased = 0x1C, kMaxPets = 64;
// Save folder flags read by GetPlayerFolder 0x2E2FC8.
constexpr std::size_t kBackupFolder = 0x4BDD, kMainFolder = 0x4B48;
enum Pass { kExpected, kRaw, kExcluded, kProbe, kPasses };

struct Api {
  const void* const* engine_var = nullptr;
  void* (*main_player)(const void*) = nullptr;
  const char16_t* (*player_name)(const void*) = nullptr;
  int (*difficulty)(const void*) = nullptr;
  u32 (*difficulty_ramp)(const void*) = nullptr;  // optional: null reads as 0
  // Optional, any null leaves character.save empty.
  bool (*cloud_storage)(const void*) = nullptr;
  const void* const* engine2_var = nullptr;  // gEngine
  void* (*game_info)(const void*) = nullptr;  // lea rax, [rcx+0x430]: reads nothing
  u32 (*game_mode)(const void*) = nullptr;
  // Steamworks flat API from the game's loaded steam_api64.dll.
  void* (*steam_user)() = nullptr;
  u64 (*steam_id)(void*) = nullptr;
  void* (*om_get)() = nullptr;
  const char* (*object_name)(const void*) = nullptr;
  u32 (*object_id)(const void*) = nullptr;
  void* (*object_from_id)(void*, u32) = nullptr;
  const void *ci_item = nullptr, *ci_equipment = nullptr, *ci_artifact = nullptr, *ci_relic = nullptr,
             *ci_enchantment = nullptr;

  float (*total_char_attr)(const void*, int) = nullptr;
  float (*total_char_mod)(const void*, int) = nullptr;
  float (*base_char_attr)(const void*, int) = nullptr;
  u32 (*points_spent)(const void*, int) = nullptr;
  float (*life_limit)(const void*) = nullptr;
  float (*mana_limit)(const void*) = nullptr;
  float (*attack_speed)(void*) = nullptr;
  float (*cast_speed)(void*) = nullptr;
  u32 (*char_level)(const void*) = nullptr;
  u32 (*skill_points)(const void*) = nullptr;
  u32 (*modifier_points)(const void*) = nullptr;
  u32 (*devotion_points)(const void*) = nullptr;
  u32 (*total_devotion_points)(const void*) = nullptr;
  void (*all_defense)(const void*, void*) = nullptr;
  const void* (*attached_items)(const void*) = nullptr;
  const void* (*inventory_items)(const void*) = nullptr;
  const void* (*skill_manager)(const void*) = nullptr;
  void (*collect_offensive_modifiers)(const void*, void*, u32) = nullptr;
  void (*collect_conversions)(const void*, void*) = nullptr;
  void (*collect_skill_attributes)(const void*, void*) = nullptr;
  float (*run_speed)(void*, bool) = nullptr;
  float (*defense_cap)(const void*) = nullptr;

  const void* (*skill_list)(const void*) = nullptr;
  const void* (*item_skill_list)(const void*) = nullptr;
  u32 (*item_skill_item_id)(const void*) = nullptr;
  u32 (*devotion_spent)(const void*) = nullptr;
  u32 (*skill_level)(const void*) = nullptr;
  u32 (*augmented_level)(const void*) = nullptr;
  u32 (*current_level)(const void*) = nullptr;
  u32 (*devotion_level)(const void*) = nullptr;
  u32 (*devotion_max_level)(const void*) = nullptr;
  u32 (*mastery_id)(const void*) = nullptr;
  int (*skill_type)(const void*) = nullptr;
  bool (*buff_active)(const void*, const std::string&) = nullptr;  // Character::IsSkillBuffActive
  const std::string& (*buff_skill_name)(const void*) = nullptr;   // Skill::GetBuffSkillName
  const void *ci_spell = nullptr, *ci_buff_self = nullptr;  // SkillActivatedSpell / SkillActivatedBuffSelf
  // (skill, Character&, Name& out, frames, frame rate): 0x51A310 / 0x20B40.
  bool (*hit_callback_point)(const void*, void*, u32*, u32*, int*) = nullptr;
  // Optional (snapshot schema 12): all four or no pets.
  const void* (*pet_pen)(const void*) = nullptr;
  const void *ci_character = nullptr, *ci_activated = nullptr;
  u32 (*default_skill_id)(const void*, int) = nullptr;  // SkillManager::GetDefaultSkillId(DefaultSkill)
  bool pets_ok = false;
  // Optional (snapshot schema 15): (player, skill id, std::string& name, SkillAttackInfo&, float& dps, main hand,
  // off hand, 0), as Player::CalculateDps 0x3DFA10 (1.3.1.2) calls it.
  void (*skill_dps)(void*, u32, void*, void*, float*, void*, void*, u32) = nullptr;
  const void* dur_duration = nullptr;  // CombatAttributeDurDamage::GetDuration, vt+0xF0 of its family
  bool skill_dps_ok = false;
  // Optional (schema 16): the reductions on the character; GetFixedDamage is vt+0x10 of DurationDamageManager.
  void* (*dur_mgr)(void*) = nullptr;
  float (*fixed_damage)(const void*, int) = nullptr;
  const void* dur_mgr_vft = nullptr;
  bool reduction_ok = false;

  void* (*char_acc_ctor)(void*) = nullptr;
  void (*char_acc_dtor)(void*) = nullptr;
  void* (*combat_acc_ctor)(void*, bool) = nullptr;
  void (*combat_acc_dtor)(void*) = nullptr;
  const void* display_vft = nullptr;
  float (*chance_mult)(void*, float) = nullptr;  // CombatDisplayAccumulator::GetChanceMultiplier
  const void* base_physical_vft = nullptr;
  void (*base_physical_values)(const void*, float&, float&, float&, float&, float&) = nullptr;
  float (*total_defense)(void*, int) = nullptr;
  float (*total_defense_mod)(void*, int) = nullptr;
  float (*total_reduction)(void*, int) = nullptr;
  float (*total_damage_mod)(void*, void*, int, int) = nullptr;
  void (*process_defense)(void*, const void*, const void*, const void*, float) = nullptr;
  const void* abs_defense_process = nullptr;
  void* (*skill_acc_ctor)(void*) = nullptr;
  void (*skill_acc_dtor)(void*) = nullptr;
  void* (*conv_acc_ctor)(void*) = nullptr;
  void (*conv_acc_dtor)(void*) = nullptr;

  const void* skill_passive_vft = nullptr;
  const void* skill_passive_char = nullptr;
  const void* skill_passive_defense = nullptr;
  const void* ci_skill_modifier = nullptr;
  void (*mod_damage)(const void*, void*, float) = nullptr;
  void (*mod_offensive_modifiers)(const void*, void*) = nullptr;
  void (*mod_skill_attributes)(const void*, void*) = nullptr;
  void (*mod_conversions)(const void*, void*) = nullptr;

  const void* (*item_set_name)(const void*) = nullptr;  // returns const std::string&
  void* (*get_relic)(const void*) = nullptr;
  void* (*get_enchantment)(const void*) = nullptr;
  bool (*requirements_met)(const void*, const void*) = nullptr;  // ItemEquipment::AreRequirementsMet(Character const*)
  bool requirements_ok = false;
  bool item_type_ok = false;
};

extern Api api;
extern bool api_ready;
// Patched vtable copy: qword before the vftable, 8 slots; vptr->[1].
extern const void* pass_vft[kPasses][kDisplayVftSlots + 1];

inline std::uintptr_t addr(const void* p) { return reinterpret_cast<std::uintptr_t>(p); }
template <class T>
T rd(const void* p, std::size_t off = 0) {
  T v;
  std::memcpy(&v, static_cast<const char*>(p) + off, sizeof v);
  return v;
}

inline std::string bounded_cstr(const char* s) {
  if (!s) return {};
  return std::string(s, strnlen(s, 1024));
}

// assign keeps the buffer; the capture reuses recycled snapshots.
inline void set_cstr(std::string& out, const char* s) {
  if (!s) return out.clear();
  out.assign(s, strnlen(s, 1024));
}

inline void set_msvc_string(std::string& out, const void* p) {
  const auto size = rd<std::uint64_t>(p, 0x10), cap = rd<std::uint64_t>(p, 0x18);
  if (size > cap || size > 4096) return void(out = "<bad string>");
  const char* data = cap >= 16 ? rd<const char*>(p, 0) : static_cast<const char*>(p);
  out.assign(data, size);
}

struct Span {
  const char* begin;
  std::size_t count;
};
inline Span vec(const void* v, std::size_t elem) {
  const char* b = rd<const char*>(v, 0);
  const char* e = rd<const char*>(v, 8);
  if (!b || e < b || (e - b) % elem != 0 || static_cast<std::size_t>(e - b) / elem > kMaxVector) return {nullptr, 0};
  return {b, static_cast<std::size_t>(e - b) / elem};
}

inline const void* class_info(const void* obj) {
  using Fn = const void* (*)(const void*);
  return rd<Fn>(rd<const void*>(obj), 0)(obj);
}

inline bool is_a(const void* ci, const void* target) {
  for (int depth = 0; ci && depth < 32; ++depth, ci = rd<const void*>(ci, 0x10))
    if (ci == target) return true;
  return false;
}

inline const char* kind_of(const void* obj) {
  const void* ci = class_info(obj);
  if (is_a(ci, api.ci_equipment)) return "ItemEquipment";
  if (is_a(ci, api.ci_relic)) return "ItemRelic";
  if (is_a(ci, api.ci_enchantment)) return "ItemEnchantment";
  if (is_a(ci, api.ci_artifact)) return "ItemArtifact";
  if (is_a(ci, api.ci_item)) return "Item";
  return "";
}

inline void* lookup(u32 id) {
  void* om = api.om_get();
  return om ? api.object_from_id(om, id) : nullptr;
}

// GetRelic / GetEnchantment read +0x1558 / +0x1560 (0x3429B0 / 0x343260).
inline u64 fingerprint_of(const void* obj) {
  if (!obj || !is_a(class_info(obj), api.ci_equipment)) return 0;
  u32 relic = 0, augment = 0;
  if (const void* r = api.get_relic(obj)) relic = api.object_id(r);
  if (const void* e = api.get_enchantment(obj)) augment = api.object_id(e);
  return (static_cast<u64>(relic) << 32) | augment;
}

using AccFn = void (*)(const void*, void*);
inline AccFn slot(const void* obj, std::size_t off) { return rd<AccFn>(rd<const void*>(obj), off); }

struct alignas(16) AccBuf {
  unsigned char bytes[kAccBytes];
};

// One buffer each: game-thread captures never re-enter.
struct Scratch {
  std::vector<int> types;
  std::vector<snap::CombatTotals> combat[kPasses];
  std::vector<snap::Damage> damage[kPasses];
  std::vector<std::pair<int, float>> cap_bonus;
  std::vector<u32> tips;
};
extern Scratch scratch;

// Each section runs under SEH (guarded()).

struct Ctx {
  snap::Snapshot& s;
  snap::Spares& spare;
  void* engine;
  void* player;
  const std::vector<u32>& tooltip_ids;
  Timing& timing;
};

// snapshot_accumulators.cpp: reads engine accumulators into snapshot rows.
using DamageFn = void (*)(const void*, void*, float);
void combat_totals(void* acc, void* player, std::vector<snap::CombatTotals>& out);
float mult_raw(void*, float);
float mult_excluded(void* acc, float chance);
float mult_probe(void* acc, float chance);
void item_combat(const void* item, std::size_t slot_off, void* player, std::vector<snap::CombatTotals>& out);
void item_damage(const void* obj, DamageFn fn, std::vector<snap::Damage>& result);
// The +0x08 damage entries; `processed`: entries after Process, a DoT's duration with its duration %.
void damage_list(const void* acc, std::vector<snap::Damage>& out, bool processed = false);
DamageFn damage_slot(const void* obj, std::size_t off);
void skill_attributes(const void* obj, AccFn fn, std::vector<snap::SkillAttribute>& out);
void conversions(const void* obj, AccFn fn, std::vector<snap::Conversion>& out);
void char_contributions(const void* obj, AccFn fn, std::vector<snap::CharContribution>& out);
void modifier_stats(const void* mod, void* player, snap::ItemStats& s);

// snapshot_items.cpp
void read_item(const void* obj, snap::Item& it, bool with_stats, void* player);
void sec_equipped(Ctx& c);
void sec_tooltip(Ctx& c);
void sec_inventory(Ctx& c);

// snapshot_character.cpp
void sec_character(Ctx& c);
void sec_defense(Ctx& c);
void sec_defense_processed(Ctx& c);
void sec_damage_modifiers(Ctx& c);
void sec_conversions(Ctx& c);
void sec_skill_attributes(Ctx& c);

// snapshot_skills.cpp
void sec_skills(Ctx& c);
void sec_item_skills(Ctx& c);
void sec_item_skill_modifiers(Ctx& c);

void sec_skill_dps(Ctx& c);

// snapshot_pets.cpp
void sec_pets(Ctx& c);

}  // namespace gdc::adapter
