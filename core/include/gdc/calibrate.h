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
  // Non-empty: the swap alone left differences, and switching this learned timed buff (model::toggle_buff)
  // explains every one ("records/skills/playerclass03/bloodofdreeg1.dbr switched on"); the pair passes.
  std::string buff;
};

// Tolerances (PROMPT.md): integer sheet values (attributes, Life, Energy, OA, DA, armor) +-1, percent
// values (resists) +-0.1, regen +-0.1 per second, run speed +-0.1 %.
Check check(const snap::Snapshot& before, const snap::Snapshot& after, const arz::Database* db);

// The swapped slots for a report: "Feet(12) a02_feet01 -> a04_feet01; Finger(4) ..." (record file names,
// "+<component>" / "+<augment>" appended, "(empty)" for none).
std::string swapped(const Check& c, const snap::Snapshot& before, const snap::Snapshot& after);

// Report lines of a checked pair (empty when skipped), for tools/calibrate and the adapter's calibration file:
//   "PASS <label> <swapped>"  [" (<buff> between the snapshots)"]   or   "FAIL <label> <swapped>"
//   "    <stat>: predicted <x>, engine <y> (tolerance <t>)"           per diff
//   "    warning: ..." / "    note: ..."                              on FAIL, or always with `verbose`
std::vector<std::string> report(const Check& c, const snap::Snapshot& before, const snap::Snapshot& after,
                                const std::string& label, bool verbose = false);

// Requirements (schema 7): every equipped item with the engine's `met` flag, checked by gdc/requirements.h on
// the same snapshot. `checked` counts them; each disagreement is one line ("<record>: model met, engine not").
struct RequirementCheck {
  int checked = 0;
  std::vector<std::string> mismatches;
};
RequirementCheck requirements(const snap::Snapshot& s);

}  // namespace gdc::calibrate
