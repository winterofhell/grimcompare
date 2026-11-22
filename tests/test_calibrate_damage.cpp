#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <tuple>

#include "game_db.h"
#include "gdc/attribute_names.h"
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
    if (k.current_level > 0) s.skill_dps.push_back({k.id, k.record, false, false, 0, {}, {}});
}

snap::SkillDps& engine_of(snap::Snapshot& s, const std::string& record) {
  for (auto& d : s.skill_dps)
    if (d.record == record) return d;
  FAIL("no engine entry for " << record);
  return s.skill_dps.front();
}

// Engine entries equal to the model's: a hit as a min / max pair around it, a DoT as per second over 4 s; L Hand types
// into the L Hand list with the dual-wield flag.
void engine_from_model(snap::Snapshot& s, const std::vector<calibrate::SkillDamageCheck>& model) {
  for (const auto& c : model) {
    if (!c.skipped.empty()) continue;
    auto& d = engine_of(s, c.record);
    for (const auto& t : c.types) {
      const float v = static_cast<float>(t.model);
      auto& list = t.off_hand ? d.off_hand : d.main_hand;
      d.dual_wield = d.dual_wield || t.off_hand;
      if (t.key.second) list.push_back({t.key.first, true, v / 4, v / 4, 4});
      else list.push_back({t.key.first, false, v * 0.9f, v * 1.1f});
    }
  }
}


// Level 94, two 1H swords (R, L) with their own affixes, the default attack and Shadow Strike (cooldown) learned; the
// reductions on the character read and zero.
snap::Snapshot dual_wield(std::vector<snap::Damage> right, std::vector<snap::Damage> left) {
  snap::Snapshot s;
  s.level = 94;
  s.defense_cap = 80;
  s.pth.minimum = 55;
  s.pth.crit_divisor_min = 100;
  s.char_attributes.push_back({1, 50, 0, 50, 0});
  s.char_attributes.push_back({4, 1000, 0, 1000, 0});
  s.damage_reduction = snap::DamageReduction{};
  for (int loc : {0, 1}) {
    snap::Item& sword = s.equipped.emplace_back();
    sword.id = static_cast<std::uint32_t>(100 + loc);
    sword.location = loc;
    sword.record = "records/items/awakened/gearweapons/swords1h/c027_sword.dbr";
    sword.stats = snap::ItemStats{};
    sword.stats->offensive_damage = loc == 0 ? std::move(right) : std::move(left);
  }
  for (const auto& [id, record, level] : {std::tuple{1u, "records/skills/default/defaultweaponattack.dbr", 1u},
                                          std::tuple{2u, "records/skills/playerclass04/shadowstrike.dbr", 10u}}) {
    snap::Skill& k = s.skills.emplace_back();
    k.id = id;
    k.record = record;
    k.level = k.current_level = level;
  }
  blank_engine(s);
  return s;
}

const calibrate::TypeDiff* type_of(const calibrate::SkillDamageCheck& c, score::DamageKey k, bool off_hand) {
  for (const auto& t : c.types)
    if (t.key == k && t.off_hand == off_hand) return &t;
  return nullptr;
}

// Every engine entry of the compared skills scaled by `f(type, dot)`.
template <class F>
void scale_engine(snap::Snapshot& s, F f) {
  for (auto& d : s.skill_dps)
    for (auto* list : {&d.main_hand, &d.off_hand})
      for (auto& e : *list) {
        e.min *= f(e.type, e.dot);
        e.max *= f(e.type, e.dot);
      }
}
}  // namespace

