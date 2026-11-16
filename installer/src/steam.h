#pragma once
// Steam's text KeyValues files (libraryfolders.vdf, localconfig.vdf, appmanifest_*.acf) and Grim Dawn's launch
// options with `dinput8=n,b` merged into WINEDLLOVERRIDES (Wine loads the ASI loader's dinput8.dll only then).
// deploy.sh and uninstall.sh print them through `gdcompare-setup launch-options`. Never writes Steam files: Steam rewrites them while it runs.
#include <filesystem>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace setup {

inline constexpr std::string_view kAppId = "219990";

struct Vdf {
  std::map<std::string, std::string> values;  // keys lower-cased
  std::map<std::string, Vdf> children;

  const Vdf* child(std::string_view key) const;
  const Vdf* path(std::initializer_list<std::string_view> keys) const;
  std::optional<std::string> value(std::string_view key) const;
};
Vdf parse_vdf(std::string_view text);

// present: lower-case file names in the game folder; given, DXVK_HDR is dropped unless a renodx*.addon64 is there
// (RenoDX HDR). Every other variable and override (dxgi for a DLSS / ReShade mod) is kept as is.
std::string merge_launch_options(std::string_view opts, const std::set<std::string>* present = nullptr);
std::string remove_launch_options(std::string_view opts);

// UserLocalConfigStore/Software/Valve/Steam/apps/219990/LaunchOptions; nullopt: the app has no entry.
std::optional<std::string> launch_options(const Vdf& localconfig);

// Library folders listed in <steam>/steamapps/libraryfolders.vdf (the root itself first).
std::vector<std::filesystem::path> steam_libraries(const std::filesystem::path& steam_root);

struct SteamUser {
  std::string id;                       // userdata/<id>
  std::optional<std::string> options;   // nullopt: no Grim Dawn entry yet
};
std::vector<SteamUser> steam_users(const std::filesystem::path& steam_root);

}  // namespace setup
