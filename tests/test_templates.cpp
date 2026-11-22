// Each shipped template on a synthetic character of its archetype (build_harness.h) with class skills from the
// database, checking the sign a player would expect.
#include <doctest/doctest.h>

#include "gdc/calibrate.h"
#include "build_harness.h"

using namespace gdc;
using namespace gdc::test;

TEST_CASE("templates: physical-melee on a Soldier with Blade Arc and a sword (installed game; skips if absent)") {
  Build b("physical-melee.toml", kSword);
  if (!b.db) return;
  b.learn("records/skills/playerclass01/_classtraining_class01.dbr", 50);
  b.learn("records/skills/playerclass01/bladearc1.dbr", 16);  // Skill_AttackWeapon, weaponDamagePct, Physical
  REQUIRE_MESSAGE(score::match(b.preset, b.s, *b.db).ok, score::match(b.preset, b.s, *b.db).why);
  b.common();
  CHECK(b.dmg(2, 40, 0) > 0);                    // +% Physical
  CHECK(b.dmg(2, 40, 0) > b.dmg(2, 10, 0));      // more is more
  CHECK(b.dmg(6, 40, 0) == 0);                   // +% Fire: not dealt
  CHECK(b.dmg(10, 40, 0) == 0);                  // +% Chaos: not dealt
  CHECK(b.chr(15, 0, 10) > 0);                   // attack speed: an attack-class skill
  CHECK(b.chr(16, 0, 10) == 0);                  // cast speed: not used
  CHECK(b.chr(2, 40) > 0);                       // Cunning: Physical
  CHECK(b.chr(2, 40) > b.chr(3, 40));            // Spirit adds no Physical
  CHECK(b.chr(21, 80) > 0);                      // Offensive Ability
  CHECK(b.ring([](snap::ItemStats& x) { x.offensive_damage = {{2, false, 20, 40}}; }) > 0);  // flat Physical
  CHECK(b.ring([](snap::ItemStats& x) { x.offensive_modifiers = {pct(59, 0.2f, 0)}; }) > 0);  // crit damage
  CHECK(b.ring([](snap::ItemStats& x) { x.offensive_damage = {{20, false, 5, 5}}; }) > 0);    // life leech
  CHECK(b.ring([](snap::ItemStats&) {}, "records/items/gearaccessories/rings/c017_ring.dbr") > 0);  // +2 Blade Arc
  CHECK(b.neck("records/items/gearaccessories/necklaces/b006a_necklace.dbr") > 0);  // +1 Soldier mastery
  // Converting the build's Physical away: a warning and, at the same flat damage and % bonuses, no gain.
  CHECK_FALSE(b.warnings(2, 6, 0.5f).empty());
  CHECK(b.warnings(6, 10, 0.5f).empty());  // Fire is not dealt: nothing to warn about
  b.sweep();
}

