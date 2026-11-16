#include "wizard.h"

#include <algorithm>
#include <fstream>
#include <map>
#include <set>
#include <sstream>

#include "buildpreset.h"
#include "catalog.h"
#include "gdc/arz.h"
#include "gdc/labels.h"
#include "gdc/score.h"
#include "gdc/snapshot.h"
#include "gdc/text.h"
#include "platform.h"
#include "saves.h"
#include "steam.h"
#include "tui.h"

namespace setup {

namespace fs = std::filesystem;
using ui::fmt;
using ui::tr;

namespace {

constexpr const char* kDefaultAttack = "records/skills/default/defaultweaponattack.dbr";
constexpr std::size_t kAdapterMaxPresets = 32;  // adapter/src/tooltip.cpp kMaxNamed

struct Ctx {
  const gdc::arz::Database& db;
  const gdc::text::Table& text;
  gdc::Labels labels;
  fs::path presets;  // <run folder>/GDCompare.presets
};

std::optional<std::string> read_file(const fs::path& p) {
  std::ifstream f(p, std::ios::binary);
  if (!f) return std::nullopt;
  std::stringstream ss;
  ss << f.rdbuf();
  return ss.str();
}

bool write_file(const fs::path& p, const std::string& text, std::string& error) {
  std::error_code ec;
  fs::create_directories(p.parent_path(), ec);
  fs::path tmp = p;
  tmp += ".gdc-tmp";
  {
    std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
    f << text;
    if (!f) return error = "could not write " + u8string(tmp), false;
  }
  fs::rename(tmp, p, ec);
  if (ec) return error = "could not write " + u8string(p) + ": " + ec.message(), false;
  return true;
}

std::string mastery_name(const Ctx& c, int m) {
  char b[4];
  std::snprintf(b, sizeof b, "%02d", m);
  return class_name(c.text, b);
}

std::string class_label(const Ctx& c, const std::vector<int>& masteries) {
  if (masteries.empty()) return tr("no mastery yet");
  std::string names;
  for (const int m : masteries) names += (names.empty() ? "" : " + ") + mastery_name(c, m);
  if (masteries.size() == 2) return class_name(c.text, class_code(masteries)) + " (" + names + ")";
  return names;
}

std::string skill_name(const Ctx& c, const std::string& record) {
  if (record == kDefaultAttack) return tr("weapon attacks");
  const std::string n = c.labels.skill(record);
  return n.empty() ? gdc::record_stem(record) : n;
}

std::string damage_words(const std::vector<std::string>& names) {
  std::string out;
  for (const auto& n : names)
    if (const auto k = gdc::score::damage_key(n)) out += (out.empty() ? "" : ", ") + gdc::score::damage_label(*k);
  return out;
}

// ------------------------------------------------------------------ character and masteries

struct Who {
  std::string name;
  std::vector<int> masteries;
  bool from_save = false;
  std::string save;  // gdc::snap::save_key; empty for a typed name
};

bool is_backup(const std::string& save) { return save.find("/__") != std::string::npos; }

// "winterofhell", or "winterofhell (backup copy)" for the engine's __<name> folder.
std::string who_label(const Who& w) { return is_backup(w.save) ? fmt(tr("{} (backup copy)"), {w.name}) : w.name; }

std::optional<Who> pick_character(const Ctx& c, const Game& game) {
  std::vector<fs::path> libraries;
  const auto roots = steam_roots();
  for (const auto& r : roots)
    for (const auto& l : steam_libraries(r)) libraries.push_back(l);
  libraries.push_back(game.root.parent_path().parent_path().parent_path());  // <library>/steamapps/common/Grim Dawn
  const auto chars = find_characters(save_roots(roots, libraries));
  std::vector<ui::Item> items;
  // "cloud/<account>" or "local" per name: shown only when one name is in more than one.
  auto root = [](const std::string& save) { return save.substr(0, save.starts_with("cloud/") ? save.find('/', 6) : 5); };
  std::map<std::string, std::set<std::string>> roots_of;
  for (const auto& ch : chars) roots_of[ch.header.name].insert(root(ch.save));
  for (const auto& ch : chars) {
    const auto m = class_masteries(ch.header.class_tag);
    std::string where;
    if (ch.save.find("/user/") != std::string::npos) where += tr(", custom game");
    if (is_backup(ch.save)) where += tr(", backup copy");
    if (roots_of[ch.header.name].size() > 1)
      where += ch.save.starts_with("cloud/") ? fmt(tr(", Steam account {}"), {root(ch.save).substr(6)}) : tr(", local save");
    items.push_back({ch.header.name, fmt(tr("{}, level {}{}{}"), {class_label(c, m), std::to_string(ch.header.level),
                                                                     ch.header.hardcore ? tr(", hardcore") : "", where})});
  }
  items.push_back({tr("Type a character name"), tr("for a character not listed")});
  int i = static_cast<int>(chars.size());
  if (!chars.empty()) {
    i = ui::choose(tr("Which character is this for? (read from your saves, nothing is changed)"), items, 0, {}, chars.size() > 8);
    if (i < 0) return std::nullopt;
  } else {
    ui::line(tr("No saved characters found."));
  }
  if (i < static_cast<int>(chars.size())) {
    const auto& ch = chars[static_cast<std::size_t>(i)];
    return Who{ch.header.name, class_masteries(ch.header.class_tag), true, ch.save};
  }
  for (;;) {
    const std::string n = ui::ask(tr("Character name, exactly as in game (empty to go back)"));
    if (n.empty()) return std::nullopt;
    return Who{n, {}, false, {}};
  }
}

std::optional<std::vector<int>> pick_masteries(const Ctx& c, const Who& w) {
  if (!w.masteries.empty()) {
    const int i = ui::choose(fmt(tr("{} plays {}."), {w.name, class_label(c, w.masteries)}),
                             {{tr("Yes, use these masteries"), ""}, {tr("Change them"), tr("for a planned second mastery")}});
    if (i < 0) return std::nullopt;
    if (i == 0) return w.masteries;
  }
  std::vector<ui::Item> items;
  for (int m = 1; m <= 10; ++m) items.push_back({mastery_name(c, m), ""});
  std::vector<int> picked;
  for (const int m : w.masteries) picked.push_back(m - 1);
  for (;;) {
    auto r = ui::choose_many(fmt(tr("Masteries of {} (pick one or two)"), {w.name}), items, picked, 2);
    if (!r) return std::nullopt;
    if (r->empty()) {
      ui::warn(tr("Pick at least one mastery."));
      continue;
    }
    std::vector<int> out;
    for (const int i : *r) out.push_back(i + 1);
    return out;
  }
}

// ------------------------------------------------------------------ catalog

std::string panel(const Ctx& c, const Entry& e) {
  std::string classes;
  for (const auto& code : e.classes) classes += (classes.empty() ? "" : ", ") + class_name(c.text, code);
  std::string skills;
  for (const auto& s : e.skills)
    if (s != kDefaultAttack) skills += (skills.empty() ? "" : ", ") + skill_name(c, s);
  std::string sets;
  for (const auto& s : e.sets) sets += (sets.empty() ? "" : ", ") + c.labels.set(s);
  std::string out = e.name + "\n\n" + tr("Classes: ") + classes + "\n" + tr("Damage: ") + damage_words(e.damage) + "\n";
  if (!skills.empty()) out += tr("Main skills: ") + skills + "\n";
  if (!e.conversion.empty()) out += tr("Conversion: ") + e.conversion + "\n";
  if (!e.stats.empty()) out += tr("Key stats: ") + e.stats + "\n";
  if (!e.items.empty()) out += tr("Key items: ") + e.items + "\n";
  if (!sets.empty()) out += tr("Target sets: ") + sets + "\n";
  return out;
}

std::optional<Entry> pick_entry(const Ctx& c, const std::vector<Entry>& catalog, const std::vector<int>& masteries) {
  static const std::map<std::string, std::string> kLabel = {
      {"melee", "Melee"}, {"ranged", "Ranged"}, {"caster", "Caster"}, {"pets", "Pets"}, {"hybrid", "Hybrid"}};
  bool all = false;
  for (;;) {
    std::vector<ui::Item> cats;
    std::vector<std::string> keys;
    for (const auto cat : kCategories) {
      const auto n = std::count_if(catalog.begin(), catalog.end(), [&](const Entry& e) { return e.category == cat && (all || fits(e, masteries)); });
      if (n == 0) continue;
      cats.push_back({tr(kLabel.at(std::string(cat))), n == 1 ? tr("1 build") : fmt(tr("{} builds"), {std::to_string(n)})});
      keys.emplace_back(cat);
    }
    cats.push_back({all ? fmt(tr("Only builds for {}"), {class_label(c, masteries)}) : tr("Show builds of every class"), ""});
    const int i = ui::choose(all ? tr("Builds of every class") : fmt(tr("Builds for {}"), {class_label(c, masteries)}), cats);
    if (i < 0) return std::nullopt;
    if (i == static_cast<int>(keys.size())) {
      all = !all;
      continue;
    }
    std::vector<const Entry*> shown;
    std::vector<ui::Item> items;
    for (const auto& e : catalog)
      if (e.category == keys[static_cast<std::size_t>(i)] && (all || fits(e, masteries))) {
        shown.push_back(&e);
        items.push_back({e.name, damage_words(e.damage)});
      }
    const int b = ui::choose(tr("Pick a build"), items, 0,
                             [&](int k) { return panel(c, *shown[static_cast<std::size_t>(k)]); }, true);
    if (b >= 0) return *shown[static_cast<std::size_t>(b)];
  }
}

// ------------------------------------------------------------------ custom

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

std::optional<BuildAnswers> custom(const Ctx& c, const std::vector<int>& masteries, const std::string& character) {
  BuildAnswers a;
  a.source = "custom";
  a.characters = {character};
  a.masteries = masteries;

  // Main skills: the class trees' attack skills (as the mod's auto mode would pick them), plus weapon attacks.
  std::vector<std::string> records{kDefaultAttack};
  std::vector<ui::Item> items{{tr("Weapon attacks"), tr("your plain attack and weapon procs")}};
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
  std::vector<int> picked;
  for (;;) {
    auto r = ui::choose_many(tr("Which skills do most of your damage? (pick 1 to 4)"), items, picked, 4);
    if (!r) return std::nullopt;
    if (r->empty()) {
      ui::warn(tr("Pick at least one; \"Weapon attacks\" if you mostly auto-attack."));
      continue;
    }
    picked = *r;
    break;
  }
  bool weapon = false;
  std::vector<std::string> dealt;
  for (const int i : picked) {
    const std::string& r = records[static_cast<std::size_t>(i)];
    a.skills.push_back(r);
    const auto kind = gdc::score::skill_kind(c.db, r, 1);
    weapon = weapon || kind.weapon || r == kDefaultAttack;
    for (const auto& k : kind.types)
      if (const std::string n = gdc::score::damage_name(k); std::find(dealt.begin(), dealt.end(), n) == dealt.end()) dealt.push_back(n);
  }
  std::vector<ui::Item> type_items;
  for (const auto& t : base_types()) type_items.push_back({tr(t.label), ""});
  if (weapon) {
    const int w = ui::choose(tr("Weapon attacks deal your weapon's damage. What does your weapon mostly deal?"), type_items, 0);
    if (w < 0) return std::nullopt;
    for (const auto& n : base_types()[static_cast<std::size_t>(w)].names)
      if (std::find(dealt.begin(), dealt.end(), n) == dealt.end()) dealt.push_back(n);
  }
  const int conv = ui::choose(fmt(tr("Your skills deal {}. Do items or skill modifiers convert it into another type?"), {damage_words(dealt)}),
                              {{tr("No, that is my damage"), ""}, {tr("Yes, I convert it"), tr("pick the type you end up with")}});
  if (conv < 0) return std::nullopt;
  a.damage = dealt;
  if (conv == 1) {
    for (;;) {
      auto r = ui::choose_many(tr("Which damage do you end up dealing after conversion?"), type_items, {}, 3);
      if (!r) return std::nullopt;
      if (r->empty()) continue;
      a.damage = types_of(*r);
      a.hints.push_back("Conversion: into " + damage_words(a.damage) + ".");
      break;
    }
  }

  const int ps = ui::choose(tr("How do you like to play?"),
                            {{tr("Balanced"), tr("damage and defense count the same")},
                             {tr("Tanky"), tr("defense counts more than damage")},
                             {tr("Glass cannon"), tr("damage counts more than defense")}});
  if (ps < 0) return std::nullopt;
  a.playstyle = ps == 1 ? Playstyle::kTanky : ps == 2 ? Playstyle::kGlass : Playstyle::kBalanced;

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

  std::string suggestion = class_label(c, masteries);
  if (const auto paren = suggestion.find(" ("); paren != std::string::npos) suggestion.resize(paren);
  a.title = ui::ask(tr("Name this build"), suggestion + " " + damage_words({a.damage.front()}));
  return a;
}

// ------------------------------------------------------------------ save

struct Existing {
  fs::path file;
  std::string text;
  gdc::score::Preset preset;  // default when it does not parse: binds no one
};

bool applies(const Existing& e, const Who& w) { return gdc::score::binds(e.preset, w.name, w.save); }

std::vector<Existing> setup_presets(const fs::path& dir) {
  std::vector<Existing> out;
  std::error_code ec;
  for (const auto& e : fs::directory_iterator(dir, ec)) {
    if (e.path().extension() != ".toml") continue;
    auto text = read_file(e.path());
    if (!text || !made_by_setup(*text)) continue;
    std::string error;
    const auto p = gdc::score::parse(*text, error);
    out.push_back({e.path(), *text, p ? *p : gdc::score::Preset{}});
  }
  std::sort(out.begin(), out.end(), [](const Existing& a, const Existing& b) { return a.file < b.file; });
  return out;
}

// One setup preset per character: the others drop it (a preset left for no one is removed, after asking).
// Dropping a save keeps the name while another listed save of that name still uses the preset.
bool unbind_elsewhere(const Ctx& c, const Who& who, const fs::path& keep) {
  const std::string character = who_label(who);
  for (auto& e : setup_presets(c.presets)) {
    if (e.file == keep || !applies(e, who)) continue;
    std::vector<std::string> saves = e.preset.match_saves, rest = e.preset.match_characters;
    std::erase_if(saves, [&](const std::string& s) {
      return who.save.empty() ? gdc::snap::save_key_name(s) == who.name : gdc::score::same_save_key(s, who.save);
    });
    if (std::none_of(saves.begin(), saves.end(), [&](const std::string& s) { return gdc::snap::save_key_name(s) == who.name; }))
      std::erase(rest, who.name);
    std::string error;
    if (rest.empty()) {
      if (ui::confirm(fmt(tr("{} was only for {}. Delete it?"), {u8string(e.file.filename()), character}), true)) {
        std::error_code ec;
        fs::remove(e.file, ec);
        if (ec) return ui::warn(fmt(tr("Could not delete {}: {}"), {u8string(e.file), ec.message()})), false;
        ui::line(fmt(tr("Deleted {}."), {u8string(e.file.filename())}));
      } else {
        ui::warn(fmt(tr("{} still lists {}; the preset tried first by file name wins."), {u8string(e.file.filename()), character}));
      }
      continue;
    }
    const auto next = with_binding(e.text, rest, saves);
    if (!next || !write_file(e.file, *next, error)) return ui::warn(fmt(tr("Could not update {}: {}"), {u8string(e.file), error})), false;
    ui::line(fmt(tr("{} no longer applies to {}."), {u8string(e.file.filename()), character}));
  }
  return true;
}

bool save(const Ctx& c, const BuildAnswers& a, const Who& who) {
  const std::string text = preset_text(a);
  std::string error;
  if (!gdc::score::parse(text, error)) {
    ui::warn(fmt(tr("The preset did not pass the check ({}); nothing was saved. Please report this."), {error}));
    return false;
  }
  // One file per character and build: "winterofhell-dw-cold-acid-3".
  std::string stem = preset_stem(who.name + (is_backup(who.save) ? "-backup-" : "-") + (a.source == "custom" ? a.title : a.source));
  for (;;) {
    stem = preset_stem(ui::ask(tr("File name"), stem));
    const fs::path p = c.presets / (stem + ".toml");
    if (const auto old = read_file(p)) {
      if (!made_by_setup(*old)) {
        ui::warn(fmt(tr("{} exists and was not made by setup. Pick another name."), {u8string(p.filename())}));
        stem += "-2";
        continue;
      }
      std::string error_old;
      const auto old_preset = gdc::score::parse(*old, error_old);
      if (old_preset && !old_preset->match_characters.empty() && !gdc::score::binds(*old_preset, who.name, who.save)) {
        ui::warn(fmt(tr("{} is in use for another character. Pick another name."), {u8string(p.filename())}));
        stem += "-2";
        continue;
      }
      if (!ui::confirm(fmt(tr("{} exists. Replace it?"), {u8string(p.filename())}), true)) continue;
    }
    if (!unbind_elsewhere(c, who, p)) return false;
    if (!write_file(p, text, error)) return ui::warn(error), false;
    ui::title(fmt(tr("Saved {}"), {u8string(p)}));
    std::size_t n = 0;
    std::error_code ec;
    for (const auto& e : fs::directory_iterator(c.presets, ec)) n += e.path().extension() == ".toml";
    if (n > kAdapterMaxPresets)
      ui::warn(fmt(tr("GDCompare.presets has {} presets; the mod reads only {} of them. Delete the ones you no longer use."),
                   {std::to_string(n), std::to_string(kAdapterMaxPresets)}));
    ui::wrap(fmt(tr("GD Compare now scores items for {} with \"{}\". The mod picks up preset changes on its own, even while "
                    "the game runs. Run setup again any time to change it or use it for another character."),
                 {who_label(who), a.title}));
    return true;
  }
}

bool bind_existing(const Ctx& c, const Existing& e, const Who& who) {
  const std::string character = who_label(who);
  if (applies(e, who)) {
    if (!unbind_elsewhere(c, who, e.file)) return false;
    ui::line(fmt(tr("{} stays in use for {}."), {u8string(e.file.filename()), character}));
    return true;
  }
  auto characters = e.preset.match_characters;
  auto saves = e.preset.match_saves;
  if (std::find(characters.begin(), characters.end(), who.name) == characters.end()) characters.push_back(who.name);
  if (!who.save.empty()) saves.push_back(who.save);
  const auto next = with_binding(e.text, characters, saves);
  std::string error;
  if (!next) return ui::warn(tr("That preset's [match] section was edited by hand; bind it by editing the file.")), false;
  if (!unbind_elsewhere(c, who, e.file) || !write_file(e.file, *next, error)) return error.empty() || (ui::warn(error), false);
  ui::line(fmt(tr("{} now applies to {}."), {u8string(e.file.filename()), character}));
  return true;
}

}  // namespace

WizardEnd wizard(const Game& game, const std::string& folder, const fs::path& payload_dir) {
  auto end = [](bool ok) { return ok ? WizardEnd::kSaved : WizardEnd::kFailed; };
  ui::title(tr("Build preset"));
  ui::line(tr("Reading the game database..."));
  std::optional<gdc::arz::Database> db;
  std::optional<gdc::text::Table> text;
  try {
    db = gdc::arz::Database::open_game(game.root);
    text = gdc::text::Table::open_game(game.root, "EN");
  } catch (const std::exception& e) {
    ui::warn(fmt(tr("Could not read the game database: {}. Check the game files in Steam (Properties -> Installed Files -> Verify) "
                    "or GOG Galaxy, then run setup again."),
                 {e.what()}));
    return WizardEnd::kFailed;
  }
  const auto catalog_text = read_file(payload_dir / "catalog" / "builds.toml");
  std::string error;
  const auto catalog = catalog_text ? load_catalog(*catalog_text, error) : std::nullopt;
  if (!catalog) ui::warn(fmt(tr("The build catalog is missing or broken ({}); only your own build can be set up."), {catalog_text ? error : "not found"}));
  Ctx c{*db, *text, gdc::Labels(&*db, &*text), game.root / u8path(folder) / "GDCompare.presets"};

  const auto who = pick_character(c, game);
  if (!who) return WizardEnd::kBack;
  const auto masteries = pick_masteries(c, *who);
  if (!masteries) return WizardEnd::kBack;

  for (;;) {
    std::vector<ui::Item> items;
    std::vector<int> kinds;  // 0 catalog, 1 custom, 2+ existing
    const auto existing = setup_presets(c.presets);
    if (catalog)
      items.push_back({tr("Pick a build from the catalog"), fmt(tr("{} builds and variants, checked against the game data"), {std::to_string(catalog->size())})}),
          kinds.push_back(0);
    items.push_back({tr("Describe my own build"), tr("a few questions")}), kinds.push_back(1);
    for (std::size_t i = 0; i < existing.size(); ++i) {
      const auto& e = existing[i];
      const bool bound = applies(e, *who);
      items.push_back({fmt(bound ? tr("Keep {}") : tr("Use {}"), {u8string(e.file.stem())}),
                       bound ? fmt(tr("in use for {}"), {who_label(*who)}) : tr("a preset you made before")});
      kinds.push_back(2 + static_cast<int>(i));
    }
    const int i = ui::choose(fmt(tr("How should GD Compare judge items for {}?"), {who_label(*who)}), items);
    if (i < 0) return WizardEnd::kBack;
    const int k = kinds[static_cast<std::size_t>(i)];
    if (k >= 2) return end(bind_existing(c, existing[static_cast<std::size_t>(k - 2)], *who));

    std::optional<BuildAnswers> a;
    if (k == 0) {
      const auto e = pick_entry(c, *catalog, *masteries);
      if (!e) continue;
      a = from_entry(*e, *masteries, who->name);
      if (!fits(*e, *masteries))
        ui::warn(fmt(tr("This build is for another class; its skills outside {} are left out."), {class_label(c, *masteries)}));
    } else {
      a = custom(c, *masteries, who->name);
      if (!a) continue;
    }
    if (!who->save.empty()) a->saves = {who->save};
    std::map<std::string, std::string> names;
    for (const auto& s : a->skills) names[s] = skill_name(c, s);
    ui::title(fmt(tr("{} for {}: your top priorities"), {a->title, who->name}));
    int n = 0;
    for (const auto& p : priorities(*a, names)) ui::wrap(std::to_string(++n) + ". " + p, 2);
    for (const auto& h : a->hints) ui::wrap(h, 2);
    if (ui::confirm(tr("\nSave this preset?"), true)) return end(save(c, *a, *who));
  }
}

}  // namespace setup
