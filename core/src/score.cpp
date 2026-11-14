#include "gdc/score.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <numeric>
#include <set>
#include <tuple>

#include "gdc/attribute_names.h"
#include "gdc/combat_formulas.h"
#include "gdc/db_fields.h"
#include "gdc/enemy.h"
#include "gdc/sets.h"
#include "gdc/stat_model.h"
#include "gdc/utility.h"

// Presets come from a string; no exceptions (the adapter must not unwind through the game), no formatters.
#define TOML_EXCEPTIONS 0
#define TOML_ENABLE_FORMATTERS 0
#include <tomlplusplus/toml.hpp>

namespace gdc::score {
namespace {

// ---------------------------------------------------------------- preset

constexpr std::pair<std::string_view, int> kThreatTypes[] = {
    {"physical", 2}, {"pierce", 4}, {"cold", 5},    {"fire", 6},   {"poison", 7},
    {"lightning", 8}, {"vitality", 9}, {"chaos", 10}, {"aether", 11}, {"bleeding", 15}};

// [thresholds] keys (docs/PRESETS.md; values in survival()).
constexpr std::string_view kThresholds[] = {"resist",      "resist_overcap", "physical_resist", "absorption",
                                            "armor",       "stun_resist",    "freeze_resist",   "slow_resist",
                                            "reflect_reduction", "life", "da"};

// Final damage types by preset name. Keys: re/CombatAttributeType.tsv (DamageAttributeAbs_* direct,
// DamageAttributeDur_* duration); labels: the game's terms (ERRATA §0: DamagePoison "Acid",
// DamageDurationPoison "Poison", DamageLife "Vitality").
struct DamageName {
  std::string_view key, label;
  int type;
  bool dot;
};
constexpr DamageName kDamageNames[] = {
    {"physical", "Physical", 2, false},   {"internal_trauma", "Internal Trauma", 2, true},
    {"pierce", "Pierce", 4, false},       {"cold", "Cold", 5, false},
    {"frostburn", "Frostburn", 5, true},  {"fire", "Fire", 6, false},
    {"burn", "Burn", 6, true},            {"acid", "Acid", 7, false},
    {"poison", "Poison", 7, true},        {"lightning", "Lightning", 8, false},
    {"electrocute", "Electrocute", 8, true}, {"vitality", "Vitality", 9, false},
    {"vitality_decay", "Vitality Decay", 9, true}, {"chaos", "Chaos", 10, false},
    {"aether", "Aether", 11, false},      {"bleeding", "Bleeding", 15, true}};

struct Parser {
  std::string& error;

  bool fail(const toml::node& n, const std::string& what) {
    error = "line " + std::to_string(n.source().begin.line) + ": " + what;
    return false;
  }

  bool number(const toml::node& n, std::string_view key, double lo, double hi, double& out) {
    const auto v = n.value<double>();
    if (!v || !n.is_number()) return fail(n, std::string(key) + " must be a number");
    if (!(*v >= lo && *v <= hi))
      return fail(n, std::string(key) + " must be in [" + std::to_string(lo) + ", " + std::to_string(hi) + "]");
    out = *v;
    return true;
  }

  // Calls each(key, node) for every key of table `name`; unknown keys are rejected by `each`.
  template <class F>
  bool table(const toml::table& root, std::string_view name, F each) {
    const toml::node* n = root.get(name);
    if (!n) return true;
    const toml::table* t = n->as_table();
    if (!t) return fail(*n, std::string(name) + " must be a table");
    for (const auto& [k, v] : *t)
      if (!each(k.str(), v)) return false;
    return true;
  }

  // An array of non-empty strings (record paths).
  bool strings(const toml::node& n, std::string_view key, std::vector<std::string>& out) {
    const toml::array* a = n.as_array();
    if (!a) return fail(n, std::string(key) + " must be an array of record paths");
    out.clear();
    for (const auto& e : *a) {
      const auto s = e.value<std::string>();
      if (!s || s->empty()) return fail(e, std::string(key) + ": each entry must be a record path");
      out.push_back(*s);
    }
    return true;
  }

