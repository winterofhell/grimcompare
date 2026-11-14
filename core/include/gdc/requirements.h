#pragma once
// Item requirements as the engine checks them (Game.dll 1.3.1.1 disassembly):
//   Item::AreRequirementsMet (0x326930): level (Character+0x1760) >= E(GetLevelRequirement, total of
//     CharAttributeType 54 LevelReqReduction), or the requirement is 0.
//   ItemEquipment::AreRequirementsMet (0x343C30): that, and for Physique / Cunning / Spirit (types 1 / 2 / 3)
//     total >= E(requirement, sum of the totals of the item's three reduction types (the first always, the
//     other two when non-zero) + GlobalReqReduction (type 53) when > 0), or the requirement is 0.
//   E(r, x) = trunc(r - r * x * 0.01 + 0.5) in float (constants 0.01 at 0x7A7FC8, 0.5 at 0x7A8024).
#include <cstdint>
#include <string>
#include <vector>

#include "gdc/snapshot.h"

namespace gdc::requirements {

struct Unmet {
  int what = 0;  // 0 level, 1 Physique, 2 Cunning, 3 Spirit
  std::uint32_t need = 0;
  double have = 0;
};

std::uint32_t effective(std::uint32_t raw, float reduction_percent);

// What `item` needs that the character in `s` (its level and char attribute totals) does not have.
// Empty when the item has no requirements data.
std::vector<Unmet> unmet(const snap::Item& item, const snap::Snapshot& s);

// "Spirit 262 (have 198)"
std::string text(const Unmet& u);

}  // namespace gdc::requirements
