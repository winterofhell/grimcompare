#include <algorithm>

#include "gdc/attribute_names.h"
#include "log.h"
#include "snapshot_internal.h"

namespace gdc::adapter {
namespace {

const std::vector<int>& combat_types(const void* acc) {
  std::vector<int>& types = scratch.types;
  types.clear();
  using GetType = int (*)(const void*);
  for (std::size_t off : {0x08u, 0x20u, 0x38u, 0x50u}) {
    const Span s = vec(static_cast<const char*>(acc) + off, sizeof(void*));
    for (std::size_t i = 0; i < s.count; ++i) {
      const void* attr = rd<const void*>(s.begin, i * sizeof(void*));
      if (!attr) continue;
      const int t = rd<GetType>(rd<const void*>(attr), 0x10)(attr);
      const auto at = std::lower_bound(types.begin(), types.end(), t);
      if (at == types.end() || *at != t) types.insert(at, t);
    }
  }
  return types;
}

}  // namespace

void combat_totals(void* acc, void* player, std::vector<snap::CombatTotals>& out) {
  out.clear();
  for (int t : combat_types(acc)) {
    snap::CombatTotals c;
    c.type = t;
    c.defense = api.total_defense(acc, t);
    c.defense_modifier = api.total_defense_mod(acc, t);
    if (t != kAbsorptionProtection) c.reduction = api.total_reduction(acc, t);  // undefined until processed (see top)
    c.damage_modifier = api.total_damage_mod(acc, player, t, kDamageDirect);
    c.dot_modifier = api.total_damage_mod(acc, player, t, kDamageDuration);
    if (c.defense || c.defense_modifier || c.reduction || c.damage_modifier || c.dot_modifier) out.push_back(c);
  }
}

float mult_raw(void*, float) { return 1.0f; }
float mult_excluded(void* acc, float chance) { return api.chance_mult(acc, chance) == 1.0f ? 1.0f : 0.0f; }
float mult_probe(void* acc, float chance) {
  if (api.chance_mult(acc, chance) == 1.0f) return 1.0f;
  const float global = rd<float>(acc, kGlobalChance);
  return global > 0 ? global : chance;
}

namespace {

void* display_acc(AccBuf& buf, Pass p) {
  void* acc = api.combat_acc_ctor(buf.bytes, kDisplayModifyProcs);
  if (p != kExpected) {
    const void* vptr = &pass_vft[p][1];
    std::memcpy(acc, &vptr, sizeof vptr);
  }
  return acc;
}

const snap::CombatTotals* find_type(const std::vector<snap::CombatTotals>& v, int t) {
  for (const auto& c : v)
    if (c.type == t) return &c;
  return nullptr;
}

template <class Fill>
void combat_with_procs(Fill fill, void* player, std::vector<snap::CombatTotals>& out) {
  auto& pass = scratch.combat;
  for (int p = kExpected; p < kPasses; ++p) {
    AccBuf buf;
    void* acc = display_acc(buf, static_cast<Pass>(p));
    fill(acc);
    combat_totals(acc, player, pass[p]);
    api.combat_acc_dtor(acc);
  }
  static constexpr struct {
    float snap::CombatTotals::* total;
    float snap::Proc::* proc;
  } fields[] = {{&snap::CombatTotals::defense, &snap::Proc::defense},
                {&snap::CombatTotals::defense_modifier, &snap::Proc::defense_modifier},
                {&snap::CombatTotals::reduction, &snap::Proc::reduction},
                {&snap::CombatTotals::damage_modifier, &snap::Proc::damage_modifier},
                {&snap::CombatTotals::dot_modifier, &snap::Proc::dot_modifier}};
  const snap::CombatTotals zero;
  for (snap::CombatTotals& row : pass[kExpected]) {
    const snap::CombatTotals* raw = find_type(pass[kRaw], row.type);
    if (!raw) continue;
    const snap::CombatTotals* ex = find_type(pass[kExcluded], row.type);
    const snap::CombatTotals* probe = find_type(pass[kProbe], row.type);
    if (!ex) ex = &zero;
    if (!probe) probe = &zero;
    snap::Proc proc;
    bool any = false;
    for (const auto& f : fields) {
      const float d = raw->*f.total - ex->*f.total;
      proc.*f.proc = d;
      if (d != 0 && !any) {
        any = true;
        proc.chance = (probe->*f.total - ex->*f.total) / d;
      }
    }
    if (any) row.proc = proc;
  }
  out.assign(pass[kExpected].begin(), pass[kExpected].end());
}

}  // namespace

void item_combat(const void* item, std::size_t slot_off, void* player, std::vector<snap::CombatTotals>& out) {
  combat_with_procs([&](void* acc) { slot(item, slot_off)(item, acc); }, player, out);
}

namespace {

void damage_list(const void* acc, std::vector<snap::Damage>& out) {
  using IntFn = int (*)(const void*);
  using MinMaxFn = void (*)(const void*, float&, float&);
  using FloatFn = float (*)(const void*);
  out.clear();
  const Span s = vec(static_cast<const char*>(acc) + 0x08, sizeof(void*));
  for (std::size_t i = 0; i < s.count; ++i) {
    const void* attr = rd<const void*>(s.begin, i * sizeof(void*));
    if (!attr) continue;
    const void* vt = rd<const void*>(attr);
    snap::Damage d;
    d.type = rd<IntFn>(vt, 0x10)(attr);
    d.dot = rd<IntFn>(vt, 0x20)(attr) == kDamageDuration;
    if (vt == api.base_physical_vft) {
      float rolled = 0, a = 0, b = 0;
      api.base_physical_values(attr, rolled, d.min, d.max, a, b);
    } else {
      rd<MinMaxFn>(vt, 0x78)(attr, d.min, d.max);
    }
    if (d.dot) d.duration = rd<FloatFn>(vt, 0xF0)(attr);
    out.push_back(d);
  }
}

}  // namespace

// Every entry exists at any multiplier, so passes line up.
void item_damage(const void* obj, DamageFn fn, std::vector<snap::Damage>& result) {
  auto& pass = scratch.damage;
  for (int p = kExpected; p < kPasses; ++p) {
    AccBuf buf;
    void* acc = display_acc(buf, static_cast<Pass>(p));
    fn(obj, acc, kDamageScale);
    damage_list(acc, pass[p]);
    api.combat_acc_dtor(acc);
  }
  std::vector<snap::Damage>& out = pass[kExpected];
  bool aligned = true;
  for (int p = kRaw; p < kPasses; ++p) {
    aligned = aligned && pass[p].size() == out.size();
    for (std::size_t i = 0; aligned && i < out.size(); ++i)
      aligned = pass[p][i].type == out[i].type && pass[p][i].dot == out[i].dot;
  }
  if (!aligned) {
    log::printf("snapshot: %s: chance passes disagree (%zu/%zu/%zu/%zu entries), no chance data",
                bounded_cstr(api.object_name(obj)).c_str(), out.size(), pass[kRaw].size(), pass[kExcluded].size(),
                pass[kProbe].size());
  } else {
    for (std::size_t i = 0; i < out.size(); ++i) {
      const snap::Damage &raw = pass[kRaw][i], &ex = pass[kExcluded][i], &probe = pass[kProbe][i];
      const float dmax = raw.max - ex.max, dmin = raw.min - ex.min;
      if (dmax == 0 && dmin == 0) continue;
      out[i].chance = dmax != 0 ? (probe.max - ex.max) / dmax : (probe.min - ex.min) / dmin;
      out[i].raw_min = raw.min;
      out[i].raw_max = raw.max;
    }
  }
  result.clear();
  for (const snap::Damage& d : out)
    if (d.min != 0 || d.max != 0 || d.raw_max != 0) result.push_back(d);
}

void skill_attributes(const void* obj, AccFn fn, std::vector<snap::SkillAttribute>& out) {
  out.clear();
  AccBuf buf;
  void* acc = api.skill_acc_ctor(buf.bytes);
  fn(obj, acc);
  for (int t = 1; t <= names::kSkillAttributeMax; ++t) {
    const snap::SkillAttribute a{t, rd<float>(acc, 0x08 + 4 * t), rd<float>(acc, 0x1C + 4 * t),
                                 rd<float>(acc, 0x30 + 4 * t), rd<float>(acc, 0x44 + 4 * t)};
    if (a.value != 0 || a.chance_value != 0 || a.modifier != 0 || a.chance_modifier != 0) out.push_back(a);
  }
  api.skill_acc_dtor(acc);
}

void conversions(const void* obj, AccFn fn, std::vector<snap::Conversion>& out) {
  out.clear();
  AccBuf buf;
  void* acc = api.conv_acc_ctor(buf.bytes);
  fn(obj, acc);
  const Span from = vec(acc, sizeof(void*));
  for (std::size_t i = 0; i < from.count; ++i) {
    const char* d = rd<const char*>(from.begin, i * sizeof(void*));
    if (!d) continue;
    const Span to = vec(d + 8, 8);
    for (std::size_t k = 0; k < to.count; ++k)
      out.push_back({rd<int>(d, 0), rd<int>(to.begin, k * 8), rd<float>(to.begin, k * 8 + 4)});
  }
  api.conv_acc_dtor(acc);
}

DamageFn damage_slot(const void* obj, std::size_t off) { return rd<DamageFn>(rd<const void*>(obj), off); }

void char_contributions(const void* obj, AccFn fn, std::vector<snap::CharContribution>& out) {
  out.clear();
  AccBuf buf;
  void* acc = api.char_acc_ctor(buf.bytes);
  fn(obj, acc);
  for (int t = 1; t <= names::kCharAttributeMax; ++t) {
    const float v = rd<float>(acc, 4 * t), m = rd<float>(acc, 0xE8 + 4 * t);
    if (v != 0 || m != 0) out.push_back({t, v, m});
  }
  api.char_acc_dtor(acc);
}

void modifier_stats(const void* mod, void* player, snap::ItemStats& s) {
  item_damage(mod, api.mod_damage, s.offensive_damage);
  combat_with_procs([&](void* acc) { api.mod_offensive_modifiers(mod, acc); }, player, s.offensive_modifiers);
  skill_attributes(mod, api.mod_skill_attributes, s.skill_attributes);
  conversions(mod, api.mod_conversions, s.conversions);
}

}  // namespace gdc::adapter
