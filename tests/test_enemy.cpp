#include <doctest/doctest.h>

#include <algorithm>
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
  const std::string names[] = {"Normal", "NormalVeteran", "Elite", "EliteVeteran", "Ultimate", "Ascendant"};
  int rows = 0;
  while (std::getline(f, line)) {
    std::istringstream in(line);
    std::string difficulty;
    double level, monsters, oa, da, hit, phys, with_hit, rr, share, crit, bosses, boss_rr, boss_share;
    in >> difficulty >> level >> monsters >> oa >> da >> hit >> phys >> with_hit >> rr >> share >> crit >> bosses >>
        boss_rr >> boss_share;
    REQUIRE(!in.fail());
    const auto d = std::find(std::begin(names), std::end(names), difficulty) - std::begin(names);
    REQUIRE(d < enemy::kDifficulties);
    const enemy::Enemy e = enemy::at(level, static_cast<int>(d));
    CHECK(e.oa == oa);
    CHECK(e.da == da);
    CHECK(e.hit == hit);
    CHECK(e.hit_physical == phys);
    CHECK(e.rr == rr);
    CHECK(e.rr_share == share);
    CHECK(e.crit_damage == crit);
    CHECK(e.boss_rr == boss_rr);
    CHECK(e.boss_rr_share == boss_share);
    ++rows;
  }
  CHECK(rows == 21 * enemy::kDifficulties);
  CHECK(enemy::at(0, 0).oa == enemy::at(1, 0).oa);
  CHECK(enemy::at(500, 2).da == enemy::at(100, 2).da);
  CHECK(enemy::at(12.5, 1).oa == doctest::Approx((enemy::at(10, 1).oa + enemy::at(15, 1).oa) / 2));
  CHECK(enemy::at(50, 9).hit == enemy::at(50, 5).hit);  // out of range: the last row
  // Bosses lower a resist more often and by more than the median monster (re/tools/enemy_table.py).
  CHECK(enemy::at(100, 0).boss_rr > enemy::at(100, 0).rr);
  CHECK(enemy::at(100, 0).boss_rr_share > enemy::at(100, 0).rr_share);
  // Only Ascendant adds crit damage (ultimateChallangeAdjustment offensiveCritDamageModifier 40).
  for (int d = 0; d < enemy::kDifficulties; ++d) CHECK(enemy::at(50, d).crit_damage == (d == 5 ? 40 : 0));
}

TEST_CASE("enemy: hits, OA and DA grow with the difficulty; the snapshot picks the row") {
  for (double level : {1.0, 27.0, 60.0, 100.0})
    // Normal < Elite < Ultimate, and each with the ramp above it (Elite Veteran: +25% +40% damage, more than
    // Ultimate's +40%).
    for (auto [a, b] : {std::pair{0, 2}, {2, 4}, {0, 1}, {2, 3}, {4, 5}}) {
      const enemy::Enemy lo = enemy::at(level, a), hi = enemy::at(level, b);
      CHECK(hi.hit > lo.hit);
      CHECK(hi.hit_physical > lo.hit_physical);
      CHECK(hi.oa > lo.oa);
      CHECK(hi.da > lo.da);
    }
  snap::Snapshot s;
  CHECK(enemy::difficulty(s) == 0);  // not read (-1): Normal
  s.difficulty_ramp = 1;             // GetChallengeAdjustment: challengeAdjustment below Ultimate (Veteran)
  CHECK(enemy::difficulty(s) == 1);
  s.difficulty = 1;
  CHECK(enemy::difficulty(s) == 3);
  s.difficulty = 2;                  // ultimateChallangeAdjustment on Ultimate (Ascendant)
  CHECK(enemy::difficulty(s) == 5);
  s.difficulty_ramp = 0;
  CHECK(enemy::difficulty(s) == 4);
  s.difficulty = 7;
  CHECK(enemy::difficulty(s) == 4);
  // re/EnemyResistByDifficulty.tsv
  std::ifstream f(std::filesystem::path(GDC_SOURCE_DIR) / "re/EnemyResistByDifficulty.tsv");
  REQUIRE(f.good());
  std::string line;
  std::getline(f, line);
  const int types[] = {2, 4, 5, 6, 7, 8, 9, 10, 11, 15};
  for (int d = 0; d < enemy::kDifficulties; ++d) {
    REQUIRE(std::getline(f, line));
    std::istringstream in(line);
    std::string name;
    in >> name;
    for (int t : types) {
      double v = -1;
      in >> v;
      CHECK(enemy::resist(t, d) == v);
    }
  }
  CHECK(enemy::resist(42, 2) == 0);
  // re/PlayerResistByDifficulty.tsv, one row per game difficulty; Veteran / Ascendant take the same.
  std::ifstream g(std::filesystem::path(GDC_SOURCE_DIR) / "re/PlayerResistByDifficulty.tsv");
  REQUIRE(g.good());
  std::getline(g, line);
  for (int base = 0; base < 3; ++base) {
    REQUIRE(std::getline(g, line));
    std::istringstream in(line);
    std::string name;
    in >> name;
    for (int t : {4, 5, 6, 7, 8, 9, 10, 11, 15}) {
      double v = -1;
      in >> v;
      CHECK(enemy::penalty(t, base * 2) == v);
      CHECK(enemy::penalty(t, base * 2 + 1) == v);
    }
  }
  CHECK(enemy::penalty(2, 4) == 0);  // physical: none
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
