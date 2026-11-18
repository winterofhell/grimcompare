#include "install.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <ctime>
#include <fstream>
#include <sstream>

#include "gdc/score.h"
#include "platform.h"
#include "sha256.h"

namespace setup {

namespace fs = std::filesystem;

namespace {

constexpr std::string_view kAsi = "GDCompare.asi", kDll = "dinput8.dll", kIni = "dinput8.ini";
constexpr std::string_view kPreset = "GDCompare.preset.toml", kPresetDir = "GDCompare.presets";

std::optional<std::string> read_file(const fs::path& p) {
  std::ifstream f(p, std::ios::binary);
  if (!f) return std::nullopt;
  std::stringstream ss;
  ss << f.rdbuf();
  return ss.str();
}

fs::path at(const fs::path& target, const std::string& rel) { return target / u8path(rel); }

bool is_preset(const std::string& name) { return name == kPreset || name.starts_with(std::string(kPresetDir) + "/"); }

std::string lower(std::string s) {
  for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return s;
}

std::string trim(std::string_view s) {
  const auto a = s.find_first_not_of(" \t\r\n"), b = s.find_last_not_of(" \t\r\n");
  return a == std::string_view::npos ? "" : std::string(s.substr(a, b - a + 1));
}

// UTF-16LE value after `key` in a version String block.
std::string version_string(const std::string& data, std::string_view key) {
  std::string pat;
  for (const char c : key) pat += c, pat += '\0';
  pat += std::string(2, '\0');
  const auto i = data.find(pat);
  if (i == std::string::npos) return "";
  std::size_t j = i + pat.size();
  while (j + 1 < data.size() && data[j] == '\0' && data[j + 1] == '\0') j += 2;
  std::string out;
  for (; j + 1 < data.size() && (data[j] || data[j + 1]) && out.size() < 64; j += 2)
    out += static_cast<unsigned char>(data[j + 1]) == 0 && static_cast<unsigned char>(data[j]) < 0x80 ? data[j] : '?';
  return out;
}

bool parses(const fs::path& p, std::string& error) {
  const auto text = read_file(p);
  if (!text) return error = "unreadable", false;
  return gdc::score::parse(*text, error).has_value();
}

std::string stamp() {
  const std::time_t t = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
  std::tm tm{};
#ifdef _WIN32
  localtime_s(&tm, &t);
#else
  localtime_r(&t, &tm);
#endif
  char b[32];
  std::strftime(b, sizeof b, "%Y%m%d-%H%M%S", &tm);
  return b;
}

bool copy_atomic(const fs::path& from, const fs::path& to, std::string& error) {
  std::error_code ec;
  fs::create_directories(to.parent_path(), ec);
  fs::path tmp = to;
  tmp += ".gdc-tmp";
  // Rename, never rewrite: a process may still map the file.
  if (!fs::copy_file(from, tmp, fs::copy_options::overwrite_existing, ec) || (fs::rename(tmp, to, ec), ec)) {
    error = "could not write " + u8string(to) + ": " + ec.message();
    fs::remove(tmp, ec);
    return false;
  }
  return true;
}

bool write_manifest(const fs::path& target, const std::map<std::string, std::string>& m, std::string& error) {
  const fs::path p = target / kManifest;
  std::error_code ec;
  if (m.empty()) return fs::remove(p, ec), true;
  fs::path tmp = p;
  tmp += ".gdc-tmp";
  {
    std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
    for (const auto& [name, hash] : m) f << name << '\t' << hash << '\n';
    if (!f) return error = "could not write " + u8string(tmp), false;
  }
  fs::rename(tmp, p, ec);
  if (ec) return error = "could not write " + u8string(p) + ": " + ec.message(), false;
  return true;
}

}  // namespace

bool Plan::changes() const {
  return std::any_of(steps.begin(), steps.end(), [](const Step& s) { return s.kind != Step::Kind::kKeep; });
}

std::optional<Payload> load_payload(const fs::path& dir, std::string& error) {
  Payload p;
  p.dir = dir;
  auto add = [&](const std::string& name, const fs::path& src) {
    const auto h = sha256_file(src);
    if (!h) return error = "missing " + u8string(src), false;
    p.files[name] = *h;
    p.sources[name] = src;
    return true;
  };
  if (!add(std::string(kAsi), dir / kAsi) || !add(std::string(kDll), dir / kDll) || !add(std::string(kIni), dir / kIni) ||
      !add(std::string(kPreset), dir / "presets" / "default.toml"))
    return std::nullopt;
  if (p.files[std::string(kDll)] != kUalSha256) return error = u8string(dir / kDll) + " is not the expected Ultimate ASI Loader build", std::nullopt;
  std::error_code ec;
  for (const auto& e : fs::directory_iterator(dir / "presets", ec)) {
    const std::string n = u8string(e.path().filename());
    if (e.path().extension() == ".toml" && n != "default.toml" && !add(std::string(kPresetDir) + "/" + n, e.path()))
      return std::nullopt;
  }
  if (ec) return error = "cannot read " + u8string(dir / "presets") + ": " + ec.message(), std::nullopt;
  return p;
}

std::map<std::string, std::string> read_manifest(const fs::path& target) {
  std::map<std::string, std::string> out;
  std::ifstream f(target / kManifest);
  std::string line;
  while (std::getline(f, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    const auto tab = line.find('\t');
    if (tab == std::string::npos || tab == 0) continue;
    const std::string name = line.substr(0, tab);
    if (name.find("..") != std::string::npos || name.front() == '/' || name.find('\\') != std::string::npos) continue;  // never leave the folder
    out[name] = line.substr(tab + 1);
  }
  return out;
}

LoaderInfo inspect_loader(const fs::path& dll) {
  LoaderInfo out;
  const auto d = read_file(dll);
  if (!d || d->size() < 0x40) return out;
  const auto u = [&](std::size_t i) { return static_cast<std::uint32_t>(static_cast<unsigned char>((*d)[i])); };
  const std::size_t pe = u(0x3C) | u(0x3D) << 8 | u(0x3E) << 16 | u(0x3F) << 24;
  if (pe + 6 <= d->size() && d->compare(pe, 4, std::string("PE\0\0", 4)) == 0) out.x64 = (u(pe + 4) | u(pe + 5) << 8) == 0x8664;
  out.product = version_string(*d, "ProductName");
  out.version = version_string(*d, "ProductVersion");
  const std::string p = lower(out.product);
  out.ual = p.find("ultimate-asi-loader") != std::string::npos || p.find("ultimate asi loader") != std::string::npos;
  return out;
}

bool ini_loads_in_dllmain(const fs::path& ini) {
  std::ifstream f(ini);
  std::string line, section;
  bool ok = false;
  while (std::getline(f, line)) {
    const std::string t = trim(line);
    if (t.empty() || t[0] == ';' || t[0] == '#') continue;
    if (t.front() == '[' && t.back() == ']') {
      section = lower(trim(t.substr(1, t.size() - 2)));
      continue;
    }
    const auto eq = t.find('=');
    if (section == "globalsets" && eq != std::string::npos && lower(trim(t.substr(0, eq))) == "dontloadfromdllmain")
      ok = trim(t.substr(eq + 1)) == "0";  // the last one wins
  }
  return ok;
}

namespace {

using Manifest = std::map<std::string, std::string>;

// sha256 of an installed file; "?" when unreadable.
std::optional<std::string> current_hash(const fs::path& target, const std::string& name) {
  std::error_code ec;
  if (!fs::exists(at(target, name), ec)) return std::nullopt;
  return sha256_file(at(target, name)).value_or("?");
}

void add(Plan& plan, Step::Kind k, const std::string& f, std::string why, bool backup = false) {
  plan.steps.push_back({k, f, std::move(why), backup});
}

void plan_uninstall(Plan& plan, const Manifest& old) {
  const fs::path& target = plan.target;
  if (old.empty()) plan.blockers.push_back("GrimCompare is not installed in " + u8string(target) + " (no " + std::string(kManifest) + ").");
  // Loader stays while other .asi plugins still use it.
  bool other_asi = false;
  std::error_code dec;
  for (const auto& e : fs::directory_iterator(target, dec))
    other_asi = other_asi || (lower(u8string(e.path().extension())) == ".asi" && u8string(e.path().filename()) != kAsi);
  for (const auto& [name, hash] : old) {
    const auto have = current_hash(target, name);
    if (!have) continue;
    if ((name == kDll || name == kIni) && other_asi) add(plan, Step::Kind::kKeep, name, "other .asi plugins in this folder use it");
    else if (*have == hash) add(plan, Step::Kind::kRemove, name, "installed by setup");
    else {
      add(plan, Step::Kind::kKeep, name, "changed since it was installed, so it is yours now");
      plan.manifest[name] = hash;
    }
  }
  std::error_code ec;
  if (fs::exists(target / "GDCompare.log", ec)) add(plan, Step::Kind::kRemove, "GDCompare.log", "the mod's log");
  for (const auto& e : fs::directory_iterator(target / kPresetDir, ec))
    if (!old.contains(std::string(kPresetDir) + "/" + u8string(e.path().filename())))
      add(plan, Step::Kind::kKeep, std::string(kPresetDir) + "/" + u8string(e.path().filename()), "your own preset");
}

// Foreign dinput8.dll: reuse a 64-bit UAL, else block.
void plan_foreign_loader(Plan& plan, const std::string& name) {
  const LoaderInfo li = inspect_loader(at(plan.target, name));
  if (li.ual && li.x64) {
    add(plan, Step::Kind::kKeep, name, "your Ultimate ASI Loader " + li.version + ", reused");
    plan.notes.push_back("Using the Ultimate ASI Loader already there (" + li.version + "). GrimCompare is tested with v9.7.4; if the game "
                         "fails to start, replace it with the dinput8.dll from this package.");
  } else if (li.ual) {
    plan.blockers.push_back("dinput8.dll is a 32-bit Ultimate ASI Loader; the 64-bit game cannot load it. Remove it, then run setup again.");
  } else {
    plan.blockers.push_back(
        "dinput8.dll in " + u8string(plan.target) + " belongs to another mod" + (li.product.empty() ? "" : " (" + li.product + ")") +
        ". GrimCompare loads through Ultimate ASI Loader, which must be dinput8.dll, so the two cannot both be installed as they are. "
        "Setup will not overwrite it. If that mod can be loaded as an .asi plugin, or under another name, set it up that way and run setup again.");
  }
}

// Differs from both the package and what setup installed.
void plan_changed_file(Plan& plan, const Manifest& old, const std::string& name, const std::string& src_hash) {
  if (name == kDll) {
    plan_foreign_loader(plan, name);
  } else if (name == kIni) {
    if (ini_loads_in_dllmain(at(plan.target, name))) {
      add(plan, Step::Kind::kKeep, name, "yours, has dontloadfromdllmain=0");
    } else {
      plan.blockers.push_back("dinput8.ini in " + u8string(plan.target) + " is not from GrimCompare and lacks \"dontloadfromdllmain=0\" under "
                              "[globalsets]. Without it the game crashes before the main menu. Add that line (or delete the file), then "
                              "run setup again.");
    }
  } else if (is_preset(name)) {
    std::string error;
    if (parses(at(plan.target, name), error)) {
      add(plan, Step::Kind::kKeep, name, "edited by you, kept (the new version is in the package's presets folder)");
      if (const auto owned = old.find(name); owned != old.end()) plan.manifest[name] = owned->second;  // uninstall keeps it as changed
    } else {
      add(plan, Step::Kind::kWrite, name, "has an error (" + error + "), replaced; your copy is backed up", true);
      plan.manifest[name] = src_hash;
    }
  } else {
    add(plan, Step::Kind::kWrite, name, "not from this setup, replaced; the old file is backed up", true);
    plan.manifest[name] = src_hash;
  }
}

void plan_install(Plan& plan, const Manifest& old, const Payload& payload) {
  for (const auto& [name, src_hash] : payload.files) {
    const auto have = current_hash(plan.target, name);
    const auto owned = old.find(name);
    const bool ours = have && owned != old.end() && owned->second == *have;
    if (!have) {
      add(plan, Step::Kind::kWrite, name, "new");
      plan.manifest[name] = src_hash;
    } else if (*have == src_hash) {
      add(plan, Step::Kind::kKeep, name, "up to date");
      plan.manifest[name] = src_hash;
    } else if (ours) {
      add(plan, Step::Kind::kWrite, name, "update");
      plan.manifest[name] = src_hash;
    } else {
      plan_changed_file(plan, old, name, src_hash);
    }
  }
  // Presets dropped from this version: removed while unchanged.
  for (const auto& [name, hash] : old)
    if (!payload.files.contains(name)) {
      const auto have = current_hash(plan.target, name);
      if (have && *have == hash) add(plan, Step::Kind::kRemove, name, "no longer shipped");
      else if (have) add(plan, Step::Kind::kKeep, name, "no longer shipped, but changed by you");
    }
}

}  // namespace

Plan make_plan(Mode mode, const fs::path& target, const Payload* payload, std::optional<bool> running) {
  Plan plan;
  plan.mode = mode;
  plan.target = target;
  if (!running) plan.blockers.push_back("Could not check whether Grim Dawn is running. Close the game and try again.");
  else if (*running) plan.blockers.push_back("Grim Dawn is running. Quit the game first, then run setup again.");
  const Manifest old = read_manifest(target);
  if (mode == Mode::kUninstall) {
    plan_uninstall(plan, old);
    return plan;
  }
  if (!payload) {
    plan.blockers.push_back("No install files found next to setup.");
    return plan;
  }
  plan_install(plan, old, *payload);
  if (!plan.blockers.empty()) plan.manifest.clear();
  return plan;
}

bool apply(const Plan& plan, const Payload* payload, std::vector<std::string>& done, std::string& error) {
  if (!plan.blockers.empty()) return error = plan.blockers.front(), false;
  if (const auto r = game_running(); !r || *r) return error = "Grim Dawn is running (or could not be checked); nothing was changed.", false;
  auto manifest = read_manifest(plan.target);
  const fs::path backup = plan.target / "GDCompare.backup" / stamp();
  bool ok = true;
  for (const auto& s : plan.steps) {
    const fs::path dst = at(plan.target, s.file);
    std::error_code ec;
    if (s.kind == Step::Kind::kWrite) {
      if (!payload || !payload->sources.contains(s.file)) {
        error = "no source for " + s.file;
        ok = false;
        break;
      }
      if (s.backup && fs::exists(dst, ec)) {
        fs::create_directories((backup / u8path(s.file)).parent_path(), ec);
        if (!fs::copy_file(dst, backup / u8path(s.file), fs::copy_options::overwrite_existing, ec)) {
          error = "could not back up " + u8string(dst) + ": " + ec.message();
          ok = false;
          break;
        }
        done.push_back("backed up " + s.file + " to " + u8string(backup));
      }
      if (!copy_atomic(payload->sources.at(s.file), dst, error)) {
        ok = false;
        break;
      }
      manifest[s.file] = payload->files.at(s.file);
      done.push_back("wrote " + s.file);
    } else if (s.kind == Step::Kind::kRemove) {
      fs::remove(dst, ec);
      if (ec) {
        error = "could not remove " + u8string(dst) + ": " + ec.message();
        ok = false;
        break;
      }
      manifest.erase(s.file);
      done.push_back("removed " + s.file);
    }
  }
  if (ok) manifest = plan.manifest;
  std::string merror;
  if (!write_manifest(plan.target, manifest, merror)) {
    if (ok) error = merror;
    ok = false;
  }
  if (ok && plan.mode == Mode::kUninstall) {
    std::error_code ec;
    if (fs::is_empty(plan.target / kPresetDir, ec) && !ec) fs::remove(plan.target / kPresetDir, ec);
  }
  return ok;
}

}  // namespace setup
