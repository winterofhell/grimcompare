// Phase 1: load under the ASI loader, identify the game build, resolve every planned
// Game.dll/Engine.dll symbol by exported name and log it. Fully passive: no hooks, no writes
// to game memory, no engine calls except Engine::GetVersion (static string getter) after the
// engine exists.
// Gate (docs/RECON.md §6): disabled only if a required export is missing; an unknown
// module hash just logs a warning.
//
// Safety rules (crash 2026-11-14, docs/BUILD.md "Safe-mode test"):
// - DllMain: DisableThreadLibraryCalls + start one worker thread. Nothing else.
// - Kill switch first (<mod dir>\GDCompare.disable or GDCOMPARE_DISABLE=1): the worker exits
//   without logging or touching anything.
// - First log line before any other work; every line goes straight to WriteFile (no user-space
//   buffer), so after a crash the log ends at the last step reached.
// - Each step runs under SEH; a fault logs, disables, returns. Nothing is rethrown.
// - A vectored handler only logs faults whose address lies inside this module; it never handles.
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
#include "hooks.h"
#include "log.h"
#include "snapshot.h"

namespace {

constexpr const char* kModVersion = "0.9.0 (accuracy pass: engine defense pipeline, crit tiers, monster table, top-3 parts)";
constexpr DWORD kPollMs = 250;
// shortcut: generous fixed waits, no source for real load times — revisit when the Phase 1 log
// shows how long modules / gGameEngine actually take.
constexpr DWORD kModulesTimeoutMs = 120'000;
constexpr DWORD kEngineTimeoutMs = 300'000;

HMODULE g_self = nullptr;
std::uintptr_t g_self_begin = 0, g_self_end = 0;
std::atomic<bool> g_enabled{false};  // read by the hook/snapshot code of later phases
std::atomic<long> g_veh_logged{0};
constexpr long kVehMaxLines = 64;

std::wstring module_path(HMODULE h) {
  std::wstring buf(32768, L'\0');
  const DWORD n = GetModuleFileNameW(h, buf.data(), static_cast<DWORD>(buf.size()));
  buf.resize(n);
  return buf;
}

std::wstring self_dir() {
  std::wstring p = module_path(g_self);
  const auto slash = p.find_last_of(L"\\/");
  return slash == std::wstring::npos ? std::wstring{} : p.substr(0, slash + 1);
}

bool kill_switch() {
  wchar_t env[8];
  const DWORD n = GetEnvironmentVariableW(L"GDCOMPARE_DISABLE", env, 8);
  if (n == 1 && env[0] == L'1') return true;
  const std::wstring flag = self_dir() + L"GDCompare.disable";
  return GetFileAttributesW(flag.c_str()) != INVALID_FILE_ATTRIBUTES;
}

std::string where(std::uintptr_t addr) {
  HMODULE m = nullptr;
  char buf[96];
  if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                         reinterpret_cast<LPCWSTR>(addr), &m)) {
    std::string name = gdc::log::utf8(module_path(m));
    const auto slash = name.find_last_of("\\/");
    if (slash != std::string::npos) name.erase(0, slash + 1);
    std::snprintf(buf, sizeof buf, "+0x%llX",
                  static_cast<unsigned long long>(addr - reinterpret_cast<std::uintptr_t>(m)));
    return name + buf;
  }
  std::snprintf(buf, sizeof buf, "0x%016llX (no module)", static_cast<unsigned long long>(addr));
  return buf;
}

// Vectored handler: log-only, lock-free, no heap (it can run on any thread, inside any state).
LONG CALLBACK fault_logger(EXCEPTION_POINTERS* ep) {
  const auto addr = reinterpret_cast<std::uintptr_t>(ep->ExceptionRecord->ExceptionAddress);
  if (addr >= g_self_begin && addr < g_self_end && g_veh_logged.fetch_add(1) < kVehMaxLines)
    gdc::log::write_fault_raw(ep->ExceptionRecord->ExceptionCode, addr - g_self_begin);
  return EXCEPTION_CONTINUE_SEARCH;
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

// --- steps

struct Module {
  const wchar_t* name;
  HMODULE handle = nullptr;
  std::span<const std::byte> image;
  gdc::pe::ExportTable exports;
  std::string build;  // known build version, "" if unknown
};

struct Ctx {
  Module game{L"Game.dll", nullptr, {}, {}, {}};
  Module engine{L"Engine.dll", nullptr, {}, {}, {}};
  gdc::targets::Report report;
};

const std::byte* target_address(const Ctx& c, std::string_view label) {
  for (const auto& it : c.report.items) {
    if (it.target->label != label || it.problem != gdc::targets::Problem::None) continue;
    return (it.target->module == gdc::targets::Module::Game ? c.game : c.engine).image.data() + it.rva;
  }
  return nullptr;
}

std::uintptr_t read_ptr(const void* p) { return *static_cast<const volatile std::uintptr_t*>(p); }

bool step_identify(Ctx&) {
  gdc::log::printf("process: %s", gdc::log::utf8(module_path(nullptr)).c_str());
  gdc::log::printf("mod:     %s", gdc::log::utf8(module_path(g_self)).c_str());
  using WineVer = const char* (*)();
  if (auto wv = reinterpret_cast<WineVer>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "wine_get_version")))
    gdc::log::printf("wine:    %s", wv());
  return true;
}

