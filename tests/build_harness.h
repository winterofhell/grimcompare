#pragma once
// A synthetic level 96 Ultimate character for preset tests: winterofhell's engine totals
// (lvl96-s5-before-ring-c031.json) with that character's skills, conversions and item stats removed. Every check
// equips one hand-written item over a bare one, so its score is the worth of exactly the stats named.
#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>
#include <functional>
#include <sstream>

#include "game_db.h"
#include "gdc/score.h"
#include "gdc/stat_model.h"

namespace gdc::test {

namespace fs = std::filesystem;

inline constexpr const char* kRing = "records/items/gearaccessories/rings/a002_ring02.dbr";  // no grants
inline constexpr const char* kNeck = "records/items/gearaccessories/necklaces/a00_necklace.dbr";
inline constexpr const char* kChest = "records/items/geartorso/a02_torso003.dbr";
inline constexpr const char* kSword = "records/items/gearweapons/swords1h/a00_sword001.dbr";  // WeaponMelee_Sword
inline constexpr const char* kGun = "records/items/gearweapons/guns2h/a01_gun2h000.dbr";      // WeaponHunting_Ranged2h
inline constexpr int kFinger = 4, kNeckSlot = 2, kChestSlot = 8;

inline std::string read(const fs::path& p) {
  std::ifstream f(p);
  REQUIRE(f.good());
  std::stringstream ss;
  ss << f.rdbuf();
  return ss.str();
}

inline snap::Item bare(int location, const char* record) {
  snap::Item it;
  it.id = 900000 + static_cast<std::uint32_t>(location);
  it.location = location;
  it.record = record;
  it.kind = "ItemEquipment";
  it.stats.emplace();
  return it;
}

inline snap::CombatTotals pct(int type, float direct, float dot) {
  snap::CombatTotals r;
  r.type = type;
  r.damage_modifier = direct;
  r.dot_modifier = dot;
  return r;
}

inline snap::CombatTotals resist(int type, float value) {
  snap::CombatTotals r;
  r.type = type;
  r.defense = value;
  return r;
}

inline score::Preset parse_preset(const std::string& text) {
  std::string error;
  auto p = score::parse(text, error);
  REQUIRE_MESSAGE(p, error);
  return *p;
}

struct Build {
  const arz::Database* db = game_db();
  score::Preset preset;
  snap::Snapshot s;

  Build(const char* preset_file, const char* weapon) : Build(parse_preset(read(fs::path(GDC_SOURCE_DIR) / "presets" / preset_file)), weapon) {}
  Build(score::Preset p, const char* weapon) : preset(std::move(p)) {
    s = snap::from_json(read(fs::path(GDC_SOURCE_DIR) / "tests/fixtures/snapshots/lvl96-s5-before-ring-c031.json"));
    s.character_name = "synthetic";
    s.skills.clear();
    s.item_skills.clear();
    s.item_skill_modifiers.clear();
    s.skill_attributes.clear();
    s.conversions.clear();
    snap::Item hand = bare(0, weapon);
    hand.stats->offensive_damage.push_back({1, false, 300, 500});  // base Physical 300-500 (kBasePhysical)
    s.equipped = {hand, bare(kNeckSlot, kNeck), bare(kFinger, kRing), bare(kChestSlot, kChest)};
    // One base resist under the cap (Pierce 30 of 80) and stun resist 40 (190 on the sheet: over its cap of 80),
    // so those are worth something.
    for (auto& d : s.defense)
      if (d.type == 4 || d.type == 42) d.defense = d.processed = d.type == 4 ? 30 : 40;
    learn("records/skills/default/defaultweaponattack.dbr", 1);  // every character has it
  }

  snap::Skill& learn(const char* record, std::uint32_t points) {
    snap::Skill k;
    k.id = static_cast<std::uint32_t>(s.skills.size() + 1);
    k.record = record;
    k.level = k.current_level = points;
    // GetMasteryId: the learned mastery of the same class directory (SetMasteryAugment matches on it).
    const std::string_view r(record);
    for (const auto& m : s.skills)
      if (m.record.find("/_classtraining_") != std::string::npos &&
          r.substr(0, r.rfind('/')) == std::string_view(m.record).substr(0, m.record.rfind('/')))
        k.mastery_id = m.id;
    s.skills.push_back(k);
    return s.skills.back();
  }
  // An active toggle or aura as the capture reads it: its collectors are present (score.cpp skill_on).
  void toggle_on(snap::Skill& k) {
    k.passive.emplace();
    k.passive->defense = {resist(2, 0)};
  }

