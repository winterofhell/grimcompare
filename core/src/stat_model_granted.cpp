#include <charconv>

#include "stat_model_internal.h"

namespace gdc::model {
namespace {

// Excludes gdc::fields mappings, flat damage, level caps, weapon flags.
std::vector<std::string> unapplied(const arz::Record& r, std::uint32_t level, bool char_def) {
  std::vector<std::string> out;
  for (const auto& [key, field] : r.fields) {
    if (field.kind != arz::Field::Kind::Int && field.kind != arz::Field::Kind::Float) continue;
    const auto v = at_level(field, level);
    if (!v || *v == 0) continue;
    double scale;
    if (fields::lookup(key, scale) || fields::flat_damage_field(key)) continue;
    if (key == "skillMaxLevel" || key == "skillUltimateLevel") continue;
    if (fields::weapon_flag(key)) continue;
    if (!char_def && (key.starts_with("defensive") || key.starts_with("character"))) continue;
    out.push_back(key);
  }
  return out;
}

Contribution only(const Contribution& c, fields::Kind kind) {
  Contribution out;
  for (const auto& [k, v] : c)
    if (std::get<0>(k) == kind) out[k] = v;
  return out;
}

// 0 for an equation ("itemLevel/4+1").
std::uint32_t constant_level(std::string_view eq) {
  std::uint32_t v = 0;
  const auto [end, ec] = std::from_chars(eq.data(), eq.data() + eq.size(), v);
  return ec == std::errc{} && end == eq.data() + eq.size() ? v : 0;
}

using Grant = std::pair<std::string, std::string>;  // (skill record, itemSkillLevelEq)

std::vector<Grant> item_grants(const snap::Item& it, const arz::Database& db) {
  std::vector<Grant> out;
  for (const auto& name : item_records(it))
    if (const auto rec = db.record(name); rec && !rec->string("itemSkillName").empty())
      out.emplace_back(std::string(rec->string("itemSkillName")), std::string(rec->string("itemSkillLevelEq")));
  return out;
}

// Grants on both items cancel out.
void drop_common(std::vector<Grant>& lost, std::vector<Grant>& gained) {
  for (auto it = lost.begin(); it != lost.end();)
    if (const auto g = std::find(gained.begin(), gained.end(), *it); g != gained.end()) {
      gained.erase(g);
      it = lost.erase(it);
    } else {
      ++it;
    }
}

std::string kind(const arz::Record* r) { return r ? r->cls : std::string("record not in the database"); }

// GetCharAttributes 0x52F920 checks the level only (§11.10).
void remove_passive(const snap::Skill& k, const arz::Record& rec, AllDeltas& out_d, Granted& g) {
  const bool on = (k.passive && (!k.passive->char_attributes.empty() || !k.passive->defense.empty())) ||
                  !fields::weapon_gated(rec);
  const Contribution at = level_effect(rec, k.current_level, k.current_level).at_from;
  const bool has_char_def = std::any_of(at.begin(), at.end(), [](const auto& kv) {
    return std::get<0>(kv.first) != fields::Kind::Damage && kv.second != 0;
  });
  if (!k.passive) {
    g.why = "capture failed";
  } else if (!on && !has_char_def && has_damage(at)) {
    g.why = "on or off unknown (damage only)";
  } else if (on) {
    Contribution c;
    for (const auto& a : k.passive->char_attributes) {
      c[{fields::Kind::Char, a.type, false}] -= a.value;
      c[{fields::Kind::Char, a.type, true}] -= a.modifier;
    }
    for (const auto& d : k.passive->defense) {
      c[{fields::Kind::Defense, d.type, false}] -= d.defense;
      c[{fields::Kind::Defense, d.type, true}] -= d.defense_modifier;
    }
    for (const auto& [key, v] : only(at, fields::Kind::Damage)) c[key] -= v;
    out_d.add(c);
    const auto rest = unapplied(rec, k.current_level, false);
    g.scored = rest.empty();
    if (!rest.empty()) g.why = rest.front();
  } else {
    g.scored = true;
  }
}

Granted lose(const snap::Snapshot& s, const snap::Item& old, const std::string& record, const arz::Database& db,
             AllDeltas& out_d, Prediction& p) {
  Granted g{record, false, false, {}};
  const auto rec = db.record(record);
  const auto k = std::find_if(s.item_skills.begin(), s.item_skills.end(), [&](const snap::Skill& x) {
    return x.record == record && (old.id == 0 || x.item_id == old.id);
  });
  if (s.schema < 6) g.why = "not in snapshot (schema < 6)";
  else if (k == s.item_skills.end()) g.why = "not in snapshot";
  else if (!rec || rec->cls != "Skill_Passive") g.why = kind(rec);
  else remove_passive(*k, *rec, out_d, g);
  if (k != s.item_skills.end())
    std::erase_if(p.after.item_skills, [&](const snap::Skill& x) { return x.id == k->id; });
  return g;
}

Granted gain(const std::string& record, const std::string& eq, const snap::Item& cand, const arz::Database& db,
             AllDeltas& out_d, Prediction& p) {
  Granted g{record, true, false, {}};
  const auto rec = db.record(record);
  const std::uint32_t level = constant_level(eq);
  if (!rec || rec->cls != "Skill_Passive") g.why = kind(rec);
  else if (fields::weapon_gated(*rec)) g.why = "weapon type";
  else if (!rec->string("petBonusName").empty()) g.why = "pet bonus";
  else if (level == 0) g.why = "level " + eq;
  else if (const auto rest = unapplied(*rec, level, true); !rest.empty()) g.why = rest.front();
  else if (const SkillEffect e = level_effect(*rec, level, level); !e.problem.empty()) g.why = e.problem;
  else {
    out_d.add(e.at_from);
    snap::Skill k;  // score_damage.cpp weapon_flat reads its flat damage
    k.record = record;
    k.level = k.current_level = level;
    k.item_id = cand.id;
    add_to(k.passive.emplace(), e.at_from);
    p.after.item_skills.push_back(std::move(k));
    g.scored = true;
  }
  return g;
}

}  // namespace

// Procs, buffs, pets, weapon-gated passives: reported, not scored (§11.10).
void apply_granted(const snap::Snapshot& s, const snap::Item* old, const snap::Item& cand, const arz::Database& db,
                   AllDeltas& out_d, Prediction& p) {
  auto lost = old ? item_grants(*old, db) : std::vector<Grant>{}, gained = item_grants(cand, db);
  drop_common(lost, gained);
  for (const auto& [record, eq] : lost) p.granted.push_back(lose(s, *old, record, db, out_d, p));
  for (const auto& [record, eq] : gained) p.granted.push_back(gain(record, eq, cand, db, out_d, p));
}

}  // namespace gdc::model
