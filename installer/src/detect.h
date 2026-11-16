#pragma once
// Where Grim Dawn is installed. Every candidate is checked on disk (Grim Dawn.exe in the root, x64/ or compat/,
// plus database/database.arz), so a wrong guess from a registry key or launcher file is dropped, never used.
#include <filesystem>
#include <string>
#include <vector>

namespace setup {

struct Game {
  std::filesystem::path root;
  std::string source;  // "Steam", "Steam (Flatpak)", "GOG", "Heroic", "folder scan", "typed in"
};

// Steam roots that exist: Linux ~/.local/share/Steam, ~/.steam/steam, Flatpak ~/.var/app/com.valvesoftware.Steam;
// Windows HKCU Software\Valve\Steam SteamPath, HKLM WOW6432Node Valve\Steam InstallPath, Program Files (x86)\Steam.
std::vector<std::filesystem::path> steam_roots();
bool flatpak_steam(const std::filesystem::path& steam_root);

bool is_game_dir(const std::filesystem::path& root);
// A folder typed in: the game root, or x64/ or compat/ inside it (normalized to the root). Empty if neither.
std::filesystem::path game_root_of(const std::filesystem::path& typed);
std::vector<Game> find_games();

// Folders under the root that hold a Grim Dawn.exe the mod can load into (x64 build): "x64", "compat".
std::vector<std::string> run_folders(const std::filesystem::path& root);

}  // namespace setup
