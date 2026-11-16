toml++ 3.4.0, single-header release file from
https://raw.githubusercontent.com/marzer/tomlplusplus/v3.4.0/toml.hpp
(sha256 6b5172ad4dd6519aec67b919181fa7a38a2234131e5b2afa232dfe444819783e). MIT, see LICENSE.
Local patches (search "grimCompare patch"), all found by tests/test_score.cpp "malformed presets" under UBSan:
- TOML_ASSERT_ASSUME no longer becomes TOML_ASSUME under NDEBUG: malformed input reaches asserted states
  ("[[[x", "[1 }"), which was undefined behavior in release builds.
- is_non_ascii_horizontal_whitespace and the bare-key-character table returned through TOML_UNREACHABLE for code
  points outside their cases: now false.
gdc_toml.hpp is the one configuration (no exceptions, no formatters, no asserts) every user includes.
Used by core/src/score.cpp and installer/src/catalog.cpp to parse presets from a string (no file or process access).
