#include <doctest/doctest.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#include "game_db.h"
#include "gdc/calibrate.h"
#include "gdc/requirements.h"
#include "gdc/score.h"
#include "gdc/tooltip.h"

using namespace gdc;
namespace fs = std::filesystem;

namespace {

snap::Snapshot load(const char* name) {
  std::ifstream f(fs::path(GDC_SOURCE_DIR) / "tests/fixtures/snapshots" / name);
  REQUIRE(f.good());
  std::stringstream ss;
  ss << f.rdbuf();
  return snap::from_json(ss.str());
}

score::Preset preset() {
  std::ifstream f(fs::path(GDC_SOURCE_DIR) / "presets/plague-garden.toml");
  REQUIRE(f.good());
  std::stringstream ss;
  ss << f.rdbuf();
  std::string error;
  auto p = score::parse(ss.str(), error);
  REQUIRE_MESSAGE(p, error);
  return *p;
}

snap::Item* at(snap::Snapshot& s, int location) {
  for (auto& e : s.equipped)
    if (e.location == location) return &e;
  return nullptr;
}

bool has(const std::vector<tooltip::Line>& lines, const std::string& text) {
  return std::any_of(lines.begin(), lines.end(), [&](const tooltip::Line& l) { return l.text.find(text) != std::string::npos; });
}

double percent(const std::vector<std::string>& log, const std::string& prefix) {
  for (const auto& l : log)
    if (l.starts_with(prefix)) return std::stod(l.substr(prefix.size()));
  return 1e9;
}

// The engine's flag as the adapter stores it: with the character state the call ran against.
void engine_says(snap::Requirements& q, const snap::Snapshot& s, bool met) {
  q.met = met;
  auto& at = q.met_at.emplace();
  at.level = s.level;
  at.difficulty = s.difficulty;
  for (int a = 0; a < 3; ++a)
    for (const auto& c : s.char_attributes)
      if (c.type == a + 1) at.attribute[a] = c.total;
}

}  // namespace

TEST_CASE("requirements: effective value as ItemEquipment::AreRequirementsMet rounds it") {
  CHECK(requirements::effective(262, 0) == 262);
  CHECK(requirements::effective(100, 10) == 90);
  CHECK(requirements::effective(101, 15) == 86);  // 101 - 15.15 + 0.5 = 86.35, truncated
  CHECK(requirements::effective(101, 10) == 91);  // 90.9 + 0.5 = 91.4
  CHECK(requirements::effective(0, 10) == 0);
  CHECK(requirements::effective(50, 200) == 0);
}

TEST_CASE("requirements: unmet reads level, attribute totals, the item's reduction types and the global one") {
  snap::Snapshot s;
  s.level = 50;
  for (auto [t, v] : {std::pair{1, 300.f}, {2, 200.f}, {3, 100.f}, {36, 10.f}, {53, 5.f}, {54, 10.f}}) s.char_attributes.push_back({t, v});
  snap::Item it;
  CHECK(requirements::unmet(it, s).empty());  // no data: not checked
  auto& q = it.requirements.emplace();
  q.level = 55;                // 55 - 5.5 + 0.5 = 50: met at level 50
  q.attribute[0] = 300;        // Physique 300 + 5% global = 285.5 -> 285: met
  q.attribute[1] = 230;        // Cunning, reduced by type 36 (10) + global (5): 230 - 34.5 + 0.5 = 196: met
  q.reductions[1][0] = 36;
  q.attribute[2] = 120;        // Spirit: 120 - 6 + 0.5 = 114 > 100
  CHECK(requirements::unmet(it, s).size() == 1);
  const auto u = requirements::unmet(it, s).at(0);
  CHECK(u.what == 3);
  CHECK(u.need == 114);
  CHECK(requirements::text(u) == "Spirit 114 (have 100)");
  q.reductions[1][0] = 0;      // without type 36: 230 - 11.5 + 0.5 = 219 > 200
  CHECK(requirements::unmet(it, s).size() == 2);
  q.level = 60;                // 60 - 6 + 0.5 = 54 > 50
  CHECK(requirements::unmet(it, s).at(0).what == 0);
}

