#include "platform.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <shlobj.h>
#include <tlhelp32.h>
#endif

namespace setup {

namespace fs = std::filesystem;

fs::path u8path(std::string_view s) { return fs::path(std::u8string(s.begin(), s.end())); }

std::string u8string(const fs::path& p) {
  const std::u8string u = p.u8string();
  return std::string(u.begin(), u.end());
}

#ifdef _WIN32

namespace {

std::wstring wide(std::string_view s) {
  if (s.empty()) return {};
  const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
  std::wstring w(static_cast<std::size_t>(n), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
  return w;
}

std::string narrow(std::wstring_view w) {
  if (w.empty()) return {};
  const int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
  std::string s(static_cast<std::size_t>(n), '\0');
  WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), s.data(), n, nullptr, nullptr);
  return s;
}

HKEY root_of(Hive h) { return h == Hive::kCurrentUser ? HKEY_CURRENT_USER : HKEY_LOCAL_MACHINE; }

}  // namespace

std::optional<std::string> env(const char* name) {
  const std::wstring n = wide(name);
  const DWORD len = GetEnvironmentVariableW(n.c_str(), nullptr, 0);
  if (len == 0) return std::nullopt;
  std::wstring v(len, L'\0');
  const DWORD got = GetEnvironmentVariableW(n.c_str(), v.data(), len);
  v.resize(got);
  return narrow(v);
}

fs::path home() {
  const auto h = env("USERPROFILE");
  return h ? u8path(*h) : fs::path();
}

fs::path exe_dir() {
  std::wstring buf(MAX_PATH, L'\0');
  for (;;) {
    const DWORD n = GetModuleFileNameW(nullptr, buf.data(), static_cast<DWORD>(buf.size()));
    if (n == 0) return {};
    if (n < buf.size()) {
      buf.resize(n);
      return fs::path(buf).parent_path();
    }
    buf.resize(buf.size() * 2);
  }
}

std::optional<bool> game_running() {
  HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
  if (snap == INVALID_HANDLE_VALUE) return std::nullopt;
  PROCESSENTRY32W e{};
  e.dwSize = sizeof e;
  bool found = false;
  for (BOOL ok = Process32FirstW(snap, &e); ok && !found; ok = Process32NextW(snap, &e))
    found = _wcsicmp(e.szExeFile, L"Grim Dawn.exe") == 0;
  CloseHandle(snap);
  return found;
}

fs::path documents_dir() {
  PWSTR p = nullptr;
  fs::path out;
  if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Documents, 0, nullptr, &p))) out = fs::path(p);
  CoTaskMemFree(p);
  return out;
}

std::optional<std::string> registry_string(Hive hive, const std::string& key, const std::string& value) {
  const std::wstring k = wide(key), v = wide(value);
  DWORD bytes = 0;
  if (RegGetValueW(root_of(hive), k.c_str(), v.c_str(), RRF_RT_REG_SZ, nullptr, nullptr, &bytes) != ERROR_SUCCESS)
    return std::nullopt;
  std::wstring out(bytes / sizeof(wchar_t) + 1, L'\0');
  if (RegGetValueW(root_of(hive), k.c_str(), v.c_str(), RRF_RT_REG_SZ, nullptr, out.data(), &bytes) != ERROR_SUCCESS)
    return std::nullopt;
  out.resize(wcsnlen(out.c_str(), out.size()));
  return narrow(out);
}

std::vector<std::string> registry_subkeys(Hive hive, const std::string& key) {
  std::vector<std::string> out;
  HKEY h = nullptr;
  if (RegOpenKeyExW(root_of(hive), wide(key).c_str(), 0, KEY_READ, &h) != ERROR_SUCCESS) return out;
  wchar_t name[256];
  for (DWORD i = 0;; ++i) {
    DWORD len = 256;
    if (RegEnumKeyExW(h, i, name, &len, nullptr, nullptr, nullptr, nullptr) != ERROR_SUCCESS) break;
    out.push_back(narrow(std::wstring_view(name, len)));
  }
  RegCloseKey(h);
  return out;
}

#else

std::optional<std::string> env(const char* name) {
  const char* v = std::getenv(name);
  if (!v) return std::nullopt;
  return std::string(v);
}

fs::path home() {
  const auto h = env("HOME");
  return h ? fs::path(*h) : fs::path();
}

fs::path exe_dir() {
  std::error_code ec;
  const fs::path p = fs::read_symlink("/proc/self/exe", ec);
  return ec ? fs::path() : p.parent_path();
}

std::optional<bool> game_running() {
  std::error_code ec;
  fs::directory_iterator it("/proc", ec);
  if (ec) return std::nullopt;
  auto ends_with_exe = [](std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s.ends_with("grim dawn.exe");
  };
  for (const auto& d : it) {
    const std::string pid = d.path().filename().string();
    if (pid.empty() || !std::all_of(pid.begin(), pid.end(), [](unsigned char c) { return std::isdigit(c); })) continue;
    std::ifstream comm(d.path() / "comm");
    std::string name;
    if (comm && std::getline(comm, name) && name == "Grim Dawn.exe") return true;
    std::ifstream cmd(d.path() / "cmdline", std::ios::binary);
    std::string arg0;
    if (cmd && std::getline(cmd, arg0, '\0') && arg0.size() > 2 && std::isalpha(static_cast<unsigned char>(arg0[0])) &&
        arg0[1] == ':' && arg0[2] == '\\' && ends_with_exe(arg0))
      return true;
  }
  return false;
}

fs::path documents_dir() { return {}; }
std::optional<std::string> registry_string(Hive, const std::string&, const std::string&) { return std::nullopt; }
std::vector<std::string> registry_subkeys(Hive, const std::string&) { return {}; }

#endif

}  // namespace setup
