#include <doctest/doctest.h>

#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "gdc/labels.h"
#include "gdc/text.h"

using namespace gdc;
namespace fs = std::filesystem;

namespace {

void put(std::vector<std::byte>& v, std::uint32_t x) {
  for (int i = 0; i < 4; ++i) v.push_back(std::byte((x >> (8 * i)) & 0xFF));
}
void put(std::vector<std::byte>& v, std::string_view s) {
  for (char c : s) v.push_back(std::byte(c));
}

// One stored file ("a.txt", type 1) and one LZ4 part file ("b.txt": literals "tagB=x").
std::vector<std::byte> archive() {
  std::vector<std::byte> d;
  put(d, std::string_view("ARC\0", 4));
  put(d, 3), put(d, 2), put(d, 1);  // version, files, parts
  put(d, 12), put(d, 10);           // part table size, string table size
  const std::size_t table_at = d.size();
  put(d, 0);                        // table offset (patched)
  const std::uint32_t a_off = static_cast<std::uint32_t>(d.size());
  put(d, "tagA=1\n");               // 7 bytes stored
  const std::uint32_t b_off = static_cast<std::uint32_t>(d.size());
  d.push_back(std::byte{0x60});     // lz4: 6 literals
  put(d, "tagB=x");                 // 7 bytes compressed -> 6
  const auto table = static_cast<std::uint32_t>(d.size());
  std::memcpy(d.data() + table_at, &table, 4);
  put(d, b_off), put(d, 7), put(d, 6);  // part 0
  put(d, "a.txtb.txt");
  auto entry = [&](std::uint32_t type, std::uint32_t off, std::uint32_t c, std::uint32_t s, std::uint32_t parts,
                   std::uint32_t name_off) {
    put(d, type), put(d, off), put(d, c), put(d, s), put(d, 0), put(d, 0), put(d, 0), put(d, parts), put(d, 0),
        put(d, 5), put(d, name_off);
  };
  entry(1, a_off, 7, 7, 0, 0);
  entry(3, 0, 7, 6, 1, 5);
  return d;
}

fs::path game_dir() {
  if (const char* d = std::getenv("GD_DIR")) return d;
  if (const char* h = std::getenv("HOME")) return fs::path(h) / ".local/share/Steam/steamapps/common/Grim Dawn";
  return {};
}

}  // namespace

TEST_CASE("text: .arc v3 stored and LZ4 files; truncated input throws") {
  const auto d = archive();
  const auto files = text::read_arc(d);
  REQUIRE(files.size() == 2);
  CHECK(files[0].first == "a.txt");
  CHECK(std::string(reinterpret_cast<const char*>(files[0].second.data()), files[0].second.size()) == "tagA=1\n");
  CHECK(files[1].first == "b.txt");
  CHECK(std::string(reinterpret_cast<const char*>(files[1].second.data()), files[1].second.size()) == "tagB=x");
  CHECK_THROWS(text::read_arc(std::span(d).first(d.size() - 3)));
}

TEST_CASE("text: tag lines, BOM, comments, CRLF, later wins; plain() drops format codes") {
  text::Table t;
  t.add("\xEF\xBB\xBF# header\r\ntagA=one\r\n\r\nnot a tag\r\ntagB=b=c\r\n");
  t.add("tagA=two");
  CHECK(t.get("tagA") == "two");
  CHECK(t.get("tagB") == "b=c");
  CHECK(t.get("tagC").empty());
  CHECK(t.size() == 2);
  CHECK(text::plain("{%.0f0}% {^E}Energy Leech Resistance") == "Energy Leech Resistance");
  CHECK(text::plain("{%+.0f0}% {^E}Max Pierce Resistance ") == "Max Pierce Resistance");
  CHECK(text::plain("Blood of Dreeg") == "Blood of Dreeg");
  CHECK(text::plain("{^E}") == "");
}

TEST_CASE("labels: fallbacks without database or text") {
  const Labels l;
  CHECK(l.resist(21) == "ManaLeach");
  CHECK(l.resist(9) == "Vitality");
  CHECK(l.skill("records/skills/playerclass03/bloodofdreeg2.dbr") == "bloodofdreeg2");
  CHECK(l.set("records/items/lootsets/itemset_d001.dbr") == "itemset_d001");
}

TEST_CASE("labels: installed game text and database (skips if absent)") {
  const fs::path dir = game_dir();
  if (dir.empty() || !fs::exists(dir / "resources/Text_EN.arc") || !fs::exists(dir / "database/database.arz")) {
    MESSAGE("game not found; skipped");
    return;
  }
  const auto text = text::Table::open_game(dir);
  const auto db = arz::Database::open_game(dir);
  const Labels l(&db, &text);
  CHECK(l.resist(21) == "Energy Leech Resist");
  CHECK(l.resist(9) == "Vitality Resist");
  CHECK(l.resist(7) == "Poison & Acid Resist");
  CHECK(l.resist(42) == "Reduced Stun Duration");
  CHECK(l.skill("records/skills/playerclass03/bloodofdreeg2.dbr") == "Aspect of the Guardian");
  CHECK(l.skill("records/skills/playerclass03/bloodofdreeg1.dbr") == "Blood of Dreeg");  // via buffSkillName
  CHECK(l.set("records/items/lootsets/itemset_d001.dbr") == "Justicar Armor");
}
