#include "gdc/tooltip.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <map>
#include <optional>
#include <set>
#include <utility>

#include "gdc/attribute_names.h"
#include "gdc/components.h"
#include "gdc/db_fields.h"
#include "gdc/font.h"
#include "gdc/requirements.h"

namespace gdc::tooltip {
namespace {

constexpr std::size_t kMaxWarnings = 6;
constexpr std::size_t kMaxUnused = 3;
}  // namespace

bool fits(const Screen& screen, std::string_view line) {
  const std::string_view sample = screen.width >= 1600 ? "The quick brown fox jumped over the lazy dog The quick"
                                                       : "The quick brown fox jumped over the lazy";
  return font::width(font::Face::LinBiolinumSans, font::pixel_size(17, screen.ui_scale), line) <=
         font::width(font::Face::Jura, font::pixel_size(16, screen.ui_scale), sample);
}

std::vector<std::string> pack(const std::string& head, const std::string& indent, const std::vector<std::string>& items,
                              const Screen& screen) {
  std::vector<std::string> out{head};
  bool fresh = true;
  for (std::size_t i = 0; i < items.size(); ++i) {
    const std::string item = items[i] + (i + 1 < items.size() ? "," : "");
    if (!fresh && !fits(screen, out.back() + " " + item)) {
      out.push_back(indent);
      fresh = true;
    }
    out.back() += (fresh ? "" : " ") + item;
    fresh = false;
  }
  return out;
}

namespace {

std::string num(double v, int decimals) {
  const double scale = std::pow(10.0, decimals);
  double r = std::round(v * scale) / scale;
  if (r == 0) r = 0;
  char buf[48];
  std::snprintf(buf, sizeof buf, "%.*f", decimals, r);
  return buf;
}

std::string signed_num(double v, int decimals) {
  const std::string s = num(v, decimals);
  return s[0] == '-' || s.find_first_not_of("0.") == std::string::npos ? s : "+" + s;
}

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
      // Life 2.5 / 1 / 1.5 and Energy 2 per point (stat_model.cpp); attribute damage % in score.cpp attribute_bonus.
      // The Energy pool is in no score part.
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

// CC resists are [other] keys (utility::values); the rest are damage taken per type.
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

struct Row {
  Line line;
  std::string stat;
  double points = 0;
  const char* via = "";
  bool damage = false;
  const char* unscored = "";
  std::string name = {};
};

// No space inside a value, so the game never wraps one.
void sheet_rows(std::vector<Row>& out, const model::Sheet& b, const model::Sheet& a, const Labels& labels,
                const std::set<std::string>& keep) {
  auto pair = [](double x, double y, int decimals) { return num(x, decimals) + "->" + num(y, decimals); };
  for (const auto& s : stats(b, a)) {
    if (s.changed())
      out.push_back({{s.delta() > 0 ? Tone::Better : Tone::Worse,
                      s.name + " " + pair(s.before, s.after, s.decimals) + s.unit + " (" + signed_num(s.delta(), s.decimals) + ")"},
                     s.stat, 0, s.via, s.damage, "", s.name});
    else if (keep.contains(s.stat))
      out.push_back({{Tone::Plain, s.name + " " + num(s.after, s.decimals) + s.unit}, s.stat, 0, s.via, s.damage, "", s.name});
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
    out.push_back({{d > 0 ? Tone::Better : d < 0 ? Tone::Worse : Tone::Plain, text}, stat, 0, "", false, "", resist_name(t, labels)});
  }
}

std::string pretty(std::string key);

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
    if (type == 40 || find(b.off.resists, type) || find(b.on.resists, type)) return "";
    // 16: defensiveTotalSpeedResistance, the engine's "TotalSpeed".
    const std::string name = type == 16 ? "Slow resist" : pretty("default: " + std::string(names::combat_attribute(type)));
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
    // Crit damage is a fraction in the totals (db_fields.h lookup()); every other damage % a percent.
    const std::string amount = signed_num(type == 59 ? v * 100 : v, 0);
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

std::string pretty(std::string key);

std::string part_name(const score::Contribution& c, const Labels& labels) {
  for (const int t : {13, 42, 43, 44, 45, 46})
    if (c.stat == resist_stat(t)) return labels.resist(t);
  return pretty(c.label);
}

std::vector<std::string> top(const score::Result& r, const Labels& labels, const std::string& indent, const Screen& screen) {
  std::vector<std::string> items;
  for (std::size_t i = 0; i < r.contributions.size() && i < 3; ++i) {
    const auto& c = r.contributions[i];
    items.push_back((c.skill.empty() ? part_name(c, labels) : labels.skill(c.skill)) + " " + signed_num(c.points, 1));
  }
  return items.empty() ? items : pack(indent + "top: ", indent + "  ", items, screen);
}

std::string terms(const score::Result& r) {
  auto pct = [](double ratio) { return signed_num((ratio - 1) * 100, 1) + "%"; };
  return "Damage " + pct(r.ratio.damage) + ", Survival " + pct(r.ratio.survival) + ", Other " + pct(r.ratio.other);
}

void add_unique(std::vector<std::string>& v, std::string s) {
  if (std::find(v.begin(), v.end(), s) == v.end()) v.push_back(std::move(s));
}

const snap::Item* at(const snap::Snapshot& s, int location) {
  for (const auto& it : s.equipped)
    if (it.location == location) return &it;
  return nullptr;
}

// Category 2 is a percentage (MECHANICS §1).
std::string rr_amount(const effects::Reduction& r, const Labels& labels) {
  double lo = r.amount.begin()->second, hi = lo;
  for (const auto& [_, v] : r.amount) lo = std::min(lo, v), hi = std::max(hi, v);
  const std::string pct = r.category == 2 ? "%" : "";
  if (r.amount.size() > 3)
    return "-" + (lo == hi ? num(lo, 0) + pct + " all resists"
                           : num(lo, 0) + ".." + num(hi, 0) + pct + " to " + std::to_string(r.amount.size()) + " resists");
  std::string t;
  for (const auto& [type, v] : r.amount) t += (t.empty() ? "-" : ", -") + num(v, 0) + pct + " " + labels.resist(type);
  return t;
}

std::string rr_uptime(const effects::Reduction& r) {
  static constexpr const char* kOrigin[] = {"derived", "preset", "default !"};
  return ", up " + num(r.uptime * 100, 0) + "% (" + kOrigin[static_cast<int>(r.origin)] + ")";
}

std::vector<Line> rr_lines(const score::Result& r, const Labels& labels) {
  std::vector<Line> out;
  std::vector<const effects::Reduction*> paired;
  for (const auto& lost : r.rr_lost) {
    const auto g = std::find_if(r.rr_gained.begin(), r.rr_gained.end(), [&](const effects::Reduction& x) {
      return x.record == lost.record && x.category == lost.category &&
             std::find(paired.begin(), paired.end(), &x) == paired.end();
    });
    if (g == r.rr_gained.end()) {
      out.push_back({Tone::Worse, "loses " + labels.skill(lost.record) + ": " + rr_amount(lost, labels) + rr_uptime(lost)});
      continue;
    }
    paired.push_back(&*g);
    const std::string was = rr_amount(lost, labels), now = rr_amount(*g, labels);
    std::string t = labels.skill(lost.record) + ": " + (was == now ? now : was + " -> " + now);
    if (std::abs(lost.uptime - g->uptime) >= 0.005) t += ", up " + num(lost.uptime * 100, 0) + "% -> " + num(g->uptime * 100, 0) + "%";
    out.push_back({Tone::Detail, t});
  }
  for (const auto& g : r.rr_gained)
    if (std::find(paired.begin(), paired.end(), &g) == paired.end())
      out.push_back({Tone::Better, labels.skill(g.record) + ": " + rr_amount(g, labels) + rr_uptime(g)});
  return out;
}

std::string pretty(std::string key) {
  if (key.starts_with("default: ")) {
    key = key.substr(key.rfind(' ') + 1);
    std::string out;
    for (std::size_t i = 0; i < key.size(); ++i) {
      if (i && std::isupper(static_cast<unsigned char>(key[i])) && std::islower(static_cast<unsigned char>(key[i - 1]))) out += ' ';
      out += key[i];
    }
    return out;
  }
  if (key.find('_') == std::string::npos) return key;
  std::replace(key.begin(), key.end(), '_', ' ');
  key[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(key[0])));
  return key;
}

std::vector<Line> target_set(const snap::Snapshot& s, const snap::Item& candidate, const score::Preset& p,
                             const arz::Database* db, const Labels& labels) {
  std::vector<Line> out;
  if (!db) return out;
  for (const auto& set : p.target_sets) {
    const arz::Record* r = db->record(set);
    if (!r) continue;
    std::vector<std::string> members;
    for (std::size_t i = 0; !r->string("setMembers", i).empty(); ++i) members.emplace_back(r->string("setMembers", i));
    if (std::find(members.begin(), members.end(), candidate.record) == members.end()) continue;
    auto has = [](const std::vector<snap::Item>& v, const std::string& rec) {
      return std::any_of(v.begin(), v.end(), [&](const snap::Item& x) { return x.record == rec; });
    };
    int worn = 0;
    std::string missing;
    for (const auto& m : members) {
      if (has(s.equipped, m)) {
        ++worn;
        continue;
      }
      missing += (missing.empty() ? "" : ", ") + labels.item(m) +
                 (m == candidate.record ? " (this)" : has(s.inventory, m) ? " (in bag)" : "");
    }
    out.push_back({Tone::Better, "target set " + labels.set(set) + ": " + std::to_string(worn) + "/" +
                                     std::to_string(members.size()) + " worn"});
    if (!missing.empty()) out.push_back({Tone::Detail, "  missing: " + missing});
  }
  return out;
}

model::Prediction take_off(const snap::Snapshot& s, int location, const arz::Database* db) {
  snap::Item none;
  none.stats.emplace();
  model::Prediction p = model::swap(s, none, location, db);
  auto& eq = p.after.equipped;
  eq.erase(std::remove_if(eq.begin(), eq.end(), [&](const snap::Item& x) { return x.location == location && x.record.empty(); }),
           eq.end());
  return p;
}

bool met_before(const snap::Item& e, const snap::Snapshot& s) {
  if (const auto engine = requirements::engine_met(e, s)) return *engine;
  return requirements::unmet(e, s).empty();
}

// An item the swap makes unequippable is scored as removed. Taking one off can break another, so at most kMaxBroken rounds.
constexpr int kMaxBroken = 4;
struct Broken {
  int location = -1;
  std::string record;
  std::vector<requirements::Unmet> why;
};
std::vector<Broken> take_off_broken(const snap::Snapshot& before, snap::Snapshot& after, int location,
                                    const arz::Database* db, std::vector<std::string>& warnings) {
  std::vector<Broken> out;
  for (int round = 0; round < kMaxBroken; ++round) {
    const snap::Item* hit = nullptr;
    std::vector<requirements::Unmet> why;
    for (const auto& e : after.equipped) {
      if (e.location == location || !e.requirements) continue;
      const auto was = std::find_if(before.equipped.begin(), before.equipped.end(),
                                    [&](const snap::Item& b) { return b.id == e.id && b.location == e.location; });
      if (was == before.equipped.end() || !met_before(*was, before)) continue;
      why = requirements::unmet(e, after);
      if (!why.empty()) {
        hit = &e;
        break;
      }
    }
    if (!hit) break;
    out.push_back({hit->location, hit->record, why});
    model::Prediction p = take_off(after, hit->location, db);
    for (auto& w : p.warnings) warnings.push_back(std::move(w));
    after = std::move(p.after);
  }
  return out;
}

}  // namespace

