#include <algorithm>
#include <tuple>

#include "gdc/db_fields.h"
#include "gdc/sets.h"
#include "gdc/stat_model.h"

#include "score_internal.h"

namespace gdc::score {
namespace {

// Stats of a petBonusName record or a Class Pet's bonuses.
using PetStats = std::map<std::tuple<fields::Kind, int, bool>, double>;

double stat(const PetStats& b, fields::Kind kind, int type, bool modifier) {
  const auto it = b.find({kind, type, modifier});
  return it != b.end() ? it->second : 0;
}

bool timed(const arz::Record& r, std::uint32_t level, const arz::Database& db) {
  const arz::Record* b = db.record(r.string("buffSkillName"));
  const arz::Record& x = b ? *b : r;
  return (classes::is_a(x.cls, "SkillBuff_Passive") || classes::is_a(x.cls, "Skill_BuffSelfDuration")) &&
         at_level(x, "skillActiveDuration", level) > 0;
}

// GameEngine::ContributePetBonus* sources (§11.7).
// shortcut: timed buff and set pet bonuses left out — revisit with a pet build.
PetStats pet_bonuses(const snap::Snapshot& s, const arz::Database& db) {
  PetStats out;
  auto take = [&](const arz::Record& r, std::uint32_t level) {
    if (const arz::Record* b = db.record(r.string("petBonusName")))
      for (const auto& [k, v] : db.derived<sets::Bonus>(*b, level, [&] { return sets::bonus(*b, static_cast<int>(level)); }).stats)
        out[k] += v;
  };
  for (const auto& it : s.equipped)
    for (const auto& r : model::item_records(it))
      if (const arz::Record* rec = db.record(r)) take(*rec, 1);
  for (const auto* list : {&s.skills, &s.item_skills})
    for (const auto& sk : *list) {
      if (sk.current_level == 0 || (list == &s.skills && sk.level == 0)) continue;
      const arz::Record* r = db.record(sk.record);
      if (!r) continue;
      const arz::Record* b = db.record(r->string("buffSkillName"));
      if (r->string("petBonusName").empty() && (!b || b->string("petBonusName").empty())) continue;
      if (timed(*r, sk.current_level, db)) continue;
      take(*r, sk.current_level);
      if (b) take(*b, sk.current_level);
    }
  return out;
}

// Pet attack / cast speed %: modifier plus TotalSpeed (13).
std::pair<double, double> pet_speed_bonus(const PetStats& b) {
  const double total = stat(b, fields::Kind::Char, kTotalSpeed, true);
  return {stat(b, fields::Kind::Char, kAttackSpeed, true) + total, stat(b, fields::Kind::Char, kCastSpeed, true) + total};
}

effects::Rates rates(const snap::Snapshot& s, const std::vector<Skill>& skills, const std::vector<Main>& mains,
                     const Settings& p, const arz::Database& db, const PetStats& b) {
  effects::Rates r;
  r.hits_taken = p.incoming_hits_per_sec;
  r.melee_hits = r.hits_taken * p.melee_share;
  r.blocks = r.hits_taken * utility::block_share(s, &db, r.hits_taken);
  r.projectile_hits = r.hits_taken - r.melee_hits;
  r.crit_hits = r.hits_taken * formulas::expected_hit(formulas::probability_to_hit(enemy_oa(s, p), model::sheet(s).da), s.pth, 0).crit_chance;
  r.low_health = p.low_health_per_sec;
  r.kills = p.kills_per_sec;
  r.pet_attacks = p.pet_attack_rate;
  r.char_level = s.level;
  std::tie(r.pet_attack_speed, r.pet_cast_speed) = pet_speed_bonus(b);
  for (std::size_t k = 0; k < skills.size(); ++k) {
    if (!mains[k].skill) continue;
    const bool weapon = mains[k].raw.weapon_pct > 0;
    const double rate = skills[k].weight * use_rate(s, *mains[k].skill, mains[k].raw);
    r.action += rate;
    if (weapon) r.weapon += rate;
  }
  r.crit = attack(s, p, 0).crit_chance;
  return r;
}

std::vector<int> enemy_types(const Settings& p) {
  std::vector<int> t;
  for (const auto& [type, _] : p.enemy_resist) t.push_back(type);
  return t;
}

// Class Pet hit, experimental (§4, §11.7).
// shortcut: player formulas, no crit / DoT / conversion — revisit with a pet build.
void pet_scaling(const snap::Snapshot& s, const Settings& p, const arz::Database& db, const PetStats& bonus,
                 const effects::Effect& e, Side::Hit& h) {
  const arz::Record* pet = db.record(e.pet_record);
  if (!pet) return;
  Raw own;
  for (const auto& [r, l] : effects::pet_skills(*pet, s.level, {}, db))
    if (const arz::Record* x = db.record(r); x && classes::is_a(x->cls, "Skill_Passive")) add_record(db, *x, l, own);
  const arz::Record* bio = db.record(pet->string("characterAttributeEquations"));
  auto attr = [&](std::string_view field, int type) {
    const double base = bio ? effects::equation(bio->string(field), s.level).value_or(0) : 0;
    return (base + stat(bonus, fields::Kind::Char, type, false)) * (1 + stat(bonus, fields::Kind::Char, type, true) / 100);
  };
  const double cunning = attr("characterDexterity", kCunning), spirit = attr("characterIntelligence", kSpirit);
  for (auto& [k, v] : h.flat) {
    double pct = 0;
    if (const auto it = own.pct.find(k); it != own.pct.end()) pct += it->second;
    for (const int t : {k.first, 60, k.first == 5 || k.first == 6 || k.first == 8 ? kElemental : -1})
      if (t > 0) pct += stat(bonus, fields::Kind::Damage, t, k.second);
    v *= std::max(0.0, 1 + pct / 100 + attribute_bonus(cunning, spirit, k.first, k.second));
  }
  const double oa_base = bio ? effects::equation(bio->string("characterOffensiveAbility"), s.level).value_or(0) : 0;
  const double oa = (oa_base + stat(bonus, fields::Kind::Char, 21, false)) * (1 + stat(bonus, fields::Kind::Char, 22, false) / 100);
  h.pet_hit = formulas::expected_hit(formulas::probability_to_hit(oa, enemy_da(s, p)), s.pth, 0).multiplier;
}

// A prediction shifts pet speed by the bonus change.
effects::Rates side_rates(const snap::Snapshot& s, const std::vector<Skill>& skills, const Settings& p,
                          const arz::Database& db, const Side& out, const std::optional<PetStats>& pets,
                          const snap::Snapshot* captured) {
  effects::Rates r = rates(s, skills, out.mains, p, db, pets ? *pets : PetStats{});
  if (pets && captured && captured != &s && !s.pets.empty()) {
    const auto [a, c] = pet_speed_bonus(pet_bonuses(*captured, db));
    r.pet_attack_shift = r.pet_attack_speed - a;
    r.pet_cast_shift = r.pet_cast_speed - c;
  }
  return r;
}

// Gear ADCtH reaches weapon skills only, scaled (§11.7).
void weigh_life_leech(Side& out, const std::vector<Skill>& skills) {
  if (skills.empty()) return;
  double& leech = out.utility.items.keys["life_leech"];
  double v = 0;
  for (std::size_t k = 0; k < skills.size(); ++k)
    if (out.mains[k].skill)
      v += skills[k].weight * (leech * std::min(1.0, out.mains[k].raw.weapon_pct / 100) + out.mains[k].raw.leech / 100);
  leech = v;
}

// Damage per second of procs and pets.
void collect_hits(const snap::Snapshot& s, const Settings& p, const arz::Database& db, std::optional<PetStats>& pets,
                  Side& out) {
  for (const auto& e : out.effects.effects) {
    if (e.source == "learned") continue;
    const arz::Record* rec = db.record(e.record);
    if (!rec) continue;
    const std::uint32_t lv = std::max<std::uint32_t>(e.level, 1);
    Amounts a;
    add_flat(db, *rec, lv, 1, a);
    if (a.empty())
      if (const arz::Record* buff = db.record(rec->string("buffSkillName"))) add_flat(db, *buff, lv, 1, a);
    // WPAttack counts only its excess over 100% (§11.7).
    // shortcut: replaced attack taken as the default attack — revisit with a weapon build.
    double weapon_pct = classes::is_a(rec->cls, "Character") ? 100 : at_level(*rec, "weaponDamagePct", lv);
    if (!e.pet && weapon_pct > 0 && rec->cls.starts_with("Skill_WPAttack_")) weapon_pct -= 100;
    Amounts pet_weapon;
    if (e.pet)
      for (const auto& [r, l] : e.weapon)
        if (const arz::Record* w = db.record(r)) add_flat(db, *w, l, 1, pet_weapon);
    if (a.empty() && (weapon_pct == 0 || (e.pet && pet_weapon.empty()))) continue;
    if (e.per_sec <= 0) {
      out.hits_unknown.emplace(e.record, e.basis);
      continue;
    }
    if (weapon_pct != 0)
      for (const auto& [k, v] : e.pet ? pet_weapon : out.gear.flat) a[k] += v * weapon_pct / 100;
    Side::Hit h{e.record, e.pet && !e.player_scaled, e.player_scaled, e.per_sec, std::move(a)};
    if (h.pet) {
      if (!pets) pets = pet_bonuses(s, db);
      pet_scaling(s, p, db, *pets, e, h);
    }
    out.hits.push_back(std::move(h));
  }
}

}  // namespace

// `captured`: the snapshot s was predicted from (null: s itself).
Side side(const snap::Snapshot& s, const std::vector<Skill>& skills, const Settings& p, const arz::Database& db,
          const Amounts& base, std::vector<std::string>& notes, const snap::Snapshot* captured) {
  Side out;
  for (const auto& k : skills) {
    const snap::Skill* sk = skill_by_record(s, k.record);
    out.mains.push_back({sk, sk ? skill_raw(s, *sk, db, notes) : Raw{}});
  }
  out.utility = utility_of(s, p, &db);
  // Only Class Pets take pet bonuses.
  // shortcut: proc-only Class Pets get no speed bonus — revisit if one shows up.
  std::optional<PetStats> pets;
  if (out.utility.context.pets) pets = pet_bonuses(s, db);
  out.effects = effects::collect(s, db, side_rates(s, skills, p, db, out, pets, captured), p.uptime, enemy_types(p));
  std::map<int, double> target = p.enemy_resist;
  for (auto& [type, r] : target) r += enemy::resist(type, enemy::difficulty(s));
  out.resist = effects::resist_multipliers(target, out.effects.reductions);
  out.gear = gear(s, db, base);
  weigh_life_leech(out, skills);
  collect_hits(s, p, db, pets, out);
  return out;
}

}  // namespace gdc::score
