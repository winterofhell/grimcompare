#include "gdc/score.h"

#include <algorithm>
#include <array>
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

// No exceptions: the adapter must not unwind through the game.
#define TOML_EXCEPTIONS 0
#define TOML_ENABLE_FORMATTERS 0
#include <tomlplusplus/toml.hpp>

namespace gdc::score {
namespace {

constexpr std::pair<std::string_view, int> kThreatTypes[] = {
    {"physical", 2}, {"pierce", 4}, {"cold", 5},    {"fire", 6},   {"poison", 7},
    {"lightning", 8}, {"vitality", 9}, {"chaos", 10}, {"aether", 11}, {"bleeding", 15}};

constexpr std::string_view kThresholds[] = {"resist",      "resist_overcap", "physical_resist", "absorption",
                                            "armor",       "stun_resist",    "freeze_resist",   "slow_resist",
                                            "reflect_reduction", "life", "da"};

// Keys: re/CombatAttributeType.tsv. In-game names differ from the DB (ERRATA §0): DamagePoison is "Acid",
// DamageDurationPoison "Poison", DamageLife "Vitality".
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

  bool strings(const toml::node& n, std::string_view key, std::vector<std::string>& out,
               std::string_view what = "record path") {
    const toml::array* a = n.as_array();
    if (!a) return fail(n, std::string(key) + " must be an array of " + std::string(what) + "s");
    out.clear();
    for (const auto& e : *a) {
      const auto s = e.value<std::string>();
      if (!s || s->empty()) return fail(e, std::string(key) + ": each entry must be a " + std::string(what));
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

// 0 = "*". DB "Poison" (type 7) is Acid in game (ERRATA §0; medal_d316_bloodypox conversionOutType Poison =
// "Vitality to Acid"), so "acid" and "poison" are the same conversion type.
std::optional<int> conversion_type(std::string_view name) {
  if (name == "*") return 0;
  if (name == "elemental") return 58;
  if (name == "acid") return 7;
  for (const auto& [n, t] : kThreatTypes)
    if (n == name && t != 15) return t;  // bleeding is not a conversion type (ERRATA §1)
  return std::nullopt;
}

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
      return ps.fail(e, std::string(key) + ": \"from->to\" with physical, pierce, cold, fire, acid (= poison), "
                                           "lightning, vitality, chaos, aether, elemental or *");
    out.emplace_back(*from, *to);
  }
  return true;
}

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
    if (k == "threshold_resist_weight") return ps.number(v, k, 0, 9, p.threshold_resist_weight);
    if (k == "recast_buffs") {
      const auto b = v.value<bool>();
      if (!b || !v.is_boolean()) return ps.fail(v, "recast_buffs must be true or false");
      p.recast_buffs = *b;
      return true;
    }
    if (k == "buff_mode") {
      const auto s = v.value<std::string>();
      if (!s || (*s != "average" && *s != "live")) return ps.fail(v, "buff_mode must be \"average\" or \"live\"");
      p.live_buffs = *s == "live";
      return true;
    }
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
    if (k == "kills_per_sec") return ps.number(v, k, 0, 100, p.kills_per_sec);
    if (k == "conversion_warn") return conversion_pairs(ps, v, k, p.conversion_warn);
    return ps.unknown(v, w + "damage", k);
  });
  ok = ok && ps.table(root, "survival", [&](std::string_view k, const toml::node& v) {
    if (k == "enemy_oa_per_level") return ps.number(v, k, 0, 1000, p.enemy_oa_per_level);
    if (k == "enemy_hit_per_level") return ps.number(v, k, 0, 100000, p.enemy_hit_per_level);
    if (k == "melee_share") return ps.number(v, k, 0, 1, p.melee_share);
    if (k == "damage_reduction_uptime") return ps.number(v, k, 0, 1, p.damage_reduction_uptime);
    if (k == "resist_reduction") {
      if (const auto s = v.value<std::string>(); s && *s == "auto") {
        p.resist_reduction.reset();
        return true;
      }
      if (v.is_string()) return ps.fail(v, "resist_reduction must be a number or \"auto\"");
      return ps.number(v, k, -100, 200, p.resist_reduction.emplace());
    }
    if (k == "resist_reduction_from") {
      const auto s = v.value<std::string>();
      const int d = !s ? -1 : *s == "normal" ? 0 : *s == "elite" ? 1 : *s == "ultimate" ? 2 : -1;
      if (d < 0) return ps.fail(v, "resist_reduction_from must be \"normal\", \"elite\" or \"ultimate\"");
      return p.resist_reduction_from = d, true;
    }
    if (k == "boss_share") return ps.number(v, k, 0, 1, p.boss_share);
    if (k == "difficulty_lookahead") {
      if (const auto b = v.value<bool>(); b && v.is_boolean()) return p.difficulty_lookahead = *b, true;
      return ps.fail(v, "difficulty_lookahead must be true or false");
    }
    if (k == "incoming_hits_per_sec") return ps.number(v, k, 0, 100, p.incoming_hits_per_sec);
    if (k == "low_health_per_sec") return ps.number(v, k, 0, 100, p.low_health_per_sec);
    if (k == "cc_share") return ps.number(v, k, 0, 1, p.cc_share);
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
    else p.thresholds.erase(std::string(k));  // 0 lets a stage drop a threshold
    return true;
  });
  ok = ok && ps.table(root, "uptime", [&](std::string_view k, const toml::node& v) {
    if (!k.starts_with("records/")) return ps.fail(v, "uptime: keys are effect record paths (\"records/...\")");
    return ps.number(v, k, 0, 1, p.uptime[std::string(k)]);
  });
  return ok;
}

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
    if (k == "characters") return ps.strings(v, k, p.match_characters, "character name");
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

constexpr int kCunning = 2, kSpirit = 3, kTotalSpeed = 13, kAttackSpeed = 15, kCastSpeed = 16;
constexpr int kBasePhysical = 1, kPhysical = 2, kBleeding = 15, kElemental = 58, kCritDamage = 59;
constexpr const char* kDefaultAttack = "records/skills/default/defaultweaponattack.dbr";  // weaponDamagePct 100
constexpr int kDamageTypes[] = {2, 4, 5, 6, 7, 8, 9, 10, 11, 15, 58};

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

// Past the end of the per-level array the last value holds.
double at_level(const arz::Record& r, std::string_view key, std::uint32_t level) {
  const auto it = r.fields.find(key);
  if (it == r.fields.end() || it->second.numbers.empty() || level == 0) return 0;
  const auto& n = it->second.numbers;
  return n[std::min<std::size_t>(level, n.size()) - 1];
}

enemy::Enemy enemy_of(const snap::Snapshot& s) { return enemy::at(s.level, enemy::difficulty(s)); }
double enemy_da(const snap::Snapshot& s, const Settings& p) {
  return p.enemy_da_per_level > 0 ? p.enemy_da_per_level * s.level : enemy_of(s).da;
}
double enemy_oa(const snap::Snapshot& s, const Settings& p) {
  return p.enemy_oa_per_level > 0 ? p.enemy_oa_per_level * s.level : enemy_of(s).oa;
}
double enemy_hit(const snap::Snapshot& s, const Settings& p, bool physical) {
  if (p.enemy_hit_per_level > 0) return p.enemy_hit_per_level * s.level;
  const enemy::Enemy e = enemy_of(s);
  return std::max(1.0, physical ? e.hit_physical : e.hit);
}

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

using Key = std::pair<int, bool>;
using Amounts = std::map<Key, double>;

