#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>

#include "game_db.h"
#include "gdc/combat_formulas.h"
#include "gdc/components.h"
#include "gdc/enemy.h"
#include "gdc/score.h"
#include "score_internal.h"

using namespace gdc;
namespace fs = std::filesystem;

namespace {

std::string read(const fs::path& p) {
  std::ifstream f(p);
  REQUIRE(f.good());
  std::stringstream ss;
  ss << f.rdbuf();
  return ss.str();
}

snap::Snapshot base(int level) {
  snap::Snapshot s;
  s.level = static_cast<std::uint32_t>(level);
  s.defense_cap = 80;
  s.pth.minimum = 55;
  s.pth.crit_divisor_min = 100;
  s.char_attributes.push_back({1, 50, 0, 50, 0});      // Physique 50: life regen 0 (playerresourcebehavior.dbr)
  s.char_attributes.push_back({4, 1000, 0, 1000, 0});  // Life
  return s;
}

void defense(snap::Snapshot& s, int type, float value) {
  snap::CombatTotals r;
  r.type = type;
  r.defense = value;
  s.defense.push_back(r);
}

score::Preset threat(std::map<int, double> t) {
  score::Preset p;
  p.threat = std::move(t);
  p.resist_reduction = 0;  // none unless the test sets one
  return p;
}

}  // namespace

TEST_CASE("score: presets/default.toml is the built-in default") {
  std::string error;
  const auto p = score::parse(read(fs::path(GDC_SOURCE_DIR) / "presets/default.toml"), error);
  REQUIRE_MESSAGE(p, error);
  CHECK(*p == score::Preset{});
}

TEST_CASE("score: preset overrides, errors with line numbers") {
  std::string error;
  auto p = score::parse("[score]\ndamage = 2\n[survival.threat]\nfire = 1\n[damage]\nskills = [\"a.dbr\"]\n", error);
  REQUIRE_MESSAGE(p, error);
  CHECK(p->damage_weight == 2);
  CHECK(p->survival_weight == 1);
  CHECK(p->threat == std::map<int, double>{{6, 1.0}});
  CHECK(p->skills == std::vector<std::string>{"a.dbr"});

  CHECK_FALSE(score::parse("[score]\ndamag = 1\n", error));
  CHECK(error == "line 2: unknown key score.damag");
  CHECK_FALSE(score::parse("[score]\n\nother = \"x\"\n", error));
  CHECK(error == "line 3: other must be a number");
  CHECK_FALSE(score::parse("[score]\nother = -1\n", error));
  CHECK(error.starts_with("line 2: other must be in [0"));
  CHECK_FALSE(score::parse("[damage]\nauto_skills = 2.5\n", error));
  CHECK_FALSE(score::parse("[survival.threat]\nfire = 0\n", error));
  CHECK_FALSE(score::parse("[stage]\n", error));
  CHECK_FALSE(score::parse("[score\n", error));
  CHECK(error.starts_with("line 1: "));
}

TEST_CASE("score: survivability from resists, the cap and resist reduction") {
  const auto p = threat({{5, 1}, {6, 1}});  // cold, fire
  snap::Snapshot b = base(10), a = base(10);
  defense(b, 6, 50);
  defense(a, 6, 70);
  // taken: before (0.5 + 1) / 2, after (0.3 + 1) / 2
  auto r = score::compare(b, a, {}, p, nullptr);
  CHECK(r.ratio.survival == doctest::Approx(0.75 / 0.65));
  CHECK(r.ratio.damage == 1);
  CHECK(r.percent == doctest::Approx((0.75 / 0.65 - 1) * 100));
  a.defense[0].defense = 100;  // capped at 80
  CHECK(score::compare(b, a, {}, p, nullptr).ratio.survival == doctest::Approx(0.75 / 0.6));
  auto q = p;
  q.resist_reduction = 30;  // fire 100 - 30 = 70 (under the cap) vs 50 - 30 = 20; cold 0 - 30
  CHECK(score::compare(b, a, {}, q, nullptr).ratio.survival == doctest::Approx((0.8 + 1.3) / (0.3 + 1.3)));

  // Only the 9 base resists are lowered: physical and stun keep their sheet value.
  auto ph = threat({{2, 1}});
  ph.resist_reduction = 30;
  ph.thresholds = {{"stun_resist", 50}};
  snap::Snapshot pb = base(10), pa = base(10);
  defense(pb, 2, 10);
  defense(pa, 2, 20);
  defense(pb, 42, 40);
  defense(pa, 42, 40);
  auto none = ph;
  none.resist_reduction = 0;
  const auto with = score::compare(pb, pa, {}, ph, nullptr), without = score::compare(pb, pa, {}, none, nullptr);
  CHECK(with.ratio.survival == doctest::Approx(without.ratio.survival));
  CHECK(with.before.find("stun_resist 40/50") != std::string::npos);
}

TEST_CASE("score: difficulty lookahead counts overcap up to the next difficulty's extra penalty") {
  auto p = threat({{6, 1}});  // fire
  auto ratio = [&](int level, int difficulty, int type, const score::Preset& q) {
    snap::Snapshot b = base(level), a = base(level);
    b.difficulty = a.difficulty = difficulty;
    defense(b, type, 80);
    defense(a, type, 105);
    return score::compare(b, a, {}, q, nullptr).ratio.survival;
  };
  // Normal, Elite from 75: -25 Fire there. Level 75: 80 -> 55 taken 0.45, 105 -> 80 taken 0.2.
  CHECK(ratio(75, 0, 6, p) == doctest::Approx(0.45 / 0.2));
  CHECK(ratio(90, 0, 6, p) == doctest::Approx(0.45 / 0.2));
  CHECK(ratio(70, 0, 6, p) == doctest::Approx((0.5 * 0.2 + 0.5 * 0.45) / 0.2));  // half way into the ramp
  CHECK(ratio(65, 0, 6, p) == doctest::Approx(1));
  // Elite adds nothing to Vitality; Ultimate adds 25 to it (playerAttributePak), from 94.
  auto life = threat({{9, 1}});
  CHECK(ratio(80, 0, 9, life) == doctest::Approx(1));
  CHECK(ratio(94, 1, 9, life) == doctest::Approx(0.45 / 0.2));
  CHECK(ratio(94, 1, 6, p) == doctest::Approx(0.45 / 0.2));
  CHECK(ratio(100, 2, 6, p) == doctest::Approx(1));  // Ultimate: nothing ahead
  auto off = p;
  off.difficulty_lookahead = false;
  CHECK(ratio(80, 0, 6, off) == doctest::Approx(1));
}

