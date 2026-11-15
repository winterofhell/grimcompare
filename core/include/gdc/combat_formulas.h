#pragma once
// Equations from records/game/combatformulas.dbr (1.3.1.1). The engine's own evaluators
// (CombatManager::DesignerCalculateOffensiveAbility / DefensiveAbility / ProbabilityToHit, 0x121630 / 0x1216E0 /
// 0x121790) write their inputs into CombatManager, so the adapter never calls them.
// shortcut: equations transcribed from the 1.3.1.1 record — revisit when core reads them from the DB.
#include "gdc/snapshot.h"

namespace gdc::formulas {

// offensiveAbilityEquation:
//   (offensiveAbilityDV + (characterLevelDV * 12) + ((dexterityDV + bonusDV) * 0.5))
//     * (1 + (offensiveAbilityModifierDV / 100)) + 53
// DesignerCalculateOffensiveAbility subtracts the OA reduction (type 22) and clamps at 0. bonusDV 0 reproduces
// the sheet (2946 at level 96).
double offensive_ability(double level, double oa, double oa_modifier, double cunning, double reduction = 0);
// defensiveAbilityEquation: same shape with strengthDV (Physique) and the DA attributes.
double defensive_ability(double level, double da, double da_modifier, double physique, double reduction = 0);
// probabilityToHitEquation:
//   ((((OA / ((DA / 3.5) + OA)) * 300) * 0.3) + (((((OA * 3.25) + 10000) - (DA * 3.25)) / 100) * 0.7)) - 50
double probability_to_hit(double oa, double da);

// Character::DesignerCalculateCriticalChance (0x686D0), as a fraction:
//   max(0, (pth - pthThreshold2) / max(CombatManager+0x27C, pth))
// pth against the target's DA; the sheet's "Chance to Crit" uses the last attacked monster.
double critical_chance(double pth, const snap::PthTable& t);

// One attack against a target, as CombatManager::TakeAttack (0x11D950) resolves it through
// CombatManager::CalculateDamageModifier (0x120B10), Game.dll 1.3.1.1:
//   pth = max(probabilityToHitEquation, pthMinimum)   (DesignerCalculateProbabilityToHit 0x121790)
//   x   = uniform roll x max(CombatManager+0x27C, pth);  x > pth: miss (only possible below 100)
//   pth <= pthThreshold1: damage x normalPTHEquation (probabilityToHitDV/70, combatformulas.dbr)
//   else damage x pthDamageModifier of the highest threshold k in 2..6 with x > pthThreshold_k (1 below
//   threshold 2). So pth past the last threshold only adds top-tier crits.
//   A modifier above 1 gets the attacker's crit damage bonus added (TakeAttack 0x11ECBD: xmm10 > 1.0 ->
//   += ParametersCombat+0xA0), i.e. the bonus is added to every crit tier.
// No thresholds (tests): no tiers, multiplier = chance.
struct Hit {
  double chance = 0;
  double crit_chance = 0;
  double multiplier = 0;   // misses 0, glancing, crit tiers + crit_bonus
};
Hit expected_hit(double pth, const snap::PthTable& t, double crit_bonus);

// records/game/playerresourcebehavior.dbr, per second (elapsedTime = 1):
//   lifeRegenEquation (lifeRegen + ((physique-50)/25)) * elapsedTime
//   manaRegenEquation (((manaRegen*((intelligence/384)+1))+((intelligence-50)/100))+6.5) * elapsedTime
// lifeRegen / manaRegen = types 6 / 7 total (includes %). CharacterBio::GetLife/ManaRegenValue (0x8B020 /
// 0x8B080) write into CharacterBio, so the adapter does not call them.
double life_regen(const snap::Snapshot& s);
double mana_regen(const snap::Snapshot& s);

double offensive_ability(const snap::Snapshot& s);
double defensive_ability(const snap::Snapshot& s);

}  // namespace gdc::formulas
