#include <algorithm>

#include "gdc/attribute_names.h"

#include "tooltip_internal.h"

namespace gdc::tooltip {
namespace {

bool met_before(const snap::Item& e, const snap::Snapshot& s) {
  if (const auto engine = requirements::engine_met(e, s)) return *engine;
  return requirements::unmet(e, s).empty();
}

// Every two-handed class name ends in 2h (1.3.1.1).
bool two_handed(const snap::Item* it, const arz::Database* db) {
  const arz::Record* r = it && db ? db->record(it->record) : nullptr;
  return r && r->cls.ends_with("2h");
}

}  // namespace

const snap::Item* at(const snap::Snapshot& s, int location) {
  for (const auto& it : s.equipped)
    if (it.location == location) return &it;
  return nullptr;
}

model::Prediction take_off(const snap::Snapshot& s, int location, const arz::Database* db) {
  snap::Item none;
  none.stats.emplace();
  model::Prediction p = model::swap(s, none, location, db);
  auto& eq = p.after.equipped;
  eq.erase(std::remove_if(eq.begin(), eq.end(), [&](const snap::Item& x) { return x.location == location && x.record.empty(); }),
           eq.end());
  return p;
}

// Taking one off can break another, so rounds are bounded.
constexpr int kMaxBroken = 4;

std::vector<Broken> take_off_broken(const snap::Snapshot& before, snap::Snapshot& after, int location,
                                    const arz::Database* db, std::vector<std::string>& warnings) {
  std::vector<Broken> out;
  for (int round = 0; round < kMaxBroken; ++round) {
    const snap::Item* hit = nullptr;
    std::vector<requirements::Unmet> why;
    for (const auto& e : after.equipped) {
      if (e.location == location || !e.requirements) continue;
      const auto was = std::find_if(before.equipped.begin(), before.equipped.end(),
                                    [&](const snap::Item& b) { return b.id == e.id && b.location == e.location; });
      if (was == before.equipped.end() || !met_before(*was, before)) continue;
      why = requirements::unmet(e, after);
      if (!why.empty()) {
        hit = &e;
        break;
      }
    }
    if (!hit) break;
    out.push_back({hit->location, hit->record, why});
    model::Prediction p = take_off(after, hit->location, db);
    for (auto& w : p.warnings) warnings.push_back(std::move(w));
    after = std::move(p.after);
  }
  return out;
}

std::vector<int> locations_for(int item_type) {
  switch (item_type) {
    case 1: return {7};
    case 2: return {2};
    case 3: return {8};      // 14 "Clothing" takes type 3 too; not a player slot
    case 4: return {11};
    case 5: return {12};
    case 6: return {4, 5};
    case 7: return {10};
    case 8: return {1};      // shield, caster off-hand
    case 9: return {0, 1};   // L Hand only while dual wielding
    case 11: return {13};
    case 13: return {6};
    case 14: return {9};
    case 15: return {3};
    default: return {};
  }
}

bool weapon(const snap::Item* it, const arz::Database* db) {
  const arz::Record* r = it && db ? db->record(it->record) : nullptr;
  return r && r->cls.starts_with("Weapon") && !r->cls.starts_with("WeaponArmor");
}

// A two-hander sits in R Hand with L Hand empty.
std::vector<Plan> plans(const snap::Snapshot& s, const snap::Item& candidate, int item_type, const arz::Database* db) {
  std::vector<Plan> out;
  auto title = [&](int loc) {
    std::string t = "vs " + std::string(names::equip_location(loc));
    if (loc == 4 || loc == 5) t += loc == 4 ? " 1" : " 2";
    return t + (at(s, loc) ? "" : " (empty)");
  };
  const snap::Item *rh = at(s, 0), *lh = at(s, 1);
  if (item_type == 9) {
    if (two_handed(&candidate, db) && lh) out.push_back({0, 1, "vs R Hand + L Hand"});
    else out.push_back({0, -1, title(0)});
    if (!two_handed(&candidate, db) && weapon(lh, db)) out.push_back({1, -1, title(1)});
  } else if (item_type == 8 && two_handed(rh, db)) {
    out.push_back({1, 0, "vs R Hand (two-handed)"});
  } else {
    for (int loc : locations_for(item_type)) out.push_back({loc, -1, title(loc)});
  }
  return out;
}

}  // namespace gdc::tooltip
