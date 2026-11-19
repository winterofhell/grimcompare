// Plague Garden at level 100: the endgame build of tests/fixtures/grimtools/2ELAe4jN (Occultist 50 / Shaman 50,
// Worldkeeper's Garden 4/4). The GrimTools ids do not map to records offline, so the character is put together from
// the database by name: the build's skills at the fixture's levels, its devotion powers, and its set and legendaries
// swapped in one by one (model::swap), so set tiers, granted skills, item skill modifiers and conversions come from
// the records. The sheet stays the harness character's (build_harness.h): values are checked for sanity and swaps for
// their sign, not for exact numbers.
#include "build_harness.h"

#include <algorithm>
#include <cmath>

#include "gdc/effects.h"
#include "gdc/attribute_names.h"
#include "gdc/sets.h"

namespace gdc::test {
namespace {

constexpr const char* kGarden = "records/items/lootsets/itemset_d313.dbr";
constexpr const char* kHelm = "records/items/gearhead/d316_head.dbr";            // Worldkeeper's Gaze
constexpr const char* kShoulders = "records/items/gearshoulders/d318_shoulder.dbr";
constexpr const char* kTorso = "records/items/geartorso/d318_torso.dbr";
constexpr const char* kMedal = "records/items/gearaccessories/medals/d316_medal.dbr";
constexpr const char* kVines = "records/skills/playerclass06/graspingvines1.dbr";
constexpr const char* kPox = "records/skills/playerclass03/pox1.dbr";
constexpr const char* kEye = "records/skills/playerclass03/evileye1.dbr";
constexpr const char* kCurse = "records/skills/playerclass03/curse1.dbr";
constexpr const char* kBloom = "records/skills/itemskillsgdx3/legendary/conjurelivingbloom.dbr";
constexpr const char* kSeed = "records/skills/itemskillsgdx3/pets/petskill_livingplant_venomousseed.dbr";
constexpr const char* kBud = "records/skills/itemskillsgdx3/pets/petskill_livingplant_poisonbloom.dbr";
constexpr int kPhysical = 2, kPoison = 7, kVitality = 9;  // CombatAttributeType: Physical, Poison (Acid), Life

snap::Item worn(int location, const char* record, const char* set = "") {
  snap::Item it = bare(location, record);
  it.set = set;
  return it;
}

snap::Item converting(int location, const char* record, const char* set, int from, int to, float share) {
  snap::Item it = worn(location, record, set);
  it.stats->conversions = {{from, to, share}};
  return it;
}

struct Garden : Build {
  Garden() : Build("plague-garden.toml", kSword) {
    s.level = 100;
    learn("records/skills/playerclass03/_classtraining_class03.dbr", 50);
    learn("records/skills/playerclass06/_classtraining_class06.dbr", 50);
    // Skill levels: the fixture's "skills" (12, 16, 10, 4, 1), named by the build guide.
    parent(kCurse, 10, {{"records/skills/playerclass03/curse2.dbr", 10}});  // Vulnerability
    parent(kPox, 12, {{"records/skills/playerclass03/pox2.dbr", 12}, {"records/skills/playerclass03/pox3.dbr", 12}});
    parent(kEye, 12, {});
    learn("records/skills/playerclass03/evileye1b.dbr", 1);  // Focused Gaze
    parent("records/skills/playerclass03/bloodofdreeg1.dbr", 16, {{"records/skills/playerclass03/bloodofdreeg2.dbr", 12}});
    learn("records/skills/playerclass03/possession1.dbr", 12);
    parent(kVines, 12, {{"records/skills/playerclass06/graspingvines2.dbr", 12}});
    parent("records/skills/playerclass06/natureblessing1.dbr", 4,  // Mogdrogen's Pact: Heart of the Wild, Oak Skin
           {{"records/skills/playerclass06/naturesblessing2.dbr", 10}, {"records/skills/playerclass06/naturesblessing3.dbr", 10}});
    learn("records/skills/playerclass06/totem1.dbr", 1);
    // Devotion powers (the guide's bindings); level 15, Rumor's 20.
    for (const char* power : {"records/skills/devotion/tier2_32d_skill.dbr",    // Rumor
                              "records/skills/devotion/tier1_02e_skill.dbr",    // Scorpion Sting
                              "records/skills/devotion/tier3_02h_skill.dbr",    // Tainted Eruption
                              "records/skills/devotion/tier1_01e_skill.dbr",    // Twin Fangs
                              "records/skills/devotion/tier2_28f_skill.dbr",    // Giant's Blood
                              "records/skills/devotion/tier3_21f_skill.dbr"}) { // Black Blood of Yugol
      const bool rumor = std::string_view(power).ends_with("tier2_32d_skill.dbr");
      auto& k = learn(power, rumor ? 20 : 15);
      k.devotion_max_level = rumor ? 20 : 15;
    }
    // Gear: the item records' own conversions by hand (bare items carry no stats), the rest from the database.
    for (const auto& it : {worn(equip::kRightHand, "records/items/gearweapons/caster/c307_dagger.dbr"),  // Dung Divider
                           worn(equip::kLeftHand, "records/items/gearweapons/shields/d203_shield.dbr"),  // Bramblevine
                           worn(equip::kNeck, "records/items/gearaccessories/necklaces/d113f_necklace.dbr"),
                           worn(equip::kFinger1, "records/items/gearaccessories/rings/d303_ring.dbr"),   // Widow's Bite
                           worn(equip::kFinger2, "records/items/awakened/gearaccessories/rings/c053_ring.dbr"),
                           worn(equip::kHands, "records/items/gearhands/c207_hands.dbr"),               // Fewmet Grips
                           worn(equip::kLegs, "records/items/gearlegs/b103e_legs.dbr"),                 // Grava'Thul's
                           worn(equip::kArtifact, "records/items/gearrelic/d114_relic.dbr"),            // Deathstalker
                           converting(equip::kHead, kHelm, kGarden, kPhysical, kPoison, 0.3f),
                           converting(equip::kShoulders, kShoulders, kGarden, kVitality, kPoison, 0.3f),
                           converting(equip::kChest, kTorso, kGarden, kVitality, kPoison, 0.3f),
                           worn(equip::kMedal, kMedal, kGarden)})
      s = model::swap(s, it, it.location, db).after;
  }

