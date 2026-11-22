#pragma once
// Adapter writes, core reads; engine getter values.
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace gdc::snap {

// Schema written; from_json reads kMinSchema..kSchema.
inline constexpr int kSchema = 17;
inline constexpr int kMinSchema = 2;

// Save key, as GameEngine::GetPlayerFolder builds it.
std::string save_key(std::uint32_t steam_account, bool custom, std::string_view folder);
// Name in a save key ("cloud/1/main/__Bob" -> "Bob"); empty otherwise.
std::string save_key_name(std::string_view key);

struct CharAttribute {
  int type = 0;
  float total = 0, modifier = 0, base = 0;
  std::uint32_t points_spent = 0;
};

struct CharContribution {
  int type = 0;
  float value = 0, modifier = 0;
};

// `chance` exact for one chance stat; several: raw-weighted mean.
struct Proc {
  float chance = 0;
  float defense = 0, defense_modifier = 0, reduction = 0, damage_modifier = 0, dot_modifier = 0;
};

struct CombatTotals {
  int type = 0;
  float defense = 0;           // GetTotalDefenseType
  float defense_modifier = 0;  // GetTotalDefenseModifierType
  float reduction = 0;         // GetTotalReduction
  float damage_modifier = 0;   // GetTotalDamageModifierType(..., DamageType 1 = direct)
  float dot_modifier = 0;      // GetTotalDamageModifierType(..., DamageType 2 = duration)
  // Defense only: after ProcessDefense, plus the +0x2C cap bonus.
  float processed = 0;
  float cap_bonus = 0;
  bool capped = false;  // processed by CombatAttributeAbsDefense::Process
  // Item stats only: the chance part at 100%.
  std::optional<Proc> proc;
};

struct Damage {
  int type = 0;
  bool dot = false;    // GetDamageType() == 2 (CombatAttributeDurDamage*)
  float min = 0, max = 0;  // GetMinMaxValues
  float duration = 0;      // GetDuration (dot only)
  // Chance entries: min / max = raw x chance / 100.
  float chance = 0, raw_min = 0, raw_max = 0;
};

// float[5] arrays at +8 / +0x1C / +0x30 / +0x44.
struct SkillAttribute {
  int type = 0;
  float value = 0, chance_value = 0, modifier = 0, chance_modifier = 0;
};

struct Conversion {
  int from = 0, to = 0;
  float percent = 0;
};

// Item+0x538 replica fields.
struct Replica {
  std::string base, prefix, suffix, modifier, transmute, relic, relic_bonus, augment, ascendant,
      ascendant2h;
  std::uint32_t seed = 0, relic_seed = 0, augment_level = 0, augment_seed = 0, var1 = 0,
                stack_count = 0, affix_rerolls = 0, rerolls = 0;
};

struct ItemStats {
  std::vector<CharContribution> char_attributes;    // GetCharAttributes
  std::vector<CombatTotals> defense;                // GetDefenseAttributes
  std::vector<CombatTotals> offensive_modifiers;    // GetOffensiveModifierAttributes
  std::vector<CombatTotals> retaliation_modifiers;  // GetRetaliationModifierAttributes
  std::vector<Damage> offensive_damage;             // GetOffensiveDamageAttributes(acc, 1.0)
  std::vector<Damage> retaliation;                  // GetRetaliationAttributes(acc, 1.0)
  std::vector<SkillAttribute> skill_attributes;     // GetSkillAttributes
  std::vector<Conversion> conversions;              // GetConversionAttributes
};

struct Component {
  std::string record;
  std::optional<ItemStats> stats;
};

// As AreRequirementsMet 0x343C30 reads; max over item and components.
struct Requirements {
  std::uint32_t level = 0;                        // vt+0x528 GetLevelRequirement
  std::uint32_t attribute[3] = {};                // vt+0x530 / 0x538 / 0x540: Physique, Cunning, Spirit
  int reductions[3][3] = {};                      // Physique +0xDF8/+0xDEC/+0xE04, Cunning +0xDFC/+0xDF0/+0xE08,
                                                  // Spirit +0xE00/+0xDF4/+0xE0C
  std::optional<bool> met;                        // ItemEquipment::AreRequirementsMet(player) when read
  // Schema 11: the state that call ran against.
  struct MetAt {
    std::uint32_t level = 0;
    int difficulty = -1;
    float attribute[3] = {};
  };
  std::optional<MetAt> met_at;
};

struct Item {
  std::uint32_t id = 0;           // Object::GetObjectId
  int location = -1;              // EquipManagerContainer+4; -1 = not equipped
  std::string record;             // Object::GetObjectName
  std::string kind;
  std::string set;
  std::optional<Replica> replica;
  std::optional<ItemStats> stats;
  std::optional<Component> relic, augment;  // ItemEquipment::GetRelic / GetEnchantment
  std::optional<Requirements> requirements;  // schema 7
};

struct Skill {
  std::uint32_t id = 0;
  std::string record;
  std::uint32_t type = 0;                // Skill::GetSkillType (raw enum)
  std::uint32_t level = 0;               // GetSkillLevel: points spent
  std::uint32_t augmented_level = 0;     // GetAugmentedLevel: +skills
  std::uint32_t current_level = 0;       // GetCurrentLevel
  std::uint32_t devotion_level = 0;      // GetDevotionLevel: celestial power level
  std::uint32_t devotion_max_level = 0;  // GetDevotionMaxLevel
  std::uint32_t mastery_id = 0;          // GetMasteryId: object id of the mastery skill
  // Own passive collectors' output now (vt+0x408 / +0x438). Schema 4.
  std::optional<ItemStats> passive;
  // Learned modifiers, as AddModifierDefenseAttributes 0x494970 walks. Schema 5.
  std::vector<std::uint32_t> modifier_ids;
  // Skill::GetItemSkillItemId 0x31D500 (Skill+0x350).
  std::uint32_t item_id = 0;
  // Schema 8: Character::IsSkillBuffActive 0x67DD0 on record or buff name.
  std::optional<bool> buff_active;
  // Schema 10: hit animation via GetHitCallbackPoint 0x51A310; 0 = unknown.
  std::uint32_t anim_frames = 0;
  std::int32_t anim_frame_rate = 0;
};

// Skill+0x158 entry, Skill::AddToItemSkillModifiers.
struct ItemSkillModifier {
  std::uint32_t skill_id = 0;
  std::string skill_record;
  std::uint32_t modifier_id = 0;
  std::string modifier_record;
  int location = -1;               // EquipLocations of the granting item
  bool from_relic = false, from_augment = false;
  std::optional<ItemStats> stats;  // Skill_Modifier::CollectModifier*
};

// Character+0x7A8 PTH table, combatformulas.dbr order.
struct PthTable {
  float minimum = 0;               // pthMinimum (+0x278)
  float crit_divisor_min = 0;      // +0x27C
  float threshold[6] = {};         // pthThreshold1..6 (+0x280, stride 8)
  float damage_modifier[6] = {};   // pthDamageModifier1..6 (+0x284, stride 8)
};

// Schema 12: a live pet from the player's PetPen.
struct PetSkill {
  std::uint32_t id = 0;
  std::string record;
  std::uint32_t current_level = 0;
  // GetHitCallbackPoint 0x51A310 called with the pet; 0 = none.
  std::uint32_t anim_frames = 0;
  std::int32_t anim_frame_rate = 0;
};
struct Pet {
  std::uint32_t id = 0, skill_id = 0;
  std::string record;
  // Pet's GetAttackSpeed / GetSpellCastSpeed, and modifiers 15 / 16.
  float attack_speed = 0, cast_speed = 0, attack_speed_modifier = 0, cast_speed_modifier = 0;
  std::uint32_t default_skill_id = 0;  // SkillManager::GetDefaultSkillId(0) 0x52EC10 of the pet's SkillManager
  std::vector<PetSkill> skills;        // SkillManager::GetSkillList of the pet
  // Schema 17: the pet's engine totals, as the player's damage_modifiers / conversions but from the pet's own
  // SkillManager (SkillManager::CollectAvailableOffensiveModifierAttributes 0x535BC0 calls the character's
  // vt+0x6F0 / +0x730 / +0x768: Pet::ContributeMisc / GameBalance / MutatorOffensiveModifierAttributes 0x3C7790 /
  // 0x3C79F0 / 0x3C7FA0, which add the pet bonuses). Empty: not captured.
  std::vector<CombatTotals> damage_modifiers;
  std::vector<Conversion> conversions;
};

// Schema 15: Player::CalculateSkillDps 0x3DFD30 (1.3.1.2), the engine's own sheet DPS numbers for one skill.
struct SkillDps {
  std::uint32_t skill_id = 0;
  std::string record;
  // The skill cannot be used with the current weapons: the engine computed the default attack instead.
  bool default_attack = false;
  // Schema 16: SkillAttackInfo+0x28, set for a weapon in the L Hand that is not a shield or caster off-hand. Then
  // `off_hand` is the L weapon's own pass; otherwise the sheet leaves `off_hand` out.
  bool dual_wield = false;
  float dps = 0;
  // Processed entries (all %, conversions and multipliers applied): per use, a DoT per second with `duration` after
  // its duration %. Chance entries at expected value.
  std::vector<Damage> main_hand, off_hand;
};

// Schema 16: DurationDamageManager::GetFixedDamage 0x2795C0 of Character::GetDurationDamageMgr (Character+0x540): the
// damage reductions on the character (enemy debuffs), as Skill::CollectCombatParameters 0x499570 reads them for every
// damage entry it processes. Percent values in %.
struct DamageReduction {
  float absolute = 0;           // 29 TotalDamageReductionAbsolute
  float percent = 0;            // 28 TotalDamageReductionPercent
  float physical_percent = 0;   // 30 PhysicalReductionPercent
  float elemental_percent = 0;  // 31 ElementalReductionPercent
};

struct Snapshot {
  int schema = kSchema;
  std::string mod_version;
  std::string game_build;
  std::string taken_at;  // UTC, ISO 8601
  bool complete = true;  // false: see errors
  std::vector<std::string> errors;

  std::string character_name;  // UTF-8
  // Schema 14 save key (save_key); empty = unknown.
  std::string save;
  std::uint32_t level = 0;
  int difficulty = -1;  // GameEngine::GetGameDifficulty (raw enum)
  // Schema 11: GetDifficultyRamp 0x2D85A0; above 0 adds challenge adjustment.
  std::uint32_t difficulty_ramp = 0;
  std::uint32_t skill_points = 0, attribute_points = 0, devotion_points = 0, devotion_points_total = 0;
  // GetNumDevotionPointsSpent 0x534D40: celestial powers count their level.
  std::uint32_t devotion_current_level_sum = 0;
  float life_max = 0, mana_max = 0, attack_speed = 0, cast_speed = 0;
  float run_speed = 0, run_speed_uncapped = 0;  // Character::GetRunSpeed(false / true)
  float defense_cap = 0;                         // base max resist
  PthTable pth;

  std::vector<CharAttribute> char_attributes;
  std::vector<CombatTotals> defense;           // Character::GetAllDefenseAttributes
  std::vector<CombatTotals> damage_modifiers;  // SkillManager::CollectAvailableOffensiveModifierAttributes
  std::vector<Conversion> conversions;         // SkillManager::CollectAvailableConversionAttributes
  // Schema 9: CollectAvailableSkillAttributes 0x535BE0 totals.
  std::vector<SkillAttribute> skill_attributes;
  std::vector<Skill> skills;
  std::vector<ItemSkillModifier> item_skill_modifiers;
  // Schema 6: GetItemSkillList 0x531730; `passive` is what each adds.
  std::vector<Skill> item_skills;
  std::vector<Item> equipped;
  // Hovered item plus the equipped items shown beside it.
  std::vector<Item> tooltip_items;
  std::uint32_t tooltip_age_ms = 0;
  std::vector<Item> inventory;
  std::vector<Pet> pets;
  std::vector<SkillDps> skill_dps;
  std::optional<DamageReduction> damage_reduction;  // schema 16, with skill_dps; absent: not read
};

std::string to_json(const Snapshot& s, int indent = 2);
// Throws std::runtime_error on malformed input or an unsupported schema.
Snapshot from_json(std::string_view text);

// Adapter: fewer allocations; reuse() returns spares in recycle() order.
struct Spares {
  std::vector<Skill> skills;
  std::vector<Item> items;
  std::vector<ItemSkillModifier> item_skill_modifiers;
};
void recycle(Snapshot& s, Spares& spare);
template <class T>
T reuse(std::vector<T>& spare) {
  if (spare.empty()) return T{};
  T t = std::move(spare.back());
  spare.pop_back();
  return t;
}

// 0.1f -> 0.1, not 0.10000000149.
double widen(float f);

}  // namespace gdc::snap