std::string escape(std::string_view text) {
  std::string out;
  for (const char c : text) {
    if (c == '{' || c == '}') out += c == '{' ? '(' : ')';
    else out.append(c == '^' || c == '&' ? 2 : 1, c);
  }
  return out;
}

Line change(double percent, double negligible) {
  if (!(std::abs(percent) >= negligible) || percent == 0) return {Tone::Plain, "~0%"};
  if (percent > 0) return {Tone::Better, "+" + num(percent, 1) + "% ^"};
  return {Tone::Worse, num(percent, 1) + "% v"};
}

namespace {

bool sheet_key(std::string_view key) {
  for (const std::string_view k : {"life_regen", "energy_regen", "run_speed", "disruption_resist", "stun_resist", "sleep_resist",
                                   "trap_resist", "freeze_resist", "petrify_resist"})
    if (k == key) return true;
  return false;
}

std::string sig(double v) {
  char buf[48];
  std::snprintf(buf, sizeof buf, "%.3g", v);
  return buf;
}

// Units: fractions x 100 = %, retaliation per 1000; unlisted stats raw.
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
  // No [other] weight: CDR works inside Damage and buff uptime; energy cost reduction is in no part.
  const Tone dir = a > b ? Tone::Better : Tone::Worse;
  if (o.key == "cooldown_reduction") return {{dir, text}, "", 0, "buff uptime", true};
  if (o.key == "skill_cost_reduction") return {{dir, text}, "", 0, "", false, "no energy model"};
  const double gain = (a - b) * o.weight;
  return {{gain > 0 ? Tone::Better : gain < 0 ? Tone::Worse : Tone::Plain, text}, o.key, 0, "", false, "", name};
}

}  // namespace

