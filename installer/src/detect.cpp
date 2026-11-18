#include "detect.h"

#include <algorithm>
#include <fstream>

#include <nlohmann/json.hpp>

#include "platform.h"
#include "steam.h"

namespace setup {

namespace fs = std::filesystem;

namespace {

bool present(const fs::path& p) {
  std::error_code ec;
  return fs::exists(p, ec);
}

void add_unique(std::vector<fs::path>& v, const fs::path& p) {
  std::error_code ec;
  if (p.empty() || !fs::is_directory(p, ec)) return;
  const fs::path c = fs::weakly_canonical(p, ec);
  if (std::none_of(v.begin(), v.end(), [&](const fs::path& x) { return x == c; })) v.push_back(c);
}

void add_game(std::vector<Game>& out, const fs::path& root, const std::string& source) {
  if (!is_game_dir(root)) return;
  std::error_code ec;
  const fs::path c = fs::weakly_canonical(root, ec);
  if (std::none_of(out.begin(), out.end(), [&](const Game& g) { return g.root == c; })) out.push_back({c, source});
}

#ifndef _WIN32
// Heroic's GOG installed.json: any install_path is tried, then checked.
void heroic(std::vector<Game>& out, const fs::path& file) {
  std::ifstream f(file);
  if (!f) return;
  const nlohmann::json j = nlohmann::json::parse(f, nullptr, false);
  if (j.is_discarded() || !j.contains("installed") || !j["installed"].is_array()) return;
  for (const auto& e : j["installed"])
    if (e.is_object() && e.contains("install_path") && e["install_path"].is_string())
      add_game(out, u8path(e["install_path"].get<std::string>()), "Heroic");
}
#endif

}  // namespace

std::vector<fs::path> steam_roots() {
  std::vector<fs::path> out;
#ifdef _WIN32
  for (const auto& [hive, key, value] : {std::tuple{Hive::kCurrentUser, "Software\\Valve\\Steam", "SteamPath"},
                                         std::tuple{Hive::kLocalMachine, "SOFTWARE\\WOW6432Node\\Valve\\Steam", "InstallPath"},
                                         std::tuple{Hive::kLocalMachine, "SOFTWARE\\Valve\\Steam", "InstallPath"}})
    if (const auto p = registry_string(hive, key, value)) add_unique(out, u8path(*p));
  if (const auto pf = env("ProgramFiles(x86)")) add_unique(out, u8path(*pf) / "Steam");
#else
  const fs::path h = home();
  if (const auto xdg = env("XDG_DATA_HOME")) add_unique(out, fs::path(*xdg) / "Steam");
  add_unique(out, h / ".local/share/Steam");
  add_unique(out, h / ".steam/steam");
  add_unique(out, h / ".steam/root");
  add_unique(out, h / ".var/app/com.valvesoftware.Steam/.local/share/Steam");
  add_unique(out, h / ".var/app/com.valvesoftware.Steam/data/Steam");
#endif
  return out;
}

bool flatpak_steam(const fs::path& steam_root) {
  return u8string(steam_root).find("/.var/app/com.valvesoftware.Steam/") != std::string::npos;
}

bool is_game_dir(const fs::path& root) {
  return present(root / "database" / "database.arz") &&
         (present(root / "Grim Dawn.exe") || present(root / "x64" / "Grim Dawn.exe") || present(root / "compat" / "Grim Dawn.exe"));
}

fs::path game_root_of(const fs::path& typed) {
  if (is_game_dir(typed)) return typed;
  if (const fs::path up = typed.parent_path(); is_game_dir(up)) return up;
  return {};
}

std::vector<Game> find_games() {
  std::vector<Game> out;
  for (const auto& s : steam_roots())
    for (const auto& lib : steam_libraries(s))
      if (present(lib / "steamapps" / ("appmanifest_" + std::string(kAppId) + ".acf")))
        add_game(out, lib / "steamapps" / "common" / "Grim Dawn", flatpak_steam(s) ? "Steam (Flatpak)" : "Steam");
#ifdef _WIN32
  // GOG Galaxy: HKLM\SOFTWARE\WOW6432Node\GOG.com\Games\<id> path (not checked against a GOG install).
  const std::string gog = "SOFTWARE\\WOW6432Node\\GOG.com\\Games";
  for (const auto& id : registry_subkeys(Hive::kLocalMachine, gog))
    if (const auto p = registry_string(Hive::kLocalMachine, gog + "\\" + id, "path")) add_game(out, u8path(*p), "GOG");
  for (const char* drive : {"C:\\GOG Games\\Grim Dawn", "C:\\Program Files (x86)\\GOG Galaxy\\Games\\Grim Dawn"})
    add_game(out, u8path(drive), "GOG");
#else
  const fs::path h = home();
  heroic(out, h / ".config/heroic/gog_store/installed.json");
  heroic(out, h / ".var/app/com.heroicgameslauncher.hgl/config/heroic/gog_store/installed.json");
  // Lutris and custom Wine prefixes: search two levels under ~/Games.
  std::error_code ec;
  for (const auto& a : fs::directory_iterator(h / "Games", ec)) {
    add_game(out, a.path(), "folder scan");
    std::error_code ec2;
    for (const auto& b : fs::directory_iterator(a.path(), ec2)) add_game(out, b.path(), "folder scan");
  }
#endif
  return out;
}

std::vector<std::string> run_folders(const fs::path& root) {
  std::vector<std::string> out;
  for (const char* sub : {"x64", "compat"})
    if (present(root / sub / "Grim Dawn.exe")) out.emplace_back(sub);
  return out;
}

}  // namespace setup
