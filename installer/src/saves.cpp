#include "saves.h"

#include <algorithm>
#include <array>
#include <fstream>
#include <iterator>

#include "gdc/snapshot.h"
#include "platform.h"

namespace setup {

namespace fs = std::filesystem;

namespace {

constexpr std::uint32_t kMagic = 0x58434447;  // "GDCX"
constexpr std::uint32_t kMaxString = 1024;    // sanity bound on name / tag length, not a game limit

class Reader {
 public:
  explicit Reader(std::span<const unsigned char> d) : d_(d) {
    if (d_.size() < 4) return;
    key_ = le32(0) ^ 0x55555555u;
    std::uint32_t k = key_;
    for (auto& t : table_) {
      k = (k >> 1) | (k << 31);
      k *= 39916801u;
      t = k;
    }
    pos_ = 4;
    ok_ = true;
  }
  bool ok() const { return ok_; }
  std::uint32_t u32() {
    if (!need(4)) return 0;
    const std::uint32_t v = le32(pos_) ^ key_;
    for (int i = 0; i < 4; ++i) mix(d_[pos_ + static_cast<std::size_t>(i)]);
    pos_ += 4;
    return v;
  }
  std::uint8_t u8() {
    if (!need(1)) return 0;
    const unsigned char raw = d_[pos_++];
    const auto v = static_cast<std::uint8_t>((raw ^ key_) & 0xFF);
    mix(raw);
    return v;
  }

 private:
  bool need(std::size_t n) {
    if (ok_ && d_.size() - pos_ >= n) return true;
    ok_ = false;
    return false;
  }
  std::uint32_t le32(std::size_t at) const {
    return std::uint32_t(d_[at]) | std::uint32_t(d_[at + 1]) << 8 | std::uint32_t(d_[at + 2]) << 16 | std::uint32_t(d_[at + 3]) << 24;
  }
  void mix(unsigned char raw) { key_ ^= table_[raw]; }

  std::span<const unsigned char> d_;
  std::size_t pos_ = 0;
  std::uint32_t key_ = 0;
  std::array<std::uint32_t, 256> table_{};
  bool ok_ = false;
};

void utf8(std::string& out, std::uint32_t c) {
  if (c < 0x80) out += static_cast<char>(c);
  else if (c < 0x800) out += static_cast<char>(0xC0 | c >> 6), out += static_cast<char>(0x80 | (c & 0x3F));
  else if (c < 0x10000)
    out += static_cast<char>(0xE0 | c >> 12), out += static_cast<char>(0x80 | (c >> 6 & 0x3F)), out += static_cast<char>(0x80 | (c & 0x3F));
  else
    out += static_cast<char>(0xF0 | c >> 18), out += static_cast<char>(0x80 | (c >> 12 & 0x3F)),
        out += static_cast<char>(0x80 | (c >> 6 & 0x3F)), out += static_cast<char>(0x80 | (c & 0x3F));
}

}  // namespace

std::optional<SaveHeader> read_save_header(std::span<const unsigned char> data, std::string& error) {
  Reader r(data);
  if (!r.ok()) return error = "file too short", std::nullopt;
  if (r.u32() != kMagic) return error = "not a Grim Dawn character file (magic)", std::nullopt;
  const std::uint32_t version = r.u32();
  if (version < 1 || version > 2) return error = "unknown character file version " + std::to_string(version), std::nullopt;
  SaveHeader h;
  const std::uint32_t name_len = r.u32();
  if (!r.ok() || name_len == 0 || name_len > kMaxString) return error = "bad name length", std::nullopt;
  std::vector<std::uint32_t> units;
  for (std::uint32_t i = 0; i < name_len; ++i) {
    const std::uint32_t lo = r.u8(), hi = r.u8();
    units.push_back(lo | hi << 8);
  }
  for (std::size_t i = 0; i < units.size(); ++i) {
    std::uint32_t c = units[i];
    if (c >= 0xD800 && c < 0xDC00 && i + 1 < units.size() && units[i + 1] >= 0xDC00 && units[i + 1] < 0xE000)
      c = 0x10000 + ((c - 0xD800) << 10) + (units[++i] - 0xDC00);
    utf8(h.name, c);
  }
  r.u8();  // sex
  const std::uint32_t tag_len = r.u32();
  if (!r.ok() || tag_len > kMaxString) return error = "bad class tag length", std::nullopt;
  for (std::uint32_t i = 0; i < tag_len; ++i) h.class_tag += static_cast<char>(r.u8());
  h.level = r.u32();
  h.hardcore = r.u8() != 0;
  if (!r.ok()) return error = "file ends inside the header", std::nullopt;
  if (h.level == 0 || h.level > 1000) return error = "bad level " + std::to_string(h.level), std::nullopt;
  return h;
}

std::vector<int> class_masteries(std::string_view tag) {
  constexpr std::string_view kPrefix = "tagSkillClassName";
  if (!tag.starts_with(kPrefix)) return {};
  const std::string_view d = tag.substr(kPrefix.size());
  if ((d.size() != 2 && d.size() != 4) || !std::all_of(d.begin(), d.end(), [](char c) { return c >= '0' && c <= '9'; }))
    return {};
  std::vector<int> out;
  for (std::size_t i = 0; i < d.size(); i += 2) {
    const int m = (d[i] - '0') * 10 + (d[i + 1] - '0');
    if (m < 1 || m > 10) return {};
    out.push_back(m);
  }
  return out;
}

std::vector<fs::path> save_roots(const std::vector<fs::path>& steam_roots, const std::vector<fs::path>& libraries) {
  std::vector<fs::path> out;
  std::error_code ec;
  for (const auto& s : steam_roots)
    for (const auto& u : fs::directory_iterator(s / "userdata", ec)) out.push_back(u.path() / "219990" / "remote" / "save");
  const fs::path my_games = fs::path("My Games") / "Grim Dawn" / "save";
  for (const auto& lib : libraries)
    out.push_back(lib / "steamapps" / "compatdata" / "219990" / "pfx" / "drive_c" / "users" / "steamuser" / "Documents" / my_games);
  if (const fs::path docs = documents_dir(); !docs.empty()) out.push_back(docs / my_games);
  std::erase_if(out, [](const fs::path& p) {
    std::error_code e;
    return !fs::is_directory(p / "main", e);
  });
  return out;
}

std::vector<Character> find_characters(const std::vector<fs::path>& roots) {
  std::vector<Character> out;
  for (const auto& root : roots) {
    const bool cloud = root.parent_path().filename() == "remote";
    std::error_code ec;
    for (const auto& d : fs::directory_iterator(root / "main", ec)) {
      const fs::path file = d.path() / "player.gdc";
      std::ifstream f(file, std::ios::binary);
      if (!f) continue;
      std::vector<unsigned char> head(4096);  // the header is well under this
      f.read(reinterpret_cast<char*>(head.data()), static_cast<std::streamsize>(head.size()));
      head.resize(static_cast<std::size_t>(f.gcount()));
      std::string error;
      if (auto h = read_save_header(head, error))
        out.push_back({std::move(*h), file, gdc::snap::save_key(cloud, u8string(d.path().filename()))});
    }
  }
  std::sort(out.begin(), out.end(), [](const Character& a, const Character& b) {
    return a.header.name != b.header.name ? a.header.name < b.header.name : a.header.level > b.header.level;
  });
  return out;
}

}  // namespace setup
