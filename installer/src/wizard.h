#pragma once
// Build wizard: character, masteries, build; writes a bound preset.
#include <filesystem>
#include <string>

#include "detect.h"

namespace setup {

enum class WizardEnd { kSaved, kBack, kFailed };
// folder: "x64" or "compat" under game.root.
WizardEnd wizard(const Game& game, const std::string& folder, const std::filesystem::path& payload_dir);

}  // namespace setup
