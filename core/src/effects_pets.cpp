// Pet attack rates, experimental (MECHANICS §4).
#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>

#include "effects_internal.h"
#include "gdc/classes.h"
#include "gdc/db_fields.h"

namespace gdc::effects {
namespace {

using Skills = std::vector<std::pair<std::string, std::uint32_t>>;

bool has_flat(const arz::Record& r, std::uint32_t level) {
  for (const auto& f : fields::flat_damage())
    if (at(r, f.min, level) > 0) return true;
  return false;
}

// The pet record, petLimit and time to live after transmuters.
struct Spawn {
  std::string pet;
  double limit = 0, ttl = 0;
};

// GetSpawnObjectTimeToLive 0x493660, AddModifierPetChanges 0x494CE0.
Spawn spawn_of(const Builder& b, const Effect& summon, const arz::Record& skill) {
  Spawn out;
  out.pet = summoned_record(skill, summon.level);
  out.limit = b.summed(summon, skill, "petLimit");
  out.ttl = b.summed(summon, skill, "spawnObjectsTimeToLive");
  for (const auto& [m, lv] : summon.modifiers)
    if (const arz::Record* r = b.db.record(m); r && classes::is_a(r->cls, "Skill_SpawnPetTransmuter")) {
      if (const double t = at(*r, "modSpawnObjectsTimeToLive", lv); t != 0) out.ttl = t;
      const auto it = r->fields.find("modSpawnObjects");
      if (it != r->fields.end() && !it->second.strings.empty())
        out.pet = it->second.strings[std::min<std::size_t>(std::max<std::uint32_t>(summon.level, 1), it->second.strings.size()) - 1];
    }
  return out;
}

// Pets alive on average; no time to live keeps them at the limit.
// shortcut: no petLimit read as one pet — revisit if a permanent summon without petLimit shows up.
double alive_pets(const Effect& summon, const Spawn& spawn, std::string& how) {
  double alive = 0;
  const bool permanent = spawn.ttl <= 0;
  if (summon.maintained || permanent) {
    alive = spawn.limit > 0 ? spawn.limit : 1;
    how = (permanent ? "permanent, kept at " : "kept at ") + fmt(alive, 0);
  } else if (summon.per_sec > 0 && spawn.ttl > 0) {
    alive = std::min(spawn.limit > 0 ? spawn.limit : 1e9, summon.per_sec * spawn.ttl);
    how = fmt(summon.per_sec, 2) + " summons/s x " + fmt(spawn.ttl, 0) + " s, limit " + fmt(spawn.limit, 0);
  }
  return alive;
}

// Skill_Passive skills with flat damage hold the pet's weapon.
Skills weapon_skills(const Skills& list, const arz::Database& db) {
  Skills weapon;
  for (const auto& [n, l] : list)
    if (const arz::Record* r = db.record(n); r && classes::is_a(r->cls, "Skill_Passive") && has_flat(*r, l))
      weapon.emplace_back(n, l);
  return weapon;
}

std::uint32_t level_in(const Skills& list, std::string_view record) {
  for (const auto& [n, l] : list)
    if (n == record) return l;
  return 0;
}

// Live pet speeds from the engine, else the record's.
struct Pace {
  const snap::Pet* live = nullptr;
  double attack = 0, cast = 0;

  template <class Match>
  const snap::PetSkill* anim(Match match) const {
    if (live)
      for (const auto& k : live->skills)
        if (match(k) && k.anim_frames > 0 && k.anim_frame_rate > 0) return &k;
    return nullptr;
  }

  // Uses per second at most, and how.
  double speed_of(const arz::Record& r, const snap::PetSkill* k, std::string& how) const {
    const bool spell = classes::speed(r.cls) == classes::Speed::kCast;
    const double sp = spell ? cast : attack;
    how = std::string(live ? "engine " : "record ") + (spell ? "cast" : "attack") + " speed " + fmt(sp, 2);
    if (!k) return sp;
    how += " x " + std::to_string(k->anim_frame_rate) + " / " + std::to_string(k->anim_frames) + " frames";
    return sp * k->anim_frame_rate / k->anim_frames;
  }