struct Raw {
  Amounts flat;            // dot: per second x duration
  Amounts pct;             // 58 / 60 spread
  double weapon_pct = 0;
  double crit = 0;
  double cooldown = 0;     // modifiers add negative values
  double leech = 0;        // own ADCtH plus modifiers' (Bramblevine)
  std::map<int, std::map<int, double>> conversion;
};

int damage_type_named(std::string_view name) {
  for (int t : kDamageTypes)
    if (names::combat_attribute(t) == name) return t;
  return 0;
}

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
  out.leech += at_level(r, "offensiveLifeLeechMin", level);
  for (const auto& [in, outk, pct] : {std::tuple{"conversionInType", "conversionOutType", "conversionPercentage"},
                                      std::tuple{"conversionInType2", "conversionOutType2", "conversionPercentage2"}}) {
    const int from = damage_type_named(r.string(in)), to = damage_type_named(r.string(outk));
    const double v = at_level(r, pct, level);
    if (from && to && v > 0) out.conversion[from][to] += v;
  }
}

// Records without damage (Skill_AttackBuffRadius) carry it on their buffSkillName record.
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

bool weapon_class(std::string_view cls) { return cls.starts_with("Weapon") && !cls.starts_with("WeaponArmor"); }

// offensiveBase<Type>Min / Max is DamageAttributeAbsBase_*: elemental base damage that shares its type with
// flat damage in the item's getter output.
Amounts hand_base(const snap::Item& it, const arz::Database& db) {
  Amounts out;
  if (it.stats)
    for (const auto& d : it.stats->offensive_damage)
      if (d.type == kBasePhysical) out[{kPhysical, false}] += (d.min + d.max) / 2.0;
  if (const arz::Record* r = db.record(it.record))
    for (const auto& f : fields::flat_damage()) {
      if (!f.min.starts_with("offensiveBase") || !f.duration.empty()) continue;
      const double lo = r->number(f.min), hi = std::max(lo, r->number(f.max));
      if (lo > 0) out[{f.type, false}] += (lo + hi) / 2;
    }
  return out;
}

// Design choice: weapon base damage is not compared (the game's tooltip does that), so both sides use the
// mean of their R Hand bases. That keeps the base identical in both directions.
// shortcut: dual wield's second weapon base not modeled — revisit with a dual-wield weapon build.
Amounts shared_base(const snap::Snapshot& before, const snap::Snapshot& after, const arz::Database& db) {
  Amounts out;
  for (const auto* s : {&before, &after})
    for (const auto& it : s->equipped) {
      if (it.location != 0) continue;
      const arz::Record* r = db.record(it.record);
      if (!r || !weapon_class(r->cls)) continue;
      for (const auto& [k, v] : hand_base(it, db)) out[k] += v / 2;
    }
  return out;
}

// Flat damage applies to weapon attacks only (community).
Amounts weapon_flat(const snap::Snapshot& s, const arz::Database& db, const Amounts& base) {
  Amounts out = base;
  for (const auto& it : s.equipped) {
    if (!it.stats) continue;
    const bool hand = it.location == 0 || it.location == 1;
    Amounts mine;
    for (const auto& d : it.stats->offensive_damage) {
      if (d.type == kBasePhysical && hand) continue;
      const int t = d.type == kBasePhysical ? kPhysical : d.type;
      if (!damage_type(t)) continue;
      // shortcut: item DoT min/max taken as per second, x duration — unverified.
      mine[{t, d.dot}] += (d.min + d.max) / 2.0 * (d.dot ? d.duration : 1.0f);
    }
    if (hand)
      for (const auto& [k, v] : hand_base(it, db))
        if (k.first != kPhysical) {  // elemental base: out of the flat damage of its type
          double& x = mine[k];
          x = std::max(0.0, x - v);
        }
    for (const auto& [k, v] : mine) out[k] += v;
  }
  for (const auto& sk : s.skills) {
    if (sk.level == 0 || sk.current_level == 0 || sk.devotion_max_level != 0) continue;
    const arz::Record* r = db.record(sk.record);
    if (!r || (r->cls != "Skill_Passive" && r->cls != "Skill_BuffSelfToggled") || !skill_on(sk, *r)) continue;
    add_flat(*r, sk.current_level, 1, out);
  }
  for (const auto& sk : s.item_skills) {
    if (sk.current_level == 0) continue;
    const arz::Record* r = db.record(sk.record);
    if (!r || r->cls != "Skill_Passive") continue;
    const bool on = (sk.passive && (!sk.passive->char_attributes.empty() || !sk.passive->defense.empty())) ||
                    !fields::weapon_gated(*r);
    if (on) add_flat(*r, sk.current_level, 1, out);
  }
  // Character::ContributeMiscOffensiveDamageAttributes 0x6AA80.
  for (const auto& [set, n] : sets::counts(s))
    if (const arz::Record* r = db.record(set))
      for (const auto& d : sets::bonus(*r, n).flat)
        out[{d.type, d.dot}] += (d.min + d.max) / 2.0 * (d.dot ? d.duration : 1.0f);
  return out;
}

