#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <set>

#include "gdc/calibrate.h"
#include "gdc/classes.h"

#include "score_internal.h"

namespace gdc::calibrate {
namespace {

using score::Amounts;
using score::Key;

// Elemental in thirds and base physical as physical, as the model keeps them.
void add_split(Amounts& out, Key k, double v) {
  if (k.first == score::kElemental) {
    for (int t : {score::kCold, score::kFire, score::kLightning}) out[{t, k.second}] += v / 3;
    return;
  }
  if (k.first == score::kBasePhysical) k.first = score::kPhysical;
  if (score::damage_type(k.first)) out[k] += v;
}

// Per use: the mean roll, a DoT per second × its processed duration. False: a DoT of unknown duration.
bool engine_amounts(const snap::SkillDps& d, Amounts& out, std::map<Key, double>& seconds) {
  for (const auto& e : d.main_hand) {
    const double mean = (static_cast<double>(e.min) + e.max) / 2;
    if (mean == 0) continue;
    if (e.dot && !(e.duration > 0)) return false;
    add_split(out, {e.type, e.dot}, e.dot ? mean * e.duration : mean);
    if (e.dot) {
      Key k{e.type == score::kBasePhysical ? score::kPhysical : e.type, true};
      if (k.first == score::kElemental)
        for (int t : {score::kCold, score::kFire, score::kLightning}) seconds[{t, true}] = e.duration;
      else
        seconds[k] = e.duration;
    }
  }
  return true;
}

const snap::SkillDps* engine_for(const snap::Snapshot& s, const snap::Skill& sk) {
  for (const auto& d : s.skill_dps)
    if (d.skill_id == sk.id) return &d;
  return nullptr;
}

// Why a skill is not compared; empty: compare it.
std::string skip_reason(const snap::Skill& sk, const snap::SkillDps& d, const arz::Database& db) {
  const arz::Record* r = db.record(sk.record);
  if (!r) return "not in the database";
  // Player::CalculateSkillDps 0x3DFD30: Skill_WeaponPool_ChargedLinear / ChargedScaling are shown as the default
  // attack at their live charges.
  if (classes::is_a(r->cls, "Skill_WeaponPool_ChargedLinear") || classes::is_a(r->cls, "Skill_WeaponPool_ChargedScaling"))
    return "charged weapon pool skill: the engine shows the default attack at live charges";
  if (d.default_attack) return "not usable with these weapons: the engine shows the default attack";
  return {};
}

}  // namespace

bool damage_off(const TypeDiff& d) {
  const double diff = std::abs(d.model - d.engine);
  return diff > 1 && diff > 0.02 * std::max(std::abs(d.model), std::abs(d.engine));
}

bool SkillDamageCheck::pass() const {
  return skipped.empty() && std::none_of(types.begin(), types.end(), damage_off);
}

std::vector<SkillDamageCheck> damage(const snap::Snapshot& s, const arz::Database& db, const std::vector<std::string>& records) {
  std::vector<SkillDamageCheck> out;
  std::vector<score::Skill> mains;
  std::vector<const snap::SkillDps*> engine;
  for (const auto& sk : s.skills) {
    if (sk.current_level == 0) continue;
    if (!records.empty() && std::find(records.begin(), records.end(), sk.record) == records.end()) continue;
    const snap::SkillDps* d = engine_for(s, sk);
    if (!d) continue;
    if (sk.record != score::kDefaultAttack && !score::skill_kind(db, sk.record, sk.current_level).main) continue;
    SkillDamageCheck c{sk.record, skip_reason(sk, *d, db), {}};
    if (c.skipped.empty()) {
      mains.push_back({sk.record, 1});
      engine.push_back(d);
    }
    out.push_back(std::move(c));
  }
  if (mains.empty()) return out;
  // As scoring builds a side, with "% Retaliation Added to Attack" in, and no enemy resists or type weights.
  score::Settings p;
  p.retaliation_damage = true;
  std::vector<std::string> notes;
  score::Side x = score::side(s, mains, p, db, score::shared_base(s, s, db), notes);
  const score::Procs procs = score::procs(s, x, p, db);
  score::add_retaliation(s, p, db, procs, score::learned_buffs(s, x, p, db), x);
  x.resist.clear();
  const score::Conversions global = score::global_pairs(s.conversions);
  std::size_t next = 0;
  for (auto& c : out) {
    if (!c.skipped.empty()) continue;
    const std::size_t i = next++;
    const score::Main& m = x.mains[i];
    Amounts model_parts, engine_parts, model;
    std::map<Key, double> seconds;
    if (!engine_amounts(*engine[i], engine_parts, seconds)) {
      c.skipped = "an engine DoT of unknown layout (no duration)";
      continue;
    }
    const score::Pool pool = score::main_converted(m.raw, x.gear, global, notes);
    score::skill_damage(s, *m.skill, m.raw, x.gear, p, notes, &model_parts, nullptr, false, &pool);
    score::retaliation_amount(m.raw.retaliation, m.raw.conversion, global, x, p, notes, &model_parts);
    for (const auto& [k, v] : model_parts) add_split(model, k, v);
    std::set<Key> keys;
    for (const auto* a : {&model, &engine_parts})
      for (const auto& [k, v] : *a)
        if (std::abs(v) >= 0.5) keys.insert(k);
    for (const Key& k : keys) {
      const auto sec = seconds.find(k);
      c.types.push_back({k, model.contains(k) ? model.at(k) : 0, engine_parts.contains(k) ? engine_parts.at(k) : 0,
                         sec != seconds.end() ? sec->second : 0});
    }
    if (c.types.empty()) c.skipped = "no damage on either side";
  }
  return out;
}

std::vector<std::string> damage_report(const std::vector<SkillDamageCheck>& checks, const std::string& label, bool verbose) {
  std::vector<std::string> out;
  auto num = [](double v) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "%.1f", v);
    return std::string(buf);
  };
  for (const auto& c : checks) {
    if (!c.skipped.empty()) {
      if (verbose) out.push_back("SKIP DMG " + label + " " + score::stem(c.record) + " (" + c.skipped + ")");
      continue;
    }
    const bool pass = c.pass();
    out.push_back(std::string(pass ? "PASS DMG " : "FAIL DMG ") + label + " " + score::stem(c.record));
    if (pass && !verbose) continue;
    for (const auto& d : c.types) {
      std::string line = "    " + score::damage_label(d.key) + ": model " + num(d.model) + ", engine " + num(d.engine);
      if (d.engine_seconds > 0) line += " (" + num(d.engine_seconds) + " s)";
      if (damage_off(d)) line += " OFF";
      out.push_back(std::move(line));
    }
  }
  return out;
}

}  // namespace gdc::calibrate
