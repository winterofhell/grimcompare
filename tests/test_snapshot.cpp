#include <doctest/doctest.h>

#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>

#include "gdc/snapshot.h"

using namespace gdc::snap;

namespace {

Snapshot sample() {
  Snapshot s;
  s.mod_version = "test";
  s.game_build = "1.3.1.1";
  s.taken_at = "2026-11-14T12:00:00Z";
  s.complete = false;
  s.errors = {"inventory: FAULT 0xC0000005"};
  s.character_name = "Bob";
  s.save = "cloud/7/main/__Bob";
  s.level = 30;
  s.difficulty = 0;
  s.skill_points = 2;
  s.attribute_points = 1;
  s.devotion_points = 3;
  s.devotion_points_total = 10;
  s.devotion_current_level_sum = 7;
  s.life_max = 1234.5f;
  s.mana_max = 400;
  s.attack_speed = 1.1f;
  s.cast_speed = 0.9f;
  s.run_speed = 1.35f;
  s.run_speed_uncapped = 1.5f;
  s.defense_cap = 80;
  s.pth = {55, 100, {70, 90, 105, 120, 130, 135}, {1, 1.1f, 1.2f, 1.3f, 1.4f, 1.5f}};
  s.char_attributes = {{1, 350, 0.1f, 300, 20}, {21, 512, 0, 0, 0}};
  s.defense = {{6, 140, 0, 0, 0, 0, 83, 3, true}, {28, 0, 0, 5, 0, 0}};
  s.damage_modifiers = {{6, 0, 0, 0, 120, 80}};
  s.conversions = {{10, 11, 50}};
  s.skills = {{77, "records/skills/a.dbr", 2, 10, 3, 13, 0, 0, 55}, {78, "records/skills/devotion/b.dbr", 0, 0, 0, 0, 7, 15, 0}};
  s.skills[0].anim_frames = 24;
  s.skills[0].anim_frame_rate = 30;
  Item it;
  it.id = 1001;
  it.location = 4;
  it.record = "records/items/gearrings/a01_ring.dbr";
  it.kind = "ItemEquipment";
  it.set = "records/sets/x.dbr";
  it.replica = Replica{};
  it.replica->base = it.record;
  it.replica->prefix = "records/items/lootaffixes/prefix/p.dbr";
  it.replica->seed = 2115467035u;
  it.replica->stack_count = 1;
  it.stats = ItemStats{{{2, 10, 0.05f}}, {{6, 25, 0, 0, 0, 0}}, {{6, 0, 0, 0, 15, 0}}, {}};
  it.stats->offensive_damage = {{6, false, 10, 20, 0}, {15, true, 30, 30, 3}, {5, false, 3, 6, 0, 20, 15, 30}};
  it.stats->offensive_modifiers[0].proc = Proc{25, 0, 0, 0, 60, 0};
  it.stats->retaliation = {{4, false, 100, 100, 0}};
  it.stats->skill_attributes = {{1, 0, 0, 5, 0}};
  it.stats->conversions = {{6, 10, 25}};
  it.relic = Component{"records/items/materia/compa.dbr", ItemStats{{{4, 30, 0}}, {}, {}, {}}};
  s.equipped = {it};
  Item hov = it;
  hov.location = -1;
  hov.relic.reset();
  s.tooltip_items = {hov, it};
  s.tooltip_age_ms = 16;
  s.item_skill_modifiers = {{77, "records/skills/a.dbr", 901, "records/skills/mod.dbr", 10, false, true,
                             ItemStats{{}, {}, {}, {}, {{5, false, 16, 16, 0}}, {}, {}, {}}}};
  s.inventory = {Item{2002, -1, "records/items/x.dbr", "Item", "", std::nullopt, std::nullopt, std::nullopt, std::nullopt}};
  Pet pet;
  pet.id = 3001;
  pet.skill_id = 77;
  pet.record = "records/skills/playerclass03/pets/pet_hellhound_a16.dbr";
  pet.attack_speed = 1.35f;
  pet.cast_speed = 1.1f;
  pet.attack_speed_modifier = 25;
  pet.default_skill_id = 3002;
  pet.skills = {{3002, "records/skills/default/defaultpetattack.dbr", 1, 20, 30},
                {3003, "records/skills/playerclass03/pets/petskill_hellhound_fireclaw.dbr", 16, 0, 0}};
  s.pets = {pet};
  SkillDps dps;
  dps.skill_id = 77;
  dps.record = "records/skills/a.dbr";
  dps.default_attack = true;
  dps.dps = 1234.5f;
  dps.main_hand = {{7, true, 50, 60, 5.5f}, {6, false, 100, 200}};
  dps.off_hand = {{2, false, 10, 20}};
  s.skill_dps = {dps};
  return s;
}

}  // namespace

