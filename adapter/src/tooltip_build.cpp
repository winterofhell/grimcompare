// Game thread: adds the cached block to a tooltip.
#include <algorithm>
#include <utility>

#include "tooltip.h"
#include "tooltip_internal.h"

namespace gdc::adapter::tip {
namespace {

const std::u16string kOk = u"GDC ok";
const std::u16string kOffThread = u"GrimCompare: tooltip built off the game thread";
const std::u16string kWaitingNone = u"GrimCompare: waiting for a character snapshot...";
const std::u16string kWaitingGear = u"GrimCompare: waiting for a snapshot of the current gear...";
const std::u16string kEquipped = u"GrimCompare: equipped";
const std::u16string kComputing = u"GrimCompare: computing... (hover again)";

enum class How { None, Hit, Miss, Pending };

struct Result {
  const char* what = "appended";
  std::size_t lines = 0;
  How how = How::None;
  u32 id = 0;
  int type = -1;
  long long read_us = 0, model_us = 0;
};

bool valid_vector(const void* v) {
  const auto b = rd<std::uintptr_t>(v, 0), e = rd<std::uintptr_t>(v, 8), c = rd<std::uintptr_t>(v, 16);
  if (!b) return !e && !c;
  return b <= e && e <= c && (e - b) % kLine == 0 && (c - b) % kLine == 0 && (e - b) / kLine <= kMaxLines;
}

void serve_hit(Entry& e, Result& r) {
  e.used = ++g_use;
  r.how = e.ready ? How::Hit : How::Pending;
  r.type = -1;
  if (e.ready && !e.logged) {
    e.logged = true;
    queue_log(e.log);
  }
}

// Reads the hovered item and queues it. False: r final.
bool queue_miss(const void* item, const Key& k, const Published* pub, bool current, Entry*& e, Result& r) {
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
    return false;
  }
  if (ct != Take::Ok) return false;
  r.type = type;
  r.how = How::Miss;
  const bool equippable = !tooltip::locations_for(type).empty();
  if (!equippable) return false;
  if (!current) {  // wait for a snapshot of the gear worn now
    g_out.push_back({kGray, pub ? &kWaitingGear : &kWaitingNone});
    pool_put({r.id, k.fp, type, std::make_shared<const snap::Item>(std::move(cand)), GetTickCount64()});
    return false;
  }
  const bool worn = std::any_of(pub->snap.equipped.begin(), pub->snap.equipped.end(),
                                [&](const snap::Item& x) { return x.id == r.id; });
  if (worn) {
    e = claim(k, true);
    e->equippable = true;
    return true;
  }
  Cand c{r.id, k.fp, type, std::make_shared<const snap::Item>(std::move(cand)), GetTickCount64()};
  if (g_jobs.size() < kMaxHover) {
    claim(k, false);
    g_jobs.push_back({c, k.shift});
  } else {
    ++g_win.queue_full;  // the next hover queues it again
  }
  pool_put(std::move(c));
  r.how = How::Pending;
  return true;
}

// Preset errors, then the cached block or a status line.
void block_lines(const Entry* e, Result& r) {
  if (!g_preset_error.empty()) g_out.push_back({kClass[static_cast<std::size_t>(tooltip::Tone::Warning)], &g_preset_error});
  if (!g_named_error.empty()) g_out.push_back({kClass[static_cast<std::size_t>(tooltip::Tone::Warning)], &g_named_error});
  if (r.how == How::Pending) {
    g_out.push_back({kGray, &kComputing});
    return;
  }
  if (e->text.empty() && e->equippable) g_out.push_back({kGray, &kEquipped});
  r.model_us = e->model_us;
  for (std::size_t i = 0; i < e->text.size() && g_out.size() < kMaxOut; ++i) g_out.push_back({e->classes[i], &e->text[i]});
}

// Fills g_out for this tooltip; never scores.
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
  if (e) serve_hit(*e, r);
  else if (!queue_miss(item, k, pub.get(), current, e, r)) return r;
  if (!g_on) {
    g_out.clear();
    r.what = "dropped: fault";
    return r;
  }
  block_lines(e, r);
  return r;
}

// The game copies our text and frees its own lines.
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

}  // namespace
}  // namespace gdc::adapter::tip

namespace gdc::adapter {

using namespace tip;

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
