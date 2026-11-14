#pragma once
// Passive calibration: every auto-snapshot record that follows another of the same chain is a swap test
// (gdc::calibrate, the check tools/calibrate runs on the store), plus the requirement check of every record.
// Results go to GDCompare.calibration.txt next to the .asi, one session header and then one block per
// checked pair ("PASS ..." / "FAIL ..." + diffs) or requirement disagreement ("REQ ..."); skipped pairs are
// only counted (log). The tooltip block shows a red line while this session has a FAIL or REQ.
#include <string>

#include "gdc/arz.h"
#include "gdc/snapshot.h"

namespace gdc::adapter {

// Once, before the writer thread starts. `mod_dir` ends with a backslash.
void calibration_init(const std::wstring& mod_dir, const char* version);

// Writer thread, after the store wrote `s` as record `name` (file stem, e.g. 20261114-174229-629-0022-delta).
// `full`: a new chain starts (nothing to compare with). `db` may be null (still loading): the pair is skipped.
// Never throws; a fault turns calibration off for the session (logged).
void calibration_record(const snap::Snapshot& s, bool full, const std::string& name, const arz::Database* db);

// Any thread: FAIL and REQ entries written this session.
int calibration_failures();

}  // namespace gdc::adapter