  score::Result worth(const snap::Item& it) const {
    const auto& settings = score::settings(preset, s);
    const auto p = model::swap(s, it, it.location, db);
    return score::compare(s, p.after, score::main_skills(s, settings, db), settings, db);
  }
  double ring(const std::function<void(snap::ItemStats&)>& stats, const char* record = kRing) const {
    snap::Item it = bare(kFinger, record);
    stats(*it.stats);
    return worth(it).percent;
  }
  double neck(const char* record) const { return worth(bare(kNeckSlot, record)).percent; }
  double dmg(int type, float direct, float dot) const {
    return ring([&](snap::ItemStats& x) { x.offensive_modifiers = {pct(type, direct, dot)}; });
  }
  double chr(int type, float value, float modifier = 0) const {
    return ring([&](snap::ItemStats& x) { x.char_attributes = {{type, value, modifier}}; });
  }
  double def(int type, float value) const {
    return ring([&](snap::ItemStats& x) { x.defense = {resist(type, value)}; });
  }
  double convert(int from, int to, float share) const {
    return ring([&](snap::ItemStats& x) { x.conversions = {{from, to, share}}; });
  }
  std::vector<score::Conversion> warnings(int from, int to, float share) const {
    snap::Item it = bare(kFinger, kRing);
    it.stats->conversions = {{from, to, share}};
    const auto p = model::swap(s, it, kFinger, db);
    return score::conversion_warnings(s, p.after, score::settings(preset, s), preset.match_damage, db);
  }

  // Defenses every build wants, whatever it deals, plus +1 to all skills (needs a learned class skill: the bonus does
  // not raise the mastery bar).
  void common() const {
    defenses();
    CHECK(neck("records/items/gearaccessories/necklaces/b201c_necklace.dbr") > 0);  // +1 to all skills
  }
  void defenses() const {
    CHECK(chr(4, 300) > 0);           // Life
    CHECK(chr(1, 20) > 0);            // Physique
    CHECK(chr(23, 60) > 0);           // Defensive Ability
    CHECK(def(4, 20) > 0);            // Pierce resist under the cap
    CHECK(def(4, 20) > def(10, 20));  // under the cap beats overcap
    CHECK(def(10, 20) >= 0);          // Chaos resist at the cap: overcap only
    CHECK(def(2, 4) > 0);             // Physical resist
    CHECK(def(42, 20) > 0);           // stun resist
    CHECK(chr(27, 5) > 0);            // dodge
    snap::Item plate = bare(kChestSlot, kChest);
    plate.stats->defense = {resist(38, 200)};
    CHECK(worth(plate).percent > 0);  // Armor
    CHECK(def(4, -20) < 0);           // a resist malus
    CHECK(chr(4, -300) < 0);          // a life malus
  }

  // The defenses keep their sign on every difficulty and at every level (enemy tables, resist penalties, lookahead).
  void sweep() {
    const auto level = s.level;
    const int difficulty = s.difficulty;
    for (int d : {0, 1, 2})
      for (std::uint32_t l : {20u, 50u, 70u, 90u, 100u}) {
        s.difficulty = d;
        s.level = l;
        INFO("difficulty ", d, " level ", l);
        CHECK(chr(4, 300) > 0);
        CHECK(chr(23, 60) >= 0);  // 0 once the enemies' hit chance is at its floor (a level 96 sheet at 20)
        CHECK(def(4, 20) > 0);
        CHECK(def(4, 20) >= def(10, 20));
        CHECK(def(10, 20) >= 0);
        CHECK(def(4, -20) < 0);
        CHECK(chr(21, 80) >= 0);
        snap::Item plate = bare(kChestSlot, kChest);
        plate.stats->defense = {resist(38, 200)};
        CHECK(worth(plate).percent > 0);
      }
    s.level = level;
    s.difficulty = difficulty;
  }
};


}  // namespace gdc::test
