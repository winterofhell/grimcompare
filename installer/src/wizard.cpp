#include "wizard.h"

#include <algorithm>
#include <fstream>
#include <map>
#include <set>
#include <sstream>

#include "catalog.h"
#include "platform.h"
#include "saves.h"
#include "steam.h"
#include "tui.h"
#include "wizard_internal.h"

namespace setup {
namespace wiz {

using ui::fmt;
using ui::tr;

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

bool is_backup(const std::string& save) { return save.find("/__") != std::string::npos; }

// "winterofhell", or "winterofhell (backup copy)" for the engine's __<name> folder.
std::string who_label(const Who& w) { return is_backup(w.save) ? fmt(tr("{} (backup copy)"), {w.name}) : w.name; }

namespace {

std::optional<Who> pick_character(const Ctx& c, const Game& game) {
  std::vector<fs::path> libraries;
  const auto roots = steam_roots();
  for (const auto& r : roots)
    for (const auto& l : steam_libraries(r)) libraries.push_back(l);
  libraries.push_back(game.root.parent_path().parent_path().parent_path());  // <library>/steamapps/common/Grim Dawn
  const auto chars = find_characters(save_roots(roots, libraries));
  std::vector<ui::Item> items;
  // "cloud/<account>" or "local", shown when a name has several saves.
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

std::optional<std::vector<Entry>> read_catalog(const fs::path& payload_dir) {
  const auto catalog_text = read_file(payload_dir / "catalog" / "builds.toml");
  std::string error;
  auto catalog = catalog_text ? load_catalog(*catalog_text, error) : std::nullopt;
  if (!catalog) ui::warn(fmt(tr("The build catalog is missing or broken ({}); only your own build can be set up."), {catalog_text ? error : "not found"}));
  return catalog;
}

// 0 catalog, 1 custom, 2 + i existing[i]; -1 back.
int pick_way(const std::optional<std::vector<Entry>>& catalog, const std::vector<Existing>& existing, const Who& who) {
  std::vector<ui::Item> items;
  std::vector<int> kinds;
  if (catalog)
    items.push_back({tr("Pick a build from the catalog"), fmt(tr("{} builds and variants, checked against the game data"), {std::to_string(catalog->size())})}),
        kinds.push_back(0);
  items.push_back({tr("Describe my own build"), tr("a few questions")}), kinds.push_back(1);
  for (std::size_t i = 0; i < existing.size(); ++i) {
    const auto& e = existing[i];
    const bool bound = applies(e, who);
    items.push_back({fmt(bound ? tr("Keep {}") : tr("Use {}"), {u8string(e.file.stem())}),
                     bound ? fmt(tr("in use for {}"), {who_label(who)}) : tr("a preset you made before")});
    kinds.push_back(2 + static_cast<int>(i));
  }
  const int i = ui::choose(fmt(tr("How should GrimCompare judge items for {}?"), {who_label(who)}), items);
  return i < 0 ? -1 : kinds[static_cast<std::size_t>(i)];
}

void show_priorities(const Ctx& c, const BuildAnswers& a, const Who& who) {
  std::map<std::string, std::string> names;
  for (const auto& s : a.skills) names[s] = skill_name(c, s);
  ui::title(fmt(tr("{} for {}: your top priorities"), {a.title, who.name}));
  int n = 0;
  for (const auto& p : priorities(a, names)) ui::wrap(std::to_string(++n) + ". " + p, 2);
  for (const auto& h : a.hints) ui::wrap(h, 2);
}

}  // namespace
}  // namespace wiz

using namespace wiz;

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
  const auto catalog = read_catalog(payload_dir);
  Ctx c{*db, *text, gdc::Labels(&*db, &*text), game.root / u8path(folder) / "GDCompare.presets"};

  const auto who = pick_character(c, game);
  if (!who) return WizardEnd::kBack;
  const auto masteries = pick_masteries(c, *who);
  if (!masteries) return WizardEnd::kBack;

  for (;;) {
    const auto existing = setup_presets(c.presets);
    const int k = pick_way(catalog, existing, *who);
    if (k < 0) return WizardEnd::kBack;
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
    show_priorities(c, *a, *who);
    if (ui::confirm(tr("\nSave this preset?"), true)) return end(save(c, *a, *who));
  }
}

}  // namespace setup