TEST_CASE("widen keeps the float's shortest decimal") {
  CHECK(widen(0.1f) == 0.1);
  CHECK(widen(1234.5f) == 1234.5);
  CHECK(widen(0.0f) == 0.0);
  CHECK(widen(-3.25f) == -3.25);
  CHECK(static_cast<float>(widen(1.0f / 3.0f)) == 1.0f / 3.0f);
}

TEST_CASE("snapshot JSON round-trips") {
  const Snapshot a = sample();
  const std::string text = to_json(a);
  const Snapshot b = from_json(text);
  CHECK(to_json(b) == text);
  CHECK(b.level == 30);
  CHECK(b.complete == false);
  REQUIRE(b.equipped.size() == 1);
  CHECK(b.equipped[0].replica->seed == 2115467035u);
  CHECK(b.equipped[0].relic->stats->char_attributes[0].value == 30);
  CHECK(b.tooltip_items[0].location == -1);
  CHECK(!b.tooltip_items[0].relic);
  CHECK(b.inventory[0].kind == "Item");
  CHECK(b.char_attributes[0].modifier == 0.1f);
  CHECK(b.run_speed == 1.35f);
  CHECK(b.defense_cap == 80);
  CHECK(b.devotion_current_level_sum == 7);
  CHECK(b.skills[0].anim_frames == 24);
  CHECK(b.skills[0].anim_frame_rate == 30);
  CHECK(b.skills[1].anim_frames == 0);
  CHECK(b.pth.crit_divisor_min == 100);
  CHECK(b.pth.threshold[1] == 90);
  CHECK(b.pth.damage_modifier[5] == 1.5f);
  CHECK(b.defense[0].processed == 83);
  CHECK(b.defense[0].cap_bonus == 3);
  CHECK(b.defense[0].capped);
  CHECK(!b.defense[1].capped);
  REQUIRE(b.conversions.size() == 1);
  CHECK(b.conversions[0].to == 11);
  const ItemStats& st = *b.equipped[0].stats;
  REQUIRE(st.offensive_damage.size() == 3);
  CHECK(st.offensive_damage[1].dot);
  CHECK(st.offensive_damage[1].duration == 3);
  CHECK(st.retaliation[0].max == 100);
  CHECK(st.offensive_damage[2].chance == 20);
  CHECK(st.offensive_damage[2].raw_max == 30);
  CHECK(st.offensive_damage[0].chance == 0);
  REQUIRE(st.offensive_modifiers[0].proc);
  CHECK(st.offensive_modifiers[0].proc->chance == 25);
  CHECK(st.offensive_modifiers[0].proc->damage_modifier == 60);
  CHECK(!st.defense[0].proc);
  CHECK(st.skill_attributes[0].modifier == 5);
  CHECK(st.conversions[0].percent == 25);
  REQUIRE(b.item_skill_modifiers.size() == 1);
  CHECK(b.item_skill_modifiers[0].from_augment);
  CHECK(!b.item_skill_modifiers[0].from_relic);
  CHECK(b.item_skill_modifiers[0].location == 10);
  CHECK(b.item_skill_modifiers[0].stats->offensive_damage[0].min == 16);
  REQUIRE(b.pets.size() == 1);
  CHECK(b.pets[0].skill_id == 77);
  CHECK(b.pets[0].attack_speed == 1.35f);
  CHECK(b.pets[0].attack_speed_modifier == 25);
  CHECK(b.pets[0].cast_speed_modifier == 0);
  CHECK(b.pets[0].default_skill_id == 3002);
  REQUIRE(b.pets[0].skills.size() == 2);
  CHECK(b.pets[0].skills[0].anim_frames == 20);
  CHECK(b.pets[0].skills[0].anim_frame_rate == 30);
  CHECK(b.pets[0].skills[1].current_level == 16);
  CHECK(b.pets[0].skills[1].anim_frames == 0);
  REQUIRE(b.skill_dps.size() == 1);
  CHECK(b.skill_dps[0].skill_id == 77);
  CHECK(b.skill_dps[0].default_attack);
  CHECK(b.skill_dps[0].dps == 1234.5f);
  REQUIRE(b.skill_dps[0].main_hand.size() == 2);
  CHECK(b.skill_dps[0].main_hand[0].dot);
  CHECK(b.skill_dps[0].main_hand[0].duration == 5.5f);
  CHECK(b.skill_dps[0].off_hand[0].max == 20);
}

