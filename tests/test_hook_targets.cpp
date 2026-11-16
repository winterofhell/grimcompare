#include <doctest/doctest.h>

#include <set>

#include "gdc/hook_targets.h"
#include "gdc/known_builds.h"

using namespace gdc::targets;
using gdc::pe::Export;

namespace {

constexpr Target kTargets[] = {
    {Module::Game, Kind::Detour, true, "t", "Hooked", "?Hooked"},
    {Module::Game, Kind::Call, true, "t", "Called", "?Called"},
    {Module::Engine, Kind::Call, false, "t", "Version", "?Version"},
};

}  // namespace

TEST_CASE("all targets present and unique → enabled") {
  const std::vector<Export> game{{"?Hooked", 0x100}, {"?Called", 0x200}};
  const std::vector<Export> engine{{"?Version", 0x300}};
  const auto r = resolve(kTargets, game, engine);
  CHECK(r.enabled());
  CHECK(r.optional_failed == 0);
  CHECK(r.items[0].rva == 0x100);
  CHECK(r.items[2].rva == 0x300);
  CHECK(r.items[2].aliases == 1);
}

TEST_CASE("names are looked up in the target's own module") {
  const std::vector<Export> game{{"?Hooked", 0x100}, {"?Called", 0x200}, {"?Version", 0x300}};
  const auto r = resolve(kTargets, game, {});
  CHECK(r.enabled());
  CHECK(r.optional_failed == 1);
  CHECK(r.items[2].problem == Problem::Missing);
}

TEST_CASE("missing required export disables") {
  const std::vector<Export> game{{"?Hooked", 0x100}};
  const auto r = resolve(kTargets, game, std::vector<Export>{{"?Version", 1}});
  CHECK_FALSE(r.enabled());
  CHECK(r.required_failed == 1);
  CHECK(r.items[1].problem == Problem::Missing);
}

TEST_CASE("folded detour target disables, folded call target does not") {
  const std::vector<Export> game{{"?Hooked", 0x100}, {"?Other", 0x100}, {"?Called", 0x200}, {"?Stub", 0x200}};
  const auto r = resolve(kTargets, game, std::vector<Export>{{"?Version", 1}});
  CHECK(r.items[0].problem == Problem::Folded);
  CHECK(r.items[0].aliases == 2);
  CHECK(r.items[1].problem == Problem::None);
  CHECK(r.items[1].aliases == 2);
  CHECK(r.required_failed == 1);
}

TEST_CASE("forwarded export counts as unresolved and is not an alias") {
  const std::vector<Export> game{{"?Hooked", 0x100}, {"?Called", 0x100, true}};
  const auto r = resolve(kTargets, game, std::vector<Export>{{"?Version", 1}});
  CHECK(r.items[0].problem == Problem::None);
  CHECK(r.items[1].problem == Problem::Forwarded);
  CHECK_FALSE(r.enabled());
}

TEST_CASE("format prints absolute address for resolved, symbol for unresolved") {
  const std::vector<Export> game{{"?Hooked", 0x33DCE0}};
  const auto r = resolve(kTargets, game, {});
  const auto s = format(r, 0x180000000, 0x7FF000000000);
  CHECK(s.find("Game+0x33DCE0 = 0x000000018033DCE0 x1  Hooked\n") != std::string::npos);
  CHECK(s.find("[MISSING  ] req call   t           Game  -  Called  {?Called}\n") != std::string::npos);
  CHECK(s.find("targets: 3, required failed: 1, optional failed: 1\n") != std::string::npos);
}

TEST_CASE("planned table has no duplicate symbols") {
  std::set<std::pair<Module, std::string_view>> seen;
  for (const auto& t : planned()) CHECK_MESSAGE(seen.insert({t.module, t.symbol}).second, t.label);
}

TEST_CASE("known build lookup") {
  CHECK(gdc::find_known_build("engine.DLL", "6dbc7458e2c4a29c8f98d2a320d4b4f9a226c35b0f18628bde0d245e89c792c9")->variant ==
        "compat");
  CHECK(gdc::find_known_build("Game.dll", "6dbc7458e2c4a29c8f98d2a320d4b4f9a226c35b0f18628bde0d245e89c792c9") == nullptr);
  CHECK(gdc::find_known_build("Game.dll", "") == nullptr);
  const auto* b = gdc::find_known_build("game.dll", "4e259d4f44d11b25086a975881f2a02c16b76c079a4cb3a3496b69b3bc7aa6f3");
  REQUIRE(b);
  CHECK(b->version == "v1.3.1.2 (x64)");
}
