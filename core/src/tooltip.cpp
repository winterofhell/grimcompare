#include "gdc/tooltip.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <optional>

#include "gdc/attribute_names.h"
#include "gdc/components.h"
#include "gdc/requirements.h"

namespace gdc::tooltip {
namespace {

constexpr std::size_t kMaxWarnings = 6;  // Shift: warnings listed one per line
constexpr std::size_t kPerLine = 3;      // "+? unscored" entries per line

// Rounded to `decimals`; "-0" printed as "0".
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

// One compared value. Changes count only when they show at the displayed precision.
struct Stat {
  std::string name;
  double before, after;
  int decimals;
  const char* unit;
  bool changed() const { return num(before, decimals) != num(after, decimals); }
  double delta() const { return after - before; }
};

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

void sheet_lines(std::vector<Line>& out, const model::Sheet& b, const model::Sheet& a, const Labels& labels) {
  const Stat stats[] = {
      {"Physique", b.physique, a.physique, 0, ""},
      {"Cunning", b.cunning, a.cunning, 0, ""},
      {"Spirit", b.spirit, a.spirit, 0, ""},
      {"Life", b.life, a.life, 0, ""},
      {"Energy", b.energy, a.energy, 0, ""},
      {"Life regen", b.life_regen, a.life_regen, 1, "/s"},
      {"Energy regen", b.energy_regen, a.energy_regen, 1, "/s"},
      {"OA", b.oa, a.oa, 0, ""},
      {"DA", b.da, a.da, 0, ""},
      {"Armor", b.armor, a.armor, 0, ""},
      {"Run speed", b.run_speed * 100, a.run_speed * 100, 0, "%"},
  };
  bool any = false;
  for (const auto& s : stats) {
    if (!s.changed()) continue;
    any = true;
    out.push_back({s.delta() > 0 ? Tone::Better : Tone::Worse,
                   s.name + " " + num(s.before, s.decimals) + s.unit + " -> " + num(s.after, s.decimals) + s.unit +
                       " (" + signed_num(s.delta(), s.decimals) + ")"});
  }
  for (int t : resist_types(b, a)) {
    // A side without the row has 0 of it under the other side's max (the cap does not change with it).
    const auto* pb = find(b.resists, t);
    const auto* pa = find(a.resists, t);
    const model::Resist x = pb ? *pb : model::Resist{t, 0, 0, 0, pa->max};
    const model::Resist y = pa ? *pa : model::Resist{t, 0, 0, 0, pb->max};
    const Stat shown{labels.resist(t), x.shown, y.shown, 0, "%"};
    const Stat raw{"", x.uncapped, y.uncapped, 0, "%"};
    const Stat max{"", x.max, y.max, 0, "%"};
    if (!shown.changed() && !raw.changed() && !max.changed()) continue;
    any = true;
    std::string text = shown.name + " " + num(x.shown, 0) + "% -> " + num(y.shown, 0) + "%";
    // Over the cap: what the sheet hides (uncapped total vs max).
    if (raw.changed() && (x.uncapped > x.max || y.uncapped > y.max))
      text += " (uncapped " + num(x.uncapped, 0) + " -> " + num(y.uncapped, 0) + ")";
    if (max.changed()) text += " (max " + num(x.max, 0) + " -> " + num(y.max, 0) + ")";
    const double d = shown.changed() ? shown.delta() : raw.delta();
    out.push_back({d > 0 ? Tone::Better : d < 0 ? Tone::Worse : Tone::Plain, text});
  }
  if (!any) out.push_back({Tone::Plain, "No sheet change"});
}

// "top: Block +32.1, Life -2.0, Bloody Pox +0.4": the three largest parts of the score change, in points of
// the total (score::Result::contributions), so any total can be checked at a glance.
std::string top(const score::Result& r, const Labels& labels) {
  std::string out;
  for (std::size_t i = 0; i < r.contributions.size() && i < 3; ++i) {
    const auto& c = r.contributions[i];
    out += (out.empty() ? "top: " : ", ") + (c.skill.empty() ? c.label : labels.skill(c.skill)) + " " + signed_num(c.points, 1);
  }
  return out;
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

// "Rumor: -30 Poison (RR 1), up 53% derived" (MECHANICS §1 types; every enemy type the same: "all").
std::string rr_text(const effects::Reduction& r, const Labels& labels) {
  std::string t = labels.skill(r.record) + ": -";
  bool same = r.amount.size() >= 9;
  for (const auto& [_, v] : r.amount) same = same && v == r.amount.begin()->second;
  if (same) {
    t += num(r.amount.begin()->second, 0) + " all";
  } else {
    std::string types;
    for (const auto& [type, v] : r.amount)
      types += (types.empty() ? "" : ", ") + num(v, 0) + " " + labels.resist(type);
    t += types;
  }
  static constexpr const char* kOrigin[] = {"derived", "preset", "default !"};
  return t + (r.category == 2 ? "%" : "") + " (RR " + std::to_string(r.category) + "), up " + num(r.uptime * 100, 0) +
         "% " + kOrigin[static_cast<int>(r.origin)];
}

// Preset target_sets: "target set Worldkeeper's Garden: 1/4 worn" and the missing pieces, for a hovered
// member (set record setMembers, DB). Pieces in the snapshot's inventory are marked "(in bag)".
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

struct Slot {
  std::string title;
  score::Result base;
  std::optional<score::Result> moved;
  std::string moved_names, misfit;  // "Frozen Heart, Exile's Riftstorm"; what does not fit
  std::vector<Line> sets;           // set piece count changes of line 1, already in its score
  std::vector<Line> broken;         // items the swap leaves unable to meet their requirements
  std::vector<Line> detail;
};

// `s` with the item at `location` taken off: an empty item swapped in, then dropped from the list.
model::Prediction take_off(const snap::Snapshot& s, int location, const arz::Database* db) {
  snap::Item none;
  none.stats.emplace();
  model::Prediction p = model::swap(s, none, location, db, model::Mode::Expected);
  auto& eq = p.after.equipped;
  eq.erase(std::remove_if(eq.begin(), eq.end(), [&](const snap::Item& x) { return x.location == location && x.record.empty(); }),
           eq.end());
  return p;
}

bool met_before(const snap::Item& e, const snap::Snapshot& s) {
  if (e.requirements && e.requirements->met) return *e.requirements->met;  // the engine's own check
  return requirements::unmet(e, s).empty();
}

// Items other than the one at `location` that met their requirements in `before` and do not in `after`
// (PROMPT: unequippable = scored as removed). Each is taken off `after`, which can break another; at most
// kMaxBroken rounds.
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

Line change(double percent, double negligible) {
  if (!(std::abs(percent) >= negligible) || percent == 0) return {Tone::Plain, "~0%"};
  if (percent > 0) return {Tone::Better, "+" + num(percent, 1) + "% ^"};
  return {Tone::Worse, num(percent, 1) + "% v"};
}

std::vector<Line> changes(const model::Sheet& before, const model::Sheet& after, const model::Prediction& p,
                          const Labels& labels) {
  std::vector<Line> out;
  sheet_lines(out, before, after, labels);
  for (const auto& s : p.skills) {
    std::string text = labels.skill(s.record) + " level " + std::to_string(s.before) + " -> " + std::to_string(s.after);
    for (const auto& x : p.inactive)
      if (x.record == s.record && x.uptime > 0) text += " (buff, up " + num(x.uptime * 100, 0) + "%)";
    out.push_back({s.after > s.before ? Tone::Better : Tone::Worse, text});
  }
  for (const auto& s : p.sets)
    out.push_back({s.after > s.before ? Tone::Better : Tone::Worse,
                   "Set " + labels.set(s.set) + " " + std::to_string(s.before) + "->" + std::to_string(s.after)});
  return out;
}

std::vector<int> locations_for(int item_type) {
  switch (item_type) {
    case 1: return {7};      // Head
    case 2: return {2};      // Amulet -> Neck
    case 3: return {8};      // Chest (location 14 "Clothing" also takes type 3: not a player slot)
    case 4: return {11};     // Legs
    case 5: return {12};     // Feet
    case 6: return {4, 5};   // Ring -> both fingers
    case 7: return {10};     // Hands
    case 8: return {1};      // WeaponArmor (shield, caster off-hand) -> L Hand
    // 9 Weapon -> R Hand: not compared (the game compares weapons itself); weapons still count in the model.
    case 11: return {13};    // Artifact
    case 13: return {6};     // Waist
    case 14: return {9};     // Shoulders
    case 15: return {3};     // Medal
    default: return {};
  }
}

std::vector<Line> compare(const snap::Snapshot& s, const snap::Item& candidate, int item_type, bool detail,
                          const arz::Database* db, const score::Preset& chosen, const Labels& labels,
                          std::vector<std::string>* log) {
  std::vector<Line> out;
  const score::Stage* stage = score::stage(chosen, s);
  const score::Settings& preset = stage ? stage->settings : chosen;
  for (const auto& e : s.equipped)
    if (e.id != 0 && e.id == candidate.id) return out;
  const auto locations = locations_for(item_type);
  if (locations.empty()) return out;

  // A shield / off-hand next to a two-handed weapon (record class *2h: WeaponMelee_Sword2h, Axe2h, Mace2h,
  // Spear2h, WeaponHunting_Ranged2h in 1.3.1.1) would unequip it: weapon swaps are not compared.
  if (item_type == 8)
    if (const snap::Item* rh = at(s, 0); rh && db)
      if (const arz::Record* r = db->record(rh->record); r && r->cls.ends_with("2h"))
        return {{Tone::Heading, "GD Compare"}, {Tone::Detail, "not compared: two-handed weapon in R Hand"}};

  const auto skills = score::main_skills(s, preset, db);
  std::vector<std::string> warnings, unscored, converts, unused;
  std::vector<std::string> notes;

  // The candidate's requirements right now; the engine's own check decides when the adapter read it.
  std::optional<Line> unmet_line;
  if (candidate.requirements) {
    const auto why = requirements::unmet(candidate, s);
    const std::optional<bool>& engine = candidate.requirements->met;
    if (engine && *engine != why.empty())
      warnings.push_back(std::string("requirements: the model says ") + (why.empty() ? "met" : "not met") +
                         ", the engine disagrees");
    if (engine ? !*engine : !why.empty()) {
      std::string t = "! requirements not met";
      for (std::size_t i = 0; i < why.size(); ++i) t += (i ? ", " : ": ") + requirements::text(why[i]);
      unmet_line = Line{Tone::Worse, t};
    }
  }
  const snap::Item cand_base = components::strip(candidate, db, notes);
  std::vector<Slot> slots;
  for (int loc : locations) {
    Slot sl;
    sl.title = "vs " + std::string(names::equip_location(loc));
    if (loc == 4 || loc == 5) sl.title += loc == 4 ? " 1" : " 2";
    const snap::Item* old = at(s, loc);
    if (!old) sl.title += " (empty)";
    if (old && loc == 1 && db)
      if (const arz::Record* r = db->record(old->record); r && r->cls.starts_with("Weapon") && !r->cls.starts_with("WeaponArmor"))
        add_unique(warnings, "replaces a weapon in L Hand: dual wield is not modeled (its weapon damage is not scored)");
    auto take = [&](model::Prediction& p) {
      for (auto& w : p.warnings) add_unique(warnings, std::move(w));
      for (const auto& u : p.unused)
        if (u.after > u.before) add_unique(unused, labels.skill(u.record) + " +" + std::to_string(u.after - u.before));
      for (const auto& x : p.inactive) {
        // The score reads its main skills (and their modifiers) from their records at the new level.
        auto scored = [&](const std::string& r) {
          return !r.empty() && std::any_of(skills.begin(), skills.end(), [&](const score::Skill& k) { return k.record == r; });
        };
        if (x.uptime > 0 || scored(x.record) || scored(x.parent)) continue;
        add_unique(unscored, labels.skill(x.record) + " " + std::to_string(x.before) + "->" + std::to_string(x.after) + " (inactive)");
      }
    };

    // Line 1: base vs base.
    std::optional<model::Prediction> stripped;
    if (old) {
      const snap::Item old_base = components::strip(*old, db, notes);
      stripped = model::swap(s, old_base, loc, db, model::Mode::Expected);
      take(*stripped);
    }
    const snap::Snapshot& from = stripped ? stripped->after : s;
    model::Prediction p1 = model::swap(from, cand_base, loc, db, model::Mode::Expected);
    take(p1);
    // Requirement breakage from the swap as hovered (components included); the same items come off line 1.
    model::Prediction pa = model::swap(s, candidate, loc, db, model::Mode::Expected);
    take(pa);
    std::vector<std::string> broke_notes;
    for (const auto& b : take_off_broken(s, pa.after, loc, db, broke_notes)) {
      std::string t = "  ! unequips " + labels.item(b.record) + ": needs ";
      for (std::size_t i = 0; i < b.why.size(); ++i) t += (i ? ", " : "") + requirements::text(b.why[i]);
      sl.broken.push_back({Tone::Worse, t + " (scored as removed)"});
      model::Prediction off = take_off(p1.after, b.location, db);
      take(off);
      p1.after = std::move(off.after);
    }
    for (auto& n : broke_notes) add_unique(warnings, std::move(n));
    for (const auto& g : p1.granted) {
      if (g.scored) continue;
      std::string why = g.why;
      if (why.starts_with("Skill_")) why = why.substr(6);  // the skill's class
      add_unique(unscored, (g.gained ? "granted skill " : "loses granted skill ") + labels.skill(g.record) + " (" + why + ")");
    }
    for (const auto& set : p1.sets) {
      const std::string name = labels.set(set.set) + " " + std::to_string(set.before) + "->" + std::to_string(set.after);
      if (!set.modeled) {
        add_unique(unscored, "set " + name);
        continue;
      }
      sl.sets.push_back({set.after > set.before ? Tone::Better : Tone::Worse, "  set " + name + " (scored)"});
      if (!set.skill_lost.empty()) add_unique(unscored, "loses set skill " + labels.skill(set.skill_lost));
      if (!set.skill_gained.empty()) add_unique(unscored, "set skill " + labels.skill(set.skill_gained));
      for (const auto& f : set.unmodeled) add_unique(unscored, "set " + f);
    }
    sl.base = score::compare(from, p1.after, skills, preset, db);
    for (auto& n : sl.base.notes) add_unique(warnings, std::move(n));
    for (const auto& u : sl.base.unscored) {
      add_unique(unscored, labels.skill(u.record) + " (uptime ?)");
      if (log) log->push_back(sl.title + " unscored " + u.record + ": " + u.why);
    }

    // Line 2: your component / augment moved onto the candidate.
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
        model::Prediction p2 = model::swap(s, components::attach(cand_base, *old, relic, augment, db, notes), loc, db, model::Mode::Expected);
        take(p2);
        std::vector<std::string> broke_notes2;
        take_off_broken(s, p2.after, loc, db, broke_notes2);
        for (auto& n : broke_notes2) add_unique(warnings, std::move(n));
        sl.moved = score::compare(s, p2.after, skills, preset, db);
        for (auto& n : sl.moved->notes) add_unique(warnings, std::move(n));
      }
    }

    if (detail || !preset.conversion_warn.empty()) {
      if (detail) sl.detail = changes(model::sheet(s), model::sheet(pa.after), pa, labels);
      // The item as hovered (with its components): conversions away from the preset's damage types.
      for (const auto& c : score::conversion_warnings(s, pa.after, preset, chosen.match_damage, db))
        add_unique(converts, "! converts away: " + (c.skill.empty() ? "" : labels.skill(c.skill) + " ") +
                                 num(c.percent, 0) + "% " + score::conversion_label(c.from) + " -> " +
                                 score::conversion_label(c.to));
    }
    if (log) {
      log->push_back(sl.title + " base: " + num(sl.base.percent, 2) + "% (" + terms(sl.base) + ")");
      log->push_back("  before: " + sl.base.before);
      log->push_back("  after:  " + sl.base.after);
      if (sl.moved) {
        log->push_back(sl.title + " moved: " + num(sl.moved->percent, 2) + "% (" + terms(*sl.moved) + ")");
        log->push_back("  before: " + sl.moved->before);
        log->push_back("  after:  " + sl.moved->after);
      }
    }
    slots.push_back(std::move(sl));
  }
  for (auto& n : notes) add_unique(warnings, std::move(n));
  if (log) {
    for (const auto& w : warnings) log->push_back("warning: " + w);
    for (const auto& c : converts) log->push_back(c);
  }

