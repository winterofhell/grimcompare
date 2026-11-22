#include <doctest/doctest.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "gdc/store.h"

using namespace gdc;
namespace fs = std::filesystem;

namespace {

std::string read(const fs::path& p) {
  std::ifstream f(p, std::ios::binary);
  REQUIRE(f.good());
  std::stringstream ss;
  ss << f.rdbuf();
  return ss.str();
}

snap::Snapshot fixture(const char* name) {
  return snap::from_json(read(fs::path(GDC_SOURCE_DIR) / "tests/fixtures/snapshots" / name));
}

struct TempDir {
  fs::path path;
  TempDir() {
    std::random_device rd;
    path = fs::temp_directory_path() / ("gdc-store-test-" + std::to_string(rd()));
    fs::create_directories(path);
  }
  ~TempDir() {
    std::error_code ec;
    fs::remove_all(path, ec);
  }
};

}  // namespace

TEST_CASE("gzip round trip, Python interop, corrupt input") {
  for (const std::string s : {std::string(), std::string("x"), std::string(100000, 'a') + "tail"})
    CHECK(store::gunzip(store::gzip(s)) == s);
  CHECK(store::gzip(std::string(100000, 'a')).size() < 1000);
  // gzip.compress(b"hello hello hello", mtime=0) from CPython (FNAME absent, OS 0xff)
  const unsigned char py[] = {0x1f, 0x8b, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0xff, 0xcb, 0x48, 0xcd, 0xc9,
                              0xc9, 0x57, 0xc8, 0x40, 0x90, 0x00, 0x80, 0x88, 0xf9, 0xe5, 0x11, 0x00, 0x00, 0x00};
  CHECK(store::gunzip(std::string(reinterpret_cast<const char*>(py), sizeof py)) == "hello hello hello");
  std::string bad = store::gzip("some text");
  bad[bad.size() - 6] ^= 1;  // CRC
  CHECK_THROWS_AS(store::gunzip(bad), std::runtime_error);
  CHECK_THROWS_AS(store::gunzip("not gzip at all, long enough"), std::runtime_error);
  CHECK_THROWS_AS(store::gunzip(std::string("\x1f\x8b\x08\x04", 4) + std::string(14, '\xff')), std::runtime_error);
}

TEST_CASE("keyed form round-trips a real snapshot and patches stay small") {
  const auto a = fixture("lvl96-before-ring-swap.json"), b = fixture("lvl96-after-ring-swap.json");
  const std::string ka = store::keyed(a), kb = store::keyed(b);
  // Same content, canonical (key) order: keying again gives the same text.
  const auto back = store::from_keyed(ka);
  CHECK(store::keyed(back) == ka);
  CHECK(back.skills.size() == a.skills.size());
  CHECK(back.equipped.size() == a.equipped.size());
  CHECK(back.char_attributes.size() == a.char_attributes.size());
  const auto j = nlohmann::json::parse(ka);
  CHECK(j.at("equipped").is_object());
  CHECK(j.at("equipped").contains("5"));
  CHECK(j.at("skills").contains("records/skills/playerclass03/possession1.dbr"));
  const std::string p = store::diff(ka, kb);
  CHECK(nlohmann::json::parse(store::apply(ka, p)) == nlohmann::json::parse(kb));
  CHECK(p.size() * 10 < kb.size());  // one ring changed
  CHECK(store::diff(ka, ka) == "{}");
}

TEST_CASE("keyed form: item skills by id, so two items granting one record both stay") {
  snap::Snapshot s;
  snap::Skill a;
  a.id = 7;
  a.record = "records/skills/itemskills/item_doombolt.dbr";
  snap::Skill b = a;
  b.id = 9;
  s.item_skills = {a, b};
  const auto j = nlohmann::json::parse(store::keyed(s));
  CHECK(j.at("item_skills").is_object());
  CHECK(j.at("item_skills").contains("7"));
  CHECK(j.at("item_skills").contains("9"));
  CHECK(store::from_keyed(store::keyed(s)).item_skills.size() == 2);
}

TEST_CASE("merge patch: removal, nested change, non-object replace") {
  const std::string a = R"({"x":1,"o":{"k":1,"gone":2},"arr":[1,2]})";
  const std::string b = R"({"o":{"k":3},"arr":[1],"new":"v"})";
  const std::string p = store::diff(a, b);
  CHECK(nlohmann::json::parse(p) == nlohmann::json::parse(R"({"x":null,"o":{"gone":null,"k":3},"arr":[1],"new":"v"})"));
  CHECK(nlohmann::json::parse(store::apply(a, p)) == nlohmann::json::parse(b));
}

TEST_CASE("folder names") {
  CHECK(store::folder_name("Nix") == "Nix");
  CHECK(store::folder_name("") == "unknown");
  CHECK(store::folder_name("a b").starts_with("a_b-"));
  CHECK(store::folder_name("a b").size() == 3 + 9);
  CHECK(store::folder_name("a b") != store::folder_name("a.b"));  // hash keeps them apart
  CHECK(store::folder_name("../x").find('/') == std::string::npos);
  CHECK(store::folder_name(std::string(80, 'z')).size() == 40 + 9);
}

