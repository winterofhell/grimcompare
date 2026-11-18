// winterpoison, level 27, Normal, plague-garden stage 10-35 (adapter 0.9.8). Fixtures: auto-snapshot records
// 0013..0015 of segment 20261115-182525-921: before / after Hermit's Legguards (c002_legs), after Heretic's Sigil
// (c015_medal). Needs the installed game for the records; skips without it.
#include <doctest/doctest.h>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "game_db.h"
#include "gdc/enemy.h"
#include "gdc/score.h"
#include "gdc/snapshot.h"
#include "gdc/tooltip.h"

using namespace gdc;
namespace fs = std::filesystem;

namespace {

std::string slurp(const fs::path& p) {
  std::ifstream f(p);
  REQUIRE(f.good());
  std::stringstream ss;
  ss << f.rdbuf();
  return ss.str();
}

snap::Snapshot fixture(const char* name) {
  return snap::from_json(slurp(fs::path(GDC_SOURCE_DIR) / "tests/fixtures/snapshots" / name));
}

score::Preset plague_garden() {
  std::string error;
  auto p = score::parse(slurp(fs::path(GDC_SOURCE_DIR) / "presets/plague-garden.toml"), error);
  REQUIRE_MESSAGE(p, error);
  return *p;
}

// The item a later snapshot wears there, as a candidate from the bag.
snap::Item worn(const snap::Snapshot& s, int location) {
  for (const auto& e : s.equipped)
    if (e.location == location) {
      snap::Item it = e;
      it.id += 1000000;
      it.location = -1;
      return it;
    }
  FAIL("slot empty");
  return {};
}

snap::CombatTotals defense(int type, float value) {
  snap::CombatTotals d;
  d.type = type;
  d.defense = value;
  return d;
}

struct Shown {
  double percent = 1e9;  // "<title> base"
  std::vector<tooltip::Line> lines;
  std::vector<std::string> log;
};
Shown hover(const snap::Snapshot& s, const snap::Item& item, int type, const char* title, const score::Preset& p) {
  Shown out;
  out.lines = tooltip::compare(s, item, type, true, game_db(), p, {}, &out.log);
  const std::string head = std::string(title) + " base: ";
  for (const auto& l : out.log)
    if (l.starts_with(head)) out.percent = std::stod(l.substr(head.size()));
  REQUIRE(out.percent < 1e9);
  return out;
}

// "[+1.1" / "[-0.2 w0.1]" at the end of a sheet line; 0 without points.
double points(const std::string& line) {
  const auto at = line.rfind('[');
  return at == std::string::npos || line.back() != ']' ? 0 : std::atof(line.c_str() + at + 1);
}

}  // namespace

// Issue: the pants scored ~0% because the lost overcap (Fire 103 -> 84, Cold 91 -> 83, Poison 108 -> 97,
// Lightning 105 -> 97, all still at the cap) was costed under a fixed 25 resist reduction.
TEST_CASE("regress: overcap lost above the cap costs what the monsters of that level apply, not a fixed 25") {
  if (!game_db()) {
    MESSAGE("game not found; skipped");
    return;
  }
  const auto s = fixture("lvl27-wp-before-legs.json");
  const auto p = plague_garden();
  REQUIRE(score::stage(p, s));
  REQUIRE(score::stage(p, s)->name == "10-35");
  CHECK_FALSE(score::stage(p, s)->settings.resist_reduction);  // auto: the monster table
  const snap::Item pants = worn(fixture("lvl27-wp-after-legs.json"), 11);
  REQUIRE(pants.record.ends_with("/c002_legs.dbr"));

  const Shown now = hover(s, pants, 4, "vs Legs", p);
  CHECK(now.percent > 5);
  int overcapped = 0;
  for (const auto& l : now.lines)
    for (const char* name : {"    Cold 80% (", "    Fire 80% (", "    Poison 80% (", "    Lightning 80% ("})
      if (l.text.starts_with(name)) {
        ++overcapped;
        CHECK(points(l.text) <= 0);
        CHECK(points(l.text) > -0.5);
      }
  CHECK(overcapped == 4);

  // The stage can still ask for a fixed buffer: the old settings give the old ~0%.
  auto fixed = p;
  for (auto& st : fixed.stages)
    if (st.name == "10-35") st.settings.resist_reduction = 25, st.settings.threshold_resist_weight = 1;
  CHECK(std::abs(hover(s, pants, 4, "vs Legs", fixed).percent) < 1);
  std::string error;
  const auto parsed = score::parse("[survival]\nresist_reduction = 3\n[[stage]]\nname = \"a\"\n[stage.survival]\n"
                                   "resist_reduction = 25\n[[stage]]\nname = \"b\"\nmin_level = 50\n[stage.survival]\n"
                                   "resist_reduction = \"auto\"\n",
                                   error);
  REQUIRE_MESSAGE(parsed, error);
  CHECK(parsed->resist_reduction == std::optional<double>(3));
  CHECK(parsed->stages.at(0).settings.resist_reduction == std::optional<double>(25));
  CHECK_FALSE(parsed->stages.at(1).settings.resist_reduction);
  CHECK_FALSE(score::parse("[survival]\nresist_reduction = \"some\"\n", error));
}

