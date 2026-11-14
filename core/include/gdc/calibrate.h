#pragma once
// Swap calibration (PROMPT.md Phase 3): two snapshots of the same character that differ only in
// equipment are replayed through model::swap (each changed slot in turn) and the predicted sheet is
// compared with the engine's. Used by tools/calibrate on the auto-snapshot store and by the tests.
#include <string>
#include <vector>

#include "gdc/arz.h"
#include "gdc/snapshot.h"

namespace gdc::calibrate {

struct Diff {
  std::string stat;  // "Life", "Resist Fire (shown)", ...
  double predicted = 0, actual = 0, tolerance = 0;
};

struct Check {
  std::string skipped;             // non-empty: not comparable, and why
  std::vector<int> locations;      // changed EquipLocations, in the order swapped
  std::vector<std::string> warnings;  // model::Prediction warnings of every swap
  std::vector<std::string> notes;     // things that may explain a diff (e.g. a buff turned on or off)
  std::vector<Diff> diffs;            // stats outside tolerance; empty = pass
};

// Tolerances (PROMPT.md): integer sheet values (attributes, Life, Energy, OA, DA, armor) +-1, percent
// values (resists) +-0.1, regen +-0.1 per second, run speed +-0.1 %.
Check check(const snap::Snapshot& before, const snap::Snapshot& after, const arz::Database* db);

}  // namespace gdc::calibrate
