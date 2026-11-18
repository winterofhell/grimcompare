#include "gdc/tooltip.h"

#include <cctype>
#include <charconv>
#include <cmath>

#include "gdc/font.h"

#include "tooltip_internal.h"

namespace gdc::tooltip {

bool fits(const Screen& screen, std::string_view line) {
  const std::string_view sample = screen.width >= 1600 ? "The quick brown fox jumped over the lazy dog The quick"
                                                       : "The quick brown fox jumped over the lazy";
  return font::width(font::Face::LinBiolinumSans, font::pixel_size(17, screen.ui_scale), line) <=
         font::width(font::Face::Jura, font::pixel_size(16, screen.ui_scale), sample);
}

std::vector<std::string> pack(const std::string& head, const std::string& indent, const std::vector<std::string>& items,
                              const Screen& screen) {
  std::vector<std::string> out{head};
  bool fresh = true;
  for (std::size_t i = 0; i < items.size(); ++i) {
    const std::string item = items[i] + (i + 1 < items.size() ? "," : "");
    if (!fresh && !fits(screen, out.back() + " " + item)) {
      out.push_back(indent);
      fresh = true;
    }
    out.back() += (fresh ? "" : " ") + item;
    fresh = false;
  }
  return out;
}

std::string num(double v, int decimals) {
  const double scale = std::pow(10.0, decimals);
  double r = std::round(v * scale) / scale;
  if (r == 0) r = 0;
  char buf[400];
  return {buf, std::to_chars(buf, buf + sizeof buf, r, std::chars_format::fixed, decimals).ptr};
}

std::string signed_num(double v, int decimals) {
  const std::string s = num(v, decimals);
  return s[0] == '-' || s.find_first_not_of("0.") == std::string::npos ? s : "+" + s;
}

std::string pretty(std::string key) {
  if (key.starts_with("default: ")) {
    key = key.substr(key.rfind(' ') + 1);
    std::string out;
    for (std::size_t i = 0; i < key.size(); ++i) {
      if (i && std::isupper(static_cast<unsigned char>(key[i])) && std::islower(static_cast<unsigned char>(key[i - 1]))) out += ' ';
      out += key[i];
    }
    return out;
  }
  if (key.find('_') == std::string::npos) return key;
  std::replace(key.begin(), key.end(), '_', ' ');
  key[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(key[0])));
  return key;
}

std::string escape(std::string_view text) {
  std::string out;
  for (const char c : text) {
    if (c == '{' || c == '}') out += c == '{' ? '(' : ')';
    else out.append(c == '^' || c == '&' ? 2 : 1, c);
  }
  return out;
}

Line change(double percent, double negligible) {
  if (!(std::abs(percent) >= negligible) || percent == 0) return {Tone::Plain, "~0%"};
  if (percent > 0) return {Tone::Better, "+" + num(percent, 1) + "% ^"};
  return {Tone::Worse, num(percent, 1) + "% v"};
}

}  // namespace gdc::tooltip
