#include "gdc/snapshot.h"

#include <stdexcept>

#include <nlohmann/json.hpp>

namespace gdc::snap {
namespace {

using J = nlohmann::ordered_json;

template <class T>
void get(const J& j, const char* k, T& out) {
  if (const auto it = j.find(k); it != j.end()) out = it->get<T>();
}

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
  if (const auto q = j.find("requirements"); q != j.end()) {
    Requirements& r = i.requirements.emplace();
    get(*q, "level", r.level);
    get(*q, "physique", r.attribute[0]);
    get(*q, "cunning", r.attribute[1]);
    get(*q, "spirit", r.attribute[2]);
    if (const auto red = q->find("reductions"); red != q->end()) {
      if (!red->is_array() || red->size() != 3) throw std::runtime_error("requirements.reductions: not 3 rows");
      for (std::size_t a = 0; a < 3; ++a) {
        const J& row = red->at(a);
        if (!row.is_array() || row.size() != 3) throw std::runtime_error("requirements.reductions: not 3 per row");
        for (std::size_t k = 0; k < 3; ++k) r.reductions[a][k] = row.at(k).get<int>();
      }
    }
    if (const auto m = q->find("met"); m != q->end()) r.met = m->get<bool>();
    if (const auto m = q->find("met_at"); m != q->end()) {
      Requirements::MetAt& at = r.met_at.emplace();
      get(*m, "level", at.level);
      get(*m, "difficulty", at.difficulty);
      const J& a = m->at("attributes");
      if (!a.is_array() || a.size() != 3) throw std::runtime_error("requirements.met_at.attributes: not 3 values");
      for (std::size_t k = 0; k < 3; ++k) at.attribute[k] = a.at(k).get<float>();
    }
  }
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
  get(j, "item_id", s.item_id);
  if (schema >= 8) s.buff_active = j.value("buff_active", false);
  get(j, "anim_frames", s.anim_frames);
  get(j, "anim_frame_rate", s.anim_frame_rate);
  return s;
}

Pet read_pet(const J& j) {
  Pet p;
  get(j, "id", p.id);
  get(j, "skill_id", p.skill_id);
  get(j, "record", p.record);
  get(j, "attack_speed", p.attack_speed);
  get(j, "cast_speed", p.cast_speed);
  get(j, "attack_speed_modifier", p.attack_speed_modifier);
  get(j, "cast_speed_modifier", p.cast_speed_modifier);
  get(j, "default_skill_id", p.default_skill_id);
  get_list(j, "skills", p.skills, [](const J& x) {
    PetSkill k;
    get(x, "id", k.id);
    get(x, "record", k.record);
    get(x, "current_level", k.current_level);
    get(x, "anim_frames", k.anim_frames);
    get(x, "anim_frame_rate", k.anim_frame_rate);
    return k;
  });
  return p;
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

SkillDps read_skill_dps(const J& j) {
  SkillDps d;
  get(j, "skill_id", d.skill_id);
  get(j, "record", d.record);
  get(j, "default_attack", d.default_attack);
  get(j, "dps", d.dps);
  get_list(j, "main_hand", d.main_hand, read_damage);
  get_list(j, "off_hand", d.off_hand, read_damage);
  return d;
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

void read_character(const J& c, Snapshot& s) {
  get(c, "name", s.character_name);
  get(c, "save", s.save);
  if (s.schema < 14) s.save.clear();  // "cloud/_Name": no account, no main / user
  get(c, "level", s.level);
  get(c, "difficulty", s.difficulty);
  get(c, "difficulty_ramp", s.difficulty_ramp);
  get(c, "skill_points", s.skill_points);
  get(c, "attribute_points", s.attribute_points);
  get(c, "devotion_points", s.devotion_points);
  get(c, "devotion_points_total", s.devotion_points_total);
  get(c, "devotion_current_level_sum", s.devotion_current_level_sum);
  get(c, "life_max", s.life_max);
  get(c, "mana_max", s.mana_max);
  get(c, "attack_speed", s.attack_speed);
  get(c, "cast_speed", s.cast_speed);
  get(c, "run_speed", s.run_speed);
  get(c, "run_speed_uncapped", s.run_speed_uncapped);
  get(c, "defense_cap", s.defense_cap);
  if (const auto p = c.find("pth"); p != c.end()) s.pth = read_pth(*p);
}

}  // namespace

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
    if (const auto c = j.find("character"); c != j.end()) read_character(*c, s);
    get_list(j, "char_attributes", s.char_attributes, read_char_attribute);
    get_list(j, "defense", s.defense, read_combat);
    get_list(j, "damage_modifiers", s.damage_modifiers, read_combat);
    get_list(j, "conversions", s.conversions, read_conversion);
    get_list(j, "skill_attributes", s.skill_attributes, read_skill_attribute);
    get_list(j, "skills", s.skills, [&](const J& x) { return read_skill(x, s.schema); });
    get_list(j, "item_skill_modifiers", s.item_skill_modifiers, read_item_skill_modifier);
    get_list(j, "item_skills", s.item_skills, [&](const J& x) { return read_skill(x, s.schema); });
    get_list(j, "equipped", s.equipped, read_item);
    get_list(j, "tooltip_items", s.tooltip_items, read_item);
    get(j, "tooltip_age_ms", s.tooltip_age_ms);
    get_list(j, "inventory", s.inventory, read_item);
    get_list(j, "pets", s.pets, read_pet);
    get_list(j, "skill_dps", s.skill_dps, read_skill_dps);
    return s;
  } catch (const nlohmann::json::exception& e) {
    throw std::runtime_error(std::string("snapshot JSON: ") + e.what());
  }
}

}  // namespace gdc::snap
