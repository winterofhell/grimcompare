#pragma once
// Game.dll 1.3.1.1:
// Item::AreRequirementsMet 0x326930: level (Character+0x1760) >= E(level req, type 54 LevelReqReduction).
// ItemEquipment::AreRequirementsMet 0x343C30: also Physique / Cunning / Spirit >= E(req, sum of the item's three
// reduction types (first always, others when non-zero) + type 53 GlobalReqReduction when > 0).
// E(r, x) = trunc(r - r * x * 0.01 + 0.5) in float (0.01 at 0x7A7FC8, 0.5 at 0x7A8024). A requirement of 0 always passes.
#include <cstdint>
#include <string>
#include <vector>

#include "gdc/snapshot.h"

namespace gdc::requirements {

struct Unmet {
  int what = 0;
  std::uint32_t need = 0;
  double have = 0;
};

std::uint32_t effective(std::uint32_t raw, float reduction_percent);

// Empty without requirements data.
std::vector<Unmet> unmet(const snap::Item& item, const snap::Snapshot& s);

std::string text(const Unmet& u);

}  // namespace gdc::requirements
