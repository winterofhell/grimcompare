// gdcompare-setup without the game: hashing, Steam files and launch options, save headers, preset text, the
// catalog, and install / update / uninstall on fake game folders (a Windows layout with x64/ and a Proton layout
// with compat/; the install rules do not depend on the OS).
#include <doctest/doctest.h>

#include <fcntl.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <set>
#include <sstream>

#include "buildpreset.h"
#include "catalog.h"
#include "detect.h"
#include "tui.h"
#include "install.h"
#include "platform.h"
#include "saves.h"
#include "sha256.h"
#include "steam.h"

using namespace setup;
namespace fs = std::filesystem;

namespace {

std::string slurp(const fs::path& p) {
  std::ifstream f(p, std::ios::binary);
  REQUIRE(f.good());
  std::stringstream ss;
  ss << f.rdbuf();
  return ss.str();
}

void put(const fs::path& p, std::string_view text) {
  fs::create_directories(p.parent_path());
  std::ofstream f(p, std::ios::binary | std::ios::trunc);
  f << text;
  REQUIRE(f.good());
}

struct TempDir {
  fs::path path;
  explicit TempDir(std::string_view name) {
    static int n = 0;
    path = fs::temp_directory_path() / ("gdc-setup-" + std::to_string(getpid()) + "-" + std::to_string(++n) + "-" + std::string(name));
    fs::remove_all(path);
    fs::create_directories(path);
  }
  ~TempDir() {
    std::error_code ec;
    fs::remove_all(path, ec);
  }
};

}  // namespace

