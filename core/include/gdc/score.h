#pragma once
// score = Damage^a x Survivability^b x Other^c (MECHANICS §5).
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "gdc/arz.h"
#include "gdc/effects.h"
#include "gdc/labels.h"
#include "gdc/snapshot.h"
#include "gdc/stat_model.h"

namespace gdc::score {

// (CombatAttributeType, is duration); acid vs poison: MECHANICS "DB names".
using DamageKey = std::pair<int, bool>;
std::optional<DamageKey> damage_key(std::string_view name);
std::string damage_label(DamageKey k);
std::string damage_name(DamageKey k);  // its [match] damage name, "" if it has none

// One stat of an item skill modifier record, as its item tooltip states it: flat damage with s's % and attribute
// bonus (per second for DoT). `scored`: some part of the score reads the field (which part depends on the skill).
// The skill's own damage (flat, %, conversion, crit, multipliers, leech) is read for main skills only.
struct ModifierPart {
  double value = 0;
  std::string unit;  // after the number: " Vitality", "s duration"
  bool scored = false;
};
std::vector<ModifierPart> modifier_parts(const snap::Snapshot& s, const arz::Record& r, bool main_skill);

// A record as main_skills' auto mode sees it.
struct SkillKind {
  bool main = false, weapon = false;
  std::vector<DamageKey> types;
};
SkillKind skill_kind(const arz::Database& db, const std::string& record, std::uint32_t level);

struct Weight {
  double weight = 0;
  std::vector<std::string> when;  // utility::holds conditions; empty = always
  bool operator==(const Weight&) const = default;
};
std::map<std::string, Weight> default_other();

// Numbers here are design choices unless cited (docs/PRESETS.md).
struct Settings {
  double damage_weight = 1.0, survival_weight = 1.0, other_weight = 0.25;  // a, b, c
  double negligible_percent = 0.1;
  double threshold_below = 1.0, threshold_above = 0.05, threshold_width = 0.05;
  // Exponent of the shared base-resist threshold term.
  double threshold_resist_weight = 1.0;
  // Exponent of every other threshold term: second tier, so one never outweighs a base resist under its cap.
  double threshold_secondary_weight = 0.1;
  // > 0: below this level, second-tier targets (but absorption) are scaled by level / threshold_level.
  double threshold_level = 0;
  // recast_buffs: uptime from duration and recharge; else record uptime.
  bool recast_buffs = true, live_buffs = false;
  std::vector<std::string> skills;  // empty = auto
  bool weigh_by_points = false;
  // [match] damage: the build's final damage types. A Settings field so stages keep it.
  std::vector<DamageKey> match_damage;
  // Damage of a type that is not one of match_damage and that no conversion can turn into one counts x this.
  double offtarget_weight = 1.0;
  int auto_skills = 3;
  // [damage] retaliation: retaliation and % Retaliation Added to Attack count in Damage, not Other.
  bool retaliation_damage = false;
  double filler_weight = 0.5;
  double enemy_da_per_level = 0;    // 0 = monster table (gdc/enemy.h)
  double pet_attack_rate = 0;       // 0 = estimated from the pet record (experimental, effects::pets)
  double kills_per_sec = 0.5;
  std::vector<std::pair<int, int>> conversion_warn;  // from -> to; 0 = any
  double enemy_oa_per_level = 0;    // 0 = monster table
  double enemy_hit_per_level = 0;   // 0 = monster table
  double melee_share = 0.5;         // melee hits face dodge, the rest deflect
  double damage_reduction_uptime = 0.5;
  // Enemy RR on base resists; empty = monster table.
  std::optional<double> resist_reduction;
  int resist_reduction_from = 0;    // game difficulty (0..2) where the number starts; below: auto
  double boss_share = 0.15;         // incoming damage share from Quest / Boss / Nemesis / SuperBoss
  // Also score next difficulty's penalty, ramping in (lookahead()).
  bool difficulty_lookahead = true;
  double incoming_hits_per_sec = 2;
  double low_health_per_sec = 0.05;
  double cc_share = 0.1;
  std::map<int, double> threat = {  // CombatAttributeType -> share of incoming damage (normalized)
      {2, 0.20}, {4, 0.05}, {5, 0.10}, {6, 0.10}, {7, 0.10}, {8, 0.10},
      {9, 0.10}, {10, 0.10}, {11, 0.10}, {15, 0.05}};
  std::map<std::string, Weight> other = default_other();
  std::string stage;
  // Mean defensive<Type> of the 48 Boss records.
  std::map<int, double> enemy_resist = {{2, 19}, {4, 22}, {5, 32}, {6, 15}, {7, 15},
                                        {8, 16}, {9, 30}, {10, 15}, {11, 17}, {15, 0}};
  std::map<std::string, double> uptime;
  std::map<std::string, double> thresholds;

  bool operator==(const Settings&) const = default;
};

struct Stage {
  std::string name;
  std::uint32_t min_level = 1, max_level = 1000;
  std::vector<std::string> required;  // TOML "requires"
  Settings settings;