TEST_CASE("templates: chaos-conversion-caster on a Demolitionist with Grenado and Blackwater (installed game; skips if absent)") {
  Build b("chaos-conversion-caster.toml", kSword);
  if (!b.db) return;
  b.learn("records/skills/playerclass02/_classtraining_class02.dbr", 50);
  b.learn("records/skills/playerclass02/grenado1.dbr", 16);     // Skill_AttackProjectile, Physical
  b.learn("records/skills/playerclass02/blackwater1.dbr", 16);  // Fire + Burn
  b.s.conversions = {{2, 10, 1.0f}, {6, 10, 1.0f}};             // Physical -> Chaos, Fire -> Chaos 100%
  REQUIRE_MESSAGE(score::match(b.preset, b.s, *b.db).ok, score::match(b.preset, b.s, *b.db).why);
  b.common();
  CHECK(b.dmg(10, 40, 0) > 0);                 // +% Chaos
  CHECK(b.dmg(2, 40, 0) == 0);                 // +% Physical: all converted before bonuses
  CHECK(b.dmg(6, 40, 0) == 0);                 // +% Fire: direct Fire converted
  CHECK(b.dmg(6, 0, 40) > 0);                  // +% Burn: DoT never converts into Chaos (ConvertDamage)
  CHECK(b.chr(16, 0, 10) > 0);                 // cast speed: projectile spells
  CHECK(b.chr(15, 0, 10) == 0);                // attack speed: not used
  CHECK(b.chr(3, 40) > 0);                     // Spirit: Chaos
  CHECK(b.chr(3, 40) > b.chr(2, 40));          // Cunning: only its life, no Physical left
  CHECK(b.chr(21, 80) > 0);                    // Offensive Ability
  CHECK(b.ring([](snap::ItemStats& x) { x.skill_attributes = {{1, 10, 0, 0, 0}}; }) > 0);  // cooldown reduction
  CHECK(b.ring([](snap::ItemStats&) {}, "records/items/gearaccessories/rings/b103a_ring.dbr") > 0);  // +3 Grenado
  CHECK(b.neck("records/items/gearaccessories/necklaces/d217_necklace.dbr") > 0);  // +Demolitionist mastery
  // Physical -> Fire takes Grenado's Physical away from Chaos (weighed to 100%: 2/3 Chaos, 1/3 Fire): warned (Fire
  // is not a [match] type) and worth less than none.
  CHECK_FALSE(b.warnings(2, 6, 0.5f).empty());
  CHECK(b.convert(2, 6, 0.5f) < 0);
  // Chaos -> Fire: all of the Chaos comes from conversions, which are not converted again (ConvertDamage), so it
  // does nothing and is not warned.
  CHECK(b.warnings(10, 6, 0.5f).empty());
  CHECK(b.convert(10, 6, 0.5f) == 0);
  b.sweep();
  CHECK(b.warnings(2, 10, 0.2f).empty());  // more of the same conversion: into Chaos, no warning
}

TEST_CASE("templates: retaliation on an Oathkeeper with Presence of Virtue and Forcewave (installed game; skips if absent)") {
  Build b("retaliation.toml", kSword);
  if (!b.db) return;
  b.learn("records/skills/playerclass09/_classtraining_class09.dbr", 50);
  b.learn("records/skills/playerclass01/_classtraining_class01.dbr", 32);
  b.toggle_on(b.learn("records/skills/playerclass09/presenceofvirtue1.dbr", 16));  // aura: retaliation, flat Physical
  b.learn("records/skills/playerclass01/shieldhammer1.dbr", 8);       // Skill_AttackWave: cast class
  REQUIRE_MESSAGE(score::match(b.preset, b.s, *b.db).ok, score::match(b.preset, b.s, *b.db).why);
  b.common();
  CHECK(b.ring([](snap::ItemStats& x) { x.retaliation = {{2, false, 200, 300}}; }) > 0);  // flat retaliation
  snap::CombatTotals ret = pct(2, 50, 0);
  CHECK(b.ring([&](snap::ItemStats& x) { x.retaliation_modifiers = {ret}; }) > 0);       // % retaliation
  CHECK(b.dmg(2, 40, 0) > 0);                    // Forcewave's Physical
  // Forcewave (Skill_AttackWave, a cast class in CalculateSkillDps) runs on its cooldown, below cast speed; with
  // only cooldown skills the default weapon attack fills in at attack speed.
  CHECK(b.ring([](snap::ItemStats& x) { x.skill_attributes = {{1, 10, 0, 0, 0}}; }) > 0);  // cooldown reduction
  CHECK(b.chr(16, 0, 10) == 0);                  // cast speed: not the cap here
  CHECK(b.chr(15, 0, 10) > 0);                   // attack speed: the filler attack
  CHECK(b.ring([](snap::ItemStats&) {}, "records/items/gearaccessories/rings/c215_ring.dbr") > 0);  // +2 Presence of Virtue
  // [damage] retaliation = true: retaliation is Damage here, Other for any other preset.
  snap::Item flat = bare(kFinger, kRing);
  flat.stats->retaliation = {{2, false, 200, 300}};
  const auto own = b.worth(flat);
  CHECK(own.ratio.damage > 1);
  CHECK(own.ratio.other == doctest::Approx(1));
  Build plain("default.toml", kSword);
  plain.s = b.s;
  const auto other = plain.worth(flat);
  CHECK(other.ratio.damage == doctest::Approx(1));
  CHECK(other.ratio.other > 1);
  // More retaliation % is more Damage.
  snap::Item more = flat;
  more.stats->retaliation_modifiers = {pct(2, 100, 0)};
  CHECK(b.worth(more).ratio.damage > own.ratio.damage);
}

