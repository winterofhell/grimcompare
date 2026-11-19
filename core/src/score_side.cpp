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

// GameEngine::ContributePetBonus* sources.
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

// The default attack, Cadence, Savagery: the uses a Skill_WPAttack_* can take.
// shortcut: the pool roll's call site is not traced; the class name stands for it — revisit with its caller.
bool weapon_pool(const Main& m, const arz::Database& db) {
  const arz::Record* r = m.skill ? db.record(m.skill->record) : nullptr;
  return r && classes::is_a(r->cls, "SkillActivatedWeaponPool");
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
    const double rate = skills[k].weight * use_rate(s, *mains[k].skill, mains[k].raw);
    r.action += rate;
    if (weapon_pool(mains[k], db)) r.weapon += rate * mains[k].pool_share();
  }
  // shortcut: crit rate before the side's DA debuffs, which need these rates — revisit if a crit proc build
  // shows the gap.
  r.crit = attack(s, p, 0, {}).crit_chance;
  return r;
}

std::vector<int> enemy_types(const Settings& p) {
  std::vector<int> t;
  for (const auto& [type, _] : p.enemy_resist) t.push_back(type);
  return t;
}

// Class Pet hit, experimental (MECHANICS §4).
// shortcut: player formulas, no crit / DoT / conversion — revisit with a pet build.
void pet_scaling(const snap::Snapshot& s, const Settings& p, const arz::Database& db, const PetStats& bonus,
                 const effects::Effect& e, const effects::Shred& da, Side::Hit& h) {
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
      if (t > 0) pct += stat(bonus, fields::Kind::Damage, t, t != kAll && k.second);  // 60: one field, both kinds
    v *= std::max(0.0, 1 + pct / 100 + attribute_bonus(cunning, spirit, k.first, k.second));
  }
  const double oa_base = bio ? effects::equation(bio->string("characterOffensiveAbility"), s.level).value_or(0) : 0;
  const double oa = (oa_base + stat(bonus, fields::Kind::Char, 21, false)) * (1 + stat(bonus, fields::Kind::Char, 22, false) / 100);
  h.pet_hit = formulas::expected_hit(formulas::probability_to_hit(oa, da.apply(enemy_da(s, p))), s.pth, 0).multiplier;
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

// Gear ADCtH reaches weapon skills only, scaled.
void weigh_life_leech(Side& out, const std::vector<Skill>& skills) {
  if (skills.empty()) return;
  double& leech = out.utility.items.keys["life_leech"];
  double v = 0;
  for (std::size_t k = 0; k < skills.size(); ++k)
    if (out.mains[k].skill)
      v += skills[k].weight * (leech * std::min(1.0, out.mains[k].raw.weapon_pct / 100) + out.mains[k].raw.leech / 100);
  leech = v;
}

// Damage per second of procs, pets and weapon pool attacks; returns the share of weapon pool uses the attacks take.
// `weapon`: weapon pool skill uses/s.
double collect_hits(const snap::Snapshot& s, const Settings& p, const arz::Database& db, std::optional<PetStats>& pets,
                    Side& out, double weapon) {
  double pooled = 0;
  for (const auto& e : out.effects.effects) {
    // A weapon pool attack (Skill_WPAttack_*, the only effects with a pool chance) replaces a weapon pool skill's use:
    // its whole hit counts, the use it took is lost (eval()).
    const bool pool = !e.pet && e.pool_chance > 0;
    if (e.source == "learned" && !pool) continue;
    const arz::Record* rec = db.record(e.record);
    if (!rec) continue;
    if (pool && std::any_of(out.mains.begin(), out.mains.end(), [&](const Main& m) { return m.skill && m.skill->record == e.record; }))
      continue;  // a main skill already, at its weight
    if (pool && e.source == "learned" && e.per_sec <= 0) continue;  // no weapon pool skill to replace
    const std::uint32_t lv = std::max<std::uint32_t>(e.level, 1);
    Amounts a;
    const arz::Record* own = rec;
    add_flat(db, *rec, lv, 1, a);
    if (a.empty())
      if (const arz::Record* buff = db.record(rec->string("buffSkillName"))) {
        add_flat(db, *buff, lv, 1, a);
        own = buff;
      }
    // A DoT from one source does not stack on a target: a new use only takes over from the running one. Past one
    // use per (seconds x duration modifier), the uses add no DoT.
    if (!e.pet && e.per_sec > 0)
      for (const auto& [type, secs] : dot_seconds(*own, lv))
        if (const auto it = a.find({type, true}); it != a.end() && secs > 0)
          it->second *= std::min(1.0, 1 / (e.per_sec * secs * out.gear.dot_scale(type)));
    const double weapon_pct = classes::is_a(rec->cls, "Character") ? 100 : at_level(*rec, "weaponDamagePct", lv);
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
      pet_scaling(s, p, db, *pets, e, out.gear.enemy_da, h);
    }
    out.hits.push_back(std::move(h));
    // Only the uses it fires on: a cooldown keeps it under its chance.
    if (pool && weapon > 0) pooled += std::min(e.pool_chance, e.per_sec / weapon);
  }
  return pooled;
}

}  // namespace

// `captured`: the snapshot s was predicted from (null: s itself).
Side side(const snap::Snapshot& s, const std::vector<Skill>& skills, const Settings& p, const arz::Database& db,
          const Amounts& base, std::vector<std::string>& notes, const snap::Snapshot* captured) {
  Side out;
  for (const auto& k : skills) {
    const snap::Skill* sk = skill_by_record(s, k.record);
    out.mains.push_back({sk, sk ? skill_raw(s, *sk, db, notes) : Raw{}});
    Main& m = out.mains.back();
    if (const arz::Record* r = sk ? db.record(sk->record) : nullptr; r && classes::is_a(r->cls, "Skill_WeaponPool_ChargedFinale")) {
      m.charges = at_level(*r, "skillChargeLevel", sk->current_level);
      m.plain.weapon_pct = 100;
      m.plain.speed = m.raw.speed;
    }
  }
  out.utility = utility_of(s, p, &db);
  // Only Class Pets take pet bonuses.
  // shortcut: proc-only Class Pets get no speed bonus — revisit if one shows up.
  std::optional<PetStats> pets;
  if (out.utility.context.pets) pets = pet_bonuses(s, db);
  const effects::Rates r = side_rates(s, skills, p, db, out, pets, captured);
  out.effects = effects::collect(s, db, r, p.uptime, enemy_types(p));
  std::map<int, double> target = p.enemy_resist;
  for (auto& [type, r] : target) r += enemy::resist(type, enemy::difficulty(s));
  out.resist = effects::resist_multipliers(target, out.effects.reductions);
  out.gear = gear(s, db, base);
  out.gear.enemy_da = effects::shred(out.effects.reductions, effects::kEnemyDA);
  weigh_life_leech(out, skills);
  out.pooled = collect_hits(s, p, db, pets, out, r.weapon);
  for (std::size_t k = 0; k < skills.size(); ++k)
    if (weapon_pool(out.mains[k], db)) out.mains[k].pool = skills[k].weight;
  return out;
}

}  // namespace gdc::score
