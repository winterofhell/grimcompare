// Detours (MinHook 1.3.4, third_party/minhook):
//   GameEngine::Update(int)              game-thread tick: calls the original first, then polls the
//                                        snapshot key and the auto-snapshot probe; a capture copies the
//                                        engine numbers into a snap::Snapshot and queues it.
//   ItemEquipment/ItemArtifact::GetUIDisplayText
//                                        records the id of the item whose tooltip is built, calls the
//                                        original, then appends the tooltip block (tooltip.h).
// Writer thread: serializes queued snapshots, writes the F9 file and the auto-snapshot store (gdc::store),
// then checks each new record against the previous one (calibration.h).
// Nothing here writes game memory. A faulting snapshot turns snapshots off for the session; any probe or
// writer failure turns auto snapshots off.
#include "hooks.h"

#include <windows.h>
#include <intrin.h>

#include <MinHook.h>

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <deque>
#include <filesystem>
#include <memory>
#include <vector>

#include "calibration.h"
#include "gdc/store.h"
#include "log.h"
#include "seh.h"
#include "tooltip.h"

namespace gdc::adapter {
namespace {

using u32 = std::uint32_t;
using UpdateFn = void (*)(void*, int);
using TooltipFn = void (*)(const void*, const void*, void*, bool);

constexpr std::size_t kMaxTooltipItems = 8;

HookConfig g_cfg;
int g_vk = VK_F9;
// Freeze key (GDCompare.ini [freeze] key, 0 = off). F10 by default: the game binds F1..F7 only
// (Documents/My Games/Grim Dawn/Settings/keybindings.txt, DirectInput codes 59..65 on 2026-11-14), Steam takes
// F12, KDE's default global shortcuts take Ctrl+F1..F12 (Ctrl+F9 was the old freeze chord) and Alt+F-keys.
int g_freeze_vk = VK_F10;
UpdateFn g_update = nullptr;
TooltipFn g_tip_equipment = nullptr, g_tip_artifact = nullptr;
std::atomic<bool> g_snapshots{false};
std::atomic<std::uint64_t> g_tick{0};
bool g_was_down = false;  // game thread only
bool g_freeze_was_down = false;  // game thread only

SRWLOCK g_tip_lock = SRWLOCK_INIT;  // no exceptions, unlike std::mutex
std::vector<u32> g_tip_ids;  // reserved at install; ids of the latest tick that built tooltips
std::vector<u32> g_tips;     // game thread: the capture's copy of g_tip_ids, reserved at install
std::uint64_t g_tip_tick = ~0ull;
ULONGLONG g_tip_ms = 0;

void latch(const void* item) noexcept {
  const u32 id = object_id(item);
  if (!id) return;
  AcquireSRWLockExclusive(&g_tip_lock);
  const std::uint64_t tick = g_tick.load();
  if (tick != g_tip_tick) {
    g_tip_ids.clear();
    g_tip_tick = tick;
  }
  if (g_tip_ids.size() < kMaxTooltipItems && std::find(g_tip_ids.begin(), g_tip_ids.end(), id) == g_tip_ids.end())
    g_tip_ids.push_back(id);  // within reserved capacity: no allocation
  g_tip_ms = GetTickCount64();
  ReleaseSRWLockExclusive(&g_tip_lock);
}

void tip_equipment(const void* self, const void* ch, void* lines, bool b) {
  latch(self);
  g_tip_equipment(self, ch, lines, b);
  tooltip_after_build(self, lines, _ReturnAddress());
}

void tip_artifact(const void* self, const void* ch, void* lines, bool b) {
  latch(self);
  g_tip_artifact(self, ch, lines, b);
  tooltip_after_build(self, lines, _ReturnAddress());
}

bool foreground_is_ours() {
  DWORD pid = 0;
  GetWindowThreadProcessId(GetForegroundWindow(), &pid);
  return pid == GetCurrentProcessId();
}

// UTC now, ISO 8601, into `out` (assign keeps its buffer).
void utc_iso(std::string& out) {
  SYSTEMTIME t;
  GetSystemTime(&t);
  char buf[64];
  std::snprintf(buf, sizeof buf, "%04u-%02u-%02uT%02u:%02u:%02u.%03uZ", unsigned{t.wYear}, unsigned{t.wMonth},
                unsigned{t.wDay}, unsigned{t.wHour}, unsigned{t.wMinute}, unsigned{t.wSecond},
                unsigned{t.wMilliseconds});
  out.assign(buf);
}

bool write_file(const std::wstring& path, const std::string& text) {
  const std::wstring tmp = path + L".tmp";
  HANDLE f = CreateFileW(tmp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (f == INVALID_HANDLE_VALUE) return false;
  DWORD written = 0;
  const bool ok = WriteFile(f, text.data(), static_cast<DWORD>(text.size()), &written, nullptr) && written == text.size();
  CloseHandle(f);
  if (!ok || !MoveFileExW(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING)) {
    DeleteFileW(tmp.c_str());
    return false;
  }
  return true;
}

// --- writer thread

// Jobs are recycled: the writer resets a finished job (snap::recycle) and returns it to g_free, so the
// game thread refills strings and vectors that already have their capacity.
struct Job {
  snap::Snapshot snap;
  snap::Spares spare;     // snap's recycled skills / items / item skill modifiers
  unsigned triggers = 0;  // store::Trigger bits
  bool manual = false;    // F9: also the pretty snapshots\snapshot-*.json file
  long long game_us = 0;  // game-thread capture time
  Probe probe;            // state at capture: the tooltip block uses the snapshot while the live probe equals it
  Timing timing;          // per section
};

constexpr std::size_t kMaxJobs = 8;
SRWLOCK g_job_lock = SRWLOCK_INIT;
CONDITION_VARIABLE g_job_cv = CONDITION_VARIABLE_INIT;
std::deque<std::unique_ptr<Job>> g_jobs;
std::vector<std::unique_ptr<Job>> g_free;  // reserved kMaxJobs at install; recycled jobs
std::atomic<bool> g_auto{false};
std::unique_ptr<store::Store> g_store;  // writer thread only after install

std::string sections(const Timing& t) {
  std::string out;
  char buf[64];
  for (int i = 0; i < t.count; ++i) {
    std::snprintf(buf, sizeof buf, "%s%s %lld", i ? ", " : "", t.sections[i].name, t.sections[i].us);
    out += buf;
  }
  return out;
}

// Resets the job and keeps it for the next capture (or frees it if enough are kept). Any thread.
void release(std::unique_ptr<Job> j) {
  snap::recycle(j->snap, j->spare);
  AcquireSRWLockExclusive(&g_job_lock);
  if (g_free.size() < kMaxJobs) g_free.push_back(std::move(j));  // within the reserve: no allocation
  ReleaseSRWLockExclusive(&g_job_lock);
}

std::unique_ptr<Job> acquire() {
  std::unique_ptr<Job> j;
  AcquireSRWLockExclusive(&g_job_lock);
  if (!g_free.empty()) {
    j = std::move(g_free.back());
    g_free.pop_back();
  }
  ReleaseSRWLockExclusive(&g_job_lock);
  return j ? std::move(j) : std::make_unique<Job>();
}

void process(Job& j) {
  const auto t0 = GetTickCount64();
  if (j.manual) {
    log::printf("snapshot key: game thread %lld us (sections us: %s)", j.game_us, sections(j.timing).c_str());
    const std::wstring dir = g_cfg.mod_dir + L"snapshots";
    CreateDirectoryW(dir.c_str(), nullptr);
    // snapshot-YYYYMMDD-HHMMSS-mmm.json from taken_at
    std::string d;
    for (char ch : j.snap.taken_at)
      if (ch >= '0' && ch <= '9') d.push_back(ch);
    d.resize(17, '0');
    const std::string name = "snapshot-" + d.substr(0, 8) + "-" + d.substr(8, 6) + "-" + d.substr(14, 3) + ".json";
    const std::wstring path = dir + L"\\" + std::wstring(name.begin(), name.end());
    if (write_file(path, snap::to_json(j.snap)))
      log::printf("snapshot written: snapshots\\%s (level %u, %zu equipped, %zu skills, %zu tooltip items, %s)",
                  name.c_str(), j.snap.level, j.snap.equipped.size(), j.snap.skills.size(), j.snap.tooltip_items.size(),
                  j.snap.complete ? "complete" : "PARTIAL");
    else
      log::printf("snapshot: writing %s failed (%lu)", name.c_str(), GetLastError());
  }
  if (!g_auto || !g_store) return;
  const auto w = g_store->write(j.snap, j.triggers);
  std::string tag;
  for (const auto& t : store::tags(j.triggers)) tag += (tag.empty() ? "" : ",") + t;
  log::printf("auto snapshot (%s): game thread %lld us; %s %s, %llu bytes, writer %llu ms%s", tag.c_str(), j.game_us,
              w.full ? "full" : "delta", w.file.filename().string().c_str(), static_cast<unsigned long long>(w.bytes),
              static_cast<unsigned long long>(GetTickCount64() - t0), j.snap.complete ? "" : " (PARTIAL)");
  log::printf("  sections us: %s", sections(j.timing).c_str());
  for (const auto& f : w.deleted) log::printf("auto snapshot: rotated out %s", f.filename().string().c_str());
  calibration_record(j.snap, w.full, w.file.stem().stem().string(), tooltip_database());  // never throws
}

void process_guarded(Job& j) {
  auto body = [&] {
    try {
      process(j);
    } catch (const std::exception& e) {
      g_auto = false;
      log::printf("auto snapshot: writer error: %s; auto snapshots off", e.what());
    } catch (...) {
      g_auto = false;
      log::write("auto snapshot: unknown writer error; auto snapshots off");
    }
  };
  seh::Fault f;
  if (!seh::run(body, f)) {
    g_auto = false;
    log::printf("auto snapshot: writer FAULT 0x%08lX at 0x%016llX; auto snapshots off", f.code,
                static_cast<unsigned long long>(f.addr));
  }
}

DWORD WINAPI writer(void*) {
  for (;;) {
    std::unique_ptr<Job> j;
    AcquireSRWLockExclusive(&g_job_lock);
    while (g_jobs.empty()) SleepConditionVariableSRW(&g_job_cv, &g_job_lock, INFINITE, 0);
    j = std::move(g_jobs.front());
    g_jobs.pop_front();
    ReleaseSRWLockExclusive(&g_job_lock);
    process_guarded(*j);
    if (j->snap.complete) tooltip_publish(j->snap, j->probe);  // guarded inside
    try {
      release(std::move(j));
    } catch (...) {  // bad_alloc growing the spares: the job is dropped, the next capture makes a new one
    }
  }
}

void enqueue(std::unique_ptr<Job> j) {
  bool queued = false;
  AcquireSRWLockExclusive(&g_job_lock);
  if (g_jobs.size() < kMaxJobs) {
    g_jobs.push_back(std::move(j));
    queued = true;
  }
  ReleaseSRWLockExclusive(&g_job_lock);
  if (queued) WakeConditionVariable(&g_job_cv);
  else log::write("snapshot: writer queue full, snapshot dropped");
}

// --- game thread

// Copies the engine numbers into a Job; serialization and disk I/O happen on the writer thread.
void capture(unsigned triggers, bool manual) {
  LARGE_INTEGER t0, t1;
  QueryPerformanceCounter(&t0);
  std::vector<u32>& tips = g_tips;
  tips.clear();
  ULONGLONG tip_ms = 0;
  if (manual) {
    AcquireSRWLockShared(&g_tip_lock);
    tips.assign(g_tip_ids.begin(), g_tip_ids.end());  // fits the reserve: no allocation under the lock
    tip_ms = g_tip_ms;
    ReleaseSRWLockShared(&g_tip_lock);
  }
  std::unique_ptr<Job> j = acquire();
  j->triggers = triggers;
  j->manual = manual;
  snap::Snapshot& s = j->snap;
  s.mod_version = g_cfg.mod_version;
  s.game_build = g_cfg.game_build;
  utc_iso(s.taken_at);
  const Take r = snapshot_take(s, j->spare, tips, manual, j->timing);
  QueryPerformanceCounter(&t1);
  j->game_us = log::elapsed_us(t0, t1);
  if (r != Take::NoPlayer && snapshot_probe(j->probe) != Take::Ok) j->probe = Probe{};  // never matches
  if (r == Take::NoPlayer) {
    if (manual) log::write("snapshot key: no main player (menu or loading), nothing written");
    release(std::move(j));
    return;
  }
  if (!tips.empty()) s.tooltip_age_ms = static_cast<u32>(std::min<ULONGLONG>(GetTickCount64() - tip_ms, UINT32_MAX));
  enqueue(std::move(j));
  if (r == Take::Faulted) {
    g_snapshots = false;
    log::write("snapshot: a section faulted; snapshots are off until the game restarts");
  }
}

// Auto snapshots: probe at most every kProbeMs; a change starts (or extends) a debounce window, and one
// capture follows when the state has been quiet for g_debounce_ms. A burst that ends where the last
// capture was (an item picked up and put back) captures nothing.
constexpr ULONGLONG kProbeMs = 100;
ULONGLONG g_debounce_ms = 2000;
Probe g_seen, g_captured;  // last probe; state at the last capture
unsigned g_pending = 0;    // trigger bits since the last capture
ULONGLONG g_quiet_at = 0, g_next_probe = 0;

unsigned changes(const Probe& a, const Probe& b) {
  if (a.player != b.player) return store::kLoad;
  unsigned t = 0;
  if (a.difficulty != b.difficulty) t |= store::kDifficulty;
  if (a.equip_hash != b.equip_hash) t |= store::kEquip;
  if (a.level != b.level) return t | store::kLevel;  // a level-up also grants points
  if (a.skill_points != b.skill_points) t |= store::kSkill;
  if (a.attribute_points != b.attribute_points) t |= store::kAttribute;
  if (a.devotion_points != b.devotion_points) t |= store::kDevotion;
  return t;
}

void auto_tick() {
  const ULONGLONG now = GetTickCount64();
  if (now < g_next_probe) return;
  g_next_probe = now + kProbeMs;
  Probe p;
  const Take r = snapshot_probe(p);
  if (r == Take::Faulted) {
    g_auto = false;
    log::write("auto snapshot: probe faulted; auto snapshots off");
    return;
  }
  if (const unsigned t = changes(g_seen, p)) {
    g_pending |= t;
    g_quiet_at = now + g_debounce_ms;
  }
  g_seen = p;
  if (!g_pending || now < g_quiet_at || r == Take::NoPlayer) return;
  const unsigned t = g_pending;
  g_pending = 0;
  if (p == g_captured) return;
  g_captured = p;
  capture(t, false);
}

void update(void* self, int arg) {
  g_update(self, arg);
  ++g_tick;
  tooltip_tick();
  if (!g_snapshots) return;
  try {
    const bool down = (GetAsyncKeyState(g_vk) & 0x8000) != 0 && foreground_is_ours();
    if (down && !g_was_down) capture(store::kManual, true);
    g_was_down = down;
    const bool freeze = g_freeze_vk != 0 && (GetAsyncKeyState(g_freeze_vk) & 0x8000) != 0 && foreground_is_ours();
    if (freeze && !g_freeze_was_down) tooltip_freeze();
    g_freeze_was_down = freeze;
    if (g_auto) auto_tick();
  } catch (const std::exception& e) {
    g_snapshots = false;
    log::printf("snapshot: C++ exception %s; snapshots off", e.what());
  } catch (...) {
    g_snapshots = false;
    log::write("snapshot: unknown C++ exception; snapshots off");
  }
}

bool create(void* target, void* detour, void** original, const char* label) {
  const MH_STATUS st = MH_CreateHook(target, detour, original);
  if (st != MH_OK) log::printf("hook %s: MH_CreateHook failed: %s", label, MH_StatusToString(st));
  return st == MH_OK;
}

}  // namespace

bool hooks_install(const Resolve& r, const HookConfig& cfg) {
  g_cfg = cfg;
  const std::wstring ini = cfg.mod_dir + L"GDCompare.ini";
  const UINT vk = GetPrivateProfileIntW(L"snapshot", L"key", VK_F9, ini.c_str());
  if (vk >= 1 && vk <= 254) {
    g_vk = static_cast<int>(vk);
  } else {
    log::printf("GDCompare.ini [snapshot] key=%u is not a virtual-key code 1..254; using F9 (120)", vk);
    g_vk = VK_F9;
  }
  const UINT fk = GetPrivateProfileIntW(L"freeze", L"key", VK_F10, ini.c_str());
  if (fk > 254) {
    log::printf("GDCompare.ini [freeze] key=%u is not a virtual-key code 1..254 (0 = off); using F10 (121)", fk);
    g_freeze_vk = VK_F10;
  } else if (static_cast<int>(fk) == g_vk && fk != 0) {
    log::printf("GDCompare.ini [freeze] key=%u is the snapshot key; freeze off", fk);
    g_freeze_vk = 0;
  } else {
    g_freeze_vk = static_cast<int>(fk);
  }
  g_tip_ids.reserve(kMaxTooltipItems);
  g_tips.reserve(kMaxTooltipItems);
  g_free.reserve(kMaxJobs);
  const bool want_auto = GetPrivateProfileIntW(L"snapshot", L"auto", 1, ini.c_str()) != 0;
  const UINT cap_mb = GetPrivateProfileIntW(L"snapshot", L"auto_cap_mb", 50, ini.c_str());
  g_debounce_ms = std::clamp<UINT>(GetPrivateProfileIntW(L"snapshot", L"auto_debounce_ms", 2000, ini.c_str()), 200, 60000);
  if (want_auto) {
    try {
      g_store = std::make_unique<store::Store>(std::filesystem::path(cfg.mod_dir + L"snapshots"),
                                               std::uint64_t{std::clamp<UINT>(cap_mb, 1, 4096)} << 20);
    } catch (const std::exception& e) {
      log::printf("auto snapshot: store init failed: %s", e.what());
    }
  }
  if (g_store) calibration_init(cfg.mod_dir, cfg.mod_version.c_str());
  HANDLE wt = CreateThread(nullptr, 0, writer, nullptr, 0, nullptr);
  if (!wt) {
    log::printf("hooks: writer thread not started (%lu)", GetLastError());
    return false;
  }
  CloseHandle(wt);

  void* update_target = const_cast<void*>(r("GameEngine::Update"));
  void* eq_target = const_cast<void*>(r("ItemEquipment::GetUIDisplayText"));
  void* art_target = const_cast<void*>(r("ItemArtifact::GetUIDisplayText"));
  if (!update_target || !eq_target || !art_target) {
    log::write("hooks: a detour target is unresolved");
    return false;
  }
  MH_STATUS st = MH_Initialize();
  if (st != MH_OK) {
    log::printf("hooks: MH_Initialize failed: %s", MH_StatusToString(st));
    return false;
  }
  if (!create(update_target, reinterpret_cast<void*>(&update), reinterpret_cast<void**>(&g_update), "GameEngine::Update") ||
      !create(eq_target, reinterpret_cast<void*>(&tip_equipment), reinterpret_cast<void**>(&g_tip_equipment),
              "ItemEquipment::GetUIDisplayText") ||
      !create(art_target, reinterpret_cast<void*>(&tip_artifact), reinterpret_cast<void**>(&g_tip_artifact),
              "ItemArtifact::GetUIDisplayText")) {
    MH_Uninitialize();
    return false;
  }
  // The block is optional: if its engine functions are not verified the tooltip stays the game's own.
  if (!tooltip_init(r, cfg.mod_dir)) log::write("tooltip: block off");
  g_auto = want_auto && g_store != nullptr;
  g_snapshots = true;  // before enabling: the first hooked Update may run immediately
  st = MH_EnableHook(MH_ALL_HOOKS);
  if (st != MH_OK) {
    g_snapshots = false;
    log::printf("hooks: MH_EnableHook failed: %s", MH_StatusToString(st));
    MH_Uninitialize();
    return false;
  }
  log::printf("hooks installed; snapshot key = virtual-key %d (F9 = 120), GDCompare.ini [snapshot] key", g_vk);
  log::printf("freeze key = virtual-key %d (F10 = 121, 0 = off), GDCompare.ini [freeze] key", g_freeze_vk);
  log::printf("auto snapshots %s (debounce %llu ms, cap %u MB; [snapshot] auto, auto_debounce_ms, auto_cap_mb)",
              g_auto ? "on" : "off", static_cast<unsigned long long>(g_debounce_ms), cap_mb);
  return true;
}

}  // namespace gdc::adapter
