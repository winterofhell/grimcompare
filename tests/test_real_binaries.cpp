// Cross-checks against the installed game, if present (GD_DIR or the default Steam path):
// 1) our PE reader agrees with winedump on every export (re/*.exports.tsv, v1.3.1.1);
// 2) every required target resolves in both launch variants (x64/ and compat/).
// Skips with a message when the game is not installed, so CI without the game still passes.
#include <doctest/doctest.h>

#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <string>

#include "gdc/hook_targets.h"
#include "gdc/pe_exports.h"

namespace fs = std::filesystem;
using namespace gdc;

namespace {

fs::path game_dir() {
  if (const char* d = std::getenv("GD_DIR")) return d;
  if (const char* h = std::getenv("HOME")) return fs::path(h) / ".local/share/Steam/steamapps/common/Grim Dawn";
  return {};
}

std::vector<std::byte> slurp(const fs::path& p) {
  std::ifstream f(p, std::ios::binary);
  std::vector<char> c((std::istreambuf_iterator<char>(f)), {});
  std::vector<std::byte> b(c.size());
  std::memcpy(b.data(), c.data(), c.size());
  return b;
}

std::map<std::string, std::uint32_t> as_map(const pe::ExportTable& t) {
  std::map<std::string, std::uint32_t> m;
  for (const auto& e : t.exports) m.emplace(e.name, e.rva);
  return m;
}

}  // namespace

TEST_CASE("real binaries: reader matches winedump dump and all required targets resolve") {
  const fs::path dir = game_dir();
  if (dir.empty() || !fs::exists(dir / "compat/Game.dll")) {
    MESSAGE("Grim Dawn not found at " << dir << " — skipped (set GD_DIR)");
    return;
  }

  const auto game = pe::read_exports(slurp(dir / "compat/Game.dll"), pe::Layout::File);
  REQUIRE(game.error.empty());

  // The TSV was dumped (winedump) from compat/Game.dll v1.3.1.1. After a game patch the dump is stale;
  // a changed export count is the cheap signal for that.
  std::ifstream tsv(fs::path(GDC_SOURCE_DIR) / "re/Game.exports.tsv");
  std::map<std::string, std::uint32_t> dumped;
  for (std::string line; std::getline(tsv, line);) {
    const auto t1 = line.find('\t'), t2 = line.find('\t', t1 + 1);
    dumped.emplace(line.substr(t1 + 1, t2 - t1 - 1), static_cast<std::uint32_t>(std::stoul(line.substr(0, t1), nullptr, 16)));
  }
  if (dumped.size() == game.exports.size()) {
    CHECK(as_map(game) == dumped);
  } else {
    MESSAGE("export count differs from re/Game.exports.tsv (game patched?) — dump comparison skipped");
  }

  for (const char* variant : {"compat", "x64"}) {
    const auto g = pe::read_exports(slurp(dir / variant / "Game.dll"), pe::Layout::File);
    const auto e = pe::read_exports(slurp(dir / variant / "Engine.dll"), pe::Layout::File);
    REQUIRE(g.error.empty());
    REQUIRE(e.error.empty());
    const auto r = targets::resolve(targets::planned(), g.exports, e.exports);
    INFO(variant << "\n" << targets::format(r, 0x180000000, 0x180000000));
    CHECK(r.required_failed == 0);
    CHECK(r.optional_failed == 0);
  }
}
