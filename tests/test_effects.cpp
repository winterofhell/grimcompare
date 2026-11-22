#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <string>

#include "game_db.h"
#include "gdc/attribute_names.h"
#include "gdc/effects.h"
#include "gdc/score.h"
#include "gdc/stat_model.h"
#include "gdc/utility.h"

using namespace gdc;

namespace {

effects::Reduction rr(int category, int type, double value, double uptime) {
  effects::Reduction r;
  r.category = category;
  r.amount[type] = value;
  r.uptime = uptime;
  return r;
}

void learn(snap::Snapshot& s, std::uint32_t id, const char* record, std::uint32_t level) {
  snap::Skill k;
  k.id = id;
  k.record = record;
  k.level = k.current_level = level;
  s.skills.push_back(k);
}

const effects::Reduction* find(const effects::Collected& c, const std::string& record, int category) {
  for (const auto& r : c.reductions)
    if (r.record == record && r.category == category) return &r;
  return nullptr;
}

constexpr const char* kCurse = "records/skills/playerclass03/curse1.dbr";
constexpr const char* kVulnerability = "records/skills/playerclass03/curse2.dbr";
constexpr const char* kRumor = "records/skills/devotion/tier2_32d_skill.dbr";
constexpr const char* kRotProc = "records/skills/itemskillsgdx3/legendary/poisondotnova_01.dbr";
constexpr const char* kGarden = "records/items/lootsets/itemset_d313.dbr";

snap::Item piece(int location, const char* record, const char* set = "") {
  snap::Item it;
  it.id = static_cast<std::uint32_t>(100 + location);
  it.location = location;
  it.record = record;
  it.set = set;
  it.stats = snap::ItemStats{};
  return it;
}

}  // namespace

TEST_CASE("effects: RR order 1 -> 2 -> 3 and highest-only overlap (docs/MECHANICS.md §1, §4)") {
  // Type 1 adds: 15 - 25 = -10 -> x1.10; type 2 does nothing below 0.
  auto m = effects::resist_multipliers({{7, 15}}, {rr(1, 7, 25, 1), rr(2, 7, 26, 1)});
  CHECK(m.at(7) == doctest::Approx(1.10));
  // 50 - 10 = 40, x (1 - 0.26) = 29.6, - 28 = 1.6 -> x0.984.
  m = effects::resist_multipliers({{7, 50}}, {rr(1, 7, 10, 1), rr(2, 7, 26, 1), rr(3, 7, 28, 1)});
  CHECK(m.at(7) == doctest::Approx(0.984));
  // Two type-2 sources: 26% up half the time, 18% always: 26 at p 0.5, 18 at p 0.5 x 1.
  m = effects::resist_multipliers({{7, 50}}, {rr(2, 7, 26, 0.5), rr(2, 7, 18, 1)});
  CHECK(m.at(7) == doctest::Approx(0.5 * (1 - 0.37) + 0.5 * (1 - 0.41)));
  // Type 1 at uptime 0.5 counts half; a type the RR does not touch keeps 1 - R / 100.
  m = effects::resist_multipliers({{7, 20}, {9, 30}}, {rr(1, 7, 20, 0.5)});
  CHECK(m.at(7) == doctest::Approx(0.9));
  CHECK(m.at(9) == doctest::Approx(0.7));
}

