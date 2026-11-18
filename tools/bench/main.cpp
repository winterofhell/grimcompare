// Times tooltip blocks per changed slot; exit 2 on error.
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "gdc/score.h"
#include "gdc/text.h"
#include "gdc/tooltip.h"

using namespace gdc;
namespace fs = std::filesystem;

namespace {

std::string slurp(const char* path) {
  std::ifstream f(path, std::ios::binary);
  if (!f) throw std::runtime_error(std::string(path) + ": cannot open");
  return std::string(std::istreambuf_iterator<char>(f), {});
}

int type_for(int location) {
  for (int t = 1; t <= 15; ++t) {
    const auto l = tooltip::locations_for(t);
    if (std::find(l.begin(), l.end(), location) != l.end()) return t;
  }
  return 0;
}

// One timed block in ms; not inlined, for profiler toggles.
[[gnu::noinline]] double timed_block(const snap::Snapshot& before, const snap::Item& cand, int type, bool detail,
                                     const arz::Database& db, const score::Preset& preset, std::size_t& lines) {
  const auto t0 = std::chrono::steady_clock::now();
  lines = tooltip::compare(before, cand, type, detail, &db, preset).size();
  return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
}

void print_block(const snap::Snapshot& before, const snap::Item& cand, int type, bool detail, const arz::Database& db,
                 const score::Preset& preset, const Labels& labels) {
  std::printf("== %s loc %d %s\n", cand.record.c_str(), cand.location, detail ? "shift" : "plain");
  for (const auto& l : tooltip::compare(before, cand, type, detail, &db, preset, labels)) std::printf("%s\n", l.text.c_str());
}

// First run decodes records (first hover); later runs hit cache.
void time_block(const snap::Snapshot& before, const snap::Item& cand, int type, bool detail, const arz::Database& db,
                const score::Preset& preset, int runs) {
  std::vector<double> ms;
  std::size_t lines = 0;
  const auto t0 = std::chrono::steady_clock::now();
  lines = tooltip::compare(before, cand, type, detail, &db, preset).size();
  const double first = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
  ms.push_back(first);
  for (int i = 1; i < runs; ++i) ms.push_back(timed_block(before, cand, type, detail, db, preset, lines));
  std::sort(ms.begin(), ms.end());
  std::printf("%-52s loc %2d %-5s %2zu lines  first %7.3f ms  median %7.3f ms  max %7.3f ms\n", cand.record.c_str(),
              cand.location, detail ? "shift" : "plain", lines, first, ms[ms.size() / 2], ms.back());
}

score::Preset load_preset(const char* preset_file) {
  score::Preset preset;
  if (!preset_file) return preset;
  std::string error;
  const auto p = score::parse(slurp(preset_file), error);
  if (!p) throw std::runtime_error(std::string(preset_file) + ": " + error);
  return *p;
}

}  // namespace

int main(int argc, char** argv) {
  const char* files[2] = {};
  const char* preset_file = nullptr;
  int runs = 50, nfiles = 0;
  bool print = false, plain_only = false;
  for (int i = 1; i < argc; ++i) {
    if (!std::strcmp(argv[i], "--preset") && i + 1 < argc) preset_file = argv[++i];
    else if (!std::strcmp(argv[i], "--n") && i + 1 < argc) runs = std::max(1, std::atoi(argv[++i]));
    else if (!std::strcmp(argv[i], "--print")) print = true;
    else if (!std::strcmp(argv[i], "--plain")) plain_only = true;  // no Shift block (profiling)
    else if (argv[i][0] != '-' && nfiles < 2) files[nfiles++] = argv[i];
    else nfiles = 3;
  }
  if (nfiles != 2) {
    std::fprintf(stderr, "usage: gdc_bench BEFORE.json AFTER.json [--preset FILE.toml] [--n RUNS] [--print] [--plain]\n");
    return 2;
  }
  try {
    fs::path gd;
    if (const char* d = std::getenv("GD_DIR")) gd = d;
    else if (const char* h = std::getenv("HOME")) gd = fs::path(h) / ".local/share/Steam/steamapps/common/Grim Dawn";
    const arz::Database db = arz::Database::open_game(gd);
    const text::Table text = text::Table::open_game(gd);
    const Labels labels(&db, &text);
    const score::Preset preset = load_preset(preset_file);
    const snap::Snapshot before = snap::from_json(slurp(files[0])), after = snap::from_json(slurp(files[1]));
    for (const auto& cand : after.equipped) {
      const auto old = std::find_if(before.equipped.begin(), before.equipped.end(),
                                    [&](const snap::Item& x) { return x.location == cand.location; });
      if (old != before.equipped.end() && old->id == cand.id && old->record == cand.record) continue;
      const int type = type_for(cand.location);
      if (!type) continue;
      for (const bool detail : {false, true}) {
        if (detail && plain_only) continue;
        if (print) print_block(before, cand, type, detail, db, preset, labels);
        else time_block(before, cand, type, detail, db, preset, runs);
      }
    }
  } catch (const std::exception& e) {
    std::fprintf(stderr, "gdc_bench: %s\n", e.what());
    return 2;
  }
  return 0;
}
