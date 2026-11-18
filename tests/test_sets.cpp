#include <doctest/doctest.h>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <string>

#include "game_db.h"
#include "gdc/score.h"
#include "gdc/sets.h"
#include "gdc/stat_model.h"

using namespace gdc;
namespace fs = std::filesystem;

namespace {

arz::Field nums(std::vector<double> v) {
  arz::Field f;
  f.kind = arz::Field::Kind::Float;
  f.numbers = std::move(v);
  return f;
}

arz::Field str(std::string v) {
  arz::Field f;
  f.kind = arz::Field::Kind::String;
  f.strings = {std::move(v)};
  return f;
}

snap::Item piece(int location, std::string record, std::string set) {
  snap::Item it;
  it.location = location;
  it.record = std::move(record);
  it.set = std::move(set);
  it.stats = snap::ItemStats{};
  return it;
}

const snap::Conversion* conversion(const snap::Snapshot& s, int from, int to) {
  for (const auto& c : s.conversions)
    if (c.from == from && c.to == to) return &c;
  return nullptr;
}

const snap::CombatTotals* damage_row(const snap::Snapshot& s, int type) {
  for (const auto& r : s.damage_modifiers)
    if (r.type == type) return &r;
  return nullptr;
}

constexpr const char* kGarden = "records/items/lootsets/itemset_d313.dbr";  // Worldkeeper's Garden

}  // namespace

TEST_CASE("sets: field arrays are indexed by piece count - 1, clamped to the last value; 0 pieces add nothing") {
  arz::Record r;
  r.fields.emplace("characterLifeModifier", nums({0, 0, 22}));
  r.fields.emplace("offensivePoisonModifier", nums({0, 120}));
  r.fields.emplace("characterLife", nums({50}));  // one value: every count
  r.fields.emplace("conversionInType", str("Life"));
  r.fields.emplace("conversionOutType", str("Poison"));
  r.fields.emplace("conversionPercentage", nums({0, 0, 0, 50}));
  r.fields.emplace("itemSkillName", str("records/skills/x.dbr"));
  r.fields.emplace("itemSkillLevel", nums({0, 0, 0, 1}));
  r.fields.emplace("itemSkillModifierControl", nums({0, 0, 0, 1}));
  r.fields.emplace("modifiedSkillName1", str("records/skills/pox1.dbr"));
  r.fields.emplace("modifierSkillName1", str("records/skills/set_pox.dbr"));
  r.fields.emplace("augmentSkillName1", str("records/skills/pox3.dbr"));
  r.fields.emplace("augmentSkillLevel1", nums({0, 0, 3}));
  r.fields.emplace("offensiveSlowPoisonMin", nums({0, 0, 0, 100}));
  r.fields.emplace("offensiveSlowPoisonDurationMin", nums({0, 0, 0, 5}));
  r.fields.emplace("offensiveSlowPoisonDurationModifier", nums({0, 0, 150}));

  CHECK(sets::bonus(r, 0).stats.empty());
  const auto b2 = sets::bonus(r, 2);
  using K = sets::Stats::key_type;
  CHECK(b2.stats.at(K{fields::Kind::Damage, 7, false}) == 120);
  CHECK(b2.stats.at(K{fields::Kind::Char, 4, false}) == 50);
  CHECK(!b2.stats.contains(K{fields::Kind::Char, 4, true}));
  CHECK(b2.conversions.empty());
  CHECK(b2.granted_skill.empty());
  CHECK(b2.modifiers.empty());
  CHECK(b2.skills.empty());

  const auto b5 = sets::bonus(r, 5);  // past every array: last values
  CHECK(b5.stats.at(K{fields::Kind::Char, 4, true}) == 22);
  CHECK(b5.stats.at(K{fields::Kind::Damage, 7, false}) == 120);
  REQUIRE(b5.conversions.size() == 1);
  CHECK(b5.conversions[0].from == 9);
  CHECK(b5.conversions[0].to == 7);
  CHECK(b5.conversions[0].percent == doctest::Approx(0.5));
  CHECK(b5.granted_skill == "records/skills/x.dbr");
  CHECK(b5.modifiers.size() == 1);
  CHECK(b5.skills.at("records/skills/pox3.dbr") == 3);
  REQUIRE(b5.flat.size() == 1);
  CHECK(b5.flat[0].type == 7);
  CHECK(b5.flat[0].dot);
  CHECK(b5.flat[0].min == 100);
  CHECK(b5.flat[0].duration == 5);
  CHECK(std::find(b5.unmodeled.begin(), b5.unmodeled.end(), "offensiveSlowPoisonDurationModifier") != b5.unmodeled.end());
}