TEST_CASE("effects: learned debuffs, devotion and item procs, set pets (installed game; skips if absent)") {
  const arz::Database* db = game_db();
  if (!db) return;
  snap::Snapshot s;
  s.level = 94;
  learn(s, 1, kCurse, 10);
  learn(s, 2, kVulnerability, 10);
  s.skills[0].modifier_ids = {2};
  learn(s, 3, kRumor, 20);
  s.skills[2].devotion_max_level = 20;
  s.equipped = {piece(4, "records/items/awakened/gearaccessories/rings/c053_ring.dbr")};
  effects::Rates rates;
  rates.action = 2;
  const std::vector<int> all = {2, 4, 5, 6, 7, 8, 9, 10, 11, 15};
  auto c = effects::collect(s, *db, rates, {}, all);

  // Curse of Frailty has no cooldown: kept up; Vulnerability (its modifier, curse2.dbr level 10) adds -25%.
  const auto* curse = find(c, kCurse, 1);
  REQUIRE(curse);
  CHECK(curse->uptime == 1);
  CHECK(curse->amount.at(7) == 25);
  CHECK(curse->amount.at(5) == 25);  // defensiveElementalResistance -> Cold, Fire, Lightning
  CHECK(curse->amount.at(2) == 25);  // curse1_buff defensivePhysical level 10

  // Rumor (tier2_32d_skill_buff.dbr level 20): -30% Poison & Acid, 15% on attack, cooldown 1 s, 4 s.
  const auto* rumor = find(c, kRumor, 1);
  REQUIRE(rumor);
  CHECK(rumor->amount.at(7) == 30);
  const double rumor_rate = 1 / (1 + 1 / (0.15 * 2));
  CHECK(rumor->uptime == doctest::Approx(std::min(1.0, rumor_rate * 4)));
  CHECK(rumor->origin == effects::Origin::Derived);

  // Awakened Ring of Eternal Rot: poisondotnova_01 18% type 2, 10% on attack, cooldown 1.5 s, 5 s.
  const auto* rot = find(c, kRotProc, 2);
  REQUIRE(rot);
  CHECK(rot->amount.at(7) == 18);
  CHECK(rot->amount.at(9) == 18);  // "to all" lands on every enemy type
  CHECK(rot->uptime == doctest::Approx(5 / (1.5 + 1 / 0.2)));

  // Cooldown reduction 20% (Skill::GetCooldownTime): shortens the devotion power's and a learned skill's
  // recharge (Mortar Trap, skillCooldownTime 2.5 s in the merged database), not the item proc's (Skill::IsItemSkill).
  {
    snap::Snapshot r = s;
    r.skill_attributes = {{1, 20}};
    learn(r, 4, "records/skills/playerclass02/mortartrap1.dbr", 1);
    const auto cr = effects::collect(r, *db, rates, {}, all);
    CHECK(find(cr, kRumor, 1)->uptime == doctest::Approx(std::min(1.0, 4 / (0.8 + 1 / (0.15 * 2)))));
    CHECK(find(cr, kRotProc, 2)->uptime == doctest::Approx(rot->uptime));
    const auto mortar = std::find_if(cr.effects.begin(), cr.effects.end(), [](const effects::Effect& e) {
      return e.record.ends_with("mortartrap1.dbr");
    });
    REQUIRE(mortar != cr.effects.end());
    INFO(mortar->basis);
    CHECK(mortar->per_sec == doctest::Approx(1 / (2.5 * 0.8)));
    CHECK(mortar->basis.find("2.5 s x (1 - 20%)") != std::string::npos);
    r.skill_attributes = {{1, 100}};  // the whole recharge: kept up
    const auto all_cdr = effects::collect(r, *db, rates, {}, all);
    CHECK(std::find_if(all_cdr.effects.begin(), all_cdr.effects.end(), [](const effects::Effect& e) {
            return e.record.ends_with("mortartrap1.dbr");
          })->maintained);
  }

  // No trigger rate: procs fall back to 0 (default), the learned curse stays up.
  c = effects::collect(s, *db, effects::Rates{}, {}, all);
  CHECK(find(c, kRumor, 1)->origin == effects::Origin::Default);
  CHECK(find(c, kRumor, 1)->uptime == 0);
  // A preset [uptime] wins.
  c = effects::collect(s, *db, effects::Rates{}, {{kRumor, 0.5}}, all);
  CHECK(find(c, kRumor, 1)->uptime == 0.5);
  CHECK(find(c, kRumor, 1)->origin == effects::Origin::Preset);

  // Worldkeeper's Garden 4/4: Conjure Living Blossom (20% on attack, cooldown 1.5 s, 24 s, limit 6) -> pets
  // whose Venomous Seed applies 28 flat RR (type 3, 5 s).
  s.equipped = {piece(7, "records/items/gearhead/d316_head.dbr", kGarden),
                piece(9, "records/items/gearshoulders/d318_shoulder.dbr", kGarden),
                piece(8, "records/items/geartorso/d318_torso.dbr", kGarden),
                piece(3, "records/items/gearaccessories/medals/d316_medal.dbr", kGarden)};
  rates.pet_attacks = 0.5;
  c = effects::collect(s, *db, rates, {}, all);
  const auto* seed = find(c, "records/skills/itemskillsgdx3/pets/petskill_livingplant_venomousseed.dbr", 3);
  REQUIRE(seed);
  CHECK(seed->amount.at(7) == 28);
  CHECK(seed->uptime == 1);  // 6 pets x 0.5 attacks/s x 5 s
  const auto blossom = std::find_if(c.effects.begin(), c.effects.end(), [](const effects::Effect& e) {
    return e.pet && e.record.ends_with("venomousseed.dbr");
  });
  REQUIRE(blossom != c.effects.end());
  CHECK(blossom->pets == doctest::Approx(6));
  // Living Bloom (the helm's own pets, 26% type 2).
  CHECK(find(c, "records/skills/itemskillsgdx3/pets/petskill_livingplant_poisonbloom.dbr", 2));
}

TEST_CASE("score: RR from a swap multiplies damage through the enemy resists (installed game; skips if absent)") {
  const arz::Database* db = game_db();
  if (!db) return;
  // Dreeg's Evil Eye (Acid + Poison) vs an enemy with 15 Poison & Acid resist; +5 Vulnerability levels.
  snap::Snapshot s;
  s.level = 94;
  s.pth.minimum = 55;
  s.char_attributes = {{4, 10000, 0, 10000, 0}};
  learn(s, 1, "records/skills/playerclass03/evileye1.dbr", 12);
  learn(s, 2, kCurse, 10);
  learn(s, 3, kVulnerability, 5);
  s.skills[1].modifier_ids = {3};
  snap::Snapshot after = s;
  after.skills[2].current_level = 10;
  score::Preset p;
  p.skills = {"records/skills/playerclass03/evileye1.dbr"};
  p.enemy_resist = {{7, 15}};
  const auto r = score::compare(s, after, score::main_skills(s, p, db), p, db);
  // curse2.dbr level 5 -> 10: -15% -> -25%: (1 - 0/100) / ... resist 15 - 15 = 0 -> 15 - 25 = -10.
  CHECK(r.ratio.damage == doctest::Approx(1.10));
  CHECK(r.unscored.empty());
}

