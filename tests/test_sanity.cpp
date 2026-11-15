// Guards against wrong-sign scores: obvious swaps for plague-garden on a real snapshot (winterpoison, level
// 16, stage 10-35). Each case equips one hand-written item over a bare one in the same slot, so its score
// is the worth of exactly the stats named.
#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <functional>
#include <sstream>

#include "game_db.h"
#include "gdc/score.h"
#include "gdc/stat_model.h"
#include "gdc/tooltip.h"

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
    ring = model::swap(s, item(4, kRing), 4, db).after;
    chest = model::swap(s, item(8, kChest), 8, db).after;
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
    const auto p = model::swap(from, it, location, db);
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

namespace {

const score::Contribution* part(const score::Result& r, const char* label) {
  for (const auto& c : r.contributions)
    if (c.label == label) return &c;
  return nullptr;
}

constexpr const char* kRingOfSteel = "records/skills/itemskills/item_ringofsteel.dbr";

}  // namespace

TEST_CASE("sanity: a granted attack proc is scored: damage and crowd control (installed game; skips if absent)") {
  const Bench b;
  if (!b.db) return;
  // Brigandine Armor "of Balanced Steel" without its stats: Ring of Steel, 20% when hit, 3 s recharge, 15%
  // weapon damage + Pierce, 1 s stun (the 0.9.0 in-game screenshot).
  snap::Item chest = Bench::item(8, "records/items/geartorso/a03_torso001b.dbr");
  chest.replica.emplace().suffix = "records/items/lootaffixes/suffix/b_ar026_ar.dbr";
  const auto r = b.worth(chest, 8);
  CHECK(r.procs.contains(kRingOfSteel));
  CHECK(r.unscored.empty());
  CHECK(r.ratio.damage > 1);  // Pierce is not the build's type, but it is still damage
  CHECK(r.ratio.damage < 1.05);
  REQUIRE(part(r, "Proc crowd control"));
  CHECK(part(r, "Proc crowd control")->points > 0);
  // The stun is worth at most cc_share of the incoming damage, and nothing at cc_share 0.
  CHECK(r.ratio.survival > 1);
  CHECK(r.ratio.survival < 1 / (1 - 0.1));
  CHECK(sum(r) == doctest::Approx(std::log(1 + r.percent / 100) * 100).epsilon(1e-6));
  Bench none = b;
  for (auto& st : none.preset.stages) st.settings.cc_share = 0;
  CHECK(none.worth(chest, 8).ratio.survival == doctest::Approx(1));
  // More hits taken -> it procs more often.
  Bench busy = b;
  for (auto& st : busy.preset.stages) st.settings.incoming_hits_per_sec *= 3;
  CHECK(busy.worth(chest, 8).percent > r.percent);
}

TEST_CASE("sanity: granted defensive procs raise Survivability through their uptime (installed game; skips if absent)") {
  const Bench b;
  if (!b.db) return;
  auto bare = [&](const Bench& x, const char* record) { return x.ring_with([](snap::ItemStats&) {}, record); };
  // Each item is equipped without its own stats, so the score is the granted skill's alone.
  constexpr const char* kWard = "records/items/gearaccessories/necklaces/c018_necklace.dbr";   // timed buff, 30% when hit
  constexpr const char* kHeal = "records/items/gearaccessories/necklaces/d201_necklace.dbr";   // heal, 30% when hit
  constexpr const char* kStone = "records/items/gearaccessories/necklaces/d117_necklace.dbr";  // flat absorption, 10% when hit
  constexpr const char* kWill = "records/items/gearweapons/shields/d004_shield.dbr";           // 30% absorption at low health
  for (const char* record : {kWard, kHeal, kStone, kWill}) {
    CAPTURE(std::string(record));
    const auto r = bare(b, record);
    CHECK(r.ratio.survival > 1);
    CHECK(r.percent > 0);
    CHECK(r.unscored.empty());
    CHECK(r.procs.size() == 1);
  }
  // Absorption and heals have their own part; a buff's stats move the parts they belong to.
  CHECK(part(bare(b, kHeal), "Proc absorb / heal"));
  CHECK(part(bare(b, kStone), "Proc absorb / heal"));
  CHECK(part(bare(b, kWill), "Proc absorb / heal"));
  CHECK(!part(bare(b, kWard), "Proc absorb / heal"));
  // Uptime follows the trigger rate: no hits, no "when hit" procs; never at low health, no low-health proc.
  // The effect is then named as unscored instead of counting 0 silently.
  Bench calm = b;
  for (auto& st : calm.preset.stages) st.settings.incoming_hits_per_sec = 0, st.settings.low_health_per_sec = 0;
  for (const char* record : {kWard, kHeal, kStone, kWill}) {
    CAPTURE(std::string(record));
    const auto r = bare(calm, record);
    CHECK(r.ratio.survival == doctest::Approx(1));
    CHECK(r.unscored.size() == 1);
  }
  Bench busy = b;
  for (auto& st : busy.preset.stages) st.settings.incoming_hits_per_sec *= 3;
  CHECK(bare(busy, kWard).ratio.survival > bare(b, kWard).ratio.survival);
  // A 30% absorption that is up only after a low-health trigger is worth less than 30% all the time.
  CHECK(bare(b, kWill).ratio.survival < 1 / 0.7);
}

