// Phase 1: load under the ASI loader, identify the game build, resolve every planned
// Game.dll/Engine.dll symbol by exported name and log it. No hooks, no writes to game memory.
// Gate (docs/RECON.md §6): disabled only if a required export is missing; an unknown
// module hash just logs a warning.
#include <windows.h>
#include <bcrypt.h>
#include <psapi.h>

#include <atomic>
#include <cstring>
#include <optional>
#include <string>
#include <vector>

#include "gdc/hook_targets.h"
#include "gdc/known_builds.h"
#include "gdc/pe_exports.h"
#include "log.h"

namespace {

constexpr const char* kModVersion = "0.1.0 (phase 1)";

HMODULE g_self = nullptr;
std::atomic<bool> g_enabled{false};  // read by the hook/snapshot code of later phases

std::wstring module_path(HMODULE h) {
  std::wstring buf(32768, L'\0');
  const DWORD n = GetModuleFileNameW(h, buf.data(), static_cast<DWORD>(buf.size()));
  buf.resize(n);
  return buf;
}

std::optional<std::string> sha256_file(const std::wstring& path) {
  HANDLE f = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                         FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
  if (f == INVALID_HANDLE_VALUE) return std::nullopt;
  BCRYPT_ALG_HANDLE alg = nullptr;
  BCRYPT_HASH_HANDLE hash = nullptr;
  std::optional<std::string> out;
  unsigned char digest[32];
  if (BCRYPT_SUCCESS(BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0)) &&
      BCRYPT_SUCCESS(BCryptCreateHash(alg, &hash, nullptr, 0, nullptr, 0, 0))) {
    std::vector<unsigned char> chunk(1 << 20);
    bool ok = true;
    for (;;) {
      DWORD got = 0;
      if (!ReadFile(f, chunk.data(), static_cast<DWORD>(chunk.size()), &got, nullptr)) {
        ok = false;
        break;
      }
      if (got == 0) break;
      if (!BCRYPT_SUCCESS(BCryptHashData(hash, chunk.data(), got, 0))) {
        ok = false;
        break;
      }
    }
    if (ok && BCRYPT_SUCCESS(BCryptFinishHash(hash, digest, sizeof digest, 0))) {
      static constexpr char hex[] = "0123456789abcdef";
      std::string s;
      for (unsigned char b : digest) {
        s += hex[b >> 4];
        s += hex[b & 15];
      }
      out = std::move(s);
    }
  }
  if (hash) BCryptDestroyHash(hash);
  if (alg) BCryptCloseAlgorithmProvider(alg, 0);
  CloseHandle(f);
  return out;
}

// --- SEH-guarded reads of game memory. Each faulting access happens inside a noinline
// callee so the __except in the caller reliably catches it (clang-cl, /EHsc).

__declspec(noinline) void parse_mapped(std::span<const std::byte> img, gdc::pe::ExportTable& out) {
  out = gdc::pe::read_exports(img, gdc::pe::Layout::Mapped);
}

bool parse_mapped_guarded(std::span<const std::byte> img, gdc::pe::ExportTable& out) {
  __try {
    parse_mapped(img, out);
    return true;
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    return false;
  }
}

__declspec(noinline) void copy_version(const void* fn, char* buf, std::size_t cap) {
  using GetVersionFn = const char* (*)();
  const char* s = reinterpret_cast<GetVersionFn>(const_cast<void*>(fn))();  // Engine::GetVersion: lea rax,[static]; ret
  if (!s) return;
  const std::size_t n = strnlen(s, cap - 1);
  std::memcpy(buf, s, n);
  buf[n] = '\0';
}

bool call_get_version_guarded(const void* fn, char* buf, std::size_t cap) {
  __try {
    copy_version(fn, buf, cap);
    return true;
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    return false;
  }
}

__declspec(noinline) std::uintptr_t read_ptr(const void* p) { return *static_cast<const volatile std::uintptr_t*>(p); }

bool read_ptr_guarded(const void* p, std::uintptr_t& out) {
  __try {
    out = read_ptr(p);
    return true;
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    return false;
  }
}

// ---

struct Module {
  const wchar_t* name;
  HMODULE handle = nullptr;
  std::span<const std::byte> image;
  gdc::pe::ExportTable exports;
};

