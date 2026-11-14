#include "gdc/combat_formulas.h"

#include <algorithm>

namespace gdc::formulas {
namespace {

double ability(double level, double value, double modifier, double attribute, double reduction) {
  const double v = (value + level * 12 + attribute * 0.5) * (1 + modifier / 100) + 53;
  return std::max(0.0, v - reduction);
}

double attr(const snap::Snapshot& s, int type) {
  for (const auto& a : s.char_attributes)
    if (a.type == type) return a.total;
  return 0;
}

}  // namespace

double offensive_ability(double level, double oa, double oa_modifier, double cunning, double reduction) {
  return ability(level, oa, oa_modifier, cunning, reduction);
}

double defensive_ability(double level, double da, double da_modifier, double physique, double reduction) {
  return ability(level, da, da_modifier, physique, reduction);
}

double probability_to_hit(double oa, double da) {
  return (((oa / ((da / 3.5) + oa)) * 300) * 0.3) + (((((oa * 3.25) + 10000) - (da * 3.25)) / 100) * 0.7) - 50;
}

double critical_chance(double pth, const snap::PthTable& t) {
  return std::max(0.0, (pth - t.threshold[1]) / std::max<double>(t.crit_divisor_min, pth));
}

Hit expected_hit(double pth, const snap::PthTable& t, double crit_bonus) {
  pth = std::max<double>(pth, t.minimum);
  const double span = std::max<double>(t.crit_divisor_min, pth);
  if (span <= 0) return {};
  Hit h;
  h.chance = std::min(pth, span) / span;
  if (t.threshold[0] <= 0) {  // no table
    h.multiplier = h.chance;
    return h;
  }
  if (pth <= t.threshold[0]) {
    h.multiplier = h.chance * pth / t.threshold[0];
    return h;
  }
  h.crit_chance = std::max(0.0, pth - t.threshold[1]) / span;
  // Bands of x up to pth: [0, threshold 2] at modifier 1, then one per threshold reached.
  double from = 0, modifier = t.damage_modifier[0], sum = 0;
  for (int k = 1; k < 6 && t.threshold[k] > 0 && t.threshold[k] < pth; ++k) {
    sum += (t.threshold[k] - from) * modifier;
    from = t.threshold[k];
    modifier = t.damage_modifier[k] + (t.damage_modifier[k] > 1 ? crit_bonus : 0);
  }
  h.multiplier = (sum + (pth - from) * modifier) / span;
  return h;
}

double life_regen(const snap::Snapshot& s) { return attr(s, 6) + (attr(s, 1) - 50) / 25; }

double mana_regen(const snap::Snapshot& s) {
  const double intelligence = attr(s, 3);
  return attr(s, 7) * (intelligence / 384 + 1) + (intelligence - 50) / 100 + 6.5;
}

double offensive_ability(const snap::Snapshot& s) {
  return offensive_ability(s.level, attr(s, 21), attr(s, 22), attr(s, 2));
}

double defensive_ability(const snap::Snapshot& s) {
  return defensive_ability(s.level, attr(s, 23), attr(s, 24), attr(s, 1));
}

}  // namespace gdc::formulas
