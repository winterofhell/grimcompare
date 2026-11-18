#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "game_db.h"
#include "gdc/calibrate.h"
#include "gdc/stat_model.h"

using namespace gdc;
namespace fs = std::filesystem;

namespace {

snap::Snapshot fixture(const char* name) {
  std::ifstream f(std::string(GDC_SOURCE_DIR) + "/tests/fixtures/snapshots/" + name);
  REQUIRE(f.good());
  std::stringstream ss;
  ss << f.rdbuf();
  return snap::from_json(ss.str());
}

// The installed game database, or null (tests then expect the "no database" warning).
const snap::Item& at(const snap::Snapshot& s, int location) {
  for (const auto& it : s.equipped)
    if (it.location == location) return it;
  FAIL("no item at location " << location);
  return s.equipped.front();
}

// Calibration tolerance (+-1 / +-0.1%): the fits below are exact to float rounding.
bool near(double a, double b) { return std::abs(a - b) <= std::max(0.01, 0.001 * std::abs(b)); }

const model::Resist* resist(const model::Sheet& s, int type) {
  for (const auto& r : s.resists)
    if (r.type == type) return &r;
  return nullptr;
}

bool has_warning(const model::Prediction& p, const std::string& text) {
  return std::any_of(p.warnings.begin(), p.warnings.end(), [&](const std::string& w) { return w.find(text) != std::string::npos; });
}

void check_core_sheet(const model::Sheet& got, const model::Sheet& want) {
  CHECK(near(got.physique, want.physique));
  CHECK(near(got.cunning, want.cunning));
  CHECK(near(got.spirit, want.spirit));
  CHECK(near(got.life, want.life));
  CHECK(near(got.energy, want.energy));
  CHECK(near(got.life_regen, want.life_regen));
  CHECK(near(got.energy_regen, want.energy_regen));
  CHECK(near(got.oa, want.oa));
  CHECK(near(got.da, want.da));
  CHECK(near(got.run_speed, want.run_speed));
}

}  // namespace

TEST_CASE("sheet armor from region chances matches the level-96 character sheet") {
  // Sheet 2026-11-14, same session as the fixture: Armor 1408.
  const auto s = fixture("lvl96-before-ring-swap.json");
  CHECK(std::round(model::armor(s)) == 1408);
}

TEST_CASE("swap model: synthetic attribute, resist, absorption and set bookkeeping") {
  snap::Snapshot s;
  s.level = 50;
  s.defense_cap = 80;
  s.char_attributes = {{3, 110, 10, 50, 0}, {4, 1250, 25, 300, 0}, {5, 300, 0, 250, 0}};
  s.defense = {{6, 70, 0, 0, 0, 0, 70, 0, true}, {38, 200, 0, 0, 0, 0, 200, 0}, {39, 70, 20, 0, 0, 0, 84, 0}};
  snap::Item old;
  old.location = 4;
  old.record = "records/a.dbr";
  old.set = "records/sets/s.dbr";
  old.stats = snap::ItemStats{};
  old.stats->defense = {{38, 100, 0, 0, 0, 0}, {39, 70, 20, 0, 0, 0}};
  snap::Item other = old;
  other.location = 5;
  other.record = "records/a2.dbr";  // a second copy of records/a.dbr would not count (sets::counts)
  other.stats->defense = {{38, 100, 0, 0, 0, 0}, {39, 70, 0, 0, 0, 0}};
  s.equipped = {old, other};
  snap::Item cand;
  cand.record = "records/b.dbr";
  cand.stats = snap::ItemStats{};
  cand.stats->char_attributes = {{3, 10, 0}};
  cand.stats->defense = {{6, 20, 0, 0, 0, 0}};

  const auto p = model::swap(s, cand, 4, nullptr);
  const auto sh = model::sheet(p.after);
  CHECK(near(sh.spirit, 121));  // (100 + 10) x 1.1
  CHECK(near(sh.life, (1000 + 1.5 * 11) * 1.25));
  CHECK(near(sh.energy, 300 + 2 * 11));
  REQUIRE(resist(sh, 6));
  CHECK(resist(sh, 6)->raw == 90);
  CHECK(resist(sh, 6)->shown == 80);
  const auto ab = std::find_if(p.after.defense.begin(), p.after.defense.end(), [](auto& r) { return r.type == 39; });
  REQUIRE(ab != p.after.defense.end());
  CHECK(ab->defense == 70);           // every armor entry carries the engine's 70; one piece still has armor
  CHECK(ab->defense_modifier == 0);   // the removed piece's +20%
  CHECK(ab->processed == 70);
  REQUIRE(p.sets.size() == 1);
  CHECK(p.sets[0].before == 2);
  CHECK(p.sets[0].after == 1);
  CHECK(has_warning(p, "set bonuses change (set record not in the database)"));
  CHECK(!p.sets[0].modeled);
  CHECK(has_warning(p, "no database"));
  CHECK(at(p.after, 4).record == "records/b.dbr");

  // No armor left: absorption 0.
  const auto q = model::swap(p.after, cand, 5, nullptr);
  const auto ab2 = std::find_if(q.after.defense.begin(), q.after.defense.end(), [](auto& r) { return r.type == 39; });
  REQUIRE(ab2 != q.after.defense.end());
  CHECK(ab2->defense == 0);
}

