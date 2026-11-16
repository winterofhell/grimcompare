#pragma once
// Characters from the save folder, read-only: the header of each save/main|user/<folder>/player.gdc.
//
// .gdc header (decoded from three local saves 2026-11-16; names, classes and levels match the characters in game):
// key = first u32 ^ 0x55555555; table[i] = k = rotr(k, 1) x 39916801 (mod 2^32) for i = 0..255, k starting at key.
// Each later read: value = raw ^ key (low byte for a u8), then key ^= table[b] for every raw byte b.
// Fields: magic u32 0x58434447 ("GDCX"), version u32 (2), name (u32 count + count x (u8, u8) UTF-16LE), sex u8,
// class tag (u32 count + bytes, "tagSkillClassName0306" or "" before the first mastery), level u32, hardcore u8,
// and from version 2 an expansion u8. Nothing past the header is read.
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

// Save roots in the order tried: Steam Cloud (userdata/<id>/219990/remote/save), the Proton prefix's and the
// Windows Documents folder's My Games/Grim Dawn/save. One entry per save folder; unreadable saves are skipped.
std::vector<std::filesystem::path> save_roots(const std::vector<std::filesystem::path>& steam_roots,
                                              const std::vector<std::filesystem::path>& libraries);
std::vector<Character> find_characters(const std::vector<std::filesystem::path>& save_roots);

}  // namespace setup
