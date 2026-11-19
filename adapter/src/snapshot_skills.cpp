#include <algorithm>
#include <cstdio>

#include "log.h"
#include "seh.h"
#include "snapshot_internal.h"

namespace gdc::adapter {
namespace {

void skill_fault(Ctx& c, const std::string& record, const char* what, const seh::Fault& f) {
  char buf[200];
  std::snprintf(buf, sizeof buf, "skill %s %s: FAULT 0x%08lX at 0x%016llX", record.c_str(), what, f.code,
                static_cast<unsigned long long>(f.addr));
  c.s.errors.emplace_back(buf);
  c.s.complete = false;
  log::printf("snapshot: %s", buf);
}

// Keyed by the skill's record or its buffSkillName (hook_targets.cpp).
void read_buff_active(Ctx& c, const void* sk, snap::Skill& s) {
  s.buff_active.reset();
  bool up = false;
  auto ask = [&] {
    up = api.buff_active(c.player, s.record);
    const std::string& buff = api.buff_skill_name(sk);
    if (!up && !buff.empty()) up = api.buff_active(c.player, buff);
  };
  seh::Fault f;
  if (seh::run(ask, f)) s.buff_active = up;
  else skill_fault(c, s.record, "buff_active", f);
}

// Only spells and self buffs, through the exported slot.
void read_hit_animation(Ctx& c, const void* sk, snap::Skill& s) {
  s.anim_frames = 0;
  s.anim_frame_rate = 0;
  const void* ci = class_info(sk);
  if (s.current_level == 0 || !(is_a(ci, api.ci_spell) || is_a(ci, api.ci_buff_self)) ||
      rd<const void*>(rd<const void*>(sk), kSlotHitCallbackPoint) != reinterpret_cast<const void*>(api.hit_callback_point))
    return;
  // As CalculateSkillDps: outputs used only when both > 0.
  u32 name = 0, frames = 0;
  int rate = 0;
  auto ask = [&] { api.hit_callback_point(sk, c.player, &name, &frames, &rate); };
  seh::Fault f;
  if (seh::run(ask, f)) {
    s.anim_frames = frames;
    s.anim_frame_rate = rate;
  } else {
    skill_fault(c, s.record, "hit animation", f);
  }
}

// Skill_Passive / Skill_BuffSelfToggled override these collectors.
void read_passive(Ctx& c, const void* sk, snap::Skill& s) {
  snap::ItemStats& p = s.passive.emplace();
  if (s.current_level == 0) return;
  auto collect = [&] {
    char_contributions(sk, slot(sk, kSlotSkillPassiveChar), p.char_attributes);
    AccBuf buf;
    void* acc = api.combat_acc_ctor(buf.bytes, kDisplayModifyProcs);
    slot(sk, kSlotSkillPassiveDefense)(sk, acc);
    combat_totals(acc, c.player, p.defense);
    api.combat_acc_dtor(acc);
  };
  seh::Fault f;
  if (seh::run(collect, f)) return;
  skill_fault(c, s.record, "passive", f);
  s.passive.reset();
}

snap::Skill read_skill(Ctx& c, const void* sk) {
  snap::Skill s = snap::reuse(c.spare.skills);
  s.id = api.object_id(sk);
  set_cstr(s.record, api.object_name(sk));
  s.type = static_cast<u32>(api.skill_type(sk));
  s.level = api.skill_level(sk);
  s.augmented_level = api.augmented_level(sk);
  s.current_level = api.current_level(sk);
  s.devotion_level = api.devotion_level(sk);
  s.devotion_max_level = api.devotion_max_level(sk);
  s.mastery_id = api.mastery_id(sk);
  read_buff_active(c, sk, s);
  const Span mods = vec(static_cast<const char*>(sk) + kSkillModifiers, sizeof(u32));
  s.modifier_ids.clear();
  for (std::size_t m = 0; m < mods.count; ++m) s.modifier_ids.push_back(rd<u32>(mods.begin, m * sizeof(u32)));
  s.item_id = 0;
  read_hit_animation(c, sk, s);
  read_passive(c, sk, s);
  return s;
}

// MSVC std::string in its heap layout over our own buffer. CalculateSkillDps only assigns the 27-char
// "tagSkillDefaultAttackName01" (0x3DFF64), and an assign that fits the capacity copies in place (0x164EB), so the
// engine never frees or grows it.
struct NameString {
  char* data;
  u64 unused;
  u64 size;
  u64 capacity;
};
constexpr std::size_t kNameBytes = 256, kAttackInfoBytes = 0x100;  // SkillAttackInfo: CalculateSkillDps writes to +0x38
constexpr std::size_t kMaxSkillDps = 48;

// One skill through Player::CalculateSkillDps; false: the call faulted.
bool read_skill_dps(Ctx& c, u32 id, snap::SkillDps& out) {
  char name[kNameBytes] = {};
  NameString str{name, 0, 0, kNameBytes - 1};
  alignas(16) unsigned char info[kAttackInfoBytes] = {};
  float dps = 0;
  AccBuf main_buf, off_buf;
  void* main = nullptr;
  void* off = nullptr;
  auto call = [&] {
    main = api.combat_acc_ctor(main_buf.bytes, kDisplayModifyProcs);
    off = api.combat_acc_ctor(off_buf.bytes, kDisplayModifyProcs);
    api.skill_dps(c.player, id, &str, info, &dps, main, off, 0);
    damage_list(main, out.main_hand, true);
    damage_list(off, out.off_hand, true);
  };
  seh::Fault f;
  const bool ok = seh::run(call, f);
  auto drop = [&] {
    if (main) api.combat_acc_dtor(main);
    if (off) api.combat_acc_dtor(off);
  };
  seh::Fault g;
  const bool dropped = seh::run(drop, g);
  out.skill_id = id;
  out.dps = dps;
  out.default_attack = str.size != 0;
  if (ok && dropped) return true;
  // The other sections stay valid: the snapshot is still complete.
  const seh::Fault& e = ok ? g : f;
  char buf[200];
  std::snprintf(buf, sizeof buf, "skill %s %s: FAULT 0x%08lX at 0x%016llX", out.record.c_str(),
                ok ? "skill dps cleanup" : "skill dps", e.code, static_cast<unsigned long long>(e.addr));
  c.s.errors.emplace_back(buf);
  log::printf("snapshot: %s", buf);
  return false;
}

}  // namespace

// Learned activated skills (modifiers count through their skill). A fault turns the check off for the session.
void sec_skill_dps(Ctx& c) {
  const void* sm = api.skill_manager(c.player);
  for (const void* list : {api.skill_list(sm), api.item_skill_list(sm)}) {
    const Span skills = vec(list, sizeof(void*));
    for (std::size_t i = 0; i < skills.count && c.s.skill_dps.size() < kMaxSkillDps; ++i) {
      const void* sk = rd<const void*>(skills.begin, i * sizeof(void*));
      if (!sk || api.current_level(sk) == 0) continue;
      const void* ci = class_info(sk);
      if (!is_a(ci, api.ci_activated) || is_a(ci, api.ci_skill_modifier)) continue;
      const u32 id = api.object_id(sk);
      if (std::any_of(c.s.skill_dps.begin(), c.s.skill_dps.end(), [&](const snap::SkillDps& d) { return d.skill_id == id; }))
        continue;
      snap::SkillDps& d = c.s.skill_dps.emplace_back();
      set_cstr(d.record, api.object_name(sk));
      if (read_skill_dps(c, id, d)) continue;
      api.skill_dps_ok = false;
      c.s.skill_dps.clear();
      log::write("snapshot: CalculateSkillDps faulted; engine damage check off");
      return;
    }
  }
}

void sec_skills(Ctx& c) {
  const void* sm = api.skill_manager(c.player);
  c.s.devotion_current_level_sum = api.devotion_spent(sm);
  const Span list = vec(api.skill_list(sm), sizeof(void*));
  for (std::size_t i = 0; i < list.count; ++i) {
    const void* sk = rd<const void*>(list.begin, i * sizeof(void*));
    if (sk) c.s.skills.push_back(read_skill(c, sk));
  }
}

// SkillManager+0x258.
void sec_item_skills(Ctx& c) {
  const Span list = vec(api.item_skill_list(api.skill_manager(c.player)), sizeof(void*));
  for (std::size_t i = 0; i < list.count; ++i) {
    const void* sk = rd<const void*>(list.begin, i * sizeof(void*));
    if (!sk) continue;
    snap::Skill s = read_skill(c, sk);
    s.item_id = api.item_skill_item_id(sk);
    c.s.item_skills.push_back(std::move(s));
  }
}

void sec_item_skill_modifiers(Ctx& c) {
  const Span list = vec(api.skill_list(api.skill_manager(c.player)), sizeof(void*));
  for (std::size_t i = 0; i < list.count; ++i) {
    const char* sk = rd<const char*>(list.begin, i * sizeof(void*));
    if (!sk) continue;
    const Span mods = vec(sk + 0x158, 0x30);
    for (std::size_t k = 0; k < mods.count; ++k) {
      const char* e = mods.begin + k * 0x30;
      snap::ItemSkillModifier m = snap::reuse(c.spare.item_skill_modifiers);
      m.skill_id = api.object_id(sk);
      set_cstr(m.skill_record, api.object_name(sk));
      m.modifier_id = rd<u32>(e, 0);
      m.location = rd<int>(e, 4);
      m.from_relic = rd<std::uint8_t>(e, 0x28) != 0;
      m.from_augment = rd<std::uint8_t>(e, 0x29) != 0;
      if (const void* mod = lookup(m.modifier_id)) {
        set_cstr(m.modifier_record, api.object_name(mod));
        // The exported collectors are Skill_Modifier's own overrides.
        if (class_info(mod) == api.ci_skill_modifier) modifier_stats(mod, c.player, m.stats.emplace());
      }
      c.s.item_skill_modifiers.push_back(std::move(m));
    }
  }
}

}  // namespace gdc::adapter
