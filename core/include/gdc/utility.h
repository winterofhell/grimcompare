#pragma once
// Utility stats: the score's Other term (docs/MECHANICS.md §5, docs/PRESETS.md "Other").
//   Other = 1 + sum over keys of weight x value + default x sum of the unlisted stats
// Every key is one line in a preset's [other] table; a weight can need a context (`when`). Values come from
// the character's totals after model::swap and, where the engine has no character total in the snapshot,
// from the equipped items' own stats (skill attributes, offensive debuffs, retaliation) — skills' parts of
// those are not seen. Any stat type no key and no other score term reads is "unlisted": it counts at the
// `default` weight per 100 of its value and is named in the log / Shift view, never dropped.
#include <map>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "gdc/arz.h"
#include "gdc/snapshot.h"

namespace gdc::utility {

struct Stat {
  std::string_view key;
  std::string_view unit;   // what a value of 1 is (the preset comment)
  double weight;           // default weight (design choice, docs/PRESETS.md)
  std::string_view when;   // default condition ("" = always)
};
// The [other] keys in file order; "default" (the weight of every unlisted stat) last.
std::span<const Stat> stats();
const Stat* stat(std::string_view key);

// What a `when` condition can test.
struct Context {
  bool shield = false;  // L Hand (location 1) holds a WeaponArmor_Shield
  bool pets = false;    // a learned or item skill with points summons pets (Skill_SpawnPet / Skill_TargetedSpawnPet)
  std::string weapon;   // R Hand (location 0) record class, e.g. WeaponMelee_Sword; "" = none
  std::string offhand;  // L Hand record class (WeaponArmor_Shield, WeaponArmor_Offhand, ...); "" = none
  std::string stage;    // the preset stage in use; "" = none
};
Context context(const snap::Snapshot& s, const arz::Database* db, std::string stage);
// "" | "shield" | "offhand" | "pets" | "weapon:<Class>" | "stage:<name>"; a leading "!" negates.
bool valid_when(std::string_view when);
bool holds(std::string_view when, const Context& c);

struct Values {
  std::map<std::string, double> keys;      // key -> value in its unit
  std::map<std::string, double> unlisted;  // "char 56 IncreasedGold" -> value / 100
};
Values values(const snap::Snapshot& s, const arz::Database* db);

// Stages of the incoming-damage pipeline that Survivability reads (docs/MECHANICS.md §9), as fractions:
// the share of a hit of size `hit` the shield blocks at `hits_per_sec` incoming hits (0 without a shield);
// the chance to dodge / deflect an attack (char 27 DodgePercent for the `melee_share` of attacks, 28
// DeflectProjectile for the rest); the strongest "x% reduced target's damage" debuff (item offensive type
// 28, items only) — the guide: only the strongest of these takes effect.
double block(const snap::Snapshot& s, const arz::Database* db, double hit, double hits_per_sec);
double avoid(const snap::Snapshot& s, double melee_share);
double damage_reduction(const snap::Snapshot& s);

}  // namespace gdc::utility
