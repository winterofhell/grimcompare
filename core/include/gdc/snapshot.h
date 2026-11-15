#pragma once
// Written by the adapter, read by core. Every number comes from an engine getter (docs/SNAPSHOT.md),
// nothing is recomputed. Zero / empty fields are omitted from the JSON.
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace gdc::snap {

// 2: devotion_current_level_sum (was devotion_points_spent), pth.
// 3: chance-based item stats carry their chance and 100% values (Damage::chance/raw_*, CombatTotals::proc);
//    item AbsorptionProtection reduction no longer written. Schema 2 still reads (no chance data).
// 4: Skill::passive, character.name. Schemas 2..3 still read (passive absent).
// 5: Skill::modifier_ids. Schemas 2..4 still read (modifier links unknown; check `schema` before use).
// 6: Snapshot::item_skills, Skill::item_id. Schemas 2..5 still read (granted skills unknown).
// 7: Item::requirements. Schemas 2..6 still read (requirements unknown: not checked).
// 8: Skill::buff_active. 9: Snapshot::skill_attributes (before: empty = unknown, check `schema`).
inline constexpr int kSchema = 9;
inline constexpr int kMinSchema = 2;

struct CharAttribute {
  int type = 0;
  float total = 0, modifier = 0, base = 0;
  std::uint32_t points_spent = 0;
};

struct CharContribution {
  int type = 0;
  float value = 0, modifier = 0;
};

// `chance` is exact for one chance stat per type; with several, their raw-weighted mean.
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
  // Defense only: after ProcessDefense with no enemy reduction, plus the cap bonus at
  // CombatAttributeAbsDefense +0x2C.
  float processed = 0;
  float cap_bonus = 0;
  bool capped = false;  // processed by CombatAttributeAbsDefense::Process
  // Item stats only: the chance-based part at 100%; the fields above are expected values.
  std::optional<Proc> proc;
};

struct Damage {
  int type = 0;
  bool dot = false;    // GetDamageType() == 2 (CombatAttributeDurDamage*)
  float min = 0, max = 0;  // GetMinMaxValues
  float duration = 0;      // GetDuration (dot only)
  // Chance entries: min / max above are raw x chance / 100, as the engine computes them.
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

// Read in place at Item+0x538. Field names from ItemReplicaInfo::StreamProperties, else GD Stash's file
// order matched to WriteProperties (docs/RECON.md §8).
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

// As ItemEquipment::AreRequirementsMet 0x343C30 reads them. Each raw value is the max of item, relic and
// augment (GetIntelligenceRequirement 0x343BD0). Formula: gdc/requirements.h.
struct Requirements {
  std::uint32_t level = 0;                        // vt+0x528 GetLevelRequirement
  std::uint32_t attribute[3] = {};                // vt+0x530 / 0x538 / 0x540: Physique, Cunning, Spirit
  int reductions[3][3] = {};                      // Physique +0xDF8/+0xDEC/+0xE04, Cunning +0xDFC/+0xDF0/+0xE08,
                                                  // Spirit +0xE00/+0xDF4/+0xE0C
  std::optional<bool> met;                        // ItemEquipment::AreRequirementsMet(player) when read
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
  // Its own CollectPassiveCharAttributes (vt+0x408) / CollectPassiveDefenseAttributes (vt+0x438) now.
  // Absent before schema 4.
  std::optional<ItemStats> passive;
  // Skill::AddModifierDefenseAttributes 0x494970 walks these, then the item modifiers at +0x158. Schema 5.
  std::vector<std::uint32_t> modifier_ids;
  // Skill::GetItemSkillItemId 0x31D500 (Skill+0x350).
  std::uint32_t item_id = 0;
  // Schema 8. Character::IsSkillBuffActive 0x67DD0 looks up the SkillManager buff map (+0x100), filled and
  // erased by CreateRemoveSkillBuff 0x53A990 by the name it is given: the record or its buff record.
  std::optional<bool> buff_active;
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

// Read at Character+0x7A8 = CombatManager+0x278 (CombatManager at Character+0x530:
// BasePhysical::GetTotalDamage 0x117560; DesignerCalculateCriticalChance 0x686D0 reads +0x7AC / +0x7B8).
// Field order: CombatManager::LoadRecord 0x11D10E..0x11D265 (combatformulas.dbr). +0x27C is not in the record;
// the ctor 0x11BC3C sets it to 100.0.
struct PthTable {
  float minimum = 0;               // pthMinimum (+0x278)
  float crit_divisor_min = 0;      // +0x27C
  float threshold[6] = {};         // pthThreshold1..6 (+0x280, stride 8)
  float damage_modifier[6] = {};   // pthDamageModifier1..6 (+0x284, stride 8)
};

struct Snapshot {
  int schema = kSchema;
  std::string mod_version;
  std::string game_build;
  std::string taken_at;  // UTC, ISO 8601
  bool complete = true;  // false: see errors
  std::vector<std::string> errors;

  std::string character_name;  // UTF-8
  std::uint32_t level = 0;
  int difficulty = -1;  // GameEngine::GetGameDifficulty (raw enum)
  std::uint32_t skill_points = 0, attribute_points = 0, devotion_points = 0, devotion_points_total = 0;
  // SkillManager::GetNumDevotionPointsSpent 0x534D40 sums GetCurrentLevel over skills with Skill+0x4A0 != 0, so
  // celestial powers count their level. Points actually spent = sum of `level` over devotion skills.
  std::uint32_t devotion_current_level_sum = 0;
  float life_max = 0, mana_max = 0, attack_speed = 0, cast_speed = 0;
  float run_speed = 0, run_speed_uncapped = 0;  // Character::GetRunSpeed(false / true)
  float defense_cap = 0;                         // base max resist
  PthTable pth;

  std::vector<CharAttribute> char_attributes;
  std::vector<CombatTotals> defense;           // Character::GetAllDefenseAttributes
  std::vector<CombatTotals> damage_modifiers;  // SkillManager::CollectAvailableOffensiveModifierAttributes
  std::vector<Conversion> conversions;         // SkillManager::CollectAvailableConversionAttributes
  // Schema 9. SkillManager::CollectAvailableSkillAttributes 0x535BE0: equipped items (vt+0x4A0 via
  // GetEquipSkillAttributes 0x69910), the character's vt+0x6D8 / +0x718 / +0x750, and GetSkillAttributes 0x52FA90
  // (active learned skills, buffs, item skills). Engine total: SkillAttributeAccumulator::GetValue 0x51F1A0 =
  // value + |value| x modifier x 0.01.
  std::vector<SkillAttribute> skill_attributes;
  std::vector<Skill> skills;
  std::vector<ItemSkillModifier> item_skill_modifiers;
  // SkillManager::GetItemSkillList 0x531730 (+0x258). GetCharAttributes 0x52F920 / GetDefenseAttributes 0x530C10 /
  // GetOffensiveModifierAttributes 0x52FDA0 add each one's collectors while GetCurrentLevel != 0, so `passive` is
  // exactly what one adds. Schema 6.
  std::vector<Skill> item_skills;
  std::vector<Item> equipped;
  // A hovered item plus the equipped items the game shows next to it, in call order.
  std::vector<Item> tooltip_items;
  std::uint32_t tooltip_age_ms = 0;
  std::vector<Item> inventory;
};

std::string to_json(const Snapshot& s, int indent = 2);
// Throws std::runtime_error on malformed input or an unsupported schema.
Snapshot from_json(std::string_view text);

// Adapter: fewer allocations in the game-thread capture. reuse() hands back spares in the order recycle()
// saw them.
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
