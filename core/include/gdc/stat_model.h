#pragma once
// Swap model on engine numbers: an item's getter output already includes relic, augment, affixes and rolls.
// Calibrated against the level-96 swap fixtures.
#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "gdc/arz.h"
#include "gdc/db_fields.h"
#include "gdc/snapshot.h"

namespace gdc::model {

struct Resist {
  int type = 0;
  double raw = 0;
  double uncapped = 0;  // Process 0x115370
  double shown = 0;     // the sheet value
  double max = 0;
};

struct Sheet {
  double physique = 0, cunning = 0, spirit = 0;
  double life = 0, energy = 0;
  double life_regen = 0, energy_regen = 0;
  double oa = 0, da = 0;
  double run_speed = 0;                          // 1.0 = 100%; cap not modeled
  double armor = 0;
  double absorption = 0;                         // capped at 100 (Process 0x118370)
  std::vector<Resist> resists;
};

std::set<std::string> item_records(const snap::Item& it);

// (sum of item armor x combatRegion*Chance + waist armor) x (1 + Protection modifier / 100).
// Inferred: matches the level-96 sheet (1408); the waist term is a fit.
double armor(const snap::Snapshot& s);

// CombatManager::PickRegion 0x120D90 picks one region per attack; an armor entry protects its own region, or all
// without one (AbsorptionProtection::Execute 0x118400). Chances sum to 1.
std::vector<std::pair<double, double>> region_armor(const snap::Snapshot& s);

Sheet sheet(const snap::Snapshot& s);

// gameengine.dbr armorDefensiveAbsorption (70 in 1.3.1.1). Snapshot's type-39 total first, else the database.
double base_absorption(const snap::Snapshot& s, const arz::Database* db);

struct SetCount {
  std::string set;
  int before = 0, after = 0;
  bool modeled = false;
  std::string skill_lost, skill_gained;
  std::vector<std::string> unmodeled;
};

struct SkillLevel {
  std::string record;
  std::uint32_t before = 0, after = 0;
};

// Its new level is in `after.skills`, so the score's main skills still see it.
struct InactiveLevel {
  std::string record;
  std::string parent;
  std::uint32_t before = 0, after = 0;
  bool timed = false;
};

struct Granted {
  std::string record;
  bool gained = false;
  bool scored = false;
  std::string why;
};

struct Prediction {
  snap::Snapshot after;
  std::vector<SetCount> sets;
  std::vector<SkillLevel> skills;
  std::vector<std::string> warnings;
  std::vector<InactiveLevel> inactive;
  std::vector<Granted> granted;
  // GetCurrentLevel stays 0 for skills with no points, so they add nothing.
  std::vector<SkillLevel> unused;
};

// `db` may be null; it supplies skill / mastery grants and max-resist bonuses.
Prediction swap(const snap::Snapshot& s, const snap::Item& candidate, int location, const arz::Database* db);

// The snapshot does not show whether such a buff runs, so calibration uses this to tell a buff cast between
// two snapshots from a swap error.
std::optional<Prediction> toggle_buff(const snap::Snapshot& s, const snap::Skill& sk, bool on, const arz::Database& db);

bool self_buff(const arz::Record& r);

// Class skills only (records/skills/playerclass*): the skill list also holds consumables. Recharge 0: none.
struct BuffTiming {
  double duration = 0, recharge = 0;
};
std::optional<BuffTiming> buff_timing(const snap::Skill& sk, const arz::Database& db);

// Skill_PassiveOnLifeBuffSelf (willtolive1), Skill_PassiveOnHitBuffSelf (fightingspirit1): scored like procs.
bool triggered_buff(const arz::Record& r);

// Takes every temporary buff out of the totals so a character scores the same whether or not a buff ran at
// capture. Needs schema 8; older snapshots come back unchanged. Idempotent.
snap::Snapshot expected(const snap::Snapshot& s, const arz::Database* db);

using Contribution = std::map<std::tuple<fields::Kind, int, bool>, double>;

struct BuffEffect {
  Contribution stats;
  std::vector<snap::Conversion> conversions;
  // Listed in the Shift view only: the snapshot's skill attribute totals are as captured.
  std::map<std::string, double> unmodeled;
  bool empty() const { return stats.empty() && conversions.empty(); }
};
BuffEffect buff_effect(const arz::Record& stats, std::uint32_t level);
BuffEffect buff_effect(const snap::Snapshot& s, const snap::Skill& learned, const arz::Database& db);

// `out` must be a copy of `s` or a previous result for the same `s`: only totals are rewritten.
void buffed(const snap::Snapshot& s, std::span<const std::pair<const BuffEffect*, double>> buffs,
            const arz::Database& db, snap::Snapshot& out);

}  // namespace gdc::model