TEST_CASE("score: auto resist reduction mixes in the bosses' RR by boss_share") {
  auto p = threat({{6, 1}});
  p.resist_reduction.reset();
  snap::Snapshot b = base(100), a = base(100);
  b.difficulty = a.difficulty = 2;  // Ultimate: no lookahead
  defense(b, 6, 80);
  defense(a, 6, 110);
  const enemy::Enemy e = enemy::at(100, 4);
  auto taken = [&](double resist, double boss) {
    auto f = [&](double rr) { return 1 - std::min(resist - rr, 80.0) / 100; };
    return ((1 - boss) * (1 - e.rr_share) + boss * (1 - e.boss_rr_share)) * f(0) + (1 - boss) * e.rr_share * f(e.rr) +
           boss * e.boss_rr_share * f(e.boss_rr);
  };
  CHECK(score::compare(b, a, {}, p, nullptr).ratio.survival == doctest::Approx(taken(80, 0.15) / taken(110, 0.15)));
  auto none = p;
  none.boss_share = 0;
  const double with = score::compare(b, a, {}, p, nullptr).ratio.survival;
  CHECK(score::compare(b, a, {}, none, nullptr).ratio.survival < with);  // bosses make overcap worth more
  auto fixed = p;
  fixed.resist_reduction = 25;  // the preset number replaces both
  CHECK(score::compare(b, a, {}, fixed, nullptr).ratio.survival == doctest::Approx(0.45 / 0.2));
  // From Ultimate on only: below it the auto mix and the lookahead, never both.
  fixed.resist_reduction_from = 2;
  CHECK(score::compare(b, a, {}, fixed, nullptr).ratio.survival == doctest::Approx(0.45 / 0.2));
  snap::Snapshot eb = b, ea = a;
  eb.difficulty = ea.difficulty = 1;  // Elite 100: lookahead 1, Fire -25 ahead
  CHECK(score::compare(eb, ea, {}, fixed, nullptr).ratio.survival ==
        doctest::Approx(score::compare(eb, ea, {}, p, nullptr).ratio.survival));
  fixed.resist_reduction_from = 0;
  CHECK(score::compare(eb, ea, {}, fixed, nullptr).ratio.survival == doctest::Approx(0.7 / 0.4));  // 25 + 25
  std::string error;
  CHECK_FALSE(score::parse("[survival]\nresist_reduction_from = \"hard\"\n", error));
  CHECK_FALSE(score::parse("[survival]\ndifficulty_lookahead = 1\n", error));
  const auto parsed = score::parse("[survival]\ndifficulty_lookahead = false\nboss_share = 0.4\n", error);
  REQUIRE_MESSAGE(parsed, error);
  CHECK_FALSE(parsed->difficulty_lookahead);
  CHECK(parsed->boss_share == 0.4);
  CHECK(parsed->resist_reduction_from == 0);
  const auto from = score::parse("[survival]\nresist_reduction = 25\nresist_reduction_from = \"ultimate\"\n", error);
  REQUIRE_MESSAGE(from, error);
  CHECK(from->resist_reduction_from == 2);
}

TEST_CASE("score: armor absorbs absorption% of up to its value per physical hit, in its hit region, after the resist") {
  auto p = threat({{2, 1}});
  p.enemy_hit_per_level = 40;  // hit = 40 x 10 = 400
  snap::Snapshot b = base(10), a = base(10);
  snap::Snapshot c = a;
  snap::Item chest;
  chest.location = 8;
  chest.stats.emplace();
  snap::CombatTotals armor;
  armor.type = 38;
  armor.defense = 1000;  // the torso is hit 26% of the time (combatRegionTorsoChance); 1000 > the hit
  chest.stats->defense.push_back(armor);
  a.equipped.push_back(chest);
  defense(a, 39, 70);  // absorption 70%
  const double f = 1 - 0.26 * 0.7;
  CHECK(score::compare(b, a, {}, p, nullptr).ratio.survival == doctest::Approx(1 / f));
  // No absorption row: every armor entry carries gameengine.dbr armorDefensiveAbsorption (engine, traced in
  // stat_model.cpp); without a database it is unknown and armor absorbs nothing.
  c.equipped.push_back(chest);
  CHECK(score::compare(b, c, {}, p, nullptr).ratio.survival == doctest::Approx(1));
  if (const arz::Database* db = game_db()) CHECK(score::compare(b, c, {}, p, db).ratio.survival == doctest::Approx(1 / f));
  a.defense.back().defense = 50;
  CHECK(score::compare(b, a, {}, p, nullptr).ratio.survival == doctest::Approx(1 / (1 - 0.26 * 0.5)));
  // Armor below the hit absorbs absorption% of the armor value only (physicalDamageDefenseEquationDGP).
  a.equipped[0].stats->defense[0].defense = 100;
  CHECK(score::compare(b, a, {}, p, nullptr).ratio.survival == doctest::Approx(400 / (400 - 0.26 * 0.5 * 100)));
  // The physical resist comes first (BasePhysical::ReduceDamage 0x1175F0): 50% leaves 200, of which the
  // armor (300 > 200) absorbs half in the torso.
  a.equipped[0].stats->defense[0].defense = 300;
  snap::CombatTotals phys;
  phys.type = 2;
  phys.defense = phys.processed = 50;
  phys.capped = true;
  a.defense.push_back(phys);
  CHECK(score::compare(b, a, {}, p, nullptr).ratio.survival == doctest::Approx(400 / (200 - 0.26 * 0.5 * 200)));
  // More armor never hurts, and past the (resisted) hit it adds nothing.
  auto more = a;
  more.equipped[0].stats->defense[0].defense = 5000;
  CHECK(score::compare(a, more, {}, p, nullptr).ratio.survival == doctest::Approx(1));
}

