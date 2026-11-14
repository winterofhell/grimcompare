#include <doctest/doctest.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#include "game_db.h"
#include "gdc/score.h"
#include "gdc/tooltip.h"

using namespace gdc;
namespace fs = std::filesystem;

namespace {

constexpr const char* kShaman = "records/skills/playerclass06/_classtraining_class06.dbr";
constexpr const char* kOccultist = "records/skills/playerclass03/_classtraining_class03.dbr";
constexpr const char* kSoldier = "records/skills/playerclass01/_classtraining_class01.dbr";
constexpr const char* kSwarm = "records/skills/playerclass06/devouringswarm1.dbr";
constexpr const char* kVines = "records/skills/playerclass06/graspingvines1.dbr";
constexpr const char* kEvilEye = "records/skills/playerclass03/evileye1.dbr";

score::Preset preset_file(const std::string& name) {
  std::ifstream f(fs::path(GDC_SOURCE_DIR) / "presets" / name);
  REQUIRE(f.good());
  std::stringstream ss;
  ss << f.rdbuf();
  std::string error;
  auto p = score::parse(ss.str(), error);
  REQUIRE_MESSAGE(p, error);
  return *p;
}

score::Preset plague_garden() { return preset_file("plague-garden.toml"); }

snap::Skill& learn(snap::Snapshot& s, const char* record, std::uint32_t points) {
  snap::Skill k;
  k.id = static_cast<std::uint32_t>(s.skills.size() + 1);
  k.record = record;
  k.level = k.current_level = points;
  s.skills.push_back(k);
  return s.skills.back();
}

snap::Snapshot fixture_snapshot(const char* name) {
  std::ifstream f(fs::path(GDC_SOURCE_DIR) / "tests/fixtures/snapshots" / name);
  REQUIRE(f.good());
  std::stringstream ss;
  ss << f.rdbuf();
  return snap::from_json(ss.str());
}

std::string stage_name(const score::Preset& p, const snap::Snapshot& s) {
  const score::Stage* st = score::stage(p, s);
  return st ? st->name : "";
}

}  // namespace

TEST_CASE("presets: plague-garden stages follow level and masteries (winterpoison's path: Shaman, Occultist at 10)") {
  const auto p = plague_garden();
  CHECK(p.name == "plague-garden");
  REQUIRE(p.stages.size() == 4);
  CHECK(p.weigh_by_points);
  CHECK(p.stages[0].settings.weigh_by_points);  // stages inherit the preset's settings
  CHECK(p.stages[0].settings.threat == score::Preset{}.threat);
  // [other] from priorities.md: leech weighted high, retaliation not sought; stages inherit them.
  CHECK(p.other.at("life_leech").weight == 1.0);
  CHECK(p.stages[3].settings.other.at("retaliation").weight == 0);
  CHECK(p.other.at("pet_bonus").when == std::vector<std::string>{"pets"});
  CHECK(p.stages[2].settings.skills.size() == 3);

  snap::Snapshot s;
  s.level = 3;
  CHECK(stage_name(p, s).empty());  // no mastery yet
  CHECK(&score::settings(p, s) == static_cast<const score::Settings*>(&p));
  learn(s, kShaman, 1);
  learn(s, kSwarm, 5);
  CHECK(stage_name(p, s) == "1-10");
  s.level = 12;
  CHECK(stage_name(p, s) == "1-10");  // no Occultist: still the Shaman stage
  learn(s, kOccultist, 1);
  CHECK(stage_name(p, s) == "10-35");
  s.level = 9;
  CHECK(stage_name(p, s) == "1-10");  // Occultist before 10: the level holds it back
  s.level = 35;
  CHECK(stage_name(p, s) == "35-93");
  s.level = 100;
  CHECK(stage_name(p, s) == "94+");
  CHECK(score::settings(p, s).skills == p.stages[3].settings.skills);
}

