#include <doctest/doctest.h>

#include <algorithm>
#include <string>
#include <vector>

#include <cstdlib>
#include <filesystem>

#include "game_db.h"
#include "gdc/db_fields.h"
#include "gdc/font.h"
#include "gdc/tooltip.h"

using namespace gdc;
using tooltip::Line;
using tooltip::Tone;

namespace {

model::Sheet base() {
  model::Sheet s;
  s.physique = 771;
  s.life = 12307;
  s.oa = 2500;
  s.da = 2400;
  s.armor = 1408;
  s.run_speed = 1.25;
  s.resists = {{6, 120, 120, 80, 80}, {10, 30, 30, 30, 80}, {9, 50, 50, 50, 80}};  // type, raw, uncapped, shown, max
  return s;
}

}  // namespace

TEST_CASE("tooltip: changes() lists every changed value, overcap, skills and sets") {
  const auto b = base();
  auto a = b;
  a.physique = 842;
  a.run_speed = 1.2;
  a.resists[0] = {6, 140, 140, 80, 80};  // overcap 120 -> 140
  a.resists[1] = {10, 30, 30, 30, 83};   // max 80 -> 83
  model::Prediction p;
  p.skills = {{"records/skills/playerclass03/possession1.dbr", 21, 19}};
  p.sets = {{"records/items/sets/set_x.dbr", 2, 3}};
  const auto lines = tooltip::changes(b, a, p);
  const std::vector<Line> want = {
      {Tone::Better, "Physique 771->842 (+71)"},
      {Tone::Worse, "Run speed 125->120% (-5)"},
      {Tone::Better, "Fire 80% (120->140)"},
      {Tone::Plain, "Chaos 30% max 80->83"},
      {Tone::Worse, "possession1 level 21->19"},
      {Tone::Better, "Set set_x 2->3"},
  };
  CHECK(lines == want);
  CHECK(tooltip::changes(b, b, {})[0] == Line{Tone::Plain, "No sheet change"});
  auto gained = b;  // a resist the character did not have
  gained.resists.push_back({21, 25, 25, 25, 80});
  CHECK(tooltip::changes(b, gained, {})[0] == Line{Tone::Better, "ManaLeach 0->25%"});
}

TEST_CASE("tooltip: changes() never puts points on a line that moved the other way") {
  auto b = base();
  b.life = 4093;
  auto a = b;
  a.life = 4002;
  score::Result r;
  r.contributions = {{"Life", "", 1.1, "life"}};
  const std::vector<Line> want = {{Tone::Better, "Life: other effects [+1.1]"}, {Tone::Worse, "Life 4093->4002 (-91)"}};
  CHECK(tooltip::changes(b, a, {}, {}, &r) == want);
  r.contributions[0].points = -2.2;
  CHECK(tooltip::changes(b, a, {}, {}, &r) == std::vector<Line>{{Tone::Worse, "Life 4093->4002 (-91) [-2.2]"}});
}

TEST_CASE("tooltip: changes() with the score: one line per value with its points, the largest first") {
  auto b = base();
  b.resists.push_back({42, 27, 27, 27, 80});  // Reduced Stun Duration
  auto a = b;
  a.life = 13219;
  a.resists.back() = {42, 11, 11, 11, 80};
  score::Result r;
  r.contributions = {{"Life", "", 7.9, "life"},
                     {"pet_bonus", "", 2.1, "pet_bonus"},
                     {"stun_resist", "", -0.4, "stun_resist"}};
  // Other terms in their own units (fractions): one with a sheet row, one without, one with no weight.
  r.other_changes = {{"stun_resist", 0.27, 0.11, 0.1}, {"pet_bonus", 0.12, 0.17, 0.5}, {"life_leech", 0.03, 0.05, 0}};
  const auto lines = tooltip::changes(b, a, {}, {}, &r);
  const std::vector<Line> want = {
      {Tone::Better, "Life 12307->13219 (+912) [+7.9]"},
      {Tone::Better, "Pet bonus 12->17% [+2.1 w0.5]"},
      {Tone::Worse, "Stun 27->11% [-0.4 w0.1]"},  // the resist's game name without text tables: its type name
      {Tone::Plain, "Life leech 3->5% [w0]"},
  };
  CHECK(lines == want);
  // Without the score: the sheet alone, in sheet order.
  CHECK(tooltip::changes(b, a, {}) == std::vector<Line>{{Tone::Better, "Life 12307->13219 (+912)"}, {Tone::Worse, "Stun 27->11%"}});
  // A value the swap does not move but whose part does (a share of Life): listed without a change.
  score::Result kept;
  kept.contributions = {{"Life", "", 1.5, "life"}};
  CHECK(tooltip::changes(b, b, {}, {}, &kept) == std::vector<Line>{{Tone::Plain, "Life 12307 [+1.5]"}});
}

