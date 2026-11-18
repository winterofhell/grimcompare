// Preset files and screen settings, polled from the game thread.
#include <algorithm>
#include <utility>

#include "tooltip_internal.h"

namespace gdc::adapter::tip {
namespace {

// Values outside GetUIScaleFactor's range (0.7..2) keep the last ones.
void poll_screen() {
  if (!g_engine_var) return;
  const void* engine = *g_engine_var;
  const void* graphics = engine ? g_graphics(engine) : nullptr;
  if (!graphics) return;
  const tooltip::Screen now{g_ui_scale(graphics), g_screen_width(graphics)};
  if (!(now.ui_scale >= 0.5f && now.ui_scale <= 4.0f) || now.width < 1 || now.width > 65536 || now == g_screen) return;
  g_screen = now;
  ++g_preset_version;
  log::printf("tooltip: UI scale %.6f, screen width %d: lists break at %s", static_cast<double>(now.ui_scale), now.width,
              now.width >= 1600 ? "the long sample" : "the short sample");
}

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

using Listing = std::vector<std::pair<std::wstring, WIN32_FILE_ATTRIBUTE_DATA>>;

// *.toml in GDCompare.presets, sorted by name.
Listing list_named() {
  Listing found;
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
  return found;
}

void poll_named() {
  const Listing found = list_named();
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
  else utf16("GrimCompare: preset GDCompare.presets\\" + failed + " (not used)", g_named_error);
  ++g_preset_version;
}

// Built-in defaults equal presets/default.toml (tests/test_score.cpp).
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
    utf16("GrimCompare: preset not loaded: " + error + " (last good preset in use)", g_preset_error);
    log::printf("tooltip: preset GDCompare.preset.toml not loaded: %s; last good preset in use", error.c_str());
  }
  ++g_preset_version;
}

}  // namespace

// Auto: first named preset that fits, else the main one.
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

void publish_presets() {
  auto p = std::make_shared<Presets>();
  p->version = g_preset_version;
  p->screen = g_screen;
  p->main = g_preset;
  for (const auto& n : g_named)
    if (n.preset) p->named.emplace_back(log::utf8(n.file), *n.preset);
  std::stable_sort(p->named.begin(), p->named.end(),
                   [](const auto& a, const auto& b) { return score::tried_first(a.second, b.second); });
  g_presets = std::move(p);
  ++g_gen;
  WakeConditionVariable(&g_cv);
}

// Once a second; a change republishes the presets.
void poll_presets(ULONGLONG now) {
  if (now - g_preset_poll_ms < kPresetPollMs) return;
  g_preset_poll_ms = now;
  AcquireSRWLockExclusive(&g_lock);
  const u64 version = g_preset_version;
  if (g_on) guarded("preset", poll_preset);
  if (g_on) guarded("named presets", poll_named);
  if (g_on) guarded("screen", poll_screen);
  if (g_on && g_preset_version != version) guarded("presets", publish_presets);
  ReleaseSRWLockExclusive(&g_lock);
}

}  // namespace gdc::adapter::tip