TEST_CASE("setup: sha256 test vectors (FIPS 180-4 examples)") {
  const auto h = [](std::string_view s) { return sha256({reinterpret_cast<const unsigned char*>(s.data()), s.size()}); };
  CHECK(h("") == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
  CHECK(h("abc") == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
  CHECK(h("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq") ==
        "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
  CHECK(h(std::string(1000000, 'a')) == "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
  TempDir d("sha");
  put(d.path / "f", "abc");
  CHECK(sha256_file(d.path / "f") == h("abc"));
  CHECK_FALSE(sha256_file(d.path / "missing"));
}

// ------------------------------------------------------------------ Steam

namespace {
const std::string kCurrent = R"(WINEDLLOVERRIDES="dxgi=n,b" LD_PRELOAD="" AMD_USERQ=1 PROTON_ENABLE_WAYLAND=1 DXVK_HDR=1 gamemoderun %command%)";
const std::string kMerged = R"(WINEDLLOVERRIDES="dxgi,dinput8=n,b" LD_PRELOAD="" AMD_USERQ=1 PROTON_ENABLE_WAYLAND=1 DXVK_HDR=1 gamemoderun %command%)";
const std::string kUserNow = R"(WINEDLLOVERRIDES="dxgi,dinput8=n,b" LD_PRELOAD="" AMD_USERQ=1 PROTON_ENABLE_WAYLAND=1 gamemoderun %command%)";
}  // namespace

TEST_CASE("setup: launch options merge (the cases deploy.sh relied on)") {
  CHECK(merge_launch_options(kCurrent) == kMerged);
  CHECK(merge_launch_options(kMerged) == kMerged);  // idempotent
  CHECK(merge_launch_options("") == R"(WINEDLLOVERRIDES="dinput8=n,b" %command%)");
  CHECK(merge_launch_options("gamemoderun %command%") == R"(WINEDLLOVERRIDES="dinput8=n,b" gamemoderun %command%)");
  CHECK(merge_launch_options("WINEDLLOVERRIDES=d3d9=n;dxgi=n,b %command% -x") == R"(WINEDLLOVERRIDES="d3d9=n;dxgi,dinput8=n,b" %command% -x)");
  CHECK(merge_launch_options(R"(WINEDLLOVERRIDES="dinput8=b" %command%)") == R"(WINEDLLOVERRIDES="dinput8=n,b" %command%)");

  const std::set<std::string> no_mods{"dinput8.dll", "game.dll"};
  CHECK(merge_launch_options(kCurrent, &no_mods) == kUserNow);  // RenoDX gone: DXVK_HDR dropped, dxgi kept
  // The user's DLSS mod: dxgi survives whether its dll is there or not.
  const std::set<std::string> with_dxgi{"dxgi.dll"};
  CHECK(merge_launch_options(kUserNow, &no_mods) == kUserNow);
  CHECK(merge_launch_options(kUserNow, &with_dxgi) == kUserNow);
  CHECK(merge_launch_options(kUserNow) == kUserNow);
  CHECK(merge_launch_options(kMerged, &no_mods) == merge_launch_options(kCurrent, &no_mods));
  const std::set<std::string> renodx{"dxgi.dll", "renodx-grimdawn.addon64"};
  CHECK(merge_launch_options(kCurrent, &renodx) == kMerged);
  const std::set<std::string> none;
  CHECK(merge_launch_options("%command% DXVK_HDR=1", &none) == R"(WINEDLLOVERRIDES="dinput8=n,b" %command%)");
  // A word that only ends in DXVK_HDR= is not the variable.
  CHECK(merge_launch_options("MY_DXVK_HDR=1 %command%", &none) == R"(WINEDLLOVERRIDES="dinput8=n,b" MY_DXVK_HDR=1 %command%)");
}

TEST_CASE("setup: launch options remove") {
  CHECK(remove_launch_options(kMerged) == kCurrent);
  CHECK(remove_launch_options(R"(WINEDLLOVERRIDES="dinput8=n,b" %command%)") == "%command%");
  CHECK(remove_launch_options(kCurrent) == kCurrent);
}

TEST_CASE("setup: vdf") {
  const std::string text = R"("UserLocalConfigStore" { "Software" { "Valve" { "Steam" { "apps" {
      "10" { "LaunchOptions" "nope" }
      // a comment
      "219990" { "LastPlayed" "1" "LaunchOptions" "WINEDLLOVERRIDES=\"dxgi=n,b\" %command%" } } } } } })";
  const Vdf v = parse_vdf(text);
  CHECK(launch_options(v) == R"(WINEDLLOVERRIDES="dxgi=n,b" %command%)");
  CHECK_FALSE(launch_options(parse_vdf(R"("UserLocalConfigStore" { "Software" { } })")));
  CHECK(launch_options(parse_vdf("")) == std::nullopt);
  CHECK(launch_options(parse_vdf(R"("a" { "b" )")) == std::nullopt);  // truncated: no crash

  TempDir d("steam");
  put(d.path / "steamapps/libraryfolders.vdf", R"("libraryfolders" { "0" { "path" ")" + d.path.string() + R"(" }
      "1" { "path" ")" + (d.path / "lib2").string() + R"(" } })");
  fs::create_directories(d.path / "lib2");
  const auto libs = steam_libraries(d.path);
  REQUIRE(libs.size() == 2);  // the root once, the second library
  CHECK(fs::equivalent(libs[0], d.path));
  CHECK(fs::equivalent(libs[1], d.path / "lib2"));
  put(d.path / "userdata/42/config/localconfig.vdf", text);
  put(d.path / "userdata/43/config/localconfig.vdf", R"("UserLocalConfigStore" { "Software" { } })");  // never launched it
  fs::create_directories(d.path / "userdata/44/config");                                                  // no file: skipped
  const auto users = steam_users(d.path);
  REQUIRE(users.size() == 2);
  CHECK(users[0].id == "42");
  CHECK(users[0].options == R"(WINEDLLOVERRIDES="dxgi=n,b" %command%)");
  CHECK_FALSE(users[1].options);
}

// ------------------------------------------------------------------ saves

namespace {

// The inverse of saves.cpp's Reader: the layout documented in saves.h.
class Writer {
 public:
  explicit Writer(std::uint32_t key) : key_(key) {
    std::uint32_t k = key;
    for (auto& t : table_) {
      k = (k >> 1) | (k << 31);
      k *= 39916801u;
      t = k;
    }
    raw32(key ^ 0x55555555u);
  }
  void u32(std::uint32_t v) {
    const std::uint32_t r = v ^ key_;
    for (int i = 0; i < 4; ++i) byte(static_cast<unsigned char>(r >> (8 * i)));
  }
  void u8(std::uint8_t v) { byte(static_cast<unsigned char>((v ^ key_) & 0xFF)); }
  std::vector<unsigned char> out;