TEST_CASE("tooltip: a value with no score part of its own says where it went") {
  const auto b = base();
  auto a = b;
  a.physique += 10, a.cunning += 10, a.spirit += 1000, a.energy += 10, a.oa += 7;
  score::Result r;
  r.contributions = {{"", "records/skills/playerclass02/x.dbr", 1.0, ""}};  // a main skill: Damage has parts
  const auto lines = tooltip::changes(b, a, {}, {}, &r);
  REQUIRE(lines.size() == 5);
  CHECK(lines[0].text.ends_with(" (in Life, DA, regen)"));
  CHECK(lines[1].text.ends_with(" (in Life, OA, Damage)"));
  CHECK(lines[2].text.ends_with(" (in Life, Energy, regen, Damage)"));
  CHECK(lines[3].text.ends_with(" (not scored)"));
  CHECK(lines[4].text.ends_with(" (in Damage)"));
  // The longest one fits as the game shows it (indented 4) on a wide screen at UI scale 1 and at 2560 px, scale
  // 1.125628 (below 1600 px at scale 1 the sheet rows wrap anyway: 288 px).
  CHECK(tooltip::fits({1.0f, 1920}, "    " + lines[2].text));
  CHECK(tooltip::fits({1.125628f, 2560}, "    " + lines[2].text));
  // No main skill: no Damage.
  r.contributions.clear();
  const auto none = tooltip::changes(b, a, {}, {}, &r);
  CHECK(none[1].text.ends_with(" (in Life, OA)"));
  CHECK(none[4].text.ends_with(" (not scored)"));
  // Without the score: no hints.
  CHECK(tooltip::changes(b, a, {})[4].text == "OA 2500->2507 (+7)");
}

TEST_CASE("tooltip: cooldown reduction says where it went, energy cost reduction why it is not scored") {
  const auto b = base();
  score::Result r;
  r.contributions = {{"", "records/skills/playerclass02/x.dbr", 1.0, ""}};
  r.other_changes = {{"cooldown_reduction", 0.18, 0.12, 0}, {"skill_cost_reduction", 0, 0.05, 0}};
  CHECK(tooltip::changes(b, b, {}, {}, &r) ==
        std::vector<Line>{{Tone::Worse, "Cooldown reduction 18->12% (in buff uptime, Damage)"},
                          {Tone::Better, "Energy cost reduction 0->5% (not scored: no energy model)"}});
  r.contributions.clear();  // no main skill: no Damage
  CHECK(tooltip::changes(b, b, {}, {}, &r)[0].text == "Cooldown reduction 18->12% (in buff uptime)");
}

TEST_CASE("tooltip: escape() keeps text literal under the font's markup") {
  CHECK(tooltip::escape("{^E}Poison & Acid") == "(^^E)Poison && Acid");
  CHECK(tooltip::escape("a^b&B&&") == "a^^b&&B&&&&");
  CHECK(tooltip::escape("Life 3403->3071 (-332)") == "Life 3403->3071 (-332)");
}

// Expected widths: the same algorithm in Python over Fonts.arc (font_tables.py's parser), by hand for the
// one-glyph cases (jura 16: "a" = {-1, -1, 9}, " " = {0, 4, 0}).
TEST_CASE("font: pixel size and width as the engine measures") {
  CHECK(font::pixel_size(17, 1.0f) == 17);
  CHECK(font::pixel_size(17, 1.125628f) == 20);  // ceil(19.14)
  CHECK(font::pixel_size(16, 1.125628f) == 19);  // ceil(18.01)
  CHECK(font::pixel_size(16, 0.3f) == 8);        // lowResMinimumFontSize
  CHECK(font::width(font::Face::Jura, 16, "a") == 8);   // advance 9, then its "after" -1
  CHECK(font::width(font::Face::Jura, 16, "  ") == 8);  // a space after a space adds the first's "after" 4
  const std::string_view sample = "The quick brown fox jumped over the lazy dog The quick";
  CHECK(font::width(font::Face::Jura, 18, sample) == 440);
  CHECK(font::width(font::Face::Jura, 20, sample) == 477);
  CHECK(font::width(font::Face::Jura, 19, sample) == 479);  // no 19 in jura.fnt: 18 (first of the nearest) x 19/18
  CHECK(font::width(font::Face::LinBiolinumSans, 17, "Life -2.0") == 54);
  CHECK(font::width(font::Face::LinBiolinumSans, 20, "Life -2.0") == 62);
}

