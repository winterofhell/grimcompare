#pragma once
// .arz v3 reader (re/tools/arz.py); never reads out of bounds.
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <atomic>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace gdc::arz {

std::vector<std::byte> lz4_block(std::span<const std::byte> src, std::size_t out_size);

struct Field {
  enum class Kind : std::uint16_t { Int = 0, Float = 1, String = 2, Bool = 3 };
  Kind kind = Kind::Int;
  std::vector<double> numbers;
  std::vector<std::string> strings;
};

// Lets std::string-keyed maps find by string_view without building a string.
struct StringHash {
  using is_transparent = void;
  std::size_t operator()(std::string_view s) const { return std::hash<std::string_view>{}(s); }
};

// Fields sorted by name, hash-indexed for speed.
class Fields {
 public:
  using value_type = std::pair<std::string, Field>;
  using const_iterator = std::vector<value_type>::const_iterator;
  const_iterator begin() const { return items_.begin(); }
  const_iterator end() const { return items_.end(); }
  std::size_t size() const { return items_.size(); }
  bool empty() const { return items_.empty(); }
  const_iterator find(std::string_view key) const;
  bool contains(std::string_view key) const { return find(key) != end(); }
  const Field& at(std::string_view key) const;  // throws std::out_of_range
  void insert_or_assign(std::string key, Field f);
  void emplace(std::string key, Field f);  // keeps an existing field, as std::map
  // Fields in any order; a later duplicate name wins.
  void assign(std::vector<value_type> v);

 private:
  void reindex();
  std::vector<value_type> items_;
  std::vector<std::uint32_t> slots_;  // index + 1 into items_, 0 empty; power-of-two size
};

struct Record {
  std::string cls;
  Fields fields;

  // Per-level arrays are indexed by level - 1.
  double number(std::string_view key, std::size_t i = 0, double fallback = 0) const;
  std::string_view string(std::string_view key, std::size_t i = 0) const;
};

class File {
 public:
  explicit File(std::vector<std::byte> data);
  // Memory-mapped: 175 MB stays file-backed, outside game private memory.
  static File open(const std::filesystem::path& path);

  bool contains(std::string_view name) const { return index_.contains(name); }
  std::size_t size() const { return index_.size(); }
  std::optional<Record> record(std::string_view name) const;

 private:
  struct Entry {
    std::string cls;
    std::uint32_t offset, compressed, decompressed;
  };
  File() = default;
  void index();
  std::shared_ptr<const void> owner_;
  std::span<const std::byte> data_;
  std::vector<std::string> strings_;
  std::unordered_map<std::string, Entry, StringHash, std::equal_to<>> index_;
};

// A later file replaces a whole record.
class Database {
 public:
  void add(File f) { files_.push_back(std::move(f)); }
  static Database open_game(const std::filesystem::path& game_dir);

  // Cached for the database's lifetime; no add() after a lookup.
  const Record* record(std::string_view name) const;
  std::size_t files() const { return files_.size(); }

  // make()'s result per record and name, cached: scoring repeats it.
  template <class T, class F>
  const T& derived(const Record& r, std::int64_t n, F make) const {
    static constexpr char tag = 0;  // one per T and make()
    if (const void* v = find_derived(&tag, &r, n)) return *static_cast<const T*>(v);
    return *static_cast<const T*>(add_derived(&tag, &r, n, std::make_shared<const T>(make())));
  }

 private:
  const void* find_derived(const void* tag, const Record* r, std::int64_t n) const;
  // A racing computation of the same key is dropped.
  const void* add_derived(const void* tag, const Record* r, std::int64_t n, std::shared_ptr<const void> v) const;
  struct Cache {
    // shortcut: spin lock instead of std::mutex, which faults on the Proton prefix's 2016 msvcp140.dll
    // (docs/BUILD.md). Held only for a map find / emplace — revisit if tooltip timings show contention.
    std::atomic_flag lock;
    std::unordered_map<std::string, std::unique_ptr<const Record>, StringHash, std::equal_to<>> records;  // null: absent
    struct Key {
      const void* tag;
      const Record* record;
      std::int64_t n;
      bool operator==(const Key&) const = default;
    };
    struct KeyHash {
      std::size_t operator()(const Key& k) const {
        return std::hash<const void*>{}(k.tag) ^ std::hash<const void*>{}(k.record) * 31 ^ std::hash<std::int64_t>{}(k.n) * 131;
      }
    };
    std::unordered_map<Key, std::shared_ptr<const void>, KeyHash> derived;
  };
  std::vector<File> files_;
  std::unique_ptr<Cache> cache_ = std::make_unique<Cache>();
};

}  // namespace gdc::arz
