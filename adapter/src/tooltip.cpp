// Tooltip block, see tooltip.h. Threads:
// - Game thread (GameEngine::Update and the GetUIDisplayText detours): reads items from the engine (the item
//   reader uses game-thread scratch), looks the block up in the cache and appends it. It never scores: a miss
//   reads the hovered item (~10 us), queues it and shows a gray "computing..." line; the block appears on the
//   next hover (no engine call to rebuild an open tooltip is traced). Every second it also lists the
//   inventory and reads a few new items per tick into the candidate pool.
// - Worker thread: scores queued hovers first, then every pool item (both Shift states) against the latest
//   snapshot and presets, and writes the score log. A new snapshot, preset, pool item or loaded database
//   starts a new pass; cached blocks are skipped.
// - Writer thread (hooks.cpp): tooltip_publish.
// A build on any other thread gets a gray line saying so. Cache, pool, queue and presets are under g_lock (the
// worker holds it only to pick work and store results). Any fault or C++ exception turns the block off for
// the session; the game's own lines are never changed.
#include "tooltip.h"

#include <windows.h>
#include <psapi.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "calibration.h"
#include "gdc/arz.h"
#include "gdc/code_scan.h"
#include "gdc/labels.h"
#include "gdc/score.h"
#include "gdc/text.h"
#include "gdc/tooltip.h"
#include "log.h"
#include "seh.h"

