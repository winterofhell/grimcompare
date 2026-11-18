#include "tooltip.h"

#include <windows.h>
#include <psapi.h>

#include <algorithm>
#include <utility>

#include "gdc/code_scan.h"
#include "tooltip_internal.h"

namespace gdc::adapter {
namespace tip {

std::atomic<bool> g_on{false};
bool g_debug = false;
CtorFn g_ctor = nullptr;
PushFn g_push = nullptr;
const void* const* g_engine_var = nullptr;
GraphicsFn g_graphics = nullptr;
ScaleFn g_ui_scale = nullptr;
WidthFn g_screen_width = nullptr;
const void* g_search_ret = nullptr;
std::atomic<const arz::Database*> g_db{nullptr};
std::atomic<const text::Table*> g_text{nullptr};
std::filesystem::path g_game_dir;
SRWLOCK g_pub_lock = SRWLOCK_INIT;
std::shared_ptr<const Published> g_pub;
u64 g_seq = 0;
std::atomic<DWORD> g_game_tid{0};

SRWLOCK g_lock = SRWLOCK_INIT;
CONDITION_VARIABLE g_cv = CONDITION_VARIABLE_INIT;
std::array<Entry, kCache> g_cache;
u64 g_use = 0;
u64 g_gen = 1;
std::vector<Cand> g_pool;
std::vector<Job> g_jobs;
std::vector<std::string> g_log_out;
std::shared_ptr<const Presets> g_presets;
tooltip::Screen g_screen;
const void* g_player = nullptr;
std::vector<Out> g_out;
score::Preset g_preset;
u64 g_preset_version = 1;
std::u16string g_preset_error;
std::wstring g_preset_path;
FILETIME g_preset_mtime{};
bool g_preset_seen = false;
ULONGLONG g_preset_poll_ms = 0;
std::vector<Named> g_named;
std::wstring g_named_dir;
std::u16string g_named_error;
u64 g_calls_total = 0;
ULONGLONG g_last_log_ms = 0, g_last_build_ms = 0, g_shift_edge_ms = 0;
u32 g_last_build_id = 0;
bool g_after_shift = false;
Window g_win;
std::vector<u32> g_samples;
ULONGLONG g_win_start = 0;
bool g_shift_down = false;
int g_shift_logs = 0;
ULONGLONG g_bag_next = 0;
std::vector<BagItem> g_bag, g_bag_todo;
std::vector<u32> g_bag_skip;
std::vector<u32> g_stat_scratch;
std::string g_auto_logged;

std::string where(std::uintptr_t a) {
  HMODULE m = nullptr;
  char buf[MAX_PATH + 32];
  if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                         reinterpret_cast<LPCWSTR>(a), &m) && m) {
    wchar_t path[MAX_PATH];
    const DWORD n = GetModuleFileNameW(m, path, MAX_PATH);
    if (n > 0 && n < MAX_PATH) {
      const std::string name = log::utf8(std::filesystem::path(path).filename().wstring());
      std::snprintf(buf, sizeof buf, "%s+0x%llX", name.c_str(), static_cast<unsigned long long>(a - addr(m)));
      return buf;
    }
  }
  std::snprintf(buf, sizeof buf, "0x%016llX", static_cast<unsigned long long>(a));
  return buf;
}

void off(const char* why) {
  g_on = false;
  log::printf("tooltip: %s; tooltip block off until the game restarts", why);
}

// Every string handed to the game goes through tooltip::escape.
void utf16(const std::string& raw, std::u16string& out) {
  out.clear();
  if (raw.empty()) return;
  const std::string s = tooltip::escape(raw);
  const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
  if (n <= 0) return;
  out.resize(static_cast<std::size_t>(n));
  MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), reinterpret_cast<wchar_t*>(out.data()), n);
}

void changed() {
  AcquireSRWLockExclusive(&g_lock);
  ++g_gen;
  ReleaseSRWLockExclusive(&g_lock);
  WakeConditionVariable(&g_cv);
}

std::shared_ptr<const Published> published() {
  AcquireSRWLockShared(&g_pub_lock);
  std::shared_ptr<const Published> p = g_pub;
  ReleaseSRWLockShared(&g_pub_lock);
  return p;
}

