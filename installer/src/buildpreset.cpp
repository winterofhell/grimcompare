#include "buildpreset.h"

#include <algorithm>
#include <cstdio>
#include <set>

namespace setup {

namespace {

constexpr const char* kDefaultAttack = "records/skills/default/defaultweaponattack.dbr";

std::string num(double v) {
  char b[32];
  std::snprintf(b, sizeof b, "%.6g", v);
  std::string s = b;
  if (s.find_first_of(".e") == std::string::npos) s += ".0";
  return s;
}

std::string list(const std::vector<std::string>& v) {
  std::string out = "[";
  for (std::size_t i = 0; i < v.size(); ++i) out += (i ? ", " : "") + toml_string(v[i]);
  return out + "]";
}

std::string comment(std::string_view s) {
  std::string out;
  std::size_t start = 0;
  while (start <= s.size()) {
    const auto nl = std::min(s.find('\n', start), s.size());
    out += "# " + std::string(s.substr(start, nl - start)) + "\n";
    start = nl + 1;
  }
  return out;
}

std::string join_words(const std::vector<std::string>& v) {
  std::string out;
  for (std::size_t i = 0; i < v.size(); ++i) out += (i == 0 ? "" : i + 1 == v.size() ? " and " : ", ") + v[i];
  return out;
}

bool in_masteries(const std::string& record, const std::vector<int>& masteries) {
  if (record == kDefaultAttack) return true;
  for (const int m : masteries) {
    char b[40];
    std::snprintf(b, sizeof b, "records/skills/playerclass%02d/", m);
    if (record.starts_with(b)) return true;
  }
  return false;
}

// Plain words for [other] keys a preset may weigh.
std::string other_words(const std::string& key) {
  static const std::map<std::string, std::string> kWords = {
      {"run_speed", "run speed"},           {"life_regen", "life regeneration"},
      {"energy_regen", "energy regeneration"}, {"life_leech", "life leech (attack damage converted to health)"},
      {"stun_resist", "stun resistance"},   {"freeze_resist", "freeze resistance"},
      {"slow_resist", "slow resistance"},   {"petrify_resist", "petrify resistance"},
      {"knockdown_resist", "knockdown resistance"}, {"trap_resist", "entrapment resistance"},
      {"sleep_resist", "sleep resistance"}, {"disruption_resist", "skill disruption protection"},
      {"retaliation", "flat retaliation damage"}, {"retaliation_modifier", "% retaliation damage"},
      {"pet_bonus", "your pets' life and resistances"}, {"racial_damage", "damage to specific enemy races"}};
  const auto it = kWords.find(key);
  return it == kWords.end() ? key : it->second;
}

}  // namespace

std::string toml_string(std::string_view v) {
  std::string out = "\"";
  for (const char c : v) {
    if (c == '"' || c == '\\') out += '\\', out += c;
    else if (static_cast<unsigned char>(c) < 0x20 || c == 0x7F) {
      char b[8];
      std::snprintf(b, sizeof b, "\\u%04X", static_cast<unsigned>(static_cast<unsigned char>(c)));
      out += b;
    } else out += c;
  }
  return out + "\"";
}

std::pair<double, double> playstyle_weights(Playstyle p) {
  // Design choice: a 25-30% shift of weight between Damage and Survivability; balanced = the default preset.
  switch (p) {
    case Playstyle::kTanky: return {0.8, 1.3};
    case Playstyle::kGlass: return {1.25, 0.75};
    case Playstyle::kBalanced: break;
  }
  return {1.0, 1.0};
}

BuildAnswers from_entry(const Entry& e, const std::vector<int>& masteries, const std::string& character) {
  BuildAnswers a;
  a.title = e.name;
  a.source = e.id;
  a.characters = {character};
  a.masteries = masteries;
  a.damage = e.damage;
  for (const auto& s : e.skills)
    if (in_masteries(s, masteries)) a.skills.push_back(s);
  a.sets = e.sets;
  a.other = e.other;
  a.pets = e.category == "pets" || e.other.contains("pet_bonus");
  // Same as presets/pet.toml: pets' own defenses ride on pet_bonus in Other, so Other weighs more.
  if (e.category == "pets") a.advanced["score.other"] = 0.35;
  if (!e.conversion.empty()) a.hints.push_back("Conversion: " + e.conversion);
  if (!e.stats.empty()) a.hints.push_back("Key stats: " + e.stats);
  if (!e.items.empty()) a.hints.push_back("Key items: " + e.items);
  return a;
}

std::string preset_text(const BuildAnswers& a) {
  std::string t = std::string(kSetupMark) + " Run setup again to rebuild it or bind it to another character.\n";
  t += comment(a.title + (a.source == "custom" ? " (your answers)" : " (catalog build " + a.source + ")"));
  for (const auto& h : a.hints) t += comment(h);
  t += "# Weights are design choices on top of the game's own numbers (docs/PRESETS.md).\n\n";

  t += "[preset]\nname = " + toml_string(preset_stem(a.title)) + "\n";
  if (!a.sets.empty()) t += "target_sets = " + list(a.sets) + "\n";

  std::vector<std::string> masteries;
  for (const int m : a.masteries) masteries.push_back(mastery_record(m));
  t += "\n[match]\ncharacters = " + list(a.characters) + "\n";
  if (!masteries.empty()) t += "masteries = " + list(masteries) + "\n";
  t += "damage = " + list(a.damage) + "\n";

  const auto [da, sb] = playstyle_weights(a.playstyle);
  std::map<std::string, std::map<std::string, double>> tables;
  if (a.playstyle != Playstyle::kBalanced) tables["score"]["damage"] = da, tables["score"]["survival"] = sb;
  for (const auto& [k, v] : a.advanced) {
    const auto dot = k.find('.');
    if (dot != std::string::npos) tables[k.substr(0, dot)][k.substr(dot + 1)] = v;
  }
  if (tables.contains("score")) {
    t += "\n[score]\n";
    for (const auto& [k, v] : tables["score"]) t += k + " = " + num(v) + "\n";
  }
  t += "\n[damage]\n";
  t += a.skills.empty() ? "skills = []  # none for these masteries: the 3 skills with the most points\n" : "skills = " + list(a.skills) + "\n";
  t += "skill_weights = \"points\"\nconversion_warn = [\"*->*\"]\n";
  for (const auto& [k, v] : tables["damage"]) t += k + " = " + num(v) + "\n";
  for (const char* table : {"survival", "thresholds"})
    if (tables.contains(table)) {
      t += std::string("\n[") + table + "]\n";
      for (const auto& [k, v] : tables[table]) t += k + " = " + num(v) + "\n";
    }
  if (!a.other.empty()) {
    t += "\n[other]\n";
    for (const auto& [k, w] : a.other) {
      t += k + " = ";
      if (w.when.empty()) t += num(w.weight);
      else t += "{ weight = " + num(w.weight) + ", when = " + (w.when.size() == 1 ? toml_string(w.when[0]) : list(w.when)) + " }";
      t += "\n";
    }
  }
  return t;
}

std::vector<std::string> priorities(const BuildAnswers& a, const std::map<std::string, std::string>& skill_names) {
  std::vector<std::string> out;
  std::vector<std::string> types;
  for (const auto& d : a.damage)
    if (const auto k = gdc::score::damage_key(d)) types.push_back(gdc::score::damage_label(*k));
  std::vector<std::string> skills;
  for (const auto& s : a.skills)
    if (const auto it = skill_names.find(s); it != skill_names.end() && !it->second.empty()) skills.push_back(it->second);
  std::string dmg = "More " + join_words(types) + " damage, plus Offensive Ability and crit damage";
  if (!skills.empty()) dmg += ", for " + join_words(skills);
  if (a.pets) dmg += ", and your pets' damage";
  const std::string live = "Staying alive: resistances up to the cap, life, armor and Defensive Ability";
  switch (a.playstyle) {
    case Playstyle::kTanky:
      out.push_back(live + " (weighs more than damage).");
      out.push_back(dmg + ".");
      break;
    case Playstyle::kGlass:
      out.push_back(dmg + " (weighs more than defense).");
      out.push_back(live + ", a little less than damage.");
      break;
    case Playstyle::kBalanced:
      out.push_back(dmg + ".");
      out.push_back(live + ", as much as damage.");
      break;
  }
  std::vector<std::pair<double, std::string>> extra;
  for (const auto& [k, w] : a.other)
    if (w.weight > 0) extra.emplace_back(w.weight, other_words(k));
  std::stable_sort(extra.begin(), extra.end(), [](const auto& x, const auto& y) { return x.first > y.first; });
  std::vector<std::string> words;
  for (const auto& [_, s] : extra) words.push_back(s);
  if (!words.empty()) out.push_back("Also: " + join_words(words) + ".");
  if (!a.sets.empty()) out.push_back("Hovering a piece of your target sets shows how many you wear and which are missing.");
  return out;
}

std::string preset_stem(std::string_view name) {
  std::string out;
  for (const char c : name) {
    const bool keep = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_';
    const char x = keep ? static_cast<char>(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c) : '-';
    if (x == '-' && (out.empty() || out.back() == '-')) continue;
    out += x;
  }
  while (!out.empty() && out.back() == '-') out.pop_back();
  if (out.size() > 60) out.resize(60);
  return out.empty() ? "build" : out;
}

bool made_by_setup(std::string_view text) { return text.starts_with(kSetupMark); }

std::optional<std::string> with_characters(std::string_view text, const std::vector<std::string>& characters) {
  if (!made_by_setup(text)) return std::nullopt;
  const std::string_view key = "\ncharacters = ";
  const auto at = text.find(key);
  if (at == std::string_view::npos) return std::nullopt;
  const auto end = text.find('\n', at + 1);
  if (end == std::string_view::npos) return std::nullopt;
  return std::string(text.substr(0, at + key.size())) + list(characters) + std::string(text.substr(end));
}

}  // namespace setup
