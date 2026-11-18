#pragma once
// Lines come from the engine's ctor and push_back.
#include <cstdint>
#include <string>

#include "gdc/arz.h"
#include "gdc/snapshot.h"
#include "snapshot.h"

namespace gdc::adapter {

bool tooltip_init(const Resolve& resolve, const std::wstring& mod_dir);

const arz::Database* tooltip_database();

void tooltip_publish(const snap::Snapshot& s, const Probe& probe);

void tooltip_freeze();

void tooltip_tick();

// `caller`: the detour's return address.
void tooltip_after_build(const void* item, void* lines, const void* caller);

}  // namespace gdc::adapter
