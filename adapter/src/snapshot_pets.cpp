#include <cstdio>

#include "log.h"
#include "seh.h"
#include "snapshot_internal.h"

namespace gdc::adapter {

// Own SEH per pet: a fault drops it, logged.
void sec_pets(Ctx& c) {
  if (!api.pets_ok) return;
  const void* head = rd<const void*>(api.pet_pen(c.player), kPetList);
  if (!head) return;
  const void* n = rd<const void*>(head, 0);
  for (std::size_t i = 0; n && n != head && i < kMaxPets; ++i, n = rd<const void*>(n, 0)) {
    if (rd<std::uint8_t>(n, kPetNodeReleased) != 0) continue;
    const u32 id = rd<u32>(n, kPetNodeId);
    snap::Pet p;
    auto read = [&] {
      void* pet = lookup(id);
      if (!pet || !is_a(class_info(pet), api.ci_character)) return;
      p.id = id;
      p.skill_id = rd<u32>(n, kPetNodeSkill);
      set_cstr(p.record, api.object_name(pet));
      p.attack_speed = api.attack_speed(pet);
      p.cast_speed = api.cast_speed(pet);
      p.attack_speed_modifier = api.total_char_mod(pet, 15);
      p.cast_speed_modifier = api.total_char_mod(pet, 16);
      const void* sm = api.skill_manager(pet);
      p.default_skill_id = api.default_skill_id(sm, 0);
      // As sec_damage_modifiers / sec_conversions for the player, on the pet's own SkillManager: every call goes
      // through Character virtuals, which Pet overrides (MECHANICS §5 "Pets").
      AccBuf buf;
      void* acc = api.combat_acc_ctor(buf.bytes, kDisplayModifyProcs);
      api.collect_offensive_modifiers(sm, acc, 0);
      combat_totals(acc, pet, p.damage_modifiers);
      api.combat_acc_dtor(acc);
      conversions(sm, api.collect_conversions, p.conversions);
      const Span list = vec(api.skill_list(sm), sizeof(void*));
      for (std::size_t k = 0; k < list.count; ++k) {
        const void* sk = rd<const void*>(list.begin, k * sizeof(void*));
        if (!sk) continue;
        snap::PetSkill& ps = p.skills.emplace_back();
        ps.id = api.object_id(sk);
        set_cstr(ps.record, api.object_name(sk));
        ps.current_level = api.current_level(sk);
        if (is_a(class_info(sk), api.ci_activated) &&
            rd<const void*>(rd<const void*>(sk), kSlotHitCallbackPoint) == reinterpret_cast<const void*>(api.hit_callback_point)) {
          u32 name = 0, frames = 0;
          int rate = 0;
          api.hit_callback_point(sk, pet, &name, &frames, &rate);
          ps.anim_frames = frames;
          ps.anim_frame_rate = rate;
        }
      }
    };
    seh::Fault f;
    if (!seh::run(read, f)) {
      char buf[160];
      std::snprintf(buf, sizeof buf, "pet %u: FAULT 0x%08lX at 0x%016llX", id, f.code, static_cast<unsigned long long>(f.addr));
      c.s.errors.emplace_back(buf);
      c.s.complete = false;
      log::printf("snapshot: %s", buf);
      continue;
    }
    if (p.id != 0) c.s.pets.push_back(std::move(p));
  }
}

}  // namespace gdc::adapter
