// A swap and its reverse must cancel: hovering B while A is worn shows x, hovering A while B is worn shows y,
// and (1 + x)(1 + y) must be 1 (within 0.5%). Checked through tooltip::compare — what the block shows — for
// every slot of every fixture against every item another fixture wears there, on the two real gear states of
// one character (engine totals on both sides), and on synthetic items: rings on both fingers, shield and
// off-hand, weapons, the two-handed pair, items that cannot be worn yet or that unequip another item.
#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#include "game_db.h"
#include "gdc/components.h"
#include "gdc/score.h"
#include "gdc/stat_model.h"
#include "gdc/tooltip.h"

using namespace gdc;
namespace fs = std::filesystem;

namespace {

constexpr double kTolerance = 0.005;

std::string read(const fs::path& p) {
  std::ifstream f(p);
  REQUIRE(f.good());
  std::stringstream ss;
  ss << f.rdbuf();
  return ss.str();
}

struct Fixture {
  std::string name;
  snap::Snapshot s;
};

const std::vector<Fixture>& fixtures() {
  static const std::vector<Fixture> all = [] {
    std::vector<Fixture> v;
    for (const auto& e : fs::directory_iterator(fs::path(GDC_SOURCE_DIR) / "tests/fixtures/snapshots"))
      if (e.path().extension() == ".json") v.push_back({e.path().filename().string(), snap::from_json(read(e.path()))});
    std::sort(v.begin(), v.end(), [](const Fixture& a, const Fixture& b) { return a.name < b.name; });
    return v;
  }();
  return all;
}

// plague-garden for its character's fixtures (names with "-wp"), the defaults for the others.
const score::Preset& preset_for(const std::string& fixture) {
  static const score::Preset garden = [] {
    std::string error;
    auto p = score::parse(read(fs::path(GDC_SOURCE_DIR) / "presets/plague-garden.toml"), error);
    REQUIRE_MESSAGE(p, error);
    return *p;
  }();
  static const score::Preset defaults;
  return fixture.find("-wp") != std::string::npos ? garden : defaults;
}

const snap::Item* at(const snap::Snapshot& s, int location) {
  for (const auto& e : s.equipped)
    if (e.location == location) return &e;
  return nullptr;
}

// Item::GetItemType of what a location holds (tooltip::locations_for backwards; hands by record class).
int item_type(const snap::Item& it, int location, const arz::Database& db) {
  switch (location) {
    case 0: return 9;
    case 1: {
      const arz::Record* r = db.record(it.record);
      return r && r->cls.starts_with("WeaponArmor") ? 8 : 9;
    }
    case 2: return 2;
    case 3: return 15;
    case 4: case 5: return 6;
    case 6: return 13;
    case 7: return 1;
    case 8: return 3;
    case 9: return 14;
    case 10: return 7;
    case 11: return 4;
    case 12: return 5;
    case 13: return 11;
    default: return 0;
  }
}

snap::Item loose(snap::Item it, std::uint32_t id) {
  it.id = id;
  it.location = -1;
  return it;
}

// The score the block shows for `candidate` going to `location`; the item's own score when the swap unequips
// something else (that part is not reversible). nullopt: no such comparison.
std::optional<double> shown(const snap::Snapshot& s, const snap::Item& candidate, int type, int location,
                            const score::Preset& preset, const arz::Database* db) {
  std::vector<tooltip::Scored> scored;
  tooltip::compare(s, candidate, type, false, db, preset, {}, nullptr, &scored);
  for (const auto& x : scored)
    if (x.location == location) return x.itself ? *x.itself : x.percent;
  return std::nullopt;
}

// `candidate` over what `s` wears at `location`, then that item back over the candidate.
struct RoundTrip {
  double x = 0, y = 0;
  double off() const { return std::abs((1 + x / 100) * (1 + y / 100) - 1); }
};
std::optional<RoundTrip> round_trip(const snap::Snapshot& s, const snap::Item& candidate, int type, int location,
                                    const score::Preset& preset, const arz::Database* db) {
  const snap::Item* old = at(s, location);
  if (!old) return std::nullopt;
  const auto x = shown(s, candidate, type, location, preset, db);
  if (!x) return std::nullopt;
  const snap::Snapshot b = model::swap(s, candidate, location, db).after;
  const auto y = shown(b, loose(*old, 990000), db ? item_type(*old, location, *db) : type, location, preset, db);
  if (!y) return std::nullopt;
  return RoundTrip{*x, *y};
}

snap::CombatTotals pct(int type, float direct, float dot) {
  snap::CombatTotals r;
  r.type = type;
  r.damage_modifier = direct;
  r.dot_modifier = dot;
  return r;
}

snap::CombatTotals def(int type, float value) {
  snap::CombatTotals r;
  r.type = type;
  r.defense = value;
  return r;
}

snap::Item made(const char* record, std::uint32_t id) {
  snap::Item it;
  it.id = id;
  it.record = record;
  it.kind = "ItemEquipment";
  it.stats.emplace();
  return it;
}

}  // namespace

