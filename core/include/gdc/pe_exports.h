#pragma once
// Minimal PE32+ export-table reader. Portable: works on a module image as mapped
// by the Windows loader (RVA == offset) and on a raw file read from disk
// (RVA translated through the section table). Every read is bounds-checked.
// Layout: Microsoft PE/COFF specification, "Optional Header Data Directories"
// and "The .edata Section".
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace gdc::pe {

struct Export {
  std::string name;
  std::uint32_t rva = 0;
  bool forwarded = false;  // rva points at a forwarder string, not code/data
};

enum class Layout { Mapped, File };

struct ExportTable {
  std::vector<Export> exports;
  std::string error;  // non-empty → table unusable
};

ExportTable read_exports(std::span<const std::byte> image, Layout layout);

}  // namespace gdc::pe