TEST_CASE("snapshot JSON: names written, zeros omitted, floats short") {
  const std::string text = to_json(sample());
  CHECK(text.find("\"name\": \"Strength\"") != std::string::npos);
  CHECK(text.find("\"name\": \"Fire\"") != std::string::npos);
  CHECK(text.find("\"modifier\": 0.1,") != std::string::npos);
  CHECK(text.find("0.10000000149") == std::string::npos);
  CHECK(text.find("\"location\": -1") == std::string::npos);
  CHECK(text.find("\"retaliation_modifiers\"") == std::string::npos);  // empty list omitted
  CHECK(text.find("\"difficulty\": 0") != std::string::npos);          // 0 is a real difficulty
  CHECK(text.find("\"difficulty_name\": \"Normal\"") != std::string::npos);
  CHECK(text.find("\"location_name\": \"Finger\"") != std::string::npos);
  CHECK(text.find("\"from_name\": \"Chaos\"") != std::string::npos);
  CHECK(text.find("\"name\": \"CooldownReduction\"") != std::string::npos);
  CHECK(text.find("\"from_relic\"") == std::string::npos);  // false omitted
}

TEST_CASE("snapshot JSON: bad input throws") {
  CHECK_THROWS_AS(from_json("{"), std::runtime_error);
  CHECK_THROWS_AS(from_json("[]"), std::runtime_error);
  CHECK_THROWS_AS(from_json(R"({"schema": 1})"), std::runtime_error);
  CHECK_THROWS_AS(from_json(R"({"schema": 16})"), std::runtime_error);
  CHECK_THROWS_AS(from_json(R"({"schema": 2, "skills": {}})"), std::runtime_error);
  CHECK_THROWS_AS(from_json(R"({"schema": 2, "character": {"level": "x"}})"), std::runtime_error);
  CHECK_THROWS_AS(from_json(R"({"schema": 2, "character": {"pth": {"thresholds": [1], "damage_modifiers": [1]}}})"),
                  std::runtime_error);
  CHECK(from_json(R"({"schema": 2})").schema == 2);  // pre-chance snapshots still read
  CHECK(from_json(R"({"schema": 3})").level == 0);
}

TEST_CASE("snapshot JSON: skill passive contributions (schema 4)") {
  Snapshot s;
  Skill a, b;
  a.record = "records/skills/a.dbr";
  a.passive = ItemStats{};
  a.passive->defense = {{10, 34, 0, 0, 0, 0}};
  b.record = "records/skills/b.dbr";
  b.passive = ItemStats{};  // contributes nothing now: omitted, reads back as empty
  s.skills = {a, b};
  const Snapshot r = from_json(to_json(s));
  REQUIRE(r.skills.size() == 2);
  REQUIRE(r.skills[0].passive);
  CHECK(r.skills[0].passive->defense[0].defense == 34);
  REQUIRE(r.skills[1].passive);
  CHECK(r.skills[1].passive->defense.empty());
  const Snapshot old = from_json(R"({"schema": 3, "skills": [{"id": 1}]})");
  CHECK(!old.skills[0].passive);  // not captured before schema 4
}

TEST_CASE("snapshot JSON: learned modifier ids (schema 5)") {
  Snapshot s;
  Skill a, b;
  a.modifier_ids = {19670, 19669};
  s.skills = {a, b};
  const std::string text = to_json(s);
  CHECK(text.find("modifier_ids") == text.rfind("modifier_ids"));  // omitted when empty
  const Snapshot r = from_json(text);
  CHECK(r.skills[0].modifier_ids == std::vector<std::uint32_t>{19670, 19669});
  CHECK(r.skills[1].modifier_ids.empty());
  Spares spare;  // recycling empties the list
  recycle(s, spare);
  CHECK(reuse(spare.skills).modifier_ids.empty());
}