DWORD WINAPI load_db(void*) {
  LARGE_INTEGER t0, t1;
  QueryPerformanceCounter(&t0);
  const arz::Database* db = nullptr;
  auto open = [&] {
    try {
      db = new arz::Database(arz::Database::open_game(g_game_dir));  // leaked on purpose: lives until the process ends
    } catch (const std::exception& e) {
      log::printf("tooltip: database not opened (%s); skill grants and max resist unchecked", e.what());
    }
  };
  seh::Fault f;
  if (!seh::run(open, f)) {
    log::printf("tooltip: database open FAULT 0x%08lX at %s; skill grants and max resist unchecked", f.code,
                where(f.addr).c_str());
    return 0;
  }
  QueryPerformanceCounter(&t1);
  if (!db) return 0;
  g_db = db;
  changed();
  log::printf("tooltip: database opened (%zu files, %lld ms)", db->files(), us(t0, t1) / 1000);
  const text::Table* table = nullptr;
  auto open_text = [&] {
    try {
      table = new text::Table(text::Table::open_game(g_game_dir));  // leaked on purpose
    } catch (const std::exception& e) {
      log::printf("tooltip: game text not loaded (%s); internal names shown", e.what());
    }
  };
  if (!seh::run(open_text, f)) {
    log::printf("tooltip: game text FAULT 0x%08lX at %s; internal names shown", f.code, where(f.addr).c_str());
    return 0;
  }
  QueryPerformanceCounter(&t0);
  if (table) {
    g_text = table;
    changed();
    log::printf("tooltip: game text loaded (%zu tags, %lld ms)", table->size(), us(t1, t0) / 1000);
  }
  return 0;
}

int data_level(const arz::Database* db) { return db ? 1 + (g_text.load() != nullptr) : 0; }