TEST_CASE("swap model: captured attack / cast speed move with the speed % (procs on attack, speed cap)") {
  snap::Snapshot s;
  s.attack_speed = 2.0f;
  s.cast_speed = 1.5f;
  s.char_attributes = {{13, 0, 10, 0, 0}, {15, 0, 40, 0, 0}, {16, 0, 20, 0, 0}};  // TotalSpeed, AttackSpeed, CastSpeed %
  snap::Item old;
  old.location = 4;
  old.record = "records/a.dbr";
  old.stats = snap::ItemStats{};
  old.stats->char_attributes = {{15, 0, 30}};
  s.equipped = {old};
  snap::Item cand;
  cand.record = "records/b.dbr";
  cand.stats = snap::ItemStats{};
  cand.stats->char_attributes = {{13, 0, 5}};
  const auto p = model::swap(s, cand, 4, nullptr);
  CHECK(p.after.attack_speed == doctest::Approx(2.0 * 1.25 / 1.5));  // 40 + 10 -> 10 + 15
  CHECK(p.after.cast_speed == doctest::Approx(1.5 * 1.35 / 1.3));    // 20 + 10 -> 20 + 15
  const auto back = model::swap(p.after, old, 4, nullptr);
  CHECK(back.after.attack_speed == doctest::Approx(2.0));
  CHECK(back.after.cast_speed == doctest::Approx(1.5));
}

TEST_CASE("swap model: the first absorption modifier gets the engine's per-entry absorption") {
  // Low-level snapshot: armor but no type-39 row (winterpoison store). An item adding +10% absorption.
  snap::Snapshot s;
  s.level = 12;
  s.defense_cap = 80;
  s.defense = {{6, 10, 0, 0, 0, 0, 10, 0, true}, {38, 300, 0, 0, 0, 0, 300, 0}};
  snap::Item cand;
  cand.record = "records/b.dbr";
  cand.stats = snap::ItemStats{};
  cand.stats->defense = {{39, 0, 10, 0, 0, 0}};
  const auto p = model::swap(s, cand, 2, nullptr);
  CHECK(has_warning(p, "absorption base unknown"));
  if (const arz::Database* db = game_db()) {
    const auto q = model::swap(s, cand, 2, db);
    const auto ab = std::find_if(q.after.defense.begin(), q.after.defense.end(), [](auto& r) { return r.type == 39; });
    REQUIRE(ab != q.after.defense.end());
    CHECK(ab->defense == 70);  // gameengine.dbr armorDefensiveAbsorption
    CHECK(near(model::sheet(q.after).absorption, 77));
  }
}

TEST_CASE("swap model: ring swap fixture (c031_ring -> b201e_ring, finger 2)") {
  const auto before = fixture("lvl96-before-ring-swap.json");
  const auto after = fixture("lvl96-after-ring-swap.json");
  const auto p = model::swap(before, at(after, 5), 5, game_db());
  const auto got = model::sheet(p.after), want = model::sheet(after);
  check_core_sheet(got, want);
  CHECK(near(got.armor, want.armor));
  // c031_ring's +2 Possession (toggle, on): 21 -> 19, defensiveChaos 34 -> 32. Needs the database.
  for (const auto& r : want.resists) {
    const auto* g = resist(got, r.type);
    REQUIRE(g);
    if (r.type == 10 && !game_db()) CHECK(g->raw - r.raw == 2);
    else CHECK(g->raw == r.raw);
  }
  if (game_db()) {
    CHECK(has_warning(p, "assumed on"));  // schema-3 fixture: no per-skill activity
    const auto it = std::find_if(p.skills.begin(), p.skills.end(),
                                 [](const auto& k) { return k.record.ends_with("/possession1.dbr"); });
    REQUIRE(it != p.skills.end());
    CHECK(it->before == 21);
    CHECK(it->after == 19);
  } else {
    CHECK(has_warning(p, "no database"));
  }
}

