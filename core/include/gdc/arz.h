#pragma once
// .arz v3 layout: re/tools/arz.py (parsed every record of 1.3.1.1 database.arz and GDX1..3). Records are LZ4
// blocks of {u16 type, u16 count, u32 key string index, count x 4-byte values}. Never reads out of bounds.
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <map>
#include <atomic>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace gdc::arz {

std::vector<std::byte> lz4_block(std::span<const std::byte> src, std::size_t out_size);

struct Field {
  enum class Kind : std::uint16_t { Int = 0, Float = 1, String = 2, Bool = 3 };
  Kind kind = Kind::Int;
  std::vector<double> numbers;
  std::vector<std::string> strings;
};

struct Record {
  std::string cls;
  std::map<std::string, Field, std::less<>> fields;

  // Per-level arrays are indexed by level - 1.
  double number(std::string_view key, std::size_t i = 0, double fallback = 0) const;
  std::string_view string(std::string_view key, std::size_t i = 0) const;
};

class File {
 public:
  explicit File(std::vector<std::byte> data);
  // Memory-mapped so the 175 MB of databases stay file-backed, out of the game's private memory.
  static File open(const std::filesystem::path& path);

  bool contains(std::string_view name) const { return index_.contains(std::string(name)); }
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
  std::unordered_map<std::string, Entry> index_;
};

// A later file replaces a record entirely. Verified: skill bonuses only in the GDX2/GDX3 copies show up in
// augmented skill levels (docs/DATABASE.md).
class Database {
 public:
  void add(File f) { files_.push_back(std::move(f)); }
  static Database open_game(const std::filesystem::path& game_dir);

  // Cached for the database's lifetime (a decode is ~60 us). Do not add() after a lookup.
  const Record* record(std::string_view name) const;
  std::size_t files() const { return files_.size(); }

 private:
  struct Cache {
    // shortcut: spin lock instead of std::mutex, which faults on the Proton prefix's 2016 msvcp140.dll
    // (docs/BUILD.md). Held only for a map find / emplace — revisit if tooltip timings show contention.
    std::atomic_flag lock;
    std::unordered_map<std::string, std::unique_ptr<const Record>> records;  // null: absent
  };
  std::vector<File> files_;
  std::unique_ptr<Cache> cache_ = std::make_unique<Cache>();
};

}  // namespace gdc::arz
