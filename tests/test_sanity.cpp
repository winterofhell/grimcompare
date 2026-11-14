// Guards against wrong-sign scores: obvious swaps for plague-garden on a real snapshot (winterpoison, level
// 16, stage 10-35). Each case equips one hand-written item over a bare one in the same slot, so its score
// is the worth of exactly the stats named.
#include <doctest/doctest.h>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <functional>
#include <sstream>

#include "game_db.h"
#include "gdc/score.h"
#include "gdc/stat_model.h"

using namespace gdc;

namespace {

std::string read(const std::filesystem::path& p) {
  std::ifstream f(p);
  REQUIRE(f.good());
  std::stringstream ss;
  ss << f.rdbuf();
  return ss.str();
}

constexpr const char* kRing = "records/items/gearaccessories/rings/a002_ring02.dbr";  // no skill grants
constexpr const char* kChest = "records/items/geartorso/a02_torso003.dbr";

struct Bench {
  const arz::Database* db = game_db();
  score::Preset preset;
  snap::Snapshot ring, chest;  // the fixture with a bare ring on finger 1 / a bare chest

  Bench() {
    std::string error;
    auto p = score::parse(read(std::filesystem::path(GDC_SOURCE_DIR) / "presets/plague-garden.toml"), error);
    REQUIRE_MESSAGE(p, error);
    preset = *p;
    if (!db) return;
    const auto s = snap::from_json(read(std::filesystem::path(GDC_SOURCE_DIR) / "tests/fixtures/snapshots/lvl16-wp-before-belt.json"));
    ring = model::swap(s, item(4, kRing), 4, db, model::Mode::Expected).after;
    chest = model::swap(s, item(8, kChest), 8, db, model::Mode::Expected).after;
  }

  static snap::Item item(int location, const char* record) {
    snap::Item it;
    it.id = 900000 + static_cast<std::uint32_t>(location);
    it.record = record;
    it.kind = "ItemEquipment";
    it.stats.emplace();
    (void)location;
    return it;
  }

  // Score of equipping `it` over the bare item at `location` (4: finger 1, 8: chest).
  score::Result worth(const snap::Item& it, int location) const {
    const snap::Snapshot& from = location == 4 ? ring : chest;
    const auto& settings = score::settings(preset, from);
    const auto p = model::swap(from, it, location, db, model::Mode::Expected);
    return score::compare(from, p.after, score::main_skills(from, settings, db), settings, db);
  }

  score::Result ring_with(const std::function<void(snap::ItemStats&)>& stats, const char* record = kRing) const {
    snap::Item it = item(4, record);
    stats(*it.stats);
    return worth(it, 4);
  }
};

snap::CombatTotals damage_pct(int type, float direct, float dot) {
  snap::CombatTotals r;
  r.type = type;
  r.damage_modifier = direct;
  r.dot_modifier = dot;
  return r;
}

snap::CombatTotals defense(int type, float value) {
  snap::CombatTotals r;
  r.type = type;
  r.defense = value;
  return r;
}

double sum(const score::Result& r) {
  double s = 0;
  for (const auto& c : r.contributions) s += c.points;
  return s;
}

}  // namespace

