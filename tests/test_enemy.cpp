#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#include "gdc/combat_formulas.h"
#include "gdc/enemy.h"

using namespace gdc;

TEST_CASE("enemy: the table is re/EnemyByLevel.tsv") {
  std::ifstream f(std::filesystem::path(GDC_SOURCE_DIR) / "re/EnemyByLevel.tsv");
  REQUIRE(f.good());
  std::string line;
  std::getline(f, line);  // header
  int rows = 0;
  while (std::getline(f, line)) {
    std::istringstream in(line);
    double level, monsters, oa, da, hit, phys;
    in >> level >> monsters >> oa >> da >> hit >> phys;
    REQUIRE(!in.fail());
    const enemy::Enemy e = enemy::at(level);
    CHECK(e.oa == oa);
    CHECK(e.da == da);
    CHECK(e.hit == hit);
    CHECK(e.hit_physical == phys);
    ++rows;
  }
  CHECK(rows == 21);
  CHECK(enemy::at(0).oa == enemy::at(1).oa);
  CHECK(enemy::at(500).da == enemy::at(100).da);
  CHECK(enemy::at(12.5).oa == doctest::Approx((enemy::at(10).oa + enemy::at(15).oa) / 2));
}

namespace {
snap::PthTable table() {  // records/game/combatformulas.dbr 1.3.1.1
  snap::PthTable t;
  t.minimum = 55;
  t.crit_divisor_min = 100;
  const float thr[] = {70, 90, 105, 120, 130, 135}, mod[] = {1.0f, 1.1f, 1.2f, 1.3f, 1.4f, 1.5f};
  for (int i = 0; i < 6; ++i) t.threshold[i] = thr[i], t.damage_modifier[i] = mod[i];
  return t;
}
}  // namespace

TEST_CASE("formulas: expected hit follows CalculateDamageModifier") {
  const snap::PthTable t = table();
  // Below the minimum: clamped to 55, glancing (55 / 70).
  formulas::Hit h = formulas::expected_hit(20, t, 0);
  CHECK(h.chance == doctest::Approx(0.55));
  CHECK(h.multiplier == doctest::Approx(0.55 * 55 / 70));
  CHECK(h.crit_chance == 0);
  // 80: hits 80%, no crit, no glancing.
  h = formulas::expected_hit(80, t, 0.5);
  CHECK(h.multiplier == doctest::Approx(0.8));
  // 95: 5 of 100 rolls crit at tier 2 (1.1 + bonus).
  h = formulas::expected_hit(95, t, 0.5);
  CHECK(h.crit_chance == doctest::Approx(0.05));
  CHECK(h.multiplier == doctest::Approx((90 + 5 * 1.6) / 100));
  // 150: every tier by its own band; past 135 only top-tier crits are added.
  h = formulas::expected_hit(150, t, 0);
  CHECK(h.chance == 1);
  CHECK(h.multiplier == doctest::Approx((90 + 15 * 1.1 + 15 * 1.2 + 10 * 1.3 + 5 * 1.4 + 15 * 1.5) / 150));
  const formulas::Hit more = formulas::expected_hit(160, t, 0);
  const double added = more.multiplier * 160 - h.multiplier * 150;
  CHECK(added == doctest::Approx(10 * 1.5));
  // Monotone in pth, continuous at the glancing threshold.
  double last = 0;
  for (double pth = 0; pth <= 200; pth += 0.5) {
    const double m = formulas::expected_hit(pth, t, 0.3).multiplier;
    const bool monotone = m >= last - 1e-12;
    CHECK(monotone);
    last = m;
  }
  CHECK(formulas::expected_hit(70.001, t, 0).multiplier == doctest::Approx(0.7).epsilon(1e-4));
  // No table: the chance alone.
  snap::PthTable none;
  none.minimum = 55, none.crit_divisor_min = 100;
  CHECK(formulas::expected_hit(80, none, 1).multiplier == doctest::Approx(0.8));
}
