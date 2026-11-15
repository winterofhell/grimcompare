#include "gdc/calibrate.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <set>

#include "gdc/attribute_names.h"
#include "gdc/requirements.h"
#include "gdc/stat_model.h"

namespace gdc::calibrate {
namespace {

const snap::Item* at(const snap::Snapshot& s, int location) {
  for (const auto& it : s.equipped)
    if (it.location == location) return &it;
  return nullptr;
}

std::string short_record(const std::string& r) {
  const auto slash = r.rfind('/');
  std::string s = slash == std::string::npos ? r : r.substr(slash + 1);
  if (s.ends_with(".dbr")) s.resize(s.size() - 4);
  return s.empty() ? "-" : s;
}

std::string item_label(const snap::Snapshot& s, int location) {
  const snap::Item* it = at(s, location);
  if (!it) return "(empty)";
  std::string out = short_record(it->record);
  if (it->relic) out += "+" + short_record(it->relic->record);
  if (it->augment) out += "+" + short_record(it->augment->record);
  return out;
}

std::string component(const std::optional<snap::Component>& c) { return c ? c->record : std::string{}; }

bool same_item(const snap::Item* a, const snap::Item* b) {
  if (!a || !b) return a == b;
  return a->id == b->id && a->record == b->record && component(a->relic) == component(b->relic) &&
         component(a->augment) == component(b->augment);
}

std::string other_change(const snap::Snapshot& a, const snap::Snapshot& b) {
  if (a.character_name != b.character_name) return "different character";
  if (a.level != b.level) return "level changed";
  if (a.difficulty != b.difficulty || a.difficulty_ramp != b.difficulty_ramp) return "difficulty changed";
  for (int t = 1; t <= 3; ++t) {
    auto spent = [t](const snap::Snapshot& s) {
      for (const auto& c : s.char_attributes)
        if (c.type == t) return c.points_spent;
      return 0u;
    };
    if (spent(a) != spent(b)) return "attribute points spent changed";
  }
  std::map<std::string, std::uint32_t> la, lb;
  for (const auto& k : a.skills) la[k.record] = k.level;
  for (const auto& k : b.skills) lb[k.record] = k.level;
  if (la != lb) return "skill or devotion points changed";
  return {};
}

bool same_passive(const snap::Skill& a, const snap::Skill& b) {
  const bool ea = !a.passive || (a.passive->char_attributes.empty() && a.passive->defense.empty());
  const bool eb = !b.passive || (b.passive->char_attributes.empty() && b.passive->defense.empty());
  if (ea || eb) return ea == eb;
  const auto &pa = *a.passive, &pb = *b.passive;
  if (pa.char_attributes.size() != pb.char_attributes.size() || pa.defense.size() != pb.defense.size()) return false;
  for (std::size_t i = 0; i < pa.char_attributes.size(); ++i) {
    const auto &x = pa.char_attributes[i], &y = pb.char_attributes[i];
    if (x.type != y.type || x.value != y.value || x.modifier != y.modifier) return false;
  }
  for (std::size_t i = 0; i < pa.defense.size(); ++i) {
    const auto &x = pa.defense[i], &y = pb.defense[i];
    if (x.type != y.type || x.defense != y.defense || x.defense_modifier != y.defense_modifier) return false;
  }
  return true;
}

void compare(std::vector<Diff>& out, const std::string& stat, double predicted, double actual, double tol) {
  if (!(std::abs(predicted - actual) <= tol)) out.push_back({stat, predicted, actual, tol});
}

std::vector<Diff> differences(const snap::Snapshot& cur, const snap::Snapshot& after) {
  std::vector<Diff> out;
  const model::Sheet got = model::sheet(cur), want = model::sheet(after);
  compare(out, "Physique", got.physique, want.physique, 1);
  compare(out, "Cunning", got.cunning, want.cunning, 1);
  compare(out, "Spirit", got.spirit, want.spirit, 1);
  compare(out, "Life", got.life, want.life, 1);
  compare(out, "Energy", got.energy, want.energy, 1);
  compare(out, "Life regen", got.life_regen, want.life_regen, 0.1);
  compare(out, "Energy regen", got.energy_regen, want.energy_regen, 0.1);
  compare(out, "OA", got.oa, want.oa, 1);
  compare(out, "DA", got.da, want.da, 1);
  compare(out, "Run speed", got.run_speed, want.run_speed, 0.001);
  compare(out, "Armor", got.armor, want.armor, 1);
  std::map<int, const model::Resist*> rg, rw;
  for (const auto& r : got.resists) rg[r.type] = &r;
  for (const auto& r : want.resists) rw[r.type] = &r;
  std::set<int> types;
  for (const auto& [t, _] : rg) types.insert(t);
  for (const auto& [t, _] : rw) types.insert(t);
  const model::Resist zero;
  for (int t : types) {
    const model::Resist& g = rg.contains(t) ? *rg[t] : zero;
    const model::Resist& w = rw.contains(t) ? *rw[t] : zero;
    const std::string name = "Resist " + std::string(names::combat_attribute(t));
    compare(out, name + " (raw)", g.raw, w.raw, 0.1);
    compare(out, name + " (shown)", g.shown, w.shown, 0.1);
  }
  // Type 59 crit damage is a fraction.
  auto by_type = [](const snap::Snapshot& s) {
    std::map<int, const snap::CombatTotals*> m;
    for (const auto& r : s.damage_modifiers) m[r.type] = &r;
    return m;
  };
  const auto dg = by_type(cur), dw = by_type(after);
  std::set<int> dt;
  for (const auto& [t, _] : dg) dt.insert(t);
  for (const auto& [t, _] : dw) dt.insert(t);
  const snap::CombatTotals none;
  for (int t : dt) {
    const auto& g = dg.contains(t) ? *dg.at(t) : none;
    const auto& w = dw.contains(t) ? *dw.at(t) : none;
    const std::string name = "Damage % " + std::string(names::combat_attribute(t));
    const double tol = t == 59 ? 0.001 : 0.1;
    compare(out, name, g.damage_modifier, w.damage_modifier, tol);
    compare(out, name + " (duration)", g.dot_modifier, w.dot_modifier, tol);
  }
  std::map<std::pair<int, int>, double> cg, cw;
  for (const auto& x : cur.conversions) cg[{x.from, x.to}] += x.percent;
  for (const auto& x : after.conversions) cw[{x.from, x.to}] += x.percent;
  std::set<std::pair<int, int>> ct;
  for (const auto& [k, _] : cg) ct.insert(k);
  for (const auto& [k, _] : cw) ct.insert(k);
  for (const auto& k : ct)
    compare(out, "Conversion " + std::string(names::combat_attribute(k.first)) + "->" + std::string(names::combat_attribute(k.second)),
            cg[k], cw[k], 0.001);
  return out;
}

}  // namespace

Check check(const snap::Snapshot& before, const snap::Snapshot& after, const arz::Database* db) {
  Check c;
  if (!before.complete || !after.complete) {
    c.skipped = "incomplete snapshot";
    return c;
  }
  if (const std::string why = other_change(before, after); !why.empty()) {
    c.skipped = why;
    return c;
  }
  std::set<int> locs;
  for (const auto* s : {&before, &after})
    for (const auto& it : s->equipped) locs.insert(it.location);
  for (int l : locs)
    if (!same_item(at(before, l), at(after, l))) c.locations.push_back(l);
  if (c.locations.empty()) {
    c.skipped = "no equipment change";
    return c;
  }
  for (int l : c.locations) {
    const snap::Item* n = at(after, l);
    const snap::Item* o = at(before, l);
    if (!n) {
      c.skipped = "slot " + std::to_string(l) + " emptied (unequip not modeled)";
      return c;
    }
    if (!n->stats || (o && !o->stats)) {
      c.skipped = "item without stats";
      return c;
    }
  }

  snap::Snapshot cur = before;
  std::set<std::string> warned;
  for (int l : c.locations) {
    model::Prediction p = model::swap(cur, *at(after, l), l, db);
    for (auto& w : p.warnings)
      if (warned.insert(w).second) c.warnings.push_back(std::move(w));
    cur = std::move(p.after);
  }

  std::map<std::string, const snap::Skill*> sb;
  for (const auto& k : before.skills) sb[k.record] = &k;
  for (const auto& k : after.skills) {
    const auto it = sb.find(k.record);
    if (it != sb.end() && it->second->current_level == k.current_level && !same_passive(*it->second, k))
      c.notes.push_back("skill activity changed: " + k.record);
  }

  c.diffs = differences(cur, after);
  // The snapshot does not show a buff cast in between (bloodofdreeg1).
  // shortcut: one buff at a time — revisit when two buffs switching in one pair shows up as a FAIL.
  if (!c.diffs.empty() && db) {
    const auto explains = [&](const snap::Skill& k, bool on) {
      const auto t = model::toggle_buff(cur, k, on, *db);
      return t && differences(t->after, after).empty();
    };
    for (const auto& k : cur.skills)
      if (const bool on = explains(k, true); on || explains(k, false)) {
        c.buff = k.record + (on ? " switched on" : " switched off");
        c.diffs.clear();
        break;
      }
  }
  return c;
}

std::string swapped(const Check& c, const snap::Snapshot& before, const snap::Snapshot& after) {
  std::string out;
  for (int l : c.locations)
    out += (out.empty() ? "" : "; ") + std::string(names::equip_location(l)) + "(" + std::to_string(l) + ") " +
           item_label(before, l) + " -> " + item_label(after, l);
  return out;
}

std::vector<std::string> report(const Check& c, const snap::Snapshot& before, const snap::Snapshot& after,
                                const std::string& label, bool verbose) {
  std::vector<std::string> out;
  if (!c.skipped.empty()) return out;
  const bool pass = c.diffs.empty();
  out.push_back(std::string(pass ? "PASS " : "FAIL ") + label + " " + swapped(c, before, after) +
                (c.buff.empty() ? "" : " (" + c.buff + " between the snapshots)"));
  auto num = [](double v) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "%g", v);
    return std::string(buf);
  };
  for (const auto& d : c.diffs)
    out.push_back("    " + d.stat + ": predicted " + num(d.predicted) + ", engine " + num(d.actual) + " (tolerance " +
                  num(d.tolerance) + ")");
  if (!pass || verbose) {
    for (const auto& w : c.warnings) out.push_back("    warning: " + w);
    for (const auto& n : c.notes) out.push_back("    note: " + n);
  }
  return out;
}

RequirementCheck requirements(const snap::Snapshot& s) {
  RequirementCheck c;
  for (const auto& e : s.equipped) {
    if (!e.requirements || !e.requirements->met) continue;
    ++c.checked;
    const auto why = requirements::unmet(e, s);
    if (why.empty() == *e.requirements->met) continue;
    std::string line = e.record + ": model " + (why.empty() ? "met" : "not met") + ", engine " +
                       (*e.requirements->met ? "met" : "not met");
    for (const auto& u : why) line += "; " + requirements::text(u);
    c.mismatches.push_back(std::move(line));
  }
  return c;
}

}  // namespace gdc::calibrate
