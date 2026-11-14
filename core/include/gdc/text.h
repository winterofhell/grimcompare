#pragma once
// Game text: .arc v3 archives (layout: re/tools/arc.py, verified on Text_EN.arc) and the tag tables in
// resources/Text_<lang>.arc (`tag=value` lines, UTF-8 with BOM, `#` comments). Expansions
// gdx1..gdx3/resources/Text_<lang>.arc redefine some base tags (1.3.1.1: 43 / 40 / 60 tags in their
// ui files); a later file wins, as in the database (docs/DATABASE.md) — assumed, not traced for text.
#include <cstddef>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace gdc::text {

// Every file of an .arc v3 archive: (name, contents). Throws std::runtime_error on malformed input.
std::vector<std::pair<std::string, std::vector<std::byte>>> read_arc(std::span<const std::byte> data);

class Table {
 public:
  // `tag=value` lines; later definitions replace earlier ones.
  void add(std::string_view file);
  // resources/Text_<lang>.arc then gdx1..gdx3; files with "console" in the name are skipped (console
  // wording of the same tags). Throws if the base archive is missing or malformed.
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

// A tag's text without format codes: "{%.0f0}% {^E}Energy Leech Resistance" -> "Energy Leech Resistance".
// Drops every {...} group, then leading characters up to the first letter and trailing spaces.
std::string plain(std::string_view game_text);

}  // namespace gdc::text