TEST_CASE("score: enemy OA against DA: misses, glancing hits and crit tiers (combatformulas.dbr table)") {
  auto p = threat({{6, 1}});
  p.enemy_oa_per_level = 100;  // enemy OA 1000
  snap::Snapshot b = base(10), a = base(10);
  const float thr[] = {70, 90, 105, 120, 130, 135}, mod[] = {1.0f, 1.1f, 1.2f, 1.3f, 1.4f, 1.5f};
  for (auto* s : {&b, &a})
    for (int i = 0; i < 6; ++i) s->pth.threshold[i] = thr[i], s->pth.damage_modifier[i] = mod[i];
  // DA = (value + 12 x level + Physique / 2) + 53 (defensiveAbilityEquation): 198 without, 1198 with +1000.
  a.char_attributes.push_back({23, 1000, 0, 0, 0});
  const double pth_b = formulas::probability_to_hit(1000, 198), pth_a = formulas::probability_to_hit(1000, 1198);
  CHECK(pth_b > 105);  // crits on the weak side
  const double expect = formulas::expected_hit(pth_b, b.pth, 0).multiplier / formulas::expected_hit(pth_a, a.pth, 0).multiplier;
  const auto r = score::compare(b, a, {}, p, nullptr);
  CHECK(r.ratio.survival == doctest::Approx(expect));
  CHECK(r.ratio.survival > 1.1);
  REQUIRE(!r.contributions.empty());
  CHECK(r.contributions[0].label == "DA (enemy hits and crits)");
  CHECK(r.contributions[0].points == doctest::Approx(std::log(expect) * 100));
}

TEST_CASE("score: dodge / deflect, block and the enemy damage reduction debuff are stages of Survivability") {
  auto p = threat({{6, 1}});
  snap::Snapshot b = base(10), a = base(10);
  a.char_attributes.push_back({27, 20, 0, 0, 0});  // 20% dodge on the melee half, no deflect
  CHECK(score::compare(b, a, {}, p, nullptr).ratio.survival == doctest::Approx(1 / (1 - 0.5 * 0.20)));
  snap::Item ring;
  ring.location = 4;
  ring.stats.emplace();
  ring.stats->offensive_damage = {{28, true, 20, 20, 3}};  // 20% reduced target's damage
  a = base(10);
  a.equipped.push_back(ring);
  const auto r = score::compare(b, a, {}, p, nullptr);
  CHECK(r.ratio.survival == doctest::Approx(1 / (1 - 0.5 * 0.20)));  // damage_reduction_uptime 0.5
  REQUIRE(r.contributions.size() == 1);
  CHECK(r.contributions[0].label == "Enemy damage reduction");
  // The parts add up to the total.
  double sum = 0;
  for (const auto& c : r.contributions) sum += c.points;
  CHECK(sum == doctest::Approx(std::log(1 + r.percent / 100) * 100));
}

TEST_CASE("score: other = 1 + weighted utility stats; conditions; unlisted stats at the default weight") {
  snap::Snapshot b = base(10), a = base(10);
  a.char_attributes.push_back({14, 1.1f, 0, 0, 0});  // run speed 110%
  defense(a, 42, 50);                                 // stun resist 50%
  a.char_attributes.push_back({56, 20, 0, 0, 0});    // Increased gold 20%: no key -> default
  score::Preset p;
  for (auto& [k, w] : p.other) w.weight = 0;
  p.other["run_speed"].weight = 0.5;
  p.other["stun_resist"].weight = 0.1;
  p.other["default"].weight = 0.05;
  const double other = 1 + 0.5 * 1.1 + 0.1 * 0.5 + 0.05 * 0.2;
  const auto r = score::compare(b, a, {}, p, nullptr);
  CHECK(r.ratio.other == doctest::Approx(other));
  CHECK(r.percent == doctest::Approx((std::pow(other, 0.25) - 1) * 100));
  auto listed = [](const score::Result& x, const score::OtherChange& want) {
    return std::any_of(x.other_changes.begin(), x.other_changes.end(), [&](const score::OtherChange& o) {
      return o.key == want.key && o.before == doctest::Approx(want.before) && o.after == doctest::Approx(want.after) &&
             o.weight == doctest::Approx(want.weight);
    });
  };
  CHECK(listed(r, {"default: char 56 IncreasedGold", 0, 0.2, 0.05}));
  // A condition that does not hold: weight 0, still listed.
  p.other["stun_resist"].when = {"shield"};
  const auto c = score::compare(b, a, {}, p, nullptr);
  CHECK(c.ratio.other == doctest::Approx(other - 0.05));
  CHECK(listed(c, {"stun_resist", 0, 0.5, 0}));
  p.other["stun_resist"].when = {"!shield"};
  CHECK(score::compare(b, a, {}, p, nullptr).ratio.other == doctest::Approx(other));
}