std::vector<Line> changes(const model::Sheet& before, const model::Sheet& after, const model::Prediction& p,
                          const Labels& labels, const score::Result* scored) {
  auto shows = [](double points) { return num(points, 1) != "0.0"; };
  // Listed even when the sheet value stays and its score part moves (share of Life, buff mixes).
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
    // A line's points carry the sign of its own change; a score part that moves the other way gets its own line.
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
        if (c.stat == part) r.points += c.points;
      const auto o = std::find_if(scored->other_changes.begin(), scored->other_changes.end(),
                                  [&](const score::OtherChange& x) { return x.key == r.stat; });
      if (o != scored->other_changes.end() && o->weight == 0) r.line.text += " [w0]";
      else if (shows(r.points) && ((r.line.tone == Tone::Better && r.points < 0) || (r.line.tone == Tone::Worse && r.points > 0)))
        against.push_back({{r.points > 0 ? Tone::Better : Tone::Worse, r.name + ": other effects [" + signed_num(r.points, 1) + "]"},
                           "", std::exchange(r.points, 0)});
      else if (shows(r.points))
        r.line.text += " [" + signed_num(r.points, 1) + (o != scored->other_changes.end() ? " w" + sig(o->weight) : "") + "]";
    }
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

std::vector<int> locations_for(int item_type) {
  switch (item_type) {
    case 1: return {7};
    case 2: return {2};
    case 3: return {8};      // location 14 "Clothing" also takes type 3: not a player slot
    case 4: return {11};
    case 5: return {12};
    case 6: return {4, 5};
    case 7: return {10};
    case 8: return {1};      // shield, caster off-hand
    case 9: return {0, 1};   // L Hand only while dual wielding
    case 11: return {13};
    case 13: return {6};
    case 14: return {9};
    case 15: return {3};
    default: return {};
  }
}

