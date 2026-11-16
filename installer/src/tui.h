#pragma once
// Console UI: arrow-key menus with search and a side panel on terminals that can (Linux termios + ANSI, Windows
// console with virtual terminal sequences), numbered menus everywhere else (--plain, pipes, TERM=dumb).
// Every user-facing string goes through tr() so a translation table can be added in one place.
#include <functional>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace setup::ui {

// shortcut: English only; tr() is the single hook for a translation table — revisit when a second language ships.
inline std::string tr(std::string_view s) { return std::string(s); }
// tr(pattern) with each "{}" replaced by the next argument.
std::string fmt(std::string_view pattern, const std::vector<std::string>& args);

struct Cancelled : std::runtime_error {
  Cancelled() : std::runtime_error("cancelled") {}
};

void init(bool plain);  // once, before anything else
bool keys();            // arrow-key menus in use
int width();

void out(std::string_view text);  // UTF-8, as is
void line(std::string_view text = "");
void title(std::string_view text);
void warn(std::string_view text);
void wrap(std::string_view text, int indent = 0);

struct Item {
  std::string label;
  std::string hint;  // dim, after the label
};

// Index into items, or -1 for back (Esc / 0). panel(i): text shown beside (or under) item i.
int choose(std::string_view heading, const std::vector<Item>& items, int start = 0,
           const std::function<std::string(int)>& panel = {}, bool search = false);
// Picked indexes (in item order), or nullopt for back. max = 0: no limit.
std::optional<std::vector<int>> choose_many(std::string_view heading, const std::vector<Item>& items,
                                            const std::vector<int>& picked, int max = 0);
std::string ask(std::string_view prompt, std::string_view fallback = "");
bool confirm(std::string_view question, bool fallback);
void pause();

}  // namespace setup::ui