TEST_CASE("score: [other] keys parse as numbers or { weight, when }") {
  std::string error;
  auto p = score::parse("[other]\npet_bonus = 0.3\nrun_speed = { weight = 0.2, when = [\"shield\", \"weapon:WeaponMelee_Sword\"] }\n", error);
  REQUIRE_MESSAGE(p, error);
  CHECK(p->other["pet_bonus"] == score::Weight{0.3, {"pets"}});  // a number keeps the default condition
  CHECK(p->other["run_speed"] == score::Weight{0.2, {"shield", "weapon:WeaponMelee_Sword"}});
  // Stages of Survivability, no longer Other keys (0.9.0).
  CHECK_FALSE(score::parse("[other]\nblock = 1\n", error));
  CHECK(error == "line 2: unknown key other.block");
  CHECK_FALSE(score::parse("[other]\ncc_resist = 1\n", error));
  CHECK(error == "line 2: unknown key other.cc_resist");
  CHECK_FALSE(score::parse("[other]\nrun_speed = { weight = 1, when = \"sometimes\" }\n", error));
  CHECK(error.find("run_speed.when") != std::string::npos);
  CHECK_FALSE(score::parse("[other]\nrun_speed = { when = \"shield\" }\n", error));
  CHECK(error == "line 2: run_speed needs weight");
  // Stages: the stage's name is what "stage:<name>" tests.
  p = score::parse("[[stage]]\nname = \"late\"\n[stage.other]\nexperience = { weight = 1, when = \"stage:late\" }\n", error);
  REQUIRE_MESSAGE(p, error);
  CHECK(p->stages[0].settings.stage == "late");
  CHECK(p->stage.empty());
}

TEST_CASE("components: strip subtracts component stats; attach adds them back") {
  snap::Item it;
  it.location = 4;
  it.record = "ring.dbr";
  it.replica.emplace();
  it.replica->relic = "relic.dbr";
  it.stats.emplace();
  it.stats->char_attributes = {{4, 270, 0}, {21, 10, 0}};
  snap::CombatTotals cold;
  cold.type = 5;
  cold.defense = 10;
  it.stats->defense = {cold};
  it.relic.emplace();
  it.relic->record = "relic.dbr";
  it.relic->stats.emplace();
  it.relic->stats->char_attributes = {{4, 70, 0}};
  it.relic->stats->defense = {cold};
  std::vector<std::string> notes;
  const snap::Item b = components::strip(it, nullptr, notes);
  CHECK_FALSE(b.relic);
  CHECK(b.replica->relic.empty());
  REQUIRE(b.stats->char_attributes.size() == 2);
  CHECK(b.stats->char_attributes[0].value == 200);
  CHECK(b.stats->defense.empty());
  CHECK(notes.size() == 1);  // no database: flat damage not removed
  const snap::Item m = components::attach(b, it, true, true, nullptr, notes);
  REQUIRE(m.relic);
  CHECK(m.replica->relic == "relic.dbr");
  CHECK(m.stats->char_attributes[0].value == 270);
  REQUIRE(m.stats->defense.size() == 1);
  CHECK(m.stats->defense[0].defense == 10);
}

TEST_CASE("components: an artifact keeps its own completion bonus; a component's bonus record moves with it") {
  snap::Item art;
  art.location = 11;
  art.record = "records/items/crafting/blueprints/relic/d016_relic.dbr";
  art.replica.emplace();
  art.replica->relic_bonus = "records/items/lootaffixes/completionrelics/aocc_20a.dbr";
  art.stats.emplace();
  std::vector<std::string> notes;
  const snap::Item a = components::strip(art, nullptr, notes);
  CHECK(a.replica->relic_bonus == art.replica->relic_bonus);
  CHECK(notes.empty());

  snap::Item donor;
  donor.record = "ring.dbr";
  donor.replica.emplace();
  donor.replica->relic = "relic.dbr";
  donor.replica->relic_bonus = "bonus.dbr";
  donor.stats.emplace();
  donor.relic.emplace();
  donor.relic->record = "relic.dbr";
  donor.relic->stats.emplace();
  const snap::Item bare = components::strip(donor, nullptr, notes);
  CHECK(bare.replica->relic_bonus.empty());
  const snap::Item m = components::attach(bare, donor, true, false, nullptr, notes);
  CHECK(m.replica->relic_bonus == "bonus.dbr");
  // Unknown bonus record: assumed to have stats, which do not move.
  CHECK(std::count_if(notes.begin(), notes.end(), [](const auto& n) { return n.find("bonus.dbr") != std::string::npos; }) == 2);
}

TEST_CASE("components and score on the installed game (skips if absent)") {
  const arz::Database* db = game_db();
  if (!db) {
    MESSAGE("game not found; skipped");
    return;
  }
  const std::string heart = "records/items/materia/compa_frozenheart.dbr", ichor = "records/items/materia/compa_mutagenicichor.dbr";
  CHECK(components::fits(heart, "", 6, *db));        // ring 1
  CHECK_FALSE(components::fits(heart, "", 1, *db));  // head 0
  CHECK_FALSE(components::fits(heart, "", 9, *db));  // weapons: not mapped
  // Mutagenic Ichor: shield 1, offhand 1, head 0. Type 8 goes by the item's class; an unknown item does not fit.
  CHECK(components::fits(ichor, "records/items/gearweapons/shields/c013_shield.dbr", 8, *db));
  CHECK(components::fits(ichor, "records/items/gearweapons/focus/c028_focus.dbr", 8, *db));
  CHECK_FALSE(components::fits(ichor, "", 8, *db));
  CHECK_FALSE(components::fits(ichor, "", 1, *db));

  // lvl96 Witch Hunter: Shadow Strike (16 points + 12 in its modifier shadowstrike_mod1 = 28), Doom Bolt (12). Not counted: Curse of Frailty (its buff record
  // has no damage), weapon pool attacks (Skill_WPAttack_BasicAttack, not an attack skill class).
  const auto s = snap::from_json(read(fs::path(GDC_SOURCE_DIR) / "tests/fixtures/snapshots/lvl96-s5-before-ring-c031.json"));
  const auto skills = score::main_skills(s, score::Preset{}, db);
  // Both have cooldowns: the default weapon attack fills in with half the weight.
  REQUIRE(skills.size() == 3);
  CHECK(skills[0].record == "records/skills/playerclass04/shadowstrike.dbr");
  CHECK(skills[0].weight == doctest::Approx(0.5 * 28 / 40));
  CHECK(skills[1].record == "records/skills/playerclass03/doombolt1.dbr");
  CHECK(skills[2].record == "records/skills/default/defaultweaponattack.dbr");
  CHECK(skills[2].weight == doctest::Approx(0.5));
  // Listed skills: only the learned ones; none learned picks as auto does.
  score::Preset listed;
  listed.skills = {"records/skills/playerclass03/doombolt1.dbr", "records/skills/playerclass05/aetherray1.dbr"};
  const auto some = score::main_skills(s, listed, db);
  REQUIRE(some.size() == 1);
  CHECK(some[0].record == "records/skills/playerclass03/doombolt1.dbr");
  listed.skills = {"records/skills/playerclass05/aetherray1.dbr"};
  const auto fallback = score::main_skills(s, listed, db);
  REQUIRE(fallback.size() == skills.size());
  for (std::size_t i = 0; i < skills.size(); ++i) CHECK(fallback[i].record == skills[i].record);

  // More chaos damage on a ring raises Damage; nothing else moves.
  auto a = s;
  for (auto& r : a.damage_modifiers)
    if (r.type == 10) r.damage_modifier += 100;
  const auto r = score::compare(s, a, skills, score::Preset{}, db);
  CHECK(r.ratio.damage > 1);
  CHECK(r.ratio.survival == 1);
  CHECK(r.percent > 0);
  MESSAGE(r.before);

  // Attack speed counts through the default attack only.
  auto fast = s;
  for (auto& c : fast.char_attributes)
    if (c.type == 15) c.modifier += 10;
  const auto f = score::compare(s, fast, skills, score::Preset{}, db);
  CHECK(f.ratio.damage == doctest::Approx(1 + 0.5 * 10 / (100 + 88.0)).epsilon(0.02));
}

