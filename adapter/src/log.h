#pragma once
#include <windows.h>

#include <cstdint>
#include <string>
#include <string_view>

namespace gdc::log {

// Best-effort: failure is silent.
void open(const std::wstring& path);
// Straight to WriteFile, no buffer, so the text survives a crash right after.
void write(std::string_view text);
void printf(const char* fmt, ...);
// For the vectored handler: no lock, no heap, no CRT.
void write_fault_raw(unsigned long code, std::uintptr_t offset_in_module);

std::string utf8(std::wstring_view w);

long long elapsed_us(LARGE_INTEGER from, LARGE_INTEGER to);

}  // namespace gdc::log
