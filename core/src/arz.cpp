#include "gdc/arz.h"

#include <cstring>
#include <stdexcept>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace gdc::arz {
namespace {

[[noreturn]] void fail(const std::string& what) { throw std::runtime_error("arz: " + what); }

struct Reader {
  std::span<const std::byte> d;
  std::size_t p = 0;

  void need(std::size_t n) const {
    if (n > d.size() || p > d.size() - n) fail("truncated");
  }
  template <class T>
  T get() {
    need(sizeof(T));
    T v;
    std::memcpy(&v, d.data() + p, sizeof v);
    p += sizeof v;
    return v;
  }
  std::string str(std::size_t n) {
    need(n);
    std::string s(reinterpret_cast<const char*>(d.data() + p), n);
    p += n;
    return s;
  }
};

constexpr std::size_t kHeader = 24;  // record data offsets are relative to the end of the header

}  // namespace

std::vector<std::byte> lz4_block(std::span<const std::byte> src, std::size_t out_size) {
  std::vector<std::byte> out;
  out.reserve(out_size);
  std::size_t i = 0;
  auto byte = [&] {
    if (i >= src.size()) fail("lz4: truncated");
    return std::to_integer<std::size_t>(src[i++]);
  };
  auto length = [&](std::size_t n) {
    if (n == 15)
      for (std::size_t b = 255; b == 255;) n += (b = byte());
    return n;
  };
  while (i < src.size()) {
    const std::size_t token = byte();
    const std::size_t lit = length(token >> 4);
    if (lit > src.size() - i || lit > out_size - out.size()) fail("lz4: literal overrun");
    out.insert(out.end(), src.begin() + static_cast<std::ptrdiff_t>(i), src.begin() + static_cast<std::ptrdiff_t>(i + lit));
    i += lit;
    if (i == src.size()) break;  // last sequence: literals only
    const std::size_t off = byte() | (byte() << 8);
    const std::size_t match = length(token & 15) + 4;
    if (off == 0 || off > out.size()) fail("lz4: bad offset");
    if (match > out_size - out.size()) fail("lz4: match overrun");
    const std::size_t from = out.size() - off;
    for (std::size_t k = 0; k < match; ++k) out.push_back(out[from + k]);  // may overlap
  }
  if (out.size() != out_size) fail("lz4: size mismatch");
  return out;
}

double Record::number(std::string_view key, std::size_t i, double fallback) const {
  const auto it = fields.find(key);
  if (it == fields.end() || i >= it->second.numbers.size()) return fallback;
  return it->second.numbers[i];
}

std::string_view Record::string(std::string_view key, std::size_t i) const {
  const auto it = fields.find(key);
  if (it == fields.end() || i >= it->second.strings.size()) return {};
  return it->second.strings[i];
}

File::File(std::vector<std::byte> data) {
  auto v = std::make_shared<const std::vector<std::byte>>(std::move(data));
  data_ = *v;
  owner_ = std::move(v);
  index();
}

void File::index() {
  Reader h{data_};
  h.get<std::uint16_t>();
  if (h.get<std::uint16_t>() != 3) fail("not a version 3 .arz");
  const auto rec_start = h.get<std::uint32_t>(), rec_size = h.get<std::uint32_t>(), rec_count = h.get<std::uint32_t>();
  const auto str_start = h.get<std::uint32_t>(), str_size = h.get<std::uint32_t>();
  if (str_start > data_.size() || str_size > data_.size() - str_start) fail("string table out of range");
  if (rec_start > data_.size() || rec_size > data_.size() - rec_start) fail("record table out of range");

  Reader s{data_.subspan(str_start, str_size)};
  while (s.p < s.d.size()) {
    const auto n = s.get<std::uint32_t>();
    for (std::uint32_t k = 0; k < n; ++k) strings_.push_back(s.str(s.get<std::uint32_t>()));
  }

  Reader r{data_.subspan(rec_start, rec_size)};
  for (std::uint32_t k = 0; k < rec_count; ++k) {
    const auto name = r.get<std::uint32_t>();
    Entry e;
    e.cls = r.str(r.get<std::uint32_t>());
    e.offset = r.get<std::uint32_t>();
    e.compressed = r.get<std::uint32_t>();
    e.decompressed = r.get<std::uint32_t>();
    r.get<std::uint64_t>();  // file time
    if (name >= strings_.size()) fail("record name index out of range");
    if (kHeader + std::size_t{e.offset} + e.compressed > data_.size()) fail("record data out of range");
    index_.insert_or_assign(strings_[name], std::move(e));
  }
}

