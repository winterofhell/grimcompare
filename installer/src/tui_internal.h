#pragma once
// Shared by the console UI sources only.
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "tui.h"

namespace setup::ui {

enum class K { kNone, kUp, kDown, kPageUp, kPageDown, kHome, kEnd, kEnter, kEsc, kBack, kSpace, kTab, kChar, kQuit };
struct Key {
  K k = K::kNone;
  std::string text;  // kChar: one UTF-8 character
};

// tui_term.cpp: the terminal.
void raw(bool on);
Key read_key();
int term_height();
std::optional<std::string> read_line();

inline constexpr std::string_view kDim = "\x1b[2m", kBold = "\x1b[1m", kRev = "\x1b[7m", kOff = "\x1b[0m",
                                  kYellow = "\x1b[33m";
std::string style(std::string_view code, std::string_view text);

// tui.cpp: text layout.
std::size_t cols(std::string_view s);
std::string clip(std::string_view s, std::size_t w);
std::string pad(std::string_view s, std::size_t w);
std::vector<std::string> wrapped(std::string_view text, std::size_t w);
std::string must_read_line();

}  // namespace setup::ui
