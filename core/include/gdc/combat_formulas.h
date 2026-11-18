#pragma once
// Equations from records/game/combatformulas.dbr (1.3.1.1). The engine's own evaluators
// (CombatManager::DesignerCalculateOffensiveAbility / DefensiveAbility / ProbabilityToHit, 0x121630 / 0x1216E0 /
// 0x121790) write their inputs into CombatManager, so the adapter never calls them.
// shortcut: equations transcribed from the 1.3.1.1 record — revisit when core reads them from the DB.
#include "gdc/snapshot.h"

namespace gdc::formulas {

// offensiveAbilityEquation from the DB, minus OA reduction.
double offensive_ability(double level, double oa, double oa_modifier, double cunning, double reduction = 0);
// defensiveAbilityEquation: same shape with strengthDV (Physique) and the DA attributes.
double defensive_ability(double level, double da, double da_modifier, double physique, double reduction = 0);
// probabilityToHitEquation from combatformulas.dbr.
double probability_to_hit(double oa, double da);

// DesignerCalculateCriticalChance 0x686D0 as a fraction.
double critical_chance(double pth, const snap::PthTable& t);

// One attack, as CombatManager::TakeAttack 0x11D950 resolves it.
struct Hit {
  double chance = 0;
  double crit_chance = 0;
  double multiplier = 0;   // misses 0, glancing, crit tiers + crit_bonus
};
Hit expected_hit(double pth, const snap::PthTable& t, double crit_bonus);

// Regen per second, playerresourcebehavior.dbr equations.
double life_regen(const snap::Snapshot& s);
double mana_regen(const snap::Snapshot& s);

double offensive_ability(const snap::Snapshot& s);
double defensive_ability(const snap::Snapshot& s);

}  // namespace gdc::formulas