TEST_CASE("snapshot JSON: item-granted skills with their item id (schema 6), keyed by id in the store") {
  Snapshot s;
  Skill a;
  a.id = 7;
  a.record = "records/skills/itemskills/legendary/item_harbingersgrasp.dbr";
  a.current_level = 1;
  a.item_id = 28753;
  a.passive = ItemStats{{{23, 25, 0}}, {}, {}, {}};
  Skill b = a;
  b.id = 8;  // a second item granting the same record
  b.item_id = 28754;
  s.item_skills = {a, b};
  const Snapshot r = from_json(to_json(s));
  REQUIRE(r.item_skills.size() == 2);
  CHECK(r.item_skills[0].item_id == 28753);
  CHECK(r.item_skills[1].item_id == 28754);
  REQUIRE(r.item_skills[0].passive);
  CHECK(r.item_skills[0].passive->char_attributes.size() == 1);
  CHECK(r.skills.empty());
  const Snapshot old = from_json(R"({"schema": 5})");
  CHECK(old.item_skills.empty());
  Spares spare;
  recycle(s, spare);
  CHECK(s.item_skills.empty());
  CHECK(reuse(spare.skills).item_id == 0);
}

TEST_CASE("recycle: default values, capacity and spare order kept") {
  std::ifstream f(std::string(GDC_SOURCE_DIR) + "/tests/fixtures/snapshots/lvl96-phase21-axe-hover.json");
  REQUIRE(f.good());
  const Snapshot orig = from_json(std::string(std::istreambuf_iterator<char>(f), {}));
  REQUIRE(!orig.skills.empty());
  REQUIRE(!orig.equipped.empty());
  REQUIRE(!orig.tooltip_items.empty());
  REQUIRE(!orig.item_skill_modifiers.empty());

  Snapshot s = orig;
  Spares spare;
  recycle(s, spare);
  CHECK(to_json(s) == to_json(Snapshot{}));
  CHECK(s.char_attributes.capacity() >= orig.char_attributes.size());
  CHECK(s.skills.capacity() >= orig.skills.size());
  CHECK(spare.skills.size() == orig.skills.size());
  CHECK(spare.items.size() == orig.equipped.size() + orig.tooltip_items.size() + orig.inventory.size());
  CHECK(spare.item_skill_modifiers.size() == orig.item_skill_modifiers.size());

  // Taken back in the original order, each equal to a default element but keeping its string buffers.
  Snapshot back, fresh;
  for (const Skill& o : orig.skills) {
    const Skill& t = back.skills.emplace_back(reuse(spare.skills));
    CHECK(t.record.empty());
    CHECK(t.record.capacity() >= o.record.size());
    fresh.skills.emplace_back();
  }
  for (const Item& o : orig.equipped) {
    const Item& t = back.equipped.emplace_back(reuse(spare.items));
    CHECK(t.record.capacity() >= o.record.size());
    fresh.equipped.emplace_back();
  }
  for (std::size_t i = 0; i < orig.item_skill_modifiers.size(); ++i) {
    back.item_skill_modifiers.push_back(reuse(spare.item_skill_modifiers));
    fresh.item_skill_modifiers.emplace_back();
  }
  CHECK(to_json(back) == to_json(fresh));

  // Empty spares: a new element.
  std::vector<Item> none;
  CHECK(reuse(none).record.capacity() == Item{}.record.capacity());
}

TEST_CASE("snapshot JSON: character skill attribute totals (schema 9)") {
  Snapshot s;
  s.skill_attributes = {{1, 17, 0, 10, 0}};
  const Snapshot back = from_json(to_json(s));
  REQUIRE(back.skill_attributes.size() == 1);
  CHECK(back.skill_attributes[0].type == 1);
  CHECK(back.skill_attributes[0].value == 17);
  CHECK(back.skill_attributes[0].modifier == 10);
  CHECK(from_json(R"({"schema": 8})").skill_attributes.empty());  // not captured before schema 9
}
