// Each shipped template on a synthetic character of its archetype: class skills from the database, a level 96
// Ultimate sheet (winterofhell's engine totals, lvl96-s5-before-ring-c031.json) with that character's skills,
// conversions and item stats removed. Every case equips one hand-written item over a bare one, so its score is
// the worth of exactly the stats named, and checks the sign a player would expect.
#include <doctest/doctest.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <functional>
#include <sstream>

#include "game_db.h"
#include "gdc/score.h"
#include "gdc/stat_model.h"

using namespace gdc;
namespace fs = std::filesystem;

namespace {

constexpr const char* kRing = "records/items/gearaccessories/rings/a002_ring02.dbr";  // no grants
constexpr const char* kNeck = "records/items/gearaccessories/necklaces/a00_necklace.dbr";
constexpr const char* kChest = "records/items/geartorso/a02_torso003.dbr";
constexpr const char* kSword = "records/items/gearweapons/swords1h/a00_sword001.dbr";  // WeaponMelee_Sword
constexpr const char* kGun = "records/items/gearweapons/guns2h/a01_gun2h000.dbr";      // WeaponHunting_Ranged2h
constexpr int kFinger = 4, kNeckSlot = 2, kChestSlot = 8;

std::string read(const fs::path& p) {
  std::ifstream f(p);
  REQUIRE(f.good());
  std::stringstream ss;
  ss << f.rdbuf();
  return ss.str();
}

snap::Item bare(int location, const char* record) {
  snap::Item it;
  it.id = 900000 + static_cast<std::uint32_t>(location);
  it.location = location;
  it.record = record;
  it.kind = "ItemEquipment";
  it.stats.emplace();
  return it;
}

snap::CombatTotals pct(int type, float direct, float dot) {
  snap::CombatTotals r;
  r.type = type;
  r.damage_modifier = direct;
  r.dot_modifier = dot;
  return r;
}

snap::CombatTotals resist(int type, float value) {
  snap::CombatTotals r;
  r.type = type;
  r.defense = value;
  return r;
}

struct Build {
  const arz::Database* db = game_db();
  score::Preset preset;
  snap::Snapshot s;

  Build(const char* preset_file, const char* weapon) {
    std::string error;
    auto p = score::parse(read(fs::path(GDC_SOURCE_DIR) / "presets" / preset_file), error);
    REQUIRE_MESSAGE(p, error);
    preset = *p;
    s = snap::from_json(read(fs::path(GDC_SOURCE_DIR) / "tests/fixtures/snapshots/lvl96-s5-before-ring-c031.json"));
    s.character_name = "synthetic";
    s.skills.clear();
    s.item_skills.clear();
    s.item_skill_modifiers.clear();
    s.skill_attributes.clear();
    s.conversions.clear();
    snap::Item hand = bare(0, weapon);
    hand.stats->offensive_damage.push_back({1, false, 300, 500});  // base Physical 300-500 (kBasePhysical)
    s.equipped = {hand, bare(kNeckSlot, kNeck), bare(kFinger, kRing), bare(kChestSlot, kChest)};
    // One base resist under the cap (Pierce 30 of 80) and stun resist 40 (190 on the sheet: over its cap of 80),
    // so those are worth something.
    for (auto& d : s.defense)
      if (d.type == 4 || d.type == 42) d.defense = d.processed = d.type == 4 ? 30 : 40;
    learn("records/skills/default/defaultweaponattack.dbr", 1);  // every character has it
  }

  snap::Skill& learn(const char* record, std::uint32_t points) {
    snap::Skill k;
    k.id = static_cast<std::uint32_t>(s.skills.size() + 1);
    k.record = record;
    k.level = k.current_level = points;
    // GetMasteryId: the learned mastery of the same class directory (SetMasteryAugment matches on it).
    const std::string_view r(record);
    for (const auto& m : s.skills)
      if (m.record.find("/_classtraining_") != std::string::npos &&
          r.substr(0, r.rfind('/')) == std::string_view(m.record).substr(0, m.record.rfind('/')))
        k.mastery_id = m.id;
    s.skills.push_back(k);
    return s.skills.back();
  }
  // An active toggle or aura as the capture reads it: its collectors are present (score.cpp skill_on).
  void toggle_on(snap::Skill& k) {
    k.passive.emplace();
    k.passive->defense = {resist(2, 0)};
  }

