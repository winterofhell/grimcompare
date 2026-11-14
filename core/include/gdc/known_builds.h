#pragma once
// Module hashes of builds the target table was checked against. Informational:
// an unknown hash only logs a warning; the export table decides enable/disable.
#include <string_view>

namespace gdc {

struct KnownBuild {
  std::string_view module;   // "Game.dll" / "Engine.dll"
  std::string_view variant;  // install folder the file came from
  std::string_view version;  // Engine::GetVersion() string of that build
  std::string_view sha256;   // lowercase hex
};

// sha256sum of the Steam install, buildid 25813250 (docs/RECON.md §1).
inline constexpr KnownBuild kKnownBuilds[] = {
    {"Game.dll", "x64+compat", "v1.3.1.1 (x64)", "cd7ecd7942d719f96b42aff4896f8c25d52152b4a41191f71a4a66ea48cecad2"},
    {"Engine.dll", "x64", "v1.3.1.1 (x64)", "4c2ea14ce1c591b315cc29a8a96819a5c3c7e194adbd6f7eea208b9bb8a93153"},
    {"Engine.dll", "compat", "v1.3.1.1 (x64)", "6dbc7458e2c4a29c8f98d2a320d4b4f9a226c35b0f18628bde0d245e89c792c9"},
};

// Case-insensitive on module name (Windows paths); exact on hash.
const KnownBuild* find_known_build(std::string_view module, std::string_view sha256);

}  // namespace gdc
