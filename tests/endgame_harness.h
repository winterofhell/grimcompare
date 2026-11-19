#pragma once
// Endgame characters put together from the database: items with their fixed record stats (legendaries and set
// pieces roll no affixes), sets from itemSetName, the build's skills, modifiers and devotion powers. The sheet starts
// from the level 96 harness character (build_harness.h), so values are checked for sanity, swaps for their sign.
#include "build_harness.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <string>
#include <vector>

#include "buildpreset.h"
#include "catalog.h"
#include "gdc/attribute_names.h"
#include "gdc/classes.h"
#include "gdc/db_fields.h"

namespace gdc::test {

inline int db_damage_type(std::string_view name) {
  for (int t : {2, 4, 5, 6, 7, 8, 9, 10, 11, 15, 58})
    if (names::combat_attribute(t) == name) return t;
  return 0;
}

inline snap::CombatTotals& totals(std::vector<snap::CombatTotals>& v, int type) {
  for (auto& c : v)
    if (c.type == type) return c;
  v.push_back({});
  v.back().type = type;
  return v.back();
}

// An item as the engine's getters would report it, from its record's fixed fields: char attributes, resists (elemental
// spread to Cold / Fire / Lightning as the engine adds them), armor, damage % (total damage spread to every type, its
// own row direct only, as fixture b002e_torso), flat and retaliation damage, conversions.
inline snap::Item db_item(const arz::Database& db, int location, const std::string& record) {
  using namespace combat_attr;
  snap::Item it = bare(location, record.c_str());
  const arz::Record* r = db.record(record);
  REQUIRE_MESSAGE(r, record);
  it.set = std::string(r->string("itemSetName"));
  snap::ItemStats& x = *it.stats;
  for (const auto& [key, field] : r->fields) {
    if (field.kind != arz::Field::Kind::Int && field.kind != arz::Field::Kind::Float) continue;
    const double v = field.numbers.empty() ? 0 : field.numbers[0];
    if (v == 0) continue;
    const auto f = static_cast<float>(v);
    if (key == "defensiveElementalResistance") {
      for (int t : {kCold, kFire, kLightning}) totals(x.defense, t).defense += f;
      continue;
    }
    if (key == "defensiveProtection") {
      totals(x.defense, kArmor).defense += f;
      continue;
    }
    if (key == "retaliationTotalDamageModifier") {
      totals(x.retaliation_modifiers, kAll).damage_modifier += f;
      continue;
    }
    double scale = 1;
    const fields::Target* t = fields::lookup(key, scale);
    if (!t) continue;
    const auto s = static_cast<float>(v * scale);
    switch (t->kind) {
      case fields::Kind::Char: {
        snap::CharContribution c{t->type, t->modifier ? 0 : s, t->modifier ? s : 0};
        x.char_attributes.push_back(c);
        break;
      }
      case fields::Kind::Defense: {
        auto& d = totals(x.defense, t->type);
        (t->modifier ? d.defense_modifier : d.defense) += s;
        break;
      }
      case fields::Kind::Damage: {
        auto add = [&](int type, bool dot) { (dot ? totals(x.offensive_modifiers, type).dot_modifier
                                                  : totals(x.offensive_modifiers, type).damage_modifier) += s; };
        if (t->type == kAll && !t->modifier) {
          for (int y : fields::spread(kAll, false)) add(y, false);
          for (int y : fields::spread(kAll, true)) add(y, true);
          add(kAll, false);
        } else if (t->type == kElemental || t->type == kAll) {
          for (int y : fields::spread(t->type, t->modifier)) add(y, t->modifier);
        } else {
          add(t->type, t->modifier);
        }
        break;
      }
      default: break;
    }
  }
  // A weapon's own Physical is its base damage (BasePhysical, as build_harness.h's hand); a shield's is not.
  const bool weapon = r->cls.starts_with("Weapon") && !r->cls.starts_with("WeaponArmor");
  for (const auto& f : fields::flat_damage()) {
    const double min = r->number(f.min);
    if (min == 0) continue;
    snap::Damage d;
    d.type = weapon && f.type == kPhysical && f.duration.empty() ? kBasePhysical : f.type;
    d.dot = !f.duration.empty();
    d.duration = d.dot ? static_cast<float>(r->number(f.duration)) : 0;
    const double chance = r->number(f.chance), mult = chance > 0 && chance < 100 ? chance / 100 : 1;
    d.min = static_cast<float>(min * mult);
    d.max = static_cast<float>(std::max(min, r->number(f.max)) * mult);
    x.offensive_damage.push_back(d);
  }
  for (const auto& [key, field] : r->fields)
    if (key.starts_with("retaliation") && key.ends_with("Min") && !field.numbers.empty() && field.numbers[0] != 0)
      if (const int t = db_damage_type(key.substr(11, key.size() - 14)); t != 0)
        x.retaliation.push_back({t, false, static_cast<float>(field.numbers[0]),
                                 static_cast<float>(std::max(field.numbers[0], r->number(key.substr(0, key.size() - 3) + "Max")))});
  for (const char* n : {"", "2"}) {
    const int from = db_damage_type(r->string(std::string("conversionInType") + n));
    const int to = db_damage_type(r->string(std::string("conversionOutType") + n));
    const double pct = r->number(std::string("conversionPercentage") + n);
    if (from && to && pct > 0) x.conversions.push_back({from, to, static_cast<float>(pct / 100)});  // a share
  }
  return it;
}

struct Learned {
  const char* record;
  std::uint32_t points;
  std::vector<std::pair<const char*, std::uint32_t>> modifiers = {};
};

struct Gear {
  int location;
  const char* record;
};

struct Family {
  const char* name;
  const char* catalog_id;
  int variant = 0;  // the catalog entry's n-th variant (0: the build itself)
  std::vector<std::pair<int, std::uint32_t>> masteries;  // mastery number, points
  std::vector<Learned> skills;
  std::vector<std::pair<const char*, std::uint32_t>> devotions;  // power record, level
  std::vector<Gear> gear;
};

inline const setup::Entry& catalog_entry(const char* id, int variant) {
  static const std::vector<setup::Entry> catalog = [] {
    std::string error;
    auto c = setup::load_catalog(read(fs::path(GDC_SOURCE_DIR) / "catalog/builds.toml"), error);
    REQUIRE_MESSAGE(c, error);
    return *c;
  }();
  const std::string want = variant ? std::string(id) + "/" + std::to_string(variant) : std::string(id);
  for (const auto& e : catalog)
    if (e.id == want || (variant == 0 && e.id == want + "/1")) return e;  // a build with variants: its first
  FAIL("no catalog build " << want);
  return catalog.front();
}

// The family's catalog preset (balanced playstyle) on its endgame character, level 100.
struct Endgame : Build {
  const Family& f;

