#include "log.h"

#include <cstdarg>
#include <cstdio>

namespace gdc::log {
namespace {
// Written once in open() before the vectored handler exists; read without the lock there.
HANDLE volatile g_file = INVALID_HANDLE_VALUE;
SRWLOCK g_lock = SRWLOCK_INIT;
}  // namespace

void open(const std::wstring& path) {
  AcquireSRWLockExclusive(&g_lock);
  if (g_file == INVALID_HANDLE_VALUE)
    g_file = CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL,
                         nullptr);
  ReleaseSRWLockExclusive(&g_lock);
}

void write(std::string_view text) {
  SYSTEMTIME t;
  GetLocalTime(&t);
  char stamp[32];
  const int n = std::snprintf(stamp, sizeof stamp, "[%02u:%02u:%02u.%03u] ", t.wHour, t.wMinute, t.wSecond,
                              t.wMilliseconds);
  std::string out;
  while (!text.empty()) {
    const auto nl = text.find('\n');
    out.append(stamp, n > 0 ? n : 0);
    out.append(text.substr(0, nl));
    out += "\r\n";
    text = nl == std::string_view::npos ? std::string_view{} : text.substr(nl + 1);
  }
  AcquireSRWLockExclusive(&g_lock);
  if (g_file != INVALID_HANDLE_VALUE) {
    DWORD written = 0;
    WriteFile(g_file, out.data(), static_cast<DWORD>(out.size()), &written, nullptr);
  }
  ReleaseSRWLockExclusive(&g_lock);
}

void printf(const char* fmt, ...) {
  char buf[1024];
  va_list ap;
  va_start(ap, fmt);
  const int n = std::vsnprintf(buf, sizeof buf, fmt, ap);
  va_end(ap);
  if (n > 0) write(std::string_view(buf, n < static_cast<int>(sizeof buf) ? n : sizeof buf - 1));
}

void write_fault_raw(unsigned long code, std::uintptr_t offset) {
  static constexpr char hex[] = "0123456789ABCDEF";
  char line[] = "[veh] fault 0x________ at GDCompare.asi+0x________________ (first chance)\r\n";
  char* p = line + 14;
  for (int i = 7; i >= 0; --i) *p++ = hex[(code >> (i * 4)) & 15];
  p = line + 42;
  for (int i = 15; i >= 0; --i) *p++ = hex[(offset >> (i * 4)) & 15];
  const HANDLE f = g_file;
  if (f == INVALID_HANDLE_VALUE) return;
  DWORD written = 0;
  WriteFile(f, line, static_cast<DWORD>(sizeof line - 1), &written, nullptr);
}

std::string utf8(std::wstring_view w) {
  if (w.empty()) return {};
  const int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
  if (n <= 0) return "?";
  std::string s(n, '\0');
  WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), s.data(), n, nullptr, nullptr);
  return s;
}

long long elapsed_us(LARGE_INTEGER a, LARGE_INTEGER b) {
  static const LONGLONG freq = [] {
    LARGE_INTEGER f;
    QueryPerformanceFrequency(&f);
    return f.QuadPart;
  }();
  return (b.QuadPart - a.QuadPart) * 1'000'000 / freq;
}

}  // namespace gdc::log
