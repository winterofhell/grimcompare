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

// `da`: the side's debuffs on the enemy.
inline formulas::Hit attack(const snap::Snapshot& s, const Settings& p, double crit_bonus, const effects::Shred& da,
                            double* pth_out = nullptr) {
  const double pth = formulas::probability_to_hit(formulas::offensive_ability(s), da.apply(enemy_da(s, p)));
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

// Retaliation before conversion: flat per hit by type (58 unspread, as convert_pairs() takes it), % by single type.
struct Retaliation {
  Amounts flat;
  std::map<int, double> pct;
};

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
  // % Retaliation Added to Attack per use: the retaliation it adds, flat already x the pct (retaliation_damage).
  Retaliation retaliation;
};

struct Gear {
  Amounts flat;
  std::map<int, double> dot;
  effects::Shred enemy_da;  // the side's debuffs; none for the auto skill pick
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
  double plain = 0;  // per use of the hits a weapon pool attack can take (Main::charges)
};

struct Buff {
  std::string record;
  double uptime = 0;
  std::string basis;
  model::BuffEffect effect;
  double absorb_pct = 0, absorb_hit = 0;  // damageAbsorptionPercent / 100, damageAbsorption / one hit
  double mult = 1;  // "Total Damage Modified by" while up: Π (1 + offensiveDamageMultModifier / 100)
  bool matters() const { return !effect.empty() || absorb_pct > 0 || absorb_hit > 0 || mult != 1; }
};

struct Procs {
  double cc = 0;
  effects::Shred enemy_oa;  // the side's debuffs on the enemy
  std::vector<std::pair<double, double>> enemy_damage;  // skills' enemy damage reduction %, uptime
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
  double oa_debuff = 1;  // part of `hit` from the side's OA debuffs on the enemy
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
  double weight = 0;
  double pool = 0;  // weapon pool skills: the main skill weight whose uses a Skill_WPAttack_* can take
  // Skill_WeaponPool_ChargedFinale (Cadence): skillChargeLevel plain weapon hits, then the skill's own hit. A weapon
  // pool attack can take a plain hit, never the finale.
  double charges = 0;
  Raw plain;  // a plain hit: 100% weapon damage
  double combo = 0;  // skillComboChargeLevel, the skill's and its modifiers': the combo charges its hits build up to
  double pool_share() const { return charges > 0 ? charges / (charges + 1) : 1; }
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
    double per_sec = 0;          // at Side::rates
    Amounts flat;                // Class Pet: with the pet's % and attribute bonuses applied
    // The record's own DoT, each kept apart: past one use per seconds (x the duration modifier) a use adds no DoT.
    struct Dot {
      Key key;
      double amount = 0, seconds = 0;
    };
    std::vector<Dot> dots;
    double pet_hit = 1;          // Class Pet: expected hit multiplier at the pet's OA
    Retaliation retaliation;     // as Raw::retaliation
    double pool_chance = 0;      // Skill_WPAttack_*: the share of weapon pool skill uses it can take
    effects::Effect::On on = effects::Effect::On::kOther;
    double chance = 0, recharge = 0;
    // Uses/s at the rates `now` (a buff state's speeds), from per_sec at `base`: 1 / (recharge + 1 / (chance x
    // rate)) scaled, so merged sources keep their sum.
    double rate(const effects::Rates& now, const effects::Rates& base) const {
      auto at = [&](const effects::Rates& r) {
        const double x = on == effects::Effect::On::kAttack ? r.action
                         : on == effects::Effect::On::kCrit ? r.action * r.crit : r.weapon;
        return x > 0 ? 1 / (recharge + 1 / (chance * x)) : 0;
      };
      if (on == effects::Effect::On::kOther || chance <= 0) return per_sec;
      const double b = at(base);
      return b > 0 ? per_sec * at(now) / b : 0;
    }
  };
  std::vector<Hit> hits;
  std::map<std::string, std::string> hits_unknown;
  effects::Rates rates;     // the trigger rates of the snapshot's own speeds
  Retaliation retaliation;  // per hit taken (retaliation_damage)
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
  double retaliation = 0;  // Raw::retaliation final, before the hit roll
};