TEST_CASE("symmetry: every fixture slot against every item another fixture wears there (installed game; skips if absent)") {
  const arz::Database* db = game_db();
  if (!db) {
    MESSAGE("game not found; skipped");
    return;
  }
  int pairs = 0;
  double worst = 0;
  std::string worst_at;
  for (const auto& f : fixtures()) {
    // Before schema 4 a snapshot does not list the item skill modifiers in effect: the model cannot take one
    // off with its item, only add it when the item comes back. A capture gap of those fixtures, not the score's.
    if (f.s.schema < 4) continue;
    for (const auto& old : f.s.equipped) {
      if (!old.stats) continue;
      const int type = item_type(old, old.location, *db);
      for (const auto& g : fixtures())
        for (const auto& cand : g.s.equipped) {
          if (!cand.stats || cand.record == old.record) continue;
          if (item_type(cand, cand.location, *db) != type) continue;
          if (std::any_of(f.s.equipped.begin(), f.s.equipped.end(), [&](const snap::Item& e) { return e.id == cand.id; })) continue;
          const auto r = round_trip(f.s, loose(cand, 980000), type, old.location, preset_for(f.name), db);
          if (!r) continue;
          ++pairs;
          if (r->off() > worst) {
            worst = r->off();
            worst_at = f.name + " loc " + std::to_string(old.location) + ": " + old.record + " <-> " + cand.record +
                       " x " + std::to_string(r->x) + " y " + std::to_string(r->y);
          }
        }
    }
  }
  MESSAGE(pairs << " pairs, worst |(1+x)(1+y)-1| = " << worst << " (" << worst_at << ")");
  CHECK(pairs > 500);
  CHECK_MESSAGE(worst < kTolerance, worst_at);
}

TEST_CASE("symmetry: the two real gear states of one character, engine totals on both sides (installed game; skips if absent)") {
  const arz::Database* db = game_db();
  if (!db) return;
  // winterpoison level 16, store 20261114-230425-127 records 2 / 3: Fleshbound Tome in L Hand, then the Heater
  // Shield. In game (0.9.1) each direction showed a gain: +6.3% and +8.7%.
  const auto tome = snap::from_json(read(fs::path(GDC_SOURCE_DIR) / "tests/fixtures/snapshots/lvl16-wp-lhand-tome.json"));
  const auto shield = snap::from_json(read(fs::path(GDC_SOURCE_DIR) / "tests/fixtures/snapshots/lvl16-wp-lhand-shield.json"));
  const auto& p = preset_for("-wp");
  const auto x = shown(tome, loose(*at(shield, 1), 970001), 8, 1, p, db);  // tome worn, shield hovered
  const auto y = shown(shield, loose(*at(tome, 1), 970002), 8, 1, p, db);  // shield worn, tome hovered
  REQUIRE(x);
  REQUIRE(y);
  MESSAGE("shield over tome " << *x << "%, tome over shield " << *y << "%");
  CHECK(std::abs((1 + *x / 100) * (1 + *y / 100) - 1) < kTolerance);
  CHECK(*x * *y <= 0);  // never a gain both ways
}

