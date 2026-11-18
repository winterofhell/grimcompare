#pragma once
// .arc v3 reader (re/tools/arc.py); later files win, assumed.
#include <cstddef>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace gdc::text {

std::vector<std::pair<std::string, std::vector<std::byte>>> read_arc(std::span<const std::byte> data);

class Table {
 public:
  void add(std::string_view file);
  // Skips files named "console" (console wording of the same tags).
  static Table open_game(const std::filesystem::path& game_dir, std::string_view lang = "EN");

  std::string_view get(std::string_view tag) const;  // "" if absent
  std::size_t size() const { return tags_.size(); }

 private:
  struct Hash {
    using is_transparent = void;
    std::size_t operator()(std::string_view s) const { return std::hash<std::string_view>{}(s); }
  };
  std::unordered_map<std::string, std::string, Hash, std::equal_to<>> tags_;
};

std::string plain(std::string_view game_text);

}  // namespace gdc::text
