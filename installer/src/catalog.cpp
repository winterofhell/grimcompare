#include "catalog.h"

#include <algorithm>
#include <cstdio>

#include <tomlplusplus/gdc_toml.hpp>

namespace setup {

namespace {

bool strings(const toml::node* n, std::vector<std::string>& out, std::string& error, std::string_view what) {
  if (!n) return true;
  const toml::array* a = n->as_array();
  if (!a) return error = std::string(what) + " must be a list of strings", false;
  out.clear();
  for (const auto& e : *a) {
    const auto s = e.value<std::string>();
    if (!s || !e.is_string()) return error = std::string(what) + " must be a list of strings", false;
    out.push_back(*s);
  }
  return true;
}

bool text(const toml::node* n, std::string& out, std::string& error, std::string_view what) {
  if (!n) return true;
  const auto s = n->value<std::string>();
  if (!s || !n->is_string()) return error = std::string(what) + " must be a string", false;
  out = *s;
  return true;
}

bool weights(const toml::node* n, std::map<std::string, gdc::score::Weight>& out, std::string& error) {
  if (!n) return true;
  const toml::table* t = n->as_table();
  if (!t) return error = "other must be a table", false;
  out.clear();
  for (const auto& [k, v] : *t) {
    gdc::score::Weight w;
    if (v.is_number()) {
      w.weight = *v.value<double>();
    } else if (const toml::table* x = v.as_table()) {
      const auto wt = x->get("weight") ? x->get("weight")->value<double>() : std::nullopt;
      if (!wt) return error = "other." + std::string(k.str()) + ".weight must be a number", false;
      w.weight = *wt;
      if (const toml::node* when = x->get("when")) {
        if (const auto s = when->value<std::string>(); s && when->is_string()) w.when = {*s};
        else if (!strings(when, w.when, error, "other." + std::string(k.str()) + ".when")) return false;
      }
    } else {
      return error = "other." + std::string(k.str()) + " must be a number or { weight, when }", false;
    }
    out[std::string(k.str())] = w;
  }
  return true;
}

// Fields an entry or variant may set.
bool fields(const toml::table& t, Entry& e, std::string& error) {
  for (const auto& [k, _] : t) {
    static constexpr std::string_view kKnown[] = {"id", "name", "category", "classes", "damage", "skills", "sets",
                                                   "other", "conversion", "stats", "items", "variant"};
    if (std::find(std::begin(kKnown), std::end(kKnown), k.str()) == std::end(kKnown))
      return error = "unknown key " + std::string(k.str()), false;
  }
  return text(t.get("name"), e.name, error, "name") && strings(t.get("classes"), e.classes, error, "classes") &&
         strings(t.get("damage"), e.damage, error, "damage") && strings(t.get("skills"), e.skills, error, "skills") &&
         strings(t.get("sets"), e.sets, error, "sets") && weights(t.get("other"), e.other, error) &&
         text(t.get("conversion"), e.conversion, error, "conversion") && text(t.get("stats"), e.stats, error, "stats") &&
         text(t.get("items"), e.items, error, "items");
}

bool check(const Entry& e, std::string& error) {
  const std::string at = "build " + e.id + ": ";
  if (e.name.empty()) return error = at + "no name", false;
  if (std::find(std::begin(kCategories), std::end(kCategories), e.category) == std::end(kCategories))
    return error = at + "category must be melee, ranged, caster, pets or hybrid", false;
  if (e.classes.empty()) return error = at + "no classes", false;
  for (const auto& c : e.classes)
    if (code_masteries(c).empty()) return error = at + "bad class code " + c, false;
  if (e.damage.empty()) return error = at + "no damage types", false;
  for (const auto& d : e.damage)
    if (!gdc::score::damage_key(d)) return error = at + "unknown damage type " + d, false;
  for (const auto& r : e.skills)
    if (!r.starts_with("records/skills/") || !r.ends_with(".dbr")) return error = at + "bad skill record " + r, false;
  for (const auto& r : e.sets)
    if (!r.starts_with("records/items/") || !r.ends_with(".dbr")) return error = at + "bad set record " + r, false;
  return true;
}

}  // namespace

std::optional<std::vector<Entry>> load_catalog(std::string_view toml_text, std::string& error) {
  toml::parse_result r = toml::parse(toml_text);
  if (!r) {
    error = "line " + std::to_string(r.error().source().begin.line) + ": " + std::string(r.error().description());
    return std::nullopt;
  }
  const toml::array* builds = r.table().get("build") ? r.table().get("build")->as_array() : nullptr;
  if (!builds) return error = "no [[build]] blocks", std::nullopt;
  std::vector<Entry> out;
  for (const auto& node : *builds) {
    const toml::table* t = node.as_table();
    if (!t) return error = "[[build]] must be a table", std::nullopt;
    Entry base;
    if (!text(t->get("id"), base.id, error, "id") || !text(t->get("category"), base.category, error, "category")) return std::nullopt;
    if (base.id.empty()) return error = "a build has no id", std::nullopt;
    if (std::any_of(out.begin(), out.end(), [&](const Entry& e) { return e.id == base.id || e.id.starts_with(base.id + "/"); }))
      return error = "duplicate build id " + base.id, std::nullopt;
    if (!fields(*t, base, error)) return error = "build " + base.id + ": " + error, std::nullopt;
    const toml::array* variants = t->get("variant") ? t->get("variant")->as_array() : nullptr;
    if (!variants || variants->empty()) {
      if (!check(base, error)) return std::nullopt;
      out.push_back(base);
      continue;
    }
    int n = 0;
    for (const auto& vn : *variants) {
      const toml::table* vt = vn.as_table();
      if (!vt) return error = "build " + base.id + ": [[build.variant]] must be a table", std::nullopt;
      Entry v = base;
      v.id = base.id + "/" + std::to_string(++n);
      if (vt->get("id") || vt->get("category") || vt->get("variant"))
        return error = "build " + base.id + ": a variant cannot set id, category or variant", std::nullopt;
      if (!fields(*vt, v, error)) return error = "build " + base.id + ": " + error, std::nullopt;
      if (vt->get("name")) v.name = base.name + " - " + v.name;
      if (!check(v, error)) return std::nullopt;
      out.push_back(std::move(v));
    }
  }
  return out;
}

std::vector<int> code_masteries(std::string_view code) {
  if ((code.size() != 2 && code.size() != 4) || !std::all_of(code.begin(), code.end(), [](char c) { return c >= '0' && c <= '9'; }))
    return {};
  std::vector<int> out;
  for (std::size_t i = 0; i < code.size(); i += 2) {
    const int m = (code[i] - '0') * 10 + (code[i + 1] - '0');
    if (m < 1 || m > 10 || std::find(out.begin(), out.end(), m) != out.end()) return {};
    out.push_back(m);
  }
  return out;
}

bool fits(const Entry& e, const std::vector<int>& masteries) {
  if (masteries.empty()) return true;
  for (const auto& c : e.classes) {
    auto m = code_masteries(c);
    if (masteries.size() == 1 ? std::find(m.begin(), m.end(), masteries[0]) != m.end()
                              : std::is_permutation(m.begin(), m.end(), masteries.begin(), masteries.end()))
      return true;
  }
  return false;
}

std::string mastery_record(int m) {
  char b[80];
  std::snprintf(b, sizeof b, "records/skills/playerclass%02d/_classtraining_class%02d.dbr", m, m);
  return b;
}

std::string class_code(std::vector<int> masteries) {
  std::sort(masteries.begin(), masteries.end());
  std::string out;
  char b[8];
  for (const int m : masteries) std::snprintf(b, sizeof b, "%02d", m), out += b;
  return out;
}

std::string class_name(const gdc::text::Table& text, std::string_view code) {
  std::string raw(text.get("tagSkillClassName" + std::string(code)));
  if (raw.empty()) return std::string(code);
  // [ms]Male[fs]Female
  if (raw.starts_with("[ms]")) {
    const auto fs = raw.find("[fs]");
    if (fs != std::string::npos) {
      const std::string male = raw.substr(4, fs - 4), female = raw.substr(fs + 4);
      return male == female ? male : male + " / " + female;
    }
  }
  return gdc::text::plain(raw);
}

}  // namespace setup