namespace gdc::adapter {
namespace {

using u32 = std::uint32_t;
using u64 = std::uint64_t;

// GameTextLine (ctor 0x313030): 0x40 bytes, +0 GameTextClass, +8 std::u16string, +0x28 bool, +0x30
// GraphicsTexture*, +0x38 float. mem::vector: {begin, end, capacity}. The inline lines of
// ItemEquipment::GetUIDisplayText use bool false, texture null, 1.0 (0x33DF35..0x33DF44).
constexpr std::size_t kLine = 0x40, kMaxLines = 4096;
static_assert(sizeof(std::u16string) == 0x20, "the GameTextLine ctor reads an MSVC x64 std::u16string");
using CtorFn = void* (*)(void* self, u32 cls, const std::u16string* text, bool flag, const void* texture, float scale);
using PushFn = void (*)(void* vec, void* line);
// GameTextClass per gdc::tooltip::Tone (re/GameTextClass.tsv): ItemSkillHeading (textgold), ItemBaseStats
// (white), ItemRelicCompleteTitle (textgreen), SkillRequirementsNotMet (textred), ItemSetBonuses
// (lightgold), GrayStats1 (gray).
constexpr u32 kClass[] = {0x24, 0x12, 0x22, 0x34, 0x1A, 0x50};
constexpr u32 kGray = 0x50;
// Item::GenerateSearchText (0x329370) calls GetUIDisplayText (vt+0x458) at +0xB8 for stash search text.
constexpr std::size_t kSearchCall = 0xB8, kSearchCallLen = 6;
constexpr unsigned char kSearchCallBytes[kSearchCallLen] = {0xFF, 0x90, 0x58, 0x04, 0x00, 0x00};  // call [rax+0x458]
// Cache: pool items x 2 Shift states + hovers, with room for the previous snapshot's entries.
constexpr std::size_t kCache = 1024, kPool = 256, kMaxOut = 64, kMaxHover = 16;
constexpr std::size_t kMaxBag = 512, kReadsPerTick = 4, kMaxSamples = 8192, kMaxLogOut = 4096;
constexpr int kFirstLogs = 20, kTraceLogs = 5, kShiftLogs = 20;
constexpr ULONGLONG kStatsMs = 60000, kShiftWindowMs = 2000, kPresetPollMs = 1000, kBagMs = 1000;
constexpr u64 kMaxPresetBytes = 1 << 20;

struct Published {
  u64 seq = 0;
  snap::Snapshot snap;
  Probe probe;
};

// What the worker scores with: copied on the game thread whenever g_preset_version changes.
struct Presets {
  u64 version = 0;
  score::Preset main;                                         // GDCompare.preset.toml
  std::vector<std::pair<std::string, score::Preset>> named;   // GDCompare.presets\*.toml that loaded, Auto order (tried_first)
};

// A block depends on the item (id, socketed component / augment), the snapshot, the presets, the data the
// labels come from (data_level) and Shift.
struct Key {
  u32 id = 0;
  u64 fp = 0, seq = 0, preset = 0;
  int data = 0;
  bool shift = false;
  bool operator==(const Key&) const = default;
};

// One cached block. Slots are reused: their vectors and strings keep their capacity.
struct Entry {
  Key key;
  u64 used = 0;            // LRU stamp; 0: empty
  bool ready = false;      // false: queued / computing
  bool equippable = false; // ready, no lines, equippable: one of the equipped items
  bool logged = true;      // `log` already written
  long long model_us = 0;
  std::vector<u32> classes;
  std::vector<std::u16string> text;
  std::vector<std::string> log;  // scores with inputs, warnings
};

// An item the worker can score: read on the game thread, shared read-only with the worker.
struct Cand {
  u32 id = 0;
  u64 fp = 0;
  int type = 0;
  std::shared_ptr<const snap::Item> item;
  ULONGLONG seen = 0;  // last hover or inventory listing
};

struct Job {
  Cand cand;
  bool shift = false;
};

struct Out {
  u32 cls;
  const std::u16string* text;
};

enum class How { None, Hit, Miss, Pending };

struct Result {
  const char* what = "appended";
  std::size_t lines = 0;
  How how = How::None;
  u32 id = 0;
  int type = -1;
  long long read_us = 0, model_us = 0;
};

std::atomic<bool> g_on{false};
bool g_debug = false;
CtorFn g_ctor = nullptr;
PushFn g_push = nullptr;
const void* g_search_ret = nullptr;
std::atomic<const arz::Database*> g_db{nullptr};
std::atomic<const text::Table*> g_text{nullptr};  // set after g_db
std::filesystem::path g_game_dir;
SRWLOCK g_pub_lock = SRWLOCK_INIT;
std::shared_ptr<const Published> g_pub;  // under g_pub_lock
u64 g_seq = 0;                           // writer thread
std::atomic<DWORD> g_game_tid{0};

const std::u16string kOk = u"GDC ok";
const std::u16string kOffThread = u"GD Compare: tooltip built off the game thread";
const std::u16string kWaitingNone = u"GD Compare: waiting for a character snapshot...";
const std::u16string kWaitingGear = u"GD Compare: waiting for a snapshot of the current gear...";
const std::u16string kEquipped = u"GD Compare: equipped";
const std::u16string kComputing = u"GD Compare: computing... (hover again)";

// Under g_lock.
SRWLOCK g_lock = SRWLOCK_INIT;
CONDITION_VARIABLE g_cv = CONDITION_VARIABLE_INIT;  // worker sleeps on it (with g_lock)
std::array<Entry, kCache> g_cache;
u64 g_use = 0;
u64 g_gen = 1;               // bumped by anything that can change a block: snapshot, presets, pool, database
std::vector<Cand> g_pool;    // reserved kPool
std::vector<Job> g_jobs;     // hovers waiting for the worker, reserved kMaxHover; oldest first
std::vector<std::string> g_log_out;  // lines for the worker to write (keeps file I/O off the game thread)
std::shared_ptr<const Presets> g_presets;
const void* g_player = nullptr;
std::vector<Out> g_out;  // reserved kMaxOut at init
// Score preset: GDCompare.preset.toml next to the .asi, polled on the game thread (tooltip_tick). A file
// that fails to parse keeps the last good preset; the block shows the error.
score::Preset g_preset;
u64 g_preset_version = 1;
std::u16string g_preset_error;  // "" = none
std::wstring g_preset_path;
FILETIME g_preset_mtime{};
bool g_preset_seen = false;  // the file existed at the last poll
ULONGLONG g_preset_poll_ms = 0;
// Named presets for Auto: GDCompare.presets\*.toml next to the .asi, file name order. While g_preset has
// [preset] auto = true (the default), the first one whose [match] fits the character scores the block
// instead of g_preset (score::matches). Reloaded when the folder's file list or a file's mtime changes.
struct Named {
  std::wstring file;
  FILETIME mtime{};
  std::optional<score::Preset> preset;  // nullopt: did not load (g_named_error)
};
constexpr std::size_t kMaxNamed = 32;
std::vector<Named> g_named;
std::wstring g_named_dir;
std::u16string g_named_error;  // "" = none
u64 g_calls_total = 0;
ULONGLONG g_last_log_ms = 0, g_last_build_ms = 0, g_shift_edge_ms = 0;
u32 g_last_build_id = 0;
bool g_after_shift = false;
struct Window {
  u64 calls = 0, hits = 0, misses = 0, pending = 0, lines = 0, skipped = 0;
  u64 computed_hover = 0, computed_pool = 0, queue_full = 0;  // worker
  long long compute_sum_us = 0, compute_max_us = 0;           // worker
} g_win;
std::vector<u32> g_samples;  // build time (us) of each call in the window, reserved kMaxSamples
ULONGLONG g_win_start = 0;
// Game thread only (tooltip_tick).
bool g_shift_down = false;
int g_shift_logs = 0;
ULONGLONG g_bag_next = 0;
std::vector<BagItem> g_bag, g_bag_todo;  // reserved kMaxBag
std::vector<u32> g_bag_skip;             // inventory items that are not comparable; reserved kMaxBag
std::vector<u32> g_stat_scratch;         // reserved kMaxSamples
// Worker thread only.
std::string g_auto_logged;  // last Auto choice written to the log

template <class T>
T rd(const void* p, std::size_t off) {
  T v;
  std::memcpy(&v, static_cast<const char*>(p) + off, sizeof v);
  return v;
}

std::uintptr_t addr(const void* p) { return reinterpret_cast<std::uintptr_t>(p); }

// "Game.dll+0x33DF4B" (module + offset) or the absolute address. Not for hot paths.
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

// Runs `f` under SEH with C++ exceptions caught; false (logged, block off) on either.
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

void utf16(const std::string& s, std::u16string& out) {
  out.clear();
  if (s.empty()) return;
  const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
  if (n <= 0) return;
  out.resize(static_cast<std::size_t>(n));
  MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), reinterpret_cast<wchar_t*>(out.data()), n);
}

