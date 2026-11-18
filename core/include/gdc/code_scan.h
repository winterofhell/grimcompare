#pragma once
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace gdc::scan {

// Object-id lookup call in GetEquipCharAttributes (§13.8).
struct LookupCall {
  std::uint64_t iat_slot;
  std::uint64_t target;
};

// Only if the pattern occurs exactly once.
std::optional<LookupCall> find_object_lookup_call(std::span<const std::byte> code, std::uint64_t code_va);

// push_back call in GetUIDisplayText after "tagTransmuted" (§13.8).
inline constexpr std::size_t kLinePushBackSite = 0x264, kLinePushBackSiteLen = 12;
std::optional<std::uint64_t> line_push_back_call(std::span<const std::byte> site, std::uint64_t site_va);

// Checks the first 0xAC bytes; it moves lines in (§13.8).
inline constexpr std::size_t kLinePushBackLen = 0xAC;
bool is_line_push_back(std::span<const std::byte> body);

}  // namespace gdc::scan
