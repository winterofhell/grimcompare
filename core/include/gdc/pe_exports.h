#pragma once
// Mapped image or raw file; bounds-checked (PE/COFF spec, .edata).
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace gdc::pe {

struct Export {
  std::string name;
  std::uint32_t rva = 0;
  bool forwarded = false;  // forwarder string, not code/data
};

enum class Layout { Mapped, File };

struct ExportTable {
  std::vector<Export> exports;
  std::string error;
};

ExportTable read_exports(std::span<const std::byte> image, Layout layout);

}  // namespace gdc::pe