namespace {

// Game.dll 0x329370 GenerateSearchText calls GetUIDisplayText at +0xB8.
constexpr std::size_t kSearchCall = 0xB8, kSearchCallLen = 6;
constexpr unsigned char kSearchCallBytes[kSearchCallLen] = {0xFF, 0x90, 0x58, 0x04, 0x00, 0x00};

// Finds the line push_back and the stash search call site.
bool bind_engine_calls(const Resolve& r) {
  g_ctor = reinterpret_cast<CtorFn>(const_cast<void*>(r("GameTextLine::GameTextLine(u16string)")));
  const auto* build = static_cast<const std::byte*>(r("ItemEquipment::GetUIDisplayText"));
  const auto* search = static_cast<const std::byte*>(r("Item::GenerateSearchText"));
  if (!g_ctor || !build || !search) {
    log::write("tooltip: GameTextLine ctor, ItemEquipment::GetUIDisplayText or Item::GenerateSearchText unresolved; "
               "tooltip block off");
    return false;
  }
  bool push_ok = false, search_ok = false;
  auto check = [&] {
    const std::byte* site = build + scan::kLinePushBackSite;
    if (const auto target = scan::line_push_back_call({site, scan::kLinePushBackSiteLen}, addr(site));
        target && scan::is_line_push_back({reinterpret_cast<const std::byte*>(*target), scan::kLinePushBackLen})) {
      g_push = reinterpret_cast<PushFn>(*target);
      push_ok = true;
    }
    search_ok = std::memcmp(search + kSearchCall, kSearchCallBytes, kSearchCallLen) == 0;
  };
  seh::Fault f;
  if (!seh::run(check, f) || !push_ok || !search_ok) {
    log::printf("tooltip: %s; tooltip block off",
                !push_ok ? "vector<GameTextLine>::push_back not found / not verified in ItemEquipment::GetUIDisplayText"
                         : "Item::GenerateSearchText call site not verified");
    return false;
  }
  g_search_ret = search + kSearchCall + kSearchCallLen;
  return true;
}

// Optional: without them lists break as at UI scale 1.
void bind_screen(const Resolve& r) {
  const void* var = r("gEngine");
  const void* graphics = r("Engine::GetGraphicsEngine");
  const void* scale = r("GraphicsEngine::GetUIScaleFactor");
  const void* width = r("GraphicsEngine::GetWidth");
  if (var && graphics && scale && width) {
    g_engine_var = static_cast<const void* const*>(var);
    g_graphics = reinterpret_cast<GraphicsFn>(const_cast<void*>(graphics));
    g_ui_scale = reinterpret_cast<ScaleFn>(const_cast<void*>(scale));
    g_screen_width = reinterpret_cast<WidthFn>(const_cast<void*>(width));
  } else {
    log::write("tooltip: gEngine / GetGraphicsEngine / GetUIScaleFactor / GetWidth unresolved; lists break for "
               "UI scale 1 below 1600 pixels");
  }
}

// No allocation growth on the game thread.
void reserve_buffers() {
  g_out.reserve(kMaxOut);
  g_pool.reserve(kPool);
  g_jobs.reserve(kMaxHover);
  g_log_out.reserve(kMaxLogOut);
  g_samples.reserve(kMaxSamples);
  g_stat_scratch.reserve(kMaxSamples);
  g_bag.reserve(kMaxBag);
  g_bag_todo.reserve(kMaxBag);
  g_bag_skip.reserve(kMaxBag);
}

bool start_threads() {
  g_on = true;
  HANDLE w = CreateThread(nullptr, 0, worker, nullptr, 0, nullptr);
  if (!w) {
    g_on = false;
    log::printf("tooltip: worker thread not started (%lu); tooltip block off", GetLastError());
    return false;
  }
  SetThreadPriority(w, THREAD_PRIORITY_BELOW_NORMAL);
  CloseHandle(w);
  if (HANDLE h = CreateThread(nullptr, 0, load_db, nullptr, 0, nullptr)) CloseHandle(h);
  else log::printf("tooltip: database thread not started (%lu); skill grants and max resist unchecked", GetLastError());
  return true;
}

// Shift edges are logged to trace tooltip rebuilds.
void watch_shift(ULONGLONG now) {
  const bool shift = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
  if (shift == g_shift_down) return;
  g_shift_down = shift;
  AcquireSRWLockExclusive(&g_lock);
  const ULONGLONG since = now - g_last_build_ms;
  if (g_last_build_ms && since < 10000) {
    g_shift_edge_ms = now;
    g_after_shift = true;
    if (g_shift_logs < kShiftLogs) {
      ++g_shift_logs;
      log::printf("tooltip: Shift %s; last build %llu ms ago (item %u)", shift ? "down" : "up",
                  static_cast<unsigned long long>(since), g_last_build_id);
    }
  }
  ReleaseSRWLockExclusive(&g_lock);
}

u32 percentile(std::vector<u32>& v, double p) {
  if (v.empty()) return 0;
  const auto i = static_cast<std::size_t>(std::min<double>(static_cast<double>(v.size()) - 1, p * static_cast<double>(v.size())));
  std::nth_element(v.begin(), v.begin() + static_cast<std::ptrdiff_t>(i), v.end());
  return v[i];
}

void log_stats(ULONGLONG now) {
  AcquireSRWLockExclusive(&g_lock);
  const Window w = std::exchange(g_win, Window{});
  g_stat_scratch.assign(g_samples.begin(), g_samples.end());
  g_samples.clear();
  const double secs = static_cast<double>(now - g_win_start) / 1000.0;
  g_win_start = now;
  const std::size_t size = cache_size(), pool = g_pool.size(), queued = g_jobs.size();
  ReleaseSRWLockExclusive(&g_lock);
  const u32 p50 = percentile(g_stat_scratch, 0.50), p99 = percentile(g_stat_scratch, 0.99);
  const u32 max = g_stat_scratch.empty() ? 0 : *std::max_element(g_stat_scratch.begin(), g_stat_scratch.end());
  const u64 computed = w.computed_hover + w.computed_pool;
  PROCESS_MEMORY_COUNTERS_EX pmc{};
  pmc.cb = sizeof pmc;
  const bool mem = GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&pmc), sizeof pmc);
  log::printf("tooltip stats (%.0f s, block %s): calls %llu (%.2f/s; %llu hit, %llu miss, %llu queued, %llu skipped), "
              "lines %llu, build us p50 %u p99 %u max %u; worker: %llu scored (%llu hover, %llu pool), avg %lld us, "
              "max %lld us, %zu queued, %llu queue-full; cache %zu/%zu, pool %zu/%zu; private %.1f MB, working set %.1f MB",
              secs, g_on ? "on" : "OFF", static_cast<unsigned long long>(w.calls),
              secs > 0 ? static_cast<double>(w.calls) / secs : 0.0, static_cast<unsigned long long>(w.hits),
              static_cast<unsigned long long>(w.misses), static_cast<unsigned long long>(w.pending),
              static_cast<unsigned long long>(w.skipped), static_cast<unsigned long long>(w.lines), p50, p99, max,
              static_cast<unsigned long long>(computed), static_cast<unsigned long long>(w.computed_hover),
              static_cast<unsigned long long>(w.computed_pool),
              computed ? w.compute_sum_us / static_cast<long long>(computed) : 0, w.compute_max_us, queued,
              static_cast<unsigned long long>(w.queue_full), size, kCache, pool, kPool,
              mem ? static_cast<double>(pmc.PrivateUsage) / 1048576.0 : -1.0,
              mem ? static_cast<double>(pmc.WorkingSetSize) / 1048576.0 : -1.0);
}

}  // namespace
}  // namespace tip

using namespace tip;

