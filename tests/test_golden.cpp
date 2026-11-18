// Golden outputs: the tooltip block (scores, the Shift text and the log lines) for every fixture slot against every item another
// fixture wears there, and for every catalog build on its synthetic character against a fixed set of items. Any
// change to the score or the text shows up here; a speed-up must leave it unchanged (numbers within 1e-9).
// GDC_GOLDEN_WRITE=<file> writes the outputs instead of comparing; GDC_GOLDEN=<file> compares against another file
// (default tests/golden/tooltip.golden). Installed game; skips if absent.
#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>

#include "build_harness.h"
#include "buildpreset.h"
#include "catalog.h"
#include "gdc/attribute_names.h"
#include "gdc/text.h"
#include "gdc/tooltip.h"

using namespace gdc;
using namespace gdc::test;

namespace {

constexpr double kTolerance = 1e-9;

fs::path game_dir() {
  if (const char* d = std::getenv("GD_DIR")) return d;
  return fs::path(std::getenv("HOME")) / ".local/share/Steam/steamapps/common/Grim Dawn";
}

// Item::GetItemType of what a location holds (as test_symmetry.cpp).
int item_type(const snap::Item& it, int location, const arz::Database& db) {
  switch (location) {
    case equip::kRightHand: return item_types::kWeapon;
    case equip::kLeftHand: {
      const arz::Record* r = db.record(it.record);
      return r && r->cls.starts_with("WeaponArmor") ? item_types::kOffHand : item_types::kWeapon;
    }
    case equip::kNeck: return item_types::kNeck;
    case equip::kMedal: return item_types::kMedal;
    case equip::kFinger1: case equip::kFinger2: return item_types::kRing;
    case equip::kWaist: return item_types::kWaist;
    case equip::kHead: return item_types::kHead;
    case equip::kChest: return item_types::kChest;
    case equip::kShoulders: return item_types::kShoulders;
    case equip::kHands: return item_types::kHands;
    case equip::kLegs: return item_types::kLegs;
    case equip::kFeet: return item_types::kFeet;
    case equip::kArtifact: return item_types::kArtifact;
    default: return 0;
  }
}

snap::Item loose(snap::Item it, std::uint32_t id) {
  it.id = id;
  it.location = -1;
  return it;
}

std::string num(double v) {
  char b[40];
  std::snprintf(b, sizeof b, "%.17g", v);
  return b;
}

std::string opt(const std::optional<double>& v) { return v ? num(*v) : "-"; }

void block(std::string& out, const std::string& id, const snap::Snapshot& s, const snap::Item& cand, int type,
           const arz::Database& db, const score::Preset& preset, const Labels& labels) {
  std::vector<tooltip::Scored> scored;
  std::vector<std::string> log;
  const auto plain = tooltip::compare(s, cand, type, false, &db, preset, labels, &log, &scored);
  const auto detail = tooltip::compare(s, cand, type, true, &db, preset, labels);
  out += "# " + id + "\n";
  for (const auto& x : scored)
    out += "= " + std::to_string(x.location) + " " + std::to_string(x.also_off) + " " + num(x.percent) + " " +
           opt(x.itself) + " " + opt(x.moved) + "\n";
  for (const auto& l : plain) out += "P " + std::to_string(static_cast<int>(l.tone)) + " " + l.text + "\n";
  for (const auto& l : detail) out += "D " + std::to_string(static_cast<int>(l.tone)) + " " + l.text + "\n";
  for (const auto& l : log) out += "L " + l + "\n";
}

// Hand-written candidates for the synthetic characters: one stat each, as build_harness.h scores them.
std::vector<std::pair<std::string, snap::Item>> synthetic_items() {
  std::vector<std::pair<std::string, snap::Item>> v;
  auto ring = [&](const std::string& name, const std::function<void(snap::ItemStats&)>& f) {
    snap::Item it = bare(kFinger, kRing);
    f(*it.stats);
    v.push_back({name, loose(it, 960000 + static_cast<std::uint32_t>(v.size()))});
  };
  for (const int t : {2, 4, 5, 6, 7, 8, 9, 10, 11}) {
    ring("damage " + std::to_string(t), [&](snap::ItemStats& x) { x.offensive_modifiers = {pct(t, 30, 30)}; });
    ring("resist " + std::to_string(t), [&](snap::ItemStats& x) { x.defense = {resist(t, 20)}; });
  }
  ring("life", [](snap::ItemStats& x) { x.char_attributes = {{4, 300, 0}}; });
  ring("physique", [](snap::ItemStats& x) { x.char_attributes = {{1, 20, 0}}; });
  ring("oa", [](snap::ItemStats& x) { x.char_attributes = {{21, 60, 0}}; });
  ring("da", [](snap::ItemStats& x) { x.char_attributes = {{23, 60, 0}}; });
  ring("attack speed", [](snap::ItemStats& x) { x.char_attributes = {{15, 0, 10}}; });
  ring("stun resist", [](snap::ItemStats& x) { x.defense = {resist(42, 20)}; });
  ring("physical to fire", [](snap::ItemStats& x) { x.conversions = {{2, 6, 50}}; });
  ring("pierce malus", [](snap::ItemStats& x) { x.defense = {resist(4, -20)}; });
  v.push_back({"+1 all skills", loose(bare(kNeckSlot, "records/items/gearaccessories/necklaces/b201c_necklace.dbr"), 961000)});
  snap::Item plate = bare(kChestSlot, kChest);
  plate.stats->defense = {resist(38, 200)};
  v.push_back({"armor", loose(plate, 961001)});
  return v;
}

std::string outputs(const arz::Database& db, const Labels& labels) {
  std::string out;
  // Fixtures, each with the preset test_symmetry.cpp gives it.
  std::vector<std::pair<std::string, snap::Snapshot>> fx;
  for (const auto& e : fs::directory_iterator(fs::path(GDC_SOURCE_DIR) / "tests/fixtures/snapshots"))
    if (e.path().extension() == ".json") fx.push_back({e.path().filename().string(), snap::from_json(read(e.path()))});
  std::sort(fx.begin(), fx.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
  const score::Preset garden = parse_preset(read(fs::path(GDC_SOURCE_DIR) / "presets/plague-garden.toml"));
  const score::Preset defaults;
  for (const auto& [name, s] : fx) {
    const score::Preset& preset = name.find("-wp") != std::string::npos ? garden : defaults;
    for (const auto& old : s.equipped) {
      if (!old.stats) continue;
      const int type = item_type(old, old.location, db);
      for (const auto& [gname, g] : fx)
        for (const auto& cand : g.equipped) {
          if (!cand.stats || cand.record == old.record || item_type(cand, cand.location, db) != type) continue;
          if (std::any_of(s.equipped.begin(), s.equipped.end(), [&](const snap::Item& e) { return e.id == cand.id; })) continue;
          block(out, name + " loc " + std::to_string(old.location) + " <- " + gname + " " + cand.record, s,
                loose(cand, 980000), type, db, preset, labels);
        }
    }
  }
  // Catalog builds on their synthetic character (test_catalog.cpp), balanced playstyle.
  std::string error;
  const auto catalog = setup::load_catalog(read(fs::path(GDC_SOURCE_DIR) / "catalog/builds.toml"), error);
  REQUIRE_MESSAGE(catalog, error);
  const auto items = synthetic_items();
  for (const auto& e : *catalog) {
    const auto masteries = setup::code_masteries(e.classes[0]);
    const setup::BuildAnswers a = setup::from_entry(e, masteries, "synthetic");
    Build b(parse_preset(setup::preset_text(a)), e.category == "ranged" ? kGun : kSword);
    std::uint32_t points = 50;
    for (const int m : masteries) b.learn(setup::mastery_record(m).c_str(), points), points = 32;
    for (const auto& sk : a.skills)
      if (sk != "records/skills/default/defaultweaponattack.dbr") b.learn(sk.c_str(), 10);
    for (const auto& [iname, it] : items) {
      const int loc = it.record == kRing ? kFinger : it.record == kChest ? kChestSlot : kNeckSlot;
      block(out, "catalog " + e.id + " <- " + iname, b.s, it, item_type(it, loc, db), db, b.preset, labels);
    }
  }
  return out;
}

std::vector<std::string> lines(const std::string& text) {
  std::vector<std::string> v;
  std::istringstream in(text);
  for (std::string l; std::getline(in, l);) v.push_back(l);
  return v;
}

// "= ..." lines: every number within kTolerance; every other line exactly.
bool same_line(const std::string& a, const std::string& b) {
  if (a == b) return true;
  if (!a.starts_with("= ") || !b.starts_with("= ")) return false;
  std::istringstream x(a.substr(2)), y(b.substr(2));
  for (std::string p, q;;) {
    const bool more = static_cast<bool>(x >> p), more_b = static_cast<bool>(y >> q);
    if (more != more_b) return false;
    if (!more) return true;
    if (p == q) continue;
    if (p == "-" || q == "-") return false;
    if (std::abs(std::strtod(p.c_str(), nullptr) - std::strtod(q.c_str(), nullptr)) > kTolerance) return false;
  }
}

}  // namespace

TEST_CASE("golden: tooltip scores and Shift text unchanged (installed game; skips if absent)") {
  const arz::Database* db = game_db();
  if (!db) return;
  const auto text = text::Table::open_game(game_dir());
  const Labels labels(db, &text);
  const std::string out = outputs(*db, labels);
  if (const char* w = std::getenv("GDC_GOLDEN_WRITE")) {
    std::ofstream(w, std::ios::binary) << out;
    MESSAGE("wrote " << w);
    return;
  }
  const char* g = std::getenv("GDC_GOLDEN");
  const fs::path golden = g ? fs::path(g) : fs::path(GDC_SOURCE_DIR) / "tests/golden/tooltip.golden";
  if (!fs::exists(golden)) {
    MESSAGE(golden.string() << " not found; skipped");
    return;
  }
  const auto want = lines(read(golden)), have = lines(out);
  int diffs = 0;
  std::string first;
  for (std::size_t i = 0; i < std::max(want.size(), have.size()); ++i) {
    const std::string a = i < want.size() ? want[i] : "<end>", b = i < have.size() ? have[i] : "<end>";
    if (same_line(a, b)) continue;
    if (++diffs <= 10) first += "line " + std::to_string(i + 1) + "\n  golden: " + a + "\n  now:    " + b + "\n";
  }
  CHECK_MESSAGE(diffs == 0, diffs << " lines differ\n" << first);
  CHECK(have.size() == want.size());
}