TEST_CASE("score: cooldown reduction raises a cooldown main skill's uses per second up to cast speed (installed game; skips if absent)") {
  const arz::Database* db = game_db();
  if (!db) return;
  snap::Snapshot s;
  s.level = 94;
  s.pth.minimum = 55;
  s.char_attributes = {{4, 10000, 0, 10000, 0}};
  learn(s, 1, "records/skills/playerclass03/evileye1.dbr", 12);  // no cooldown of its own
  learn(s, 2, "records/skills/playerclass03/evileye1b.dbr", 1);  // its transmuter: skillCooldownTime 4 s
  s.skills[0].modifier_ids = {2};
  s.cast_speed = 100;  // no cap
  snap::Snapshot after = s;
  after.skill_attributes = {{1, 20}};  // CooldownReduction 20%
  score::Preset p;
  p.skills = {"records/skills/playerclass03/evileye1.dbr"};
  auto damage = [&](const snap::Snapshot& b, const snap::Snapshot& a) {
    return score::compare(b, a, score::main_skills(b, p, db), p, db).ratio.damage;
  };
  CHECK(damage(s, after) == doctest::Approx(1 / 0.8));  // 4 s -> 3.2 s
  CHECK(damage(after, s) == doctest::Approx(0.8));
  // Cast speed caps it: 0.25/s -> min(0.3125, 0.27)/s.
  s.cast_speed = after.cast_speed = 0.27;
  CHECK(damage(s, after) == doctest::Approx(0.27 / 0.25));
  CHECK(score::compare(s, after, score::main_skills(s, p, db), p, db).after.find("0.27/s (speed cap)") != std::string::npos);
  // Capped on both sides: cooldown reduction adds nothing.
  s.cast_speed = after.cast_speed = 0.2;
  CHECK(damage(s, after) == doctest::Approx(1));
  // With its hit animation (schema 10) the cap is cast speed x frame rate / frames: 0.2 x 30 / 20 = 0.3/s, so
  // 0.25/s -> min(0.3125, 0.3)/s.
  s.skills[0].anim_frames = after.skills[0].anim_frames = 20;
  s.skills[0].anim_frame_rate = after.skills[0].anim_frame_rate = 30;
  CHECK(damage(s, after) == doctest::Approx(0.3 / 0.25));
  CHECK(score::compare(s, after, score::main_skills(s, p, db), p, db).after.find("0.30/s (speed cap) (anim 20f@30 -> 0.30/s)") !=
        std::string::npos);
  // Vile Eye (evileye2) adds weapon damage %; the skill stays on the cast animation's rate, not attack speed.
  learn(s, 3, "records/skills/playerclass03/evileye2.dbr", 5);
  s.skills[0].modifier_ids = {2, 3};
  s.attack_speed = 0.1f;
  {
    snap::Snapshot a2 = s;
    a2.skill_attributes = after.skill_attributes;
    CHECK(damage(s, a2) == doctest::Approx(0.3 / 0.25));
  }
  s.skills.pop_back();
  s.skills[0].modifier_ids = {2};
  s.skills[0].anim_frames = after.skills[0].anim_frames = 0;
  // Not in Other: listed at weight 0 (the Shift view says where it went).
  const auto r = score::compare(s, after, score::main_skills(s, p, db), p, db);
  CHECK(r.ratio.other == doctest::Approx(1));
}

TEST_CASE("score: procs and pets add damage on the main skills' per-second scale (installed game; skips if absent)") {
  const arz::Database* db = game_db();
  if (!db) return;
  snap::Snapshot s;
  s.level = 94;
  s.pth.minimum = 55;
  s.cast_speed = 2;
  s.char_attributes = {{4, 10000, 0, 10000, 0}};
  learn(s, 1, "records/skills/playerclass03/evileye1.dbr", 12);
  s.equipped = {piece(7, "records/items/gearhead/d316_head.dbr", kGarden),
                piece(9, "records/items/gearshoulders/d318_shoulder.dbr", kGarden),
                piece(8, "records/items/geartorso/d318_torso.dbr", kGarden)};
  snap::Snapshot after = s;
  after.equipped.push_back(piece(3, "records/items/gearaccessories/medals/d316_medal.dbr", kGarden));
  score::Preset p;
  p.skills = {"records/skills/playerclass03/evileye1.dbr"};
  p.pet_attack_rate = 0.5;
  // 4/4 adds Conjure Living Blossom: up to 6 pets with Venomous Seed (200-350 Acid + 300 x 5 s Poison).
  // shortcut in the test: the set's other 4/4 bonuses also change the sheet; only the direction is checked.
  const auto r = score::compare(s, after, score::main_skills(s, p, db), p, db);
  CHECK(r.ratio.damage > 1.05);
  // Living Blossom is PetPlayerScaling (effects::summoned): it attacks with the player's bonuses, so +100%
  // Acid / Poison on the sheet raises its damage per attack like a proc's.
  auto per_attack = [](const std::string& text) {
    const auto at = text.find("venomousseed (player-scaled pet) ");
    REQUIRE(at != std::string::npos);
    const auto x = text.find(" x ", at);
    return std::stod(text.substr(x + 3));
  };
  snap::Snapshot boosted = after;
  snap::CombatTotals acid;
  acid.type = 7;
  acid.damage_modifier = acid.dot_modifier = 100;
  boosted.damage_modifiers.push_back(acid);
  const auto b = score::compare(s, boosted, score::main_skills(s, p, db), p, db);
  CHECK(per_attack(b.after) > per_attack(r.after) * 1.3);
  // Without a cast speed the on-attack summon has no rate: the pets are named unscored, not guessed.
  s.cast_speed = after.cast_speed = 0;
  const auto n = score::compare(s, after, score::main_skills(s, p, db), p, db);
  CHECK(std::any_of(n.unscored.begin(), n.unscored.end(),
                    [](const score::Unscored& u) { return u.record.ends_with("venomousseed.dbr"); }));
}

TEST_CASE("score: a conversion moves the source type's DoT too, except to Pierce / Chaos / Aether (installed game; skips if absent)") {
  // CombatAttributeAccumulator::ConvertDamage (0x119ED0): Sigil of Consumption deals Vitality and Vitality Decay.
  const arz::Database* db = game_db();
  if (!db) return;
  snap::Snapshot s;
  s.level = 50;
  learn(s, 1, "records/skills/playerclass03/_classtraining_class03.dbr", 20);
  learn(s, 2, "records/skills/playerclass03/sigilofdestruction1.dbr", 10);
  auto share = [&](const std::map<score::DamageKey, double>& p, score::DamageKey k) {
    const auto it = p.find(k);
    return it != p.end() ? it->second : 0.0;
  };
  s.conversions = {{9, 7, 1.0f}};  // Vitality -> Acid: Vitality Decay becomes Poison
  auto p = score::damage_profile(s, *db);
  CHECK(share(p, {7, false}) > 0);
  CHECK(share(p, {7, true}) > 0);
  CHECK(share(p, {9, false}) == 0);
  CHECK(share(p, {9, true}) == 0);
  s.conversions = {{9, 10, 1.0f}};  // Vitality -> Chaos: Chaos has no DoT, Vitality Decay stays
  p = score::damage_profile(s, *db);
  CHECK(share(p, {10, false}) > 0);
  CHECK(share(p, {10, true}) == 0);
  CHECK(share(p, {9, true}) > 0);
  CHECK(share(p, {9, false}) == 0);
}

