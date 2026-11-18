#include "gdc/score.h"

#include <algorithm>
#include <cmath>
#include <set>

#include "gdc/attribute_names.h"

#include "score_internal.h"

namespace gdc::score {
namespace {

std::string resist_inputs(const Side& x) {
  std::string out = "enemy x";
  for (const auto& [t, m] : x.resist) out += " " + std::string(names::combat_attribute(t)) + " " + fmt(m, 2);
  return out;
}

// Splits the term's ln ratio over parts by linear change.
void contribute(std::vector<Contribution>& out, double ratio, double exponent, std::vector<Contribution> parts) {
  double sum = 0;
  for (const auto& c : parts) sum += c.points;
  if (!(ratio > 0) || sum == 0) return;
  const double scale = std::log(ratio) * exponent * 100 / sum;
  for (auto& c : parts)
    if (c.points != 0) {
      c.points *= scale;
      out.push_back(std::move(c));
    }
}

std::string resist_label(int type) {
  if (type == kPhysical) return "Physical taken (resist, armor)";
  if (type == kPoison) return "Acid/Poison Resist";
  if (type == kLife) return "Vitality Resist";
  return std::string(names::combat_attribute(type)) + " Resist";
}

// Effects scored on one side only are listed as unscored.
void add_one_sided(std::vector<Unscored>& unscored, const std::map<std::string, std::string>& b,
                   const std::map<std::string, std::string>& a) {
  for (const auto* m : {&b, &a})
    for (const auto& [record, why] : *m)
      if (!(m == &b ? a : b).contains(record) &&
          std::none_of(unscored.begin(), unscored.end(), [&](const Unscored& u) { return u.record == record; }))
        unscored.push_back({record, why});
}

void add_hits(const Scored& before, const Scored& after, Result& out) {
  for (const Side* x : {&before.side, &after.side})
    for (const auto& h : x->hits) {
      out.procs.insert(h.record);
      out.pets = out.pets || (h.pet && !h.player_scaled);
    }
}

bool pets_only(const Scored& before, const Scored& after) {
  for (const Scored* sd : {&before, &after})
    for (const auto& [prob, e] : sd->states)
      if (!(e.extra.per_sec > 0)) return false;
  return true;
}

// A pure summoner: Damage is procs and pets per second.
void pet_damage_ratio(const Scored& before, const Scored& after, const Settings& p, Result& out) {
  auto extra = [](const Eval& e) { return e.extra.per_sec; };
  out.ratio.damage = std::exp(after.ln_mean(extra) - before.ln_mean(extra));
  out.before += "procs+pets " + fmt(before.shown().extra.per_sec) + "/s:" + before.shown().extra.inputs + "; ";
  out.after += "procs+pets " + fmt(after.shown().extra.per_sec) + "/s:" + after.shown().extra.inputs + "; ";
  add_hits(before, after, out);
  add_one_sided(out.unscored, before.side.hits_unknown, after.side.hits_unknown);
  contribute(out.contributions, out.ratio.damage, p.damage_weight, {{"procs and pets", "", after.ln_mean(extra) - before.ln_mean(extra), ""}});
}

std::map<std::string, std::string> default_reductions(const Side& x) {
  std::map<std::string, std::string> m;
  for (const auto& r : x.effects.reductions)
    if (r.origin == effects::Origin::Default) m.emplace(r.record, r.basis);
  return m;
}

// Reductions in `x` with no equal one in `other`.
void changed_reductions(const Side& x, const Side& other, std::vector<effects::Reduction>& to) {
  for (const auto& r : x.effects.reductions)
    if (std::none_of(other.effects.reductions.begin(), other.effects.reductions.end(), [&](const effects::Reduction& o) {
          return o.record == r.record && o.category == r.category && o.amount == r.amount &&
                 std::abs(o.uptime - r.uptime) < 1e-6;
        }))
      to.push_back(r);
}

// Cooldown skills at their pace; others share by weight (§5).
double main_per_sec(const Eval& e, const std::vector<Skill>& skills) {
  double on_cooldown = 0, spam = 0, spam_weight = 0;
  for (std::size_t k = 0; k < skills.size(); ++k) {
    const SkillDamage& x = e.skills[k];
    if (x.cooldown > 0) on_cooldown += x.per_use * x.uses;
    else spam += skills[k].weight * x.per_use * x.rate, spam_weight += skills[k].weight;
  }
  return on_cooldown + (spam_weight > 0 ? spam / spam_weight : 0);
}

// D = geomean Π value^weight × (1 + extra/main)
void main_damage_ratio(const Scored& before, const Scored& after, const std::vector<Skill>& skills, const Settings& p,
                       std::vector<std::string>& notes_b, std::vector<std::string>& notes_a, Result& out) {
  const Side &sb = before.side, &sa = after.side;
  const Eval &b0 = before.shown(), &a0 = after.shown();
  add_one_sided(out.unscored, default_reductions(sb), default_reductions(sa));
  changed_reductions(sb, sa, out.rr_lost);
  changed_reductions(sa, sb, out.rr_gained);
  auto main = [&](const Eval& e) { return main_per_sec(e, skills); };
  std::vector<bool> both(skills.size(), true);
  bool extra = false, mains = true;
  for (const Scored* sd : {&before, &after})
    for (const auto& [prob, e] : sd->states) {
      for (std::size_t k = 0; k < skills.size(); ++k) both[k] = both[k] && e.skills[k].value > 0;
      extra = extra || e.extra.per_sec > 0;
      mains = mains && main(e) > 0;
    }
  auto share = [&](const Eval& e) { return extra && mains ? 1 + e.extra.per_sec / main(e) : 1.0; };
  auto damage = [&](const Eval& e) {
    double ln = 0;
    for (std::size_t k = 0; k < skills.size(); ++k)
      if (both[k]) ln += skills[k].weight * std::log(e.skills[k].value);
    return std::exp(ln) * share(e);
  };
  std::vector<Contribution> parts;
  for (std::size_t k = 0; k < skills.size(); ++k) {
    out.before += b0.skills[k].inputs + " w " + fmt(skills[k].weight, 2) + "; ";
    out.after += a0.skills[k].inputs + "; ";
    if (!both[k]) out.notes.push_back(stem(skills[k].record) + ": no damage computed");
    auto value = [&](const Eval& e) { return e.skills[k].value; };
    parts.push_back({"", skills[k].record, both[k] ? skills[k].weight * (after.ln_mean(value) - before.ln_mean(value)) : 0, ""});
  }
  const double ratio = std::exp(after.ln_mean(damage) - before.ln_mean(damage));
  const Extra &eb = b0.extra, &ea = a0.extra;
  if (extra && mains) {
    out.before += "main " + fmt(main(b0)) + "/s, procs+pets " + fmt(eb.per_sec) + "/s:" + eb.inputs + "; ";
    out.after += "main " + fmt(main(a0)) + "/s, procs+pets " + fmt(ea.per_sec) + "/s:" + ea.inputs + "; ";
    parts.push_back({"procs and pets", "", after.ln_mean(share) - before.ln_mean(share), ""});
    add_hits(before, after, out);
  } else if (extra) {
    out.notes.push_back("main skills' damage per second unknown (no attack / cast speed): procs and pets not scored");
  }
  add_one_sided(out.unscored, sb.hits_unknown, sa.hits_unknown);
  out.ratio.damage = ratio;
  contribute(out.contributions, ratio, p.damage_weight, std::move(parts));
  out.before += resist_inputs(sb) + "; ";
  out.after += resist_inputs(sa) + "; ";
  for (auto* notes : {&notes_b, &notes_a})
    for (auto& n : *notes)
      if (std::find(out.notes.begin(), out.notes.end(), n) == out.notes.end()) out.notes.push_back(std::move(n));
}

// Survivability ratio and parts; a resist's target joins its line.
void survival_ratio(const Survival& sb, const Survival& sa, const Settings& p, Result& out) {
  out.ratio.survival = sb.ehp > 0 ? sa.ehp / sb.ehp : 1;
  auto factor = [&](const char* label, double b, double a, const char* stat = "") {
    if (a > 0 && b > 0 && a != b) out.contributions.push_back({label, "", std::log(a / b) * p.survival_weight * 100, stat});
  };
  factor("Life", sb.life, sa.life, "life");
  factor("DA (enemy hits and crits)", sa.hit, sb.hit, "da");
  factor("Dodge / deflect", 1 - sa.avoid, 1 - sb.avoid);
  factor("Block", 1 - sa.block, 1 - sb.block);
  factor("Enemy damage reduction", 1 - sa.reduction, 1 - sb.reduction);
  factor("Proc crowd control", 1 - sa.cc, 1 - sb.cc);
  factor("Proc absorb / heal", 1 - sa.procs, 1 - sb.procs);
  double tb = 0, ta = 0;
  std::vector<Contribution> types;
  for (const auto& [type, v] : sb.taken) {
    const double a = sa.taken.contains(type) ? sa.taken.at(type) : 0;
    tb += v, ta += a;
    types.push_back({resist_label(type), "", v - a, "resist:" + std::to_string(type)});
  }
  if (ta > 0 && tb > 0) contribute(out.contributions, tb / ta, p.survival_weight, std::move(types));
  for (const auto& [name, f] : sb.thresholds) {
    const auto it = sa.thresholds.find(name);
    if (it == sa.thresholds.end()) continue;
    if (!name.starts_with("resist:")) {
      factor(("target " + name).c_str(), f, it->second);
      continue;
    }
    const auto part = std::find_if(out.contributions.begin(), out.contributions.end(),
                                   [&](const Contribution& c) { return c.stat == name; });
    const double points = f > 0 && it->second > 0 ? std::log(it->second / f) * p.survival_weight * 100 : 0;
    if (part != out.contributions.end()) part->points += points;
    else if (points != 0) out.contributions.push_back({resist_label(std::stoi(name.substr(7))), "", points, name});
  }
}

void other_ratio(const Other& ob, const Other& oa, const Settings& p, Result& out) {
  out.ratio.other = ob.value > 0 ? oa.value / ob.value : 1;
  std::set<std::string> names;
  for (const auto* o : {&ob, &oa})
    for (const auto& [k, _] : o->terms) names.insert(k);
  std::vector<Contribution> parts;
  for (const auto& k : names) {
    const auto b = ob.terms.contains(k) ? ob.terms.at(k) : std::pair{0.0, 0.0};
    const auto a = oa.terms.contains(k) ? oa.terms.at(k) : std::pair{0.0, 0.0};
    parts.push_back({k, "", a.first * a.second - b.first * b.second, k});
    if (std::abs(a.first * a.second - b.first * b.second) < 1e-9 && std::abs(a.first - b.first) < 1e-9) continue;
    out.other_changes.push_back({k, b.first, a.first, a.second != 0 ? a.second : b.second});
  }
  contribute(out.contributions, out.ratio.other, p.other_weight, std::move(parts));
}

}  // namespace

struct BeforeSide {
  const snap::Snapshot* snapshot;
  Amounts base;
  Scored scored;
  std::vector<std::string> notes;
};

Result compare(const snap::Snapshot& before, const snap::Snapshot& after, const std::vector<Skill>& skills,
               const Settings& p, const arz::Database* db, Memo* memo) {
  Result out;
  std::vector<std::string> notes_b, notes_a;
  const Amounts base = db ? shared_base(before, after, *db) : Amounts{};
  // model::swap predictions keep before's capture time and live pets.
  const snap::Snapshot* captured = after.taken_at == before.taken_at ? &before : nullptr;
  Memo own;
  if (!memo) memo = &own;
  if (!*memo || (*memo)->snapshot != &before || (*memo)->base != base) {
    auto side = std::make_shared<BeforeSide>(BeforeSide{&before, base, {}, {}});
    side->scored = score_side(before, skills, p, db, base, side->notes);
    *memo = std::move(side);
  }
  const Scored& scored_before = (*memo)->scored;
  notes_b = (*memo)->notes;
  const Scored scored_after = score_side(after, skills, p, db, base, notes_a, captured);
  if (!scored_before.buffs.empty()) out.before += "buff states (likeliest shown): " + scored_before.buffs + "; ";
  if (!scored_after.buffs.empty()) out.after += "buff states (likeliest shown): " + scored_after.buffs + "; ";
  if (!db) out.notes.push_back("no database: damage not scored");
  else if (skills.empty() && pets_only(scored_before, scored_after)) pet_damage_ratio(scored_before, scored_after, p, out);
  else if (skills.empty()) out.notes.push_back("no main damage skill found: damage not scored");
  else main_damage_ratio(scored_before, scored_after, skills, p, notes_b, notes_a, out);
  const Procs &pb = scored_before.procs, &pa = scored_after.procs;
  add_one_sided(out.unscored, pb.unknown, pa.unknown);
  out.procs.insert(pb.scored.begin(), pb.scored.end());
  out.procs.insert(pa.scored.begin(), pa.scored.end());
  const Survival sb = mixed_survival(scored_before), sa = mixed_survival(scored_after);
  survival_ratio(sb, sa, p, out);
  const Other ob = mixed_other(scored_before), oa = mixed_other(scored_after);
  other_ratio(ob, oa, p, out);
  std::sort(out.contributions.begin(), out.contributions.end(),
            [](const Contribution& a, const Contribution& b) { return std::abs(a.points) > std::abs(b.points); });
  out.before += sb.inputs + "; " + ob.inputs;
  out.after += sa.inputs + "; " + oa.inputs;
  if (char_modifier(before, kTotalSpeed) != char_modifier(after, kTotalSpeed))
    out.notes.push_back("total speed changes: added to attack and cast speed (unverified)");
  const double total = std::pow(out.ratio.damage, p.damage_weight) * std::pow(out.ratio.survival, p.survival_weight) *
                       std::pow(out.ratio.other, p.other_weight);
  out.percent = std::isfinite(total) ? (total - 1) * 100 : 0;
  if (!std::isfinite(total)) out.notes.push_back("score not finite");
  return out;
}

}  // namespace gdc::score
