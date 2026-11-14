// gdc::fields must be exactly the plain value/modifier rows of the engine-extracted TSVs.
#include <doctest/doctest.h>

#include <algorithm>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <tuple>
#include <vector>

#include "gdc/db_fields.h"

namespace {

std::vector<std::vector<std::string>> rows(const char* file) {
  std::ifstream f(std::string(GDC_SOURCE_DIR) + "/re/" + file);
  REQUIRE(f.good());
  std::vector<std::vector<std::string>> out;
  std::string line;
  std::getline(f, line);  // header
  while (std::getline(f, line)) {
    std::vector<std::string> cols;
    std::istringstream row(line);
    for (std::string c; std::getline(row, c, '\t');) cols.push_back(c);
    out.push_back(cols);
  }
  return out;
}

using Key = std::tuple<std::string, int, int, bool>;  // field, kind, type, modifier

}  // namespace

TEST_CASE("database field table matches the extracted engine tables") {
  std::map<std::string, int> combat_type;
  for (const auto& r : rows("CombatAttributeType.tsv"))
    if (r.size() >= 2 && r[0] != "?") combat_type[r[1]] = std::stoi(r[0]);

  std::vector<Key> want;
  for (const auto& r : rows("DefenseAttributeFields.tsv")) {
    REQUIRE(r.size() == 3);
    const std::string& cls = r[0];
    const auto t = combat_type.find(cls);
    if (t == combat_type.end() || t->second == 58 || t->second == 60) continue;
    if (cls.starts_with("DefenseAttributeAbsMod_")) want.emplace_back(r[2], 1, t->second, true);
    else if (cls.starts_with("DefenseAttributeAbs_")) want.emplace_back(r[2], 1, t->second, false);
  }
  for (const auto& r : rows("CharAttributeType.tsv")) {
    REQUIRE(r.size() >= 3);
    if (r[1] == "CharAttributeMod_LifeMultiplier") continue;
    if (r[1].starts_with("CharAttributeVal_")) want.emplace_back(r[2], 0, std::stoi(r[0]), false);
    else if (r[1].starts_with("CharAttributeMod_")) want.emplace_back(r[2], 0, std::stoi(r[0]), true);
  }
  std::vector<Key> have;
  for (const auto& [f, t] : gdc::fields::all())
    have.emplace_back(std::string(f), t.kind == gdc::fields::Kind::Defense, t.type, t.modifier);
  std::sort(want.begin(), want.end());
  CHECK(have == want);  // also: sorted, which target() relies on

  const auto* chaos = gdc::fields::target("defensiveChaos");
  REQUIRE(chaos);
  CHECK(chaos->kind == gdc::fields::Kind::Defense);
  CHECK(chaos->type == 10);
  CHECK(!chaos->modifier);
  const auto* prot = gdc::fields::target("defensiveProtectionModifier");
  REQUIRE(prot);
  CHECK(prot->type == 40);
  CHECK(prot->modifier);
  CHECK(gdc::fields::target("defensiveChaosMaxResist") == nullptr);
  CHECK(gdc::fields::target("") == nullptr);
}
