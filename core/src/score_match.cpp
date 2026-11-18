#include <algorithm>
#include <cctype>

#include "score_internal.h"

namespace gdc::score {
namespace {

bool has_retaliation(const arz::Record& r, std::uint32_t level) {
  for (const auto& [key, _] : r.fields)
    if (std::string_view(key).starts_with("retaliation") && at_level(r, key, level) != 0) return true;
  return false;
}

// Every learned mastery must be listed.
bool masteries_match(const Preset& p, const snap::Snapshot& s, const arz::Database& db, const Labels& labels, Match& m) {
  std::string names;
  for (const auto& sk : s.skills) {
    if (sk.level == 0) continue;
    const arz::Record* r = db.record(sk.record);
    if (!r || r->cls != "Skill_Mastery") continue;
    if (std::find(p.masteries.begin(), p.masteries.end(), sk.record) == p.masteries.end()) {
      m.why = "mastery " + labels.skill(sk.record) + " not in [match]";
      return false;
    }
    names += (names.empty() ? "" : ", ") + labels.skill(sk.record);
  }
  if (names.empty()) {
    m.why = "no mastery";
    return false;
  }
  m.why = "masteries " + names;
  return true;
}

void damage_share_match(const Preset& p, const snap::Snapshot& s, const arz::Database& db, Match& m) {
  const auto profile = damage_profile(s, db);
  if (profile.empty()) {
    m.why += "; no main damage skill";
    return;
  }
  double share = 0;
  std::string terms;
  for (const auto& k : p.match_damage) {
    const auto it = profile.find(k);
    const double v = it != profile.end() ? it->second : 0;
    share += v;
    terms += (terms.empty() ? "" : " + ") + damage_label(k) + " " + fmt(v * 100) + "%";
  }
  m.ok = share >= p.match_share;
  std::vector<std::pair<double, DamageKey>> top;
  for (const auto& [k, v] : profile) top.emplace_back(v, k);
  std::sort(top.rbegin(), top.rend());
  std::string all;
  for (const auto& [v, k] : top) all += (all.empty() ? "" : ", ") + damage_label(k) + " " + fmt(v * 100) + "%";
  m.why += "; " + terms + " = " + fmt(share * 100) + "% " + (m.ok ? ">= " : "< ") + fmt(p.match_share * 100) +
           "% of main skill damage (" + all + ")";
}

}  // namespace


bool tried_first(const Preset& a, const Preset& b) {
  if (a.match_character.empty() != b.match_character.empty()) return !a.match_character.empty();
  if (a.match_characters.empty() != b.match_characters.empty()) return !a.match_characters.empty();
  return a.masteries.size() < b.masteries.size();
}

Archetype archetype(const snap::Snapshot& s, const arz::Database& db) {
  enum Cat { kNone, kPet, kAttack, kWeapon, kRetaliation };
  auto category = [&](const snap::Skill& sk) {
    const arz::Record* r = db.record(sk.record);
    if (!r) return kNone;
    const std::uint32_t lv = sk.current_level;
    if (r->cls == "Skill_SpawnPet" || r->cls == "Skill_TargetedSpawnPet") {
      const arz::Record* pet = effects::summoned(*r, lv, db);
      return pet && pet->cls == "Pet" ? kPet : kNone;
    }
    const arz::Record* buff = db.record(r->string("buffSkillName"));
    // Every playerclass record of these classes has weaponDamagePct.
    if (r->cls.starts_with("Skill_WeaponPool_") || r->cls.starts_with("Skill_WPAttack_")) return kWeapon;
    if (attack_class(r->cls)) {
      const arz::Record* d = has_damage(db, *r, lv) ? r : (buff && has_damage(db, *buff, lv) ? buff : nullptr);
      if (d) return at_level(*d, "weaponDamagePct", lv) > 0 ? kWeapon : kAttack;
    }
    if (has_retaliation(*r, lv) || (buff && has_retaliation(*buff, lv))) return kRetaliation;
    return kNone;
  };
  std::map<std::uint32_t, const snap::Skill*> parent;
  for (const auto& sk : s.skills)
    if (sk.level > 0)
      for (const auto id : sk.modifier_ids) parent.emplace(id, &sk);
  double pet = 0, attack = 0, weapon = 0, retaliation = 0;
  for (const auto& sk : s.skills) {
    if (sk.level == 0 || sk.current_level == 0 || sk.devotion_max_level != 0) continue;
    if (!sk.record.starts_with("records/skills/playerclass")) continue;
    const auto it = parent.find(sk.id);
    const snap::Skill& root = it != parent.end() ? *it->second : sk;
    if (it == parent.end()) {
      const arz::Record* r = db.record(sk.record);
      if (!r || r->cls == "Skill_Mastery") continue;
    }
    switch (category(root)) {
      case kPet: pet += sk.level; break;
      case kWeapon: weapon += sk.level; attack += sk.level; break;
      case kAttack: attack += sk.level; break;
      case kRetaliation: retaliation += sk.level; break;
      case kNone: break;
    }
  }
  Archetype a;
  a.why = "pet " + fmt(pet) + ", retaliation " + fmt(retaliation) + ", attack " + fmt(attack) + " (weapon " +
          fmt(weapon) + ")";
  if (pet > 0 && pet >= attack) a.name = "pet";
  else if (retaliation > 0 && retaliation >= attack) a.name = "retaliation";
  else if (attack == 0) a.name = "";
  else if (weapon * 2 >= attack) a.name = utility::context(s, &db, "").weapon.starts_with("WeaponHunting_") ? "ranged" : "melee";
  else a.name = "caster";
  return a;
}

bool same_save_key(std::string_view a, std::string_view b) {
  return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) {
           return std::tolower(static_cast<unsigned char>(x)) == std::tolower(static_cast<unsigned char>(y));
         });
}

bool in_bound_save(const Preset& p, std::string_view name, std::string_view save) {
  if (save.empty()) return true;
  bool named = false;
  for (const auto& k : p.match_saves) {
    if (same_save_key(k, save)) return true;
    named = named || snap::save_key_name(k) == name;
  }
  return !named;
}

bool binds(const Preset& p, std::string_view name, std::string_view save) {
  return std::find(p.match_characters.begin(), p.match_characters.end(), name) != p.match_characters.end() &&
         in_bound_save(p, name, save);
}

Match match(const Preset& p, const snap::Snapshot& s, const arz::Database& db, const Labels& labels) {
  Match m;
  const bool same_save = in_bound_save(p, s.character_name, s.save);
  if (binds(p, s.character_name, s.save)) {
    m.ok = true;
    m.why = "[match] characters lists " + s.character_name + (s.save.empty() ? "" : " (" + s.save + ")");
    return m;
  }
  if (p.masteries.empty()) {
    m.why = "no [match]";
    return m;
  }
  if (!p.match_character.empty() && p.match_character != s.character_name) {
    m.why = "for character " + p.match_character;
    return m;
  }
  if (!same_save) {
    m.why = "[match] saves binds " + s.character_name + " to another save than " + s.save;
    return m;
  }
  if (!masteries_match(p, s, db, labels, m)) return m;
  if (!p.match_archetype.empty()) {
    const Archetype a = archetype(s, db);
    if (a.name != p.match_archetype) {
      m.why += "; archetype " + (a.name.empty() ? std::string("none") : a.name) + ", not " + p.match_archetype +
               " (" + a.why + ")";
      return m;
    }
    m.why += "; " + a.name + " (" + a.why + ")";
  }
  if (p.match_damage.empty()) {
    m.ok = true;
    return m;
  }
  damage_share_match(p, s, db, m);
  return m;
}

}  // namespace gdc::score
