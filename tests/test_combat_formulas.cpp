#include <doctest/doctest.h>

#include <cmath>
#include <fstream>
#include <sstream>
#include <string>

#include "gdc/combat_formulas.h"

using namespace gdc;

namespace {

snap::Snapshot fixture(const char* name) {
  std::ifstream f(std::string(GDC_SOURCE_DIR) + "/tests/fixtures/snapshots/" + name);
  REQUIRE(f.good());
  std::stringstream ss;
  ss << f.rdbuf();
  return snap::from_json(ss.str());
}

}  // namespace

TEST_CASE("OA/DA from a level-96 snapshot match the character sheet") {
  // Sheet screenshot 2026-11-14 (same session as the snapshot): OA 2946, DA 2328.
  const auto s = fixture("lvl96-before-ring-swap.json");
  CHECK(std::floor(formulas::offensive_ability(s)) == 2946);
  CHECK(std::floor(formulas::defensive_ability(s)) == 2328);
}

TEST_CASE("OA/DA equation and reduction clamp") {
  // (702 + 96*12 + 1114.46*0.5) * 1.2 + 53
  CHECK(formulas::offensive_ability(96, 702, 20, 1114.46) == doctest::Approx(2946.476));
  CHECK(formulas::offensive_ability(96, 702, 20, 1114.46, 100) == doctest::Approx(2846.476));
  CHECK(formulas::offensive_ability(1, 0, 0, 0, 1e6) == 0);
  CHECK(formulas::defensive_ability(1, 0, 0, 0) == doctest::Approx(12 + 53));
}

TEST_CASE("probability to hit") {
  // OA == DA: (1/(1/3.5+1))*300*0.3 + 100*0.7 - 50 = 70 + 20 = 90
  CHECK(formulas::probability_to_hit(1000, 1000) == doctest::Approx(90));
  CHECK(formulas::probability_to_hit(2000, 1000) > formulas::probability_to_hit(1000, 1000));
}

TEST_CASE("critical chance") {
  // records/game/combatformulas.dbr pthMinimum / pthThreshold1..6 / pthDamageModifier1..6;
  // crit_divisor_min 100 = CombatManager::CombatManager.
  const snap::PthTable t{55, 100, {70, 90, 105, 120, 130, 135}, {1.0f, 1.1f, 1.2f, 1.3f, 1.4f, 1.5f}};
  CHECK(formulas::critical_chance(80, t) == 0);
  CHECK(formulas::critical_chance(95, t) == doctest::Approx(0.05));        // pth < 100: divided by 100
  CHECK(formulas::critical_chance(118.35, t) == doctest::Approx(0.2395).epsilon(1e-3));  // divided by pth
}

TEST_CASE("regeneration from a level-96 snapshot matches the character sheet") {
  // Sheet page II, 2026-11-14 (same session as the snapshot): Health Regeneration 86.04, Energy 24.08.
  const auto s = fixture("lvl96-before-ring-swap.json");
  CHECK(std::abs(formulas::life_regen(s) - 86.04) < 0.01);
  CHECK(std::abs(formulas::mana_regen(s) - 24.08) < 0.01);
}

TEST_CASE("phase 2.1 snapshot: weapon base damage, expected-value chance stats, PTH table") {
  // F9 2026-11-14 with the sheet open, hovering a junk axe; tooltips: axe 76-207 Physical, 44-57 Pierce,
  // "30% Chance of +315% Pierce Damage"; both daggers 55-95 Chaos.
  const auto s = fixture("lvl96-phase21-axe-hover.json");
  const auto& axe = s.tooltip_items.at(0);
  REQUIRE(axe.record == "records/items/gearweapons/axe1h/a13_axe001.dbr");
  const auto& dmg = axe.stats->offensive_damage;
  REQUIRE(!dmg.empty());
  CHECK(dmg[0].type == 1);  // CombatAttributeDamage_BasePhysical
  CHECK(dmg[0].min == 76);
  CHECK(dmg[0].max == 207);
  CHECK(axe.stats->offensive_modifiers.at(0).damage_modifier == doctest::Approx(0.30 * 315));
  for (const auto& it : s.equipped)
    if (it.location == 0 || it.location == 1) {
      CHECK(it.stats->offensive_damage.at(0).min == 55);
      CHECK(it.stats->offensive_damage.at(0).max == 95);
    }
  CHECK(s.pth.threshold[1] == 90);
  CHECK(s.pth.crit_divisor_min == 100);
  CHECK(std::floor(formulas::offensive_ability(s)) == 2946);
  CHECK(std::abs(formulas::life_regen(s) - 86.04) < 0.01);
}

TEST_CASE("swap fixture: two max-poison items raise Poison 25 -> 91 (cap 80 + 11, raw 105)") {
  // F9 2026-11-14: ring (location 5) and chest (8) swapped for b102e_ring / a11_torso004; sheet hover
  // "91% (+14% over maximum)". Character raw Poison moves by exactly the items' Poison delta.
  const auto before = fixture("lvl96-phase21-axe-hover.json");
  const auto after = fixture("lvl96-swap-poison-ring-chest.json");
  constexpr int kPoison = 7;
  auto row = [](const snap::Snapshot& s) -> const snap::CombatTotals& {
    for (const auto& c : s.defense)
      if (c.type == kPoison) return c;
    FAIL("no Poison row");
    return s.defense.front();
  };
  CHECK(row(before).defense == 25);
  CHECK(row(before).processed == 25);
  CHECK(row(after).defense == 105);
  CHECK(row(after).processed == 91);
  CHECK(row(after).cap_bonus == 11);
  CHECK(after.defense_cap + row(after).cap_bonus == 91);
  auto items_poison = [](const snap::Snapshot& s) {
    double sum = 0;
    for (const auto& it : s.equipped) {
      const snap::ItemStats* parts[] = {it.stats ? &*it.stats : nullptr,
                                        it.relic && it.relic->stats ? &*it.relic->stats : nullptr,
                                        it.augment && it.augment->stats ? &*it.augment->stats : nullptr};
      for (const auto* st : parts)
        if (st)
          for (const auto& c : st->defense)
            if (c.type == kPoison) sum += c.defense;
    }
    return sum;
  };
  CHECK(items_poison(after) - items_poison(before) == 80);
  CHECK(row(after).defense - row(before).defense == 80);
}
