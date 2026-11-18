#include "gdc/components.h"

#include <algorithm>
#include <cmath>

#include "gdc/db_fields.h"

namespace gdc::components {
namespace {

constexpr int kAbsorption = 39;
constexpr double kEps = 1e-4;

template <class Row>
Row& row(std::vector<Row>& v, int type) {
  for (Row& r : v)
    if (r.type == type) return r;
  Row r;
  r.type = type;
  return v.emplace_back(r);
}

bool zero(const snap::CharContribution& c) { return std::abs(c.value) < kEps && std::abs(c.modifier) < kEps; }
bool zero(const snap::CombatTotals& c) {
  return std::abs(c.defense) < kEps && std::abs(c.defense_modifier) < kEps && std::abs(c.reduction) < kEps &&
         std::abs(c.damage_modifier) < kEps && std::abs(c.dot_modifier) < kEps && !c.proc;
}
bool zero(const snap::SkillAttribute& c) {
  return std::abs(c.value) < kEps && std::abs(c.chance_value) < kEps && std::abs(c.modifier) < kEps &&
         std::abs(c.chance_modifier) < kEps;
}

void add(std::vector<snap::CharContribution>& to, const std::vector<snap::CharContribution>& from, float sign) {
  for (const auto& c : from) {
    auto& r = row(to, c.type);
    r.value += sign * c.value;
    r.modifier += sign * c.modifier;
  }
  std::erase_if(to, [](const auto& c) { return zero(c); });
}

void add(std::vector<snap::CombatTotals>& to, const std::vector<snap::CombatTotals>& from, float sign) {
  for (const auto& c : from) {
    auto& r = row(to, c.type);
    if (c.type != kAbsorption) r.defense += sign * c.defense;
    r.defense_modifier += sign * c.defense_modifier;
    r.reduction += sign * c.reduction;
    r.damage_modifier += sign * c.damage_modifier;
    r.dot_modifier += sign * c.dot_modifier;
  }
  std::erase_if(to, [](const auto& c) { return zero(c); });
}

void add(std::vector<snap::SkillAttribute>& to, const std::vector<snap::SkillAttribute>& from, float sign) {
  for (const auto& c : from) {
    auto& r = row(to, c.type);
    r.value += sign * c.value;
    r.chance_value += sign * c.chance_value;
    r.modifier += sign * c.modifier;
    r.chance_modifier += sign * c.chance_modifier;
  }
  std::erase_if(to, [](const auto& c) { return zero(c); });
}

void add(std::vector<snap::Conversion>& to, const std::vector<snap::Conversion>& from, float sign) {
  for (const auto& c : from) {
    auto at = std::find_if(to.begin(), to.end(), [&](const auto& x) { return x.from == c.from && x.to == c.to; });
    if (at == to.end()) at = to.insert(to.end(), snap::Conversion{c.from, c.to, 0});
    at->percent += sign * c.percent;
  }
  std::erase_if(to, [](const auto& c) { return std::abs(c.percent) < 1e-6; });
}

void add(snap::ItemStats& to, const snap::ItemStats& from, float sign) {
  add(to.char_attributes, from.char_attributes, sign);
  add(to.defense, from.defense, sign);
  add(to.offensive_modifiers, from.offensive_modifiers, sign);
  add(to.retaliation_modifiers, from.retaliation_modifiers, sign);
  add(to.skill_attributes, from.skill_attributes, sign);
  add(to.conversions, from.conversions, sign);
}

std::vector<snap::Damage> flat_damage(const arz::Record& rec) {
  std::vector<snap::Damage> out;
  for (const auto& f : fields::flat_damage()) {
    const double min = rec.number(f.min);
    if (min == 0) continue;
    snap::Damage d;
    d.type = f.type;
    d.dot = !f.duration.empty();
    d.duration = d.dot ? static_cast<float>(rec.number(f.duration)) : 0.0f;
    const double max = std::max(min, rec.number(f.max));
    const double chance = rec.number(f.chance);
    if (chance > 0 && chance < 100) {
      d.chance = static_cast<float>(chance);
      d.raw_min = static_cast<float>(min);
      d.raw_max = static_cast<float>(max);
      d.min = static_cast<float>(min * chance / 100);
      d.max = static_cast<float>(max * chance / 100);
    } else {
      d.min = static_cast<float>(min);
      d.max = static_cast<float>(max);
    }
    out.push_back(d);
  }
  return out;
}

bool same(const snap::Damage& a, const snap::Damage& b) {
  auto near = [](float x, float y) { return std::abs(x - y) <= 0.01f; };
  return a.type == b.type && a.dot == b.dot && near(a.min, b.min) && near(a.max, b.max) &&
         (!a.dot || near(a.duration, b.duration));
}

void remove_damage(snap::Item& it, const std::string& record, const arz::Database* db, std::vector<std::string>& notes) {
  if (!db) {
    notes.push_back("no database: flat damage of " + record + " not removed");
    return;
  }
  const arz::Record* rec = db->record(record);
  if (!rec) {
    notes.push_back(record + " not in the database: its flat damage is not removed");
    return;
  }
  for (const auto& d : flat_damage(*rec)) {
    auto& v = it.stats->offensive_damage;
    const auto at = std::find_if(v.begin(), v.end(), [&](const snap::Damage& x) { return same(x, d); });
    if (at != v.end()) v.erase(at);
    else notes.push_back("flat damage (type " + std::to_string(d.type) + ") of " + record + " not found on the item");
  }
}

void add_damage(snap::Item& it, const std::string& record, const arz::Database* db, std::vector<std::string>& notes) {
  if (!db) {
    notes.push_back("no database: flat damage of " + record + " not added");
    return;
  }
  const arz::Record* rec = db->record(record);
  if (!rec) {
    notes.push_back(record + " not in the database: its flat damage is not added");
    return;
  }
  for (const auto& d : flat_damage(*rec)) it.stats->offensive_damage.push_back(d);
}

// Unknown record: assume it has stats.
bool has_stats(const std::string& record, const arz::Database* db) {
  const arz::Record* rec = db ? db->record(record) : nullptr;
  if (!rec) return true;
  for (const auto& [key, field] : rec->fields) {
    if (field.kind != arz::Field::Kind::Int && field.kind != arz::Field::Kind::Float) continue;
    if (std::none_of(field.numbers.begin(), field.numbers.end(), [](double v) { return v != 0; })) continue;
    double scale;
    if (fields::lookup(key, scale) || fields::flat_damage_field(key)) return true;
  }
  return false;
}

// ItemReplicaInfo +0xB0: completion bonus (artifacts: their own).
bool component_bonus(const snap::Item& it) {
  return it.replica && !it.replica->relic_bonus.empty() && (it.relic || !it.replica->relic.empty());
}

}  // namespace

snap::Item strip(const snap::Item& it, const arz::Database* db, std::vector<std::string>& notes) {
  snap::Item out = it;
  for (const auto* c : {&it.relic, &it.augment}) {
    if (!*c) continue;
    if (out.stats && (*c)->stats) add(*out.stats, *(*c)->stats, -1);
    else if (out.stats) notes.push_back("component " + (*c)->record + " has no stats: not removed");
    if (out.stats) remove_damage(out, (*c)->record, db, notes);
  }
  if (it.replica && ((!it.replica->relic.empty() && !it.relic) || (!it.replica->augment.empty() && !it.augment)))
    notes.push_back("component in the item's replica but not captured: not removed");
  // shortcut: a completion bonus's rolled stats are not in ItemRelic::GetCharAttributes (0x349C00 reads only the
  // AttributePak at +0xD50) and their location is not traced — revisit with a component-with-bonus capture.
  if (component_bonus(it) && has_stats(it.replica->relic_bonus, db))
    notes.push_back("component completion bonus " + it.replica->relic_bonus + ": its stats stay on the item (not traced)");
  out.relic.reset();
  out.augment.reset();
  if (out.replica) {
    if (component_bonus(it)) out.replica->relic_bonus.clear();
    out.replica->relic.clear();
    out.replica->augment.clear();
    out.replica->relic_seed = out.replica->augment_seed = out.replica->augment_level = 0;
  }
  return out;
}

snap::Item attach(const snap::Item& base, const snap::Item& donor, bool with_relic, bool with_augment,
                  const arz::Database* db, std::vector<std::string>& notes) {
  snap::Item out = base;
  if (!out.replica) out.replica.emplace();
  auto put = [&](const std::optional<snap::Component>& c, std::optional<snap::Component>& slot, std::string& replica) {
    if (!c) return;
    slot = c;
    replica = c->record;
    if (!out.stats) return;
    if (c->stats) add(*out.stats, *c->stats, +1);
    else notes.push_back("component " + c->record + " has no stats: not added");
    add_damage(out, c->record, db, notes);
  };
  if (with_relic) {
    put(donor.relic, out.relic, out.replica->relic);
    if (donor.relic && component_bonus(donor)) {
      out.replica->relic_bonus = donor.replica->relic_bonus;  // skill grants move with it
      if (has_stats(donor.replica->relic_bonus, db))
        notes.push_back("completion bonus " + donor.replica->relic_bonus + ": stats not moved (not traced)");
    }
  }
  if (with_augment) put(donor.augment, out.augment, out.replica->augment);
  return out;
}

bool fits(const std::string& component_record, const std::string& item_record, int item_type,
          const arz::Database& db) {
  const char* field = nullptr;
  switch (item_type) {
    case 1: field = "head"; break;
    case 2: field = "amulet"; break;
    case 3: field = "chest"; break;
    case 4: field = "legs"; break;
    case 5: field = "feet"; break;
    case 6: field = "ring"; break;
    case 7: field = "hands"; break;
    case 8: {
      const arz::Record* item = db.record(item_record);
      if (!item) return false;
      if (item->cls == "WeaponArmor_Shield") field = "shield";
      else if (item->cls == "WeaponArmor_Offhand") field = "offhand";
      else return false;
      break;
    }
    case 13: field = "waist"; break;
    case 14: field = "shoulders"; break;
    case 15: field = "medal"; break;
    default: return false;
  }
  const arz::Record* rec = db.record(component_record);
  return rec && rec->number(field) != 0;
}

}  // namespace gdc::components
