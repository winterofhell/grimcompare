#include <doctest/doctest.h>

#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "gdc/arz.h"

using namespace gdc::arz;
namespace fs = std::filesystem;

namespace {

std::vector<std::byte> bytes(std::initializer_list<int> v) {
  std::vector<std::byte> b;
  for (int x : v) b.push_back(static_cast<std::byte>(x));
  return b;
}

template <class T>
void put(std::vector<std::byte>& b, T v) {
  const auto at = b.size();
  b.resize(at + sizeof v);
  std::memcpy(b.data() + at, &v, sizeof v);
}

// A literals-only LZ4 block (valid: the last sequence carries literals only).
std::vector<std::byte> lz4_literals(const std::vector<std::byte>& raw) {
  std::vector<std::byte> out;
  std::size_t n = raw.size();
  out.push_back(static_cast<std::byte>(std::min<std::size_t>(n, 15) << 4));
  if (n >= 15) {
    for (n -= 15; n >= 255; n -= 255) out.push_back(std::byte{255});
    out.push_back(static_cast<std::byte>(n));
  }
  out.insert(out.end(), raw.begin(), raw.end());
  return out;
}

// strings: 0 "records/a.dbr", 1 "levelRequirement", 2 "offensiveFireMin", 3 "setName", 4 "records/sets/s.dbr"
std::vector<std::byte> tiny_arz() {
  const std::vector<std::string> strings = {"records/a.dbr", "levelRequirement", "offensiveFireMin", "setName",
                                            "records/sets/s.dbr"};
  std::vector<std::byte> fields;
  put<std::uint16_t>(fields, 0), put<std::uint16_t>(fields, 1), put<std::uint32_t>(fields, 1), put<std::int32_t>(fields, 68);
  put<std::uint16_t>(fields, 1), put<std::uint16_t>(fields, 2), put<std::uint32_t>(fields, 2);
  put<float>(fields, 10.5f), put<float>(fields, 20);
  put<std::uint16_t>(fields, 2), put<std::uint16_t>(fields, 1), put<std::uint32_t>(fields, 3), put<std::uint32_t>(fields, 4);
  const auto packed = lz4_literals(fields);

  std::vector<std::byte> f(24);
  const std::uint32_t data_off = 0;  // relative to the end of the header
  f.insert(f.end(), packed.begin(), packed.end());
  const auto rec_start = static_cast<std::uint32_t>(f.size());
  put<std::uint32_t>(f, 0);
  const std::string cls = "ArmorJewelry_Ring";
  put<std::uint32_t>(f, static_cast<std::uint32_t>(cls.size()));
  for (char c : cls) f.push_back(static_cast<std::byte>(c));
  put<std::uint32_t>(f, data_off), put<std::uint32_t>(f, static_cast<std::uint32_t>(packed.size()));
  put<std::uint32_t>(f, static_cast<std::uint32_t>(fields.size())), put<std::uint64_t>(f, 0);
  const auto rec_size = static_cast<std::uint32_t>(f.size()) - rec_start;
  const auto str_start = static_cast<std::uint32_t>(f.size());
  put<std::uint32_t>(f, static_cast<std::uint32_t>(strings.size()));
  for (const auto& s : strings) {
    put<std::uint32_t>(f, static_cast<std::uint32_t>(s.size()));
    for (char c : s) f.push_back(static_cast<std::byte>(c));
  }
  const auto str_size = static_cast<std::uint32_t>(f.size()) - str_start;
  std::vector<std::byte> h;
  put<std::uint16_t>(h, 2), put<std::uint16_t>(h, 3), put(h, rec_start), put(h, rec_size), put<std::uint32_t>(h, 1);
  put(h, str_start), put(h, str_size);
  std::memcpy(f.data(), h.data(), 24);
  return f;
}

fs::path game_dir() {
  if (const char* d = std::getenv("GD_DIR")) return d;
  if (const char* h = std::getenv("HOME")) return fs::path(h) / ".local/share/Steam/steamapps/common/Grim Dawn";
  return {};
}

}  // namespace

TEST_CASE("lz4 block: literals, overlapping match, long lengths") {
  CHECK(lz4_block(bytes({0x30, 'a', 'b', 'c'}), 3) == bytes({'a', 'b', 'c'}));
  // "ab" then match offset 2, length 4+2 = 6 (overlapping copy) -> "abababab"
  CHECK(lz4_block(bytes({0x22, 'a', 'b', 0x02, 0x00}), 8) == bytes({'a', 'b', 'a', 'b', 'a', 'b', 'a', 'b'}));
  // match length 15 + 255 + 1 + 4 = 275 of "x"
  const auto rle = lz4_block(bytes({0x1F, 'x', 0x01, 0x00, 0xFF, 0x01}), 276);
  CHECK(rle.size() == 276);
  CHECK(rle.back() == std::byte{'x'});
  std::vector<std::byte> raw(300, std::byte{'q'});
  CHECK(lz4_block(lz4_literals(raw), 300) == raw);
}