 private:
  void raw32(std::uint32_t v) {
    for (int i = 0; i < 4; ++i) out.push_back(static_cast<unsigned char>(v >> (8 * i)));
  }
  void byte(unsigned char raw) {
    out.push_back(raw);
    key_ ^= table_[raw];
  }
  std::uint32_t key_;
  std::array<std::uint32_t, 256> table_{};
};

std::vector<unsigned char> save(std::uint32_t key, const std::u16string& name, std::string_view tag, std::uint32_t level,
                                bool hardcore, std::uint32_t version = 2) {
  Writer w(key);
  w.u32(0x58434447);
  w.u32(version);
  w.u32(static_cast<std::uint32_t>(name.size()));
  for (const char16_t c : name) w.u8(static_cast<std::uint8_t>(c & 0xFF)), w.u8(static_cast<std::uint8_t>(c >> 8));
  w.u8(1);
  w.u32(static_cast<std::uint32_t>(tag.size()));
  for (const char c : tag) w.u8(static_cast<std::uint8_t>(c));
  w.u32(level);
  w.u8(hardcore ? 1 : 0);
  w.u8(3);
  return w.out;
}

}  // namespace

TEST_CASE("setup: save header") {
  std::string error;
  for (const std::uint32_t key : {0u, 0x12345678u, 0xFFFFFFFFu}) {
    const auto h = read_save_header(save(key, u"Ölaf \U0001F600", "tagSkillClassName0306", 85, true), error);
    REQUIRE_MESSAGE(h, error);
    CHECK(h->name == "Ölaf \xF0\x9F\x98\x80");  // UTF-16 surrogate pair -> one UTF-8 code point
    CHECK(h->class_tag == "tagSkillClassName0306");
    CHECK(h->level == 85);
    CHECK(h->hardcore);
  }
  const auto fresh = read_save_header(save(7, u"New", "", 1, false, 1), error);
  REQUIRE(fresh);
  CHECK(fresh->class_tag.empty());

  auto bad = save(7, u"X", "", 1, false);
  CHECK_FALSE(read_save_header(std::span(bad).first(10), error));  // cut inside the header
  CHECK_FALSE(read_save_header(save(7, u"X", "", 0, false), error));  // level 0
  CHECK_FALSE(read_save_header(save(7, u"X", "", 1, false, 9), error));  // unknown version
  CHECK_FALSE(read_save_header(save(7, u"", "", 1, false), error));
  bad[5] ^= 0x40;  // magic
  CHECK_FALSE(read_save_header(bad, error));
  CHECK_FALSE(read_save_header({}, error));

  CHECK(class_masteries("tagSkillClassName0306") == std::vector<int>{3, 6});
  CHECK(class_masteries("tagSkillClassName09") == std::vector<int>{9});
  CHECK(class_masteries("").empty());
  CHECK(class_masteries("tagSkillClassName1112").empty());
  CHECK(class_masteries("tagSkillClassName03x6").empty());

  TempDir d("saves");
  const fs::path root = d.path / "userdata/1/219990/remote/save";
  const auto a = save(1, u"Bea", "tagSkillClassName01", 10, false), b = save(2, u"Al", "tagSkillClassName0102", 90, false);
  put(root / "main/_Bea/player.gdc", std::string(a.begin(), a.end()));
  put(root / "main/_Al/player.gdc", std::string(b.begin(), b.end()));
  put(root / "main/_broken/player.gdc", "nope");
  put(root / "main/__Al/player.gdc", std::string(b.begin(), b.end()));  // the engine's backup folder
  const fs::path custom_only = d.path / "userdata/2/219990/remote/save";  // a custom-game character only
  put(custom_only / "user/_Al/player.gdc", std::string(b.begin(), b.end()));
  put(d.path / "userdata/x/219990/remote/save/main/_Al/player.gdc", std::string(b.begin(), b.end()));  // not an account
  const auto roots = save_roots({d.path}, {});
  REQUIRE(roots.size() == 3);
  const auto chars = find_characters(roots);
  REQUIRE(chars.size() == 4);  // the broken one and the one without an account skipped
  CHECK(chars[3].header.name == "Bea");
  CHECK(chars[3].save == "cloud/1/main/_Bea");
  std::set<std::string> al{chars[0].save, chars[1].save, chars[2].save};
  CHECK(al == std::set<std::string>{"cloud/1/main/_Al", "cloud/1/main/__Al", "cloud/2/user/_Al"});

  const fs::path local = d.path / "Documents/My Games/Grim Dawn/save";
  put(local / "main/_Al/player.gdc", std::string(b.begin(), b.end()));
  const auto both = find_characters({root, local});
  CHECK(std::count_if(both.begin(), both.end(), [](const Character& c) { return c.save == "local/main/_Al"; }) == 1);
}