TEST_CASE("score: conversion_warn: added conversions away from the target types") {
  std::string error;
  auto p = score::parse("[match]\nmasteries = [\"a.dbr\"]\ndamage = [\"acid\", \"poison\"]\n"
                        "[damage]\nconversion_warn = [\"vitality->*\", \"*->chaos\"]\n", error);
  REQUIRE_MESSAGE(p, error);
  CHECK(p->conversion_warn == std::vector<std::pair<int, int>>{{9, 0}, {0, 10}});
  snap::Snapshot before, after;
  before.conversions = {{9, 10, 0.25f}};
  after.conversions = {{9, 10, 0.75f}, {9, 7, 0.5f}, {6, 10, 0.2f}, {6, 5, 1.0f}};
  const auto w = score::conversion_warnings(before, after, *p, p->match_damage, nullptr);
  REQUIRE(w.size() == 2);  // Vitality -> Acid/Poison is a target; Fire -> Cold is not listed
  CHECK(w[0].from == 6);
  CHECK(w[0].to == 10);
  CHECK(w[0].percent == doctest::Approx(20));
  CHECK(w[1].from == 9);
  CHECK(w[1].percent == doctest::Approx(50));  // only what the swap adds
  CHECK(score::conversion_label(7) == "Acid/Poison");
  CHECK(score::conversion_warnings(before, after, score::Preset{}, {}, nullptr).empty());  // no list: no warning

  // DB "Poison" is Acid in game: "acid" names the same type (7); freeze writes "acid".
  CHECK(score::parse("[damage]\nconversion_warn = [\"acid->*\", \"poison->vitality\"]\n", error)->conversion_warn ==
        std::vector<std::pair<int, int>>{{7, 0}, {7, 9}});
  CHECK_FALSE(score::parse("[damage]\nconversion_warn = [\"bleeding->*\"]\n", error));
  CHECK(error.starts_with("line 2: conversion_warn: \"from->to\""));
  CHECK_FALSE(score::parse("[damage]\nconversion_warn = [\"vitality\"]\n", error));
  CHECK_FALSE(score::parse("[damage]\nconversion_warn = \"vitality->*\"\n", error));
}

TEST_CASE("score: threshold curve: steep below, flat above, smooth at the target") {
  score::Settings p;  // below 1, above 0.05, width 0.05
  const double at = score::threshold_factor(80, 80, p);
  // Well below: slope ~1 per relative step; well above: ~0.05.
  CHECK(std::log(score::threshold_factor(40, 80, p) / score::threshold_factor(48, 80, p)) == doctest::Approx(-0.1).epsilon(0.01));
  CHECK(std::log(score::threshold_factor(160, 80, p) / score::threshold_factor(168, 80, p)) == doctest::Approx(-0.005).epsilon(0.01));
  // No step: 1% around the target moves the factor by less than 1%.
  CHECK(std::abs(score::threshold_factor(80.8, 80, p) / at - 1) < 0.01);
  CHECK(std::abs(score::threshold_factor(79.2, 80, p) / at - 1) < 0.01);
  CHECK(score::threshold_factor(50, 0, p) == 1);  // no target

  std::string error;
  auto pr = score::parse("[thresholds]\nresist = 80\nstun_resist = 75\n[[stage]]\nname = \"x\"\n[stage.thresholds]\nstun_resist = 0\nlife = 16000\n", error);
  REQUIRE_MESSAGE(pr, error);
  CHECK(pr->thresholds == std::map<std::string, double>{{"resist", 80}, {"stun_resist", 75}});
  CHECK(pr->stages[0].settings.thresholds == std::map<std::string, double>{{"resist", 80}, {"life", 16000}});
  CHECK_FALSE(score::parse("[thresholds]\nresists = 80\n", error));
  CHECK(error == "line 2: unknown key thresholds.resists");
}

