#include <doctest/doctest.h>

#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <vector>

#include "gdc/code_scan.h"

using gdc::scan::find_object_lookup_call;
namespace fs = std::filesystem;

namespace {

std::vector<std::byte> bytes(std::initializer_list<int> v) {
  std::vector<std::byte> b;
  for (int x : v) b.push_back(static_cast<std::byte>(x));
  return b;
}

// call [rip+0x100]; mov rcx,rax; mov edx,edi; call rel32 -0x20
const std::vector<std::byte> kSite = bytes({0xFF, 0x15, 0x00, 0x01, 0x00, 0x00, 0x48, 0x8B, 0xC8, 0x8B, 0xD7, 0xE8,
                                            0xE0, 0xFF, 0xFF, 0xFF});

}  // namespace

TEST_CASE("lookup call: decodes both rel32 targets") {
  std::vector<std::byte> code = bytes({0x90, 0x90});
  code.insert(code.end(), kSite.begin(), kSite.end());
  code.push_back(std::byte{0xC3});
  const auto r = find_object_lookup_call(code, 0x1000);
  REQUIRE(r);
  CHECK(r->iat_slot == 0x1002 + 6 + 0x100);
  CHECK(r->target == 0x1002 + 16 - 0x20);
}

TEST_CASE("lookup call: none, truncated, or ambiguous -> nullopt") {
  CHECK(!find_object_lookup_call(bytes({0x90, 0xC3}), 0));
  std::vector<std::byte> cut(kSite.begin(), kSite.end() - 1);
  CHECK(!find_object_lookup_call(cut, 0));
  std::vector<std::byte> twice = kSite;
  twice.insert(twice.end(), kSite.begin(), kSite.end());
  CHECK(!find_object_lookup_call(twice, 0));
  std::vector<std::byte> other = kSite;
  other[10] = std::byte{0xD6};  // mov edx,esi: a different call
  CHECK(!find_object_lookup_call(other, 0));
}

// Bytes of the installed compat/Game.dll at `rva` (empty and a MESSAGE when the game is absent).
std::vector<std::byte> game_code(std::uint32_t rva, std::size_t len) {
  fs::path dir;
  if (const char* d = std::getenv("GD_DIR")) dir = d;
  else if (const char* h = std::getenv("HOME")) dir = fs::path(h) / ".local/share/Steam/steamapps/common/Grim Dawn";
  const fs::path dll = dir / "compat/Game.dll";
  if (!fs::exists(dll)) {
    MESSAGE("Game.dll not found at " << dll << " — skipped (set GD_DIR)");
    return {};
  }
  std::ifstream f(dll, std::ios::binary);
  std::vector<char> raw((std::istreambuf_iterator<char>(f)), {});
  auto u32 = [&](std::size_t off) {
    std::uint32_t v;
    std::memcpy(&v, raw.data() + off, 4);
    return v;
  };
  auto u16 = [&](std::size_t off) {
    std::uint16_t v;
    std::memcpy(&v, raw.data() + off, 2);
    return v;
  };
  const std::uint32_t pe = u32(0x3C);
  const std::uint16_t nsec = u16(pe + 6), opt = u16(pe + 20);
  for (std::uint16_t i = 0; i < nsec; ++i) {
    const std::size_t h = pe + 24 + opt + std::size_t{i} * 40;
    const std::uint32_t va = u32(h + 12), size = u32(h + 16), ptr = u32(h + 20);
    if (rva >= va && rva - va + len <= size) {
      std::vector<std::byte> b(len);
      std::memcpy(b.data(), raw.data() + ptr + (rva - va), len);
      return b;
    }
  }
  FAIL("rva not in a section");
  return {};
}

constexpr std::uint64_t kBase = 0x180000000;

TEST_CASE("lookup call: real Game.dll 1.3.1.1") {
  constexpr std::uint32_t kFn = 0x695E0;  // Character::GetEquipCharAttributes (re/Game.exports.tsv)
  const auto code = game_code(kFn, 0x100);
  if (code.empty()) return;
  const auto r = find_object_lookup_call(code, kBase + kFn);
  REQUIRE(r);
  CHECK(r->target == kBase + 0x19D40);
  CHECK(r->iat_slot == kBase + 0x609098);  // IAT: Engine.dll Singleton<ObjectManager>::Get
}

TEST_CASE("line push_back: decodes the call, rejects other bytes") {
  // lea rdx,[rbp-0x59]; mov rcx,rsi; call rel32 -0x100
  auto site = bytes({0x48, 0x8D, 0x55, 0xA7, 0x48, 0x8B, 0xCE, 0xE8, 0x00, 0xFF, 0xFF, 0xFF});
  CHECK(gdc::scan::line_push_back_call(site, 0x5000) == 0x5000 + 12 - 0x100);
  CHECK(!gdc::scan::line_push_back_call({site.data(), 11}, 0x5000));
  site[6] = std::byte{0xCF};  // mov rcx,rdi
  CHECK(!gdc::scan::line_push_back_call(site, 0x5000));
  CHECK(!gdc::scan::is_line_push_back(bytes({0x48, 0x89, 0x5c})));
}

TEST_CASE("line push_back: real Game.dll 1.3.1.1") {
  constexpr std::uint32_t kFn = 0x33DCE0;  // ItemEquipment::GetUIDisplayText
  const auto site = game_code(kFn + gdc::scan::kLinePushBackSite, gdc::scan::kLinePushBackSiteLen);
  if (site.empty()) return;
  const auto t = gdc::scan::line_push_back_call(site, kBase + kFn + gdc::scan::kLinePushBackSite);
  REQUIRE(t);
  CHECK(*t == kBase + 0x1C550);
  CHECK(gdc::scan::is_line_push_back(game_code(0x1C550, gdc::scan::kLinePushBackLen)));
  CHECK(!gdc::scan::is_line_push_back(game_code(0x1C4C0, gdc::scan::kLinePushBackLen)));
}
