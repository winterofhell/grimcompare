#pragma once
// Median Common / Champion / Hero monster per level and difficulty (1.3.1.1 + GDX1..3), from
// re/EnemyByLevel.tsv, re/EnemyResistByDifficulty.tsv and re/PlayerResistByDifficulty.tsv
// (re/tools/enemy_table.py, docs/MECHANICS.md §8, §9).
#include "gdc/snapshot.h"

namespace gdc::enemy {

// Normal, Normal Veteran, Elite, Elite Veteran, Ultimate, Ascendant: the game difficulty x 2, + 1 while the
// difficulty ramp is above 0 (GameEngine::GetChallengeAdjustment 0x2F1C20 applied on top).
constexpr int kDifficulties = 6;

struct Enemy {
  double oa = 0, da = 0;    // combatformulas.dbr ability equations on the bio record
  double hit = 0;           // flat direct damage of one skill hit
  double hit_physical = 0;
  double rr = 0;            // what a monster debuff takes off a base resist, among the monsters that lower it
  double rr_share = 0;      // share of monsters that lower a given base resist
  double crit_damage = 0;   // % crit damage the difficulty adds (offensiveCritDamageModifier)
  double boss_rr = 0;       // rr and rr_share over the Quest, Boss (Nemesis) and SuperBoss records
  double boss_rr_share = 0;
};

int difficulty(const snap::Snapshot& s);

Enemy at(double level, int difficulty);

// What gameengine.dbr monsterAttributePak adds to every monster's resist (CombatAttributeType) at that difficulty.
double resist(int type, int difficulty);

// What gameengine.dbr playerAttributePak takes off the player's resist (CombatAttributeType) at that difficulty.
// Already in the snapshot's totals; used for what the next difficulty takes off on top.
double penalty(int type, int difficulty);

}  // namespace gdc::enemy
