#pragma once
// Tooltip block text for one hovered item (PROMPT.md Phase 4, score v1). ASCII only: the game's English
// fonts (Fonts.arc, FNTX) are assumed to cover U+0020..007E only (unverified: the format is not parsed), so no
// arrows; the heading uses " - ", not " | ".
//   hover  "GD Compare", then per location (rings: both fingers, the better one marked):
//            "vs Finger 1: +6.4% ^ (best)"            base item vs base item (components and augments
//                                                     removed from both)
//            "  your Frozen Heart, Exile's Riftstorm moved: +7.1% ^"   candidate base + your current
//                                                     item's component / augment (those that fit) vs now
//          then "+? unscored: ..." and "! model inaccurate (Shift: why)".
//   Shift  the same, plus per location the score terms (Damage / Survivability / Other) of both lines and
//          every changed sheet value of the item as hovered (changes()), and the warnings.
// Text only; the adapter maps tones to GameTextClass values (re/GameTextClass.tsv) and builds the lines.
#include <cstdint>
#include <string>
#include <vector>

#include "gdc/labels.h"
#include "gdc/score.h"
#include "gdc/stat_model.h"

namespace gdc::tooltip {

enum class Tone : std::uint8_t { Heading, Plain, Better, Worse, Warning, Detail };

struct Line {
  Tone tone = Tone::Plain;
  std::string text;  // ASCII
  bool operator==(const Line&) const = default;
};

// Equip locations an item of engine type `item_type` (Item::GetItemType, vt+0x550) can go to, from
// EquipmentCtrl::ValidateItemType (0x288D30): location -> required type. Type 8 (shield / caster off-hand)
// -> L Hand. Empty: not equippable, a type with no location there, or a weapon (type 9 -> R Hand): weapons
// are not compared, the game compares them itself.
std::vector<int> locations_for(int item_type);

// "+6.4% ^", "-2.1% v", or "~0%" when |percent| < negligible; the tone to show it in.
Line change(double percent, double negligible);

// Every changed sheet value, skill level and set count (with its tier change, `gdc::sets`) between `before` and `after` (Shift view).
std::vector<Line> changes(const model::Sheet& before, const model::Sheet& after, const model::Prediction& p,
                          const Labels& labels = {});

// The whole block for `candidate` (with stats) against snapshot `s`. Empty when the item is not
// equippable, a weapon, or one of the equipped items; a shield / off-hand next to a two-handed weapon gets
// a "not compared" line. `log` (if given) gets each score with its inputs and
// every warning, prefixed with the location. Scores with the preset's stage for `s` (score::stage); the
// heading names the preset and stage when the preset has a name.
std::vector<Line> compare(const snap::Snapshot& s, const snap::Item& candidate, int item_type, bool detail,
                          const arz::Database* db, const score::Preset& preset, const Labels& labels = {},
                          std::vector<std::string>* log = nullptr);

}  // namespace gdc::tooltip