TEST_CASE("swap model: poison swap fixture (ring 5 + chest 8), Poison 25 -> 91") {
  const auto before = fixture("lvl96-phase21-axe-hover.json");
  const auto after = fixture("lvl96-swap-poison-ring-chest.json");
  const auto p1 = model::swap(before, at(after, 5), 5, game_db());
  const auto p2 = model::swap(p1.after, at(after, 8), 8, game_db());
  const auto got = model::sheet(p2.after), want = model::sheet(after);
  for (const auto* p : {&p1, &p2})
    for (const auto& w : p->warnings)
      CHECK_MESSAGE((w.find("assumed on") != std::string::npos || (!game_db() && w.starts_with("no database"))), w);
  check_core_sheet(got, want);
  for (const auto& r : want.resists) {
    const auto* g = resist(got, r.type);
    REQUIRE(g);
    if (r.type == 10 && !game_db()) CHECK(g->raw - r.raw == 2);  // Possession, see the ring test
    else CHECK(g->raw == r.raw);
    CHECK(g->shown == r.shown);
  }
  REQUIRE(resist(got, 7));
  CHECK(resist(got, 7)->raw == 105);
  CHECK(resist(got, 7)->shown == 91);
  CHECK(resist(got, 7)->uncapped - resist(got, 7)->max == 14);  // sheet "+14% over maximum"
  if (game_db()) {
    // b002e_torso's +3 wpattack0 (Skill_Passive, learned 1): 5 -> 2, defensiveProtectionModifier 9 -> 5.
    // With a11_torso004's +8%: Protection 9 - 4 + 8 = 13, armor exact (sheet 1386). Its +3 Blood of Dreeg
    // (a timed buff, not running) changes nothing: OA matches above.
    CHECK(near(got.armor, want.armor));
    CHECK(std::round(got.armor) == 1386);
  } else {
    CHECK(has_warning(p1, "no database"));
  }
}

TEST_CASE("swap model: skill grants use the engine's activity when captured (schema 4)") {
  if (!game_db()) return;
  auto before = fixture("lvl96-before-ring-swap.json");
  const auto after = fixture("lvl96-after-ring-swap.json");
  auto& pos = *std::find_if(before.skills.begin(), before.skills.end(),
                            [](const auto& k) { return k.record.ends_with("/possession1.dbr"); });
  for (auto& k : before.skills) k.passive = snap::ItemStats{};
  // Possession at 21: defensiveChaos 34, defensiveDisruption 100 (the record's values).
  pos.passive->defense = {{10, 34, 0, 0, 0, 0}, {13, 100, 0, 0, 0, 0}};
  auto p = model::swap(before, at(after, 5), 5, game_db());
  CHECK(!has_warning(p, "assumed"));
  CHECK(resist(model::sheet(p.after), 10)->raw == resist(model::sheet(after), 10)->raw);

  pos.passive = snap::ItemStats{};  // toggled off: no change
  p = model::swap(before, at(after, 5), 5, game_db());
  CHECK(resist(model::sheet(p.after), 10)->raw - resist(model::sheet(after), 10)->raw == 2);

  pos.passive->defense = {{10, 30, 0, 0, 0, 0}};  // engine disagrees with the record: not applied, warned
  p = model::swap(before, at(after, 5), 5, game_db());
  CHECK(has_warning(p, "engine values differ"));
  CHECK(resist(model::sheet(p.after), 10)->raw - resist(model::sheet(after), 10)->raw == 2);
}

