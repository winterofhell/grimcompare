#pragma once
// ASCII only: game fonts have just U+0020..007E.
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

// Slot -> item type, as ValidateItemType 0x288D30.
std::vector<int> locations_for(int item_type);

Line change(double percent, double negligible);

std::vector<Line> changes(const model::Sheet& before, const model::Sheet& after, const model::Prediction& p,
                          const Labels& labels = {}, const score::Result* scored = nullptr);

// Escapes ^ & { } as ProcessCharacter would draw them.
std::string escape(std::string_view text);

// GraphicsEngine::GetUIScaleFactor and GetWidth, read at run time.
struct Screen {
  float ui_scale = 1.0f;
  int width = 0;
  bool operator==(const Screen&) const = default;
};

// Tooltip width from a sample sentence in jura 16.
bool fits(const Screen& screen, std::string_view line);
// Breaks only between items; overlong items get their own line.
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

// Empty when not equippable or equipped; sides from model::expected.
std::vector<Line> compare(const snap::Snapshot& s, const snap::Item& candidate, int item_type, bool detail,
                          const arz::Database* db, const score::Preset& preset, const Labels& labels = {},
                          std::vector<std::string>* log = nullptr, std::vector<Scored>* scored = nullptr,
                          const Screen& screen = {});

}  // namespace gdc::tooltip
