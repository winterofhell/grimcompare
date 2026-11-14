#include <doctest/doctest.h>

#include <cstring>
#include <vector>

#include "gdc/pe_exports.h"

using gdc::pe::Layout;
using gdc::pe::read_exports;

namespace {

struct Buf {
  std::vector<std::byte> b;
  explicit Buf(std::size_t n) : b(n) {}
  void u16(std::size_t o, std::uint16_t v) { std::memcpy(&b[o], &v, 2); }
  void u32(std::size_t o, std::uint32_t v) { std::memcpy(&b[o], &v, 4); }
  void str(std::size_t o, const char* s) { std::memcpy(&b[o], s, std::strlen(s) + 1); }
};

// Smallest PE32+ with one section (VA 0x1000, file offset 0x200) holding an export
// directory: Alpha and Beta share RVA 0x2000 (ICF-style), Fwd is a forwarder.
Buf make_file() {
  Buf f(0x600);
  f.u16(0, 0x5A4D);
  f.u32(0x3C, 0x40);
  f.u32(0x40, 0x00004550);
  f.u16(0x44, 0x8664);  // machine
  f.u16(0x46, 1);       // sections
  f.u16(0x54, 0xF0);    // optional header size
  const std::size_t opt = 0x58;
  f.u16(opt, 0x20B);
  f.u32(opt + 108, 16);
  f.u32(opt + 112, 0x1000);  // export dir RVA
  f.u32(opt + 116, 0x200);   // export dir size
  const std::size_t sec = opt + 0xF0;
  f.u32(sec + 8, 0x400);
  f.u32(sec + 12, 0x1000);
  f.u32(sec + 16, 0x400);
  f.u32(sec + 20, 0x200);

  auto at = [](std::uint32_t rva) { return std::size_t{rva - 0x1000u + 0x200u}; };
  f.u32(at(0x1000) + 20, 3);
  f.u32(at(0x1000) + 24, 3);
  f.u32(at(0x1000) + 28, 0x1040);
  f.u32(at(0x1000) + 32, 0x1050);
  f.u32(at(0x1000) + 36, 0x1060);
  f.u32(at(0x1040), 0x2000);
  f.u32(at(0x1044), 0x2000);
  f.u32(at(0x1048), 0x1100);
  f.u32(at(0x1050), 0x1080);
  f.u32(at(0x1054), 0x1088);
  f.u32(at(0x1058), 0x1090);
  f.u16(at(0x1060), 0);
  f.u16(at(0x1062), 1);
  f.u16(at(0x1064), 2);
  f.str(at(0x1080), "Alpha");
  f.str(at(0x1088), "Beta");
  f.str(at(0x1090), "Fwd");
  f.str(at(0x1100), "OTHER.Func");
  return f;
}

Buf make_mapped(const Buf& file) {
  Buf m(0x2000);
  std::memcpy(&m.b[0], &file.b[0], 0x200);
  std::memcpy(&m.b[0x1000], &file.b[0x200], 0x400);
  return m;
}

void check_table(const gdc::pe::ExportTable& t) {
  REQUIRE(t.error.empty());
  REQUIRE(t.exports.size() == 3);
  CHECK(t.exports[0].name == "Alpha");
  CHECK(t.exports[0].rva == 0x2000);
  CHECK_FALSE(t.exports[0].forwarded);
  CHECK(t.exports[1].name == "Beta");
  CHECK(t.exports[1].rva == 0x2000);
  CHECK(t.exports[2].name == "Fwd");
  CHECK(t.exports[2].forwarded);
}

}  // namespace

TEST_CASE("file layout translates RVAs through the section table") { check_table(read_exports(make_file().b, Layout::File)); }

TEST_CASE("mapped layout reads RVAs directly") { check_table(read_exports(make_mapped(make_file()).b, Layout::Mapped)); }

TEST_CASE("malformed images fail closed with an error") {
  CHECK_FALSE(read_exports({}, Layout::File).error.empty());

  auto f = make_file();
  f.u16(0, 0);
  CHECK(read_exports(f.b, Layout::File).error == "no MZ header");

  f = make_file();
  f.u16(0x58, 0x10B);
  CHECK(read_exports(f.b, Layout::File).error == "not PE32+");

  f = make_file();
  f.u16(0x200 + 0x60, 7);  // ordinal beyond NumberOfFunctions
  CHECK(read_exports(f.b, Layout::File).error == "ordinal out of range");

  f = make_file();
  f.b.resize(0x290);  // cuts into the name strings
  CHECK_FALSE(read_exports(f.b, Layout::File).error.empty());

  f = make_file();
  std::memset(&f.b[0x300], 'x', 0x100);  // no NUL from file offset 0x300 to EOF
  f.u32(0x200 + 0x50, 0x11FF);           // first name → file offset 0x3FF
  f.b.resize(0x400);
  CHECK(read_exports(f.b, Layout::File).error == "unterminated export name");
}

TEST_CASE("image without export directory is valid and empty") {
  auto f = make_file();
  f.u32(0x58 + 112, 0);
  f.u32(0x58 + 116, 0);
  const auto t = read_exports(f.b, Layout::File);
  CHECK(t.error.empty());
  CHECK(t.exports.empty());
}
