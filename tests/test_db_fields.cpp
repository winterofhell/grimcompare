// gdc::fields must be exactly the plain value/modifier rows of the engine-extracted TSVs.
#include <doctest/doctest.h>

#include <algorithm>
#include <fstream>
#include <map>
#include <set>
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
    if (t == combat_type.end()) continue;
    if (cls.starts_with("DefenseAttributeAbsMod_")) want.emplace_back(r[2], 1, t->second, true);
    else if (cls.starts_with("DefenseAttributeAbs_") || cls.starts_with("DefenseAttributeMisc_Block"))
      want.emplace_back(r[2], 1, t->second, false);
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
  // 0x200C30 / 0x200A70: flat entries on the base resists.
  CHECK(gdc::fields::resist_spread(60).size() == 10);
  CHECK(gdc::fields::resist_spread(58).size() == 3);
  CHECK(gdc::fields::resist_spread(5).empty());
  CHECK(gdc::fields::target("") == nullptr);
}

TEST_CASE("damage field tables match the extracted engine table") {
  std::set<std::tuple<std::string, int, bool>> mods_want;
  std::set<std::tuple<std::string, std::string, std::string, std::string, int>> flat_want = {
      {"offensivePhysicalMin", "offensivePhysicalMax", "", "offensivePhysicalChance", 2}};  // DamageAttribute_Physical: no load field in the table
  const std::set<int> types = {2, 4, 5, 6, 7, 8, 9, 10, 11, 15, 58, 60};
  std::set<std::tuple<std::string, std::string, std::string, std::string, int>> ret_want;
  for (const auto& r : rows("CombatAttributeType.tsv")) {
    if (r.size() < 3 || r[0] == "?" || !types.contains(std::stoi(r[0]))) continue;
    std::map<std::string, std::string> tag;
    std::istringstream in(r[2]);
    for (std::string t; in >> t;)
      if (const auto eq = t.find('='); eq != std::string::npos) tag[t.substr(0, eq)] = t.substr(eq + 1);
    const std::string& cls = r[1];
    const int type = std::stoi(r[0]);
    if (tag.contains("LoadModifier") && cls.starts_with("DamageAttributeAbsMod_")) mods_want.emplace(tag["LoadModifier"], type, false);
    if (tag.contains("LoadModifier") && cls.starts_with("DamageAttributeDurMod_")) mods_want.emplace(tag["LoadModifier"], type, true);
    if (tag.contains("LoadValueMin") && (cls.starts_with("DamageAttributeAbs_") || cls.starts_with("DamageAttributeAbsBase_")))
      flat_want.emplace(tag["LoadValueMin"], tag["LoadValueMax"], "", tag["LoadChance"], type);
    if (tag.contains("LoadValueMin") && cls.starts_with("DamageAttributeDur_"))
      flat_want.emplace(tag["LoadValueMin"], tag["LoadValueMax"], tag["LoadDurationMin"], tag["LoadChance"], type);
    if (type != 15 && tag.contains("LoadValueMin") && cls.starts_with("RetaliationAttributeAbs_"))
      ret_want.emplace("flat", tag["LoadValueMin"], tag["LoadValueMax"], tag["LoadChance"], type);
    if (type != 15 && tag.contains("LoadModifier") && cls.starts_with("RetaliationAttributeAbsMod_"))
      ret_want.emplace("mod", tag["LoadModifier"], "", "", type);
  }
  std::set<std::tuple<std::string, std::string, std::string, std::string, int>> ret_have;
  for (const auto& f : gdc::fields::retaliation()) {
    if (!f.min.empty()) ret_have.emplace("flat", std::string(f.min), std::string(f.max), std::string(f.chance), f.type);
    ret_have.emplace("mod", std::string(f.modifier), "", "", f.type);
  }
  CHECK(ret_have == ret_want);
  std::set<std::tuple<std::string, int, bool>> mods_have;
  std::vector<std::string> names;
  for (const auto& [f, t] : gdc::fields::damage_modifiers()) {
    CHECK(t.kind == gdc::fields::Kind::Damage);
    mods_have.emplace(std::string(f), t.type, t.modifier);
    names.emplace_back(f);
  }
  CHECK(std::is_sorted(names.begin(), names.end()));
  CHECK(mods_have == mods_want);
  std::set<std::tuple<std::string, std::string, std::string, std::string, int>> flat_have;
  for (const auto& f : gdc::fields::flat_damage())
    flat_have.emplace(std::string(f.min), std::string(f.max), std::string(f.duration), std::string(f.chance), f.type);
  CHECK(flat_have == flat_want);
  CHECK(gdc::fields::damage_modifier("offensiveSlowPoisonModifier")->modifier);
  CHECK(gdc::fields::spread(58, false).size() == 3);
  double scale = 0;
  const auto* crit = gdc::fields::lookup("offensiveCritDamageModifier", scale);
  REQUIRE(crit);
  CHECK(crit->type == 59);
  CHECK(scale == doctest::Approx(0.01));
  CHECK(gdc::fields::lookup("offensiveFireModifier", scale) == gdc::fields::damage_modifier("offensiveFireModifier"));
  CHECK(scale == 1);
  CHECK(!gdc::fields::lookup("offensiveNothing", scale));
}
