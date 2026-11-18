#include "gdc/utility.h"

#include <algorithm>
#include <cmath>
#include <set>

#include "gdc/attribute_names.h"
#include "gdc/db_fields.h"
#include "gdc/effects.h"
#include "gdc/enemy.h"
#include "gdc/stat_model.h"

namespace gdc::utility {
namespace {

// Weights are design choices (docs/PRESETS.md "Other").
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
    {"experience", "100% experience gained, x (1 - level / max level)", 0.2, ""},
    {"retaliation", "1000 retaliation damage", 0.02, ""},
    {"retaliation_modifier", "100% retaliation damage", 0.02, ""},
    {"pet_bonus", "100% mean pet bonus, defensive part (pet damage is in Damage)", 0.2, "pets"},
    {"racial_damage", "100% more damage to every enemy (racial bonus % x the listed races' share)", 1.0, ""},
    {"racial_defense", "100% less damage from every enemy (racial bonus % x the listed races' share)", 1.0, ""},
    {"default", "100 of any stat no other key or score term reads", 0.05, ""},
};

using char_attr::kBlockRecovery, char_attr::kDodge, char_attr::kDeflect, char_attr::kExperience;
using combat_attr::kLifeLeech, combat_attr::kTotalDamageReduction, combat_attr::kBlockChance, combat_attr::kBlockAmount;
constexpr int kCooldown = 1, kCost = 2;

// Read elsewhere or keyed here.
const std::set<int> kCharCovered = {1, 2, 3, 4, 5, 6, 7, 8, 13, 14, 15, 16, 17, 21, 22, 23, 24, kBlockRecovery, kDodge, kDeflect, kExperience};
const std::set<int> kDefenseCovered = {0,  1,  2,  4,  5,  6,  7,  8,  9,  10, 11, 13, 15, 16, 17, 18, 19, 38, 39,
                                       40, 42, 43, 44, 45, 46, 48, kBlockChance, kBlockAmount, 57, 58, 60, 64, 65};
const std::set<int> kDamageCovered = {1, 2, 4, 5, 6, 7, 8, 9, 10, 11, 15, 58, 59, 60};
const std::set<int> kOffenseCovered = {1, 2, 4, 5, 6, 7, 8, 9, 10, 11, 15, 58, kLifeLeech, kTotalDamageReduction, 32, 33, 34, 35, 36, 37};

// GetValue 0x51F1A0 without chance parts; pre-schema 9: items only.
template <class F>
void for_skill_attributes(const snap::Snapshot& s, F f) {
  for (const auto& a : s.skill_attributes) f(a.type, a.value + std::abs(a.value) * a.modifier / 100.0);
  if (s.schema < 9)
    for (const auto& it : s.equipped)
      if (it.stats)
        for (const auto& a : it.stats->skill_attributes) f(a.type, static_cast<double>(a.value));
}

double skill_attribute(const snap::Snapshot& s, int type) {
  double out = 0;
  for_skill_attributes(s, [&](int t, double v) { if (t == type) out += v; });
  return out;
}

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

// shortcut: which of defense / modifier the engine fills for types 55 / 56 is not traced — revisit with a shield
// snapshot.
double defense_raw(const snap::Snapshot& s, int type) {
  for (const auto& r : s.defense)
    if (r.type == type) return static_cast<double>(r.defense) + r.defense_modifier;
  return 0;
}

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

// The parts score::Damage reads for a Class Pet's hits (damage %, Cunning, Spirit, speeds, OA) are left to it;
// shortcut: the rest (life, resists, DA, regen, ...) is weighed by its mean % only — revisit with a pet survival
// model.
// racialBonusRace: the share of the monster table the bonus applies to (enemy::race_share). The engine reads the
// first value of racialBonusPercentDamage / Defense (0x33FC00: [entry+0x20] / [entry+0x38], element 0).
double racial_share(const arz::Record& r) {
  const auto it = r.fields.find("racialBonusRace");
  return it == r.fields.end() ? 0 : enemy::race_share(it->second.strings);
}

// Char pet bonuses left out of the mean.
const std::set<int> kPetSkipped = {char_attr::kCunning,     char_attr::kSpirit,     char_attr::kTotalSpeed,
                                  char_attr::kAttackSpeed, char_attr::kCastSpeed, char_attr::kOffensiveAbility,
                                  char_attr::kOffensiveAbilityModifier};

double pet_bonus(const arz::Record& r) {
  double sum = 0;
  int n = 0;
  for (const auto& [key, field] : r.fields) {
    if (field.kind != arz::Field::Kind::Int && field.kind != arz::Field::Kind::Float) continue;
    if (field.numbers.empty() || field.numbers[0] == 0) continue;
    double scale = 1;
    const fields::Target* t = fields::lookup(key, scale);
    if (!t || t->kind == fields::Kind::Damage) continue;
    if (t->kind == fields::Kind::Char && kPetSkipped.contains(t->type)) continue;
    sum += field.numbers[0] * scale;
    ++n;
  }
  return n ? sum / n / 100 : 0;
}

}  // namespace

