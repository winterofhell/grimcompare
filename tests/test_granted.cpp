#include <doctest/doctest.h>

#include <algorithm>
#include <string>

#include "game_db.h"
#include "gdc/score.h"
#include "gdc/stat_model.h"

using namespace gdc;

namespace {

// d011_hands grants item_harbingersgrasp (Skill_Passive, Ranged2h, itemSkillLevelEq "1"): level 1 DA +25,
// Chaos 10-28, Chaos +52% (database.arz 1.3.1.1).
constexpr const char* kHands = "records/items/gearhands/d011_hands.dbr";
constexpr const char* kGrasp = "records/skills/itemskills/legendary/item_harbingersgrasp.dbr";

snap::Item item(std::uint32_t id, int location, std::string record) {
  snap::Item it;
  it.id = id;
  it.location = location;
  it.record = std::move(record);
  it.stats = snap::ItemStats{};
  return it;
}

snap::Snapshot wearing_hands(bool on) {
  snap::Snapshot s;
  s.level = 50;
  s.defense_cap = 80;
  s.char_attributes = {{23, 1025, 0, 1025, 0}};  // DA (re/CharAttributeType.tsv)
  s.damage_modifiers = {{10, 0, 0, 0, 152, 0}};
  s.equipped = {item(11, 10, kHands)};
  snap::Skill k;
  k.id = 900;
  k.record = kGrasp;
  k.level = k.current_level = 1;
  k.item_id = 11;
  k.passive = snap::ItemStats{};
  if (on) k.passive->char_attributes.push_back({23, 25, 0});
  s.item_skills = {k};
  return s;
}

double da(const snap::Snapshot& s) {
  for (const auto& a : s.char_attributes)
    if (a.type == 23) return a.total;
  return 0;
}

const model::Granted* granted(const model::Prediction& p, const char* record) {
  const auto it = std::find_if(p.granted.begin(), p.granted.end(), [&](const auto& g) { return g.record == record; });
  return it != p.granted.end() ? &*it : nullptr;
}

}  // namespace

TEST_CASE("granted skills: removing an item takes off what its skill adds now (captured), damage % from its record") {
  const arz::Database* db = game_db();
  if (!db) return;
  const auto p = model::swap(wearing_hands(true), item(12, 10, "records/items/gearhands/a01_hands02.dbr"), 10, db);
  const auto* g = granted(p, kGrasp);
  REQUIRE(g);
  CHECK(!g->gained);
  CHECK(g->scored);
  CHECK(da(p.after) == doctest::Approx(1000));
  CHECK(p.after.damage_modifiers[0].damage_modifier == doctest::Approx(100));
  CHECK(p.after.item_skills.empty());
}

TEST_CASE("granted skills: a weapon-gated passive that is off now changes nothing when removed") {
  const arz::Database* db = game_db();
  if (!db) return;
  const auto p = model::swap(wearing_hands(false), item(12, 10, "records/items/gearhands/a01_hands02.dbr"), 10, db);
  const auto* g = granted(p, kGrasp);
  REQUIRE(g);
  CHECK(g->scored);
  CHECK(da(p.after) == doctest::Approx(1025));
  CHECK(p.after.damage_modifiers[0].damage_modifier == doctest::Approx(152));
}

TEST_CASE("granted skills: unscored with the reason — old snapshot, weapon type, proc; a grant on both items is no change") {
  const arz::Database* db = game_db();
  if (!db) return;
  snap::Snapshot old = wearing_hands(true);
  old.schema = 5;
  old.item_skills.clear();
  const auto a = model::swap(old, item(12, 10, "records/items/gearhands/a01_hands02.dbr"), 10, db);
  REQUIRE(granted(a, kGrasp));
  CHECK(!granted(a, kGrasp)->scored);
  CHECK(granted(a, kGrasp)->why == "not in snapshot (schema < 6)");

  snap::Snapshot bare;
  bare.level = 50;
  const auto b = model::swap(bare, item(13, 10, kHands), 10, db);
  REQUIRE(granted(b, kGrasp));
  CHECK(granted(b, kGrasp)->gained);
  CHECK(granted(b, kGrasp)->why == "weapon type");
  // c031_ring: item_doombolt, a proc (cast_@enemyonanyhit_20%).
  const auto c = model::swap(bare, item(14, 4, "records/items/gearaccessories/rings/c031_ring.dbr"), 4, db);
  REQUIRE(granted(c, "records/skills/itemskills/item_doombolt.dbr"));
  CHECK(granted(c, "records/skills/itemskills/item_doombolt.dbr")->why.starts_with("Skill_"));

  const auto same = model::swap(wearing_hands(true), item(15, 10, kHands), 10, db);
  CHECK(same.granted.empty());
  CHECK(same.after.item_skills.size() == 1);
}

TEST_CASE("granted skills: the score counts an item-granted passive's flat damage while it is on") {
  const arz::Database* db = game_db();
  if (!db) return;
  snap::Snapshot on = wearing_hands(true);
  on.skills.push_back({});
  on.skills.back().record = "records/skills/default/defaultweaponattack.dbr";
  on.skills.back().level = on.skills.back().current_level = 1;
  snap::Item weapon = item(1, 0, "records/items/w.dbr");
  weapon.stats->offensive_damage.push_back({2, false, 100, 100});
  on.equipped.push_back(weapon);
  on.damage_modifiers.clear();
  snap::Snapshot off = on;
  off.item_skills[0].passive = snap::ItemStats{};
  score::Preset p;
  p.skills = {"records/skills/default/defaultweaponattack.dbr"};
  p.enemy_resist.clear();  // no enemy model: raw damage sums
  // 100 Physical vs 100 Physical + 19 Chaos (10-28 average).
  const auto r = score::compare(off, on, score::main_skills(off, p, db), p, db);
  CHECK(r.ratio.damage == doctest::Approx(1.19).epsilon(0.001));
}