  static score::Preset preset_of(const Family& f) {
    const setup::Entry& e = catalog_entry(f.catalog_id, f.variant);
    std::vector<int> m;
    for (const auto& [mastery, _] : f.masteries) m.push_back(mastery);
    return parse_preset(setup::preset_text(setup::from_entry(e, m, "synthetic")));
  }

  explicit Endgame(const Family& fam) : Build(preset_of(fam), kSword), f(fam) {
    using namespace combat_attr;
    s.level = 100;
    s.equipped.clear();
    // The harness sheet's +% damage is its own character's (+2263% Chaos): every type at one level instead, so the
    // family's gear and skills decide its types.
    for (int t : {kPhysical, kPierce, kCold, kFire, kPoison, kLightning, kLife, kChaos, kAether, kBleeding}) {
      auto& c = totals(s.damage_modifiers, t);
      c.damage_modifier = t == kBleeding ? 0 : 150;
      c.dot_modifier = t == kPierce ? 0 : 150;
    }
    totals(s.damage_modifiers, kAll).damage_modifier = 0;
    for (const auto& [mastery, points] : f.masteries) learn(setup::mastery_record(mastery).c_str(), points);
    for (const auto& k : f.skills) {
      snap::Skill& learned = learn(k.record, k.points);
      // Toggles and forms are kept on, as a capture of the character in combat reads them.
      if (const arz::Record* r = db->record(k.record);
          r && classes::is_a(r->cls, "Skill_BuffSelfToggled") && !classes::is_a(r->cls, "Skill_BuffSelfDuration"))
        toggle_on(learned);
      const std::uint32_t id = learned.id;
      std::vector<std::uint32_t> ids;
      for (const auto& [m, lv] : k.modifiers) ids.push_back(learn(m, lv).id);
      for (auto& x : s.skills)
        if (x.id == id) x.modifier_ids = ids;
    }
    for (const auto& [power, level] : f.devotions) learn(power, level).devotion_max_level = level;
    for (const auto& g : f.gear) wear(db_item(*db, g.location, g.record));
    // The slots ring() and chest() add stats on.
    for (const auto& [location, record] : {std::pair{equip::kFinger2, kRing}, std::pair{equip::kChest, kChest}})
      if (std::none_of(s.equipped.begin(), s.equipped.end(), [&](const snap::Item& it) { return it.location == location; }))
        wear(bare(location, record));
  }

  void wear(const snap::Item& it) { s = model::swap(s, it, it.location, db).after; }

  score::Result vs(const snap::Snapshot& after) const {
    const auto& settings = score::settings(preset, s);
    return score::compare(s, after, score::main_skills(s, settings, db), settings, db);
  }
  double swap(const snap::Item& it) const { return vs(model::swap(s, it, it.location, db).after).percent; }
  const snap::Item& at(int location) const {
    for (const auto& it : s.equipped)
      if (it.location == location) return it;
    FAIL("nothing worn at " << location);
    return s.equipped.front();
  }
  // The item worn at `location` plus the stats named (added to its own).
  double plus(int location, const std::function<void(snap::ItemStats&)>& stats) const {
    snap::Item it = at(location);
    it.id += 1000;
    snap::ItemStats more;
    stats(more);
    snap::ItemStats& x = *it.stats;
    auto add = [](auto& to, const auto& from) { to.insert(to.end(), from.begin(), from.end()); };
    auto merge = [](std::vector<snap::CombatTotals>& to, const std::vector<snap::CombatTotals>& from) {  // one row per type
      for (const auto& c : from) {
        auto& t = totals(to, c.type);
        t.defense += c.defense, t.defense_modifier += c.defense_modifier;
        t.damage_modifier += c.damage_modifier, t.dot_modifier += c.dot_modifier;
      }
    };
    add(x.char_attributes, more.char_attributes);
    merge(x.defense, more.defense);
    merge(x.offensive_modifiers, more.offensive_modifiers);
    merge(x.retaliation_modifiers, more.retaliation_modifiers);
    add(x.offensive_damage, more.offensive_damage);
    add(x.retaliation, more.retaliation);
    add(x.skill_attributes, more.skill_attributes);
    add(x.conversions, more.conversions);
    return swap(it);
  }
  // The item worn at `location` replaced by a record without grants or set (its fixed stats only).
  double replaced(int location, const char* record) const { return swap(db_item(*db, location, record)); }
  double ring(const std::function<void(snap::ItemStats&)>& stats) const { return plus(equip::kFinger2, stats); }
  double chest(const std::function<void(snap::ItemStats&)>& stats) const { return plus(equip::kChest, stats); }  // armor counts there
};

}  // namespace gdc::test
