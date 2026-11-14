// Every name in gdc::names must be (the end of) an exported class name carrying that enum value
// in re/*AttributeType.tsv, and every value the TSVs list must have a name.
#include <doctest/doctest.h>

#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string>

#include "gdc/attribute_names.h"

namespace {

// value -> class names, from "value\tclass\t..." lines (header and '?' rows skipped)
std::map<int, std::set<std::string>> load(const char* file) {
  std::ifstream f(std::string(GDC_SOURCE_DIR) + "/re/" + file);
  REQUIRE(f.good());
  std::map<int, std::set<std::string>> m;
  std::string line;
  std::getline(f, line);
  while (std::getline(f, line)) {
    std::istringstream row(line);
    std::string value, cls;
    std::getline(row, value, '\t');
    std::getline(row, cls, '\t');
    if (value.empty() || value == "?") continue;
    m[std::stoi(value)].insert(cls);
  }
  return m;
}

bool ends_with(const std::string& s, const std::string& suffix) {
  return s.size() >= suffix.size() && s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

}  // namespace

TEST_CASE("CharAttributeType names match exported classes") {
  const auto tsv = load("CharAttributeType.tsv");
  for (int t = 0; t <= gdc::names::kCharAttributeMax; ++t) {
    const std::string name(gdc::names::char_attribute(t));
    CAPTURE(t);
    CAPTURE(name);
    const auto it = tsv.find(t);
    if (it == tsv.end()) {
      CHECK(name.empty());
      continue;
    }
    REQUIRE(!name.empty());
    CHECK((it->second.count("CharAttributeVal_" + name) + it->second.count("CharAttributeMod_" + name)) > 0);
  }
  CHECK(gdc::names::char_attribute(-1).empty());
  CHECK(gdc::names::char_attribute(gdc::names::kCharAttributeMax + 1).empty());
  CHECK(tsv.rbegin()->first == gdc::names::kCharAttributeMax);
}

TEST_CASE("CombatAttributeType names match exported classes") {
  const auto tsv = load("CombatAttributeType.tsv");
  for (int t = 0; t <= gdc::names::kCombatAttributeMax; ++t) {
    const std::string name(gdc::names::combat_attribute(t));
    CAPTURE(t);
    CAPTURE(name);
    const auto it = tsv.find(t);
    if (it == tsv.end()) {
      CHECK(name.empty());
      continue;
    }
    REQUIRE(!name.empty());
    bool found = false;
    for (const auto& cls : it->second) found = found || ends_with(cls, name);
    CHECK(found);
  }
  CHECK(gdc::names::combat_attribute(gdc::names::kCombatAttributeMax + 1).empty());
  CHECK(tsv.rbegin()->first == gdc::names::kCombatAttributeMax);
}

TEST_CASE("enum names from engine string functions") {
  using namespace gdc::names;
  CHECK(skill_attribute(1) == "CooldownReduction");
  CHECK(skill_attribute(4) == "ComboChargeSpendReduction");
  CHECK(skill_attribute(0).empty());
  CHECK(skill_attribute(5).empty());
  CHECK(difficulty(0) == "Normal");
  CHECK(difficulty(2) == "Ultimate");
  CHECK(difficulty(3).empty());
  CHECK(difficulty(-1).empty());
  CHECK(equip_location(0) == "R Hand");
  CHECK(equip_location(4) == "Finger");
  CHECK(equip_location(5) == "Finger");
  CHECK(equip_location(13) == "Artifact");
  CHECK(equip_location(14) == "Clothing");
  CHECK(equip_location(15).empty());
}
