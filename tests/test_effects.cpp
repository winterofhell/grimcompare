#include <doctest/doctest.h>

#include <algorithm>
#include <string>

#include "game_db.h"
#include "gdc/effects.h"
#include "gdc/score.h"
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
