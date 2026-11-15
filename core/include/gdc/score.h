#pragma once
// score = Damage^a x Survivability^b x Other^c; change % = (after / before - 1) x 100.
// Only ratios are reported, so terms need no absolute unit. Sources: docs/MECHANICS.md §5, defense order §9.
#include <cstdint>
#include <map>
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

// (CombatAttributeType, is duration). Acid = Poison type 7 direct, poison = type 7 duration (ERRATA §0).
using DamageKey = std::pair<int, bool>;
std::optional<DamageKey> damage_key(std::string_view name);
std::string damage_label(DamageKey k);

struct Weight {
  double weight = 0;
  std::vector<std::string> when;  // utility::holds conditions; empty = always
  bool operator==(const Weight&) const = default;
};
std::map<std::string, Weight> default_other();

// Every number here is a design choice unless it cites a source (docs/PRESETS.md).
struct Settings {
  double damage_weight = 1.0, survival_weight = 1.0, other_weight = 0.25;  // a, b, c
  double negligible_percent = 0.1;
  double threshold_below = 1.0, threshold_above = 0.05, threshold_width = 0.05;
  // The 9 base resists share one threshold term (geometric mean of their factors) raised to this: 1 = together
  // they count like one other threshold, 9 = each resist counts like one.
  double threshold_resist_weight = 1.0;
  // recast_buffs: uptime = min(1, duration / (recharge x (1 - CDR))); else [uptime] from the record.
  // live_buffs: take each buff as the snapshot caught it.
  bool recast_buffs = true, live_buffs = false;
  std::vector<std::string> skills;  // empty = auto
  bool weigh_by_points = false;
  int auto_skills = 3;
  double filler_weight = 0.5;
  double enemy_da_per_level = 0;    // 0 = monster table (gdc/enemy.h)
  double pet_attack_rate = 0;       // 0 = unknown, pet effects score 0
  double kills_per_sec = 0.5;
  std::vector<std::pair<int, int>> conversion_warn;  // from -> to; 0 = any
  double enemy_oa_per_level = 0;    // 0 = monster table
  double enemy_hit_per_level = 0;   // 0 = monster table
  double melee_share = 0.5;         // melee hits face dodge, the rest deflect
  double damage_reduction_uptime = 0.5;
  // Enemy resistance reduction on the 9 base resists. Empty = the monster table's (gdc/enemy.h): its rr for
  // its rr_share of the incoming damage, bosses' boss_rr for boss_share x boss_rr_share. A number is always on.
  std::optional<double> resist_reduction;
  int resist_reduction_from = 0;    // game difficulty (0..2) from which the number applies; below it: auto
  double boss_share = 0.15;         // share of incoming damage from Quest / Boss / Nemesis / SuperBoss enemies
  // On Normal and Elite: score the base resists also under the next difficulty's extra penalty, for a share
  // that ramps from 0 to 1 over the last levels before that difficulty (score.cpp lookahead()).
  bool difficulty_lookahead = true;
  double incoming_hits_per_sec = 2;
  double low_health_per_sec = 0.05;
  double cc_share = 0.1;
  std::map<int, double> threat = {  // CombatAttributeType -> share of incoming damage (normalized)
      {2, 0.20}, {4, 0.05}, {5, 0.10}, {6, 0.10}, {7, 0.10}, {8, 0.10},
      {9, 0.10}, {10, 0.10}, {11, 0.10}, {15, 0.05}};
  std::map<std::string, Weight> other = default_other();
  std::string stage;
  // Mean defensive<Type> of the 48 Boss records under records/creatures/enemies/ (database.arz 1.3.1.1);
  // enemy::resist adds the difficulty's share.
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
  std::vector<DamageKey> match_damage;
  double match_share = 0.5;
  std::string match_character;
  std::vector<std::string> match_characters;  // always match, skipping every other check
  std::string match_archetype;
  std::vector<Stage> stages;           // file order
  std::vector<std::string> target_sets;

  bool operator==(const Preset&) const = default;
};

// Unknown keys, wrong types and out-of-range values are errors. Never throws.
std::optional<Preset> parse(std::string_view toml, std::string& error);

// Last matching stage in file order.
const Stage* stage(const Preset& p, const snap::Snapshot& s);
const Settings& settings(const Preset& p, const snap::Snapshot& s);

struct Match {
  bool ok = false;
  std::string why;
};
Match match(const Preset& p, const snap::Snapshot& s, const arz::Database& db, const Labels& labels = {});

// A build's own preset before a template that lists all ten masteries.
bool tried_first(const Preset& a, const Preset& b);

// Pet summons are Skill_SpawnPet / Skill_TargetedSpawnPet whose spawnObjects has Class Pet; the
// PetPlayerScaling ones (mines, totems, seals, Wind Devils) are not pets. Modifiers count for their parent.
// Thresholds in archetype() are design choices.
struct Archetype {
  std::string name;
  std::string why;
};
Archetype archetype(const snap::Snapshot& s, const arz::Database& db);

std::map<DamageKey, double> damage_profile(const snap::Snapshot& s, const arz::Database& db);

struct Skill {
  std::string record;
  double weight = 0;
};

// Smooth threshold, no step: ln f = above x u - (below - above) x width x ln(1 + e^(-u / width)),
// u = value / target - 1.
double threshold_factor(double value, double target, const Settings& p);

struct Conversion {
  std::string skill;  // empty: gear conversion
  int from = 0, to = 0;
  double percent = 0;
};
std::vector<Conversion> conversion_warnings(const snap::Snapshot& before, const snap::Snapshot& after,
                                            const Settings& p, const std::vector<DamageKey>& targets,
                                            const arz::Database* db);
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
  model::Sheet sheet;     // buffs up at least half the time on, the rest off
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
  std::vector<effects::Reduction> rr_lost, rr_gained;
  std::vector<OtherChange> other_changes;
  // Each term's ln ratio split over its parts and scaled by the term's exponent, so the parts sum to
  // ln(1 + percent / 100) x 100.
  std::vector<Contribution> contributions;
};

// Buffs that are not always up split each side into states. Terms are computed per state and mixed
// geometrically (ln X = sum p x ln X_state, buffs independent) so caps see real sheets and the reverse swap
// stays exact. Only the kMaxEnumerated buffs with the largest uptime x (1 - uptime) are enumerated.
Result compare(const snap::Snapshot& before, const snap::Snapshot& after, const std::vector<Skill>& skills,
               const Settings& p, const arz::Database* db);

}  // namespace gdc::score
