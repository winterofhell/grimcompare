#include "gdc/classes.h"

#include <algorithm>
#include <array>
#include <iterator>
#include <unordered_map>
#include <utility>

namespace gdc::classes {
namespace {

constexpr std::pair<std::string_view, std::string_view> kParents[] = {
#include "class_tree.inc"
};

constexpr int index_of(std::string_view cls) {
  const auto* it = std::lower_bound(std::begin(kParents), std::end(kParents), cls,
                                    [](const auto& row, std::string_view c) { return row.first < c; });
  return it != std::end(kParents) && it->first == cls ? static_cast<int>(it - std::begin(kParents)) : -1;
}

// Parent row of each row, -1 at a root: is_a walks ints instead of searching names at every level.
constexpr auto kParentRow = [] {
  std::array<int, std::size(kParents)> out{};
  for (std::size_t i = 0; i < out.size(); ++i) out[i] = index_of(kParents[i].second);
  return out;
}();

static_assert(std::is_sorted(std::begin(kParents), std::end(kParents),
                             [](const auto& a, const auto& b) { return a.first < b.first; }));
static_assert([] {
  for (std::size_t i = 0; i < std::size(kParents); ++i)
    if (!kParents[i].second.empty() && kParentRow[i] < 0) return false;  // every parent is a row of its own
  return true;
}());

}  // namespace

bool is_a(std::string_view cls, std::string_view base) {
  if (cls == base) return true;
  // Hashed: scoring asks this a few thousand times per tooltip.
  static const std::unordered_map<std::string_view, int> rows = [] {
    std::unordered_map<std::string_view, int> m;
    for (std::size_t i = 0; i < std::size(kParents); ++i) m.emplace(kParents[i].first, static_cast<int>(i));
    return m;
  }();
  auto row = [&](std::string_view c) {
    const auto it = rows.find(c);
    return it != rows.end() ? it->second : -1;
  };
  const int b = row(base);
  if (b < 0) return false;
  int i = row(cls);
  for (int depth = 0; i >= 0 && depth < 32; ++depth)
    if ((i = kParentRow[i]) == b) return true;
  return false;
}

Speed speed(std::string_view cls) {
  if (is_a(cls, "SkillActivatedWeapon") || is_a(cls, "SkillActivatedWeaponPool") || is_a(cls, "Skill_AttackRadiusSpin"))
    return Speed::kAttack;
  if (is_a(cls, "SkillActivatedSpell") || is_a(cls, "SkillActivatedBuffSelf")) return Speed::kCast;
  return Speed::kNone;
}

}  // namespace gdc::classes
