#pragma once
// Grim Dawn game database (.arz v3) reader. Layout: re/tools/arz.py (recon reader that parsed every
// record of database.arz and GDX1..3.arz in 1.3.1.1); records are LZ4 blocks of
// {u16 type, u16 count, u32 key string index, count x 4-byte values}. Throws std::runtime_error on
// malformed input; never reads out of bounds.
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <map>
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
  static File open(const std::filesystem::path& path);

  bool contains(std::string_view name) const { return index_.contains(std::string(name)); }
  std::size_t size() const { return index_.size(); }
  std::optional<Record> record(std::string_view name) const;

 private:
  struct Entry {
    std::string cls;
    std::uint32_t offset, compressed, decompressed;
  };
  std::vector<std::byte> data_;
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

  std::optional<Record> record(std::string_view name) const;
  std::size_t files() const { return files_.size(); }

 private:
  std::vector<File> files_;
};

}  // namespace gdc::arz