TEST_CASE("score: gear ADCtH counts only on weapon-damage skills; a skill modifier's on its skill (installed game; skips if absent)") {
  // Skill::CollectCombatParameters adds gear damage attributes only for weapon damage % > 0;
  // DamageAttributeAbs_LifeLeech scales them by min(%/100, 1). Bramblevine's modifier: 15% on Dreeg's Evil Eye.
  const arz::Database* db = game_db();
  if (!db) return;
  snap::Snapshot s;
  s.level = 94;
  s.cast_speed = s.attack_speed = 2;
  s.char_attributes = {{4, 10000, 0, 10000, 0}};
  learn(s, 1, "records/skills/playerclass03/evileye1.dbr", 12);
  learn(s, 2, "records/skills/default/defaultweaponattack.dbr", 1);
  snap::Snapshot after = s;
  snap::Item ring = piece(4, "records/items/gearaccessories/rings/a002_ring02.dbr");
  ring.stats->offensive_damage.push_back({20, false, 10, 10});  // 10% ADCtH
  after.equipped.push_back(ring);
  score::Preset p;
  p.other["life_leech"] = {1.0, {}};
  p.skills = {"records/skills/playerclass03/evileye1.dbr"};
  CHECK(score::compare(s, after, score::main_skills(s, p, db), p, db).ratio.other == doctest::Approx(1));
  p.skills = {"records/skills/default/defaultweaponattack.dbr"};  // weaponDamagePct 100
  CHECK(score::compare(s, after, score::main_skills(s, p, db), p, db).ratio.other > 1.05);

  p.skills = {"records/skills/playerclass03/evileye1.dbr"};
  snap::ItemSkillModifier m;
  m.skill_id = 1;
  m.skill_record = "records/skills/playerclass03/evileye1.dbr";
  m.modifier_record = "records/skills/itemskillsgdx2/skillmodifiers/legendary/shield_d203_evileye.dbr";
  after = s;
  after.item_skill_modifiers.push_back(m);
  CHECK(score::compare(s, after, score::main_skills(s, p, db), p, db).ratio.other > 1.1);  // + 0.15 x 1.0
}

TEST_CASE("utility: \"pets\" holds only for Class Pet summons, not PetPlayerScaling ones (installed game; skips if absent)") {
  const arz::Database* db = game_db();
  if (!db) return;
  snap::Snapshot s;
  snap::Skill bloom;  // Worldkeeper's Garden 4/4: Conjure Living Blossom -> itempet_livingplantbloom_01 (PetPlayerScaling)
  bloom.record = "records/skills/itemskillsgdx3/legendary/conjurelivingbloom.dbr";
  bloom.current_level = 1;
  s.item_skills.push_back(bloom);
  CHECK_FALSE(utility::context(s, db, "").pets);
  learn(s, 1, "records/skills/playerclass03/summon_hellhound1.dbr", 3);  // -> pet_hellhound_a03 (Pet)
  CHECK(utility::context(s, db, "").pets);
}

TEST_CASE("effects: pet skill level equations (skillLevel<n>, characterAttributeEquations)") {
  CHECK(*effects::equation("16", 50) == 16);
  CHECK(*effects::equation("charLevel*1", 50) == 50);          // pet_hellhound_a16 skillLevel2
  CHECK(*effects::equation("(charLevel/25)", 50) == 2);        // skillLevel3
  CHECK(*effects::equation("CHARLEVEL - 2 * 3", 10) == 4);     // case, precedence
  CHECK(*effects::equation("charLevel/25+1", 50) == 3);        // the commonest shape in the database
  CHECK(*effects::equation("((charLevel*21.3)^1.29)+35", 10) == doctest::Approx(std::pow(213.0, 1.29) + 35));
  CHECK_FALSE(effects::equation("charLevel/0", 1));
  CHECK_FALSE(effects::equation("2+", 1));
  CHECK_FALSE(effects::equation("(1", 1));
  CHECK_FALSE(effects::equation("level*2", 1));
  CHECK_FALSE(effects::equation("", 1));
}

TEST_CASE("effects: a pet without a time to live is kept at petLimit; a transmuter's lifetime by its rate (installed game; skips if absent)") {
  const arz::Database* db = game_db();
  if (!db) return;
  effects::Rates rates;
  rates.char_level = 96;
  rates.action = 2;
  snap::Snapshot s;
  s.level = 96;
  auto pets_of = [&](std::string_view summon) {
    std::vector<effects::Effect> v;
    for (const auto& e : effects::collect(s, *db, rates, {}, {}).effects)
      if (e.pet && e.source.ends_with(summon)) v.push_back(e);
    return v;
  };
  // summon_hellhound1: no spawnObjectsTimeToLive, petLimit 1, cooldown 18 s.
  learn(s, 1, "records/skills/playerclass03/summon_hellhound1.dbr", 16);
  auto v = pets_of("summon_hellhound1.dbr");
  const auto hound = std::find_if(v.begin(), v.end(), [](const effects::Effect& e) {
    return e.record.ends_with("pet_hellhound_a16.dbr");  // its plain attack
  });
  REQUIRE(hound != v.end());
  INFO(hound->basis);
  CHECK(hound->pets == 1);
  CHECK(hound->per_sec > 0);
  CHECK_FALSE(hound->player_scaled);
  CHECK(hound->basis.find("permanent") != std::string::npos);
  // summon_bladespirit: PetPlayerScaling, no time to live anywhere (invincible, stays until replaced); petLimit 3
  // at level 16.
  learn(s, 2, "records/skills/playerclass04/summon_bladespirit.dbr", 16);
  v = pets_of("summon_bladespirit.dbr");
  REQUIRE_FALSE(v.empty());
  for (const auto& e : v) {
    INFO(e.record, ": ", e.basis);
    CHECK(e.player_scaled);
    CHECK(e.pets == 3);
  }
  // summon_blightbeast1 (petLimit 1, cooldown 18 s) + summon_blightbeast1b (Skill_SpawnPetTransmuter: petLimit +2,
  // cooldown -13 s, modSpawnObjectsTimeToLive 12, modSpawnObjects blightbeast_<level>b): one cast per 5 s x 12 s,
  // up to 3 pets.
  learn(s, 3, "records/skills/playerclass08/summon_blightbeast1.dbr", 16);
  learn(s, 4, "records/skills/playerclass08/summon_blightbeast1b.dbr", 1);
  s.skills.back().modifier_ids.clear();
  s.skills[s.skills.size() - 2].modifier_ids = {4};
  v = pets_of("summon_blightbeast1.dbr");
  REQUIRE_FALSE(v.empty());
  for (const auto& e : v) {
    INFO(e.record, ": ", e.basis);
    CHECK(e.pet_record == "records/skills/playerclass08/pets/blightbeast_16b.dbr");
    CHECK(e.pets == doctest::Approx(12.0 / 5));
  }
  // head_d005 (SkillSecondary_PetModifier, cooldownTime -2) shortens the summon's cooldown to 3 s: 12 / 3, the limit.
  snap::ItemSkillModifier fiend;
  fiend.skill_id = 3;
  fiend.skill_record = "records/skills/playerclass08/summon_blightbeast1.dbr";
  fiend.modifier_record = "records/skills/itemskillsgdx1/skillmodifiers/upgradedgdx1/head_d005_summonblightbeast.dbr";
  s.item_skill_modifiers.push_back(fiend);
  for (const auto& e : pets_of("summon_blightbeast1.dbr")) CHECK(e.pets == 3);
  s.item_skill_modifiers.clear();
  s.skill_attributes = {{1, 60}};  // CooldownReduction 60%: 2 s, so the limit
  for (const auto& e : pets_of("summon_blightbeast1.dbr")) CHECK(e.pets == 3);
}