TEST_CASE("lz4 block: malformed input throws") {
  CHECK_THROWS_AS(lz4_block(bytes({0x30, 'a'}), 3), std::runtime_error);                  // literals truncated
  CHECK_THROWS_AS(lz4_block(bytes({0x10, 'a', 0x05, 0x00}), 5), std::runtime_error);      // offset beyond output
  CHECK_THROWS_AS(lz4_block(bytes({0x10, 'a', 0x00, 0x00}), 5), std::runtime_error);      // offset 0
  CHECK_THROWS_AS(lz4_block(bytes({0x30, 'a', 'b', 'c'}), 4), std::runtime_error);        // size mismatch
  CHECK_THROWS_AS(lz4_block(bytes({0x1F, 'x', 0x01, 0x00, 0xFF}), 300), std::runtime_error);  // length truncated
}

TEST_CASE("arz: synthetic file parses, values typed, unknown names empty") {
  const File f(tiny_arz());
  CHECK(f.size() == 1);
  CHECK(f.contains("records/a.dbr"));
  CHECK(!f.record("records/missing.dbr"));
  const auto r = f.record("records/a.dbr");
  REQUIRE(r);
  CHECK(r->cls == "ArmorJewelry_Ring");
  CHECK(r->number("levelRequirement") == 68);
  CHECK(r->number("offensiveFireMin", 1) == 20);
  CHECK(r->number("offensiveFireMin", 2, -1) == -1);
  CHECK(r->string("setName") == "records/sets/s.dbr");
  CHECK(r->string("nope").empty());
  CHECK(r->fields.at("offensiveFireMin").kind == Field::Kind::Float);

  Database db;
  db.add(File(tiny_arz()));
  CHECK(db.record("records/a.dbr")->number("levelRequirement") == 68);
  CHECK(db.record("records/a.dbr") == db.record("records/a.dbr"));  // decoded once, cached
  CHECK(db.record("records/missing.dbr") == nullptr);
  CHECK(db.record("records/missing.dbr") == nullptr);  // absent, cached as absent

  // The same bytes from disk (memory-mapped).
  const auto path = std::filesystem::temp_directory_path() / "gdc-test-tiny.arz";
  {
    const auto bytes = tiny_arz();
    std::ofstream(path, std::ios::binary).write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
  }
  {
    const File m = File::open(path);
    CHECK(m.record("records/a.dbr")->string("setName") == "records/sets/s.dbr");
  }
  std::filesystem::remove(path);
  CHECK_THROWS_AS(File::open(path), std::runtime_error);
}

TEST_CASE("arz: malformed files throw") {
  auto good = tiny_arz();
  CHECK_THROWS_AS(File(std::vector<std::byte>(10)), std::runtime_error);
  auto bad_version = good;
  bad_version[2] = std::byte{2};
  CHECK_THROWS_AS(File{bad_version}, std::runtime_error);
  auto bad_table = good;
  bad_table[4] = std::byte{0xFF}, bad_table[5] = std::byte{0xFF}, bad_table[6] = std::byte{0xFF};
  CHECK_THROWS_AS(File{bad_table}, std::runtime_error);
  auto cut = good;
  cut.resize(cut.size() - 3);  // inside the string table
  CHECK_THROWS_AS(File{cut}, std::runtime_error);
}

TEST_CASE("arz: installed game database (skips if absent)") {
  const fs::path dir = game_dir();
  if (dir.empty() || !fs::exists(dir / "database" / "database.arz")) {
    MESSAGE("game database not found; skipped");
    return;
  }
  const Database db = Database::open_game(dir);
  CHECK(db.files() == 4);
  const auto cf = db.record("records/game/combatformulas.dbr");
  REQUIRE(cf);
  CHECK(cf->number("pthMinimum") == 55);
  CHECK(cf->number("pthThreshold2") == 90);
  CHECK(cf->number("pthDamageModifier6") == doctest::Approx(1.5));
  // Layering: GDX2's copy of c031_ring (Life 200, +2 Celestial Guardian) replaces the base one (150).
  const auto ring = db.record("records/items/gearaccessories/rings/c031_ring.dbr");
  REQUIRE(ring);
  CHECK(ring->number("characterLife") == 200);
  CHECK(ring->string("augmentSkillName2") == "records/skills/playerclass09/summon_celestialguardian2_petmodifier.dbr");
  CHECK(db.record("records/items/gearaccessories/rings/b102e_ring.dbr")->string("augmentSkillName3") ==
        "records/skills/playerclass09/aegis1.dbr");
}