File File::open(const std::filesystem::path& path) {
  File f;
  std::size_t size = 0;
#ifdef _WIN32
  HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (h == INVALID_HANDLE_VALUE) fail("cannot open " + path.string());
  LARGE_INTEGER li{};
  const bool sized = GetFileSizeEx(h, &li) != 0;
  HANDLE m = sized && li.QuadPart > 0 ? CreateFileMappingW(h, nullptr, PAGE_READONLY, 0, 0, nullptr) : nullptr;
  CloseHandle(h);  // the mapping keeps the file open
  if (!m) fail("cannot map " + path.string());
  const void* view = MapViewOfFile(m, FILE_MAP_READ, 0, 0, 0);
  CloseHandle(m);  // the view keeps the mapping
  if (!view) fail("cannot map " + path.string());
  size = static_cast<std::size_t>(li.QuadPart);
  f.owner_ = std::shared_ptr<const void>(view, [](const void* p) { UnmapViewOfFile(p); });
#else
  const int fd = ::open(path.c_str(), O_RDONLY);
  if (fd < 0) fail("cannot open " + path.string());
  struct stat st {};
  void* view = fstat(fd, &st) == 0 && st.st_size > 0
                   ? mmap(nullptr, static_cast<std::size_t>(st.st_size), PROT_READ, MAP_PRIVATE, fd, 0)
                   : MAP_FAILED;
  ::close(fd);  // the mapping keeps the file
  if (view == MAP_FAILED) fail("cannot map " + path.string());
  size = static_cast<std::size_t>(st.st_size);
  f.owner_ = std::shared_ptr<const void>(view, [size](const void* p) { munmap(const_cast<void*>(p), size); });
#endif
  f.data_ = {static_cast<const std::byte*>(f.owner_.get()), size};
  f.index();
  return f;
}

std::optional<Record> File::record(std::string_view name) const {
  const auto it = index_.find(std::string(name));
  if (it == index_.end()) return std::nullopt;
  const Entry& e = it->second;
  const auto raw = lz4_block(data_.subspan(kHeader + e.offset, e.compressed), e.decompressed);
  Record rec;
  rec.cls = e.cls;
  Reader r{raw};
  while (r.p < raw.size()) {
    const auto type = r.get<std::uint16_t>(), count = r.get<std::uint16_t>();
    const auto key = r.get<std::uint32_t>();
    if (key >= strings_.size()) fail("field key index out of range");
    if (type > 3) fail("unknown field type " + std::to_string(type));
    Field f;
    f.kind = static_cast<Field::Kind>(type);
    for (std::uint16_t k = 0; k < count; ++k) {
      if (f.kind == Field::Kind::Float) {
        f.numbers.push_back(r.get<float>());
      } else if (f.kind == Field::Kind::String) {
        const auto idx = r.get<std::uint32_t>();
        if (idx >= strings_.size()) fail("string value index out of range");
        f.strings.push_back(strings_[idx]);
      } else {
        f.numbers.push_back(r.get<std::int32_t>());
      }
    }
    rec.fields.insert_or_assign(strings_[key], std::move(f));
  }
  return rec;
}

Database Database::open_game(const std::filesystem::path& game_dir) {
  Database db;
  db.add(File::open(game_dir / "database" / "database.arz"));
  for (int n = 1; n <= 3; ++n) {
    const auto p = game_dir / ("gdx" + std::to_string(n)) / "database" / ("GDX" + std::to_string(n) + ".arz");
    if (std::filesystem::exists(p)) db.add(File::open(p));
  }
  return db;
}

namespace {

struct Spin {
  std::atomic_flag& f;
  explicit Spin(std::atomic_flag& flag) : f(flag) {
    while (f.test_and_set(std::memory_order_acquire)) {
    }
  }
  ~Spin() { f.clear(std::memory_order_release); }
  Spin(const Spin&) = delete;
  Spin& operator=(const Spin&) = delete;
};

}  // namespace

const Record* Database::record(std::string_view name) const {
  {
    Spin l(cache_->lock);
    if (const auto it = cache_->records.find(std::string(name)); it != cache_->records.end()) return it->second.get();
  }
  std::unique_ptr<const Record> rec;  // decoded outside the lock; a racing decode of the same name is dropped
  for (auto it = files_.rbegin(); it != files_.rend(); ++it)
    if (it->contains(name)) {
      if (auto r = it->record(name)) rec = std::make_unique<const Record>(std::move(*r));
      break;
    }
  Spin l(cache_->lock);
  return cache_->records.try_emplace(std::string(name), std::move(rec)).first->second.get();
}

}  // namespace gdc::arz