bool load_module(Module& m) {
  m.handle = GetModuleHandleW(m.name);
  const std::string name = gdc::log::utf8(m.name);
  if (!m.handle) {
    gdc::log::printf("%s: not loaded", name.c_str());
    return false;
  }
  MODULEINFO mi{};
  if (!GetModuleInformation(GetCurrentProcess(), m.handle, &mi, sizeof mi)) {
    gdc::log::printf("%s: GetModuleInformation failed (%lu)", name.c_str(), GetLastError());
    return false;
  }
  m.image = {static_cast<const std::byte*>(mi.lpBaseOfDll), mi.SizeOfImage};
  const std::wstring path = module_path(m.handle);
  gdc::log::printf("%s: base 0x%016llX size 0x%X path %s", name.c_str(),
                   static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(mi.lpBaseOfDll)), mi.SizeOfImage,
                   gdc::log::utf8(path).c_str());

  if (const auto sha = sha256_file(path)) {
    const gdc::KnownBuild* kb = gdc::find_known_build(name, *sha);
    if (kb)
      gdc::log::printf("%s: sha256 %s = known build %.*s [%.*s]", name.c_str(), sha->c_str(),
                       static_cast<int>(kb->version.size()), kb->version.data(), static_cast<int>(kb->variant.size()),
                       kb->variant.data());
    else
      gdc::log::printf("%s: sha256 %s = UNKNOWN build (warning only)", name.c_str(), sha->c_str());
  } else {
    gdc::log::printf("%s: sha256 unavailable (warning only)", name.c_str());
  }

  if (!parse_mapped_guarded(m.image, m.exports)) {
    gdc::log::printf("%s: access violation while reading export table", name.c_str());
    return false;
  }
  if (!m.exports.error.empty()) {
    gdc::log::printf("%s: export table unreadable: %s", name.c_str(), m.exports.error.c_str());
    return false;
  }
  gdc::log::printf("%s: %zu named exports", name.c_str(), m.exports.exports.size());
  return true;
}

void run() {
  std::wstring log_path = module_path(g_self);
  const auto dot = log_path.find_last_of(L'.');
  log_path = (dot == std::wstring::npos ? log_path : log_path.substr(0, dot)) + L".log";
  gdc::log::open(log_path);

  gdc::log::printf("GD Compare %s, built %s %s", kModVersion, __DATE__, __TIME__);
  gdc::log::printf("process: %s", gdc::log::utf8(module_path(nullptr)).c_str());
  gdc::log::printf("mod:     %s", gdc::log::utf8(module_path(g_self)).c_str());
  using WineVer = const char* (*)();
  if (auto wv = reinterpret_cast<WineVer>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "wine_get_version")))
    gdc::log::printf("wine:    %s", wv());

  Module game{L"Game.dll", nullptr, {}, {}}, engine{L"Engine.dll", nullptr, {}, {}};
  const bool game_ok = load_module(game);
  const bool engine_ok = load_module(engine);
  if (!game_ok || !engine_ok) {
    gdc::log::write("STATUS: DISABLED (game modules unavailable)");
    return;
  }

  const auto report = gdc::targets::resolve(gdc::targets::planned(), game.exports.exports, engine.exports.exports);
  const auto base = [](const Module& m) { return static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(m.handle)); };
  gdc::log::write("hook targets:\n" + gdc::targets::format(report, base(game), base(engine)));

  for (const auto& it : report.items) {
    if (it.problem != gdc::targets::Problem::None) continue;
    const std::byte* addr = (it.target->module == gdc::targets::Module::Game ? game : engine).image.data() + it.rva;
    if (it.target->label == "Engine::GetVersion") {
      char ver[128] = "?";
      if (call_get_version_guarded(addr, ver, sizeof ver))
        gdc::log::printf("Engine::GetVersion() = \"%s\"", ver);
      else
        gdc::log::write("Engine::GetVersion() faulted");
    } else if (it.target->kind == gdc::targets::Kind::Data) {
      std::uintptr_t v = 0;
      if (read_ptr_guarded(addr, v))
        gdc::log::printf("*%.*s = 0x%016llX%s", static_cast<int>(it.target->label.size()), it.target->label.data(),
                         static_cast<unsigned long long>(v), v ? "" : " (not created yet at load time; normal)");
      else
        gdc::log::printf("*%.*s faulted", static_cast<int>(it.target->label.size()), it.target->label.data());
    }
  }

  g_enabled = report.enabled();
  if (report.enabled())
    gdc::log::printf("STATUS: ENABLED (%zu optional target(s) missing)", report.optional_failed);
  else
    gdc::log::printf("STATUS: DISABLED (%zu required target(s) unresolved, see above)", report.required_failed);
}

__declspec(noinline) void run_noexcept() {
  try {
    run();
  } catch (const std::exception& e) {
    gdc::log::printf("STATUS: DISABLED (exception: %s)", e.what());
    g_enabled = false;
  } catch (...) {
    gdc::log::write("STATUS: DISABLED (unknown exception)");
    g_enabled = false;
  }
}

DWORD WINAPI worker(void*) {
  __try {
    run_noexcept();
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    g_enabled = false;
    gdc::log::printf("STATUS: DISABLED (SEH exception 0x%08lX)", GetExceptionCode());
  }
  return 0;
}

}  // namespace

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID) {
  if (reason == DLL_PROCESS_ATTACH) {
    g_self = inst;
    DisableThreadLibraryCalls(inst);
    // Loader lock is held here: do nothing but start the worker.
    if (HANDLE t = CreateThread(nullptr, 0, worker, nullptr, 0, nullptr)) CloseHandle(t);
  }
  return TRUE;
}