TEST_CASE("regress: a resist that ends under 80 is never free") {
  if (!game_db()) {
    MESSAGE("game not found; skipped");
    return;
  }
  const auto s = fixture("lvl27-wp-before-legs.json");  // Fire 103 uncapped
  const auto p = plague_garden();
  // The worn pants again, with nothing changed but Fire.
  auto same_but_fire = [&](float fire) {
    snap::Item it = worn(s, 11);
    REQUIRE(it.stats);
    it.stats->defense.push_back(defense(6, fire));
    return hover(s, it, 4, "vs Legs", p).percent;
  };
  const double to_cap = same_but_fire(-23), under = same_but_fire(-25), far = same_but_fire(-33);
  CHECK(to_cap <= 0);        // 103 -> 80: only overcap
  CHECK(to_cap > -0.5);
  CHECK(under < -1);         // 103 -> 78
  CHECK(far < under - 5);    // 103 -> 70: every further point costs
  // The real pants with 10 Fire less (84 -> 74) lose clearly against themselves.
  snap::Item pants = worn(fixture("lvl27-wp-after-legs.json"), 11);
  const double kept = hover(s, pants, 4, "vs Legs", p).percent;
  pants.stats->defense.push_back(defense(6, -10));
  CHECK(hover(s, pants, 4, "vs Legs", p).percent < kept - 3);  // 6 points under the cap
}

// Issue: "Life 4093->4002 (-91) [+1.1]". The sheet lines compared the items as hovered (the worn medal's Corpse
// Dust is lost), the points came from the comparison without components (Life 3957 -> 4002).
TEST_CASE("regress: every sheet line's points have the sign of the line's own change") {
  if (!game_db()) {
    MESSAGE("game not found; skipped");
    return;
  }
  const auto s = fixture("lvl27-wp-after-legs.json");
  const snap::Item medal = worn(fixture("lvl27-wp-after-medal.json"), 3);
  REQUIRE(medal.record.ends_with("/c015_medal.dbr"));
  const Shown shown = hover(s, medal, 15, "vs Medal", plague_garden());

  bool life = false;
  int checked = 0;
  for (const auto& l : shown.lines) {
    const std::string& t = l.text;
    if (t.starts_with("    Life 4093->4002 (-91)")) {
      life = true;
      CHECK(points(t) < 0);
    }
    // "(+12)" / "(-91)": the line's own change.
    const auto open = t.find(" (");
    if (!t.starts_with("    ") || open == std::string::npos || (t[open + 2] != '+' && t[open + 2] != '-')) continue;
    const double delta = std::atof(t.c_str() + open + 2), pts = points(t);
    if (pts == 0 || delta == 0) continue;
    ++checked;
    CAPTURE(t);
    CHECK((delta > 0) == (pts > 0));
  }
  CHECK(life);
  CHECK(checked >= 2);
  // Tone and points agree on every line that has points.
  for (const auto& l : shown.lines) {
    const double pts = points(l.text);
    if (pts == 0 || !l.text.starts_with("    ")) continue;
    CAPTURE(l.text);
    CHECK_FALSE((l.tone == tooltip::Tone::Better && pts < 0));
    CHECK_FALSE((l.tone == tooltip::Tone::Worse && pts > 0));
  }
  // Both comparisons are in the log; they differ by the component.
  double hovered = 1e9;
  for (const auto& l : shown.log)
    if (l.starts_with("vs Medal as hovered: ")) hovered = std::stod(l.substr(21));
  CHECK(hovered < shown.percent);
}