  bool unknown(const toml::node& n, std::string_view table, std::string_view key) {
    return fail(n, "unknown key " + std::string(table) + "." + std::string(key));
  }
};

}  // namespace

namespace {

// Conversion types by name (conversionInType values, CombatAttributeType); 0 = "*".
std::optional<int> conversion_type(std::string_view name) {
  if (name == "*") return 0;
  if (name == "elemental") return 58;
  for (const auto& [n, t] : kThreatTypes)
    if (n == name && t != 15) return t;  // bleeding is not a conversion type (ERRATA §1 enum)
  return std::nullopt;
}

// conversion_warn = ["poison->*", "vitality->chaos"].
bool conversion_pairs(Parser& ps, const toml::node& n, std::string_view key, std::vector<std::pair<int, int>>& out) {
  const toml::array* a = n.as_array();
  if (!a) return ps.fail(n, std::string(key) + " must be an array of \"from->to\" strings");
  out.clear();
  for (const auto& e : *a) {
    const auto s = e.value<std::string>();
    const auto arrow = s ? s->find("->") : std::string::npos;
    const auto from = arrow != std::string::npos ? conversion_type(std::string_view(*s).substr(0, arrow)) : std::nullopt;
    const auto to = arrow != std::string::npos ? conversion_type(std::string_view(*s).substr(arrow + 2)) : std::nullopt;
    if (!from || !to)
      return ps.fail(e, std::string(key) + ": \"from->to\" with physical, pierce, cold, fire, poison, lightning, "
                                           "vitality, chaos, aether, elemental or *");
    out.emplace_back(*from, *to);
  }
  return true;
}

// [score], [damage], [survival], [other] of `root` (the file, or one [[stage]]) onto `p`. `where` prefixes
// key names in errors ("" or "stage.").
bool settings_tables(Parser& ps, const toml::table& root, std::string_view where, Settings& p) {
  const std::string w(where);
  bool ok = ps.table(root, "score", [&](std::string_view k, const toml::node& v) {
    if (k == "damage") return ps.number(v, k, 0, 10, p.damage_weight);
    if (k == "survival") return ps.number(v, k, 0, 10, p.survival_weight);
    if (k == "other") return ps.number(v, k, 0, 10, p.other_weight);
    if (k == "negligible_percent") return ps.number(v, k, 0, 100, p.negligible_percent);
    if (k == "threshold_below") return ps.number(v, k, 0, 100, p.threshold_below);
    if (k == "threshold_above") return ps.number(v, k, 0, 100, p.threshold_above);
    if (k == "threshold_width") return ps.number(v, k, 0.001, 1, p.threshold_width);
    return ps.unknown(v, w + "score", k);
  });
  ok = ok && ps.table(root, "damage", [&](std::string_view k, const toml::node& v) {
    if (k == "skills") return ps.strings(v, k, p.skills);
    if (k == "skill_weights") {
      const auto s = v.value<std::string>();
      if (!s || (*s != "equal" && *s != "points")) return ps.fail(v, "skill_weights must be \"equal\" or \"points\"");
      p.weigh_by_points = *s == "points";
      return true;
    }
    if (k == "auto_skills") {
      double n = 0;
      if (!ps.number(v, k, 1, 10, n)) return false;
      if (n != std::floor(n)) return ps.fail(v, "auto_skills must be a whole number");
      p.auto_skills = static_cast<int>(n);
      return true;
    }
    if (k == "enemy_da_per_level") return ps.number(v, k, 0, 1000, p.enemy_da_per_level);
    if (k == "filler_weight") return ps.number(v, k, 0, 1, p.filler_weight);
    if (k == "pet_attack_rate") return ps.number(v, k, 0, 100, p.pet_attack_rate);
    if (k == "conversion_warn") return conversion_pairs(ps, v, k, p.conversion_warn);
    return ps.unknown(v, w + "damage", k);
  });
  ok = ok && ps.table(root, "survival", [&](std::string_view k, const toml::node& v) {
    if (k == "enemy_oa_per_level") return ps.number(v, k, 0, 1000, p.enemy_oa_per_level);
    if (k == "enemy_hit_per_level") return ps.number(v, k, 0, 100000, p.enemy_hit_per_level);
    if (k == "melee_share") return ps.number(v, k, 0, 1, p.melee_share);
    if (k == "damage_reduction_uptime") return ps.number(v, k, 0, 1, p.damage_reduction_uptime);
    if (k == "resist_reduction") return ps.number(v, k, -100, 200, p.resist_reduction);
    if (k == "incoming_hits_per_sec") return ps.number(v, k, 0, 100, p.incoming_hits_per_sec);
    if (k == "threat") {
      const toml::table* t = v.as_table();
      if (!t) return ps.fail(v, "threat must be a table of shares");
      p.threat.clear();
      for (const auto& [tk, tv] : *t) {
        const auto at = std::find_if(std::begin(kThreatTypes), std::end(kThreatTypes),
                                     [&](const auto& e) { return e.first == tk.str(); });
        if (at == std::end(kThreatTypes)) return ps.unknown(tv, w + "survival.threat", tk.str());
        double share = 0;
        if (!ps.number(tv, tk.str(), 0, 1000, share)) return false;
        if (share > 0) p.threat[at->second] = share;
      }
      if (p.threat.empty()) return ps.fail(v, "threat needs at least one share > 0");
      return true;
    }
    return ps.unknown(v, w + "survival", k);
  });
  ok = ok && ps.table(root, "other", [&](std::string_view k, const toml::node& v) {
    if (!utility::stat(k)) return ps.unknown(v, w + "other", k);
    Weight& out = p.other[std::string(k)];
    // A number sets the weight and keeps the key's condition; { weight = x, when = "..." | [...] } sets both.
    if (v.is_number()) return ps.number(v, k, -10, 10, out.weight);
    const toml::table* t = v.as_table();
    if (!t) return ps.fail(v, std::string(k) + " must be a number or { weight = x, when = \"...\" }");
    out.when.clear();
    for (const auto& [tk, tv] : *t) {
      if (tk.str() == "weight") {
        if (!ps.number(tv, k, -10, 10, out.weight)) return false;
      } else if (tk.str() == "when") {
        std::vector<std::string> conds;
        if (const auto one = tv.value<std::string>()) conds.push_back(*one);
        else if (!ps.strings(tv, "when", conds)) return false;
        for (const auto& c : conds)
          if (!utility::valid_when(c))
            return ps.fail(tv, std::string(k) + ".when: \"" + c + "\" (shield, offhand, pets, weapon:<Class>, stage:<name>; ! negates)");
        out.when = std::move(conds);
      } else {
        return ps.unknown(tv, w + "other." + std::string(k), tk.str());
      }
    }
    if (!t->contains("weight")) return ps.fail(v, std::string(k) + " needs weight");
    return true;
  });
  ok = ok && ps.table(root, "enemy", [&](std::string_view k, const toml::node& v) {
    const auto at = std::find_if(std::begin(kThreatTypes), std::end(kThreatTypes), [&](const auto& e) { return e.first == k; });
    if (at == std::end(kThreatTypes)) return ps.unknown(v, w + "enemy", k);
    return ps.number(v, k, -100, 100, p.enemy_resist[at->second]);
  });
  ok = ok && ps.table(root, "thresholds", [&](std::string_view k, const toml::node& v) {
    if (std::find(std::begin(kThresholds), std::end(kThresholds), k) == std::end(kThresholds))
      return ps.unknown(v, w + "thresholds", k);
    double t = 0;
    if (!ps.number(v, k, 0, 1e6, t)) return false;
    if (t > 0) p.thresholds[std::string(k)] = t;
    else p.thresholds.erase(std::string(k));  // 0: off (a stage can drop one)
    return true;
  });
  ok = ok && ps.table(root, "uptime", [&](std::string_view k, const toml::node& v) {
    if (!k.starts_with("records/")) return ps.fail(v, "uptime: keys are effect record paths (\"records/...\")");
    return ps.number(v, k, 0, 1, p.uptime[std::string(k)]);
  });
  return ok;
}

// An array of damage type names (kDamageNames) -> keys.
bool damage_keys(Parser& ps, const toml::node& n, std::string_view key, std::vector<DamageKey>& out) {
  const toml::array* a = n.as_array();
  if (!a) return ps.fail(n, std::string(key) + " must be an array of damage types");
  out.clear();
  for (const auto& e : *a) {
    const auto s = e.value<std::string>();
    const auto k = s ? damage_key(*s) : std::nullopt;
    if (!k) return ps.fail(e, std::string(key) + ": unknown damage type (acid, poison, vitality, ...)");
    out.push_back(*k);
  }
  return true;
}

bool is_settings_table(std::string_view k) {
  return k == "score" || k == "damage" || k == "survival" || k == "other" || k == "enemy" || k == "uptime" ||
         k == "thresholds";
}

bool level(Parser& ps, const toml::node& v, std::string_view k, std::uint32_t& out) {
  double n = 0;
  if (!ps.number(v, k, 1, 1000, n)) return false;
  if (n != std::floor(n)) return ps.fail(v, std::string(k) + " must be a whole number");
  out = static_cast<std::uint32_t>(n);
  return true;
}

bool stage_block(Parser& ps, const toml::node& n, const Settings& base, Stage& st) {
  const toml::table* t = n.as_table();
  if (!t) return ps.fail(n, "each [[stage]] must be a table");
  st.settings = base;
  for (const auto& [k, v] : *t) {
    const std::string_view key = k.str();
    bool ok = true;
    if (key == "name") {
      const auto s = v.value<std::string>();
      ok = s && !s->empty() ? (st.name = *s, true) : ps.fail(v, "stage.name must be a non-empty string");
    } else if (key == "min_level") ok = level(ps, v, key, st.min_level);
    else if (key == "max_level") ok = level(ps, v, key, st.max_level);
    else if (key == "requires") ok = ps.strings(v, key, st.required);
    else if (!is_settings_table(key)) ok = ps.unknown(v, "stage", key);
    if (!ok) return false;
  }
  if (st.name.empty()) return ps.fail(n, "[[stage]] needs a name");
  if (st.min_level > st.max_level) return ps.fail(n, "stage " + st.name + ": min_level > max_level");
  st.settings.stage = st.name;
  return settings_tables(ps, *t, "stage.", st.settings);
}

}  // namespace

std::map<std::string, Weight> default_other() {
  std::map<std::string, Weight> out;
  for (const auto& st : utility::stats()) {
    Weight& w = out[std::string(st.key)];
    w.weight = st.weight;
    if (!st.when.empty()) w.when.emplace_back(st.when);
  }
  return out;
}

std::optional<DamageKey> damage_key(std::string_view name) {
  for (const auto& d : kDamageNames)
    if (d.key == name) return DamageKey{d.type, d.dot};
  return std::nullopt;
}

std::string damage_label(DamageKey k) {
  for (const auto& d : kDamageNames)
    if (d.type == k.first && d.dot == k.second) return std::string(d.label);
  return "type " + std::to_string(k.first) + (k.second ? " dot" : "");
}

std::optional<Preset> parse(std::string_view text, std::string& error) {
  toml::parse_result r = toml::parse(text);
  if (!r) {
    error = "line " + std::to_string(r.error().source().begin.line) + ": " + std::string(r.error().description());
    return std::nullopt;
  }
  const toml::table& root = r.table();
  Preset p;
  Parser ps{error};
  for (const auto& [k, v] : root)
    if (!is_settings_table(k.str()) && k.str() != "preset" && k.str() != "match" && k.str() != "stage") {
      ps.fail(v, "unknown table " + std::string(k.str()));
      return std::nullopt;
    }
  bool ok = settings_tables(ps, root, "", p);
  ok = ok && ps.table(root, "preset", [&](std::string_view k, const toml::node& v) {
    if (k == "name") {
      const auto s = v.value<std::string>();
      return s ? (p.name = *s, true) : ps.fail(v, "preset.name must be a string");
    }
    if (k == "auto") {
      const auto b = v.value<bool>();
      return b && v.is_boolean() ? (p.auto_select = *b, true) : ps.fail(v, "preset.auto must be true or false");
    }
    if (k == "target_sets") return ps.strings(v, k, p.target_sets);
    return ps.unknown(v, "preset", k);
  });
  ok = ok && ps.table(root, "match", [&](std::string_view k, const toml::node& v) {
    if (k == "masteries") return ps.strings(v, k, p.masteries);
    if (k == "damage") return damage_keys(ps, v, k, p.match_damage);
    if (k == "min_share") return ps.number(v, k, 0, 1, p.match_share);
    if (k == "character") {
      const auto c = v.value<std::string>();
      return c ? (p.match_character = *c, true) : ps.fail(v, "match.character must be a string");
    }
    if (k == "archetype") {
      const auto a = v.value<std::string>();
      static constexpr std::string_view kNames[] = {"pet", "retaliation", "melee", "ranged", "caster"};
      if (!a || std::find(std::begin(kNames), std::end(kNames), *a) == std::end(kNames))
        return ps.fail(v, "match.archetype must be one of pet, retaliation, melee, ranged, caster");
      return p.match_archetype = *a, true;
    }
    return ps.unknown(v, "match", k);
  });
  if (ok)
    if (const toml::node* n = root.get("stage")) {
      const toml::array* a = n->as_array();
      if (!a) ok = ps.fail(*n, "stage must be [[stage]] blocks");
      else
        for (const auto& e : *a) {
          Stage st;
          if (!(ok = stage_block(ps, e, p, st))) break;
          p.stages.push_back(std::move(st));
        }
    }
  if (!ok) return std::nullopt;
  return p;
}

namespace {

const snap::Skill* learned(const snap::Snapshot& s, const std::string& record) {
  for (const auto& sk : s.skills)
    if (sk.record == record && sk.level > 0) return &sk;
  return nullptr;
}

}  // namespace

const Stage* stage(const Preset& p, const snap::Snapshot& s) {
  const Stage* out = nullptr;
  for (const auto& st : p.stages)
    if (s.level >= st.min_level && s.level <= st.max_level &&
        std::all_of(st.required.begin(), st.required.end(), [&](const std::string& r) { return learned(s, r); }))
      out = &st;
  return out;
}

const Settings& settings(const Preset& p, const snap::Snapshot& s) {
  const Stage* st = stage(p, s);
  return st ? st->settings : p;
}

namespace {

// ---------------------------------------------------------------- shared

constexpr int kCunning = 2, kSpirit = 3, kTotalSpeed = 13, kAttackSpeed = 15, kCastSpeed = 16;
constexpr int kBasePhysical = 1, kPhysical = 2, kBleeding = 15, kElemental = 58, kCritDamage = 59;
constexpr const char* kDefaultAttack = "records/skills/default/defaultweaponattack.dbr";  // weaponDamagePct 100
constexpr int kDamageTypes[] = {2, 4, 5, 6, 7, 8, 9, 10, 11, 15, 58};  // re/CombatAttributeType.tsv

bool damage_type(int t) { return std::find(std::begin(kDamageTypes), std::end(kDamageTypes), t) != std::end(kDamageTypes); }

double char_total(const snap::Snapshot& s, int type) {
  for (const auto& a : s.char_attributes)
    if (a.type == type) return a.total;
  return 0;
}

double char_modifier(const snap::Snapshot& s, int type) {
  for (const auto& a : s.char_attributes)
    if (a.type == type) return a.modifier;
  return 0;
}

const snap::CombatTotals* find(const std::vector<snap::CombatTotals>& v, int type) {
  for (const auto& r : v)
    if (r.type == type) return &r;
  return nullptr;
}

const snap::Skill* skill_by_record(const snap::Snapshot& s, const std::string& record) {
  for (const auto& k : s.skills)
    if (k.record == record) return &k;
  return nullptr;
}

// Value of a per-level field at `level` (1-based); past the end of the array its last value; 0 at level 0.
double at_level(const arz::Record& r, std::string_view key, std::uint32_t level) {
  const auto it = r.fields.find(key);
  if (it == r.fields.end() || it->second.numbers.empty() || level == 0) return 0;
  const auto& n = it->second.numbers;
  return n[std::min<std::size_t>(level, n.size()) - 1];
}

// The enemy the character is scored against: the preset's per-level numbers, else the monster table.
double enemy_da(const snap::Snapshot& s, const Settings& p) {
  return p.enemy_da_per_level > 0 ? p.enemy_da_per_level * s.level : enemy::at(s.level).da;
}
double enemy_oa(const snap::Snapshot& s, const Settings& p) {
  return p.enemy_oa_per_level > 0 ? p.enemy_oa_per_level * s.level : enemy::at(s.level).oa;
}
// One incoming hit (block is measured against it) and its physical part (armor).
double enemy_hit(const snap::Snapshot& s, const Settings& p, bool physical) {
  if (p.enemy_hit_per_level > 0) return p.enemy_hit_per_level * s.level;
  const enemy::Enemy e = enemy::at(s.level);
  return std::max(1.0, physical ? e.hit_physical : e.hit);
}

// The character's attack against the enemy model (formulas::expected_hit).
formulas::Hit attack(const snap::Snapshot& s, const Settings& p, double crit_bonus, double* pth_out = nullptr) {
  const double pth = formulas::probability_to_hit(formulas::offensive_ability(s), enemy_da(s, p));
  if (pth_out) *pth_out = pth;
  return formulas::expected_hit(pth, s.pth, crit_bonus);
}

std::string fmt(double v, int decimals = 0) {
  char buf[48];
  std::snprintf(buf, sizeof buf, "%.*f", decimals, v);
  return buf;
}

// Three significant digits, no trailing zeros ("0", "0.2", "0.055").
std::string sig(double v) {
  char buf[48];
  std::snprintf(buf, sizeof buf, "%.3g", v);
  return buf;
}

std::string stem(const std::string& record) {
  const auto slash = record.rfind('/');
  std::string s = slash == std::string::npos ? record : record.substr(slash + 1);
  if (s.ends_with(".dbr")) s.resize(s.size() - 4);
  return s;
}

// ---------------------------------------------------------------- damage

using Key = std::pair<int, bool>;  // (CombatAttributeType, dot)
using Amounts = std::map<Key, double>;

// What one skill record (or modifier) adds at a level.
struct Raw {
  Amounts flat;            // average damage; dot: per second x duration
  Amounts pct;             // the skill's own % per (type, dot), 58 / 60 spread
  double weapon_pct = 0;   // weaponDamagePct
  double crit = 0;         // offensiveCritDamageModifier (%)
  double cooldown = 0;     // skillCooldownTime (modifiers add negative values)
  std::map<int, std::map<int, double>> conversion;  // from -> to -> %
};

int damage_type_named(std::string_view name) {
  for (int t : kDamageTypes)
    if (names::combat_attribute(t) == name) return t;
  return 0;
}

// Flat damage fields (fields::flat_damage) of `r` at `level` into `out`, scaled by `scale`.
void add_flat(const arz::Record& r, std::uint32_t level, double scale, Amounts& out) {
  for (const auto& f : fields::flat_damage()) {
    const double min = at_level(r, f.min, level);
    if (min == 0) continue;
    const double max = std::max(min, at_level(r, f.max, level));
    double v = (min + max) / 2;
    if (!f.duration.empty()) v *= at_level(r, f.duration, level);  // shortcut: per second x duration — unverified
    const double chance = at_level(r, f.chance, level);
    if (chance > 0 && chance < 100) v *= chance / 100;
    out[{f.type, !f.duration.empty()}] += v * scale;
  }
}

bool has_damage(const arz::Record& r, std::uint32_t level) {
  Amounts a;
  add_flat(r, level, 1, a);
  return !a.empty() || at_level(r, "weaponDamagePct", level) > 0;
}

void add_record(const arz::Record& r, std::uint32_t level, Raw& out) {
  add_flat(r, level, 1, out.flat);
  for (const auto& [field, t] : fields::damage_modifiers()) {
    const double v = at_level(r, field, level);
    if (v == 0) continue;
    if (t.type == 58 || t.type == 60)
      for (int x : fields::spread(t.type, t.modifier)) out.pct[{x, t.modifier}] += v;
    else out.pct[{t.type, t.modifier}] += v;
  }
  out.weapon_pct += at_level(r, "weaponDamagePct", level);
  out.crit += at_level(r, "offensiveCritDamageModifier", level);
  out.cooldown += at_level(r, "skillCooldownTime", level);
  for (const auto& [in, outk, pct] : {std::tuple{"conversionInType", "conversionOutType", "conversionPercentage"},
                                      std::tuple{"conversionInType2", "conversionOutType2", "conversionPercentage2"}}) {
    const int from = damage_type_named(r.string(in)), to = damage_type_named(r.string(outk));
    const double v = at_level(r, pct, level);
    if (from && to && v > 0) out.conversion[from][to] += v;
  }
}

// The skill's record (or, for a record without damage such as Skill_AttackBuffRadius, its buffSkillName
// record) at its current level, plus its learned modifiers (snap::Skill::modifier_ids) and item skill
// modifiers.
Raw skill_raw(const snap::Snapshot& s, const snap::Skill& sk, const arz::Database& db, std::vector<std::string>& notes) {
  Raw raw;
  const arz::Record* rec = db.record(sk.record);
  if (!rec) return raw;
  if (!has_damage(*rec, sk.current_level))
    if (const arz::Record* buff = db.record(rec->string("buffSkillName"))) rec = buff;
  add_record(*rec, sk.current_level, raw);
  for (const std::uint32_t id : sk.modifier_ids)
    for (const auto& m : s.skills)
      if (m.id == id && m.current_level > 0)
        if (const arz::Record* mr = db.record(m.record)) add_record(*mr, m.current_level, raw);
  for (const auto& m : s.item_skill_modifiers)
    if (m.skill_id == sk.id && !m.modifier_record.empty()) {
      if (const arz::Record* mr = db.record(m.modifier_record)) add_record(*mr, 1, raw);
      else notes.push_back("item skill modifier " + stem(m.modifier_record) + " not in the database");
    }
  return raw;
}

bool skill_on(const snap::Skill& sk, const arz::Record& r) {
  if (sk.passive && (!sk.passive->char_attributes.empty() || !sk.passive->defense.empty())) return true;
  return r.cls == "Skill_Passive";  // the capture holds char / defense collectors only
}

// Flat damage a weapon attack adds at weaponDamagePct: equipped items' offensive damage and the flat
// damage of passive and toggled skills that are on (community: flat damage applies to weapon attacks only).
Amounts weapon_flat(const snap::Snapshot& s, const arz::Database& db) {
  Amounts out;
  for (const auto& it : s.equipped) {
    if (!it.stats) continue;
    bool skip_base = false;
    if (it.location == 1) {  // L Hand
      const arz::Record* r = db.record(it.record);
      const std::string_view cls = r ? std::string_view(r->cls) : std::string_view{};
      // shortcut: dual wield not modeled (main hand only) — revisit if weapons are compared again.
      if (cls.starts_with("Weapon") && !cls.starts_with("WeaponArmor")) continue;
      skip_base = true;  // shield / off-hand base damage counts only for shield skills
    }
    Amounts mine;
    for (const auto& d : it.stats->offensive_damage) {
      if (d.type == kBasePhysical && skip_base) continue;
      const int t = d.type == kBasePhysical ? kPhysical : d.type;
      if (!damage_type(t)) continue;  // debuffs (28, 32, ...): not scored
      // shortcut: item DoT min/max taken as per second, x duration — unverified.
      mine[{t, d.dot}] += (d.min + d.max) / 2.0 * (d.dot ? d.duration : 1.0f);
    }
    // A shield's elemental base damage (offensiveBase<Type>Min / Max, DamageAttributeAbsBase_*: 176 of 356
    // WeaponArmor_Shield records in 1.3.1.1) shares its type with flat damage in the item's getter output:
    // taken back out by the base record's values.
    if (skip_base)
      if (const arz::Record* r = db.record(it.record))
        for (const auto& f : fields::flat_damage()) {
          if (!f.min.starts_with("offensiveBase") || !f.duration.empty()) continue;
          const double lo = r->number(f.min), hi = std::max(lo, r->number(f.max));
          if (lo == 0) continue;
          double& v = mine[{f.type, false}];
          v = std::max(0.0, v - (lo + hi) / 2);
        }
    for (const auto& [k, v] : mine) out[k] += v;
  }
  for (const auto& sk : s.skills) {
    if (sk.level == 0 || sk.current_level == 0 || sk.devotion_max_level != 0) continue;
    const arz::Record* r = db.record(sk.record);
    if (!r || (r->cls != "Skill_Passive" && r->cls != "Skill_BuffSelfToggled") || !skill_on(sk, *r)) continue;
    add_flat(*r, sk.current_level, 1, out);
  }
  // Item-granted passives that are on (snapshot.h `item_skills`): captured stats, or no weapon flags (their
  // collectors then always run).
  for (const auto& sk : s.item_skills) {
    if (sk.current_level == 0) continue;
    const arz::Record* r = db.record(sk.record);
    if (!r || r->cls != "Skill_Passive") continue;
    const bool on = (sk.passive && (!sk.passive->char_attributes.empty() || !sk.passive->defense.empty())) ||
                    !fields::weapon_gated(*r);
    if (on) add_flat(*r, sk.current_level, 1, out);
  }
  // Set bonuses' flat damage at the equipped piece count (Character::ContributeMiscOffensiveDamageAttributes
  // 0x6AA80, sets.h).
  for (const auto& [set, n] : sets::counts(s))
    if (const arz::Record* r = db.record(set))
      for (const auto& d : sets::bonus(*r, n).flat)
        out[{d.type, d.dot}] += (d.min + d.max) / 2.0 * (d.dot ? d.duration : 1.0f);
  return out;
}

// Raw amounts by original type -> by final type: the skill's own conversions first, then the character's
// on what is left; converted damage is not converted again (docs/MECHANICS.md §2). Sums over 100% are
// scaled to 100% (unverified, noted).
Amounts convert(const Amounts& in, const std::map<int, std::map<int, double>>& skill_pct,
                const std::vector<snap::Conversion>& global, std::vector<std::string>& notes) {
  std::map<int, std::map<int, double>> g;
  for (const auto& c : global) g[c.from][c.to] += c.percent * 100;
  Amounts out;
  for (const auto& [key, amount] : in) {
    double left = amount;
    const std::map<int, std::map<int, double>>* levels[] = {&skill_pct, &g};
    for (const auto* level : levels) {
      const auto it = level->find(key.first);
      if (it == level->end()) continue;
      double sum = 0;
      for (const auto& [to, pct] : it->second) sum += pct;
      if (sum <= 0) continue;
      const double scale = sum > 100 ? 100 / sum : 1;
      if (sum > 100.01) notes.push_back("conversion > 100% from " + std::string(names::combat_attribute(key.first)) + " (scaled)");
      const double moved = left * std::min(sum, 100.0) / 100;
      for (const auto& [to, pct] : it->second) out[{to, key.second}] += left * pct * scale / 100;
      left -= moved;
    }
    out[key] += left;
  }
  // Elemental: a third each to Cold, Fire, Lightning (unverified).
  for (const bool dot : {false, true}) {
    const auto it = out.find({kElemental, dot});
    if (it == out.end()) continue;
    const double third = it->second / 3;
    out.erase(it);
    for (int t : fields::spread(kElemental, false)) out[{t, dot}] += third;
  }
  return out;
}

// combatformulas.dbr: physical / pierce damage x (Cunning/245 + 1), physical duration (bleeding, internal
// trauma) x (Cunning/215 + 1), magical x (Spirit/215 + 1), magical duration x (Spirit/200 + 1).
// shortcut: applied as a separate multiplier — unverified whether the engine adds it to the % bonuses.
double attribute_factor(const snap::Snapshot& s, int type, bool dot) {
  const bool physical = type == kPhysical || type == 4 || type == kBleeding;
  if (physical) return 1 + char_total(s, kCunning) / (dot ? 215.0 : 245.0);
  return 1 + char_total(s, kSpirit) / (dot ? 200.0 : 215.0);
}

// "+x% <DoT> duration" (DamageAttributeDurMod_* LoadDurationModifier fields, re/CombatAttributeType.tsv) of the
// equipped items' records, in percent per type. A longer DoT deals its per-second damage for longer.
// shortcut: the snapshot has no total for it (CombatAttributeAccumulator::GetTotalDurationModifierType is not
// captured), so this is the record value (no affix roll) and skills' / devotions' parts are missing, which
// overstates the ratio when they are large — revisit when the adapter captures the total. Applied to the
// final type like the % bonuses (unverified).
double dot_duration(const snap::Snapshot& s, const arz::Database& db, int type) {
  static constexpr std::pair<int, std::string_view> kFields[] = {
      {2, "offensiveSlowPhysicalDurationModifier"},  {5, "offensiveSlowColdDurationModifier"},
      {6, "offensiveSlowFireDurationModifier"},      {7, "offensiveSlowPoisonDurationModifier"},
      {8, "offensiveSlowLightningDurationModifier"}, {9, "offensiveSlowLifeDurationModifier"},
      {15, "offensiveSlowBleedingDurationModifier"}};
  const auto f = std::find_if(std::begin(kFields), std::end(kFields), [&](const auto& e) { return e.first == type; });
  if (f == std::end(kFields)) return 0;
  double sum = 0;
  for (const auto& it : s.equipped)
    for (const auto& r : model::item_records(it))
      if (const arz::Record* rec = db.record(r)) sum += rec->number(f->second);
  return sum;
}

struct SkillDamage {
  double value = 0;
  std::string inputs;
  // For the main skills' damage per second (procs and pets, compare()): one use (after hit and crit), the
  // skill's cooldown, and uses per second without one (snapshot attack / cast speed, unverified as per second).
  double per_use = 0, cooldown = 0, rate = 0;
};

// `parts` (if given) gets the damage per final type (after conversion, %, attribute factor; before hit / crit
// / speed, which are the same for every type).
// `resist` (if given): damage multiplier per resist type from the enemy model and RR (effects.h).
SkillDamage skill_damage(const snap::Snapshot& s, const std::string& record, const Settings& p, const arz::Database& db,
                         std::vector<std::string>& notes, Amounts* parts = nullptr,
                         const std::map<int, double>* resist = nullptr) {
  SkillDamage out;
  const snap::Skill* sk = skill_by_record(s, record);
  if (!sk || sk->current_level == 0) return out;
  const Raw raw = skill_raw(s, *sk, db, notes);
  Amounts amounts = raw.flat;
  if (raw.weapon_pct > 0)
    for (const auto& [k, v] : weapon_flat(s, db)) amounts[k] += v * raw.weapon_pct / 100;
  for (auto& [k, v] : amounts) {  // skill-rank % applies to the original type (docs/MECHANICS.md §2.5)
    const auto it = raw.pct.find(k);
    if (it != raw.pct.end()) v *= 1 + it->second / 100;
  }
  double sum = 0;
  for (const auto& [k, v] : convert(amounts, raw.conversion, s.conversions, notes)) {
    const auto* m = find(s.damage_modifiers, k.first);
    const double pct = m ? (k.second ? m->dot_modifier : m->damage_modifier) : 0;
    double x = v * std::max(0.0, 1 + pct / 100) * attribute_factor(s, k.first, k.second);
    if (k.second) x *= 1 + dot_duration(s, db, k.first) / 100;
    if (resist)
      if (const auto r = resist->find(k.first); r != resist->end()) x *= std::max(0.0, r->second);
    sum += x;
    if (parts) (*parts)[k] += x;
  }
  const auto* crit_row = find(s.damage_modifiers, kCritDamage);
  const double crit_bonus = (crit_row ? crit_row->damage_modifier : 0) + raw.crit / 100;
  double pth = 0;
  const formulas::Hit h = attack(s, p, crit_bonus, &pth);
  const bool weapon = raw.weapon_pct > 0;
  double speed = 1;
  if (raw.cooldown <= 0)
    speed = 1 + (char_modifier(s, weapon ? kAttackSpeed : kCastSpeed) + char_modifier(s, kTotalSpeed)) / 100;
  out.value = sum * h.multiplier * std::max(0.0, speed);
  out.per_use = sum * h.multiplier;
  out.cooldown = raw.cooldown;
  out.rate = weapon ? s.attack_speed : s.cast_speed;
  out.inputs = stem(record) + " L" + std::to_string(sk->current_level) + " dmg " + fmt(sum) + " pth " + fmt(pth) +
               " hit " + fmt(h.chance, 2) + " crit " + fmt(h.crit_chance, 2) + "+" + fmt(crit_bonus, 2) + " x" +
               fmt(h.multiplier, 3) +
               (raw.cooldown > 0 ? " cooldown" : " speed " + fmt(speed, 2)) + " = " + fmt(out.value);
  return out;
}

// ---------------------------------------------------------------- survivability, other

// Survivability and its parts (the contributions are differences of these between two sides).
struct Survival {
  double ehp = 0;
  std::string inputs;
  double life = 0;
  double hit = 1;        // expected damage factor of one enemy attack: miss, glancing, crit tiers (DA)
  double avoid = 0;      // dodge / deflect chance
  double block = 0;      // share of a hit the shield blocks
  double reduction = 0;  // enemy damage reduction debuff x its uptime
  std::map<int, double> taken;             // threat type -> its share x damage taken after resists (and armor)
  std::map<std::string, double> thresholds;  // [thresholds] name -> factor
};

// Sheet absorption; with no type-39 row (no absorption modifier anywhere) the engine's per-entry value while
// any armor exists (model::base_absorption, traced).
double absorption(const snap::Snapshot& s, const model::Sheet& sh, const arz::Database* db) {
  if (sh.absorption > 0) return sh.absorption;
  return sh.armor > 0 ? model::base_absorption(s, db) : 0;
}

constexpr int kBase[] = {4, 5, 6, 7, 8, 9, 10, 11, 15};  // the 9 base resists (Pierce .. Bleeding)
// resist_reduction lowers these only: physical and crowd-control resists keep their sheet value (the
// guide's overcap target is for the 9 base resists, docs/build-ref/priorities.md §2).
bool base_resist(int type) { return std::find(std::begin(kBase), std::end(kBase), type) != std::end(kBase); }

// One incoming attack through the engine's order of defenses (docs/MECHANICS.md §9, CombatManager::TakeAttack
// 0x11D950): dodge / deflect -> enemy OA vs DA (miss, glancing, crit tiers) -> shield block -> resists ->
// armor on what the physical resist left (CombatAttributeDamage_BasePhysical::ReduceDamage 0x1175F0 scales
// the damage by the resist at once and only collects armor; Execute 0x117260 applies the armor of the hit
// region through physicalDamageDefenseEquationDGP / DLEP). The enemy's own damage reduction debuff lowers the
// damage before any of it.
Survival survival(const snap::Snapshot& s, const Settings& p, const arz::Database* db) {
  const model::Sheet sh = model::sheet(s);
  Survival out;
  out.life = sh.life;
  const double pth = formulas::probability_to_hit(enemy_oa(s, p), sh.da);
  const formulas::Hit h = formulas::expected_hit(pth, s.pth, 0);  // shortcut: monster crit damage bonus not read
  out.hit = h.multiplier;
  out.avoid = utility::avoid(s, p.melee_share);
  out.block = utility::block(s, db, enemy_hit(s, p, false), p.incoming_hits_per_sec);
  out.reduction = p.damage_reduction_uptime * utility::damage_reduction(s);
  const double hp = enemy_hit(s, p, true);
  const double abs = std::min(100.0, absorption(s, sh, db)) / 100;
  const auto regions = model::region_armor(s);
  double shares = 0;
  for (const auto& [type, share] : p.threat) shares += share;
  double taken = 0;
  for (const auto& [type, share] : p.threat) {
    const auto x = std::find_if(sh.resists.begin(), sh.resists.end(), [&](const auto& r) { return r.type == type; });
    const double rr = base_resist(type) ? p.resist_reduction : 0;
    const double r = x != sh.resists.end() ? std::min(x->uncapped - rr, x->max)
                                           : std::min(-rr, static_cast<double>(s.defense_cap));
    double f = std::max(0.0, 1 - r / 100);
    if (type == kPhysical) {
      const double d = hp * f;  // after the resist
      double left = 0;
      for (const auto& [chance, armor] : regions) left += chance * (d - abs * std::min(d, armor));
      f = left / hp;
    }
    out.taken[type] = share * f / shares;
    taken += out.taken[type];
  }
  const double per_attack =
      std::max(1e-6, out.hit * (1 - out.avoid) * (1 - out.block) * (1 - out.reduction) * taken);
  out.ehp = sh.life / per_attack;
  out.inputs = "life " + fmt(sh.life) + " da " + fmt(sh.da) + " enemy pth " + fmt(pth) + " x" + fmt(out.hit, 3) +
               " avoid " + fmt(out.avoid, 3) + " block " + fmt(out.block, 3) + " reduced " + fmt(out.reduction, 3) +
               " armor " + fmt(sh.armor) + " abs " + fmt(abs * 100) + "% hit " + fmt(hp) + " taken " + fmt(taken, 3) +
               " ehp " + fmt(out.ehp);
  if (p.thresholds.empty()) return out;

  // Thresholds: Survivability x the curve of each (the 9 base resists: geometric mean of their factors).
  auto resist = [&](int type) -> const model::Resist* {
    const auto x = std::find_if(sh.resists.begin(), sh.resists.end(), [&](const auto& r) { return r.type == type; });
    return x != sh.resists.end() ? &*x : nullptr;
  };
  auto shown = [&](int type) {
    const model::Resist* x = resist(type);
    const double rr = base_resist(type) ? p.resist_reduction : 0;
    return x ? std::min(x->uncapped - rr, x->max) : -rr;
  };
  // Not cappable, not reprocessed by model::swap: defense x (1 + modifier%) (unverified: that this is the
  // sheet's value; type 16 "TotalSpeed" read as Slow Resistance, 57 Reflect as reflected damage reduction).
  auto plain = [&](int type) {
    for (const auto& r : s.defense)
      if (r.type == type) return r.defense * (1 + r.defense_modifier / 100.0);
    return 0.0;
  };
  double f_all = 1;
  std::string terms;
  for (const auto& [name, target] : p.thresholds) {
    double f = 1, value = 0;
    if (name == "resist" || name == "resist_overcap") {
      for (int t : kBase) {
        const model::Resist* x = resist(t);
        const double v = name == "resist" ? shown(t) : x ? x->uncapped - x->max : -s.defense_cap;
        value += v / std::size(kBase);
        f *= std::pow(threshold_factor(v, target, p), 1.0 / std::size(kBase));
      }
    } else {
      value = name == "physical_resist"     ? shown(kPhysical)
              : name == "absorption"        ? abs * 100
              : name == "armor"             ? sh.armor
              : name == "stun_resist"       ? shown(42)
              : name == "freeze_resist"     ? shown(45)
              : name == "slow_resist"       ? plain(16)
              : name == "reflect_reduction" ? plain(57)
              : name == "life"              ? sh.life
                                            : sh.da;
      f = threshold_factor(value, target, p);
    }
    out.thresholds[name] = f;
    f_all *= f;
    terms += " " + name + " " + fmt(value) + "/" + fmt(target) + " x" + fmt(f, 3);
  }
  out.ehp *= f_all;
  out.inputs += "; thresholds" + terms + " = x" + fmt(f_all, 3);
  return out;
}

struct Other {
  double value = 1;
  std::string inputs;
  std::map<std::string, std::pair<double, double>> terms;  // key or unlisted stat -> (value, weight in effect)
};

// 1 + sum of weight x value over the [other] keys whose conditions hold, + default x each unlisted stat.
Other other(const snap::Snapshot& s, const Settings& p, const arz::Database* db) {
  const utility::Context ctx = utility::context(s, db, p.stage);
  const utility::Values v = utility::values(s, db);
  auto weight = [&](const std::string& key) {
    const auto it = p.other.find(key);
    if (it == p.other.end()) return 0.0;
    for (const auto& w : it->second.when)
      if (!utility::holds(w, ctx)) return 0.0;
    return it->second.weight;
  };
  Other out;
  for (const auto& [key, x] : v.keys) out.terms[key] = {x, weight(key)};
  const double wd = weight("default");
  for (const auto& [name, x] : v.unlisted) out.terms["default: " + name] = {x, wd};
  for (const auto& [name, t] : out.terms) {
    out.value += t.first * t.second;
    if (t.first != 0) out.inputs += name + " " + sig(t.first) + "x" + sig(t.second) + " ";
  }
  out.value = std::max(1e-6, out.value);
  out.inputs += "= " + fmt(out.value, 3);
  return out;
}

}  // namespace

namespace {

// Trigger rates for procs (effects.h): the main skills' attack / cast speed (Character::GetAttackSpeed /
// GetSpellCastSpeed, read as uses per second — unverified) and crit chance, weighted like Damage.
effects::Rates rates(const snap::Snapshot& s, const std::vector<Skill>& skills, const Settings& p, const arz::Database& db) {
  effects::Rates r;
  r.hits_taken = p.incoming_hits_per_sec;
  r.pet_attacks = p.pet_attack_rate;
  std::vector<std::string> notes;
  for (const auto& k : skills) {
    const snap::Skill* sk = skill_by_record(s, k.record);
    if (!sk) continue;
    const bool weapon = skill_raw(s, *sk, db, notes).weapon_pct > 0;
    r.action += k.weight * (weapon ? s.attack_speed : s.cast_speed);
  }
  r.crit = attack(s, p, 0).crit_chance;
  return r;
}

std::vector<int> enemy_types(const Settings& p) {
  std::vector<int> t;
  for (const auto& [type, _] : p.enemy_resist) t.push_back(type);
  return t;
}

struct Side {
  effects::Collected effects;
  std::map<int, double> resist;  // damage multiplier per resist type
};

Side side(const snap::Snapshot& s, const std::vector<Skill>& skills, const Settings& p, const arz::Database& db) {
  Side out;
  out.effects = effects::collect(s, db, rates(s, skills, p, db), p.uptime, enemy_types(p));
  out.resist = effects::resist_multipliers(p.enemy_resist, out.effects.reductions);
  return out;
}

// Damage per second of the procs and pets in `x` (not learned skills: those are main skills or unscored).
// Procs: their record's flat damage (or its buffSkillName record's), the character's conversions, % per type,
// attribute factor, enemy resist, hit and crit — like a main skill. Pets: the attack record's flat damage x enemy
// resist only. shortcut: pet bonuses, the pet's own OA / crit and PetPlayerScaling are not modelled — revisit
// with a pet DPS test.
struct Extra {
  double per_sec = 0;
  std::string inputs;
  std::map<std::string, std::string> unknown;  // damaging effect record -> why its rate is unknown
};

Extra extra_damage(const snap::Snapshot& s, const Side& x, const Settings& p, const arz::Database& db,
                   std::vector<std::string>& notes) {
  Extra out;
  const auto* crit_row = find(s.damage_modifiers, kCritDamage);
  const double hit = attack(s, p, crit_row ? crit_row->damage_modifier : 0).multiplier;
  auto resist = [&](int type) {
    const auto r = x.resist.find(type);
    return r != x.resist.end() ? std::max(0.0, r->second) : 1.0;
  };
  for (const auto& e : x.effects.effects) {
    if (e.source == "learned") continue;
    const arz::Record* rec = db.record(e.record);
    if (!rec) continue;
    Amounts a;
    add_flat(*rec, std::max<std::uint32_t>(e.level, 1), 1, a);
    if (a.empty())
      if (const arz::Record* buff = db.record(rec->string("buffSkillName"))) add_flat(*buff, std::max<std::uint32_t>(e.level, 1), 1, a);
    if (a.empty()) continue;
    if (e.per_sec <= 0) {
      out.unknown.emplace(e.record, e.basis);
      continue;
    }
    double per_use = 0;
    if (e.pet) {
      for (const auto& [k, v] : a) per_use += v * resist(k.first);
    } else {
      for (const auto& [k, v] : convert(a, {}, s.conversions, notes)) {
        const auto* m = find(s.damage_modifiers, k.first);
        const double pct = m ? (k.second ? m->dot_modifier : m->damage_modifier) : 0;
        per_use += v * std::max(0.0, 1 + pct / 100) * attribute_factor(s, k.first, k.second) * resist(k.first) *
                   (k.second ? 1 + dot_duration(s, db, k.first) / 100 : 1.0);
      }
      per_use *= hit;
    }
    out.per_sec += e.per_sec * per_use;
    out.inputs += " " + stem(e.record) + (e.pet ? " (pet) " : " ") + fmt(e.per_sec, 2) + "/s x " + fmt(per_use);
  }
  return out;
}

std::string resist_inputs(const Side& x) {
  std::string out = "enemy x";
  for (const auto& [t, m] : x.resist) out += " " + std::string(names::combat_attribute(t)) + " " + fmt(m, 2);
  return out;
}

}  // namespace

std::vector<Skill> main_skills(const snap::Snapshot& s, const Settings& p, const arz::Database* db) {
  std::vector<Skill> out;
  if (!db) return out;
  if (!p.skills.empty()) {
    for (const auto& r : p.skills)
      if (const auto* sk = skill_by_record(s, r); sk && sk->current_level > 0)
        out.push_back({r, p.weigh_by_points ? std::max<double>(sk->level, 1) : 1.0});
  } else {
    std::vector<std::pair<std::uint32_t, std::string>> found;
    for (const auto& sk : s.skills) {
      if (sk.level == 0 || sk.current_level == 0 || sk.devotion_max_level != 0) continue;
      if (!sk.record.starts_with("records/skills/playerclass")) continue;
      const arz::Record* r = db->record(sk.record);
      if (!r || !r->cls.starts_with("Skill_Attack")) continue;
      const arz::Record* d = r;
      if (!has_damage(*d, sk.current_level)) d = db->record(r->string("buffSkillName"));
      if (!d || !has_damage(*d, sk.current_level)) continue;
      found.emplace_back(sk.level, sk.record);
    }
    std::sort(found.begin(), found.end(), [](const auto& a, const auto& b) { return a.first != b.first ? a.first > b.first : a.second < b.second; });
    if (found.size() > static_cast<std::size_t>(p.auto_skills)) found.resize(static_cast<std::size_t>(p.auto_skills));
    for (const auto& [points, r] : found) out.push_back({r, static_cast<double>(points)});
  }
  const double total = std::accumulate(out.begin(), out.end(), 0.0, [](double a, const Skill& k) { return a + k.weight; });
  for (auto& k : out) k.weight /= total;
  // Between cooldowns the character attacks with its weapon: the default attack fills in.
  const snap::Skill* filler = skill_by_record(s, kDefaultAttack);
  if (p.skills.empty() && !out.empty() && p.filler_weight > 0 && filler && filler->current_level > 0) {
    std::vector<std::string> notes;
    const bool all_cooldown = std::all_of(out.begin(), out.end(), [&](const Skill& k) {
      return skill_raw(s, *skill_by_record(s, k.record), *db, notes).cooldown > 0;
    });
    if (all_cooldown) {
      for (auto& k : out) k.weight *= 1 - p.filler_weight;
      out.push_back({kDefaultAttack, p.filler_weight});
    }
  }
  return out;
}

std::map<DamageKey, double> damage_profile(const snap::Snapshot& s, const arz::Database& db) {
  Settings auto_pick;
  auto_pick.filler_weight = 0;
  std::map<DamageKey, double> out;
  std::vector<std::string> notes;
  for (const auto& k : main_skills(s, auto_pick, &db)) {
    Amounts parts;
    skill_damage(s, k.record, auto_pick, db, notes, &parts);
    const double total = std::accumulate(parts.begin(), parts.end(), 0.0, [](double a, const auto& kv) { return a + kv.second; });
    if (total <= 0) continue;
    for (const auto& [key, v] : parts) out[key] += k.weight * v / total;
  }
  const double sum = std::accumulate(out.begin(), out.end(), 0.0, [](double a, const auto& kv) { return a + kv.second; });
  for (auto& [_, v] : out) v /= sum;
  return out;
}

namespace {

// Entry of a per-level string array at `level` (past the end: its last entry).
std::string string_at(const arz::Record& r, std::string_view key, std::uint32_t level) {
  const auto it = r.fields.find(key);
  if (it == r.fields.end() || it->second.strings.empty() || level == 0) return {};
  const auto& v = it->second.strings;
  return v[std::min<std::size_t>(level, v.size()) - 1];
}

bool has_retaliation(const arz::Record& r, std::uint32_t level) {
  for (const auto& [key, _] : r.fields)
    if (std::string_view(key).starts_with("retaliation") && at_level(r, key, level) != 0) return true;
  return false;
}

}  // namespace

bool tried_first(const Preset& a, const Preset& b) {
  if (a.match_character.empty() != b.match_character.empty()) return !a.match_character.empty();
  return a.masteries.size() < b.masteries.size();
}

Archetype archetype(const snap::Snapshot& s, const arz::Database& db) {
  enum Cat { kNone, kPet, kAttack, kWeapon, kRetaliation };
  auto category = [&](const snap::Skill& sk) {
    const arz::Record* r = db.record(sk.record);
    if (!r) return kNone;
    const std::uint32_t lv = sk.current_level;
    if (r->cls == "Skill_SpawnPet" || r->cls == "Skill_TargetedSpawnPet") {
      const arz::Record* pet = db.record(string_at(*r, "spawnObjects", lv));
      return pet && pet->cls == "Pet" ? kPet : kNone;
    }
    const arz::Record* buff = db.record(r->string("buffSkillName"));
    // Weapon pool skills and their pool attacks: every playerclass record of these classes has weaponDamagePct.
    if (r->cls.starts_with("Skill_WeaponPool_") || r->cls.starts_with("Skill_WPAttack_")) return kWeapon;
    if (r->cls.starts_with("Skill_Attack")) {
      const arz::Record* d = has_damage(*r, lv) ? r : (buff && has_damage(*buff, lv) ? buff : nullptr);
      if (d) return at_level(*d, "weaponDamagePct", lv) > 0 ? kWeapon : kAttack;
    }
    if (has_retaliation(*r, lv) || (buff && has_retaliation(*buff, lv))) return kRetaliation;
    return kNone;
  };
  std::map<std::uint32_t, const snap::Skill*> parent;  // modifier id -> learned parent
  for (const auto& sk : s.skills)
    if (sk.level > 0)
      for (const auto id : sk.modifier_ids) parent.emplace(id, &sk);
  double pet = 0, attack = 0, weapon = 0, retaliation = 0;
  for (const auto& sk : s.skills) {
    if (sk.level == 0 || sk.current_level == 0 || sk.devotion_max_level != 0) continue;
    if (!sk.record.starts_with("records/skills/playerclass")) continue;
    const auto it = parent.find(sk.id);
    const snap::Skill& root = it != parent.end() ? *it->second : sk;
    if (it == parent.end()) {
      const arz::Record* r = db.record(sk.record);
      if (!r || r->cls == "Skill_Mastery") continue;
    }
    switch (category(root)) {
      case kPet: pet += sk.level; break;
      case kWeapon: weapon += sk.level; attack += sk.level; break;
      case kAttack: attack += sk.level; break;
      case kRetaliation: retaliation += sk.level; break;
      case kNone: break;
    }
  }
  Archetype a;
  a.why = "pet " + fmt(pet) + ", retaliation " + fmt(retaliation) + ", attack " + fmt(attack) + " (weapon " +
          fmt(weapon) + ")";
  if (pet > 0 && pet >= attack) a.name = "pet";
  else if (retaliation > 0 && retaliation >= attack) a.name = "retaliation";
  else if (attack == 0) a.name = "";
  else if (weapon * 2 >= attack) a.name = utility::context(s, &db, "").weapon.starts_with("WeaponHunting_") ? "ranged" : "melee";
  else a.name = "caster";
  return a;
}

Match match(const Preset& p, const snap::Snapshot& s, const arz::Database& db, const Labels& labels) {
  Match m;
  if (p.masteries.empty()) {
    m.why = "no [match]";
    return m;
  }
  if (!p.match_character.empty() && p.match_character != s.character_name) {
    m.why = "for character " + p.match_character;
    return m;
  }
  std::string names;
  for (const auto& sk : s.skills) {
    if (sk.level == 0) continue;
    const arz::Record* r = db.record(sk.record);
    if (!r || r->cls != "Skill_Mastery") continue;
    if (std::find(p.masteries.begin(), p.masteries.end(), sk.record) == p.masteries.end()) {
      m.why = "mastery " + labels.skill(sk.record) + " not in [match]";
      return m;
    }
    names += (names.empty() ? "" : ", ") + labels.skill(sk.record);
  }
  if (names.empty()) {
    m.why = "no mastery";
    return m;
  }
  m.why = "masteries " + names;
  if (!p.match_archetype.empty()) {
    const Archetype a = archetype(s, db);
    if (a.name != p.match_archetype) {
      m.why += "; archetype " + (a.name.empty() ? std::string("none") : a.name) + ", not " + p.match_archetype +
               " (" + a.why + ")";
      return m;
    }
    m.why += "; " + a.name + " (" + a.why + ")";
  }
  if (p.match_damage.empty()) {
    m.ok = true;
    return m;
  }
  const auto profile = damage_profile(s, db);
  if (profile.empty()) {
    m.why += "; no main damage skill";
    return m;
  }
  double share = 0;
  std::string terms;
  for (const auto& k : p.match_damage) {
    const auto it = profile.find(k);
    const double v = it != profile.end() ? it->second : 0;
    share += v;
    terms += (terms.empty() ? "" : " + ") + damage_label(k) + " " + fmt(v * 100) + "%";
  }
  m.ok = share >= p.match_share;
  std::vector<std::pair<double, DamageKey>> top;
  for (const auto& [k, v] : profile) top.emplace_back(v, k);
  std::sort(top.rbegin(), top.rend());
  std::string all;
  for (const auto& [v, k] : top) all += (all.empty() ? "" : ", ") + damage_label(k) + " " + fmt(v * 100) + "%";
  m.why += "; " + terms + " = " + fmt(share * 100) + "% " + (m.ok ? ">= " : "< ") + fmt(p.match_share * 100) +
           "% of main skill damage (" + all + ")";
  return m;
}

double threshold_factor(double value, double target, const Settings& p) {
  if (target <= 0) return 1;
  const double u = value / target - 1, z = -u / p.threshold_width;
  const double softplus = z > 30 ? z : std::log1p(std::exp(z));  // ln(1 + e^z) without overflow
  return std::exp(p.threshold_above * u - (p.threshold_below - p.threshold_above) * p.threshold_width * softplus);
}

namespace {

// TOML basic string.
std::string toml_string(std::string_view v) {
  std::string out = "\"";
  for (const char c : v) {
    if (c == '"' || c == '\\') out += '\\', out += c;
    else if (static_cast<unsigned char>(c) < 0x20 || c == 0x7F) {
      char buf[8];
      std::snprintf(buf, sizeof buf, "\\u%04X", static_cast<unsigned>(static_cast<unsigned char>(c)));
      out += buf;
    } else out += c;
  }
  return out + "\"";
}

// Text for a # comment: control characters (a newline would end the comment) become spaces.
std::string comment(std::string v) {
  for (char& c : v)
    if (static_cast<unsigned char>(c) < 0x20 || c == 0x7F) c = ' ';
  return v;
}

// Shortest text that reads back as the same double.
std::string number(double v) {
  char buf[64];
  const auto r = std::to_chars(buf, buf + sizeof buf, v);
  return std::string(buf, r.ptr);
}

std::string_view threat_name(int type) {
  for (const auto& [n, t] : kThreatTypes)
    if (t == type) return n;
  return {};
}

std::string conversion_name(int type) {
  if (type == 0) return "*";
  if (type == 58) return "elemental";
  return std::string(threat_name(type));
}

}  // namespace

std::string freeze(const Preset& chosen, const snap::Snapshot& s, const arz::Database& db, const std::string& name,
                   const Labels& labels) {
  const Stage* st = stage(chosen, s);
  const Settings& from = st ? st->settings : chosen;
  const auto skills = main_skills(s, from, &db);
  std::string o;
  auto line = [&](std::string_view k, const std::string& v, std::string_view note = {}) {
    o += std::string(k) + " = " + v + (note.empty() ? "" : "  # " + comment(std::string(note))) + "\n";
  };
  o += "# Frozen by GD Compare for " + comment(s.character_name) + " (level " + std::to_string(s.level) + ") from " +
       comment(chosen.name.empty() ? "the generic preset" : chosen.name) + (st ? ", stage " + comment(st->name) : "") + ".\n";
  o += "# The settings in use at that moment, with the main skills written out. Auto uses this file for this\n";
  o += "# character only ([match] character); edit it freely, delete it to go back to Auto. Freezing again\n";
  o += "# overwrites it: rename a copy you want to keep (and change its [preset] name).\n\n";
  o += "[preset]\n";
  line("name", toml_string(name));
  o += "\n[match]\n";
  line("character", toml_string(s.character_name));
  o += "masteries = [\n";
  for (const auto& sk : s.skills)
    if (sk.level > 0)
      if (const arz::Record* r = db.record(sk.record); r && r->cls == "Skill_Mastery")
        o += "  " + toml_string(sk.record) + ",  # " + comment(labels.skill(sk.record)) + "\n";
  o += "]\n";
  if (!chosen.match_archetype.empty()) line("archetype", toml_string(chosen.match_archetype));
  if (!chosen.match_damage.empty()) {
    std::string d;
    for (const auto& k : chosen.match_damage)
      for (const auto& n : kDamageNames)
        if (n.type == k.first && n.dot == k.second) d += (d.empty() ? "" : ", ") + toml_string(n.key);
    line("damage", "[" + d + "]");
    line("min_share", number(chosen.match_share));
  }
  o += "\n[score]\n";
  line("damage", number(from.damage_weight));
  line("survival", number(from.survival_weight));
  line("other", number(from.other_weight));
  line("negligible_percent", number(from.negligible_percent));
  line("threshold_below", number(from.threshold_below));
  line("threshold_above", number(from.threshold_above));
  line("threshold_width", number(from.threshold_width));
  o += "\n[damage]\n";
  o += "skills = [\n";
  for (const auto& k : skills)
    o += "  " + toml_string(k.record) + ",  # " + comment(labels.skill(k.record)) + " (weight " + number(std::round(k.weight * 1000) / 1000) + ")\n";
  o += "]\n";
  line("skill_weights", from.skills.empty() || from.weigh_by_points ? "\"points\"" : "\"equal\"",
       from.skills.empty() ? "picked automatically when frozen: by points spent" : "");
  line("auto_skills", std::to_string(from.auto_skills));
  line("filler_weight", number(from.filler_weight));
  line("enemy_da_per_level", number(from.enemy_da_per_level));
  line("pet_attack_rate", number(from.pet_attack_rate));
  std::string cw;
  for (const auto& [a, b] : from.conversion_warn) cw += (cw.empty() ? "" : ", ") + toml_string(conversion_name(a) + "->" + conversion_name(b));
  line("conversion_warn", "[" + cw + "]");
  o += "\n[survival]\n";
  line("enemy_oa_per_level", number(from.enemy_oa_per_level));
  line("enemy_hit_per_level", number(from.enemy_hit_per_level));
  line("melee_share", number(from.melee_share));
  line("damage_reduction_uptime", number(from.damage_reduction_uptime));
  line("resist_reduction", number(from.resist_reduction));
  line("incoming_hits_per_sec", number(from.incoming_hits_per_sec));
  o += "\n[survival.threat]\n";
  for (const auto& [t, v] : from.threat) line(threat_name(t), number(v));
  o += "\n[other]\n";
  // A frozen file has no stages: stage conditions are resolved for the stage in use (true: dropped; false:
  // weight 0).
  utility::Context at_stage;
  at_stage.stage = from.stage;
  for (const auto& st : utility::stats()) {
    const auto it = from.other.find(std::string(st.key));
    Weight w = it != from.other.end() ? it->second : Weight{};
    for (const auto& c : w.when)
      if (c.find("stage:") != std::string::npos && !utility::holds(c, at_stage)) w = Weight{};
    std::erase_if(w.when, [](const std::string& c) { return c.find("stage:") != std::string::npos; });
    std::string when;
    for (const auto& c : w.when) when += (when.empty() ? "" : ", ") + toml_string(c);
    line(st.key, w.when.empty() ? number(w.weight) : "{ weight = " + number(w.weight) + ", when = [" + when + "] }",
         "per " + std::string(st.unit));
  }
  o += "\n[enemy]\n";
  for (const auto& [t, v] : from.enemy_resist) line(threat_name(t), number(v));
  o += "\n[thresholds]\n";
  for (const auto& [k, v] : from.thresholds) line(k, number(v));
  o += "\n[uptime]\n";
  for (const auto& [k, v] : from.uptime) line(toml_string(k), number(v));
  return o;
}

std::string conversion_label(int type) {
  if (type == 7) return "Acid/Poison";
  if (type == 58) return "Elemental";
  return damage_label({type, false});
}

std::vector<Conversion> conversion_warnings(const snap::Snapshot& before, const snap::Snapshot& after,
                                            const Settings& p, const std::vector<DamageKey>& targets,
                                            const arz::Database* db) {
  std::vector<Conversion> out;
  if (p.conversion_warn.empty()) return out;
  auto warned = [&](int from, int to) {
    for (const auto& t : targets)
      if (t.first == to) return false;
    return std::any_of(p.conversion_warn.begin(), p.conversion_warn.end(), [&](const auto& w) {
      return (w.first == 0 || w.first == from) && (w.second == 0 || w.second == to);
    });
  };
  std::map<std::pair<int, int>, double> was, now;
  for (const auto& c : before.conversions) was[{c.from, c.to}] += c.percent * 100;
  for (const auto& c : after.conversions) now[{c.from, c.to}] += c.percent * 100;
  for (const auto& [k, v] : now)
    if (const double d = v - was[k]; d > 0.01 && warned(k.first, k.second)) out.push_back({"", k.first, k.second, d});
  if (!db) return out;
  for (const auto& m : after.item_skill_modifiers) {
    const bool old = std::any_of(before.item_skill_modifiers.begin(), before.item_skill_modifiers.end(),
                                 [&](const snap::ItemSkillModifier& b) {
                                   return b.skill_id == m.skill_id && b.modifier_record == m.modifier_record;
                                 });
    const arz::Record* r = old ? nullptr : db->record(m.modifier_record);
    if (!r) continue;
    Raw raw;
    add_record(*r, 1, raw);
    for (const auto& [from, tos] : raw.conversion)
      for (const auto& [to, pct] : tos)
        if (warned(from, to)) out.push_back({m.skill_record, from, to, pct});
  }
  return out;
}

namespace {

// Splits a term's ln ratio over its parts in proportion to their linear changes and appends them, in points
// of the total (x the term's exponent x 100). Parts that did not move are dropped.
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
  if (type == 7) return "Acid/Poison Resist";
  if (type == 9) return "Vitality Resist";
  return std::string(names::combat_attribute(type)) + " Resist";
}

}  // namespace