TEST_CASE("score: thresholds weigh a resist gain below the target more than above it") {
  snap::Snapshot before;
  before.level = 90;
  before.defense_cap = 80;
  before.char_attributes = {{4, 10000, 0, 10000, 0}};
  for (int t : {4, 5, 6, 7, 8, 9, 10, 11, 15}) before.defense.push_back({t, 60, 0, 0, 0, 0, 60, 0});
  auto plus = [](snap::Snapshot s, int type, float v) {
    for (auto& r : s.defense)
      if (r.type == type) r.defense = r.processed = r.defense + v;
    return s;
  };
  score::Preset p;
  p.thresholds = {{"resist", 80}};
  score::Preset none;
  const double with = score::compare(before, plus(before, 9, 10), {}, p, nullptr).ratio.survival;
  const double without = score::compare(before, plus(before, 9, 10), {}, none, nullptr).ratio.survival;
  CHECK(with > without);  // 60 -> 70 Vitality is below the 80 target: extra weight
}

// Two tiers: a base resist under the cap outweighs any crowd-control resist; the second tier is bounded.
TEST_CASE("score: second-tier thresholds are bounded and their targets scale with level") {
  auto p = threat({{8, 1}});
  p.thresholds = {{"resist", 80}, {"stun_resist", 75}};
  snap::Snapshot b = base(39), a = base(39);
  defense(b, 8, 80);
  defense(a, 8, 64);  // Lightning 16 under the cap
  defense(b, 42, 26);
  defense(a, 42, 38);  // stun resist +12, far under 75
  const auto r = score::compare(b, a, {}, p, nullptr);
  CHECK(r.ratio.survival < 1);
  auto steep = p;
  steep.threshold_secondary_weight = 1;  // the old full-weight term
  CHECK(score::compare(b, a, {}, steep, nullptr).ratio.survival > r.ratio.survival);
  // 0 -> 75 alone: at most about 10%.
  snap::Snapshot none = base(39), full = base(39);
  defense(none, 8, 80);
  defense(full, 8, 80);
  defense(full, 42, 75);
  const double gain = score::compare(none, full, {}, p, nullptr).ratio.survival;
  CHECK(gain > 1);
  CHECK(gain < 1.12);
  // threshold_level: target x level / threshold_level below it; absorption keeps its value.
  auto lv = p;
  lv.threshold_level = 94;
  lv.thresholds["absorption"] = 90;
  const auto scaled = score::compare(b, a, {}, lv, nullptr);
  CHECK(scaled.targets.at("stun_resist") == doctest::Approx(75.0 * 39 / 94));
  CHECK(scaled.targets.at("absorption") == 90);
  CHECK_FALSE(scaled.targets.contains(""));
  snap::Snapshot b94 = b, a94 = a;
  b94.level = a94.level = 94;
  CHECK(score::compare(b94, a94, {}, lv, nullptr).targets.at("stun_resist") == 75);
  std::string error;
  const auto parsed = score::parse("[score]\nthreshold_secondary_weight = 0.3\nthreshold_level = 94\n", error);
  REQUIRE_MESSAGE(parsed, error);
  CHECK(parsed->threshold_secondary_weight == 0.3);
  CHECK(parsed->threshold_level == 94);
  CHECK_FALSE(score::parse("[score]\nthreshold_level = 101\n", error));
  CHECK_FALSE(score::parse("[score]\nthreshold_secondary_weight = -1\n", error));
}

// ConvertCombatAttributes 0x1197B0: the copy's damage and its % are both scaled by the share.
TEST_CASE("score: conversion carries a part's skill % scaled by its share squared") {
  std::vector<std::string> notes;
  using A = score::Amounts;
  const A bonus{{{6, false}, 40}};  // 100 Fire with +40% skill %: +40
  A out_bonus;
  const A out = score::convert_pairs({{{6, false}, 100}}, {}, {{6, {{5, 50.0}}}}, notes, &bonus, &out_bonus);
  CHECK(out.at({5, false}) == doctest::Approx(50));
  CHECK(out.at({6, false}) == doctest::Approx(50));
  CHECK(out_bonus.at({5, false}) == doctest::Approx(10));  // 40 x 0.5^2
  CHECK(out_bonus.at({6, false}) == doctest::Approx(10));
  A elemental;
  score::convert_pairs({{{2, false}, 90}}, {}, {{2, {{58, 30.0}}}}, notes, &bonus, &elemental);
  CHECK(elemental.empty());  // the bonus is Fire's; Physical had none
  const A phys{{{2, false}, 90}};
  score::convert_pairs({{{2, false}, 90}}, {}, {{2, {{58, 30.0}}}}, notes, &phys, &elemental);
  CHECK(elemental.at({5, false}) == doctest::Approx(90 * 0.1 * 0.1));  // a third of 30%: (0.1)^2 each
  CHECK(elemental.at({2, false}) == doctest::Approx(90 * 0.7 * 0.7));
}

TEST_CASE("score: offtarget_weight scales the types that are not and cannot become [match] damage") {
  using namespace combat_attr;
  score::Settings p;
  p.offtarget_weight = 0.2;
  CHECK(score::type_weight(p, {kBleeding, true}) == 1);  // no [match] damage: every type is the target
  p.match_damage = {{kPoison, false}, {kPoison, true}};  // Acid, Poison
  CHECK(score::type_weight(p, {kBleeding, true}) == 0.2);  // nothing converts into or out of Bleeding
  CHECK(score::type_weight(p, {kBleeding, false}) == 0.2);
  CHECK(score::type_weight(p, {kLife, true}) == 1);  // Vitality Decay -> Poison
  CHECK(score::type_weight(p, {kFire, false}) == 1);
  CHECK(score::type_weight(p, {kPoison, true}) == 1);
  p.match_damage = {{kChaos, false}};
  CHECK(score::type_weight(p, {kFire, true}) == 0.2);  // DoT stays DoT and never becomes Chaos
  CHECK(score::type_weight(p, {kFire, false}) == 1);
  p.offtarget_weight = 1;
  CHECK(score::type_weight(p, {kFire, true}) == 1);

  // plague-garden: 1.0 while levelling, 0.5 from 35, 0.2 from 94; every stage keeps [match] damage.
  std::string error;
  const auto pg = score::parse(read(fs::path(GDC_SOURCE_DIR) / "presets/plague-garden.toml"), error);
  REQUIRE_MESSAGE(pg, error);
  CHECK(pg->offtarget_weight == 1);
  for (const auto& st : pg->stages) {
    CAPTURE(st.name);
    CHECK(st.settings.match_damage == pg->match_damage);
    CHECK(st.settings.offtarget_weight == (st.name == "35-93" ? 0.5 : st.name == "94+" ? 0.2 : 1.0));
  }
  CHECK_FALSE(score::parse("[damage]\nofftarget_weight = 1.5\n", error));
}

