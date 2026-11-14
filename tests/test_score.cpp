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
#include "gdc/score.h"

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
  CHECK(std::find(r.other_changes.begin(), r.other_changes.end(), "default: char 56 IncreasedGold 0 -> 0.2 (w 0.05)") != r.other_changes.end());
  // A condition that does not hold: weight 0, still listed.
  p.other["stun_resist"].when = {"shield"};
  const auto c = score::compare(b, a, {}, p, nullptr);
  CHECK(c.ratio.other == doctest::Approx(other - 0.05));
  CHECK(std::find(c.other_changes.begin(), c.other_changes.end(), "stun_resist 0 -> 0.5 (weight 0)") != c.other_changes.end());
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

  // lvl96 Witch Hunter: Shadow Strike (16 points), Doom Bolt (12). Not counted: Curse of Frailty (its buff record
  // has no damage), weapon pool attacks (Skill_WPAttack_BasicAttack, not an attack skill class).
  const auto s = snap::from_json(read(fs::path(GDC_SOURCE_DIR) / "tests/fixtures/snapshots/lvl96-s5-before-ring-c031.json"));
  const auto skills = score::main_skills(s, score::Preset{}, db);
  // Both have cooldowns: the default weapon attack fills in with half the weight.
  REQUIRE(skills.size() == 3);
  CHECK(skills[0].record == "records/skills/playerclass04/shadowstrike.dbr");
  CHECK(skills[0].weight == doctest::Approx(0.5 * 16 / 28));
  CHECK(skills[1].record == "records/skills/playerclass03/doombolt1.dbr");
  CHECK(skills[2].record == "records/skills/default/defaultweaponattack.dbr");
  CHECK(skills[2].weight == doctest::Approx(0.5));

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