Result compare(const snap::Snapshot& before, const snap::Snapshot& after, const std::vector<Skill>& skills,
               const Settings& p, const arz::Database* db) {
  Result out;
  // Damage: weighted sum of each skill's after / before.
  if (!db) out.notes.push_back("no database: damage not scored");
  else if (skills.empty()) out.notes.push_back("no main damage skill found: damage not scored");
  else {
    const Side sb = side(before, skills, p, *db), sa = side(after, skills, p, *db);
    // Effects at the fallback uptime on one side only: the swap adds or removes them unscored.
    auto defaults = [](const Side& x) {
      std::map<std::string, std::string> m;
      for (const auto& r : x.effects.reductions)
        if (r.origin == effects::Origin::Default) m.emplace(r.record, r.basis);
      return m;
    };
    const auto db_ = defaults(sb), da_ = defaults(sa);
    for (const auto* m : {&db_, &da_})
      for (const auto& [record, why] : *m)
        if (!(m == &db_ ? da_ : db_).contains(record)) out.unscored.push_back({record, why});
    auto changed = [](const Side& x, const Side& other, std::vector<effects::Reduction>& to) {
      for (const auto& r : x.effects.reductions)
        if (std::none_of(other.effects.reductions.begin(), other.effects.reductions.end(), [&](const effects::Reduction& o) {
              return o.record == r.record && o.category == r.category && o.amount == r.amount &&
                     std::abs(o.uptime - r.uptime) < 1e-6;
            }))
          to.push_back(r);
    };
    changed(sb, sa, out.rr_lost);
    changed(sa, sb, out.rr_gained);
    double ratio = 0;
    std::vector<Contribution> parts;
    // Main skills' damage per second before the swap: each cooldown skill once per cooldown, the others
    // sharing the remaining casts by weight at the snapshot's attack / cast speed (design choice, docs/MECHANICS.md §5).
    double on_cooldown = 0, spam = 0, spam_weight = 0;
    std::vector<std::string> notes_b, notes_a;
    for (const auto& k : skills) {
      const SkillDamage b = skill_damage(before, k.record, p, *db, notes_b, nullptr, &sb.resist);
      const SkillDamage a = skill_damage(after, k.record, p, *db, notes_a, nullptr, &sa.resist);
      out.before += b.inputs + " w " + fmt(k.weight, 2) + "; ";
      out.after += a.inputs + "; ";
      ratio += k.weight * (b.value > 0 ? a.value / b.value : 1);
      parts.push_back({"", k.record, k.weight * (b.value > 0 ? a.value / b.value - 1 : 0)});
      if (b.value <= 0) out.notes.push_back(stem(k.record) + ": no damage computed");
      if (b.cooldown > 0) on_cooldown += b.per_use / b.cooldown;
      else spam += k.weight * b.per_use * b.rate, spam_weight += k.weight;
    }
    // Procs and pets: main / s x ratio + theirs after, over main / s + theirs before.
    const Extra eb = extra_damage(before, sb, p, *db, notes_b), ea = extra_damage(after, sa, p, *db, notes_a);
    const double main = on_cooldown + (spam_weight > 0 ? spam / spam_weight : 0);
    if (eb.per_sec > 0 || ea.per_sec > 0) {
      if (main > 0) {
        out.before += "main " + fmt(main) + "/s, procs+pets " + fmt(eb.per_sec) + "/s:" + eb.inputs + "; ";
        out.after += "procs+pets " + fmt(ea.per_sec) + "/s:" + ea.inputs + "; ";
        const double with = (main * ratio + ea.per_sec) / (main + eb.per_sec);
        // In the mixed ratio the skills weigh main / (main + procs before); the procs and pets get the rest.
        for (auto& c : parts) c.points *= main / (main + eb.per_sec);
        parts.push_back({"procs and pets", "", (ea.per_sec - eb.per_sec) / (main + eb.per_sec)});
        ratio = with;
      } else {
        out.notes.push_back("main skills' damage per second unknown (no attack / cast speed): procs and pets not scored");
      }
    }
    for (const auto* m : {&eb.unknown, &ea.unknown})
      for (const auto& [record, why] : *m)
        if (!(m == &eb.unknown ? ea.unknown : eb.unknown).contains(record) &&
            std::none_of(out.unscored.begin(), out.unscored.end(), [&](const Unscored& u) { return u.record == record; }))
          out.unscored.push_back({record, why});
    out.ratio.damage = ratio;
    contribute(out.contributions, ratio, p.damage_weight, std::move(parts));
    out.before += resist_inputs(sb) + "; ";
    out.after += resist_inputs(sa) + "; ";
    for (auto& n : notes_a)
      if (std::find(out.notes.begin(), out.notes.end(), n) == out.notes.end()) out.notes.push_back(std::move(n));
  }
  const Survival sb = survival(before, p, db), sa = survival(after, p, db);
  out.ratio.survival = sb.ehp > 0 ? sa.ehp / sb.ehp : 1;
  {
    // Survivability is a product, so each factor has its own ln ratio; the sum over damage types is split.
    auto factor = [&](const char* label, double b, double a) {
      if (a > 0 && b > 0 && a != b) out.contributions.push_back({label, "", std::log(a / b) * p.survival_weight * 100});
    };
    factor("Life", sb.life, sa.life);
    factor("DA (enemy hits and crits)", sa.hit, sb.hit);
    factor("Dodge / deflect", 1 - sa.avoid, 1 - sb.avoid);
    factor("Block", 1 - sa.block, 1 - sb.block);
    factor("Enemy damage reduction", 1 - sa.reduction, 1 - sb.reduction);
    double tb = 0, ta = 0;
    std::vector<Contribution> types;
    for (const auto& [type, v] : sb.taken) {
      const double a = sa.taken.contains(type) ? sa.taken.at(type) : 0;
      tb += v, ta += a;
      types.push_back({resist_label(type), "", v - a});
    }
    if (ta > 0 && tb > 0) contribute(out.contributions, tb / ta, p.survival_weight, std::move(types));
    for (const auto& [name, f] : sb.thresholds)
      if (const auto it = sa.thresholds.find(name); it != sa.thresholds.end())
        factor(("target " + name).c_str(), f, it->second);
  }
  const Other ob = other(before, p, db), oa = other(after, p, db);
  out.ratio.other = ob.value > 0 ? oa.value / ob.value : 1;
  {
    std::set<std::string> names;
    for (const auto* o : {&ob, &oa})
      for (const auto& [k, _] : o->terms) names.insert(k);
    std::vector<Contribution> parts;
    for (const auto& k : names) {
      const auto b = ob.terms.contains(k) ? ob.terms.at(k) : std::pair{0.0, 0.0};
      const auto a = oa.terms.contains(k) ? oa.terms.at(k) : std::pair{0.0, 0.0};
      parts.push_back({k, "", a.first * a.second - b.first * b.second});
      if (std::abs(a.first * a.second - b.first * b.second) < 1e-9 && std::abs(a.first - b.first) < 1e-9) continue;
      const double w = a.second != 0 ? a.second : b.second;
      out.other_changes.push_back(k + " " + sig(b.first) + " -> " + sig(a.first) + (w == 0 ? " (weight 0)" : " (w " + sig(w) + ")"));
    }
    contribute(out.contributions, out.ratio.other, p.other_weight, std::move(parts));
  }
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