TEST_CASE("tooltip: fits() against the game's sample width") {
  // 2560 x 1440 at UI scale 1.125628: sample in jura 19 = 479 px, lines in linbiolinum_sansserif 20.
  const tooltip::Screen wide{1.125628f, 2560};
  CHECK(tooltip::fits(wide, "  top: Bloody Pox +0.4, Life -2.0, Block +32.1, Cold Resistanc"));    // 476
  CHECK(!tooltip::fits(wide, "  top: Bloody Pox +0.4, Life -2.0, Block +32.1, Cold Resistance"));  // 485
  // Below 1600 pixels the shorter sample: 288 px at scale 1.
  const tooltip::Screen narrow{1.0f, 1280};
  CHECK(tooltip::fits(narrow, "top: " + std::string(20, 'a') + ","));                         // 195
  CHECK(!tooltip::fits(narrow, "  " + std::string(20, 'b') + ", " + std::string(20, 'c')));  // 335
}

TEST_CASE("tooltip: pack() breaks only between items, greedily") {
  const tooltip::Screen narrow{1.0f, 1280};
  const std::string a(20, 'a'), b(20, 'b'), c(20, 'c'), wide(60, 'w');
  CHECK(tooltip::pack("top: ", "  ", {}, narrow) == std::vector<std::string>{"top: "});
  CHECK(tooltip::pack("top: ", "  ", {a, b, c}, narrow) ==
        std::vector<std::string>{"top: " + a + ",", "  " + b + ",", "  " + c});
  // An item wider than a line stands alone on its own.
  CHECK(tooltip::pack("x: ", "  ", {a, wide, b}, narrow) ==
        std::vector<std::string>{"x: " + a + ",", "  " + wide + ",", "  " + b});
  const std::vector<std::string> items{"Bloody Pox +0.4", "Life -2.0", "Block +32.1", "Cold Resistance +1.2", "Damage +3.0"};
  const auto lines = tooltip::pack("  top: ", "    ", items, tooltip::Screen{1.125628f, 2560});
  CHECK(lines == std::vector<std::string>{"  top: Bloody Pox +0.4, Life -2.0, Block +32.1,",
                                          "    Cold Resistance +1.2, Damage +3.0"});
}

TEST_CASE("tooltip: change() text: ASCII arrows, negligible as ~0%") {
  CHECK(tooltip::change(6.44, 0.1) == Line{Tone::Better, "+6.4% ^"});
  CHECK(tooltip::change(-2.06, 0.1) == Line{Tone::Worse, "-2.1% v"});
  CHECK(tooltip::change(0.04, 0.1) == Line{Tone::Plain, "~0%"});
  CHECK(tooltip::change(-0.04, 0.1) == Line{Tone::Plain, "~0%"});
  CHECK(tooltip::change(0, 0) == Line{Tone::Plain, "~0%"});
}

TEST_CASE("tooltip: locations by item type (EquipmentCtrl::ValidateItemType 0x288D30)") {
  CHECK(tooltip::locations_for(6) == std::vector<int>{4, 5});
  CHECK(tooltip::locations_for(3) == std::vector<int>{8});
  CHECK(tooltip::locations_for(15) == std::vector<int>{3});
  CHECK(tooltip::locations_for(9) == std::vector<int>{0, 1});  // weapon: R Hand, L Hand while dual wielding
  CHECK(tooltip::locations_for(8) == std::vector<int>{1});  // shield / caster off-hand -> L Hand
  CHECK(tooltip::locations_for(0).empty());
  CHECK(tooltip::locations_for(10).empty());
}

