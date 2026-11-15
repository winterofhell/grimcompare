#pragma once
// defensive*: DefenseAttributeAbs_* / AbsMod_* +0x50 load field (re/DefenseAttributeFields.tsv). character*: the
// +0x40 string (re/CharAttributeType.tsv); CharAttributeMod_LifeMultiplier is not a plain % and left out.
// tests/test_db_fields.cpp checks the table against the TSVs.
#include <span>
#include <string_view>
#include <utility>

#include "gdc/arz.h"

namespace gdc::fields {

enum class Kind { Char, Defense, Damage };

struct Target {
  Kind kind;
  int type;
  bool modifier;
};

const Target* target(std::string_view field);
std::span<const std::pair<std::string_view, Target>> all();

// The engine spreads 58 / 60 over single types in damage_modifiers (fixture b002e_torso: +29% all damage shows on
// every type and as row 60); spread() does the same.
const Target* damage_modifier(std::string_view field);
std::span<const std::pair<std::string_view, Target>> damage_modifiers();
// Spread targets as seen in fixture b002e_torso.
std::span<const int> spread(int type, bool dot);

// Crit damage is a percent in records and a fraction in row 59 (witchfire2.dbr 22 -> 23 moved the row 0.36 ->
// 0.37), so `scale` converts.
const Target* lookup(std::string_view field, double& scale);

// offensivePhysicalMin/Max has no load field in re/CombatAttributeType.tsv but follows the pattern and 33 player
// skill records use it.
struct FlatDamage {
  std::string_view min, max, duration;  // "" = direct
  std::string_view chance;              // absent = always
  int type;
};
std::span<const FlatDamage> flat_damage();
// skill_base.tpl "Qualifying Weapons" (templates.arc). How the engine checks them is not traced.
bool weapon_flag(std::string_view field);
bool weapon_gated(const arz::Record& skill);
bool flat_damage_field(std::string_view field);

}  // namespace gdc::fields