TEST_CASE("sanity: obvious swaps for plague-garden have the obvious sign (installed game; skips if absent)") {
  const Bench b;
  if (!b.db) {
    MESSAGE("game not found; skipped");
    return;
  }
  REQUIRE(score::stage(b.preset, b.ring));
  CHECK(score::stage(b.preset, b.ring)->name == "10-35");
  auto dmg = [&](int type, float direct, float dot) {
    return b.ring_with([&](snap::ItemStats& s) { s.offensive_modifiers = {damage_pct(type, direct, dot)}; });
  };
  auto chr = [&](int type, float value, float modifier = 0) {
    return b.ring_with([&](snap::ItemStats& s) { s.char_attributes = {{type, value, modifier}}; });
  };
  auto def = [&](int type, float value) {
    return b.ring_with([&](snap::ItemStats& s) { s.defense = {defense(type, value)}; });
  };

  // Damage the build deals (Dreeg's Evil Eye: Acid + Poison; Devouring Swarm: Vitality + Bleeding).
  const auto acid = dmg(7, 30, 0), poison = dmg(7, 0, 30), fire = dmg(6, 30, 30), chaos = dmg(10, 30, 0);
  CHECK(acid.percent > 0);                         // 1  +% Acid
  CHECK(poison.percent > 0);                       // 2  +% Poison
  CHECK(acid.percent > fire.percent);              // 3  +% Acid ring vs +% Fire ring
  CHECK(poison.percent > fire.percent);            // 4  +% Poison ring vs +% Burn ring
  CHECK(dmg(7, 30, 0).percent > dmg(7, 10, 0).percent);  // 5  more is more
  CHECK(dmg(9, 30, 0).percent > 0);                // 6  +% Vitality (Devouring Swarm)
  CHECK(dmg(15, 0, 30).percent > 0);               // 7  +% Bleeding
  // The engine keeps direct and duration bonuses apart (docs/MECHANICS.md §3): +% Acid moves no Poison part,
  // so Acid + Poison on one ring is worth more than either.
  CHECK(dmg(7, 30, 30).percent > acid.percent);    // 8
  CHECK(dmg(7, 30, 30).percent > poison.percent);  // 9
  // Stats the build does not use score exactly 0.
  CHECK(fire.percent == 0);                        // 10 +% Fire / Burn
  CHECK(chaos.percent == 0);                       // 11 +% Chaos
  CHECK(fire.contributions.empty());
  CHECK(b.ring_with([](snap::ItemStats& s) { s.retaliation = {{6, false, 100, 100}}; }).percent == 0);  // 12 weight 0
  // +skills: Bloody Pox (learned) against Cadence + Aegis of Menhir (no points); only the records' grants
  // count here (f001a is an amulet record: it has no proc, unlike the +Cadence rings).
  const auto pox = b.ring_with([](snap::ItemStats&) {}, "records/items/faction/accessories/rings/f010a_ring.dbr");
  const auto cadence = b.ring_with([](snap::ItemStats&) {}, "records/items/faction/accessories/necklaces/f001a_necklace.dbr");
  CHECK(pox.percent > 0);                          // 13 +2 Bloody Pox
  CHECK(cadence.percent == 0);                     // 14 +2 to an unused skill
  CHECK(pox.percent > cadence.percent);            // 15
  // Offense through the sheet.
  CHECK(chr(21, 50).percent > 0);                  // 16 Offensive Ability
  CHECK(chr(3, 25).percent > 0);                   // 17 Spirit
  CHECK(chr(16, 0, 10).percent > 0);               // 18 cast speed
  CHECK(b.ring_with([](snap::ItemStats& s) { s.offensive_modifiers = {damage_pct(59, 0.15f, 0)}; }).percent > 0);  // 19 crit damage
  // Defense.
  CHECK(chr(4, 100).percent > 0);                  // 20 Life
  CHECK(chr(1, 10).percent > 0);                   // 21 Physique
  CHECK(chr(23, 50).percent > 0);                  // 22 Defensive Ability
  CHECK(def(4, 20).percent > 0);                   // 23 Pierce resist
  CHECK(def(10, 20).percent > 0);                  // 24 Chaos resist
  CHECK(def(2, 3).percent > 0);                    // 25 Physical resist
  CHECK(def(42, 20).percent > 0);                  // 26 stun resist
  CHECK(chr(27, 5).percent > 0);                   // 27 dodge
  CHECK(b.ring_with([](snap::ItemStats& s) { s.skill_attributes = {{1, 10, 0, 0, 0}}; }).percent > 0);  // 28 cooldown reduction
  // +% Poison duration comes from the record: the awakened Ring of Eternal Rot,
  // offensiveSlowPoisonDurationModifier 100.
  const arz::Record* rot = b.db->record("records/items/awakened/gearaccessories/rings/c053_ring.dbr");
  if (rot && rot->number("offensiveSlowPoisonDurationModifier") > 0) {
    const auto with = b.ring_with([](snap::ItemStats&) {}, "records/items/awakened/gearaccessories/rings/c053_ring.dbr");
    CHECK(with.percent > 0);                       // 30 +% Poison duration (and the ring's +skills / proc)
  }
  snap::Item plate = Bench::item(8, kChest);
  plate.stats->defense = {defense(38, 100)};
  CHECK(b.worth(plate, 8).percent > 0);            // 29 Armor
}

TEST_CASE("sanity: losing a resist below the target is negative and shows as the top part (installed game; skips if absent)") {
  const Bench b;
  if (!b.db) return;
  // Every base resist of the fixture is far below 80 + 25, so each lost point costs.
  for (const int type : {4, 5, 6, 7, 8, 9, 10, 11, 15}) {
    const auto r = b.ring_with([&](snap::ItemStats& s) { s.defense = {defense(type, -20)}; });
    CAPTURE(type);
    CHECK(r.percent < 0);
    REQUIRE(r.contributions.size() >= 2);
    CHECK(r.contributions[0].points < 0);
    CHECK(sum(r) == doctest::Approx(std::log(1 + r.percent / 100) * 100).epsilon(1e-6));
  }
  // With a gain on the same ring the total may be positive, but only with an explicit larger part.
  const auto mixed = b.ring_with([](snap::ItemStats& s) {
    s.defense = {defense(6, -5)};
    s.char_attributes = {{4, 400, 0}};
  });
  CHECK(mixed.percent > 0);
  REQUIRE(!mixed.contributions.empty());
  CHECK(mixed.contributions[0].label == "Life");
  double gain = 0, loss = 0;
  for (const auto& c : mixed.contributions) (c.points > 0 ? gain : loss) += c.points;
  CHECK(loss < 0);
  CHECK(gain > -loss);
}

TEST_CASE("sanity: adding a stat never lowers the score (installed game; skips if absent)") {
  const Bench b;
  if (!b.db) return;
  // Character attributes (CharAttributeType, value then %), defenses and damage % one at a time, in three sizes.
  for (const float size : {1.0f, 10.0f, 100.0f}) {
    for (int type = 1; type <= 60; ++type) {
      if (type == 8) continue;  // ManaLimitReserve: more reserved energy is worse
      for (const bool modifier : {false, true}) {
        const auto r = b.ring_with([&](snap::ItemStats& s) { s.char_attributes = {{type, modifier ? 0 : size, modifier ? size : 0}}; });
        CAPTURE(type);
        CAPTURE(modifier);
        CAPTURE(size);
        CHECK(r.percent >= -1e-9);
      }
    }
    for (const int type : {2, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 16, 38, 39, 40, 42, 43, 44, 45, 46, 48, 55, 56, 57}) {
      const auto r = b.ring_with([&](snap::ItemStats& s) { s.defense = {defense(type, size)}; });
      CAPTURE(type);
      CAPTURE(size);
      CHECK(r.percent >= -1e-9);
    }
    for (const int type : {2, 4, 5, 6, 7, 8, 9, 10, 11, 15, 58, 60}) {
      const auto r = b.ring_with([&](snap::ItemStats& s) { s.offensive_modifiers = {damage_pct(type, size, size)}; });
      CAPTURE(type);
      CAPTURE(size);
      CHECK(r.percent >= -1e-9);
    }
  }
}
