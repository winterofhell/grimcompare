#include <doctest/doctest.h>

#include "game_db.h"
#include "gdc/classes.h"

using namespace gdc;

TEST_CASE("classes: engine tree") {
  CHECK(classes::is_a("Skill_BuffSelfImmobilize", "Skill_BuffSelfDuration"));
  CHECK(classes::is_a("Skill_BuffSelfImmobilize", "Skill_BuffSelfToggled"));
  CHECK(classes::is_a("Skill_PassiveOnCritBuffSelf", "Skill_PassiveOnHitBuffSelf"));
  CHECK(classes::is_a("SkillBuff_PassiveShield", "SkillBuff_Passive"));
  CHECK(classes::is_a("PetPlayerScaling", "Pet"));
  CHECK(classes::is_a("Skill_Passive", "Skill_Passive"));
  CHECK_FALSE(classes::is_a("Skill_Passive", "Skill_BuffSelfToggled"));
  CHECK_FALSE(classes::is_a("Pet", "PetPlayerScaling"));
  CHECK_FALSE(classes::is_a("NoSuchClass", "Skill"));
  CHECK(classes::is_a("NoSuchClass", "NoSuchClass"));
  CHECK_FALSE(classes::is_a("", "Skill"));
}

TEST_CASE("classes: speed by class as CalculateSkillDps") {
  using S = classes::Speed;
  CHECK(classes::speed("Skill_WPAttack_BasicAttack") == S::kAttack);
  CHECK(classes::speed("Skill_WeaponPool_ChargedFinale") == S::kAttack);
  CHECK(classes::speed("Skill_AttackWeaponCharge") == S::kAttack);
  CHECK(classes::speed("Skill_AttackRadiusSpin") == S::kAttack);  // a SkillChanneled spell, checked first
  CHECK(classes::speed("Skill_AttackWave") == S::kCast);         // shieldhammer1: weaponDamagePct 100
  CHECK(classes::speed("Skill_OnHitAttackRadius") == S::kCast);  // counterstrike1
  CHECK(classes::speed("Skill_AttackProjectile") == S::kCast);
  CHECK(classes::speed("Skill_BuffSelfToggled") == S::kCast);
  CHECK(classes::speed("Skill_Passive") == S::kNone);
  CHECK(classes::speed("SkillSecondary_AttackRadius") == S::kNone);
  CHECK(classes::speed("") == S::kNone);
}

TEST_CASE("classes: player skill records use known classes") {
  const arz::Database* db = game_db();
  if (!db) return;
  for (const char* r : {"records/skills/playerclass01/shieldhammer1.dbr", "records/skills/playerclass04/bladebarrier1.dbr",
                        "records/skills/playerclass07/passive02.dbr", "records/skills/playerclass09/eyeofreckoning1.dbr",
                        "records/skills/playerclass10/werewolf1.dbr", "records/skills/playerclass02/pets/mortartrap19.dbr"}) {
    CAPTURE(r);
    const arz::Record* rec = db->record(r);
    REQUIRE(rec);
    CHECK(classes::is_a(rec->cls, "Skill") != classes::is_a(rec->cls, "Character"));
  }
}