bool step_wait_modules(Ctx& c) {
  for (DWORD waited = 0;; waited += kPollMs) {
    c.game.handle = GetModuleHandleW(c.game.name);
    c.engine.handle = GetModuleHandleW(c.engine.name);
    if (c.game.handle && c.engine.handle) {
      gdc::log::printf("Game.dll + Engine.dll loaded (waited %lu ms)", waited);
      return true;
    }
    if (waited >= kModulesTimeoutMs) {
      gdc::log::printf("timeout after %lu ms: Game.dll %s, Engine.dll %s", waited, c.game.handle ? "loaded" : "missing",
                       c.engine.handle ? "loaded" : "missing");
      return false;
    }
    Sleep(kPollMs);
  }
}

bool read_module(Module& m) {
  const std::string name = gdc::log::utf8(m.name);
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
    if (kb) m.build = std::string(kb->version);
    if (kb)
      gdc::log::printf("%s: sha256 %s = known build %.*s [%.*s]", name.c_str(), sha->c_str(),
                       static_cast<int>(kb->version.size()), kb->version.data(), static_cast<int>(kb->variant.size()),
                       kb->variant.data());
    else
      gdc::log::printf("%s: sha256 %s = UNKNOWN build (warning only)", name.c_str(), sha->c_str());
  } else {
    gdc::log::printf("%s: sha256 unavailable (warning only)", name.c_str());
  }

  m.exports = gdc::pe::read_exports(m.image, gdc::pe::Layout::Mapped);
  if (!m.exports.error.empty()) {
    gdc::log::printf("%s: export table unreadable: %s", name.c_str(), m.exports.error.c_str());
    return false;
  }
  gdc::log::printf("%s: %zu named exports", name.c_str(), m.exports.exports.size());
  return true;
}

bool step_read_modules(Ctx& c) { return read_module(c.game) && read_module(c.engine); }

bool step_resolve(Ctx& c) {
  c.report = gdc::targets::resolve(gdc::targets::planned(), c.game.exports.exports, c.engine.exports.exports);
  const auto base = [](const Module& m) { return static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(m.handle)); };
  gdc::log::write("hook targets:\n" + gdc::targets::format(c.report, base(c.game), base(c.engine)));
  if (!c.report.enabled())
    gdc::log::printf("%zu required target(s) unresolved, see above", c.report.required_failed);
  return c.report.enabled();
}

bool step_wait_engine(Ctx& c) {
  const std::byte* var = target_address(c, "gGameEngine");
  if (!var) {
    gdc::log::write("gGameEngine not resolved");
    return false;
  }
  for (DWORD waited = 0;; waited += kPollMs) {
    if (const std::uintptr_t v = read_ptr(var)) {
      gdc::log::printf("gGameEngine = 0x%016llX (waited %lu ms)", static_cast<unsigned long long>(v), waited);
      return true;
    }
    if (waited >= kEngineTimeoutMs) {
      gdc::log::printf("timeout after %lu ms: gGameEngine still null", waited);
      return false;
    }
    Sleep(kPollMs);
  }
}

bool step_read_data(Ctx& c) {
  for (const auto& it : c.report.items) {
    if (it.problem != gdc::targets::Problem::None || it.target->kind != gdc::targets::Kind::Data) continue;
    const std::byte* addr = (it.target->module == gdc::targets::Module::Game ? c.game : c.engine).image.data() + it.rva;
    gdc::log::printf("*%.*s = 0x%016llX", static_cast<int>(it.target->label.size()), it.target->label.data(),
                     static_cast<unsigned long long>(read_ptr(addr)));
  }
  return true;
}

bool step_version(Ctx& c) {
  const std::byte* fn = target_address(c, "Engine::GetVersion");
  if (!fn) {
    gdc::log::write("Engine::GetVersion not resolved (optional); skipped");
    return true;
  }
  using GetVersionFn = const char* (*)();
  const char* s = reinterpret_cast<GetVersionFn>(const_cast<std::byte*>(fn))();  // lea rax,[static]; ret
  char ver[128] = "(null)";
  if (s) {
    const std::size_t n = strnlen(s, sizeof ver - 1);
    std::memcpy(ver, s, n);
    ver[n] = '\0';
  }
  gdc::log::printf("Engine::GetVersion() = \"%s\"", ver);
  return true;
}

gdc::adapter::Resolve resolver(const Ctx& c) {
  return [&c](std::string_view label) -> const void* { return target_address(c, label); };
}

