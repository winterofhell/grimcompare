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

// shortcut: record DurMod values only — revisit with GetTotalDurationModifierType.
std::map<int, double> dot_duration(const snap::Snapshot& s, const arz::Database& db) {
  static constexpr std::pair<int, std::string_view> kFields[] = {
      {2, "offensiveSlowPhysicalDurationModifier"},  {5, "offensiveSlowColdDurationModifier"},
      {6, "offensiveSlowFireDurationModifier"},      {7, "offensiveSlowPoisonDurationModifier"},
      {8, "offensiveSlowLightningDurationModifier"}, {9, "offensiveSlowLifeDurationModifier"},
      {15, "offensiveSlowBleedingDurationModifier"}};
  std::map<int, double> out;
  for (const auto& it : s.equipped)
    for (const auto& r : model::item_records(it))
      if (const arz::Record* rec = db.record(r))
        for (const auto& [type, field] : kFields)
          if (const double v = rec->number(field); v != 0) out[type] += v;
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

// Per type, after the skill's % and conversions.
Amounts main_converted(const Raw& raw, const Gear& g, const Conversions& global, std::vector<std::string>& notes) {
  Amounts amounts = raw.flat;
  if (raw.weapon_pct > 0)
    for (const auto& [k, v] : g.flat) amounts[k] += v * raw.weapon_pct / 100;
  split_elemental(amounts);  // raw.pct holds Elemental % spread already
  for (auto& [k, v] : amounts) {  // skill % applies to the original type (MECHANICS §2.5)
    const auto it = raw.pct.find(k);
    if (it != raw.pct.end()) v *= 1 + it->second / 100;
  }
  return convert_pairs(amounts, raw.conversion, global, notes);
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
  return {weapon_flat(s, db, base), dot_duration(s, db)};
}

// Max uses per second; animation-based for spells.
double use_rate(const snap::Snapshot& s, const snap::Skill& sk, const Raw& raw) {
  if (attack_speed(raw)) return s.attack_speed;
  if (has_anim(sk)) return static_cast<double>(s.cast_speed) * sk.anim_frame_rate / sk.anim_frames;
  return s.cast_speed;
}

// `converted`: main_converted() already done for this snapshot's conversions.
SkillDamage skill_damage(const snap::Snapshot& s, const snap::Skill& sk, const Raw& raw, const Gear& g, const Settings& p,
                         std::vector<std::string>& notes, Amounts* parts,
                         const std::map<int, double>* resist, bool explain,
                         const Amounts* converted) {
  SkillDamage out;
  if (sk.current_level == 0) return out;
  const Amounts own = converted ? Amounts{} : main_converted(raw, g, global_pairs(s.conversions), notes);
  double sum = 0;
  for (const auto& [k, v] : converted ? *converted : own) {
    const auto* m = find(s.damage_modifiers, k.first);
    const double pct = m ? (k.second ? m->dot_modifier : m->damage_modifier) : 0;
    double x = v * std::max(0.0, 1 + pct / 100 + attribute_bonus(s, k.first, k.second));
    if (k.second) x *= g.dot_scale(k.first);
    if (resist)
      if (const auto r = resist->find(k.first); r != resist->end()) x *= std::max(0.0, r->second);
    sum += x;
    if (parts) (*parts)[k] += x;
  }
  const auto* crit_row = find(s.damage_modifiers, kCritDamage);
  const double crit_bonus = (crit_row ? crit_row->damage_modifier : 0) + raw.crit / 100;
  double pth = 0;
  const formulas::Hit h = attack(s, p, crit_bonus, &pth);
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
    c.mains.push_back(m.skill && m.skill->current_level != 0 ? main_converted(m.raw, x.gear, global, notes) : Amounts{});
  for (const auto& e : x.hits) c.hits.push_back(e.pet ? Amounts{} : convert_pairs(e.flat, {}, global, notes));
  if (!memo) return own.emplace(std::move(c));
  memo->push_back(std::move(c));
  return memo->back();
}

Extra extra_damage(const snap::Snapshot& s, const Side& x, const Settings& p, bool explain, const Converted& conv) {
  Extra out;
  const auto* crit_row = find(s.damage_modifiers, kCritDamage);
  const double hit = attack(s, p, crit_row ? crit_row->damage_modifier : 0).multiplier;
  auto resist = [&](int type) {
    const auto r = x.resist.find(type);
    return r != x.resist.end() ? std::max(0.0, r->second) : 1.0;
  };
  for (std::size_t i = 0; i < x.hits.size(); ++i) {
    const auto& e = x.hits[i];
    double per_use = 0;
    if (e.pet) {
      for (const auto& [k, v] : e.flat) per_use += v * resist(k.first);
      per_use *= e.pet_hit;
    } else {
      for (const auto& [k, v] : conv.hits[i]) {
        const auto* m = find(s.damage_modifiers, k.first);
        const double pct = m ? (k.second ? m->dot_modifier : m->damage_modifier) : 0;
        per_use += v * std::max(0.0, 1 + pct / 100 + attribute_bonus(s, k.first, k.second)) * resist(k.first) *
                   (k.second ? x.gear.dot_scale(k.first) : 1.0);
      }
      per_use *= hit;
    }
    out.per_sec += e.per_sec * per_use;
    if (explain)
      out.inputs += " " + stem(e.record) + (e.pet ? " (pet) " : e.player_scaled ? " (player-scaled pet) " : " ") +
                  fmt(e.per_sec, 2) + "/s x " + fmt(per_use);
  }
  return out;
}

}  // namespace gdc::score