TEST_CASE("calibrate: schema-4 swaps from the auto-snapshot store pass") {
  // Store 20261114-123319-910 records 1->2 (finger 5: b201e_ring -> c031_ring) and 3->4 (chest:
  // a11_torso004 -> b002e_torso). The ring's +2 Possession needs the database; Possession's engine numbers
  // include the dual-wielded b013e swords' item skill modifiers (+8% attack speed each).
  const auto r1 = fixture("lvl96-s4-before-ring-c031.json"), r2 = fixture("lvl96-s4-after-ring-c031.json");
  const auto c1 = calibrate::check(r1, r2, game_db());
  CHECK(c1.skipped.empty());
  CHECK(c1.locations == std::vector<int>{5});
  if (game_db()) {
    for (const auto& d : c1.diffs) CHECK_MESSAGE(false, d.stat << " " << d.predicted << " vs " << d.actual);
    // c031's skill grant moves Blood of Dreeg's modifier; its parent link is captured from schema 5.
    REQUIRE(c1.warnings.size() == 1);
    CHECK(c1.warnings[0] == "skill bloodofdreeg2.dbr: modifier level change not modeled (its parent skill is captured from schema 5)");
  } else {
    // Without the records the ring's skill grant (+2 Possession) and its damage % through skills are missing.
    std::vector<std::string> stats;
    for (const auto& d : c1.diffs) stats.push_back(d.stat);
    CHECK(stats == std::vector<std::string>{"Resist Chaos (raw)", "Damage % Poison", "Damage % Poison (duration)",
                                            "Damage % Life", "Damage % Life (duration)", "Damage % Chaos"});
  }
  const auto c2 = calibrate::check(fixture("lvl96-s4-before-chest-b002e.json"), fixture("lvl96-s4-after-chest-b002e.json"),
                                   game_db());
  CHECK(c2.skipped.empty());
  if (game_db())
    for (const auto& d : c2.diffs) CHECK_MESSAGE(false, d.stat << " " << d.predicted << " vs " << d.actual);

  // Schema 5 (store 20261114-125844-357 records 5 -> 6, finger 5: b201e_ring -> c031_ring): the captured
  // bloodofdreeg1 -> bloodofdreeg2 link replaces the schema-4 "parent unknown" warning.
  const auto c3 = calibrate::check(fixture("lvl96-s5-before-ring-c031.json"), fixture("lvl96-s5-after-ring-c031.json"),
                                   game_db());
  CHECK(c3.skipped.empty());
  if (game_db()) {
    for (const auto& d : c3.diffs) CHECK_MESSAGE(false, d.stat << " " << d.predicted << " vs " << d.actual);
    for (const auto& w : c3.warnings) CHECK_MESSAGE(false, w);
  }

  // Not comparable: points spent in between.
  auto lvl = r2;
  lvl.level += 1;
  CHECK(calibrate::check(r1, lvl, game_db()).skipped == "level changed");
  CHECK(calibrate::check(r1, r1, game_db()).skipped == "no equipment change");
}

TEST_CASE("calibrate: a timed buff cast between two snapshots is told from a swap error") {
  // winterpoison store 20261114-174229-629 #21 -> #22 (feet a02_feet01 -> a04_feet01). Blood of Dreeg
  // (bloodofdreeg1, Skill_BuffRadius, level 3) was cast in between: its buff record gives OA 26 and life regen
  // 26 (x 1.24 regen modifier = 32.24), the necklace's item skill modifier necklace_b001_bloodofdreeg crit
  // damage +15% and Elemental -> Poison 45%, Aether -> Poison 100% while it runs. Nothing in the snapshot
  // shows the buff.
  const auto before = fixture("lvl15-wp-before-feet.json"), after = fixture("lvl15-wp-after-feet-dreeg-on.json");
  const auto c = calibrate::check(before, after, game_db());
  CHECK(c.skipped.empty());
  CHECK(calibrate::swapped(c, before, after) == "Feet(12) a02_feet01 -> a04_feet01");
  if (!game_db()) return;
  for (const auto& d : c.diffs) CHECK_MESSAGE(false, d.stat << " " << d.predicted << " vs " << d.actual);
  CHECK(c.buff == "records/skills/playerclass03/bloodofdreeg1.dbr switched on");

  // Reversed (feet back, the buff ran out): the same buff, switched off.
  const auto back = calibrate::check(after, before, game_db());
  CHECK(back.diffs.empty());
  CHECK(back.buff == "records/skills/playerclass03/bloodofdreeg1.dbr switched off");

  // A difference the buff does not explain stays a failure.
  auto off = after;
  for (auto& a : off.char_attributes)
    if (a.type == 21) a.total += 10;  // OA
  const auto bad = calibrate::check(before, off, game_db());
  CHECK(bad.buff.empty());
  CHECK_FALSE(bad.diffs.empty());
}

