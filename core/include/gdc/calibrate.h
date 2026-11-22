#pragma once
// Equipment-only snapshot pairs replayed through model::swap, checked against engine.
#include <string>
#include <vector>

#include "gdc/arz.h"
#include "gdc/score.h"
#include "gdc/snapshot.h"

namespace gdc::calibrate {

struct Diff {
  std::string stat;
  double predicted = 0, actual = 0, tolerance = 0;
};

struct Check {
  std::string skipped;             // non-empty: not comparable, why
  std::vector<int> locations;
  std::vector<std::string> warnings;
  std::vector<std::string> notes;
  std::vector<Diff> diffs;            // empty = pass
  // Passes when one learned timed buff toggling explains every diff.
  std::string buff;
};

// Tolerances: integers +-1, resists and regen +-0.1, run speed +-0.1%.
Check check(const snap::Snapshot& before, const snap::Snapshot& after, const arz::Database* db);

std::string swapped(const Check& c, const snap::Snapshot& before, const snap::Snapshot& after);

std::vector<std::string> report(const Check& c, const snap::Snapshot& before, const snap::Snapshot& after,
                                const std::string& label, bool verbose = false);

// Engine damage check (snapshot schema 15): the model's damage per use of each attack skill, per type, against
// Player::CalculateSkillDps in the same snapshot. Uses the snapshot as captured, buffs up as they were. Schema 16: the
// L Hand pass of a dual wielder, the damage reductions on the character.
struct TypeDiff {
  score::DamageKey key;  // Elemental split in thirds, as the model does
  double model = 0, engine = 0;
  double engine_seconds = 0;  // DoT: the engine's duration after its duration %
  bool off_hand = false;      // the L Hand pass
};
struct SkillDamageCheck {
  std::string record;
  std::string skipped;          // non-empty: not compared, why
  std::vector<TypeDiff> types;  // every type either side deals
  std::string note;             // what was applied to either side to compare them
  // The model rules this check runs through ("weapon damage", "dual wield", "conversion", ...): a FAIL points at
  // one of them, a PASS confirms them all for these numbers.
  std::vector<std::string> rules;
  bool pass() const;
};
// Off: more than 2% of the larger side and more than 1 damage.
bool damage_off(const TypeDiff& d);
// `records`: only these skills; empty = every learned or item-granted skill the snapshot has engine numbers for.
std::vector<SkillDamageCheck> damage(const snap::Snapshot& s, const arz::Database& db,
                                     const std::vector<std::string>& records = {});
// Schema 17: each live pet's % damage per type (its SkillManager's offensive modifier totals) against the record model
// (pet passives + pet bonuses). TypeDiff values are % here; rules "pet %".
std::vector<SkillDamageCheck> pets(const snap::Snapshot& s, const arz::Database& db);
// Over any number of checks: per rule, per skill and per type how many passed and failed, then the skip reasons.
// A rule is confirmed when every check through it passed, failing when one did not.
std::vector<std::string> damage_summary(const std::vector<SkillDamageCheck>& checks);
// PASS / FAIL per compared skill, then the types of a FAIL (of every skill when verbose); SKIP with the reason.
std::vector<std::string> damage_report(const std::vector<SkillDamageCheck>& checks, const std::string& label,
                                       bool verbose = false);

struct RequirementCheck {
  int checked = 0;
  std::vector<std::string> mismatches;
};
RequirementCheck requirements(const snap::Snapshot& s);

}  // namespace gdc::calibrate