// Converted once per conversion set; buff states mostly share one.
struct Converted {
  std::vector<snap::Conversion> conversions;
  std::vector<Pool> mains;     // per Side::mains entry, empty for one not learned
  struct Hit {
    Amounts flat;
    std::vector<std::pair<double, Amounts>> dots;  // Side::Hit::dots: seconds, converted
    double retaliation = 0;
  };
  std::vector<Hit> hits;       // per Side::hits entry, empty for a pet's
  double retaliation = 0;      // per hit taken, final
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
// Expected DoT seconds per use of each type the record deals (duration x chance; the longest when a type repeats).
std::map<int, double> dot_seconds(const arz::Record& r, std::uint32_t level);
void add_pair(Conversions& c, int from, int to, double pct);
bool has_damage(const arz::Database& db, const arz::Record& r, std::uint32_t level);
void add_record(const arz::Database& db, const arz::Record& r, std::uint32_t level, Raw& out,
                std::string_view source = {}, double scale = 1);
// Skill_WeaponPool_ChargedScaling::GetMaxChargeScale 0x5103C0: skillChargeMultipliers[skillChargeLevel - 1] / 100 (the
// last entry past the end), 1 without multipliers.
double max_charge_scale(const arz::Record& r, std::uint32_t level);
Raw skill_raw(const snap::Snapshot& s, const snap::Skill& sk, const arz::Database& db, std::vector<std::string>& notes);
// A skill's modifiers with their levels: learned ones at their level, item ones at 1.
using Modifiers = std::vector<std::pair<std::string, std::uint32_t>>;
Modifiers modifiers_of(const snap::Snapshot& s, const snap::Skill& sk);
bool skill_on(const snap::Skill& sk, const arz::Record& r);
bool attack_class(std::string_view cls);

// score_retaliation.cpp
// Settings::retaliation_damage: retaliation per hit taken into Side::retaliation, % Retaliation Added to Attack into
// the main skills' Raw::retaliation and the hits'.
void add_retaliation(const snap::Snapshot& s, const Settings& p, const arz::Database& db, const Procs& procs,
                     const std::vector<Buff>& learned, Side& side);
// Final amount: conversions, the retaliation % of the final type, the enemy's resists and the preset's type weights.
double retaliation_amount(const Retaliation& t, const Conversions& local, const Conversions& global, const Side& side,
                          const Settings& p, std::vector<std::string>& notes, Amounts* parts = nullptr);

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
const Converted& converted_for(const snap::Snapshot& s, const Side& x, const Settings& p, std::vector<Converted>* memo,
                               std::optional<Converted>& own, std::vector<std::string>& notes);
// `mult`: the state's buff damage multiplier, the player's own hits only. `now`: the state's trigger rates.
Extra extra_damage(const snap::Snapshot& s, const Side& x, const Settings& p, bool explain, const Converted& conv,
                   double mult, const effects::Rates& now);

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
// Cast buffs not in the totals at capture, and charged Skill_WeaponPool_ChargedScaling main skills of `side`.
std::vector<Buff> learned_buffs(const snap::Snapshot& s, const Side& side, const Settings& p, const arz::Database& db);
Procs procs(const snap::Snapshot& s, const Side& x, const Settings& p, const arz::Database& db);
// Returns the absorbed share; `mult`: the buffs' damage multiplier in this state.
double state(const snap::Snapshot& s, const Procs& pr, const std::vector<Buff>& learned,
             const std::map<std::string, double>& shares, const arz::Database& db, snap::Snapshot& out,
             double* mult = nullptr);

// score_states.cpp
Scored score_side(const snap::Snapshot& s, const std::vector<Skill>& skills, const Settings& p, const arz::Database* db,
                  const Amounts& base, std::vector<std::string>& notes, const snap::Snapshot* captured = nullptr);
Survival mixed_survival(const Scored& sd);
Other mixed_other(const Scored& sd);

}  // namespace gdc::score