bool step_snapshot_init(Ctx& c) { return gdc::adapter::snapshot_init(resolver(c)); }

bool step_hooks(Ctx& c) {
  return gdc::adapter::hooks_install(resolver(c), {self_dir(), kModVersion, c.game.build});
}

// --- step runner. The SEH frame has no C++ objects; C++ exceptions are caught one level down.

using StepFn = bool (*)(Ctx&);
enum StepResult : int { kFault = -2, kCppException = -1, kFailed = 0, kOk = 1 };

__declspec(noinline) int run_cpp(StepFn fn, Ctx& c) {
  try {
    return fn(c) ? kOk : kFailed;
  } catch (const std::exception& e) {
    gdc::log::printf("C++ exception: %s", e.what());
  } catch (...) {
    gdc::log::write("unknown C++ exception");
  }
  return kCppException;
}

int capture(EXCEPTION_POINTERS* ep, DWORD& code, std::uintptr_t& addr) {
  code = ep->ExceptionRecord->ExceptionCode;
  addr = reinterpret_cast<std::uintptr_t>(ep->ExceptionRecord->ExceptionAddress);
  return EXCEPTION_EXECUTE_HANDLER;
}

__declspec(noinline) int run_seh(StepFn fn, Ctx& c, DWORD& code, std::uintptr_t& addr) {
  __try {
    return run_cpp(fn, c);
  } __except (capture(GetExceptionInformation(), code, addr)) {
    return kFault;
  }
}

bool step(int n, int total, const char* name, StepFn fn, Ctx& c) {
  gdc::log::printf("step %d/%d %s", n, total, name);
  DWORD code = 0;
  std::uintptr_t addr = 0;
  const int r = run_seh(fn, c, code, addr);
  if (r == kOk) return true;
  if (r == kFault)
    gdc::log::printf("step %d/%d %s: FAULT 0x%08lX at %s", n, total, name, code, where(addr).c_str());
  gdc::log::printf("STATUS: DISABLED (step %d/%d %s failed)", n, total, name);
  return false;
}

void run() {
  struct Step {
    const char* name;
    StepFn fn;
  };
  static constexpr Step kSteps[] = {
      {"identify", step_identify},
      {"wait for Game.dll + Engine.dll", step_wait_modules},
      {"read modules", step_read_modules},
      {"resolve exports", step_resolve},
      {"wait for gGameEngine", step_wait_engine},
      {"read exported data", step_read_data},
      {"Engine::GetVersion", step_version},
      {"snapshot: bind + verify", step_snapshot_init},
      {"install hooks", step_hooks},
  };
  constexpr int total = static_cast<int>(std::size(kSteps));
  Ctx c;
  for (int i = 0; i < total; ++i)
    if (!step(i + 1, total, kSteps[i].name, kSteps[i].fn, c)) return;
  g_enabled = true;
  gdc::log::printf("STATUS: ENABLED (%zu optional target(s) missing; read-only snapshots on the hotkey and auto snapshots)",
                   c.report.optional_failed);
}

void start() {
  if (kill_switch()) return;

  std::wstring log_path = module_path(g_self);
  const auto dot = log_path.find_last_of(L'.');
  log_path = (dot == std::wstring::npos ? log_path : log_path.substr(0, dot)) + L".log";
  gdc::log::open(log_path);
  gdc::log::printf("GD Compare %s (built %s %s) worker start, pid %lu tid %lu", kModVersion, __DATE__, __TIME__,
                   GetCurrentProcessId(), GetCurrentThreadId());

  MODULEINFO mi{};
  if (GetModuleInformation(GetCurrentProcess(), g_self, &mi, sizeof mi)) {
    g_self_begin = reinterpret_cast<std::uintptr_t>(mi.lpBaseOfDll);
    g_self_end = g_self_begin + mi.SizeOfImage;
    // Stay mapped for the process lifetime: the vectored handler must never dangle.
    HMODULE pinned = nullptr;
    if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
                           reinterpret_cast<LPCWSTR>(&fault_logger), &pinned) &&
        AddVectoredExceptionHandler(1, fault_logger))
      gdc::log::write("fault logger installed");
    else
      gdc::log::printf("fault logger not installed (%lu)", GetLastError());
  }
  run();
}

DWORD WINAPI worker(void*) {
  DWORD code = 0;
  std::uintptr_t addr = 0;
  __try {
    start();
  } __except (capture(GetExceptionInformation(), code, addr)) {
    g_enabled = false;
    gdc::log::printf("STATUS: DISABLED (unguarded fault 0x%08lX at 0x%016llX)", code,
                     static_cast<unsigned long long>(addr));
  }
  return 0;
}

}  // namespace

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID) {
  if (reason == DLL_PROCESS_ATTACH) {
    g_self = inst;
    DisableThreadLibraryCalls(inst);
    if (HANDLE t = CreateThread(nullptr, 0, worker, nullptr, 0, nullptr)) CloseHandle(t);
  }
  return TRUE;
}
