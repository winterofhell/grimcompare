// gdcompare-setup: installs GD Compare into the folder Grim Dawn runs from and writes build presets.
// usage: gdcompare-setup [--plain] [--payload DIR] [--game DIR] [--wizard]
//        gdcompare-setup launch-options add|remove [--steam DIR] [--game-folder DIR]   (scripts/deploy.sh)
#include <algorithm>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <set>
#include <sstream>

#include "catalog.h"
#include "detect.h"
#include "install.h"
#include "platform.h"
#include "steam.h"
#include "tui.h"
#include "wizard.h"

namespace fs = std::filesystem;
using namespace setup;
using ui::fmt;
using ui::tr;

namespace {

constexpr std::string_view kVersion = "1.0.0";

struct Options {
  bool plain = false, wizard_only = false;
  fs::path payload, game;
};

fs::path find_payload(const Options& o) {
  if (!o.payload.empty()) return o.payload;
  const fs::path exe = exe_dir();
  for (const fs::path& p : {exe.parent_path() / "payload", exe / "payload"})
    if (std::error_code ec; fs::is_directory(p, ec)) return p;
  return exe.parent_path() / "payload";
}

// Under the home folder as ~/... (Linux): the full Steam path alone fills an 80-column line.
std::string short_path(const fs::path& p) {
  const std::string s = u8string(p);
#ifndef _WIN32
  const std::string h = u8string(home());
  if (!h.empty() && h != "/" && s.starts_with(h + "/")) return "~" + s.substr(h.size());
#endif
  return s;
}

std::string default_folder(const fs::path& root) {
  const auto folders = run_folders(root);
  for (const auto& f : folders)
    if (std::error_code ec; fs::exists(root / f / kManifest, ec)) return f;
#ifdef _WIN32
  const char* preferred = "x64";
#else
  const char* preferred = "compat";
#endif
  if (std::find(folders.begin(), folders.end(), preferred) != folders.end()) return preferred;
  return folders.empty() ? "" : folders.front();
}

std::optional<Game> pick_game(const Options& o) {
  if (!o.game.empty()) {
    const fs::path root = game_root_of(o.game);
    if (root.empty()) {
      ui::warn(fmt(tr("{} is not a Grim Dawn folder: it has no Grim Dawn.exe and database/database.arz. Give the game's main "
                      "folder (the one that holds Grim Dawn.exe and the x64 folder)."),
                   {u8string(o.game)}));
      return std::nullopt;
    }
    return Game{root, "typed in"};
  }
  const auto games = find_games();
  if (games.size() == 1) return games.front();
  if (!games.empty()) {
    std::vector<ui::Item> items;
    for (const auto& g : games) items.push_back({u8string(g.root), g.source});
    items.push_back({tr("Type another folder"), ""});
    const int i = ui::choose(tr("Grim Dawn is installed in more than one place. Which one do you play?"), items);
    if (i < 0) return std::nullopt;
    if (i < static_cast<int>(games.size())) return games[static_cast<std::size_t>(i)];
  } else {
    ui::line(tr("Could not find Grim Dawn in Steam, GOG or Heroic."));
  }
  for (;;) {
    const std::string typed = ui::ask(tr("Grim Dawn folder (the one with Grim Dawn.exe; empty to quit)"));
    if (typed.empty()) return std::nullopt;
    if (const fs::path root = game_root_of(u8path(typed)); !root.empty()) return Game{root, "typed in"};
    ui::warn(tr("That folder has no Grim Dawn.exe with database/database.arz next to it."));
  }
}

void print_plan(const Plan& p) {
  std::vector<const Step*> write, remove, keep;
  for (const auto& s : p.steps)
    (s.kind == Step::Kind::kWrite ? write : s.kind == Step::Kind::kRemove ? remove : keep).push_back(&s);
  ui::title(fmt(tr("In {}:"), {u8string(p.target)}));
  auto group = [](std::string_view head, const std::vector<const Step*>& v) {
    if (v.empty()) return;
    ui::line(std::string("  ") + std::string(head));
    for (const auto* s : v) ui::hang("    ", s->file + "  - " + s->reason);
  };
  group(tr("Write:"), write);
  group(tr("Remove:"), remove);
  group(tr("Leave as is:"), keep);
  for (const auto& n : p.notes) ui::wrap("Note: " + n, 2);
  for (const auto& b : p.blockers) ui::warn(b);
}

// Lower-case file names in folder (merge_launch_options looks for renodx*.addon64 there).
std::set<std::string> lower_names(const fs::path& folder) {
  std::set<std::string> out;
  std::error_code ec;
  for (const auto& e : fs::directory_iterator(folder, ec)) {
    std::string n = u8string(e.path().filename());
    for (char& c : n) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    out.insert(n);
  }
  return out;
}

#ifndef _WIN32
void steam_launch_options(bool add, const fs::path& folder) {
  const std::set<std::string> present = lower_names(folder);
  const auto fn = [&](const std::string& o) { return add ? merge_launch_options(o, folder.empty() ? nullptr : &present) : remove_launch_options(o); };
  bool any = false;
  for (const auto& root : steam_roots())
    for (const auto& u : steam_users(root)) {
      if (!u.options) continue;
      any = true;
      const std::string next = fn(*u.options);
      ui::line(fmt(tr("  Steam user {}:"), {u.id}));
      ui::line(fmt(tr("    now:    {}"), {u.options->empty() ? tr("(empty)") : *u.options}));
      ui::line(fmt(tr("    set to: {}{}"), {next, next == *u.options ? tr("   (already right)") : ""}));
    }
  if (!any) ui::line(fmt(tr("  No Steam user has Grim Dawn launch options yet. Set them to:\n    {}"), {fn("")}));
}
#endif

void after_install(const Game& g, const std::string& folder) {
#ifdef _WIN32
  (void)g;
  (void)folder;
  ui::wrap(tr("\nDone. Start Grim Dawn as usual and hover an item: the tooltip shows the score. Hold Shift for details."));
#else
  ui::title(tr("One more step: tell Wine to load the ASI loader."));
  if (g.source.starts_with("Steam")) {
    ui::wrap(tr("In Steam: Grim Dawn -> Properties -> Launch Options. Paste the \"set to\" line (it keeps what you have):"));
    steam_launch_options(true, g.root / u8path(folder));
    if (folder == "compat") ui::wrap(tr("Then start the game with the \"Compatibility Mode\" entry (it runs compat/Grim Dawn.exe)."));
    else ui::wrap(tr("Then start the game with the entry that runs x64/Grim Dawn.exe."));
  } else {
    ui::wrap(tr("Add this environment variable in your launcher's settings for Grim Dawn (Heroic and Lutris both have an "
                "\"Environment variables\" section). If WINEDLLOVERRIDES is already set, add dinput8=n,b to it with a ';'."));
    ui::line("    WINEDLLOVERRIDES=dinput8=n,b");
    ui::line(fmt(tr("The launcher must start {}/Grim Dawn.exe."), {folder}));
  }
  ui::wrap(tr("Hover an item in game: the tooltip shows the score. Hold Shift for details."));
#endif
}

bool run_plan(Mode mode, const Game& g, const std::string& folder, const Payload* payload) {
  const fs::path target = g.root / u8path(folder);
  const Plan plan = make_plan(mode, target, payload, game_running());
  print_plan(plan);
  if (!plan.blockers.empty()) {
    ui::line(tr("\nNothing was changed."));
    return false;
  }
  if (!plan.changes()) {
    ui::line(tr("\nEverything is already in place."));
    if (mode == Mode::kInstall) after_install(g, folder);
    return true;
  }
  if (!ui::confirm(mode == Mode::kInstall ? tr("\nWrite these files?") : tr("\nRemove these files?"), mode == Mode::kInstall)) {
    ui::line(tr("Nothing was changed."));
    return false;
  }
  std::vector<std::string> done;
  std::string error;
  const bool ok = apply(plan, payload, done, error);
  for (const auto& d : done) ui::line("  " + d);
  if (!ok) {
    ui::warn(error);
    ui::wrap(tr("Check that Grim Dawn is closed, the folder is not read-only and no antivirus holds the files, then run "
                "setup again: it finishes the install or undoes it."));
    return false;
  }
  if (mode == Mode::kInstall) {
    after_install(g, folder);
  } else {
    ui::wrap(tr("\nGD Compare is removed. Your own presets and backups (GDCompare.backup) are kept."));
#ifndef _WIN32
    if (g.source.starts_with("Steam")) {
      ui::line(tr("You can take dinput8 out of the Steam launch options again:"));
      steam_launch_options(false, {});
    } else {
      ui::line(tr("You can remove dinput8=n,b from WINEDLLOVERRIDES in your launcher."));
    }
#endif
  }
  return true;
}

int launch_options_cli(int argc, char** argv) {
  if (argc < 3 || (std::string_view(argv[2]) != "add" && std::string_view(argv[2]) != "remove")) {
    std::fprintf(stderr, "usage: gdcompare-setup launch-options add|remove [--steam DIR] [--game-folder DIR]\n");
    return 2;
  }
  const bool add = std::string_view(argv[2]) == "add";
  std::vector<fs::path> roots;
  fs::path folder;
  for (int i = 3; i + 1 < argc; i += 2) {
    if (std::string_view(argv[i]) == "--steam") roots.push_back(u8path(argv[i + 1]));
    else if (std::string_view(argv[i]) == "--game-folder") folder = u8path(argv[i + 1]);
    else return std::fprintf(stderr, "unknown option %s\n", argv[i]), 2;
  }
  if (roots.empty()) roots = steam_roots();
  const std::set<std::string> present = lower_names(folder);
  const auto fn = [&](const std::string& o) { return add ? merge_launch_options(o, folder.empty() ? nullptr : &present) : remove_launch_options(o); };
  bool found = false;
  for (const auto& r : roots)
    for (const auto& u : steam_users(r)) {
      if (!u.options) continue;
      found = true;
      const std::string next = fn(*u.options);
      std::printf("Steam user %s:\n  current : %s\n  set to  : %s%s\n", u.id.c_str(), u.options->empty() ? "(empty)" : u.options->c_str(),
                  next.c_str(), next == *u.options ? "   (already correct)" : "");
    }
  if (!found) std::printf("No Steam user has Grim Dawn launch options yet. Set to:\n  %s\n", fn("").c_str());
  return 0;
}

int run(const Options& o) {
  ui::title(fmt(tr("GD Compare {} setup"), {std::string(kVersion)}));
  ui::wrap(tr("Installs the item comparison mod into your Grim Dawn folder and sets up how it scores items for your build. "
              "It never touches your saves or the game's own files, and asks before it writes anything."));

  const fs::path payload_dir = find_payload(o);
  std::string payload_error;
  const std::optional<Payload> payload = load_payload(payload_dir, payload_error);
  if (!payload) ui::warn(fmt(tr("Install files are missing or damaged ({}). Download the release zip again."), {payload_error}));

  std::optional<Game> game = pick_game(o);
  if (!game) return 1;
  std::string folder = default_folder(game->root);
  if (folder.empty()) {
    ui::warn(tr("This Grim Dawn has no x64 or compat folder with Grim Dawn.exe. GD Compare needs the 64-bit game."));
    return 1;
  }
  if (o.wizard_only) return wizard(*game, folder, payload_dir) == WizardEnd::kFailed ? 1 : 0;

  for (;;) {
    const fs::path target = game->root / u8path(folder);
    const bool installed = !read_manifest(target).empty();
    std::vector<ui::Item> items;
    std::vector<int> actions;  // 0 install, 1 wizard, 2 uninstall, 3 folder, 4 quit
    if (payload) items.push_back({installed ? tr("Update or repair GD Compare") : tr("Install GD Compare"), ""}), actions.push_back(0);
    items.push_back({tr("Set up a build preset for a character"), tr("the build wizard")}), actions.push_back(1);
    if (installed) items.push_back({tr("Uninstall GD Compare"), ""}), actions.push_back(2);
    items.push_back({tr("Change folder"), tr("other game folder, or x64 / compat")}), actions.push_back(3);
    items.push_back({tr("Quit"), ""}), actions.push_back(4);
    const int i = ui::choose(fmt(tr("Grim Dawn: {} ({})\nThe mod goes into: {}/\n\nWhat would you like to do?"),
                                 {short_path(game->root), game->source, folder}),
                             items, 0, {}, false, true);
    const int a = i < 0 ? 4 : actions[static_cast<std::size_t>(i)];
    if (a == 4) return 0;
    bool shown = true;  // results on screen: wait before the menu clears them
    if (a == 0) {
      if (run_plan(Mode::kInstall, *game, folder, &*payload) && ui::confirm(tr("\nSet up a build preset for your character now?"), true))
        wizard(*game, folder, payload_dir);
    } else if (a == 1) {
      shown = wizard(*game, folder, payload_dir) != WizardEnd::kBack;
    } else if (a == 2) {
      run_plan(Mode::kUninstall, *game, folder, nullptr);
    } else if (a == 3) {
      const auto folders = run_folders(game->root);
      std::vector<ui::Item> fi;
      for (const auto& f : folders) fi.push_back({f + "/", f == "compat" ? tr("Steam's Compatibility Mode entry") : tr("the 64-bit game")});
      fi.push_back({tr("Another Grim Dawn folder"), ""});
      const auto now = std::find(folders.begin(), folders.end(), folder);
      const int f = ui::choose(tr("Where should the mod go?"), fi, static_cast<int>(now - folders.begin()));
      if (f < 0) continue;
      if (f < static_cast<int>(folders.size())) {
        folder = folders[static_cast<std::size_t>(f)];
        shown = false;
      } else if (auto g = pick_game(Options{})) {
        if (const std::string next = default_folder(g->root); !next.empty()) {
          game = g, folder = next;
          shown = false;
        } else {
          ui::warn(fmt(tr("{} has no x64 or compat folder with Grim Dawn.exe; staying with the folder before."), {u8string(g->root)}));
        }
      } else {
        shown = false;
      }
    }
    if (shown) ui::pause();
  }
}

}  // namespace

