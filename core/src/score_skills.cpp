#include <algorithm>
#include <cctype>
#include <numbers>
#include <numeric>
#include <set>
#include <tuple>

#include "gdc/attribute_names.h"
#include "gdc/db_fields.h"

#include "score_internal.h"

namespace gdc::score {
namespace {

int damage_type_named(std::string_view name) {
  for (int t : kDamageTypes)
    if (names::combat_attribute(t) == name) return t;
  return 0;
}

// In fields::flat_damage() order; a type can repeat.
using FlatParts = std::vector<std::pair<Key, double>>;

const FlatParts& flat_parts(const arz::Database& db, const arz::Record& r, std::uint32_t level) {
  return db.derived<FlatParts>(r, level, [&] {
    FlatParts out;
    for (const auto& f : fields::flat_damage()) {
      const double min = at_level(r, f.min, level);
      if (min == 0) continue;
      const double max = std::max(min, at_level(r, f.max, level));
      double v = (min + max) / 2;
      if (!f.duration.empty()) v *= at_level(r, f.duration, level);  // shortcut: per second x duration — unverified
      const double chance = at_level(r, f.chance, level);
      if (chance > 0 && chance < 100) v *= chance / 100;
      out.push_back({{f.type, !f.duration.empty()}, v});
    }
    return out;
  });
}

bool main_class(std::string_view cls) { return attack_class(cls) || classes::is_a(cls, "SkillActivatedWeaponPool"); }

// Points spent in a skill and its learned modifiers and transmuter (Skill+0x140): Bloody Pox 1 + Black Death 12.
std::uint32_t points(const snap::Snapshot& s, const snap::Skill& sk) {
  std::uint32_t out = sk.level;
  for (const std::uint32_t id : sk.modifier_ids)
    for (const auto& m : s.skills)
      if (m.id == id) out += m.level;
  return out;
}

}  // namespace

void add_flat(const arz::Database& db, const arz::Record& r, std::uint32_t level, double scale, Amounts& out) {
  for (const auto& [k, v] : flat_parts(db, r, level)) out[k] += v * scale;
}

std::map<int, double> dot_seconds(const arz::Record& r, std::uint32_t level) {
  std::map<int, double> out;
  for (const auto& f : fields::flat_damage())
    if (!f.duration.empty() && at_level(r, f.min, level) != 0) {
      const double chance = at_level(r, f.chance, level);
      const double secs = at_level(r, f.duration, level) * (chance > 0 && chance < 100 ? chance / 100 : 1);
      out[f.type] = std::max(out[f.type], secs);
    }
  return out;
}

// AddAttribute 0x17D890: a new source type appends, known ones add.
void add_pair(Conversions& c, int from, int to, double pct) {
  auto it = std::find_if(c.begin(), c.end(), [&](const auto& x) { return x.first == from; });
  if (it == c.end()) it = c.insert(c.end(), {from, {}});
  it->second[to] += pct;
}

bool has_damage(const arz::Database& db, const arz::Record& r, std::uint32_t level) {
  return !flat_parts(db, r, level).empty() || at_level(r, "weaponDamagePct", level) > 0;
}

// `source`: names the record's flat damage in Raw::sources. `scale`: on everything but cooldown and conversions.
void add_record(const arz::Database& db, const arz::Record& r, std::uint32_t level, Raw& out, std::string_view source,
                double scale) {
  Amounts own;
  add_flat(db, r, level, scale, own);
  for (const auto& [k, v] : own) out.flat[k] += v;
  if (!own.empty() && !source.empty()) out.sources.emplace_back(source, std::move(own));
  for (const auto& [type, field] : kDotDuration)
    if (const double v = at_level(r, field, level); v != 0) out.duration[type] += v * scale;
  for (const auto& [field, t] : fields::damage_modifiers()) {
    const double v = at_level(r, field, level) * scale;
    if (v == 0) continue;
    if (t.type == kElemental || t.type == kAll)
      for (int x : fields::spread(t.type, t.modifier)) out.pct[{x, t.modifier}] += v;
    else out.pct[{t.type, t.modifier}] += v;
    if (t.type == kAll && !t.modifier)  // +% All Damage reaches DoT too (MECHANICS §3)
      for (int x : fields::spread(kAll, true)) out.pct[{x, true}] += v;
  }
  // DamageAttributeAbsMod_DamageMultiplier::AddModifierToAccumulator 0x19D200: one entry per record, type 0x3D,
  // value × 0.01. ModifyDamage 0x119280 runs every additive modifier first, then each multiplier entry on its own
  // (CombatAttribute_DamageMultiplier::Execute 0x117E50: every damage type, max(0, 1 + value) × 100 to ScaleDamage
  // 0x113930 / 0x114360, which scales the damage fields and leaves the % sum). So entries compound and multiply
  // the skill's damage after its additive %.
  if (const double m = at_level(r, "offensiveDamageMultModifier", level); m != 0) out.mult *= std::max(0.0, 1 + m * scale / 100);
  out.weapon_pct += at_level(r, "weaponDamagePct", level) * scale;
  out.crit += at_level(r, "offensiveCritDamageModifier", level) * scale;
  out.cooldown += at_level(r, "skillCooldownTime", level);
  if (classes::is_a(r.cls, "SkillSecondary_PetModifier")) out.cooldown += at_level(r, "cooldownTime", level);  // Builder::summed
  out.leech += at_level(r, "offensiveLifeLeechMin", level) * scale;
  add_area(r, level, out.area);
  for (const auto& [in, outk, pct] : {std::tuple{"conversionInType", "conversionOutType", "conversionPercentage"},
                                      std::tuple{"conversionInType2", "conversionOutType2", "conversionPercentage2"}}) {
    const int from = damage_type_named(r.string(in)), to = damage_type_named(r.string(outk));
    const double v = at_level(r, pct, level);
    if (from && to && v > 0) add_pair(out.conversion, from, to, v);
  }
}

// The skill record and its modifiers name some fields apart (projectileExplosionRadius / explosionRadius,
// projectilePiercingChance / projectilePiercing); a record carries one of each pair.
// shortcut: a modifier's projectileLaunchNumber is taken to add to a base skillProjectileNumber or numProjectiles as
// it adds to a base projectileLaunchNumber (0x54A650) — revisit with an in-game projectile count.
void add_area(const arz::Record& r, std::uint32_t level, Area& out, std::string_view only) {
  auto v = [&](std::string_view f) { return only.empty() || only == f ? at_level(r, f, level) : 0.0; };
  out.radius += v("skillTargetRadius");
  out.angle += v("skillTargetAngle");
  out.number += v("skillTargetNumber");
  out.explosion += v("projectileExplosionRadius") + v("explosionRadius");
  out.projectiles += v("skillProjectileNumber") + v("projectileLaunchNumber") + v("numProjectiles");
  out.pierce += v("projectilePiercingChance") + v("projectilePiercing");
  out.sparks += v("sparkMaxNumber");
  out.spark_chance += v("sparkChance");
  out.length += v("waveDistance") + v("maxRange") * classes::is_a(r.cls, "Skill_AttackSpellCone");
  out.start_width += v("waveStartWidth") + v("startWidth");
  out.end_width += v("waveEndWidth") + v("endWidth");
  out.projectile |= r.cls.find("Projectile") != std::string::npos;
  out.chain |= classes::is_a(r.cls, "Skill_AttackChain");
}

// Pack: pack_size enemies spread evenly on a disc of pack_radius around the target, the player among them.
// An area A hits the target and each other enemy with chance min(1, A / pack area).
// - Chain (Skill_AttackChain::ActivateNow 0x4B25C0): max(1, sparkMaxNumber) jumps; before each, IGenerate(0, 100)
//   against sparkChance, the first miss ends the chain; a jump picks an enemy not yet hit (GameEngine::PickObject).
// - Projectiles: each keeps its own hit list (ProjectileBase::AddProjectileModifier 0x4067E0, FilterAndAddKnownTargets
//   0x407730), so every projectile can hit, the same target too. A pierce roll after each hit
//   (ProjectileBase::Penetrate 0x4048E0: chance summed over the modifiers + the projectile's own, against
//   [0.1, 100)) carries it on through the enemies in its line; an explosion hits its area instead.
// - Wave and spell cone: length x mean width. Radius: π r². Weapon cone: angle / 360 of the pack around the player.
// - skillTargetNumber caps an area or cone.
// shortcut: the pack geometry (size, radius, line) is a preset choice and every projectile finds an enemy; fragments
// and beams count one target — revisit with an in-game pack test.
double pack_targets(const Area& a, const Settings& p) {
  const double others = std::max(0.0, p.pack_size - 1);
  const double disc = std::numbers::pi * p.pack_radius * p.pack_radius;
  auto in = [&](double area) { return 1 + others * std::min(1.0, area / disc); };
  if (a.chain) {
    const double q = std::clamp(a.spark_chance / 100, 0.0, 1.0);
    const int jumps = static_cast<int>(std::min(std::max(1.0, a.sparks), std::floor(others)));
    double t = 1, step = 1;
    for (int k = 0; k < jumps; ++k) t += step *= q;
    return t;
  }
  double t = 1;
  if (a.projectile && (a.projectiles > 0 || a.explosion > 0 || a.pierce > 0)) {
    double per = a.explosion > 0 ? in(std::numbers::pi * a.explosion * a.explosion) : 1;
    if (a.pierce > 0) {
      const double c = std::min(1.0, a.pierce / 100);
      const int line = static_cast<int>(std::min(p.pack_size, p.pack_line));
      double hits = 0, step = 1;
      for (int k = 0; k < line; ++k) hits += step, step *= c;
      per = std::max(per, hits);
    }
    // A lone target takes one projectile, as in single target, so a pack of one is one target.
    // shortcut: projectiles past the pack size (shotgun on one enemy, which the hit lists allow) are not counted —
    // revisit with an in-game hit count on one target.
    return std::min(std::max(1.0, a.projectiles), p.pack_size) * per;
  }
  // Leaps, charges and teleports carry a waveDistance (how far they move) but no width: their radius counts.
  if (a.length > 0 && a.start_width + a.end_width > 0) t = in(a.length * (a.start_width + a.end_width) / 2);
  else if (a.radius > 0) t = in(std::numbers::pi * a.radius * a.radius);
  else if (a.angle > 0) t = 1 + others * std::min(1.0, a.angle / 360);
  return a.number > 0 ? std::min(t, std::max(1.0, a.number)) : t;
}

double max_charge_scale(const arz::Record& r, std::uint32_t level) {
  const auto it = r.fields.find("skillChargeMultipliers");
  if (it == r.fields.end() || it->second.numbers.empty()) return 1;
  const auto& m = it->second.numbers;
  const double charges = at_level(r, "skillChargeLevel", level);
  const std::size_t i = charges >= 1 ? std::min(static_cast<std::size_t>(charges) - 1, m.size() - 1) : m.size() - 1;
  return m[i] / 100;  // 0x7A6FA0: 0.01
}

Modifiers modifiers_of(const snap::Snapshot& s, const snap::Skill& sk) {
  Modifiers out;
  for (const std::uint32_t id : sk.modifier_ids)
    for (const auto& m : s.skills)
      if (m.id == id && m.current_level > 0) out.emplace_back(m.record, m.current_level);
  for (const auto& m : s.item_skill_modifiers)
    if (m.skill_id == sk.id && !m.modifier_record.empty()) out.emplace_back(m.modifier_record, 1);
  return out;
}

// Records without damage (Skill_AttackBuffRadius) carry it on their buffSkillName record.
Raw skill_raw(const snap::Snapshot& s, const snap::Skill& sk, const arz::Database& db, std::vector<std::string>& notes) {
  Raw raw;
  const arz::Record* rec = db.record(sk.record);
  if (!rec) return raw;
  raw.speed = classes::speed(rec->cls);
  // Skill_WeaponPool_ChargedScaling (Savagery, Righteous Fervor): CollectLocalOffensiveDamageAttributes 0x510E90,
  // CollectLocalOffensiveModifierAttributes 0x510F80 and CollectLocalWeaponDamage 0x511030 add the skill's own
  // damage, % and weapon % at the current charge scale; its modifiers' at 1. IncrementCharge 0x510100 and
  // DecrementCharge 0x5102A0 move one charge at a time, each kept skillChargeDuration (2 s) without a use.
  // shortcut: sustained combat keeps the charges at the level's max (one use per skillChargeDuration is enough); the
  // ramp after a pause is not counted — revisit with a build that attacks less often than that.
  const double scale = classes::is_a(rec->cls, "Skill_WeaponPool_ChargedScaling") ? max_charge_scale(*rec, sk.current_level) : 1;
  if (!has_damage(db, *rec, sk.current_level))
    if (const arz::Record* buff = db.record(rec->string("buffSkillName"))) rec = buff;
  add_record(db, *rec, sk.current_level, raw, stem(sk.record), scale);
  for (const std::uint32_t id : sk.modifier_ids)
    for (const auto& m : s.skills)
      if (m.id == id && m.current_level > 0)
        if (const arz::Record* mr = db.record(m.record)) add_record(db, *mr, m.current_level, raw, stem(m.record));
  for (const auto& m : s.item_skill_modifiers)
    if (m.skill_id == sk.id && !m.modifier_record.empty()) {
      if (const arz::Record* mr = db.record(m.modifier_record)) add_record(db, *mr, 1, raw, stem(m.modifier_record) + " (item)");
      else notes.push_back("item skill modifier " + stem(m.modifier_record) + " not in the database");
    }
  return raw;
}

namespace {

// " Offensive Ability", "% Offensive Ability", " Vitality Resist": in-game damage names, else engine names spaced.
std::string stat_unit(const fields::Target& t) {
  const std::string_view label = t.kind == fields::Kind::Defense ? damage_label(t.type, false) : std::string_view{};
  std::string_view name = !label.empty() ? label : t.kind == fields::Kind::Char ? names::char_attribute(t.type) : names::combat_attribute(t.type);
  const bool pct = t.modifier || name.ends_with("Modifier");  // e.g. OffensiveAbilityModifier: a %, not flat
  if (name.ends_with("Modifier")) name.remove_suffix(8);
  std::string out = pct ? "%" : "";
  for (const char c : name) {
    if (std::isupper(static_cast<unsigned char>(c))) out += ' ';
    out += c;
  }
  return t.kind == fields::Kind::Defense ? out + " Resist" : out;
}

}  // namespace

std::vector<ModifierPart> modifier_parts(const snap::Snapshot& s, const arz::Record& r, bool main_skill, bool pack,
                                        const arz::Database& db, std::string_view skill) {
  std::vector<ModifierPart> out;
  // An area field is scored when it moves the skill's targets in the default pack (Aegis' sparks do not: its class is
  // no Skill_AttackChain).
  Area base;
  if (pack && main_skill) {
    const auto sk = std::find_if(s.skills.begin(), s.skills.end(), [&](const snap::Skill& k) { return k.record == skill; });
    std::vector<std::string> notes;
    if (sk != s.skills.end()) base = skill_raw(s, *sk, db, notes).area;
    else if (const arz::Record* own = db.record(skill)) add_area(*own, 1, base);
  }
  auto reaches = [&](std::string_view key) {
    Area with = base;
    add_area(r, 1, with, key);
    return pack_targets(with, Settings{}) != pack_targets(base, Settings{});
  };
  std::set<std::string_view> done{"skillMaxLevel"};
  for (const auto& f : fields::flat_damage()) {
    done.insert({f.min, f.max, f.duration, f.chance});
    const double min = at_level(r, f.min, 1);
    if (min == 0) continue;
    double v = (min + std::max(min, at_level(r, f.max, 1))) / 2;
    if (const double chance = at_level(r, f.chance, 1); chance > 0 && chance < 100) v *= chance / 100;
    const bool dot = !f.duration.empty();
    if (const auto* m = find(s.damage_modifiers, f.type); m && f.type != kElemental)
      v *= std::max(0.0, 1 + (dot ? m->dot_modifier : m->damage_modifier) / 100 + attribute_bonus(char_total(s, kCunning), char_total(s, kSpirit), f.type, dot));
    const double seconds = dot ? at_level(r, f.duration, 1) : 0;
    out.push_back({v, " " + std::string(damage_label(f.type, dot)) + (dot ? "/s" : "") +
                          (dot && seconds != 1 ? " for " + fmt(seconds, 1) + "s" : ""), main_skill});
  }
  for (const auto& [in, to, pct] : {std::tuple{"conversionInType", "conversionOutType", "conversionPercentage"},
                                    std::tuple{"conversionInType2", "conversionOutType2", "conversionPercentage2"}}) {
    done.insert({in, to, pct});
    const int a = damage_type_named(r.string(in)), b = damage_type_named(r.string(to));
    if (const double v = at_level(r, pct, 1); a && b && v > 0)
      out.push_back({v, "% " + std::string(damage_label(a, false)) + " converted to " + std::string(damage_label(b, false)), main_skill});
  }
  static constexpr std::pair<std::string_view, std::string_view> kNamed[] = {
      {"weaponDamagePct", "% weapon damage"},       {"offensiveCritDamageModifier", "% crit damage"},
      {"offensiveDamageMultModifier", "% total damage modified"},
      {"skillCooldownTime", "s cooldown"},          {"offensiveLifeLeechMin", "% damage converted to health"},
      {"petLimit", " pet limit"},                   {"spawnObjectsTimeToLive", "s pet duration"},
      {"skillActiveDuration", "s duration"}};
  // add_area()'s modifier fields.
  static constexpr std::pair<std::string_view, std::string_view> kArea[] = {
      {"skillTargetRadius", "m radius"},     {"skillTargetAngle", " degree arc"},  {"skillTargetNumber", " targets"},
      {"explosionRadius", "m explosion"},    {"projectileLaunchNumber", " projectiles"},
      {"projectilePiercing", "% pierce"},    {"sparkChance", "% chain chance"},   {"sparkMaxNumber", " chain jumps"},
      {"waveDistance", "m wave length"},     {"waveStartWidth", "m wave start"},  {"waveEndWidth", "m wave end"},
      {"projectileExplosionRadius", "m explosion"}, {"projectilePiercingChance", "% pierce"},
      {"skillProjectileNumber", " projectiles"},    {"numProjectiles", " projectiles"},
      {"maxRange", "m range"}, {"startWidth", "m cone start"}, {"endWidth", "m cone end"}};
  for (const auto& [key, field] : r.fields) {
    if (field.kind == arz::Field::Kind::Bool || field.numbers.empty() || field.numbers[0] == 0 || done.contains(key) ||
        fields::weapon_flag(key))
      continue;
    const double v = field.numbers[0];
    double scale = 1;
    const auto named = std::find_if(std::begin(kNamed), std::end(kNamed), [&](const auto& n) { return n.first == key; });
    const auto area = std::find_if(std::begin(kArea), std::end(kArea), [&](const auto& n) { return n.first == key; });
    if (area != std::end(kArea))
      out.push_back({v, std::string(area->second), main_skill && pack && reaches(key)});
    else if (const fields::Target* t = fields::damage_modifier(key))
      out.push_back({v, "% " + std::string(damage_label(t->type, t->modifier)) + " damage", main_skill});
    else if (const auto d = std::find_if(std::begin(kDotDuration), std::end(kDotDuration), [&](const auto& x) { return x.second == key; });
             d != std::end(kDotDuration))
      out.push_back({v, "% " + std::string(damage_label(d->first, true)) + " duration", main_skill});
    // shortcut: cooldown counts for any skill (buff and debuff uptime read it too) — revisit when a cooldown
    // modifier on a skill without damage, buff or debuff shows up.
    else if (named != std::end(kNamed))
      out.push_back({v, std::string(named->second),
                     key == "skillCooldownTime" || key == "petLimit" || key == "spawnObjectsTimeToLive" ||
                         (main_skill && key != "skillActiveDuration")});  // a modifier's duration is not read
    else if (key.find("ResistanceReduction") != std::string::npos)
      out.push_back({v, key.ends_with("DurationMin") ? "s resist reduction" : key.find("Percent") != std::string::npos ? "% resist reduction" : " resist reduction", true});
    else if (const fields::Target* t = fields::lookup(key, scale))
      out.push_back({v * scale, stat_unit(*t), true});  // sheet or buff stats
    else
      out.push_back({v, " " + key, false});
  }
  return out;
}

bool skill_on(const snap::Skill& sk, const arz::Record& r) {
  if (sk.passive && (!sk.passive->char_attributes.empty() || !sk.passive->defense.empty())) return true;
  return r.cls == "Skill_Passive";  // the capture holds char / defense collectors only
}

// Own attacks; toggles and auras only add flat damage.
bool attack_class(std::string_view cls) {
  return cls.starts_with("Skill_Attack") || classes::is_a(cls, "Skill_BuffAttackRadiusToggled");
}

SkillKind skill_kind(const arz::Database& db, const std::string& record, std::uint32_t level) {
  SkillKind out;
  const arz::Record* r = db.record(record);
  if (!r || !main_class(r->cls)) return out;
  const arz::Record* d = r;
  if (!has_damage(db, *d, level)) d = db.record(r->string("buffSkillName"));
  if (!d || !has_damage(db, *d, level)) return out;
  out.main = true;
  out.weapon = at_level(*d, "weaponDamagePct", level) > 0;
  Amounts flat;
  add_flat(db, *d, level, 1, flat);
  for (const auto& [k, v] : flat) {
    if (v <= 0) continue;
    const std::vector<int> types = k.first == kElemental ? std::vector<int>{5, 6, 8}
                                   : k.first == kBasePhysical ? std::vector<int>{kPhysical}
                                                              : std::vector<int>{k.first};
    for (const int t : types)
      if (const DamageKey key{t, k.second}; !damage_name(key).empty() &&
                                             std::find(out.types.begin(), out.types.end(), key) == out.types.end())
        out.types.push_back(key);
  }
  return out;
}

std::vector<Skill> main_skills(const snap::Snapshot& s, const Settings& p, const arz::Database* db) {
  std::vector<Skill> out;
  if (!db) return out;
  for (const auto& r : p.skills)
    if (const auto* sk = skill_by_record(s, r); sk && sk->current_level > 0)
      out.push_back({r, p.weigh_by_points ? std::max<double>(points(s, *sk), 1) : 1.0});
  // No listed skill learned yet: pick as auto does.
  const bool picked = out.empty();
  if (picked) {
    std::vector<std::pair<std::uint32_t, std::string>> found;
    for (const auto& sk : s.skills) {
      if (sk.level == 0 || sk.current_level == 0 || sk.devotion_max_level != 0) continue;
      if (!sk.record.starts_with("records/skills/playerclass")) continue;
      const arz::Record* r = db->record(sk.record);
      if (!r || !main_class(r->cls)) continue;
      const arz::Record* d = r;
      if (!has_damage(*db, *d, sk.current_level)) d = db->record(r->string("buffSkillName"));
      if (!d || !has_damage(*db, *d, sk.current_level)) continue;
      found.emplace_back(points(s, sk), sk.record);
    }
    std::sort(found.begin(), found.end(), [](const auto& a, const auto& b) { return a.first != b.first ? a.first > b.first : a.second < b.second; });
    if (found.size() > static_cast<std::size_t>(p.auto_skills)) found.resize(static_cast<std::size_t>(p.auto_skills));
    for (const auto& [points, r] : found) out.push_back({r, static_cast<double>(points)});
  }
  const double total = std::accumulate(out.begin(), out.end(), 0.0, [](double a, const Skill& k) { return a + k.weight; });
  for (auto& k : out) k.weight /= total;
  const snap::Skill* filler = skill_by_record(s, kDefaultAttack);
  if (picked && !out.empty() && p.filler_weight > 0 && filler && filler->current_level > 0) {
    std::vector<std::string> notes;
    const bool all_cooldown = std::all_of(out.begin(), out.end(), [&](const Skill& k) {
      return skill_raw(s, *skill_by_record(s, k.record), *db, notes).cooldown > 0;
    });
    if (all_cooldown) {
      for (auto& k : out) k.weight *= 1 - p.filler_weight;
      out.push_back({kDefaultAttack, p.filler_weight});
    }
  }
  return out;
}

std::map<DamageKey, double> damage_profile(const snap::Snapshot& s, const arz::Database& db, bool global_conversions) {
  Settings auto_pick;
  auto_pick.filler_weight = 0;
  std::map<DamageKey, double> out;
  std::vector<std::string> notes;
  const Gear g = gear(s, db, shared_base(s, s, db));
  for (const auto& k : main_skills(s, auto_pick, &db)) {
    Amounts parts;
    const snap::Skill& sk = *skill_by_record(s, k.record);
    const Raw raw = skill_raw(s, sk, db, notes);
    const Pool own = global_conversions ? Pool{} : main_converted(raw, g, {}, notes);
    skill_damage(s, sk, raw, g, auto_pick, notes, &parts, nullptr, false, global_conversions ? nullptr : &own);
    const double total = std::accumulate(parts.begin(), parts.end(), 0.0, [](double a, const auto& kv) { return a + kv.second; });
    if (total <= 0) continue;
    for (const auto& [key, v] : parts) out[key] += k.weight * v / total;
  }
  const double sum = std::accumulate(out.begin(), out.end(), 0.0, [](double a, const auto& kv) { return a + kv.second; });
  for (auto& [_, v] : out) v /= sum;
  return out;
}

}  // namespace gdc::score
