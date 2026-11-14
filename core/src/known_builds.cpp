#include "gdc/known_builds.h"

#include <algorithm>
#include <cctype>

namespace gdc {

const KnownBuild* find_known_build(std::string_view module, std::string_view sha256) {
  auto ieq = [](std::string_view a, std::string_view b) {
    return std::ranges::equal(a, b, [](unsigned char x, unsigned char y) { return std::tolower(x) == std::tolower(y); });
  };
  for (const auto& b : kKnownBuilds)
    if (ieq(b.module, module) && b.sha256 == sha256) return &b;
  return nullptr;
}

}  // namespace gdc
