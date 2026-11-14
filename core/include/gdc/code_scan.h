#pragma once
// Locate unexported Game.dll functions through call sites inside exported ones.
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace gdc::scan {

// The engine's object-id lookup is not exported. Character::GetEquipCharAttributes
// (Game.dll 1.3.1.1, rva 0x695E0, call site at +0x82) resolves each equipped item id with:
//   FF 15 <rel32>   call [rip+rel32]   ; Engine.dll Singleton<ObjectManager>::Get, via the IAT
//   48 8B C8        mov  rcx, rax
//   8B D7           mov  edx, edi      ; the id
//   E8 <rel32>      call <lookup>      ; Object* (ObjectManager*, unsigned id), rva 0x19D40
struct LookupCall {
  std::uint64_t iat_slot;  // address of the IAT entry the first call reads
  std::uint64_t target;    // address of the lookup function
};

// `code` holds the bytes at virtual address `code_va`. Returns the call only if the pattern
// occurs exactly once.
std::optional<LookupCall> find_object_lookup_call(std::span<const std::byte> code, std::uint64_t code_va);

// mem::vector<GameTextLine>::push_back(GameTextLine&&) is not exported. ItemEquipment::GetUIDisplayText
// (1.3.1.1 rva 0x33DCE0) appends its "tagTransmuted" line with it at +kLinePushBackSite:
//   48 8D 55 A7   lea  rdx, [rbp-0x59]   ; the GameTextLine temporary
//   48 8B CE      mov  rcx, rsi          ; the vector
//   E8 <rel32>    call push_back         ; rva 0x1C550
// `site`: the 12 bytes there, at virtual address `site_va`. The call target, or nullopt if they differ.
inline constexpr std::size_t kLinePushBackSite = 0x264, kLinePushBackSiteLen = 12;
std::optional<std::uint64_t> line_push_back_call(std::span<const std::byte> site, std::uint64_t site_va);

// true if `body` starts with the 1.3.1.1 push_back (0xAC bytes; its calls are rel32, so the bytes do not
// depend on the load address). It grows the vector (0x1C430) and MOVES the line in: the u16string at +8
// goes through 0x1C4C0, which takes over a heap buffer and leaves the source empty. A line passed to it
// must therefore own a buffer from the game's allocator (GameTextLine's exported ctor copies into one).
inline constexpr std::size_t kLinePushBackLen = 0xAC;
bool is_line_push_back(std::span<const std::byte> body);

}  // namespace gdc::scan
