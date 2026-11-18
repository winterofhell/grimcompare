#include "stat_model_internal.h"

namespace gdc::model {

void merge(Hidden& h, const sets::Bonus& b) {
  for (const auto& [k, v] : b.skills) h.skills[k] += v;
  for (const auto& [k, v] : b.masteries) h.masteries[k] += v;
  h.all += b.all;
  h.set_modifiers.insert(b.modifiers.begin(), b.modifiers.end());
  for (const auto& [k, v] : b.max_resist) h.max_resist[k] += v;
}

namespace {

// One record's Hidden part in field order, so sums match.
struct HiddenOp {
  enum Kind { Skill, Mastery, All, Modifier, MaxResist } kind;
  std::string key, value;
  double v = 0;
};

const std::vector<HiddenOp>& hidden_ops(const arz::Record& rec, const arz::Database& db) {
  return db.derived<std::vector<HiddenOp>>(rec, 0, [&] {
    std::vector<HiddenOp> ops;
    for (const auto& [key, field] : rec.fields) {
      auto grant = [&](HiddenOp::Kind kind, std::string_view name_prefix, std::string_view level_prefix) {
        if (!key.starts_with(name_prefix) || field.strings.empty() || field.strings[0].empty()) return;
        const std::string level_key = std::string(level_prefix) + key.substr(name_prefix.size());
        ops.push_back({kind, field.strings[0], {}, rec.number(level_key)});
      };
      grant(HiddenOp::Skill, "augmentSkillName", "augmentSkillLevel");
      grant(HiddenOp::Mastery, "augmentMasteryName", "augmentMasteryLevel");
      if (key == "augmentAllLevel") ops.push_back({HiddenOp::All, {}, {}, rec.number(key)});
      if (key.starts_with("modifierSkillName") && !field.strings.empty() && !field.strings[0].empty()) {
        const std::string_view target = rec.string("modifiedSkillName" + key.substr(17));
        if (!target.empty()) ops.push_back({HiddenOp::Modifier, std::string(target), field.strings[0], 0});
      }
      if (key.ends_with("MaxResist") && rec.number(key) != 0) ops.push_back({HiddenOp::MaxResist, key, {}, rec.number(key)});
    }
    return ops;
  });
}

}  // namespace

Hidden hidden(const snap::Item* it, const arz::Database& db) {
  Hidden h;
  if (!it) return h;
  for (const auto& name : item_records(*it)) {
    const auto rec = db.record(name);
    if (!rec) continue;
    for (const auto& op : hidden_ops(*rec, db)) switch (op.kind) {
        case HiddenOp::Skill: h.skills[op.key] += op.v; break;
        case HiddenOp::Mastery: h.masteries[op.key] += op.v; break;
        case HiddenOp::All: h.all += op.v; break;
        case HiddenOp::Modifier: h.modifiers.emplace(op.key, op.value); break;
        case HiddenOp::MaxResist: h.max_resist[op.key] += op.v; break;
      }
  }
  return h;
}

Grants grants(const snap::Snapshot& s, const Hidden& before, const Hidden& after) {
  Grants out;
  for (const auto* h : {&before, &after})
    for (const auto& [k, _] : h->skills)
      if (const double d = get(after.skills, k) - get(before.skills, k); d != 0) out.skills[k] = d;
  std::set<std::string> records;
  for (const auto* h : {&before, &after})
    for (const auto& [m, _] : h->masteries) records.insert(m);
  for (const auto& m : records)
    if (const double d = get(after.masteries, m) - get(before.masteries, m); d != 0) {
      out.masteries.push_back({m, {}, d});
      for (const auto& other : s.skills)
        if (other.id != 0 && other.record == m) out.masteries.back().ids.insert(other.id);
    }
  return out;
}

// Grants differing by exactly 0 are skipped: +0 changes nothing.
double grant_delta(const snap::Skill& sk, const Grants& g, const Hidden& before, const Hidden& after) {
  const auto it = g.skills.find(sk.record);
  double d = (it != g.skills.end() ? it->second : 0.0) + after.all - before.all;
  for (const auto& m : g.masteries)
    if (sk.record == m.record || m.ids.contains(sk.mastery_id)) d += m.delta;
  return d;
}

}  // namespace gdc::model