TEST_CASE("engine damage check: the summary counts rules, skills and types") {
  calibrate::SkillDamageCheck ok{"records/skills/a/fire1.dbr", {}, {{{6, false}, 100, 100}}, {}, {"weapon damage", "DoT"}};
  calibrate::SkillDamageCheck bad{"records/skills/a/cold1.dbr", {}, {{{5, true}, 100, 150}, {{6, false}, 10, 10}}, {}, {"DoT"}};
  calibrate::SkillDamageCheck skip{"records/skills/a/x.dbr", "no damage on either side", {}, {}, {}};
  calibrate::SkillDamageCheck pet{"records/p/pet.dbr", {}, {{{6, false}, 50, 50}}, {}, {"pet %"}};
  const auto lines = calibrate::damage_summary({ok, bad, ok, skip, skip, pet});
  const std::vector<std::string> want = {
      "RULES confirmed: pet % (1), weapon damage (2)",
      "RULE failing: DoT: 1 of 3 off (cold1)",
      "SKILL cold1: 0 pass, 1 fail",
      "SKILL fire1: 2 pass, 0 fail",
      "SKILL pet: 1 pass, 0 fail",
      "TYPE Fire: 3 ok, 0 off",
      "TYPE Frostburn: 0 ok, 1 off (cold1)",
      "TYPE pet Fire: 1 ok, 0 off",
      "SKIPPED 2x no damage on either side",
  };
  CHECK(lines == want);
  CHECK(calibrate::damage_summary({}) == std::vector<std::string>{"RULES confirmed: none"});
}

TEST_CASE("engine damage check: item-granted skills are checked too") {
  const arz::Database* db = game_db();
  if (!db) return;
  snap::Snapshot s = fixture("lvl27-wp-after-legs.json");
  blank_engine(s);
  for (const auto& k : s.item_skills)
    if (k.current_level > 0) s.skill_dps.push_back({k.id, k.record, false, false, 0, {}, {}});
  const auto model = calibrate::damage(s, *db);
  const auto item = std::find_if(model.begin(), model.end(), [](const auto& c) {
    return c.skipped.empty() && std::find(c.rules.begin(), c.rules.end(), "item skill") != c.rules.end();
  });
  REQUIRE(item != model.end());
  INFO(item->record);
  CHECK_FALSE(item->pass());
  engine_from_model(s, model);
  for (const auto& c : calibrate::damage(s, *db))
    if (c.record == item->record) CHECK(c.pass());
}

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

TEST_CASE("engine damage check: dual wield compares each hand, the shared DoT folds into the R Hand (installed game; skips if absent)") {
  const arz::Database* db = game_db();
  if (!db) return;
  // R: +10 Fire, Bleeding 20/s x 3 s; L: +20 Fire, Bleeding 10/s x 3 s.
  snap::Snapshot s = dual_wield({{1, false, 100, 120}, {6, false, 10, 10}, {15, true, 20, 20, 3}},
                                {{1, false, 100, 120}, {6, false, 20, 20}, {15, true, 10, 10, 3}});
  const auto model = calibrate::damage(s, *db);
  REQUIRE(model.size() == 2);
  for (const auto& c : model) {
    INFO(c.record);
    REQUIRE(c.skipped.empty());
    const auto *fire_r = type_of(c, {6, false}, false), *fire_l = type_of(c, {6, false}, true);
    REQUIRE(fire_r);
    REQUIRE(fire_l);
    CHECK(fire_l->model > fire_r->model);  // each hand its own affix
    // Each weapon's own Bleeding is its own source: it stays in its hand's list.
    const auto *bleed_r = type_of(c, {15, true}, false), *bleed_l = type_of(c, {15, true}, true);
    REQUIRE(bleed_r);
    REQUIRE(bleed_l);
    CHECK(bleed_l->model == doctest::Approx(bleed_r->model / 2));
  }
  // A ring's Bleeding is in both passes: it folds, the R Hand list keeps it, the L Hand list is unchanged.
  snap::Snapshot ring = s;
  snap::Item& band = ring.equipped.emplace_back();
  band.id = 102;
  band.location = equip::kFinger1;
  band.record = "records/items/gearaccessories/rings/c017_ring.dbr";
  band.stats = snap::ItemStats{};
  band.stats->offensive_damage = {{15, true, 10, 10, 3}};
  const auto shared = calibrate::damage(ring, *db);
  REQUIRE(shared.size() == model.size());
  for (std::size_t i = 0; i < model.size(); ++i) {
    CHECK(type_of(shared[i], {15, true}, true)->model == doctest::Approx(type_of(model[i], {15, true}, true)->model));
    CHECK(type_of(shared[i], {15, true}, false)->model > type_of(model[i], {15, true}, false)->model);
  }
  // The model's own numbers per hand pass; 10% more on one L Hand type fails that type only.
  engine_from_model(s, model);
  for (const auto& d : s.skill_dps) CHECK(d.dual_wield);
  for (const auto& c : calibrate::damage(s, *db)) CHECK(c.pass());
  auto& d = engine_of(s, "records/skills/default/defaultweaponattack.dbr");
  const auto fire = std::find_if(d.off_hand.begin(), d.off_hand.end(), [](const snap::Damage& e) { return e.type == 6; });
  REQUIRE(fire != d.off_hand.end());
  fire->min *= 1.1f;
  fire->max *= 1.1f;
  const auto off = calibrate::damage(s, *db, {d.record});
  REQUIRE(off.size() == 1);
  int n_off = 0;
  for (const auto& t : off[0].types) {
    if (calibrate::damage_off(t)) CHECK(t.off_hand);
    n_off += calibrate::damage_off(t);
  }
  CHECK(n_off == 1);
  const auto lines = calibrate::damage_report(off, "x");
  CHECK(std::any_of(lines.begin(), lines.end(), [](const std::string& l) { return l.starts_with("    L Hand Fire") && l.ends_with(" OFF"); }));
}

