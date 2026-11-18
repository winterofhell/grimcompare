#include "gdc/attribute_names.h"
#include "snapshot_internal.h"

namespace gdc::adapter {
namespace {

void item_stats(const void* item, void* player, bool with_damage, snap::ItemStats& s) {
  char_contributions(item, slot(item, kSlotChar), s.char_attributes);
  item_combat(item, kSlotDefense, player, s.defense);
  item_combat(item, kSlotOffMod, player, s.offensive_modifiers);
  item_combat(item, kSlotRetMod, player, s.retaliation_modifiers);
  skill_attributes(item, slot(item, kSlotSkill), s.skill_attributes);
  conversions(item, slot(item, kSlotConversion), s.conversions);
  s.offensive_damage.clear();
  s.retaliation.clear();
  if (with_damage) {
    item_damage(item, damage_slot(item, kSlotDamage), s.offensive_damage);
    item_damage(item, damage_slot(item, kSlotRetaliation), s.retaliation);
  }
}

void replica(const void* item, snap::Replica& out) {
  const char* r = static_cast<const char*>(item) + kReplica;
  set_msvc_string(out.base, r + 0x08);
  set_msvc_string(out.prefix, r + 0x28);
  set_msvc_string(out.suffix, r + 0x48);
  out.seed = rd<u32>(r, 0x68);
  set_msvc_string(out.modifier, r + 0x70);
  set_msvc_string(out.relic, r + 0x90);
  set_msvc_string(out.relic_bonus, r + 0xB0);
  out.relic_seed = rd<u32>(r, 0xD0);
  set_msvc_string(out.augment, r + 0xD8);
  out.augment_level = rd<u32>(r, 0xF8);
  out.augment_seed = rd<u32>(r, 0xFC);
  set_msvc_string(out.transmute, r + 0x100);
  set_msvc_string(out.ascendant, r + 0x120);
  set_msvc_string(out.ascendant2h, r + 0x140);
  out.var1 = rd<u32>(r, 0x160);
  out.stack_count = rd<u32>(r, 0x178);
  out.affix_rerolls = rd<u32>(r, 0x17C);
  out.rerolls = rd<u32>(r, 0x180);
}

void component(const void* obj, void* player, bool with_stats, snap::Component& c) {
  set_cstr(c.record, api.object_name(obj));
  if (with_stats && *kind_of(obj)) item_stats(obj, player, false, c.stats.emplace());
}

void requirements(const void* obj, const void* player, snap::Item& it) {
  using ReqFn = u32 (*)(const void*);
  snap::Requirements q;
  q.level = rd<ReqFn>(rd<const void*>(obj), kSlotReqLevel)(obj);
  for (int a = 0; a < 3; ++a) {
    q.attribute[a] = rd<ReqFn>(rd<const void*>(obj), kSlotReqAttr[a])(obj);
    for (int k = 0; k < 3; ++k) {
      const u32 t = rd<u32>(obj, kReqReduction[a][k]);
      if (t > static_cast<u32>(names::kCharAttributeMax)) return;  // not a CharAttributeType, layout differs
      q.reductions[a][k] = static_cast<int>(t);
    }
  }
  if (player) {
    q.met = api.requirements_met(obj, player);
    // The state that call saw, to spot a stale flag.
    snap::Requirements::MetAt& at = q.met_at.emplace();
    at.level = api.char_level(player);
    const void* engine = *api.engine_var;
    at.difficulty = engine ? api.difficulty(engine) : -1;
    for (int a = 0; a < 3; ++a) at.attribute[a] = api.total_char_attr(player, a + 1);
  }
  it.requirements = q;
}

}  // namespace

void read_item(const void* obj, snap::Item& it, bool with_stats, void* player) {
  it.id = api.object_id(obj);
  set_cstr(it.record, api.object_name(obj));
  const char* kind = kind_of(obj);
  it.kind.assign(kind);
  if (!*kind) return;  // nothing else is safe to read
  replica(obj, it.replica.emplace());
  if (it.kind == "ItemEquipment") {
    set_msvc_string(it.set, api.item_set_name(obj));
    if (const void* r = api.get_relic(obj)) component(r, player, with_stats, it.relic.emplace());
    if (const void* e = api.get_enchantment(obj)) component(e, player, with_stats, it.augment.emplace());
    if (api.requirements_ok) requirements(obj, player, it);
  }
  if (with_stats && it.kind != "Item")
    item_stats(obj, player, it.kind == "ItemEquipment" || it.kind == "ItemArtifact", it.stats.emplace());
}

void sec_equipped(Ctx& c) {
  const Span items = vec(api.attached_items(c.player), 12);
  for (std::size_t i = 0; i < items.count; ++i) {
    const u32 id = rd<u32>(items.begin, i * 12);
    const void* obj = lookup(id);
    if (!obj) {
      c.s.errors.push_back("equipped: id " + std::to_string(id) + " not found");
      c.s.complete = false;
      continue;
    }
    snap::Item it = snap::reuse(c.spare.items);
    read_item(obj, it, true, c.player);
    it.location = rd<int>(items.begin, i * 12 + 4);
    c.s.equipped.push_back(std::move(it));
  }
}

void sec_tooltip(Ctx& c) {
  for (u32 id : c.tooltip_ids) {
    const void* obj = lookup(id);
    if (!obj) continue;
    snap::Item it = snap::reuse(c.spare.items);
    read_item(obj, it, true, c.player);
    c.s.tooltip_items.push_back(std::move(it));
  }
}

void sec_inventory(Ctx& c) {
  const Span ids = vec(api.inventory_items(c.player), sizeof(u32));
  for (std::size_t i = 0; i < ids.count; ++i) {
    const void* obj = lookup(rd<u32>(ids.begin, i * sizeof(u32)));
    if (!obj) continue;
    snap::Item it = snap::reuse(c.spare.items);
    read_item(obj, it, false, c.player);
    c.s.inventory.push_back(std::move(it));
  }
}

}  // namespace gdc::adapter
