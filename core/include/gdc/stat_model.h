#pragma once
// Defensive stat model for an equipment swap (PROMPT.md Slice A). Works on engine numbers from a
// snapshot: the character's totals and each item's own getter output (which already includes the
// item's relic/augment, affixes and stat rolls). A swap removes the old item's contributions and adds
// the candidate's, then re-derives what depends on them. Calibrated against the level-96 swap fixtures
// (tests/test_stat_model.cpp); what it cannot see is reported in Prediction::warnings.
#include <cstdint>
#include <optional>
#include <set>
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
  double absorption = 0;                         // type 39 processed (%), capped at 100 (Process 0x118370)
  std::vector<Resist> resists;                   // cappable types present in the snapshot, by type
};

// Every database record that makes up the item: base, affixes, transmute, components, augment (replica and
// component records).
std::set<std::string> item_records(const snap::Item& it);

// Sheet armor: (sum over armor slots of item armor x combatformulas.dbr region chance % + waist armor)
// x (1 + Protection (type 40) modifier / 100). Head 15, shoulders 15, torso 26, arms (hands) 12, legs 20,
// feet 12 (combatRegion*Chance). Inferred: matches the level-96 sheet (1408); the waist term is a fit.
double armor(const snap::Snapshot& s);

// Armor per hit region, as (chance of the region, armor that works there): the engine picks one region per
// attack (CombatManager::PickRegion 0x120D90, combatRegion*Chance) and an armor entry protects only its own
// region, or every region when it has none (CombatAttributeDefense_AbsorptionProtection::Execute 0x118400).
// Same terms as armor(): the six armor slots by region, the waist in every region (the sheet fit), all
// x (1 + Protection modifier / 100). Chances sum to 1.
std::vector<std::pair<double, double>> region_armor(const snap::Snapshot& s);

Sheet sheet(const snap::Snapshot& s);

// Absorption of every armor entry: the engine gives each one GameEngine::GetArmorDefensiveAbsorption
// (gameengine.dbr armorDefensiveAbsorption, 70 in 1.3.1.1; traced in swap()). The snapshot's type-39 total
// when it has one, else the database value; 0 when neither is available.
double base_absorption(const snap::Snapshot& s, const arz::Database* db);

struct SetCount {
  std::string set;  // set record (ItemEquipment::GetItemSetName)
  int before = 0, after = 0;  // equipped pieces (sets::counts)
  bool modeled = false;       // the bonus change at these counts is applied (set record in the database)
  std::string skill_lost, skill_gained;  // granted skill (itemSkillName) at the old / new count when it changes
  std::vector<std::string> unmodeled;    // bonus fields that change but are not applied (sets::Bonus::unmodeled)
};

struct SkillLevel {
  std::string record;
  std::uint32_t before = 0, after = 0;  // GetCurrentLevel
};

// A level change of a skill that contributes nothing to the sheet now (the snapshot shows it inactive). Its
// new level is in `after.skills`, so a reader of the skill's record (the score's main skills) still sees it.
struct InactiveLevel {
  std::string record;
  std::string parent;  // the parent skill for a Skill_Modifier, else empty
  std::uint32_t before = 0, after = 0;
  // > 0 (Mode::Expected only): a timed buff (skillActiveDuration) caught between casts; its stat change at the
  // new level is applied to `after` x this uptime (buff_uptime in stat_model.cpp). 0: not applied.
  double uptime = 0;
};

// A skill granted by the removed or the added item (itemSkillName on its records).
struct Granted {
  std::string record;
  bool gained = false;  // the candidate grants it (else the removed item did)
  bool scored = false;  // what it adds is applied to `after` (or it adds nothing now)
  std::string why;      // not scored: the reason ("weapon type", "level itemLevel/4+1", "Skill_AttackRadius", ...)
};

struct Prediction {
  snap::Snapshot after;               // `s` with the swap applied to totals and equipped items
  std::vector<SetCount> sets;         // sets whose equipped piece count changes
  std::vector<SkillLevel> skills;     // learned skills whose current level changes (item skill grants)
  std::vector<std::string> warnings;  // reasons the prediction may be off; empty = no known gap
  // Damage-bearing level changes of inactive skills: "+? unscored" in the tooltip unless the score reads
  // the skill (a main skill or its modifier).
  std::vector<InactiveLevel> inactive;
  std::vector<Granted> granted;
  // +levels to skills with no points spent (GetCurrentLevel stays 0): they add nothing (score 0). before /
  // after: the bonus levels.
  std::vector<SkillLevel> unused;
};

// Engine: what the engine's totals will show right after the swap (calibration). Expected: also the change of
// timed buffs caught between casts, at their uptime (InactiveLevel::uptime) — what the score weighs.
enum class Mode { Engine, Expected };

// Equip `candidate` (with stats) at `location`, replacing what is there. `db` (may be null) supplies what
// item stats do not show: skill / mastery grants (applied through the skills' records) and max-resist
// bonuses (detected only).
Prediction swap(const snap::Snapshot& s, const snap::Item& candidate, int location, const arz::Database* db,
                Mode mode = Mode::Engine);

// A timed buff switched on (`on`) or off: its stats record (the buffSkillName record or its own,
// SkillBuff_Passive / Skill_BuffSelfDuration with a skillActiveDuration) at the skill's current level, its
// learned modifiers at theirs and its item skill modifiers (stats and conversions), added to or taken from
// `s` and re-derived like a swap. nullopt: `sk` is not such a buff (or not learned), or a record is missing.
// The snapshot does not show whether such a buff runs (bloodofdreeg1 has no capture), so calibration uses
// this to tell a buff cast between two snapshots from a swap error (winterpoison store 20261114-174229-629
// #21 -> #22: Blood of Dreeg 3 + necklace_b001_bloodofdreeg = OA +26, regen 26 x 1.24, Aether -> Poison 100%).
std::optional<Prediction> toggle_buff(const snap::Snapshot& s, const snap::Skill& sk, bool on, const arz::Database& db);

}  // namespace gdc::model