TEST_CASE("engine damage check: the reductions on the character are undone per type (installed game; skips if absent)") {
  const arz::Database* db = game_db();
  if (!db) return;
  snap::Snapshot s = dual_wield({{1, false, 100, 120}, {5, false, 10, 10}, {6, true, 10, 10, 3}}, {{1, false, 100, 120}});
  engine_from_model(s, calibrate::damage(s, *db));
  // 20% total, 30% Physical, 25% Elemental: Physical keeps 70%, direct Cold 75%, a Fire DoT 80% (DoTs take only the
  // Physical one).
  s.damage_reduction = snap::DamageReduction{0, 20, 30, 25};
  scale_engine(s, [](int type, bool dot) {
    if (type == 1 || type == 2) return 0.7f;
    if (!dot && (type == 5 || type == 6 || type == 8 || type == 58)) return 0.75f;
    return 0.8f;
  });
  for (const auto& c : calibrate::damage(s, *db)) {
    INFO(c.record);
    CHECK(c.pass());
    CHECK(c.note.find("engine without the damage reductions on the character") != std::string::npos);
  }
  s.damage_reduction->absolute = 5;
  CHECK(calibrate::damage(s, *db)[0].skipped.starts_with("a flat damage reduction"));
  s.damage_reduction->absolute = 0;
  s.damage_reduction->percent = 100;
  CHECK(calibrate::damage(s, *db)[0].skipped.starts_with("a damage reduction of 100%"));
  s.damage_reduction.reset();
  CHECK(calibrate::damage(s, *db)[0].skipped == "the damage reductions on the character were not read");
  s.schema = 15;  // before the read existed: compared as is
  CHECK(calibrate::damage(s, *db)[0].skipped.empty());
}

TEST_CASE("engine damage check: a timed buff's Total Damage Modified scales the model (installed game; skips if absent)") {
  const arz::Database* db = game_db();
  if (!db) return;
  snap::Snapshot s = dual_wield({{1, false, 100, 120}}, {{1, false, 100, 120}});
  const auto plain = calibrate::damage(s, *db);
  // Mark of Torment: -25% Total Damage while up.
  snap::Skill& torment = s.item_skills.emplace_back();
  torment.id = 50;
  torment.record = "records/skills/itemskills/item_torment.dbr";
  torment.level = torment.current_level = 1;
  torment.item_id = 100;
  torment.buff_active = true;
  const auto up = calibrate::damage(s, *db);
  REQUIRE(up.size() == plain.size());
  for (std::size_t i = 0; i < up.size(); ++i) {
    INFO(up[i].record);
    REQUIRE(up[i].types.size() == plain[i].types.size());
    for (std::size_t t = 0; t < up[i].types.size(); ++t) CHECK(up[i].types[t].model == doctest::Approx(plain[i].types[t].model * 0.75));
    CHECK(up[i].note == "model x0.750 for buffs up: item_torment");
  }
  torment.buff_active = false;
  for (const auto& c : calibrate::damage(s, *db)) CHECK(c.note.empty());
}