TEST_CASE("symmetry: synthetic rings, off-hands and weapons, items that cannot be worn yet or unequip another (installed game; skips if absent)") {
  const arz::Database* db = game_db();
  if (!db) return;
  const auto base = snap::from_json(read(fs::path(GDC_SOURCE_DIR) / "tests/fixtures/snapshots/lvl16-wp-lhand-shield.json"));
  const auto& p = preset_for("-wp");
  auto check = [&](const snap::Snapshot& s, const snap::Item& cand, int type, int location, const char* what) {
    const auto r = round_trip(s, cand, type, location, p, db);
    REQUIRE_MESSAGE(r, what);
    INFO(what << ": x " << r->x << " y " << r->y);
    CHECK(r->off() < kTolerance);
    return *r;
  };

  // Rings on either finger: damage %, resists, attributes, life; skills moving in opposite directions.
  snap::Item ring = made("records/items/gearaccessories/rings/a002_ring02.dbr", 960001);
  ring.stats->offensive_modifiers = {pct(7, 40, 60)};
  ring.stats->defense = {def(6, 20), def(4, 15)};
  ring.stats->char_attributes = {{3, 30, 0}, {4, 150, 5}};
  for (int finger : {4, 5}) check(base, ring, 6, finger, "ring");

  // Shield -> caster off-hand and back (block and the shield's stats against a tome's).
  snap::Item tome = made("records/items/gearweapons/focus/a02_focus01.dbr", 960002);
  tome.stats->offensive_modifiers = {pct(7, 22, 35)};
  tome.stats->char_attributes = {{21, 8, 0}};
  const RoundTrip off = check(base, tome, 8, 1, "off-hand over shield");
  CHECK(off.x != 0);

  // A one-handed weapon in R Hand: affixes count, base damage does not.
  snap::Item sword = made("records/items/gearweapons/swords1h/a02_sword001.dbr", 960003);
  REQUIRE(db->record(sword.record));
  sword.stats->offensive_modifiers = {pct(7, 30, 30)};
  snap::Damage basedmg;
  basedmg.type = 1;  // BasePhysical
  basedmg.min = 400, basedmg.max = 600;
  sword.stats->offensive_damage = {basedmg};
  const RoundTrip wpn = check(base, sword, 9, 0, "weapon");
  CHECK(wpn.x != 0);
  // The same weapon with ten times the base damage is no change: base damage is not compared.
  const snap::Snapshot armed = model::swap(base, sword, 0, db).after;
  snap::Item heavy = sword;
  heavy.id = 960008;
  heavy.stats->offensive_damage[0].min = 4000, heavy.stats->offensive_damage[0].max = 6000;
  CHECK(shown(armed, heavy, 9, 0, p, db).value() == doctest::Approx(0).epsilon(1e-9));

  // Two-handed over sword + shield (the pair), then back one hand at a time: the three steps cancel. On items
  // without components (line 1 leaves the compared items' components out, which a chain of three would count
  // unevenly) and with the same base damage on both weapons (the base both sides share is the mean of the two
  // compared weapons', so a chain through different bases can drift; a swap and its reverse cannot).
  std::vector<std::string> notes;
  snap::Snapshot plain = base;
  for (int hand : {0, 1})
    plain = model::swap(plain, components::strip(*at(plain, hand), db, notes), hand, db).after;
  snap::Item gun = made("records/items/gearweapons/guns2h/a02_gun2h001.dbr", 960004);
  gun.stats->offensive_modifiers = {pct(7, 50, 50)};
  for (const auto& d : at(plain, 0)->stats->offensive_damage)
    if (d.type == 1) gun.stats->offensive_damage.push_back(d);
  std::vector<tooltip::Scored> scored;
  tooltip::compare(plain, gun, 9, false, db, p, {}, nullptr, &scored);
  REQUIRE(scored.size() == 1);
  CHECK(scored[0].also_off == 1);
  snap::Snapshot two = model::swap(plain, gun, 0, db).after;
  {
    snap::Item none;
    none.stats.emplace();
    two = model::swap(two, none, 1, db).after;
    std::erase_if(two.equipped, [](const snap::Item& e) { return e.location == 1; });
  }
  const auto back1 = shown(two, loose(*at(plain, 0), 960005), 9, 0, p, db);
  REQUIRE(back1);
  const snap::Snapshot one = model::swap(two, loose(*at(plain, 0), 960005), 0, db).after;
  const auto back2 = shown(one, loose(*at(plain, 1), 960006), 8, 1, p, db);
  REQUIRE(back2);
  INFO("pair " << scored[0].percent << " back " << *back1 << " " << *back2);
  CHECK(scored[0].percent != 0);
  CHECK(std::abs((1 + scored[0].percent / 100) * (1 + *back1 / 100) * (1 + *back2 / 100) - 1) < kTolerance);

  // Not wearable yet (level): scored as if worn, so it still cancels.
  snap::Item later = ring;
  later.id = 960007;
  later.requirements.emplace().level = base.level + 2;
  later.requirements->met = false;
  check(base, later, 6, 4, "requirements not met");

  // A ring without the Physique the old one gave: the legs' requirement breaks. The item's own score cancels
  // with the way back; the total (legs taken off) is lower than that.
  snap::Snapshot tight = base;
  for (auto& e : tight.equipped)
    if (e.location == 4) e.stats->char_attributes.push_back({1, 40, 0});
  tight = model::swap(base, *at(tight, 4), 4, db).after;  // +40 Physique on finger 1
  double physique = 0;
  for (const auto& a : tight.char_attributes)
    if (a.type == 1) physique = a.total;
  for (auto& e : tight.equipped)
    if (e.location == 11) {
      e.requirements.emplace().attribute[0] = static_cast<std::uint32_t>(physique) - 10;
      e.requirements->met = true;
    }
  scored.clear();
  tooltip::compare(tight, ring, 6, false, db, p, {}, nullptr, &scored);
  REQUIRE(scored.size() == 2);
  REQUIRE(scored[0].itself);
  CHECK(scored[0].percent < *scored[0].itself);
  check(tight, ring, 6, 4, "requirement breakage");
}
