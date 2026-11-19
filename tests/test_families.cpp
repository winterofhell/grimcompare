// One endgame character per build family of the catalog, put together from the database (endgame_harness.h): the
// family's catalog preset, its skills with their modifiers, devotion powers, and set and legendary items. The sheet
// is checked for sanity, then obvious swaps for their sign: losing a set piece or a build item, the family's damage
// type against one it does not deal, defenses, speed, conversions away from the damage.
#include "endgame_harness.h"

#include <cstdio>
#include <deque>

namespace gdc::test {
namespace {

using namespace combat_attr;
using equip::kFinger1, equip::kHands, equip::kHead, equip::kLeftHand, equip::kMedal, equip::kRightHand,
    equip::kShoulders, equip::kWaist;

constexpr const char* kHeadBlank = "records/items/gearhead/a01_head001.dbr";
constexpr const char* kShoulderBlank = "records/items/gearshoulders/a02_shoulder01.dbr";
constexpr const char* kShieldBlank = "records/items/gearweapons/shields/a00_shield01.dbr";
constexpr const char* kDaggerBlank = "records/items/gearweapons/caster/a01_dagger004.dbr";
constexpr const char* kFocusBlank = "records/items/gearweapons/focus/a06_focus01.dbr";
constexpr const char* kMaceBlank = "records/items/gearweapons/melee2h/a07_blunt2h001.dbr";
constexpr const char* kAxe2hBlank = "records/items/gearweapons/melee2h/a07_axe2h001.dbr";
constexpr const char* kGunBlank = "records/items/gearweapons/guns1h/a07_gun1h002.dbr";
constexpr const char* kAxeBlank = "records/items/gearweapons/axe1h/a07_axe002.dbr";
constexpr const char* kMedalBlank = "records/items/gearaccessories/medals/a001_medal01.dbr";
constexpr const char* kWaistBlank = "records/items/gearaccessories/waist/a04_waist001.dbr";

constexpr int kCast = char_attr::kCastSpeed, kAttack = char_attr::kAttackSpeed;

std::string pc(int cls, const char* name) {
  char buf[96];
  std::snprintf(buf, sizeof buf, "records/skills/playerclass%02d/%s.dbr", cls, name);
  return buf;
}

struct Spec {
  int main;            // the family's damage type
  int off;             // one it does not deal and does not convert to
  int speed;           // attack or cast speed
  bool pets = false;   // Class Pets deal the damage: the player's own +% are near nothing
  int from = 0;        // a type the build converts away (0: none)
  std::vector<Gear> losses;  // build items whose plain replacement must score below
};

// Keeps the record names Learned / Gear point at (a deque never moves its elements).
struct Owned {
  std::deque<std::string> strings;
  const char* operator()(std::string s) { return strings.emplace_back(std::move(s)).c_str(); }
};

void sane(const Endgame& g, const Spec& spec) {
  const auto r = g.vs(g.s);
  CHECK(r.percent == doctest::Approx(0));
  INFO(r.after);
  CHECK(r.ratio.damage == doctest::Approx(1));
  for (const auto& d : g.s.defense)
    if (d.type >= kCold && d.type <= kAether) CHECK(d.defense < 400);  // no runaway resist from the item fields
  CHECK(g.s.level == 100);
  if (spec.pets) CHECK(r.pets);
}

void swaps(const Endgame& g, const Spec& spec) {
  for (const auto& [location, record] : spec.losses) {
    INFO("lose ", g.at(location).record, " for ", std::string(record));
    CHECK(g.replaced(location, record) < 0);
  }
  const auto pct_of = [&](int type) { return g.ring([&](snap::ItemStats& x) { x.offensive_modifiers = {pct(type, 100, 100)}; }); };
  const double main = pct_of(spec.main), off = pct_of(spec.off);
  INFO("+100% main ", main, ", off ", off);
  if (spec.pets) {
    CHECK(main >= 0);
    CHECK(std::abs(off) < 1);
  } else {
    CHECK(main > 1);
    CHECK(main > off);
    CHECK(std::abs(off) < main / 2);
  }
  CHECK(g.ring([](snap::ItemStats& x) { x.char_attributes = {{char_attr::kLife, 300, 0}}; }) > 0);
  CHECK(g.ring([](snap::ItemStats& x) { x.char_attributes = {{char_attr::kLife, -300, 0}}; }) < 0);
  CHECK(g.ring([](snap::ItemStats& x) { x.defense = {resist(kPierce, 20)}; }) > 0);  // under the cap
  CHECK(g.ring([](snap::ItemStats& x) { x.defense = {resist(kPierce, -20)}; }) < 0);
  CHECK(g.ring([](snap::ItemStats& x) { x.char_attributes = {{23, 60, 0}}; }) > 0);  // Defensive Ability
  CHECK(g.chest([](snap::ItemStats& x) { x.defense = {resist(kArmor, 200)}; }) > 0);
  const double oa = g.ring([](snap::ItemStats& x) { x.char_attributes = {{char_attr::kOffensiveAbility, 100, 0}}; });
  const double speed = g.ring([&](snap::ItemStats& x) { x.char_attributes = {{spec.speed, 0, 10}}; });
  if (spec.pets) {
    CHECK(oa >= 0);
    CHECK(speed >= 0);
  } else {
    CHECK(oa > 0);
    CHECK(speed > 0);
  }
  // Half of the family's type converted to one it does not deal.
  if (!spec.pets) CHECK(g.ring([&](snap::ItemStats& x) { x.conversions = {{spec.main, spec.off, 0.5f}}; }) < 0);
  // A type the build converts away is worth less than the one it converts to.
  if (spec.from) CHECK(pct_of(spec.from) < main);
}

void run(const Family& f, const Spec& spec) {
  INFO("family ", std::string(f.name));
  Endgame g(f);
  sane(g, spec);
  swaps(g, spec);
}

}  // namespace

TEST_CASE("families: one endgame character per catalog family, sane sheet and swap signs (installed game; skips if absent)") {
  if (!game_db()) return;
  Owned o;
  const auto sk = [&](int cls, const char* name) { return o(pc(cls, name)); };
  const auto it = [&](const char* rel) { return o(std::string("records/items/") + rel); };
  const auto dev = [&](const char* name) { return std::pair<const char*, std::uint32_t>{o(std::string("records/skills/devotion/") + name + ".dbr"), 15}; };

  SUBCASE("caster DoT: Evil Eye, Dreeg set") {
    const Family f{"evil-eye", "evil-eye", 0, {{3, 50}, {9, 50}},
                   {{sk(3, "evileye1"), 16, {{sk(3, "evileye2"), 12}, {sk(3, "evileye3"), 12}, {sk(3, "evileye4"), 12}}},
                    {sk(3, "curse1"), 12, {{sk(3, "curse2"), 10}}},
                    {sk(3, "bloodofdreeg1"), 12}},
                   {dev("tier1_02e_skill"), dev("tier2_18f_skill")},
                   {{kRightHand, it("upgraded/gearweapons/caster/d016_dagger.dbr")},
                    {kLeftHand, it("upgraded/gearweapons/focus/d010_focus.dbr")},
                    {kHead, it("upgraded/gearhead/d014_head.dbr")},
                    {equip::kChest, it("upgraded/geartorso/d015_torso.dbr")},
                    {kShoulders, it("upgraded/gearshoulders/d016_shoulder.dbr")},
                    {equip::kNeck, it("upgraded/gearaccessories/necklaces/d018_necklace.dbr")}}};
    run(f, {kPoison, kFire, kCast, false, 0, {{kHead, kHeadBlank}, {kShoulders, kShoulderBlank}, {kRightHand, kDaggerBlank}}});
  }
  SUBCASE("caster direct: Arcane Missiles, Mageslayer set") {
    const Family f{"prm", "prm", 0, {{5, 50}, {7, 50}},
                   {{sk(5, "arcanemissile"), 16, {{sk(5, "arcanemissile2"), 12}, {sk(5, "arcanemissile3"), 12}, {sk(5, "arcanemissile4"), 12}}}},
                   {dev("tier3_06f_skill"), dev("tier2_13g_skill")},
                   {{kRightHand, it("upgraded/gearweapons/caster/d006_scepter.dbr")},
                    {kLeftHand, it("upgraded/gearweapons/focus/d013_focus.dbr")},
                    {equip::kNeck, it("upgraded/gearaccessories/necklaces/d017_necklace.dbr")},
                    {kFinger1, it("upgraded/gearaccessories/rings/d013_ring.dbr")},
                    {equip::kFinger2, it("upgraded/gearaccessories/rings/d014_ring.dbr")}}};
    run(f, {kLightning, kChaos, kCast, false, 0, {{kLeftHand, kFocusBlank}, {kFinger1, kRing}, {kRightHand, kDaggerBlank}}});
  }
  SUBCASE("conversion caster: Drain Essence, Aether and Vitality to Cold") {
    const Family f{"drain-essence", "drain-essence", 0, {{4, 50}, {8, 50}},
                   {{sk(8, "lifetap1"), 16, {{sk(8, "lifetap2"), 12}, {sk(8, "lifetap3"), 12}}}},
                   {dev("tier2_15g_skill"), dev("tier3_12g_skill")},
                   {{kRightHand, it("gearweapons/caster/d209_dagger.dbr")},
                    {kLeftHand, it("upgraded/gearweapons/focus/d010_focus.dbr")},
                    {kHead, it("gearhead/d211_head.dbr")},
                    {equip::kChest, it("geartorso/d209_torso.dbr")},
                    {kShoulders, it("gearshoulders/d209_shoulder.dbr")}}};
    run(f, {kCold, kFire, kCast, false, kAether, {{kHead, kHeadBlank}, {equip::kChest, kChest}, {kShoulders, kShoulderBlank}}});
  }
  SUBCASE("melee 2H: Savagery, Gutsmasher") {
    const Family f{"savagery-physical", "savagery-physical", 0, {{3, 50}, {6, 50}},
                   {{sk(6, "savagery1"), 16, {{sk(6, "savagery2"), 12}, {sk(6, "savagery3"), 12}}}},
                   {dev("tier3_04g_skill"), dev("tier1_10e_skill")},
                   {{kRightHand, it("gearweapons/melee2h/d107_blunt2h.dbr")}, {kHead, it("gearhead/d208_head.dbr")}}};  // Gutsmasher: Lightning to Physical
    run(f, {kPhysical, kAether, kAttack, false, 0, {{kRightHand, kMaceBlank}, {kHead, kHeadBlank}}});
  }
  SUBCASE("dual wield: Pierce Cadence, Markovian's set") {
    const Family f{"dw-pierce", "dw-pierce", 0, {{1, 50}, {4, 50}},
                   {{sk(1, "cadence1"), 16, {{sk(1, "cadence2"), 12}}}, {sk(1, "weaponpool1"), 12}, {sk(1, "weaponpool2"), 12}},
                   {dev("tier1_01e_skill"), dev("tier2_06g_skill")},
                   {{kRightHand, it("gearweapons/swords1h/d107_sword.dbr")},
                    {kLeftHand, it("upgraded/gearweapons/swords1h/d011_sword.dbr")},
                    {kHead, it("gearhead/d126_head.dbr")},
                    {equip::kChest, it("geartorso/d123_torso.dbr")},
                    {kShoulders, it("gearshoulders/d122_shoulder.dbr")}}};
    run(f, {kPierce, kCold, kAttack, false, 0, {{kHead, kHeadBlank}, {kShoulders, kShoulderBlank}, {kLeftHand, kSword}}});
  }
  SUBCASE("sword and board: Blade Arc and Righteous Fervor") {
    const Family f{"sword-and-board", "sword-and-board", 0, {{1, 50}, {9, 50}},
                   {{sk(1, "bladearc1"), 16, {{sk(1, "bladearc2"), 12}}},
                    {sk(9, "righteousfervor1"), 16, {{sk(9, "righteousfervor2"), 12}, {sk(9, "righteousfervor3"), 12}}}},
                   {dev("tier3_03g_skill"), dev("tier1_21e_skill")},
                   {{kRightHand, it("gearweapons/swords1h/d204_sword.dbr")},
                    {kLeftHand, it("gearweapons/shields/d205_shield.dbr")},
                    {kHead, it("gearhead/d306_head.dbr")},
                    {kHands, it("gearhands/d107_hands.dbr")}}};
    run(f, {kPhysical, kCold, kAttack, false, 0, {{kHead, kHeadBlank}, {kLeftHand, kShieldBlank}, {kRightHand, kSword}}});
  }
  SUBCASE("ranged: dual pistol Fire Strike, Dagallon's set") {
    const Family f{"pistol-fire-strike", "pistol-fire-strike", 0, {{2, 50}, {7, 50}},
                   {{sk(2, "flamestrike1"), 12, {{sk(2, "flamestrike2"), 12}, {sk(2, "flamestrike3"), 12}, {sk(2, "flamestrike4"), 12}}}},
                   {dev("tier2_07g_skill"), dev("tier1_20e_skill")},
                   {{kRightHand, it("gearweapons/guns1h/d110_gun1h.dbr")},
                    {kLeftHand, it("gearweapons/guns1h/d111_gun1h.dbr")},
                    {kHead, it("gearhead/d122_head.dbr")},
                    {equip::kChest, it("geartorso/d119_torso.dbr")}}};
    run(f, {kFire, kCold, kAttack, false, kPhysical, {{kHead, kHeadBlank}, {equip::kChest, kChest}, {kRightHand, kGunBlank}}});
  }
  SUBCASE("weapon pool: Cadence with Zolhan's and Markovian's on a two-hander") {
    const Family f{"cadence-cold", "cadence-cold", 0, {{1, 50}, {7, 50}},
                   {{sk(1, "cadence1"), 16, {{sk(1, "cadence2"), 12}}}, {sk(1, "weaponpool1"), 12}, {sk(1, "weaponpool2"), 12}},
                   {dev("tier2_15g_skill")},
                   {{kRightHand, it("gearweapons/melee2h/d208_blunt2h.dbr")},
                    {equip::kNeck, it("gearaccessories/necklaces/d314_necklace.dbr")}}};
    run(f, {kCold, kAether, kAttack, false, kPhysical, {{kRightHand, kMaceBlank}}});
    // Markovian's and Zolhan's take Cadence's plain hits only: dropping them loses damage.
    Endgame g(f);
    snap::Snapshot without = g.s;
    for (auto& k : without.skills)
      if (k.record.find("/weaponpool") != std::string::npos) k.level = k.current_level = 0;
    CHECK(g.vs(without).ratio.damage < 1);
  }
  SUBCASE("retaliation: Aegis tank") {
    const Family f{"retaliation-tank", "retaliation-tank", 1, {{1, 50}, {9, 50}},
                   {{sk(9, "aegis1"), 16, {{sk(9, "aegis2"), 12}, {sk(9, "aegis3"), 12}}}, {sk(9, "righteousfervor1"), 12}},
                   {dev("tier3_03g_skill")},
                   {{kRightHand, it("gearweapons/swords1h/d204_sword.dbr")},
                    {kLeftHand, it("upgraded/gearweapons/shields/d005_shield.dbr")}}};
    const Spec spec{kPhysical, kCold, kAttack, false, 0, {{kLeftHand, kShieldBlank}}};
    run(f, spec);
    Endgame g(f);
    CHECK(g.ring([](snap::ItemStats& x) { x.retaliation = {{kPhysical, false, 1000, 1000}}; }) > 0);
    CHECK(g.ring([](snap::ItemStats& x) { x.retaliation_modifiers = {pct(kAll, 100, 0)}; }) > 0);
  }
  SUBCASE("true pets: Skeletons, Hellhound and Raven, Bysmiel's set") {
    const Family f{"skeleton-army", "skeleton-army", 0, {{3, 50}, {8, 50}},
                   {{sk(8, "summon_skeleton1"), 16, {{sk(8, "summon_skeleton2"), 12}, {sk(8, "summon_skeleton3_petmodifier"), 12}}},
                    {sk(3, "summon_hellhound1"), 16, {{sk(3, "summon_hellhound2_petmodifier"), 12}}},
                    {sk(8, "masterofdeath1"), 12}},
                   {dev("tier2_09f_skill")},
                   {{kRightHand, it("awakened/gearweapons/caster/c022_dagger.dbr")},
                    {kLeftHand, it("awakened/gearweapons/focus/c012_focus.dbr")},
                    {kHead, it("awakened/gearhead/c021_head.dbr")},
                    {kShoulders, it("awakened/gearshoulders/c012_shoulder.dbr")},
                    {kMedal, it("awakened/gearaccessories/medals/c012_medal.dbr")}}};
    run(f, {kChaos, kAether, kCast, true, 0, {{kHead, kHeadBlank}, {kMedal, kMedalBlank}, {kLeftHand, kFocusBlank}}});
  }
  SUBCASE("player-scaled pets: Mortar Trap, Mortar set") {
    const Family f{"mortar-trap", "mortar-trap", 0, {{2, 50}, {9, 50}},
                   {{sk(2, "mortartrap1"), 16, {{sk(2, "mortartrap2"), 12}}}, {sk(2, "flamestrike1"), 12}},
                   {dev("tier2_07g_skill")},
                   {{kRightHand, it("gearweapons/guns1h/d110_gun1h.dbr")},
                    {kLeftHand, it("gearweapons/focus/d214_focus.dbr")},
                    {kHead, it("gearhead/d222_head.dbr")},
                    {equip::kChest, it("geartorso/d218_torso.dbr")},
                    {kShoulders, it("gearshoulders/d218_shoulder.dbr")}}};
    run(f, {kFire, kCold, kAttack, false, 0, {{kHead, kHeadBlank}, {kShoulders, kShoulderBlank}, {kLeftHand, kFocusBlank}}});
  }
  SUBCASE("shapeshift: Werewolf, Fenris set") {
    const Family f{"werewolf", "werewolf", 0, {{6, 50}, {10, 50}},
                   {{sk(10, "werewolf1"), 16, {{sk(10, "werewolf2"), 12}, {sk(10, "werewolf3"), 12}}},
                    {sk(10, "werewolf1_skill01_claws"), 16},
                    {sk(10, "werewolf1_skill02_charge"), 12},
                    {sk(10, "onslaught1"), 12}},
                   {dev("tier2_15g_skill"), dev("tier1_10e_skill")},
                   {{kRightHand, it("gearweapons/axe1h/d305_axe.dbr")},
                    {kHead, it("gearhead/d307_head.dbr")},
                    {equip::kChest, it("geartorso/d307_torso.dbr")},
                    {kShoulders, it("gearshoulders/d307_shoulder.dbr")},
                    {equip::kNeck, it("gearaccessories/necklaces/d310_necklace.dbr")}}};
    run(f, {kCold, kFire, kAttack, false, kPierce, {{kHead, kHeadBlank}, {kShoulders, kShoulderBlank}, {kRightHand, kAxeBlank}}});
  }
  SUBCASE("totems: Storm Totem with Savagery") {
    const Family f{"storm-totem", "storm-totem", 0, {{5, 50}, {6, 50}},
                   {{sk(6, "savagery1"), 16, {{sk(6, "savagery2"), 12}}}, {sk(6, "stormtotem01"), 16}},
                   {dev("tier2_13g_skill")},
                   {{kRightHand, it("upgraded/gearweapons/melee2h/d006_axe2h.dbr")},
                    {kHead, it("gearhead/d311_head.dbr")},
                    {kShoulders, it("gearshoulders/d312_shoulder.dbr")},
                    {kWaist, it("gearaccessories/waist/d311_waist.dbr")}}};
    run(f, {kLightning, kChaos, kAttack, false, 0, {{kHead, kHeadBlank}, {kWaist, kWaistBlank}, {kRightHand, kAxe2hBlank}}});
  }
}

}  // namespace gdc::test