TEST_CASE("tooltip: compare scores each finger and marks the better one; nothing for equipped or unknown items") {
  snap::Snapshot s;
  s.level = 10;
  s.defense_cap = 80;
  s.char_attributes = {{1, 50, 0, 50, 0}, {4, 1000, 0, 1000, 0}};  // Physique 50 (regen 0), Life 1000
  snap::Item worn;
  worn.id = 1;
  worn.location = 4;
  worn.stats = snap::ItemStats{{{4, 100, 0}}, {}, {}, {}};  // Life +100 on finger 1
  s.equipped = {worn};
  s.char_attributes[1].total = 1100;
  snap::Item cand;
  cand.id = 2;
  cand.stats = snap::ItemStats{{{4, 200, 0}}, {}, {}, {}};  // Life +200
  std::vector<std::string> log;
  const auto lines = tooltip::compare(s, cand, 6, false, nullptr, score::Preset{}, {}, &log);
  // Survival = EHP ratio = Life ratio: finger 1 1200 / 1100, finger 2 (empty) 1300 / 1100.
  const std::vector<Line> want = {
      {Tone::Heading, "GrimCompare"},
      {Tone::Better, "vs Finger 1: +9.1% ^"},
      {Tone::Better, "vs Finger 2 (empty): +18.2% ^ (best)"},
      {Tone::Warning, "! model inaccurate (Shift: why)"},  // no database
  };
  CHECK(lines == want);
  CHECK(std::find(log.begin(), log.end(), "warning: no database: skill grants, set bonuses and max resist not checked") != log.end());
  CHECK(log[0] == "vs Finger 1 base: 9.09% (Damage 0.0%, Survival +9.1%, Other 0.0%)");

  CHECK(tooltip::compare(s, worn, 6, false, nullptr, {}).empty());  // hovering the worn ring
  CHECK(tooltip::compare(s, cand, 0, false, nullptr, {}).empty());
}

TEST_CASE("tooltip: component lines: moved onto the candidate, or does not fit (installed game; skips if absent)") {
  const auto* dir = std::getenv("HOME");
  const std::filesystem::path game = std::getenv("GD_DIR") ? std::filesystem::path(std::getenv("GD_DIR"))
                                     : dir ? std::filesystem::path(dir) / ".local/share/Steam/steamapps/common/Grim Dawn"
                                           : std::filesystem::path{};
  if (game.empty() || !std::filesystem::exists(game / "database/database.arz")) {
    MESSAGE("game not found; skipped");
    return;
  }
  static const auto db = arz::Database::open_game(game);
  snap::Snapshot s;
  s.level = 10;
  s.defense_cap = 80;
  s.char_attributes = {{1, 50, 0, 50, 0}, {4, 1070, 0, 1070, 0}};
  snap::Item worn;
  worn.id = 1;
  worn.location = 4;
  worn.record = "records/items/gearaccessories/rings/a001_ring01.dbr";
  worn.stats = snap::ItemStats{{{4, 70, 0}}, {}, {}, {}};
  worn.relic.emplace();
  worn.relic->record = "records/items/materia/compa_frozenheart.dbr";  // ring 1: Life +70
  worn.relic->stats = snap::ItemStats{{{4, 70, 0}}, {}, {}, {}};
  s.equipped = {worn};
  snap::Item cand;
  cand.id = 2;
  cand.record = "records/items/gearaccessories/rings/a001_ring01.dbr";
  cand.stats = snap::ItemStats{{{4, 20, 0}}, {}, {}, {}};
  static const auto text = text::Table::open_game(game);
  const Labels labels(&db, &text);
  const auto ring = tooltip::compare(s, cand, 6, false, &db, score::Preset{}, labels);
  REQUIRE(ring.size() >= 4);
  CHECK(ring[1].text == "vs Finger 1: +2.0% ^ (best)");  // 1020 / 1000; finger 2 (empty): 1090 / 1070
  CHECK(ring[2].text == "  with your Frozen Heart: +1.9% ^");          // (1000 + 20 + 70) / 1070
  // The same component in a helm slot item: compa_frozenheart has ring 1, head 0.
  s.equipped[0].location = 7;
  const auto head = tooltip::compare(s, cand, 1, false, &db, score::Preset{}, labels);
  CHECK(std::none_of(head.begin(), head.end(), [](const Line& l) { return l.text.find("with your") != std::string::npos; }));
  CHECK(std::find(head.begin(), head.end(), Line{Tone::Detail, "  your Frozen Heart does not fit this item"}) != head.end());
}