TEST_CASE("sets: piece count counts distinct equipped records") {
  snap::Snapshot s;
  s.equipped = {piece(4, "records/r.dbr", "records/set.dbr"), piece(5, "records/r.dbr", "records/set.dbr"),
                piece(7, "records/h.dbr", "records/set.dbr"), piece(8, "records/c.dbr", "")};
  const auto n = sets::counts(s);
  REQUIRE(n.size() == 1);
  CHECK(n.at("records/set.dbr") == 2);
}

TEST_CASE("sets: completing and breaking Worldkeeper's Garden applies the real tier change") {
  const arz::Database* db = game_db();
  if (!db) return;  // needs the game database
  snap::Snapshot s;
  s.level = 94;
  s.defense_cap = 80;
  s.char_attributes = {{4, 10000, 22, 10000 / 1.22f, 0}};  // the 3pc +22% Life is in the sheet
  s.damage_modifiers = {{7, 0, 0, 0, 500, 0}};
  s.equipped = {piece(7, "records/items/gearhead/d316_head.dbr", kGarden),
                piece(9, "records/items/gearshoulders/d318_shoulder.dbr", kGarden),
                piece(8, "records/items/geartorso/d318_torso.dbr", kGarden)};
  // 3 -> 4: the medal completes the set. 3pc and 4pc share +22% Life, +120% Acid; 4pc adds 50% Vitality ->
  // Acid and grants Conjure Living Blossom.
  const auto medal = piece(-1, "records/items/gearaccessories/medals/d316_medal.dbr", kGarden);
  const auto up = model::swap(s, medal, 3, db);
  REQUIRE(up.sets.size() == 1);
  CHECK(up.sets[0].before == 3);
  CHECK(up.sets[0].after == 4);
  CHECK(up.sets[0].modeled);
  CHECK(up.sets[0].skill_gained == "records/skills/itemskillsgdx3/legendary/conjurelivingbloom.dbr");
  const auto* c = conversion(up.after, 9, 7);
  REQUIRE(c);
  CHECK(c->percent == doctest::Approx(0.5));
  CHECK(damage_row(up.after, 7)->damage_modifier == doctest::Approx(500));  // 120 both tiers
  CHECK(std::none_of(up.warnings.begin(), up.warnings.end(), [](const auto& w) { return w.find("set bonuses") != std::string::npos; }));

  // 4 -> 3 undoes it.
  const auto down = model::swap(up.after, piece(-1, "records/items/gearaccessories/necklaces/a01_necklace02.dbr", ""), 3, db);
  REQUIRE(down.sets.size() == 1);
  CHECK(down.sets[0].skill_lost == "records/skills/itemskillsgdx3/legendary/conjurelivingbloom.dbr");
  CHECK(conversion(down.after, 9, 7) == nullptr);

  // 2 -> 1 loses the 2pc +120% Acid; 3 -> 2 loses the 3pc +22% Life.
  snap::Snapshot two = s;
  two.equipped.pop_back();
  const auto one = model::swap(two, piece(-1, "records/items/gearshoulders/a02_shoulder01.dbr", ""), 9, db);
  CHECK(damage_row(one.after, 7)->damage_modifier == doctest::Approx(380));
  const auto life = model::swap(s, piece(-1, "records/items/geartorso/a02_torso004.dbr", ""), 8, db);
  CHECK(model::sheet(life.after).life == doctest::Approx(10000 / 1.22));
}

TEST_CASE("sets: score counts the set's flat damage at the equipped piece count") {
  const arz::Database* db = game_db();
  if (!db) return;
  // 4pc: +100 Poison per second for 5 s on weapon attacks (itemset_d313 offensiveSlowPoisonMin / DurationMin).
  snap::Snapshot s;
  s.level = 94;
  s.pth.minimum = 55;
  s.char_attributes = {{4, 10000, 0, 10000, 0}};
  s.skills.push_back({});
  s.skills.back().record = "records/skills/default/defaultweaponattack.dbr";
  s.skills.back().level = s.skills.back().current_level = 1;
  snap::Item weapon = piece(0, "records/items/w.dbr", "");
  weapon.stats->offensive_damage.push_back({2, false, 100, 100});
  s.equipped = {weapon, piece(7, "records/items/gearhead/d316_head.dbr", kGarden),
                piece(9, "records/items/gearshoulders/d318_shoulder.dbr", kGarden),
                piece(8, "records/items/geartorso/d318_torso.dbr", kGarden)};
  snap::Snapshot full = s;
  full.equipped.push_back(piece(3, "records/items/gearaccessories/medals/d316_medal.dbr", kGarden));
  score::Preset p;
  p.skills = {"records/skills/default/defaultweaponattack.dbr"};
  p.enemy_resist.clear();  // no enemy model: raw damage sums
  const auto r = score::compare(s, full, score::main_skills(s, p, db), p, db);
  // 100 Physical vs 100 Physical + 500 Poison (attribute factors 1 at 0 Cunning / Spirit).
  CHECK(r.ratio.damage == doctest::Approx(6.0).epsilon(0.01));
}