  double speed(const arz::Record& r, std::string_view name, std::string& how) const {
    return speed_of(r, anim([&](const snap::PetSkill& k) { return k.record == name; }), how);
  }
};

Pace pace_of(const snap::Snapshot& s, const Rates& rates, const arz::Record& pet, const std::string& pet_name, bool scaled) {
  Pace p;
  for (const auto& live : s.pets)
    if (live.record == pet_name && live.attack_speed > 0 && live.cast_speed > 0) {
      p.live = &live;
      break;
    }
  // Pet bonus % change since capture; cap not re-applied.
  auto shifted = [](double captured, double mod, double shift) {
    return 1 + mod / 100 > 0 ? captured * std::max(0.0, 1 + (mod + shift) / 100) / (1 + mod / 100) : captured;
  };
  p.attack = p.live ? shifted(p.live->attack_speed, p.live->attack_speed_modifier, scaled ? 0 : rates.pet_attack_shift)
                    : pet.number("characterAttackSpeed") * (1 + (scaled ? 0 : rates.pet_attack_speed) / 100);
  p.cast = p.live ? shifted(p.live->cast_speed, p.live->cast_speed_modifier, scaled ? 0 : rates.pet_cast_shift)
                  : pet.number("characterSpellCastSpeed") * (1 + (scaled ? 0 : rates.pet_cast_speed) / 100);
  return p;
}

struct Use {
  std::string record;
  std::uint32_t level;
  double per_pet;
  std::string how;
  Origin origin;
};

// One pet's action and the share of its time.
struct Uses {
  std::vector<Use> list;
  double busy = 0;
};

// specialAttack<n>: chance / max(delay, cooldown) per second.
void special_uses(const arz::Record& pet, const std::string& attack, const Skills& skills, const Pace& pace,
                  const arz::Database& db, Uses& out) {
  for (const char* n : {"", "2", "3", "4"}) {
    const std::string name(pet.string(std::string("specialAttack") + n + "SkillName"));
    const std::uint32_t lv = level_in(skills, name);
    const arz::Record* r = lv ? db.record(name) : nullptr;
    if (!r || name == attack) continue;
    const double chance = pet.number(std::string("specialAttack") + n + "Chance") / 100;
    const double wait = std::max(pet.number(std::string("specialAttack") + n + "Delay"), at(*r, "skillCooldownTime", lv));
    if (wait <= 0 || chance <= 0) continue;
    std::string how;
    const double sp = pace.speed(*r, name, how), u = sp > 0 ? std::min(chance / wait, sp) : chance / wait;
    if (sp > 0) out.busy += u / sp;
    out.list.push_back({name, lv, u, "special every " + fmt(wait, 1) + " s x " + fmt(chance * 100, 0) + "%, " + how, Origin::Derived});
  }
}

// Other activated skills with a cooldown: once per cooldown.
void cooldown_uses(const std::string& attack, const Skills& skills, const Pace& pace, const arz::Database& db, Uses& out) {
  for (const auto& [name, lv] : skills) {
    const arz::Record* r = db.record(name);
    if (!r || name == attack || !classes::is_a(r->cls, "SkillActivated")) continue;
    if (std::any_of(out.list.begin(), out.list.end(), [&](const Use& u) { return u.record == name; })) continue;
    const double cooldown = at(*r, "skillCooldownTime", lv);
    if (cooldown <= 0) continue;
    std::string how;
    const double sp = pace.speed(*r, name, how), u = sp > 0 ? std::min(1 / cooldown, sp) : 1 / cooldown;
    if (sp > 0) out.busy += u / sp;
    out.list.push_back({name, lv, u, "cooldown " + fmt(cooldown, 1) + " s, " + how, Origin::Derived});
  }
}

// Plain attack fills the time specials leave; preset rate overrides.
void plain_use(const arz::Record& pet, const std::string& pet_name, const std::string& attack, const Skills& skills,
               bool has_weapon, const Pace& pace, const Rates& rates, const arz::Database& db, Uses& out) {
  const arz::Record* plain = attack.empty() ? &pet : db.record(attack);
  if (!plain || (plain == &pet && !has_weapon)) return;
  const std::uint32_t lv = plain == &pet ? 1 : std::max<std::uint32_t>(level_in(skills, attack), 1);
  const std::string& record = plain == &pet ? pet_name : attack;
  if (rates.pet_attacks > 0) {
    out.list.push_back({record, lv, rates.pet_attacks, "preset pet_attack_rate", Origin::Preset});
    return;
  }
  // The pet's own weapon: GetDefaultSkillId(0) holds the animation.
  const snap::PetSkill* k = plain == &pet ? pace.anim([&](const snap::PetSkill& x) { return x.id == pace.live->default_skill_id; })
                                          : pace.anim([&](const snap::PetSkill& x) { return x.record == attack; });
  std::string how;
  if (const double sp = pace.speed_of(*plain, k, how); sp > 0)
    out.list.push_back({record, lv, sp * std::max(0.0, 1 - out.busy),
                        "plain attack, " + how + (out.busy > 0 ? " x (1 - " + fmt(out.busy, 2) + " specials)" : ""),
                        Origin::Derived});
}

struct Parser {
  std::string_view s;
  double level;
  std::size_t i = 0;
  bool ok = true;

