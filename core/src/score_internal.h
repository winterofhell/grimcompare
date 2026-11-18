#pragma once
// Private to the score*.cpp files.
#include <algorithm>
#include <charconv>
#include <cmath>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "gdc/attribute_names.h"
#include "gdc/classes.h"
#include "gdc/combat_formulas.h"
#include "gdc/enemy.h"
#include "gdc/score.h"
#include "gdc/utility.h"

namespace gdc::score {

using char_attr::kCunning, char_attr::kSpirit, char_attr::kTotalSpeed, char_attr::kAttackSpeed, char_attr::kCastSpeed;
using combat_attr::kBasePhysical, combat_attr::kPhysical, combat_attr::kPierce, combat_attr::kCold, combat_attr::kFire,
    combat_attr::kPoison, combat_attr::kLightning, combat_attr::kLife, combat_attr::kChaos, combat_attr::kAether,
    combat_attr::kBleeding, combat_attr::kElemental, combat_attr::kCritDamage, combat_attr::kAll;

inline constexpr std::pair<std::string_view, int> kThreatTypes[] = {
    {"physical", kPhysical}, {"pierce", kPierce},       {"cold", kCold},   {"fire", kFire},     {"poison", kPoison},
    {"lightning", kLightning}, {"vitality", kLife}, {"chaos", kChaos}, {"aether", kAether}, {"bleeding", kBleeding}};

// Types: re/CombatAttributeType.tsv. Game names differ from DB (MECHANICS "DB names").
struct DamageName {
  std::string_view key, label;
  int type;
  bool dot;
};
inline constexpr DamageName kDamageNames[] = {
    {"physical", "Physical", 2, false},   {"internal_trauma", "Internal Trauma", 2, true},
    {"pierce", "Pierce", 4, false},       {"cold", "Cold", 5, false},
    {"frostburn", "Frostburn", 5, true},  {"fire", "Fire", 6, false},
    {"burn", "Burn", 6, true},            {"acid", "Acid", 7, false},
    {"poison", "Poison", 7, true},        {"lightning", "Lightning", 8, false},
    {"electrocute", "Electrocute", 8, true}, {"vitality", "Vitality", 9, false},
    {"vitality_decay", "Vitality Decay", 9, true}, {"chaos", "Chaos", 10, false},
    {"aether", "Aether", 11, false},      {"bleeding", "Bleeding", 15, true}};

inline constexpr std::pair<int, std::string_view> kDotDuration[] = {
    {2, "offensiveSlowPhysicalDurationModifier"},  {5, "offensiveSlowColdDurationModifier"},
    {6, "offensiveSlowFireDurationModifier"},      {7, "offensiveSlowPoisonDurationModifier"},
    {8, "offensiveSlowLightningDurationModifier"}, {9, "offensiveSlowLifeDurationModifier"},
    {15, "offensiveSlowBleedingDurationModifier"}};

constexpr const char* kDefaultAttack = "records/skills/default/defaultweaponattack.dbr";  // weaponDamagePct 100
constexpr int kDamageTypes[] = {kPhysical, kPierce, kCold,  kFire,   kPoison,  kLightning,
                                 kLife,     kChaos,  kAether, kBleeding, kElemental};

inline bool damage_type(int t) { return std::find(std::begin(kDamageTypes), std::end(kDamageTypes), t) != std::end(kDamageTypes); }

inline double char_total(const snap::Snapshot& s, int type) {
  for (const auto& a : s.char_attributes)
    if (a.type == type) return a.total;
  return 0;
}

inline double char_modifier(const snap::Snapshot& s, int type) {
  for (const auto& a : s.char_attributes)
    if (a.type == type) return a.modifier;
  return 0;
}

inline const snap::CombatTotals* find(const std::vector<snap::CombatTotals>& v, int type) {
  for (const auto& r : v)
    if (r.type == type) return &r;
  return nullptr;
}

inline const snap::Skill* skill_by_record(const snap::Snapshot& s, const std::string& record) {
  for (const auto& k : s.skills)
    if (k.record == record) return &k;
  return nullptr;
}

// Levels past the array's end use its last value.
inline double at_level(const arz::Record& r, std::string_view key, std::uint32_t level) {
  const auto it = r.fields.find(key);
  if (it == r.fields.end() || it->second.numbers.empty() || level == 0) return 0;
  const auto& n = it->second.numbers;
  return n[std::min<std::size_t>(level, n.size()) - 1];
}

inline enemy::Enemy enemy_of(const snap::Snapshot& s) { return enemy::at(s.level, enemy::difficulty(s)); }
inline double enemy_da(const snap::Snapshot& s, const Settings& p) {
  return p.enemy_da_per_level > 0 ? p.enemy_da_per_level * s.level : enemy_of(s).da;
}
inline double enemy_oa(const snap::Snapshot& s, const Settings& p) {
  return p.enemy_oa_per_level > 0 ? p.enemy_oa_per_level * s.level : enemy_of(s).oa;
}
inline double enemy_hit(const snap::Snapshot& s, const Settings& p, bool physical) {
  if (p.enemy_hit_per_level > 0) return p.enemy_hit_per_level * s.level;
  const enemy::Enemy e = enemy_of(s);
  return std::max(1.0, physical ? e.hit_physical : e.hit);
}

inline formulas::Hit attack(const snap::Snapshot& s, const Settings& p, double crit_bonus, double* pth_out = nullptr) {
  const double pth = formulas::probability_to_hit(formulas::offensive_ability(s), enemy_da(s, p));
  if (pth_out) *pth_out = pth;
  return formulas::expected_hit(pth, s.pth, crit_bonus);
}

inline std::string fmt(double v, int decimals = 0) {
  char buf[400];  // fits %.*f of any double; to_chars rounds as printf does
  return {buf, std::to_chars(buf, buf + sizeof buf, v, std::chars_format::fixed, decimals).ptr};
}

inline std::string sig(double v) {
  char buf[48];  // as %.3g
  return {buf, std::to_chars(buf, buf + sizeof buf, v, std::chars_format::general, 3).ptr};
}

inline std::string stem(const std::string& record) {
  const auto slash = record.rfind('/');
  std::string s = slash == std::string::npos ? record : record.substr(slash + 1);
  if (s.ends_with(".dbr")) s.resize(s.size() - 4);
  return s;
}

using Key = std::pair<int, bool>;
using Amounts = std::map<Key, double>;

struct Raw {
  Amounts flat;            // dot: per second x duration
  Amounts pct;             // 58 / 60 spread
  std::map<int, double> duration;  // DoT duration %, by type
  std::vector<std::pair<std::string, Amounts>> sources;  // flat per record: the skill, then its modifiers
  double mult = 1;  // "Total Damage Modified by": Π (1 + offensiveDamageMultModifier / 100) of its records
  double weapon_pct = 0;
  classes::Speed speed = classes::Speed::kNone;  // of the skill's own record
  double crit = 0;
  double cooldown = 0;     // modifiers add negative values
  double leech = 0;        // own ADCtH plus modifiers' (Bramblevine)
  Conversions conversion;  // in the order CollectLocalConversionAttributes adds them
};

struct Gear {
  Amounts flat;
  std::map<int, double> dot;
  // DurDamage::Process 0x114206: duration × (1 + Σ duration % / 100), skill and gear % in one sum.
  double dot_scale(int type, double skill_pct = 0) const {
    const auto it = dot.find(type);
    return 1 + ((it != dot.end() ? it->second : 0) + skill_pct) / 100;
  }
};

struct SkillDamage {
  double value = 0;
  std::string inputs;
  double per_use = 0, cooldown = 0, uses = 0, rate = 0;
};

struct Buff {
  std::string record;
  double uptime = 0;
  std::string basis;
  model::BuffEffect effect;
  double absorb_pct = 0, absorb_hit = 0;  // damageAbsorptionPercent / 100, damageAbsorption / one hit
  bool matters() const { return !effect.empty() || absorb_pct > 0 || absorb_hit > 0; }
};

struct Procs {
  double cc = 0;
  std::vector<Buff> buffs;
  double restored = 0;
  std::string inputs;
  std::map<std::string, std::string> unknown;
  std::set<std::string> scored;
};

struct Survival {
  double ehp = 0;
  std::string inputs;
  double life = 0;
  double hit = 1;
  double avoid = 0;
  double block = 0;
  double reduction = 0;
  double cc = 0;
  double procs = 0;
  std::map<int, double> taken;
  std::map<std::string, double> thresholds;
  std::map<std::string, double> targets;  // second tier, as scaled
};

struct Other {
  double value = 1;
  std::string inputs;
  std::map<std::string, std::pair<double, double>> terms;
};

struct Utility {
  utility::Context context;
  utility::Values items;
};

struct Main {
  const snap::Skill* skill = nullptr;  // null: not in the snapshot
  Raw raw;
};

struct Side {
  effects::Collected effects;
  std::map<int, double> resist;
  Gear gear;
  std::vector<Main> mains;
  Utility utility;
  struct Hit {
    std::string record;
    bool pet = false;            // Class Pet: pet bonuses, not the player's
    bool player_scaled = false;  // uses the player's bonuses, like a proc
    double per_sec = 0;
    Amounts flat;                // Class Pet: with the pet's % and attribute bonuses applied
    double pet_hit = 1;          // Class Pet: expected hit multiplier at the pet's OA
  };
  std::vector<Hit> hits;
  std::map<std::string, std::string> hits_unknown;
};

// Procs and pets per second (MECHANICS §4).
// shortcut: no pet crit damage — revisit with a pet DPS test.
struct Extra {
  double per_sec = 0;
  std::string inputs;
};

// A main skill's damage after conversion, per final type: `base` takes the sheet % and attribute bonus, `bonus` is
// what the skill's own % adds (damage × skill % / 100, carried through conversion).
struct Pool {
  Amounts base, bonus;
};

// Converted once per conversion set; buff states mostly share one.
struct Converted {
  std::vector<snap::Conversion> conversions;
  std::vector<Pool> mains;     // per Side::mains entry, empty for one not learned
  std::vector<Amounts> hits;   // per Side::hits entry, empty for a pet's
};

struct Eval {
  std::vector<SkillDamage> skills;
  Extra extra;
  Survival survival;
  Other other;
};

struct Scored {
  Side side;
  Procs procs;
  std::vector<std::pair<double, Eval>> states;  // probabilities sum to 1
  std::string buffs;
  const Eval& shown() const {
    return std::max_element(states.begin(), states.end(), [](const auto& a, const auto& b) { return a.first < b.first; })->second;
  }
  // Geometric mean: ln X = Σ p × ln X_state.
  template <class F>
  double ln_mean(F f) const {
    double v = 0;
    for (const auto& [prob, e] : states) v += prob * std::log(f(e));
    return v;
  }
  template <class F>
  double mean(F f) const {
    double v = 0;
    for (const auto& [prob, e] : states) v += prob * f(e);
    return v;
  }
};

// score_skills.cpp
void add_flat(const arz::Database& db, const arz::Record& r, std::uint32_t level, double scale, Amounts& out);
void add_pair(Conversions& c, int from, int to, double pct);
bool has_damage(const arz::Database& db, const arz::Record& r, std::uint32_t level);
void add_record(const arz::Database& db, const arz::Record& r, std::uint32_t level, Raw& out,
                std::string_view source = {});
Raw skill_raw(const snap::Snapshot& s, const snap::Skill& sk, const arz::Database& db, std::vector<std::string>& notes);
bool skill_on(const snap::Skill& sk, const arz::Record& r);
bool attack_class(std::string_view cls);

// score_conversion.cpp
void split_elemental(Amounts& a);
Conversions global_pairs(const std::vector<snap::Conversion>& global);
Amounts convert_pairs(const Amounts& input, const Conversions& skill_pct, const Conversions& g,
                      std::vector<std::string>& notes, const Amounts* bonus = nullptr, Amounts* bonus_out = nullptr);

// score_damage.cpp
Amounts shared_base(const snap::Snapshot& before, const snap::Snapshot& after, const arz::Database& db);
Gear gear(const snap::Snapshot& s, const arz::Database& db, const Amounts& base);
// The skill's flat and weapon damage after its own and the global conversions.
Pool main_converted(const Raw& raw, const Gear& g, const Conversions& global, std::vector<std::string>& notes);
double attribute_bonus(double cunning, double spirit, int type, bool dot);
double use_rate(const snap::Snapshot& s, const snap::Skill& sk, const Raw& raw);
SkillDamage skill_damage(const snap::Snapshot& s, const snap::Skill& sk, const Raw& raw, const Gear& g, const Settings& p,
                         std::vector<std::string>& notes, Amounts* parts = nullptr,
                         const std::map<int, double>* resist = nullptr, bool explain = true,
                         const Pool* converted = nullptr);
std::string skill_parts(const snap::Snapshot& s, const Settings& p, const Raw& raw, const Gear& g,
                        const std::map<int, double>* resist, std::vector<std::string>& notes);
// Settings::offtarget_weight for a type no conversion can turn into one of the build's, else 1.
double type_weight(const Settings& p, Key k);
const Converted& converted_for(const snap::Snapshot& s, const Side& x, std::vector<Converted>* memo,
                               std::optional<Converted>& own, std::vector<std::string>& notes);
Extra extra_damage(const snap::Snapshot& s, const Side& x, const Settings& p, bool explain, const Converted& conv);

// score_survival.cpp
Survival survival(const snap::Snapshot& s, const Procs& x, double absorbed, const Settings& p, const arz::Database* db,
                  bool explain);

// score_other.cpp
Utility utility_of(const snap::Snapshot& s, const Settings& p, const arz::Database* db);
Other other(const snap::Snapshot& s, const Utility& u, const Settings& p, bool explain);

// score_side.cpp
Side side(const snap::Snapshot& s, const std::vector<Skill>& skills, const Settings& p, const arz::Database& db,
          const Amounts& base, std::vector<std::string>& notes, const snap::Snapshot* captured = nullptr);

// score_buffs.cpp
std::vector<Buff> learned_buffs(const snap::Snapshot& s, const Settings& p, const arz::Database& db);
Procs procs(const snap::Snapshot& s, const Side& x, const Settings& p, const arz::Database& db);
double state(const snap::Snapshot& s, const Procs& pr, const std::vector<Buff>& learned,
             const std::map<std::string, double>& shares, const arz::Database& db, snap::Snapshot& out);

// score_states.cpp
Scored score_side(const snap::Snapshot& s, const std::vector<Skill>& skills, const Settings& p, const arz::Database* db,
                  const Amounts& base, std::vector<std::string>& notes, const snap::Snapshot* captured = nullptr);
Survival mixed_survival(const Scored& sd);
Other mixed_other(const Scored& sd);

}  // namespace gdc::score