TEST_CASE("presets: stage and match errors") {
  std::string error;
  CHECK_FALSE(score::parse("[[stage]]\nmin_level = 3\n", error));
  CHECK(error == "line 1: [[stage]] needs a name");
  CHECK_FALSE(score::parse("[[stage]]\nname = \"a\"\nfoo = 1\n", error));
  CHECK(error == "line 3: unknown key stage.foo");
  CHECK_FALSE(score::parse("[[stage]]\nname = \"a\"\n[stage.damage]\nskils = []\n", error));
  CHECK(error == "line 4: unknown key stage.damage.skils");
  CHECK_FALSE(score::parse("[[stage]]\nname = \"a\"\nmin_level = 5\nmax_level = 4\n", error));
  CHECK_FALSE(score::parse("[[stage]]\nname = \"a\"\nmin_level = 2.5\n", error));
  CHECK_FALSE(score::parse("[[stage]]\nname = \"a\"\nrequires = [\"\"]\n", error));
  CHECK_FALSE(score::parse("[match]\nmasteries = \"x\"\n", error));
  CHECK_FALSE(score::parse("[match]\ndamage = [\"acidic\"]\n", error));
  CHECK(error == "line 2: damage: unknown damage type (acid, poison, vitality, ...)");
  CHECK_FALSE(score::parse("[match]\nmin_share = 2\n", error));
  CHECK_FALSE(score::parse("[preset]\nauto = 1\n", error));
  CHECK_FALSE(score::parse("[damage]\nskill_weights = \"most\"\n", error));

  // A stage overrides only what it states; later [score] text still lands in the preset itself.
  const auto p = score::parse("[[stage]]\nname = \"a\"\n[stage.score]\nother = 0\n", error);
  REQUIRE_MESSAGE(p, error);
  CHECK(p->other_weight == 0.25);
  CHECK(p->stages[0].settings.other_weight == 0);
  CHECK(p->stages[0].settings.damage_weight == 1);
  CHECK(p->auto_select);
  CHECK(score::parse("[preset]\nauto = false\n", error)->auto_select == false);
}

TEST_CASE("presets: Auto match by masteries and main damage types; main skills weighed by points (installed game; skips if absent)") {
  const arz::Database* db = game_db();
  if (!db) return;
  const auto p = plague_garden();
  REQUIRE(p.match_damage == std::vector<score::DamageKey>{{7, false}, {7, true}});
  snap::Snapshot s;
  s.level = 9;
  CHECK(score::match(p, s, *db).why == "no mastery");
  learn(s, kShaman, 5);
  learn(s, kSwarm, 16);
  learn(s, kVines, 1);
  // Devouring Swarm (devouringswarm1_buff.dbr) deals Vitality + Bleeding, Grasping Vines Physical + Bleeding.
  const auto m = score::match(p, s, *db);
  CHECK_FALSE(m.ok);
  CHECK(m.why.starts_with("masteries "));
  CHECK(m.why.find("Acid 0% + Poison 0% = 0% < 50% of main skill damage (") != std::string::npos);
  CHECK(score::match(score::Preset{}, s, *db).why == "no [match]");

  const auto skills = score::main_skills(s, score::settings(p, s), db);
  REQUIRE(skills.size() == 2);
  CHECK(skills[0].record == kSwarm);
  CHECK(skills[0].weight == doctest::Approx(16.0 / 17));
  CHECK(skills[1].weight == doctest::Approx(1.0 / 17));

  // Dreeg's Evil Eye (Acid + Poison, evileye1.dbr) as the top skill, Physical -> Acid on the gear for Vines.
  learn(s, kOccultist, 10);
  learn(s, kEvilEye, 22);
  s.conversions.push_back({2, 7, 1.0f});
  const auto profile = score::damage_profile(s, *db);
  double acid_poison = profile.at({7, false}) + profile.at({7, true});
  CHECK(acid_poison > 0.5);
  CHECK((profile.count({2, false}) == 0 || profile.at({2, false}) < 1e-9));  // Vines: Physical all converted
  CHECK(score::match(p, s, *db).ok);

  learn(s, kSoldier, 1);
  CHECK(score::match(p, s, *db).why.find("not in [match]") != std::string::npos);  // a third mastery: another build
}

TEST_CASE("presets: the block heading names the preset and stage") {
  const auto p = plague_garden();
  snap::Snapshot s;
  s.level = 5;
  s.defense_cap = 80;
  s.char_attributes = {{4, 1000, 0, 1000, 0}};
  learn(s, kShaman, 1);
  snap::Item cand;
  cand.id = 2;
  cand.stats = snap::ItemStats{{{4, 200, 0}}, {}, {}, {}};
  const auto lines = tooltip::compare(s, cand, 6, false, nullptr, p);
  REQUIRE(!lines.empty());
  CHECK(lines[0].text == "GD Compare - plague-garden - stage 1-10");
}

