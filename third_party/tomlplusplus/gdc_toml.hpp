#pragma once
// The one toml++ configuration in this program (the macros must match in every file that includes it).
// No exceptions: the adapter must not unwind through the game. No asserts: malformed user presets reach states the
// parser asserts on; its error paths handle them (tests/test_score.cpp "malformed presets").
#define TOML_EXCEPTIONS 0
#define TOML_ENABLE_FORMATTERS 0
#define TOML_ASSERT(expr) static_assert(true)
#include "toml.hpp"