// Skill conversions first, then the character's on the rest; converted damage is not converted again
// (MECHANICS §2). Sums over 100% are scaled to 100% (unverified).
// CombatAttributeAccumulator::ConvertDamage 0x119ED0 moves DoT along with direct damage, except into Pierce,
// Chaos, Aether (no DoT form). Elemental -> X runs before the element's own conversions (community order,
// MECHANICS §2.4; engine order not traced).
Amounts convert(const Amounts& in, const std::map<int, std::map<int, double>>& skill_pct,
                const std::vector<snap::Conversion>& global, std::vector<std::string>& notes) {
  std::map<int, std::map<int, double>> g;
  for (const auto& c : global) g[c.from][c.to] += c.percent * 100;
  auto no_dot = [](int t) { return t == 4 || t == 10 || t == 11; };  // ConvertDamage 0x119FE0
  Amounts out;
  for (const auto& [key, amount] : in) {
    double left = amount;
    const bool element = key.first == 5 || key.first == 6 || key.first == 8;
    const std::map<int, std::map<int, double>>* levels[] = {&skill_pct, &g};
    for (const auto* level : levels)
      for (const int from : {element ? kElemental : 0, key.first}) {
        const auto it = from ? level->find(from) : level->end();
        if (it == level->end()) continue;
        double sum = 0;
        for (const auto& [to, pct] : it->second)
          if (!(key.second && no_dot(to))) sum += pct;
        if (sum <= 0) continue;
        const double scale = sum > 100 ? 100 / sum : 1;
        if (sum > 100.01) notes.push_back("conversion > 100% from " + std::string(names::combat_attribute(from)) + " (scaled)");
        const double moved = left * std::min(sum, 100.0) / 100;
        for (const auto& [to, pct] : it->second)
          if (!(key.second && no_dot(to))) out[{to, key.second}] += left * pct * scale / 100;
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

// combatformulas.dbr: physical / pierce + Cunning/245, physical DoT + Cunning/215, magical + Spirit/215,
// magical DoT + Spirit/200; damage = base x (1 + % / 100 + this). Checked in game (lvl16-wp-lhand-* fixtures):
// Poison Nova 21/s x (1 + 0.93 + 198.22 / 200) = 61 x 5 s = 305 shown (MECHANICS §5).
double attribute_bonus(const snap::Snapshot& s, int type, bool dot) {
  const bool physical = type == kPhysical || type == 4 || type == kBleeding;
  if (physical) return char_total(s, kCunning) / (dot ? 215.0 : 245.0);
  return char_total(s, kSpirit) / (dot ? 200.0 : 215.0);
}

// shortcut: record value of DamageAttributeDurMod_* only — no affix rolls, no skill / devotion parts, so large
// values overstate the ratio. Revisit when the adapter captures GetTotalDurationModifierType.
std::map<int, double> dot_duration(const snap::Snapshot& s, const arz::Database& db) {
  static constexpr std::pair<int, std::string_view> kFields[] = {
      {2, "offensiveSlowPhysicalDurationModifier"},  {5, "offensiveSlowColdDurationModifier"},
      {6, "offensiveSlowFireDurationModifier"},      {7, "offensiveSlowPoisonDurationModifier"},
      {8, "offensiveSlowLightningDurationModifier"}, {9, "offensiveSlowLifeDurationModifier"},
      {15, "offensiveSlowBleedingDurationModifier"}};
  std::map<int, double> out;
  for (const auto& it : s.equipped)
    for (const auto& r : model::item_records(it))
      if (const arz::Record* rec = db.record(r))
        for (const auto& [type, field] : kFields)
          if (const double v = rec->number(field); v != 0) out[type] += v;
  return out;
}

struct Gear {
  Amounts flat;
  std::map<int, double> dot;
  double dot_scale(int type) const {
    const auto it = dot.find(type);
    return it != dot.end() ? 1 + it->second / 100 : 1.0;
  }
};

Gear gear(const snap::Snapshot& s, const arz::Database& db, const Amounts& base) {
  return {weapon_flat(s, db, base), dot_duration(s, db)};
}

struct SkillDamage {
  double value = 0;
  std::string inputs;
  double per_use = 0, cooldown = 0, uses = 0, rate = 0;
};

// Design choice: cast whenever ready. Recharge x (1 - CDR) as in Skill::GetCooldownTime, capped by attack /
// cast speed read as uses per second (unverified; 0 = no cap).
struct CooldownUses {
  double per_sec = 0, cdr = 0;
  bool capped = false;
};
CooldownUses cooldown_uses(double cooldown, double cdr, double rate) {
  CooldownUses out{0, cdr};
  const double recharge = cooldown * (1 - cdr);
  if (recharge > 0) out.per_sec = 1 / recharge;
  if (rate > 0 && (recharge <= 0 || out.per_sec > rate)) out.per_sec = rate, out.capped = true;
  else if (recharge <= 0) out.per_sec = 1 / cooldown;
  return out;
}

// A captured hit animation (schema 10) means a SkillActivatedSpell / BuffSelf: Player::CalculateSkillDps picks the cast
// path by class (0x3E0BEE..0x3E0CFD), whatever weapon damage % its modifiers add (Vile Eye on Dreeg's Evil Eye).
bool has_anim(const snap::Skill& sk) { return sk.anim_frames > 0 && sk.anim_frame_rate > 0; }

// Uses per second at most. With a hit animation: cast speed x frame rate / frames, as CalculateSkillDps stores in
// SkillAttackInfo+0x2C. Without one: attack speed for weapon-damage skills, cast speed for the rest, read as uses per
// second (unverified).
double use_rate(const snap::Snapshot& s, const snap::Skill& sk, bool weapon) {
  if (has_anim(sk)) return static_cast<double>(s.cast_speed) * sk.anim_frame_rate / sk.anim_frames;
  return weapon ? s.attack_speed : s.cast_speed;
}

SkillDamage skill_damage(const snap::Snapshot& s, const snap::Skill& sk, const Raw& raw, const Gear& g, const Settings& p,
                         std::vector<std::string>& notes, Amounts* parts = nullptr,
                         const std::map<int, double>* resist = nullptr) {
  SkillDamage out;
  if (sk.current_level == 0) return out;
  Amounts amounts = raw.flat;
  if (raw.weapon_pct > 0)
    for (const auto& [k, v] : g.flat) amounts[k] += v * raw.weapon_pct / 100;
  for (auto& [k, v] : amounts) {  // skill % applies to the original type (MECHANICS §2.5)
    const auto it = raw.pct.find(k);
    if (it != raw.pct.end()) v *= 1 + it->second / 100;
  }
  double sum = 0;
  for (const auto& [k, v] : convert(amounts, raw.conversion, s.conversions, notes)) {
    const auto* m = find(s.damage_modifiers, k.first);
    const double pct = m ? (k.second ? m->dot_modifier : m->damage_modifier) : 0;
    double x = v * std::max(0.0, 1 + pct / 100 + attribute_bonus(s, k.first, k.second));
    if (k.second) x *= g.dot_scale(k.first);
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
  out.per_use = sum * h.multiplier;
  out.cooldown = raw.cooldown;
  out.rate = use_rate(s, sk, weapon);
  std::string pace;
  if (raw.cooldown > 0) {
    const CooldownUses u = cooldown_uses(raw.cooldown, utility::cooldown_reduction(s), out.rate);
    out.uses = u.per_sec;
    out.value = out.per_use * u.per_sec;
    pace = " cooldown " + fmt(raw.cooldown, 2) + " s x (1 - " + fmt(u.cdr * 100) + "%) " + fmt(u.per_sec, 2) + "/s" +
           (u.capped ? " (speed cap)" : "");
  } else {
    const bool attack_speed = weapon && !has_anim(sk);
    const double speed = 1 + (char_modifier(s, attack_speed ? kAttackSpeed : kCastSpeed) + char_modifier(s, kTotalSpeed)) / 100;
    out.value = out.per_use * std::max(0.0, speed);
    pace = " speed " + fmt(speed, 2);
  }
  if (has_anim(sk))
    pace += " (anim " + std::to_string(sk.anim_frames) + "f@" + std::to_string(sk.anim_frame_rate) + " -> " +
            fmt(out.rate, 2) + "/s)";
  out.inputs = stem(sk.record) + " L" + std::to_string(sk.current_level) + " dmg " + fmt(sum) + " pth " + fmt(pth) +
               " hit " + fmt(h.chance, 2) + " crit " + fmt(h.crit_chance, 2) + "+" + fmt(crit_bonus, 2) + " x" +
               fmt(h.multiplier, 3) + pace + " = " + fmt(out.value);
  return out;
}

struct Buff {
  std::string record;
  double uptime = 0;
  std::string basis;
  model::BuffEffect effect;
  double absorb_pct = 0, absorb_hit = 0;  // damageAbsorptionPercent / 100, damageAbsorption / one hit
  bool matters() const { return !effect.empty() || absorb_pct > 0 || absorb_hit > 0; }
};

struct Procs {
  double cc = 0;
  std::vector<Buff> buffs;
  double restored = 0;
  std::string inputs;
  std::map<std::string, std::string> unknown;
  std::set<std::string> scored;
};

struct Survival {
  double ehp = 0;
  std::string inputs;
  double life = 0;
  double hit = 1;
  double avoid = 0;
  double block = 0;
  double reduction = 0;
  double cc = 0;
  double procs = 0;
  std::map<int, double> taken;
  std::map<std::string, double> thresholds;
};

// No type-39 row: the engine's per-entry value while any armor exists (model::base_absorption).
double absorption(const snap::Snapshot& s, const model::Sheet& sh, const arz::Database* db) {
  if (sh.absorption > 0) return sh.absorption;
  return sh.armor > 0 ? model::base_absorption(s, db) : 0;
}

constexpr int kBase[] = {4, 5, 6, 7, 8, 9, 10, 11, 15};
// Physical and CC resists keep their sheet value; the overcap target is for the 9 base resists
// (docs/build-ref/priorities.md §2).
bool base_resist(int type) { return std::find(std::begin(kBase), std::end(kBase), type) != std::end(kBase); }

// Share of the next difficulty's extra resist penalty to score under (MECHANICS §9 "Difficulty lookahead").
// Design choice: the database has no level range per difficulty (records/proxies/limit_area*.dbr clamp the
// spawns of 10 pools only). Elite from level 75, Ultimate from 94 (docs/build-ref/plague-garden.md level plan),
// ramping in linearly over the 10 levels before.
double lookahead(const snap::Snapshot& s, const Settings& p) {
  constexpr double kNext[] = {75, 94};  // Normal -> Elite, Elite -> Ultimate
  constexpr double kRamp = 10;
  const int base = enemy::difficulty(s) / 2;
  if (!p.difficulty_lookahead || base > 1) return 0;
  return std::clamp((s.level - (kNext[base] - kRamp)) / kRamp, 0.0, 1.0);
}

// Engine order (MECHANICS §9, CombatManager::TakeAttack 0x11D950): dodge / deflect -> OA vs DA -> block ->
// resists -> armor on what the physical resist left. CombatAttributeDamage_BasePhysical::ReduceDamage 0x1175F0
// applies the resist and only collects armor; Execute 0x117260 applies the hit region's armor
// (physicalDamageDefenseEquationDGP / DLEP). The enemy damage reduction debuff comes before all of it.
Survival survival(const snap::Snapshot& s, const Procs& x, double absorbed, const Settings& p, const arz::Database* db) {
  const model::Sheet sh = model::sheet(s);
  Survival out;
  out.life = sh.life;
  const double pth = formulas::probability_to_hit(enemy_oa(s, p), sh.da);
  // Monster crit damage: only the difficulty's (gameascendant ultimateChallangeAdjustment +40%); their own
  // offensiveCritDamageModifier is not in the table.
  const formulas::Hit h = formulas::expected_hit(pth, s.pth, enemy_of(s).crit_damage / 100);
  out.hit = h.multiplier;
  out.avoid = utility::avoid(s, p.melee_share);
  out.block = utility::block(s, db, enemy_hit(s, p, false), p.incoming_hits_per_sec);
  out.reduction = p.damage_reduction_uptime * utility::damage_reduction(s);
  const double hp = enemy_hit(s, p, true);
  const double abs = std::min(100.0, absorption(s, sh, db)) / 100;
  const auto regions = model::region_armor(s);
  double shares = 0;
  for (const auto& [type, share] : p.threat) shares += share;
  // Enemy resistance reduction: a preset number is always on; the monster table's is on for its share of the
  // incoming damage, the bosses' for boss_share of it (MECHANICS §9 "Enemy resistance reduction"). Each state
  // is also scored under the next difficulty's extra penalty for the lookahead share.
  struct Reduced {
    double share, rr;
    bool ahead = false;
  };
  const enemy::Enemy table = enemy_of(s);
  std::vector<Reduced> states;
  const int difficulty = enemy::difficulty(s);
  if (p.resist_reduction && difficulty / 2 >= p.resist_reduction_from) {
    states.push_back({1, *p.resist_reduction});
  } else {
    const double b = p.boss_share;
    states.push_back({(1 - b) * (1 - table.rr_share) + b * (1 - table.boss_rr_share), 0});
    states.push_back({(1 - b) * table.rr_share, table.rr});
    states.push_back({b * table.boss_rr_share, table.boss_rr});
  }
  const double ahead = lookahead(s, p);
  if (ahead > 0)
    for (std::size_t i = 0, n = states.size(); i < n; ++i) {
      states.push_back({states[i].share * ahead, states[i].rr, true});
      states[i].share *= 1 - ahead;
    }
  // What a state takes off a resist: the 9 base resists only.
  auto reduced = [&](int type, const Reduced& st) {
    if (!base_resist(type)) return 0.0;
    return st.rr + (st.ahead ? enemy::penalty(type, difficulty + 2) - enemy::penalty(type, difficulty) : 0);
  };
  double taken = 0;
  for (const auto& [type, share] : p.threat) {
    const auto x = std::find_if(sh.resists.begin(), sh.resists.end(), [&](const auto& r) { return r.type == type; });
    double f = 0;
    for (const Reduced& st : states) {
      const double rr = reduced(type, st);
      const double r = x != sh.resists.end() ? std::min(x->uncapped - rr, x->max)
                                             : std::min(-rr, static_cast<double>(s.defense_cap));
      f += st.share * std::max(0.0, 1 - r / 100);
    }
    if (type == kPhysical) {
      const double d = hp * f;
      double left = 0;
      for (const auto& [chance, armor] : regions) left += chance * (d - abs * std::min(d, armor));
      f = left / hp;
    }
    out.taken[type] = share * f / shares;
    taken += out.taken[type];
  }
  out.cc = p.cc_share * x.cc;
  std::string rr_terms;
  for (const Reduced& st : states)
    if (!st.ahead && (st.rr != 0 || states.size() == 1)) rr_terms += " " + fmt(st.rr, 1) + " x" + fmt(st.share, 3);
  if (ahead > 0) rr_terms += " ahead x" + fmt(ahead, 2);
  double per_attack = out.hit * (1 - out.avoid) * (1 - out.block) * (1 - out.reduction) * (1 - out.cc) * taken;
  // Heals cover at most 90% of the life lost per second (design choice).
  const double lost = p.incoming_hits_per_sec * enemy_hit(s, p, false) * per_attack * absorbed;
  const double restored = lost > 0 ? std::min(0.9, x.restored / lost) : 0;
  out.procs = 1 - absorbed * (1 - restored);
  per_attack = std::max(1e-6, per_attack * (1 - out.procs));
  out.ehp = sh.life / per_attack;
  out.inputs = "life " + fmt(sh.life) + " da " + fmt(sh.da) + " enemy pth " + fmt(pth) + " x" + fmt(out.hit, 3) +
               " avoid " + fmt(out.avoid, 3) + " block " + fmt(out.block, 3) + " reduced " + fmt(out.reduction, 3) +
               (x.inputs.empty() ? "" : " cc " + fmt(out.cc, 3) + " procs " + fmt(out.procs, 3) + " (" + x.inputs + ")") +
               " armor " + fmt(sh.armor) + " abs " + fmt(abs * 100) + "% hit " + fmt(hp) + " enemy rr" + rr_terms +
               " taken " + fmt(taken, 3) +
               " ehp " + fmt(out.ehp);
  if (p.thresholds.empty()) return out;

  // The 9 base resists use the geometric mean of their factors.
  auto resist = [&](int type) -> const model::Resist* {
    const auto x = std::find_if(sh.resists.begin(), sh.resists.end(), [&](const auto& r) { return r.type == type; });
    return x != sh.resists.end() ? &*x : nullptr;
  };
  auto shown = [&](int type, double rr = 0) {
    const model::Resist* x = resist(type);
    return x ? std::min(x->uncapped - rr, x->max) : -rr;
  };
  // Unverified: that defense x (1 + modifier%) is the sheet's value. Type 16 "TotalSpeed" is read as Slow
  // Resistance, 57 Reflect as reflected damage reduction.
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
        double ft = 1;
        for (const Reduced& st : states) {
          const double v = name == "resist" ? shown(t, reduced(t, st)) : x ? x->uncapped - x->max : -s.defense_cap;
          value += st.share * v / std::size(kBase);
          ft *= std::pow(threshold_factor(v, target, p), st.share * p.threshold_resist_weight / std::size(kBase));
        }
        f *= ft;
        if (name == "resist") out.thresholds["resist:" + std::to_string(t)] = ft;  // one part per resist
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
    if (name != "resist") out.thresholds[name] = f;
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
  std::map<std::string, std::pair<double, double>> terms;
};

struct Utility {
  utility::Context context;
  utility::Values items;
};

Utility utility_of(const snap::Snapshot& s, const Settings& p, const arz::Database* db) {
  return {utility::context(s, db, p.stage), utility::item_values(s, db)};
}

Other other(const snap::Snapshot& s, const Utility& u, const Settings& p) {
  const utility::Context& ctx = u.context;
  const utility::Values v = utility::values(s, u.items);
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

struct Main {
  const snap::Skill* sk = nullptr;  // null: not in the snapshot
  Raw raw;
};

effects::Rates rates(const snap::Snapshot& s, const std::vector<Skill>& skills, const std::vector<Main>& mains,
                     const Settings& p, const arz::Database& db) {
  effects::Rates r;
  r.hits_taken = p.incoming_hits_per_sec;
  r.melee_hits = r.hits_taken * p.melee_share;
  r.blocks = r.hits_taken * utility::block_share(s, &db, r.hits_taken);
  r.projectile_hits = r.hits_taken - r.melee_hits;
  r.crit_hits = r.hits_taken * formulas::expected_hit(formulas::probability_to_hit(enemy_oa(s, p), model::sheet(s).da), s.pth, 0).crit_chance;
  r.low_health = p.low_health_per_sec;
  r.kills = p.kills_per_sec;
  r.pet_attacks = p.pet_attack_rate;
  for (std::size_t k = 0; k < skills.size(); ++k) {
    if (!mains[k].sk) continue;
    const bool weapon = mains[k].raw.weapon_pct > 0;
    const double rate = skills[k].weight * use_rate(s, *mains[k].sk, weapon);
    r.action += rate;
    if (weapon) r.weapon += rate;
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
  std::map<int, double> resist;
  Gear gear;
  std::vector<Main> mains;
  Utility utility;
  struct Hit {
    std::string record;
    bool pet = false;            // own bonuses not modelled
    bool player_scaled = false;  // uses the player's bonuses, like a proc
    double per_sec = 0;
    Amounts flat;
  };
  std::vector<Hit> hits;
  std::map<std::string, std::string> hits_unknown;
};

Side side(const snap::Snapshot& s, const std::vector<Skill>& skills, const Settings& p, const arz::Database& db,
          const Amounts& base, std::vector<std::string>& notes) {
  Side out;
  for (const auto& k : skills) {
    const snap::Skill* sk = skill_by_record(s, k.record);
    out.mains.push_back({sk, sk ? skill_raw(s, *sk, db, notes) : Raw{}});
  }
  out.effects = effects::collect(s, db, rates(s, skills, out.mains, p, db), p.uptime, enemy_types(p));
  std::map<int, double> target = p.enemy_resist;
  for (auto& [type, r] : target) r += enemy::resist(type, enemy::difficulty(s));
  out.resist = effects::resist_multipliers(target, out.effects.reductions);
  out.gear = gear(s, db, base);
  out.utility = utility_of(s, p, &db);
  // Gear ADCtH only reaches skills with weapon damage, x min(weaponDamagePct / 100, 1):
  // SkillManager::CollectAvailableOffensiveDamageAttributes 0x535FB0 adds GetEquipArmorDamageAttributes only from
  // Skill::CollectCombatParameters 0x499E40 when weapon % > 0, and DamageAttributeAbs_LifeLeech::
  // AddDamageToAccumulator 0x19A810 takes min(scale, 1.0). So spells get none. The skill's and its modifiers'
  // own ADCtH count in full (unverified: Skill_Modifier passes its caller's scale).
  if (!skills.empty()) {
    double& leech = out.utility.items.keys["life_leech"];
    double v = 0;
    for (std::size_t k = 0; k < skills.size(); ++k)
      if (out.mains[k].sk)
        v += skills[k].weight * (leech * std::min(1.0, out.mains[k].raw.weapon_pct / 100) + out.mains[k].raw.leech / 100);
    leech = v;
  }
  for (const auto& e : out.effects.effects) {
    if (e.source == "learned") continue;
    const arz::Record* rec = db.record(e.record);
    if (!rec) continue;
    const std::uint32_t lv = std::max<std::uint32_t>(e.level, 1);
    Amounts a;
    add_flat(*rec, lv, 1, a);
    if (a.empty())
      if (const arz::Record* buff = db.record(rec->string("buffSkillName"))) add_flat(*buff, lv, 1, a);
    // A Skill_WPAttack_* replaces a weapon attack, so only its excess over 100% counts.
    // shortcut: the replaced attack is taken as the default attack — revisit with a weapon build.
    double weapon_pct = e.pet ? 0 : at_level(*rec, "weaponDamagePct", lv);
    if (weapon_pct > 0 && rec->cls.starts_with("Skill_WPAttack_")) weapon_pct -= 100;
    if (a.empty() && weapon_pct == 0) continue;
    if (e.per_sec <= 0) {
      out.hits_unknown.emplace(e.record, e.basis);
      continue;
    }
    if (weapon_pct != 0)
      for (const auto& [k, v] : out.gear.flat) a[k] += v * weapon_pct / 100;
    out.hits.push_back({e.record, e.pet && !e.player_scaled, e.player_scaled, e.per_sec, std::move(a)});
  }
  return out;
}

// PetPlayerScaling pets attack with the owner's combat parameters (effects::summoned), like procs.
// shortcut: Class Pet bonuses and the pet's own OA / crit are not modelled — revisit with a pet DPS test.
struct Extra {
  double per_sec = 0;
  std::string inputs;
};

Extra extra_damage(const snap::Snapshot& s, const Side& x, const Settings& p, std::vector<std::string>& notes) {
  Extra out;
  const auto* crit_row = find(s.damage_modifiers, kCritDamage);
  const double hit = attack(s, p, crit_row ? crit_row->damage_modifier : 0).multiplier;
  auto resist = [&](int type) {
    const auto r = x.resist.find(type);
    return r != x.resist.end() ? std::max(0.0, r->second) : 1.0;
  };
  for (const auto& e : x.hits) {
    double per_use = 0;
    if (e.pet) {
      for (const auto& [k, v] : e.flat) per_use += v * resist(k.first);
    } else {
      for (const auto& [k, v] : convert(e.flat, {}, s.conversions, notes)) {
        const auto* m = find(s.damage_modifiers, k.first);
        const double pct = m ? (k.second ? m->dot_modifier : m->damage_modifier) : 0;
        per_use += v * std::max(0.0, 1 + pct / 100 + attribute_bonus(s, k.first, k.second)) * resist(k.first) *
                   (k.second ? x.gear.dot_scale(k.first) : 1.0);
      }
      per_use *= hit;
    }
    out.per_sec += e.per_sec * per_use;
    out.inputs += " " + stem(e.record) + (e.pet ? " (pet) " : e.player_scaled ? " (player-scaled pet) " : " ") +
                  fmt(e.per_sec, 2) + "/s x " + fmt(per_use);
  }
  return out;
}

// re/CombatAttributeType.tsv 42..53: offensive<Kind>Min / Max in seconds, offensive<Kind>Chance in %.
constexpr std::string_view kControl[] = {"Stun", "Freeze", "Knockdown", "Fear", "Confusion", "Petrify", "Sleep", "Trap", "Convert"};

std::vector<Buff> learned_buffs(const snap::Snapshot& s, const Settings& p, const arz::Database& db) {
  std::vector<Buff> out;
  const double cdr = utility::cooldown_reduction(s);
  for (const auto& sk : s.skills) {
    if (!sk.buff_active || *sk.buff_active) continue;  // unknown, or still in the totals
    const auto t = model::buff_timing(sk, db);
    if (!t) continue;
    Buff b{sk.record, 1, {}, model::buff_effect(s, sk, db)};
    if (const auto o = p.uptime.find(sk.record); !p.recast_buffs && o != p.uptime.end()) {
      b.uptime = o->second;
      b.basis = p.live_buffs ? "live" : "preset [uptime]";
    } else if (t->recharge > 0) {
      const double recharge = t->recharge * (1 - cdr);
      b.uptime = recharge > 0 ? std::min(1.0, t->duration / recharge) : 1.0;
      b.basis = fmt(t->duration, 1) + " s / (" + fmt(t->recharge, 1) + " s x (1 - " + fmt(cdr * 100) + "% cooldown reduction))";
    } else {
      b.basis = fmt(t->duration, 1) + " s, no recharge";
    }
    out.push_back(std::move(b));
  }
  return out;
}

Procs procs(const snap::Snapshot& s, const Side& x, const Settings& p, const arz::Database& db) {
  Procs out;
  const double hit = enemy_hit(s, p, false), life = model::sheet(s).life;
  double free = 1;
  for (const auto& e : x.effects.effects) {
    if (e.pet) continue;
    const arz::Record* rec = db.record(e.record);
    // A cast buff is a learned_buffs() entry; here only passives with their own trigger.
    if (!rec || (e.source == "learned" && !model::triggered_buff(*rec))) continue;
    const arz::Record* buff = db.record(rec->string("buffSkillName"));
    const std::uint32_t lv = std::max<std::uint32_t>(e.level, 1);
    const auto o = p.uptime.find(e.record);
    const bool known = e.maintained || e.per_sec > 0 || o != p.uptime.end();
    auto up = [&](double duration) { return o != p.uptime.end() ? o->second : effects::uptime(e, duration); };
    auto seen = [&](const std::string& what) {
      if (known) out.scored.insert(e.record), out.inputs += (out.inputs.empty() ? "" : ", ") + stem(e.record) + " " + what;
      else out.unknown.emplace(e.record, e.basis);
      return known;
    };
    // One enemy is held once: longest kind x its chance.
    double held = 0;
    for (const arz::Record* r : {rec, buff})
      for (const auto kind : kControl) {
        const std::string f = "offensive" + std::string(kind);
        const double min = r ? at_level(*r, f + "Min", lv) : 0, chance = r ? at_level(*r, f + "Chance", lv) : 0;
        if (min > 0) held = std::max(held, (min + std::max(min, at_level(*r, f + "Max", lv))) / 2 * (chance > 0 && chance < 100 ? chance / 100 : 1));
      }
    if (held > 0 && seen("cc " + fmt(up(held), 3))) free *= 1 - up(held);
    const arz::Record* stats = buff && model::self_buff(*buff) ? buff : model::self_buff(*rec) ? rec : nullptr;
    if (!stats) continue;
    // A low-health buff is up exactly while needed, so it counts for that share of the life pool x the chance
    // it is off cooldown. Design choice: assumed to last while life stays under the threshold.
    const double duration = at_level(*stats, "skillActiveDuration", lv);
    const double u = o != p.uptime.end() || e.life_below <= 0 || p.low_health_per_sec <= 0
                         ? up(duration)
                         : std::min(1.0, e.life_below / 100) * std::min(1.0, e.per_sec / p.low_health_per_sec);
    if (!seen("buff " + fmt(u, 3))) continue;
    const double absorb = at_level(*stats, "damageAbsorption", lv);
    if (duration > 0) {
      out.buffs.push_back({e.record, u, o != p.uptime.end() ? "preset [uptime]" : fmt(duration, 1) + " s, " + e.basis,
                           model::buff_effect(*stats, lv),
                           std::min(1.0, at_level(*stats, "damageAbsorptionPercent", lv) / 100), std::min(1.0, absorb / hit)});
    } else {
      out.restored += e.per_sec * absorb;
    }
    out.restored += e.per_sec * (at_level(*stats, "skillLifePercent", lv) / 100 * life + at_level(*stats, "skillLifeBonus", lv));
  }
  out.cc = 1 - free;
  return out;
}

// `shares`: buff record -> share of time up in this state; unnamed buffs count at their uptime.
double state(const snap::Snapshot& s, const Procs& pr, const std::vector<Buff>& learned,
             const std::map<std::string, double>& shares, const arz::Database& db, snap::Snapshot& out) {
  std::vector<std::pair<const model::BuffEffect*, double>> buffs;
  double absorbed = 1;
  for (const auto* list : {&pr.buffs, &learned})
    for (const auto& b : *list) {
      const auto it = shares.find(b.record);
      const double sh = it != shares.end() ? it->second : b.uptime;
      buffs.emplace_back(&b.effect, sh);
      absorbed *= (1 - sh * b.absorb_pct) * (1 - sh * b.absorb_hit);
    }
  model::buffed(s, buffs, db, out);
  // One hit size stands for all, so without the 10% floor a large flat absorption reads as immunity.
  return std::max(0.1, absorbed);
}

struct Eval {
  std::vector<SkillDamage> skills;
  Extra extra;
  Survival sv;
  Other ot;
};

Eval eval(const snap::Snapshot& s, double absorbed, const Side& x, const Procs& pr, const Settings& p,
          const arz::Database* db, std::vector<std::string>& notes) {
  Eval e;
  if (db && !x.mains.empty()) {
    for (const auto& m : x.mains)
      e.skills.push_back(m.sk ? skill_damage(s, *m.sk, m.raw, x.gear, p, notes, nullptr, &x.resist) : SkillDamage{});
    e.extra = extra_damage(s, x, p, notes);
  }
  e.sv = survival(s, pr, absorbed, p, db);
  e.ot = other(s, x.utility, p);
  return e;
}

constexpr std::size_t kMaxEnumerated = 4;

struct Scored {
  Side x;
  Procs pr;
  std::vector<std::pair<double, Eval>> states;  // sums to 1
  std::string buffs;
  const Eval& shown() const {
    return std::max_element(states.begin(), states.end(), [](const auto& a, const auto& b) { return a.first < b.first; })->second;
  }
  // Geometric mean of the states: ln X = sum p x ln X_state, same as the total D^a x S^b x O^c.
  template <class F>
  double ln_mean(F f) const {
    double v = 0;
    for (const auto& [prob, e] : states) v += prob * std::log(f(e));
    return v;
  }
  template <class F>
  double mean(F f) const {
    double v = 0;
    for (const auto& [prob, e] : states) v += prob * f(e);
    return v;
  }
};

// A buff of unmapped stats gives the same result on or off, so it gets no state.
std::vector<const Buff*> open_buffs(const std::vector<Buff>& learned, const std::vector<Buff>& procs) {
  std::vector<const Buff*> open;
  for (const auto* list : {&learned, &procs})
    for (const auto& b : *list)
      if (b.uptime > 0 && b.uptime < 1 && b.matters() &&
          std::none_of(open.begin(), open.end(), [&](const Buff* o) { return o->record == b.record; }))
        open.push_back(&b);
  std::stable_sort(open.begin(), open.end(), [](const Buff* a, const Buff* b) {
    return a->uptime * (1 - a->uptime) > b->uptime * (1 - b->uptime);
  });
  return open;
}

Scored score_side(const snap::Snapshot& s, const std::vector<Skill>& skills, const Settings& p, const arz::Database* db,
                  const Amounts& base, std::vector<std::string>& notes) {
  Scored out;
  if (!db) {
    out.x.utility = utility_of(s, p, db);
    out.states.emplace_back(1.0, eval(s, 1, out.x, out.pr, p, db, notes));
    return out;
  }
  out.x = side(s, skills, p, *db, base, notes);
  out.pr = procs(s, out.x, p, *db);
  const std::vector<Buff> learned = learned_buffs(s, p, *db);
  std::vector<const Buff*> open = open_buffs(learned, out.pr.buffs);
  if (open.size() > kMaxEnumerated) open.resize(kMaxEnumerated);
  for (const Buff* b : open) out.buffs += (out.buffs.empty() ? "" : ", ") + stem(b->record) + " up " + fmt(b->uptime, 3) + " (" + b->basis + ")";
  snap::Snapshot work = s;
  for (std::size_t mask = 0; mask < (std::size_t{1} << open.size()); ++mask) {
    std::map<std::string, double> shares;
    double prob = 1;
    for (std::size_t i = 0; i < open.size(); ++i) {
      const bool on = mask >> i & 1;
      shares[open[i]->record] = on;
      prob *= on ? open[i]->uptime : 1 - open[i]->uptime;
    }
    const double absorbed = state(s, out.pr, learned, shares, *db, work);
    out.states.emplace_back(prob, eval(work, absorbed, out.x, out.pr, p, db, notes));
  }
  return out;
}

// Geometric mean per factor so the factors still multiply to the term. Per-type damage taken is a sum,
// so its parts are means scaled to the term.
Survival mixed_survival(const Scored& sd) {
  Survival out = sd.shown().sv;
  auto g = [&](auto f) { return std::exp(sd.ln_mean(f)); };
  out.ehp = g([](const Eval& e) { return e.sv.ehp; });
  out.life = g([](const Eval& e) { return e.sv.life; });
  out.hit = g([](const Eval& e) { return e.sv.hit; });
  out.avoid = 1 - g([](const Eval& e) { return 1 - e.sv.avoid; });
  out.block = 1 - g([](const Eval& e) { return 1 - e.sv.block; });
  out.reduction = 1 - g([](const Eval& e) { return 1 - e.sv.reduction; });
  out.cc = 1 - g([](const Eval& e) { return 1 - e.sv.cc; });
  out.procs = 1 - g([](const Eval& e) { return 1 - e.sv.procs; });
  auto total = [](const Eval& e) {
    return std::accumulate(e.sv.taken.begin(), e.sv.taken.end(), 0.0, [](double a, const auto& kv) { return a + kv.second; });
  };
  const double sum = sd.mean(total), scale = sum > 0 ? g(total) / sum : 1;
  for (auto& [type, v] : out.taken) v = scale * sd.mean([&, t = type](const Eval& e) { return e.sv.taken.at(t); });
  for (auto& [name, v] : out.thresholds) v = g([&, n = name](const Eval& e) { return e.sv.thresholds.at(n); });
  return out;
}

Other mixed_other(const Scored& sd) {
  Other out = sd.shown().ot;
  out.value = std::exp(sd.ln_mean([](const Eval& e) { return e.ot.value; }));
  for (auto& [key, v] : out.terms) {
    auto term = [&, k = key](const Eval& e) { return e.ot.terms.contains(k) ? e.ot.terms.at(k) : std::pair{0.0, 0.0}; };
    v = {sd.mean([&](const Eval& e) { return term(e).first; }), sd.mean([&](const Eval& e) { return term(e).second; })};
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
  const Gear g = gear(s, db, shared_base(s, s, db));
  for (const auto& k : main_skills(s, auto_pick, &db)) {
    Amounts parts;
    const snap::Skill& sk = *skill_by_record(s, k.record);
    skill_damage(s, sk, skill_raw(s, sk, db, notes), g, auto_pick, notes, &parts);
    const double total = std::accumulate(parts.begin(), parts.end(), 0.0, [](double a, const auto& kv) { return a + kv.second; });
    if (total <= 0) continue;
    for (const auto& [key, v] : parts) out[key] += k.weight * v / total;
  }
  const double sum = std::accumulate(out.begin(), out.end(), 0.0, [](double a, const auto& kv) { return a + kv.second; });
  for (auto& [_, v] : out) v /= sum;
  return out;
}

namespace {

bool has_retaliation(const arz::Record& r, std::uint32_t level) {
  for (const auto& [key, _] : r.fields)
    if (std::string_view(key).starts_with("retaliation") && at_level(r, key, level) != 0) return true;
  return false;
}

}  // namespace

bool tried_first(const Preset& a, const Preset& b) {
  if (a.match_character.empty() != b.match_character.empty()) return !a.match_character.empty();
  if (a.match_characters.empty() != b.match_characters.empty()) return !a.match_characters.empty();
  return a.masteries.size() < b.masteries.size();
}

Archetype archetype(const snap::Snapshot& s, const arz::Database& db) {
  enum Cat { kNone, kPet, kAttack, kWeapon, kRetaliation };
  auto category = [&](const snap::Skill& sk) {
    const arz::Record* r = db.record(sk.record);
    if (!r) return kNone;
    const std::uint32_t lv = sk.current_level;
    if (r->cls == "Skill_SpawnPet" || r->cls == "Skill_TargetedSpawnPet") {
      const arz::Record* pet = effects::summoned(*r, lv, db);
      return pet && pet->cls == "Pet" ? kPet : kNone;
    }
    const arz::Record* buff = db.record(r->string("buffSkillName"));
    // Every playerclass record of these classes has weaponDamagePct.
    if (r->cls.starts_with("Skill_WeaponPool_") || r->cls.starts_with("Skill_WPAttack_")) return kWeapon;
    if (r->cls.starts_with("Skill_Attack")) {
      const arz::Record* d = has_damage(*r, lv) ? r : (buff && has_damage(*buff, lv) ? buff : nullptr);
      if (d) return at_level(*d, "weaponDamagePct", lv) > 0 ? kWeapon : kAttack;
    }
    if (has_retaliation(*r, lv) || (buff && has_retaliation(*buff, lv))) return kRetaliation;
    return kNone;
  };
  std::map<std::uint32_t, const snap::Skill*> parent;
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
  if (std::find(p.match_characters.begin(), p.match_characters.end(), s.character_name) != p.match_characters.end()) {
    m.ok = true;
    m.why = "[match] characters lists " + s.character_name;
    return m;
  }
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
  const double softplus = z > 30 ? z : std::log1p(std::exp(z));  // no overflow
  return std::exp(p.threshold_above * u - (p.threshold_below - p.threshold_above) * p.threshold_width * softplus);
}

namespace {

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

// A newline would end the # comment.
std::string comment(std::string v) {
  for (char& c : v)
    if (static_cast<unsigned char>(c) < 0x20 || c == 0x7F) c = ' ';
  return v;
}

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
  if (type == 7) return "acid";
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
  line("threshold_resist_weight", number(from.threshold_resist_weight));
  line("recast_buffs", from.recast_buffs ? "true" : "false");
  line("buff_mode", from.live_buffs ? "\"live\"" : "\"average\"");
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
  line("kills_per_sec", number(from.kills_per_sec));
  std::string cw;
  for (const auto& [a, b] : from.conversion_warn) cw += (cw.empty() ? "" : ", ") + toml_string(conversion_name(a) + "->" + conversion_name(b));
  line("conversion_warn", "[" + cw + "]");
  o += "\n[survival]\n";
  line("enemy_oa_per_level", number(from.enemy_oa_per_level));
  line("enemy_hit_per_level", number(from.enemy_hit_per_level));
  line("melee_share", number(from.melee_share));
  line("damage_reduction_uptime", number(from.damage_reduction_uptime));
  line("resist_reduction", from.resist_reduction ? number(*from.resist_reduction) : "\"auto\"");
  line("resist_reduction_from", toml_string(std::array{"normal", "elite", "ultimate"}[from.resist_reduction_from]));
  line("boss_share", number(from.boss_share));
  line("difficulty_lookahead", from.difficulty_lookahead ? "true" : "false");
  line("incoming_hits_per_sec", number(from.incoming_hits_per_sec));
  line("low_health_per_sec", number(from.low_health_per_sec));
  line("cc_share", number(from.cc_share));
  o += "\n[survival.threat]\n";
  for (const auto& [t, v] : from.threat) line(threat_name(t), number(v));
  o += "\n[other]\n";
  // Frozen files have no stages: conditions are resolved for the stage in use.
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

// Parts get the term's ln ratio in proportion to their linear changes.
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
  std::vector<std::string> notes_b, notes_a;
  const Amounts base = db ? shared_base(before, after, *db) : Amounts{};
  const Scored B = score_side(before, skills, p, db, base, notes_b), A = score_side(after, skills, p, db, base, notes_a);
  const Eval &b0 = B.shown(), &a0 = A.shown();
  const Procs &pb = B.pr, &pa = A.pr;
  if (!B.buffs.empty()) out.before += "buff states (likeliest shown): " + B.buffs + "; ";
  if (!A.buffs.empty()) out.after += "buff states (likeliest shown): " + A.buffs + "; ";
  auto one_sided = [&](const std::map<std::string, std::string>& b, const std::map<std::string, std::string>& a) {
    for (const auto* m : {&b, &a})
      for (const auto& [record, why] : *m)
        if (!(m == &b ? a : b).contains(record) &&
            std::none_of(out.unscored.begin(), out.unscored.end(), [&](const Unscored& u) { return u.record == record; }))
          out.unscored.push_back({record, why});
  };
  if (!db) out.notes.push_back("no database: damage not scored");
  else if (skills.empty()) out.notes.push_back("no main damage skill found: damage not scored");
  else {
    const Side &sb = B.x, &sa = A.x;
    auto defaults = [](const Side& x) {
      std::map<std::string, std::string> m;
      for (const auto& r : x.effects.reductions)
        if (r.origin == effects::Origin::Default) m.emplace(r.record, r.basis);
      return m;
    };
    one_sided(defaults(sb), defaults(sa));
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
    // Damage depends on one side alone, so a swap and its reverse are exact reciprocals:
    // D = geomean over states of prod_skills value^weight x (1 + procs and pets per s / main skills per s).
    // Main skills per second: cooldown skills at cooldown_uses, the rest share casts by weight at the snapshot's
    // attack / cast speed (design choice, MECHANICS §5).
    auto main = [&](const Eval& e) {
      double on_cooldown = 0, spam = 0, spam_weight = 0;
      for (std::size_t k = 0; k < skills.size(); ++k) {
        const SkillDamage& x = e.skills[k];
        if (x.cooldown > 0) on_cooldown += x.per_use * x.uses;
        else spam += skills[k].weight * x.per_use * x.rate, spam_weight += skills[k].weight;
      }
      return on_cooldown + (spam_weight > 0 ? spam / spam_weight : 0);
    };
    std::vector<bool> both(skills.size(), true);
    bool extra = false, mains = true;
    for (const Scored* sd : {&B, &A})
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
      parts.push_back({"", skills[k].record, both[k] ? skills[k].weight * (A.ln_mean(value) - B.ln_mean(value)) : 0, ""});
    }
    const double ratio = std::exp(A.ln_mean(damage) - B.ln_mean(damage));
    const Extra &eb = b0.extra, &ea = a0.extra;
    if (extra && mains) {
      out.before += "main " + fmt(main(b0)) + "/s, procs+pets " + fmt(eb.per_sec) + "/s:" + eb.inputs + "; ";
      out.after += "main " + fmt(main(a0)) + "/s, procs+pets " + fmt(ea.per_sec) + "/s:" + ea.inputs + "; ";
      parts.push_back({"procs and pets", "", A.ln_mean(share) - B.ln_mean(share), ""});
      for (const Side* x : {&sb, &sa})
        for (const auto& h : x->hits) out.procs.insert(h.record);
    } else if (extra) {
      out.notes.push_back("main skills' damage per second unknown (no attack / cast speed): procs and pets not scored");
    }
    one_sided(sb.hits_unknown, sa.hits_unknown);
    out.ratio.damage = ratio;
    contribute(out.contributions, ratio, p.damage_weight, std::move(parts));
    out.before += resist_inputs(sb) + "; ";
    out.after += resist_inputs(sa) + "; ";
    for (auto* notes : {&notes_b, &notes_a})
      for (auto& n : *notes)
        if (std::find(out.notes.begin(), out.notes.end(), n) == out.notes.end()) out.notes.push_back(std::move(n));
  }
  one_sided(pb.unknown, pa.unknown);
  out.procs.insert(pb.scored.begin(), pb.scored.end());
  out.procs.insert(pa.scored.begin(), pa.scored.end());
  const Survival sb = mixed_survival(B), sa = mixed_survival(A);
  out.ratio.survival = sb.ehp > 0 ? sa.ehp / sb.ehp : 1;
  {
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
    // A resist's target part joins its damage part: one line per resist.
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
  const Other ob = mixed_other(B), oa = mixed_other(A);
  out.ratio.other = ob.value > 0 ? oa.value / ob.value : 1;
  {
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

std::string_view damage_label(int type, bool dot) {
  // 58 / 59 / 60: db_fields.h.
  if (type == 58) return "Elemental";
  if (type == 59) return "Crit";
  if (type == 60) return "all";
  for (const auto& d : kDamageNames)
    if (d.type == type && d.dot == dot) return d.label;
  return {};
}

BuffView buff_view(const snap::Snapshot& s, const std::vector<Skill>& skills, const Settings& p, const arz::Database* db) {
  BuffView out;
  if (!db) {
    out.sheet = model::sheet(s);
    return out;
  }
  std::vector<std::string> notes;
  const Side x = side(s, skills, p, *db, {}, notes);
  const Procs pr = procs(s, x, p, *db);
  const std::vector<Buff> learned = learned_buffs(s, p, *db);
  std::vector<Buff> all = learned;
  for (const auto& b : pr.buffs)
    if (std::none_of(all.begin(), all.end(), [&](const Buff& o) { return o.record == b.record; })) all.push_back(b);
  std::map<std::string, double> usual;
  for (const auto& b : all) usual[b.record] = b.uptime >= 0.5;
  snap::Snapshot work = s;
  auto sheet = [&](const std::map<std::string, double>& shares) {
    state(s, pr, learned, shares, *db, work);
    return model::sheet(work);
  };
  out.sheet = sheet(usual);
  for (const auto& b : all) {
    auto other = usual;
    other[b.record] = !(b.uptime >= 0.5);
    const model::Sheet flipped = sheet(other);
    out.buffs.push_back({b.record, b.uptime, b.basis, b.uptime >= 0.5 ? flipped : out.sheet, b.uptime >= 0.5 ? out.sheet : flipped,
                         b.effect, b.absorb_pct, b.absorb_hit});
  }
  return out;
}

}  // namespace gdc::score
