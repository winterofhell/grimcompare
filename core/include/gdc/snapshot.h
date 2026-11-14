#pragma once
// Snapshot of what the engine reports for the main player, written by the adapter on a hotkey
// and read back by core for replay and tests. Every number is copied from an engine getter
// (docs/SNAPSHOT.md lists the source of each field); nothing here is recomputed.
// Zero / empty fields are omitted from the JSON and read back as zero / empty.
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
inline constexpr int kSchema = 7;
inline constexpr int kMinSchema = 2;

// Character::GetTotalCharAttribute / GetTotalCharModifier / GetBaseCharAttribute,
// Character::GetPointsSpent (attribute points; meaningful for types 1..3).
struct CharAttribute {
  int type = 0;
  float total = 0, modifier = 0, base = 0;
  std::uint32_t points_spent = 0;
};

// One item's raw CharAttributeAccumulator arrays (values at +0, modifiers at +0xE8).
struct CharContribution {
  int type = 0;
  float value = 0, modifier = 0;
};

// Chance-based part of an item's totals for one type, at 100%. `chance` (percent) is exact for one
// chance stat per type; with several, it is their raw-weighted mean for the first field below that has one.
struct Proc {
  float chance = 0;
  float defense = 0, defense_modifier = 0, reduction = 0, damage_modifier = 0, dot_modifier = 0;
};

// CombatAttributeAccumulator aggregate getters for one CombatAttributeType.
struct CombatTotals {
  int type = 0;
  float defense = 0;           // GetTotalDefenseType
  float defense_modifier = 0;  // GetTotalDefenseModifierType
  float reduction = 0;         // GetTotalReduction
  float damage_modifier = 0;   // GetTotalDamageModifierType(..., DamageType 1 = direct)
  float dot_modifier = 0;      // GetTotalDamageModifierType(..., DamageType 2 = duration)
  // Character defense only: GetTotalDefenseType after ProcessDefense with no enemy reduction
  // (modifiers applied, capped), and the per-type cap bonus (CombatAttributeAbsDefense +0x2C).
  float processed = 0;
  float cap_bonus = 0;
  bool capped = false;  // an entry of this type is processed by CombatAttributeAbsDefense::Process (capped)
  // Item stats only: the chance-based ("x% chance of ...") part of this type. The fields above are
  // expected values (chance-based stats at chance %); these are that part at 100%.
  std::optional<Proc> proc;
};

// One CombatAttribute* from an accumulator filled by a damage getter called with scale 1.0.
struct Damage {
  int type = 0;
  bool dot = false;    // GetDamageType() == 2 (CombatAttributeDurDamage*)
  float min = 0, max = 0;  // GetMinMaxValues
  float duration = 0;      // GetDuration (dot only)
  // Item stats only, chance-based entries ("x% chance of ..."): chance in percent and min/max at 100%.
  // min/max above are the expected values (raw * chance / 100, as the engine computes them).
  float chance = 0, raw_min = 0, raw_max = 0;
};

// SkillAttributeAccumulator slot for one SkillAttributeType (float[5] arrays at +8/+0x1C/+0x30/+0x44).
struct SkillAttribute {
  int type = 0;
  float value = 0, chance_value = 0, modifier = 0, chance_modifier = 0;
};

// ConversionAttributeAccumulator entry: `percent` of `from` damage converted to `to`.
struct Conversion {
  int from = 0, to = 0;
  float percent = 0;
};

// ItemReplicaInfo, read in place at Item+0x538 (Item::GetItemReplicaInfo). Field names:
// ItemReplicaInfo::StreamProperties keys where it has one, otherwise GD Stash's file order
// matched to WriteProperties' order (docs/RECON.md §8).
struct Replica {
  std::string base, prefix, suffix, modifier, transmute, relic, relic_bonus, augment, ascendant,
      ascendant2h;
  std::uint32_t seed = 0, relic_seed = 0, augment_level = 0, augment_seed = 0, var1 = 0,
                stack_count = 0, affix_rerolls = 0, rerolls = 0;
};

// Per-accumulator output of the item's own virtual getters (or a skill modifier's collectors).
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

// An ItemEquipment's requirements as ItemEquipment::AreRequirementsMet (0x343C30) reads them (Game.dll 1.3.1.1):
// raw values from the item's virtual getters (each the max of the item, its relic and its augment:
// GetIntelligenceRequirement 0x343BD0), and per attribute the item's three CharAttributeType ids whose
// character totals reduce it (0 = none; gdc/requirements.h has the formula).
struct Requirements {
  std::uint32_t level = 0;                        // vt+0x528 GetLevelRequirement
  std::uint32_t attribute[3] = {};                // vt+0x530 / 0x538 / 0x540: Physique, Cunning, Spirit
  int reductions[3][3] = {};                      // Physique +0xDF8/+0xDEC/+0xE04, Cunning +0xDFC/+0xDF0/+0xE08,
                                                  // Spirit +0xE00/+0xDF4/+0xE0C
  std::optional<bool> met;                        // ItemEquipment::AreRequirementsMet(player) when read
};