TEST_CASE("requirements: snapshot JSON round trip") {
  snap::Snapshot s;
  snap::Item it;
  it.id = 7;
  auto& q = it.requirements.emplace();
  q.level = 94;
  q.attribute[2] = 262;
  q.reductions[2][0] = 52;
  q.reductions[2][2] = 31;
  q.met = false;
  s.equipped.push_back(it);
  const auto back = snap::from_json(snap::to_json(s));
  REQUIRE(back.equipped.at(0).requirements);
  const auto& r = *back.equipped.at(0).requirements;
  CHECK(r.level == 94);
  CHECK(r.attribute[2] == 262);
  CHECK(r.reductions[2][0] == 52);
  CHECK(r.reductions[2][2] == 31);
  REQUIRE(r.met);
  CHECK_FALSE(*r.met);
  CHECK_FALSE(snap::from_json(snap::to_json(snap::Snapshot{})).schema < snap::kSchema);
}

TEST_CASE("requirements: tooltip says so when the candidate's are not met; the engine's check decides") {
  const auto s = load("lvl16-wp-before-belt.json");  // Spirit 198.2
  const auto p = preset();
  snap::Item horn = *std::find_if(s.equipped.begin(), s.equipped.end(), [](const snap::Item& e) { return e.location == 1; });
  horn.id = 900001;
  horn.location = -1;
  auto& q = horn.requirements.emplace();
  q.attribute[2] = 262;
  std::vector<std::string> log;
  auto lines = tooltip::compare(s, horn, 8, false, game_db(), p, {}, &log);
  CHECK(has(lines, "! can't equip yet: Spirit 262 (have 198)"));
  CHECK(has(lines, "vs L Hand: "));
  CHECK(has(lines, " (if equipped)"));  // scored as if it were worn

  engine_says(q, s, true);  // the engine says met (a reduction the model misses): no line, a model warning
  log.clear();
  lines = tooltip::compare(s, horn, 8, false, game_db(), p, {}, &log);
  CHECK_FALSE(has(lines, "can't equip yet"));
  CHECK(std::any_of(log.begin(), log.end(), [](const std::string& l) { return l.find("engine disagrees") != std::string::npos; }));

  q.attribute[2] = 0;
  engine_says(q, s, false);  // the engine says not met, the model finds nothing: still shown
  lines = tooltip::compare(s, horn, 8, false, game_db(), p, {}, &log);
  CHECK(has(lines, "! can't equip yet"));
}

TEST_CASE("requirements: an item the swap leaves unequippable is shown and scored as removed") {
  if (!game_db()) {
    MESSAGE("game not found; skipped");
    return;
  }
  auto s = load("lvl16-wp-before-belt.json");  // Physique 236; legs give +10 of it
  const auto p = preset();
  snap::Item legs = *at(s, 11);
  legs.id = 900002;
  legs.location = -1;
  auto& ca = legs.stats->char_attributes;
  ca.erase(std::remove_if(ca.begin(), ca.end(), [](const snap::CharContribution& c) { return c.type == 1; }), ca.end());

  std::vector<std::string> log;
  auto lines = tooltip::compare(s, legs, 4, false, game_db(), p, {}, &log);
  const double kept = percent(log, "vs Legs base: ");
  CHECK_FALSE(has(lines, "unequips"));

  // The shield needs Physique 230: met at 236, not at 226 after the swap.
  at(s, 1)->requirements.emplace().attribute[0] = 230;
  log.clear();
  lines = tooltip::compare(s, legs, 4, false, game_db(), p, {}, &log);
  const double broken = percent(log, "vs Legs base: ");
  CHECK(has(lines, ": needs Physique 230 (have 226)"));
  CHECK(broken < kept);
  // The item's own score (nothing taken off) next to the total.
  CHECK(has(lines, "  item itself: "));
  CHECK(has(lines, ", but it unequips 1 item:"));
  CHECK(percent(log, "vs Legs item itself: ") == doctest::Approx(kept).epsilon(0.001));

  // Already unmet before the swap (engine): not the swap's doing.
  engine_says(*at(s, 1)->requirements, s, false);
  lines = tooltip::compare(s, legs, 4, false, game_db(), p, {}, nullptr);
  CHECK_FALSE(has(lines, "unequips"));
}