TEST_CASE("regress: enemy hits and OA follow the snapshot's difficulty") {
  if (!game_db()) {
    MESSAGE("game not found; skipped");
    return;
  }
  auto s = fixture("lvl27-wp-before-legs.json");
  const auto p = plague_garden();
  const auto skills = score::main_skills(s, score::stage(p, s)->settings, game_db());
  auto read = [&](int difficulty, std::uint32_t ramp, const char* key) {
    s.difficulty = difficulty;
    s.difficulty_ramp = ramp;
    const std::string text = score::compare(s, s, skills, score::stage(p, s)->settings, game_db()).before;
    const auto at = text.find(key);
    REQUIRE(at != std::string::npos);
    return std::atof(text.c_str() + at + std::string(key).size());
  };
  double hit = 0, pth = 0;
  for (const auto [difficulty, ramp] : {std::pair{0, 0u}, std::pair{1, 0u}, std::pair{2, 0u}, std::pair{2, 1u}}) {
    const double h = read(difficulty, ramp, "% hit "), e = read(difficulty, ramp, " enemy pth ");
    CAPTURE(difficulty);
    CHECK(h > hit);
    CHECK(e > pth);
    hit = h, pth = e;
  }
  s.difficulty = 0, s.difficulty_ramp = 0;
  CHECK(read(0, 0, "% hit ") == doctest::Approx(enemy::at(27, 0).hit_physical).epsilon(0.01));
}

namespace {

// The item the snapshot was taken hovering, as a bag candidate.
snap::Item hovered(const snap::Snapshot& s, std::string_view record) {
  for (const auto& t : s.tooltip_items)
    if (t.record.find(record) != std::string::npos) {
      snap::Item it = t;
      it.location = -1;
      it.id += 1000000;
      return it;
    }
  FAIL("not hovered");
  return {};
}

const tooltip::Line* line(const Shown& s, std::string_view start) {
  for (const auto& l : s.lines)
    if (l.text.starts_with(start)) return &l;
  return nullptr;
}

double part(const std::string& log, std::string_view name) {
  const auto at = log.find(name);
  return at == std::string::npos ? 1e9 : std::stod(log.substr(at + name.size()));
}

}  // namespace

// winterpoison level 39, plague-garden 35-93, F9 snapshots 2026-11-18. Issue: Galeslice for Bloodfeast scored
// Damage +0.1% and "procs and pets +5.7" while losing the Bloody Pox modifier (Vitality and Bleeding, which
// nothing converts; the Pox damage that matters is Black Death's Poison).
TEST_CASE("regress: losing an item skill modifier costs that modifier's share of the skill") {
  if (!game_db()) {
    MESSAGE("game not found; skipped");
    return;
  }
  const auto s = fixture("lvl39-wp-medal-hover.json");
  const auto p = plague_garden();
  REQUIRE(score::stage(p, s)->name == "35-93");
  const Shown shown = hover(s, hovered(s, "b203b_medal"), 15, "vs Medal", p);
  std::string base, before, after;
  for (const auto& l : shown.log) {
    if (l.starts_with("vs Medal base: ")) base = l;
    if (before.empty() && l.starts_with("  before: ")) before = l;
    if (after.empty() && l.starts_with("  after:  ")) after = l;
  }
  // Bloody Pox by source: the modifier's flat damage is in the skill's pool and leaves with the medal.
  const double pox_before = part(before, "pox1 L1 dmg "), pox_after = part(after, "pox1 L1 dmg ");
  const double vit = part(before, "medal_b013_bloodypox (item) Vitality "), bleed = part(before, "(item) Vitality " + std::to_string(static_cast<int>(vit)) + " Bleeding ");
  CHECK(after.find("medal_b013_bloodypox") == std::string::npos);
  CHECK(pox_after / pox_before == doctest::Approx(1 - (vit + bleed) / pox_before).epsilon(0.02));
  // Damage: Bloody Pox's weight (1 + Black Death 12 of 29 points: 0.45) x that loss (~37%), not ~0.
  CHECK(before.find(" w 0.45;") != std::string::npos);
  const double damage = part(base, "(Damage ");
  CHECK(damage < -12);
  CHECK(damage > -22);
  CHECK(line(shown, "  top:")->text.find("procs and pets") == std::string::npos);
  // Shift: the modifiers per skill, the item tooltip's numbers, lost in red (no game text here: record names).
  const auto* pox = line(shown, "    pox1 (item): -443 Vitality,");
  REQUIRE(pox);
  CHECK(pox->tone == tooltip::Tone::Worse);
  std::string all;
  for (const auto& l : shown.lines) all += l.text + "\n";
  CHECK(all.find("-391 Bleeding/s") != std::string::npos);
  CHECK(all.find("    bladetrap1 (item, not learned): -344 Vitality (not scored),\n      -2s duration (not scored)") != std::string::npos);
  // Bloody Pox learned but not a main skill: nothing reads its modifier's damage.
  auto eye = p;
  eye.skills = {"records/skills/playerclass03/evileye1.dbr"};
  for (auto& st : eye.stages) st.settings.skills = eye.skills;
  const Shown other = hover(s, hovered(s, "b203b_medal"), 15, "vs Medal", eye);
  REQUIRE(line(other, "    pox1 (item): -443 Vitality (not scored),"));
  all.clear();
  for (const auto& l : other.lines) all += l.text + "\n";
  CHECK(all.find("-391 Bleeding/s (not scored)") != std::string::npos);
}