namespace {

struct Plan {
  int location = -1;
  int also_off = -1;
  std::string title;
};

bool weapon(const snap::Item* it, const arz::Database* db) {
  const arz::Record* r = it && db ? db->record(it->record) : nullptr;
  return r && r->cls.starts_with("Weapon") && !r->cls.starts_with("WeaponArmor");
}

// *2h classes in 1.3.1.1: WeaponMelee_Sword2h, Axe2h, Mace2h, Spear2h, WeaponHunting_Ranged2h.
bool two_handed(const snap::Item* it, const arz::Database* db) {
  const arz::Record* r = it && db ? db->record(it->record) : nullptr;
  return r && r->cls.ends_with("2h");
}

// The engine keeps a two-handed weapon in R Hand with L Hand empty, so a two-hander replaces the pair and
// an off-hand next to one replaces it too.
std::vector<Plan> plans(const snap::Snapshot& s, const snap::Item& candidate, int item_type, const arz::Database* db) {
  std::vector<Plan> out;
  auto title = [&](int loc) {
    std::string t = "vs " + std::string(names::equip_location(loc));
    if (loc == 4 || loc == 5) t += loc == 4 ? " 1" : " 2";
    return t + (at(s, loc) ? "" : " (empty)");
  };
  const snap::Item *rh = at(s, 0), *lh = at(s, 1);
  if (item_type == 9) {
    if (two_handed(&candidate, db) && lh) out.push_back({0, 1, "vs R Hand + L Hand"});
    else out.push_back({0, -1, title(0)});
    if (!two_handed(&candidate, db) && weapon(lh, db)) out.push_back({1, -1, title(1)});
  } else if (item_type == 8 && two_handed(rh, db)) {
    out.push_back({1, 0, "vs R Hand (two-handed)"});
  } else {
    for (int loc : locations_for(item_type)) out.push_back({loc, -1, title(loc)});
  }
  return out;
}

struct Slot {
  std::string title;
  score::Result base;
  std::optional<score::Result> itself;
  std::optional<score::Result> moved;
  std::string moved_names, misfit;
  std::optional<score::Result> hovered;  // with both items' components, when that differs from base
  std::vector<Line> sets;
  std::vector<Line> broken;
  std::vector<Line> detail;
  std::vector<Line> buffs;
};

void unused_lines(std::vector<Line>& out, const std::map<std::string, std::uint32_t>& unused, bool detail, const Labels& labels,
                  const Screen& screen) {
  std::map<std::pair<std::string, std::uint32_t>, int> groups;
  for (const auto& [record, n] : unused)
    if (const std::string m = labels.mastery(record); !m.empty()) ++groups[{m, n}];
  std::vector<std::string> single;
  for (const auto& [record, n] : unused) {
    const std::string m = labels.mastery(record);
    if (m.empty() || groups[{m, n}] < 3) single.push_back(labels.skill(record) + " +" + std::to_string(n));
  }
  for (const auto& [key, count] : groups)
    if (count >= 3)
      out.push_back({Tone::Detail, "+" + std::to_string(key.second) + " " + key.first + ": " + std::to_string(count) + " unused skills (0 points)"});
  if (!detail && single.size() > kMaxUnused) {
    const std::size_t more = single.size() - kMaxUnused;
    single.resize(kMaxUnused);
    single.push_back("... " + std::to_string(more) + " more (Shift)");
  }
  if (!single.empty())
    for (auto& l : pack("unused (0 points): ", "  ", single, screen)) out.push_back({Tone::Detail, std::move(l)});
}

std::string unscored_reason(const std::string& why) {
  if (why.empty() || why.starts_with("Skill")) return "not modeled";
  if (why.starts_with("level ")) return "level unknown";
  return why;
}

}  // namespace