bool tooltip_init(const Resolve& r, const std::wstring& mod_dir) {
  const std::wstring ini = mod_dir + L"GDCompare.ini";
  g_preset_path = mod_dir + L"GDCompare.preset.toml";
  g_named_dir = mod_dir + L"GDCompare.presets\\";
  g_debug = GetPrivateProfileIntW(L"tooltip", L"debug", 0, ini.c_str()) != 0;
  if (!bind_engine_calls(r)) return false;
  bind_screen(r);
  reserve_buffers();
  log::printf("tooltip: push_back at %s (verified %zu bytes), search-text caller %s, debug line %s "
              "(GDCompare.ini [tooltip] debug)",
              where(reinterpret_cast<std::uintptr_t>(g_push)).c_str(), scan::kLinePushBackLen, where(addr(g_search_ret)).c_str(),
              g_debug ? "on" : "off");
  // Mod folder: compat\ or x64\ under the game root.
  g_game_dir = std::filesystem::path(mod_dir).parent_path().parent_path();
  g_win_start = GetTickCount64();
  AcquireSRWLockExclusive(&g_lock);
  publish_presets();
  ReleaseSRWLockExclusive(&g_lock);
  if (!start_threads()) return false;
  log::printf("tooltip: block on (scored on a worker thread; pool %zu items, cache %zu blocks); database from %s",
              kPool, kCache, log::utf8(g_game_dir.wstring()).c_str());
  return true;
}

const arz::Database* tooltip_database() { return g_db.load(); }

void tooltip_publish(const snap::Snapshot& s, const Probe& probe) {
  if (!g_on) return;
  std::shared_ptr<const Published> old;
  guarded("publish", [&] {
    auto p = std::make_shared<Published>();
    p->seq = ++g_seq;
    p->snap = s;
    p->snap.tooltip_items.clear();
    // Auto snapshots keep the last manual inventory list.
    // shortcut: an empty inventory also keeps the old list (no "inventory read" flag) — revisit when the snapshot
    // records whether that section ran.
    p->snap.inventory.clear();
    if (!s.inventory.empty()) {
      p->snap.inventory.reserve(s.inventory.size());
      for (const auto& it : s.inventory) {
        snap::Item light;
        light.id = it.id;
        light.record = it.record;
        p->snap.inventory.push_back(std::move(light));
      }
    } else if (const auto prev = published()) {
      p->snap.inventory = prev->snap.inventory;
    }
    p->probe = probe;
    // Warm the database cache with each equipped item's own swap.
    if (const arz::Database* db = g_db.load()) {
      for (const auto& it : p->snap.equipped) (void)model::swap(p->snap, it, it.location, db);
      const score::Preset defaults;
      (void)score::compare(p->snap, p->snap, score::main_skills(p->snap, defaults, db), defaults, db);
    }
    AcquireSRWLockExclusive(&g_pub_lock);
    old = std::exchange(g_pub, std::move(p));
    ReleaseSRWLockExclusive(&g_pub_lock);
  });
  changed();
}

void tooltip_freeze() {
  if (!g_on) return;
  guarded("freeze", [&] {
    const std::shared_ptr<const Published> pub = published();
    const arz::Database* db = g_db.load();
    if (!pub || !db) {
      log::printf("freeze: %s; nothing written", !pub ? "no character snapshot yet" : "database not loaded yet");
      return;
    }
    const std::string file = score::frozen_file(pub->snap);
    AcquireSRWLockShared(&g_lock);
    const std::shared_ptr<const Presets> ps = g_presets;
    ReleaseSRWLockShared(&g_lock);
    const Labels labels(db, g_text.load());
    std::string line;
    const score::Preset& chosen = choose(*ps, pub->snap, db, labels, line);
    const std::string from = chosen.name.empty() ? "GDCompare.preset.toml" : chosen.name;
    const std::string text = score::freeze(chosen, pub->snap, *db, file, labels);
    CreateDirectoryW(g_named_dir.c_str(), nullptr);  // fails harmlessly when it exists
    const std::wstring path = g_named_dir + std::wstring(file.begin(), file.end()) + L".toml";  // ASCII by construction
    HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
      log::printf("freeze: cannot write GDCompare.presets\\%s.toml (%lu)", file.c_str(), GetLastError());
      return;
    }
    DWORD written = 0;
    const BOOL ok = WriteFile(h, text.data(), static_cast<DWORD>(text.size()), &written, nullptr);
    CloseHandle(h);
    if (!ok || written != text.size())
      log::printf("freeze: writing GDCompare.presets\\%s.toml failed (%lu)", file.c_str(), GetLastError());
    else
      log::printf("freeze: wrote GDCompare.presets\\%s.toml from %s (snapshot %llu); Auto uses it for this character",
                  file.c_str(), from.c_str(), static_cast<unsigned long long>(pub->seq));
  });
}

void tooltip_tick() {
  DWORD expected = 0;
  g_game_tid.compare_exchange_strong(expected, GetCurrentThreadId());
  const ULONGLONG now = GetTickCount64();
  watch_shift(now);
  poll_presets(now);
  if (g_on) guarded("inventory", [&] { bag_tick(now); });
  if (now - g_win_start >= kStatsMs) log_stats(now);
}

}  // namespace gdc::adapter
