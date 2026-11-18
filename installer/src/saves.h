#pragma once
// Characters from player.gdc headers, read-only.
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace setup {

struct SaveHeader {
  std::string name;       // UTF-8
  std::string class_tag;  // tagSkillClassNameXXYY
  std::uint32_t level = 0;
  bool hardcore = false;
};
std::optional<SaveHeader> read_save_header(std::span<const unsigned char> data, std::string& error);

// "tagSkillClassName0306" -> {3, 6}; "" or anything else -> {}.
std::vector<int> class_masteries(std::string_view class_tag);

struct Character {
  SaveHeader header;
  std::filesystem::path file;
  std::string save;  // gdc::snap::save_key: "cloud/<account>/main/_Name" under Steam's userdata, else "local/..."
};

// Save roots: Steam Cloud, Proton prefix, Windows Documents.
std::vector<std::filesystem::path> save_roots(const std::vector<std::filesystem::path>& steam_roots,
                                              const std::vector<std::filesystem::path>& libraries);
std::vector<Character> find_characters(const std::vector<std::filesystem::path>& save_roots);

}  // namespace setup
