// Runs on the game thread in the GameEngine::Update detour. Calls only exported getters, exported
// accumulator ctors / dtors on our own buffers, item getters through vtable slots checked against the exported
// vftables, and the unexported object-id lookup found by gdc::scan. Layout reads and their sources:
//   Character+0x5D0  mem::vector<EquipManagerContainer>: Character::GetAttachedItems (0x63B90)
//   EquipManagerContainer = 12 bytes {u32 id, EquipLocations, flags}: its ctor (0x288670) and
//                    EquipManager::GetItemId (0x2847D0, stride 12, location at +4)
//   mem::vector = {T* begin, T* end, T* cap}: EquipManager::GetItemId reads begin/end at +0/+8
//   Item+0x538       ItemReplicaInfo: Item::GetItemReplicaInfo (0x3244D0)
//   ItemReplicaInfo  string/u32 offsets: ItemReplicaInfo::StreamProperties (0x585840) / ReadProperties
//   std::string      MSVC x64 layout {buf[16] | ptr, size @0x10, capacity @0x18}
//   CharAttributeAccumulator values float[58] @0, modifiers float[58] @0xE8: AddValue / GetModifier
//   CombatAttributeAccumulator vectors of CombatAttribute* @0x08/0x20/0x38/0x50: its dtor/Clear
//   CombatAttribute vtable +0x10 = GetType: CombatAttributeAccumulator::GetTotalDefenseType
//   CombatAttribute vtable +0x20 GetDamageType, +0x78 GetMinMaxValues, +0xF0 GetDuration: the
//                    CombatAttributeAbsDamage / DurDamage vftables (0x7347D8 / 0x734568); the
//                    CombatAttribute base slots are stubs (ret / return 0), so any subclass is safe
//   CombatAttributeAbsDefense +0x2C = cap bonus: its ModifyDefenseCap (vt+0xA0, 0x113AE0) adds to it,
//                    its Process (0x115370) caps at Character::GetDefenseAttributeCap() + [+0x2C]
//   SkillAttributeAccumulator float[5] at +0x08 value / +0x1C chance value / +0x30 modifier /
//                    +0x44 chance modifier: AddValue / AddChanceValue / AddModifier / AddChanceModifier
//                    (0x51F160..0x51F190); ctor clears 0x50 bytes from +8
//   ConversionAttributeAccumulator = mem::vector<Data*>; Data {u32 from; mem::vector<{u32 to, float pct}>
//                    at +8}: ConversionAttributeAccumulator::AddAttribute (0x17D890)
//   Skill+0x158      mem::vector of 0x30-byte {u32 modifier id, EquipLocations, std::string set,
//                    bool relic @0x28, bool augment @0x29}: Skill::AddToItemSkillModifiers (0x48FD30),
//                    Skill::GetItemSkillModifiers (0x48FFD0)
//   Object vtable +0 = GetClassInfo, RTTI_ClassInfo+0x10 = parent: Character::GetEquipCharAttributes
//   CombatDisplayAccumulator(bool) writes up to +0x8B: its ctor (0x11B980)
//   CombatAttributeDamage_BasePhysical: its +8 type is DamageAttribute_BasePhysical::vt+0x28 = 0
//                    (AddDamageToAccumulator 0x1A0370), for which GetMinMaxValues (0x117500) writes
//                    nothing; GetValues (vt+0x110) returns {rolled, min, max, ?, ?} (+0x20/+0x24/+0x28/+0x48/+0x2C)
//   Character+0x7A8  CombatManager PTH table (gdc::snap::PthTable has the sources)
//   CombatDisplayAccumulator vftable: 8 slots (0x6D3CF8, CombatAttributeAccumulator's follows at
//                    0x6D3D38); +0x18 GetChanceMultiplier, +0x28 SetGlobalChance writes +0x88 (0x16A40);
//                    GetChanceMultiplier (0x11BA10) reads +0x88; the dtor (0x11B9F0) rewrites the vptr
//   CombatAttributeDefense_AbsorptionProtection +0x28: read by GetTotalReduction (0x1182F0), written
//                    only by Process (0x118370), never by the ctor (0x1182A0): undefined until processed
// RVAs: Game.dll 1.3.1.1.
#include "snapshot.h"

#include <windows.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "gdc/attribute_names.h"
#include "gdc/code_scan.h"
#include "log.h"
#include "seh.h"

