#include "gdc/stat_model.h"

#include <algorithm>
#include <optional>
#include <set>

#include "gdc/combat_formulas.h"

#include "stat_model_internal.h"

namespace gdc::model {
namespace {

// combatformulas.dbr combatRegion*Chance by EquipLocations; waist adds its armor whole (fit).
double region_weight(int location) {
  switch (location) {
    case 7: return 0.15;   // Head: combatRegionHeadChance
    case 9: return 0.15;   // Shoulders: combatRegionShouldersChance
    case 8: return 0.26;   // Chest: combatRegionTorsoChance
    case 10: return 0.12;  // Hands: combatRegionArmsChance
    case 11: return 0.20;  // Legs: combatRegionLegsChance
    case 12: return 0.12;  // Feet: combatRegionFeetChance
    case 6: return 1.0;    // fit
    default: return 0.0;
  }
}

double item_armor(const snap::Item& it) {
  if (!it.stats) return 0;
  const auto* r = find(it.stats->defense, kArmor);
  return r ? r->defense : 0;
}

}  // namespace

std::set<std::string> item_records(const snap::Item& it) {
  std::set<std::string> records = {it.record};
  if (it.replica)
    for (const std::string* r : {&it.replica->prefix, &it.replica->suffix, &it.replica->modifier,
                                 &it.replica->transmute, &it.replica->relic, &it.replica->relic_bonus,
                                 &it.replica->augment})
      records.insert(*r);
  if (it.relic) records.insert(it.relic->record);
  if (it.augment) records.insert(it.augment->record);
  records.erase("");
  return records;
}

std::vector<std::pair<double, double>> region_armor(const snap::Snapshot& s) {
  const auto* prot = find(s.defense, kProtection);
  const double scale = 1 + (prot ? prot->defense_modifier : 0) / 100;
  double everywhere = 0;
  for (const auto& it : s.equipped)
    if (it.location == 6) everywhere += item_armor(it);
  std::vector<std::pair<double, double>> out;
  for (const int location : {7, 9, 8, 10, 11, 12}) {
    double a = everywhere;
    for (const auto& it : s.equipped)
      if (it.location == location) a += item_armor(it);
    out.emplace_back(region_weight(location), a * scale);
  }
  return out;
}

double armor(const snap::Snapshot& s) {
  double sum = 0;
  for (const auto& it : s.equipped) sum += region_weight(it.location) * item_armor(it);
  const auto* prot = find(s.defense, kProtection);
  return sum * (1 + (prot ? prot->defense_modifier : 0) / 100);
}

double base_absorption(const snap::Snapshot& s, const arz::Database* db) {
  if (const auto* r = find(s.defense, kAbsorption); r && r->defense > 0) return r->defense;
  const arz::Record* rec = db ? db->record("records/game/gameengine.dbr") : nullptr;
  return rec ? rec->number("armorDefensiveAbsorption") : 0;
}

Sheet sheet(const snap::Snapshot& s) {
  auto total = [&](int t) {
    for (const auto& a : s.char_attributes)
      if (a.type == t) return static_cast<double>(a.total);
    return 0.0;
  };
  Sheet out;
  out.physique = total(kPhysique);
  out.cunning = total(kCunning);
  out.spirit = total(kSpirit);
  out.life = total(kLife);
  out.energy = total(kEnergy);
  out.life_regen = formulas::life_regen(s);
  out.energy_regen = formulas::mana_regen(s);
  out.oa = formulas::offensive_ability(s);
  out.da = formulas::defensive_ability(s);
  out.run_speed = total(kRunSpeed);
  out.armor = armor(s);
  const bool processed = has_processed(s);
  if (const auto* ab = find(s.defense, kAbsorption))
    out.absorption = processed ? ab->processed : std::min(100.0, ab->defense * (1 + ab->defense_modifier / 100.0));
  for (const auto& r : s.defense) {
    if (!cappable(r.type)) continue;
    Resist x;
    x.type = r.type;
    x.raw = r.defense;
    x.uncapped = uncapped(r);
    x.max = s.defense_cap + r.cap_bonus;
    x.shown = processed ? r.processed : x.uncapped;
    out.resists.push_back(x);
  }
  return out;
}

namespace {

// derive() averages absorption values, so only its modifier moves.
void add_item_stats(const snap::Item* it, double sign, AllDeltas& all) {
  if (!it || !it->stats) return;
  for (const auto& c : it->stats->char_attributes) {
    all.chr[c.type].first += sign * c.value;
    all.chr[c.type].second += sign * c.modifier;
  }
  for (const auto& c : it->stats->defense) {
    if (c.type != kAbsorption) all.def[c.type].first += sign * c.defense;
    all.def[c.type].second += sign * c.defense_modifier;
  }
  for (const auto& c : it->stats->offensive_modifiers) {
    all.dmg[c.type].first += sign * c.damage_modifier;
    all.dmg[c.type].second += sign * c.dot_modifier;
  }
}

// SkillAttributeAccumulator::AddValue 0x51F160 and siblings sum each array.
void swap_skill_attributes(const snap::Item* old, const snap::Item& candidate, Prediction& p) {
  for (const auto* it : {old, &candidate}) {
    if (!it || !it->stats) continue;
    const float sign = it == old ? -1.0f : 1.0f;
    for (const auto& a : it->stats->skill_attributes) {
      snap::SkillAttribute& t = row(p.after.skill_attributes, a.type);
      t.value += sign * a.value, t.chance_value += sign * a.chance_value;
      t.modifier += sign * a.modifier, t.chance_modifier += sign * a.chance_modifier;
    }
  }
}

void place(const snap::Item& candidate, int location, Prediction& p) {
  snap::Item placed = candidate;
  placed.location = location;
  auto& eq = p.after.equipped;
  if (const auto it = std::find_if(eq.begin(), eq.end(), [&](const snap::Item& x) { return x.location == location; });
      it != eq.end())
    *it = placed;
  else
    eq.push_back(placed);
}

// One set whose worn count changes; grants join hb / ha.
SetCount set_change(const arz::Record& rec, SetCount c, AllDeltas& all, Hidden& hb, Hidden& ha, Prediction& p) {
  c.modeled = true;
  const sets::Bonus b = sets::bonus(rec, c.before), a = sets::bonus(rec, c.after);
  sets::Stats delta = a.stats;
  for (const auto& [key, v] : b.stats) delta[key] -= v;
  all.add(delta);
  merge(hb, b);
  merge(ha, a);
  for (const auto* x : {&b, &a})
    for (const auto& cv : x->conversions) add_conversion(p.after.conversions, cv.from, cv.to, (x == &b ? -1 : 1) * cv.percent);
  if (b.granted_skill != a.granted_skill || b.granted_level != a.granted_level) {
    c.skill_lost = b.granted_skill;
    c.skill_gained = a.granted_skill;
  }
  for (const auto* x : {&b, &a})
    for (const auto& f : x->unmodeled)
      if (std::find(c.unmodeled.begin(), c.unmodeled.end(), f) == c.unmodeled.end() &&
          sets::bonus_field(rec, f, c.before) != sets::bonus_field(rec, f, c.after))
        c.unmodeled.push_back(f);
  return c;
}

// Counts include the removed item's set.
void set_changes(const snap::Snapshot& s, const arz::Database* db, AllDeltas& all, Hidden& hb, Hidden& ha, Prediction& p) {
  const auto sb = sets::counts(s), sa = sets::counts(p.after);
  std::set<std::string> names;
  for (const auto& [k, _] : sb) names.insert(k);
  for (const auto& [k, _] : sa) names.insert(k);
  bool unmodeled_sets = false;
  for (const auto& k : names) {
    SetCount c;
    c.set = k;
    c.before = sb.contains(k) ? sb.at(k) : 0;
    c.after = sa.contains(k) ? sa.at(k) : 0;
    if (c.before == c.after) continue;
    const auto rec = db ? db->record(k) : nullptr;
    if (!rec) {
      unmodeled_sets = true;
      p.sets.push_back(std::move(c));
      continue;
    }
    p.sets.push_back(set_change(*rec, std::move(c), all, hb, ha, p));
  }
  if (unmodeled_sets) p.warnings.push_back("set bonuses change (set record not in the database)");
}

}  // namespace

Prediction swap(const snap::Snapshot& s, const snap::Item& candidate, int location, const arz::Database* db) {
  Prediction p;
  p.after = s;
  const snap::Item* old = at(s, location);
  if (!candidate.stats) p.warnings.push_back("candidate item has no stats");
  AllDeltas all;
  add_item_stats(old, -1, all);
  add_item_stats(&candidate, +1, all);
  if (s.schema >= 9) swap_skill_attributes(old, candidate, p);
  place(candidate, location, p);
  Hidden hb = db ? hidden(old, *db) : Hidden{}, ha = db ? hidden(&candidate, *db) : Hidden{};
  set_changes(s, db, all, hb, ha, p);
  if (db) {
    apply_skill_levels(s, location, hb, ha, *db, all, p);
    apply_granted(s, old, candidate, *db, all, p);
  }
  derive(s, db, all, p);
  for (const auto* it : {old, &candidate})
    if (it && it->stats)
      for (const auto& c : it->stats->conversions) add_conversion(p.after.conversions, c.from, c.to, (it == old ? -1 : 1) * c.percent);

  if (!db) p.warnings.push_back("no database: skill grants, set bonuses and max resist not checked");
  else if (hb.max_resist != ha.max_resist)
    p.warnings.push_back("max resist changes (not modeled)");
  return p;
}

std::optional<Prediction> toggle_buff(const snap::Snapshot& s, const snap::Skill& sk, bool on, const arz::Database& db) {
  Prediction p;
  p.after = s;
  AllDeltas all;
  if (!add_buff(s, sk, on ? 1 : -1, db, all, p.after.conversions)) return std::nullopt;
  derive(s, &db, all, p);
  return p;
}

snap::Snapshot expected(const snap::Snapshot& s, const arz::Database* db) {
  if (!db) return s;
  Prediction p;
  p.after = s;
  AllDeltas all;
  bool any = false;
  for (auto* list : {&p.after.skills, &p.after.item_skills})
    for (snap::Skill& sk : *list) {
      if (!sk.buff_active) continue;  // schema < 8: leave the engine totals
      const bool active = *sk.buff_active;
      if (list == &p.after.skills && buff_timing(sk, *db)) {
        if (active) any |= add_buff(s, sk, -1, *db, all, p.after.conversions);
        sk.buff_active = false;
        continue;
      }
      sk.buff_active.reset();  // idempotent
      if (active && sk.current_level > 0) {
        // Toggles and auras stay in the totals as captured.
        const arz::Record* rec = db->record(sk.record);
        if (!rec) continue;
        if (list == &p.after.skills && sk.devotion_max_level == 0 && !triggered_buff(*rec)) continue;
        const arz::Record& stats = stats_record(*rec, *db);
        if (!self_buff(stats)) continue;
        all.add(scaled(level_effect(stats, sk.current_level, sk.current_level).at_from, -1));
        any = true;
      }
    }
  if (any) derive(s, db, all, p);
  return std::move(p.after);
}

}  // namespace gdc::model
