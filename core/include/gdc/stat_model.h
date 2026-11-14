#pragma once
// Defensive stat model for an equipment swap (PROMPT.md Slice A). Works on engine numbers from a
// snapshot: the character's totals and each item's own getter output (which already includes the
// item's relic/augment, affixes and stat rolls). A swap removes the old item's contributions and adds
// the candidate's, then re-derives what depends on them. Calibrated against the level-96 swap fixtures
// (tests/test_stat_model.cpp); what it cannot see is reported in Prediction::warnings.
#include <cstdint>
#include <string>
#include <vector>

#include "gdc/arz.h"
#include "gdc/snapshot.h"

namespace gdc::model {

struct Resist {
  int type = 0;
  double raw = 0;       // GetTotalDefenseType, unprocessed
  double uncapped = 0;  // raw + |raw| * modifier% (CombatAttributeAbsDefense::Process 0x115370)
  double shown = 0;     // min(uncapped, defense_cap + cap_bonus): the sheet value
  double max = 0;       // defense_cap + cap_bonus
};

// The numbers the tooltip block compares.
struct Sheet {
  double physique = 0, cunning = 0, spirit = 0;  // CharAttributeType 1..3 totals
  double life = 0, energy = 0;                   // types 4, 5
  double life_regen = 0, energy_regen = 0;       // formulas::life_regen / mana_regen
  double oa = 0, da = 0;                         // formulas::offensive_ability / defensive_ability
  double run_speed = 0;                          // type 14 total (1.0 = 100%); cap not modeled
  double armor = 0;                              // armor(), the sheet's Armor
  std::vector<Resist> resists;                   // cappable types present in the snapshot, by type
};

// Sheet armor: (sum over armor slots of item armor x combatformulas.dbr region chance % + waist armor)
// x (1 + Protection (type 40) modifier / 100). Head 15, shoulders 15, torso 26, arms (hands) 12, legs 20,
// feet 12 (combatRegion*Chance). Inferred: matches the level-96 sheet (1408); the waist term is a fit.
double armor(const snap::Snapshot& s);

Sheet sheet(const snap::Snapshot& s);

struct SetCount {
  std::string set;  // set record (ItemEquipment::GetItemSetName)
  int before = 0, after = 0;
};

struct SkillLevel {
  std::string record;
  std::uint32_t before = 0, after = 0;  // GetCurrentLevel
};

struct Prediction {
  snap::Snapshot after;               // `s` with the swap applied to totals and equipped items
  std::vector<SetCount> sets;         // sets whose equipped piece count changes
  std::vector<SkillLevel> skills;     // learned skills whose current level changes (item skill grants)
  std::vector<std::string> warnings;  // reasons the prediction may be off; empty = no known gap
};

// Equip `candidate` (with stats) at `location`, replacing what is there. `db` (may be null) supplies what
// item stats do not show: skill / mastery grants (applied through the skills' records) and max-resist
// bonuses (detected only).
Prediction swap(const snap::Snapshot& s, const snap::Item& candidate, int location, const arz::Database* db);

}  // namespace gdc::model