TEST_CASE("tooltip: +levels on an inactive skill are unscored unless the score reads it (installed game; skips if absent)") {
  const arz::Database* db = game_db();
  if (!db) return;
  snap::Snapshot s;
  s.level = 30;
  s.defense_cap = 80;
  s.char_attributes = {{4, 1000, 0, 1000, 0}};
  snap::Skill swarm;  // an attack skill: its capture is empty (not a passive)
  swarm.id = 5;
  swarm.record = "records/skills/playerclass06/devouringswarm1.dbr";
  swarm.level = swarm.current_level = 5;
  swarm.passive = snap::ItemStats{};
  snap::Skill attack;
  attack.id = 6;
  attack.record = "records/skills/default/defaultweaponattack.dbr";
  attack.level = attack.current_level = 1;
  attack.passive = snap::ItemStats{};
  s.skills = {swarm, attack};
  snap::Item belt;  // c010_waist: +2 Devouring Swarm (augmentSkillName1)
  belt.id = 2;
  belt.record = "records/items/gearaccessories/waist/c010_waist.dbr";
  belt.stats = snap::ItemStats{};
  const Labels labels(db, nullptr);
  auto unscored = [&](const score::Preset& p) {
    std::string all;
    for (const auto& l : tooltip::compare(s, belt, 13, false, db, p, labels))
      if (l.text.starts_with("+? unscored") || l.text.starts_with("  ")) all += l.text;
    return all;
  };
  score::Preset main;
  main.skills = {swarm.record};
  CHECK(unscored(main).find("(not active now)") == std::string::npos);  // the score reads it at the new level
  score::Preset other;
  other.skills = {attack.record};
  CHECK(unscored(other).find("level 5->7 (not active now)") != std::string::npos);
}

TEST_CASE("tooltip: RR the swap adds (Shift) and procs with unknown uptime (installed game; skips if absent)") {
  const arz::Database* db = game_db();
  if (!db) return;
  snap::Snapshot s;
  s.level = 94;
  s.defense_cap = 80;
  s.char_attributes = {{4, 10000, 0, 10000, 0}};
  snap::Skill eye;
  eye.id = 1;
  eye.record = "records/skills/playerclass03/evileye1.dbr";
  eye.level = eye.current_level = 12;
  s.skills = {eye};
  snap::Item ring;  // Awakened Ring of Eternal Rot: 10% on attack -> poisondotnova_01, 18% RR type 2
  ring.id = 2;
  ring.record = "records/items/awakened/gearaccessories/rings/c053_ring.dbr";
  ring.stats = snap::ItemStats{};
  const Labels labels(db, nullptr);
  auto all = [&](bool detail) {
    std::string t;
    for (const auto& l : tooltip::compare(s, ring, 6, detail, db, score::Preset{}, labels)) t += l.text + "\n";
    return t;
  };
  // No cast speed in the snapshot: the on-attack proc has no trigger rate -> uptime 0, named as unscored.
  CHECK(all(false).find("(rate unknown)") != std::string::npos);
  s.cast_speed = 2;
  const std::string shift = all(true);
  CHECK(shift.find("(rate unknown)") == std::string::npos);
  CHECK(shift.find(": -18% all resists, up ") != std::string::npos);
  CHECK(shift.find("% (derived)") != std::string::npos);
}

TEST_CASE("tooltip: a conversion away from the preset's damage types shows red (no database needed)") {
  snap::Snapshot s;
  s.level = 90;
  s.defense_cap = 80;
  s.char_attributes = {{4, 1000, 0, 1000, 0}};
  snap::Item ring;  // c117_ring: conversionInType Life -> Fire 25% (DB)
  ring.id = 2;
  ring.record = "records/items/awakened/gearaccessories/rings/c117_ring.dbr";
  ring.stats = snap::ItemStats{};
  ring.stats->conversions = {{9, 6, 0.25f}};
  score::Preset p;
  p.match_damage = {{7, false}, {7, true}};
  p.conversion_warn = {{9, 0}};
  const auto lines = tooltip::compare(s, ring, 6, false, nullptr, p);
  CHECK(std::find(lines.begin(), lines.end(), Line{Tone::Worse, "! converts away: 25% Vitality -> Fire"}) != lines.end());
  p.conversion_warn.clear();
  const auto none = tooltip::compare(s, ring, 6, false, nullptr, p);
  CHECK(std::none_of(none.begin(), none.end(), [](const Line& l) { return l.text.starts_with("! converts"); }));
}

