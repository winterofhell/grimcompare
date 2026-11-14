#pragma once
// Grim Dawn game database (.arz v3) reader. Layout: re/tools/arz.py (recon reader that parsed every
// record of database.arz and GDX1..3.arz in 1.3.1.1); records are LZ4 blocks of
// {u16 type, u16 count, u32 key string index, count x 4-byte values}. Throws std::runtime_error on
// malformed input; never reads out of bounds.
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

// Raw LZ4 block (no frame), as used by .arz records and .arc parts. Output must be exactly out_size.
std::vector<std::byte> lz4_block(std::span<const std::byte> src, std::size_t out_size);

struct Field {
  enum class Kind : std::uint16_t { Int = 0, Float = 1, String = 2, Bool = 3 };
  Kind kind = Kind::Int;
  std::vector<double> numbers;       // Int / Float / Bool values
  std::vector<std::string> strings;  // String values
};

struct Record {
  std::string cls;  // record type, e.g. "ArmorJewelry_Ring"
  std::map<std::string, Field, std::less<>> fields;

  // Value i of a numeric field (per-level arrays are indexed by level - 1); `fallback` if absent.
  double number(std::string_view key, std::size_t i = 0, double fallback = 0) const;
  // Value i of a string field; empty if absent.
  std::string_view string(std::string_view key, std::size_t i = 0) const;
};

class File {
 public:
  explicit File(std::vector<std::byte> data);
  // Memory-mapped read-only (mmap / MapViewOfFile): only the pages a lookup touches are read, and they
  // stay file-backed (the adapter keeps the 175 MB of 1.3.1.1 databases out of the game's private memory).
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
  std::shared_ptr<const void> owner_;  // keeps data_ alive (a vector or a mapping)
  std::span<const std::byte> data_;
  std::vector<std::string> strings_;
  std::unordered_map<std::string, Entry> index_;
};

// Base database plus expansions; a record in a later file replaces the earlier one entirely.
// Verified against snapshots (docs/DATABASE.md): skill bonuses present only in the GDX2/GDX3 copies of
// equipped items show up in the character's augmented skill levels.
class Database {
 public:
  void add(File f) { files_.push_back(std::move(f)); }
  // database/database.arz then gdx1..gdx3/database/GDX<n>.arz that exist; throws if the base is missing.
  static Database open_game(const std::filesystem::path& game_dir);

  // Decoded once, then cached for the database's lifetime (a decode is ~60 us: LZ4 + field map); null if
  // absent. Thread-safe; the pointer stays valid while the database lives. Do not add() after a lookup.
  const Record* record(std::string_view name) const;
  std::size_t files() const { return files_.size(); }

 private:
  struct Cache {
    // shortcut: a spin lock, not std::mutex: the adapter runs on the Proton prefix's 2016 msvcp140.dll,
    // where a std::mutex built with current MSVC headers faults on its first lock (adapter 0.4.0 publish
    // fault, docs/BUILD.md). Held only for a map find / emplace — revisit if tooltip timings show contention.
    std::atomic_flag lock;  // clear (C++20 default)
    std::unordered_map<std::string, std::unique_ptr<const Record>> records;  // null: absent
  };
  std::vector<File> files_;
  std::unique_ptr<Cache> cache_ = std::make_unique<Cache>();
};

}  // namespace gdc::arz