TEST_CASE("presets: freeze writes the settings in use, parse reads them back (installed game; skips if absent)") {
  const arz::Database* db = game_db();
  if (!db) return;
  snap::Snapshot s;
  s.character_name = "Wint\"er\\\n";  // quotes, backslashes and a newline survive
  s.level = 40;
  snap::Skill mastery;
  mastery.id = 1;
  mastery.record = "records/skills/playerclass06/_classtraining_class06.dbr";  // Shaman
  mastery.level = mastery.current_level = 20;
  snap::Skill swarm;
  swarm.id = 2;
  swarm.record = "records/skills/playerclass06/devouringswarm1.dbr";
  swarm.level = swarm.current_level = 12;
  s.skills = {mastery, swarm};
  std::string error;
  const auto chosen = score::parse(
      "[preset]\nname = \"x\"\n[match]\nmasteries = [\"records/skills/playerclass06/_classtraining_class06.dbr\"]\n"
      "damage = [\"acid\", \"poison\"]\nmin_share = 0.3\n[score]\nthreshold_width = 0.07\n[damage]\n"
      "conversion_warn = [\"vitality->*\", \"*->elemental\"]\n[enemy]\npoison = 33\n[uptime]\n\"records/a.dbr\" = 0.25\n"
      "[[stage]]\nname = \"s\"\nmin_level = 30\n[stage.thresholds]\nresist = 80\nlife = 1234.5\n[stage.survival.threat]\nfire = 0.3\n"
      "[stage.other]\npet_bonus = { weight = 0.7, when = [\"shield\", \"stage:s\"] }\nrun_speed = { weight = 0.4, when = \"stage:t\" }\n",
      error);
  REQUIRE_MESSAGE(chosen, error);
  const std::string text = score::freeze(*chosen, s, *db, "frozen-winter");
  const auto back = score::parse(text, error);
  REQUIRE_MESSAGE(back, std::string(error + "\n" + text));
  score::Settings expected = chosen->stages[0].settings;
  expected.skills = {swarm.record};
  expected.weigh_by_points = true;
  expected.stage.clear();                               // no stages in a frozen file:
  expected.other["pet_bonus"] = {0.7, {"shield"}};          // "stage:s" holds -> dropped
  expected.other["run_speed"] = {};                   // "stage:t" does not -> weight 0
  CHECK(static_cast<const score::Settings&>(*back) == expected);
  CHECK(back->name == "frozen-winter");
  CHECK(back->match_character == s.character_name);
  CHECK(back->masteries == std::vector<std::string>{mastery.record});
  CHECK(back->match_damage == chosen->match_damage);
  CHECK(back->match_share == 0.3);
  CHECK(back->stages.empty());
  // Only this character.
  CHECK(score::match(*back, s, *db).ok == score::match(*chosen, s, *db).ok);
  snap::Snapshot other = s;
  other.character_name = "someone";
  CHECK_FALSE(score::match(*back, other, *db).ok);
}

// Regression (adapter 0.7.0 scored this +2.3%): winterpoison, level 16, stage 10-35. Worn belt a02_waist001
// (Pierce 15, Fire 20, Cold 9, Lightning 9, Life +88, 7 armor) vs a Vigorous Chain Belt of Fortitude
// (a05_waist001: 24 armor, Physique +28, Life +8%; stats as its tooltip shows them). Before: Pierce 25,
// Cold 92, Fire 93, Lightning 106 uncapped (cap 80). After: Pierce 10, Fire 73 (7 below cap), Cold 83 and
// Lightning 97 (overcap 3 / 17). The guide puts caps + 25-30 overcap first until the base is closed
// (docs/build-ref/priorities.md §3), so the swap must score negative.
TEST_CASE("presets: plague-garden: +28 Physique / +8% Life does not beat 15 Pierce + 20 Fire res near cap (installed game; skips if absent)") {
  const arz::Database* db = game_db();
  if (!db) {
    MESSAGE("game not found; skipped");
    return;
  }
  std::ifstream f(fs::path(GDC_SOURCE_DIR) / "tests/fixtures/snapshots/lvl16-wp-before-belt.json");
  REQUIRE(f.good());
  std::stringstream ss;
  ss << f.rdbuf();
  const auto s = snap::from_json(ss.str());
  const auto p = plague_garden();
  REQUIRE(stage_name(p, s) == "10-35");

  snap::Item belt;
  belt.id = 677044;
  belt.record = "records/items/gearaccessories/waist/a05_waist001.dbr";
  belt.kind = "ItemEquipment";
  belt.stats.emplace();
  belt.stats->char_attributes = {{1, 28, 0}, {4, 0, 8}};  // Physique +28, Life +8%
  snap::CombatTotals armor;
  armor.type = 38;  // AbsorptionProtection: item armor (the worn belt's 7 Armor reads the same)
  armor.defense = 24;
  belt.stats->defense = {armor};

  std::vector<std::string> log;
  const auto lines = tooltip::compare(s, belt, 13, false, db, p, {}, &log);  // 13: belt (locations_for)
  REQUIRE_FALSE(lines.empty());
  double percent = 1e9;
  for (const auto& l : log)
    if (l.starts_with("vs Waist base: ")) percent = std::stod(l.substr(15));
  REQUIRE(percent < 1e9);
  for (const auto& l : log) MESSAGE(l);
  CHECK(percent < 0);
}

