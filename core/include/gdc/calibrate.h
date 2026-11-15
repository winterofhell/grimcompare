#pragma once
// Two snapshots that differ only in equipment, replayed through model::swap and checked against the engine.
#include <string>
#include <vector>

#include "gdc/arz.h"
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
  // The pair passes when one learned timed buff switching explains every diff.
  std::string buff;
};

// Tolerances: integer sheet values +-1, resists +-0.1, regen +-0.1/s, run speed +-0.1%.
Check check(const snap::Snapshot& before, const snap::Snapshot& after, const arz::Database* db);

std::string swapped(const Check& c, const snap::Snapshot& before, const snap::Snapshot& after);

std::vector<std::string> report(const Check& c, const snap::Snapshot& before, const snap::Snapshot& after,
                                const std::string& label, bool verbose = false);

struct RequirementCheck {
  int checked = 0;
  std::vector<std::string> mismatches;
};
RequirementCheck requirements(const snap::Snapshot& s);

}  // namespace gdc::calibrate
