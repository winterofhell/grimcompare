// Terminal I/O: termios + ANSI, or the Windows console.
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <iostream>

#include "platform.h"
#include "tui_internal.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#else
#include <csignal>
#include <poll.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>
#endif

namespace setup::ui {
namespace {

bool g_keys = false;   // arrow-key menus
bool g_ansi = false;   // escape codes understood (bold, dim, reverse)
bool g_color = false;  // and color wanted (no NO_COLOR)

}  // namespace

#ifdef _WIN32

namespace {

HANDLE in_h() { return GetStdHandle(STD_INPUT_HANDLE); }
HANDLE out_h() { return GetStdHandle(STD_OUTPUT_HANDLE); }
DWORD g_in_mode = 0;
bool g_console_in = false;

std::string utf8(std::wstring_view w) {
  if (w.empty()) return {};
  const int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
  std::string s(static_cast<std::size_t>(n), '\0');
  WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), s.data(), n, nullptr, nullptr);
  return s;
}

}  // namespace

void raw(bool on) { SetConsoleMode(in_h(), on ? ENABLE_EXTENDED_FLAGS : g_in_mode); }

Key read_key() {
  for (;;) {
    INPUT_RECORD r;
    DWORD n = 0;
    if (!ReadConsoleInputW(in_h(), &r, 1, &n) || n == 0) return {K::kQuit, ""};
    if (r.EventType != KEY_EVENT || !r.Event.KeyEvent.bKeyDown) continue;
    const auto& e = r.Event.KeyEvent;
    switch (e.wVirtualKeyCode) {
      case VK_UP: return {K::kUp, ""};
      case VK_DOWN: return {K::kDown, ""};
      case VK_PRIOR: return {K::kPageUp, ""};
      case VK_NEXT: return {K::kPageDown, ""};
      case VK_HOME: return {K::kHome, ""};
      case VK_END: return {K::kEnd, ""};
      case VK_RETURN: return {K::kEnter, ""};
      case VK_ESCAPE: return {K::kEsc, ""};
      case VK_BACK: return {K::kBack, ""};
      case VK_TAB: return {K::kTab, ""};
      case VK_SPACE: return {K::kSpace, " "};
      default: break;
    }
    const wchar_t c = e.uChar.UnicodeChar;
    if (c == 3) return {K::kQuit, ""};  // Ctrl+C with processed input off
    if (c >= 32) return {K::kChar, utf8(std::wstring_view(&c, 1))};
  }
}

int term_width() {
  CONSOLE_SCREEN_BUFFER_INFO i;
  if (!GetConsoleScreenBufferInfo(out_h(), &i)) return 80;
  return i.srWindow.Right - i.srWindow.Left + 1;
}
int term_height() {
  CONSOLE_SCREEN_BUFFER_INFO i;
  if (!GetConsoleScreenBufferInfo(out_h(), &i)) return 25;
  return i.srWindow.Bottom - i.srWindow.Top + 1;
}

std::optional<std::string> read_line() {
  if (g_console_in) {
    SetConsoleMode(in_h(), g_in_mode | ENABLE_LINE_INPUT | ENABLE_ECHO_INPUT | ENABLE_PROCESSED_INPUT);
    std::wstring buf;
    wchar_t chunk[256];
    for (;;) {
      DWORD n = 0;
      if (!ReadConsoleW(in_h(), chunk, 256, &n, nullptr) || n == 0) return std::nullopt;
      buf.append(chunk, n);
      if (buf.find(L'\n') != std::wstring::npos) break;
    }
    while (!buf.empty() && (buf.back() == L'\n' || buf.back() == L'\r')) buf.pop_back();
    return utf8(buf);
  }
  std::string s;
  if (!std::getline(std::cin, s)) return std::nullopt;
  if (!s.empty() && s.back() == '\r') s.pop_back();
  return s;
}

#else

namespace {

termios g_saved{};
bool g_have_saved = false;

void restore() {
  if (g_have_saved) tcsetattr(STDIN_FILENO, TCSANOW, &g_saved);
  if (g_keys) (void)!write(STDOUT_FILENO, "\x1b[?25h", 6);
}

void on_signal(int) {
  restore();
  (void)!write(STDOUT_FILENO, "\n", 1);
  _exit(130);
}

int getc_timeout(int ms) {
  pollfd p{STDIN_FILENO, POLLIN, 0};
  if (poll(&p, 1, ms) <= 0) return -1;
  unsigned char c;
  return read(STDIN_FILENO, &c, 1) == 1 ? c : -1;
}

}  // namespace

