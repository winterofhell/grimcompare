#include <algorithm>

#include "gdc/db_fields.h"
#include "gdc/sets.h"
#include "gdc/stat_model.h"

#include "score_internal.h"

namespace gdc::score {
namespace {

using Totals = Retaliation;

void add_pct(Totals& t, int type, double v) {
  const std::span<const int> spread = fields::spread(type, false);
  for (int x : spread.empty() ? std::span<const int>(&type, 1) : spread) t.pct[x] += v;
}

// A record's (or a set's) retaliation fields. Flat with a chance counts at its chance.
// shortcut: DoTs (retaliationSlow*), crowd control, % of current life, retaliationDamageMultModifier and modifier
// chances are left out — revisit with a retaliation build that leans on them.
template <class At>
void read(const At& at, double share, Totals& out) {
  for (const auto& f : fields::retaliation()) {
    if (const double min = f.min.empty() ? 0 : at(f.min); min != 0) {
      const double max = std::max(min, at(f.max)), chance = at(f.chance);
      out.flat[{f.type, false}] += share * (min + max) / 2 * (chance > 0 && chance < 100 ? chance / 100 : 1);
    }
    if (const double m = at(f.modifier); m != 0) add_pct(out, f.type, share * m);
  }
}

void read(const arz::Record& r, std::uint32_t level, double share, Totals& out) {
  read([&](std::string_view k) { return at_level(r, k, level); }, share, out);
}

// Skill::CollectLocalRetaliationDamagePercent (vt+0x400): retaliationDamagePct of the skill and its modifiers.
double added_pct(const arz::Record& r, std::uint32_t level, const Modifiers& mods, const arz::Database& db) {
  double pct = at_level(r, "retaliationDamagePct", level);
  for (const auto& [m, ml] : mods)
    if (const arz::Record* x = db.record(m)) pct += at_level(*x, "retaliationDamagePct", ml);
  return pct;
}

// The share of the time a skill's retaliation is up: passives always, buffs at their uptime, kept-up casts
// (toggles, auras) always. An attack's own retaliation is its local retaliation, added to that attack only.
double up_share(const snap::Skill& sk, const arz::Record& r, const Side& side, const Procs& procs,
                const std::vector<Buff>& learned, const Settings& p, const arz::Database& db) {
  if (const auto o = p.uptime.find(sk.record); o != p.uptime.end()) return o->second;
  for (const auto* list : {&procs.buffs, &learned})
    for (const auto& b : *list)
      if (b.record == sk.record) return b.uptime;
  if (classes::is_a(r.cls, "Skill_Passive") && !model::triggered_buff(r)) return 1;
  for (const auto& e : side.effects.effects)
    if (!e.pet && e.record == sk.record) {
      const arz::Record* buff = db.record(r.string("buffSkillName"));
      return effects::uptime(e, at_level(buff ? *buff : r, "skillActiveDuration", sk.current_level));
    }
  return 0;
}

// Character totals: items, sets, skills (SkillManager::CollectAvailableRetaliationAttributes 0x535C40 and
// CollectAvailableRetaliationModifierAttributes 0x535CE0 read equipment, the character and the skill manager).
Totals totals(const snap::Snapshot& s, const Settings& p, const arz::Database& db, const Procs& procs,
              const std::vector<Buff>& learned, const Side& side) {
  Totals t;
  for (const auto& it : s.equipped) {
    if (!it.stats) continue;
    for (const auto& d : it.stats->retaliation)
      if (!d.dot) t.flat[{d.type, false}] += (d.min + d.max) / 2;  // min / max already x chance
    for (const auto& m : it.stats->retaliation_modifiers) add_pct(t, m.type, m.damage_modifier);
  }
  for (const auto& [set, n] : sets::counts(s))
    if (const arz::Record* r = db.record(set)) read([&](std::string_view k) { return sets::bonus_field(*r, k, n); }, 1, t);
  for (const auto* list : {&s.skills, &s.item_skills})
    for (const auto& sk : *list) {
      if (sk.current_level == 0 || (list == &s.skills && sk.level == 0)) continue;
      const arz::Record* r = db.record(sk.record);
      if (!r || classes::is_a(r->cls, "Skill_Modifier")) continue;  // read with its parent
      const Modifiers mods = modifiers_of(s, sk);
      if (added_pct(*r, sk.current_level, mods, db) > 0) continue;
      const double share = up_share(sk, *r, side, procs, learned, p, db);
      if (share <= 0) continue;
      read(*r, sk.current_level, share, t);
      if (const arz::Record* buff = db.record(r->string("buffSkillName"))) read(*buff, sk.current_level, share, t);
      for (const auto& [m, ml] : mods)
        if (const arz::Record* x = db.record(m)) read(*x, ml, share, t);
    }
  return t;
}

// Skill::CollectCombatParameters 0x499570: with a retaliationDamagePct, the skill's local retaliation (vt+0x3F0)
// and the character's (0x535C40) at pct x 0.01, with local (vt+0x3F8) and character retaliation %.
Totals added(const arz::Record& r, std::uint32_t level, const Modifiers& mods, const Totals& global_totals,
             const arz::Database& db) {
  const double pct = added_pct(r, level, mods, db);
  if (pct <= 0) return {};
  Totals t = global_totals;
  read(r, level, 1, t);
  for (const auto& [m, ml] : mods)
    if (const arz::Record* x = db.record(m)) read(*x, ml, 1, t);
  for (auto& [k, v] : t.flat) v *= pct / 100;  // flat only: the % entries are not scaled
  return t;
}

}  // namespace

// CombatAttributeAccumulator: conversions, then ModifyDamage with the retaliation % of the final type; the enemy's
// resists and the preset's type weights as for any hit. Done per buff state (converted_for), with its conversions.
double retaliation_amount(const Retaliation& t, const Conversions& local, const Conversions& global, const Side& side,
                          const Settings& p, std::vector<std::string>& notes, Amounts* parts) {
  if (t.flat.empty()) return 0;
  double sum = 0;
  for (const auto& [k, v] : convert_pairs(t.flat, local, global, notes)) {
    const auto pct = t.pct.find(k.first);
    const auto r = side.resist.find(k.first);
    const double x = v * std::max(0.0, 1 + (pct != t.pct.end() ? pct->second : 0) / 100) *
                     (r != side.resist.end() ? std::max(0.0, r->second) : 1.0) * type_weight(p, k);
    sum += x;
    if (parts) (*parts)[k] += x;
  }
  return sum;
}

// CombatManager::Retaliation 0x11FBB0: flat at scale 1.0, retaliation %, global conversions; no offensive %,
// attribute bonus or crit.
void add_retaliation(const snap::Snapshot& s, const Settings& p, const arz::Database& db, const Procs& procs,
                     const std::vector<Buff>& learned, Side& side) {
  side.retaliation = totals(s, p, db, procs, learned, side);
  for (auto& m : side.mains) {
    if (!m.skill) continue;
    if (const arz::Record* r = db.record(m.skill->record))
      m.raw.retaliation = added(*r, m.skill->current_level, modifiers_of(s, *m.skill), side.retaliation, db);
  }
  for (auto& h : side.hits) {
    if (h.pet) continue;
    const arz::Record* r = db.record(h.record);
    if (!r) continue;
    const auto e = std::find_if(side.effects.effects.begin(), side.effects.effects.end(),
                                [&](const effects::Effect& x) { return !x.pet && x.record == h.record; });
    if (e == side.effects.effects.end()) continue;
    h.retaliation = added(*r, std::max<std::uint32_t>(e->level, 1), e->modifiers, side.retaliation, db);
  }
}

}  // namespace gdc::score
