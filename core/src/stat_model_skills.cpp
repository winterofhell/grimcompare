#include "gdc/sets.h"

#include "stat_model_internal.h"

namespace gdc::model {
namespace {

// Engine numbers differ from record + modifiers: a conditional part.
void engine_check(const snap::Snapshot& s, const snap::Skill& sk, std::uint32_t from, const Contribution& attached,
                  SkillEffect& e) {
  if (e.problem.empty() && sk.passive) {
    Contribution at_from = e.at_from;
    Contribution engine;
    for (const auto& c : sk.passive->char_attributes) {
      if (c.value != 0) engine[{fields::Kind::Char, c.type, false}] += c.value;
      if (c.modifier != 0) engine[{fields::Kind::Char, c.type, true}] += c.modifier;
    }
    for (const auto& c : sk.passive->defense) {
      if (c.defense != 0) engine[{fields::Kind::Defense, c.type, false}] += c.defense;
      if (c.defense_modifier != 0) engine[{fields::Kind::Defense, c.type, true}] += c.defense_modifier;
    }
    for (const auto& [k, v] : attached) at_from[k] += v;
    std::erase_if(at_from, [](const auto& kv) { return kv.second == 0 || std::get<0>(kv.first) == fields::Kind::Damage; });
    const bool same = engine.size() == at_from.size() &&
                      std::all_of(engine.begin(), engine.end(), [&](const auto& kv) {
                        const auto it = at_from.find(kv.first);
                        return it != at_from.end() && std::abs(it->second - kv.second) <= 0.01;
                      });
    if (!same)
      e.problem = "engine values differ from the record at level " + std::to_string(from) +
                  (s.schema < 5 ? " (learned modifiers are captured from schema 5)" : "");
  }
}

const snap::Skill* modifier_parent(const std::vector<snap::Skill>& skills, std::uint32_t id) {
  for (const auto& k : skills)
    if (std::find(k.modifier_ids.begin(), k.modifier_ids.end(), id) != k.modifier_ids.end()) return &k;
  return nullptr;
}

bool contributes(const snap::Skill& sk, const arz::Record& rec, bool& assumed) {
  if (sk.passive) return !sk.passive->char_attributes.empty() || !sk.passive->defense.empty();
  const bool on = rec.cls == "Skill_Passive" || rec.cls == "Skill_BuffSelfToggled";
  assumed = assumed || on;
  return on;
}

using Modifiers = std::multiset<std::pair<std::string, std::string>>;  // (skill, modifier record)

std::multiset<std::string> records_of(const Modifiers& m, const std::string& skill) {
  std::multiset<std::string> out;
  if (m.empty()) return out;
  for (auto it = m.lower_bound({skill, std::string{}}); it != m.end() && it->first == skill; ++it) out.insert(it->second);
  return out;
}

// One apply_skill_levels pass.
struct Pass {
  const snap::Snapshot& s;
  int location;
  const Hidden& before;
  const Hidden& after;
  const arz::Database& db;
  AllDeltas& deltas;
  Prediction& p;
  std::set<std::string_view, std::less<>> modded = {};  // skills with a modifier on either side
  bool assumed = false;
  std::set<const snap::Skill*> running = {};  // timed buffs up at capture
};

std::multiset<std::string> modifiers_of(const Pass& ps, const Hidden& h, const std::string& skill) {
  if (!ps.modded.contains(skill)) return std::multiset<std::string>{};
  std::multiset<std::string> out = records_of(h.modifiers, skill);
  for (auto& r : records_of(h.set_modifiers, skill)) out.insert(r);
  return out;
}

struct Levels {
  std::uint32_t from = 0, to = 0;
  double augmented = 0;
};

// GetCurrentLevel 0x4909C0: min(level + augmented, cap).
std::optional<Levels> levels(Pass& ps, const snap::Skill& sk, const arz::Record& rec, double d, const std::string& name) {
  // shortcut: Skill_BuffRadius cap read from buffSkillName — revisit when a buff skill at its cap disagrees.
  auto cap_of = [](const arz::Record& r) { return std::max(r.number("skillUltimateLevel"), r.number("skillMaxLevel")); };
  double cap = cap_of(rec);
  if (cap == 0 && !rec.string("buffSkillName").empty())
    if (const auto buff = ps.db.record(rec.string("buffSkillName"))) cap = cap_of(*buff);
  if (cap == 0) {
    ps.p.warnings.push_back("skill " + name + ": no level cap in its record");
    return std::nullopt;
  }
  const double aug = std::max(0.0, sk.augmented_level + d);
  const auto to = static_cast<std::uint32_t>(std::min(sk.level + aug, cap));
  const std::uint32_t from = sk.current_level;
  if (from != std::min<double>(sk.level + sk.augmented_level, cap)) {
    ps.p.warnings.push_back("skill " + name + ": current level " + std::to_string(from) + " does not follow from level + bonus");
    return std::nullopt;
  }
  return Levels{from, to, aug};
}

void add_modifier(const Pass& ps, const std::string& record, double sign, Contribution& into, std::string& problem) {
  const SkillEffect m = modifier_effect(record, ps.db);
  if (!m.problem.empty()) problem = m.problem;
  for (const auto& [k, v] : m.delta) into[k] += sign * v;
}

// Modifiers on the skill at capture: item and learned.
Contribution attached(const Pass& ps, const snap::Skill& sk, std::string& problem) {
  Contribution out;
  for (const auto& m : ps.s.item_skill_modifiers)
    if (m.skill_id == sk.id && !m.modifier_record.empty()) add_modifier(ps, m.modifier_record, +1, out, problem);
  for (const std::uint32_t id : sk.modifier_ids) {
    const auto ms = std::find_if(ps.s.skills.begin(), ps.s.skills.end(), [&](const snap::Skill& k) { return k.id == id; });
    const auto mrec = ms != ps.s.skills.end() ? ps.db.record(ms->record) : nullptr;
    if (!mrec) {
      problem = "modifier skill " + std::to_string(id) + " not in the snapshot or the database";
      continue;
    }
    const SkillEffect& m = ps.db.derived<SkillEffect>(*mrec, ms->current_level, [&] {
      return level_effect(*mrec, ms->current_level, ms->current_level);
    });
    if (!m.problem.empty()) problem = m.problem;
    // AddModifierDefenseAttributes 0x494970: Skill_Modifier only.
    const bool any = std::any_of(m.at_from.begin(), m.at_from.end(), [](const auto& kv) { return kv.second != 0; });
    if (mrec->cls != "Skill_Modifier" && any) problem = "linked " + mrec->cls + " " + ms->record + " has stats";
    for (const auto& [k, v] : m.at_from) out[k] += v;
  }
  return out;
}

// The swapped item's and sets' modifier entries for the skill.
void update_modifier_list(Pass& ps, const snap::Skill& sk) {
  auto& list = ps.p.after.item_skill_modifiers;
  auto push = [&](const std::string& r, int loc) {
    snap::ItemSkillModifier m;
    m.skill_id = sk.id;
    m.skill_record = sk.record;
    m.modifier_record = r;
    m.location = loc;
    list.push_back(std::move(m));
  };
  if (const auto item_added = records_of(ps.after.modifiers, sk.record); records_of(ps.before.modifiers, sk.record) != item_added) {
    std::erase_if(list, [&](const snap::ItemSkillModifier& m) { return m.skill_id == sk.id && m.location == ps.location; });
    for (const auto& r : item_added) push(r, ps.location);
  }
  // Set modifiers' engine slot is unknown, so match by record.
  const auto set_before = records_of(ps.before.set_modifiers, sk.record), set_after = records_of(ps.after.set_modifiers, sk.record);
  for (const auto& r : std::set<std::string>(set_before.begin(), set_before.end()))
    for (auto n = set_before.count(r); n > set_after.count(r); --n) {
      const auto at = std::find_if(list.begin(), list.end(), [&](const snap::ItemSkillModifier& m) {
        return m.skill_id == sk.id && m.modifier_record == r && m.location != ps.location;
      });
      if (at != list.end()) list.erase(at);
    }
  for (const auto& r : std::set<std::string>(set_after.begin(), set_after.end()))
    for (auto n = set_after.count(r); n > set_before.count(r); --n) push(r, -1);
}

// A Skill_Modifier adds through its parent's collector.
void modifier_level(Pass& ps, const snap::Skill& sk, const arz::Record& rec, Levels lv, const std::string& name) {
  Prediction& p = ps.p;
  const snap::Skill* parent = modifier_parent(ps.s.skills, sk.id);
  const auto prec = parent ? ps.db.record(parent->record) : nullptr;
  if (!prec) {
    p.warnings.push_back("skill " + name + ": modifier level change not modeled (" +
                         (ps.s.schema < 5 ? "its parent skill is captured from schema 5)" : "no parent skill)"));
    return;
  }
  const SkillEffect e = level_effect(rec, lv.from, lv.to);
  if (!contributes(*parent, *prec, ps.assumed)) {
    if (parent->buff_active && buff_timing(*parent, ps.db)) {
      p.inactive.push_back({sk.record, parent->record, lv.from, lv.to, true});
      if (*parent->buff_active) ps.running.insert(parent);
    } else if (damage_changes(rec, lv.from, lv.to, ps.db)) {
      p.inactive.push_back({sk.record, parent->record, lv.from, lv.to});
    }
    return;
  }
  if (!e.problem.empty()) {
    p.warnings.push_back("skill " + name + ": " + e.problem);
    return;
  }
  ps.deltas.add(e.delta);
  snap::Skill& pout = *std::find_if(p.after.skills.begin(), p.after.skills.end(),
                                    [&](const snap::Skill& k) { return k.id == parent->id && k.record == parent->record; });
  if (pout.passive) add_to(*pout.passive, e.delta);
}

// A skill whose stats are not in the captured totals.
void inactive_level(Pass& ps, const snap::Skill& sk, const arz::Record& rec, SkillEffect e, Levels lv, bool same_mods) {
  const arz::Record& srec = stats_record(rec, ps.db);
  if (&srec != &rec) e = level_effect(srec, lv.from, lv.to);
  // Capture sees char / defense collectors: damage-only passives count on.
  if (has_damage(e.delta) && e.problem.empty() && rec.cls == "Skill_Passive" && same_mods) {
    ps.deltas.add(e.delta, true);
  } else if (sk.buff_active && buff_timing(sk, ps.db)) {
    if (lv.to != lv.from) ps.p.inactive.push_back({sk.record, {}, lv.from, lv.to, true});
    if (*sk.buff_active) ps.running.insert(&sk);
  } else if (triggered_buff(rec)) {
    if (lv.to != lv.from) ps.p.inactive.push_back({sk.record, {}, lv.from, lv.to, true});
  } else if (damage_changes(rec, lv.from, lv.to, ps.db)) {
    ps.p.inactive.push_back({sk.record, {}, lv.from, lv.to});
  }
}

void apply_skill(Pass& ps, const snap::Skill& sk, const Grants& granted) {
  if (sk.devotion_max_level != 0) return;
  Prediction& p = ps.p;
  const double d = grant_delta(sk, granted, ps.before, ps.after);
  if (sk.level == 0) {  // GetCurrentLevel stays 0 whatever the bonus
    if (d != 0) p.unused.push_back({sk.record, sk.augmented_level, static_cast<std::uint32_t>(std::max(0.0, sk.augmented_level + d))});
    return;
  }
  const auto removed = modifiers_of(ps, ps.before, sk.record), added = modifiers_of(ps, ps.after, sk.record);
  if (d == 0 && removed == added) return;
  const auto rec = ps.db.record(sk.record);
  const std::string name = sk.record.substr(sk.record.rfind('/') + 1);
  if (!rec) {
    p.warnings.push_back("skill " + name + ": record not in the database");
    return;
  }
  // The mastery bar takes no item bonus.
  if (rec->cls == "Skill_Mastery") return;
  const std::optional<Levels> lv = levels(ps, sk, *rec, d, name);
  if (!lv || (lv->to == lv->from && removed == added)) return;
  snap::Skill& out = *std::find_if(p.after.skills.begin(), p.after.skills.end(),
                                   [&](const snap::Skill& k) { return k.id == sk.id && k.record == sk.record; });
  if (lv->to != lv->from) {
    p.skills.push_back({sk.record, lv->from, lv->to});
    out.augmented_level = static_cast<std::uint32_t>(lv->augmented);
    out.current_level = lv->to;
  }
  std::string problem;
  const Contribution attached_now = attached(ps, sk, problem);
  update_modifier_list(ps, sk);
  if (rec->cls == "Skill_Modifier") {
    if (lv->to != lv->from) modifier_level(ps, sk, *rec, *lv, name);
    return;
  }
  SkillEffect e = level_effect(*rec, lv->from, lv->to);
  if (!contributes(sk, *rec, ps.assumed)) {
    inactive_level(ps, sk, *rec, std::move(e), *lv, removed == added);
    return;
  }
  engine_check(ps.s, sk, lv->from, attached_now, e);
  for (const auto& r : removed) add_modifier(ps, r, -1, e.delta, problem);
  for (const auto& r : added) add_modifier(ps, r, +1, e.delta, problem);
  // A modifier's conversion on a skill whose stats hold (a toggle, Werewolf Form) converts all the character's damage
  // while it holds, as a toggle's own stats do (Fenris 5/5: Werewolf Form Pierce and Physical to Cold).
  // shortcut: community reading (buff-skill transmuters convert every skill), engine path not traced — revisit with
  // a capture in and out of Werewolf Form.
  for (const auto* list : {&removed, &added})
    for (const auto& r : *list)
      if (const arz::Record* mr = ps.db.record(r))
        for (const auto& c : sets::conversions(*mr, 1))
          add_conversion(p.after.conversions, c.from, c.to, (list == &removed ? -1 : 1) * c.percent);
  if (e.problem.empty()) e.problem = problem;
  if (!e.problem.empty()) {
    p.warnings.push_back("skill " + name + ": " + e.problem);
    return;
  }
  ps.deltas.add(e.delta);
  if (out.passive) add_to(*out.passive, e.delta);  // keeps engine numbers right for a following swap
}

// Grants to skills the character lacks are unused points.
void unused_grants(const snap::Snapshot& s, const Hidden& before, const Hidden& after, Prediction& p) {
  for (const auto* h : {&before, &after})
    for (const auto& [record, _] : h->skills)
      if (std::none_of(s.skills.begin(), s.skills.end(), [&](const snap::Skill& k) { return k.record == record; }) &&
          std::none_of(p.unused.begin(), p.unused.end(), [&](const SkillLevel& u) { return u.record == record; })) {
        const double b = get(before.skills, record), a = get(after.skills, record);
        if (a != b) p.unused.push_back({record, static_cast<std::uint32_t>(b), static_cast<std::uint32_t>(a)});
      }
}

// Running timed buff changes level: swap old effect for new.
void swap_running(Pass& ps) {
  for (const snap::Skill* sk : ps.running) {
    const auto now = std::find_if(ps.p.after.skills.begin(), ps.p.after.skills.end(),
                                  [&](const snap::Skill& k) { return k.id == sk->id && k.record == sk->record; });
    add_buff(ps.s, *sk, -1, ps.db, ps.deltas, ps.p.after.conversions);
    add_buff(ps.p.after, *now, +1, ps.db, ps.deltas, ps.p.after.conversions);
  }
}

}  // namespace

// No passive data (schema < 4): passives and toggles count on.
void apply_skill_levels(const snap::Snapshot& s, int location, const Hidden& before, const Hidden& after,
                        const arz::Database& db, AllDeltas& out_d, Prediction& p) {
  if (before.all != after.all)
    p.warnings.push_back("all-skills grant changes: applied to every skill (the engine's one exclusion is not traced)");
  Pass ps{s, location, before, after, db, out_d, p};
  for (const auto* m : {&before.modifiers, &before.set_modifiers, &after.modifiers, &after.set_modifiers})
    for (const auto& [k, _] : *m) ps.modded.insert(k);
  const Grants granted = grants(s, before, after);
  for (const auto& sk : s.skills) apply_skill(ps, sk, granted);
  unused_grants(s, before, after, p);
  if (ps.assumed) p.warnings.push_back("skill activity not in snapshot: passives and toggles assumed on");
  swap_running(ps);
}

}  // namespace gdc::model
