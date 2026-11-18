#include <algorithm>
#include <charconv>
#include <cmath>
#include <set>
#include <utility>

#include "gdc/attribute_names.h"
#include "gdc/db_fields.h"

#include "tooltip_internal.h"

namespace gdc::tooltip {
namespace {

// Changes count only when they show at the displayed precision.
struct Stat {
  std::string name;
  double before, after;
  int decimals;
  const char* unit;
  const char* stat;
  const char* via = "";
  bool damage = false;
  bool changed() const { return num(before, decimals) != num(after, decimals); }
  double delta() const { return after - before; }
};

std::vector<Stat> stats(const model::Sheet& b, const model::Sheet& a) {
  return {
      // Per-point values: stat_model.cpp; Energy is in no part.
      {"Physique", b.physique, a.physique, 0, "", "", "Life, DA, regen"},
      {"Cunning", b.cunning, a.cunning, 0, "", "", "Life, OA", true},
      {"Spirit", b.spirit, a.spirit, 0, "", "", "Life, Energy, regen", true},
      {"Life", b.life, a.life, 0, "", "life"},
      {"Energy", b.energy, a.energy, 0, "", ""},
      {"Life regen", b.life_regen, a.life_regen, 1, "/s", "life_regen"},
      {"Energy regen", b.energy_regen, a.energy_regen, 1, "/s", "energy_regen"},
      {"OA", b.oa, a.oa, 0, "", "", "", true},
      {"DA", b.da, a.da, 0, "", "da"},
      {"Armor", b.armor, a.armor, 0, "", "armor"},
      {"Run speed", b.run_speed * 100, a.run_speed * 100, 0, "%", "run_speed"},
  };
}

const model::Resist* find(const std::vector<model::Resist>& v, int type) {
  for (const auto& r : v)
    if (r.type == type) return &r;
  return nullptr;
}

std::vector<int> resist_types(const model::Sheet& a, const model::Sheet& b) {
  std::vector<int> t;
  for (const auto* s : {&a, &b})
    for (const auto& r : s->resists)
      if (std::find(t.begin(), t.end(), r.type) == t.end()) t.push_back(r.type);
  std::sort(t.begin(), t.end());
  return t;
}

std::string resist_name(int type, const Labels& labels) {
  std::string name = labels.resist(type);
  if (constexpr std::string_view kTail = " Resist"; name.ends_with(kTail)) name.resize(name.size() - kTail.size());
  return name;
}

}  // namespace

// CC resists are [other] keys; the rest are damage taken.
std::string resist_stat(int type) {
  switch (type) {
    case 13: return "disruption_resist";
    case 42: return "stun_resist";
    case 43: return "sleep_resist";
    case 44: return "trap_resist";
    case 45: return "freeze_resist";
    case 46: return "petrify_resist";
    default: return "resist:" + std::to_string(type);
  }
}

namespace {

struct Row {
  Line line;
  std::string stat;
  double points = 0;
  const char* via = "";
  bool damage = false;
  const char* unscored = "";
  std::string name = {};
  bool kept = false;  // listed only for its score part
};

// No spaces inside a value, so it never wraps.
void sheet_rows(std::vector<Row>& out, const model::Sheet& b, const model::Sheet& a, const Labels& labels,
                const std::set<std::string>& keep) {
  auto pair = [](double x, double y, int decimals) { return num(x, decimals) + "->" + num(y, decimals); };
  for (const auto& s : stats(b, a)) {
    if (s.changed())
      out.push_back({{s.delta() > 0 ? Tone::Better : Tone::Worse,
                      s.name + " " + pair(s.before, s.after, s.decimals) + s.unit + " (" + signed_num(s.delta(), s.decimals) + ")"},
                     s.stat, 0, s.via, s.damage, "", s.name});
    else if (keep.contains(s.stat))
      out.push_back({{Tone::Plain, s.name + " " + num(s.after, s.decimals) + s.unit}, s.stat, 0, s.via, s.damage, "", s.name, true});
  }
  for (int t : resist_types(b, a)) {
    // The cap does not change with a missing row.
    const auto* pb = find(b.resists, t);
    const auto* pa = find(a.resists, t);
    const model::Resist x = pb ? *pb : model::Resist{t, 0, 0, 0, pa->max};
    const model::Resist y = pa ? *pa : model::Resist{t, 0, 0, 0, pb->max};
    const Stat shown{"", x.shown, y.shown, 0, "%", ""};
    const Stat raw{"", x.uncapped, y.uncapped, 0, "%", ""};
    const Stat max{"", x.max, y.max, 0, "%", ""};
    const std::string stat = resist_stat(t);
    if (!shown.changed() && !raw.changed() && !max.changed() && !keep.contains(stat)) continue;
    std::string text = resist_name(t, labels) + " " + (shown.changed() ? pair(x.shown, y.shown, 0) : num(y.shown, 0)) + "%";
    // Unless the sheet says the same (snapshot without engine-capped values).
    const std::string behind = pair(x.uncapped, y.uncapped, 0);
    if (raw.changed() && (x.uncapped > x.max || y.uncapped > y.max) && behind != pair(x.shown, y.shown, 0))
      text += " (" + behind + ")";
    if (max.changed()) text += " max " + pair(x.max, y.max, 0);
    const double d = shown.changed() ? shown.delta() : raw.delta();
    const bool kept = !shown.changed() && !raw.changed() && !max.changed();
    out.push_back({{d > 0 ? Tone::Better : d < 0 ? Tone::Worse : Tone::Plain, text}, stat, 0, "", false, "", resist_name(t, labels), kept});
  }
}

// Char modifiers and defense values are percents.
std::string off_sheet(const std::tuple<fields::Kind, int, bool>& key, double v, const score::BuffUse& b) {
  const auto& [kind, type, modifier] = key;
  if (kind == fields::Kind::Char) {
    if ((type >= 1 && type <= 7) || type == 14 || (type >= 21 && type <= 24)) return "";
    struct Name {
      int type;
      bool modifier;
      std::string_view name;
      bool percent;
    };
    static constexpr Name kNames[] = {
        {13, true, "Total speed", true},      {15, true, "Attack speed", true},       {16, true, "Cast speed", true},
        {18, true, "Max run speed", true},    {19, true, "Max attack speed", true},   {20, true, "Max cast speed", true},
        {27, false, "Avoid melee", true},     {28, false, "Avoid projectiles", true}, {8, false, "Energy reserved", false},
        {25, false, "Block recovery", true}};
    for (const Name& n : kNames)
      if (n.type == type && n.modifier == modifier)
        return signed_num(v, 0) + (n.percent ? "% " : " ") + std::string(n.name);
    return signed_num(v, v == std::round(v) ? 0 : 1) + (modifier ? "% " : " ") +
           pretty("default: " + std::string(names::char_attribute(type)));
  }
  if (kind == fields::Kind::Defense) {
    if (type == combat_attr::kProtection || find(b.off.resists, type) || find(b.on.resists, type)) return "";
    // 16: defensiveTotalSpeedResistance, the engine's "TotalSpeed".
    const std::string name = type == combat_attr::kTotalSpeed ? "Slow resist" : pretty("default: " + std::string(names::combat_attribute(type)));
    return signed_num(v, 0) + "% " + name;
  }
  return "";
}

std::vector<std::string> buff_items(const score::BuffUse& b, const Labels& labels) {
  std::vector<std::string> out;
  for (const auto& s : stats(b.off, b.on))
    if (s.changed()) out.push_back(signed_num(s.delta(), s.decimals) + s.unit + " " + s.name);
  for (int t : resist_types(b.off, b.on)) {
    const auto* x = find(b.off.resists, t);
    const auto* y = find(b.on.resists, t);
    const Stat raw{"", x ? x->uncapped : 0, y ? y->uncapped : 0, 0, "%", ""};
    if (raw.changed()) out.push_back(signed_num(raw.delta(), 0) + "% " + labels.resist(t));
  }
  std::vector<std::pair<std::string, std::string>> damage;
  for (const auto& [key, v] : b.effect.stats) {
    const auto& [kind, type, dot] = key;
    const std::string_view name = kind == fields::Kind::Damage ? score::damage_label(type, dot) : "";
    if (name.empty()) continue;
    // Crit damage is a fraction in totals; others are percent.
    const std::string amount = signed_num(type == combat_attr::kCritDamage ? v * 100 : v, 0);
    const auto same = std::find_if(damage.begin(), damage.end(), [&](const auto& d) { return d.first == amount; });
    if (same != damage.end()) same->second += "/" + std::string(name);
    else damage.emplace_back(amount, name);
  }
  for (const auto& [amount, names] : damage) out.push_back(amount + "% " + names + " damage");
  for (const auto& [key, v] : b.effect.stats)
    if (const std::string item = off_sheet(key, v, b); !item.empty()) out.push_back(item);
  if (b.absorb_pct > 0) out.push_back("absorbs " + num(b.absorb_pct * 100, 0) + "%");
  if (b.absorb_hit > 0) out.push_back("absorbs " + num(b.absorb_hit * 100, 0) + "% of a hit");
  if (!b.effect.conversions.empty()) out.push_back("converts damage");
  for (const auto& [field, v] : b.effect.unmodeled)
    out.push_back(signed_num(v, 0) + "% " + (field == "skillCooldownReduction" ? "Cooldown reduction" : "Energy cost reduction") +
                  " (not modeled)");
  return out;
}

}  // namespace

std::vector<Line> buff_lines(const score::BuffView& before, const score::BuffView& after, const Labels& labels,
                             const Screen& screen) {
  std::vector<Line> out;
  auto pct = [](double u) { return num(u * 100, 0) + "%"; };
  auto in = [](const score::BuffView& v, const std::string& record) {
    const auto it = std::find_if(v.buffs.begin(), v.buffs.end(), [&](const score::BuffUse& b) { return b.record == record; });
    return it != v.buffs.end() ? &*it : nullptr;
  };
  for (const auto& b : after.buffs) {
    const score::BuffUse* was = in(before, b.record);
    std::string t = labels.skill(b.record) + " (" + (was ? "" : "new, ") + "up " + pct(b.uptime);
    if (was && pct(was->uptime) != pct(b.uptime)) t += ", was " + pct(was->uptime);
    t += b.uptime >= 0.5 ? ", included above)" : ", not in the sheet above)";
    const auto items = buff_items(b, labels);
    out.push_back({Tone::Detail, t + (items.empty() ? "" : ":")});
    if (!items.empty())
      for (auto& l : pack("  ", "  ", items, screen)) out.push_back({Tone::Detail, std::move(l)});
  }
  for (const auto& b : before.buffs)
    if (!in(after, b.record)) out.push_back({Tone::Worse, "loses " + labels.skill(b.record) + " (up " + pct(b.uptime) + ")"});
  return out;
}

namespace {

bool sheet_key(std::string_view key) {
  for (const std::string_view k : {"life_regen", "energy_regen", "run_speed", "disruption_resist", "stun_resist", "sleep_resist",
                                   "trap_resist", "freeze_resist", "petrify_resist"})
    if (k == key) return true;
  return false;
}

std::string sig(double v) {
  char buf[48];  // as %.3g
  return {buf, std::to_chars(buf, buf + sizeof buf, v, std::chars_format::general, 3).ptr};
}

// Units: fractions x 100 = %, retaliation per 1000, unlisted raw.
Row other_row(const score::OtherChange& o) {
  struct Unit {
    std::string_view key, name;
    double scale;
    const char* unit;
  };
  static constexpr Unit kUnits[] = {
      {"cooldown_reduction", "Cooldown reduction", 100, "%"}, {"skill_cost_reduction", "Energy cost reduction", 100, "%"},
      {"life_leech", "Life leech", 100, "%"},                 {"experience", "Experience gain", 100, "%"},
      {"retaliation", "Retaliation", 1000, ""},               {"retaliation_modifier", "Retaliation damage", 100, "%"},
      {"pet_bonus", "Pet bonus", 100, "%"},                   {"knockdown_resist", "Knockdown resist", 100, "%"},
      {"slow_resist", "Slow resist", 100, "%"},                {"racial_damage", "Racial damage (mean)", 100, "%"},
      {"racial_defense", "Racial defense (mean)", 100, "%"}};
  const auto u = std::find_if(std::begin(kUnits), std::end(kUnits), [&](const Unit& x) { return x.key == o.key; });
  const std::string name = u != std::end(kUnits) ? std::string(u->name) : pretty(o.key);
  const double scale = u != std::end(kUnits) ? u->scale : 100;
  const double b = o.before * scale, a = o.after * scale;
  const int decimals = num(b, 0) != num(a, 0) ? 0 : 1;
  const std::string text = name + " " + num(b, decimals) + "->" + num(a, decimals) + (u != std::end(kUnits) ? u->unit : "");
  // No weight: CDR acts through Damage and uptime; ECR nowhere.
  const Tone dir = a > b ? Tone::Better : Tone::Worse;
  if (o.key == "cooldown_reduction") return {{dir, text}, "", 0, "buff uptime", true};
  if (o.key == "skill_cost_reduction") return {{dir, text}, "", 0, "", false, "no energy model"};
  const double gain = (a - b) * o.weight;
  return {{gain > 0 ? Tone::Better : gain < 0 ? Tone::Worse : Tone::Plain, text}, o.key, 0, "", false, "", name};
}

}  // namespace

namespace {

// (modified skill, modifier) pairs of an item's records, for skills the character has not learned: the engine
// attaches modifiers to learned skills only (Skill+0x158).
std::multiset<std::pair<std::string, std::string>> unlearned_modifiers(const snap::Snapshot& s, const snap::Item* it,
                                                                       const arz::Database& db) {
  std::multiset<std::pair<std::string, std::string>> out;
  if (!it) return out;
  for (const auto& name : model::item_records(*it))
    if (const arz::Record* r = db.record(name))
      for (const auto& [key, field] : r->fields) {
        if (!key.starts_with("modifierSkillName") || field.strings.empty() || field.strings[0].empty()) continue;
        const std::string skill(r->string("modifiedSkillName" + key.substr(17)));
        const bool learned = std::any_of(s.skills.begin(), s.skills.end(), [&](const snap::Skill& k) {
          return k.record == skill && k.current_level > 0;
        });
        if (!skill.empty() && !learned) out.emplace(skill, field.strings[0]);
      }
  return out;
}

}  // namespace

// Item skill modifiers the swap adds (green) or removes (red), one line per skill, numbers as the item tooltip.
std::vector<Line> modifier_lines(const snap::Snapshot& before, const snap::Snapshot& after, const snap::Item* old,
                                 const snap::Item& candidate, const std::vector<score::Skill>& skills,
                                 const arz::Database* db, const Labels& labels, const Screen& screen) {
  std::vector<Line> out;
  if (!db) return out;
  auto line = [&](const std::string& skill, const std::string& modifier, bool lost, bool learned) {
    const arz::Record* r = db->record(modifier);
    if (!r) return;
    const bool main = std::any_of(skills.begin(), skills.end(), [&](const score::Skill& k) { return k.record == skill; });
    std::vector<std::string> items;
    for (const auto& part : score::modifier_parts(before, *r, main))
      items.push_back(signed_num(lost ? -part.value : part.value, std::abs(part.value) < 10 && part.value != std::round(part.value) ? 1 : 0) + part.unit +
                      (part.scored && learned ? "" : " (not scored)"));
    if (items.empty()) return;
    for (auto& l : pack(labels.skill(skill) + (learned ? " (item): " : " (item, not learned): "), "  ", items, screen))
      out.push_back({lost ? Tone::Worse : Tone::Better, std::move(l)});
  };
  auto has = [](const snap::Snapshot& s, const snap::ItemSkillModifier& m) {
    return std::any_of(s.item_skill_modifiers.begin(), s.item_skill_modifiers.end(), [&](const snap::ItemSkillModifier& x) {
      return x.skill_record == m.skill_record && x.modifier_record == m.modifier_record;
    });
  };
  for (const auto* side : {&before, &after})
    for (const auto& m : side->item_skill_modifiers)
      if (!has(side == &before ? after : before, m)) line(m.skill_record, m.modifier_record, side == &before, true);
  const auto was = unlearned_modifiers(before, old, *db), now = unlearned_modifiers(before, &candidate, *db);
  for (const auto& p : was)
    if (!now.contains(p)) line(p.first, p.second, true, false);
  for (const auto& p : now)
    if (!was.contains(p)) line(p.first, p.second, false, false);
  return out;
}

std::vector<Line> changes(const model::Sheet& before, const model::Sheet& after, const model::Prediction& p,
                          const Labels& labels, const score::Result* scored) {
  auto shows = [](double points) { return num(points, 1) != "0.0"; };
  // Listed when its score part moves, sheet value or not.
  std::set<std::string> keep;
  if (scored)
    for (const auto& c : scored->contributions)
      if (!c.stat.empty() && shows(c.points)) keep.insert(c.stat);
  std::vector<Row> rows;
  sheet_rows(rows, before, after, labels, keep);
  if (scored) {
    for (const auto& o : scored->other_changes)
      if (!sheet_key(o.key)) rows.push_back(other_row(o));
    const bool armor = std::any_of(rows.begin(), rows.end(), [](const Row& r) { return r.stat == "armor"; });
    const bool damage = std::any_of(scored->contributions.begin(), scored->contributions.end(),
                                    [](const score::Contribution& c) { return !c.skill.empty(); });
    // Points carry their line's sign; opposite moves get own lines.
    std::vector<Row> against;
    for (auto& r : rows) {
      const std::string part = r.stat == "armor" ? "resist:2" : r.stat == "resist:2" && armor ? "" : r.stat;
      if (r.stat.empty()) {
        std::string via = r.via;
        if (r.damage && damage) via += via.empty() ? "Damage" : ", Damage";
        r.line.text += !via.empty()   ? " (in " + via + ")"
                       : *r.unscored ? std::string(" (not scored: ") + r.unscored + ")"
                                     : " (not scored)";
      }
      if (part.empty()) continue;
      for (const auto& c : scored->contributions)
        if (c.stat == part || (r.stat == "armor" && c.stat == "armor")) r.points += c.points;
      const auto target = scored->targets.find(r.stat);
      const std::string at = target != scored->targets.end() ? " target " + num(target->second, 0) : "";
      const auto o = std::find_if(scored->other_changes.begin(), scored->other_changes.end(),
                                  [&](const score::OtherChange& x) { return x.key == r.stat; });
      if (o != scored->other_changes.end() && o->weight == 0) r.line.text += " [w0]";
      else if (shows(r.points) && ((r.line.tone == Tone::Better && r.points < 0) || (r.line.tone == Tone::Worse && r.points > 0)))
        against.push_back({{r.points > 0 ? Tone::Better : Tone::Worse, r.name + ": other effects [" + signed_num(r.points, 1) + "]"},
                           "", std::exchange(r.points, 0)});
      else if (shows(r.points))
        r.line.text += " [" + signed_num(r.points, 1) + (o != scored->other_changes.end() ? " w" + sig(o->weight) : "") + at + "]";
    }
    // A row kept for a part that went to another line (Physical's to Armor) says nothing.
    std::erase_if(rows, [&](const Row& r) { return r.kept && !shows(r.points); });
    rows.insert(rows.end(), against.begin(), against.end());
    std::stable_sort(rows.begin(), rows.end(), [](const Row& a, const Row& b) { return std::abs(a.points) > std::abs(b.points); });
  }
  std::vector<Line> out;
  for (auto& r : rows) out.push_back(std::move(r.line));
  if (out.empty()) out.push_back({Tone::Plain, "No sheet change"});
  for (const auto& s : p.skills)
    out.push_back({s.after > s.before ? Tone::Better : Tone::Worse,
                   labels.skill(s.record) + " level " + std::to_string(s.before) + "->" + std::to_string(s.after)});
  for (const auto& s : p.sets)
    out.push_back({s.after > s.before ? Tone::Better : Tone::Worse,
                   "Set " + labels.set(s.set) + " " + std::to_string(s.before) + "->" + std::to_string(s.after)});
  return out;
}

}  // namespace gdc::tooltip
