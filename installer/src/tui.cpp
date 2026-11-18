#include "tui.h"

#include <algorithm>
#include <cctype>

#include "tui_internal.h"

namespace setup::ui {

// Display width in code points; no wide-glyph table needed.
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

std::string must_read_line() {
  auto s = read_line();
  if (!s) throw Cancelled();
  return *s;
}

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

void line(std::string_view text) { out(std::string(text) + "\n"); }
// Not wrapped: headings hold paths that must copy whole.
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