TEST_CASE("score: a shield's base damage is not weapon damage (installed game; skips if absent)") {
  const arz::Database* db = game_db();
  if (!db) return;
  // c009_shield: offensiveBaseCold / Fire / LightningMin 58 (no Max: 58-58); its getter reports them as flat
  // Cold / Fire / Lightning, plus an affix's +10 Fire. Only the +10 is flat damage for a weapon attack.
  snap::Snapshot s = base(94);
  snap::Item shield;
  shield.location = 1;
  shield.record = "records/items/awakened/gearweapons/shields/c009_shield.dbr";
  shield.stats = snap::ItemStats{};
  shield.stats->offensive_damage = {{5, false, 58, 58}, {6, false, 68, 68}, {8, false, 58, 58}, {1, false, 100, 120}};
  snap::Item sword;  // the weapon attack's own damage
  sword.location = 0;
  sword.record = "records/items/awakened/gearweapons/swords1h/c027_sword.dbr";
  sword.stats = snap::ItemStats{};
  sword.stats->offensive_damage = {{1, false, 100, 120}};
  s.equipped = {sword};
  snap::Snapshot without = s;
  s.equipped.push_back(shield);
  snap::Skill attack;
  attack.id = 1;
  attack.record = "records/skills/default/defaultweaponattack.dbr";
  attack.level = attack.current_level = 1;
  s.skills = without.skills = {attack};
  score::Preset p;
  p.skills = {attack.record};
  const auto skills = score::main_skills(s, p, db);
  CHECK(score::compare(without, s, skills, p, db).ratio.damage > 1);  // the +10 Fire
  snap::Snapshot only_base = s;
  only_base.equipped[1].stats->offensive_damage = {{5, false, 58, 58}, {6, false, 58, 58}, {8, false, 58, 58}, {1, false, 100, 120}};
  CHECK(score::compare(without, only_base, skills, p, db).ratio.damage == doctest::Approx(1));
}

TEST_CASE("score: convert as the engine") {
  std::vector<std::string> notes;
  using M = std::map<score::DamageKey, double>;
  auto near = [](const M& got, const M& want) {
    for (const auto& [k, v] : want) {
      CAPTURE(k.first);
      CAPTURE(k.second);
      CHECK(got.contains(k));
      if (got.contains(k)) CHECK(got.at(k) == doctest::Approx(v));
    }
    double a = 0, b = 0;
    for (const auto& [_, v] : got) a += v;
    for (const auto& [_, v] : want) b += v;
    CHECK(a == doctest::Approx(b));  // nothing lost or made
  };
  SUBCASE("Elemental in is a third each, so Fire -> X takes its fire part") {
    near(score::convert({{{58, false}, 300}}, {}, {{6, 10, 0.5f}}, notes),
         {{{5, false}, 100}, {{6, false}, 50}, {{10, false}, 50}, {{8, false}, 100}});
  }
  SUBCASE("into Elemental: a third each") {
    near(score::convert({{{2, false}, 90}}, {}, {{2, 58, 0.3f}}, notes),
         {{{2, false}, 63}, {{5, false}, 9}, {{6, false}, 9}, {{8, false}, 9}});
  }
  SUBCASE("converted damage is not converted again") {
    near(score::convert({{{10, false}, 100}}, {}, {{10, 9, 0.5f}, {9, 7, 1.0f}}, notes),
         {{{10, false}, 50}, {{9, false}, 50}});
  }
  SUBCASE("skill level first, the character's on the rest") {
    near(score::convert({{{6, false}, 100}}, {{6, {{5, 50.0}}}}, {{5, 8, 1.0f}, {6, 8, 1.0f}}, notes),
         {{{5, false}, 50}, {{8, false}, 50}});
  }
  SUBCASE("over 100% is weighed, DoT skips Chaos but keeps the weighed share") {
    near(score::convert({{{6, false}, 100}, {{6, true}, 100}}, {}, {{6, 10, 0.6f}, {6, 9, 0.9f}}, notes),
         {{{10, false}, 40}, {{9, false}, 60}, {{9, true}, 60}, {{6, true}, 40}});
    CHECK_FALSE(notes.empty());
  }
  // ConversionAttributeAccumulator::Apply 0x17DB20: one ConvertDamage per source type, in first-seen order.
  SUBCASE("Elemental seen first: Elemental -> X before Fire -> Y") {
    near(score::convert({{{6, false}, 100}}, {}, {{58, 10, 0.5f}, {6, 9, 0.5f}}, notes),
         {{{10, false}, 50}, {{9, false}, 25}, {{6, false}, 25}});
  }
  SUBCASE("Fire seen first: Fire -> Y before Elemental -> X") {
    near(score::convert({{{6, false}, 100}}, {}, {{6, 9, 0.5f}, {58, 10, 0.5f}}, notes),
         {{{9, false}, 50}, {{10, false}, 25}, {{6, false}, 25}});
  }
  SUBCASE("a source type's pairs all take from the same amount") {
    near(score::convert({{{6, false}, 100}}, {}, {{6, 9, 0.5f}, {58, 10, 0.2f}, {6, 10, 0.25f}}, notes),
         {{{9, false}, 50}, {{10, false}, 25 + 5}, {{6, false}, 20}});
  }
  SUBCASE("skill level keeps its order too") {
    near(score::convert({{{8, false}, 100}}, {{8, {{2, 50.0}}}, {58, {{10, 50.0}}}}, {}, notes),
         {{{2, false}, 50}, {{10, false}, 25}, {{8, false}, 25}});
  }
}

