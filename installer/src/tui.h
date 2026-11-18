#pragma once
// Console UI; all user strings go through tr().
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
// lead, then text wrapped under its start ("  3) label (hint)").
void hang(std::string_view lead, std::string_view text);

struct Item {
  std::string label;
  std::string hint;  // dim, after the label
};

// Index or -1 for back.
int choose(std::string_view heading, const std::vector<Item>& items, int start = 0,
           const std::function<std::string(int)>& panel = {}, bool search = false, bool quits = false);
// Picked indexes in item order, nullopt: back; max 0: unlimited.
std::optional<std::vector<int>> choose_many(std::string_view heading, const std::vector<Item>& items,
                                            const std::vector<int>& picked, int max = 0);
std::string ask(std::string_view prompt, std::string_view fallback = "");
bool confirm(std::string_view question, bool fallback);
void pause();

}  // namespace setup::ui