// Regression (adapter 0.9.8, winterpoison): Heretic's Sigil (c015_medal, level 27) hovered at level 26, then
// scored again after the level-up with the flag read at 26: "can't equip yet" and "the model says met, the
// engine disagrees" on an item that was equippable (and later worn).
TEST_CASE("requirements: an engine flag read before a level, attribute or difficulty change is not used") {
  const auto before = load("lvl27-wp-after-legs.json");  // level 27, the medal still in the bag
  const auto after = load("lvl27-wp-after-medal.json");
  snap::Item medal = *std::find_if(after.equipped.begin(), after.equipped.end(), [](const snap::Item& e) { return e.location == 3; });
  REQUIRE(medal.record.ends_with("/c015_medal.dbr"));
  REQUIRE(medal.requirements);
  REQUIRE(medal.requirements->level == 27);
  medal.id = 900003;
  medal.location = -1;
  snap::Snapshot younger = before;
  younger.level = 26;
  const auto p = preset();
  auto shown = [&](const snap::Snapshot& s) {
    std::vector<std::string> log;
    const auto lines = tooltip::compare(s, medal, 15, true, game_db(), p, {}, &log);
    const bool disagrees = std::any_of(log.begin(), log.end(), [](const std::string& l) { return l.find("engine disagrees") != std::string::npos; });
    return std::pair{has(lines, "can't equip yet"), disagrees};
  };

  engine_says(*medal.requirements, younger, false);  // read at level 26
  CHECK(requirements::engine_met(medal, younger) == std::optional<bool>(false));
  CHECK(shown(younger) == std::pair{true, false});
  // Level 27: the old flag is dropped, the formula on the new sheet decides.
  CHECK_FALSE(requirements::engine_met(medal, before));
  CHECK(shown(before) == std::pair{false, false});

  // Same for a spent attribute point and a difficulty change.
  engine_says(*medal.requirements, before, false);
  CHECK(requirements::engine_met(medal, before) == std::optional<bool>(false));
  snap::Snapshot stronger = before;
  for (auto& c : stronger.char_attributes)
    if (c.type == 1) c.total += 8;
  CHECK_FALSE(requirements::engine_met(medal, stronger));
  snap::Snapshot elite = before;
  elite.difficulty = 1;
  CHECK_FALSE(requirements::engine_met(medal, elite));
  // A flag without the state it was read in (snapshots before schema 11) is never used.
  medal.requirements->met_at.reset();
  CHECK_FALSE(requirements::engine_met(medal, before));
  CHECK(shown(before) == std::pair{false, false});

  // The state survives the JSON round trip.
  snap::Snapshot s = before;
  engine_says(*medal.requirements, before, true);
  s.tooltip_items = {medal};
  const auto back = snap::from_json(snap::to_json(s));
  CHECK(requirements::engine_met(back.tooltip_items.at(0), back) == std::optional<bool>(true));
}

TEST_CASE("requirements: calibrate compares the formula with the engine's flag on every equipped item") {
  snap::Snapshot s;
  for (auto [t, v] : {std::pair{1, 236.f}, {3, 198.f}}) s.char_attributes.push_back({t, v});
  snap::Item a, b, c;
  a.record = "a.dbr";
  a.requirements.emplace().attribute[0] = 230;
  a.requirements->met = true;   // agrees
  b.record = "b.dbr";
  b.requirements.emplace().attribute[2] = 262;
  b.requirements->met = true;   // engine met, formula not: a reduction it misses
  c.record = "c.dbr";
  c.requirements.emplace().attribute[2] = 262;  // no engine flag: not checked
  s.equipped = {a, b, c};
  const auto r = calibrate::requirements(s);
  CHECK(r.checked == 2);
  REQUIRE(r.mismatches.size() == 1);
  CHECK(r.mismatches[0] == "b.dbr: model not met, engine met; Spirit 262 (have 198)");
}
