#pragma once
// Score v1: one number per character state, compared between two states (before / after a swap):
//   score = Damage^a x Survivability^b x Other^c,   change % = (after / before - 1) x 100
// Only ratios are reported, so the terms need no absolute unit.
//   Damage        main skills' expected damage (per use; per second unless the skill has a cooldown),
//                 weighted; each skill: its own flat damage (+ weapon and gear flat damage for weapon
//                 skills), conversions, the character's % per final type, attribute bonus, hit / crit, speed.
//   Survivability effective health: Life / damage taken per incoming attack through the engine's order of
//                 defenses (docs/MECHANICS.md §9): dodge / deflect, enemy OA vs DA (miss, glancing, crits),
//                 block, enemy damage reduction, resists, then armor per hit region for physical.
//   Other         1 + weighted utility stats (gdc/utility.h).
// Sources and unverified parts: docs/MECHANICS.md §5. Every number the preset sets is a design choice:
// presets/default.toml.
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "gdc/arz.h"
#include "gdc/effects.h"
#include "gdc/labels.h"
#include "gdc/snapshot.h"

namespace gdc::score {

// A final damage type: CombatAttributeType and direct (false) or duration (true) damage, as the score keys
// amounts (re/CombatAttributeType.tsv; acid = Poison type 7 direct, poison = type 7 duration, ERRATA §0).
using DamageKey = std::pair<int, bool>;
// Preset name ("acid", "vitality_decay", ...) -> key; nullopt if unknown.
std::optional<DamageKey> damage_key(std::string_view name);
// Display name ("Acid", "Vitality Decay"); "type <n>" for a key without one.
std::string damage_label(DamageKey k);

// One [other] weight: per unit of the key's value (gdc::utility::stats), counted only while every `when`
// condition holds (utility::holds; empty = always).
struct Weight {
  double weight = 0;
  std::vector<std::string> when;
  bool operator==(const Weight&) const = default;
};
// utility::stats() with their default weights and conditions.
std::map<std::string, Weight> default_other();

// The numbers a preset sets; a [[stage]] overrides some of them.
struct Settings {
  // [score]
  double damage_weight = 1.0, survival_weight = 1.0, other_weight = 0.25;  // a, b, c
  double negligible_percent = 0.1;  // |change| below this shows as "~0%"
  // Threshold curve (threshold_factor): log slope per relative step below / above the target, and the width
  // (relative to the target) of the bend between them. Design choices (docs/PRESETS.md).
  double threshold_below = 1.0, threshold_above = 0.05, threshold_width = 0.05;
  // [damage]
  std::vector<std::string> skills;  // skill records; empty = auto (learned class attack skills by points)
  bool weigh_by_points = false;     // skills: weights by points spent (skill_weights = "points") or equal
  int auto_skills = 3;
  double filler_weight = 0.5;       // auto: share of the default weapon attack when every skill has a cooldown
  double enemy_da_per_level = 0;    // enemy DA = this x character level; 0 = the monster table (gdc/enemy.h)
  double pet_attack_rate = 0;       // attacks per second of one pet; 0 = unknown: pet effects fall back to 0
  // conversion_warn: "from->to" conversions (CombatAttributeType; 0 = "*", any) that show red when an item
  // adds them and their output is not a [match] damage type.
  std::vector<std::pair<int, int>> conversion_warn;
  // [survival]
  double enemy_oa_per_level = 0;    // enemy OA = this x character level; 0 = the monster table
  double enemy_hit_per_level = 0;   // size of one hit (armor, block) = this x character level; 0 = the table
  double melee_share = 0.5;         // share of incoming attacks that are melee (dodge) rather than projectiles (deflect)
  double damage_reduction_uptime = 0.5;  // uptime of the items' "x% reduced target's damage" debuff
  double resist_reduction = 0;      // subtracted from each of the 9 base resists (Pierce..Bleeding) before its cap
  double incoming_hits_per_sec = 2; // hits taken per second: "when hit" procs (docs/PRESETS.md)
  std::map<int, double> threat = {  // CombatAttributeType -> share of incoming damage (normalized)
      {2, 0.20}, {4, 0.05}, {5, 0.10}, {6, 0.10}, {7, 0.10}, {8, 0.10},
      {9, 0.10}, {10, 0.10}, {11, 0.10}, {15, 0.05}};
  // [other] key -> weight (gdc::utility; Other = 1 + sum of weight x value, unlisted stats at "default").
  std::map<std::string, Weight> other = default_other();
  std::string stage;  // the [[stage]] these settings come from ("" = none): `when = "stage:<name>"`
  // [enemy] the target's resistance per type (CombatAttributeType); RR lowers it, damage x (1 - resist / 100).
  // Default: mean of the 48 records/creatures/enemies/* records with monsterClassification Boss (database.arz
  // 1.3.1.1, defensive<Type> base values; difficulty scaling not included).
  std::map<int, double> enemy_resist = {{2, 19}, {4, 22}, {5, 32}, {6, 15}, {7, 15},
                                        {8, 16}, {9, 30}, {10, 15}, {11, 17}, {15, 0}};
  // [uptime] effect record -> uptime in [0, 1], over the derived value (docs/MECHANICS.md §4).
  std::map<std::string, double> uptime;
  // [thresholds] defense targets, name (kThresholds in score.cpp) -> target; Survivability x threshold_factor
  // of each. Empty: none.
  std::map<std::string, double> thresholds;

