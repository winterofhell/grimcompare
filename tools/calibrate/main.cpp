// Replays stored swaps; exit 0 pass, 1 fail, 2 error.
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <string>

#include "gdc/calibrate.h"
#include "gdc/score.h"
#include "gdc/store.h"

using namespace gdc;
namespace fs = std::filesystem;

namespace {

fs::path game_dir() {
  if (const char* d = std::getenv("GD_DIR")) return d;
  if (const char* h = std::getenv("HOME")) return fs::path(h) / ".local/share/Steam/steamapps/common/Grim Dawn";
  return {};
}

struct Totals {
  int checked = 0, failed = 0, skipped = 0;
  int req_checked = 0, req_failed = 0;  // schema 7
};

struct Ctx {
  const arz::Database* db = nullptr;
  score::Preset preset;
  bool verbose = false;
};

std::string pct(double ratio) {
  char buf[32];
  std::snprintf(buf, sizeof buf, "%+.2f%%", (ratio - 1) * 100);
  return buf;
}

void print_score(const snap::Snapshot& a, const snap::Snapshot& b, const Ctx& x) {
  const score::Stage* st = score::stage(x.preset, a);
  const score::Settings& set = st ? st->settings : x.preset;
  const auto r = score::compare(a, b, score::main_skills(a, set, x.db), set, x.db);
  std::cout << "    score " << (st ? "[stage " + st->name + "] " : "") << pct(1 + r.percent / 100) << " (Damage " << pct(r.ratio.damage) << ", Survival "
            << pct(r.ratio.survival) << ", Other " << pct(r.ratio.other) << ")\n";
  if (x.verbose) {
    std::cout << "      before: " << r.before << "\n      after:  " << r.after << "\n";
    for (const auto& n : r.notes) std::cout << "      score note: " << n << "\n";
  }
}

void report(const std::string& head, const snap::Snapshot& a, const snap::Snapshot& b, const calibrate::Check& c,
            const Ctx& x, Totals& t) {
  if (!c.skipped.empty()) {
    ++t.skipped;
    if (x.verbose) std::cout << head << ": skipped (" << c.skipped << ")\n";
    return;
  }
  ++t.checked;
  if (!c.diffs.empty()) ++t.failed;
  const auto lines = calibrate::report(c, a, b, head, x.verbose);
  std::cout << lines[0] << "\n";
  print_score(a, b, x);
  for (std::size_t i = 1; i < lines.size(); ++i) std::cout << lines[i] << "\n";
}

snap::Snapshot load_json(const char* path) {
  std::ifstream f(path, std::ios::binary);
  if (!f) throw std::runtime_error(std::string(path) + ": cannot open");
  return snap::from_json(std::string(std::istreambuf_iterator<char>(f), {}));
}

void calibrate_dir(const fs::path& char_dir, const Ctx& x, Totals& t) {
  std::cout << "== " << char_dir.filename().string() << "\n";
  std::unique_ptr<store::Record> prev;
  store::read(char_dir, [&](const store::Record& r) {
    const auto rq = calibrate::requirements(r.snapshot);
    t.req_checked += rq.checked;
    t.req_failed += static_cast<int>(rq.mismatches.size());
    for (const auto& m : rq.mismatches) std::cout << "REQ " << r.segment << " #" << r.seq << " " << m << "\n";
    if (prev && !r.full) {
      char head[64];
      std::snprintf(head, sizeof head, "%s #%04u->#%04u", r.segment.c_str(), prev->seq, r.seq);
      report(head, prev->snapshot, r.snapshot, calibrate::check(prev->snapshot, r.snapshot, x.db), x, t);
    }
    prev = std::make_unique<store::Record>(r);
  });
}

}  // namespace

int main(int argc, char** argv) {
  fs::path dir;
  std::string only;
  bool verbose = false;
  const char* pair[2] = {};
  const char* preset_file = nullptr;
  for (int i = 1; i < argc; ++i) {
    if (!std::strcmp(argv[i], "--verbose")) verbose = true;
    else if (!std::strcmp(argv[i], "--char") && i + 1 < argc) only = argv[++i];
    else if (!std::strcmp(argv[i], "--preset") && i + 1 < argc) preset_file = argv[++i];
    else if (!std::strcmp(argv[i], "--pair") && i + 2 < argc) pair[0] = argv[++i], pair[1] = argv[++i];
    else if (argv[i][0] != '-' && dir.empty()) dir = argv[i];
    else {
      std::cerr << "usage: gdc_calibrate [DIR] [--char NAME] [--verbose] [--preset FILE] | --pair BEFORE.json AFTER.json\n";
      return 2;
    }
  }
  try {
    const fs::path gd = game_dir();
    std::unique_ptr<arz::Database> db;
    if (!gd.empty() && fs::exists(gd / "database" / "database.arz"))
      db = std::make_unique<arz::Database>(arz::Database::open_game(gd));
    else
      std::cout << "(no game database: skill grants not applied)\n";
    Ctx x;
    x.db = db.get();
    x.verbose = verbose;
    if (preset_file) {
      std::ifstream f(preset_file, std::ios::binary);
      if (!f) throw std::runtime_error(std::string(preset_file) + ": cannot open");
      std::string error;
      const auto p = score::parse(std::string(std::istreambuf_iterator<char>(f), {}), error);
      if (!p) throw std::runtime_error(std::string(preset_file) + ": " + error);
      x.preset = *p;
    }
    Totals t;
    if (pair[0]) {
      const auto a = load_json(pair[0]), b = load_json(pair[1]);
      x.verbose = true;
      report("pair", a, b, calibrate::check(a, b, db.get()), x, t);
    } else {
      if (dir.empty()) dir = gd / "compat" / "snapshots";
      const bool is_char = fs::is_directory(dir) && std::any_of(fs::directory_iterator(dir), fs::directory_iterator{},
                                                                  [](const auto& e) { return e.path().string().ends_with(".json.gz"); });
      if (is_char) {
        calibrate_dir(dir, x, t);
      } else {
        for (const auto& e : fs::directory_iterator(dir))
          if (e.is_directory() && (only.empty() || e.path().filename() == only)) calibrate_dir(e.path(), x, t);
      }
    }
    std::cout << t.checked << " checked, " << t.failed << " failed, " << t.skipped << " skipped; requirements "
              << t.req_checked << " checked, " << t.req_failed << " disagree with the engine\n";
    return t.failed || t.req_failed ? 1 : 0;
  } catch (const std::exception& e) {
    std::cerr << "gdc_calibrate: " << e.what() << "\n";
    return 2;
  }
}
