#pragma once
// Item requirement check, as Game.dll does it (MECHANICS §7).
#include <cstdint>
#include <optional>
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

// The engine's own check, only for the same captured state.
std::optional<bool> engine_met(const snap::Item& item, const snap::Snapshot& s);

std::string text(const Unmet& u);

}  // namespace gdc::requirements