int main(int argc, char** argv) {
  if (argc >= 2 && std::string_view(argv[1]) == "launch-options") return launch_options_cli(argc, argv);
  Options o;
  for (int i = 1; i < argc; ++i) {
    const std::string_view a = argv[i];
    if (a == "--plain") o.plain = true;
    else if (a == "--wizard") o.wizard_only = true;
    else if (a == "--payload" && i + 1 < argc) o.payload = u8path(argv[++i]);
    else if (a == "--game" && i + 1 < argc) o.game = u8path(argv[++i]);
    else if (a == "--version") return std::printf("gdcompare-setup %s\n", std::string(kVersion).c_str()), 0;
    else {
      std::printf("usage: gdcompare-setup [--plain] [--payload DIR] [--game DIR] [--wizard]\n"
                  "       gdcompare-setup launch-options add|remove [--steam DIR] [--game-folder DIR]\n");
      return a == "--help" || a == "-h" ? 0 : 2;
    }
  }
  ui::init(o.plain);
  try {
    return run(o);
  } catch (const ui::Cancelled&) {
    ui::line(tr("\nCancelled. Nothing more was changed."));
    return 130;
  } catch (const std::exception& e) {
    ui::warn(fmt(tr("Setup stopped: {}. Nothing more was changed; run setup again. If it stops the same way, report this "
                    "message."),
                 {e.what()}));
    return 1;
  }
}
