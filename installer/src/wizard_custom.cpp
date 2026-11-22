// Questions for a build of your own.
#include <algorithm>

#include "tui.h"
#include "wizard_internal.h"

namespace setup::wiz {

using ui::fmt;
using ui::tr;

namespace {

struct BaseType {
  const char* label;
  std::vector<std::string> names;
};
const std::vector<BaseType>& base_types() {
  static const std::vector<BaseType> kTypes = {
      {"Physical", {"physical", "internal_trauma"}}, {"Pierce", {"pierce"}},
      {"Cold", {"cold", "frostburn"}},               {"Fire", {"fire", "burn"}},
      {"Lightning", {"lightning", "electrocute"}},   {"Acid / Poison", {"acid", "poison"}},
      {"Vitality", {"vitality", "vitality_decay"}},  {"Chaos", {"chaos"}},
      {"Aether", {"aether"}},                        {"Bleeding", {"bleeding"}}};
  return kTypes;
}

std::vector<std::string> types_of(const std::vector<int>& picked) {
  std::vector<std::string> out;
  for (const int i : picked)
    for (const auto& n : base_types()[static_cast<std::size_t>(i)].names)
      if (std::find(out.begin(), out.end(), n) == out.end()) out.push_back(n);
  return out;
}

struct Advanced {
  const char* key;
  const char* label;
  const char* help;
};
const std::vector<Advanced>& advanced_list() {
  static const std::vector<Advanced> kList = {
      {"score.damage", "Damage weight", "How strongly damage counts (the exponent of Damage). Higher = damage matters more."},
      {"score.survival", "Survival weight", "How strongly staying alive counts (the exponent of Survivability)."},
      {"score.other", "Extras weight", "How strongly small extras count: run speed, regeneration, crowd-control resists."},
      {"thresholds.resist", "Resistance target", "Each base resistance below this costs a lot, above it adds little. 0 = off, 80 = the cap."},
      {"damage.pack_share", "Pack share", "Part of the fighting against packs rather than one target (0..1): area, extra targets, projectiles and chains count there."},
      {"damage.kills_per_sec", "Kills per second", "Enemies you kill per second while fighting: drives \"on kill\" effects. Lower it for boss fights."},
      {"survival.incoming_hits_per_sec", "Hits taken per second", "Drives \"when hit\" effects such as retaliation procs and defensive buffs."},
      {"survival.melee_share", "Share of melee hits", "Part of the hits you take that are melee (0..1); the rest are projectiles."},
      {"survival.boss_share", "Share of boss damage", "Part of the damage you take that comes from bosses (0..1)."},
  };
  return kList;
}

double default_of(const std::string& key, const BuildAnswers& a) {
  const gdc::score::Settings s;
  const auto [da, sb] = playstyle_weights(a.playstyle);
  if (key == "score.damage") return da;
  if (key == "score.survival") return sb;
  if (key == "score.other") return a.pets ? 0.35 : s.other_weight;
  if (key == "thresholds.resist") return 0;
  if (key == "damage.pack_share") return s.pack_share;
  if (key == "damage.kills_per_sec") return s.kills_per_sec;
  if (key == "survival.incoming_hits_per_sec") return s.incoming_hits_per_sec;
  if (key == "survival.melee_share") return s.melee_share;
  if (key == "survival.boss_share") return s.boss_share;
  return 0;
}

std::string num(double v) {
  char b[32];
  std::snprintf(b, sizeof b, "%g", v);
  return b;
}

void advanced(BuildAnswers& a) {
  for (;;) {
    std::vector<ui::Item> items;
    for (const auto& x : advanced_list()) {
      const auto it = a.advanced.find(x.key);
      const double d = default_of(x.key, a);
      items.push_back({x.label, it == a.advanced.end() ? fmt(tr("{} (default)"), {num(d)}) : fmt(tr("{} (default {})"), {num(it->second), num(d)})});
    }
    items.push_back({tr("Done"), ""});
    const int i = ui::choose(tr("Advanced numbers (Enter to change one)"), items, static_cast<int>(items.size()) - 1,
                             [&](int k) { return k < static_cast<int>(advanced_list().size()) ? tr(advanced_list()[static_cast<std::size_t>(k)].help) : ""; });
    if (i < 0 || i == static_cast<int>(advanced_list().size())) return;
    const auto& x = advanced_list()[static_cast<std::size_t>(i)];
    ui::wrap(tr(x.help));
    const std::string v = ui::ask(fmt(tr("{} (empty = default {})"), {tr(x.label), num(default_of(x.key, a))}));
    if (v.empty()) {
      a.advanced.erase(x.key);
      continue;
    }
    try {
      std::size_t used = 0;
      const double d = std::stod(v, &used);
      if (used != v.size()) throw std::invalid_argument("trailing");
      BuildAnswers trial = a;
      trial.advanced[x.key] = d;
      std::string error;
      if (!gdc::score::parse(preset_text(trial), error)) {
        ui::warn(fmt(tr("Not accepted: {}"), {error}));
        continue;
      }
      a.advanced[x.key] = d;
    } catch (const std::exception&) {
      ui::warn(tr("Type a number, like 0.5."));
    }
  }
}

// Class trees' attack skills, as auto picks, plus weapon attacks.
std::vector<std::string> main_skill_choices(const Ctx& c, const std::vector<int>& masteries, std::vector<ui::Item>& items) {
  std::vector<std::string> records{kDefaultAttack};
  items = {{tr("Weapon attacks"), tr("your plain attack and weapon procs")}};
  for (const int m : masteries) {
    char b[80];
    std::snprintf(b, sizeof b, "records/skills/playerclass%02d/_classtree_class%02d.dbr", m, m);
    const gdc::arz::Record* tree = c.db.record(b);
    if (!tree) continue;
    std::vector<std::pair<int, std::string>> slots;
    for (const auto& [k, f] : tree->fields)
      if (k.starts_with("skillName") && !f.strings.empty() && !f.strings[0].empty()) {
        try {
          slots.emplace_back(std::stoi(k.substr(9)), f.strings[0]);
        } catch (const std::exception&) {
        }
      }
    std::sort(slots.begin(), slots.end());
    for (const auto& [_, r] : slots) {
      const auto kind = gdc::score::skill_kind(c.db, r, 1);
      if (!kind.main) continue;
      std::vector<std::string> names;
      for (const auto& k : kind.types) names.push_back(gdc::score::damage_name(k));
      std::string hint = mastery_name(c, m);
      if (kind.weapon) hint += tr(", weapon damage");
      if (!names.empty()) hint += ", " + damage_words(names);
      records.push_back(r);
      items.push_back({skill_name(c, r), hint});
    }
  }
  return records;
}

std::optional<std::vector<int>> pick_main_skills(const std::vector<ui::Item>& items) {
  for (;;) {
    auto r = ui::choose_many(tr("Which skills do most of your damage? (pick 1 to 4)"), items, {}, 4);
    if (!r) return std::nullopt;
    if (!r->empty()) return r;
    ui::warn(tr("Pick at least one; \"Weapon attacks\" if you mostly auto-attack."));
  }
}

void add_unique(std::vector<std::string>& out, const std::string& n) {
  if (std::find(out.begin(), out.end(), n) == out.end()) out.push_back(n);
}

// Skills' damage, weapon type, then conversions. False: back.
bool pick_damage(bool weapon, std::vector<std::string> dealt, BuildAnswers& a) {
  std::vector<ui::Item> type_items;
  for (const auto& t : base_types()) type_items.push_back({tr(t.label), ""});
  if (weapon) {
    const int w = ui::choose(tr("Weapon attacks deal your weapon's damage. What does your weapon mostly deal?"), type_items, 0);
    if (w < 0) return false;
    for (const auto& n : base_types()[static_cast<std::size_t>(w)].names) add_unique(dealt, n);
  }
  const int conv = ui::choose(fmt(tr("Your skills deal {}. Do items or skill modifiers convert it into another type?"), {damage_words(dealt)}),
                              {{tr("No, that is my damage"), ""}, {tr("Yes, I convert it"), tr("pick the type you end up with")}});
  if (conv < 0) return false;
  a.damage = dealt;
  if (conv != 1) return true;
  for (;;) {
    auto r = ui::choose_many(tr("Which damage do you end up dealing after conversion?"), type_items, {}, 3);
    if (!r) return false;
    if (r->empty()) continue;
    a.damage = types_of(*r);
    a.hints.push_back("Conversion: into " + damage_words(a.damage) + ".");
    return true;
  }
}

bool pick_playstyle(BuildAnswers& a) {
  const int ps = ui::choose(tr("How do you like to play?"),
                            {{tr("Balanced"), tr("damage and defense count the same")},
                             {tr("Tanky"), tr("defense counts more than damage")},
                             {tr("Glass cannon"), tr("damage counts more than defense")}});
  if (ps < 0) return false;
  a.playstyle = ps == 1 ? Playstyle::kTanky : ps == 2 ? Playstyle::kGlass : Playstyle::kBalanced;
  return true;
}

void ask_extras(BuildAnswers& a) {
  a.pets = ui::confirm(tr("Do your pets (Hellhound, Briarthorn, Skeletons, ...) do a big part of your damage?"), false);
  if (a.pets) {
    // As presets/pet.toml.
    a.other["pet_bonus"] = {1.0, {"pets"}};
    a.other["retaliation"] = {0, {}};
    a.other["retaliation_modifier"] = {0, {}};
    a.advanced["score.other"] = 0.35;
  }
  if (ui::confirm(tr("Is retaliation (damage back to attackers) a big part of your damage?"), false)) {
    // As presets/retaliation.toml.
    a.other["retaliation"] = {0.5, {}};
    a.other["retaliation_modifier"] = {0.5, {}};
  }
  if (ui::confirm(tr("Do you use a shield?"), false))
    a.hints.push_back("Uses a shield: block chance, block amount and recovery are scored in Survivability already.");
  if (ui::confirm(tr("Change advanced numbers? (most players skip this)"), false)) advanced(a);
}

}  // namespace

std::optional<BuildAnswers> custom(const Ctx& c, const std::vector<int>& masteries, const std::string& character) {
  BuildAnswers a;
  a.source = "custom";
  a.characters = {character};
  a.masteries = masteries;
  std::vector<ui::Item> items;
  const std::vector<std::string> records = main_skill_choices(c, masteries, items);
  const auto picked = pick_main_skills(items);
  if (!picked) return std::nullopt;
  bool weapon = false;
  std::vector<std::string> dealt;
  for (const int i : *picked) {
    const std::string& r = records[static_cast<std::size_t>(i)];
    a.skills.push_back(r);
    const auto kind = gdc::score::skill_kind(c.db, r, 1);
    weapon = weapon || kind.weapon || r == kDefaultAttack;
    for (const auto& k : kind.types) add_unique(dealt, gdc::score::damage_name(k));
  }
  if (!pick_damage(weapon, dealt, a) || !pick_playstyle(a)) return std::nullopt;
  ask_extras(a);
  std::string suggestion = class_label(c, masteries);
  if (const auto paren = suggestion.find(" ("); paren != std::string::npos) suggestion.resize(paren);
  a.title = ui::ask(tr("Name this build"), suggestion + " " + damage_words({a.damage.front()}));
  return a;
}

}  // namespace setup::wiz
