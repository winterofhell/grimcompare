#include "gdc/enemy.h"

#include <algorithm>
#include <iterator>

namespace gdc::enemy {
namespace {

struct Row {
  double level;
  Enemy e;
};
// re/EnemyByLevel.tsv: level, {oa, da, hit, hit_physical}.
constexpr Row kRows[] = {
    {1, {176, 115, 85, 23}},
    {5, {268, 195, 134, 40}},
    {10, {384, 302, 209, 69}},
    {15, {500, 413, 298, 105}},
    {20, {616, 524, 441, 161}},
    {25, {732, 634, 564, 214}},
    {30, {848, 745, 716, 267}},
    {35, {964, 856, 882, 332}},
    {40, {1080, 966, 1104, 430}},
    {45, {1196, 1077, 1307, 510}},
    {50, {1312, 1188, 1545, 608}},
    {55, {1428, 1299, 1789, 706}},
    {60, {1544, 1410, 2152, 854}},
    {65, {1660, 1520, 2452, 970}},
    {70, {1776, 1631, 2742, 1083}},
    {75, {1892, 1742, 3211, 1207}},
    {80, {2008, 1852, 3604, 1365}},
    {85, {2124, 1960, 3971, 1484}},
    {90, {2240, 2068, 4403, 1599}},
    {95, {2356, 2175, 4873, 1732}},
    {100, {2472, 2283, 5337, 1907}}};

}  // namespace

Enemy at(double level) {
  const Row* hi = std::find_if(std::begin(kRows), std::end(kRows), [&](const Row& r) { return r.level >= level; });
  if (hi == std::begin(kRows)) return hi->e;
  if (hi == std::end(kRows)) return std::prev(hi)->e;
  const Row* lo = std::prev(hi);
  const double t = (level - lo->level) / (hi->level - lo->level);
  auto mix = [&](double a, double b) { return a + (b - a) * t; };
  return {mix(lo->e.oa, hi->e.oa), mix(lo->e.da, hi->e.da), mix(lo->e.hit, hi->e.hit),
          mix(lo->e.hit_physical, hi->e.hit_physical)};
}

}  // namespace gdc::enemy
