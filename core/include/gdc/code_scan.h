#pragma once
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace gdc::scan {

// Character::GetEquipCharAttributes (1.3.1.1 rva 0x695E0, site +0x82):
// FF 15 <rel32>   call [rip+rel32]   ; Singleton<ObjectManager>::Get via the IAT
// 48 8B C8        mov  rcx, rax
// 8B D7           mov  edx, edi      ; the id
// E8 <rel32>      call <lookup>      ; rva 0x19D40
struct LookupCall {
  std::uint64_t iat_slot;
  std::uint64_t target;
};

// Only if the pattern occurs exactly once.
std::optional<LookupCall> find_object_lookup_call(std::span<const std::byte> code, std::uint64_t code_va);

// ItemEquipment::GetUIDisplayText (rva 0x33DCE0) appends "tagTransmuted" with it at +kLinePushBackSite:
// 48 8D 55 A7   lea  rdx, [rbp-0x59]
// 48 8B CE      mov  rcx, rsi
// E8 <rel32>    call push_back         ; rva 0x1C550
inline constexpr std::size_t kLinePushBackSite = 0x264, kLinePushBackSiteLen = 12;
std::optional<std::uint64_t> line_push_back_call(std::span<const std::byte> site, std::uint64_t site_va);

// Checks the first 0xAC bytes (calls are rel32, so position independent). It MOVES the line in: the u16string at
// +8 goes through 0x1C4C0, which takes over the heap buffer. So a line must own a buffer from the game's allocator,
// which GameTextLine's exported ctor provides.
inline constexpr std::size_t kLinePushBackLen = 0xAC;
bool is_line_push_back(std::span<const std::byte> body);

}  // namespace gdc::scan