void raw(bool on) {
  if (!g_have_saved) return;
  if (!on) {
    tcsetattr(STDIN_FILENO, TCSANOW, &g_saved);
    return;
  }
  termios t = g_saved;
  t.c_lflag &= static_cast<tcflag_t>(~(ICANON | ECHO));
  t.c_cc[VMIN] = 1;
  t.c_cc[VTIME] = 0;
  tcsetattr(STDIN_FILENO, TCSANOW, &t);
}

Key read_key() {
  const int c = getc_timeout(-1);
  if (c < 0) return {K::kQuit, ""};
  if (c == 27) {
    const int a = getc_timeout(40);
    if (a != '[' && a != 'O') return {K::kEsc, ""};
    std::string seq;
    for (int b; (b = getc_timeout(40)) >= 0;) {
      seq += static_cast<char>(b);
      if ((b >= 'A' && b <= 'Z') || b == '~') break;
    }
    if (seq == "A") return {K::kUp, ""};
    if (seq == "B") return {K::kDown, ""};
    if (seq == "H" || seq == "1~" || seq == "7~") return {K::kHome, ""};
    if (seq == "F" || seq == "4~" || seq == "8~") return {K::kEnd, ""};
    if (seq == "5~") return {K::kPageUp, ""};
    if (seq == "6~") return {K::kPageDown, ""};
    return {K::kNone, ""};
  }
  if (c == '\r' || c == '\n') return {K::kEnter, ""};
  if (c == 127 || c == 8) return {K::kBack, ""};
  if (c == '\t') return {K::kTab, ""};
  if (c == ' ') return {K::kSpace, " "};
  if (c == 4) return {K::kQuit, ""};  // Ctrl+D
  if (c < 32) return {K::kNone, ""};
  std::string ch(1, static_cast<char>(c));
  const int extra = c >= 0xF0 ? 3 : c >= 0xE0 ? 2 : c >= 0xC0 ? 1 : 0;
  for (int i = 0; i < extra; ++i)
    if (const int b = getc_timeout(40); b >= 0) ch += static_cast<char>(b);
  return {K::kChar, ch};
}

int term_width() {
  winsize w{};
  return ioctl(STDOUT_FILENO, TIOCGWINSZ, &w) == 0 && w.ws_col > 0 ? w.ws_col : 80;
}
int term_height() {
  winsize w{};
  return ioctl(STDOUT_FILENO, TIOCGWINSZ, &w) == 0 && w.ws_row > 0 ? w.ws_row : 24;
}

std::optional<std::string> read_line() {
  std::string s;
  if (!std::getline(std::cin, s)) return std::nullopt;
  return s;
}

#endif

std::string style(std::string_view code, std::string_view text) {
  const bool on = code == kYellow ? g_color : g_ansi;
  return on ? std::string(code) + std::string(text) + std::string(kOff) : std::string(text);
}

void init(bool plain) {
#ifdef _WIN32
  SetConsoleOutputCP(CP_UTF8);
  SetConsoleCP(CP_UTF8);
  DWORD om = 0;
  g_console_in = GetConsoleMode(in_h(), &g_in_mode) != 0;
  const bool vt = GetConsoleMode(out_h(), &om) &&
                  SetConsoleMode(out_h(), om | ENABLE_VIRTUAL_TERMINAL_PROCESSING | ENABLE_PROCESSED_OUTPUT);
  g_ansi = vt;
  g_keys = !plain && g_console_in && vt;
#else
  const auto term = env("TERM");
  g_ansi = isatty(STDOUT_FILENO) && term && *term != "dumb";
  g_keys = !plain && isatty(STDIN_FILENO) && g_ansi;
  if (isatty(STDIN_FILENO) && tcgetattr(STDIN_FILENO, &g_saved) == 0) {
    g_have_saved = true;
    std::atexit(restore);
    std::signal(SIGINT, on_signal);
    std::signal(SIGTERM, on_signal);
    std::signal(SIGHUP, on_signal);
  }
#endif
  const auto no_color = env("NO_COLOR");  // no-color.org: set and not empty
  g_color = g_ansi && !(no_color && !no_color->empty());
}

bool keys() { return g_keys; }
int width() { return std::clamp(term_width(), 40, 160); }

void out(std::string_view text) {
#ifdef _WIN32
  DWORD n = 0;
  if (GetConsoleMode(out_h(), &n)) {
    const int len = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    std::wstring w(static_cast<std::size_t>(len), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), w.data(), len);
    WriteConsoleW(out_h(), w.data(), static_cast<DWORD>(w.size()), &n, nullptr);
    return;
  }
#endif
  std::fwrite(text.data(), 1, text.size(), stdout);
  std::fflush(stdout);
}

}  // namespace setup::ui
