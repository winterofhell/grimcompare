#include "gdc/text.h"

#include <cctype>
#include <cstring>
#include <fstream>
#include <stdexcept>

#include "gdc/arz.h"

namespace gdc::text {
namespace {

[[noreturn]] void fail(const std::string& what) { throw std::runtime_error("arc: " + what); }

template <class T>
T get(std::span<const std::byte> d, std::size_t off) {
  if (off > d.size() || sizeof(T) > d.size() - off) fail("truncated");
  T v;
  std::memcpy(&v, d.data() + off, sizeof v);
  return v;
}

std::span<const std::byte> sub(std::span<const std::byte> d, std::size_t off, std::size_t n) {
  if (off > d.size() || n > d.size() - off) fail("range out of file");
  return d.subspan(off, n);
}

std::vector<std::byte> slurp(const std::filesystem::path& p) {
  std::ifstream f(p, std::ios::binary);
  if (!f) throw std::runtime_error("cannot open " + p.string());
  std::vector<char> c((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
  std::vector<std::byte> out(c.size());
  std::memcpy(out.data(), c.data(), c.size());
  return out;
}

void add_archive(Table& t, const std::filesystem::path& p) {
  const auto data = slurp(p);
  for (const auto& [name, bytes] : read_arc(data)) {
    if (!name.ends_with(".txt") || name.find("console") != std::string::npos) continue;
    t.add({reinterpret_cast<const char*>(bytes.data()), bytes.size()});
  }
}

}  // namespace

// .arc v3 header, part and entry layout (§13.28).
std::vector<std::pair<std::string, std::vector<std::byte>>> read_arc(std::span<const std::byte> d) {
  if (d.size() < 28 || std::memcmp(d.data(), "ARC\0", 4) != 0) fail("not an .arc file");
  if (get<std::uint32_t>(d, 4) != 3) fail("not version 3");
  const auto files = get<std::uint32_t>(d, 8), parts = get<std::uint32_t>(d, 12);
  const auto part_size = get<std::uint32_t>(d, 16), str_size = get<std::uint32_t>(d, 20);
  const std::size_t table = get<std::uint32_t>(d, 24);
  if (static_cast<std::size_t>(parts) * 12 > part_size) fail("part table too small");
  const std::size_t strs = table + part_size, ents = strs + str_size;
  const auto names = sub(d, strs, str_size);
  std::vector<std::pair<std::string, std::vector<std::byte>>> out;
  for (std::uint32_t i = 0; i < files; ++i) {
    const std::size_t e = ents + 44 * static_cast<std::size_t>(i);
    const auto type = get<std::uint32_t>(d, e), off = get<std::uint32_t>(d, e + 4);
    const auto csz = get<std::uint32_t>(d, e + 8), dsz = get<std::uint32_t>(d, e + 12);
    const auto nparts = get<std::uint32_t>(d, e + 28), first = get<std::uint32_t>(d, e + 32);
    const auto nlen = get<std::uint32_t>(d, e + 36), noff = get<std::uint32_t>(d, e + 40);
    const auto n = sub(names, noff, nlen);
    std::string name(reinterpret_cast<const char*>(n.data()), n.size());
    std::vector<std::byte> bytes;
    if (type == 1 && csz == dsz) {
      const auto b = sub(d, off, dsz);
      bytes.assign(b.begin(), b.end());
    } else {
      if (first > parts || nparts > parts - first) fail("part index out of range");
      for (std::uint32_t k = first; k < first + nparts; ++k) {
        const std::size_t pe = table + 12 * static_cast<std::size_t>(k);
        const auto poff = get<std::uint32_t>(d, pe), pc = get<std::uint32_t>(d, pe + 4), pd = get<std::uint32_t>(d, pe + 8);
        const auto src = sub(d, poff, pc);
        if (pc == pd) bytes.insert(bytes.end(), src.begin(), src.end());
        else {
          const auto part = arz::lz4_block(src, pd);
          bytes.insert(bytes.end(), part.begin(), part.end());
        }
      }
      if (bytes.size() != dsz) fail("size mismatch for " + name);
    }
    out.emplace_back(std::move(name), std::move(bytes));
  }
  return out;
}

void Table::add(std::string_view file) {
  if (file.starts_with("\xEF\xBB\xBF")) file.remove_prefix(3);
  while (!file.empty()) {
    const auto nl = file.find('\n');
    std::string_view line = file.substr(0, nl);
    file.remove_prefix(nl == std::string_view::npos ? file.size() : nl + 1);
    if (line.ends_with('\r')) line.remove_suffix(1);
    if (line.empty() || line[0] == '#') continue;
    const auto eq = line.find('=');
    if (eq == 0 || eq == std::string_view::npos) continue;
    tags_.insert_or_assign(std::string(line.substr(0, eq)), std::string(line.substr(eq + 1)));
  }
}

Table Table::open_game(const std::filesystem::path& game_dir, std::string_view lang) {
  const std::string file = "Text_" + std::string(lang) + ".arc";
  Table t;
  add_archive(t, game_dir / "resources" / file);
  for (int i = 1; i <= 3; ++i) {
    const auto p = game_dir / ("gdx" + std::to_string(i)) / "resources" / file;
    if (std::filesystem::exists(p)) add_archive(t, p);
  }
  return t;
}

std::string_view Table::get(std::string_view tag) const {
  const auto it = tags_.find(tag);
  return it == tags_.end() ? std::string_view{} : std::string_view(it->second);
}

std::string plain(std::string_view s) {
  std::string out;
  for (std::size_t i = 0; i < s.size(); ++i) {
    if (s[i] == '{') {
      const auto close = s.find('}', i);
      if (close == std::string_view::npos) break;
      i = close;
      continue;
    }
    out += s[i];
  }
  std::size_t b = 0;
  while (b < out.size() && !std::isalpha(static_cast<unsigned char>(out[b]))) ++b;
  std::size_t e = out.size();
  while (e > b && out[e - 1] == ' ') --e;
  return out.substr(b, e - b);
}

}  // namespace gdc::text
