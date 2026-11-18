#pragma once
// Informational: an unknown hash only warns; the export table decides.
#include <string_view>

namespace gdc {

struct KnownBuild {
  std::string_view module;
  std::string_view variant;
  std::string_view version;  // Engine::GetVersion()
  std::string_view sha256;
};

// Steam buildids 25813250 (1.3.1.1), 26655545 (1.3.1.2).
inline constexpr KnownBuild kKnownBuilds[] = {
    {"Game.dll", "x64+compat", "v1.3.1.1 (x64)", "cd7ecd7942d719f96b42aff4896f8c25d52152b4a41191f71a4a66ea48cecad2"},
    {"Engine.dll", "x64", "v1.3.1.1 (x64)", "4c2ea14ce1c591b315cc29a8a96819a5c3c7e194adbd6f7eea208b9bb8a93153"},
    {"Engine.dll", "compat", "v1.3.1.1 (x64)", "6dbc7458e2c4a29c8f98d2a320d4b4f9a226c35b0f18628bde0d245e89c792c9"},
    {"Game.dll", "x64+compat", "v1.3.1.2 (x64)", "4e259d4f44d11b25086a975881f2a02c16b76c079a4cb3a3496b69b3bc7aa6f3"},
    {"Engine.dll", "x64", "v1.3.1.2 (x64)", "b0881e4e2f7bc689871cca2310adb589b35731972412561fe59d05b1cf20b63c"},
    {"Engine.dll", "compat", "v1.3.1.2 (x64)", "ad7fdc9537a5e690f071ab7be8e048110e0c6d3b69e80f6da38b4afe87120ffe"},
};

const KnownBuild* find_known_build(std::string_view module, std::string_view sha256);

}  // namespace gdc
