#include "gdc/code_scan.h"

#include <cstring>

namespace gdc::scan {
namespace {

std::int32_t rel32(const std::byte* p) {
  std::int32_t v;
  std::memcpy(&v, p, sizeof v);
  return v;
}

}  // namespace

std::optional<LookupCall> find_object_lookup_call(std::span<const std::byte> code, std::uint64_t code_va) {
  // 6 (call [rip]) + 3 (mov rcx,rax) + 2 (mov edx,edi) + 5 (call rel32)
  constexpr std::size_t kLen = 16;
  constexpr unsigned char kMid[] = {0x48, 0x8B, 0xC8, 0x8B, 0xD7, 0xE8};
  std::optional<LookupCall> found;
  int matches = 0;
  for (std::size_t i = 0; i + kLen <= code.size(); ++i) {
    const std::byte* p = code.data() + i;
    if (p[0] != std::byte{0xFF} || p[1] != std::byte{0x15}) continue;
    if (std::memcmp(p + 6, kMid, sizeof kMid) != 0) continue;
    const std::uint64_t va = code_va + i;
    found = LookupCall{va + 6 + static_cast<std::uint64_t>(static_cast<std::int64_t>(rel32(p + 2))),
                       va + kLen + static_cast<std::uint64_t>(static_cast<std::int64_t>(rel32(p + 12)))};
    ++matches;
  }
  return matches == 1 ? found : std::nullopt;
}

}  // namespace gdc::scan
