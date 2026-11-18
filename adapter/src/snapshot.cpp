#include <cstdio>

#include "log.h"
#include "seh.h"
#include "snapshot_internal.h"

namespace gdc::adapter {

Api api;
bool api_ready = false;
const void* pass_vft[kPasses][kDisplayVftSlots + 1] = {};
Scratch scratch;

namespace {

bool guarded(const char* name, void (*fn)(Ctx&), Ctx& c) {
  auto call = [&] { fn(c); };
  seh::Fault f;
  LARGE_INTEGER t0, t1;
  QueryPerformanceCounter(&t0);
  const bool ok = seh::run(call, f);
  QueryPerformanceCounter(&t1);
  if (c.timing.count < Timing::kMax) c.timing.sections[c.timing.count++] = {name, log::elapsed_us(t0, t1)};
  if (ok) return true;
  char buf[160];
  std::snprintf(buf, sizeof buf, "%s: FAULT 0x%08lX at 0x%016llX", name, f.code, static_cast<unsigned long long>(f.addr));
  c.s.errors.emplace_back(buf);
  c.s.complete = false;
  log::printf("snapshot section %s", buf);
  return false;
}

}  // namespace

Take snapshot_take(snap::Snapshot& s, snap::Spares& spare, const std::vector<std::uint32_t>& tooltip_ids,
                   bool with_inventory, Timing& timing) {
  timing.count = 0;
  if (!api_ready) return Take::NoPlayer;
  void* engine = nullptr;
  void* player = nullptr;
  auto find = [&] {
    engine = const_cast<void*>(*api.engine_var);
    player = engine ? api.main_player(engine) : nullptr;
  };
  seh::Fault f;
  if (!seh::run(find, f)) {
    char buf[96];
    std::snprintf(buf, sizeof buf, "main player: FAULT 0x%08lX at 0x%016llX", f.code, static_cast<unsigned long long>(f.addr));
    s.errors.emplace_back(buf);
    s.complete = false;
    log::printf("snapshot: %s", buf);
    return Take::Faulted;
  }
  if (!player) return Take::NoPlayer;
  Ctx c{s, spare, engine, player, tooltip_ids, timing};
  bool ok = guarded("character", sec_character, c);
  ok = guarded("defense", sec_defense, c) && ok;
  ok = guarded("defense_processed", sec_defense_processed, c) && ok;
  ok = guarded("damage_modifiers", sec_damage_modifiers, c) && ok;
  ok = guarded("conversions", sec_conversions, c) && ok;
  ok = guarded("skill_attributes", sec_skill_attributes, c) && ok;
  ok = guarded("skills", sec_skills, c) && ok;
  ok = guarded("item_skill_modifiers", sec_item_skill_modifiers, c) && ok;
  ok = guarded("item_skills", sec_item_skills, c) && ok;
  ok = guarded("pets", sec_pets, c) && ok;
  ok = guarded("equipped", sec_equipped, c) && ok;
  ok = guarded("tooltip", sec_tooltip, c) && ok;
  if (with_inventory) ok = guarded("inventory", sec_inventory, c) && ok;
  return ok ? Take::Ok : Take::Faulted;
}

Take snapshot_probe(Probe& out) {
  if (!api_ready) return Take::NoPlayer;
  out = Probe{};
  auto read = [&] {
    void* engine = const_cast<void*>(*api.engine_var);
    void* p = engine ? api.main_player(engine) : nullptr;
    if (!p) return;
    out.player = p;
    out.level = api.char_level(p);
    out.skill_points = api.skill_points(p);
    out.attribute_points = api.modifier_points(p);
    out.devotion_points = api.devotion_points(p);
    out.difficulty = api.difficulty(engine);
    out.difficulty_ramp = api.difficulty_ramp ? api.difficulty_ramp(engine) : 0;
    const Span items = vec(api.attached_items(p), 12);
    std::uint64_t h = 1469598103934665603ull;
    auto mix = [&h](u32 v) {
      for (int b = 0; b < 4; ++b) h = (h ^ ((v >> (8 * b)) & 0xFF)) * 1099511628211ull;
    };
    for (std::size_t i = 0; i < items.count; ++i) {
      const u32 id = rd<u32>(items.begin, i * 12);
      mix(id);
      mix(rd<u32>(items.begin, i * 12 + 4));
      // Socketing changes neither the item id nor its slot.
      const u64 fp = fingerprint_of(lookup(id));
      mix(static_cast<u32>(fp >> 32));
      mix(static_cast<u32>(fp));
    }
    out.equip_hash = h;
  };
  seh::Fault f;
  if (!seh::run(read, f)) {
    log::printf("snapshot probe: FAULT 0x%08lX at 0x%016llX", f.code, static_cast<unsigned long long>(f.addr));
    return Take::Faulted;
  }
  return out.player ? Take::Ok : Take::NoPlayer;
}

namespace {
Take candidate(const void* item, u32 id, snap::Item& out, int& item_type) {
  if (!api_ready || !api.item_type_ok) return Take::NoPlayer;
  bool found = false;
  auto read = [&] {
    void* engine = const_cast<void*>(*api.engine_var);
    void* player = engine ? api.main_player(engine) : nullptr;
    if (!player) return;
    const void* obj = lookup(id ? id : api.object_id(item));
    if (!obj || !is_a(class_info(obj), api.ci_item)) return;
    using TypeFn = int (*)(const void*);
    item_type = rd<TypeFn>(rd<const void*>(obj), kSlotItemType)(obj);
    read_item(obj, out, true, player);
    found = true;
  };
  seh::Fault f;
  if (!seh::run(read, f)) {
    log::printf("tooltip candidate: FAULT 0x%08lX at 0x%016llX", f.code, static_cast<unsigned long long>(f.addr));
    return Take::Faulted;
  }
  return found ? Take::Ok : Take::NoPlayer;
}
}  // namespace

Take snapshot_candidate(const void* item, snap::Item& out, int& item_type) { return candidate(item, 0, out, item_type); }

Take snapshot_candidate_id(std::uint32_t id, snap::Item& out, int& item_type) {
  return id ? candidate(nullptr, id, out, item_type) : Take::NoPlayer;
}

Take snapshot_bag(std::vector<BagItem>& out) {
  out.clear();
  if (!api_ready) return Take::NoPlayer;
  bool player_found = false;
  auto read = [&] {
    void* engine = const_cast<void*>(*api.engine_var);
    void* player = engine ? api.main_player(engine) : nullptr;
    if (!player) return;
    player_found = true;
    const Span ids = vec(api.inventory_items(player), sizeof(u32));
    for (std::size_t i = 0; i < ids.count && out.size() < out.capacity(); ++i) {  // no allocation
      const u32 id = rd<u32>(ids.begin, i * sizeof(u32));
      if (const void* obj = lookup(id)) out.push_back({id, fingerprint_of(obj)});
    }
  };
  seh::Fault f;
  if (!seh::run(read, f)) {
    log::printf("tooltip bag: FAULT 0x%08lX at 0x%016llX", f.code, static_cast<unsigned long long>(f.addr));
    return Take::Faulted;
  }
  return player_found ? Take::Ok : Take::NoPlayer;
}

std::uint64_t item_fingerprint(const void* item) {
  if (!api_ready || !item) return 0;
  u64 fp = 0;
  auto read = [&] { fp = fingerprint_of(lookup(api.object_id(item))); };
  seh::Fault f;
  return seh::run(read, f) ? fp : 0;
}

std::uint32_t object_id(const void* obj) {
  u32 id = 0;
  if (!api.object_id || !obj) return 0;
  auto get = [&] { id = api.object_id(obj); };
  seh::Fault f;
  return seh::run(get, f) ? id : 0;
}

}  // namespace gdc::adapter