long long us(LARGE_INTEGER a, LARGE_INTEGER b) { return log::elapsed_us(a, b); }

// Any thread, not under g_lock: something a block depends on changed; the worker starts a new pass.
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
      db = new arz::Database(arz::Database::open_game(g_game_dir));  // kept until the process ends
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
  const text::Table* tx = nullptr;
  auto open_text = [&] {
    try {
      tx = new text::Table(text::Table::open_game(g_game_dir));  // kept until the process ends
    } catch (const std::exception& e) {
      log::printf("tooltip: game text not loaded (%s); internal names shown", e.what());
    }
  };
  if (!seh::run(open_text, f)) {
    log::printf("tooltip: game text FAULT 0x%08lX at %s; internal names shown", f.code, where(f.addr).c_str());
    return 0;
  }
  QueryPerformanceCounter(&t0);
  if (tx) {
    g_text = tx;
    changed();
    log::printf("tooltip: game text loaded (%zu tags, %lld ms)", tx->size(), us(t1, t0) / 1000);
  }
  return 0;
}

// 0: no database, 1: database, 2: database and game text.
int data_level(const arz::Database* db) { return db ? 1 + (g_text.load() != nullptr) : 0; }

bool valid_vector(const void* v) {
  const auto b = rd<std::uintptr_t>(v, 0), e = rd<std::uintptr_t>(v, 8), c = rd<std::uintptr_t>(v, 16);
  if (!b) return !e && !c;
  return b <= e && e <= c && (e - b) % kLine == 0 && (c - b) % kLine == 0 && (e - b) / kLine <= kMaxLines;
}

// Auto: the first named preset (score::tried_first order) made for this character (score::match), else
// GDCompare.preset.toml. `line`: the choice and why (each named preset's match result), for the log.
const score::Preset& choose(const Presets& ps, const snap::Snapshot& s, const arz::Database* db, const Labels& labels,
                            std::string& line) {
  const score::Preset* p = &ps.main;
  std::string what = "GDCompare.preset.toml", why;
  if (!ps.main.auto_select) why = "[preset] auto = false";
  else if (!db) why = "no database";
  else
    for (const auto& [file, preset] : ps.named) {
      const score::Match m = score::match(preset, s, *db, labels);
      why += (why.empty() ? "" : "; ") + file + (m.ok ? " fits: " : " not used: ") + m.why;
      if (m.ok) {
        p = &preset;
        what = file;
        break;
      }
    }
  if (const score::Stage* st = score::stage(*p, s)) what += ", stage " + st->name;
  line = what + (why.empty() ? "" : " (" + why + ")");
  return *p;
}

// --- under g_lock

std::size_t cache_size() {
  std::size_t n = 0;
  for (const auto& e : g_cache) n += e.used != 0;
  return n;
}

Entry* find(const Key& k) {
  for (auto& e : g_cache)
    if (e.used && e.key == k) return &e;
  return nullptr;
}

Entry* lru() {
  Entry* e = &g_cache[0];
  for (auto& x : g_cache)
    if (x.used < e->used) e = &x;
  return e;
}

// A fresh slot for `k` (the least recently used one), marked used now.
Entry* claim(const Key& k, bool ready) {
  Entry* e = lru();
  e->key = k;
  e->ready = ready;
  e->equippable = false;
  e->logged = true;
  e->model_us = 0;
  e->classes.clear();
  e->text.clear();
  e->log.clear();
  e->used = ++g_use;
  return e;
}

// Adds or refreshes a pool item (the least recently seen one goes when full) and starts a worker pass.
void pool_put(Cand c) {
  auto it = std::find_if(g_pool.begin(), g_pool.end(), [&](const Cand& x) { return x.id == c.id; });
  if (it == g_pool.end() && g_pool.size() < kPool) it = g_pool.insert(g_pool.end(), Cand{});  // within the reserve
  else if (it == g_pool.end())
    it = std::min_element(g_pool.begin(), g_pool.end(), [](const Cand& a, const Cand& b) { return a.seen < b.seen; });
  *it = std::move(c);
  ++g_gen;
  WakeConditionVariable(&g_cv);
}

// A different character (or none): nothing cached applies.
void reset_for(const void* player) {
  if (player == g_player) return;
  if (g_player) log::printf("tooltip: character changed; cache cleared (%zu entries, %zu pool items)", cache_size(), g_pool.size());
  for (auto& e : g_cache) e.used = 0;
  g_pool.clear();
  g_jobs.clear();
  g_bag_skip.clear();
  g_bag_todo.clear();
  g_player = player;
  ++g_gen;
}

// The worker writes these (the game thread never waits on the log file).
void queue_log(std::string line) {
  if (g_log_out.size() < kMaxLogOut) g_log_out.push_back(std::move(line));
  WakeConditionVariable(&g_cv);
}

void queue_log(std::vector<std::string>& lines) {
  for (auto& l : lines) queue_log(std::move(l));
  lines.clear();
}