namespace {

// Every named preset the deploy installs (presets/*.toml but default.toml), in Auto's order.
std::vector<std::pair<std::string, score::Preset>> named_presets() {
  std::vector<std::pair<std::string, score::Preset>> out;
  for (const auto& e : fs::directory_iterator(fs::path(GDC_SOURCE_DIR) / "presets"))
    if (e.path().extension() == ".toml" && e.path().filename() != "default.toml")
      out.emplace_back(e.path().filename().string(), preset_file(e.path().filename().string()));
  std::sort(out.begin(), out.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
  std::stable_sort(out.begin(), out.end(), [](const auto& a, const auto& b) { return score::tried_first(a.second, b.second); });
  return out;
}

// The adapter's Auto choice (tooltip.cpp choose): the first named preset that fits, "" = none.
std::string auto_pick(const snap::Snapshot& s, const arz::Database& db) {
  for (const auto& [file, p] : named_presets())
    if (score::match(p, s, db).ok) return file;
  return "";
}

void equip(snap::Snapshot& s, int location, const char* record) {
  snap::Item it;
  it.location = location;
  it.record = record;
  s.equipped.push_back(it);
}

constexpr const char* kDemolitionist = "records/skills/playerclass02/_classtraining_class02.dbr";
constexpr const char* kOathkeeper = "records/skills/playerclass09/_classtraining_class09.dbr";

}  // namespace

TEST_CASE("presets: [match] archetype parses; Auto tries character presets, then fewer masteries") {
  std::string error;
  const auto p = score::parse("[match]\narchetype = \"pet\"\n", error);
  REQUIRE_MESSAGE(p, error);
  CHECK(p->match_archetype == "pet");
  CHECK_FALSE(score::parse("[match]\narchetype = \"summoner\"\n", error));
  CHECK(error == "line 2: match.archetype must be one of pet, retaliation, melee, ranged, caster");

  score::Preset frozen, own, generic;
  frozen.match_character = "winterofhell";
  frozen.masteries.assign(3, "x");
  own.masteries.assign(2, "x");
  generic.masteries.assign(10, "x");
  CHECK(score::tried_first(frozen, own));
  CHECK(score::tried_first(own, generic));
  CHECK_FALSE(score::tried_first(generic, own));
  CHECK_FALSE(score::tried_first(own, own));
  const auto order = named_presets();
  REQUIRE(order.size() == 5);
  CHECK(order[0].first == "plague-garden.toml");  // 2 masteries before the 10-mastery templates (file order)
  CHECK(order[1].first == "chaos-conversion-caster.toml");
  CHECK(order[4].first == "retaliation.toml");
}

TEST_CASE("presets: archetype from class skill points; templates picked by Auto (installed game; skips if absent)") {
  const arz::Database* db = game_db();
  if (!db) return;

  SUBCASE("caster: Demolitionist spells, Fire and Physical converted to Chaos") {
    snap::Snapshot s;
    s.level = 30;
    learn(s, kDemolitionist, 20);
    learn(s, "records/skills/playerclass02/grenado1.dbr", 10);    // Skill_AttackProjectile, Physical
    learn(s, "records/skills/playerclass02/blackwater1.dbr", 5);  // Skill_AttackProjectile, Fire
    CHECK(score::archetype(s, *db).name == "caster");
    CHECK(score::archetype(s, *db).why == "pet 0, retaliation 0, attack 15 (weapon 0)");
    CHECK(auto_pick(s, *db) == "");  // no conversion: not Chaos
    s.conversions = {{2, 10, 1.0f}, {6, 10, 1.0f}};  // Physical -> Chaos, Fire -> Chaos 100%
    CHECK(auto_pick(s, *db) == "chaos-conversion-caster.toml");
  }

  SUBCASE("melee / ranged: Soldier weapon attack; the R Hand class decides") {
    snap::Snapshot s;
    s.level = 30;
    learn(s, kSoldier, 20);
    learn(s, "records/skills/playerclass01/shieldhammer1.dbr", 12);  // Skill_AttackWave, weaponDamagePct, Physical
    equip(s, 0, "records/items/gearweapons/swords1h/a00_sword001.dbr");  // WeaponMelee_Sword
    CHECK(score::archetype(s, *db).name == "melee");
    CHECK(auto_pick(s, *db) == "physical-melee.toml");
    s.equipped.clear();
    equip(s, 0, "records/items/gearweapons/guns1h/a01_gun1h001.dbr");  // WeaponHunting_Ranged1h
    CHECK(score::archetype(s, *db).name == "ranged");
    CHECK(auto_pick(s, *db) == "");  // no ranged template
    learn(s, "records/skills/playerclass02/grenado1.dbr", 13);  // spell points outweigh: weapon 12 x 2 < 25
    CHECK(score::archetype(s, *db).name == "caster");
  }

  SUBCASE("pet: summons whose spawned record is Class Pet; modifiers count through modifier_ids") {
    snap::Snapshot s;
    s.level = 30;
    learn(s, kOccultist, 20);
    learn(s, kEvilEye, 10);
    snap::Skill& hound = learn(s, "records/skills/playerclass03/summon_hellhound1.dbr", 3);  // -> pet_hellhound_a01 (Pet)
    const std::uint32_t hound_id = hound.id;
    const std::uint32_t mod_id = learn(s, "records/skills/playerclass03/summon_hellhound2_petmodifier.dbr", 8).id;
    CHECK(score::archetype(s, *db).name == "caster");  // unlinked modifier: pet 3 < attack 10
    for (auto& k : s.skills)
      if (k.id == hound_id) k.modifier_ids = {mod_id};
    CHECK(score::archetype(s, *db).why == "pet 11, retaliation 0, attack 10 (weapon 0)");
    CHECK(auto_pick(s, *db) == "pet.toml");

    snap::Snapshot traps;
    traps.level = 30;
    learn(traps, kDemolitionist, 20);
    learn(traps, "records/skills/playerclass02/mortartrap1.dbr", 12);  // -> mortartrap01: PetPlayerScaling
    learn(traps, "records/skills/playerclass02/grenado1.dbr", 1);
    CHECK(score::archetype(traps, *db).name == "caster");
  }

  SUBCASE("retaliation: Presence of Virtue's buff has retaliation damage") {
    snap::Snapshot s;
    s.level = 30;
    learn(s, kOathkeeper, 20);
    learn(s, kSoldier, 10);
    learn(s, "records/skills/playerclass09/presenceofvirtue1.dbr", 12);  // buff: retaliationPhysicalMin
    learn(s, "records/skills/playerclass01/shieldhammer1.dbr", 8);
    equip(s, 0, "records/items/gearweapons/swords1h/a00_sword001.dbr");
    CHECK(score::archetype(s, *db).why == "pet 0, retaliation 12, attack 8 (weapon 8)");
    CHECK(auto_pick(s, *db) == "retaliation.toml");
    for (auto& k : s.skills)
      if (k.record.ends_with("shieldhammer1.dbr")) k.level = k.current_level = 13;
    CHECK(score::archetype(s, *db).name == "melee");
  }

  SUBCASE("the two real characters keep their presets") {
    // winterpoison, level 15 (store #0021): Evil Eye, Swarm, Pox, Vines; Blood of Dreeg 1 point. Swarm still
    // deals Vitality + Bleeding unconverted, so plague-garden does not fit yet (as before) and no template
    // takes it: GDCompare.preset.toml scores, as before the templates.
    const auto wp = fixture_snapshot("lvl15-wp-before-feet.json");
    CHECK(score::archetype(wp, *db).why == "pet 0, retaliation 1, attack 19 (weapon 0)");
    CHECK(score::archetype(wp, *db).name == "caster");
    CHECK(auto_pick(wp, *db) == "");
    // winterofhell, level 96: Shadow Strike + mods and the Nightblade pool attacks (weapon), Doom Bolt, Blood
    // of Dreeg. Melee, but not mostly Physical: physical-melee does not fit either.
    const auto woh = fixture_snapshot("lvl96-s5-after-ring-c031.json");
    CHECK(score::archetype(woh, *db).why == "pet 0, retaliation 18, attack 43 (weapon 31)");
    CHECK(score::archetype(woh, *db).name == "melee");
    CHECK(auto_pick(woh, *db) == "");
  }
}