struct Item {
  std::uint32_t id = 0;           // Object::GetObjectId
  int location = -1;              // EquipLocations from EquipManagerContainer+4; -1 = not equipped
  std::string record;             // Object::GetObjectName
  std::string kind;               // most derived of ItemEquipment, ItemArtifact, ItemRelic, ItemEnchantment, Item
  std::string set;                // ItemEquipment::GetItemSetName; "" = no set
  std::optional<Replica> replica;
  std::optional<ItemStats> stats;
  std::optional<Component> relic, augment;  // ItemEquipment::GetRelic / GetEnchantment
  std::optional<Requirements> requirements;  // ItemEquipment only; schema 7
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
  // What the skill adds to the character right now: its own CollectPassiveCharAttributes (vt+0x408) and
  // CollectPassiveDefenseAttributes (vt+0x438) into fresh accumulators (char_attributes / defense only).
  // Empty = contributes nothing now (an inactive buff, a toggle that is off, an attack skill). Absent
  // before schema 4; from schema 4 an omitted "passive" reads as empty.
  std::optional<ItemStats> passive;
  // Skill+0x140 mem::vector<u32> (Skill::GetModifiers 0x3C5970, appended by Skill::AddModifier 0x48FD00): ids
  // of the learned Skill_Modifier skills (and transmuters) whose stats this skill's collectors add
  // (Skill::AddModifierDefenseAttributes 0x494970 walks it, then the item skill modifiers at +0x158).
  // Schema 5; empty before.
  std::vector<std::uint32_t> modifier_ids;
  // Item skills only: Skill::GetItemSkillItemId (0x31D500, Skill+0x350), the granting item's object id.
  std::uint32_t item_id = 0;
};

// An item skill modifier attached to a skill (Skill+0x158 entry, Skill::AddToItemSkillModifiers).
struct ItemSkillModifier {
  std::uint32_t skill_id = 0;      // the modified skill
  std::string skill_record;
  std::uint32_t modifier_id = 0;   // the Skill_Modifier object
  std::string modifier_record;
  int location = -1;               // EquipLocations of the granting item
  bool from_relic = false, from_augment = false;
  std::optional<ItemStats> stats;  // Skill_Modifier::CollectModifier* (as the skill would add them)
};

// CombatManager's probability-to-hit table, read in place at Character+0x7A8 (= CombatManager+0x278;
// CombatManager is embedded at Character+0x530: CombatAttributeDamage_BasePhysical::GetTotalDamage
// (0x117560), Character::DesignerCalculateCriticalChance (0x686D0) reads +0x7AC / +0x7B8).
// Field order: CombatManager::LoadRecord (0x11D10E..0x11D265) from records/game/combatformulas.dbr;
// +0x27C is not in the record, CombatManager::CombatManager (0x11BC3C) sets it to 100.0.
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
  bool complete = true;  // false: a section faulted or was unavailable, see errors
  std::vector<std::string> errors;

  std::string character_name;  // Player::GetPlayerName (UTF-16 in the engine), UTF-8
  std::uint32_t level = 0;
  int difficulty = -1;  // GameEngine::GetGameDifficulty (raw enum)
  std::uint32_t skill_points = 0, attribute_points = 0, devotion_points = 0, devotion_points_total = 0;
  // SkillManager::GetNumDevotionPointsSpent (0x534D40): despite the name, the sum of Skill::GetCurrentLevel
  // over skills with Skill+0x4A0 != 0 (devotion nodes count 1, celestial powers their level). Points
  // actually spent = the sum of `level` over devotion skills.
  std::uint32_t devotion_current_level_sum = 0;
  float life_max = 0, mana_max = 0, attack_speed = 0, cast_speed = 0;  // Character::GetLifeLimit, ...
  float run_speed = 0, run_speed_uncapped = 0;  // Character::GetRunSpeed(false / true)
  float defense_cap = 0;                         // Player::GetDefenseAttributeCap (base max resist)
  PthTable pth;

  std::vector<CharAttribute> char_attributes;
  std::vector<CombatTotals> defense;           // Character::GetAllDefenseAttributes
  std::vector<CombatTotals> damage_modifiers;  // SkillManager::CollectAvailableOffensiveModifierAttributes
  std::vector<Conversion> conversions;         // SkillManager::CollectAvailableConversionAttributes
  std::vector<Skill> skills;
  std::vector<ItemSkillModifier> item_skill_modifiers;
  // Skills granted by equipped items (itemSkillName): SkillManager::GetItemSkillList (0x531730,
  // SkillManager+0x258). SkillManager::GetCharAttributes (0x52F920) / GetDefenseAttributes (0x530C10) /
  // GetOffensiveModifierAttributes (0x52FDA0) add each one's collectors (vt+0x408 / +0x438 / +0x420) while
  // Skill::GetCurrentLevel (vt+0x1C8) != 0, as for the learned skills, so `passive` is exactly what one
  // adds to the totals. Schema 6; empty before (unknown: check `schema`).
  std::vector<Skill> item_skills;
  std::vector<Item> equipped;
  // Items whose tooltip the engine built in the most recent frame that built any, in call order
  // (a hovered item plus the equipped items the game shows for comparison).
  std::vector<Item> tooltip_items;
  std::uint32_t tooltip_age_ms = 0;
  std::vector<Item> inventory;
};

// indent < 0: compact (one line, no spaces).
std::string to_json(const Snapshot& s, int indent = 2);
// Throws std::runtime_error on malformed input or an unsupported schema.
Snapshot from_json(std::string_view text);

// Reuse of a snapshot (adapter: fewer allocations in the game-thread capture; strings are most of them).
// recycle() resets `s` to Snapshot{} except that strings and vectors of plain values keep their capacity,
// and moves every Skill / Item / ItemSkillModifier into `spare`, itself reset the same way (optional
// members are emptied, their storage freed). reuse() pops a spare element (or returns a new one), in the
// order recycle() saw them (equipped, tooltip_items, inventory share `items`).
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

// The float's shortest decimal representation, as a double: 0.1f -> 0.1 (not 0.10000000149).
double widen(float f);

}  // namespace gdc::snap