TEST_CASE("setup: the real saves decode (local saves; skips if absent)") {
  const char* h = std::getenv("HOME");
  if (!h) return;
  const auto chars = find_characters(save_roots({fs::path(h) / ".local/share/Steam"}, {}));
  if (chars.empty()) return;
  bool found = false;
  for (const auto& c : chars) {
    CHECK(c.header.level >= 1);
    found = found || (c.header.name == "winterofhell" && c.header.class_tag == "tagSkillClassName0304" && c.header.level == 96);
  }
  CHECK(found);
}

// ------------------------------------------------------------------ preset text and catalog

TEST_CASE("setup: preset_stem, with_binding, toml_string") {
  CHECK(preset_stem("Blitz / Vire's Might skater") == "blitz-vire-s-might-skater");
  CHECK(preset_stem("  ") == "build");
  CHECK(preset_stem("../../x") == "x");
  CHECK(preset_stem(std::string(100, 'a')).size() == 60);
  CHECK(toml_string("a\"b\\c\n") == R"("a\"b\\c\u000A")");

  BuildAnswers a;
  a.title = "Test";
  a.source = "custom";
  a.characters = {"Al"};
  a.masteries = {1};
  a.damage = {"physical"};
  const std::string t = preset_text(a);
  CHECK(made_by_setup(t));
  const auto t2 = with_binding(t, {"Al", "Bea \"B\""}, {});
  REQUIRE(t2);
  std::string error;
  const auto p = gdc::score::parse(*t2, error);
  REQUIRE_MESSAGE(p, error);
  CHECK(p->match_characters == std::vector<std::string>{"Al", "Bea \"B\""});
  CHECK(p->match_saves.empty());
  CHECK_FALSE(with_binding("[preset]\nname = \"x\"\n", {"Al"}, {}));  // not written by setup

  // A saves line is added after characters, replaced, and dropped again; the rest of the file stays.
  const auto t3 = with_binding(t, {"Al"}, {"cloud/1/main/_Al"});
  REQUIRE(t3);
  auto q = gdc::score::parse(*t3, error);
  REQUIRE_MESSAGE(q, error);
  CHECK(q->match_saves == std::vector<std::string>{"cloud/1/main/_Al"});
  const auto t4 = with_binding(*t3, {"Al"}, {"cloud/1/main/_Al", "local/main/__Al"});
  REQUIRE(t4);
  q = gdc::score::parse(*t4, error);
  REQUIRE_MESSAGE(q, error);
  CHECK(q->match_saves == std::vector<std::string>{"cloud/1/main/_Al", "local/main/__Al"});
  CHECK(with_binding(*t4, {"Al"}, {}) == t);
  a.saves = {"cloud/1/main/_Al"};
  CHECK(preset_text(a) == *t3);
}

namespace {
std::vector<Entry> catalog() {
  std::string error;
  auto c = load_catalog(slurp(fs::path(GDC_SOURCE_DIR) / "catalog/builds.toml"), error);
  REQUIRE_MESSAGE(c, error);
  return *c;
}
}  // namespace