TEST_CASE("parse: malformed presets are errors, never a crash (the adapter parses user files)") {
  std::string error;
  for (const char* bad : {"[[[x]]]", "[[[", "[ [x] ]", "[\"", "[a.[b]]", "[[]]", "[]", "a.=1", "a = [[[", "x = {", "x = {a=", "= 1",
                          "[preset]\nname = \"\\u\"", "[preset]\nname = '''", "\xEF\xBB", "\xFF\xFE[", "[preset]\nname = 1e999999"}) {
    INFO(bad);
    CHECK_FALSE(score::parse(bad, error));
  }
  // Deterministic byte mutations of every shipped preset: whatever comes back, parse returns.
  std::uint32_t seed = 12345;
  const auto next = [&] { return seed = seed * 1664525u + 1013904223u; };
  for (const auto& e : fs::directory_iterator(fs::path(GDC_SOURCE_DIR) / "presets")) {
    const std::string text = read(e.path());
    for (int i = 0; i < 300; ++i) {
      std::string t = text;
      for (int k = 0; k < 3; ++k) t[next() % t.size()] = "[]{}\"'=.,\n#\\x0"[next() % 15];
      (void)score::parse(t, error);
    }
  }
}

TEST_CASE("parse: every Unicode code point where the parser classifies characters") {
  std::string error;
  const auto utf8 = [](char32_t c) {
    std::string o;
    if (c < 0x80) o += static_cast<char>(c);
    else if (c < 0x800) o += static_cast<char>(0xC0 | (c >> 6)), o += static_cast<char>(0x80 | (c & 0x3F));
    else if (c < 0x10000)
      o += static_cast<char>(0xE0 | (c >> 12)), o += static_cast<char>(0x80 | ((c >> 6) & 0x3F)), o += static_cast<char>(0x80 | (c & 0x3F));
    else
      o += static_cast<char>(0xF0 | (c >> 18)), o += static_cast<char>(0x80 | ((c >> 12) & 0x3F)),
          o += static_cast<char>(0x80 | ((c >> 6) & 0x3F)), o += static_cast<char>(0x80 | (c & 0x3F));
    return o;
  };
  for (char32_t c = 0x80; c <= 0x10FFFF; ++c) {
    if (c >= 0xD800 && c <= 0xDFFF) continue;
    const std::string u = utf8(c);
    (void)score::parse("[preset]\nname =" + u + "\"x\"", error);  // whitespace position
    (void)score::parse("[preset]\n" + u + " = 1", error);         // bare key
    (void)score::parse("[" + u + "]", error);                     // table header
  }
  CHECK(score::parse("[preset]\nname = \"Ölaf's ☃ build\"", error));
}

TEST_CASE("+% All Damage from a buff or a skill reaches DoT too; buff fields past their array end hold the last value (installed game; skips if absent)") {
  // MECHANICS §3: type 60 spreads over both kinds.
  model::BuffEffect all;
  all.stats[{fields::Kind::Damage, 60, false}] = 35;
  const std::pair<const model::BuffEffect*, double> up[] = {{&all, 1.0}};
  snap::Snapshot s, on;
  const arz::Database* db = game_db();
  if (!db) return;
  model::buffed(s, up, *db, on);
  auto row = [&](int type) { return *score::find(on.damage_modifiers, type); };
  CHECK(row(6).damage_modifier == 35);
  CHECK(row(6).dot_modifier == 35);
  CHECK(row(15).dot_modifier == 35);   // Bleeding: DoT only
  CHECK(row(4).damage_modifier == 35);  // Pierce: direct only
  CHECK(row(4).dot_modifier == 0);

  // Soul Harvest (soulscythe2.dbr L4): 62% on the skill's direct and DoT keys.
  score::Raw raw;
  score::add_record(*db, *db->record("records/skills/playerclass08/soulscythe2.dbr"), 4, raw);
  CHECK(raw.pct.at({9, false}) == 62);
  CHECK(raw.pct.at({9, true}) == 62);
  CHECK(raw.pct.at({15, true}) == 62);

  // item_murderousintent.dbr at L3: OA [100, 120, 150], DA [-50, -60].
  const model::BuffEffect m = model::buff_effect(*db->record("records/skills/itemskills/item_murderousintent.dbr"), 3);
  CHECK(m.stats.at({fields::Kind::Char, 21, false}) == 150);
  CHECK(m.stats.at({fields::Kind::Char, 23, false}) == -60);
}

TEST_CASE("score: DoT duration from devotion nodes and passives always, from a toggle only while on (installed game; skips if absent)") {
  const arz::Database* db = game_db();
  if (!db) return;
  auto skill = [](std::uint32_t id, const char* record, std::uint32_t level) {
    snap::Skill k;
    k.id = id;
    k.record = record;
    k.level = k.current_level = level;
    k.buff_active = false;  // as schema >= 8 captures read: anything not flagged on
    return k;
  };
  snap::Snapshot s;
  s.skills.push_back(skill(1, "records/skills/devotion/tier1_02d.dbr", 1));  // +30% Poison duration
  s.skills.back().level = 0;
  s.skills.back().devotion_max_level = 1;
  s.skills.push_back(skill(2, "records/skills/playerclass06/primalbond01.dbr", 10));  // +64% Bleeding duration at 10
  auto dot = [&](int type) { const auto d = score::gear(s, *db, {}).dot; return d.contains(type) ? d.at(type) : 0.0; };
  CHECK(dot(7) == 30);
  CHECK(dot(15) == 0);  // toggled off
  s.skills[1].buff_active = true;
  CHECK(dot(15) == 64);
  s.skills[1].buff_active.reset();  // after model::expected(): on only with its collectors in the capture
  CHECK(dot(15) == 0);
}