namespace {

constexpr const char* kDreeg = "records/skills/playerclass03/bloodofdreeg1.dbr";

// winterpoison at level 22 with Blood of Dreeg running (schema 8: buff_active).
snap::Snapshot level22() {
  return snap::from_json(read(std::filesystem::path(GDC_SOURCE_DIR) / "tests/fixtures/snapshots/lvl22-wp-dreeg-on.json"));
}

double poison(const model::Sheet& sh) {
  for (const auto& r : sh.resists)
    if (r.type == 7) return r.uncapped;
  return 0;
}

}  // namespace

TEST_CASE("sanity: only class skills are timed buffs, at duration / (recharge x (1 - CDR)) (installed game; skips if absent)") {
  const arz::Database* db = game_db();
  if (!db) return;
  const snap::Snapshot raw = level22();
  const snap::Snapshot s = model::expected(raw, db);
  CHECK(snap::to_json(model::expected(s, db)) == snap::to_json(s));  // idempotent
  const score::Settings p;
  const auto skills = score::main_skills(s, p, db);
  const score::BuffView v = score::buff_view(s, skills, p, db);
  // Blood of Dreeg rank 3: 60 s every 12 s x (1 - 12% from the off-hand). The 50 consumables in the skill list
  // (resist potions: skillActiveDuration 900, no recharge) are not buffs: Poison & Acid stays the engine's 59.
  REQUIRE(v.buffs.size() == 1);
  CHECK(v.buffs[0].record == kDreeg);
  CHECK(v.buffs[0].uptime == 1);
  CHECK(v.buffs[0].basis == "60.0 s / (12.0 s x (1 - 12% cooldown reduction))");
  CHECK(poison(v.sheet) == 59);
  CHECK(v.sheet.oa == doctest::Approx(model::sheet(raw).oa));  // out and back in: the engine's sheet
  const double oa = v.buffs[0].on.oa - v.buffs[0].off.oa;  // bloodofdreeg1_buff.dbr rank 3: 26 x the sheet's OA %
  CHECK(oa >= 26);
  CHECK(oa < 28);
  // A longer recharge than duration: 60 / (100 x 0.88).
  for (const auto& k : s.skills)
    if (k.record == kDreeg) {
      const auto t = model::buff_timing(k, *db);
      REQUIRE(t);
      CHECK(std::min(1.0, t->duration / (100 * (1 - 0.12))) == doctest::Approx(0.6818).epsilon(0.001));
    }
}

TEST_CASE("sanity: a resist that only matters while a buff is down scores between the buff's two states (installed game; skips if absent)") {
  const arz::Database* db = game_db();
  if (!db) return;
  snap::Snapshot s = model::expected(level22(), db);
  // Aspect of the Guardian (bloodofdreeg2: Poison & Acid resist while the buff is up) learned.
  for (auto& k : s.skills)
    if (k.record.ends_with("/bloodofdreeg2.dbr")) k.level = k.current_level = 5;
  score::Settings p;
  p.recast_buffs = false;
  p.uptime[kDreeg] = 0.5;
  const auto skills = score::main_skills(s, p, db);
  const auto dreeg = score::buff_view(s, skills, p, db).buffs.at(0);
  const double gain = poison(dreeg.on) - poison(dreeg.off);
  REQUIRE(gain >= 10);
  // The buff alone reaches the cap (80); the ring's +10 fits under it only while the buff is down.
  for (auto& r : s.defense)
    if (r.type == 7) r.defense = r.processed = static_cast<float>(80 - gain);
  snap::Item ring = Bench::item(4, kRing);
  s = model::swap(s, ring, 4, db).after;  // bare
  ring.stats->defense = {defense(7, 10)};
  const snap::Snapshot worn = model::swap(s, ring, 4, db).after;
  auto at = [&](double uptime) {
    p.uptime[kDreeg] = uptime;
    return score::compare(s, worn, skills, p, db);
  };
  const score::Result down = at(0), up = at(1), half = at(0.5);
  CHECK(up.ratio.survival == doctest::Approx(1));  // capped either way
  CHECK(down.ratio.survival > 1.001);
  CHECK(half.ratio.survival > up.ratio.survival);
  CHECK(half.ratio.survival < down.ratio.survival);
  CHECK(sum(half) == doctest::Approx(std::log(1 + half.percent / 100) * 100));  // the parts still add up
  // The reverse swap is the reciprocal in every mix.
  const score::Result back = score::compare(worn, s, skills, p, db);
  CHECK((1 + half.percent / 100) * (1 + back.percent / 100) == doctest::Approx(1));
  // Adding the buff's stats x uptime to one sheet instead would put the resist at 80 - gain / 2 on both sides
  // and value the whole +10; the mix values it for half the time.
  CHECK(half.ratio.survival - 1 < 0.75 * (down.ratio.survival - 1));
}