  // The better finger by line 1, if the difference shows.
  std::size_t best = slots.size();
  if (slots.size() == 2 && std::abs(slots[0].base.percent - slots[1].base.percent) >= preset.negligible_percent)
    best = slots[0].base.percent > slots[1].base.percent ? 0 : 1;

  std::string heading = "GD Compare";
  // " - ", not " | ": the game font's glyph coverage is unverified (tooltip.h).
  if (!chosen.name.empty()) heading += " - " + chosen.name;
  if (stage) heading += " - stage " + stage->name;
  out.push_back({Tone::Heading, heading});
  if (unmet_line) out.push_back(*unmet_line);
  for (std::size_t i = 0; i < slots.size(); ++i) {
    const Slot& sl = slots[i];
    Line l = change(sl.base.percent, preset.negligible_percent);
    l.text = sl.title + ": " + l.text + (i == best ? " (best)" : "");
    out.push_back(std::move(l));
    if (detail) {
      out.push_back({Tone::Detail, "  " + terms(sl.base)});
      if (const std::string t = top(sl.base, labels); !t.empty()) out.push_back({Tone::Detail, "  " + t});
    }
    out.insert(out.end(), sl.sets.begin(), sl.sets.end());
    out.insert(out.end(), sl.broken.begin(), sl.broken.end());
    if (sl.moved) {
      Line m = change(sl.moved->percent, preset.negligible_percent);
      m.text = "  your " + sl.moved_names + " moved: " + m.text;
      out.push_back(std::move(m));
      if (detail) {
        out.push_back({Tone::Detail, "    " + terms(*sl.moved)});
        if (const std::string t = top(*sl.moved, labels); !t.empty()) out.push_back({Tone::Detail, "    " + t});
      }
    }
    if (!sl.misfit.empty()) out.push_back({Tone::Detail, "  your " + sl.misfit + ": does not fit"});
    if (detail) {
      for (const auto& r : sl.base.rr_lost) out.push_back({Tone::Worse, "  loses " + rr_text(r, labels)});
      for (const auto& r : sl.base.rr_gained) out.push_back({Tone::Better, "  " + rr_text(r, labels)});
      for (const auto& c : sl.base.other_changes) out.push_back({Tone::Detail, "  Other: " + c});
      out.push_back({Tone::Detail, "  as is:"});
      for (const auto& d : sl.detail) out.push_back({d.tone, "  " + d.text});
    }
  }
  for (auto& l : target_set(s, candidate, chosen, db, labels)) out.push_back(std::move(l));
  for (const auto& c : converts) out.push_back({Tone::Worse, c});
  for (std::size_t i = 0; i < unscored.size(); i += kPerLine) {
    std::string text = i == 0 ? "+? unscored: " : "  ";
    for (std::size_t k = i; k < std::min(unscored.size(), i + kPerLine); ++k) text += (k > i ? ", " : "") + unscored[k];
    out.push_back({Tone::Detail, text});
  }
  // +skills to skills without points: grey, score 0. Shift lists them all, else the first line's worth.
  if (!unused.empty()) {
    const std::size_t shown = detail ? unused.size() : std::min(unused.size(), kPerLine);
    for (std::size_t i = 0; i < shown; i += kPerLine) {
      std::string text = i == 0 ? "(unused, 0 points): " : "  ";
      for (std::size_t k = i; k < std::min(shown, i + kPerLine); ++k) text += (k > i ? ", " : "") + unused[k];
      out.push_back({Tone::Detail, text});
    }
    if (shown < unused.size()) out.push_back({Tone::Detail, "  ... " + std::to_string(unused.size() - shown) + " more (Shift)"});
  }
  if (!warnings.empty() && !detail) out.push_back({Tone::Warning, "! model inaccurate (Shift: why)"});
  if (!warnings.empty() && detail) {
    out.push_back({Tone::Warning, "! model inaccurate:"});
    for (std::size_t i = 0; i < warnings.size() && i < kMaxWarnings; ++i) out.push_back({Tone::Detail, warnings[i]});
    if (warnings.size() > kMaxWarnings)
      out.push_back({Tone::Detail, "... " + std::to_string(warnings.size() - kMaxWarnings) + " more in GDCompare.log"});
  }
  return out;
}

}  // namespace gdc::tooltip