TEST_CASE("swap model: a learned modifier's level change goes through its parent") {
  if (!game_db()) return;
  // c031_ring moves bloodofdreeg2 (Skill_Modifier; defensivePoison 120@15 -> 105@12, defensivePhysical
  // 17 -> 14) under bloodofdreeg1 (Skill_BuffRadius, captured passive empty: the buff wasn't counted).
  auto before = fixture("lvl96-s4-before-ring-c031.json");
  const auto after = fixture("lvl96-s4-after-ring-c031.json");
  auto skill = [&](const char* tail) -> snap::Skill& {
    return *std::find_if(before.skills.begin(), before.skills.end(), [&](const auto& k) { return k.record.ends_with(tail); });
  };
  skill("/bloodofdreeg1.dbr").modifier_ids = {skill("/bloodofdreeg2.dbr").id};
  auto p = model::swap(before, at(after, 5), 5, game_db());
  for (const auto& w : p.warnings) CHECK_MESSAGE(false, w);  // parent off: nothing to apply
  const auto poison = [](const model::Sheet& sh) { return resist(sh, 7)->raw; };
  CHECK(poison(model::sheet(p.after)) == poison(model::sheet(after)));  // the ring's own stats only

  skill("/bloodofdreeg1.dbr").passive = snap::ItemStats{};
  skill("/bloodofdreeg1.dbr").passive->defense = {{7, 120, 0, 0, 0, 0}, {2, 17, 0, 0, 0, 0}};  // parent on
  p = model::swap(before, at(after, 5), 5, game_db());
  for (const auto& w : p.warnings) CHECK_MESSAGE(false, w);
  CHECK(poison(model::sheet(p.after)) - poison(model::sheet(after)) == -15);
  CHECK(resist(model::sheet(p.after), 2)->raw - resist(model::sheet(after), 2)->raw == -3);
  const auto& parent = *std::find_if(p.after.skills.begin(), p.after.skills.end(),
                                     [](const auto& k) { return k.record.ends_with("/bloodofdreeg1.dbr"); });
  CHECK(parent.passive->defense[0].defense == 105);
}

TEST_CASE("swap model: removing an item drops its item skill modifiers from an active skill") {
  if (!game_db()) return;
  // b013e_sword (R hand): +3 Possession, +2 Witchfire, modifiers sword1h_b013_possession(b) on Possession.
  auto s = fixture("lvl96-s4-after-ring-c031.json");
  // Schema 4 lacks Skill+0x140; Witchfire's engine passive (reserve 201, Life 28) is witchfire1@17 +
  // witchfire2@15 (its learned modifier), so link them as a schema-5 capture would.
  auto skill = [&](const char* tail) -> snap::Skill& {
    return *std::find_if(s.skills.begin(), s.skills.end(), [&](const auto& k) { return k.record.ends_with(tail); });
  };
  skill("/witchfire1.dbr").modifier_ids = {skill("/witchfire2.dbr").id};
  snap::Item cand = at(s, 0);  // same stats, a record that grants nothing
  cand.record = "records/test/no-such-item.dbr";
  cand.replica.reset();
  cand.relic.reset();
  cand.augment.reset();
  const auto p = model::swap(s, cand, 0, game_db());
  const auto pos = std::find_if(p.skills.begin(), p.skills.end(), [](const auto& k) { return k.record.ends_with("/possession1.dbr"); });
  REQUIRE(pos != p.skills.end());
  CHECK(pos->before == 21);
  CHECK(pos->after == 18);
  const auto& after_pos = *std::find_if(p.after.skills.begin(), p.after.skills.end(),
                                        [](const auto& k) { return k.record.ends_with("/possession1.dbr"); });
  REQUIRE(after_pos.passive);
  REQUIRE(after_pos.passive->char_attributes.size() == 1);
  CHECK(after_pos.passive->char_attributes[0].modifier == 8);  // one sword left
  CHECK(resist(model::sheet(p.after), 10)->raw - resist(model::sheet(s), 10)->raw == -3);  // defensiveChaos 34 -> 31
  CHECK(std::none_of(p.after.item_skill_modifiers.begin(), p.after.item_skill_modifiers.end(),
                     [](const auto& m) { return m.location == 0; }));
  for (const auto& w : p.warnings) CHECK_MESSAGE(false, w);
}

