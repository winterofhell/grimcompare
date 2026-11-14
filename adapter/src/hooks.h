#pragma once
#include <string>

#include "snapshot.h"

namespace gdc::adapter {

struct HookConfig {
  std::wstring mod_dir;  // ends with a path separator
  std::string mod_version;
  std::string game_build;
};

// Reads <mod_dir>GDCompare.ini, installs the GameEngine::Update and tooltip detours (MinHook).
// Requires snapshot_init. false (logged) → nothing hooked.
bool hooks_install(const Resolve& resolve, const HookConfig& cfg);

}  // namespace gdc::adapter
