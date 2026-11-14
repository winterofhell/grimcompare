#include "gdc/utility.h"

#include <algorithm>
#include <cmath>
#include <set>

#include "gdc/attribute_names.h"
#include "gdc/db_fields.h"
#include "gdc/stat_model.h"

namespace gdc::utility {
namespace {

// Weights are design choices (docs/PRESETS.md "Other"); the value sources are in values() below.
constexpr Stat kStats[] = {
    {"run_speed", "100% run speed", 0.5, ""},
    {"life_regen", "1% of Life regenerated per second", 0.02, ""},
    {"energy_regen", "10 Energy regenerated per second", 0.02, ""},
    {"life_leech", "100% of attack damage converted to health", 0.3, ""},
    {"stun_resist", "100% reduced stun duration", 0.1, ""},
    {"freeze_resist", "100% reduced freeze duration", 0.05, ""},
    {"petrify_resist", "100% reduced petrify duration", 0.05, ""},
    {"sleep_resist", "100% reduced sleep duration", 0.05, ""},
    {"trap_resist", "100% reduced entrapment duration", 0.05, ""},
    {"knockdown_resist", "100% reduced knockdown duration", 0.05, ""},
    {"slow_resist", "100% slow resistance", 0.05, ""},
    {"disruption_resist", "100% skill disruption protection", 0.05, ""},
    {"cooldown_reduction", "100% skill cooldown reduction", 0.5, ""},
    {"skill_cost_reduction", "100% skill energy cost reduction", 0.1, ""},
    {"experience", "100% experience gained, x (1 - level / max level)", 0.2, ""},
    {"retaliation", "1000 retaliation damage", 0.02, ""},
    {"retaliation_modifier", "100% retaliation damage", 0.02, ""},
    {"pet_bonus", "100% mean pet bonus", 0.2, "pets"},
    {"default", "100 of any stat no other key or score term reads", 0.05, ""},
};

// CharAttributeType / CombatAttributeType values (re/CharAttributeType.tsv, re/CombatAttributeType.tsv).
constexpr int kBlockRecovery = 25, kDodge = 27, kDeflect = 28, kExperience = 55;
constexpr int kLifeLeech = 20, kTotalDamageReduction = 28;
constexpr int kBlockChance = 55, kBlockAmount = 56;
constexpr int kCooldown = 1, kCost = 2;  // SkillAttributeType

// Read by another score term (Damage, Survivability, thresholds) or by a key here. Char 8 ManaLimitReserve is
// Energy reserved by toggled skills (more is worse): left out.
const std::set<int> kCharCovered = {1, 2, 3, 4, 5, 6, 7, 8, 13, 14, 15, 16, 17, 21, 22, 23, 24, kBlockRecovery, kDodge, kDeflect, kExperience};
const std::set<int> kDefenseCovered = {0,  1,  2,  4,  5,  6,  7,  8,  9,  10, 11, 13, 15, 16, 38, 39, 40, 42,
                                       43, 44, 45, 46, 48, kBlockChance, kBlockAmount, 57, 58, 60, 64, 65};
const std::set<int> kDamageCovered = {1, 2, 4, 5, 6, 7, 8, 9, 10, 11, 15, 58, 59, 60};
// Items' offensive entries: flat damage (Damage), life leech, the damage-reduction debuff, RR (gdc::effects).
const std::set<int> kOffenseCovered = {1, 2, 4, 5, 6, 7, 8, 9, 10, 11, 15, 58, kLifeLeech, kTotalDamageReduction, 32, 33, 34, 35, 36, 37};

const snap::Item* at(const snap::Snapshot& s, int location) {
  for (const auto& it : s.equipped)
    if (it.location == location) return &it;
  return nullptr;
}

std::string cls(const snap::Snapshot& s, int location, const arz::Database* db) {
  const snap::Item* it = at(s, location);
  const arz::Record* r = it && db ? db->record(it->record) : nullptr;
  return r ? r->cls : std::string{};
}

double char_total(const snap::Snapshot& s, int type) {
  for (const auto& a : s.char_attributes)
    if (a.type == type) return a.total;
  return 0;
}

// Not reprocessed by the swap model: defense + modifier (only one is set for these types). shortcut: which of
// the two the engine fills for types 55 / 56 is not traced — revisit with a shield snapshot.
double defense_raw(const snap::Snapshot& s, int type) {
  for (const auto& r : s.defense)
    if (r.type == type) return static_cast<double>(r.defense) + r.defense_modifier;
  return 0;
}

// The sheet value of a cappable resist (stun, freeze, ...), else defense x (1 + modifier %).
double resist(const model::Sheet& sh, const snap::Snapshot& s, int type) {
  for (const auto& r : sh.resists)
    if (r.type == type) return r.shown;
  for (const auto& r : s.defense)
    if (r.type == type) return r.defense * (1 + r.defense_modifier / 100.0);
  return 0;
}

std::string label(std::string_view what, int type, std::string_view name) {
  return std::string(what) + " " + std::to_string(type) + (name.empty() ? "" : " " + std::string(name));
}

// Mean of the % fields a pet bonus record maps (gdc::fields), as a fraction. shortcut: a pet bonus is weighed
// by its mean % only — revisit with a pet damage / survival model.
double pet_bonus(const arz::Record& r) {
  double sum = 0;
  int n = 0;
  for (const auto& [key, field] : r.fields) {
    if (field.kind != arz::Field::Kind::Int && field.kind != arz::Field::Kind::Float) continue;
    if (field.numbers.empty() || field.numbers[0] == 0) continue;
    double scale = 1;
    if (!fields::lookup(key, scale)) continue;
    sum += field.numbers[0] * scale;
    ++n;
  }
  return n ? sum / n / 100 : 0;
}

}  // namespace

// Fraction of an incoming hit's damage the shield blocks, CombatManager::TakeAttack (0x11EDAC..0x11EF4C,
// Game.dll 1.3.1.1): no block while the recovery timer (CombatManager+0x2D0) runs; chance = meleeBlockEquation
// (blockChanceDV + blockChanceModifierDV: shield defensiveBlockChance + defense type 55, combatformulas.dbr);
// on a block every damage attribute goes through DesignerCalculateShieldBlockDamageReduction
// (shieldDamageReductionEquationDGB / DLEB: damage above the block amount loses amount x absorption, damage
// below it loses damage x absorption), amount = shield defensiveBlock x (1 + defense type 56 / 100),
// absorption = blockAbsorption; then the timer = blockRecoveryTime x (1 - char 25 / 100).
// shortcut: the hit is taken as one damage attribute of size `hit` — revisit with multi-type enemy hits.
double block(const snap::Snapshot& s, const arz::Database* db, double hit, double hits_per_sec) {
  const snap::Item* lh = at(s, 1);
  const arz::Record* r = lh && db ? db->record(lh->record) : nullptr;
  if (!r || r->cls != "WeaponArmor_Shield" || hit <= 0) return 0;
  const double chance = std::clamp((r->number("defensiveBlockChance") + defense_raw(s, kBlockChance)) / 100, 0.0, 1.0);
  const double absorb = r->number("blockAbsorption", 0, 100) / 100;
  const double amount = r->number("defensiveBlock") * (1 + defense_raw(s, kBlockAmount) / 100);
  const double recovery = r->number("blockRecoveryTime") * std::max(0.0, 1 - char_total(s, kBlockRecovery) / 100);
  double share = chance;  // blocked hits per incoming hit
  if (hits_per_sec > 0 && recovery > 0) share = std::min(chance, 1 / (recovery * hits_per_sec));
  return share * absorb * std::min(amount, hit) / hit;
}

double avoid(const snap::Snapshot& s, double melee_share) {
  return std::clamp((melee_share * char_total(s, kDodge) + (1 - melee_share) * char_total(s, kDeflect)) / 100, 0.0, 1.0);
}

double damage_reduction(const snap::Snapshot& s) {
  double best = 0;
  for (const auto& it : s.equipped)
    if (it.stats)
      for (const auto& d : it.stats->offensive_damage)
        if (d.type == kTotalDamageReduction) best = std::max(best, static_cast<double>(d.min));
  return std::clamp(best / 100, 0.0, 1.0);
}

std::span<const Stat> stats() { return kStats; }

const Stat* stat(std::string_view key) {
  for (const auto& s : kStats)
    if (s.key == key) return &s;
  return nullptr;
}

Context context(const snap::Snapshot& s, const arz::Database* db, std::string stage) {
  Context c;
  c.weapon = cls(s, 0, db);
  c.offhand = cls(s, 1, db);
  c.shield = c.offhand == "WeaponArmor_Shield";
  c.stage = std::move(stage);
  if (db)
    for (const auto* list : {&s.skills, &s.item_skills})
      for (const auto& k : *list)
        if (k.current_level > 0)
          if (const arz::Record* r = db->record(k.record); r && (r->cls == "Skill_SpawnPet" || r->cls == "Skill_TargetedSpawnPet"))
            c.pets = true;
  return c;
}

bool valid_when(std::string_view w) {
  if (w.starts_with('!')) w.remove_prefix(1);
  return w.empty() || w == "shield" || w == "offhand" || w == "pets" ||
         (w.starts_with("weapon:") && w.size() > 7) || (w.starts_with("stage:") && w.size() > 6);
}

bool holds(std::string_view w, const Context& c) {
  const bool neg = w.starts_with('!');
  if (neg) w.remove_prefix(1);
  bool v = true;
  if (w == "shield") v = c.shield;
  else if (w == "offhand") v = c.offhand == "WeaponArmor_Offhand";
  else if (w == "pets") v = c.pets;
  else if (w.starts_with("weapon:")) v = c.weapon == w.substr(7);
  else if (w.starts_with("stage:")) v = c.stage == w.substr(6);
  return neg ? !v : v;
}

Values values(const snap::Snapshot& s, const arz::Database* db) {
  const model::Sheet sh = model::sheet(s);
  Values out;
  auto& k = out.keys;
  k["run_speed"] = sh.run_speed;
  k["life_regen"] = sh.life > 0 ? sh.life_regen / sh.life * 100 : 0;
  k["energy_regen"] = sh.energy_regen / 10;
  k["stun_resist"] = resist(sh, s, 42) / 100;
  k["sleep_resist"] = resist(sh, s, 43) / 100;
  k["trap_resist"] = resist(sh, s, 44) / 100;
  k["freeze_resist"] = resist(sh, s, 45) / 100;
  k["petrify_resist"] = resist(sh, s, 46) / 100;
  k["knockdown_resist"] = resist(sh, s, 48) / 100;
  k["slow_resist"] = resist(sh, s, 16) / 100;  // type 16 "TotalSpeed" read as Slow Resistance (unverified)
  k["disruption_resist"] = resist(sh, s, 13) / 100;
  // records/creatures/pc/playerlevels.dbr maxPlayerLevel (100 in 1.3.1.1): no experience to gain at the cap.
  const arz::Record* levels = db ? db->record("records/creatures/pc/playerlevels.dbr") : nullptr;
  const double max_level = levels ? levels->number("maxPlayerLevel") : 0;
  k["experience"] = max_level > 0 ? char_total(s, kExperience) / 100 * std::max(0.0, 1 - s.level / max_level) : 0;

  // Item-only stats (the snapshot has no character total for them). shortcut: skills' parts are not seen —
  // revisit when the capture reads the character's skill attribute / offensive accumulators.
  double leech = 0, cdr = 0, cost = 0, ret = 0, ret_mod = 0, pets = 0;
  for (const auto& it : s.equipped) {
    if (it.stats) {
      for (const auto& d : it.stats->offensive_damage) {
        if (d.type == kLifeLeech) leech += d.min;
        else if (!kOffenseCovered.contains(d.type))
          out.unlisted[label("item offense", d.type, names::combat_attribute(d.type))] += d.min / 100;
      }
      for (const auto& a : it.stats->skill_attributes) {
        if (a.type == kCooldown) cdr += a.value;
        else if (a.type == kCost) cost += a.value;
        else out.unlisted[label("skill", a.type, names::skill_attribute(a.type))] += a.value / 100;
      }
      for (const auto& d : it.stats->retaliation) ret += (d.min + d.max) / 2;
      for (const auto& m : it.stats->retaliation_modifiers) ret_mod = std::max(ret_mod, static_cast<double>(m.damage_modifier));
    }
    if (db)
      for (const auto& r : model::item_records(it))
        if (const arz::Record* rec = db->record(r))
          if (const std::string pb(rec->string("petBonusName")); !pb.empty())
            if (const arz::Record* b = db->record(pb)) pets += pet_bonus(*b);
  }
  k["life_leech"] = leech / 100;
  k["cooldown_reduction"] = cdr / 100;
  k["skill_cost_reduction"] = cost / 100;
  k["retaliation"] = ret / 1000;
  k["retaliation_modifier"] = ret_mod / 100;
  k["pet_bonus"] = pets;

  for (const auto& a : s.char_attributes)
    if (!kCharCovered.contains(a.type) && a.total != 0)
      out.unlisted[label("char", a.type, names::char_attribute(a.type))] += a.total / 100.0;
  for (const auto& r : s.defense)
    if (!kDefenseCovered.contains(r.type) && (r.defense != 0 || r.defense_modifier != 0))
      out.unlisted[label("defense", r.type, names::combat_attribute(r.type))] += (r.defense + r.defense_modifier) / 100.0;
  for (const auto& r : s.damage_modifiers)
    if (!kDamageCovered.contains(r.type) && (r.damage_modifier != 0 || r.dot_modifier != 0))
      out.unlisted[label("damage %", r.type, names::combat_attribute(r.type))] += (r.damage_modifier + r.dot_modifier) / 100.0;
  return out;
}

}  // namespace gdc::utility
