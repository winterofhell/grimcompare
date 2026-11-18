#pragma once
// Swap tests from consecutive auto-snapshots; results in GDCompare.calibration.txt.
#include <string>

#include "gdc/arz.h"
#include "gdc/snapshot.h"

namespace gdc::adapter {

void calibration_init(const std::wstring& mod_dir, const char* version);

// `db` may be null while loading: pair skipped. Never throws.
void calibration_record(const snap::Snapshot& s, bool full, const std::string& name, const arz::Database* db);

int calibration_failures();

}  // namespace gdc::adapter
