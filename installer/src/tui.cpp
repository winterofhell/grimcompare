#include "tui.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>

#include "platform.h"
#include <cstdio>
#include <iostream>

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

enum class K { kNone, kUp, kDown, kPageUp, kPageDown, kHome, kEnd, kEnter, kEsc, kBack, kSpace, kTab, kChar, kQuit };
struct Key {
  K k = K::kNone;
  std::string text;  // kChar: one UTF-8 character
};

// Display width as code points (no wide-glyph table: labels are game names and English text).
std::size_t cols(std::string_view s) {
  std::size_t n = 0;
  for (const unsigned char c : s) n += (c & 0xC0) != 0x80;
  return n;
}

std::string clip(std::string_view s, std::size_t w) {
  std::string out;
  std::size_t n = 0;
  for (std::size_t i = 0; i < s.size(); ++i) {
    const unsigned char c = static_cast<unsigned char>(s[i]);
    if ((c & 0xC0) != 0x80 && n++ == w) break;
    out += s[i];
  }
  return out;
}

std::string pad(std::string_view s, std::size_t w) {
  std::string out = clip(s, w);
  out.append(w - std::min(w, cols(out)), ' ');
  return out;
}

std::vector<std::string> wrapped(std::string_view text, std::size_t w) {
  std::vector<std::string> out;
  if (w < 8) w = 8;
  std::size_t start = 0;
  while (start <= text.size()) {
    const auto nl = std::min(text.find('\n', start), text.size());
    std::string_view para = text.substr(start, nl - start);
    start = nl + 1;
    std::string cur;
    std::size_t lead = para.find_first_not_of(' ');
    const std::string indent(lead == std::string_view::npos ? 0 : lead, ' ');
    std::size_t i = 0;
    while (i < para.size()) {
      while (i < para.size() && para[i] == ' ') ++i;
      const auto e = std::min(para.find(' ', i), para.size());
      const std::string_view word = para.substr(i, e - i);
      i = e;
      if (word.empty()) continue;
      if (!cur.empty() && cols(cur) + 1 + cols(word) > w) out.push_back(cur), cur = indent;
      cur += (cur.empty() || cur == indent ? "" : " ") + std::string(word);
    }
    out.push_back(cur);
    if (nl == text.size()) break;
  }
  return out;
}

bool matches(const Item& it, const std::string& q) {
  if (q.empty()) return true;
  auto low = [](std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
  };
  const std::string hay = low(it.label + " " + it.hint);
  std::size_t from = 0;
  std::string word;
  for (const std::string ql = low(q) + " "; from < ql.size(); ++from) {
    if (ql[from] != ' ') {
      word += ql[from];
      continue;
    }
    if (!word.empty() && hay.find(word) == std::string::npos) return false;
    word.clear();
  }
  return true;
}

#ifdef _WIN32

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

int getc_timeout(int ms) {
  pollfd p{STDIN_FILENO, POLLIN, 0};
  if (poll(&p, 1, ms) <= 0) return -1;
  unsigned char c;
  return read(STDIN_FILENO, &c, 1) == 1 ? c : -1;
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

constexpr std::string_view kDim = "\x1b[2m", kBold = "\x1b[1m", kRev = "\x1b[7m", kOff = "\x1b[0m", kYellow = "\x1b[33m";

std::string style(std::string_view code, std::string_view text) {
  const bool on = code == kYellow ? g_color : g_ansi;
  return on ? std::string(code) + std::string(text) + std::string(kOff) : std::string(text);
}

std::string must_read_line() {
  auto s = read_line();
  if (!s) throw Cancelled();
  return *s;
}

// Heading lines, each clipped to the screen: the arrow-key menus redraw from the top, so what the user must still
// see (the game folder above the main menu) goes in the heading.
std::vector<std::string> heading_lines(std::string_view heading, int w) {
  std::vector<std::string> out;
  for (std::size_t start = 0;;) {
    const auto nl = heading.find('\n', start);
    out.push_back(clip(heading.substr(start, nl - start), static_cast<std::size_t>(w - 1)));
    if (nl == std::string_view::npos) return out;
    start = nl + 1;
  }
}

struct Screen {
  std::string buf;
  void add(std::string_view s) { buf += s, buf += "\x1b[K\r\n"; }
  void flush() {
    out("\x1b[H" + buf + "\x1b[J");
    buf.clear();
  }
};

}  // namespace

