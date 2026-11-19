#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#include "game_db.h"
#include "gdc/calibrate.h"

using namespace gdc;
namespace fs = std::filesystem;

namespace {

snap::Snapshot fixture(const std::string& name) {
  std::ifstream f(std::string(GDC_SOURCE_DIR) + "/tests/fixtures/snapshots/" + name);
  std::stringstream ss;
  ss << f.rdbuf();
  return snap::from_json(ss.str());
}

// Empty engine entries for every learned skill: damage() then reports the model's side alone.
void blank_engine(snap::Snapshot& s) {
  s.skill_dps.clear();
  for (const auto& k : s.skills)
    if (k.current_level > 0) s.skill_dps.push_back({k.id, k.record, false, 0, {}, {}});
}

snap::SkillDps& engine_of(snap::Snapshot& s, const std::string& record) {
  for (auto& d : s.skill_dps)
    if (d.record == record) return d;
  FAIL("no engine entry for " << record);
  return s.skill_dps.front();
}

// Engine entries equal to the model's: a hit as a min / max pair around it, a DoT as per second over 4 s.
void engine_from_model(snap::Snapshot& s, const std::vector<calibrate::SkillDamageCheck>& model) {
  for (const auto& c : model) {
    if (!c.skipped.empty()) continue;
    auto& d = engine_of(s, c.record);
    for (const auto& t : c.types) {
      const float v = static_cast<float>(t.model);
      if (t.key.second) d.main_hand.push_back({t.key.first, true, v / 4, v / 4, 4});
      else d.main_hand.push_back({t.key.first, false, v * 0.9f, v * 1.1f});
    }
  }
}

}  // namespace

TEST_CASE("engine damage check: tolerance is 2% and 1 damage") {
  CHECK(!calibrate::damage_off({{6, false}, 100, 101.9}));
  CHECK(calibrate::damage_off({{6, false}, 100, 103}));
  CHECK(!calibrate::damage_off({{6, false}, 10, 10.9}));  // under 1 damage
  CHECK(calibrate::damage_off({{6, false}, 0, 5}));
}

TEST_CASE("engine damage check: the model's own numbers pass, a changed type fails") {
  const arz::Database* db = game_db();
  if (!db) return;
  snap::Snapshot s = fixture("lvl96-s4-before-chest-b002e.json");
  blank_engine(s);
  const auto model = calibrate::damage(s, *db);
  int compared = 0;
  for (const auto& c : model)
    if (c.skipped.empty()) {
      ++compared;
      CHECK_FALSE(c.pass());  // no engine entries
    }
  REQUIRE(compared > 0);
  engine_from_model(s, model);
  const auto same = calibrate::damage(s, *db);
  for (const auto& c : same) {
    INFO(c.record);
    if (!c.skipped.empty()) continue;
    CHECK(c.pass());
    for (const auto& t : c.types)
      if (t.key.second) CHECK(t.engine_seconds == 4);
  }
  // 10% more of one type: that type is off, the others are not.
  const auto& first = *std::find_if(model.begin(), model.end(), [](const auto& c) { return c.skipped.empty(); });
  auto& d = engine_of(s, first.record);
  REQUIRE(!d.main_hand.empty());
  auto& big = *std::max_element(d.main_hand.begin(), d.main_hand.end(),
                                [](const auto& a, const auto& b) { return a.max * (a.dot ? a.duration : 1) < b.max * (b.dot ? b.duration : 1); });
  big.min *= 1.1f;
  big.max *= 1.1f;
  const auto off = calibrate::damage(s, *db, {first.record});
  REQUIRE(off.size() == 1);
  CHECK_FALSE(off[0].pass());
  int n_off = 0;
  for (const auto& t : off[0].types) n_off += calibrate::damage_off(t);
  CHECK(n_off == 1);
  const auto lines = calibrate::damage_report(off, "x");
  REQUIRE(lines.size() > 1);
  CHECK(lines[0].starts_with("FAIL DMG x "));
}

TEST_CASE("engine damage check: Elemental in thirds, default attack and unknown DoT skipped") {
  const arz::Database* db = game_db();
  if (!db) return;
  snap::Snapshot s = fixture("lvl96-s4-before-chest-b002e.json");
  blank_engine(s);
  const auto model = calibrate::damage(s, *db);
  const auto& first = *std::find_if(model.begin(), model.end(), [](const auto& c) { return c.skipped.empty(); });
  snap::Snapshot e = s;
  engine_of(e, first.record).main_hand = {{58, false, 30, 30}};
  const auto split = calibrate::damage(e, *db, {first.record});
  REQUIRE(split.size() == 1);
  for (int type : {5, 6, 8}) {
    const auto t = std::find_if(split[0].types.begin(), split[0].types.end(),
                                [&](const calibrate::TypeDiff& d) { return d.key == score::DamageKey{type, false}; });
    REQUIRE(t != split[0].types.end());
    CHECK(t->engine == doctest::Approx(10));
  }
  engine_of(e, first.record).main_hand = {{7, true, 10, 10, 0}};
  CHECK(calibrate::damage(e, *db, {first.record})[0].skipped == "an engine DoT of unknown layout (no duration)");
  engine_of(e, first.record).default_attack = true;
  CHECK(calibrate::damage(e, *db, {first.record})[0].skipped.starts_with("not usable"));
}

TEST_CASE("engine damage check: the model side runs on every fixture") {
  const arz::Database* db = game_db();
  if (!db) return;
  for (const auto& f : fs::directory_iterator(std::string(GDC_SOURCE_DIR) + "/tests/fixtures/snapshots")) {
    snap::Snapshot s = fixture(f.path().filename().string());
    blank_engine(s);
    INFO(f.path().filename().string());
    for (const auto& c : calibrate::damage(s, *db))
      for (const auto& t : c.types) CHECK(std::isfinite(t.model));
  }
}
