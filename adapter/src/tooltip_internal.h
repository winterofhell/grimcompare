#pragma once
// Adapter tooltip internals; threads and layouts in MECHANICS §12.2.
#include <windows.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "gdc/arz.h"
#include "gdc/labels.h"
#include "gdc/score.h"
#include "gdc/text.h"
#include "gdc/tooltip.h"
#include "log.h"
#include "seh.h"
#include "snapshot.h"

namespace gdc::adapter::tip {

using u32 = std::uint32_t;
using u64 = std::uint64_t;

// GameTextLine, Game.dll ctor 0x313030: 0x40 bytes (MECHANICS §12.2).
constexpr std::size_t kLine = 0x40, kMaxLines = 4096;
static_assert(sizeof(std::u16string) == 0x20, "the GameTextLine ctor reads an MSVC x64 std::u16string");
using CtorFn = void* (*)(void* self, u32 cls, const std::u16string* text, bool flag, const void* texture, float scale);
using PushFn = void (*)(void* vec, void* line);
using GraphicsFn = void* (*)(const void* engine);
using ScaleFn = float (*)(const void* graphics);
using WidthFn = int (*)(const void* graphics);
// re/GameTextClass.tsv.
constexpr u32 kClass[] = {0x24, 0x12, 0x22, 0x34, 0x1A, 0x50};
constexpr u32 kGray = 0x50;
// Room for the previous snapshot's entries too.
constexpr std::size_t kCache = 1024, kPool = 256, kMaxOut = 64, kMaxHover = 16;
constexpr std::size_t kMaxBag = 512, kReadsPerTick = 4, kMaxSamples = 8192, kMaxLogOut = 4096;
constexpr std::size_t kMaxNamed = 32;
constexpr int kFirstLogs = 20, kTraceLogs = 5, kShiftLogs = 20;
constexpr ULONGLONG kStatsMs = 60000, kShiftWindowMs = 2000, kPresetPollMs = 1000, kBagMs = 1000;
constexpr u64 kMaxPresetBytes = 1 << 20;

struct Published {
  u64 seq = 0;
  snap::Snapshot snap;
  Probe probe;
};

struct Presets {
  u64 version = 0;
  score::Preset main;
  std::vector<std::pair<std::string, score::Preset>> named;   // Auto order (tried_first)
  tooltip::Screen screen;
};

struct Key {
  u32 id = 0;
  u64 fp = 0, seq = 0, preset = 0;
  int data = 0;
  bool shift = false;
  bool operator==(const Key&) const = default;
};

struct Entry {
  Key key;
  u64 used = 0;            // LRU stamp; 0 = empty
  bool ready = false;
  bool equippable = false; // ready with no lines: already equipped
  bool logged = true;
  long long model_us = 0;
  std::vector<u32> classes;
  std::vector<std::u16string> text;
  std::vector<std::string> log;
};

struct Cand {
  u32 id = 0;
  u64 fp = 0;
  int type = 0;
  std::shared_ptr<const snap::Item> item;
  ULONGLONG seen = 0;
};

struct Job {
  Cand cand;
  bool shift = false;
};

struct Out {
  u32 cls;
  const std::u16string* text;
};

struct Named {
  std::wstring file;
  FILETIME mtime{};
  std::optional<score::Preset> preset;
};

// Counters for one stats log line.
struct Window {
  u64 calls = 0, hits = 0, misses = 0, pending = 0, lines = 0, skipped = 0;
  u64 computed_hover = 0, computed_pool = 0, queue_full = 0;
  long long compute_sum_us = 0, compute_max_us = 0;
};

// Set once by tooltip_init.
extern std::atomic<bool> g_on;
extern bool g_debug;
extern CtorFn g_ctor;
extern PushFn g_push;
extern const void* const* g_engine_var;
extern GraphicsFn g_graphics;
extern ScaleFn g_ui_scale;
extern WidthFn g_screen_width;
extern const void* g_search_ret;
extern std::filesystem::path g_game_dir;
extern std::wstring g_preset_path, g_named_dir;

// Loaded once on its own thread.
extern std::atomic<const arz::Database*> g_db;
extern std::atomic<const text::Table*> g_text;

// The last snapshot, under g_pub_lock.
extern SRWLOCK g_pub_lock;
extern std::shared_ptr<const Published> g_pub;
extern u64 g_seq;
extern std::atomic<DWORD> g_game_tid;

// Everything below is under g_lock.
extern SRWLOCK g_lock;
extern CONDITION_VARIABLE g_cv;
extern std::array<Entry, kCache> g_cache;
extern u64 g_use, g_gen;
extern std::vector<Cand> g_pool;
extern std::vector<Job> g_jobs;
extern std::vector<std::string> g_log_out;  // keeps file I/O off the game thread
extern std::shared_ptr<const Presets> g_presets;
extern tooltip::Screen g_screen;
extern const void* g_player;
extern std::vector<Out> g_out;
extern std::string g_auto_logged;

// Preset files; a broken file keeps the last good preset.
extern score::Preset g_preset;
extern u64 g_preset_version;
extern std::u16string g_preset_error, g_named_error;
extern FILETIME g_preset_mtime;
extern bool g_preset_seen;
extern ULONGLONG g_preset_poll_ms;
extern std::vector<Named> g_named;

// Call log and stats.
extern u64 g_calls_total;
extern ULONGLONG g_last_log_ms, g_last_build_ms, g_shift_edge_ms;
extern u32 g_last_build_id;
extern bool g_after_shift;
extern Window g_win;
extern std::vector<u32> g_samples, g_stat_scratch;
extern ULONGLONG g_win_start;
extern bool g_shift_down;
extern int g_shift_logs;

// Inventory precompute.
extern ULONGLONG g_bag_next;
extern std::vector<BagItem> g_bag, g_bag_todo;
extern std::vector<u32> g_bag_skip;

template <class T>
T rd(const void* p, std::size_t off) {
  T v;
  std::memcpy(&v, static_cast<const char*>(p) + off, sizeof v);
  return v;
}

inline std::uintptr_t addr(const void* p) { return reinterpret_cast<std::uintptr_t>(p); }

inline long long us(LARGE_INTEGER a, LARGE_INTEGER b) { return log::elapsed_us(a, b); }

// tooltip.cpp
std::string where(std::uintptr_t a);
void off(const char* why);
void utf16(const std::string& raw, std::u16string& out);
void changed();
std::shared_ptr<const Published> published();
DWORD WINAPI load_db(void*);
int data_level(const arz::Database* db);

// Runs f; a fault or exception turns the block off.
template <class F>
bool guarded(const char* what, F&& f) {
  auto body = [&] {
    try {
      f();
    } catch (const std::exception& e) {
      char buf[256];
      std::snprintf(buf, sizeof buf, "%s: C++ exception %s", what, e.what());
      off(buf);
    } catch (...) {
      char buf[128];
      std::snprintf(buf, sizeof buf, "%s: unknown C++ exception", what);
      off(buf);
    }
  };
  seh::Fault fault;
  if (seh::run(body, fault)) return g_on;
  char buf[200];
  std::snprintf(buf, sizeof buf, "%s: FAULT 0x%08lX at %s", what, fault.code, where(fault.addr).c_str());
  off(buf);
  return false;
}

// tooltip_worker.cpp: cache, pool and the scoring thread.
std::size_t cache_size();
Entry* find(const Key& k);
Entry* claim(const Key& k, bool ready);
void pool_put(Cand c);
void reset_for(const void* player);
void queue_log(std::string line);
void queue_log(std::vector<std::string>& lines);
DWORD WINAPI worker(void*);
void bag_tick(ULONGLONG now);

// tooltip_presets.cpp
const score::Preset& choose(const Presets& ps, const snap::Snapshot& s, const arz::Database* db, const Labels& labels,
                            std::string& line);
void publish_presets();
void poll_presets(ULONGLONG now);

}  // namespace gdc::adapter::tip
