#include "gdc/snapshot.h"

#include <algorithm>
#include <charconv>
#include <system_error>
#include <type_traits>

namespace gdc::snap {

double widen(float f) {
  char buf[32];
  const auto r = std::to_chars(buf, buf + sizeof buf, f);
  double d = f;
  if (r.ec == std::errc{}) std::from_chars(buf, r.ptr, d);
  return d;
}

namespace {

std::string keep(std::string& s) {
  std::string out = std::move(s);
  out.clear();
  return out;
}

template <class T>
std::vector<T> keep(std::vector<T>& v) {
  static_assert(std::is_trivially_destructible_v<T>, "clear() would free the elements' storage");
  std::vector<T> out = std::move(v);
  out.clear();
  return out;
}

void reset(Skill& s) {
  std::string r = keep(s.record);
  auto ids = keep(s.modifier_ids);
  s = Skill{};
  s.record = std::move(r);
  s.modifier_ids = std::move(ids);
}

void reset(Item& it) {
  std::string r = keep(it.record), k = keep(it.kind), set = keep(it.set);
  it = Item{};
  it.record = std::move(r);
  it.kind = std::move(k);
  it.set = std::move(set);
}

void reset(ItemSkillModifier& m) {
  std::string sr = keep(m.skill_record), mr = keep(m.modifier_record);
  m = ItemSkillModifier{};
  m.skill_record = std::move(sr);
  m.modifier_record = std::move(mr);
}

// Reversed so reuse() (pop_back) returns them in order.
template <class T>
void spare_out(std::vector<T>& from, std::vector<T>& spare) {
  for (auto it = from.rbegin(); it != from.rend(); ++it) {
    reset(*it);
    spare.push_back(std::move(*it));
  }
  from.clear();
}

}  // namespace

std::string save_key(std::uint32_t steam_account, bool custom, std::string_view folder) {
  return (steam_account ? "cloud/" + std::to_string(steam_account) + "/" : std::string("local/")) +
         (custom ? "user/" : "main/") + std::string(folder);
}

std::string save_key_name(std::string_view key) {
  std::vector<std::string_view> parts;
  for (std::size_t at = 0;;) {
    const auto slash = key.find('/', at);
    parts.push_back(key.substr(at, slash == std::string_view::npos ? std::string_view::npos : slash - at));
    if (slash == std::string_view::npos) break;
    at = slash + 1;
  }
  const bool cloud = parts[0] == "cloud" && parts.size() == 4 && !parts[1].empty() &&
                     std::all_of(parts[1].begin(), parts[1].end(), [](char c) { return c >= '0' && c <= '9'; });
  const bool local = parts[0] == "local" && parts.size() == 3;
  if (!cloud && !local) return {};
  if (const auto f = parts[parts.size() - 2]; f != "main" && f != "user") return {};
  std::string_view folder = parts.back();
  if (folder.starts_with("__")) folder.remove_prefix(2);
  else if (folder.starts_with('_')) folder.remove_prefix(1);
  else return {};
  return std::string(folder);
}

void recycle(Snapshot& s, Spares& spare) {
  spare_out(s.inventory, spare.items);
  spare_out(s.tooltip_items, spare.items);
  spare_out(s.equipped, spare.items);
  spare_out(s.item_skills, spare.skills);
  spare_out(s.skills, spare.skills);
  spare_out(s.item_skill_modifiers, spare.item_skill_modifiers);
  std::string mv = keep(s.mod_version), gb = keep(s.game_build), at = keep(s.taken_at), name = keep(s.character_name),
              save = keep(s.save);
  auto ca = keep(s.char_attributes);
  auto def = keep(s.defense);
  auto dm = keep(s.damage_modifiers);
  auto conv = keep(s.conversions);
  auto sa = keep(s.skill_attributes);
  // Capacity of an empty vector survives the move back.
  auto sk = std::move(s.skills);
  auto ism = std::move(s.item_skill_modifiers);
  auto isk = std::move(s.item_skills);
  auto eq = std::move(s.equipped);
  auto tt = std::move(s.tooltip_items);
  auto inv = std::move(s.inventory);
  s = Snapshot{};
  s.mod_version = std::move(mv);
  s.game_build = std::move(gb);
  s.taken_at = std::move(at);
  s.character_name = std::move(name);
  s.save = std::move(save);
  s.char_attributes = std::move(ca);
  s.defense = std::move(def);
  s.damage_modifiers = std::move(dm);
  s.conversions = std::move(conv);
  s.skill_attributes = std::move(sa);
  s.skills = std::move(sk);
  s.item_skill_modifiers = std::move(ism);
  s.item_skills = std::move(isk);
  s.equipped = std::move(eq);
  s.tooltip_items = std::move(tt);
  s.inventory = std::move(inv);
}

}  // namespace gdc::snap