namespace gdc::adapter {
namespace {

using u32 = std::uint32_t;
using u64 = std::uint64_t;

// Item slots in the Object-subobject vftable, as called by Character::GetEquipCharAttributes (*0x498),
// GetEquipOffensiveModifierAttributes (*0x4B0), GetEquipRetaliationModifierAttributes (*0x4C0),
// GetAllDefenseAttributes (*0x4C8); skill / damage / retaliation / conversion slots from the exported
// ItemEquipment vftable. ItemRelic / ItemEnchantment have stubs at 0x4A8 / 0x4B8. Checked in init.
constexpr std::size_t kSlotChar = 0x498, kSlotSkill = 0x4A0, kSlotDamage = 0x4A8, kSlotOffMod = 0x4B0,
                      kSlotRetaliation = 0x4B8, kSlotRetMod = 0x4C0, kSlotDefense = 0x4C8, kSlotConversion = 0x4D0;
// Scale 1.0, as Player::CalculateRetaliationValues 0x3CDE50 passes (const 0x7A8054);
// DamageAttributeAbsBaseElemental::AddDamageToAccumulator 0x18F530 multiplies min / max by it.
constexpr float kDamageScale = 1.0f;
constexpr std::size_t kReplica = 0x538;
constexpr std::size_t kAccBytes = 0x1000;  // CharAttributeAccumulator writes up to +0x338
constexpr std::size_t kMaxVector = 4096;
constexpr int kDamageDirect = 1, kDamageDuration = 2;  // CombatAttributeAbsDamage / DurDamage::GetDamageType
// Values Skill::CollectWeaponDamage writes inline (0x4991AB..0x499204): +0x84 ModifyProcs 1, +0x85 IncludeProcs 0,
// +0x88 GlobalChance 0. Then GetChanceMultiplier 0x11BA10 returns chance x 0.01, so chance stats count at
// their expected value instead of a roll.
constexpr bool kDisplayModifyProcs = true;
constexpr std::size_t kSlotGetValues = 0x110;  // CombatAttributeDamage_BasePhysical vftable 0x7358E8
constexpr std::size_t kPthTable = 0x7A8;
// AddDamageToAccumulator 0x18F530 multiplies by acc vt+0x18 (chance) and never stores the chance, so each item
// getter also runs with a patched vtable copy returning 1 (raw), 0 (excluded) and the effective chance
// (as 0x11BA45..0x11BA5B). Non-chance stats keep multiplier 1.
constexpr std::size_t kDisplayVftSlots = 8, kSlotChanceMultiplier = 0x18, kGlobalChance = 0x88;
// Skill's own entries are the shared `ret` stub (0x15880); Skill_Passive's (0x711BA0) hold the overrides.
constexpr std::size_t kSlotSkillPassiveChar = 0x408, kSlotSkillPassiveDefense = 0x438;
constexpr int kAbsorptionProtection = 38;  // CombatAttributeDefense_AbsorptionProtection::GetType (0x1182E0)
// Skill+0x140: Skill::GetModifiers 0x3C5970, AddModifier 0x48FD00, AddModifierDefenseAttributes 0x494970.
constexpr std::size_t kSkillModifiers = 0x140;
// Player::CalculateSkillDps calls vt+0x6E0 on an is-a SkillActivated (0x3E09A3) and uses the frames only for an
// is-a SkillActivatedSpell / SkillActivatedBuffSelf (0x3E0BEE..0x3E0CFD). Only objects whose slot holds the
// exported SkillActivated::GetHitCallbackPoint (0x51A310) are called.
constexpr std::size_t kSlotHitCallbackPoint = 0x6E0;
// ArmorJewelry_Ring::vftable{Object} 0x6D7658 + 0x550 = GetItemType 0x2DE70; ItemEquipment's (0x76F718) =
// Item::GetItemType 0x167E0.
constexpr std::size_t kSlotItemType = 0x550;
// ItemEquipment::AreRequirementsMet 0x343C30 calls vt+0x530 / 0x538 / 0x540 (Str / Dex / Int) and
// Item::AreRequirementsMet 0x326930 vt+0x528 (level); it reads the reduction CharAttributeType ids at these offsets
// (first always, the other two when non-zero).
constexpr std::size_t kSlotReqLevel = 0x528, kSlotReqAttr[3] = {0x530, 0x538, 0x540};
constexpr std::size_t kReqReduction[3][3] = {{0xDF8, 0xDEC, 0xE04}, {0xDFC, 0xDF0, 0xE08}, {0xE00, 0xDF4, 0xE0C}};
// Character::GetPetPen 0x7E100 (Character+0x2050); PetPen+8 is an MSVC std::list head (PetPen::GetPetOwner 0x3C9B00),
// node {next, prev, u32 pet id @0x10, u32 summoning skill id @0x14, ..., bool released @0x1C}:
// Skill_SpawnPet::KillExtraPets 0x509890.
constexpr std::size_t kPetList = 8, kPetNodeId = 0x10, kPetNodeSkill = 0x14, kPetNodeReleased = 0x1C, kMaxPets = 64;
// GameEngine::GetPlayerFolder(Player*) 0x2E2FC8 (1.3.1.2): a set byte at Player+0x4BDD picks MangleBackupPlayerName
// ("__" + name, rva 0x61815c) over ManglePlayerName ("_" + name, rva 0x6180ec) for the save folder.
constexpr std::size_t kBackupFolder = 0x4BDD;
enum Pass { kExpected, kRaw, kExcluded, kProbe, kPasses };

struct Api {
  const void* const* engine_var = nullptr;
  void* (*main_player)(const void*) = nullptr;
  const char16_t* (*player_name)(const void*) = nullptr;
  int (*difficulty)(const void*) = nullptr;
  u32 (*difficulty_ramp)(const void*) = nullptr;  // optional: null reads as 0
  bool (*cloud_storage)(const void*) = nullptr;   // optional: null leaves character.save empty
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
  // (skill, Character&, Name& out, frames out, frame rate out): 0x51A310 / 0x20B40.
  bool (*hit_callback_point)(const void*, void*, u32*, u32*, int*) = nullptr;
  // Optional (snapshot schema 12): all four or no pets.
  const void* (*pet_pen)(const void*) = nullptr;
  const void *ci_character = nullptr, *ci_activated = nullptr;
  u32 (*default_skill_id)(const void*, int) = nullptr;  // SkillManager::GetDefaultSkillId(DefaultSkill)
  bool pets_ok = false;

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

Api g;
bool g_ready = false;
// Patched vtable copy: the qword before the vftable, then its 8 slots; the vptr points at [1].
const void* g_pass_vft[kPasses][kDisplayVftSlots + 1] = {};

template <class F>
bool bind(const Resolve& r, F& out, std::string_view label) {
  const void* p = r(label);
  if (!p) log::printf("snapshot: %.*s unresolved", static_cast<int>(label.size()), label.data());
  out = reinterpret_cast<F>(const_cast<void*>(p));
  return p != nullptr;
}

std::uintptr_t addr(const void* p) { return reinterpret_cast<std::uintptr_t>(p); }
template <class T>
T rd(const void* p, std::size_t off = 0) {
  T v;
  std::memcpy(&v, static_cast<const char*>(p) + off, sizeof v);
  return v;
}

std::string bounded_cstr(const char* s) {
  if (!s) return {};
  return std::string(s, strnlen(s, 1024));
}

// assign keeps the buffer; the capture reuses recycled snapshots.
void set_cstr(std::string& out, const char* s) {
  if (!s) return out.clear();
  out.assign(s, strnlen(s, 1024));
}

void set_msvc_string(std::string& out, const void* p) {
  const auto size = rd<std::uint64_t>(p, 0x10), cap = rd<std::uint64_t>(p, 0x18);
  if (size > cap || size > 4096) return void(out = "<bad string>");
  const char* data = cap >= 16 ? rd<const char*>(p, 0) : static_cast<const char*>(p);
  out.assign(data, size);
}

struct Span {
  const char* begin;
  std::size_t count;
};
Span vec(const void* v, std::size_t elem) {
  const char* b = rd<const char*>(v, 0);
  const char* e = rd<const char*>(v, 8);
  if (!b || e < b || (e - b) % elem != 0 || static_cast<std::size_t>(e - b) / elem > kMaxVector) return {nullptr, 0};
  return {b, static_cast<std::size_t>(e - b) / elem};
}

const void* class_info(const void* obj) {
  using Fn = const void* (*)(const void*);
  return rd<Fn>(rd<const void*>(obj), 0)(obj);
}

bool is_a(const void* ci, const void* target) {
  for (int depth = 0; ci && depth < 32; ++depth, ci = rd<const void*>(ci, 0x10))
    if (ci == target) return true;
  return false;
}

const char* kind_of(const void* obj) {
  const void* ci = class_info(obj);
  if (is_a(ci, g.ci_equipment)) return "ItemEquipment";
  if (is_a(ci, g.ci_relic)) return "ItemRelic";
  if (is_a(ci, g.ci_enchantment)) return "ItemEnchantment";
  if (is_a(ci, g.ci_artifact)) return "ItemArtifact";
  if (is_a(ci, g.ci_item)) return "Item";
  return "";
}

void* lookup(u32 id) {
  void* om = g.om_get();
  return om ? g.object_from_id(om, id) : nullptr;
}

// GetRelic / GetEnchantment read +0x1558 / +0x1560 (0x3429B0 / 0x343260).
u64 fingerprint_of(const void* obj) {
  if (!obj || !is_a(class_info(obj), g.ci_equipment)) return 0;
  u32 relic = 0, augment = 0;
  if (const void* r = g.get_relic(obj)) relic = g.object_id(r);
  if (const void* e = g.get_enchantment(obj)) augment = g.object_id(e);
  return (static_cast<u64>(relic) << 32) | augment;
}

using AccFn = void (*)(const void*, void*);
AccFn slot(const void* obj, std::size_t off) { return rd<AccFn>(rd<const void*>(obj), off); }

struct alignas(16) AccBuf {
  unsigned char bytes[kAccBytes];
};

// One buffer each is safe: every capture runs on the game thread and none re-enters.
struct Scratch {
  std::vector<int> types;
  std::vector<snap::CombatTotals> combat[kPasses];
  std::vector<snap::Damage> damage[kPasses];
  std::vector<std::pair<int, float>> cap_bonus;
  std::vector<u32> tips;
};
Scratch g_scratch;

const std::vector<int>& combat_types(const void* acc) {
  std::vector<int>& types = g_scratch.types;
  types.clear();
  using GetType = int (*)(const void*);
  for (std::size_t off : {0x08u, 0x20u, 0x38u, 0x50u}) {
    const Span s = vec(static_cast<const char*>(acc) + off, sizeof(void*));
    for (std::size_t i = 0; i < s.count; ++i) {
      const void* attr = rd<const void*>(s.begin, i * sizeof(void*));
      if (!attr) continue;
      const int t = rd<GetType>(rd<const void*>(attr), 0x10)(attr);
      const auto at = std::lower_bound(types.begin(), types.end(), t);
      if (at == types.end() || *at != t) types.insert(at, t);
    }
  }
  return types;
}

void combat_totals(void* acc, void* player, std::vector<snap::CombatTotals>& out) {
  out.clear();
  for (int t : combat_types(acc)) {
    snap::CombatTotals c;
    c.type = t;
    c.defense = g.total_defense(acc, t);
    c.defense_modifier = g.total_defense_mod(acc, t);
    if (t != kAbsorptionProtection) c.reduction = g.total_reduction(acc, t);  // undefined until processed (see top)
    c.damage_modifier = g.total_damage_mod(acc, player, t, kDamageDirect);
    c.dot_modifier = g.total_damage_mod(acc, player, t, kDamageDuration);
    if (c.defense || c.defense_modifier || c.reduction || c.damage_modifier || c.dot_modifier) out.push_back(c);
  }
}

float mult_raw(void*, float) { return 1.0f; }
float mult_excluded(void* acc, float chance) { return g.chance_mult(acc, chance) == 1.0f ? 1.0f : 0.0f; }
float mult_probe(void* acc, float chance) {
  if (g.chance_mult(acc, chance) == 1.0f) return 1.0f;
  const float global = rd<float>(acc, kGlobalChance);
  return global > 0 ? global : chance;
}

void* display_acc(AccBuf& buf, Pass p) {
  void* acc = g.combat_acc_ctor(buf.bytes, kDisplayModifyProcs);
  if (p != kExpected) {
    const void* vptr = &g_pass_vft[p][1];
    std::memcpy(acc, &vptr, sizeof vptr);
  }
  return acc;
}

const snap::CombatTotals* find_type(const std::vector<snap::CombatTotals>& v, int t) {
  for (const auto& c : v)
    if (c.type == t) return &c;
  return nullptr;
}

template <class Fill>
void combat_with_procs(Fill fill, void* player, std::vector<snap::CombatTotals>& out) {
  auto& pass = g_scratch.combat;
  for (int p = kExpected; p < kPasses; ++p) {
    AccBuf buf;
    void* acc = display_acc(buf, static_cast<Pass>(p));
    fill(acc);
    combat_totals(acc, player, pass[p]);
    g.combat_acc_dtor(acc);
  }
  static constexpr struct {
    float snap::CombatTotals::* total;
    float snap::Proc::* proc;
  } fields[] = {{&snap::CombatTotals::defense, &snap::Proc::defense},
                {&snap::CombatTotals::defense_modifier, &snap::Proc::defense_modifier},
                {&snap::CombatTotals::reduction, &snap::Proc::reduction},
                {&snap::CombatTotals::damage_modifier, &snap::Proc::damage_modifier},
                {&snap::CombatTotals::dot_modifier, &snap::Proc::dot_modifier}};
  const snap::CombatTotals zero;
  for (snap::CombatTotals& row : pass[kExpected]) {
    const snap::CombatTotals* raw = find_type(pass[kRaw], row.type);
    if (!raw) continue;
    const snap::CombatTotals* ex = find_type(pass[kExcluded], row.type);
    const snap::CombatTotals* probe = find_type(pass[kProbe], row.type);
    if (!ex) ex = &zero;
    if (!probe) probe = &zero;
    snap::Proc proc;
    bool any = false;
    for (const auto& f : fields) {
      const float d = raw->*f.total - ex->*f.total;
      proc.*f.proc = d;
      if (d != 0 && !any) {
        any = true;
        proc.chance = (probe->*f.total - ex->*f.total) / d;
      }
    }
    if (any) row.proc = proc;
  }
  out.assign(pass[kExpected].begin(), pass[kExpected].end());
}

void item_combat(const void* item, std::size_t slot_off, void* player, std::vector<snap::CombatTotals>& out) {
  combat_with_procs([&](void* acc) { slot(item, slot_off)(item, acc); }, player, out);
}

void damage_list(const void* acc, std::vector<snap::Damage>& out) {
  using IntFn = int (*)(const void*);
  using MinMaxFn = void (*)(const void*, float&, float&);
  using FloatFn = float (*)(const void*);
  out.clear();
  const Span s = vec(static_cast<const char*>(acc) + 0x08, sizeof(void*));
  for (std::size_t i = 0; i < s.count; ++i) {
    const void* attr = rd<const void*>(s.begin, i * sizeof(void*));
    if (!attr) continue;
    const void* vt = rd<const void*>(attr);
    snap::Damage d;
    d.type = rd<IntFn>(vt, 0x10)(attr);
    d.dot = rd<IntFn>(vt, 0x20)(attr) == kDamageDuration;
    if (vt == g.base_physical_vft) {
      float rolled = 0, a = 0, b = 0;
      g.base_physical_values(attr, rolled, d.min, d.max, a, b);
    } else {
      rd<MinMaxFn>(vt, 0x78)(attr, d.min, d.max);
    }
    if (d.dot) d.duration = rd<FloatFn>(vt, 0xF0)(attr);
    out.push_back(d);
  }
}

using DamageFn = void (*)(const void*, void*, float);
// Entries line up across passes because every entry is created regardless of multiplier.
void item_damage(const void* obj, DamageFn fn, std::vector<snap::Damage>& result) {
  auto& pass = g_scratch.damage;
  for (int p = kExpected; p < kPasses; ++p) {
    AccBuf buf;
    void* acc = display_acc(buf, static_cast<Pass>(p));
    fn(obj, acc, kDamageScale);
    damage_list(acc, pass[p]);
    g.combat_acc_dtor(acc);
  }
  std::vector<snap::Damage>& out = pass[kExpected];
  bool aligned = true;
  for (int p = kRaw; p < kPasses; ++p) {
    aligned = aligned && pass[p].size() == out.size();
    for (std::size_t i = 0; aligned && i < out.size(); ++i)
      aligned = pass[p][i].type == out[i].type && pass[p][i].dot == out[i].dot;
  }
  if (!aligned) {
    log::printf("snapshot: %s: chance passes disagree (%zu/%zu/%zu/%zu entries), no chance data",
                bounded_cstr(g.object_name(obj)).c_str(), out.size(), pass[kRaw].size(), pass[kExcluded].size(),
                pass[kProbe].size());
  } else {
    for (std::size_t i = 0; i < out.size(); ++i) {
      const snap::Damage &raw = pass[kRaw][i], &ex = pass[kExcluded][i], &probe = pass[kProbe][i];
      const float dmax = raw.max - ex.max, dmin = raw.min - ex.min;
      if (dmax == 0 && dmin == 0) continue;
      out[i].chance = dmax != 0 ? (probe.max - ex.max) / dmax : (probe.min - ex.min) / dmin;
      out[i].raw_min = raw.min;
      out[i].raw_max = raw.max;
    }
  }
  result.clear();
  for (const snap::Damage& d : out)
    if (d.min != 0 || d.max != 0 || d.raw_max != 0) result.push_back(d);
}

void skill_attributes(const void* obj, AccFn fn, std::vector<snap::SkillAttribute>& out) {
  out.clear();
  AccBuf buf;
  void* acc = g.skill_acc_ctor(buf.bytes);
  fn(obj, acc);
  for (int t = 1; t <= names::kSkillAttributeMax; ++t) {
    const snap::SkillAttribute a{t, rd<float>(acc, 0x08 + 4 * t), rd<float>(acc, 0x1C + 4 * t),
                                 rd<float>(acc, 0x30 + 4 * t), rd<float>(acc, 0x44 + 4 * t)};
    if (a.value != 0 || a.chance_value != 0 || a.modifier != 0 || a.chance_modifier != 0) out.push_back(a);
  }
  g.skill_acc_dtor(acc);
}

void conversions(const void* obj, AccFn fn, std::vector<snap::Conversion>& out) {
  out.clear();
  AccBuf buf;
  void* acc = g.conv_acc_ctor(buf.bytes);
  fn(obj, acc);
  const Span from = vec(acc, sizeof(void*));
  for (std::size_t i = 0; i < from.count; ++i) {
    const char* d = rd<const char*>(from.begin, i * sizeof(void*));
    if (!d) continue;
    const Span to = vec(d + 8, 8);
    for (std::size_t k = 0; k < to.count; ++k)
      out.push_back({rd<int>(d, 0), rd<int>(to.begin, k * 8), rd<float>(to.begin, k * 8 + 4)});
  }
  g.conv_acc_dtor(acc);
}

DamageFn damage_slot(const void* obj, std::size_t off) { return rd<DamageFn>(rd<const void*>(obj), off); }

void char_contributions(const void* obj, AccFn fn, std::vector<snap::CharContribution>& out) {
  out.clear();
  AccBuf buf;
  void* acc = g.char_acc_ctor(buf.bytes);
  fn(obj, acc);
  for (int t = 1; t <= names::kCharAttributeMax; ++t) {
    const float v = rd<float>(acc, 4 * t), m = rd<float>(acc, 0xE8 + 4 * t);
    if (v != 0 || m != 0) out.push_back({t, v, m});
  }
  g.char_acc_dtor(acc);
}

void item_stats(const void* item, void* player, bool with_damage, snap::ItemStats& s) {
  char_contributions(item, slot(item, kSlotChar), s.char_attributes);
  item_combat(item, kSlotDefense, player, s.defense);
  item_combat(item, kSlotOffMod, player, s.offensive_modifiers);
  item_combat(item, kSlotRetMod, player, s.retaliation_modifiers);
  skill_attributes(item, slot(item, kSlotSkill), s.skill_attributes);
  conversions(item, slot(item, kSlotConversion), s.conversions);
  s.offensive_damage.clear();
  s.retaliation.clear();
  if (with_damage) {
    item_damage(item, damage_slot(item, kSlotDamage), s.offensive_damage);
    item_damage(item, damage_slot(item, kSlotRetaliation), s.retaliation);
  }
}

void replica(const void* item, snap::Replica& out) {
  const char* r = static_cast<const char*>(item) + kReplica;
  set_msvc_string(out.base, r + 0x08);
  set_msvc_string(out.prefix, r + 0x28);
  set_msvc_string(out.suffix, r + 0x48);
  out.seed = rd<u32>(r, 0x68);
  set_msvc_string(out.modifier, r + 0x70);
  set_msvc_string(out.relic, r + 0x90);
  set_msvc_string(out.relic_bonus, r + 0xB0);
  out.relic_seed = rd<u32>(r, 0xD0);
  set_msvc_string(out.augment, r + 0xD8);
  out.augment_level = rd<u32>(r, 0xF8);
  out.augment_seed = rd<u32>(r, 0xFC);
  set_msvc_string(out.transmute, r + 0x100);
  set_msvc_string(out.ascendant, r + 0x120);
  set_msvc_string(out.ascendant2h, r + 0x140);
  out.var1 = rd<u32>(r, 0x160);
  out.stack_count = rd<u32>(r, 0x178);
  out.affix_rerolls = rd<u32>(r, 0x17C);
  out.rerolls = rd<u32>(r, 0x180);
}

void component(const void* obj, void* player, bool with_stats, snap::Component& c) {
  set_cstr(c.record, g.object_name(obj));
  if (with_stats && *kind_of(obj)) item_stats(obj, player, false, c.stats.emplace());
}

void requirements(const void* obj, const void* player, snap::Item& it) {
  using ReqFn = u32 (*)(const void*);
  snap::Requirements q;
  q.level = rd<ReqFn>(rd<const void*>(obj), kSlotReqLevel)(obj);
  for (int a = 0; a < 3; ++a) {
    q.attribute[a] = rd<ReqFn>(rd<const void*>(obj), kSlotReqAttr[a])(obj);
    for (int k = 0; k < 3; ++k) {
      const u32 t = rd<u32>(obj, kReqReduction[a][k]);
      if (t > static_cast<u32>(names::kCharAttributeMax)) return;  // not a CharAttributeType, layout differs
      q.reductions[a][k] = static_cast<int>(t);
    }
  }
  if (player) {
    q.met = g.requirements_met(obj, player);
    // The state that call saw, so a later snapshot can tell a stale flag (requirements::engine_met).
    snap::Requirements::MetAt& at = q.met_at.emplace();
    at.level = g.char_level(player);
    const void* engine = *g.engine_var;
    at.difficulty = engine ? g.difficulty(engine) : -1;
    for (int a = 0; a < 3; ++a) at.attribute[a] = g.total_char_attr(player, a + 1);
  }
  it.requirements = q;
}

void read_item(const void* obj, snap::Item& it, bool with_stats, void* player) {
  it.id = g.object_id(obj);
  set_cstr(it.record, g.object_name(obj));
  const char* kind = kind_of(obj);
  it.kind.assign(kind);
  if (!*kind) return;  // nothing else is safe to read
  replica(obj, it.replica.emplace());
  if (it.kind == "ItemEquipment") {
    set_msvc_string(it.set, g.item_set_name(obj));
    if (const void* r = g.get_relic(obj)) component(r, player, with_stats, it.relic.emplace());
    if (const void* e = g.get_enchantment(obj)) component(e, player, with_stats, it.augment.emplace());
    if (g.requirements_ok) requirements(obj, player, it);
  }
  if (with_stats && it.kind != "Item")
    item_stats(obj, player, it.kind == "ItemEquipment" || it.kind == "ItemArtifact", it.stats.emplace());
}

// Each section runs under SEH (guarded()).

struct Ctx {
  snap::Snapshot& s;
  snap::Spares& spare;
  void* engine;
  void* player;
  const std::vector<u32>& tooltip_ids;
  Timing& timing;
};

void player_name(const void* player, std::string& out) {
  out.clear();
  const char16_t* n = g.player_name(player);
  if (!n) return;
  int len = 0;
  while (len < 64 && n[len]) ++len;
  const auto* w = reinterpret_cast<const wchar_t*>(n);
  const int bytes = WideCharToMultiByte(CP_UTF8, 0, w, len, nullptr, 0, nullptr, nullptr);
  out.resize(static_cast<std::size_t>(std::max(bytes, 0)));
  if (bytes > 0) WideCharToMultiByte(CP_UTF8, 0, w, len, out.data(), bytes, nullptr, nullptr);
}

void sec_character(Ctx& c) {
  void* p = c.player;
  player_name(p, c.s.character_name);
  if (g.cloud_storage && !c.s.character_name.empty())
    c.s.save = snap::save_key(g.cloud_storage(c.engine),
                              (rd<std::uint8_t>(p, kBackupFolder) != 0 ? "__" : "_") + c.s.character_name);
  c.s.level = g.char_level(p);
  c.s.difficulty = g.difficulty(c.engine);
  c.s.difficulty_ramp = g.difficulty_ramp ? g.difficulty_ramp(c.engine) : 0;
  c.s.skill_points = g.skill_points(p);
  c.s.attribute_points = g.modifier_points(p);
  c.s.devotion_points = g.devotion_points(p);
  c.s.devotion_points_total = g.total_devotion_points(p);
  c.s.life_max = g.life_limit(p);
  c.s.mana_max = g.mana_limit(p);
  c.s.attack_speed = g.attack_speed(p);
  c.s.cast_speed = g.cast_speed(p);
  // Character::GetRunSpeed 0x69040: true returns before CapRunSpeed / CapAbsoluteRunSpeed.
  c.s.run_speed = g.run_speed(p, false);
  c.s.run_speed_uncapped = g.run_speed(p, true);
  c.s.defense_cap = g.defense_cap(p);
  const char* pth = static_cast<const char*>(p) + kPthTable;
  c.s.pth.minimum = rd<float>(pth, 0);
  c.s.pth.crit_divisor_min = rd<float>(pth, 4);
  for (int i = 0; i < 6; ++i) {
    c.s.pth.threshold[i] = rd<float>(pth, 8 + 8 * i);
    c.s.pth.damage_modifier[i] = rd<float>(pth, 12 + 8 * i);
  }
  for (int t = 1; t <= names::kCharAttributeMax; ++t)
    c.s.char_attributes.push_back(
        {t, g.total_char_attr(p, t), g.total_char_mod(p, t), g.base_char_attr(p, t), t <= 3 ? g.points_spent(p, t) : 0});
}

void sec_defense(Ctx& c) {
  AccBuf buf;
  void* acc = g.combat_acc_ctor(buf.bytes, kDisplayModifyProcs);
  g.all_defense(c.player, acc);
  combat_totals(acc, c.player, c.s.defense);
  g.combat_acc_dtor(acc);
}

// As for an incoming hit with no enemy reduction: ProcessDefense 0x11AB40 runs the modifiers, then each
// entry's Process applies its % and cap. ReductionInfo (0x18 bytes) and DamageScaleInfo (0 = no scaling) are zero.
void sec_defense_processed(Ctx& c) {
  struct alignas(16) Zero {
    unsigned char b[64] = {};
  } const reduction, scale;
  AccBuf buf;
  void* acc = g.combat_acc_ctor(buf.bytes, kDisplayModifyProcs);
  g.all_defense(c.player, acc);
  g.process_defense(acc, c.player, &reduction, &scale, 0.0f);
  auto& cap_bonus = g_scratch.cap_bonus;
  cap_bonus.clear();
  const Span s = vec(static_cast<const char*>(acc) + 0x38, sizeof(void*));
  using IntFn = int (*)(const void*);
  for (std::size_t i = 0; i < s.count; ++i) {
    const void* attr = rd<const void*>(s.begin, i * sizeof(void*));
    if (!attr || rd<const void*>(rd<const void*>(attr), 0x28) != g.abs_defense_process) continue;
    const int t = rd<IntFn>(rd<const void*>(attr), 0x10)(attr);
    const float bonus = rd<float>(attr, 0x2C);
    const auto it = std::find_if(cap_bonus.begin(), cap_bonus.end(), [&](const auto& e) { return e.first == t; });
    if (it == cap_bonus.end()) cap_bonus.emplace_back(t, bonus);
    else it->second = std::max(it->second, bonus);
  }
  for (snap::CombatTotals& row : c.s.defense) {
    row.processed = g.total_defense(acc, row.type);
    const auto it = std::find_if(cap_bonus.begin(), cap_bonus.end(), [&](const auto& e) { return e.first == row.type; });
    if (it != cap_bonus.end()) {
      row.cap_bonus = it->second;
      row.capped = true;
    }
  }
  g.combat_acc_dtor(acc);
}

// Player::CalculateDamageModifiers 0x3CD7A0 is Clear + this call with 0 on a cached accumulator.
void sec_damage_modifiers(Ctx& c) {
  AccBuf buf;
  void* acc = g.combat_acc_ctor(buf.bytes, kDisplayModifyProcs);
  g.collect_offensive_modifiers(g.skill_manager(c.player), acc, 0);
  combat_totals(acc, c.player, c.s.damage_modifiers);
  g.combat_acc_dtor(acc);
}

void sec_conversions(Ctx& c) {
  conversions(g.skill_manager(c.player), g.collect_conversions, c.s.conversions);
}

void sec_skill_attributes(Ctx& c) {
  skill_attributes(g.skill_manager(c.player), g.collect_skill_attributes, c.s.skill_attributes);
}

snap::Skill read_skill(Ctx& c, const void* sk) {
  snap::Skill s = snap::reuse(c.spare.skills);
  s.id = g.object_id(sk);
  set_cstr(s.record, g.object_name(sk));
  s.type = static_cast<u32>(g.skill_type(sk));
  s.level = g.skill_level(sk);
  s.augmented_level = g.augmented_level(sk);
  s.current_level = g.current_level(sk);
  s.devotion_level = g.devotion_level(sk);
  s.devotion_max_level = g.devotion_max_level(sk);
  s.mastery_id = g.mastery_id(sk);
  // The buff map is keyed by the skill's record or its buffSkillName record (hook_targets.cpp).
  s.buff_active.reset();
  {
    bool up = false;
    auto ask = [&] {
      up = g.buff_active(c.player, s.record);
      const std::string& buff = g.buff_skill_name(sk);
      if (!up && !buff.empty()) up = g.buff_active(c.player, buff);
    };
    seh::Fault f;
    if (seh::run(ask, f)) {
      s.buff_active = up;
    } else {
      char buf[200];
      std::snprintf(buf, sizeof buf, "skill %s buff_active: FAULT 0x%08lX at 0x%016llX", s.record.c_str(), f.code,
                    static_cast<unsigned long long>(f.addr));
      c.s.errors.emplace_back(buf);
      c.s.complete = false;
      log::printf("snapshot: %s", buf);
    }
  }
  const Span mods = vec(static_cast<const char*>(sk) + kSkillModifiers, sizeof(u32));
  s.modifier_ids.clear();
  for (std::size_t m = 0; m < mods.count; ++m) s.modifier_ids.push_back(rd<u32>(mods.begin, m * sizeof(u32)));
  s.item_id = 0;
  s.anim_frames = 0;
  s.anim_frame_rate = 0;
  const void* ci = class_info(sk);
  if (s.current_level != 0 && (is_a(ci, g.ci_spell) || is_a(ci, g.ci_buff_self)) &&
      rd<const void*>(rd<const void*>(sk), kSlotHitCallbackPoint) == reinterpret_cast<const void*>(g.hit_callback_point)) {
    // As CalculateSkillDps: Name starts at 0, the return value is not checked, the outputs only when both > 0.
    u32 name = 0, frames = 0;
    int rate = 0;
    auto ask = [&] { g.hit_callback_point(sk, c.player, &name, &frames, &rate); };
    seh::Fault f;
    if (seh::run(ask, f)) {
      s.anim_frames = frames;
      s.anim_frame_rate = rate;
    } else {
      char buf[200];
      std::snprintf(buf, sizeof buf, "skill %s hit animation: FAULT 0x%08lX at 0x%016llX", s.record.c_str(), f.code,
                    static_cast<unsigned long long>(f.addr));
      c.s.errors.emplace_back(buf);
      c.s.complete = false;
      log::printf("snapshot: %s", buf);
    }
  }
  // Skill_Passive / Skill_BuffSelfToggled override these collectors.
  snap::ItemStats& p = s.passive.emplace();
  if (s.current_level != 0) {
    auto collect = [&] {
      char_contributions(sk, slot(sk, kSlotSkillPassiveChar), p.char_attributes);
      AccBuf buf;
      void* acc = g.combat_acc_ctor(buf.bytes, kDisplayModifyProcs);
      slot(sk, kSlotSkillPassiveDefense)(sk, acc);
      combat_totals(acc, c.player, p.defense);
      g.combat_acc_dtor(acc);
    };
    seh::Fault f;
    if (!seh::run(collect, f)) {
      char buf[200];
      std::snprintf(buf, sizeof buf, "skill %s passive: FAULT 0x%08lX at 0x%016llX", s.record.c_str(), f.code,
                    static_cast<unsigned long long>(f.addr));
      c.s.errors.emplace_back(buf);
      c.s.complete = false;
      log::printf("snapshot: %s", buf);
      s.passive.reset();
    }
  }
  return s;
}

void sec_skills(Ctx& c) {
  const void* sm = g.skill_manager(c.player);
  c.s.devotion_current_level_sum = g.devotion_spent(sm);
  const Span list = vec(g.skill_list(sm), sizeof(void*));
  for (std::size_t i = 0; i < list.count; ++i) {
    const void* sk = rd<const void*>(list.begin, i * sizeof(void*));
    if (sk) c.s.skills.push_back(read_skill(c, sk));
  }
}

// SkillManager+0x258.
void sec_item_skills(Ctx& c) {
  const Span list = vec(g.item_skill_list(g.skill_manager(c.player)), sizeof(void*));
  for (std::size_t i = 0; i < list.count; ++i) {
    const void* sk = rd<const void*>(list.begin, i * sizeof(void*));
    if (!sk) continue;
    snap::Skill s = read_skill(c, sk);
    s.item_id = g.item_skill_item_id(sk);
    c.s.item_skills.push_back(std::move(s));
  }
}

// Each pet under its own SEH: a fault drops that pet, logged, and marks the capture incomplete.
void sec_pets(Ctx& c) {
  if (!g.pets_ok) return;
  const void* head = rd<const void*>(g.pet_pen(c.player), kPetList);
  if (!head) return;
  const void* n = rd<const void*>(head, 0);
  for (std::size_t i = 0; n && n != head && i < kMaxPets; ++i, n = rd<const void*>(n, 0)) {
    if (rd<std::uint8_t>(n, kPetNodeReleased) != 0) continue;
    const u32 id = rd<u32>(n, kPetNodeId);
    snap::Pet p;
    auto read = [&] {
      void* pet = lookup(id);
      if (!pet || !is_a(class_info(pet), g.ci_character)) return;
      p.id = id;
      p.skill_id = rd<u32>(n, kPetNodeSkill);
      set_cstr(p.record, g.object_name(pet));
      p.attack_speed = g.attack_speed(pet);
      p.cast_speed = g.cast_speed(pet);
      p.attack_speed_modifier = g.total_char_mod(pet, 15);
      p.cast_speed_modifier = g.total_char_mod(pet, 16);
      const void* sm = g.skill_manager(pet);
      p.default_skill_id = g.default_skill_id(sm, 0);
      const Span list = vec(g.skill_list(sm), sizeof(void*));
      for (std::size_t k = 0; k < list.count; ++k) {
        const void* sk = rd<const void*>(list.begin, k * sizeof(void*));
        if (!sk) continue;
        snap::PetSkill& ps = p.skills.emplace_back();
        ps.id = g.object_id(sk);
        set_cstr(ps.record, g.object_name(sk));
        ps.current_level = g.current_level(sk);
        if (is_a(class_info(sk), g.ci_activated) &&
            rd<const void*>(rd<const void*>(sk), kSlotHitCallbackPoint) == reinterpret_cast<const void*>(g.hit_callback_point)) {
          u32 name = 0, frames = 0;
          int rate = 0;
          g.hit_callback_point(sk, pet, &name, &frames, &rate);
          ps.anim_frames = frames;
          ps.anim_frame_rate = rate;
        }
      }
    };
    seh::Fault f;
    if (!seh::run(read, f)) {
      char buf[160];
      std::snprintf(buf, sizeof buf, "pet %u: FAULT 0x%08lX at 0x%016llX", id, f.code, static_cast<unsigned long long>(f.addr));
      c.s.errors.emplace_back(buf);
      c.s.complete = false;
      log::printf("snapshot: %s", buf);
      continue;
    }
    if (p.id != 0) c.s.pets.push_back(std::move(p));
  }
}

void modifier_stats(const void* mod, void* player, snap::ItemStats& s) {
  item_damage(mod, g.mod_damage, s.offensive_damage);
  combat_with_procs([&](void* acc) { g.mod_offensive_modifiers(mod, acc); }, player, s.offensive_modifiers);
  skill_attributes(mod, g.mod_skill_attributes, s.skill_attributes);
  conversions(mod, g.mod_conversions, s.conversions);
}

void sec_item_skill_modifiers(Ctx& c) {
  const Span list = vec(g.skill_list(g.skill_manager(c.player)), sizeof(void*));
  for (std::size_t i = 0; i < list.count; ++i) {
    const char* sk = rd<const char*>(list.begin, i * sizeof(void*));
    if (!sk) continue;
    const Span mods = vec(sk + 0x158, 0x30);
    for (std::size_t k = 0; k < mods.count; ++k) {
      const char* e = mods.begin + k * 0x30;
      snap::ItemSkillModifier m = snap::reuse(c.spare.item_skill_modifiers);
      m.skill_id = g.object_id(sk);
      set_cstr(m.skill_record, g.object_name(sk));
      m.modifier_id = rd<u32>(e, 0);
      m.location = rd<int>(e, 4);
      m.from_relic = rd<std::uint8_t>(e, 0x28) != 0;
      m.from_augment = rd<std::uint8_t>(e, 0x29) != 0;
      if (const void* mod = lookup(m.modifier_id)) {
        set_cstr(m.modifier_record, g.object_name(mod));
        // The exported collectors are Skill_Modifier's own overrides.
        if (class_info(mod) == g.ci_skill_modifier) modifier_stats(mod, c.player, m.stats.emplace());
      }
      c.s.item_skill_modifiers.push_back(std::move(m));
    }
  }
}

void sec_equipped(Ctx& c) {
  const Span items = vec(g.attached_items(c.player), 12);
  for (std::size_t i = 0; i < items.count; ++i) {
    const u32 id = rd<u32>(items.begin, i * 12);
    const void* obj = lookup(id);
    if (!obj) {
      c.s.errors.push_back("equipped: id " + std::to_string(id) + " not found");
      c.s.complete = false;
      continue;
    }
    snap::Item it = snap::reuse(c.spare.items);
    read_item(obj, it, true, c.player);
    it.location = rd<int>(items.begin, i * 12 + 4);
    c.s.equipped.push_back(std::move(it));
  }
}

void sec_tooltip(Ctx& c) {
  for (u32 id : c.tooltip_ids) {
    const void* obj = lookup(id);
    if (!obj) continue;
    snap::Item it = snap::reuse(c.spare.items);
    read_item(obj, it, true, c.player);
    c.s.tooltip_items.push_back(std::move(it));
  }
}

void sec_inventory(Ctx& c) {
  const Span ids = vec(g.inventory_items(c.player), sizeof(u32));
  for (std::size_t i = 0; i < ids.count; ++i) {
    const void* obj = lookup(rd<u32>(ids.begin, i * sizeof(u32)));
    if (!obj) continue;
    snap::Item it = snap::reuse(c.spare.items);
    read_item(obj, it, false, c.player);
    c.s.inventory.push_back(std::move(it));
  }
}

bool guarded(const char* name, void (*fn)(Ctx&), Ctx& c) {
  auto call = [&] { fn(c); };
  seh::Fault f;
  LARGE_INTEGER t0, t1;
  QueryPerformanceCounter(&t0);
  const bool ok = seh::run(call, f);
  QueryPerformanceCounter(&t1);
  if (c.timing.count < Timing::kMax) c.timing.sections[c.timing.count++] = {name, log::elapsed_us(t0, t1)};
  if (ok) return true;
  char buf[160];
  std::snprintf(buf, sizeof buf, "%s: FAULT 0x%08lX at 0x%016llX", name, f.code, static_cast<unsigned long long>(f.addr));
  c.s.errors.emplace_back(buf);
  c.s.complete = false;
  log::printf("snapshot section %s", buf);
  return false;
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

}  // namespace

bool snapshot_init(const Resolve& r) {
  bool ok = true;
  auto b = [&](auto& out, std::string_view label) { ok = bind(r, out, label) && ok; };
  b(g.engine_var, "gGameEngine");
  b(g.main_player, "GameEngine::GetMainPlayer");
  b(g.player_name, "Player::GetPlayerName");
  b(g.difficulty, "GameEngine::GetGameDifficulty");
  // A string literal would pick std::bind by ADL; the string_view picks ours.
  bind(r, g.difficulty_ramp, std::string_view{"GameEngine::GetDifficultyRamp"});
  bind(r, g.cloud_storage, std::string_view{"GameEngine::GetCloudStorage"});
  b(g.om_get, "Singleton<ObjectManager>::Get");
  b(g.object_name, "Object::GetObjectName");
  b(g.object_id, "Object::GetObjectId");
  b(g.ci_item, "Item::classInfo");
  b(g.ci_equipment, "ItemEquipment::classInfo");
  b(g.ci_artifact, "ItemArtifact::classInfo");
  b(g.ci_relic, "ItemRelic::classInfo");
  b(g.ci_enchantment, "ItemEnchantment::classInfo");
  b(g.total_char_attr, "Character::GetTotalCharAttribute");
  b(g.total_char_mod, "Character::GetTotalCharModifier");
  b(g.base_char_attr, "Character::GetBaseCharAttribute");
  b(g.points_spent, "Character::GetPointsSpent");
  b(g.life_limit, "Character::GetLifeLimit");
  b(g.mana_limit, "Character::GetManaLimit");
  b(g.attack_speed, "Character::GetAttackSpeed");
  b(g.cast_speed, "Character::GetSpellCastSpeed");
  b(g.char_level, "Character::GetCharLevel");
  b(g.skill_points, "Character::GetSkillPoints");
  b(g.modifier_points, "Character::GetModifierPoints");
  b(g.devotion_points, "Character::GetDevotionPoints");
  b(g.total_devotion_points, "Character::GetTotalDevotionPoints");
  b(g.all_defense, "Character::GetAllDefenseAttributes");
  b(g.attached_items, "Character::GetAttachedItems");
  b(g.inventory_items, "Character::GetInventoryItems");
  b(g.skill_manager, "Character::GetSkillManager");
  b(g.collect_offensive_modifiers, "SkillManager::CollectAvailableOffensiveModifierAttributes");
  b(g.collect_conversions, "SkillManager::CollectAvailableConversionAttributes");
  b(g.collect_skill_attributes, "SkillManager::CollectAvailableSkillAttributes");
  b(g.run_speed, "Character::GetRunSpeed");
  b(g.defense_cap, "Player::GetDefenseAttributeCap");
  b(g.skill_list, "SkillManager::GetSkillList");
  b(g.item_skill_list, "SkillManager::GetItemSkillList");
  b(g.item_skill_item_id, "Skill::GetItemSkillItemId");
  b(g.devotion_spent, "SkillManager::GetNumDevotionPointsSpent");
  b(g.skill_level, "Skill::GetSkillLevel");
  b(g.augmented_level, "Skill::GetAugmentedLevel");
  b(g.current_level, "Skill::GetCurrentLevel");
  b(g.devotion_level, "Skill::GetDevotionLevel");
  b(g.devotion_max_level, "Skill::GetDevotionMaxLevel");
  b(g.mastery_id, "Skill::GetMasteryId");
  b(g.skill_type, "Skill::GetSkillType");
  b(g.buff_active, "Character::IsSkillBuffActive");
  b(g.buff_skill_name, "Skill::GetBuffSkillName");
  b(g.ci_spell, "SkillActivatedSpell::classInfo");
  b(g.ci_buff_self, "SkillActivatedBuffSelf::classInfo");
  b(g.hit_callback_point, "SkillActivated::GetHitCallbackPoint");
  {
    auto opt = [&](auto& out, std::string_view label) { return bind(r, out, label); };  // optional: no `ok`
    g.pets_ok = opt(g.pet_pen, "Character::GetPetPen") && opt(g.ci_character, "Character::classInfo") &&
                opt(g.ci_activated, "SkillActivated::classInfo") && opt(g.default_skill_id, "SkillManager::GetDefaultSkillId");
  }
  if (!g.pets_ok) log::write("snapshot: pet getters unresolved; pets not read");
  b(g.char_acc_ctor, "CharAttributeAccumulator::CharAttributeAccumulator()");
  b(g.char_acc_dtor, "CharAttributeAccumulator::~CharAttributeAccumulator()");
  b(g.combat_acc_ctor, "CombatDisplayAccumulator::CombatDisplayAccumulator(bool)");
  b(g.combat_acc_dtor, "CombatDisplayAccumulator::~CombatDisplayAccumulator()");
  b(g.display_vft, "CombatDisplayAccumulator::vftable");
  b(g.chance_mult, "CombatDisplayAccumulator::GetChanceMultiplier");
  b(g.base_physical_vft, "CombatAttributeDamage_BasePhysical::vftable");
  b(g.base_physical_values, "CombatAttributeDamage_BasePhysical::GetValues");
  b(g.total_defense, "CombatAttributeAccumulator::GetTotalDefenseType");
  b(g.total_defense_mod, "CombatAttributeAccumulator::GetTotalDefenseModifierType");
  b(g.total_reduction, "CombatAttributeAccumulator::GetTotalReduction");
  b(g.total_damage_mod, "CombatAttributeAccumulator::GetTotalDamageModifierType");
  b(g.process_defense, "CombatAttributeAccumulator::ProcessDefense");
  b(g.abs_defense_process, "CombatAttributeAbsDefense::Process");
  b(g.skill_acc_ctor, "SkillAttributeAccumulator::SkillAttributeAccumulator()");
  b(g.skill_acc_dtor, "SkillAttributeAccumulator::~SkillAttributeAccumulator()");
  b(g.conv_acc_ctor, "ConversionAttributeAccumulator::ConversionAttributeAccumulator()");
  b(g.conv_acc_dtor, "ConversionAttributeAccumulator::~ConversionAttributeAccumulator()");
  b(g.skill_passive_vft, "Skill_Passive::vftable");
  b(g.skill_passive_char, "Skill_Passive::CollectPassiveCharAttributes");
  b(g.skill_passive_defense, "Skill_Passive::CollectPassiveDefenseAttributes");
  b(g.ci_skill_modifier, "Skill_Modifier::classInfo");
  b(g.mod_damage, "Skill_Modifier::CollectModifierOffensiveDamageAttributes");
  b(g.mod_offensive_modifiers, "Skill_Modifier::CollectModifierOffensiveModifierAttributes");
  b(g.mod_skill_attributes, "Skill_Modifier::CollectModifierSkillAttributes");
  b(g.mod_conversions, "Skill_Modifier::CollectModifierConversionAttributes");
  b(g.item_set_name, "ItemEquipment::GetItemSetName");
  b(g.get_relic, "ItemEquipment::GetRelic");
  b(g.get_enchantment, "ItemEquipment::GetEnchantment");
  b(g.requirements_met, "ItemEquipment::AreRequirementsMet");
  if (!ok) return false;

  // Accept the decoded call only if its first call goes through the IAT entry for Engine.dll's
  // Singleton<ObjectManager>::Get.
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
  if (via_iat != reinterpret_cast<const void*>(g.om_get)) {
    log::printf("snapshot: lookup call's IAT slot 0x%016llX holds 0x%016llX, not Singleton<ObjectManager>::Get",
                static_cast<unsigned long long>(call->iat_slot), static_cast<unsigned long long>(addr(via_iat)));
    return false;
  }
  g.object_from_id = reinterpret_cast<void* (*)(void*, u32)>(call->target);
  log::printf("snapshot: object lookup at 0x%016llX", static_cast<unsigned long long>(call->target));

  ok = verify_slots(r, "ItemEquipment", true) && verify_slots(r, "ItemArtifact", true) &&
       verify_slots(r, "ItemRelic", false) && verify_slots(r, "ItemEnchantment", false);
  if (!ok) return false;
  if (rd<const void*>(g.base_physical_vft, kSlotGetValues) != reinterpret_cast<const void*>(g.base_physical_values)) {
    log::write("snapshot: CombatAttributeDamage_BasePhysical vftable slot 0x110 is not GetValues");
    return false;
  }
  if (rd<const void*>(g.display_vft, kSlotChanceMultiplier) != reinterpret_cast<const void*>(g.chance_mult)) {
    log::write("snapshot: CombatDisplayAccumulator vftable slot 0x18 is not GetChanceMultiplier");
    return false;
  }
  if (rd<const void*>(g.skill_passive_vft, kSlotSkillPassiveChar) != g.skill_passive_char ||
      rd<const void*>(g.skill_passive_vft, kSlotSkillPassiveDefense) != g.skill_passive_defense) {
    log::write("snapshot: Skill_Passive vftable slots 0x408 / 0x438 are not its CollectPassive* overrides");
    return false;
  }
  float (*const mult[kPasses])(void*, float) = {nullptr, mult_raw, mult_excluded, mult_probe};
  for (int p = kRaw; p < kPasses; ++p) {
    g_pass_vft[p][0] = rd<const void*>(static_cast<const char*>(g.display_vft) - sizeof(void*));
    for (std::size_t i = 0; i < kDisplayVftSlots; ++i) g_pass_vft[p][1 + i] = rd<const void*>(g.display_vft, 8 * i);
    g_pass_vft[p][1 + kSlotChanceMultiplier / 8] = reinterpret_cast<const void*>(mult[p]);
  }
  log::write("snapshot: item vtable slots verified");
  {
    const void* ring_vft = r("ArmorJewelry_Ring::vftable{Object}");
    const void* ring_type = r("ArmorJewelry_Ring::GetItemType");
    const void* item_type = r("Item::GetItemType");
    const void* eq_vft = r("ItemEquipment::vftable{Object}");
    g.item_type_ok = ring_vft && ring_type && item_type && rd<const void*>(ring_vft, kSlotItemType) == ring_type &&
                     rd<const void*>(eq_vft, kSlotItemType) == item_type;
    if (!g.item_type_ok) log::write("snapshot: Item::GetItemType slot 0x550 not verified; tooltip block off");
    const void* req[] = {r("ItemEquipment::GetLevelRequirement"), r("ItemEquipment::GetStrengthRequirement"),
                         r("ItemEquipment::GetDexterityRequirement"), r("ItemEquipment::GetIntelligenceRequirement")};
    const std::size_t slots[] = {kSlotReqLevel, kSlotReqAttr[0], kSlotReqAttr[1], kSlotReqAttr[2]};
    g.requirements_ok = eq_vft != nullptr;
    for (int i = 0; i < 4 && g.requirements_ok; ++i) g.requirements_ok = req[i] && rd<const void*>(eq_vft, slots[i]) == req[i];
    if (!g.requirements_ok) log::write("snapshot: requirement getter slots 0x528..0x540 not verified; requirements not read");
  }
  g_ready = true;
  return true;
}

Take snapshot_take(snap::Snapshot& s, snap::Spares& spare, const std::vector<std::uint32_t>& tooltip_ids,
                   bool with_inventory, Timing& timing) {
  timing.count = 0;
  if (!g_ready) return Take::NoPlayer;
  void* engine = nullptr;
  void* player = nullptr;
  auto find = [&] {
    engine = const_cast<void*>(*g.engine_var);
    player = engine ? g.main_player(engine) : nullptr;
  };
  seh::Fault f;
  if (!seh::run(find, f)) {
    char buf[96];
    std::snprintf(buf, sizeof buf, "main player: FAULT 0x%08lX at 0x%016llX", f.code, static_cast<unsigned long long>(f.addr));
    s.errors.emplace_back(buf);
    s.complete = false;
    log::printf("snapshot: %s", buf);
    return Take::Faulted;
  }
  if (!player) return Take::NoPlayer;
  Ctx c{s, spare, engine, player, tooltip_ids, timing};
  bool ok = guarded("character", sec_character, c);
  ok = guarded("defense", sec_defense, c) && ok;
  ok = guarded("defense_processed", sec_defense_processed, c) && ok;
  ok = guarded("damage_modifiers", sec_damage_modifiers, c) && ok;
  ok = guarded("conversions", sec_conversions, c) && ok;
  ok = guarded("skill_attributes", sec_skill_attributes, c) && ok;
  ok = guarded("skills", sec_skills, c) && ok;
  ok = guarded("item_skill_modifiers", sec_item_skill_modifiers, c) && ok;
  ok = guarded("item_skills", sec_item_skills, c) && ok;
  ok = guarded("pets", sec_pets, c) && ok;
  ok = guarded("equipped", sec_equipped, c) && ok;
  ok = guarded("tooltip", sec_tooltip, c) && ok;
  if (with_inventory) ok = guarded("inventory", sec_inventory, c) && ok;
  return ok ? Take::Ok : Take::Faulted;
}

Take snapshot_probe(Probe& out) {
  if (!g_ready) return Take::NoPlayer;
  out = Probe{};
  auto read = [&] {
    void* engine = const_cast<void*>(*g.engine_var);
    void* p = engine ? g.main_player(engine) : nullptr;
    if (!p) return;
    out.player = p;
    out.level = g.char_level(p);
    out.skill_points = g.skill_points(p);
    out.attribute_points = g.modifier_points(p);
    out.devotion_points = g.devotion_points(p);
    out.difficulty = g.difficulty(engine);
    out.difficulty_ramp = g.difficulty_ramp ? g.difficulty_ramp(engine) : 0;
    const Span items = vec(g.attached_items(p), 12);
    std::uint64_t h = 1469598103934665603ull;
    auto mix = [&h](u32 v) {
      for (int b = 0; b < 4; ++b) h = (h ^ ((v >> (8 * b)) & 0xFF)) * 1099511628211ull;
    };
    for (std::size_t i = 0; i < items.count; ++i) {
      const u32 id = rd<u32>(items.begin, i * 12);
      mix(id);
      mix(rd<u32>(items.begin, i * 12 + 4));
      // Socketing changes neither the item id nor its slot.
      const u64 fp = fingerprint_of(lookup(id));
      mix(static_cast<u32>(fp >> 32));
      mix(static_cast<u32>(fp));
    }
    out.equip_hash = h;
  };
  seh::Fault f;
  if (!seh::run(read, f)) {
    log::printf("snapshot probe: FAULT 0x%08lX at 0x%016llX", f.code, static_cast<unsigned long long>(f.addr));
    return Take::Faulted;
  }
  return out.player ? Take::Ok : Take::NoPlayer;
}

namespace {
Take candidate(const void* item, u32 id, snap::Item& out, int& item_type) {
  if (!g_ready || !g.item_type_ok) return Take::NoPlayer;
  bool found = false;
  auto read = [&] {
    void* engine = const_cast<void*>(*g.engine_var);
    void* player = engine ? g.main_player(engine) : nullptr;
    if (!player) return;
    const void* obj = lookup(id ? id : g.object_id(item));
    if (!obj || !is_a(class_info(obj), g.ci_item)) return;
    using TypeFn = int (*)(const void*);
    item_type = rd<TypeFn>(rd<const void*>(obj), kSlotItemType)(obj);
    read_item(obj, out, true, player);
    found = true;
  };
  seh::Fault f;
  if (!seh::run(read, f)) {
    log::printf("tooltip candidate: FAULT 0x%08lX at 0x%016llX", f.code, static_cast<unsigned long long>(f.addr));
    return Take::Faulted;
  }
  return found ? Take::Ok : Take::NoPlayer;
}
}  // namespace

Take snapshot_candidate(const void* item, snap::Item& out, int& item_type) { return candidate(item, 0, out, item_type); }

Take snapshot_candidate_id(std::uint32_t id, snap::Item& out, int& item_type) {
  return id ? candidate(nullptr, id, out, item_type) : Take::NoPlayer;
}

Take snapshot_bag(std::vector<BagItem>& out) {
  out.clear();
  if (!g_ready) return Take::NoPlayer;
  bool player_found = false;
  auto read = [&] {
    void* engine = const_cast<void*>(*g.engine_var);
    void* player = engine ? g.main_player(engine) : nullptr;
    if (!player) return;
    player_found = true;
    const Span ids = vec(g.inventory_items(player), sizeof(u32));
    for (std::size_t i = 0; i < ids.count && out.size() < out.capacity(); ++i) {  // no allocation
      const u32 id = rd<u32>(ids.begin, i * sizeof(u32));
      if (const void* obj = lookup(id)) out.push_back({id, fingerprint_of(obj)});
    }
  };
  seh::Fault f;
  if (!seh::run(read, f)) {
    log::printf("tooltip bag: FAULT 0x%08lX at 0x%016llX", f.code, static_cast<unsigned long long>(f.addr));
    return Take::Faulted;
  }
  return player_found ? Take::Ok : Take::NoPlayer;
}

std::uint64_t item_fingerprint(const void* item) {
  if (!g_ready || !item) return 0;
  u64 fp = 0;
  auto read = [&] { fp = fingerprint_of(lookup(g.object_id(item))); };
  seh::Fault f;
  return seh::run(read, f) ? fp : 0;
}

std::uint32_t object_id(const void* obj) {
  u32 id = 0;
  if (!g.object_id || !obj) return 0;
  auto get = [&] { id = g.object_id(obj); };
  seh::Fault f;
  return seh::run(get, f) ? id : 0;
}

}  // namespace gdc::adapter
