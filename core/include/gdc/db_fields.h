#pragma once
// Load field tables, checked against re/*.tsv by tests.
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

// Engine spreads 58 / 60 over single types.
const Target* damage_modifier(std::string_view field);
std::span<const std::pair<std::string_view, Target>> damage_modifiers();
// Spread targets as seen in fixture b002e_torso.
std::span<const int> spread(int type, bool dot);

// Crit damage: percent in records, fraction in row 59.
const Target* lookup(std::string_view field, double& scale);

// No TSV load field, but 33 skills use it.
struct FlatDamage {
  std::string_view min, max, duration;  // "" = direct
  std::string_view chance;              // absent = always
  int type;
};
std::span<const FlatDamage> flat_damage();
// skill_base.tpl "Qualifying Weapons"; engine check not traced.
bool weapon_flag(std::string_view field);
bool weapon_gated(const arz::Record& skill);
bool flat_damage_field(std::string_view field);

}  // namespace gdc::fields