// The lines for one build (into g_out) and what happened. Game thread.
Result plan(const void* item, void* lines, const void* caller, bool game_thread) {
  Result r;
  g_out.clear();
  if (caller == g_search_ret) {
    r.what = "skipped: stash search text";
    return r;
  }
  if (!valid_vector(lines)) {
    r.what = "dropped: line vector fails the layout check";
    return r;
  }
  if (g_debug) g_out.push_back({kGray, &kOk});
  r.id = object_id(item);
  if (!game_thread) {
    g_out.push_back({kGray, &kOffThread});
    return r;
  }
  if (!r.id) return r;
  Probe live;
  const Take t = snapshot_probe(live);
  if (t == Take::Faulted) {
    off("character probe faulted");
    r.what = "dropped: probe faulted";
    g_out.clear();
    return r;
  }
  if (t != Take::Ok) return r;
  reset_for(live.player);
  const std::shared_ptr<const Published> pub = published();
  const bool current = pub && live == pub->probe;
  const bool shift = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
  const arz::Database* db = g_db.load();
  const Key k{r.id, item_fingerprint(item), pub ? pub->seq : 0, g_preset_version, data_level(db), shift};
  Entry* e = current ? find(k) : nullptr;
  if (e) {
    e->used = ++g_use;
    r.how = e->ready ? How::Hit : How::Pending;
    r.type = -1;
    if (e->ready && !e->logged) {
      e->logged = true;
      queue_log(e->log);
    }
  } else {
    // Miss: read the item here (game-thread reader), score on the worker.
    LARGE_INTEGER t0, t1;
    QueryPerformanceCounter(&t0);
    snap::Item cand;
    int type = 0;
    const Take ct = snapshot_candidate(item, cand, type);
    QueryPerformanceCounter(&t1);
    r.read_us = us(t0, t1);
    if (ct == Take::Faulted) {
      off("reading the hovered item faulted");
      r.what = "dropped: fault";
      g_out.clear();
      return r;
    }
    if (ct != Take::Ok) return r;
    r.type = type;
    r.how = How::Miss;
    const bool equippable = !tooltip::locations_for(type).empty();
    if (!equippable) return r;
    if (!current) {  // the block waits for a snapshot of the gear worn now
      g_out.push_back({kGray, pub ? &kWaitingGear : &kWaitingNone});
      pool_put({r.id, k.fp, type, std::make_shared<const snap::Item>(std::move(cand)), GetTickCount64()});
      return r;
    }
    const bool worn = std::any_of(pub->snap.equipped.begin(), pub->snap.equipped.end(),
                                  [&](const snap::Item& x) { return x.id == r.id; });
    if (worn) {
      e = claim(k, true);
      e->equippable = true;
    } else {
      Cand c{r.id, k.fp, type, std::make_shared<const snap::Item>(std::move(cand)), GetTickCount64()};
      if (g_jobs.size() < kMaxHover) {
        claim(k, false);
        g_jobs.push_back({c, shift});
      } else {
        ++g_win.queue_full;  // no pending entry: the next hover queues it again
      }
      pool_put(std::move(c));
      r.how = How::Pending;
    }
  }
  if (!g_on) {
    g_out.clear();
    r.what = "dropped: fault";
    return r;
  }
  if (!g_preset_error.empty()) g_out.push_back({kClass[static_cast<std::size_t>(tooltip::Tone::Warning)], &g_preset_error});
  if (!g_named_error.empty()) g_out.push_back({kClass[static_cast<std::size_t>(tooltip::Tone::Warning)], &g_named_error});
  if (r.how == How::Pending) {
    g_out.push_back({kGray, &kComputing});
    return r;
  }
  if (e->text.empty() && e->equippable) g_out.push_back({kGray, &kEquipped});
  r.model_us = e->model_us;
  for (std::size_t i = 0; i < e->text.size() && g_out.size() < kMaxOut; ++i) g_out.push_back({e->classes[i], &e->text[i]});
  return r;
}

// Each line: the engine's ctor copies our text into a buffer from the game's allocator; push_back moves
// it into the vector (the temporary is left empty, nothing to free).
void append(void* lines, Result& r) {
  for (const Out& o : g_out) {
    alignas(16) unsigned char tmp[kLine] = {};
    g_ctor(tmp, o.cls, o.text, false, nullptr, 1.0f);
    g_push(lines, tmp);
    ++r.lines;
  }
}

const char* how_text(How h) {
  switch (h) {
    case How::Hit: return "cache hit";
    case How::Miss: return "cache miss";
    case How::Pending: return "queued for the worker";
    case How::None: break;
  }
  return "no block";
}

void log_call(const Result& r, const void* caller, DWORD tid, long long took_us, bool trace) {
  char hdr[512];
  std::snprintf(hdr, sizeof hdr,
                "tooltip call #%llu: tid %lu%s, caller %s, item %u%s%s, %s %zu line(s), %s, %lld us (read %lld; worker model %lld)",
                static_cast<unsigned long long>(g_calls_total), tid, tid == g_game_tid.load() ? " (game)" : " (NOT game)",
                where(addr(caller)).c_str(), r.id, r.type >= 0 ? ", type " : "",
                r.type >= 0 ? std::to_string(r.type).c_str() : "", r.what, r.lines, how_text(r.how), took_us, r.read_us,
                r.model_us);
  queue_log(hdr);
  if (!trace) return;
  void* frames[10] = {};
  const USHORT n = RtlCaptureStackBackTrace(0, 10, frames, nullptr);
  std::string s = "  stack:";
  for (USHORT i = 0; i < n; ++i) s += " " + where(addr(frames[i]));
  queue_log(std::move(s));
}

