#pragma once
// Wizard-written preset bound to characters (docs/PRESETS.md, §13.30).
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "catalog.h"
#include "gdc/score.h"

namespace setup {

inline constexpr std::string_view kSetupMark = "# Written by gdcompare-setup.";

enum class Playstyle { kBalanced, kTanky, kGlass };

struct BuildAnswers {
  std::string title;                    // shown in comments and the summary
  std::string source;                   // catalog id, or "custom"
  std::vector<std::string> characters;  // [match] characters
  std::vector<std::string> saves;       // [match] saves (snap::save_key) of those names
  std::vector<int> masteries;
  std::vector<std::string> damage;      // [match] damage names
  std::vector<std::string> skills;      // [damage] skills (already filtered to the masteries)
  std::vector<std::string> sets;        // [preset] target_sets
  std::map<std::string, gdc::score::Weight> other;
  Playstyle playstyle = Playstyle::kBalanced;
  bool pets = false;
  // Advanced numbers by "<table>.<key>" ("damage.kills_per_sec", "thresholds.resist", "score.other"); unset = default.
  std::map<std::string, double> advanced;
  std::vector<std::string> hints;       // comment lines (conversion, key stats, items)
};

// [score] damage / survival exponents per playstyle (design choice).
std::pair<double, double> playstyle_weights(Playstyle p);

// Catalog answers: skills kept if mastery's or the default attack.
BuildAnswers from_entry(const Entry& e, const std::vector<int>& masteries, const std::string& character);

std::string preset_text(const BuildAnswers& a);

// "Your top priorities", most important first; skill_names per record.
std::vector<std::string> priorities(const BuildAnswers& a, const std::map<std::string, std::string>& skill_names);

// File stem from a build name: [A-Za-z0-9_-], else '-'.
std::string preset_stem(std::string_view name);

// For setup's presets: rewrites the [match] characters and saves lines.
bool made_by_setup(std::string_view text);
std::optional<std::string> with_binding(std::string_view text, const std::vector<std::string>& characters,
                                        const std::vector<std::string>& saves);

std::string toml_string(std::string_view s);

}  // namespace setup