TEST_CASE("effects: a live pet's engine speed and hit animation set its attack rate (installed game; skips if absent)") {
  const arz::Database* db = game_db();
  if (!db) return;
  effects::Rates rates;
  rates.char_level = 96;
  snap::Snapshot s;
  s.level = 96;
  learn(s, 1, "records/skills/playerclass03/summon_hellhound1.dbr", 16);
  auto plain = [&] {
    for (const auto& e : effects::collect(s, *db, rates, {}, {}).effects)
      if (e.pet && e.record.ends_with("pet_hellhound_a16.dbr")) return e;
    FAIL("no plain attack");
    return effects::Effect{};
  };
  const double estimate = plain().per_sec;
  CHECK(plain().basis.find("record attack speed") != std::string::npos);
  snap::Pet hound;
  hound.record = "records/skills/playerclass03/pets/pet_hellhound_a16.dbr";
  hound.attack_speed = 1.5f;
  hound.cast_speed = 1.2f;
  hound.attack_speed_modifier = 20;
  hound.default_skill_id = 9;
  hound.skills = {{9, "records/skills/default/defaultpetattack.dbr", 1, 20, 30}};
  s.pets = {hound};
  auto e = plain();
  INFO(e.basis);
  CHECK(e.basis.find("engine attack speed 1.50 x 30 / 20 frames") != std::string::npos);
  CHECK(e.per_sec > 0);
  CHECK(e.per_sec == doctest::Approx(1.5 * 30 / 20));  // no special with a level in pet_hellhound_a16
  CHECK(e.per_sec != doctest::Approx(estimate));
  // +30% pet attack speed since the capture: 1.5 x (1 + 0.5) / (1 + 0.2) = 1.875.
  rates.pet_attack_shift = 30;
  e = plain();
  CHECK(e.basis.find("engine attack speed 1.88") != std::string::npos);
  // Without its default skill's animation the speed alone is the rate.
  rates.pet_attack_shift = 0;
  s.pets[0].skills[0].anim_frames = 0;
  e = plain();
  CHECK(e.basis.find("engine attack speed 1.50") != std::string::npos);
  CHECK(e.basis.find("frames") == std::string::npos);
}

TEST_CASE("effects: buff classes by the engine's class tree (installed game; skips if absent)") {
  const arz::Database* db = game_db();
  if (!db) return;
  // bladebarrier1: Skill_BuffSelfImmobilize < Skill_BuffSelfDuration, skillActiveDuration 3, recharge 36 at 1.
  snap::Skill barrier;
  barrier.record = "records/skills/playerclass04/bladebarrier1.dbr";
  barrier.level = barrier.current_level = 1;
  const auto t = model::buff_timing(barrier, *db);
  REQUIRE(t);
  CHECK(t->duration == 3);
  CHECK(t->recharge == 36);

  // playerclass07/passive02: Skill_PassiveOnCritBuffSelf, the owner's crits (SkillManager::OnCriticalAttack),
  // onHitActivationChance 100, cooldown 5 s.
  effects::Rates rates;
  rates.action = 2;
  rates.crit = 0.5;
  snap::Snapshot s;
  learn(s, 1, "records/skills/playerclass07/passive02.dbr", 1);
  auto c = effects::collect(s, *db, rates, {}, {});
  const auto crit = std::find_if(c.effects.begin(), c.effects.end(),
                                 [](const effects::Effect& e) { return e.record.ends_with("playerclass07/passive02.dbr"); });
  REQUIRE(crit != c.effects.end());
  INFO(crit->basis);
  CHECK(crit->per_sec > 0);
  CHECK(crit->per_sec <= 1 / 5.0 + 1e-9);

  // blackwater3 (Skill_ProjectileModifier, -RR): counted through Blackwater, never as a skill of its own.
  snap::Snapshot w;
  learn(w, 1, "records/skills/playerclass02/blackwater1.dbr", 10);
  learn(w, 2, "records/skills/playerclass02/blackwater3.dbr", 3);
  w.skills[0].modifier_ids = {2};
  c = effects::collect(w, *db, rates, {}, {});
  for (const auto& r : c.reductions) CHECK_FALSE(r.record.ends_with("blackwater3.dbr"));
  for (const auto& e : c.effects) CHECK_FALSE(e.record.ends_with("blackwater3.dbr"));
}

