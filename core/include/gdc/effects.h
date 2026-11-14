#pragma once
// Conditional effects (docs/MECHANICS.md §4): the skills that act on the target besides the main skills —
// debuffs of learned skills, devotion celestial powers, item- and set-granted procs and pets — with the
// resistance reduction (RR, MECHANICS §1) they apply and how much of the time it is up. One mechanism for all:
// every effect has uses per second and an uptime in [0, 1] with its origin (derived / preset / default).
//
// Sources (database fields; ERRATA §0, re/CombatAttributeType.tsv):
//   RR type 1   defensive<Type> < 0 on a debuff record (debufSkill > 0 or class SkillBuff_Debuf), its learned
//               modifiers (Skill::modifier_ids) and item skill modifiers; duration skillActiveDuration
//   RR type 2   offensive{Total,Physical,Elemental}ResistanceReductionPercentMin (types 32 / 34 / 36)
//   RR type 3   offensive{Total,Physical,Elemental}ResistanceReductionAbsoluteMin (types 33 / 35 / 37)
//               duration: the field's DurationMin
//   procs       templateAutoCast (devotion powers) / itemSkillAutoController (items, sets): controller record
//               chanceToRun (%) and triggerType
//   pets        Skill_SpawnPet / Skill_TargetedSpawnPet: spawnObjects (pet record), petLimit,
//               spawnObjectsTimeToLive; the pet's attackSkillName and skillName<n> / skillLevel<n>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "gdc/arz.h"
#include "gdc/snapshot.h"

namespace gdc::effects {

enum class Origin : std::uint8_t { Derived, Preset, Default };  // Default: fallback 0 (never silent)

// Trigger rates the model supplies (per second).
struct Rates {
  double action = 0;      // the character's attacks / casts (AttackEnemy)
  double crit = 0;        // share of those that crit (AttackEnemyCrit)
  double hits_taken = 0;  // incoming hits (HitByEnemy): preset incoming_hits_per_sec
  double pet_attacks = 0; // attacks per second of one pet: preset pet_attack_rate (0 = unknown, fallback)
};

// One skill acting on the target.
struct Effect {
  std::string record;      // the record whose fields apply (debuff, proc attack, pet attack skill)
  std::string skill;       // the skill that is cast / procs / summons the pet
  std::string source;      // what grants it: item / set / devotion skill record, "learned"
  std::uint32_t level = 0; // level for `record`'s per-level arrays
  bool pet = false;
  bool maintained = false; // learned or activated skill without a cooldown: assumed kept up (uptime 1)
  double per_sec = 0;      // uses per second (pets: attacks of all living pets); 0 = never / unknown
  double pets = 0;         // pets: expected number alive
  Origin origin = Origin::Default;
  std::string basis;       // how per_sec came about ("15% on attack x 1.80/s, cooldown 1 s"); Default: why unknown
  std::vector<std::pair<std::string, std::uint32_t>> modifiers;  // records (and levels) adding to `record`
};

// RR category per docs/MECHANICS.md §1.
struct Reduction {
  std::string record;  // the effect's record
  int category = 1;
  std::map<int, double> amount;  // resist type (CombatAttributeType) -> reduction (> 0)
  double uptime = 0;
  Origin origin = Origin::Default;
  std::string basis;
};

struct Collected {
  std::vector<Effect> effects;
  std::vector<Reduction> reductions;
};

// Every effect of `s` (equipped items, set bonuses at their piece count, learned skills, devotions) and the RR
// they apply. `overrides`: preset [uptime], effect record -> uptime (applies to that effect's RR).
// `resists`: the resist types RR "to all" lands on (the enemy model's).
Collected collect(const snap::Snapshot& s, const arz::Database& db, const Rates& rates,
                  const std::map<std::string, double>& overrides, const std::vector<int>& resists);

// Expected damage multiplier (1 - resist / 100) per resist type for an enemy with `enemy` resists under
// `rr`: type 1 adds (value x uptime), types 2 and 3 are highest-only, each source active with probability
// u_k x prod over stronger (1 - u_j) (MECHANICS §4, sources assumed independent); order 1 -> 2 -> 3, type 2
// only while the resist is above 0 (MECHANICS §1).
std::map<int, double> resist_multipliers(const std::map<int, double>& enemy, const std::vector<Reduction>& rr);

}  // namespace gdc::effects