TEST_CASE("retaliation: % Retaliation Added to Attack scales the character's retaliation (installed game; skips if absent)") {
  Build b("retaliation.toml", kSword);
  if (!b.db) return;
  b.learn("records/skills/playerclass09/_classtraining_class09.dbr", 50);
  b.learn("records/skills/playerclass01/_classtraining_class01.dbr", 32);
  b.toggle_on(b.learn("records/skills/playerclass09/presenceofvirtue1.dbr", 16));
  // Counter Strike: retaliationDamagePct on its own record (Skill_OnHitAttackRadius).
  const char* counter = "records/skills/playerclass01/counterstrike1.dbr";
  b.learn(counter, 12);
  b.preset.skills = {counter};
  snap::Item flat = bare(kFinger, kRing);
  flat.stats->retaliation = {{2, false, 200, 300}};
  const auto r = b.worth(flat);
  CHECK(r.ratio.damage > 1);
  // Without a retaliationDamagePct the attack gains nothing from retaliation; only hits taken do.
  score::Preset off = b.preset;
  off.skills = {"records/skills/playerclass01/shieldhammer1.dbr"};
  Build other(off, kSword);
  other.s = b.s;
  other.learn("records/skills/playerclass01/shieldhammer1.dbr", 8);
  CHECK(r.after.find("(retaliation added ") != std::string::npos);
  CHECK(other.worth(flat).after.find("(retaliation added ") == std::string::npos);
}

TEST_CASE("templates: pet on an Occultist with Hellhound; player-scaled summons apart (installed game; skips if absent)") {
  Build b("pet.toml", kSword);
  if (!b.db) return;
  b.learn("records/skills/playerclass03/_classtraining_class03.dbr", 50);
  const std::uint32_t hound = b.learn("records/skills/playerclass03/summon_hellhound1.dbr", 16).id;
  const std::uint32_t mod = b.learn("records/skills/playerclass03/summon_hellhound2_petmodifier.dbr", 12).id;
  for (auto& k : b.s.skills)
    if (k.id == hound) k.modifier_ids = {mod};
  REQUIRE_MESSAGE(score::match(b.preset, b.s, *b.db).ok, score::match(b.preset, b.s, *b.db).why);
  b.common();
  // A pet bonus ring (b005e: petBonusName item_petbonus_cultistring_05, +50% total damage, +8% life).
  snap::Item cult = bare(kFinger, "records/items/gearaccessories/rings/b005e_ring.dbr");
  const auto r = b.worth(cult);
  CHECK(r.percent > 0);
  CHECK(r.ratio.damage > 1);
  CHECK(r.pets);  // labelled experimental in the tooltip
  CHECK(b.ring([](snap::ItemStats&) {}, "records/items/gearaccessories/rings/c040_ring.dbr") > 0);  // +2 Hellhound
  CHECK(b.neck("records/items/gearaccessories/necklaces/c003_necklace.dbr") > 0);  // +Occultist mastery
  // Grim Harvest boots (c101: pet bonus +6% crit damage only): crit damage counts only above the crit threshold,
  // which this Hellhound's hit chance does not reach.
  CHECK(b.worth(bare(12, "records/items/gearfeet/c101_feet.dbr")).ratio.damage == doctest::Approx(1));
  // A live pet's engine totals (schema 17) replace the record %: with +300% of every type already on it, the ring's +50%
  // adds less.
  snap::Pet live;
  live.record = "records/skills/playerclass03/pets/pet_hellhound_a16.dbr";
  for (const int type : {2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 15}) {
    snap::CombatTotals row;
    row.type = type;
    row.damage_modifier = row.dot_modifier = 300;
    live.damage_modifiers.push_back(row);
  }
  b.s.pets = {live};
  const auto engine = b.worth(cult);
  CHECK(engine.ratio.damage > 1);
  CHECK(engine.ratio.damage < r.ratio.damage);
  // The engine check of a pet's %: rows equal to the record model pass, 20 points more on Fire is off.
  b.s.schema = snap::kSchema;
  for (auto& row : b.s.pets[0].damage_modifiers) row.damage_modifier = row.dot_modifier = 0;
  const auto model = calibrate::pets(b.s, *b.db);
  REQUIRE(model.size() == 1);
  REQUIRE(model[0].skipped.empty());
  CHECK(model[0].rules == std::vector<std::string>{"pet %"});
  for (const auto& t : model[0].types)
    for (auto& row : b.s.pets[0].damage_modifiers)
      if (row.type == t.key.first) (t.key.second ? row.dot_modifier : row.damage_modifier) = static_cast<float>(t.model);
  CHECK(calibrate::pets(b.s, *b.db)[0].pass());
  for (auto& row : b.s.pets[0].damage_modifiers)
    if (row.type == 6) row.damage_modifier += 20;
  const auto off = calibrate::pets(b.s, *b.db);
  CHECK_FALSE(off[0].pass());
  for (const auto& t : off[0].types) CHECK(calibrate::damage_off(t) == (t.key == score::DamageKey{6, false}));
  b.s.pets[0].damage_modifiers.clear();
  CHECK(calibrate::pets(b.s, *b.db)[0].skipped == "no engine totals captured for this pet");
  b.s.schema = 16;
  CHECK(calibrate::pets(b.s, *b.db).empty());  // not captured before schema 17
  b.s.pets.clear();
  // The player's own offense does not reach a Class Pet.
  CHECK(b.dmg(6, 40, 40) == 0);
  CHECK(b.chr(21, 80) == 0);
  CHECK(b.chr(15, 0, 10) == 0);

  // Mortar Trap (PetPlayerScaling): the player's bonuses, not pet bonuses.
  Build t("default.toml", kSword);
  t.learn("records/skills/playerclass02/_classtraining_class02.dbr", 50);
  t.learn("records/skills/playerclass02/mortartrap1.dbr", 16);
  t.learn("records/skills/playerclass02/grenado1.dbr", 1);
  CHECK(t.dmg(6, 40, 40) > 0);  // Mortar Trap's Fire
  const auto tc = t.worth(bare(kFinger, "records/items/gearaccessories/rings/b005e_ring.dbr"));
  CHECK(tc.ratio.damage == doctest::Approx(1));
  CHECK_FALSE(tc.pets);
}

