#pragma once
// catalog/builds.toml: the wizard's builds, one Entry per variant.
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "gdc/score.h"
#include "gdc/text.h"

namespace setup {

inline constexpr std::string_view kCategories[] = {"melee", "ranged", "caster", "pets", "hybrid"};

struct Entry {
  std::string id;  // build id, "/<n>" appended for its n-th variant
  std::string name;
  std::string category;
  std::vector<std::string> classes;  // "XXYY"
  std::vector<std::string> damage;   // [match] damage names
  std::vector<std::string> skills, sets;
  std::map<std::string, gdc::score::Weight> other;
  std::string conversion, stats, items;
};

std::optional<std::vector<Entry>> load_catalog(std::string_view toml, std::string& error);

// Mastery numbers of a class code: "0306" -> {3, 6}.
std::vector<int> code_masteries(std::string_view code);
// Entry fits the class, or its one mastery; none: all.
bool fits(const Entry& e, const std::vector<int>& masteries);

std::string mastery_record(int mastery);  // records/skills/playerclassNN/_classtraining_classNN.dbr
// Class or mastery name from Text_EN, gender forms joined (§13.31).
std::string class_name(const gdc::text::Table& text, std::string_view code);
std::string class_code(std::vector<int> masteries);  // {6, 3} -> "0306"

}  // namespace setup
