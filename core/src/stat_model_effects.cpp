#include "gdc/classes.h"

#include "stat_model_internal.h"

namespace gdc::model {
namespace {

double value_at(const arz::Record& r, std::string_view key, std::uint32_t level) {
  const auto it = r.fields.find(std::string(key));
  if (it == r.fields.end()) return 0;
  return at_level(it->second, level).value_or(it->second.numbers.empty() ? 0 : it->second.numbers.back());
}

// Timed buff classes; toggles, auras, debuffs aren't.
std::optional<BuffTiming> timing(const arz::Record& stats, std::uint32_t level) {
  if (!classes::is_a(stats.cls, "SkillBuff_Passive") && !classes::is_a(stats.cls, "Skill_BuffSelfDuration")) return std::nullopt;
  const double duration = value_at(stats, "skillActiveDuration", level);
  if (duration <= 0) return std::nullopt;
  return BuffTiming{duration, value_at(stats, "skillCooldownTime", level)};
}

std::optional<BuffEffect> learned_effect(const snap::Snapshot& s, const snap::Skill& sk, const arz::Database& db) {
  if (!buff_timing(sk, db)) return std::nullopt;
  const arz::Record& stats = stats_record(*db.record(sk.record), db);
  std::vector<std::pair<const arz::Record*, std::uint32_t>> parts{{&stats, sk.current_level}};
  for (const std::uint32_t id : sk.modifier_ids) {
    const auto ms = std::find_if(s.skills.begin(), s.skills.end(), [&](const snap::Skill& k) { return k.id == id; });
    if (ms == s.skills.end()) return std::nullopt;
    if (ms->current_level == 0) continue;
    const arz::Record* mrec = db.record(ms->record);
    if (!mrec) return std::nullopt;
    parts.emplace_back(mrec, ms->current_level);
  }
  for (const auto& m : s.item_skill_modifiers) {
    if (m.skill_id != sk.id || m.modifier_record.empty()) continue;
    const arz::Record* mrec = db.record(m.modifier_record);
    if (!mrec) return std::nullopt;
    parts.emplace_back(mrec, 1);  // skillMaxLevel 1
  }
  BuffEffect out;
  for (const auto& [r, level] : parts) {
    const sets::Bonus b = sets::bonus(*r, static_cast<int>(level));
    for (const auto& [k, v] : b.stats) out.stats[k] += v;
    for (const auto& c : b.conversions) add_conversion(out.conversions, c.from, c.to, c.percent);
    add_unmodeled(*r, level, out);
  }
  drop_zeros(out.stats);
  return out;
}

}  // namespace

std::optional<double> at_level(const arz::Field& f, std::uint32_t level) {
  if (level == 0 || f.numbers.empty()) return 0.0;
  if (f.numbers.size() == 1) return f.numbers[0];
  if (level > f.numbers.size()) return std::nullopt;
  return f.numbers[level - 1];
}

// Added via the skill's own collector (e.g. possession1).
SkillEffect modifier_effect(const std::string& record, const arz::Database& db) {
  SkillEffect e;
  const auto rec = db.record(record);
  if (!rec) {
    e.problem = "item skill modifier " + record + " not in the database";
    return e;
  }
  for (const auto& [key, field] : rec->fields) {
    if (field.kind != arz::Field::Kind::Int && field.kind != arz::Field::Kind::Float) continue;
    const bool any = std::any_of(field.numbers.begin(), field.numbers.end(), [](double v) { return v != 0; });
    if (!any) continue;
    double scale = 1;
    const fields::Target* t = fields::lookup(key, scale);
    if (!t) {
      if (key.starts_with("defensive") || key.starts_with("character"))
        e.problem = "item skill modifier field " + key + " is not modeled";
      continue;
    }
    if (field.numbers.size() > 1) {
      e.problem = "item skill modifier field " + key + " varies by level";
      continue;
    }
    e.delta[{t->kind, t->type, t->modifier}] += field.numbers[0] * scale;
  }
  return e;
}

SkillEffect level_effect(const arz::Record& rec, std::uint32_t from, std::uint32_t to) {
  SkillEffect e;
  for (const auto& [key, field] : rec.fields) {
    if (field.kind != arz::Field::Kind::Int && field.kind != arz::Field::Kind::Float) continue;
    const auto a = at_level(field, from), b = at_level(field, to);
    double scale = 1;
    const fields::Target* t = fields::lookup(key, scale);
    if (!t) {
      // Only defensive* / character* fields reach predicted sheet values.
      if ((key.starts_with("defensive") || key.starts_with("character")) && a != b)
        e.problem = key + " is not modeled";
      continue;
    }
    if (!a || !b) {
      e.problem = key + " has no value past level " + std::to_string(field.numbers.size());
      continue;
    }
    e.at_from[{t->kind, t->type, t->modifier}] += *a * scale;
    if (*b != *a) e.delta[{t->kind, t->type, t->modifier}] += (*b - *a) * scale;
  }
  return e;
}

void add_to(snap::ItemStats& passive, const Contribution& delta) {
  for (const auto& [k, v] : delta) {
    if (v == 0) continue;
    const auto [kind, type, modifier] = k;
    if (kind == fields::Kind::Damage) continue;
    if (kind == fields::Kind::Char) {
      auto& c = row(passive.char_attributes, type);
      (modifier ? c.modifier : c.value) += static_cast<float>(v);
    } else {
      auto& c = row(passive.defense, type);
      (modifier ? c.defense_modifier : c.defense) += static_cast<float>(v);
    }
  }
}

bool has_damage(const Contribution& c) {
  return std::any_of(c.begin(), c.end(), [](const auto& kv) { return std::get<0>(kv.first) == fields::Kind::Damage && kv.second != 0; });
}

// Skill_AttackBuffRadius-style skills keep damage on their buffSkillName record.
bool damage_changes(const arz::Record& r, std::uint32_t from, std::uint32_t to, const arz::Database& db) {
  auto differs = [&](const arz::Record& x, std::string_view key) {
    if (key.empty()) return false;
    const auto it = x.fields.find(key);
    return it != x.fields.end() && at_level(it->second, from) != at_level(it->second, to);
  };
  for (const arz::Record* x : {&r, db.record(r.string("buffSkillName"))}) {
    if (!x) continue;
    if (differs(*x, "weaponDamagePct")) return true;
    for (const auto& f : fields::flat_damage())
      if (differs(*x, f.min) || differs(*x, f.max) || differs(*x, f.duration) || differs(*x, f.chance)) return true;
    for (const auto& [key, target] : fields::damage_modifiers())
      if (differs(*x, key)) return true;
  }
  return false;
}

// bloodofdreeg1 -> bloodofdreeg1_buff.dbr (SkillBuff_Passive).
const arz::Record& stats_record(const arz::Record& rec, const arz::Database& db) {
  const std::string buff(rec.string("buffSkillName"));
  const arz::Record* b = buff.empty() ? nullptr : db.record(buff);
  return b ? *b : rec;
}

Contribution scaled(const Contribution& c, double f) {
  Contribution out;
  for (const auto& [k, v] : c) out[k] = v * f;
  return out;
}

void add_conversion(std::vector<snap::Conversion>& v, int from, int to, double delta) {
  auto at = std::find_if(v.begin(), v.end(), [&](const snap::Conversion& x) { return x.from == from && x.to == to; });
  if (at == v.end()) at = v.insert(v.end(), snap::Conversion{from, to, 0});
  at->percent = static_cast<float>(at->percent + delta);
  if (std::abs(at->percent) < 1e-6) v.erase(at);
}

void drop_zeros(Contribution& c) {
  std::erase_if(c, [](const auto& kv) { return kv.second == 0; });
}

// Fields at min(level - 1, size - 1), as sets::bonus.
void add_unmodeled(const arz::Record& r, std::uint32_t level, BuffEffect& out) {
  if (level == 0) return;
  for (const char* name : {"skillCooldownReduction", "skillManaCostReduction"}) {
    const auto it = r.fields.find(name);
    if (it == r.fields.end() || it->second.numbers.empty()) continue;
    const auto& n = it->second.numbers;
    if (const double v = n[std::min<std::size_t>(level - 1, n.size() - 1)]; v != 0) out.unmodeled[name] += v;
  }
}

void add_effect(const BuffEffect& e, double f, AllDeltas& all, std::vector<snap::Conversion>& out) {
  all.add(scaled(e.stats, f));
  for (const auto& c : e.conversions) add_conversion(out, c.from, c.to, f * c.percent);
}

bool add_buff(const snap::Snapshot& s, const snap::Skill& sk, double f, const arz::Database& db, AllDeltas& all,
              std::vector<snap::Conversion>& out) {
  const auto e = learned_effect(s, sk, db);
  if (e) add_effect(*e, f, all, out);
  return e.has_value();
}

bool self_buff(const arz::Record& r) {
  return classes::is_a(r.cls, "Skill_BuffSelfDuration") || r.cls == "Skill_BuffSelfShield" || r.cls == "Skill_GiveBonus" ||
         triggered_buff(r) || classes::is_a(r.cls, "SkillBuff_Passive");
}

// Engine subclasses included: Skill_PassiveOnCritBuffSelf (playerclass07/passive02), Skill_PassiveOnHitBuffShield.
bool triggered_buff(const arz::Record& r) {
  return classes::is_a(r.cls, "Skill_PassiveOnLifeBuffSelf") || classes::is_a(r.cls, "Skill_PassiveOnHitBuffSelf");
}

std::optional<BuffTiming> buff_timing(const snap::Skill& sk, const arz::Database& db) {
  if (sk.level == 0 || sk.current_level == 0 || sk.devotion_max_level != 0) return std::nullopt;
  if (!sk.record.starts_with("records/skills/playerclass")) return std::nullopt;
  const arz::Record* rec = db.record(sk.record);
  return rec ? timing(stats_record(*rec, db), sk.current_level) : std::nullopt;
}

BuffEffect buff_effect(const arz::Record& stats, std::uint32_t level) {
  BuffEffect out{level_effect(stats, level, level).at_from, {}, {}};
  add_unmodeled(stats, level, out);
  drop_zeros(out.stats);
  return out;
}

BuffEffect buff_effect(const snap::Snapshot& s, const snap::Skill& learned, const arz::Database& db) {
  return learned_effect(s, learned, db).value_or(BuffEffect{});
}

void buffed(const snap::Snapshot& s, std::span<const std::pair<const BuffEffect*, double>> buffs,
            const arz::Database& db, snap::Snapshot& out) {
  out.char_attributes = s.char_attributes;
  out.defense = s.defense;
  out.damage_modifiers = s.damage_modifiers;
  out.conversions = s.conversions;
  AllDeltas all;
  bool any = false;
  for (const auto& [e, share] : buffs) {
    if (share == 0 || e->empty()) continue;
    add_effect(*e, share, all, out.conversions);
    any = true;
  }
  if (!any) return;
  Prediction p;
  p.after = std::move(out);
  derive(s, &db, all, p);
  out = std::move(p.after);
}


}  // namespace gdc::model
