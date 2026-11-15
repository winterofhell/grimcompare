#pragma once
// Other = 1 + sum of weight x value + default x sum of unlisted stats (MECHANICS §5). Unlisted stats count at
// `default` per 100 and are always named, never dropped.
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
  std::string_view unit;
  double weight;           // design choice
  std::string_view when;   // "" = always
};
// "default" last.
std::span<const Stat> stats();
const Stat* stat(std::string_view key);

struct Context {
  bool shield = false;
  bool pets = false;    // Class Pet only; PetPlayerScaling summons take no pet bonus
  std::string weapon;
  std::string offhand;
  std::string stage;
};
Context context(const snap::Snapshot& s, const arz::Database* db, std::string stage);
// A leading "!" negates.
bool valid_when(std::string_view when);
bool holds(std::string_view when, const Context& c);

struct Values {
  std::map<std::string, double> keys;
  std::map<std::string, double> unlisted;  // value / 100
};
Values values(const snap::Snapshot& s, const arz::Database* db);
// For callers that evaluate one character under several buff states.
Values item_values(const snap::Snapshot& s, const arz::Database* db);
Values values(const snap::Snapshot& s, Values items);
// Skill::GetCooldownTime 0x4917E0 = skillCooldownTime x 1000 x (1 - 0.01 x CDR), skipped for item skills
// (Skill+0x2F9) and potion skills (Skill+0x2FC): MECHANICS §4 "Recharge".
double cooldown_reduction(const snap::Snapshot& s);

// Only the strongest "reduced target's damage" debuff applies (the guide).
double block(const snap::Snapshot& s, const arz::Database* db, double hit, double hits_per_sec);
// At most one block per recovery time.
double block_share(const snap::Snapshot& s, const arz::Database* db, double hits_per_sec);
double avoid(const snap::Snapshot& s, double melee_share);
double damage_reduction(const snap::Snapshot& s);

}  // namespace gdc::utility