TEST_CASE("setup: the catalog loads and every build gives a valid preset in every playstyle") {
  const auto c = catalog();
  std::set<std::string> builds;
  for (const auto& e : c) builds.insert(e.id.substr(0, e.id.find('/')));
  CHECK(builds.size() == 55);
  for (const auto& e : c) {
    INFO(e.id);
    REQUIRE_FALSE(e.classes.empty());
    CHECK_FALSE(e.damage.empty());
    CHECK(fits(e, code_masteries(e.classes[0])));
    for (const auto ps : {Playstyle::kBalanced, Playstyle::kTanky, Playstyle::kGlass}) {
      BuildAnswers a = from_entry(e, code_masteries(e.classes[0]), "Someone");
      a.playstyle = ps;
      std::string error;
      const auto p = gdc::score::parse(preset_text(a), error);
      REQUIRE_MESSAGE(p, error);
      CHECK(p->match_characters == std::vector<std::string>{"Someone"});
      CHECK(p->target_sets == e.sets);
      CHECK(p->damage_weight == doctest::Approx(playstyle_weights(ps).first));
      CHECK(p->survival_weight == doctest::Approx(playstyle_weights(ps).second));
      CHECK_FALSE(priorities(a, {}).empty());
    }
  }
}

TEST_CASE("setup: catalog rejects bad input") {
  std::string error;
  CHECK_FALSE(load_catalog("", error));
  CHECK_FALSE(load_catalog("[[build]]\nid = \"a\"\nname = \"A\"\ncategory = \"swim\"\nclasses = [\"01\"]\ndamage = [\"fire\"]\n", error));
  CHECK_FALSE(load_catalog("[[build]]\nid = \"a\"\nname = \"A\"\ncategory = \"melee\"\nclasses = [\"0101\"]\ndamage = [\"fire\"]\n", error));
  CHECK_FALSE(load_catalog("[[build]]\nid = \"a\"\nname = \"A\"\ncategory = \"melee\"\nclasses = [\"01\"]\ndamage = [\"heat\"]\n", error));
  CHECK_FALSE(load_catalog("[[build]]\nid = \"a\"\nname = \"A\"\ncategory = \"melee\"\nclasses = [\"01\"]\ndamage = [\"fire\"]\nwat = 1\n", error));
  const std::string ok = "[[build]]\nid = \"a\"\nname = \"A\"\ncategory = \"melee\"\nclasses = [\"01\"]\ndamage = [\"fire\"]\n";
  CHECK(load_catalog(ok, error));
  CHECK_FALSE(load_catalog(ok + ok, error));  // duplicate id
}

TEST_CASE("setup: fits, class codes") {
  Entry e;
  e.classes = {"0306", "03"};
  CHECK(fits(e, {3}));
  CHECK(fits(e, {6, 3}));
  CHECK_FALSE(fits(e, {3, 5}));
  CHECK_FALSE(fits(e, {5}));
  CHECK(fits(e, {}));
  CHECK(class_code({6, 3}) == "0306");
  CHECK(code_masteries("0306") == std::vector<int>{3, 6});
  CHECK(code_masteries("0303").empty());
  CHECK(code_masteries("11").empty());
}

// ------------------------------------------------------------------ install

namespace {

std::string utf16(std::string_view s) {
  std::string out;
  for (const char c : s) out += c, out += '\0';
  return out;
}

// A minimal PE with a version-resource-like string block: what inspect_loader reads.
std::string fake_dll(std::uint16_t machine, std::string_view product, std::string_view version) {
  std::string d(0x40, '\0');
  d[0] = 'M', d[1] = 'Z', d[0x3C] = 0x40;
  d += std::string("PE\0\0", 4);
  d += static_cast<char>(machine & 0xFF), d += static_cast<char>(machine >> 8);
  d += std::string(32, '\0');
  d += utf16("ProductName") + std::string(2, '\0') + std::string(2, '\0') + utf16(product) + std::string(2, '\0');
  d += utf16("ProductVersion") + std::string(2, '\0') + utf16(version) + std::string(2, '\0');
  return d;
}

struct Fake {
  TempDir tmp;
  fs::path target, src;
  Payload payload;

