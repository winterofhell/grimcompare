#pragma once
// Median Common / Champion / Hero monster per level (1.3.1.1 + GDX1..3), from re/EnemyByLevel.tsv
// (re/tools/enemy_table.py). No difficulty adjustment.
namespace gdc::enemy {

struct Enemy {
  double oa = 0, da = 0;    // combatformulas.dbr ability equations on the bio record
  double hit = 0;           // flat direct damage of one skill hit
  double hit_physical = 0;
};

Enemy at(double level);

}  // namespace gdc::enemy
