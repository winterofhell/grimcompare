#include "gdc/attribute_names.h"

#include "stat_model_internal.h"

namespace gdc::model {
namespace {

// playerlevels.dbr increments per 8 attribute points (§11.9).
constexpr double kLifePerPhysique = 20.0 / 8, kLifePerCunning = 8.0 / 8, kLifePerSpirit = 12.0 / 8;
constexpr double kEnergyPerSpirit = 16.0 / 8;

// Char attributes: total = flat x (1 + modifier%).
void derive_attributes(Deltas& d, Prediction& p) {
  std::map<int, double> derived;
  auto apply = [&](int t) {
    const auto [dv, dm] = d[t];
    const double extra = derived[t];
    if (dv == 0 && dm == 0 && extra == 0) return 0.0;
    snap::CharAttribute& a = row(p.after.char_attributes, t);
    const double scale = 1 + a.modifier / 100.0;
    if (scale <= 0) {
      p.warnings.push_back("attribute " + std::string(names::char_attribute(t)) + ": modifier <= -100%");
      return 0.0;
    }
    const double before = a.total;
    a.modifier = static_cast<float>(a.modifier + dm);
    a.total = static_cast<float>((before / scale + dv + extra) * (1 + a.modifier / 100.0));
    return static_cast<double>(a.total) - before;
  };
  const double dp = apply(kPhysique), dc = apply(kCunning), ds = apply(kSpirit);
  derived[kLife] = kLifePerPhysique * dp + kLifePerCunning * dc + kLifePerSpirit * ds;
  derived[kEnergy] = kEnergyPerSpirit * ds;
  std::set<int> rest;
  for (const auto& [t, _] : d) rest.insert(t);
  rest.insert(kLife), rest.insert(kEnergy);
  for (int t : rest)
    if (t != kPhysique && t != kCunning && t != kSpirit) apply(t);
}

// Absorption entries average; the rest sum (§11.9).
void derive_defense(const snap::Snapshot& s, const arz::Database* db, const Deltas& def, Prediction& p) {
  for (const auto& [t, v] : def) {
    if (v.first == 0 && v.second == 0) continue;
    snap::CombatTotals& r = row(p.after.defense, t);
    r.defense = static_cast<float>(r.defense + v.first);
    r.defense_modifier = static_cast<float>(r.defense_modifier + v.second);
  }
  // Absorption is the gameengine.dbr constant while any armor exists (§11.9).
  if (snap::CombatTotals* r = find_mut(p.after.defense, kAbsorption)) {
    const snap::CombatTotals* a = find(p.after.defense, kArmor);
    if (a && a->defense > 0) {
      const double base = base_absorption(s, db);
      if (base > 0) r->defense = static_cast<float>(base);
      else p.warnings.push_back("armor absorption base unknown (no database)");
    } else {
      r->defense = 0;
    }
  }
  if (!has_processed(s)) return;
  const auto* prot = find(p.after.defense, kProtection);
  for (snap::CombatTotals& r : p.after.defense) {
    if (cappable(r.type))
      r.processed = static_cast<float>(std::min(uncapped(r), static_cast<double>(s.defense_cap + r.cap_bonus)));
    else if (r.type == kArmor)  // fixture: 6614 x 1.13 = 7473.82
      r.processed = static_cast<float>(r.defense * (1 + (prot ? prot->defense_modifier : 0) / 100.0));
    else if (r.type == kAbsorption)  // fixtures: 70 x 1.4 = 98; cap 100 (Process 0x118370)
      r.processed = static_cast<float>(std::min(100.0, r.defense * (1 + r.defense_modifier / 100.0)));
  }
}

}  // namespace

void derive(const snap::Snapshot& s, const arz::Database* db, AllDeltas& all, Prediction& p) {
  derive_attributes(all.chr, p);
  derive_defense(s, db, all.def, p);
  for (const auto& [t, v] : all.dmg) {
    if (v.first == 0 && v.second == 0) continue;
    snap::CombatTotals& r = row(p.after.damage_modifiers, t);
    r.damage_modifier = static_cast<float>(r.damage_modifier + v.first);
    r.dot_modifier = static_cast<float>(r.dot_modifier + v.second);
  }
}

}  // namespace gdc::model