  score::Result worth(const snap::Item& it) const {
    const auto& settings = score::settings(preset, s);
    const auto p = model::swap(s, it, it.location, db);
    return score::compare(s, p.after, score::main_skills(s, settings, db), settings, db);
  }
  double ring(const std::function<void(snap::ItemStats&)>& stats, const char* record = kRing) const {
    snap::Item it = bare(kFinger, record);
    stats(*it.stats);
    return worth(it).percent;
  }
  double neck(const char* record) const { return worth(bare(kNeckSlot, record)).percent; }
  double dmg(int type, float direct, float dot) const {
    return ring([&](snap::ItemStats& x) { x.offensive_modifiers = {pct(type, direct, dot)}; });
  }
  double chr(int type, float value, float modifier = 0) const {
    return ring([&](snap::ItemStats& x) { x.char_attributes = {{type, value, modifier}}; });
  }
  double def(int type, float value) const {
    return ring([&](snap::ItemStats& x) { x.defense = {resist(type, value)}; });
  }
  double convert(int from, int to, float share) const {
    return ring([&](snap::ItemStats& x) { x.conversions = {{from, to, share}}; });
  }
  std::vector<score::Conversion> warnings(int from, int to, float share) const {
    snap::Item it = bare(kFinger, kRing);
    it.stats->conversions = {{from, to, share}};
    const auto p = model::swap(s, it, kFinger, db);
    return score::conversion_warnings(s, p.after, score::settings(preset, s), preset.match_damage, db);
  }

  // Defenses every build wants, whatever it deals.
  void common() const {
    CHECK(chr(4, 300) > 0);           // Life
    CHECK(chr(1, 20) > 0);            // Physique
    CHECK(chr(23, 60) > 0);           // Defensive Ability
    CHECK(def(4, 20) > 0);            // Pierce resist under the cap
    CHECK(def(4, 20) > def(10, 20));  // under the cap beats overcap
    CHECK(def(10, 20) >= 0);          // Chaos resist at the cap: overcap only
    CHECK(def(2, 4) > 0);             // Physical resist
    CHECK(def(42, 20) > 0);           // stun resist
    CHECK(chr(27, 5) > 0);            // dodge
    snap::Item plate = bare(kChestSlot, kChest);
    plate.stats->defense = {resist(38, 200)};
    CHECK(worth(plate).percent > 0);  // Armor
    CHECK(def(4, -20) < 0);           // a resist malus
    CHECK(chr(4, -300) < 0);          // a life malus
    CHECK(neck("records/items/gearaccessories/necklaces/b201c_necklace.dbr") > 0);  // +1 to all skills
  }

  // The defenses keep their sign on every difficulty and at every level (enemy tables, resist penalties, lookahead).
  void sweep() {
    const auto level = s.level;
    const int difficulty = s.difficulty;
    for (int d : {0, 1, 2})
      for (std::uint32_t l : {20u, 50u, 70u, 90u, 100u}) {
        s.difficulty = d;
        s.level = l;
        INFO("difficulty ", d, " level ", l);
        CHECK(chr(4, 300) > 0);
        CHECK(chr(23, 60) >= 0);  // 0 once the enemies' hit chance is at its floor (a level 96 sheet at 20)
        CHECK(def(4, 20) > 0);
        CHECK(def(4, 20) >= def(10, 20));
        CHECK(def(10, 20) >= 0);
        CHECK(def(4, -20) < 0);
        CHECK(chr(21, 80) >= 0);
        snap::Item plate = bare(kChestSlot, kChest);
        plate.stats->defense = {resist(38, 200)};
        CHECK(worth(plate).percent > 0);
      }
    s.level = level;
    s.difficulty = difficulty;
  }
};

}  // namespace

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
