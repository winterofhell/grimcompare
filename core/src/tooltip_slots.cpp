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
    out.push_back({.location = hit->location, .record = hit->record, .why = why});
    model::Prediction p = take_off(after, hit->location, db);
    for (auto& w : p.warnings) warnings.push_back(std::move(w));
    after = std::move(p.after);
  }
  return out;
}

std::vector<int> locations_for(int item_type) {
  using namespace equip;
  switch (item_type) {
    case item_types::kHead: return {kHead};
    case item_types::kNeck: return {kNeck};
    case item_types::kChest: return {kChest};  // 14 "Clothing" takes it too; not a player slot
    case item_types::kLegs: return {kLegs};
    case item_types::kFeet: return {kFeet};
    case item_types::kRing: return {kFinger1, kFinger2};
    case item_types::kHands: return {kHands};
    case item_types::kOffHand: return {kLeftHand};  // shield, caster off-hand
    case item_types::kWeapon: return {kRightHand, kLeftHand};  // L Hand only while dual wielding
    case item_types::kArtifact: return {kArtifact};
    case item_types::kWaist: return {kWaist};
    case item_types::kShoulders: return {kShoulders};
    case item_types::kMedal: return {kMedal};
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
    if (loc == equip::kFinger1 || loc == equip::kFinger2) t += loc == equip::kFinger1 ? " 1" : " 2";
    return t + (at(s, loc) ? "" : " (empty)");
  };
  const snap::Item *rh = at(s, equip::kRightHand), *lh = at(s, equip::kLeftHand);
  if (item_type == item_types::kWeapon) {
    if (two_handed(&candidate, db) && lh)
      out.push_back({.location = equip::kRightHand, .also_off = equip::kLeftHand, .title = "vs R Hand + L Hand"});
    else out.push_back({.location = equip::kRightHand, .title = title(equip::kRightHand)});
    if (!two_handed(&candidate, db) && weapon(lh, db))
      out.push_back({.location = equip::kLeftHand, .title = title(equip::kLeftHand)});
  } else if (item_type == item_types::kOffHand && two_handed(rh, db)) {
    out.push_back({.location = equip::kLeftHand, .also_off = equip::kRightHand, .title = "vs R Hand (two-handed)"});
  } else {
    for (int loc : locations_for(item_type)) out.push_back({.location = loc, .title = title(loc)});
  }
  return out;
}

}  // namespace gdc::tooltip