// --- worker thread

struct Computed {
  bool equippable = false;
  long long us = 0;
  std::vector<u32> classes;
  std::vector<std::u16string> text;
  std::vector<std::string> log;
};

// Not under g_lock. false: faulted (block off).
bool compute(const Published& pub, const Presets& ps, const Cand& c, bool shift, const arz::Database* db, Computed& out) {
  return guarded("worker", [&] {
    LARGE_INTEGER t0, t1;
    QueryPerformanceCounter(&t0);
    const Labels labels(db, db ? g_text.load() : nullptr);
    std::string chose;
    const score::Preset& preset = choose(ps, pub.snap, db, labels, chose);
    if (chose != g_auto_logged) {
      g_auto_logged = chose;
      log::printf("tooltip: scoring with %s", chose.c_str());
    }
    out.equippable = !tooltip::locations_for(c.type).empty();
    std::vector<tooltip::Line> lines = tooltip::compare(pub.snap, *c.item, c.type, shift, db, preset, labels, &out.log);
    // Passive calibration found a swap the model got wrong this session: say so under the heading.
    if (const int n = calibration_failures(); n > 0 && !lines.empty())
      lines.insert(lines.begin() + 1, {tooltip::Tone::Warning, "! calibration: " + std::to_string(n) +
                                                                   " failed this session (GDCompare.calibration.txt)"});
    out.classes.reserve(lines.size());
    out.text.resize(lines.size());
    for (std::size_t i = 0; i < lines.size(); ++i) {
      out.classes.push_back(kClass[static_cast<std::size_t>(lines[i].tone)]);
      utf16(lines[i].text, out.text[i]);
    }
    QueryPerformanceCounter(&t1);
    out.us = us(t0, t1);
  });
}

// Under g_lock: the result into the entry for `k` (the pending one, else a fresh slot).
void store(const Key& k, Computed&& c, bool logged) {
  Entry* e = find(k);
  if (!e) e = claim(k, true);
  e->ready = true;
  e->equippable = c.equippable;
  e->logged = logged;
  e->model_us = c.us;
  e->classes = std::move(c.classes);
  e->text = std::move(c.text);
  e->log = std::move(c.log);
}

DWORD WINAPI worker(void*) {
  struct Task {
    Cand cand;
    bool shift = false;
  };
  std::vector<Task> pass;  // pool items x Shift states, most recently seen first, Shift off first
  std::size_t pos = 0;
  u64 pass_gen = 0;
  std::vector<std::string> logs;
  AcquireSRWLockExclusive(&g_lock);
  for (;;) {
    while (g_on && g_log_out.empty() && g_jobs.empty() && g_gen == pass_gen && pos >= pass.size())
      SleepConditionVariableSRW(&g_cv, &g_lock, INFINITE, 0);
    if (!g_on) break;
    if (!g_log_out.empty()) {
      logs.swap(g_log_out);
      ReleaseSRWLockExclusive(&g_lock);
      for (const auto& l : logs) log::write(l);
      logs.clear();
      AcquireSRWLockExclusive(&g_lock);
      continue;
    }
    const std::shared_ptr<const Published> pub = published();
    const std::shared_ptr<const Presets> ps = g_presets;
    const arz::Database* db = g_db.load();
    if (!pub || !ps) {  // nothing to score against yet
      g_jobs.clear();
      pass_gen = g_gen;
      pos = pass.size();
      continue;
    }
    const int data = data_level(db);
    Task task;
    bool hover = false;
    if (!g_jobs.empty()) {
      task = {g_jobs.front().cand, g_jobs.front().shift};
      g_jobs.erase(g_jobs.begin());
      hover = true;
    } else {
      if (pass_gen != g_gen) {
        pass_gen = g_gen;
        pass.clear();
        std::vector<const Cand*> order;
        order.reserve(g_pool.size());
        for (const Cand& c : g_pool) order.push_back(&c);
        std::sort(order.begin(), order.end(), [](const Cand* a, const Cand* b) { return a->seen > b->seen; });
        for (const bool shift : {false, true})
          for (const Cand* c : order) pass.push_back({*c, shift});
        pos = 0;
      }
      if (pos >= pass.size()) continue;
      task = std::move(pass[pos++]);
    }
    const Key k{task.cand.id, task.cand.fp, pub->seq, ps->version, data, task.shift};
    if (const Entry* e = find(k); e && e->ready) continue;
    ReleaseSRWLockExclusive(&g_lock);
    Computed out;
    const bool ok = compute(*pub, *ps, task.cand, task.shift, db, out);
    if (ok && hover) {  // likely shown on the next hover: log now
      log::printf("tooltip score: item %u%s, %lld us on the worker", task.cand.id, task.shift ? " (Shift)" : "", out.us);
      for (const auto& l : out.log) log::printf("  %s", l.c_str());
      out.log.clear();
    } else if (ok) {
      out.log.insert(out.log.begin(), "tooltip score: item " + std::to_string(task.cand.id) +
                                          (task.shift ? " (Shift)" : "") + ", " + std::to_string(out.us) +
                                          " us on the worker (precomputed)");
      for (std::size_t i = 1; i < out.log.size(); ++i) out.log[i].insert(0, "  ");
    }
    AcquireSRWLockExclusive(&g_lock);
    if (!ok) break;
    const long long took = out.us;
    store(k, std::move(out), hover);
    ++(hover ? g_win.computed_hover : g_win.computed_pool);
    g_win.compute_sum_us += took;
    g_win.compute_max_us = std::max(g_win.compute_max_us, took);
  }
  ReleaseSRWLockExclusive(&g_lock);
  log::write("tooltip: worker stopped");
  return 0;
}

