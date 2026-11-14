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

}  // namespace gdc::scan
