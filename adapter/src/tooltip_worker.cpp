// Cache, candidate pool and the scoring thread.
#include <algorithm>
#include <utility>

#include "calibration.h"
#include "tooltip_internal.h"

namespace gdc::adapter::tip {
namespace {

Entry* lru() {
  Entry* e = &g_cache[0];
  for (auto& x : g_cache)
    if (x.used < e->used) e = &x;
  return e;
}

struct Computed {
  bool equippable = false;
  long long us = 0;
  std::vector<u32> classes;
  std::vector<std::u16string> text;
  std::vector<std::string> log;
};

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
    std::vector<tooltip::Line> lines =
        tooltip::compare(pub.snap, *c.item, c.type, shift, db, preset, labels, &out.log, nullptr, ps.screen);
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

// Precompute order: pooled items newest first, then again with Shift.
struct PoolPass {
  std::vector<Job> tasks;
  std::size_t pos = 0;
  u64 gen = 0;
};

void restart(PoolPass& pass) {
  pass.gen = g_gen;
  pass.tasks.clear();
  std::vector<const Cand*> order;
  order.reserve(g_pool.size());
  for (const Cand& c : g_pool) order.push_back(&c);
  std::sort(order.begin(), order.end(), [](const Cand* a, const Cand* b) { return a->seen > b->seen; });
  for (const bool shift : {false, true})
    for (const Cand* c : order) pass.tasks.push_back({*c, shift});
  pass.pos = 0;
}

// Hovered items first, then the pool. False: nothing to score.
bool next_task(PoolPass& pass, Job& task, bool& hover) {
  if (!g_jobs.empty()) {
    task = {g_jobs.front().cand, g_jobs.front().shift};
    g_jobs.erase(g_jobs.begin());
    hover = true;
    return true;
  }
  if (pass.gen != g_gen) restart(pass);
  if (pass.pos >= pass.tasks.size()) return false;
  task = std::move(pass.tasks[pass.pos++]);
  return true;
}

// Called under g_lock; writes with the lock released.
void write_logs(std::vector<std::string>& logs) {
  logs.swap(g_log_out);
  ReleaseSRWLockExclusive(&g_lock);
  for (const auto& l : logs) log::write(l);
  logs.clear();
  AcquireSRWLockExclusive(&g_lock);
}

// Hover scores log now; precomputed ones when first shown.
void log_score(const Job& task, bool hover, Computed& out) {
  if (hover) {
    log::printf("tooltip score: item %u%s, %lld us on the worker", task.cand.id, task.shift ? " (Shift)" : "", out.us);
    for (const auto& l : out.log) log::printf("  %s", l.c_str());
    out.log.clear();
    return;
  }
  out.log.insert(out.log.begin(), "tooltip score: item " + std::to_string(task.cand.id) +
                                      (task.shift ? " (Shift)" : "") + ", " + std::to_string(out.us) +
                                      " us on the worker (precomputed)");
  for (std::size_t i = 1; i < out.log.size(); ++i) out.log[i].insert(0, "  ");
}

void record(const Key& k, Computed&& out, bool hover) {
  const long long took = out.us;
  store(k, std::move(out), hover);
  ++(hover ? g_win.computed_hover : g_win.computed_pool);
  g_win.compute_sum_us += took;
  g_win.compute_max_us = std::max(g_win.compute_max_us, took);
}

}  // namespace

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

void pool_put(Cand c) {
  auto it = std::find_if(g_pool.begin(), g_pool.end(), [&](const Cand& x) { return x.id == c.id; });
  if (it == g_pool.end() && g_pool.size() < kPool) it = g_pool.insert(g_pool.end(), Cand{});
  else if (it == g_pool.end())
    it = std::min_element(g_pool.begin(), g_pool.end(), [](const Cand& a, const Cand& b) { return a.seen < b.seen; });
  *it = std::move(c);
  ++g_gen;
  WakeConditionVariable(&g_cv);
}

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

// The game thread never waits on the log file.
void queue_log(std::string line) {
  if (g_log_out.size() < kMaxLogOut) g_log_out.push_back(std::move(line));
  WakeConditionVariable(&g_cv);
}

void queue_log(std::vector<std::string>& lines) {
  for (auto& l : lines) queue_log(std::move(l));
  lines.clear();
}

DWORD WINAPI worker(void*) {
  PoolPass pass;
  std::vector<std::string> logs;
  AcquireSRWLockExclusive(&g_lock);
  for (;;) {
    while (g_on && g_log_out.empty() && g_jobs.empty() && g_gen == pass.gen && pass.pos >= pass.tasks.size())
      SleepConditionVariableSRW(&g_cv, &g_lock, INFINITE, 0);
    if (!g_on) break;
    if (!g_log_out.empty()) {
      write_logs(logs);
      continue;
    }
    const std::shared_ptr<const Published> pub = published();
    const std::shared_ptr<const Presets> ps = g_presets;
    const arz::Database* db = g_db.load();
    if (!pub || !ps) {
      g_jobs.clear();
      pass.gen = g_gen;
      pass.pos = pass.tasks.size();
      continue;
    }
    const int data = data_level(db);
    Job task;
    bool hover = false;
    if (!next_task(pass, task, hover)) continue;
    const Key k{task.cand.id, task.cand.fp, pub->seq, ps->version, data, task.shift};
    if (const Entry* e = find(k); e && e->ready) continue;
    ReleaseSRWLockExclusive(&g_lock);
    Computed out;
    const bool ok = compute(*pub, *ps, task.cand, task.shift, db, out);
    if (ok) log_score(task, hover, out);
    AcquireSRWLockExclusive(&g_lock);
    if (!ok) break;
    record(k, std::move(out), hover);
  }
  ReleaseSRWLockExclusive(&g_lock);
  log::write("tooltip: worker stopped");
  return 0;
}

// Reads a few inventory items per tick into the pool.
void bag_tick(ULONGLONG now) {
  const std::shared_ptr<const Published> pub = published();
  if (!pub) return;
  if (now >= g_bag_next) {
    g_bag_next = now + kBagMs;
    const Take t = snapshot_bag(g_bag);
    if (t == Take::Faulted) return off("inventory listing faulted");
    if (t != Take::Ok) return;
    AcquireSRWLockExclusive(&g_lock);
    if (!g_player) reset_for(pub->probe.player);
    if (pub->probe.player != g_player) {  // another character: reset_for on the next hover
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

}  // namespace gdc::adapter::tip
