#pragma once
// Database field -> engine attribute, for the record fields the swap model reads from skill records.
//   defensive*   DefenseAttributeAbs_* (value) / DefenseAttributeAbsMod_* (modifier): the class's +0x50
//                load field in Game.dll (re/DefenseAttributeFields.tsv, re/tools/defense_fields.py), its
//                CombatAttributeType from re/CombatAttributeType.tsv
//   character*   CharAttributeVal_* (value) / CharAttributeMod_* (modifier): the +0x40 string of the class
//                (re/CharAttributeType.tsv). CharAttributeMod_LifeMultiplier is left out (not a plain %).
// Left out on purpose: durations, caps, the composite Elemental (58) / AllResistance (60) types and
// everything else; tests/test_db_fields.cpp checks the table against the TSVs.
#include <span>
#include <string_view>
#include <utility>

#include "gdc/arz.h"

namespace gdc::fields {

enum class Kind { Char, Defense, Damage };

struct Target {
  Kind kind;
  int type;       // CharAttributeType or CombatAttributeType
  bool modifier;  // accumulator modifier (%) rather than value
};

// nullptr when `field` is not in the table.
const Target* target(std::string_view field);
std::span<const std::pair<std::string_view, Target>> all();

// offensive* % fields of the damage types: DamageAttributeAbsMod_* (Kind::Damage, modifier false = direct)
// and DamageAttributeDurMod_* (modifier true = duration) LoadModifier fields from re/CombatAttributeType.tsv,
// for types 2, 4..11, 15, 58 (Elemental) and 60 (TotalDamage). In the character's damage_modifiers the
// engine already spreads 58 / 60 over the single types (fixture b002e_torso: +29% all damage appears on every
// type and as row 60); callers spread them the same way (spread()).
const Target* damage_modifier(std::string_view field);
std::span<const std::pair<std::string_view, Target>> damage_modifiers();
// Types that a 58 / 60 modifier lands on, by direct (dot false) or duration: 60 -> every type that has that
// kind of row in fixture b002e_torso (direct 2, 4..11; duration 2, 5..11, 15); 58 -> 5, 6, 8.
std::span<const int> spread(int type, bool dot);

// target(), else damage_modifier(), else offensiveCritDamageModifier -> (Damage, 59, false); `scale` gets the
// factor from the record's unit to the snapshot's: crit damage is a percent in records and a fraction in the
// type-59 row (DamageAttributeAbsMod_CritDamageModifier; witchfire2.dbr 22 -> 23 at levels 14 -> 15 moved the
// row 0.36 -> 0.37, store 20261114-172933-952 records 1 -> 2), 1 for everything else.
const Target* lookup(std::string_view field, double& scale);

// Flat damage fields: DamageAttributeAbs_* / AbsBase_* (direct: LoadValueMin / LoadValueMax / LoadChance) and
// DamageAttributeDur_* (duration: LoadValueMin / LoadValueMax / LoadDurationMin / LoadChance) from
// re/CombatAttributeType.tsv,
// plus offensivePhysicalMin/Max: DamageAttribute_Physical has no load field in that table, the name follows the
// pattern and 33 player skill records use it (database.arz 1.3.1.1).
struct FlatDamage {
  std::string_view min, max, duration;  // duration "" = direct
  std::string_view chance;              // "x% chance of" (percent); absent = always
  int type;                             // CombatAttributeType; 58 = Elemental
};
std::span<const FlatDamage> flat_damage();
// skill_base.tpl group "Qualifying Weapons" (database/templates.arc): a skill with one of these fields set
// works only with those weapons. How the engine checks them is not traced.
bool weapon_flag(std::string_view field);
bool weapon_gated(const arz::Record& skill);
// Whether `field` is one of flat_damage()'s min / max / duration / chance fields.
bool flat_damage_field(std::string_view field);

}  // namespace gdc::fields
