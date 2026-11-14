#pragma once
#include <windows.h>

#include <string>
#include <string_view>

namespace gdc::log {

// Opens <path> for writing (truncates). Failure is silent: logging is best-effort.
void open(const std::wstring& path);
// Thread-safe. Each '\n'-separated line gets a timestamp prefix.
void write(std::string_view text);
void printf(const char* fmt, ...);

std::string utf8(std::wstring_view w);

}  // namespace gdc::log
