#pragma once
#include <windows.h>

#include <cstdint>
#include <string>
#include <string_view>

namespace gdc::log {

// Opens <path> for writing (truncates). Failure is silent: logging is best-effort.
void open(const std::wstring& path);
// Thread-safe. Each '\n'-separated line gets a timestamp prefix. Every call goes straight to
// WriteFile (no user-space buffer), so the text survives a process crash right after it.
void write(std::string_view text);
void printf(const char* fmt, ...);
// For the vectored exception handler only: no lock, no heap, no CRT. One fixed-format line.
void write_fault_raw(unsigned long code, std::uintptr_t offset_in_module);

std::string utf8(std::wstring_view w);

// Microseconds between two QueryPerformanceCounter readings.
long long elapsed_us(LARGE_INTEGER from, LARGE_INTEGER to);

}  // namespace gdc::log
