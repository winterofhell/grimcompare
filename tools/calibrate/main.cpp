// gdc_calibrate: replays equipment changes through the swap model and compares with the engine
// (gdc::calibrate). Every pair of consecutive auto-snapshot records that differs only in equipment is a
// swap test, so normal play produces calibration data.
//
// usage: gdc_calibrate [DIR] [--char NAME] [--verbose]
//        gdc_calibrate --pair BEFORE.json AFTER.json
//   DIR        snapshots folder (default $GD_DIR or the Steam install, compat/snapshots) or one character's
//   --verbose  also list skipped pairs and model warnings of passing ones
// The game database ($GD_DIR/database) supplies skill grants; without it the model warns.
// Exit: 0 every checked pair passes, 1 a pair fails, 2 usage or read error.
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

#include "gdc/attribute_names.h"
#include "gdc/calibrate.h"
#include "gdc/store.h"

using namespace gdc;
namespace fs = std::filesystem;

namespace {

fs::path game_dir() {
  if (const char* d = std::getenv("GD_DIR")) return d;
  if (const char* h = std::getenv("HOME")) return fs::path(h) / ".local/share/Steam/steamapps/common/Grim Dawn";
  return {};
}

std::string short_record(const std::string& r) {
  const auto slash = r.rfind('/');
  std::string s = slash == std::string::npos ? r : r.substr(slash + 1);
  if (s.ends_with(".dbr")) s.resize(s.size() - 4);
  return s.empty() ? "-" : s;
}

std::string item_label(const snap::Snapshot& s, int location) {
  for (const auto& it : s.equipped)
    if (it.location == location) {
      std::string out = short_record(it.record);
      if (it.relic) out += "+" + short_record(it.relic->record);
      if (it.augment) out += "+" + short_record(it.augment->record);
      return out;
    }
  return "(empty)";
}

struct Totals {
  int checked = 0, failed = 0, skipped = 0;
};

void report(const std::string& head, const snap::Snapshot& a, const snap::Snapshot& b, const calibrate::Check& c,
            bool verbose, Totals& t) {
  if (!c.skipped.empty()) {
    ++t.skipped;
    if (verbose) std::cout << head << ": skipped (" << c.skipped << ")\n";
    return;
  }
  ++t.checked;
  std::string what;
  for (int l : c.locations)
    what += (what.empty() ? "" : "; ") + std::string(names::equip_location(l)) + "(" + std::to_string(l) + ") " +
            item_label(a, l) + " -> " + item_label(b, l);
  const bool pass = c.diffs.empty();
  if (!pass) ++t.failed;
  std::cout << head << " " << what << ": " << (pass ? "PASS" : "FAIL") << "\n";
  for (const auto& d : c.diffs)
    std::cout << "    " << d.stat << ": predicted " << d.predicted << ", engine " << d.actual << " (tolerance "
              << d.tolerance << ")\n";
  if (!pass || verbose) {
    for (const auto& w : c.warnings) std::cout << "    warning: " << w << "\n";
    for (const auto& n : c.notes) std::cout << "    note: " << n << "\n";
  }
}

snap::Snapshot load_json(const char* path) {
  std::ifstream f(path, std::ios::binary);
  if (!f) throw std::runtime_error(std::string(path) + ": cannot open");
  return snap::from_json(std::string(std::istreambuf_iterator<char>(f), {}));
}

void calibrate_dir(const fs::path& char_dir, const arz::Database* db, bool verbose, Totals& t) {
  std::cout << "== " << char_dir.filename().string() << "\n";
  std::unique_ptr<store::Record> prev;
  store::read(char_dir, [&](const store::Record& r) {
    if (prev && !r.full) {
      char head[64];
      std::snprintf(head, sizeof head, "%s #%04u->#%04u", r.segment.c_str(), prev->seq, r.seq);
      report(head, prev->snapshot, r.snapshot, calibrate::check(prev->snapshot, r.snapshot, db), verbose, t);
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
  for (int i = 1; i < argc; ++i) {
    if (!std::strcmp(argv[i], "--verbose")) verbose = true;
    else if (!std::strcmp(argv[i], "--char") && i + 1 < argc) only = argv[++i];
    else if (!std::strcmp(argv[i], "--pair") && i + 2 < argc) pair[0] = argv[++i], pair[1] = argv[++i];
    else if (argv[i][0] != '-' && dir.empty()) dir = argv[i];
    else {
      std::cerr << "usage: gdc_calibrate [DIR] [--char NAME] [--verbose] | --pair BEFORE.json AFTER.json\n";
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
    Totals t;
    if (pair[0]) {
      const auto a = load_json(pair[0]), b = load_json(pair[1]);
      report("pair", a, b, calibrate::check(a, b, db.get()), true, t);
    } else {
      if (dir.empty()) dir = gd / "compat" / "snapshots";
      const bool is_char = fs::is_directory(dir) && std::any_of(fs::directory_iterator(dir), fs::directory_iterator{},
                                                                  [](const auto& e) { return e.path().string().ends_with(".json.gz"); });
      if (is_char) {
        calibrate_dir(dir, db.get(), verbose, t);
      } else {
        for (const auto& e : fs::directory_iterator(dir))
          if (e.is_directory() && (only.empty() || e.path().filename() == only)) calibrate_dir(e.path(), db.get(), verbose, t);
      }
    }
    std::cout << t.checked << " checked, " << t.failed << " failed, " << t.skipped << " skipped\n";
    return t.failed ? 1 : 0;
  } catch (const std::exception& e) {
    std::cerr << "gdc_calibrate: " << e.what() << "\n";
    return 2;
  }
}