  Fake(std::string_view layout, std::string_view folder) : tmp(layout) {
    const fs::path root = tmp.path / "Grim Dawn";
    put(root / "database/database.arz", "db");
    put(root / folder / "Grim Dawn.exe", "exe");
    target = root / folder;
    src = tmp.path / "payload";
    ship("GDCompare.asi", "asi v1");
    ship("dinput8.dll", fake_dll(0x8664, "Ultimate-ASI-Loader-x64", "9.7.4"));
    ship("dinput8.ini", slurp(fs::path(GDC_SOURCE_DIR) / "adapter/ual/dinput8.ini"));
    ship("GDCompare.preset.toml", slurp(fs::path(GDC_SOURCE_DIR) / "presets/default.toml"));
    ship("GDCompare.presets/pet.toml", slurp(fs::path(GDC_SOURCE_DIR) / "presets/pet.toml"));
  }
  void ship(const std::string& name, std::string_view text) {
    put(src / name, text);
    payload.files[name] = *sha256_file(src / name);
    payload.sources[name] = src / name;
  }
  Plan plan(Mode m = Mode::kInstall) const { return make_plan(m, target, &payload, false); }
  void run(const Plan& p) const {
    REQUIRE(p.blockers.empty());
    std::vector<std::string> done;
    std::string error;
    REQUIRE_MESSAGE(apply(p, &payload, done, error), error);
  }
  void install() const { run(plan()); }
  const Step* step(const Plan& p, std::string_view file) const {
    for (const auto& s : p.steps)
      if (s.file == file) return &s;
    return nullptr;
  }
};

bool game_closed() {
  const auto r = game_running();
  if (!r || *r) MESSAGE("Grim Dawn is running (or /proc is unreadable): install apply tests skipped");
  return r && !*r;
}

}  // namespace

TEST_CASE("setup: game folders and run folders") {
  Fake w("win", "x64"), l("linux", "compat");
  CHECK(is_game_dir(w.target.parent_path()));
  CHECK(run_folders(w.target.parent_path()) == std::vector<std::string>{"x64"});
  CHECK(run_folders(l.target.parent_path()) == std::vector<std::string>{"compat"});
  CHECK(game_root_of(l.target) == l.target.parent_path());
  CHECK(game_root_of(l.target.parent_path().parent_path()).empty());  // above the game folder
}

TEST_CASE("setup: fresh install, nothing to do after, update, uninstall") {
  if (!game_closed()) return;
  for (const char* folder : {"x64", "compat"}) {
    INFO(folder);
    Fake f("fresh", folder);
    put(f.target / "dxgi.dll", "the user's DLSS mod");
    const Plan p = f.plan();
    CHECK(p.steps.size() == 5);
    for (const auto& s : p.steps) CHECK(s.kind == Step::Kind::kWrite);
    f.install();
    CHECK(slurp(f.target / "GDCompare.asi") == "asi v1");
    CHECK(read_manifest(f.target).size() == 5);
    CHECK_FALSE(f.plan().changes());  // repair of a good install: nothing

    f.ship("GDCompare.asi", "asi v2");
    const Plan up = f.plan();
    REQUIRE(f.step(up, "GDCompare.asi"));
    CHECK(f.step(up, "GDCompare.asi")->kind == Step::Kind::kWrite);
    CHECK(f.step(up, "dinput8.dll")->kind == Step::Kind::kKeep);
    f.run(up);
    CHECK(slurp(f.target / "GDCompare.asi") == "asi v2");

    // A preset made by the wizard and the log.
    put(f.target / "GDCompare.presets/my-build.toml", "# Written by gdcompare-setup.\n");
    put(f.target / "GDCompare.log", "log");
    const Plan un = f.plan(Mode::kUninstall);
    CHECK(f.step(un, "GDCompare.presets/my-build.toml")->kind == Step::Kind::kKeep);
    f.run(un);
    for (const char* gone : {"GDCompare.asi", "dinput8.dll", "dinput8.ini", "GDCompare.preset.toml", "GDCompare.presets/pet.toml",
                             "GDCompare.log", "GDCompare.installed"})
      CHECK_FALSE(fs::exists(f.target / gone));
    CHECK(fs::exists(f.target / "GDCompare.presets/my-build.toml"));
    CHECK(slurp(f.target / "dxgi.dll") == "the user's DLSS mod");
    CHECK(fs::exists(f.target / "Grim Dawn.exe"));
    CHECK(fs::exists(f.target.parent_path() / "database/database.arz"));
  }
}