TEST_CASE("tooltip: target_sets note on a hovered member (installed game; skips if absent)") {
  const arz::Database* db = game_db();
  if (!db) return;
  snap::Snapshot s;
  s.level = 94;
  s.defense_cap = 80;
  s.char_attributes = {{4, 1000, 0, 1000, 0}};
  snap::Item worn;
  worn.id = 1;
  worn.location = 9;
  worn.record = "records/items/gearshoulders/d318_shoulder.dbr";
  worn.stats = snap::ItemStats{};
  s.equipped = {worn};
  snap::Item bag = worn;
  bag.id = 3;
  bag.record = "records/items/geartorso/d318_torso.dbr";
  s.inventory = {bag};
  snap::Item helm;
  helm.id = 2;
  helm.record = "records/items/gearhead/d316_head.dbr";
  helm.stats = snap::ItemStats{};
  score::Preset p;
  p.target_sets = {"records/items/lootsets/itemset_d313.dbr"};
  const Labels labels(db, nullptr);
  const auto lines = tooltip::compare(s, helm, 1, false, db, p, labels);
  const auto at = std::find_if(lines.begin(), lines.end(), [](const Line& l) { return l.text.starts_with("target set "); });
  REQUIRE(at != lines.end());
  CHECK(at->text.ends_with(": 1/4 worn"));
  REQUIRE(at + 1 != lines.end());
  CHECK((at + 1)->text.find("(this)") != std::string::npos);
  CHECK((at + 1)->text.find("(in bag)") != std::string::npos);
  // Not a member: no note.
  helm.record = "records/items/gearhead/d011_head.dbr";
  for (const auto& l : tooltip::compare(s, helm, 1, false, db, p, labels)) CHECK_FALSE(l.text.starts_with("target set"));
}

TEST_CASE("tooltip: shields compare in L Hand, against the weapon next to a two-handed one (installed game; skips if absent)") {
  const arz::Database* db = game_db();
  if (!db) return;
  snap::Snapshot s;
  s.level = 94;
  s.defense_cap = 80;
  s.char_attributes = {{4, 10000, 0, 10000, 0}};
  snap::Item sword;
  sword.id = 1;
  sword.location = 0;
  sword.record = "records/items/awakened/gearweapons/swords1h/c027_sword.dbr";
  sword.stats = snap::ItemStats{};
  s.equipped = {sword};
  snap::Item shield;  // c009_shield: block 32% / 1275 / 0.85 s
  shield.id = 2;
  shield.record = "records/items/awakened/gearweapons/shields/c009_shield.dbr";
  shield.stats = snap::ItemStats{};
  const Labels labels(db, nullptr);
  const auto lines = tooltip::compare(s, shield, 8, true, db, score::Preset{}, labels);
  REQUIRE(lines.size() > 1);
  std::string all;
  for (const auto& l : lines) all += l.text + "\n";
  INFO(all);
  CHECK(lines[1].text.starts_with("vs L Hand (empty): +"));
  CHECK(std::any_of(lines.begin(), lines.end(), [](const Line& l) { return l.text.starts_with("  top: Block +"); }));
  s.equipped[0].record = "records/items/awakened/gearweapons/melee2h/c024_sword2h.dbr";
  // Next to a two-handed weapon the shield is compared against it: the pair (the weapon comes off).
  std::vector<tooltip::Scored> scored;
  const auto two = tooltip::compare(s, shield, 8, false, db, score::Preset{}, labels, nullptr, &scored);
  REQUIRE(two.size() >= 2);
  CHECK(two[1].text.starts_with("vs R Hand (two-handed): "));
  REQUIRE(scored.size() == 1);
  CHECK(scored[0].location == 1);
  CHECK(scored[0].also_off == 0);
}

TEST_CASE("tooltip: buff_lines() lists what no sheet line shows") {
  score::BuffUse b;
  b.record = "records/skills/test/haste.dbr";
  b.uptime = 1;
  using K = fields::Kind;
  b.effect.stats = {{{K::Char, 4, false}, 50},    // Life: a sheet line (none here: off and on are the same sheet)
                    {{K::Char, 13, true}, 4},     // characterTotalSpeedModifier
                    {{K::Char, 15, true}, 10},    // characterAttackSpeedModifier
                    {{K::Char, 56, false}, 20},   // no short name: the engine's
                    {{K::Defense, 16, false}, 10}};
  b.effect.unmodeled = {{"skillCooldownReduction", 100}};
  score::BuffView after;
  after.buffs.push_back(b);
  CHECK(tooltip::buff_lines({}, after, {}, tooltip::Screen{1.0f, 2560}) ==
        std::vector<Line>{{Tone::Detail, "haste (new, up 100%, included above):"},
                          {Tone::Detail, "  +4% Total speed, +10% Attack speed,"},
                          {Tone::Detail, "  +20 Increased Gold, +10% Slow resist,"},
                          {Tone::Detail, "  +100% Cooldown reduction (not modeled)"}});
}
