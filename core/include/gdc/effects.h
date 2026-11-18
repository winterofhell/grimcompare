#pragma once
// Effects: uses per second and uptime (MECHANICS §1, §4).
#include <algorithm>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
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
  double pet_attacks = 0; // preset pet_attack_rate per pet; 0 = record estimate
  double pet_attack_speed = 0, pet_cast_speed = 0;  // pet bonus %, Class Pet only
  // Pet bonus % change since the live pets' capture.
  double pet_attack_shift = 0, pet_cast_shift = 0;
  double char_level = 0;  // pets' charLevel (the player's level)
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
  // LowHealth threshold; 0 when the controller has none.
  double life_below = 0;
  Origin origin = Origin::Default;
  std::string basis;
  std::vector<std::pair<std::string, std::uint32_t>> modifiers;
  // Pets: the pet record and skills holding its weapon damage.
  std::string pet_record;
  std::vector<std::pair<std::string, std::uint32_t>> weapon;
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

// Pet stats get pet bonuses; PetPlayerScaling uses the owner's.
const arz::Record* summoned(const arz::Record& skill, std::uint32_t level, const arz::Database& db);
std::string summoned_record(const arz::Record& skill, std::uint32_t level);  // "": not a summon

// Parses a DB equation of charLevel; nullopt otherwise.
std::optional<double> equation(std::string_view expr, double char_level);

// A pet's skills and levels, incl. the pet modifier's.
std::vector<std::pair<std::string, std::uint32_t>> pet_skills(const arz::Record& pet, double char_level,
                                                              const std::vector<std::pair<std::string, std::uint32_t>>& modifiers,
                                                              const arz::Database& db);

inline double uptime(const Effect& e, double duration) {
  return e.maintained ? 1.0 : std::min(1.0, e.per_sec * std::max(0.0, duration));
}

// RR stacking: type 1 sums, 2 / 3 highest-only.
std::map<int, double> resist_multipliers(const std::map<int, double>& enemy, const std::vector<Reduction>& rr);

}  // namespace gdc::effects