std::string fmt(std::string_view pattern, const std::vector<std::string>& args) {
  std::string out;
  std::size_t next = 0;
  for (std::size_t i = 0; i < pattern.size(); ++i) {
    if (pattern.substr(i, 2) == "{}" && next < args.size()) {
      out += args[next++];
      ++i;
    } else {
      out += pattern[i];
    }
  }
  return out;
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

void line(std::string_view text) { out(std::string(text) + "\n"); }
// Not wrapped: headings hold paths, which must stay whole to be copied.
void title(std::string_view text) { line("\n" + style(kBold, text)); }
void warn(std::string_view text) {
  for (const auto& l : wrapped(text, static_cast<std::size_t>(width() - 4))) line(style(kYellow, "! " + l));
}
void wrap(std::string_view text, int indent) {
  for (const auto& l : wrapped(text, static_cast<std::size_t>(width() - indent - 1))) line(std::string(static_cast<std::size_t>(indent), ' ') + l);
}

void hang(std::string_view lead, std::string_view text) {
  const std::size_t n = cols(lead);
  const auto lines = wrapped(text, static_cast<std::size_t>(width() - 1) - std::min<std::size_t>(n, static_cast<std::size_t>(width() / 2)));
  for (std::size_t i = 0; i < lines.size(); ++i) line((i == 0 ? std::string(lead) : std::string(n, ' ')) + lines[i]);
}

int choose(std::string_view heading, const std::vector<Item>& items, int start, const std::function<std::string(int)>& panel,
           bool search, bool quits) {
  if (items.empty()) return -1;
  const std::string back = quits ? tr("quit") : tr("go back");
  if (!g_keys) {
    std::string q;
    for (;;) {
      std::vector<int> shown;
      for (int i = 0; i < static_cast<int>(items.size()); ++i)
        if (matches(items[static_cast<std::size_t>(i)], q)) shown.push_back(i);
      title(heading);
      for (std::size_t n = 0; n < shown.size(); ++n) {
        const Item& it = items[static_cast<std::size_t>(shown[n])];
        hang("  " + std::to_string(n + 1) + ") ", it.label + (it.hint.empty() ? "" : "   (" + it.hint + ")"));
      }
      const auto def = std::find(shown.begin(), shown.end(), start);
      std::string help = def != shown.end() ? fmt(tr("Number (Enter = {})"), {std::to_string(def - shown.begin() + 1)}) : tr("Number");
      if (panel) help += tr(", ?N for details");
      if (search) help += tr(", /text to search");
      out(help + fmt(tr(", 0 to {}: "), {back}));
      const std::string a = must_read_line();
      if (a.starts_with("/")) {
        q = a.substr(1);
        continue;
      }
      const bool info = a.starts_with("?");
      try {
        const int n = std::stoi(info ? a.substr(1) : a);
        if (n == 0 && !info) return -1;
        if (n >= 1 && n <= static_cast<int>(shown.size())) {
          if (!info) return shown[static_cast<std::size_t>(n - 1)];
          if (panel) wrap(panel(shown[static_cast<std::size_t>(n - 1)]), 4);
          continue;
        }
      } catch (const std::exception&) {
      }
      if (a.empty() && def != shown.end()) return start;
      warn(fmt(tr("Type one of the numbers above, or 0 to {}."), {back}));
    }
  }

  raw(true);
  out("\x1b[?25l");
  std::string q;
  int cur = std::clamp(start, 0, static_cast<int>(items.size()) - 1), top = 0;
  int result = -1;
  for (;;) {
    std::vector<int> shown;
    for (int i = 0; i < static_cast<int>(items.size()); ++i)
      if (matches(items[static_cast<std::size_t>(i)], q)) shown.push_back(i);
    auto pos = std::find(shown.begin(), shown.end(), cur);
    if (pos == shown.end() && !shown.empty()) cur = shown.front(), pos = shown.begin();
    const int w = width(), h = term_height();
    const bool side = panel && w >= 100;
    const int list_w = side ? std::min(52, w / 2) : w - 1;
    const auto head = heading_lines(heading, w);
    const int header = static_cast<int>(head.size()) + 1 + (search ? 1 : 0);
    const std::vector<std::string> body = panel && !shown.empty()
                                              ? wrapped(panel(cur), static_cast<std::size_t>(side ? w - list_w - 4 : w - 3))
                                              : std::vector<std::string>{};
    const int below = side || body.empty() ? 0 : std::min<int>(static_cast<int>(body.size()) + 1, h / 2);
    const int rows = std::max(3, h - header - 2 - below);
    const int at = static_cast<int>(pos - shown.begin());
    if (at < top) top = at;
    if (at >= top + rows) top = at - rows + 1;
    top = std::clamp(top, 0, std::max(0, static_cast<int>(shown.size()) - rows));

    Screen s;
    for (const auto& l : head) s.add(style(kBold, l));
    if (search) s.add(tr("Search: ") + q + style(kDim, "_"));
    s.add("");
    for (int r = 0; r < rows; ++r) {
      std::string left;
      const int idx = top + r;
      if (idx < static_cast<int>(shown.size())) {
        const Item& it = items[static_cast<std::size_t>(shown[static_cast<std::size_t>(idx)])];
        const auto lw = static_cast<std::size_t>(list_w);
        if (shown[static_cast<std::size_t>(idx)] == cur) {
          left = style(kRev, pad(" > " + it.label + (it.hint.empty() ? "" : "  " + it.hint), lw));
        } else {
          const std::string label = clip("   " + it.label, lw);
          const std::size_t room = lw - cols(label);
          const std::string hint = it.hint.empty() || room < 4 ? "" : clip("  " + it.hint, room);
          left = label + style(kDim, hint) + std::string(room - cols(hint), ' ');
        }
      } else {
        const std::string blank = pad(r == 0 && shown.empty() ? tr("   (nothing matches)") : "", static_cast<std::size_t>(list_w));
        left = style(kDim, blank);
      }
      if (side && r < static_cast<int>(body.size())) left += style(kDim, " | ") + body[static_cast<std::size_t>(r)];
      s.add(left);
    }
    if (below) {
      s.add(style(kDim, std::string(static_cast<std::size_t>(std::min(w - 1, 60)), '-')));
      for (int r = 0; r + 1 < below; ++r) s.add(" " + body[static_cast<std::size_t>(r)]);
    }
    s.add(style(kDim, fmt(tr(search ? "Up/Down move, Enter select, Esc {}, type to search" : "Up/Down move, Enter select, Esc {}"),
                          {quits ? tr("quit") : tr("back")})));
    s.flush();

    const Key k = read_key();
    const int n = static_cast<int>(shown.size());
    if (k.k == K::kQuit) {
      raw(false);
      out("\x1b[?25h");
      throw Cancelled();
    }
    if (k.k == K::kEnter && n > 0) {
      result = cur;
      break;
    }
    if (k.k == K::kEsc) {
      if (search && !q.empty()) {
        q.clear();
        continue;
      }
      break;
    }
    if (n == 0 && k.k != K::kBack && k.k != K::kChar && k.k != K::kSpace) continue;
    if (k.k == K::kUp) cur = shown[static_cast<std::size_t>((at - 1 + n) % n)];
    else if (k.k == K::kDown || k.k == K::kTab) cur = shown[static_cast<std::size_t>((at + 1) % n)];
    else if (k.k == K::kPageUp) cur = shown[static_cast<std::size_t>(std::max(0, at - rows))];
    else if (k.k == K::kPageDown) cur = shown[static_cast<std::size_t>(std::min(n - 1, at + rows))];
    else if (k.k == K::kHome) cur = shown.front();
    else if (k.k == K::kEnd) cur = shown.back();
    else if (search && k.k == K::kBack && !q.empty()) {
      // The last UTF-8 character: its continuation bytes, then its lead (or ASCII) byte.
      while (!q.empty() && (static_cast<unsigned char>(q.back()) & 0xC0) == 0x80) q.pop_back();
      if (!q.empty()) q.pop_back();
    } else if (search && (k.k == K::kChar || k.k == K::kSpace)) {
      if (!(q.empty() && k.text == "/")) q += k.text;  // "/" opens search in the numbered menus; here typing does
    }
  }
  raw(false);
  out("\x1b[?25h\x1b[H\x1b[J");
  return result;
}

std::optional<std::vector<int>> choose_many(std::string_view heading, const std::vector<Item>& items, const std::vector<int>& picked,
                                            int max) {
  std::vector<bool> on(items.size(), false);
  for (const int i : picked)
    if (i >= 0 && i < static_cast<int>(items.size())) on[static_cast<std::size_t>(i)] = true;
  auto result = [&] {
    std::vector<int> r;
    for (std::size_t i = 0; i < on.size(); ++i)
      if (on[i]) r.push_back(static_cast<int>(i));
    return r;
  };
  if (!g_keys) {
    for (;;) {
      title(heading);
      for (std::size_t i = 0; i < items.size(); ++i)
        hang("  " + std::to_string(i + 1) + ") [" + (on[i] ? "x" : " ") + "] ",
             items[i].label + (items[i].hint.empty() ? "" : "   (" + items[i].hint + ")"));
      out(tr("Numbers separated by spaces (Enter keeps the marked ones, 0 to go back): "));
      const std::string a = must_read_line();
      if (a == "0") return std::nullopt;
      if (a.empty()) return result();
      std::vector<bool> next(items.size(), false);
      bool bad = false;
      std::string num;
      for (const char c : a + " ") {
        if (std::isdigit(static_cast<unsigned char>(c))) {
          num += c;
          continue;
        }
        if (num.empty()) continue;
        const int n = std::stoi(num);
        num.clear();
        if (n < 1 || n > static_cast<int>(items.size())) bad = true;
        else next[static_cast<std::size_t>(n - 1)] = true;
      }
      if (max > 0 && std::count(next.begin(), next.end(), true) > max) bad = true;
      if (bad) {
        warn(max > 0 ? fmt(tr("Pick up to {} of the numbers above."), {std::to_string(max)}) : tr("Pick from the numbers above."));
        continue;
      }
      on = next;
      return result();
    }
  }
  raw(true);
  out("\x1b[?25l");
  int cur = 0, top = 0;
  std::string note;
  for (;;) {
    const int w = width(), h = term_height();
    const auto head = heading_lines(heading, w);
    const int rows = std::max(3, h - 4 - static_cast<int>(head.size()));
    const int n = static_cast<int>(items.size());
    if (cur < top) top = cur;
    if (cur >= top + rows) top = cur - rows + 1;
    Screen s;
    for (const auto& l : head) s.add(style(kBold, l));
    s.add(note.empty() ? "" : style(kYellow, note));
    for (int r = 0; r < rows && top + r < n; ++r) {
      const int i = top + r;
      const Item& it = items[static_cast<std::size_t>(i)];
      std::string text = pad(std::string(i == cur ? " > " : "   ") + "[" + (on[static_cast<std::size_t>(i)] ? "x" : " ") + "] " + it.label +
                                 (it.hint.empty() ? "" : "  " + it.hint),
                             static_cast<std::size_t>(w - 1));
      s.add(i == cur ? style(kRev, text) : text);
    }
    s.add(style(kDim, tr("Up/Down move, Space mark, Enter done, Esc back")));
    s.flush();
    const Key k = read_key();
    note.clear();
    if (k.k == K::kQuit) {
      raw(false);
      out("\x1b[?25h");
      throw Cancelled();
    }
    if (k.k == K::kEsc) {
      raw(false);
      out("\x1b[?25h\x1b[H\x1b[J");
      return std::nullopt;
    }
    if (k.k == K::kEnter) break;
    if (k.k == K::kUp) cur = (cur - 1 + n) % n;
    else if (k.k == K::kDown || k.k == K::kTab) cur = (cur + 1) % n;
    else if (k.k == K::kHome) cur = 0;
    else if (k.k == K::kEnd) cur = n - 1;
    else if (k.k == K::kSpace) {
      auto b = on[static_cast<std::size_t>(cur)];  // vector<bool> proxy
      if (!b && max > 0 && std::count(on.begin(), on.end(), true) >= max) note = fmt(tr("Up to {}; unmark one first."), {std::to_string(max)});
      else b = !b;
    }
  }
  raw(false);
  out("\x1b[?25h\x1b[H\x1b[J");
  return result();
}

std::string ask(std::string_view prompt, std::string_view fallback) {
  out(std::string(prompt) + (fallback.empty() ? "" : " [" + std::string(fallback) + "]") + ": ");
  std::string a = must_read_line();
  const auto b = a.find_first_not_of(" \t"), e = a.find_last_not_of(" \t");
  a = b == std::string::npos ? "" : a.substr(b, e - b + 1);
  return a.empty() ? std::string(fallback) : a;
}

bool confirm(std::string_view question, bool fallback) {
  for (;;) {
    const std::string a = ask(std::string(question) + (fallback ? tr(" (Y/n)") : tr(" (y/N)")), "");
    if (a.empty()) return fallback;
    const char c = static_cast<char>(std::tolower(static_cast<unsigned char>(a[0])));
    if (c == 'y') return true;
    if (c == 'n') return false;
    warn(tr("Answer y or n."));
  }
}

void pause() { ask(tr("Press Enter to continue"), ""); }

}  // namespace setup::ui