TEST_CASE("effects: debuff OA / DA shred, all-resist modifiers, enemy damage reduction (installed game; skips if absent)") {
  const arz::Database* db = game_db();
  if (!db) return;
  snap::Snapshot s;
  s.level = 94;
  learn(s, 1, kCurse, 10);
  learn(s, 2, kVulnerability, 10);
  s.skills[0].modifier_ids = {2};
  learn(s, 3, "records/skills/playerclass03/pox1.dbr", 10);
  learn(s, 4, "records/skills/playerclass03/pox2.dbr", 10);
  learn(s, 5, "records/skills/playerclass03/pox1b.dbr", 1);
  s.skills[2].modifier_ids = {4, 5};
  learn(s, 6, "records/skills/playerclass01/warcry1.dbr", 10);
  snap::ItemSkillModifier m;  // ascended a305a: defensiveAllResistance -1
  m.skill_id = 1;
  m.modifier_record = "records/skills/itemskillsgdx3/skillmodifiers/ascended/playerclass03/a305a.dbr";
  s.item_skill_modifiers.push_back(m);
  effects::Rates rates;
  rates.action = 2;
  const std::vector<int> all = {2, 4, 5, 6, 7, 8, 9, 10, 11, 15};
  const auto c = effects::collect(s, *db, rates, {{"records/skills/playerclass01/warcry1.dbr", 0.5}}, all);

  // Curse of Frailty L10 + a305a: Pierce and Aether only from the modifier's all-resist, the rest added.
  const auto* curse = find(c, kCurse, 1);
  REQUIRE(curse);
  CHECK(curse->amount.at(4) == 1);
  CHECK(curse->amount.at(11) == 1);
  CHECK(curse->amount.at(7) == 26);
  // Vulnerability L10: -150 DA (curse2 characterDefensiveAbility), at the curse's uptime.
  const auto* frail = find(c, kCurse, effects::kEnemyDA);
  REQUIRE(frail);
  CHECK(frail->amount.at(0) == 150);
  CHECK(frail->uptime == curse->uptime);
  // Bloody Pox: Wasting L10 -150 OA (pox2), the transmuter -25% DA (pox1b).
  const auto* oa = find(c, "records/skills/playerclass03/pox1.dbr", effects::kEnemyOA);
  REQUIRE(oa);
  CHECK(oa->amount.at(0) == 150);
  const auto* pct = find(c, "records/skills/playerclass03/pox1.dbr", effects::kEnemyDA);
  REQUIRE(pct);
  CHECK(pct->amount.at(1) == 25);
  CHECK_FALSE(pct->amount.contains(0));
  // War Cry L10: 20% enemy damage reduction, preset uptime.
  const auto* cry = find(c, "records/skills/playerclass01/warcry1.dbr", effects::kEnemyDamage);
  REQUIRE(cry);
  CHECK(cry->amount.at(0) == 20);
  CHECK(cry->uptime == 0.5);
  // Never a resist multiplier.
  const auto mult = effects::resist_multipliers({{7, 0}}, {*cry, *oa, *frail});
  CHECK(mult.at(7) == 1);

  // Expected values: flat and % sum x uptime; (base - flat) x (1 - %).
  const auto da = effects::shred(c.reductions, effects::kEnemyDA);
  CHECK(da.flat == doctest::Approx(150 * frail->uptime));
  CHECK(da.pct == doctest::Approx(25 * pct->uptime));
  CHECK(effects::Shred{100, 25}.apply(500) == doctest::Approx(300));
  CHECK(effects::Shred{600, 0}.apply(500) == 0);
  // Highest-only: 20% half the time, else 10%.
  CHECK(effects::expected_highest({{0.2, 0.5}, {0.1, 1}}) == doctest::Approx(0.15));
  CHECK(effects::expected_highest({}) == 0);
}

TEST_CASE("score: enemy damage reduction from a skill joins the items' strongest-only stage (installed game; skips if absent)") {
  const arz::Database* db = game_db();
  if (!db) return;
  snap::Snapshot s;
  s.level = 94;
  s.char_attributes = {{4, 10000, 0, 10000, 0}};
  learn(s, 1, "records/skills/playerclass01/warcry1.dbr", 10);
  s.skills[0].current_level = 0;
  snap::Snapshot after = s;
  after.skills[0].current_level = 10;
  score::Preset p;
  p.uptime["records/skills/playerclass01/warcry1.dbr"] = 1;
  const auto r = score::compare(s, after, score::main_skills(s, p, db), p, db);
  // 20% less damage taken: EHP x 1 / 0.8.
  CHECK(r.ratio.survival == doctest::Approx(1.25));
  CHECK(r.ratio.damage == doctest::Approx(1));
}

TEST_CASE("score: a devotion proc's enemy damage reduction counts at its uptime (installed game; skips if absent)") {
  const arz::Database* db = game_db();
  if (!db) return;
  // Abomination's Tainted Eruption: tier3_02h_skill, offensiveTotalDamageReductionPercentMin 2..16 by level, 5 s.
  constexpr const char* kEruption = "records/skills/devotion/tier3_02h_skill.dbr";
  snap::Snapshot s;
  s.level = 94;
  s.char_attributes = {{4, 10000, 0, 10000, 0}};
  learn(s, 1, kEruption, 10);
  s.skills[0].devotion_max_level = 15;
  effects::Rates rates;
  rates.char_level = 94;
  rates.action = 2;
  const auto c = effects::collect(s, *db, rates, {}, {});
  const auto* r = find(c, kEruption, effects::kEnemyDamage);
  REQUIRE(r);
  CHECK(r->amount.at(0) == 11);
  CHECK(r->uptime > 0);
  snap::Snapshot before = s;
  before.skills[0].current_level = 0;
  score::Preset p;
  p.uptime[kEruption] = 1;
  const auto res = score::compare(before, s, score::main_skills(s, p, db), p, db);
  // The rest is its Confusion (offensiveConfusionMin 1.8 s) as proc crowd control.
  const auto dr = std::find_if(res.contributions.begin(), res.contributions.end(),
                               [](const score::Contribution& c) { return c.label == "Enemy damage reduction"; });
  REQUIRE(dr != res.contributions.end());
  CHECK(dr->points == doctest::Approx(100 * std::log(1 / 0.89)));
}

