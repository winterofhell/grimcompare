#include <algorithm>

#include "score_internal.h"

namespace gdc::score {
Utility utility_of(const snap::Snapshot& s, const Settings& p, const arz::Database* db) {
  return {utility::context(s, db, p.stage), utility::item_values(s, db)};
}

Other other(const snap::Snapshot& s, const Utility& u, const Settings& p, bool explain) {
  const utility::Context& ctx = u.context;
  const utility::Values v = utility::values(s, u.items);
  auto weight = [&](const std::string& key) {
    const auto it = p.other.find(key);
    if (it == p.other.end()) return 0.0;
    for (const auto& w : it->second.when)
      if (!utility::holds(w, ctx)) return 0.0;
    return it->second.weight;
  };
  Other out;
  for (const auto& [key, x] : v.keys) out.terms[key] = {x, weight(key)};
  const double wd = weight("default");
  for (const auto& [name, x] : v.unlisted) out.terms["default: " + name] = {x, wd};
  for (const auto& [name, t] : out.terms) {
    out.value += t.first * t.second;
    if (explain && t.first != 0) out.inputs += name + " " + sig(t.first) + "x" + sig(t.second) + " ";
  }
  out.value = std::max(1e-6, out.value);
  if (explain) out.inputs += "= " + fmt(out.value, 3);
  return out;
}

}  // namespace gdc::score
