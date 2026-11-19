#include <doctest/doctest.h>

#include <algorithm>

#include "game_db.h"
#include "gdc/stat_model.h"
#include "gdc/enemy.h"
#include "gdc/utility.h"

using namespace gdc;

namespace {

snap::Item item(std::uint32_t id, int location, const char* record) {
  snap::Item it;
  it.id = id;
  it.location = location;
  it.record = record;
  it.stats = snap::ItemStats{};
  return it;
}

}  // namespace

TEST_CASE("utility: when conditions") {
  utility::Context c;
  c.shield = true;
  c.weapon = "WeaponMelee_Sword";
  c.stage = "94+";
  CHECK(utility::holds("", c));
  CHECK(utility::holds("shield", c));
  CHECK_FALSE(utility::holds("!shield", c));
  CHECK_FALSE(utility::holds("pets", c));
  CHECK(utility::holds("weapon:WeaponMelee_Sword", c));
  CHECK_FALSE(utility::holds("weapon:WeaponMelee_Axe", c));
  CHECK(utility::holds("stage:94+", c));
  CHECK(utility::valid_when("!stage:x"));
  CHECK_FALSE(utility::valid_when("weapon:"));
  CHECK_FALSE(utility::valid_when("always"));
  CHECK(utility::stats().back().key == "default");
}

TEST_CASE("utility: avoidance, item cooldown reduction, experience, unlisted stats") {
  snap::Snapshot s;
  s.schema = 8;  // no character total for skill attributes: the items' sum
  s.level = 50;
  s.char_attributes = {{27, 12, 0, 0, 0}, {28, 8, 0, 0, 0}, {55, 10, 0, 0, 0}, {57, 30, 0, 0, 0}};
  auto ring = item(1, 4, "records/items/x.dbr");
  ring.stats->skill_attributes = {{1, 5, 0, 0, 0}, {2, 10, 0, 0, 0}, {3, 20, 0, 0, 0}, {4, 30, 0, 0, 0}};
  ring.stats->offensive_damage = {{20, false, 3, 3}, {28, true, 15, 15, 3}};
  s.equipped = {ring};
  const auto v = utility::values(s, game_db());
  // Dodge 12% of melee, deflect 8% of projectiles: at 75% melee 11%.
  CHECK(utility::avoid(s, 0.75) == doctest::Approx(0.11));
  CHECK(v.keys.at("cooldown_reduction") == doctest::Approx(0.05));
  CHECK(v.keys.at("skill_cost_reduction") == doctest::Approx(0.10));
  CHECK(v.keys.at("life_leech") == doctest::Approx(0.03));
  CHECK_FALSE(v.keys.contains("damage_reduction"));
  // The strongest "reduced target's damage" debuff only.
  auto amulet = item(2, 2, "records/items/y.dbr");
  amulet.stats->offensive_damage = {{28, true, 10, 10, 3}};
  s.equipped.push_back(amulet);
  CHECK(utility::damage_reduction(s) == doctest::Approx(0.15));
  s.equipped.pop_back();
  CHECK(v.unlisted.at("char 57 LightRadius") == doctest::Approx(0.30));
  CHECK(v.unlisted.at("skill 3 ProjectileSpeedModifier") == doctest::Approx(0.20));
  // Combo Charge Spend Reduction counts in Damage only.
  CHECK(utility::combo_keep(s) == doctest::Approx(0.30));
  CHECK(std::none_of(v.unlisted.begin(), v.unlisted.end(), [](const auto& kv) { return kv.first.starts_with("skill 4 "); }));
  // playerlevels.dbr maxPlayerLevel 100: 10% at level 50 counts half, at 100 nothing.
  if (game_db()) {
    CHECK(v.keys.at("experience") == doctest::Approx(0.05));
    s.level = 100;
    CHECK(utility::values(s, game_db()).keys.at("experience") == 0);
  }
}

TEST_CASE("utility: shield block from the shield record (installed game; skips if absent)") {
  const arz::Database* db = game_db();
  if (!db) return;
  snap::Snapshot s;
  s.level = 94;
  // c009_shield: defensiveBlockChance 32, defensiveBlock 1275, blockRecoveryTime 0.85, blockAbsorption 100.
  s.equipped = {item(1, 1, "records/items/awakened/gearweapons/shields/c009_shield.dbr")};
  // 2 hits/s: at most 1 / (0.85 x 2) = 0.59 of them can be blocked > 0.32; 1275 of a 3760 hit.
  CHECK(utility::block(s, db, 3760, 2) == doctest::Approx(0.32 * 1275 / 3760));
  // A hit below the block amount loses hit x absorption (shieldDamageReductionEquationDLEB).
  CHECK(utility::block(s, db, 500, 0) == doctest::Approx(0.32));
  // +18% block chance (type 55, additive) and 50% faster recovery (char 25); 4 hits/s: recovery-bound.
  s.defense = {{55, 18}};
  s.char_attributes = {{25, 50, 0, 0, 0}};
  CHECK(utility::block(s, db, 3760, 4) == doctest::Approx(0.50 * 1275 / 3760));
  s.char_attributes.clear();
  CHECK(utility::block(s, db, 3760, 4) == doctest::Approx(1 / (0.85 * 4) * 1275 / 3760).epsilon(1e-4));
  const auto c = utility::context(s, db, "");
  CHECK(c.shield);
  CHECK(c.offhand == "WeaponArmor_Shield");
  // No shield, no block (a caster off-hand: WeaponArmor_Offhand).
  s.equipped = {item(1, 1, "records/items/awakened/gearweapons/focus/c012_focus.dbr")};
  CHECK(utility::block(s, db, 3760, 2) == 0);
}