TEST_CASE("sanity: a buff that is always up scores the same in average and live mode (installed game; skips if absent)") {
  const arz::Database* db = game_db();
  if (!db) return;
  const snap::Snapshot raw = level22();  // Blood of Dreeg running, uptime 100%
  snap::Item ring = Bench::item(4, kRing);
  ring.stats->defense = {defense(7, 30)};
  ring.stats->char_attributes = {{4, 200, 0}};
  auto run = [&](const score::Preset& p, const snap::Snapshot& s) {
    std::vector<tooltip::Scored> scored;
    const auto lines = tooltip::compare(s, ring, 6, true, db, p, Labels(db, nullptr), nullptr, &scored);
    REQUIRE(!scored.empty());
    return std::pair{scored[0].percent, lines};
  };
  score::Preset average, live;
  live.live_buffs = true;
  const auto a = run(average, raw), l = run(live, raw);
  CHECK(a.first != 0);
  CHECK(a.first == l.first);
  CHECK(a.second == l.second);
  // Its own part, not the sheet's values again: "+31.1/s Life regen, ..." on the line after its name.
  const auto head = std::find_if(a.second.begin(), a.second.end(), [](const tooltip::Line& x) {
    return x.text.find("bloodofdreeg1 (up 100%, included above):") != std::string::npos;
  });
  REQUIRE(head != a.second.end());
  REQUIRE(head + 1 != a.second.end());
  CHECK(head[1].text.starts_with("    +"));
  CHECK(head[1].text.find("/s Life regen") != std::string::npos);
  CHECK(head[1].text.find("->") == std::string::npos);
  // Caught between casts: live scores it as down, average does not care.
  snap::Snapshot down = model::expected(raw, db);
  CHECK(run(average, down).first == a.first);
  CHECK(run(live, down).first != a.first);
}

TEST_CASE("sanity: a buff record's cooldown reduction is read for the Shift view (installed game; skips if absent)") {
  const arz::Database* db = game_db();
  if (!db) return;
  // tier3_01f_skill_cooldownreduction.dbr (Skill_BuffSelfDuration): skillCooldownReduction 100, no mapped stat.
  const arz::Record* cdr = db->record("records/skills/devotion/tier3_01f_skill_cooldownreduction.dbr");
  REQUIRE(cdr);
  const model::BuffEffect e = model::buff_effect(*cdr, 1);
  CHECK(e.unmodeled.at("skillCooldownReduction") == 100);
}

TEST_CASE("sanity: a learned passive with its own trigger is a buff at its uptime (installed game; skips if absent)") {
  const arz::Database* db = game_db();
  if (!db) return;
  // Fighting Spirit rank 3 (fightingspirit1.dbr, Skill_PassiveOnHitBuffSelf): 24% when hit, 5.6 s every 5 s,
  // +70 Offensive Ability, +65% to all damage.
  constexpr const char* kSpirit = "records/skills/playerclass01/fightingspirit1.dbr";
  const snap::Snapshot base = model::expected(level22(), db);
  snap::Snapshot s = base;
  snap::Skill sk;
  sk.id = 990001;
  sk.record = kSpirit;
  sk.level = sk.current_level = 3;
  sk.passive.emplace();
  s.skills.push_back(sk);
  const score::Settings p;
  const auto skills = score::main_skills(s, p, db);
  const score::BuffView v = score::buff_view(s, skills, p, db);
  const auto buff = std::find_if(v.buffs.begin(), v.buffs.end(), [&](const score::BuffUse& b) { return b.record == kSpirit; });
  REQUIRE(buff != v.buffs.end());
  CHECK(buff->uptime > 0.2);
  CHECK(buff->uptime < 1);
  CHECK(buff->on.oa - buff->off.oa >= 70);
  // Learning it is worth damage, less than the buff's whole +65%.
  const score::Result r = score::compare(base, s, skills, p, db);
  CHECK(r.ratio.damage > 1.01);
  CHECK(r.ratio.damage < 1.65);
  CHECK(r.unscored.empty());
  // Caught while it was up, its stats are in the engine's totals: model::expected takes them out, like an
  // item's proc, so the score is the same either way.
  snap::Snapshot up = s;
  up.skills.back().buff_active = true;
  auto oa_flat = [](const snap::Snapshot& x) {
    for (const auto& a : x.char_attributes)
      if (a.type == 21) return static_cast<double>(a.total);
    return 0.0;
  };
  CHECK(oa_flat(model::expected(up, db)) < oa_flat(up) - 69);
}