  bool operator==(const Stage&) const = default;
};

struct Preset : Settings {
  std::string name;
  bool auto_select = true;
  std::vector<std::string> masteries;
  double match_share = 0.5;
  std::string match_character;
  std::vector<std::string> match_characters;  // always match, skipping every other check
  // Save keys (snap::save_key); a named save binds that name.
  std::vector<std::string> match_saves;
  std::string match_archetype;
  std::vector<Stage> stages;           // file order
  std::vector<std::string> target_sets;

  bool operator==(const Preset&) const = default;
};

// Unknown keys, bad types, out-of-range values: errors. Never throws.
std::optional<Preset> parse(std::string_view toml, std::string& error);

// Last matching stage in file order.
const Stage* stage(const Preset& p, const snap::Snapshot& s);
const Settings& settings(const Preset& p, const snap::Snapshot& s);

struct Match {
  bool ok = false;
  std::string why;
};
Match match(const Preset& p, const snap::Snapshot& s, const arz::Database& db, const Labels& labels = {});

// Save keys compare like Windows folder names (ASCII case-insensitive).
bool same_save_key(std::string_view a, std::string_view b);
// False when [match] saves name this character, not this save.
bool in_bound_save(const Preset& p, std::string_view name, std::string_view save);
// Listed in [match] characters with a bound save: applies outright.
bool binds(const Preset& p, std::string_view name, std::string_view save);

// A build's own preset before an all-masteries template.
bool tried_first(const Preset& a, const Preset& b);

// Pet summons spawn a Class Pet; PetPlayerScaling ones aren't.
struct Archetype {
  std::string name;
  std::string why;
};
Archetype archetype(const snap::Snapshot& s, const arz::Database& db);

// Each final damage type's share of the main skills' damage. `global_conversions` false: without the
// character's conversions (the types the skills deal themselves).
std::map<DamageKey, double> damage_profile(const snap::Snapshot& s, const arz::Database& db, bool global_conversions = true);

// Source type -> (to -> percent), in engine first-seen order.
using Conversions = std::vector<std::pair<int, std::map<int, double>>>;

// Damage by type after conversions, as CollectCombatParameters (MECHANICS §2).
std::map<DamageKey, double> convert(const std::map<DamageKey, double>& in, const Conversions& skill,
                                    const std::vector<snap::Conversion>& global, std::vector<std::string>& notes);

struct Skill {
  std::string record;
  double weight = 0;
};

// Smooth threshold, no step.
double threshold_factor(double value, double target, const Settings& p);

struct Conversion {
  std::string skill;  // empty: gear conversion
  int from = 0, to = 0;
  double percent = 0;
};
std::vector<Conversion> conversion_warnings(const snap::Snapshot& before, const snap::Snapshot& after,
                                            const Settings& p, const std::vector<DamageKey>& targets,
                                            const arz::Database* db);
// F10 file name for this character, ASCII only.
std::string frozen_file(const snap::Snapshot& s);
// parse(freeze(...)) must give back the same settings.
std::string freeze(const Preset& chosen, const snap::Snapshot& s, const arz::Database& db, const std::string& name,
                   const Labels& labels = {});

std::string conversion_label(int type);

std::vector<Skill> main_skills(const snap::Snapshot& s, const Settings& p, const arz::Database* db);

struct Terms {
  double damage = 1, survival = 1, other = 1;
};

struct Unscored {
  std::string record;
  std::string why;
};

struct Contribution {
  std::string label;
  std::string skill;
  double points = 0;
  std::string stat;  // "life", "da", "resist:<type>" ("resist:2" includes armor), or an Other label
};

struct OtherChange {
  std::string key;
  double before = 0, after = 0, weight = 0;
  bool operator==(const OtherChange&) const = default;
};

struct BuffUse {
  std::string record;
  double uptime = 0;
  std::string basis;
  model::Sheet off, on;
  model::BuffEffect effect;
  double absorb_pct = 0, absorb_hit = 0;
};
std::string_view damage_label(int type, bool dot);
struct BuffView {
  model::Sheet sheet;     // buffs up half the time or more on, others off
  std::vector<BuffUse> buffs;
};
BuffView buff_view(const snap::Snapshot& s, const std::vector<Skill>& skills, const Settings& p, const arz::Database* db);

struct Result {
  Terms ratio;
  double percent = 0;
  std::string before, after;
  std::vector<std::string> notes;
  std::vector<Unscored> unscored;
  std::set<std::string> procs;
  bool pets = false;  // Class Pet damage is in Damage (experimental estimate, docs/MECHANICS.md §4)
  std::vector<effects::Reduction> rr_lost, rr_gained;
  std::vector<OtherChange> other_changes;
  // Each term's ln ratio split over parts, scaled by exponent; one part per stat.
  std::vector<Contribution> contributions;
  std::map<std::string, double> targets;  // stat -> threshold target in effect
};

// Mixes buff states; memo's inputs must outlive it unchanged.
struct BeforeSide;
using Memo = std::shared_ptr<const BeforeSide>;
Result compare(const snap::Snapshot& before, const snap::Snapshot& after, const std::vector<Skill>& skills,
               const Settings& p, const arz::Database* db, Memo* memo = nullptr);

}  // namespace gdc::score
