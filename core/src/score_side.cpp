#include <algorithm>
#include <tuple>

#include "gdc/db_fields.h"
#include "gdc/sets.h"
#include "gdc/stat_model.h"

#include "score_internal.h"

namespace gdc::score {
namespace {

// A Class Pet's bonuses: the stats, conversions and DoT duration % of its petBonusName records.
struct PetStats {
  std::map<std::tuple<fields::Kind, int, bool>, double> stats;
  std::vector<snap::Conversion> conversions;  // in record order
  std::map<int, double> dot_duration;         // kDotDuration type -> %
};

double stat(const PetStats& b, fields::Kind kind, int type, bool modifier) {
  const auto it = b.stats.find({kind, type, modifier});
  return it != b.stats.end() ? it->second : 0;
}

// % damage of the final type k: its own, All (60: one field, both kinds) and, for cold / fire / lightning, Elemental.
double bonus_pct(const PetStats& b, const Key& k) {
  double pct = 0;
  for (const int t : {k.first, 60, k.first == 5 || k.first == 6 || k.first == 8 ? kElemental : -1})
    if (t > 0) pct += stat(b, fields::Kind::Damage, t, t != kAll && k.second);
  return pct;
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
    if (const arz::Record* b = db.record(r.string("petBonusName"))) {
      const auto& bonus = db.derived<sets::Bonus>(*b, level, [&] { return sets::bonus(*b, static_cast<int>(level)); });
      for (const auto& [k, v] : bonus.stats) out.stats[k] += v;
      out.conversions.insert(out.conversions.end(), bonus.conversions.begin(), bonus.conversions.end());
      const auto& dot = db.derived<std::map<int, double>>(*b, level, [&] {
        std::map<int, double> d;
        for (const auto& [type, field] : kDotDuration)
          if (const double v = sets::bonus_field(*b, field, static_cast<int>(level)); v != 0) d[type] += v;
        return d;
      });
      for (const auto& [type, v] : dot) out.dot_duration[type] += v;
    }
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

// The Skill_Passive skills of a pet's record at the character's level.
Raw pet_passives(const snap::Snapshot& s, const arz::Database& db, const arz::Record& pet) {
  Raw own;
  for (const auto& [r, l] : effects::pet_skills(pet, s.level, {}, db))
    if (const arz::Record* x = db.record(r); x && classes::is_a(x->cls, "Skill_Passive")) add_record(db, *x, l, own);
  return own;
}

// Class Pet hit, experimental (MECHANICS §4 / §5). Skill::CollectCombatParameters 0x499570 runs for the pet's skill
// as for the player's: its flat damage converted by the pet's conversions (Pet::ContributeMiscConversionAttributes:
// the pet bonuses'), then the pet's % of the final type (its passives and pet bonuses), DoT duration × (1 + Σ
// duration %) (DurDamage::Process 0x114206), crit damage at the hit roll.
// `live`: the pet's engine totals (schema 17) replace the record %; a prediction shifts them by the pet bonus change
// since `captured` (its pet bonuses).
// shortcut: player formulas for the attribute bonus and OA; a pet skill's own conversion is not read — revisit with
// a pet build.
void pet_scaling(const snap::Snapshot& s, const Settings& p, const arz::Database& db, const PetStats& bonus,
                 const effects::Effect& e, const effects::Shred& da, const snap::Pet* live, const PetStats& captured,
                 Side::Hit& h) {
  const arz::Record* pet = db.record(e.pet_record);
  if (!pet) return;
  const Raw own = pet_passives(s, db, *pet);
  const arz::Record* bio = db.record(pet->string("characterAttributeEquations"));
  auto attr = [&](std::string_view field, int type) {
    const double base = bio ? effects::equation(bio->string(field), s.level).value_or(0) : 0;
    return (base + stat(bonus, fields::Kind::Char, type, false)) * (1 + stat(bonus, fields::Kind::Char, type, true) / 100);
  };
  const double cunning = attr("characterDexterity", kCunning), spirit = attr("characterIntelligence", kSpirit);
  const bool engine = live && !live->damage_modifiers.empty();
  if (!bonus.conversions.empty()) {
    std::vector<std::string> notes;  // the player's conversion notes cover gear; a pet's are not shown
    split_elemental(h.flat);
    h.flat = convert_pairs(h.flat, {}, global_pairs(bonus.conversions), notes);
  }
  for (auto& [k, v] : h.flat) {
    double pct = 0;
    if (engine) {
      if (const auto* row = find(live->damage_modifiers, k.first)) pct += k.second ? row->dot_modifier : row->damage_modifier;
      pct += bonus_pct(bonus, k) - bonus_pct(captured, k);
    } else {
      if (const auto it = own.pct.find(k); it != own.pct.end()) pct += it->second;
      pct += bonus_pct(bonus, k);
    }
    v *= std::max(0.0, 1 + pct / 100 + attribute_bonus(cunning, spirit, k.first, k.second));
    if (k.second) {
      const auto d = bonus.dot_duration.find(k.first);
      const auto o = own.duration.find(k.first);
      v *= 1 + ((d != bonus.dot_duration.end() ? d->second : 0) + (o != own.duration.end() ? o->second : 0)) / 100;
    }
  }
  double crit = stat(bonus, fields::Kind::Damage, kCritDamage, false);
  if (engine) {
    const auto* row = find(live->damage_modifiers, kCritDamage);
    crit += (row ? row->damage_modifier : 0) - stat(captured, fields::Kind::Damage, kCritDamage, false);
  }
  const double oa_base = bio ? effects::equation(bio->string("characterOffensiveAbility"), s.level).value_or(0) : 0;
  const double oa = (oa_base + stat(bonus, fields::Kind::Char, 21, false)) * (1 + stat(bonus, fields::Kind::Char, 22, false) / 100);
  h.pet_hit = formulas::expected_hit(formulas::probability_to_hit(oa, da.apply(enemy_da(s, p))), s.pth, crit).multiplier;
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

// Damage per second of procs, pets and weapon pool attacks.
void collect_hits(const snap::Snapshot& s, const Settings& p, const arz::Database& db, std::optional<PetStats>& pets,
                  const snap::Snapshot* captured, Side& out) {
  std::optional<PetStats> at_capture;
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
    // A DoT from one source does not stack on a target: past one use per (seconds x duration modifier), the uses
    // add no DoT. Kept apart, so each buff state caps it at its own rate (extra_damage).
    std::vector<Side::Hit::Dot> dots;
    if (!e.pet)
      for (const auto& [type, secs] : dot_seconds(*own, lv))
        if (const auto it = a.find({type, true}); it != a.end() && secs > 0) {
          dots.push_back({it->first, it->second, secs * out.gear.dot_scale(type)});
          a.erase(it);
        }
    const double weapon_pct = classes::is_a(rec->cls, "Character") ? 100 : at_level(*rec, "weaponDamagePct", lv);
    Amounts pet_weapon;
    if (e.pet)
      for (const auto& [r, l] : e.weapon)
        if (const arz::Record* w = db.record(r)) add_flat(db, *w, l, 1, pet_weapon);
    if (a.empty() && dots.empty() && (weapon_pct == 0 || (e.pet && pet_weapon.empty()))) continue;
    if (e.per_sec <= 0) {
      out.hits_unknown.emplace(e.record, e.basis);
      continue;
    }
    // shortcut: a proc's weapon damage takes the R Hand pass while dual wielding — revisit with a traced proc hit.
    if (weapon_pct != 0)
      for (const auto& [k, v] : e.pet ? pet_weapon : out.gear.flat) a[k] += v * weapon_pct / 100;
    Side::Hit h;
    h.record = e.record, h.pet = e.pet && !e.player_scaled, h.player_scaled = e.player_scaled, h.per_sec = e.per_sec;
    h.flat = std::move(a), h.dots = std::move(dots);
    if (h.pet) {
      if (!pets) pets = pet_bonuses(s, db);
      const auto it = std::find_if(s.pets.begin(), s.pets.end(), [&](const snap::Pet& x) { return x.record == e.pet_record; });
      const snap::Pet* live = it != s.pets.end() ? &*it : nullptr;
      // Read only with the pet's engine totals.
      if (!at_capture && live && !live->damage_modifiers.empty())
        at_capture = captured && captured != &s ? pet_bonuses(*captured, db) : *pets;
      pet_scaling(s, p, db, *pets, e, out.gear.enemy_da, live, at_capture ? *at_capture : *pets, h);
    } else {
      h.on = e.on, h.chance = e.chance, h.recharge = e.recharge;
    }
    if (pool) h.pool_chance = e.pool_chance;
    if (p.pack_share > 0) {
      Area area;
      add_area(*rec, lv, area);
      if (own != rec) add_area(*own, lv, area);
      h.pack = pack_factor(area, p);
    }
    out.hits.push_back(std::move(h));
  }
}

}  // namespace

// `captured`: the snapshot s was predicted from (null: s itself).
std::map<Key, double> pet_record_pct(const snap::Snapshot& s, const arz::Database& db, const std::string& pet_record,
                                     const std::vector<Key>& keys) {
  std::map<Key, double> out;
  const arz::Record* pet = db.record(pet_record);
  if (!pet) return out;
  const Raw own = pet_passives(s, db, *pet);
  const PetStats bonus = pet_bonuses(s, db);
  for (const Key& k : keys) {
    const auto it = own.pct.find(k);
    out[k] = (it != own.pct.end() ? it->second : 0) + bonus_pct(bonus, k);
  }
  return out;
}

Side side(const snap::Snapshot& s, const std::vector<Skill>& skills, const Settings& p, const arz::Database& db,
          const Bases& base, std::vector<std::string>& notes, const snap::Snapshot* captured) {
  Side out;
  for (const auto& k : skills) {
    const snap::Skill* sk = skill_by_record(s, k.record);
    if (!sk)
      for (const auto& x : s.item_skills)
        if (x.record == k.record) sk = &x;
    Main& m = out.mains.emplace_back();
    m.skill = sk;
    if (sk) m.raw = skill_raw(s, *sk, db, notes);
    m.weight = k.weight;
    // SkillManager::GetOffensiveModifierAttributes 0x52F6E0 leaves out the passive % of the skill being used (same
    // object name, 0x52F810..0x52F86B): a charged Skill_WeaponPool_ChargedScaling hits with its own % and its
    // modifiers' once, locally (0x510F80). Its charged buff (learned_buffs()) or the capture's totals hold those for
    // every hit, so its own hit takes none from skill_raw().
    // shortcut: a capture taken charged is taken at the max charge scale — revisit when the snapshot records the
    // charge count.
    if (const arz::Record* r = sk ? db.record(sk->record) : nullptr; r && classes::is_a(r->cls, "Skill_WeaponPool_ChargedScaling")) {
      m.raw.pct.clear();
      m.raw.crit = 0;
    }
    if (const arz::Record* r = sk ? db.record(sk->record) : nullptr; r && classes::is_a(r->cls, "Skill_WeaponPool_ChargedFinale")) {
      m.charges = at_level(*r, "skillChargeLevel", sk->current_level);
      m.plain.weapon_pct = 100;
      m.plain.speed = m.raw.speed;
    }
    if (const arz::Record* r = sk ? db.record(sk->record) : nullptr) {
      m.combo = at_level(*r, "skillComboChargeLevel", sk->current_level);
      for (const auto& [mr, ml] : modifiers_of(s, *sk))
        if (const arz::Record* x = db.record(mr)) m.combo += at_level(*x, "skillComboChargeLevel", ml);
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
  out.rates = r;
  collect_hits(s, p, db, pets, captured, out);
  for (std::size_t k = 0; k < skills.size(); ++k)
    if (weapon_pool(out.mains[k], db)) out.mains[k].pool = skills[k].weight;
  return out;
}

}  // namespace gdc::score
