#pragma once
// Phase 2 snapshot reader (docs/SNAPSHOT.md). Read-only: engine getters only.
#include <cstdint>
#include <functional>
#include <string_view>
#include <vector>

#include "gdc/snapshot.h"

namespace gdc::adapter {

// Address of a resolved hook target by label (gdc::targets), nullptr if unresolved.
using Resolve = std::function<const void*(std::string_view label)>;

// Bind every function the reader calls, find the unexported object lookup and check the item
// vtable slots against the exported vftables. false (each problem logged) → no snapshots.
bool snapshot_init(const Resolve& resolve);

enum class Take { NoPlayer, Ok, Faulted };
// Game-thread time per section of one snapshot_take, for the log.
struct Timing {
  static constexpr int kMax = 12;
  struct Section {
    const char* name = nullptr;
    long long us = 0;
  } sections[kMax];
  int count = 0;
};
// Game thread only (GameEngine::Update detour). `out` is Snapshot{} or recycled (snap::recycle), its
// spare elements in `spare`. NoPlayer: nothing to write. Faulted: at least one section faulted (listed
// in out.errors, out.complete false); the rest is still filled. with_inventory false: the inventory
// section is skipped (auto snapshots).
Take snapshot_take(snap::Snapshot& out, snap::Spares& spare, const std::vector<std::uint32_t>& tooltip_ids,
                   bool with_inventory, Timing& timing);

// What auto snapshots watch, read with a few exported getters; no allocation, no engine writes.
struct Probe {
  const void* player = nullptr;
  std::uint32_t level = 0, skill_points = 0, attribute_points = 0, devotion_points = 0;
  int difficulty = -1;
  std::uint64_t equip_hash = 0;  // FNV-1a over the attached items' (id, location, relic id, augment id)
  bool operator==(const Probe&) const = default;
};
// Game thread only. NoPlayer: menu / loading (out.player null). Faulted: logged.
Take snapshot_probe(Probe& out);

// Object::GetObjectId under SEH; 0 on a fault. Used by the tooltip detours.
std::uint32_t object_id(const void* obj);

}  // namespace gdc::adapter
