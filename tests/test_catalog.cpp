// Every catalog build as the wizard writes it, on a synthetic character of its first class (build_harness.h) with
// the build's skills learned: defenses always count, more of a stat is never worse, and the playstyle moves
// weight between Damage and Survivability the way its label says. Installed game; skips if absent.
#include <doctest/doctest.h>

#include <algorithm>

#include "build_harness.h"
#include "buildpreset.h"
#include "catalog.h"
#include "gdc/text.h"

using namespace gdc;
using namespace gdc::test;

namespace {

constexpr int kTypes[] = {2, 4, 5, 6, 7, 8, 9, 10, 11};  // score.cpp kThreatTypes without bleeding

Build character(const setup::Entry& e, setup::Playstyle ps) {
  const auto masteries = setup::code_masteries(e.classes[0]);
  setup::BuildAnswers a = setup::from_entry(e, masteries, "synthetic");
  a.playstyle = ps;
  Build b(parse_preset(setup::preset_text(a)), e.category == "ranged" ? kGun : kSword);
  if (!b.db) return b;
  std::uint32_t points = 50;
  for (const int m : masteries) b.learn(setup::mastery_record(m).c_str(), points), points = 32;
  for (const auto& s : a.skills)
    if (s != "records/skills/default/defaultweaponattack.dbr") b.learn(s.c_str(), 10);
  return b;
}

}  // namespace

TEST_CASE("catalog: every build on a synthetic character of its class (installed game; skips if absent)") {
  if (!game_db()) return;
  std::string error;
  const auto catalog = setup::load_catalog(read(fs::path(GDC_SOURCE_DIR) / "catalog/builds.toml"), error);
  REQUIRE_MESSAGE(catalog, error);
  fs::path dir;
  if (const char* d = std::getenv("GD_DIR")) dir = d;
  else dir = fs::path(std::getenv("HOME")) / ".local/share/Steam/steamapps/common/Grim Dawn";
  const auto text = text::Table::open_game(dir);
  for (const auto& e : *catalog) {
    INFO(e.id);
    for (const auto& c : e.classes) CHECK_MESSAGE(!text.get("tagSkillClassName" + c).empty(), "no such class ", c);
    Build b = character(e, setup::Playstyle::kBalanced);
    REQUIRE_MESSAGE(score::match(b.preset, b.s, *b.db).ok, score::match(b.preset, b.s, *b.db).why);
    if (std::any_of(b.s.skills.begin(), b.s.skills.end(), [](const snap::Skill& k) {
          return k.record.find("/_classtraining_") == std::string::npos && k.record.find("/default/") == std::string::npos;
        }))
      b.common();
    else
      b.defenses();  // summoners learn only the default attack here: +1 to all skills has nothing to raise
    double best = 0;
    for (const int t : kTypes) {
      INFO("type ", t);
      const double lo = b.dmg(t, 10, 10), hi = b.dmg(t, 40, 40);
      CHECK(hi >= lo - 1e-9);  // more is never worse
      CHECK(lo >= -1e-9);      // a damage bonus is never a loss
      best = std::max(best, hi);
    }
    CHECK(best > 0);  // some damage type pays
    CHECK(b.chr(4, 600) >= b.chr(4, 300));

    // Tanky values life against damage more than glass cannon does.
    const Build tank = character(e, setup::Playstyle::kTanky), glass = character(e, setup::Playstyle::kGlass);
    double t_dmg = 0, g_dmg = 0;
    for (const int t : kTypes) t_dmg = std::max(t_dmg, tank.dmg(t, 40, 40)), g_dmg = std::max(g_dmg, glass.dmg(t, 40, 40));
    REQUIRE(t_dmg > 0);
    REQUIRE(g_dmg > 0);
    CHECK(tank.chr(4, 300) / t_dmg > glass.chr(4, 300) / g_dmg);
  }
}

TEST_CASE("skill_kind on known records (installed game; skips if absent)") {
  const arz::Database* db = game_db();
  if (!db) return;
  const auto has = [](const score::SkillKind& k, const char* name) {
    return std::find(k.types.begin(), k.types.end(), *score::damage_key(name)) != k.types.end();
  };
  const auto blade = score::skill_kind(*db, "records/skills/playerclass01/bladearc1.dbr", 1);  // Skill_AttackWeapon
  CHECK(blade.main);
  CHECK(blade.weapon);
  const auto grenado = score::skill_kind(*db, "records/skills/playerclass02/grenado1.dbr", 1);  // Skill_AttackProjectile
  CHECK(grenado.main);
  CHECK(has(grenado, "physical"));
  CHECK_FALSE(score::skill_kind(*db, "records/skills/playerclass03/summon_hellhound1.dbr", 1).main);  // a pet
  CHECK_FALSE(score::skill_kind(*db, "records/skills/playerclass09/presenceofvirtue1.dbr", 1).main);  // an aura
  CHECK_FALSE(score::skill_kind(*db, "records/skills/playerclass01/_classtraining_class01.dbr", 1).main);
  CHECK_FALSE(score::skill_kind(*db, "records/nothing.dbr", 1).main);
}
