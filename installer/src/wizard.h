#pragma once
// The build wizard: pick a character from the saves, its masteries, then a catalog build or a few questions, and
// write GDCompare.presets/<name>.toml bound to that character.
#include <filesystem>
#include <string>

#include "detect.h"

namespace setup {

// folder: "x64" or "compat" under game.root. false: stopped before saving.
bool wizard(const Game& game, const std::string& folder, const std::filesystem::path& payload_dir);

}  // namespace setup
