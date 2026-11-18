#include <algorithm>

#include "gdc/attribute_names.h"
#include "gdc/db_fields.h"

#include "score_internal.h"

namespace gdc::score {
// ConvertCombatAttributes 0x1199F0: Elemental is ⅓ Cold, Fire, Lightning.
void split_elemental(Amounts& a) {
  for (const bool dot : {false, true}) {
    const auto it = a.find({kElemental, dot});
    if (it == a.end()) continue;
    const double third = it->second / 3;
    a.erase(it);
    for (int t : fields::spread(kElemental, false)) a[{t, dot}] += third;
  }
}

// Engine conversion order and weighing, as Skill::CollectCombatParameters 0x499E40.
Conversions global_pairs(const std::vector<snap::Conversion>& global) {
  Conversions g;
  for (const auto& c : global) add_pair(g, c.from, c.to, c.percent * 100);
  return g;
}

// convert() with gear and buff pairs prepared once.
// `bonus` (damage × skill %, per type) follows its damage: CombatAttributeAccumulator::ConvertDamage 0x119C90 copies
// the attribute with its % (ConvertCombatAttributes 0x1197B0, vt+0x108) and scales both damage and % by each part's
// share (ScaleDamage 0x113930, ScaleModifiers 0x113970), so a part f of it keeps f² of the bonus.
Amounts convert_pairs(const Amounts& input, const Conversions& skill_pct, const Conversions& g,
                      std::vector<std::string>& notes, const Amounts* bonus, Amounts* bonus_out) {
  auto no_dot = [](int t) { return t == 4 || t == 10 || t == 11; };
  Amounts in = input, in_bonus = bonus ? *bonus : Amounts{};
  split_elemental(in);
  split_elemental(in_bonus);
  Amounts out, out_bonus;
  for (const auto& [key, amount] : in) {
    double left = amount, left_bonus = in_bonus.contains(key) ? in_bonus.at(key) : 0;
    const bool element = key.first == 5 || key.first == 6 || key.first == 8;
    for (const Conversions* level : {&skill_pct, static_cast<const Conversions*>(&g)})
      for (const auto& [from, tos] : *level) {
        if (from != key.first && !(element && from == kElemental)) continue;
        double total = 0;
        for (const auto& [to, pct] : tos) total += std::max(0.0, pct);
        if (total <= 0) continue;
        const double scale = total > 100 ? 100 / total : 1;
        if (total > 100.01) notes.push_back("conversion > 100% from " + std::string(names::combat_attribute(from)) + " (weighed)");
        double moved = 0, share = 0;
        for (const auto& [to, pct] : tos) {
          if (pct <= 0 || (key.second && no_dot(to))) continue;
          const double part = left * pct * scale / 100, f = pct * scale / 100;
          out[{to, key.second}] += part;
          moved += part;
          share += f;
          if (left_bonus == 0) continue;
          if (to == kElemental)  // a third each, each scaled by its own share (0x1199F0)
            for (int t : fields::spread(kElemental, false)) out_bonus[{t, key.second}] += left_bonus * (f / 3) * (f / 3);
          else out_bonus[{to, key.second}] += left_bonus * f * f;
        }
        left -= moved;
        left_bonus *= (1 - share) * (1 - share);
      }
    out[key] += left;
    if (left_bonus != 0) out_bonus[key] += left_bonus;
  }
  split_elemental(out);
  if (bonus_out) *bonus_out = std::move(out_bonus);
  return out;
}

std::map<DamageKey, double> convert(const std::map<DamageKey, double>& input, const Conversions& skill_pct,
                                    const std::vector<snap::Conversion>& global, std::vector<std::string>& notes) {
  return convert_pairs(input, skill_pct, global_pairs(global), notes);
}

std::string conversion_label(int type) {
  if (type == kPoison) return "Acid/Poison";
  if (type == kElemental) return "Elemental";
  return damage_label({type, false});
}

std::vector<Conversion> conversion_warnings(const snap::Snapshot& before, const snap::Snapshot& after,
                                            const Settings& p, const std::vector<DamageKey>& targets,
                                            const arz::Database* db) {
  std::vector<Conversion> out;
  if (p.conversion_warn.empty()) return out;
  // Build types: >= 10% of main damage, design choice.
  // shortcut: skill conversion output counts as source — revisit if it over-warns.
  auto types = [&](bool global_conversions) {
    std::set<int> t;
    if (db)
      for (const auto& [k, share] : damage_profile(before, *db, global_conversions))
        if (share >= 0.1) t.insert(k.first);
    return t;
  };
  std::optional<std::set<int>> source, dealt;
  auto deals = [&](int type) {
    if (!source) source = types(false);
    if (source->empty()) return true;
    if (type == kElemental) return source->contains(5) || source->contains(6) || source->contains(8);
    if (type == kPhysical) return source->contains(kPhysical) || source->contains(kBasePhysical);
    return source->contains(type);
  };
  auto warned = [&](int from, int to) {
    for (const auto& t : targets)
      if (t.first == to) return false;
    const bool listed = std::any_of(p.conversion_warn.begin(), p.conversion_warn.end(), [&](const auto& w) {
      return (w.first == 0 || w.first == from) && (w.second == 0 || w.second == to);
    });
    if (!listed || !deals(from)) return false;
    if (!targets.empty()) return true;  // a preset's [match] types are the build's
    if (!dealt) dealt = types(true);
    return to == kElemental || dealt->empty() || !dealt->contains(to);
  };
  std::map<std::pair<int, int>, double> was, now;
  for (const auto& c : before.conversions) was[{c.from, c.to}] += c.percent * 100;
  for (const auto& c : after.conversions) now[{c.from, c.to}] += c.percent * 100;
  for (const auto& [k, v] : now)
    if (const double d = v - was[k]; d > 0.01 && warned(k.first, k.second)) out.push_back({"", k.first, k.second, d});
  if (!db) return out;
  for (const auto& m : after.item_skill_modifiers) {
    const bool old = std::any_of(before.item_skill_modifiers.begin(), before.item_skill_modifiers.end(),
                                 [&](const snap::ItemSkillModifier& b) {
                                   return b.skill_id == m.skill_id && b.modifier_record == m.modifier_record;
                                 });
    const arz::Record* r = old ? nullptr : db->record(m.modifier_record);
    if (!r) continue;
    Raw raw;
    add_record(*db, *r, 1, raw);
    for (const auto& [from, tos] : raw.conversion)
      for (const auto& [to, pct] : tos)
        if (warned(from, to)) out.push_back({m.skill_record, from, to, pct});
  }
  return out;
}

}  // namespace gdc::score
