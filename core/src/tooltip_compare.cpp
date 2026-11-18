#include <algorithm>
#include <cmath>
#include <map>
#include <optional>

#include "gdc/components.h"
#include "gdc/db_fields.h"
#include "gdc/requirements.h"

#include "tooltip_internal.h"

namespace gdc::tooltip {
namespace {

constexpr std::size_t kMaxWarnings = 6;

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

// Inputs shared by every slot of one compare.
struct Context {
  const snap::Snapshot& s;
  const snap::Item& candidate;
  int item_type;
  bool detail;
  const arz::Database* db;
  const score::Preset& chosen;
  const score::Settings& preset;
  const std::vector<score::Skill>& skills;
  const Labels& labels;
  const Screen& screen;
  std::vector<std::string>* log;
};

// What the slots gather for the shared closing lines.
struct Found {
  snap::Item candidate_base;  // components stripped
  score::Memo memo;           // compares with s before
  std::optional<score::BuffView> view;  // s's buffs, the same for every slot
  std::vector<std::string> warnings, unscored, converts, notes;
  std::map<std::string, std::uint32_t> unused;
};

void add_unique(std::vector<std::string>& v, std::string s) {
  if (std::find(v.begin(), v.end(), s) == v.end()) v.push_back(std::move(s));
}

std::string unscored_reason(const std::string& why) {
  if (why.empty() || why.starts_with("Skill")) return "not modeled";
  if (why.starts_with("level ")) return "level unknown";
  return why;
}

// Live buff mode: active buffs count as always up.
score::Settings live_settings(score::Settings live, const snap::Snapshot& raw, const arz::Database* db) {
  if (live.live_buffs && db) {
    live.recast_buffs = false;
    for (const auto& sk : raw.skills)
      if (sk.buff_active && model::buff_timing(sk, *db)) live.uptime[sk.record] = *sk.buff_active;
  }
  return live;
}

// The engine's own check wins when the adapter read one.
std::optional<Line> unmet_line(const snap::Item& candidate, const snap::Snapshot& raw, std::vector<std::string>& warnings) {
  if (!candidate.requirements) return std::nullopt;
  const auto why = requirements::unmet(candidate, raw);
  const std::optional<bool> engine = requirements::engine_met(candidate, raw);
  if (engine && *engine != why.empty())
    warnings.push_back(std::string("requirements: the model says ") + (why.empty() ? "met" : "not met") +
                       ", the engine disagrees");
  if (engine ? *engine : why.empty()) return std::nullopt;
  std::string t = "! can't equip yet";
  for (std::size_t i = 0; i < why.size(); ++i) t += (i ? ", " : ": ") + requirements::text(why[i]);
  return Line{Tone::Worse, t};
}

std::string hand_class(const snap::Item* it, const arz::Database* db) {
  const arz::Record* r = it && db ? db->record(it->record) : nullptr;
  return r ? r->cls : std::string{};
}

std::string gated_skill(const Context& ctx) {
  if (ctx.db)
    for (const auto& sk : ctx.s.skills)
      if (sk.level > 0 && sk.devotion_max_level == 0)
        if (const arz::Record* r = ctx.db->record(sk.record); r && r->cls != "Skill_Mastery" && fields::weapon_gated(*r))
          return ctx.labels.skill(sk.record);
  return {};
}

// shortcut: weapon-gated skills not re-checked when hand classes change — revisit with a weapon-swap build.
void hand_warnings(const Context& ctx, Found& found, const Plan& plan, const snap::Item* old) {
  const int loc = plan.location;
  if (loc > 1 && plan.also_off < 0) return;
  const snap::Item* off = plan.also_off >= 0 ? at(ctx.s, plan.also_off) : nullptr;
  const bool changes = hand_class(old, ctx.db) != hand_class(&ctx.candidate, ctx.db) || off;
  if (const std::string g = changes ? gated_skill(ctx) : std::string{}; !g.empty())
    add_unique(found.warnings, "weapon type changes: " + g + " needs a weapon type (not re-checked)");
  if (weapon(at(ctx.s, 1), ctx.db) || (loc == 1 && weapon(&ctx.candidate, ctx.db)))
    add_unique(found.warnings, "dual wield: the L Hand weapon's base damage is not modeled");
}

// A prediction's warnings, unused skill points, inactive skills.
void record(const Context& ctx, Found& found, model::Prediction& p) {
  for (auto& w : p.warnings) add_unique(found.warnings, std::move(w));
  for (const auto& u : p.unused)
    if (u.after > u.before) found.unused[u.record] = std::max(found.unused[u.record], u.after - u.before);
  auto main = [&](const std::string& r) {
    return !r.empty() && std::any_of(ctx.skills.begin(), ctx.skills.end(), [&](const score::Skill& k) { return k.record == r; });
  };
  for (const auto& x : p.inactive) {
    if (x.timed || main(x.record) || main(x.parent)) continue;
    add_unique(found.unscored, ctx.labels.skill(x.record) + " level " + std::to_string(x.before) + "->" +
                                   std::to_string(x.after) + " (not active now)");
  }
}

model::Prediction equip(const Context& ctx, Found& found, const Plan& plan, const snap::Snapshot& from, const snap::Item& it) {
  model::Prediction p = model::swap(from, it, plan.location, ctx.db);
  record(ctx, found, p);
  if (plan.also_off >= 0 && at(p.after, plan.also_off)) {
    model::Prediction off = take_off(p.after, plan.also_off, ctx.db);
    record(ctx, found, off);
    p.after = std::move(off.after);
    p.sets.insert(p.sets.end(), off.sets.begin(), off.sets.end());
    p.granted.insert(p.granted.end(), off.granted.begin(), off.granted.end());
  }
  return p;
}

// No components: swapping it for itself changes nothing.
bool bare(const snap::Item& it) {
  const auto& r = it.replica;
  return !it.relic && !it.augment &&
         (!r || (r->relic.empty() && r->augment.empty() && r->relic_seed == 0 && r->augment_seed == 0 && r->augment_level == 0));
}

// Items the swap makes unequippable come off both predictions.
void unequip_broken(const Context& ctx, Found& found, int loc, const snap::Snapshot& from, model::Prediction& hovered_swap,
                    model::Prediction& base_swap, Slot& slot) {
  std::vector<std::string> broke_notes;
  const auto broken = take_off_broken(ctx.s, hovered_swap.after, loc, ctx.db, broke_notes);
  for (auto& n : broke_notes) add_unique(found.warnings, std::move(n));
  if (!broken.empty()) slot.itself = score::compare(from, base_swap.after, ctx.skills, ctx.preset, ctx.db);
  for (const auto& b : broken) {
    const snap::Item* worn = at(ctx.s, b.location);
    std::string t = "  ! " + (worn ? ctx.labels.item(*worn) : ctx.labels.item(b.record)) + ": needs ";
    for (std::size_t i = 0; i < b.why.size(); ++i) t += (i ? ", " : "") + requirements::text(b.why[i]);
    slot.broken.push_back({Tone::Worse, t});
    model::Prediction off = take_off(base_swap.after, b.location, ctx.db);
    record(ctx, found, off);
    base_swap.after = std::move(off.after);
  }
}

void set_lines(const Context& ctx, Found& found, const model::Prediction& base_swap, Slot& slot) {
  for (const auto& set : base_swap.sets) {
    const std::string name = ctx.labels.set(set.set) + " " + std::to_string(set.before) + "->" + std::to_string(set.after);
    if (!set.modeled) {
      add_unique(found.unscored, "set " + name);
      continue;
    }
    slot.sets.push_back({set.after > set.before ? Tone::Better : Tone::Worse, "  set " + name + " (scored)"});
    for (const auto& f : set.unmodeled) add_unique(found.unscored, "set bonus " + pretty(f));
  }
}

// Granted and set skills the score skipped, plus notes.
void note_unscored(const Context& ctx, Found& found, const model::Prediction& base_swap, Slot& slot) {
  auto valued = [&](const std::string& record) {
    return slot.base.procs.contains(record) ||
           std::any_of(slot.base.unscored.begin(), slot.base.unscored.end(), [&](const auto& u) { return u.record == record; });
  };
  for (const auto& g : base_swap.granted)
    if (!g.scored && !valued(g.record))
      add_unique(found.unscored, (g.gained ? "" : "loses ") + ctx.labels.skill(g.record) + " (" + unscored_reason(g.why) + ")");
  for (const auto& set : base_swap.sets) {
    if (!set.modeled) continue;
    if (!set.skill_lost.empty() && !valued(set.skill_lost))
      add_unique(found.unscored, "loses " + ctx.labels.skill(set.skill_lost) + " (set skill)");
    if (!set.skill_gained.empty() && !valued(set.skill_gained))
      add_unique(found.unscored, ctx.labels.skill(set.skill_gained) + " (set skill)");
  }
  for (auto& n : slot.base.notes) add_unique(found.warnings, std::move(n));
  for (const auto& u : slot.base.unscored) {
    add_unique(found.unscored, ctx.labels.skill(u.record) + " (rate unknown)");
    if (ctx.log) ctx.log->push_back(slot.title + " unscored " + u.record + ": " + u.why);
  }
}

std::string join(const std::vector<std::string>& v) {
  std::string t;
  for (const auto& x : v) t += (t.empty() ? "" : ", ") + x;
  return t;
}

// The old item's relic and augment, moved where they fit.
void moved_components(const Context& ctx, Found& found, const Plan& plan, const snap::Item& old, Slot& slot) {
  std::vector<std::string> fit, misfit;
  bool relic = false, augment = false;
  for (const auto* part : {&old.relic, &old.augment}) {
    if (!*part) continue;
    const bool ok = ctx.db && components::fits((*part)->record, ctx.candidate.record, ctx.item_type, *ctx.db);
    (ok ? fit : misfit).push_back(ctx.labels.item((*part)->record));
    (part == &old.relic ? relic : augment) = ok;
  }
  slot.moved_names = join(fit);
  slot.misfit = join(misfit);
  if (!relic && !augment) return;
  model::Prediction p = equip(ctx, found, plan, ctx.s, components::attach(found.candidate_base, old, relic, augment, ctx.db, found.notes));
  std::vector<std::string> broke_notes;
  take_off_broken(ctx.s, p.after, plan.location, ctx.db, broke_notes);
  for (auto& n : broke_notes) add_unique(found.warnings, std::move(n));
  slot.moved = score::compare(ctx.s, p.after, ctx.skills, ctx.preset, ctx.db, &found.memo);
  for (auto& n : slot.moved->notes) add_unique(found.warnings, std::move(n));
}

// Shift sheet and buff lines, and conversion warnings.
void slot_detail(const Context& ctx, Found& found, const snap::Item* old, const model::Prediction& hovered_swap, Slot& slot) {
  if (ctx.detail) {
    if (!found.view) found.view = score::buff_view(ctx.s, ctx.skills, ctx.preset, ctx.db);
    const score::BuffView& vb = *found.view;
    const score::BuffView va = score::buff_view(hovered_swap.after, ctx.skills, ctx.preset, ctx.db);
    // Sheet points come from the items as hovered, components included.
    if ((old && (old->relic || old->augment)) || ctx.candidate.relic || ctx.candidate.augment)
      slot.hovered = score::compare(ctx.s, hovered_swap.after, ctx.skills, ctx.preset, ctx.db, &found.memo);
    slot.detail = changes(vb.sheet, va.sheet, hovered_swap, ctx.labels, slot.hovered ? &*slot.hovered : &slot.base);
    slot.buffs = buff_lines(vb, va, ctx.labels, ctx.screen);
  }
  for (const auto& c : score::conversion_warnings(ctx.s, hovered_swap.after, ctx.preset, ctx.chosen.match_damage, ctx.db))
    add_unique(found.converts, "! converts away: " + (c.skill.empty() ? "" : ctx.labels.skill(c.skill) + " ") +
                                   num(c.percent, 0) + "% " + score::conversion_label(c.from) + " -> " +
                                   score::conversion_label(c.to));
}

void log_slot(std::vector<std::string>& log, const Slot& slot) {
  log.push_back(slot.title + " base: " + num(slot.base.percent, 2) + "% (" + terms(slot.base) + ")");
  log.push_back("  before: " + slot.base.before);
  log.push_back("  after:  " + slot.base.after);
  if (slot.hovered) log.push_back(slot.title + " as hovered: " + num(slot.hovered->percent, 2) + "% (" + terms(*slot.hovered) + ")");
  if (slot.itself) log.push_back(slot.title + " item itself: " + num(slot.itself->percent, 2) + "% (" + terms(*slot.itself) + ")");
  if (slot.moved) {
    log.push_back(slot.title + " moved: " + num(slot.moved->percent, 2) + "% (" + terms(*slot.moved) + ")");
    log.push_back("  before: " + slot.moved->before);
    log.push_back("  after:  " + slot.moved->after);
  }
}

// Base: both items without components; s's side is scored once.
Slot score_slot(const Context& ctx, Found& found, const Plan& plan) {
  const int loc = plan.location;
  Slot slot;
  slot.title = plan.title;
  const snap::Item* old = at(ctx.s, loc);
  hand_warnings(ctx, found, plan, old);
  std::optional<model::Prediction> stripped;
  if (old && !bare(*old)) {
    const snap::Item old_base = components::strip(*old, ctx.db, found.notes);
    stripped = model::swap(ctx.s, old_base, loc, ctx.db);
    record(ctx, found, *stripped);
  }
  const snap::Snapshot& from = stripped ? stripped->after : ctx.s;
  model::Prediction base_swap = equip(ctx, found, plan, from, found.candidate_base);
  model::Prediction hovered_swap = equip(ctx, found, plan, ctx.s, ctx.candidate);
  unequip_broken(ctx, found, loc, from, hovered_swap, base_swap, slot);
  set_lines(ctx, found, base_swap, slot);
  slot.base = score::compare(from, base_swap.after, ctx.skills, ctx.preset, ctx.db, stripped ? nullptr : &found.memo);
  note_unscored(ctx, found, base_swap, slot);
  if (old && (old->relic || old->augment)) moved_components(ctx, found, plan, *old, slot);
  if (ctx.detail || !ctx.preset.conversion_warn.empty()) slot_detail(ctx, found, old, hovered_swap, slot);
  if (ctx.log) log_slot(*ctx.log, slot);
  return slot;
}

std::size_t best_slot(const std::vector<Slot>& slots, double negligible) {
  if (slots.size() == 2 && std::abs(slots[0].base.percent - slots[1].base.percent) >= negligible)
    return slots[0].base.percent > slots[1].base.percent ? 0 : 1;
  return slots.size();
}

// The game's fonts have no other dash (tooltip.h).
Line heading(const score::Preset& chosen, const score::Stage* stage) {
  std::string t = "GrimCompare";
  if (!chosen.name.empty()) t += " - " + chosen.name;
  if (stage) t += " - stage " + stage->name;
  return {Tone::Heading, t};
}

std::string short_change(const score::Result& r, double negligible) {
  std::string t = change(r.percent, negligible).text;
  if (t.ends_with(" ^") || t.ends_with(" v")) t.resize(t.size() - 2);
  return t;
}

void slot_lines(std::vector<Line>& out, const Context& ctx, const Slot& slot, bool best, bool unmet) {
  const double negligible = ctx.preset.negligible_percent;
  Line l = change(slot.base.percent, negligible);
  l.text = slot.title + ": " + l.text + (best ? " (best)" : "") + (unmet ? " (if equipped)" : "");
  out.push_back(std::move(l));
  if (slot.itself) {
    const std::size_t n = slot.broken.size();
    out.push_back({Tone::Worse, "  item itself: " + short_change(*slot.itself, negligible) + ", but it unequips " +
                                    std::to_string(n) + (n == 1 ? " item:" : " items:")});
    out.insert(out.end(), slot.broken.begin(), slot.broken.end());
  }
  if (slot.moved) {
    Line m = change(slot.moved->percent, negligible);
    m.text = "  with your " + slot.moved_names + ": " + m.text;
    out.push_back(std::move(m));
  }
  if (!slot.misfit.empty()) out.push_back({Tone::Detail, "  your " + slot.misfit + " does not fit this item"});
  if (slot.base.pets) out.push_back({Tone::Detail, "  pet damage: experimental estimate"});
  out.insert(out.end(), slot.sets.begin(), slot.sets.end());
  if (!ctx.detail) return;
  out.push_back({Tone::Detail, "  " + terms(slot.base)});
  for (auto& t : top(slot.base, ctx.labels, "  ", ctx.screen)) out.push_back({Tone::Detail, std::move(t)});
  if (slot.moved) {
    out.push_back({Tone::Detail, "  with your " + slot.moved_names + ": " + terms(*slot.moved)});
    for (auto& t : top(*slot.moved, ctx.labels, "    ", ctx.screen)) out.push_back({Tone::Detail, std::move(t)});
  }
  const bool differs = slot.hovered && short_change(*slot.hovered, negligible) != short_change(slot.base, negligible);
  out.push_back({Tone::Detail, differs ? "  sheet as hovered, components included: " + short_change(*slot.hovered, negligible) +
                                             " [points of it]:"
                                       : "  sheet as hovered [points of total]:"});
  for (const auto& d : slot.detail) out.push_back({d.tone, "    " + d.text});
  for (auto& r : rr_lines(slot.base, ctx.labels)) out.push_back({r.tone, "    " + r.text});
  for (const auto& d : slot.buffs) out.push_back({d.tone, "  " + d.text});
}

void footer_lines(std::vector<Line>& out, const Context& ctx, const Found& found) {
  for (const auto& c : found.converts) out.push_back({Tone::Worse, c});
  if (!found.unscored.empty())
    for (auto& l : pack("+? unscored: ", "  ", found.unscored, ctx.screen)) out.push_back({Tone::Detail, std::move(l)});
  const auto& warnings = found.warnings;
  if (!warnings.empty() && !ctx.detail) out.push_back({Tone::Warning, "! model inaccurate (Shift: why)"});
  if (!warnings.empty() && ctx.detail) {
    out.push_back({Tone::Warning, "! model inaccurate:"});
    for (std::size_t i = 0; i < warnings.size() && i < kMaxWarnings; ++i) out.push_back({Tone::Detail, "  " + warnings[i]});
    if (warnings.size() > kMaxWarnings)
      out.push_back({Tone::Detail, "  ... " + std::to_string(warnings.size() - kMaxWarnings) + " more in GDCompare.log"});
  }
  unused_lines(out, found.unused, ctx.detail, ctx.labels, ctx.screen);
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
  const score::Settings preset = live_settings(stage ? stage->settings : chosen, raw, db);
  const auto skills = score::main_skills(s, preset, db);
  const Context ctx{s, candidate, item_type, detail, db, chosen, preset, skills, labels, screen, log};
  Found found;
  const std::optional<Line> unmet = unmet_line(candidate, raw, found.warnings);
  found.candidate_base = components::strip(candidate, db, found.notes);

  std::vector<Slot> slots;
  for (const Plan& plan : plans(s, candidate, item_type, db)) {
    slots.push_back(score_slot(ctx, found, plan));
    const Slot& slot = slots.back();
    if (!scored) continue;
    Scored x{plan.location, plan.also_off, slot.base.percent, {}, {}};
    if (slot.itself) x.itself = slot.itself->percent;
    if (slot.moved) x.moved = slot.moved->percent;
    scored->push_back(x);
  }
  for (auto& n : found.notes) add_unique(found.warnings, std::move(n));
  if (log) {
    for (const auto& w : found.warnings) log->push_back("warning: " + w);
    for (const auto& c : found.converts) log->push_back(c);
  }

  out.push_back(heading(chosen, stage));
  if (unmet) out.push_back(*unmet);
  const std::size_t best = best_slot(slots, preset.negligible_percent);
  for (std::size_t i = 0; i < slots.size(); ++i) slot_lines(out, ctx, slots[i], i == best, unmet.has_value());
  for (auto& l : target_set(s, candidate, chosen, db, labels)) out.push_back(std::move(l));
  footer_lines(out, ctx, found);
  return out;
}

}  // namespace gdc::tooltip
