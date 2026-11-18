#pragma once
// Finds Grim Dawn; every candidate is checked on disk.
#include <filesystem>
#include <string>
#include <vector>

namespace setup {

struct Game {
  std::filesystem::path root;
  std::string source;  // "Steam", "Steam (Flatpak)", "GOG", "Heroic", "folder scan", "typed in"
};

// Steam roots that exist, Linux and Windows.
std::vector<std::filesystem::path> steam_roots();
bool flatpak_steam(const std::filesystem::path& steam_root);

bool is_game_dir(const std::filesystem::path& root);
// Typed folder normalized to the game root; empty if invalid.
std::filesystem::path game_root_of(const std::filesystem::path& typed);
std::vector<Game> find_games();

// Subfolders with a loadable x64 Grim Dawn.exe: "x64", "compat".
std::vector<std::string> run_folders(const std::filesystem::path& root);

}  // namespace setup
