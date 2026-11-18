#include <algorithm>
#include <numeric>
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

}  // namespace

void add_flat(const arz::Database& db, const arz::Record& r, std::uint32_t level, double scale, Amounts& out) {
  for (const auto& [k, v] : flat_parts(db, r, level)) out[k] += v * scale;
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

void add_record(const arz::Database& db, const arz::Record& r, std::uint32_t level, Raw& out) {
  add_flat(db, r, level, 1, out.flat);
  for (const auto& [field, t] : fields::damage_modifiers()) {
    const double v = at_level(r, field, level);
    if (v == 0) continue;
    if (t.type == kElemental || t.type == kAll)
      for (int x : fields::spread(t.type, t.modifier)) out.pct[{x, t.modifier}] += v;
    else out.pct[{t.type, t.modifier}] += v;
  }
  out.weapon_pct += at_level(r, "weaponDamagePct", level);
  out.crit += at_level(r, "offensiveCritDamageModifier", level);
  out.cooldown += at_level(r, "skillCooldownTime", level);
  out.leech += at_level(r, "offensiveLifeLeechMin", level);
  for (const auto& [in, outk, pct] : {std::tuple{"conversionInType", "conversionOutType", "conversionPercentage"},
                                      std::tuple{"conversionInType2", "conversionOutType2", "conversionPercentage2"}}) {
    const int from = damage_type_named(r.string(in)), to = damage_type_named(r.string(outk));
    const double v = at_level(r, pct, level);
    if (from && to && v > 0) add_pair(out.conversion, from, to, v);
  }
}

// Records without damage (Skill_AttackBuffRadius) carry it on their buffSkillName record.
Raw skill_raw(const snap::Snapshot& s, const snap::Skill& sk, const arz::Database& db, std::vector<std::string>& notes) {
  Raw raw;
  const arz::Record* rec = db.record(sk.record);
  if (!rec) return raw;
  raw.speed = classes::speed(rec->cls);
  if (!has_damage(db, *rec, sk.current_level))
    if (const arz::Record* buff = db.record(rec->string("buffSkillName"))) rec = buff;
  add_record(db, *rec, sk.current_level, raw);
  for (const std::uint32_t id : sk.modifier_ids)
    for (const auto& m : s.skills)
      if (m.id == id && m.current_level > 0)
        if (const arz::Record* mr = db.record(m.record)) add_record(db, *mr, m.current_level, raw);
  for (const auto& m : s.item_skill_modifiers)
    if (m.skill_id == sk.id && !m.modifier_record.empty()) {
      if (const arz::Record* mr = db.record(m.modifier_record)) add_record(db, *mr, 1, raw);
      else notes.push_back("item skill modifier " + stem(m.modifier_record) + " not in the database");
    }
  return raw;
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
      out.push_back({r, p.weigh_by_points ? std::max<double>(sk->level, 1) : 1.0});
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
      found.emplace_back(sk.level, sk.record);
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

std::map<DamageKey, double> damage_profile(const snap::Snapshot& s, const arz::Database& db) {
  Settings auto_pick;
  auto_pick.filler_weight = 0;
  std::map<DamageKey, double> out;
  std::vector<std::string> notes;
  const Gear g = gear(s, db, shared_base(s, s, db));
  for (const auto& k : main_skills(s, auto_pick, &db)) {
    Amounts parts;
    const snap::Skill& sk = *skill_by_record(s, k.record);
    skill_damage(s, sk, skill_raw(s, sk, db, notes), g, auto_pick, notes, &parts);
    const double total = std::accumulate(parts.begin(), parts.end(), 0.0, [](double a, const auto& kv) { return a + kv.second; });
    if (total <= 0) continue;
    for (const auto& [key, v] : parts) out[key] += k.weight * v / total;
  }
  const double sum = std::accumulate(out.begin(), out.end(), 0.0, [](double a, const auto& kv) { return a + kv.second; });
  for (auto& [_, v] : out) v /= sum;
  return out;
}

}  // namespace gdc::score
