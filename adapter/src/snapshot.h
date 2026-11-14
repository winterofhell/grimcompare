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

// The hovered item for the tooltip block, read like an equipped item (with stats), and its engine
// type (Item::GetItemType). Game thread. `out`: a default Item. NoPlayer: no player, not an item, or the
// type slot was not verified at init. Faulted: logged.
Take snapshot_candidate(const void* item, snap::Item& out, int& item_type);

// snapshot_candidate for the item with object id `id`. Game thread.
Take snapshot_candidate_id(std::uint32_t id, snap::Item& out, int& item_type);

// One inventory item: its object id and item_fingerprint.
struct BagItem {
  std::uint32_t id = 0;
  std::uint64_t fingerprint = 0;
};
// Game thread. The main player's inventory items (Character::GetInventoryItems), at most out.capacity()
// (no allocation). NoPlayer: no player. Faulted: logged.
Take snapshot_bag(std::vector<BagItem>& out);

// Object ids of the item's socketed component and augment, (relic << 32) | augment; 0 when it has none,
// is not ItemEquipment, or on a fault. Socketing changes neither the item id nor its slot. Game thread.
std::uint64_t item_fingerprint(const void* item);

// Object::GetObjectId under SEH; 0 on a fault. Used by the tooltip detours.
std::uint32_t object_id(const void* obj);

}  // namespace gdc::adapter
