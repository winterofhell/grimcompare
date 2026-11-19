#include <algorithm>

#include "gdc/db_fields.h"
#include "gdc/sets.h"
#include "gdc/stat_model.h"

#include "score_internal.h"

namespace gdc::score {
namespace {

bool weapon_class(std::string_view cls) { return cls.starts_with("Weapon") && !cls.starts_with("WeaponArmor"); }

// Elemental base shares its type with flat damage.
Amounts hand_base(const snap::Item& it, const arz::Database& db) {
  Amounts out;
  if (it.stats)
    for (const auto& d : it.stats->offensive_damage)
      if (d.type == kBasePhysical) out[{kPhysical, false}] += (d.min + d.max) / 2.0;
  if (const arz::Record* r = db.record(it.record))
    for (const auto& f : fields::flat_damage()) {
      if (!f.min.starts_with("offensiveBase") || !f.duration.empty()) continue;
      const double lo = r->number(f.min), hi = std::max(lo, r->number(f.max));
      if (lo > 0) out[{f.type, false}] += (lo + hi) / 2;
    }
  return out;
}

// Flat damage applies to weapon attacks only (community).
Amounts weapon_flat(const snap::Snapshot& s, const arz::Database& db, const Amounts& base) {
  Amounts out = base;
  for (const auto& it : s.equipped) {
    if (!it.stats) continue;
    const bool hand = it.location == equip::kRightHand || it.location == equip::kLeftHand;
    Amounts mine;
    for (const auto& d : it.stats->offensive_damage) {
      if (d.type == kBasePhysical && hand) continue;
      const int t = d.type == kBasePhysical ? kPhysical : d.type;
      if (!damage_type(t)) continue;
      // shortcut: item DoT min/max taken as per second, x duration — unverified.
      mine[{t, d.dot}] += (d.min + d.max) / 2.0 * (d.dot ? d.duration : 1.0f);
    }
    if (hand)
      for (const auto& [k, v] : hand_base(it, db))
        if (k.first != kPhysical) {  // elemental base: out of the flat damage of its type
          double& x = mine[k];
          x = std::max(0.0, x - v);
        }
    for (const auto& [k, v] : mine) out[k] += v;
  }
  for (const auto& sk : s.skills) {
    if (sk.level == 0 || sk.current_level == 0 || sk.devotion_max_level != 0) continue;
    const arz::Record* r = db.record(sk.record);
    // Passives, toggles and auras only.
    // shortcut: timed buff flat damage left out — revisit for weapon builds.
    if (!r || !(classes::is_a(r->cls, "Skill_Passive") || classes::is_a(r->cls, "Skill_BuffSelfToggled") ||
                classes::is_a(r->cls, "Skill_BuffRadiusToggled")))
      continue;
    if (!skill_on(sk, *r) || model::buff_timing(sk, db)) continue;
    const arz::Record* stats = r;
    if (!has_damage(db, *r, sk.current_level))
      if (const arz::Record* buff = db.record(r->string("buffSkillName"));
          buff && classes::is_a(buff->cls, "SkillBuff_Passive"))  // not a debuff record
        stats = buff;
    add_flat(db, *stats, sk.current_level, 1, out);
  }
  for (const auto& sk : s.item_skills) {
    if (sk.current_level == 0) continue;
    const arz::Record* r = db.record(sk.record);
    if (!r || r->cls != "Skill_Passive") continue;
    const bool on = (sk.passive && (!sk.passive->char_attributes.empty() || !sk.passive->defense.empty())) ||
                    !fields::weapon_gated(*r);
    if (on) add_flat(db, *r, sk.current_level, 1, out);
  }
  // Character::ContributeMiscOffensiveDamageAttributes 0x6AA80.
  for (const auto& [set, n] : sets::counts(s))
    if (const arz::Record* r = db.record(set))
      for (const auto& d : sets::bonus(*r, n).flat)
        out[{d.type, d.dot}] += (d.min + d.max) / 2.0 * (d.dot ? d.duration : 1.0f);
  return out;
}

// Items, set bonuses, and learned passives, toggles and devotion nodes with their modifiers. A modifier of any other
// skill is that skill's own (Raw::duration).
// shortcut: record DurMod values only, timed buffs left out — revisit with GetTotalDurationModifierType.
std::map<int, double> dot_duration(const snap::Snapshot& s, const arz::Database& db) {
  std::map<int, double> out;
  using Durations = std::vector<std::pair<int, double>>;
  auto durations = [](const arz::Record& r, std::uint32_t level) {
    Durations d;
    for (const auto& [t, field] : kDotDuration)
      if (const double x = at_level(r, field, level); x != 0) d.emplace_back(t, x);
    return d;
  };
  auto add = [&](const Durations& d) {
    for (const auto& [type, v] : d) out[type] += v;
  };
  auto take = [&](const arz::Record& r, std::uint32_t level) {
    add(db.derived<Durations>(r, level, [&] { return durations(r, level); }));
  };
  for (const auto& it : s.equipped)
    for (const auto& r : model::item_records(it))
      if (const arz::Record* rec = db.record(r)) take(*rec, 1);
  for (const auto& [set, n] : sets::counts(s))
    if (const arz::Record* rec = db.record(set)) take(*rec, static_cast<std::uint32_t>(n));
  for (const auto& sk : s.skills) {
    if (sk.current_level == 0 || (sk.level == 0 && sk.devotion_max_level == 0)) continue;
    const arz::Record* rec = db.record(sk.record);
    if (!rec) continue;
    // Absent for other classes. Skill_BuffSelfDuration (timed buffs, oils) derives from Skill_BuffSelfToggled
    // (re/GameClassTree.tsv).
    const auto& own = db.derived<std::optional<Durations>>(*rec, sk.current_level, [&]() -> std::optional<Durations> {
      if (classes::is_a(rec->cls, "Skill_Passive") ||
          (classes::is_a(rec->cls, "Skill_BuffSelfToggled") && !classes::is_a(rec->cls, "Skill_BuffSelfDuration")))
        return durations(*rec, sk.current_level);
      return std::nullopt;
    });
    if (!own) continue;
    // Passives and devotion nodes always hold; a toggle only while on (flagged, or its collectors in the capture).
    if (!classes::is_a(rec->cls, "Skill_Passive") && sk.buff_active != true && !skill_on(sk, *rec)) continue;
    add(*own);
    for (const std::uint32_t id : sk.modifier_ids)
      for (const auto& m : s.skills)
        if (m.id == id && m.current_level > 0)
          if (const arz::Record* mr = db.record(m.record)) take(*mr, m.current_level);
    for (const auto& m : s.item_skill_modifiers)
      if (m.skill_id == sk.id)
        if (const arz::Record* mr = db.record(m.modifier_record)) take(*mr, 1);
  }
  return out;
}

// Cast whenever ready, capped by attack / cast speed.
struct CooldownUses {
  double per_sec = 0, cdr = 0;
  bool capped = false;
};
CooldownUses cooldown_uses(double cooldown, double cdr, double rate) {
  CooldownUses out{0, cdr};
  const double recharge = cooldown * (1 - cdr);
  if (recharge > 0) out.per_sec = 1 / recharge;
  if (rate > 0 && (recharge <= 0 || out.per_sec > rate)) out.per_sec = rate, out.capped = true;
  else if (recharge <= 0) out.per_sec = 1 / cooldown;
  return out;
}

// Captured for SkillActivatedSpell / BuffSelf only (schema 10).
bool has_anim(const snap::Skill& sk) { return sk.anim_frames > 0 && sk.anim_frame_rate > 0; }

// Player::CalculateSkillDps picks the speed by skill class.
bool attack_speed(const Raw& raw) {
  return raw.speed == classes::Speed::kAttack || (raw.speed == classes::Speed::kNone && raw.weapon_pct > 0);
}

// Skill::CollectCombatParameters 0x499570 collects the skill's flat damage with its learned and item modifiers'
// (CollectLocalOffensiveDamageAttributes 0x496440 → AddModifierOffensiveDamageAttributes 0x493480) and their %
// (0x4964F0 → 0x4935B0), adds the % to each attribute (ModifyDamage 0x119280 at 0x499A87), converts, then adds the
// character's % (SkillManager::CollectAvailableOffensiveModifierAttributes 0x535BC0, ModifyDamage at 0x499DB1).
// Each attribute sums its % and applies it once (ModifyAbsoluteDamage 0x1138A0, CombatAttributeAbsDamage::Process
// 0x113450): skill % and sheet % add.
Pool pool(const Amounts& flat, const Raw& raw, const Conversions& global, std::vector<std::string>& notes) {
  Amounts amounts = flat, bonus;
  split_elemental(amounts);  // raw.pct holds Elemental % spread already
  for (const auto& [k, v] : amounts)
    if (const auto it = raw.pct.find(k); it != raw.pct.end()) bonus[k] = v * it->second / 100;
  Pool out;
  out.base = convert_pairs(amounts, raw.conversion, global, notes, &bonus, &out.bonus);
  return out;
}

Amounts weapon_part(const Raw& raw, const Gear& g) {
  Amounts out;
  if (raw.weapon_pct > 0)
    for (const auto& [k, v] : g.flat) out[k] += v * raw.weapon_pct / 100;
  return out;
}

bool same(const std::vector<snap::Conversion>& a, const std::vector<snap::Conversion>& b) {
  return std::equal(a.begin(), a.end(), b.begin(), b.end(), [](const snap::Conversion& x, const snap::Conversion& y) {
    return x.from == y.from && x.to == y.to && x.percent == y.percent;
  });
}

}  // namespace

// Both sides share the mean weapon base, so swaps stay symmetric.
// shortcut: second dual-wield base ignored — revisit with a dual-wield build.
Amounts shared_base(const snap::Snapshot& before, const snap::Snapshot& after, const arz::Database& db) {
  Amounts out;
  for (const auto* s : {&before, &after})
    for (const auto& it : s->equipped) {
      if (it.location != equip::kRightHand) continue;
      const arz::Record* r = db.record(it.record);
      if (!r || !weapon_class(r->cls)) continue;
      for (const auto& [k, v] : hand_base(it, db)) out[k] += v / 2;
    }
  return out;
}

// combatformulas.dbr Cunning / Spirit divisors, checked in game.
double attribute_bonus(double cunning, double spirit, int type, bool dot) {
  const bool physical = type == kPhysical || type == kPierce || type == kBleeding;
  if (physical) return cunning / (dot ? 215.0 : 245.0);
  return spirit / (dot ? 200.0 : 215.0);
}
double attribute_bonus(const snap::Snapshot& s, int type, bool dot) {
  return attribute_bonus(char_total(s, kCunning), char_total(s, kSpirit), type, dot);
}

Gear gear(const snap::Snapshot& s, const arz::Database& db, const Amounts& base) {
  return {weapon_flat(s, db, base), dot_duration(s, db), {}};
}

// Max uses per second; animation-based for spells.
double use_rate(const snap::Snapshot& s, const snap::Skill& sk, const Raw& raw) {
  if (attack_speed(raw)) return s.attack_speed;
  if (has_anim(sk)) return static_cast<double>(s.cast_speed) * sk.anim_frame_rate / sk.anim_frames;
  return s.cast_speed;
}

namespace {

// A conversion keeps a part's DoT flag (Vitality Decay -> Poison) and never moves DoT into Pierce, Chaos or Aether
// (ConvertDamage 0x119FE0). In the 1.3.1.1 database every damage type but Bleeding is some record's
// conversionInType, each converts into every other, and none into Bleeding: Bleeding stays Bleeding.
bool convertible(Key from, Key to) {
  if (from == to) return true;
  if (from.first == kBleeding || to.first == kBleeding || from.second != to.second) return false;
  return !(from.second && (to.first == kPierce || to.first == kChaos || to.first == kAether));
}

}  // namespace

double type_weight(const Settings& p, Key k) {
  if (p.offtarget_weight == 1 || p.match_damage.empty()) return 1;
  for (const auto& t : p.match_damage)
    if (convertible(k, t)) return 1;
  return p.offtarget_weight;
}

namespace {

// Per final type: base × (1 + sheet % + attribute bonus) + the skill %'s part, × the skill's damage multiplier, DoT ×
// duration, × enemy resist, × the type's weight.
double final_damage(const snap::Snapshot& s, const Settings& p, const Raw& raw, const Gear& g, const Pool& in,
                    const std::map<int, double>* resist, Amounts* parts) {
  double sum = 0;
  for (const auto& [k, v] : in.base) {
    const auto* m = find(s.damage_modifiers, k.first);
    const double pct = m ? (k.second ? m->dot_modifier : m->damage_modifier) : 0;
    const auto b = in.bonus.find(k);
    double x = std::max(0.0, v * (1 + pct / 100 + attribute_bonus(s, k.first, k.second)) + (b != in.bonus.end() ? b->second : 0)) *
               raw.mult * type_weight(p, k);
    // shortcut: the skill's duration % taken by final type — revisit for a converted DoT with one.
    if (k.second) {
      const auto d = raw.duration.find(k.first);
      x *= g.dot_scale(k.first, d != raw.duration.end() ? d->second : 0);
    }
    if (resist)
      if (const auto r = resist->find(k.first); r != resist->end()) x *= std::max(0.0, r->second);
    sum += x;
    if (parts) (*parts)[k] += x;
  }
  return sum;
}

std::string amounts_text(const Amounts& a) {
  std::string out;
  for (const auto& [k, v] : a)
    if (std::abs(v) >= 0.5) out += (out.empty() ? "" : " ") + std::string(damage_label(k)) + " " + fmt(v);
  return out.empty() ? "0" : out;
}

}  // namespace

Pool main_converted(const Raw& raw, const Gear& g, const Conversions& global, std::vector<std::string>& notes) {
  Amounts amounts = raw.flat;
  for (const auto& [k, v] : weapon_part(raw, g)) amounts[k] += v;
  return pool(amounts, raw, global, notes);
}

// Per source: the skill's record, each modifier, the weapon part; they sum to the skill's damage per use.
std::string skill_parts(const snap::Snapshot& s, const Settings& p, const Raw& raw, const Gear& g,
                        const std::map<int, double>* resist, std::vector<std::string>& notes) {
  const Conversions global = global_pairs(s.conversions);
  std::string out;
  auto add = [&](const std::string& name, const Amounts& flat) {
    Amounts parts;
    final_damage(s, p, raw, g, pool(flat, raw, global, notes), resist, &parts);
    out += (out.empty() ? "" : ", ") + name + " " + amounts_text(parts);
  };
  for (const auto& [name, flat] : raw.sources) add(name, flat);
  if (const Amounts w = weapon_part(raw, g); !w.empty()) add("weapon", w);
  return out;
}

// `converted`: main_converted() already done for this snapshot's conversions.
SkillDamage skill_damage(const snap::Snapshot& s, const snap::Skill& sk, const Raw& raw, const Gear& g, const Settings& p,
                         std::vector<std::string>& notes, Amounts* parts,
                         const std::map<int, double>* resist, bool explain,
                         const Pool* converted) {
  SkillDamage out;
  if (sk.current_level == 0) return out;
  const Pool own = converted ? Pool{} : main_converted(raw, g, global_pairs(s.conversions), notes);
  const double sum = final_damage(s, p, raw, g, converted ? *converted : own, resist, parts);
  const auto* crit_row = find(s.damage_modifiers, kCritDamage);
  const double crit_bonus = (crit_row ? crit_row->damage_modifier : 0) + raw.crit / 100;
  double pth = 0;
  const formulas::Hit h = attack(s, p, crit_bonus, g.enemy_da, &pth);
  out.per_use = sum * h.multiplier;
  out.cooldown = raw.cooldown;
  out.rate = use_rate(s, sk, raw);
  std::string pace;
  if (raw.cooldown > 0) {
    const CooldownUses u = cooldown_uses(raw.cooldown, utility::cooldown_reduction(s), out.rate);
    out.uses = u.per_sec;
    out.value = out.per_use * u.per_sec;
    if (explain)
      pace = " cooldown " + fmt(raw.cooldown, 2) + " s x (1 - " + fmt(u.cdr * 100) + "%) " + fmt(u.per_sec, 2) + "/s" +
           (u.capped ? " (speed cap)" : "");
  } else {
    const double speed = 1 + (char_modifier(s, attack_speed(raw) ? kAttackSpeed : kCastSpeed) + char_modifier(s, kTotalSpeed)) / 100;
    out.value = out.per_use * std::max(0.0, speed);
    if (explain) pace = " speed " + fmt(speed, 2);
  }
  if (!explain) return out;
  if (!attack_speed(raw) && has_anim(sk))
    pace += " (anim " + std::to_string(sk.anim_frames) + "f@" + std::to_string(sk.anim_frame_rate) + " -> " +
            fmt(out.rate, 2) + "/s)";
  out.inputs = stem(sk.record) + " L" + std::to_string(sk.current_level) + " dmg " + fmt(sum) + " pth " + fmt(pth) +
               " hit " + fmt(h.chance, 2) + " crit " + fmt(h.crit_chance, 2) + "+" + fmt(crit_bonus, 2) + " x" +
               fmt(h.multiplier, 3) + pace + " = " + fmt(out.value);
  if (raw.sources.size() > 1) out.inputs += " (dmg by source: " + skill_parts(s, p, raw, g, resist, notes) + ")";
  return out;
}

// `memo` is shared by a side's buff states.
const Converted& converted_for(const snap::Snapshot& s, const Side& x, std::vector<Converted>* memo,
                               std::optional<Converted>& own, std::vector<std::string>& notes) {
  if (memo)
    for (const auto& c : *memo)
      if (same(c.conversions, s.conversions)) return c;
  Converted c{s.conversions, {}, {}};
  const Conversions global = global_pairs(s.conversions);
  for (const auto& m : x.mains)
    c.mains.push_back(m.skill && m.skill->current_level != 0 ? main_converted(m.raw, x.gear, global, notes) : Pool{});
  for (const auto& e : x.hits) c.hits.push_back(e.pet ? Amounts{} : convert_pairs(e.flat, {}, global, notes));
  if (!memo) return own.emplace(std::move(c));
  memo->push_back(std::move(c));
  return memo->back();
}

Extra extra_damage(const snap::Snapshot& s, const Side& x, const Settings& p, bool explain, const Converted& conv,
                   double mult) {
  Extra out;
  const auto* crit_row = find(s.damage_modifiers, kCritDamage);
  const double hit = attack(s, p, crit_row ? crit_row->damage_modifier : 0, x.gear.enemy_da).multiplier;
  auto resist = [&](int type) {
    const auto r = x.resist.find(type);
    return r != x.resist.end() ? std::max(0.0, r->second) : 1.0;
  };
  // Per damage kind, the same for every hit: worked out once.
  std::vector<std::pair<std::pair<int, bool>, double>> scales;
  auto scale = [&](const std::pair<int, bool>& k) {
    for (const auto& [key, v] : scales)
      if (key == k) return v;
    const auto* m = find(s.damage_modifiers, k.first);
    const double pct = m ? (k.second ? m->dot_modifier : m->damage_modifier) : 0;
    const double v = std::max(0.0, 1 + pct / 100 + attribute_bonus(s, k.first, k.second)) * resist(k.first) *
                     (k.second ? x.gear.dot_scale(k.first) : 1.0) * type_weight(p, k);
    scales.emplace_back(k, v);
    return v;
  };
  for (std::size_t i = 0; i < x.hits.size(); ++i) {
    const auto& e = x.hits[i];
    double per_use = 0;
    if (e.pet) {
      for (const auto& [k, v] : e.flat) per_use += v * resist(k.first) * type_weight(p, k);
      per_use *= e.pet_hit;
    } else {
      for (const auto& [k, v] : conv.hits[i]) per_use += v * scale(k);
      per_use *= hit * mult;
    }
    out.per_sec += e.per_sec * per_use;
    if (explain)
      out.inputs += " " + stem(e.record) + (e.pet ? " (pet) " : e.player_scaled ? " (player-scaled pet) " : " ") +
                  fmt(e.per_sec, 2) + "/s x " + fmt(per_use);
  }
  return out;
}

}  // namespace gdc::score