std::vector<Line> compare(const snap::Snapshot& raw, const snap::Item& candidate, int item_type, bool detail,
                          const arz::Database* db, const score::Preset& chosen, const Labels& labels,
                          std::vector<std::string>* log, std::vector<Scored>* scored, const Screen& screen) {
  std::vector<Line> out;
  for (const auto& e : raw.equipped)
    if (e.id != 0 && e.id == candidate.id) return out;
  if (locations_for(item_type).empty()) return out;
  const snap::Snapshot s = model::expected(raw, db);
  const score::Stage* stage = score::stage(chosen, s);
  score::Settings live = stage ? stage->settings : chosen;
  if (live.live_buffs && db) {
    live.recast_buffs = false;
    for (const auto& sk : raw.skills)
      if (sk.buff_active && model::buff_timing(sk, *db)) live.uptime[sk.record] = *sk.buff_active;
  }
  const score::Settings& preset = live;
  const auto skills = score::main_skills(s, preset, db);
  std::vector<std::string> warnings, unscored, converts, notes;
  std::map<std::string, std::uint32_t> unused;

  // The engine's own check decides when the adapter read it against this snapshot's character state.
  std::optional<Line> unmet_line;
  if (candidate.requirements) {
    const auto why = requirements::unmet(candidate, raw);
    const std::optional<bool> engine = requirements::engine_met(candidate, raw);
    if (engine && *engine != why.empty())
      warnings.push_back(std::string("requirements: the model says ") + (why.empty() ? "met" : "not met") +
                         ", the engine disagrees");
    if (engine ? !*engine : !why.empty()) {
      std::string t = "! can't equip yet";
      for (std::size_t i = 0; i < why.size(); ++i) t += (i ? ", " : ": ") + requirements::text(why[i]);
      unmet_line = Line{Tone::Worse, t};
    }
  }
  // shortcut: weapon-gated skills not re-checked when hand classes change — revisit with a weapon-swap build.
  auto hand_class = [&](const snap::Item* it) {
    const arz::Record* r = it && db ? db->record(it->record) : nullptr;
    return r ? r->cls : std::string{};
  };
  auto gated_skill = [&]() -> std::string {
    if (db)
      for (const auto& sk : s.skills)
        if (sk.level > 0 && sk.devotion_max_level == 0)
          if (const arz::Record* r = db->record(sk.record); r && r->cls != "Skill_Mastery" && fields::weapon_gated(*r))
            return labels.skill(sk.record);
    return {};
  };

  const snap::Item cand_base = components::strip(candidate, db, notes);
  std::vector<Slot> slots;
  for (const Plan& plan : plans(s, candidate, item_type, db)) {
    const int loc = plan.location;
    Slot sl;
    sl.title = plan.title;
    const snap::Item* old = at(s, loc);
    if (loc <= 1 || plan.also_off >= 0) {
      const snap::Item* off = plan.also_off >= 0 ? at(s, plan.also_off) : nullptr;
      const bool changes = hand_class(old) != hand_class(&candidate) || off;
      if (const std::string g = changes ? gated_skill() : std::string{}; !g.empty())
        add_unique(warnings, "weapon type changes: " + g + " needs a weapon type (not re-checked)");
      if (weapon(at(s, 1), db) || (loc == 1 && weapon(&candidate, db)))
        add_unique(warnings, "dual wield: the L Hand weapon's base damage is not modeled");
    }
    auto take = [&](model::Prediction& p) {
      for (auto& w : p.warnings) add_unique(warnings, std::move(w));
      for (const auto& u : p.unused)
        if (u.after > u.before) unused[u.record] = std::max(unused[u.record], u.after - u.before);
      for (const auto& x : p.inactive) {
        auto read = [&](const std::string& r) {
          return !r.empty() && std::any_of(skills.begin(), skills.end(), [&](const score::Skill& k) { return k.record == r; });
        };
        if (x.timed || read(x.record) || read(x.parent)) continue;
        add_unique(unscored, labels.skill(x.record) + " level " + std::to_string(x.before) + "->" + std::to_string(x.after) + " (not active now)");
      }
    };
    auto equip = [&](const snap::Snapshot& from, const snap::Item& it) {
      model::Prediction p = model::swap(from, it, loc, db);
      take(p);
      if (plan.also_off >= 0 && at(p.after, plan.also_off)) {
        model::Prediction off = take_off(p.after, plan.also_off, db);
        take(off);
        p.after = std::move(off.after);
        p.sets.insert(p.sets.end(), off.sets.begin(), off.sets.end());
        p.granted.insert(p.granted.end(), off.granted.begin(), off.granted.end());
      }
      return p;
    };

    std::optional<model::Prediction> stripped;
    if (old) {
      const snap::Item old_base = components::strip(*old, db, notes);
      stripped = model::swap(s, old_base, loc, db);
      take(*stripped);
    }
    const snap::Snapshot& from = stripped ? stripped->after : s;
    model::Prediction p1 = equip(from, cand_base);
    model::Prediction pa = equip(s, candidate);
    std::vector<std::string> broke_notes;
    const auto broken = take_off_broken(s, pa.after, loc, db, broke_notes);
    for (auto& n : broke_notes) add_unique(warnings, std::move(n));
    if (!broken.empty()) sl.itself = score::compare(from, p1.after, skills, preset, db);
    for (const auto& b : broken) {
      const snap::Item* worn = at(s, b.location);
      std::string t = "  ! " + (worn ? labels.item(*worn) : labels.item(b.record)) + ": needs ";
      for (std::size_t i = 0; i < b.why.size(); ++i) t += (i ? ", " : "") + requirements::text(b.why[i]);
      sl.broken.push_back({Tone::Worse, t});
      model::Prediction off = take_off(p1.after, b.location, db);
      take(off);
      p1.after = std::move(off.after);
    }
    for (const auto& set : p1.sets) {
      const std::string name = labels.set(set.set) + " " + std::to_string(set.before) + "->" + std::to_string(set.after);
      if (!set.modeled) {
        add_unique(unscored, "set " + name);
        continue;
      }
      sl.sets.push_back({set.after > set.before ? Tone::Better : Tone::Worse, "  set " + name + " (scored)"});
      for (const auto& f : set.unmodeled) add_unique(unscored, "set bonus " + pretty(f));
    }
    sl.base = score::compare(from, p1.after, skills, preset, db);
    auto valued = [&](const std::string& record) {
      return sl.base.procs.contains(record) ||
             std::any_of(sl.base.unscored.begin(), sl.base.unscored.end(), [&](const auto& u) { return u.record == record; });
    };
    for (const auto& g : p1.granted)
      if (!g.scored && !valued(g.record))
        add_unique(unscored, (g.gained ? "" : "loses ") + labels.skill(g.record) + " (" + unscored_reason(g.why) + ")");
    for (const auto& set : p1.sets) {
      if (!set.modeled) continue;
      if (!set.skill_lost.empty() && !valued(set.skill_lost)) add_unique(unscored, "loses " + labels.skill(set.skill_lost) + " (set skill)");
      if (!set.skill_gained.empty() && !valued(set.skill_gained)) add_unique(unscored, labels.skill(set.skill_gained) + " (set skill)");
    }
    for (auto& n : sl.base.notes) add_unique(warnings, std::move(n));
    for (const auto& u : sl.base.unscored) {
      add_unique(unscored, labels.skill(u.record) + " (rate unknown)");
      if (log) log->push_back(sl.title + " unscored " + u.record + ": " + u.why);
    }

    if (old && (old->relic || old->augment)) {
      std::vector<std::string> fit, misfit;
      bool relic = false, augment = false;
      for (const auto* c : {&old->relic, &old->augment}) {
        if (!*c) continue;
        const bool ok = db && components::fits((*c)->record, candidate.record, item_type, *db);
        (ok ? fit : misfit).push_back(labels.item((*c)->record));
        (c == &old->relic ? relic : augment) = ok;
      }
      auto join = [](const std::vector<std::string>& v) {
        std::string t;
        for (const auto& x : v) t += (t.empty() ? "" : ", ") + x;
        return t;
      };
      sl.moved_names = join(fit);
      sl.misfit = join(misfit);
      if (relic || augment) {
        model::Prediction p2 = equip(s, components::attach(cand_base, *old, relic, augment, db, notes));
        std::vector<std::string> broke_notes2;
        take_off_broken(s, p2.after, loc, db, broke_notes2);
        for (auto& n : broke_notes2) add_unique(warnings, std::move(n));
        sl.moved = score::compare(s, p2.after, skills, preset, db);
        for (auto& n : sl.moved->notes) add_unique(warnings, std::move(n));
      }
    }

    if (detail || !preset.conversion_warn.empty()) {
      if (detail) {
        const score::BuffView vb = score::buff_view(s, skills, preset, db), va = score::buff_view(pa.after, skills, preset, db);
        // The sheet lines compare the items as they are, so their points come from that comparison, not from
        // base (components stripped from both).
        if ((old && (old->relic || old->augment)) || candidate.relic || candidate.augment)
          sl.hovered = score::compare(s, pa.after, skills, preset, db);
        sl.detail = changes(vb.sheet, va.sheet, pa, labels, sl.hovered ? &*sl.hovered : &sl.base);
        sl.buffs = buff_lines(vb, va, labels, screen);
      }
      for (const auto& c : score::conversion_warnings(s, pa.after, preset, chosen.match_damage, db))
        add_unique(converts, "! converts away: " + (c.skill.empty() ? "" : labels.skill(c.skill) + " ") +
                                 num(c.percent, 0) + "% " + score::conversion_label(c.from) + " -> " +
                                 score::conversion_label(c.to));
    }
    if (log) {
      log->push_back(sl.title + " base: " + num(sl.base.percent, 2) + "% (" + terms(sl.base) + ")");
      log->push_back("  before: " + sl.base.before);
      log->push_back("  after:  " + sl.base.after);
      if (sl.hovered) log->push_back(sl.title + " as hovered: " + num(sl.hovered->percent, 2) + "% (" + terms(*sl.hovered) + ")");
      if (sl.itself) log->push_back(sl.title + " item itself: " + num(sl.itself->percent, 2) + "% (" + terms(*sl.itself) + ")");
      if (sl.moved) {
        log->push_back(sl.title + " moved: " + num(sl.moved->percent, 2) + "% (" + terms(*sl.moved) + ")");
        log->push_back("  before: " + sl.moved->before);
        log->push_back("  after:  " + sl.moved->after);
      }
    }
    if (scored) {
      Scored x{loc, plan.also_off, sl.base.percent, {}, {}};
      if (sl.itself) x.itself = sl.itself->percent;
      if (sl.moved) x.moved = sl.moved->percent;
      scored->push_back(x);
    }
    slots.push_back(std::move(sl));
  }
  for (auto& n : notes) add_unique(warnings, std::move(n));
  if (log) {
    for (const auto& w : warnings) log->push_back("warning: " + w);
    for (const auto& c : converts) log->push_back(c);
  }

  std::size_t best = slots.size();
  if (slots.size() == 2 && std::abs(slots[0].base.percent - slots[1].base.percent) >= preset.negligible_percent)
    best = slots[0].base.percent > slots[1].base.percent ? 0 : 1;

  std::string heading = "GD Compare";
  // The game's fonts have no other dash (tooltip.h).
  if (!chosen.name.empty()) heading += " - " + chosen.name;
  if (stage) heading += " - stage " + stage->name;
  out.push_back({Tone::Heading, heading});
  if (unmet_line) out.push_back(*unmet_line);
  auto short_change = [&](const score::Result& r) {
    std::string t = change(r.percent, preset.negligible_percent).text;
    if (t.ends_with(" ^") || t.ends_with(" v")) t.resize(t.size() - 2);
    return t;
  };
  for (std::size_t i = 0; i < slots.size(); ++i) {
    const Slot& sl = slots[i];
    Line l = change(sl.base.percent, preset.negligible_percent);
    l.text = sl.title + ": " + l.text + (i == best ? " (best)" : "") + (unmet_line ? " (if equipped)" : "");
    out.push_back(std::move(l));
    if (sl.itself) {
      const std::size_t n = sl.broken.size();
      out.push_back({Tone::Worse, "  item itself: " + short_change(*sl.itself) + ", but it unequips " + std::to_string(n) +
                                      (n == 1 ? " item:" : " items:")});
      out.insert(out.end(), sl.broken.begin(), sl.broken.end());
    }
    if (sl.moved) {
      Line m = change(sl.moved->percent, preset.negligible_percent);
      m.text = "  with your " + sl.moved_names + ": " + m.text;
      out.push_back(std::move(m));
    }
    if (!sl.misfit.empty()) out.push_back({Tone::Detail, "  your " + sl.misfit + " does not fit this item"});
    if (sl.base.pets) out.push_back({Tone::Detail, "  pet damage: experimental estimate"});
    out.insert(out.end(), sl.sets.begin(), sl.sets.end());
    if (!detail) continue;
    out.push_back({Tone::Detail, "  " + terms(sl.base)});
    for (auto& t : top(sl.base, labels, "  ", screen)) out.push_back({Tone::Detail, std::move(t)});
    if (sl.moved) {
      out.push_back({Tone::Detail, "  with your " + sl.moved_names + ": " + terms(*sl.moved)});
      for (auto& t : top(*sl.moved, labels, "    ", screen)) out.push_back({Tone::Detail, std::move(t)});
    }
    const bool differs = sl.hovered && short_change(*sl.hovered) != short_change(sl.base);
    out.push_back({Tone::Detail, differs ? "  sheet as hovered, components included: " + short_change(*sl.hovered) + " [points of it]:"
                                         : "  sheet as hovered [points of total]:"});
    for (const auto& d : sl.detail) out.push_back({d.tone, "    " + d.text});
    for (auto& r : rr_lines(sl.base, labels)) out.push_back({r.tone, "    " + r.text});
    for (const auto& d : sl.buffs) out.push_back({d.tone, "  " + d.text});
  }
  for (auto& l : target_set(s, candidate, chosen, db, labels)) out.push_back(std::move(l));

  for (const auto& c : converts) out.push_back({Tone::Worse, c});
  if (!unscored.empty())
    for (auto& l : pack("+? unscored: ", "  ", unscored, screen)) out.push_back({Tone::Detail, std::move(l)});
  if (!warnings.empty() && !detail) out.push_back({Tone::Warning, "! model inaccurate (Shift: why)"});
  if (!warnings.empty() && detail) {
    out.push_back({Tone::Warning, "! model inaccurate:"});
    for (std::size_t i = 0; i < warnings.size() && i < kMaxWarnings; ++i) out.push_back({Tone::Detail, "  " + warnings[i]});
    if (warnings.size() > kMaxWarnings)
      out.push_back({Tone::Detail, "  ... " + std::to_string(warnings.size() - kMaxWarnings) + " more in GDCompare.log"});
  }
  unused_lines(out, unused, detail, labels, screen);
  return out;
}

}  // namespace gdc::tooltip