TEST_CASE("effects: a pet's debuff aura lowers enemy damage while a pet lives (installed game; skips if absent)") {
  const arz::Database* db = game_db();
  if (!db) return;
  // Black Blood of Yugol (tier3_21f_skill, petLimit 6, 8 s) -> pet_blackblood_15, whose petskill_blackblood_aura
  // (Skill_BuffRadiusToggled) applies petskill_blackblood_aura_buff: offensiveTotalDamageReductionPercentMin 20 at 15.
  constexpr const char* kBlackBlood = "records/skills/devotion/tier3_21f_skill.dbr";
  constexpr const char* kAura = "records/skills/devotion/pets/petskill_blackblood_aura.dbr";
  snap::Snapshot s;
  s.level = 94;
  learn(s, 1, kBlackBlood, 15);
  s.skills[0].devotion_max_level = 15;
  effects::Rates rates;
  rates.char_level = 94;
  const auto none = effects::collect(s, *db, rates, {}, {});
  CHECK_FALSE(find(none, kAura, effects::kEnemyDamage));  // no summon rate: no pets, no aura
  rates.hits_taken = 1;  // its devotion trigger is "when hit"
  const auto up = effects::collect(s, *db, rates, {}, {});
  const auto* r = find(up, kAura, effects::kEnemyDamage);
  REQUIRE(r);
  CHECK(r->amount.at(0) == 20);
  CHECK(r->uptime > 0);
  CHECK(r->uptime <= 1);
}

TEST_CASE("score: a timed buff's Total Damage Modified scales the player's hits while it is up (installed game; skips if absent)") {
  const arz::Database* db = game_db();
  if (!db) return;
  snap::Snapshot s;
  s.level = 94;
  s.char_attributes = {{4, 10000, 0, 10000, 0}};
  learn(s, 1, "records/skills/playerclass03/evileye1.dbr", 12);
  snap::Snapshot after = s;
  // c011_waist grants item_torment: offensiveDamageMultModifier -25, Skill_BuffSelfDuration.
  after.equipped.push_back(piece(9, "records/items/gearaccessories/waist/c011_waist.dbr"));
  const std::string torment = "records/skills/itemskills/item_torment.dbr";
  score::Preset p;
  p.skills = {"records/skills/playerclass03/evileye1.dbr"};
  p.uptime[torment] = 1;
  CHECK(score::compare(s, after, score::main_skills(s, p, db), p, db).ratio.damage == doctest::Approx(0.75));
  p.uptime[torment] = 0.5;  // two states, 1 and 0.75
  const double half = score::compare(s, after, score::main_skills(s, p, db), p, db).ratio.damage;
  CHECK(half > 0.75);
  CHECK(half < 1);
}

TEST_CASE("effects: weapon pool attacks share the weapon pool skill's uses, past 100% by weight (installed game; skips if absent)") {
  const arz::Database* db = game_db();
  if (!db) return;
  constexpr const char* kZolhan = "records/skills/playerclass01/weaponpool2.dbr";  // skillChanceWeight 12, 15, 18, 20, ...
  effects::Rates rates;
  rates.char_level = 94;
  rates.action = rates.weapon = 2;
  snap::Snapshot s;
  s.level = 94;
  learn(s, 1, kZolhan, 4);
  snap::ItemSkillModifier m;  // shield_d007: +10 chance
  m.skill_id = 1;
  m.skill_record = kZolhan;
  m.modifier_record = "records/skills/itemskillsgdx1/skillmodifiers/upgradedgdx1/shield_d007_zolhantechnique.dbr";
  s.item_skill_modifiers.push_back(m);
  auto pooled = [&](const snap::Snapshot& x) {
    std::map<std::string, std::pair<double, double>> out;  // record -> chance, uses/s
    for (const auto& e : effects::collect(x, *db, rates, {}, {}).effects)
      if (e.pool_chance > 0) out[e.record] = {e.pool_chance, e.per_sec};
    return out;
  };
  auto one = pooled(s);
  CHECK(one[kZolhan].first == doctest::Approx(0.30));
  CHECK(one[kZolhan].second == doctest::Approx(0.60));
  // Four weights of 30: one roll over 120.
  snap::Snapshot many;
  many.level = 94;
  learn(many, 1, kZolhan, 22);
  learn(many, 2, "records/skills/playerclass01/weaponpool1.dbr", 22);
  learn(many, 3, "records/skills/playerclass06/weaponpool01.dbr", 20);
  learn(many, 4, "records/skills/playerclass06/weaponpool02.dbr", 20);
  const auto four = pooled(many);
  REQUIRE(four.size() == 4);
  for (const auto& [record, v] : four) {
    CHECK(v.first == doctest::Approx(0.25));
    CHECK(v.second == doctest::Approx(0.5));
  }
}

TEST_CASE("score: a weapon pool attack replaces default attacks by its chance (installed game; skips if absent)") {
  const arz::Database* db = game_db();
  if (!db) return;
  snap::Snapshot s;
  s.level = 94;
  s.attack_speed = 2;
  s.char_attributes = {{4, 10000, 0, 10000, 0}};
  learn(s, 1, "records/skills/default/defaultweaponattack.dbr", 1);
  learn(s, 2, "records/skills/playerclass01/weaponpool2.dbr", 0);  // Zolhan's Technique
  snap::Item sword = piece(0, "records/items/gearweapons/swords1h/c005_sword.dbr");
  sword.stats->offensive_damage.push_back({2, false, 100, 100});
  s.equipped.push_back(sword);
  snap::Snapshot after = s;
  after.skills[1].level = after.skills[1].current_level = 4;  // 20%
  score::Preset p;
  p.skills = {"records/skills/default/defaultweaponattack.dbr"};
  const auto r = score::compare(s, after, score::main_skills(s, p, db), p, db);
  // 80% of the uses stay 100 weapon damage, 20% become level 4's 145% weapon + 48 Internal Trauma x 5 s. At 2 x 20%
  // = 0.4 uses/s the 5 s DoT runs twice over and does not stack with itself: half of its 240 counts, 265 per use.
  CHECK(r.ratio.damage == doctest::Approx(0.8 + 0.2 * 2.65).epsilon(0.005));
  CHECK(r.after.find("replaced weapon pool uses 0.200") != std::string::npos);
}