  void parent(const char* record, std::uint32_t points, std::vector<std::pair<const char*, std::uint32_t>> mods) {
    const std::uint32_t id = learn(record, points).id;
    std::vector<std::uint32_t> ids;
    for (const auto& [m, lv] : mods) ids.push_back(learn(m, lv).id);
    for (auto& k : s.skills)
      if (k.id == id) k.modifier_ids = ids;
  }

  score::Result vs(const snap::Snapshot& after) const {
    const auto& settings = score::settings(preset, s);
    return score::compare(s, after, score::main_skills(s, settings, db), settings, db);
  }
  double swap(const snap::Item& it) const { return vs(model::swap(s, it, it.location, db).after).percent; }
};

}  // namespace

// "<record stem> L<n> dmg <per use>" in the explained text, the shown buff state.
double per_use(const std::string& text, std::string_view stem) {
  const auto at = text.find(std::string(stem) + " L");
  REQUIRE(at != std::string::npos);
  const auto d = text.find(" dmg ", at);
  REQUIRE(d != std::string::npos);
  return std::stod(text.substr(d + 5));
}

// "enemy x ... <type> <multiplier>": the enemy resist multiplier after RR.
double enemy_x(const std::string& text, std::string_view type) {
  const auto at = text.find("enemy x ");
  REQUIRE(at != std::string::npos);
  const auto t = text.find(" " + std::string(type) + " ", at);
  REQUIRE(t != std::string::npos);
  return std::stod(text.substr(t + type.size() + 2));
}

TEST_CASE("endgame plague-garden: the level 100 character reads sanely (installed game; skips if absent)") {
  if (!game_db()) return;
  Garden g;
  REQUIRE(score::match(g.preset, g.s, *g.db).ok);  // Acid + Poison after conversions: at least min_share
  CHECK(sets::counts(g.s).at(kGarden) == 4);
  const auto mods_on = [&](const char* skill) {
    std::vector<std::string> out;
    for (const auto& m : g.s.item_skill_modifiers)
      if (m.skill_record == skill) out.push_back(m.modifier_record.substr(m.modifier_record.rfind('/') + 1));
    std::sort(out.begin(), out.end());
    return out;
  };
  // Grasping Vines: the set's 4/4 modifier, the helm's and Bramblevine's.
  CHECK(mods_on(kVines) == std::vector<std::string>{"head_d316_graspingvines.dbr", "set_d313_graspingvines.dbr",
                                                     "shield_d203_graspingvines.dbr"});
  CHECK(mods_on(kPox) == std::vector<std::string>{"medal_d316_bloodypox.dbr", "set_d313_bloodypox.dbr"});

  const auto r = g.vs(g.s);
  CHECK(r.percent == doctest::Approx(0));
  // Living Bloom (the helm's Conjure Living Bud) and Living Blossom (the 4/4 Conjure Living Blossom) attack with the
  // player's bonuses.
  CHECK(r.after.find("petskill_livingplant_poisonbloom (player-scaled pet)") != std::string::npos);
  CHECK(r.after.find("petskill_livingplant_venomousseed (player-scaled pet)") != std::string::npos);
  // RR: Vulnerability, Rumor, Widow's Bite and the Blossoms' 28 flat leave Acid / Poison the weakest resist.
  const double poison = enemy_x(r.after, "Poison");
  CHECK(poison > 1.4);
  CHECK(poison < 2.5);
  for (const char* other : {"Fire", "Cold", "Lightning", "Aether"}) CHECK(poison > enemy_x(r.after, other));

  // Damage Modified on Grasping Vines: set_d313_graspingvines offensiveDamageMultModifier 150 -> x2.5 per use.
  snap::Snapshot without = g.s;
  std::erase_if(without.item_skill_modifiers, [](const snap::ItemSkillModifier& m) {
    return m.modifier_record.ends_with("set_d313_graspingvines.dbr");
  });
  const auto w = g.vs(without);
  CHECK(per_use(r.after, "graspingvines1") / per_use(w.after, "graspingvines1") == doctest::Approx(2.5).epsilon(0.001));
  CHECK(w.ratio.damage < 1);
}

TEST_CASE("endgame plague-garden: obvious swaps keep their sign (installed game; skips if absent)") {
  if (!game_db()) return;
  Garden g;
  auto plain = [](int location, const char* record) { return worn(location, record); };
  auto widow = [&](const std::function<void(snap::ItemStats&)>& stats) {  // Widow's Bite plus the stats named
    snap::Item it = worn(equip::kFinger1, "records/items/gearaccessories/rings/d303_ring.dbr");
    stats(*it.stats);
    return g.swap(it);
  };
  // Losing a set piece, a skill grant or a proc.
  CHECK(g.swap(plain(equip::kMedal, "records/items/gearaccessories/medals/a001_medal01.dbr")) < -5);  // 4/4 -> 3/4
  CHECK(g.swap(plain(equip::kHead, "records/items/gearhead/a01_head001.dbr")) < -5);  // Living Bud, 100% Vines conversion
  CHECK(g.swap(plain(equip::kShoulders, "records/items/gearshoulders/a02_shoulder01.dbr")) < 0);
  CHECK(g.swap(plain(equip::kLeftHand, "records/items/gearweapons/shields/a00_shield01.dbr")) < 0);  // +4 Vines / Eye
  CHECK(g.swap(plain(equip::kRightHand, kSword)) < 0);                    // +1 all skills, Noxious Fumes
  CHECK(g.swap(plain(equip::kNeck, kNeck)) < 0);                          // +1 Shaman, +2 Mogdrogen's Pact
  CHECK(g.swap(plain(equip::kFinger2, kRing)) < 0);                       // Eternal Rot's poison nova
  CHECK(g.swap(plain(equip::kLegs, "records/items/gearlegs/a00_legs01.dbr")) < 0);  // +3 Vulnerability (RR)
  // Stats on top of Widow's Bite.
  const double acid = widow([](snap::ItemStats& x) { x.offensive_modifiers = {pct(7, 100, 100)}; });
  const double fire = widow([](snap::ItemStats& x) { x.offensive_modifiers = {pct(6, 100, 100)}; });
  CHECK(acid > 1);
  CHECK(acid > fire);
  CHECK(widow([](snap::ItemStats& x) { x.char_attributes = {{1, 50, 0}}; }) > 0);  // Physique
  CHECK(widow([](snap::ItemStats& x) { x.char_attributes = {{4, 1000, 0}}; }) > 0);  // Life
  CHECK(widow([](snap::ItemStats& x) { x.defense = {resist(4, 20)}; }) > 0);         // Pierce resist under the cap
  CHECK(widow([](snap::ItemStats& x) { x.defense = {resist(4, -20)}; }) < 0);
  CHECK(widow([](snap::ItemStats& x) { x.conversions = {{7, 6, 0.5f}}; }) < 0);       // Acid -> Fire: off-target
}

}  // namespace gdc::test