// CombatManager::TakeAttack 0x11EDAC..0x11EF4C: no block while the recovery timer (+0x2D0) runs; chance =
// meleeBlockEquation (defensiveBlockChance + defense 55). A block runs every damage attribute through
// DesignerCalculateShieldBlockDamageReduction (DGB / DLEB): above the block amount it loses amount x absorption, below
// it damage x absorption; amount = defensiveBlock x (1 + defense 56 / 100). Timer = blockRecoveryTime x
// (1 - char 25 / 100).
// shortcut: the hit is one damage attribute of size `hit` — revisit with multi-type enemy hits.
double block_share(const snap::Snapshot& s, const arz::Database* db, double hits_per_sec) {
  const snap::Item* lh = at(s, equip::kLeftHand);
  const arz::Record* r = lh && db ? db->record(lh->record) : nullptr;
  if (!r || r->cls != "WeaponArmor_Shield") return 0;
  const double chance = std::clamp((r->number("defensiveBlockChance") + defense_raw(s, kBlockChance)) / 100, 0.0, 1.0);
  const double recovery = r->number("blockRecoveryTime") * std::max(0.0, 1 - char_total(s, kBlockRecovery) / 100);
  return hits_per_sec > 0 && recovery > 0 ? std::min(chance, 1 / (recovery * hits_per_sec)) : chance;
}

double block(const snap::Snapshot& s, const arz::Database* db, double hit, double hits_per_sec) {
  const double share = block_share(s, db, hits_per_sec);
  if (share <= 0 || hit <= 0) return 0;
  const arz::Record* r = db->record(at(s, equip::kLeftHand)->record);
  const double absorb = r->number("blockAbsorption", 0, 100) / 100;
  const double amount = r->number("defensiveBlock") * (1 + defense_raw(s, kBlockAmount) / 100);
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
        if (k.current_level > 0 && !c.pets)
          if (const arz::Record* r = db->record(k.record))
            if (const arz::Record* pet = effects::summoned(*r, k.current_level, *db); pet && pet->cls == "Pet")
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

Values values(const snap::Snapshot& s, const arz::Database* db) { return values(s, item_values(s, db)); }

double cooldown_reduction(const snap::Snapshot& s) { return std::clamp(skill_attribute(s, kCooldown) / 100, 0.0, 1.0); }

Values values(const snap::Snapshot& s, Values out) {
  const model::Sheet sh = model::sheet(s);
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
  k["slow_resist"] = resist(sh, s, 16) / 100;  // type 16 "TotalSpeed"
  k["disruption_resist"] = resist(sh, s, 13) / 100;
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

Values item_values(const snap::Snapshot& s, const arz::Database* db) {
  Values out;
  auto& k = out.keys;
  // playerlevels.dbr maxPlayerLevel (100 in 1.3.1.1): no experience at the cap.
  const arz::Record* levels = db ? db->record("records/creatures/pc/playerlevels.dbr") : nullptr;
  const double max_level = levels ? levels->number("maxPlayerLevel") : 0;
  k["experience"] = max_level > 0 ? char_total(s, kExperience) / 100 * std::max(0.0, 1 - s.level / max_level) : 0;

  for_skill_attributes(s, [&](int type, double v) {
    if (type != kCooldown && type != kCost && v != 0) out.unlisted[label("skill", type, names::skill_attribute(type))] += v / 100;
  });
  // shortcut: no character total for these, so skills' parts are not seen — revisit when the capture reads the
  // character's offensive accumulators.
  double leech = 0, ret = 0, ret_mod = 0, pets = 0, racial_damage = 0, racial_defense = 0;
  for (const auto& it : s.equipped) {
    if (it.stats) {
      for (const auto& d : it.stats->offensive_damage) {
        if (d.type == kLifeLeech) leech += d.min;
        else if (!kOffenseCovered.contains(d.type))
          out.unlisted[label("item offense", d.type, names::combat_attribute(d.type))] += d.min / 100;
      }
      for (const auto& d : it.stats->retaliation) ret += (d.min + d.max) / 2;
      for (const auto& m : it.stats->retaliation_modifiers) ret_mod = std::max(ret_mod, static_cast<double>(m.damage_modifier));
    }
    if (db)
      for (const auto& r : model::item_records(it))
        if (const arz::Record* rec = db->record(r)) {
          if (const std::string pb(rec->string("petBonusName")); !pb.empty())
            if (const arz::Record* b = db->record(pb)) pets += pet_bonus(*b);
          const double share = racial_share(*rec);
          racial_damage += rec->number("racialBonusPercentDamage") / 100 * share;
          racial_defense += rec->number("racialBonusPercentDefense") / 100 * share;
        }
  }
  k["life_leech"] = leech / 100;
  // Weight 0: listed only so the Shift view shows them.
  k["cooldown_reduction"] = skill_attribute(s, kCooldown) / 100;
  k["skill_cost_reduction"] = skill_attribute(s, kCost) / 100;
  k["retaliation"] = ret / 1000;
  k["retaliation_modifier"] = ret_mod / 100;
  k["pet_bonus"] = pets;
  k["racial_damage"] = racial_damage;
  k["racial_defense"] = racial_defense;
  return out;
}

}  // namespace gdc::utility
