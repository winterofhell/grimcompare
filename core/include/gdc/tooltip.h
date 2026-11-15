#pragma once
// ASCII only: every English font (Fonts.arc *.fnt; GraphicsFont2::Initialize 0xAFC90) has exactly
// U+0020..007E per style, so "->" and " - " instead of arrows and dashes. Layout examples: tests/test_tooltip.cpp.
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "gdc/labels.h"
#include "gdc/score.h"
#include "gdc/stat_model.h"

namespace gdc::tooltip {

enum class Tone : std::uint8_t { Heading, Plain, Better, Worse, Warning, Detail };

struct Line {
  Tone tone = Tone::Plain;
  std::string text;
  bool operator==(const Line&) const = default;
};

// EquipmentCtrl::ValidateItemType 0x288D30: location -> required type. Type 8 (shield / off-hand) -> L Hand;
// type 9 (weapon) -> R Hand and L Hand.
std::vector<int> locations_for(int item_type);

Line change(double percent, double negligible);

std::vector<Line> changes(const model::Sheet& before, const model::Sheet& after, const model::Prediction& p,
                          const Labels& labels = {}, const score::Result* scored = nullptr);

// GraphicsFont2::ProcessCharacter (Engine.dll 0xB0C00) reads pairs: "^" + letter is a colour, "^" + anything
// else draws neither, "&" + B / I / S sets the style. "^^" and "&&" draw one, so both are doubled. "{" and "}"
// are never drawn and become "(" and ")".
std::string escape(std::string_view text);

// GraphicsEngine::GetUIScaleFactor and GetWidth, read at run time.
struct Screen {
  float ui_scale = 1.0f;
  int width = 0;
  bool operator==(const Screen&) const = default;
};

// Item tooltip (Grim Dawn.exe provider 0x1EE9F0, layout 0x236020, mode 1) is as wide as a sample in the default
// style (jura 16, StyleManager::LoadStyle): "The quick brown fox jumped over the lazy dog The quick" at >= 1600 px,
// else "The quick brown fox jumped over the lazy". GrayStats1 has no indent (gameengine.dbr).
bool fits(const Screen& screen, std::string_view line);
// Breaks only between items; an item too wide for a line gets its own.
std::vector<std::string> pack(const std::string& head, const std::string& indent, const std::vector<std::string>& items,
                              const Screen& screen);

std::vector<Line> buff_lines(const score::BuffView& before, const score::BuffView& after, const Labels& labels,
                             const Screen& screen);

struct Scored {
  int location = -1;
  int also_off = -1;   // two-handed <-> one-handed + off-hand
  double percent = 0;
  std::optional<double> itself, moved;
};

// Empty when the item is not equippable or already equipped. Both sides come from model::expected(s).
std::vector<Line> compare(const snap::Snapshot& s, const snap::Item& candidate, int item_type, bool detail,
                          const arz::Database* db, const score::Preset& preset, const Labels& labels = {},
                          std::vector<std::string>* log = nullptr, std::vector<Scored>* scored = nullptr,
                          const Screen& screen = {});

}  // namespace gdc::tooltip