  bool operator==(const Settings&) const = default;
};

// [[stage]]: applies while the character's level is in [min_level, max_level] and every `requires` skill
// record (skill or mastery) has points spent. `settings` = the preset's own with the stage's tables on top.
struct Stage {
  std::string name;
  std::uint32_t min_level = 1, max_level = 1000;
  std::vector<std::string> required;  // TOML key "requires"
  Settings settings;

  bool operator==(const Stage&) const = default;
};

struct Preset : Settings {
  std::string name;                    // [preset] name: shown in the block heading ("" = none)
  bool auto_select = true;             // [preset] auto: let a matching named preset replace this one (adapter)
  std::vector<std::string> masteries;  // [match] masteries: Skill_Mastery records this build may use
  std::vector<DamageKey> match_damage; // [match] damage: final types the main skills must mostly deal
  double match_share = 0.5;            // [match] min_share: their least share of the main skills' damage
  std::string match_character;         // [match] character: only this character (Player name; "" = any)
  std::string match_archetype;         // [match] archetype: archetype(s).name must equal it ("" = any)
  std::vector<Stage> stages;           // file order
  std::vector<std::string> target_sets;// [preset] target_sets: set records whose pieces get a note on hover

  bool operator==(const Preset&) const = default;
};

// TOML text -> preset. Missing keys keep the defaults above; unknown tables or keys, wrong types and
// out-of-range values are errors. Never throws: nullopt and `error` ("line 3: ...") on failure.
std::optional<Preset> parse(std::string_view toml, std::string& error);

// The last stage (file order) that applies to `s`; nullptr if none does.
const Stage* stage(const Preset& p, const snap::Snapshot& s);
// The stage's settings, or the preset's own when no stage applies.
const Settings& settings(const Preset& p, const snap::Snapshot& s);

struct Match {
  bool ok = false;
  std::string why;  // for the log: "masteries Shaman, Occultist; Acid 61% + Poison 24% = 85% >= 50% (...)"
};
// Auto: whether `p` is made for this character — it has [match] masteries, the character has spent points in
// at least one mastery (a learned skill whose record has Class Skill_Mastery), every such mastery is listed,
// with [match] archetype, archetype(s) is that one, and, with [match] damage, those final types make at
// least min_share of damage_profile(s).
Match match(const Preset& p, const snap::Snapshot& s, const arz::Database& db, const Labels& labels = {});

// Auto's try order among named presets (stable sort, ties keep file-name order): one with [match] character
// first, then fewer [match] masteries — a build's own preset before a template that lists all ten.
bool tried_first(const Preset& a, const Preset& b);

// What the character's class skill points mostly go to: "pet", "retaliation", "melee", "ranged", "caster",
// or "" (no attack, pet or retaliation points). Each learned class skill (not a devotion, not a Skill_Mastery)
// is one of: pet — Skill_SpawnPet / Skill_TargetedSpawnPet whose spawnObjects record has Class Pet (the
// PetPlayerScaling summons — mines, totems, seals, Wind Devils — are not); attack — a Skill_Attack* with
// damage (record or buffSkillName record, as main_skills), a Skill_WeaponPool_* or Skill_WPAttack_*; weapon
// attack — such an attack with weaponDamagePct > 0, every Skill_WeaponPool_* / Skill_WPAttack_* (all have
// it); retaliation — the record or its buff has a
// non-zero retaliation* field. A skill in another learned skill's modifier_ids (schema 5) takes its parent's
// category; unlinked modifiers count for nothing. Points per category = spent points (level).
// Decision (design thresholds): pet if pet points >= attack points, else retaliation if retaliation
// >= attack, else melee / ranged (R Hand WeaponHunting_*) if weapon attack points >= half of attack points,
// else caster.
struct Archetype {
  std::string name;
  std::string why;  // "pet 0, retaliation 1, attack 25 (weapon 0)"
};
Archetype archetype(const snap::Snapshot& s, const arz::Database& db);

// Share of each final damage type (after conversions, with the character's % per type) in the main skills'
// damage, the main skills as the default Auto picks them (top `auto_skills` class attack skills by points,
// no weapon-attack filler), each skill weighted by points. Empty when there is no such skill.
std::map<DamageKey, double> damage_profile(const snap::Snapshot& s, const arz::Database& db);

struct Skill {
  std::string record;
  double weight = 0;
};

// Smooth soft threshold: ln f = above x u - (below - above) x width x ln(1 + e^(-u / width)),
// u = value / target - 1. Slope `below` well under the target, `above` well over it, no step anywhere.
double threshold_factor(double value, double target, const Settings& p);

// A conversion `after` has and `before` has not (or less of).
struct Conversion {
  std::string skill;  // the skill an item skill modifier converts; empty: the character's (gear) conversion
  int from = 0, to = 0;
  double percent = 0;
};
// Conversions the swap adds that `p.conversion_warn` names and whose output type is not one of `targets`
// (Preset::match_damage types): the character's (gear, components, sets: snapshot conversions) and item
// skill modifiers' (conversionInType / OutType / Percentage, 2, on the modifier record at level 1).
std::vector<Conversion> conversion_warnings(const snap::Snapshot& before, const snap::Snapshot& after,
                                            const Settings& p, const std::vector<DamageKey>& targets,
                                            const arz::Database* db);
// Freeze (PROMPT.md "freeze current Auto"): a preset file named `name` that scores `s` the way `chosen` does
// now — the settings of its stage for `s`, with the main skills it picks (main_skills) listed explicitly
// (weighted by points when they were picked automatically), [match] for this character's masteries and name,
// no stages. parse() of the text gives exactly those settings.
std::string freeze(const Preset& chosen, const snap::Snapshot& s, const arz::Database& db, const std::string& name,
                   const Labels& labels = {});

// "Acid/Poison" (type 7 converts both), "Elemental", else damage_label of the direct type.
std::string conversion_label(int type);

// The skills Damage is computed from (weights sum to 1): preset.skills if set (equal weights or by points
// spent, unlearned ones dropped), else the `auto_skills` learned class skills of an attack class (Skill_Attack*) with
// damage at their level, weighted by points spent; when all of those have a cooldown, the default weapon
// attack is added with `filler_weight` of the total. Empty without a database or such a skill.
std::vector<Skill> main_skills(const snap::Snapshot& s, const Settings& p, const arz::Database* db);

struct Terms {
  double damage = 1, survival = 1, other = 1;
};

struct Unscored {
  std::string record;  // the effect's record
  std::string why;     // why its uptime is the fallback 0
};

struct Contribution {
  std::string label;  // "Life", "Poison Resist", "target resist", "cooldown_reduction", ...
  std::string skill;  // a main skill's record instead of a label
  double points = 0;
};

struct Result {
  Terms ratio;           // after / before per term
  double percent = 0;    // (D^a x S^b x O^c - 1) x 100
  std::string before, after;       // inputs of each side, one line each (for the log)
  std::vector<std::string> notes;  // parts not scored or not exact
  std::vector<Unscored> unscored;  // effects at the fallback uptime 0 that the swap adds or removes
  // RR the swap changes (Shift view): entries (record, category) missing or different on the other side.
  std::vector<effects::Reduction> rr_lost, rr_gained;
  // Other terms that change (Shift view): "cooldown_reduction 0 -> 0.05 (w 0.5)", unlisted stats by name.
  std::vector<std::string> other_changes;
  // What moved the score, largest first: every part of Damage (per main skill, procs + pets), Survivability
  // (Life, enemy hit / crit chance from DA, dodge / deflect, block, enemy damage reduction, damage taken per
  // type, each threshold) and Other (per key), in points of `percent`. Each term's ln ratio is split over its
  // parts and weighted by the term's exponent, so the parts add up to ln(1 + percent / 100) x 100.
  std::vector<Contribution> contributions;
};

// `skills` from main_skills(before); evaluated on both sides.
Result compare(const snap::Snapshot& before, const snap::Snapshot& after, const std::vector<Skill>& skills,
               const Settings& p, const arz::Database* db);

}  // namespace gdc::score
