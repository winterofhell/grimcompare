#include "gdc/calibrate.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <set>

#include "gdc/attribute_names.h"
#include "gdc/stat_model.h"

namespace gdc::calibrate {
namespace {

const snap::Item* at(const snap::Snapshot& s, int location) {
  for (const auto& it : s.equipped)
    if (it.location == location) return &it;
  return nullptr;
}

std::string component(const std::optional<snap::Component>& c) { return c ? c->record : std::string{}; }

bool same_item(const snap::Item* a, const snap::Item* b) {
  if (!a || !b) return a == b;
  return a->id == b->id && a->record == b->record && component(a->relic) == component(b->relic) &&
         component(a->augment) == component(b->augment);
}

// Attribute points (types 1..3), skill and devotion points by record: what else could move the sheet.
std::string other_change(const snap::Snapshot& a, const snap::Snapshot& b) {
  if (a.character_name != b.character_name) return "different character";
  if (a.level != b.level) return "level changed";
  if (a.difficulty != b.difficulty) return "difficulty changed";
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

void compare(Check& c, const std::string& stat, double predicted, double actual, double tol) {
  if (!(std::abs(predicted - actual) <= tol)) c.diffs.push_back({stat, predicted, actual, tol});
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

  // A skill whose contribution changed without a level change: a buff or toggle switched in between.
  std::map<std::string, const snap::Skill*> sb;
  for (const auto& k : before.skills) sb[k.record] = &k;
  for (const auto& k : after.skills) {
    const auto it = sb.find(k.record);
    if (it != sb.end() && it->second->current_level == k.current_level && !same_passive(*it->second, k))
      c.notes.push_back("skill activity changed: " + k.record);
  }

  const model::Sheet got = model::sheet(cur), want = model::sheet(after);
  compare(c, "Physique", got.physique, want.physique, 1);
  compare(c, "Cunning", got.cunning, want.cunning, 1);
  compare(c, "Spirit", got.spirit, want.spirit, 1);
  compare(c, "Life", got.life, want.life, 1);
  compare(c, "Energy", got.energy, want.energy, 1);
  compare(c, "Life regen", got.life_regen, want.life_regen, 0.1);
  compare(c, "Energy regen", got.energy_regen, want.energy_regen, 0.1);
  compare(c, "OA", got.oa, want.oa, 1);
  compare(c, "DA", got.da, want.da, 1);
  compare(c, "Run speed", got.run_speed, want.run_speed, 0.001);
  compare(c, "Armor", got.armor, want.armor, 1);
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
    compare(c, name + " (raw)", g.raw, w.raw, 0.1);
    compare(c, name + " (shown)", g.shown, w.shown, 0.1);
  }
  return c;
}

}  // namespace gdc::calibrate