TEST_CASE("utility: pets context and pet bonus (installed game; skips if absent)") {
  const arz::Database* db = game_db();
  if (!db) return;
  snap::Snapshot s;
  snap::Skill raven;
  raven.record = "records/skills/playerclass03/summon_raven1.dbr";  // Skill_TargetedSpawnPet
  raven.level = raven.current_level = 1;
  s.skills = {raven};
  CHECK(utility::context(s, db, "").pets);
  s.skills[0].current_level = 0;
  CHECK_FALSE(utility::context(s, db, "").pets);
  // b012a_medal: petBonusName item_petbonus_medal_halion_01 (characterOffensiveAbilityModifier 5,
  // defensiveConvert 100). OA is scored in pet damage, so the defensive part is the convert alone: 100%.
  s.equipped = {item(1, 3, "records/items/gearaccessories/medals/b012a_medal.dbr")};
  CHECK(utility::values(s, db).keys.at("pet_bonus") == doctest::Approx(1.0));
}

TEST_CASE("enemy: race share counts a monster once if any of its races is listed, case-insensitively") {
  // re/EnemyRaces.tsv: 2559 monsters; race003 33 + race003,race009 36 + race002,race005 18 + race005 309 +
  // race005,race012 1 = 397.
  CHECK(enemy::race_share({"Race003", "race005"}) == doctest::Approx(397.0 / 2559));
  CHECK(enemy::race_share({"Race009"}) == doctest::Approx((18 + 36 + 46 + 29 + 274 + 30) / 2559.0));
  CHECK(enemy::race_share({"Aetherial", ""}) == 0);  // no monster carries it
  CHECK(enemy::race_share({}) == 0);
}

TEST_CASE("utility: racial bonus x the monster table's share of its races (installed game; skips if absent)") {
  const arz::Database* db = game_db();
  if (!db) return;
  snap::Snapshot s;
  // d005_ring: racialBonusRace Race003, Race005; racialBonusPercentDamage 10 -> 0.1 x 397 / 2559.
  s.equipped = {item(1, 7, "records/items/gearaccessories/rings/d005_ring.dbr")};
  const auto k = utility::values(s, db).keys;
  CHECK(k.at("racial_damage") == doctest::Approx(0.1 * 397 / 2559));
  CHECK(k.at("racial_defense") == doctest::Approx(0.0));
}

TEST_CASE("utility: cooldown reduction is the character's total from schema 9; a swap moves it by the items' parts") {
  snap::Snapshot s;
  s.char_attributes = {{4, 1000, 0, 0, 0}};
  auto ring = item(1, 4, "records/items/x.dbr");
  ring.stats->skill_attributes = {{1, 5, 0, 0, 0}};
  s.equipped = {ring};
  // 5 from the ring + 12 from skills and devotions; +10% of it from a modifier
  // (SkillAttributeAccumulator::GetValue: value + |value| x modifier / 100).
  s.skill_attributes = {{1, 17, 0, 10, 0}, {2, 8, 0, 0, 0}};
  const auto v = utility::values(s, nullptr);
  CHECK(v.keys.at("cooldown_reduction") == doctest::Approx(0.187));
  CHECK(v.keys.at("skill_cost_reduction") == doctest::Approx(0.08));
  auto other = item(2, 4, "records/items/y.dbr");
  other.stats->skill_attributes = {{1, 8, 0, 0, 0}};
  const auto after = model::swap(s, other, 4, nullptr).after;
  CHECK(utility::values(after, nullptr).keys.at("cooldown_reduction") == doctest::Approx(0.22));  // (17 - 5 + 8) x 1.1
  // An older snapshot has no total: the items alone.
  s.schema = 8;
  s.skill_attributes.clear();
  CHECK(utility::values(s, nullptr).keys.at("cooldown_reduction") == doctest::Approx(0.05));
}

TEST_CASE("utility: slow resist counts once (defense 17..19 are copies of 16)") {
  snap::Snapshot s;
  // One defensiveTotalSpeedResistance 24 as the engine shows it (fixture lvl15-wp-after-feet-dreeg-on).
  for (int t : {16, 17, 18, 19}) s.defense.push_back({t, 24, 0, 0, 0, 0, 24, 0});
  const auto v = utility::values(s, nullptr);
  CHECK(v.keys.at("slow_resist") == doctest::Approx(0.24));
  CHECK(v.unlisted.empty());
}