TEST_CASE("score: an attack speed buff speeds weapon pool attacks with the uses they take (installed game; skips if absent)") {
  const arz::Database* db = game_db();
  if (!db) return;
  snap::Snapshot s;
  s.level = 94;
  s.attack_speed = 2;
  s.char_attributes = {{4, 10000, 0, 10000, 0}};
  learn(s, 1, "records/skills/default/defaultweaponattack.dbr", 1);
  learn(s, 2, "records/skills/playerclass04/wpattack1.dbr", 0);  // Amarasta's Quick Cut: no DoT
  snap::Item sword = piece(0, "records/items/gearweapons/swords1h/c005_sword.dbr");
  sword.stats->offensive_damage.push_back({2, false, 100, 100});
  s.equipped.push_back(sword);
  score::Preset p;
  p.skills = {"records/skills/default/defaultweaponattack.dbr"};
  p.enemy_resist[4] = 1000;  // Enchant's +100% Pierce reaches only Quick Cut's Pierce: none gets through
  auto ratio = [&](bool buffed) {
    snap::Snapshot before = s;
    if (buffed) {  // Nightblade's Enchant at 12: +14% total speed, 60 s every 8 s, so always up
      learn(before, 3, "records/skills/playerclass04/nightbladeenchant1.dbr", 12);
      before.skills.back().buff_active = false;
    }
    snap::Snapshot after = before;
    after.skills[1].level = after.skills[1].current_level = 4;  // 19%
    return score::compare(before, after, score::main_skills(before, p, db), p, db).ratio.damage;
  };
  // Each use is the default attack or Quick Cut, whatever the speed: the gain is the same with the buff.
  const double plain = ratio(false);
  CHECK(plain > 1);
  CHECK(ratio(true) == doctest::Approx(plain).epsilon(1e-6));
}

TEST_CASE("score: a weapon pool attack on cooldown replaces only the uses it fires on (installed game; skips if absent)") {
  const arz::Database* db = game_db();
  if (!db) return;
  snap::Snapshot s;
  s.level = 94;
  s.attack_speed = 2;
  s.char_attributes = {{4, 10000, 0, 10000, 0}};
  learn(s, 1, "records/skills/default/defaultweaponattack.dbr", 1);
  snap::Item sword = piece(0, "records/items/gearweapons/swords1h/c005_sword.dbr");
  sword.stats->offensive_damage.push_back({2, false, 100, 100});
  s.equipped.push_back(sword);
  // c011_axe grants item_addnotch.dbr: weight 10, 2 s cooldown. At 2 uses/s it fires 1 / (2 + 1 / 0.2) = 1/7 per
  // second, a 1/14 share of the uses, not its 10% weight.
  snap::Item axe = piece(0, "records/items/gearweapons/axe1h/c011_axe.dbr");
  axe.stats->offensive_damage.push_back({2, false, 100, 100});
  const snap::Snapshot after = model::swap(s, axe, 0, db).after;
  score::Preset p;
  p.skills = {"records/skills/default/defaultweaponattack.dbr"};
  const auto r = score::compare(s, after, score::main_skills(s, p, db), p, db);
  CHECK(r.after.find("replaced weapon pool uses 0.071") != std::string::npos);
}

TEST_CASE("effects: one proc granted by two items is one effect at the summed rate (installed game; skips if absent)") {
  const arz::Database* db = game_db();
  if (!db) return;
  constexpr const char* kFumes = "records/skills/itemskillsgdx2/noxiousfumes_01.dbr";
  snap::Snapshot s;
  s.level = 100;
  s.equipped = {piece(equip::kHands, "records/items/gearhands/c207_hands.dbr")};  // Fewmet Grips
  effects::Rates rates;
  rates.action = 2;
  auto fumes = [&](const effects::Collected& c) {
    return std::count_if(c.effects.begin(), c.effects.end(), [&](const effects::Effect& e) { return e.record == kFumes; });
  };
  const auto one = effects::collect(s, *db, rates, {}, {});
  REQUIRE(fumes(one) == 1);
  // 15% on attack at 2/s, cooldown 2 s: 1 / (2 + 1 / 0.3) per controller; -OA for 5 s.
  const double rate = 1 / (2 + 1 / 0.3);
  const auto* oa = find(one, kFumes, effects::kEnemyOA);
  REQUIRE(oa);
  CHECK(oa->uptime == doctest::Approx(rate * 5));

  // Dung Divider grants the same record: each item's controller rolls (the novas add), the debuff is one instance.
  s.equipped.push_back(piece(equip::kRightHand, "records/items/gearweapons/caster/c307_dagger.dbr"));
  const auto two = effects::collect(s, *db, rates, {}, {});
  REQUIRE(fumes(two) == 1);
  const auto e = std::find_if(two.effects.begin(), two.effects.end(), [&](const effects::Effect& x) { return x.record == kFumes; });
  CHECK(e->per_sec == doctest::Approx(2 * rate));
  CHECK(std::count_if(two.reductions.begin(), two.reductions.end(), [&](const effects::Reduction& r) {
          return r.record == kFumes && r.category == effects::kEnemyOA;
        }) == 1);
  CHECK(find(two, kFumes, effects::kEnemyOA)->uptime == 1);  // min(1, 2 x 0.94)
  CHECK(find(two, kFumes, effects::kEnemyOA)->amount.at(0) == oa->amount.at(0));
}