// Same character: Bloody Pox 1 / Wasting 0 / Black Death 12, Evil Eye 12 + 3 modifiers, Grasping Vines 1.
TEST_CASE("regress: a skill's modifiers and transmuter count toward its weight and its damage") {
  const arz::Database* db = game_db();
  if (!db) {
    MESSAGE("game not found; skipped");
    return;
  }
  const auto s = fixture("lvl39-wp-medal-hover.json");
  const auto preset = plague_garden();
  const score::Settings& p = score::settings(preset, s);
  auto with = [&](const char* stem, std::uint32_t points, std::uint32_t current) {
    auto out = s;
    for (auto& k : out.skills)
      if (k.record.ends_with(stem)) k.level = points, k.current_level = current;
    return out;
  };
  auto pox = [](const std::string& log) { return part(log, "pox1 L1 dmg "); };
  // Weight: points in the skill and its modifiers, 1 + 12 of 29.
  const auto skills = score::main_skills(s, p, db);
  const auto it = std::find_if(skills.begin(), skills.end(), [](const auto& k) { return k.record.ends_with("/pox1.dbr"); });
  REQUIRE(it != skills.end());
  CHECK(it->weight == doctest::Approx(13.0 / 29));
  // Wasting: 1 point, then +3 levels from gear, scores through Bloody Pox.
  const auto one = with("/pox2.dbr", 1, 1);
  const auto wasting = score::compare(one, with("/pox2.dbr", 1, 4), score::main_skills(one, p, db), p, db);
  CHECK(pox(wasting.after) > pox(wasting.before));
  CHECK(wasting.ratio.damage > 1);
  // The transmuter: "Total Damage Modified by 60%" on all of Bloody Pox's damage, after the % sum.
  const auto transmuted = score::compare(s, with("/pox1b.dbr", 1, 1), skills, p, db);
  CHECK(pox(transmuted.after) / pox(transmuted.before) == doctest::Approx(1.6).epsilon(0.002));
}

// Same character. Issue: the belt scored +7.7%: "top: Target stun resist +24.0, Lightning Resist -12.3, Life -6.1"
// for 12% stun resist toward an endgame 80% target against Lightning 79 -> 63, under the cap.
TEST_CASE("regress: a base resist dropping under the cap outweighs crowd-control resist") {
  if (!game_db()) {
    MESSAGE("game not found; skipped");
    return;
  }
  const auto s = fixture("lvl39-wp-belt-hover.json");
  const Shown shown = hover(s, hovered(s, "a07_waist001"), 13, "vs Waist", plague_garden());
  CHECK(shown.percent < 0);
  const auto* top = line(shown, "  top: ");
  REQUIRE(top);
  CHECK(top->text.starts_with("  top: Lightning Resist -"));
  const auto* lightning = line(shown, "    Lightning 79->63%");
  const auto* stun = line(shown, "    Stun 26->38%");  // "Reduced Stun Duration" with game text
  REQUIRE(lightning);
  REQUIRE(stun);
  CHECK(points(stun->text) > 0);
  CHECK(points(stun->text) < -points(lightning->text) / 4);
  CHECK(stun->text.find(" target 31]") != std::string::npos);  // 75 x 39 / 94
  for (const auto& l : shown.lines) {
    CAPTURE(l.text);
    CHECK_FALSE(l.text.find("Target ") != std::string::npos);
    CHECK_FALSE(l.text == "    Physical 14%");  // unchanged and no points: not shown
  }
}