TEST_CASE("templates: default on a ranged Demolitionist with Flame Touched (installed game; skips if absent)") {
  Build b("default.toml", kGun);
  if (!b.db) return;
  b.learn("records/skills/playerclass02/_classtraining_class02.dbr", 50);
  b.learn("records/skills/playerclass02/flamestrike1.dbr", 12);  // Skill_WeaponPool_BasicAttack, Fire
  b.learn("records/skills/playerclass02/blackwater1.dbr", 8);
  b.common();
  CHECK(b.dmg(6, 40, 0) > 0);            // +% Fire
  CHECK(b.dmg(6, 0, 40) > 0);            // +% Burn
  CHECK(b.dmg(9, 40, 0) == 0);           // +% Vitality: not dealt
  CHECK(b.dmg(2, 40, 0) > 0);            // the gun's Physical
  CHECK(b.chr(15, 0, 10) > 0);           // attack speed: weapon pool attacks
  CHECK(b.chr(16, 0, 10) > 0);           // cast speed: Blackwater
  CHECK(b.chr(21, 80) > 0);              // Offensive Ability
  CHECK(b.chr(3, 40) > 0);               // Spirit: Fire
  CHECK(b.ring([](snap::ItemStats& x) { x.offensive_damage = {{6, false, 20, 40}}; }) > 0);  // flat Fire
  CHECK(b.ring([](snap::ItemStats& x) { x.offensive_modifiers = {pct(59, 0.2f, 0)}; }) > 0);  // crit damage
  CHECK(b.chr(55, 20) > 0);              // experience: little at level 96, nothing at 100
  CHECK(b.chr(55, 20) < b.chr(21, 80));
  CHECK(b.neck("records/items/gearaccessories/necklaces/d217_necklace.dbr") > 0);  // +Demolitionist mastery
}
