#include <algorithm>
#include <cmath>

#include "score_internal.h"

#include <tomlplusplus/gdc_toml.hpp>

namespace gdc::score {
namespace {

constexpr std::string_view kThresholds[] = {"resist",      "resist_overcap", "physical_resist", "absorption",
                                            "armor",       "stun_resist",    "freeze_resist",   "slow_resist",
                                            "reflect_reduction", "life", "da"};

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

// 0 = "*". Acid and poison share DB type 7.
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

bool score_table(Parser& ps, const toml::table& root, const std::string& w, Settings& p) {
  return ps.table(root, "score", [&](std::string_view k, const toml::node& v) {
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
}

bool damage_table(Parser& ps, const toml::table& root, const std::string& w, Settings& p) {
  return ps.table(root, "damage", [&](std::string_view k, const toml::node& v) {
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
}

bool threat_table(Parser& ps, const toml::node& v, const std::string& w, Settings& p) {
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

bool survival_table(Parser& ps, const toml::table& root, const std::string& w, Settings& p) {
  return ps.table(root, "survival", [&](std::string_view k, const toml::node& v) {
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
    if (k == "threat") return threat_table(ps, v, w, p);
    return ps.unknown(v, w + "survival", k);
  });
}

bool other_table(Parser& ps, const toml::table& root, const std::string& w, Settings& p) {
  return ps.table(root, "other", [&](std::string_view k, const toml::node& v) {
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
}

bool enemy_table(Parser& ps, const toml::table& root, const std::string& w, Settings& p) {
  return ps.table(root, "enemy", [&](std::string_view k, const toml::node& v) {
    const auto at = std::find_if(std::begin(kThreatTypes), std::end(kThreatTypes), [&](const auto& e) { return e.first == k; });
    if (at == std::end(kThreatTypes)) return ps.unknown(v, w + "enemy", k);
    return ps.number(v, k, -100, 100, p.enemy_resist[at->second]);
  });
}

bool thresholds_table(Parser& ps, const toml::table& root, const std::string& w, Settings& p) {
  return ps.table(root, "thresholds", [&](std::string_view k, const toml::node& v) {
    if (std::find(std::begin(kThresholds), std::end(kThresholds), k) == std::end(kThresholds))
      return ps.unknown(v, w + "thresholds", k);
    double t = 0;
    if (!ps.number(v, k, 0, 1e6, t)) return false;
    if (t > 0) p.thresholds[std::string(k)] = t;
    else p.thresholds.erase(std::string(k));  // 0 lets a stage drop a threshold
    return true;
  });
}

bool uptime_table(Parser& ps, const toml::table& root, Settings& p) {
  return ps.table(root, "uptime", [&](std::string_view k, const toml::node& v) {
    if (!k.starts_with("records/")) return ps.fail(v, "uptime: keys are effect record paths (\"records/...\")");
    return ps.number(v, k, 0, 1, p.uptime[std::string(k)]);
  });
}

bool settings_tables(Parser& ps, const toml::table& root, std::string_view where, Settings& p) {
  const std::string w(where);
  return score_table(ps, root, w, p) &&
         damage_table(ps, root, w, p) &&
         survival_table(ps, root, w, p) &&
         other_table(ps, root, w, p) &&
         enemy_table(ps, root, w, p) &&
         thresholds_table(ps, root, w, p) &&
         uptime_table(ps, root, p);
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

bool preset_table(Parser& ps, const toml::table& root, Preset& p) {
  return ps.table(root, "preset", [&](std::string_view k, const toml::node& v) {
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
}

bool match_table(Parser& ps, const toml::table& root, Preset& p) {
  return ps.table(root, "match", [&](std::string_view k, const toml::node& v) {
    if (k == "masteries") return ps.strings(v, k, p.masteries);
    if (k == "damage") return damage_keys(ps, v, k, p.match_damage);
    if (k == "min_share") return ps.number(v, k, 0, 1, p.match_share);
    if (k == "characters") return ps.strings(v, k, p.match_characters, "character name");
    if (k == "saves") {
      if (!ps.strings(v, k, p.match_saves, "save key")) return false;
      for (const auto& s : p.match_saves)
        if (snap::save_key_name(s).empty()) return ps.fail(v, "match.saves: \"" + s + "\" is not like \"cloud/<Steam account>/main/_Name\" or \"local/main/_Name\"");
      return true;
    }
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
}

bool stage_blocks(Parser& ps, const toml::table& root, Preset& p) {
  const toml::node* n = root.get("stage");
  if (!n) return true;
  const toml::array* a = n->as_array();
  if (!a) return ps.fail(*n, "stage must be [[stage]] blocks");
  for (const auto& e : *a) {
    Stage st;
    if (!stage_block(ps, e, p, st)) return false;
    p.stages.push_back(std::move(st));
  }
  return true;
}

const snap::Skill* learned(const snap::Snapshot& s, const std::string& record) {
  for (const auto& sk : s.skills)
    if (sk.record == record && sk.level > 0) return &sk;
  return nullptr;
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

std::string damage_name(DamageKey k) {
  for (const auto& d : kDamageNames)
    if (d.type == k.first && d.dot == k.second) return std::string(d.key);
  return "";
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
  if (!settings_tables(ps, root, "", p) || !preset_table(ps, root, p) || !match_table(ps, root, p) ||
      !stage_blocks(ps, root, p))
    return std::nullopt;
  return p;
}

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

std::string_view damage_label(int type, bool dot) {
  // 58 / 59 / 60: db_fields.h.
  if (type == 58) return "Elemental";
  if (type == 59) return "Crit";
  if (type == 60) return "all";
  for (const auto& d : kDamageNames)
    if (d.type == type && d.dot == dot) return d.label;
  return {};
}

}  // namespace gdc::score
