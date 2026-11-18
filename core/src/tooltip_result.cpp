#include <algorithm>
#include <cmath>
#include <map>

#include "tooltip_internal.h"

namespace gdc::tooltip {
namespace {

constexpr std::size_t kMaxUnused = 3;

std::string part_name(const score::Contribution& c, const Labels& labels) {
  for (const int t : {13, 42, 43, 44, 45, 46})
    if (c.stat == resist_stat(t)) return labels.resist(t);
  return pretty(c.label);
}

// Category 2 is a percentage (MECHANICS §1).
std::string rr_amount(const effects::Reduction& r, const Labels& labels) {
  double lo = r.amount.begin()->second, hi = lo;
  for (const auto& [_, v] : r.amount) lo = std::min(lo, v), hi = std::max(hi, v);
  const std::string pct = r.category == 2 ? "%" : "";
  if (r.amount.size() > 3)
    return "-" + (lo == hi ? num(lo, 0) + pct + " all resists"
                           : num(lo, 0) + ".." + num(hi, 0) + pct + " to " + std::to_string(r.amount.size()) + " resists");
  std::string t;
  for (const auto& [type, v] : r.amount) t += (t.empty() ? "-" : ", -") + num(v, 0) + pct + " " + labels.resist(type);
  return t;
}

std::string rr_uptime(const effects::Reduction& r) {
  static constexpr const char* kOrigin[] = {"derived", "preset", "default !"};
  return ", up " + num(r.uptime * 100, 0) + "% (" + kOrigin[static_cast<int>(r.origin)] + ")";
}

}  // namespace

std::vector<std::string> top(const score::Result& r, const Labels& labels, const std::string& indent, const Screen& screen) {
  std::vector<std::string> items;
  for (std::size_t i = 0; i < r.contributions.size() && i < 3; ++i) {
    const auto& c = r.contributions[i];
    items.push_back((c.skill.empty() ? part_name(c, labels) : labels.skill(c.skill)) + " " + signed_num(c.points, 1));
  }
  return items.empty() ? items : pack(indent + "top: ", indent + "  ", items, screen);
}

std::string terms(const score::Result& r) {
  auto pct = [](double ratio) { return signed_num((ratio - 1) * 100, 1) + "%"; };
  return "Damage " + pct(r.ratio.damage) + ", Survival " + pct(r.ratio.survival) + ", Other " + pct(r.ratio.other);
}

std::vector<Line> rr_lines(const score::Result& r, const Labels& labels) {
  std::vector<Line> out;
  std::vector<const effects::Reduction*> paired;
  for (const auto& lost : r.rr_lost) {
    const auto g = std::find_if(r.rr_gained.begin(), r.rr_gained.end(), [&](const effects::Reduction& x) {
      return x.record == lost.record && x.category == lost.category &&
             std::find(paired.begin(), paired.end(), &x) == paired.end();
    });
    if (g == r.rr_gained.end()) {
      out.push_back({Tone::Worse, "loses " + labels.skill(lost.record) + ": " + rr_amount(lost, labels) + rr_uptime(lost)});
      continue;
    }
    paired.push_back(&*g);
    const std::string was = rr_amount(lost, labels), now = rr_amount(*g, labels);
    std::string t = labels.skill(lost.record) + ": " + (was == now ? now : was + " -> " + now);
    if (std::abs(lost.uptime - g->uptime) >= 0.005) t += ", up " + num(lost.uptime * 100, 0) + "% -> " + num(g->uptime * 100, 0) + "%";
    out.push_back({Tone::Detail, t});
  }
  for (const auto& g : r.rr_gained)
    if (std::find(paired.begin(), paired.end(), &g) == paired.end())
      out.push_back({Tone::Better, labels.skill(g.record) + ": " + rr_amount(g, labels) + rr_uptime(g)});
  return out;
}

std::vector<Line> target_set(const snap::Snapshot& s, const snap::Item& candidate, const score::Preset& p,
                             const arz::Database* db, const Labels& labels) {
  std::vector<Line> out;
  if (!db) return out;
  for (const auto& set : p.target_sets) {
    const arz::Record* r = db->record(set);
    if (!r) continue;
    std::vector<std::string> members;
    for (std::size_t i = 0; !r->string("setMembers", i).empty(); ++i) members.emplace_back(r->string("setMembers", i));
    if (std::find(members.begin(), members.end(), candidate.record) == members.end()) continue;
    auto has = [](const std::vector<snap::Item>& v, const std::string& rec) {
      return std::any_of(v.begin(), v.end(), [&](const snap::Item& x) { return x.record == rec; });
    };
    int worn = 0;
    std::string missing;
    for (const auto& m : members) {
      if (has(s.equipped, m)) {
        ++worn;
        continue;
      }
      missing += (missing.empty() ? "" : ", ") + labels.item(m) +
                 (m == candidate.record ? " (this)" : has(s.inventory, m) ? " (in bag)" : "");
    }
    out.push_back({Tone::Better, "target set " + labels.set(set) + ": " + std::to_string(worn) + "/" +
                                     std::to_string(members.size()) + " worn"});
    if (!missing.empty()) out.push_back({Tone::Detail, "  missing: " + missing});
  }
  return out;
}

void unused_lines(std::vector<Line>& out, const std::map<std::string, std::uint32_t>& unused, bool detail, const Labels& labels,
                  const Screen& screen) {
  std::map<std::pair<std::string, std::uint32_t>, int> groups;
  for (const auto& [record, n] : unused)
    if (const std::string m = labels.mastery(record); !m.empty()) ++groups[{m, n}];
  std::vector<std::string> single;
  for (const auto& [record, n] : unused) {
    const std::string m = labels.mastery(record);
    if (m.empty() || groups[{m, n}] < 3) single.push_back(labels.skill(record) + " +" + std::to_string(n));
  }
  for (const auto& [key, count] : groups)
    if (count >= 3)
      out.push_back({Tone::Detail, "+" + std::to_string(key.second) + " " + key.first + ": " + std::to_string(count) + " unused skills (0 points)"});
  if (!detail && single.size() > kMaxUnused) {
    const std::size_t more = single.size() - kMaxUnused;
    single.resize(kMaxUnused);
    single.push_back("... " + std::to_string(more) + " more (Shift)");
  }
  if (!single.empty())
    for (auto& l : pack("unused (0 points): ", "  ", single, screen)) out.push_back({Tone::Detail, std::move(l)});
}

}  // namespace gdc::tooltip
