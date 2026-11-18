#include "gdc/requirements.h"

#include <cmath>

#include "gdc/attribute_names.h"

namespace gdc::requirements {
namespace {

using char_attr::kGlobalReqReduction, char_attr::kLevelReqReduction;

float total(const snap::Snapshot& s, int type) {
  for (const auto& a : s.char_attributes)
    if (a.type == type) return a.total;
  return 0;
}

}  // namespace

std::uint32_t effective(std::uint32_t raw, float reduction_percent) {
  const float r = static_cast<float>(raw);
  const float v = r - reduction_percent * 0.01f * r + 0.5f;
  return v <= 0 ? 0 : static_cast<std::uint32_t>(v);  // cvttss2si; a reduction over 100% only reaches 0
}

std::vector<Unmet> unmet(const snap::Item& item, const snap::Snapshot& s) {
  std::vector<Unmet> out;
  if (!item.requirements) return out;
  const snap::Requirements& q = *item.requirements;
  if (q.level != 0) {
    const std::uint32_t need = effective(q.level, total(s, kLevelReqReduction));
    if (static_cast<double>(s.level) < need) out.push_back({0, need, static_cast<double>(s.level)});
  }
  const float global = total(s, kGlobalReqReduction);
  for (int a = 0; a < 3; ++a) {
    if (q.attribute[a] == 0) continue;
    float red = total(s, q.reductions[a][0]);
    for (int k = 1; k < 3; ++k)
      if (q.reductions[a][k] != 0) red += total(s, q.reductions[a][k]);
    if (global > 0) red += global;
    const std::uint32_t need = effective(q.attribute[a], red);
    const float have = total(s, a + 1);
    if (have < static_cast<float>(need)) out.push_back({a + 1, need, have});
  }
  return out;
}

std::optional<bool> engine_met(const snap::Item& item, const snap::Snapshot& s) {
  if (!item.requirements || !item.requirements->met || !item.requirements->met_at) return std::nullopt;
  const snap::Requirements::MetAt& at = *item.requirements->met_at;
  if (at.level != s.level || at.difficulty != s.difficulty) return std::nullopt;
  for (int a = 0; a < 3; ++a)
    if (std::abs(at.attribute[a] - total(s, a + 1)) >= 0.5f) return std::nullopt;
  return item.requirements->met;
}

std::string text(const Unmet& u) {
  static const char* const kName[] = {"Level", "Physique", "Cunning", "Spirit"};
  return std::string(kName[u.what]) + " " + std::to_string(u.need) + " (have " +
         std::to_string(static_cast<long long>(std::floor(u.have))) + ")";
}

}  // namespace gdc::requirements
