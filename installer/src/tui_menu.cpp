// Menus: numbered, or arrow keys with search and panel.
#include <algorithm>
#include <cctype>

#include "tui_internal.h"

namespace setup::ui {
namespace {

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

std::vector<int> matching(const std::vector<Item>& items, const std::string& q) {
  std::vector<int> shown;
  for (int i = 0; i < static_cast<int>(items.size()); ++i)
    if (matches(items[static_cast<std::size_t>(i)], q)) shown.push_back(i);
  return shown;
}

// Lines clipped; menus redraw from the top, keep context here.
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

// For plain terminals and redirected input.
int choose_numbered(std::string_view heading, const std::vector<Item>& items, int start,
                    const std::function<std::string(int)>& panel, bool search, const std::string& back) {
  std::string q;
  for (;;) {
    const std::vector<int> shown = matching(items, q);
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

// Arrow-key menu state, redrawn after every key.
struct Menu {
  std::string_view heading;
  const std::vector<Item>& items;
  const std::function<std::string(int)>& panel;
  bool search = false, quits = false;
  std::string q;
  int cur = 0, top = 0;

  std::string list_row(const std::vector<int>& shown, int idx, bool first, std::size_t lw) const {
    if (idx < static_cast<int>(shown.size())) {
      const Item& it = items[static_cast<std::size_t>(shown[static_cast<std::size_t>(idx)])];
      if (shown[static_cast<std::size_t>(idx)] == cur)
        return style(kRev, pad(" > " + it.label + (it.hint.empty() ? "" : "  " + it.hint), lw));
      const std::string label = clip("   " + it.label, lw);
      const std::size_t room = lw - cols(label);
      const std::string hint = it.hint.empty() || room < 4 ? "" : clip("  " + it.hint, room);
      return label + style(kDim, hint) + std::string(room - cols(hint), ' ');
    }
    const std::string blank = pad(first && shown.empty() ? tr("   (nothing matches)") : "", lw);
    return style(kDim, blank);
  }

  // Draws the screen; returns how many list rows fit.
  int draw(const std::vector<int>& shown, int at) {
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
    if (at < top) top = at;
    if (at >= top + rows) top = at - rows + 1;
    top = std::clamp(top, 0, std::max(0, static_cast<int>(shown.size()) - rows));

    Screen s;
    for (const auto& l : head) s.add(style(kBold, l));
    if (search) s.add(tr("Search: ") + q + style(kDim, "_"));
    s.add("");
    for (int r = 0; r < rows; ++r) {
      std::string left = list_row(shown, top + r, r == 0, static_cast<std::size_t>(list_w));
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
    return rows;
  }

  // Moves or edits the search; true ends the menu.
  bool key(const Key& k, const std::vector<int>& shown, int at, int rows, int& result) {
    const int n = static_cast<int>(shown.size());
    if (k.k == K::kQuit) {
      raw(false);
      out("\x1b[?25h");
      throw Cancelled();
    }
    if (k.k == K::kEnter && n > 0) {
      result = cur;
      return true;
    }
    if (k.k == K::kEsc) {
      if (!search || q.empty()) return true;
      q.clear();
      return false;
    }
    if (n == 0 && k.k != K::kBack && k.k != K::kChar && k.k != K::kSpace) return false;
    if (k.k == K::kUp) cur = shown[static_cast<std::size_t>((at - 1 + n) % n)];
    else if (k.k == K::kDown || k.k == K::kTab) cur = shown[static_cast<std::size_t>((at + 1) % n)];
    else if (k.k == K::kPageUp) cur = shown[static_cast<std::size_t>(std::max(0, at - rows))];
    else if (k.k == K::kPageDown) cur = shown[static_cast<std::size_t>(std::min(n - 1, at + rows))];
    else if (k.k == K::kHome) cur = shown.front();
    else if (k.k == K::kEnd) cur = shown.back();
    else if (search && k.k == K::kBack && !q.empty()) {
      // Drop the last UTF-8 character, continuation bytes first.
      while (!q.empty() && (static_cast<unsigned char>(q.back()) & 0xC0) == 0x80) q.pop_back();
      if (!q.empty()) q.pop_back();
    } else if (search && (k.k == K::kChar || k.k == K::kSpace)) {
      if (!(q.empty() && k.text == "/")) q += k.text;  // "/" opens search in the numbered menus; here typing does
    }
    return false;
  }
};

// Marks from typed numbers. False: go back.
bool mark_numbered(std::string_view heading, const std::vector<Item>& items, std::vector<bool>& on, int max) {
  for (;;) {
    title(heading);
    for (std::size_t i = 0; i < items.size(); ++i)
      hang("  " + std::to_string(i + 1) + ") [" + (on[i] ? "x" : " ") + "] ",
           items[i].label + (items[i].hint.empty() ? "" : "   (" + items[i].hint + ")"));
    out(tr("Numbers separated by spaces (Enter keeps the marked ones, 0 to go back): "));
    const std::string a = must_read_line();
    if (a == "0") return false;
    if (a.empty()) return true;
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
    return true;
  }
}

// Marks with Space. False: go back.
bool mark_keys(std::string_view heading, const std::vector<Item>& items, std::vector<bool>& on, int max) {
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
      return false;
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
  return true;
}

}  // namespace

int choose(std::string_view heading, const std::vector<Item>& items, int start, const std::function<std::string(int)>& panel,
           bool search, bool quits) {
  if (items.empty()) return -1;
  const std::string back = quits ? tr("quit") : tr("go back");
  if (!keys()) return choose_numbered(heading, items, start, panel, search, back);
  raw(true);
  out("\x1b[?25l");
  Menu m{heading, items, panel, search, quits, "", std::clamp(start, 0, static_cast<int>(items.size()) - 1), 0};
  int result = -1;
  for (;;) {
    const std::vector<int> shown = matching(items, m.q);
    auto pos = std::find(shown.begin(), shown.end(), m.cur);
    if (pos == shown.end() && !shown.empty()) m.cur = shown.front(), pos = shown.begin();
    const int at = static_cast<int>(pos - shown.begin());
    const int rows = m.draw(shown, at);
    if (m.key(read_key(), shown, at, rows, result)) break;
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
  const bool done = keys() ? mark_keys(heading, items, on, max) : mark_numbered(heading, items, on, max);
  if (!done) return std::nullopt;
  std::vector<int> r;
  for (std::size_t i = 0; i < on.size(); ++i)
    if (on[i]) r.push_back(static_cast<int>(i));
  return r;
}

}  // namespace setup::ui
