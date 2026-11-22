#pragma once
// Median monster per level and difficulty (MECHANICS §8, §9).
#include <map>
#include <string>
#include <vector>

#include "gdc/snapshot.h"

namespace gdc::enemy {

// Difficulty x 2, plus 1 while ramp is up.
constexpr int kDifficulties = 6;

struct Enemy {
  double oa = 0, da = 0;    // combatformulas.dbr ability equations on the bio record
  double hit = 0;           // flat direct damage of one skill hit
  double hit_physical = 0;
  double rr = 0;            // what monster debuffs take off a base resist
  double rr_share = 0;      // share of monsters that lower a given base resist
  double crit_damage = 0;   // % crit damage the difficulty adds (offensiveCritDamageModifier)
  double boss_rr = 0;       // rr and rr_share over Quest, Boss and SuperBoss records
  double boss_rr_share = 0;
};

int difficulty(const snap::Snapshot& s);

Enemy at(double level, int difficulty);

// CombatAttributeType -> mean share of the monsters' largest hit (a skill or the weapon attack, direct + DoT), the default
// incoming damage mix.
std::map<int, double> threat(double level, int difficulty);

// Difficulty's monsterAttributePak resist bonus (gameengine.dbr).
double resist(int type, int difficulty);

// Share of monsters a racialBonusRace list applies to.
double race_share(const std::vector<std::string>& races);

// playerAttributePak resist penalty, for the next difficulty's lookahead.
double penalty(int type, int difficulty);

}  // namespace gdc::enemy
