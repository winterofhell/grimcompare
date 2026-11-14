#include "gdc/snapshot.h"

#include <charconv>
#include <stdexcept>
#include <system_error>
#include <type_traits>

#include <nlohmann/json.hpp>

#include "gdc/attribute_names.h"

namespace gdc::snap {
namespace {

using J = nlohmann::ordered_json;

void put(J& j, const char* k, float v) {
  if (v != 0) j[k] = widen(v);
}
void put(J& j, const char* k, std::uint32_t v) {
  if (v != 0) j[k] = v;
}
void put(J& j, const char* k, const std::string& v) {
  if (!v.empty()) j[k] = v;
}
void put(J& j, const char* k, bool v) {
  if (v) j[k] = true;
}

template <class T>
void get(const J& j, const char* k, T& out) {
  if (const auto it = j.find(k); it != j.end()) out = it->get<T>();
}

J char_attribute(const CharAttribute& a) {
  J j{{"type", a.type}};
  put(j, "name", std::string(names::char_attribute(a.type)));
  put(j, "total", a.total);
  put(j, "modifier", a.modifier);
  put(j, "base", a.base);
  put(j, "points_spent", a.points_spent);
  return j;
}

J char_contribution(const CharContribution& c) {
  J j{{"type", c.type}};
  put(j, "name", std::string(names::char_attribute(c.type)));
  put(j, "value", c.value);
  put(j, "modifier", c.modifier);
  return j;
}

J combat(const CombatTotals& c) {
  J j{{"type", c.type}};
  put(j, "name", std::string(names::combat_attribute(c.type)));
  put(j, "defense", c.defense);
  put(j, "defense_modifier", c.defense_modifier);
  put(j, "reduction", c.reduction);
  put(j, "damage_modifier", c.damage_modifier);
  put(j, "dot_modifier", c.dot_modifier);
  put(j, "processed", c.processed);
  put(j, "cap_bonus", c.cap_bonus);
  put(j, "capped", c.capped);
  if (c.proc) {
    J p{{"chance", widen(c.proc->chance)}};
    put(p, "defense", c.proc->defense);
    put(p, "defense_modifier", c.proc->defense_modifier);
    put(p, "reduction", c.proc->reduction);
    put(p, "damage_modifier", c.proc->damage_modifier);
    put(p, "dot_modifier", c.proc->dot_modifier);
    j["proc"] = std::move(p);
  }
  return j;
}

J damage(const Damage& d) {
  J j{{"type", d.type}};
  put(j, "name", std::string(names::combat_attribute(d.type)));
  put(j, "dot", d.dot);
  put(j, "min", d.min);
  put(j, "max", d.max);
  put(j, "duration", d.duration);
  put(j, "chance", d.chance);
  put(j, "raw_min", d.raw_min);
  put(j, "raw_max", d.raw_max);
  return j;
}

J skill_attribute(const SkillAttribute& a) {
  J j{{"type", a.type}};
  put(j, "name", std::string(names::skill_attribute(a.type)));
  put(j, "value", a.value);
  put(j, "chance_value", a.chance_value);
  put(j, "modifier", a.modifier);
  put(j, "chance_modifier", a.chance_modifier);
  return j;
}

J conversion(const Conversion& c) {
  J j{{"from", c.from}, {"to", c.to}};
  put(j, "from_name", std::string(names::combat_attribute(c.from)));
  put(j, "to_name", std::string(names::combat_attribute(c.to)));
  put(j, "percent", c.percent);
  return j;
}

template <class T, class F>
void put_list(J& j, const char* k, const std::vector<T>& v, F f) {
  if (v.empty()) return;
  J a = J::array();
  for (const auto& e : v) a.push_back(f(e));
  j[k] = std::move(a);
}

J replica(const Replica& r) {
  J j = J::object();
  put(j, "base", r.base);
  put(j, "prefix", r.prefix);
  put(j, "suffix", r.suffix);
  put(j, "modifier", r.modifier);
  put(j, "transmute", r.transmute);
  put(j, "relic", r.relic);
  put(j, "relic_bonus", r.relic_bonus);
  put(j, "augment", r.augment);
  put(j, "ascendant", r.ascendant);
  put(j, "ascendant2h", r.ascendant2h);
  put(j, "seed", r.seed);
  put(j, "relic_seed", r.relic_seed);
  put(j, "augment_level", r.augment_level);
  put(j, "augment_seed", r.augment_seed);
  put(j, "var1", r.var1);
  put(j, "stack_count", r.stack_count);
  put(j, "affix_rerolls", r.affix_rerolls);
  put(j, "rerolls", r.rerolls);
  return j;
}

J stats(const ItemStats& s) {
  J j = J::object();
  put_list(j, "char_attributes", s.char_attributes, char_contribution);
  put_list(j, "defense", s.defense, combat);
  put_list(j, "offensive_modifiers", s.offensive_modifiers, combat);
  put_list(j, "retaliation_modifiers", s.retaliation_modifiers, combat);
  put_list(j, "offensive_damage", s.offensive_damage, damage);
  put_list(j, "retaliation", s.retaliation, damage);
  put_list(j, "skill_attributes", s.skill_attributes, skill_attribute);
  put_list(j, "conversions", s.conversions, conversion);
  return j;
}

J component(const Component& c) {
  J j = J::object();
  put(j, "record", c.record);
  if (c.stats) j["stats"] = stats(*c.stats);
  return j;
}

J item(const Item& i) {
  J j{{"id", i.id}};
  if (i.location != -1) {
    j["location"] = i.location;
    put(j, "location_name", std::string(names::equip_location(i.location)));
  }
  put(j, "record", i.record);
  put(j, "kind", i.kind);
  put(j, "set", i.set);
  if (i.replica) j["replica"] = replica(*i.replica);
  if (i.stats) j["stats"] = stats(*i.stats);
  if (i.relic) j["relic"] = component(*i.relic);
  if (i.augment) j["augment"] = component(*i.augment);
  return j;
}

J skill(const Skill& s) {
  J j{{"id", s.id}};
  put(j, "record", s.record);
  put(j, "type", s.type);
  put(j, "level", s.level);
  put(j, "augmented_level", s.augmented_level);
  put(j, "current_level", s.current_level);
  put(j, "devotion_level", s.devotion_level);
  put(j, "devotion_max_level", s.devotion_max_level);
  put(j, "mastery_id", s.mastery_id);
  if (s.passive && (!s.passive->char_attributes.empty() || !s.passive->defense.empty()))
    j["passive"] = stats(*s.passive);
  if (!s.modifier_ids.empty()) j["modifier_ids"] = s.modifier_ids;
  return j;
}

J item_skill_modifier(const ItemSkillModifier& m) {
  J j{{"skill_id", m.skill_id}};
  put(j, "skill_record", m.skill_record);
  put(j, "modifier_id", m.modifier_id);
  put(j, "modifier_record", m.modifier_record);
  if (m.location != -1) {
    j["location"] = m.location;
    put(j, "location_name", std::string(names::equip_location(m.location)));
  }
  put(j, "from_relic", m.from_relic);
  put(j, "from_augment", m.from_augment);
  if (m.stats) j["stats"] = stats(*m.stats);
  return j;
}

// --- reading

CharAttribute read_char_attribute(const J& j) {
  CharAttribute a;
  get(j, "type", a.type);
  get(j, "total", a.total);
  get(j, "modifier", a.modifier);
  get(j, "base", a.base);
  get(j, "points_spent", a.points_spent);
  return a;
}

CharContribution read_char_contribution(const J& j) {
  CharContribution c;
  get(j, "type", c.type);
  get(j, "value", c.value);
  get(j, "modifier", c.modifier);
  return c;
}

CombatTotals read_combat(const J& j) {
  CombatTotals c;
  get(j, "type", c.type);
  get(j, "defense", c.defense);
  get(j, "defense_modifier", c.defense_modifier);
  get(j, "reduction", c.reduction);
  get(j, "damage_modifier", c.damage_modifier);
  get(j, "dot_modifier", c.dot_modifier);
  get(j, "processed", c.processed);
  get(j, "cap_bonus", c.cap_bonus);
  get(j, "capped", c.capped);
  if (const auto it = j.find("proc"); it != j.end()) {
    Proc p;
    get(*it, "chance", p.chance);
    get(*it, "defense", p.defense);
    get(*it, "defense_modifier", p.defense_modifier);
    get(*it, "reduction", p.reduction);
    get(*it, "damage_modifier", p.damage_modifier);
    get(*it, "dot_modifier", p.dot_modifier);
    c.proc = p;
  }
  return c;
}

Damage read_damage(const J& j) {
  Damage d;
  get(j, "type", d.type);
  get(j, "dot", d.dot);
  get(j, "min", d.min);
  get(j, "max", d.max);
  get(j, "duration", d.duration);
  get(j, "chance", d.chance);
  get(j, "raw_min", d.raw_min);
  get(j, "raw_max", d.raw_max);
  return d;
}

SkillAttribute read_skill_attribute(const J& j) {
  SkillAttribute a;
  get(j, "type", a.type);
  get(j, "value", a.value);
  get(j, "chance_value", a.chance_value);
  get(j, "modifier", a.modifier);
  get(j, "chance_modifier", a.chance_modifier);
  return a;
}

Conversion read_conversion(const J& j) {
  Conversion c;
  get(j, "from", c.from);
  get(j, "to", c.to);
  get(j, "percent", c.percent);
  return c;
}

template <class T, class F>
void get_list(const J& j, const char* k, std::vector<T>& out, F f) {
  const auto it = j.find(k);
  if (it == j.end()) return;
  if (!it->is_array()) throw std::runtime_error(std::string(k) + ": not an array");
  for (const auto& e : *it) out.push_back(f(e));
}

Replica read_replica(const J& j) {
  Replica r;
  get(j, "base", r.base);
  get(j, "prefix", r.prefix);
  get(j, "suffix", r.suffix);
  get(j, "modifier", r.modifier);
  get(j, "transmute", r.transmute);
  get(j, "relic", r.relic);
  get(j, "relic_bonus", r.relic_bonus);
  get(j, "augment", r.augment);
  get(j, "ascendant", r.ascendant);
  get(j, "ascendant2h", r.ascendant2h);
  get(j, "seed", r.seed);
  get(j, "relic_seed", r.relic_seed);
  get(j, "augment_level", r.augment_level);
  get(j, "augment_seed", r.augment_seed);
  get(j, "var1", r.var1);
  get(j, "stack_count", r.stack_count);
  get(j, "affix_rerolls", r.affix_rerolls);
  get(j, "rerolls", r.rerolls);
  return r;
}

ItemStats read_stats(const J& j) {
  ItemStats s;
  get_list(j, "char_attributes", s.char_attributes, read_char_contribution);
  get_list(j, "defense", s.defense, read_combat);
  get_list(j, "offensive_modifiers", s.offensive_modifiers, read_combat);
  get_list(j, "retaliation_modifiers", s.retaliation_modifiers, read_combat);
  get_list(j, "offensive_damage", s.offensive_damage, read_damage);
  get_list(j, "retaliation", s.retaliation, read_damage);
  get_list(j, "skill_attributes", s.skill_attributes, read_skill_attribute);
  get_list(j, "conversions", s.conversions, read_conversion);
  return s;
}

Component read_component(const J& j) {
  Component c;
  get(j, "record", c.record);
  if (j.contains("stats")) c.stats = read_stats(j.at("stats"));
  return c;
}

Item read_item(const J& j) {
  Item i;
  get(j, "id", i.id);
  get(j, "location", i.location);
  get(j, "record", i.record);
  get(j, "kind", i.kind);
  get(j, "set", i.set);
  if (j.contains("replica")) i.replica = read_replica(j.at("replica"));
  if (j.contains("stats")) i.stats = read_stats(j.at("stats"));
  if (j.contains("relic")) i.relic = read_component(j.at("relic"));
  if (j.contains("augment")) i.augment = read_component(j.at("augment"));
  return i;
}

Skill read_skill(const J& j, int schema) {
  Skill s;
  get(j, "id", s.id);
  get(j, "record", s.record);
  get(j, "type", s.type);
  get(j, "level", s.level);
  get(j, "augmented_level", s.augmented_level);
  get(j, "current_level", s.current_level);
  get(j, "devotion_level", s.devotion_level);
  get(j, "devotion_max_level", s.devotion_max_level);
  get(j, "mastery_id", s.mastery_id);
  if (j.contains("passive")) s.passive = read_stats(j.at("passive"));
  else if (schema >= 4) s.passive = ItemStats{};
  get(j, "modifier_ids", s.modifier_ids);
  return s;
}

ItemSkillModifier read_item_skill_modifier(const J& j) {
  ItemSkillModifier m;
  get(j, "skill_id", m.skill_id);
  get(j, "skill_record", m.skill_record);
  get(j, "modifier_id", m.modifier_id);
  get(j, "modifier_record", m.modifier_record);
  get(j, "location", m.location);
  get(j, "from_relic", m.from_relic);
  get(j, "from_augment", m.from_augment);
  if (j.contains("stats")) m.stats = read_stats(j.at("stats"));
  return m;
}

J pth(const PthTable& t) {
  J j = J::object();
  j["minimum"] = widen(t.minimum);
  j["crit_divisor_min"] = widen(t.crit_divisor_min);
  for (int i = 0; i < 6; ++i) {
    j["thresholds"].push_back(widen(t.threshold[i]));
    j["damage_modifiers"].push_back(widen(t.damage_modifier[i]));
  }
  return j;
}

PthTable read_pth(const J& j) {
  PthTable t;
  get(j, "minimum", t.minimum);
  get(j, "crit_divisor_min", t.crit_divisor_min);
  const J& th = j.at("thresholds");
  const J& dm = j.at("damage_modifiers");
  if (th.size() != 6 || dm.size() != 6) throw std::runtime_error("pth: expected 6 thresholds and 6 damage modifiers");
  for (int i = 0; i < 6; ++i) {
    t.threshold[i] = th[i].get<float>();
    t.damage_modifier[i] = dm[i].get<float>();
  }
  return t;
}

}  // namespace

double widen(float f) {
  char buf[32];
  const auto r = std::to_chars(buf, buf + sizeof buf, f);
  double d = f;
  if (r.ec == std::errc{}) std::from_chars(buf, r.ptr, d);
  return d;
}

std::string to_json(const Snapshot& s, int indent) {
  J j{{"schema", s.schema}};
  put(j, "mod_version", s.mod_version);
  put(j, "game_build", s.game_build);
  put(j, "taken_at", s.taken_at);
  j["complete"] = s.complete;
  if (!s.errors.empty()) j["errors"] = s.errors;

  J c = J::object();
  put(c, "name", s.character_name);
  put(c, "level", s.level);
  c["difficulty"] = s.difficulty;
  put(c, "difficulty_name", std::string(names::difficulty(s.difficulty)));
  put(c, "skill_points", s.skill_points);
  put(c, "attribute_points", s.attribute_points);
  put(c, "devotion_points", s.devotion_points);
  put(c, "devotion_points_total", s.devotion_points_total);
  put(c, "devotion_current_level_sum", s.devotion_current_level_sum);
  put(c, "life_max", s.life_max);
  put(c, "mana_max", s.mana_max);
  put(c, "attack_speed", s.attack_speed);
  put(c, "cast_speed", s.cast_speed);
  put(c, "run_speed", s.run_speed);
  put(c, "run_speed_uncapped", s.run_speed_uncapped);
  put(c, "defense_cap", s.defense_cap);
  if (s.pth.crit_divisor_min != 0) c["pth"] = pth(s.pth);
  j["character"] = std::move(c);

  put_list(j, "char_attributes", s.char_attributes, char_attribute);
  put_list(j, "defense", s.defense, combat);
  put_list(j, "damage_modifiers", s.damage_modifiers, combat);
  put_list(j, "conversions", s.conversions, conversion);
  put_list(j, "skills", s.skills, skill);
  put_list(j, "item_skill_modifiers", s.item_skill_modifiers, item_skill_modifier);
  put_list(j, "equipped", s.equipped, item);
  put_list(j, "tooltip_items", s.tooltip_items, item);
  put(j, "tooltip_age_ms", s.tooltip_age_ms);
  put_list(j, "inventory", s.inventory, item);
  return indent < 0 ? j.dump() : j.dump(indent) + "\n";
}

Snapshot from_json(std::string_view text) {
  try {
    const J j = J::parse(text);
    if (!j.is_object()) throw std::runtime_error("not a JSON object");
    Snapshot s;
    get(j, "schema", s.schema);
    if (s.schema < kMinSchema || s.schema > kSchema)
      throw std::runtime_error("unsupported schema " + std::to_string(s.schema) + " (expected " +
                               std::to_string(kMinSchema) + ".." + std::to_string(kSchema) + ")");
    get(j, "mod_version", s.mod_version);
    get(j, "game_build", s.game_build);
    get(j, "taken_at", s.taken_at);
    get(j, "complete", s.complete);
    get(j, "errors", s.errors);
    if (const auto c = j.find("character"); c != j.end()) {
      get(*c, "name", s.character_name);
      get(*c, "level", s.level);
      get(*c, "difficulty", s.difficulty);
      get(*c, "skill_points", s.skill_points);
      get(*c, "attribute_points", s.attribute_points);
      get(*c, "devotion_points", s.devotion_points);
      get(*c, "devotion_points_total", s.devotion_points_total);
      get(*c, "devotion_current_level_sum", s.devotion_current_level_sum);
      get(*c, "life_max", s.life_max);
      get(*c, "mana_max", s.mana_max);
      get(*c, "attack_speed", s.attack_speed);
      get(*c, "cast_speed", s.cast_speed);
      get(*c, "run_speed", s.run_speed);
      get(*c, "run_speed_uncapped", s.run_speed_uncapped);
      get(*c, "defense_cap", s.defense_cap);
      if (const auto p = c->find("pth"); p != c->end()) s.pth = read_pth(*p);
    }
    get_list(j, "char_attributes", s.char_attributes, read_char_attribute);
    get_list(j, "defense", s.defense, read_combat);
    get_list(j, "damage_modifiers", s.damage_modifiers, read_combat);
    get_list(j, "conversions", s.conversions, read_conversion);
    get_list(j, "skills", s.skills, [&](const J& x) { return read_skill(x, s.schema); });
    get_list(j, "item_skill_modifiers", s.item_skill_modifiers, read_item_skill_modifier);
    get_list(j, "equipped", s.equipped, read_item);
    get_list(j, "tooltip_items", s.tooltip_items, read_item);
    get(j, "tooltip_age_ms", s.tooltip_age_ms);
    get_list(j, "inventory", s.inventory, read_item);
    return s;
  } catch (const nlohmann::json::exception& e) {
    throw std::runtime_error(std::string("snapshot JSON: ") + e.what());
  }
}

namespace {

// Strings keep their buffer: moved out, cleared, moved back after the reset.
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

// Reversed, so that reuse() (pop_back) returns them in their original order.
template <class T>
void spare_out(std::vector<T>& from, std::vector<T>& spare) {
  for (auto it = from.rbegin(); it != from.rend(); ++it) {
    reset(*it);
    spare.push_back(std::move(*it));
  }
  from.clear();
}

}  // namespace

void recycle(Snapshot& s, Spares& spare) {
  // Pushed last-first: inventory, tooltip_items, equipped, so equipped come back first.
  spare_out(s.inventory, spare.items);
  spare_out(s.tooltip_items, spare.items);
  spare_out(s.equipped, spare.items);
  spare_out(s.skills, spare.skills);
  spare_out(s.item_skill_modifiers, spare.item_skill_modifiers);
  std::string mv = keep(s.mod_version), gb = keep(s.game_build), at = keep(s.taken_at), name = keep(s.character_name);
  auto ca = keep(s.char_attributes);
  auto def = keep(s.defense);
  auto dm = keep(s.damage_modifiers);
  auto conv = keep(s.conversions);
  // skills / item_skill_modifiers / equipped / tooltip_items / inventory: empty vectors whose capacity
  // stays (capacity of an empty vector survives the move back).
  auto sk = std::move(s.skills);
  auto ism = std::move(s.item_skill_modifiers);
  auto eq = std::move(s.equipped);
  auto tt = std::move(s.tooltip_items);
  auto inv = std::move(s.inventory);
  s = Snapshot{};
  s.mod_version = std::move(mv);
  s.game_build = std::move(gb);
  s.taken_at = std::move(at);
  s.character_name = std::move(name);
  s.char_attributes = std::move(ca);
  s.defense = std::move(def);
  s.damage_modifiers = std::move(dm);
  s.conversions = std::move(conv);
  s.skills = std::move(sk);
  s.item_skill_modifiers = std::move(ism);
  s.equipped = std::move(eq);
  s.tooltip_items = std::move(tt);
  s.inventory = std::move(inv);
}

}  // namespace gdc::snap
