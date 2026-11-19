#include "gdc/snapshot.h"

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
  if (i.requirements) {
    const Requirements& r = *i.requirements;
    J q = J::object();
    put(q, "level", r.level);
    put(q, "physique", r.attribute[0]);
    put(q, "cunning", r.attribute[1]);
    put(q, "spirit", r.attribute[2]);
    J red = J::array();
    for (const auto& a : r.reductions) red.push_back({a[0], a[1], a[2]});
    q["reductions"] = std::move(red);
    if (r.met) q["met"] = *r.met;
    if (r.met_at)
      q["met_at"] = {{"level", r.met_at->level},
                     {"difficulty", r.met_at->difficulty},
                     {"attributes", {r.met_at->attribute[0], r.met_at->attribute[1], r.met_at->attribute[2]}}};
    j["requirements"] = std::move(q);
  }
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
  put(j, "item_id", s.item_id);
  if (s.buff_active && *s.buff_active) j["buff_active"] = true;
  put(j, "anim_frames", s.anim_frames);
  if (s.anim_frame_rate != 0) j["anim_frame_rate"] = s.anim_frame_rate;
  return j;
}

J pet(const Pet& p) {
  J j{{"id", p.id}};
  put(j, "skill_id", p.skill_id);
  put(j, "record", p.record);
  put(j, "attack_speed", p.attack_speed);
  put(j, "cast_speed", p.cast_speed);
  put(j, "attack_speed_modifier", p.attack_speed_modifier);
  put(j, "cast_speed_modifier", p.cast_speed_modifier);
  put(j, "default_skill_id", p.default_skill_id);
  put_list(j, "skills", p.skills, [](const PetSkill& k) {
    J x{{"id", k.id}};
    put(x, "record", k.record);
    put(x, "current_level", k.current_level);
    put(x, "anim_frames", k.anim_frames);
    if (k.anim_frame_rate != 0) x["anim_frame_rate"] = k.anim_frame_rate;
    return x;
  });
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

J skill_dps(const SkillDps& d) {
  J j{{"skill_id", d.skill_id}};
  put(j, "record", d.record);
  put(j, "default_attack", d.default_attack);
  put(j, "dps", d.dps);
  put_list(j, "main_hand", d.main_hand, damage);
  put_list(j, "off_hand", d.off_hand, damage);
  return j;
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

J character(const Snapshot& s) {
  J c = J::object();
  put(c, "name", s.character_name);
  put(c, "save", s.save);
  put(c, "level", s.level);
  c["difficulty"] = s.difficulty;
  put(c, "difficulty_name", std::string(names::difficulty(s.difficulty)));
  put(c, "difficulty_ramp", s.difficulty_ramp);
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
  return c;
}

}  // namespace

std::string to_json(const Snapshot& s, int indent) {
  J j{{"schema", s.schema}};
  put(j, "mod_version", s.mod_version);
  put(j, "game_build", s.game_build);
  put(j, "taken_at", s.taken_at);
  j["complete"] = s.complete;
  if (!s.errors.empty()) j["errors"] = s.errors;

  j["character"] = character(s);

  put_list(j, "char_attributes", s.char_attributes, char_attribute);
  put_list(j, "defense", s.defense, combat);
  put_list(j, "damage_modifiers", s.damage_modifiers, combat);
  put_list(j, "conversions", s.conversions, conversion);
  put_list(j, "skill_attributes", s.skill_attributes, skill_attribute);
  put_list(j, "skills", s.skills, skill);
  put_list(j, "item_skill_modifiers", s.item_skill_modifiers, item_skill_modifier);
  put_list(j, "item_skills", s.item_skills, skill);
  put_list(j, "equipped", s.equipped, item);
  put_list(j, "tooltip_items", s.tooltip_items, item);
  put(j, "tooltip_age_ms", s.tooltip_age_ms);
  put_list(j, "inventory", s.inventory, item);
  put_list(j, "pets", s.pets, pet);
  put_list(j, "skill_dps", s.skill_dps, skill_dps);
  return indent < 0 ? j.dump() : j.dump(indent) + "\n";
}

}  // namespace gdc::snap