  void space() {
    while (i < s.size() && (s[i] == ' ' || s[i] == '\t')) ++i;
  }
  bool eat(char c) {
    space();
    if (i < s.size() && s[i] == c) return ++i, true;
    return false;
  }
  double primary() {
    space();
    if (eat('(')) {
      const double v = sum();
      if (!eat(')')) ok = false;
      return v;
    }
    if (eat('-')) return -primary();
    constexpr std::string_view kName = "charlevel";
    if (s.size() - i >= kName.size()) {
      bool same = true;
      for (std::size_t k = 0; k < kName.size(); ++k)
        same = same && std::tolower(static_cast<unsigned char>(s[i + k])) == kName[k];
      if (same) return i += kName.size(), level;
    }
    double v = 0;
    const auto [end, ec] = std::from_chars(s.data() + i, s.data() + s.size(), v);
    if (ec != std::errc{}) return ok = false, 0.0;
    i = static_cast<std::size_t>(end - s.data());
    return v;
  }
  double power() {
    const double b = primary();
    return eat('^') ? std::pow(b, power()) : b;
  }
  double product() {
    double v = power();
    for (;;) {
      if (eat('*')) v *= power();
      else if (eat('/')) {
        const double d = power();
        if (d == 0) return ok = false, 0.0;
        v /= d;
      } else return v;
    }
  }
  double sum() {
    double v = product();
    for (;;) {
      if (eat('+')) v += product();
      else if (eat('-')) v -= product();
      else return v;
    }
  }
};

}  // namespace

// One effect per pet skill in use, all pets together.
void Builder::pets(const Effect summon, const arz::Record& skill) {
  const Spawn spawn = spawn_of(*this, summon, skill);
  const arz::Record* pet = db.record(spawn.pet);
  if (!pet) return;
  std::string how;
  const double alive = alive_pets(summon, spawn, how);
  const bool scaled = pet->cls == "PetPlayerScaling";
  const Skills skills = pet_skills(*pet, rates.char_level, summon.modifiers, db);
  const Skills weapon = weapon_skills(skills, db);
  const Pace pace = pace_of(s, rates, *pet, spawn.pet, scaled);
  const std::string attack(pet->string("attackSkillName"));
  Uses uses;
  special_uses(*pet, attack, skills, pace, db, uses);
  cooldown_uses(attack, skills, pace, db, uses);
  plain_use(*pet, spawn.pet, attack, skills, !weapon.empty(), pace, rates, db, uses);
  for (const auto& u : uses.list) {
    Effect e;
    e.record = e.skill = u.record;
    e.source = summon.skill;
    e.level = u.level;
    e.pet = true;
    e.player_scaled = scaled;
    e.pets = alive;
    e.pet_record = spawn.pet;
    e.weapon = weapon;
    if (alive > 0 && u.per_pet > 0) {
      e.per_sec = alive * u.per_pet;
      e.origin = u.origin;
      e.basis = fmt(alive, 1) + " pets (" + how + ") x " + fmt(u.per_pet, 2) + "/s (" + u.how + ")";
    } else {
      e.basis = alive > 0 ? "pet attack rate unknown" : "summon rate unknown (" + summon.basis + ")";
    }
    add(std::move(e));
  }
}

std::optional<double> equation(std::string_view expr, double char_level) {
  Parser p{expr, char_level};
  const double v = p.sum();
  p.space();
  if (!p.ok || p.i != expr.size() || !std::isfinite(v)) return std::nullopt;
  return v;
}

Skills pet_skills(const arz::Record& pet, double char_level, const Skills& modifiers, const arz::Database& db) {
  Skills out;
  auto put = [&](std::string name, std::uint32_t lv) {
    if (name.empty() || lv == 0) return;
    for (auto& [n, l] : out)
      if (n == name) return void(l = std::max(l, lv));
    out.emplace_back(std::move(name), lv);
  };
  for (const auto& [key, field] : pet.fields) {
    if (!key.starts_with("skillName") || field.strings.empty()) continue;
    const auto lk = pet.fields.find("skillLevel" + key.substr(9));
    if (lk == pet.fields.end()) continue;
    double lv = lk->second.numbers.empty() ? 0 : lk->second.numbers[0];
    if (!lk->second.strings.empty())
      lv = equation(lk->second.strings[0], char_level).value_or(0);  // shortcut: unparsed equation = not learned
    if (lv >= 1) put(field.strings[0], static_cast<std::uint32_t>(lv));
  }
  for (const auto& [m, lv] : modifiers)
    if (const arz::Record* r = db.record(m); r && classes::is_a(r->cls, "SkillSecondary_PetModifier"))
      put(std::string(r->string("petSkillName")), lv);
  return out;
}

}  // namespace gdc::effects
