#pragma once
#include <string>

#include "snapshot.h"

namespace gdc::adapter {

struct HookConfig {
  std::wstring mod_dir;
  std::string mod_version;
  std::string game_build;
};

bool hooks_install(const Resolve& resolve, const HookConfig& cfg);

}  // namespace gdc::adapter
