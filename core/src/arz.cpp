#include "gdc/arz.h"

#include <algorithm>
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

constexpr std::size_t kHeader = 24;  // record data offsets count from the header's end

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

namespace {

// Cheap hash over length, head, middle and tail (§13.23).
std::size_t key_hash(std::string_view s) {
  auto load = [&](std::size_t at) {
    std::uint64_t v;
    std::memcpy(&v, s.data() + at, 8);
    return v;
  };
  auto mix = [](std::uint64_t h) {
    h ^= h >> 31;
    h *= 0x9E3779B97F4A7C15ull;
    return h ^ (h >> 29);
  };
  std::uint64_t h = s.size();
  if (s.size() >= 8) {
    h = mix(h ^ load(0));
    h = mix(h ^ load(s.size() / 2 - 4));
    h = mix(h ^ load(s.size() - 8));
  } else {
    for (const char c : s) h = (h << 8) | static_cast<unsigned char>(c);
    h = mix(h);
  }
  return static_cast<std::size_t>(mix(h));
}

}  // namespace

Fields::const_iterator Fields::find(std::string_view key) const {
  if (slots_.empty()) return end();
  const std::size_t mask = slots_.size() - 1;
  for (std::size_t h = key_hash(key) & mask;; h = (h + 1) & mask) {
    const std::uint32_t i = slots_[h];
    if (i == 0) return end();
    if (items_[i - 1].first == key) return items_.begin() + (i - 1);
  }
}

const Field& Fields::at(std::string_view key) const {
  const auto it = find(key);
  if (it == end()) throw std::out_of_range("no field " + std::string(key));
  return it->second;
}

void Fields::insert_or_assign(std::string key, Field f) {
  const auto it = std::lower_bound(items_.begin(), items_.end(), key, [](const value_type& x, const std::string& k) { return x.first < k; });
  if (it != items_.end() && it->first == key) {
    it->second = std::move(f);
    return;
  }
  items_.insert(it, {std::move(key), std::move(f)});
  reindex();
}

void Fields::emplace(std::string key, Field f) {
  if (!contains(key)) insert_or_assign(std::move(key), std::move(f));
}

void Fields::assign(std::vector<value_type> v) {
  std::stable_sort(v.begin(), v.end(), [](const value_type& a, const value_type& b) { return a.first < b.first; });
  std::size_t w = 0;
  for (std::size_t i = 0; i < v.size(); ++i) {
    if (w > 0 && v[w - 1].first == v[i].first) {
      v[w - 1].second = std::move(v[i].second);
      continue;
    }
    if (w != i) v[w] = std::move(v[i]);
    ++w;
  }
  v.resize(w);
  items_ = std::move(v);
  reindex();
}

void Fields::reindex() {
  if (items_.size() >= 0x40000000) fail("record has too many fields");
  std::size_t n = 4;
  while (n < items_.size() * 2) n *= 2;
  slots_.assign(items_.empty() ? 0 : n, 0);
  for (std::size_t i = 0; i < items_.size(); ++i)
    for (std::size_t h = key_hash(items_[i].first) & (n - 1);; h = (h + 1) & (n - 1))
      if (slots_[h] == 0) {
        slots_[h] = static_cast<std::uint32_t>(i + 1);
        break;
      }
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
  std::vector<Fields::value_type> fields;
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
    fields.emplace_back(strings_[key], std::move(f));
  }
  rec.fields.assign(std::move(fields));
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
  if (name.empty()) return nullptr;  // an absent reference field (buffSkillName, petBonusName...)
  {
    Spin l(cache_->lock);
    if (const auto it = cache_->records.find(name); it != cache_->records.end()) return it->second.get();
  }
  std::unique_ptr<const Record> rec;  // decoded outside the lock; a racing duplicate is dropped
  for (auto it = files_.rbegin(); it != files_.rend(); ++it)
    if (it->contains(name)) {
      if (auto r = it->record(name)) rec = std::make_unique<const Record>(std::move(*r));
      break;
    }
  Spin l(cache_->lock);
  return cache_->records.try_emplace(std::string(name), std::move(rec)).first->second.get();
}

const void* Database::find_derived(const void* tag, const Record* r, std::int64_t n) const {
  Spin l(cache_->lock);
  const auto it = cache_->derived.find({tag, r, n});
  return it != cache_->derived.end() ? it->second.get() : nullptr;
}

const void* Database::add_derived(const void* tag, const Record* r, std::int64_t n, std::shared_ptr<const void> v) const {
  Spin l(cache_->lock);
  return cache_->derived.try_emplace({tag, r, n}, std::move(v)).first->second.get();
}

}  // namespace gdc::arz
