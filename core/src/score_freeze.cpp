#include <array>
#include <cctype>
#include <charconv>
#include <cmath>
#include <cstdio>

#include "score_internal.h"

namespace gdc::score {
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
  if (type == kElemental) return "elemental";
  if (type == kPoison) return "acid";
  return std::string(threat_name(type));
}

}  // namespace

std::string frozen_file(const snap::Snapshot& s) {
  std::string out = "frozen-";
  for (const char c : s.character_name)
    out += std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_' ? c : '_';
  if (out == "frozen-") out += "character";
  if (snap::save_key_name(s.save).empty()) return out;
  // Same name on another account, save or backup: separate file.
  const std::string_view key = s.save;
  out += key.starts_with("cloud/") ? "-" + std::string(key.substr(6, key.find('/', 6) - 6)) : std::string("-local");
  if (key.find("/user/") != std::string_view::npos) out += "-custom";
  if (key.find("/__") != std::string_view::npos) out += "-backup";
  return out;
}

namespace {

void line(std::string& o, std::string_view k, const std::string& v, std::string_view note = {}) {
  o += std::string(k) + " = " + v + (note.empty() ? "" : "  # " + comment(std::string(note))) + "\n";
}

void write_header(std::string& o, const Preset& chosen, const snap::Snapshot& s, const Stage* st, const std::string& name) {
  o += "# Frozen by GrimCompare for " + comment(s.character_name) + " (level " + std::to_string(s.level) + ") from " +
       comment(chosen.name.empty() ? "the generic preset" : chosen.name) + (st ? ", stage " + comment(st->name) : "") + ".\n";
  o += "# The settings in use at that moment, with the main skills written out. Auto uses this file for this\n";
  o += "# character only ([match] character); edit it freely, delete it to go back to Auto. Freezing again\n";
  o += "# overwrites it: rename a copy you want to keep (and change its [preset] name).\n\n";
  o += "[preset]\n";
  line(o, "name", toml_string(name));
}

void write_match(std::string& o, const Preset& chosen, const snap::Snapshot& s, const arz::Database& db,
                 const Labels& labels) {
  o += "\n[match]\n";
  line(o, "character", toml_string(s.character_name));
  if (!s.save.empty()) line(o, "saves", "[" + toml_string(s.save) + "]");
  o += "masteries = [\n";
  for (const auto& sk : s.skills)
    if (sk.level > 0)
      if (const arz::Record* r = db.record(sk.record); r && r->cls == "Skill_Mastery")
        o += "  " + toml_string(sk.record) + ",  # " + comment(labels.skill(sk.record)) + "\n";
  o += "]\n";
  if (!chosen.match_archetype.empty()) line(o, "archetype", toml_string(chosen.match_archetype));
  if (!chosen.match_damage.empty()) {
    std::string d;
    for (const auto& k : chosen.match_damage)
      for (const auto& n : kDamageNames)
        if (n.type == k.first && n.dot == k.second) d += (d.empty() ? "" : ", ") + toml_string(n.key);
    line(o, "damage", "[" + d + "]");
    line(o, "min_share", number(chosen.match_share));
  }
}

void write_score(std::string& o, const Settings& from) {
  o += "\n[score]\n";
  line(o, "damage", number(from.damage_weight));
  line(o, "survival", number(from.survival_weight));
  line(o, "other", number(from.other_weight));
  line(o, "negligible_percent", number(from.negligible_percent));
  line(o, "threshold_below", number(from.threshold_below));
  line(o, "threshold_above", number(from.threshold_above));
  line(o, "threshold_width", number(from.threshold_width));
  line(o, "threshold_resist_weight", number(from.threshold_resist_weight));
  line(o, "recast_buffs", from.recast_buffs ? "true" : "false");
  line(o, "buff_mode", from.live_buffs ? "\"live\"" : "\"average\"");
}

void write_damage(std::string& o, const Settings& from, const std::vector<Skill>& skills, const Labels& labels) {
  o += "\n[damage]\n";
  o += "skills = [\n";
  for (const auto& k : skills)
    o += "  " + toml_string(k.record) + ",  # " + comment(labels.skill(k.record)) + " (weight " + number(std::round(k.weight * 1000) / 1000) + ")\n";
  o += "]\n";
  line(o, "skill_weights", from.skills.empty() || from.weigh_by_points ? "\"points\"" : "\"equal\"",
       from.skills.empty() ? "picked automatically when frozen: by points spent" : "");
  line(o, "auto_skills", std::to_string(from.auto_skills));
  line(o, "filler_weight", number(from.filler_weight));
  line(o, "enemy_da_per_level", number(from.enemy_da_per_level));
  line(o, "pet_attack_rate", number(from.pet_attack_rate));
  line(o, "kills_per_sec", number(from.kills_per_sec));
  std::string cw;
  for (const auto& [a, b] : from.conversion_warn) cw += (cw.empty() ? "" : ", ") + toml_string(conversion_name(a) + "->" + conversion_name(b));
  line(o, "conversion_warn", "[" + cw + "]");
}

void write_survival(std::string& o, const Settings& from) {
  o += "\n[survival]\n";
  line(o, "enemy_oa_per_level", number(from.enemy_oa_per_level));
  line(o, "enemy_hit_per_level", number(from.enemy_hit_per_level));
  line(o, "melee_share", number(from.melee_share));
  line(o, "damage_reduction_uptime", number(from.damage_reduction_uptime));
  line(o, "resist_reduction", from.resist_reduction ? number(*from.resist_reduction) : "\"auto\"");
  line(o, "resist_reduction_from", toml_string(std::array{"normal", "elite", "ultimate"}[from.resist_reduction_from]));
  line(o, "boss_share", number(from.boss_share));
  line(o, "difficulty_lookahead", from.difficulty_lookahead ? "true" : "false");
  line(o, "incoming_hits_per_sec", number(from.incoming_hits_per_sec));
  line(o, "low_health_per_sec", number(from.low_health_per_sec));
  line(o, "cc_share", number(from.cc_share));
  o += "\n[survival.threat]\n";
  for (const auto& [t, v] : from.threat) line(o, threat_name(t), number(v));
}

// Frozen files have no stages: resolve stage conditions now.
void write_other(std::string& o, const Settings& from) {
  o += "\n[other]\n";
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
    line(o, st.key, w.when.empty() ? number(w.weight) : "{ weight = " + number(w.weight) + ", when = [" + when + "] }",
         "per " + std::string(st.unit));
  }
}

void write_enemy_thresholds_uptime(std::string& o, const Settings& from) {
  o += "\n[enemy]\n";
  for (const auto& [t, v] : from.enemy_resist) line(o, threat_name(t), number(v));
  o += "\n[thresholds]\n";
  for (const auto& [k, v] : from.thresholds) line(o, k, number(v));
  o += "\n[uptime]\n";
  for (const auto& [k, v] : from.uptime) line(o, toml_string(k), number(v));
}

}  // namespace

std::string freeze(const Preset& chosen, const snap::Snapshot& s, const arz::Database& db, const std::string& name,
                   const Labels& labels) {
  const Stage* st = stage(chosen, s);
  const Settings& from = st ? st->settings : chosen;
  const auto skills = main_skills(s, from, &db);
  std::string o;
  write_header(o, chosen, s, st, name);
  write_match(o, chosen, s, db, labels);
  write_score(o, from);
  write_damage(o, from, skills, labels);
  write_survival(o, from);
  write_other(o, from);
  write_enemy_thresholds_uptime(o, from);
  return o;
}

}  // namespace gdc::score
