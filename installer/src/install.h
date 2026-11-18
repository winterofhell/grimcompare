#pragma once
// Install, update, repair, uninstall; only files we installed and that are unchanged are ours.
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace setup {

// Ultimate ASI Loader x64 v9.7.4, pinned in scripts/deploy.sh.
inline constexpr std::string_view kUalSha256 = "fa266e3513d02c08a1b808f28c10538a489eaffaa4b0707f7cc1066e71b5afd7";
inline constexpr std::string_view kManifest = "GDCompare.installed";

struct Payload {
  std::filesystem::path dir;               // GDCompare.asi, dinput8.dll, dinput8.ini, presets/, catalog/
  std::map<std::string, std::string> files;  // install name ("GDCompare.presets/pet.toml") -> sha256
  std::map<std::string, std::filesystem::path> sources;
};
// Checks every file exists and dinput8.dll is the pinned loader.
std::optional<Payload> load_payload(const std::filesystem::path& dir, std::string& error);

std::map<std::string, std::string> read_manifest(const std::filesystem::path& target);

// dinput8.dll identity: UAL product name and PE machine.
struct LoaderInfo {
  bool ual = false, x64 = false;
  std::string product, version;
};
LoaderInfo inspect_loader(const std::filesystem::path& dll);
// [globalsets] dontloadfromdllmain=0 (adapter/ual/dinput8.ini says why the game needs it).
bool ini_loads_in_dllmain(const std::filesystem::path& ini);

enum class Mode { kInstall, kUninstall };

struct Step {
  enum class Kind { kWrite, kKeep, kRemove };
  Kind kind = Kind::kKeep;
  std::string file;    // relative to the target, '/' separated
  std::string reason;  // plain words for the summary
  bool backup = false;
};

struct Plan {
  Mode mode = Mode::kInstall;
  std::filesystem::path target;
  std::vector<Step> steps;
  std::vector<std::string> blockers;  // nothing is written while there is one
  std::vector<std::string> notes;
  std::map<std::string, std::string> manifest;  // after the plan is applied; empty = remove the manifest
  bool changes() const;
};

// payload unused for kUninstall; game_running nullopt blocks too.
Plan make_plan(Mode mode, const std::filesystem::path& target, const Payload* payload, std::optional<bool> game_running);

// Temp file, rename; on error stops, manifest lists what's done.
bool apply(const Plan& plan, const Payload* payload, std::vector<std::string>& done, std::string& error);

}  // namespace setup