TEST_CASE("swap model: +1 Occultist (d016_relic) on a timed buff between casts, unused skills, the mastery bar") {
  const arz::Database* db = game_db();
  if (!db) return;
  snap::Snapshot s;
  s.level = 96;
  s.defense_cap = 80;
  s.char_attributes = {{21, 2000, 0, 0, 0}, {6, 50, 0, 0, 0}};
  s.defense = {{7, 50}};
  auto skill = [&](std::uint32_t id, const char* tail, std::uint32_t level, std::uint32_t aug) {
    snap::Skill k;
    k.id = id;
    k.record = std::string("records/skills/playerclass03/") + tail;
    k.level = level;
    k.augmented_level = aug;
    k.current_level = level ? std::min(level + aug, 26u) : 0;
    k.mastery_id = id == 1 ? 0 : 1;
    k.passive = snap::ItemStats{};  // all inactive: the buff is down, the modifier with it
    return k;
  };
  s.skills = {skill(1, "_classtraining_class03.dbr", 50, 0), skill(2, "bloodofdreeg1.dbr", 6, 5),
              skill(3, "bloodofdreeg2.dbr", 11, 0), skill(4, "evileye1.dbr", 0, 0)};
  s.skills[1].modifier_ids = {3};
  snap::Item relic;
  relic.id = 9;
  relic.record = "records/items/gearrelic/d016_relic.dbr";
  relic.stats = snap::ItemStats{};
  auto row = [](const std::vector<snap::CharAttribute>& v, int t) {
    for (const auto& a : v)
      if (a.type == t) return static_cast<double>(a.total);
    return 0.0;
  };
  auto def = [](const std::vector<snap::CombatTotals>& v, int t) {
    for (const auto& a : v)
      if (a.type == t) return static_cast<double>(a.defense);
    return 0.0;
  };

  // The buff is out of the totals (model::expected), so they do not move: the score adds it per buff state at
  // the new levels (bloodofdreeg1_buff.dbr 11 -> 12, bloodofdreeg2.dbr 11 -> 12).
  s.skills[1].buff_active = false;
  const auto p = model::swap(s, relic, 13, db);
  for (const auto& w : p.warnings) CHECK_MESSAGE(false, w);
  CHECK(row(p.after.char_attributes, 21) == 2000);
  CHECK(def(p.after.defense, 7) == 50);
  REQUIRE(p.inactive.size() == 2);
  CHECK(p.inactive[0].timed);
  CHECK(p.inactive[1].timed);
  // Switched on at the new levels: OA 102, life regen 104 (rank 12), defensivePoison 100 (modifier rank 12).
  const snap::Skill* dreeg = &p.after.skills[1];
  const model::BuffEffect effect = model::buff_effect(p.after, *dreeg, *db);
  const std::pair<const model::BuffEffect*, double> up[] = {{&effect, 1.0}};
  snap::Snapshot on = p.after;
  model::buffed(p.after, up, *db, on);
  CHECK(row(on.char_attributes, 21) == doctest::Approx(2102));
  CHECK(row(on.char_attributes, 6) == doctest::Approx(154));
  CHECK(def(on.defense, 7) == doctest::Approx(150));
  REQUIRE(model::buff_timing(*dreeg, *db));
  CHECK(model::buff_timing(*dreeg, *db)->duration == 60);
  CHECK(model::buff_timing(*dreeg, *db)->recharge == 12);
  // The mastery bar takes no item bonus; Dreeg's Evil Eye has no points: unused, 0 -> 1 bonus level.
  CHECK(std::none_of(p.skills.begin(), p.skills.end(), [](const auto& k) { return k.record.ends_with("_classtraining_class03.dbr"); }));
  REQUIRE(p.unused.size() == 1);
  CHECK(p.unused[0].record.ends_with("evileye1.dbr"));
  CHECK(p.unused[0].after == 1);
}
