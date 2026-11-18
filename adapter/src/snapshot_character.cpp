#include <algorithm>

#include "gdc/attribute_names.h"
#include "snapshot_internal.h"

namespace gdc::adapter {
namespace {

void player_name(const void* player, std::string& out) {
  out.clear();
  const char16_t* n = api.player_name(player);
  if (!n) return;
  int len = 0;
  while (len < 64 && n[len]) ++len;
  const auto* w = reinterpret_cast<const wchar_t*>(n);
  const int bytes = WideCharToMultiByte(CP_UTF8, 0, w, len, nullptr, 0, nullptr, nullptr);
  out.resize(static_cast<std::size_t>(std::max(bytes, 0)));
  if (bytes > 0) WideCharToMultiByte(CP_UTF8, 0, w, len, out.data(), bytes, nullptr, nullptr);
}

}  // namespace

void sec_character(Ctx& c) {
  void* p = c.player;
  player_name(p, c.s.character_name);
  c.s.save.clear();
  if (api.cloud_storage && api.engine2_var && api.game_info && api.game_mode && !c.s.character_name.empty()) {
    const bool cloud = api.cloud_storage(c.engine);
    const void* engine2 = *api.engine2_var;
    void* info = engine2 ? api.game_info(engine2) : nullptr;
    // SteamID64 low 32 bits = userdata/<id> account folder.
    void* user = cloud && api.steam_user ? api.steam_user() : nullptr;
    const auto account = static_cast<u32>(user ? api.steam_id(user) : 0);
    if (info && (!cloud || account != 0))
      c.s.save = snap::save_key(account, rd<std::uint8_t>(p, kMainFolder) == 0 && api.game_mode(info) != 1,
                                (rd<std::uint8_t>(p, kBackupFolder) != 0 ? "__" : "_") + c.s.character_name);
  }
  c.s.level = api.char_level(p);
  c.s.difficulty = api.difficulty(c.engine);
  c.s.difficulty_ramp = api.difficulty_ramp ? api.difficulty_ramp(c.engine) : 0;
  c.s.skill_points = api.skill_points(p);
  c.s.attribute_points = api.modifier_points(p);
  c.s.devotion_points = api.devotion_points(p);
  c.s.devotion_points_total = api.total_devotion_points(p);
  c.s.life_max = api.life_limit(p);
  c.s.mana_max = api.mana_limit(p);
  c.s.attack_speed = api.attack_speed(p);
  c.s.cast_speed = api.cast_speed(p);
  // Character::GetRunSpeed 0x69040: true returns before CapRunSpeed / CapAbsoluteRunSpeed.
  c.s.run_speed = api.run_speed(p, false);
  c.s.run_speed_uncapped = api.run_speed(p, true);
  c.s.defense_cap = api.defense_cap(p);
  const char* pth = static_cast<const char*>(p) + kPthTable;
  c.s.pth.minimum = rd<float>(pth, 0);
  c.s.pth.crit_divisor_min = rd<float>(pth, 4);
  for (int i = 0; i < 6; ++i) {
    c.s.pth.threshold[i] = rd<float>(pth, 8 + 8 * i);
    c.s.pth.damage_modifier[i] = rd<float>(pth, 12 + 8 * i);
  }
  for (int t = 1; t <= names::kCharAttributeMax; ++t)
    c.s.char_attributes.push_back(
        {t, api.total_char_attr(p, t), api.total_char_mod(p, t), api.base_char_attr(p, t), t <= 3 ? api.points_spent(p, t) : 0});
}

void sec_defense(Ctx& c) {
  AccBuf buf;
  void* acc = api.combat_acc_ctor(buf.bytes, kDisplayModifyProcs);
  api.all_defense(c.player, acc);
  combat_totals(acc, c.player, c.s.defense);
  api.combat_acc_dtor(acc);
}

// As an incoming hit with no enemy reduction (§13.3).
void sec_defense_processed(Ctx& c) {
  struct alignas(16) Zero {
    unsigned char b[64] = {};
  } const reduction, scale;
  AccBuf buf;
  void* acc = api.combat_acc_ctor(buf.bytes, kDisplayModifyProcs);
  api.all_defense(c.player, acc);
  api.process_defense(acc, c.player, &reduction, &scale, 0.0f);
  auto& cap_bonus = scratch.cap_bonus;
  cap_bonus.clear();
  const Span s = vec(static_cast<const char*>(acc) + 0x38, sizeof(void*));
  using IntFn = int (*)(const void*);
  for (std::size_t i = 0; i < s.count; ++i) {
    const void* attr = rd<const void*>(s.begin, i * sizeof(void*));
    if (!attr || rd<const void*>(rd<const void*>(attr), 0x28) != api.abs_defense_process) continue;
    const int t = rd<IntFn>(rd<const void*>(attr), 0x10)(attr);
    const float bonus = rd<float>(attr, 0x2C);
    const auto it = std::find_if(cap_bonus.begin(), cap_bonus.end(), [&](const auto& e) { return e.first == t; });
    if (it == cap_bonus.end()) cap_bonus.emplace_back(t, bonus);
    else it->second = std::max(it->second, bonus);
  }
  for (snap::CombatTotals& row : c.s.defense) {
    row.processed = api.total_defense(acc, row.type);
    const auto it = std::find_if(cap_bonus.begin(), cap_bonus.end(), [&](const auto& e) { return e.first == row.type; });
    if (it != cap_bonus.end()) {
      row.cap_bonus = it->second;
      row.capped = true;
    }
  }
  api.combat_acc_dtor(acc);
}

// As Player::CalculateDamageModifiers 0x3CD7A0: Clear, then this call.
void sec_damage_modifiers(Ctx& c) {
  AccBuf buf;
  void* acc = api.combat_acc_ctor(buf.bytes, kDisplayModifyProcs);
  api.collect_offensive_modifiers(api.skill_manager(c.player), acc, 0);
  combat_totals(acc, c.player, c.s.damage_modifiers);
  api.combat_acc_dtor(acc);
}

void sec_conversions(Ctx& c) {
  conversions(api.skill_manager(c.player), api.collect_conversions, c.s.conversions);
}

void sec_skill_attributes(Ctx& c) {
  skill_attributes(api.skill_manager(c.player), api.collect_skill_attributes, c.s.skill_attributes);
}

}  // namespace gdc::adapter
