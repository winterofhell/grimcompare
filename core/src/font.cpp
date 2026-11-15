#include "gdc/font.h"

#include <climits>
#include <cmath>
#include <cstdlib>
#include <span>

namespace gdc::font {
namespace {

struct Glyph {
  std::int8_t bearing;   // FNTMetric +2
  std::int8_t after;     // FNTMetric +4
  std::uint8_t advance;  // FNTMetric +6
};
struct Style {
  int size;          // Style+0x98
  Glyph glyphs[96];  // U+0000, then U+0020..007E
};

#include "font_tables.inc"

std::span<const Style> styles(Face f) {
  if (f == Face::Jura) return kJura;
  return kLinBiolinumSans;
}

const Glyph& glyph(const Style& s, char c) {
  const auto u = static_cast<unsigned char>(c);
  return u >= 0x20 && u <= 0x7E ? s.glyphs[u - 0x20 + 1] : s.glyphs[0];
}

}  // namespace

int pixel_size(int points, float ui_scale) {
  if (points < 1) points = 1;
  if (ui_scale == 1.0f) return points;
  const int px = static_cast<int>(std::ceil(static_cast<float>(points) * ui_scale));
  return px < 8 ? 8 : px;
}

int width(Face face, int size, std::string_view text) {
  const Style* style = nullptr;
  int best = INT_MAX;
  for (const Style& s : styles(face))
    if (const int d = std::abs(size - s.size); d < best) {
      best = d;
      style = &s;
    }
  const float scale = static_cast<float>(size) / static_cast<float>(style->size);
  int x = 0, after = 0;
  for (std::size_t i = 0; i < text.size(); ++i) {
    const Glyph& g = glyph(*style, text[i]);
    if (i > 0) {
      if (text[i] != ' ' || text[i - 1] == ' ') x += after;
      if (g.bearing < 0 && x < -g.bearing) x = -g.bearing;
      x += g.bearing;
    }
    x += static_cast<int>(std::floor(static_cast<float>(g.advance) * scale + 0.5f));
    after = g.after;
  }
  return x + after;
}

}  // namespace gdc::font
