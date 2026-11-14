#include "calibration.h"

#include <windows.h>

#include <atomic>
#include <cstdio>
#include <exception>
#include <optional>
#include <set>
#include <vector>

#include "gdc/calibrate.h"
#include "log.h"
#include "seh.h"

namespace gdc::adapter {
namespace {

constexpr LONGLONG kRotateBytes = 2LL << 20;  // at init: a larger file moves to GDCompare.calibration.old.txt

std::wstring g_path;
std::string g_version;
std::atomic<bool> g_on{false};
std::atomic<int> g_failures{0};
// Writer thread only.
std::optional<snap::Snapshot> g_prev;
std::set<std::string> g_req_seen;  // requirement disagreements already written this session
bool g_header = false;

void append(const std::vector<std::string>& lines) {
  std::string out;
  if (!g_header) {
    SYSTEMTIME t;
    GetLocalTime(&t);
    char buf[96];
    std::snprintf(buf, sizeof buf, "== %04u-%02u-%02u %02u:%02u:%02u session, GD Compare %s\r\n", t.wYear, t.wMonth,
                  t.wDay, t.wHour, t.wMinute, t.wSecond, g_version.c_str());
    out += buf;
    g_header = true;
  }
  for (const auto& l : lines) out += l + "\r\n";
  const HANDLE h = CreateFileW(g_path.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ, nullptr, OPEN_ALWAYS,
                               FILE_ATTRIBUTE_NORMAL, nullptr);
  if (h == INVALID_HANDLE_VALUE) {
    log::printf("calibration: cannot open GDCompare.calibration.txt (%lu)", GetLastError());
    return;
  }
  DWORD written = 0;
  if (!WriteFile(h, out.data(), static_cast<DWORD>(out.size()), &written, nullptr))
    log::printf("calibration: writing GDCompare.calibration.txt failed (%lu)", GetLastError());
  CloseHandle(h);
}

void record(const snap::Snapshot& s, bool full, const std::string& name, const arz::Database* db) {
  const std::string label = s.character_name + " " + name;
  std::vector<std::string> out;
  const auto rq = calibrate::requirements(s);
  for (const auto& m : rq.mismatches)
    if (g_req_seen.insert(m).second) {
      out.push_back("REQ  " + label + " " + m);
      ++g_failures;
    }
  if (full || !g_prev) {
    log::printf("calibration: %s starts a chain (%d requirement check(s))", name.c_str(), rq.checked);
  } else if (!db) {
    log::printf("calibration: %s skipped (database not loaded yet)", name.c_str());
  } else {
    const calibrate::Check c = calibrate::check(*g_prev, s, db);
    if (!c.skipped.empty()) {
      log::printf("calibration: %s skipped (%s)", name.c_str(), c.skipped.c_str());
    } else {
      const auto lines = calibrate::report(c, *g_prev, s, label);
      log::printf("calibration: %s", lines[0].c_str());
      if (!c.diffs.empty()) ++g_failures;
      out.insert(out.end(), lines.begin(), lines.end());
    }
  }
  if (!out.empty()) append(out);
  g_prev = s;
}

}  // namespace

void calibration_init(const std::wstring& mod_dir, const char* version) {
  g_path = mod_dir + L"GDCompare.calibration.txt";
  g_version = version;
  WIN32_FILE_ATTRIBUTE_DATA a;
  if (GetFileAttributesExW(g_path.c_str(), GetFileExInfoStandard, &a) &&
      ((static_cast<LONGLONG>(a.nFileSizeHigh) << 32) | a.nFileSizeLow) > kRotateBytes)
    MoveFileExW(g_path.c_str(), (mod_dir + L"GDCompare.calibration.old.txt").c_str(), MOVEFILE_REPLACE_EXISTING);
  g_on = true;
}

void calibration_record(const snap::Snapshot& s, bool full, const std::string& name, const arz::Database* db) {
  if (!g_on) return;
  auto body = [&] {
    try {
      record(s, full, name, db);
    } catch (const std::exception& e) {
      g_on = false;
      log::printf("calibration: C++ exception %s; calibration off", e.what());
    } catch (...) {
      g_on = false;
      log::write("calibration: unknown C++ exception; calibration off");
    }
  };
  seh::Fault f;
  if (!seh::run(body, f)) {
    g_on = false;
    log::printf("calibration: FAULT 0x%08lX at 0x%016llX; calibration off", f.code,
                static_cast<unsigned long long>(f.addr));
  }
}

int calibration_failures() { return g_failures.load(); }

}  // namespace gdc::adapter
