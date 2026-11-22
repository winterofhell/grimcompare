#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <set>

#include "gdc/calibrate.h"
#include "gdc/classes.h"
#include "gdc/stat_model.h"

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

// Skill::CollectCombatParameters 0x499570 hands Process every entry the reductions on the character
// (DurationDamageManager::GetFixedDamage 28 / 30 / 31). CombatAttributeAbsDamage::Process 0x113450 takes the larger of
// the total % and Physical % for Physical (type 2), of the total % and Elemental % for Cold / Fire / Lightning;
// CombatAttributeDurDamage::Process 0x113F30 the Physical one only. The flat one (29) comes off each entry.
double reduction_pct(const snap::DamageReduction& r, int type, bool dot) {
  double pct = r.percent;
  if (type == score::kPhysical) pct = std::max<double>(pct, r.physical_percent);
  if (!dot && (type == score::kCold || type == score::kFire || type == score::kLightning))
    pct = std::max<double>(pct, r.elemental_percent);
  return pct;
}

// Per use: the mean roll, a DoT per second × its processed duration, divided back by the reductions on the character.
// False: a DoT of unknown duration.
bool engine_amounts(const std::vector<snap::Damage>& list, const snap::DamageReduction& red, Amounts& out,
                    std::map<Key, double>& seconds) {
  for (const auto& e : list) {
    const double mean = (static_cast<double>(e.min) + e.max) / 2;
    if (mean == 0) continue;
    if (e.dot && !(e.duration > 0)) return false;
    const double kept = 1 - reduction_pct(red, e.type, e.dot) / 100;
    add_split(out, {e.type, e.dot}, (e.dot ? mean * e.duration : mean) / kept);
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

// The record holding a buff's own stats, as procs() takes it; null: not a buff.
const arz::Record* buff_stats(const arz::Record& rec, const arz::Database& db) {
  const arz::Record* buff = db.record(rec.string("buffSkillName"));
  return buff && model::self_buff(*buff) ? buff : model::self_buff(rec) ? &rec : nullptr;
}

// Buffs up at capture that passives and toggles are not: their skill records count nowhere in the captured totals.
template <class F>
void each_active_buff(const snap::Snapshot& s, const arz::Database& db, F f) {
  for (const auto* list : {&s.skills, &s.item_skills})
    for (const auto& sk : *list) {
      if (sk.buff_active != true || sk.current_level == 0) continue;
      const arz::Record* rec = db.record(sk.record);
      // Skill_BuffSelfDuration (timed) derives from Skill_BuffSelfToggled (re/GameClassTree.tsv).
      if (!rec || classes::is_a(rec->cls, "Skill_Passive") || classes::is_a(rec->cls, "Skill_BuffRadiusToggled") ||
          (classes::is_a(rec->cls, "Skill_BuffSelfToggled") && !classes::is_a(rec->cls, "Skill_BuffSelfDuration")))
        continue;
      if (const arz::Record* stats = buff_stats(*rec, db)) f(sk, *stats);
    }
}

// "Total Damage Modified" of timed buffs up at capture: a character modifier (MECHANICS §2 rule 7) the sheet applies
// with the others (ModifyDamage at 0x499DB1), in no captured total.
double active_buff_mult(const snap::Snapshot& s, const arz::Database& db, std::string& names) {
  double mult = 1;
  each_active_buff(s, db, [&](const snap::Skill& sk, const arz::Record& stats) {
    double m = std::max(0.0, 1 + score::at_level(stats, "offensiveDamageMultModifier", sk.current_level) / 100);
    for (const auto& [mr, ml] : score::modifiers_of(s, sk))
      if (const arz::Record* x = db.record(mr)) m *= std::max(0.0, 1 + score::at_level(*x, "offensiveDamageMultModifier", ml) / 100);
    if (m == 1) return;
    mult *= m;
    names += (names.empty() ? "" : ", ") + score::stem(sk.record);
  });
  return mult;
}

// A timed buff's flat damage is left out of the model's gear (weapon_flat()).
std::string active_buff_flat(const snap::Snapshot& s, const arz::Database& db) {
  std::string out;
  each_active_buff(s, db, [&](const snap::Skill& sk, const arz::Record& stats) {
    if (out.empty() && score::has_damage(db, stats, sk.current_level)) out = score::stem(sk.record);
  });
  return out;
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

// For the whole snapshot; empty: compare.
std::string snapshot_skip(const snap::Snapshot& s, const arz::Database& db) {
  if (s.schema >= 16 && !s.damage_reduction) return "the damage reductions on the character were not read";
  if (s.damage_reduction && s.damage_reduction->absolute != 0)
    return "a flat damage reduction on the character at capture (taken off each engine entry)";
  if (s.damage_reduction)
    for (const auto pct : {s.damage_reduction->percent, s.damage_reduction->physical_percent, s.damage_reduction->elemental_percent})
      if (pct >= 100) return "a damage reduction of 100% or more on the character at capture";
  if (const std::string b = active_buff_flat(s, db); !b.empty())
    return "a timed buff with flat damage up at capture (" + b + ")";
  return {};
}

// Model and engine per type into `out`, the L Hand pass as `off_hand`.
void compare(const Amounts& model, const Amounts& engine, const std::map<Key, double>& seconds, bool off_hand,
             std::vector<TypeDiff>& out) {
  std::set<Key> keys;
  for (const auto* a : {&model, &engine})
    for (const auto& [k, v] : *a)
      if (std::abs(v) >= 0.5) keys.insert(k);
  for (const Key& k : keys) {
    const auto sec = seconds.find(k);
    out.push_back({k, model.contains(k) ? model.at(k) : 0, engine.contains(k) ? engine.at(k) : 0,
                   sec != seconds.end() ? sec->second : 0, off_hand});
  }
}

std::string pct_text(double v) {
  char buf[32];
  std::snprintf(buf, sizeof buf, "%g%%", v);
  return buf;
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
  const std::string whole = snapshot_skip(s, db);
  for (const auto* list : {&s.skills, &s.item_skills})
    for (const auto& sk : *list) {
      if (sk.current_level == 0) continue;
      if (!records.empty() && std::find(records.begin(), records.end(), sk.record) == records.end()) continue;
      const bool item = list == &s.item_skills;
      // side() takes the learned one, then the first item skill of the record.
      if (item && (score::skill_by_record(s, sk.record) ||
                   std::find_if(s.item_skills.data(), &sk, [&](const snap::Skill& x) { return x.record == sk.record; }) != &sk))
        continue;
      const snap::SkillDps* d = engine_for(s, sk);
      if (!d) continue;
      if (sk.record != score::kDefaultAttack && !score::skill_kind(db, sk.record, sk.current_level).main) continue;
      SkillDamageCheck c{sk.record, skip_reason(sk, *d, db), {}, {}, {}};
      if (item) c.rules.push_back("item skill");
      if (c.skipped.empty()) c.skipped = whole;
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
  const snap::DamageReduction red = s.damage_reduction.value_or(snap::DamageReduction{});
  std::string buffs;
  const double mult = active_buff_mult(s, db, buffs);
  std::string note;
  if (mult != 1) note = "model x" + score::fmt(mult, 3) + " for buffs up: " + buffs;
  if (red.percent != 0 || red.physical_percent != 0 || red.elemental_percent != 0)
    note += (note.empty() ? "" : "; ") + std::string("engine without the damage reductions on the character (") +
            pct_text(red.percent) + " all, " + pct_text(red.physical_percent) + " physical, " +
            pct_text(red.elemental_percent) + " elemental)";
  std::size_t next = 0;
  for (auto& c : out) {
    if (!c.skipped.empty()) continue;
    const std::size_t i = next++;
    const score::Main& m = x.mains[i];
    const snap::SkillDps& d = *engine[i];
    Amounts engine_r, engine_l;
    std::map<Key, double> seconds;
    if (!engine_amounts(d.main_hand, red, engine_r, seconds) ||
        (d.dual_wield && !engine_amounts(d.off_hand, red, engine_l, seconds))) {
      c.skipped = "an engine DoT of unknown layout (no duration)";
      continue;
    }
    const score::Pool pool = score::main_converted(m.raw, x.gear, global, notes);
    if (!x.gear.off.empty() && !pool.off && m.raw.weapon_pct > 0) {
      c.skipped = "dual wield, a weapon damage skill outside the weapon classes: one pass with both weapons, not modeled";
      continue;
    }
    // Per hand: each pass with its retaliation, × the buffs' multiplier.
    auto hand = [&](const score::Pool& one) {
      Amounts parts, model;
      score::skill_damage(s, *m.skill, m.raw, x.gear, p, notes, &parts, nullptr, false, &one);
      score::retaliation_amount(m.raw.retaliation, m.raw.conversion, global, x, p, notes, &parts);
      for (const auto& [k, v] : parts) add_split(model, k, v * mult);
      return model;
    };
    score::Pool r = pool;
    r.off.reset();
    Amounts model_r = hand(r), model_l;
    if (pool.off) {
      score::Pool l;
      static_cast<score::Pass&>(l) = *pool.off;
      l.retaliation = pool.retaliation;
      model_l = hand(l);
      // CalculateSkillDps folds each L pass DoT into the R pass one of the same source (FilterSameSource 0x11AE40):
      // the part both passes share leaves the L list, each weapon's own stays (as score::skill_damage).
      if (pool.shared) {
        static_cast<score::Pass&>(l) = *pool.shared;
        for (const auto& [k, v] : hand(l))
          if (const auto it = model_l.find(k); k.second && it != model_l.end()) it->second = std::max(0.0, it->second - v);
      }
    }
    compare(model_r, engine_r, seconds, false, c.types);
    // Without the flag (shield, caster off-hand, one weapon) the sheet leaves the L Hand list out.
    if (d.dual_wield || pool.off) compare(model_l, d.dual_wield ? engine_l : Amounts{}, seconds, true, c.types);
    c.note = note;
    if (c.types.empty()) c.skipped = "no damage on either side";
    if (const arz::Record* r = db.record(c.record)) c.rules.push_back("class " + r->cls);
    if (c.record == score::kDefaultAttack) c.rules.push_back("default attack");
    if (m.raw.weapon_pct > 0) c.rules.push_back("weapon damage");
    if (!m.raw.flat.empty()) c.rules.push_back("skill flat damage");
    if (pool.off) c.rules.push_back("dual wield");
    if (!m.raw.conversion.empty()) c.rules.push_back("skill conversion");
    if (!global.empty()) c.rules.push_back("gear conversion");
    if (!m.raw.retaliation.flat.empty()) c.rules.push_back("retaliation added");
    if (std::any_of(c.types.begin(), c.types.end(), [](const TypeDiff& t) { return t.key.second; })) c.rules.push_back("DoT");
    if (mult != 1) c.rules.push_back("buff multiplier");
    if (red.percent != 0 || red.physical_percent != 0 || red.elemental_percent != 0) c.rules.push_back("damage reductions");
  }
  return out;
}

std::vector<SkillDamageCheck> pets(const snap::Snapshot& s, const arz::Database& db) {
  std::vector<SkillDamageCheck> out;
  for (const auto& pet : s.pets) {
    SkillDamageCheck& c = out.emplace_back();
    c.record = pet.record;
    c.rules = {"pet %"};
    if (pet.damage_modifiers.empty()) {
      if (s.schema < 17) out.pop_back();  // not captured before schema 17
      else c.skipped = "no engine totals captured for this pet";
      continue;
    }
    if (!db.record(pet.record)) {
      c.skipped = "not in the database";
      continue;
    }
    std::vector<Key> keys;
    for (const auto& row : pet.damage_modifiers)
      if (score::damage_type(row.type)) keys.insert(keys.end(), {{row.type, false}, {row.type, true}});
    const auto model = score::pet_record_pct(s, db, pet.record, keys);
    for (const auto& row : pet.damage_modifiers)
      if (score::damage_type(row.type))
        for (const bool dot : {false, true})
          c.types.push_back({{row.type, dot}, model.at({row.type, dot}), dot ? row.dot_modifier : row.damage_modifier, 0, false});
    if (c.types.empty()) c.skipped = "no damage type in the engine totals";
  }
  return out;
}

std::vector<std::string> damage_summary(const std::vector<SkillDamageCheck>& checks) {
  struct Count {
    int pass = 0, fail = 0;
    std::set<std::string> failing;
    void add(bool ok, const std::string& who) {
      ok ? ++pass : ++fail;
      if (!ok) failing.insert(who);
    }
  };
  std::map<std::string, Count> rules, skills, types;
  std::map<std::string, int> skips;
  for (const auto& c : checks) {
    if (!c.skipped.empty()) {
      ++skips[c.skipped];
      continue;
    }
    const std::string who = score::stem(c.record);
    const bool ok = c.pass();
    for (const auto& r : c.rules) rules[r].add(ok, who);
    skills[who].add(ok, who);
    const bool pet = std::find(c.rules.begin(), c.rules.end(), "pet %") != c.rules.end();
    for (const auto& t : c.types)
      types[std::string(pet ? "pet " : "") + (t.off_hand ? "L Hand " : "") + score::damage_label(t.key)].add(!damage_off(t), who);
  }
  auto names = [](const std::set<std::string>& v) {
    std::string out;
    for (const auto& n : v) out += (out.empty() ? "" : ", ") + n;
    return out;
  };
  std::vector<std::string> out;
  std::string confirmed;
  for (const auto& [r, n] : rules)
    if (!n.fail) confirmed += (confirmed.empty() ? "" : ", ") + r + " (" + std::to_string(n.pass) + ")";
  out.push_back("RULES confirmed: " + (confirmed.empty() ? std::string("none") : confirmed));
  for (const auto& [r, n] : rules)
    if (n.fail)
      out.push_back("RULE failing: " + r + ": " + std::to_string(n.fail) + " of " + std::to_string(n.pass + n.fail) +
                    " off (" + names(n.failing) + ")");
  for (const auto& [k, n] : skills)
    out.push_back("SKILL " + k + ": " + std::to_string(n.pass) + " pass, " + std::to_string(n.fail) + " fail");
  for (const auto& [k, n] : types)
    out.push_back("TYPE " + k + ": " + std::to_string(n.pass) + " ok, " + std::to_string(n.fail) + " off" +
                  (n.fail ? " (" + names(n.failing) + ")" : ""));
  for (const auto& [why, n] : skips) out.push_back("SKIPPED " + std::to_string(n) + "x " + why);
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
    const std::string what = std::find(c.rules.begin(), c.rules.end(), "pet %") != c.rules.end() ? "PET% " : "DMG ";
    if (!c.skipped.empty()) {
      out.push_back("SKIP " + what + label + " " + score::stem(c.record) + " (" + c.skipped + ")");
      continue;
    }
    const bool pass = c.pass();
    out.push_back(std::string(pass ? "PASS " : "FAIL ") + what + label + " " + score::stem(c.record) +
                  (c.note.empty() ? "" : " (" + c.note + ")"));
    if (pass && !verbose) continue;
    for (const auto& d : c.types) {
      std::string line = "    " + std::string(d.off_hand ? "L Hand " : "") + score::damage_label(d.key) + ": model " +
                         num(d.model) + ", engine " + num(d.engine);
      if (d.engine_seconds > 0) line += " (" + num(d.engine_seconds) + " s)";
      if (damage_off(d)) line += " OFF";
      out.push_back(std::move(line));
    }
  }
  return out;
}

}  // namespace gdc::calibrate
