#include <algorithm>
#include <cmath>

#include "gdc/stat_model.h"

#include "score_internal.h"

namespace gdc::score {
namespace {

// No type-39 row: base absorption while any armor exists.
double absorption(const snap::Snapshot& s, const model::Sheet& sh, const arz::Database* db) {
  if (sh.absorption > 0) return sh.absorption;
  return sh.armor > 0 ? model::base_absorption(s, db) : 0;
}

constexpr int kBase[] = {kPierce, kCold, kFire, kPoison, kLightning, kLife, kChaos, kAether, kBleeding};
// Overcap targets cover these 9 base resists.
bool base_resist(int type) { return std::find(std::begin(kBase), std::end(kBase), type) != std::end(kBase); }

// Share scored under the next difficulty (MECHANICS §9 lookahead).
double lookahead(const snap::Snapshot& s, const Settings& p) {
  constexpr double kNext[] = {75, 94};  // Normal -> Elite, Elite -> Ultimate
  constexpr double kRamp = 10;
  const int base = enemy::difficulty(s) / 2;
  if (!p.difficulty_lookahead || base > 1) return 0;
  return std::clamp((s.level - (kNext[base] - kRamp)) / kRamp, 0.0, 1.0);
}

// One enemy resist reduction case and its damage share.
struct RrState {
  double share, rr;
  bool ahead = false;  // under the next difficulty's penalty
};

// Preset RR always on; else monster and boss shares (§9).
std::vector<RrState> rr_states(const snap::Snapshot& s, const Settings& p, double ahead) {
  const enemy::Enemy table = enemy_of(s);
  std::vector<RrState> states;
  const int difficulty = enemy::difficulty(s);
  if (p.resist_reduction && difficulty / 2 >= p.resist_reduction_from) {
    states.push_back({1, *p.resist_reduction});
  } else {
    const double b = p.boss_share;
    states.push_back({(1 - b) * (1 - table.rr_share) + b * (1 - table.boss_rr_share), 0});
    states.push_back({(1 - b) * table.rr_share, table.rr});
    states.push_back({b * table.boss_rr_share, table.boss_rr});
  }
  if (ahead > 0)
    for (std::size_t i = 0, n = states.size(); i < n; ++i) {
      states.push_back({states[i].share * ahead, states[i].rr, true});
      states[i].share *= 1 - ahead;
    }
  return states;
}

// RR only lowers the 9 base resists.
double rr_on(int type, const RrState& st, int difficulty) {
  if (!base_resist(type)) return 0.0;
  return st.rr + (st.ahead ? enemy::penalty(type, difficulty + 2) - enemy::penalty(type, difficulty) : 0);
}

// Damage share per threat type; armor after physical resist.
double damage_taken(const snap::Snapshot& s, const model::Sheet& sh, const Settings& p,
                    const std::vector<RrState>& states, double physical_hit, double armor_absorb, Survival& out) {
  const int difficulty = enemy::difficulty(s);
  const auto regions = model::region_armor(s);
  double shares = 0;
  for (const auto& [type, share] : p.threat) shares += share;
  double taken = 0;
  for (const auto& [type, share] : p.threat) {
    const auto x = std::find_if(sh.resists.begin(), sh.resists.end(), [&](const auto& r) { return r.type == type; });
    double f = 0;
    for (const RrState& st : states) {
      const double rr = rr_on(type, st, difficulty);
      const double r = x != sh.resists.end() ? std::min(x->uncapped - rr, x->max)
                                             : std::min(-rr, static_cast<double>(s.defense_cap));
      f += st.share * std::max(0.0, 1 - r / 100);
    }
    if (type == kPhysical) {
      const double d = physical_hit * f;
      double left = 0;
      for (const auto& [chance, armor] : regions) left += chance * (d - armor_absorb * std::min(d, armor));
      f = left / physical_hit;
    }
    out.taken[type] = share * f / shares;
    taken += out.taken[type];
  }
  return taken;
}

// [thresholds] targets scale EHP; base resists use a geometric mean.
void apply_thresholds(const snap::Snapshot& s, const model::Sheet& sh, const Settings& p,
                      const std::vector<RrState>& states, double armor_absorb, bool explain, Survival& out) {
  const int difficulty = enemy::difficulty(s);
  auto resist = [&](int type) -> const model::Resist* {
    const auto x = std::find_if(sh.resists.begin(), sh.resists.end(), [&](const auto& r) { return r.type == type; });
    return x != sh.resists.end() ? &*x : nullptr;
  };
  auto shown = [&](int type, double rr = 0) {
    const model::Resist* x = resist(type);
    return x ? std::min(x->uncapped - rr, x->max) : -rr;
  };
  // Unverified: sheet = defense x (1 + mod%); 16 Slow, 57 Reflect.
  auto plain = [&](int type) {
    for (const auto& r : s.defense)
      if (r.type == type) return r.defense * (1 + r.defense_modifier / 100.0);
    return 0.0;
  };
  double f_all = 1;
  std::string terms;
  for (const auto& [name, target] : p.thresholds) {
    double f = 1, value = 0;
    if (name == "resist" || name == "resist_overcap") {
      for (int t : kBase) {
        const model::Resist* x = resist(t);
        double ft = 1;
        for (const RrState& st : states) {
          const double v = name == "resist" ? shown(t, rr_on(t, st, difficulty)) : x ? x->uncapped - x->max : -s.defense_cap;
          value += st.share * v / std::size(kBase);
          ft *= std::pow(threshold_factor(v, target, p), st.share * p.threshold_resist_weight / std::size(kBase));
        }
        f *= ft;
        if (name == "resist") out.thresholds["resist:" + std::to_string(t)] = ft;  // one part per resist
      }
    } else {
      // Absorption is a percentage of armor, not an amount that grows with level.
      const double scaled = p.threshold_level > 0 && s.level < p.threshold_level && name != "absorption"
                                ? target * s.level / p.threshold_level
                                : target;
      value = name == "physical_resist"     ? shown(kPhysical)
              : name == "absorption"        ? armor_absorb * 100
              : name == "armor"             ? sh.armor
              : name == "stun_resist"       ? shown(42)
              : name == "freeze_resist"     ? shown(45)
              : name == "slow_resist"       ? plain(16)
              : name == "reflect_reduction" ? plain(57)
              : name == "life"              ? sh.life
                                            : sh.da;
      f = std::pow(threshold_factor(value, scaled, p), p.threshold_secondary_weight);
      out.targets[name] = scaled;
    }
    if (name != "resist") out.thresholds[name] = f;
    f_all *= f;
    if (explain) terms += " " + name + " " + fmt(value) + "/" + fmt(out.targets.contains(name) ? out.targets[name] : target) + " x" + fmt(f, 3);
  }
  out.ehp *= f_all;
  if (explain) out.inputs += "; thresholds" + terms + " = x" + fmt(f_all, 3);
}

}  // namespace

// Engine defense order, TakeAttack 0x11D950 (MECHANICS §9).
Survival survival(const snap::Snapshot& s, const Procs& procs, double absorbed, const Settings& p,
                  const arz::Database* db, bool explain) {
  const model::Sheet sh = model::sheet(s);
  Survival out;
  out.life = sh.life;
  const double pth = formulas::probability_to_hit(enemy_oa(s, p), sh.da);
  // Monster crit damage: difficulty bonus only (ultimateChallangeAdjustment).
  const formulas::Hit h = formulas::expected_hit(pth, s.pth, enemy_of(s).crit_damage / 100);
  out.hit = h.multiplier;
  out.avoid = utility::avoid(s, p.melee_share);
  out.block = utility::block(s, db, enemy_hit(s, p, false), p.incoming_hits_per_sec);
  out.reduction = p.damage_reduction_uptime * utility::damage_reduction(s);
  const double physical_hit = enemy_hit(s, p, true);
  const double armor_absorb = std::min(100.0, absorption(s, sh, db)) / 100;
  const double ahead = lookahead(s, p);
  const std::vector<RrState> states = rr_states(s, p, ahead);
  const double taken = damage_taken(s, sh, p, states, physical_hit, armor_absorb, out);
  out.cc = p.cc_share * procs.cc;
  std::string rr_terms;
  for (const RrState& st : states)
    if (explain && !st.ahead && (st.rr != 0 || states.size() == 1)) rr_terms += " " + fmt(st.rr, 1) + " x" + fmt(st.share, 3);
  if (explain && ahead > 0) rr_terms += " ahead x" + fmt(ahead, 2);
  double per_attack = out.hit * (1 - out.avoid) * (1 - out.block) * (1 - out.reduction) * (1 - out.cc) * taken;
  // Heals cover at most 90% of life lost (design choice).
  const double lost = p.incoming_hits_per_sec * enemy_hit(s, p, false) * per_attack * absorbed;
  const double restored = lost > 0 ? std::min(0.9, procs.restored / lost) : 0;
  out.procs = 1 - absorbed * (1 - restored);
  per_attack = std::max(1e-6, per_attack * (1 - out.procs));
  out.ehp = sh.life / per_attack;
  if (explain)
    out.inputs = "life " + fmt(sh.life) + " da " + fmt(sh.da) + " enemy pth " + fmt(pth) + " x" + fmt(out.hit, 3) +
               " avoid " + fmt(out.avoid, 3) + " block " + fmt(out.block, 3) + " reduced " + fmt(out.reduction, 3) +
               (procs.inputs.empty() ? "" : " cc " + fmt(out.cc, 3) + " procs " + fmt(out.procs, 3) + " (" + procs.inputs + ")") +
               " armor " + fmt(sh.armor) + " abs " + fmt(armor_absorb * 100) + "% hit " + fmt(physical_hit) + " enemy rr" + rr_terms +
               " taken " + fmt(taken, 3) +
               " ehp " + fmt(out.ehp);
  if (!p.thresholds.empty()) apply_thresholds(s, sh, p, states, armor_absorb, explain, out);
  return out;
}

double threshold_factor(double value, double target, const Settings& p) {
  if (target <= 0) return 1;
  const double u = value / target - 1, z = -u / p.threshold_width;
  const double softplus = z > 30 ? z : std::log1p(std::exp(z));  // no overflow
  return std::exp(p.threshold_above * u - (p.threshold_below - p.threshold_above) * p.threshold_width * softplus);
}

}  // namespace gdc::score