// --- game thread

// Lists the inventory every kBagMs and reads up to kReadsPerTick new or changed items per tick into the pool.
void bag_tick(ULONGLONG now) {
  const std::shared_ptr<const Published> pub = published();
  if (!pub) return;  // nothing to score against yet
  if (now >= g_bag_next) {
    g_bag_next = now + kBagMs;
    const Take t = snapshot_bag(g_bag);
    if (t == Take::Faulted) return off("inventory listing faulted");
    if (t != Take::Ok) return;
    AcquireSRWLockExclusive(&g_lock);
    if (!g_player) reset_for(pub->probe.player);
    if (pub->probe.player != g_player) {  // another character's snapshot: the next hover resets (reset_for)
      ReleaseSRWLockExclusive(&g_lock);
      return;
    }
    g_bag_todo.clear();
    for (const BagItem& b : g_bag) {
      if (std::find(g_bag_skip.begin(), g_bag_skip.end(), b.id) != g_bag_skip.end()) continue;
      const auto it = std::find_if(g_pool.begin(), g_pool.end(), [&](const Cand& c) { return c.id == b.id; });
      if (it != g_pool.end() && it->fp == b.fingerprint) it->seen = std::max(it->seen, now - 1);
      else if (g_bag_todo.size() < kMaxBag) g_bag_todo.push_back(b);
    }
    ReleaseSRWLockExclusive(&g_lock);
  }
  for (std::size_t i = 0; i < kReadsPerTick && !g_bag_todo.empty(); ++i) {
    const BagItem b = g_bag_todo.back();
    g_bag_todo.pop_back();
    snap::Item item;
    int type = 0;
    const Take t = snapshot_candidate_id(b.id, item, type);
    if (t == Take::Faulted) return off("reading an inventory item faulted");
    AcquireSRWLockExclusive(&g_lock);
    if (t == Take::Ok && !tooltip::locations_for(type).empty())
      pool_put({b.id, b.fingerprint, type, std::make_shared<const snap::Item>(std::move(item)), now - 1});
    else if (g_bag_skip.size() < kMaxBag)
      g_bag_skip.push_back(b.id);
    ReleaseSRWLockExclusive(&g_lock);
  }
}

// Under g_lock, game thread: the presets the worker scores with, after a change.
void publish_presets() {
  auto p = std::make_shared<Presets>();
  p->version = g_preset_version;
  p->main = g_preset;
  for (const auto& n : g_named)
    if (n.preset) p->named.emplace_back(log::utf8(n.file), *n.preset);
  std::stable_sort(p->named.begin(), p->named.end(),
                   [](const auto& a, const auto& b) { return score::tried_first(a.second, b.second); });
  g_presets = std::move(p);
  ++g_gen;
  WakeConditionVariable(&g_cv);
}

// One preset file (at most kMaxPresetBytes); nullopt and `error` on failure.
std::optional<score::Preset> load_preset(const std::wstring& path, const WIN32_FILE_ATTRIBUTE_DATA& fa, std::string& error) {
  const u64 size = (static_cast<u64>(fa.nFileSizeHigh) << 32) | fa.nFileSizeLow;
  if (size > kMaxPresetBytes) {
    error = "file larger than 1 MB";
    return std::nullopt;
  }
  std::string text;
  HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                         OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (h == INVALID_HANDLE_VALUE) {
    error = "cannot open (" + std::to_string(GetLastError()) + ")";
    return std::nullopt;
  }
  text.resize(static_cast<std::size_t>(size));
  DWORD got = 0;
  const bool ok = ReadFile(h, text.data(), static_cast<DWORD>(text.size()), &got, nullptr) && got == text.size();
  if (!ok) error = "read failed (" + std::to_string(GetLastError()) + ")";
  CloseHandle(h);
  if (!ok) return std::nullopt;
  return score::parse(text, error);
}

