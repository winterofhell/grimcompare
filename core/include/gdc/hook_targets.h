#pragma once
// Every Game.dll / Engine.dll symbol the adapter plans to call, detour, or read,
// resolved by exported (MSVC-mangled) name. Source of names: export tables of
// v1.3.1.1, dumped in re/Game.exports.tsv and re/Engine.exports.tsv.
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "gdc/pe_exports.h"

namespace gdc::targets {

enum class Module : std::uint8_t { Game, Engine };
// Call: we call it. Detour: we hook it (needs a unique export, see `Folded`). Data: exported variable.
enum class Kind : std::uint8_t { Call, Detour, Data };

struct Target {
  Module module;
  Kind kind;
  bool required;
  std::string_view group;
  std::string_view label;
  std::string_view symbol;
};

std::span<const Target> planned();

enum class Problem : std::uint8_t {
  None,
  Missing,    // no export with this name
  Forwarded,  // export forwards to another DLL; not code in this module
  Folded,     // detour target shares its RVA with other exports (linker ICF): hooking would hit them too
};

struct Resolution {
  const Target* target;
  std::uint32_t rva = 0;
  std::uint32_t aliases = 0;  // exports sharing this RVA, including itself
  Problem problem = Problem::None;
};

struct Report {
  std::vector<Resolution> items;
  std::size_t required_failed = 0;
  std::size_t optional_failed = 0;
  bool enabled() const { return required_failed == 0; }
};

Report resolve(std::span<const Target> targets, std::span<const pe::Export> game,
               std::span<const pe::Export> engine);

// One line per target with absolute addresses (module base + RVA), then a summary line.
std::string format(const Report& r, std::uint64_t game_base, std::uint64_t engine_base);

std::string_view to_string(Module m);
std::string_view to_string(Kind k);
std::string_view to_string(Problem p);

}  // namespace gdc::targets