TEST_CASE("setup: edited presets are kept; broken ones are backed up and replaced") {
  if (!game_closed()) return;
  Fake f("presets", "compat");
  f.install();
  const std::string edited = slurp(f.target / "GDCompare.preset.toml") + "\n# mine\n";
  put(f.target / "GDCompare.preset.toml", edited);
  put(f.target / "GDCompare.presets/pet.toml", "[[[ broken");
  const Plan p = f.plan();
  CHECK(f.step(p, "GDCompare.preset.toml")->kind == Step::Kind::kKeep);
  CHECK(f.step(p, "GDCompare.presets/pet.toml")->kind == Step::Kind::kWrite);
  CHECK(f.step(p, "GDCompare.presets/pet.toml")->backup);
  f.run(p);
  CHECK(slurp(f.target / "GDCompare.preset.toml") == edited);
  bool backed = false;
  for (const auto& e : fs::recursive_directory_iterator(f.target / "GDCompare.backup"))
    backed = backed || (e.path().filename() == "pet.toml" && slurp(e.path()) == "[[[ broken");
  CHECK(backed);

  // Uninstall keeps the edited preset and still lists it, so a later uninstall keeps it too.
  const Plan un = f.plan(Mode::kUninstall);
  CHECK(f.step(un, "GDCompare.preset.toml")->kind == Step::Kind::kKeep);
  f.run(un);
  CHECK(slurp(f.target / "GDCompare.preset.toml") == edited);
  CHECK(read_manifest(f.target).contains("GDCompare.preset.toml"));
}

TEST_CASE("setup: a preset no longer shipped is removed only while unchanged") {
  if (!game_closed()) return;
  Fake f("dropped", "x64");
  f.ship("GDCompare.presets/old.toml", slurp(fs::path(GDC_SOURCE_DIR) / "presets/retaliation.toml"));
  f.install();
  f.payload.files.erase("GDCompare.presets/old.toml");
  const Plan p = f.plan();
  REQUIRE(f.step(p, "GDCompare.presets/old.toml"));
  CHECK(f.step(p, "GDCompare.presets/old.toml")->kind == Step::Kind::kRemove);
  f.run(p);
  CHECK_FALSE(fs::exists(f.target / "GDCompare.presets/old.toml"));
}

TEST_CASE("setup: a foreign dinput8.dll is never overwritten") {
  Fake f("foreign", "x64");
  put(f.target / "dinput8.dll", fake_dll(0x8664, "ReShade", "6.0"));
  Plan p = f.plan();
  REQUIRE(p.blockers.size() == 1);
  CHECK(p.blockers[0].find("another mod (ReShade)") != std::string::npos);
  CHECK(p.manifest.empty());
  std::vector<std::string> done;
  std::string error;
  CHECK_FALSE(apply(p, &f.payload, done, error));
  CHECK(slurp(f.target / "dinput8.dll") == fake_dll(0x8664, "ReShade", "6.0"));
  CHECK_FALSE(fs::exists(f.target / "GDCompare.asi"));

  put(f.target / "dinput8.dll", fake_dll(0x14C, "Ultimate-ASI-Loader", "9.7.4"));  // 32-bit build
  p = f.plan();
  REQUIRE(p.blockers.size() == 1);
  CHECK(p.blockers[0].find("32-bit") != std::string::npos);
}

TEST_CASE("setup: an existing x64 Ultimate ASI Loader is reused and left on uninstall") {
  if (!game_closed()) return;
  Fake f("ual", "compat");
  const std::string theirs = fake_dll(0x8664, "Ultimate-ASI-Loader-x64", "9.8.0");
  put(f.target / "dinput8.dll", theirs);
  const Plan p = f.plan();
  CHECK(p.blockers.empty());
  CHECK(f.step(p, "dinput8.dll")->kind == Step::Kind::kKeep);
  CHECK_FALSE(p.notes.empty());
  CHECK_FALSE(p.manifest.contains("dinput8.dll"));
  f.run(p);
  f.run(f.plan(Mode::kUninstall));
  CHECK(slurp(f.target / "dinput8.dll") == theirs);
}

TEST_CASE("setup: the loader stays on uninstall while another .asi uses it") {
  if (!game_closed()) return;
  Fake f("otherasi", "x64");
  f.install();
  put(f.target / "Other.asi", "x");
  const Plan un = f.plan(Mode::kUninstall);
  CHECK(f.step(un, "dinput8.dll")->kind == Step::Kind::kKeep);
  CHECK(f.step(un, "GDCompare.asi")->kind == Step::Kind::kRemove);
  f.run(un);
  CHECK(fs::exists(f.target / "dinput8.dll"));
  CHECK(fs::exists(f.target / "dinput8.ini"));
}

