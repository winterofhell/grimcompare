#pragma once
// Every effect has uses per second and an uptime in [0, 1] tagged with its origin (MECHANICS §1, §4).
// DB sources (ERRATA §0, re/CombatAttributeType.tsv):
// RR type 1: defensive<Type> < 0 on a debuff record (debufSkill > 0 or SkillBuff_Debuf), duration skillActiveDuration
// RR type 2 / 3: offensive{Total,Physical,Elemental}ResistanceReduction{Percent,Absolute}Min (32..37), duration
// the field's DurationMin
// procs: templateAutoCast / itemSkillAutoController -> chanceToRun, triggerType, triggerParam; passives:
// lifeMonitorPercent, onHitActivationChance, skillChanceWeight (Skill_WPAttack_*)
// pets: spawnObjects, petLimit, spawnObjectsTimeToLive, the pet's attackSkillName and skillName<n>
#include <algorithm>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "gdc/arz.h"
#include "gdc/snapshot.h"

namespace gdc::effects {

enum class Origin : std::uint8_t { Derived, Preset, Default };  // Default: fallback 0, never silent

struct Rates {
  double action = 0;      // AttackEnemy
  double crit = 0;        // AttackEnemyCrit
  double weapon = 0;      // Skill_WPAttack_*
  double hits_taken = 0;  // HitByEnemy
  double melee_hits = 0;  // HitByMelee
  double projectile_hits = 0;  // HitByProjectile
  double crit_hits = 0;   // HitByCrit
  double blocks = 0;      // Block
  double low_health = 0;
  double kills = 0;       // OnKill
  double pet_attacks = 0; // 0 = unknown
};

struct Effect {
  std::string record;
  std::string skill;
  std::string source;
  std::uint32_t level = 0;
  bool pet = false;
  bool player_scaled = false;  // PetPlayerScaling: the player's bonuses, no pet bonuses
  bool maintained = false; // no cooldown: assumed kept up
  double per_sec = 0;      // pets: all living pets; 0 = never / unknown
  double pets = 0;
  // LowHealth: controller triggerParam or lifeMonitorPercent. 0 when the controller has none (the engine's
  // default threshold is not traced).
  double life_below = 0;
  Origin origin = Origin::Default;
  std::string basis;
  std::vector<std::pair<std::string, std::uint32_t>> modifiers;
};

struct Reduction {
  std::string record;
  int category = 1;
  std::map<int, double> amount;
  double uptime = 0;
  Origin origin = Origin::Default;
  std::string basis;
};

struct Collected {
  std::vector<Effect> effects;
  std::vector<Reduction> reductions;
};

Collected collect(const snap::Snapshot& s, const arz::Database& db, const Rates& rates,
                  const std::map<std::string, double>& overrides, const std::vector<int>& resists);

// Pet: Pet::ContributeMisc* (0x3C7C10..) add GameEngine::ContributePetBonus*, so pet bonuses apply.
// PetPlayerScaling overrides all seven ContributeMisc* (0x3C8840..0x3C88B0) to Character's, so no pet bonus, and
// Skill::CollectCombatParameters 0x499E40 swaps its caster for the owning Player.
const arz::Record* summoned(const arz::Record& skill, std::uint32_t level, const arz::Database& db);

inline double uptime(const Effect& e, double duration) {
  return e.maintained ? 1.0 : std::min(1.0, e.per_sec * std::max(0.0, duration));
}

// Type 1 adds value x uptime; types 2 and 3 are highest-only, each source active with probability
// u_k x prod over stronger (1 - u_j), sources independent (MECHANICS §4). Order 1 -> 2 -> 3; type 2 only while the
// resist is above 0 (MECHANICS §1).
std::map<int, double> resist_multipliers(const std::map<int, double>& enemy, const std::vector<Reduction>& rr);

}  // namespace gdc::effects