TEST_CASE("trigger tags") {
  CHECK(store::tags(store::kEquip | store::kLevel) == std::vector<std::string>{"equip", "level"});
  CHECK(store::tags(0).empty());
  CHECK(store::tags(store::kBuff) == std::vector<std::string>{"buff"});
}

TEST_CASE("store: full baseline then deltas that rebuild every snapshot; rotation keeps active segments") {
  TempDir tmp;
  auto a = fixture("lvl96-before-ring-swap.json"), b = fixture("lvl96-after-ring-swap.json");
  a.character_name = b.character_name = "Nix";
  a.taken_at = "2026-11-14T12:00:00.000Z";
  b.taken_at = "2026-11-14T12:00:05.000Z";
  store::Store st(tmp.path, 50ull << 20);
  const auto w0 = st.write(a, store::kLoad);
  const auto w1 = st.write(b, store::kEquip);
  auto c = a;
  c.taken_at = "2026-11-14T12:00:09.000Z";
  const auto w2 = st.write(c, store::kEquip | store::kSkill);
  CHECK(w0.full);
  CHECK(!w1.full);
  CHECK(!w2.full);
  CHECK(w0.file.parent_path().filename() == "Nix");
  CHECK(w0.file.filename() == "20261114-120000-000-0000-full.json.gz");
  CHECK(w2.file.filename() == "20261114-120000-000-0002-delta.json.gz");
  CHECK(w1.bytes * 10 < w0.bytes);

  // Rebuild as tools/snapsum does.
  auto rec = [&](const store::Store::Written& w) { return nlohmann::ordered_json::parse(store::gunzip(read(w.file))); };
  const auto r0 = rec(w0), r1 = rec(w1), r2 = rec(w2);
  CHECK(r0.at("format") == "gdc-snap/1");
  CHECK(r2.at("trigger") == nlohmann::ordered_json::array({"equip", "skill"}));
  CHECK(r1.at("seq") == 1);
  std::string k = r0.at("snapshot").dump();
  CHECK(k == store::keyed(a));
  k = store::apply(k, r1.at("patch").dump());
  CHECK(store::keyed(store::from_keyed(k)) == store::keyed(b));
  k = store::apply(k, r2.at("patch").dump());
  CHECK(store::keyed(store::from_keyed(k)) == store::keyed(c));

  // store::read rebuilds the same chain; stray files are ignored.
  std::ofstream(w0.file.parent_path() / "notes.txt") << "x";
  std::vector<store::Record> back;
  store::read(w0.file.parent_path(), [&](const store::Record& r) { back.push_back(r); });
  REQUIRE(back.size() == 3);
  CHECK(back[0].full);
  CHECK(back[0].segment == "20261114-120000-000");
  CHECK(back[2].seq == 2);
  CHECK(back[1].triggers == store::kEquip);
  CHECK(back[2].triggers == (store::kEquip | store::kSkill));
  CHECK(store::keyed(back[0].snapshot) == store::keyed(a));
  CHECK(store::keyed(back[1].snapshot) == store::keyed(b));
  CHECK(store::keyed(back[2].snapshot) == store::keyed(c));
  // A delta without its baseline throws.
  TempDir lone;
  fs::copy_file(w1.file, lone.path / w1.file.filename());
  CHECK_THROWS_AS(store::read(lone.path, [](const store::Record&) {}), std::runtime_error);

  // A small cap: each record starts a new segment (chain > cap / 4) and older ones are deleted whole.
  store::Store small(tmp.path, w0.bytes * 2);
  auto d = a;
  d.character_name = "Other";
  std::vector<store::Store::Written> ws;
  for (int i = 0; i < 4; ++i) {
    d.taken_at = "2026-11-14T13:00:0" + std::to_string(i) + ".000Z";
    ws.push_back(small.write(d, store::kManual));
    CHECK(ws.back().full);
  }
  CHECK(fs::exists(ws.back().file));        // active segment kept
  CHECK(!fs::exists(w0.file));              // the oldest segment (Nix) went first
  std::uint64_t total = 0;
  for (const auto& e : fs::recursive_directory_iterator(tmp.path))
    if (e.is_regular_file()) total += e.file_size();
  CHECK(total <= w0.bytes * 2);
}

// Writes a small store for tests/test_snapsum.py when GDC_STORE_OUT is set (ctest fixture "store").
TEST_CASE("store: fixture for tools/snapsum") {
  const char* out = std::getenv("GDC_STORE_OUT");
  if (!out) return;
  fs::remove_all(out);
  auto a = fixture("lvl96-before-ring-swap.json"), b = fixture("lvl96-after-ring-swap.json");
  a.character_name = b.character_name = "Nix";
  a.taken_at = "2026-11-14T12:00:00.000Z";
  b.taken_at = "2026-11-14T12:00:05.000Z";
  auto c = b;
  c.taken_at = "2026-11-14T12:00:09.000Z";
  c.level += 1;
  c.skill_points += 3;
  store::Store st(out, 50ull << 20);
  CHECK(st.write(a, store::kLoad).full);
  CHECK(!st.write(b, store::kEquip).full);
  CHECK(!st.write(c, store::kLevel).full);
  std::ofstream(fs::path(out) / "expected-0001.json") << snap::to_json(b);
}
