#include "steam.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>

#include "platform.h"

namespace setup {

namespace fs = std::filesystem;

namespace {

std::string lower(std::string_view s) {
  std::string out(s);
  for (char& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return out;
}

bool space(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\v' || c == '\f'; }

std::vector<std::string> tokens(std::string_view t) {
  std::vector<std::string> out;
  std::size_t i = 0;
  while (i < t.size()) {
    const char c = t[i];
    if (c == '{' || c == '}') {
      out.emplace_back(1, c);
      ++i;
    } else if (c == '"') {
      std::string buf;
      std::size_t j = i + 1;
      for (; j < t.size() && t[j] != '"'; ++j) {
        if (t[j] == '\\' && j + 1 < t.size()) ++j;
        buf += t[j];
      }
      out.push_back(std::move(buf));
      i = j + 1;
    } else if (t.substr(i, 2) == "//") {
      const auto nl = t.find('\n', i);
      i = nl == std::string_view::npos ? t.size() : nl + 1;
    } else {
      ++i;
    }
  }
  return out;
}

// "a,b=n,b;c=b" -> {[a,b], "n,b"}, {[c], "b"}; an entry without '=' has no mode.
struct Override {
  std::vector<std::string> dlls;
  std::optional<std::string> mode;
};

std::vector<Override> parse_overrides(std::string_view v) {
  std::vector<Override> out;
  std::size_t start = 0;
  while (start <= v.size()) {
    const auto end = std::min(v.find(';', start), v.size());
    const std::string_view entry = v.substr(start, end - start);
    start = end + 1;
    if (entry.empty()) continue;
    Override o;
    const auto eq = entry.find('=');
    const std::string_view names = entry.substr(0, eq);
    if (eq != std::string_view::npos) o.mode = std::string(entry.substr(eq + 1));
    std::size_t a = 0;
    while (a <= names.size()) {
      const auto b = std::min(names.find(',', a), names.size());
      if (b > a) o.dlls.emplace_back(names.substr(a, b - a));
      a = b + 1;
    }
    out.push_back(std::move(o));
  }
  return out;
}

std::string format_overrides(const std::vector<Override>& entries) {
  std::string out;
  for (const auto& e : entries) {
    if (e.dlls.empty()) continue;
    if (!out.empty()) out += ';';
    for (std::size_t i = 0; i < e.dlls.size(); ++i) out += (i ? "," : "") + e.dlls[i];
    if (e.mode) out += "=" + *e.mode;
  }
  return out;
}

// DXVK_HDR=<value> as its own word, with the whitespace after it.
std::string drop_hdr(std::string s) {
  constexpr std::string_view kVar = "DXVK_HDR=";
  std::size_t i = 0;
  while ((i = s.find(kVar, i)) != std::string::npos) {
    if (i > 0 && !space(s[i - 1])) {
      i += kVar.size();
      continue;
    }
    std::size_t j = i + kVar.size();
    while (j < s.size() && !space(s[j])) ++j;
    while (j < s.size() && space(s[j])) ++j;
    s.erase(i, j - i);
  }
  return s;
}

std::string trim(std::string_view s) {
  std::size_t a = 0, b = s.size();
  while (a < b && space(s[a])) ++a;
  while (b > a && space(s[b - 1])) --b;
  return std::string(s.substr(a, b - a));
}

std::string rewrite(std::string_view in, bool add, const std::set<std::string>* present) {
  constexpr std::string_view kDll = "dinput8", kMode = "n,b", kVar = "WINEDLLOVERRIDES=";
  std::string opts(in);
  if (present && std::none_of(present->begin(), present->end(), [](const std::string& f) {
        return f.starts_with("renodx") && f.ends_with(".addon64");
      }))
    opts = drop_hdr(opts);
  // WINEDLLOVERRIDES=("..."|\S*), first occurrence.
  const auto at = opts.find(kVar);
  std::size_t end = std::string::npos;
  std::string value;
  if (at != std::string::npos) {
    std::size_t v = at + kVar.size();
    if (v < opts.size() && opts[v] == '"' && opts.find('"', v + 1) != std::string::npos) {
      end = opts.find('"', v + 1) + 1;
    } else {
      end = v;
      while (end < opts.size() && !space(opts[end])) ++end;
    }
    value = opts.substr(v, end - v);
    const auto a = value.find_first_not_of('"'), b = value.find_last_not_of('"');  // str.strip('"')
    value = a == std::string::npos ? "" : value.substr(a, b - a + 1);
  }
  auto entries = parse_overrides(value);
  for (auto& e : entries)
    std::erase_if(e.dlls, [&](const std::string& d) { return lower(d) == kDll; });
  if (add) {
    const auto t = std::find_if(entries.begin(), entries.end(), [&](const Override& e) { return e.mode == kMode && !e.dlls.empty(); });
    if (t != entries.end()) t->dlls.emplace_back(kDll);
    else entries.push_back({{std::string(kDll)}, std::string(kMode)});
  }
  const std::string formatted = format_overrides(entries);
  const std::string token = formatted.empty() ? "" : std::string(kVar) + "\"" + formatted + "\"";
  if (at != std::string::npos) {
    opts = trim(opts.substr(0, at) + token + opts.substr(end));
    std::string collapsed;
    for (const char c : opts)
      if (!(c == ' ' && !collapsed.empty() && collapsed.back() == ' ')) collapsed += c;
    opts = collapsed;
  } else if (!token.empty()) {
    opts = trim(token + " " + opts);
  }
  if (add && opts.find("%command%") == std::string::npos) opts = trim(opts + " %command%");
  return opts;
}

std::optional<std::string> read_text(const fs::path& p) {
  std::ifstream f(p, std::ios::binary);
  if (!f) return std::nullopt;
  std::stringstream ss;
  ss << f.rdbuf();
  return ss.str();
}

}  // namespace

const Vdf* Vdf::child(std::string_view key) const {
  const auto it = children.find(lower(key));
  return it == children.end() ? nullptr : &it->second;
}

const Vdf* Vdf::path(std::initializer_list<std::string_view> keys) const {
  const Vdf* n = this;
  for (const auto k : keys)
    if (!(n = n->child(k))) return nullptr;
  return n;
}

std::optional<std::string> Vdf::value(std::string_view key) const {
  const auto it = values.find(lower(key));
  if (it == values.end()) return std::nullopt;
  return it->second;
}

Vdf parse_vdf(std::string_view text) {
  Vdf root;
  std::vector<Vdf*> stack;
  Vdf* cur = &root;
  std::optional<std::string> key;
  for (auto& tok : tokens(text)) {
    if (tok == "{") {
      Vdf& child = cur->children[lower(key.value_or(""))];
      stack.push_back(cur);
      cur = &child;
      key.reset();
    } else if (tok == "}") {
      if (!stack.empty()) cur = stack.back(), stack.pop_back();
      key.reset();
    } else if (!key) {
      key = std::move(tok);
    } else {
      cur->values[lower(*key)] = std::move(tok);
      key.reset();
    }
  }
  return root;
}

std::string merge_launch_options(std::string_view opts, const std::set<std::string>* present) {
  return rewrite(opts, true, present);
}

std::string remove_launch_options(std::string_view opts) { return rewrite(opts, false, nullptr); }

std::optional<std::string> launch_options(const Vdf& localconfig) {
  const Vdf* app = localconfig.path({"UserLocalConfigStore", "Software", "Valve", "Steam", "apps", kAppId});
  if (!app) return std::nullopt;
  return app->value("LaunchOptions").value_or("");
}

std::vector<fs::path> steam_libraries(const fs::path& steam_root) {
  std::vector<fs::path> out{steam_root};
  const auto text = read_text(steam_root / "steamapps" / "libraryfolders.vdf");
  if (!text) return out;
  const Vdf v = parse_vdf(*text);
  if (const Vdf* lf = v.child("libraryfolders"))
    for (const auto& [_, lib] : lf->children)
      if (const auto p = lib.value("path")) {
        const fs::path lp = u8path(*p);
        std::error_code ec;
        if (std::none_of(out.begin(), out.end(), [&](const fs::path& x) { return fs::equivalent(x, lp, ec); }))
          out.push_back(lp);
      }
  return out;
}

std::vector<SteamUser> steam_users(const fs::path& steam_root) {
  std::vector<SteamUser> out;
  std::error_code ec;
  for (const auto& d : fs::directory_iterator(steam_root / "userdata", ec)) {
    const auto text = read_text(d.path() / "config" / "localconfig.vdf");
    if (!text) continue;
    out.push_back({u8string(d.path().filename()), launch_options(parse_vdf(*text))});
  }
  std::sort(out.begin(), out.end(), [](const SteamUser& a, const SteamUser& b) { return a.id < b.id; });
  return out;
}

}  // namespace setup