// Under g_lock, game thread. Reloads the named presets when the folder's *.toml list or a file's
// modification time changed; a file that fails to load is skipped and named in the block.
void poll_named() {
  std::vector<std::pair<std::wstring, WIN32_FILE_ATTRIBUTE_DATA>> found;
  WIN32_FIND_DATAW fd{};
  HANDLE h = FindFirstFileW((g_named_dir + L"*.toml").c_str(), &fd);
  if (h != INVALID_HANDLE_VALUE) {
    do {
      if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
      WIN32_FILE_ATTRIBUTE_DATA fa{};
      fa.ftLastWriteTime = fd.ftLastWriteTime;
      fa.nFileSizeHigh = fd.nFileSizeHigh;
      fa.nFileSizeLow = fd.nFileSizeLow;
      found.emplace_back(fd.cFileName, fa);
    } while (found.size() < kMaxNamed && FindNextFileW(h, &fd));
    FindClose(h);
  }
  std::sort(found.begin(), found.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
  const bool same = found.size() == g_named.size() &&
                    std::equal(found.begin(), found.end(), g_named.begin(), [](const auto& f, const Named& n) {
                      return f.first == n.file && CompareFileTime(&f.second.ftLastWriteTime, &n.mtime) == 0;
                    });
  if (same) return;
  std::vector<Named> named;
  std::string failed;
  for (const auto& [file, fa] : found) {
    Named n{file, fa.ftLastWriteTime, std::nullopt};
    std::string error;
    n.preset = load_preset(g_named_dir + file, fa, error);
    if (n.preset) log::printf("tooltip: named preset %s loaded", log::utf8(file).c_str());
    else {
      log::printf("tooltip: named preset %s not loaded: %s", log::utf8(file).c_str(), error.c_str());
      if (failed.empty()) failed = log::utf8(file) + ": " + error;
    }
    named.push_back(std::move(n));
  }
  g_named = std::move(named);
  if (failed.empty()) g_named_error.clear();
  else utf16("GD Compare: preset GDCompare.presets\\" + failed + " (not used)", g_named_error);
  ++g_preset_version;
}

// Under g_lock, game thread. Re-reads the preset when its modification time changes; a missing file means
// the built-in defaults (equal to presets/default.toml, tests/test_score.cpp).
void poll_preset() {
  WIN32_FILE_ATTRIBUTE_DATA fa{};
  const bool exists = GetFileAttributesExW(g_preset_path.c_str(), GetFileExInfoStandard, &fa) != 0;
  if (!exists) {
    if (g_preset_seen) {
      g_preset_seen = false;
      g_preset = score::Preset{};
      g_preset_error.clear();
      ++g_preset_version;
      log::write("tooltip: preset file removed; built-in defaults");
    }
    return;
  }
  if (g_preset_seen && CompareFileTime(&fa.ftLastWriteTime, &g_preset_mtime) == 0) return;
  g_preset_seen = true;
  g_preset_mtime = fa.ftLastWriteTime;
  std::string error;
  std::optional<score::Preset> p = load_preset(g_preset_path, fa, error);
  if (p) {
    g_preset = std::move(*p);
    g_preset_error.clear();
    log::write("tooltip: preset loaded from GDCompare.preset.toml");
  } else {
    utf16("GD Compare: preset not loaded: " + error + " (last good preset in use)", g_preset_error);
    log::printf("tooltip: preset GDCompare.preset.toml not loaded: %s; last good preset in use", error.c_str());
  }
  ++g_preset_version;
}

// p-th percentile (0..1) of `v` (reordered); 0 when empty.
u32 percentile(std::vector<u32>& v, double p) {
  if (v.empty()) return 0;
  const auto i = static_cast<std::size_t>(std::min<double>(static_cast<double>(v.size()) - 1, p * static_cast<double>(v.size())));
  std::nth_element(v.begin(), v.begin() + static_cast<std::ptrdiff_t>(i), v.end());
  return v[i];
}

}  // namespace

