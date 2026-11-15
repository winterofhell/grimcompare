#pragma once
// docs/SNAPSHOT.md. Read-only: engine getters only.
#include <cstdint>
#include <functional>
#include <string_view>
#include <vector>

#include "gdc/snapshot.h"

namespace gdc::adapter {

using Resolve = std::function<const void*(std::string_view label)>;

// false: no snapshots (each problem logged).
bool snapshot_init(const Resolve& resolve);

enum class Take { NoPlayer, Ok, Faulted };
struct Timing {
  static constexpr int kMax = 12;
  struct Section {
    const char* name = nullptr;
    long long us = 0;
  } sections[kMax];
  int count = 0;
};
// Game thread only. Faulted: some sections faulted (out.errors), the rest is filled.
Take snapshot_take(snap::Snapshot& out, snap::Spares& spare, const std::vector<std::uint32_t>& tooltip_ids,
                   bool with_inventory, Timing& timing);

// No allocation, no engine writes.
struct Probe {
  const void* player = nullptr;
  std::uint32_t level = 0, skill_points = 0, attribute_points = 0, devotion_points = 0;
  int difficulty = -1;
  std::uint64_t equip_hash = 0;  // FNV-1a over (id, location, relic id, augment id)
  bool operator==(const Probe&) const = default;
};
Take snapshot_probe(Probe& out);

// NoPlayer also when the type slot was not verified at init.
Take snapshot_candidate(const void* item, snap::Item& out, int& item_type);

Take snapshot_candidate_id(std::uint32_t id, snap::Item& out, int& item_type);

struct BagItem {
  std::uint32_t id = 0;
  std::uint64_t fingerprint = 0;
};
// At most out.capacity(): no allocation.
Take snapshot_bag(std::vector<BagItem>& out);

std::uint64_t item_fingerprint(const void* item);

std::uint32_t object_id(const void* obj);

}  // namespace gdc::adapter