TEST_CASE("setup: a foreign dinput8.ini needs dontloadfromdllmain=0") {
  Fake f("ini", "x64");
  put(f.target / "dinput8.ini", "[globalsets]\ndontloadfromdllmain=1\n");
  CHECK(f.plan().blockers.size() == 1);
  put(f.target / "dinput8.ini", "; mine\n[GlobalSets]\n dontloadfromdllmain = 1\ndontloadfromdllmain=0\n");
  const Plan p = f.plan();
  CHECK(p.blockers.empty());
  CHECK(f.step(p, "dinput8.ini")->kind == Step::Kind::kKeep);
  put(f.target / "dinput8.ini", "[other]\ndontloadfromdllmain=0\n");
  CHECK(f.plan().blockers.size() == 1);
}

TEST_CASE("setup: blocked while the game runs or is not installed; payload and manifest checks") {
  Fake f("blocked", "x64");
  CHECK(make_plan(Mode::kInstall, f.target, &f.payload, true).blockers.size() == 1);
  CHECK(make_plan(Mode::kInstall, f.target, &f.payload, std::nullopt).blockers.size() == 1);
  CHECK(make_plan(Mode::kUninstall, f.target, nullptr, false).blockers.size() == 1);  // not installed

  std::string error;
  fs::create_directories(f.src / "presets");
  fs::copy_file(f.src / "GDCompare.preset.toml", f.src / "presets/default.toml");
  CHECK_FALSE(load_payload(f.src, error));  // the fake dinput8.dll is not the pinned loader
  CHECK(error.find("Ultimate ASI Loader") != std::string::npos);

  put(f.target / "GDCompare.installed", "../evil\tx\n/abs\tx\na\\b\tx\nGDCompare.asi\tabc\r\nnotab\n");
  const auto m = read_manifest(f.target);
  CHECK(m.size() == 1);
  CHECK(m.at("GDCompare.asi") == "abc");
}

namespace {

// Runs f with stdin from `input` and stdout discarded (numbered menus: not a terminal), restored on a throw too.
template <class F>
auto with_input(const std::string& input, F f) {
  struct Redirect {
    std::istringstream in;
    std::streambuf* old;
    int saved;
    explicit Redirect(const std::string& s) : in(s), old(std::cin.rdbuf(in.rdbuf())) {
      std::fflush(stdout);
      saved = dup(STDOUT_FILENO);
      const int null = open("/dev/null", O_WRONLY);
      dup2(null, STDOUT_FILENO);
      close(null);
    }
    ~Redirect() {
      std::fflush(stdout);
      dup2(saved, STDOUT_FILENO);
      close(saved);
      std::cin.rdbuf(old);
    }
  } r(input);
  return f();
}

}  // namespace

TEST_CASE("setup: numbered menus take Enter for the default, 0 for back, retry on bad input") {
  using namespace setup;
  ui::init(true);
  const std::vector<ui::Item> items{{"a", ""}, {"b", "hint"}, {"c", ""}};
  CHECK(with_input("\n", [&] { return ui::choose("h", items, 2); }) == 2);
  CHECK(with_input("0\n", [&] { return ui::choose("h", items); }) == -1);
  CHECK(with_input("x\n9\n2\n", [&] { return ui::choose("h", items); }) == 1);
  CHECK(with_input("/c\n1\n", [&] { return ui::choose("h", items, 0, {}, true); }) == 2);  // numbers count the matches
  CHECK_THROWS_AS(with_input("", [&] { return ui::choose("h", items); }), ui::Cancelled);
  CHECK(with_input("\n", [&] { return ui::confirm("q", true); }));
  CHECK_FALSE(with_input("maybe\nn\n", [&] { return ui::confirm("q", true); }));
  CHECK(with_input("\n", [&] { return ui::ask("p", "def"); }) == "def");
  const auto many = with_input("1 9\n1 3\n", [&] { return ui::choose_many("h", items, {}, 2); });
  REQUIRE(many);
  CHECK(*many == std::vector<int>{0, 2});
  CHECK_FALSE(with_input("0\n", [&] { return ui::choose_many("h", items, {1}); }));
}