bool tooltip_init(const Resolve& r, const std::wstring& mod_dir) {
  const std::wstring ini = mod_dir + L"GDCompare.ini";
  g_preset_path = mod_dir + L"GDCompare.preset.toml";
  g_named_dir = mod_dir + L"GDCompare.presets\\";
  g_debug = GetPrivateProfileIntW(L"tooltip", L"debug", 0, ini.c_str()) != 0;
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
    if (const auto t = scan::line_push_back_call({site, scan::kLinePushBackSiteLen}, addr(site));
        t && scan::is_line_push_back({reinterpret_cast<const std::byte*>(*t), scan::kLinePushBackLen})) {
      g_push = reinterpret_cast<PushFn>(*t);
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
  g_out.reserve(kMaxOut);
  g_pool.reserve(kPool);
  g_jobs.reserve(kMaxHover);
  g_log_out.reserve(kMaxLogOut);
  g_samples.reserve(kMaxSamples);
  g_stat_scratch.reserve(kMaxSamples);
  g_bag.reserve(kMaxBag);
  g_bag_todo.reserve(kMaxBag);
  g_bag_skip.reserve(kMaxBag);
  log::printf("tooltip: push_back at %s (verified %zu bytes), search-text caller %s, debug line %s "
              "(GDCompare.ini [tooltip] debug)",
              where(reinterpret_cast<std::uintptr_t>(g_push)).c_str(), scan::kLinePushBackLen, where(addr(g_search_ret)).c_str(),
              g_debug ? "on" : "off");
  // The game root holds database/database.arz; the mod sits in its compat\ or x64\ folder.
  g_game_dir = std::filesystem::path(mod_dir).parent_path().parent_path();
  g_win_start = GetTickCount64();
  AcquireSRWLockExclusive(&g_lock);
  publish_presets();  // built-in defaults until the first poll
  ReleaseSRWLockExclusive(&g_lock);
  g_on = true;
  HANDLE w = CreateThread(nullptr, 0, worker, nullptr, 0, nullptr);
  if (!w) {
    g_on = false;
    log::printf("tooltip: worker thread not started (%lu); tooltip block off", GetLastError());
    return false;
  }
  SetThreadPriority(w, THREAD_PRIORITY_BELOW_NORMAL);  // background scoring yields to the game's threads
  CloseHandle(w);
  if (HANDLE h = CreateThread(nullptr, 0, load_db, nullptr, 0, nullptr)) CloseHandle(h);
  else log::printf("tooltip: database thread not started (%lu); skill grants and max resist unchecked", GetLastError());
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
    // Inventory: records only (target_sets "(in bag)"). Only manual snapshots read it; an auto snapshot keeps
    // the last manual one's list (the writer thread is the only one that replaces g_pub).
    // shortcut: an empty inventory also keeps the old list (no "inventory read" flag in the snapshot) —
    // revisit when the snapshot records whether the inventory section ran.
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
    // Warm the database cache here: each equipped item swapped into its own slot decodes what a block needs
    // (the items' records and the skills they touch).
    if (const arz::Database* db = g_db.load()) {
      for (const auto& it : p->snap.equipped) (void)model::swap(p->snap, it, it.location, db);
      const score::Preset defaults;  // the records touched are the same for every preset
      (void)score::compare(p->snap, p->snap, score::main_skills(p->snap, defaults, db), defaults, db);
    }
    AcquireSRWLockExclusive(&g_pub_lock);
    old = std::exchange(g_pub, std::move(p));
    ReleaseSRWLockExclusive(&g_pub_lock);
  });
  changed();  // the worker scores the pool against it
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
    std::string file = "frozen-";
    for (const char c : pub->snap.character_name)
      file += std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_' ? c : '_';
    if (file == "frozen-") file += "character";
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
  const bool shift = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
  if (shift != g_shift_down) {
    g_shift_down = shift;
    AcquireSRWLockExclusive(&g_lock);
    const ULONGLONG since = now - g_last_build_ms;
    if (g_last_build_ms && since < 10000) {
      g_shift_edge_ms = now;
      g_after_shift = true;  // the next build within kShiftWindowMs is logged regardless of the rate limit
      if (g_shift_logs < kShiftLogs) {
        ++g_shift_logs;
        log::printf("tooltip: Shift %s; last build %llu ms ago (item %u)", shift ? "down" : "up",
                    static_cast<unsigned long long>(since), g_last_build_id);
      }
    }
    ReleaseSRWLockExclusive(&g_lock);
  }
  if (now - g_preset_poll_ms >= kPresetPollMs) {
    g_preset_poll_ms = now;
    AcquireSRWLockExclusive(&g_lock);
    const u64 version = g_preset_version;
    if (g_on) guarded("preset", poll_preset);
    if (g_on) guarded("named presets", poll_named);
    if (g_on && g_preset_version != version) guarded("presets", publish_presets);
    ReleaseSRWLockExclusive(&g_lock);
  }
  if (g_on) guarded("inventory", [&] { bag_tick(now); });
  if (now - g_win_start < kStatsMs) return;
  AcquireSRWLockExclusive(&g_lock);
  const Window w = std::exchange(g_win, Window{});
  g_stat_scratch.assign(g_samples.begin(), g_samples.end());  // within the reserve
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

void tooltip_after_build(const void* item, void* lines, const void* caller) {
  if (!g_on || !item || !lines) return;
  LARGE_INTEGER t0, t1;
  QueryPerformanceCounter(&t0);
  const DWORD tid = GetCurrentThreadId();
  const bool game_thread = tid == g_game_tid.load();
  AcquireSRWLockExclusive(&g_lock);
  Result r;
  guarded("build", [&] {
    r = plan(item, lines, caller, game_thread);
    if (g_on && !g_out.empty()) append(lines, r);
  });
  QueryPerformanceCounter(&t1);
  const long long took = us(t0, t1);
  const ULONGLONG now = GetTickCount64();
  ++g_calls_total;
  ++g_win.calls;
  g_win.lines += r.lines;
  if (g_samples.size() < kMaxSamples) g_samples.push_back(static_cast<u32>(std::min<long long>(took, UINT32_MAX)));
  if (r.what[0] == 's') ++g_win.skipped;
  else if (r.how == How::Hit) ++g_win.hits;
  else if (r.how == How::Pending) ++g_win.pending;
  else ++g_win.misses;
  const bool after_shift = g_after_shift && now - g_shift_edge_ms < kShiftWindowMs;
  if (r.what[0] != 's') {
    g_last_build_ms = now;
    g_last_build_id = r.id;
    if (after_shift) g_after_shift = false;
  }
  const bool first = g_calls_total <= kFirstLogs;
  if (first || after_shift || r.how == How::Pending || now - g_last_log_ms >= 1000) {
    g_last_log_ms = now;
    log_call(r, caller, tid, took, g_calls_total <= kTraceLogs);
  }
  ReleaseSRWLockExclusive(&g_lock);
}

}  // namespace gdc::adapter
