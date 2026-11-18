// Preset files: write, bind to a character, unbind elsewhere.
#include <algorithm>

#include "gdc/snapshot.h"
#include "platform.h"
#include "tui.h"
#include "wizard_internal.h"

namespace setup::wiz {

using ui::fmt;
using ui::tr;

constexpr std::size_t kAdapterMaxPresets = 32;  // adapter/src/tooltip_internal.h kMaxNamed

namespace {

// One setup preset per character; others drop it (§13.40).
bool unbind_elsewhere(const Ctx& c, const Who& who, const fs::path& keep) {
  const std::string character = who_label(who);
  for (auto& e : setup_presets(c.presets)) {
    if (e.file == keep || !applies(e, who)) continue;
    std::vector<std::string> saves = e.preset.match_saves, rest = e.preset.match_characters;
    std::erase_if(saves, [&](const std::string& s) {
      return who.save.empty() ? gdc::snap::save_key_name(s) == who.name : gdc::score::same_save_key(s, who.save);
    });
    if (std::none_of(saves.begin(), saves.end(), [&](const std::string& s) { return gdc::snap::save_key_name(s) == who.name; }))
      std::erase(rest, who.name);
    std::string error;
    if (rest.empty()) {
      if (ui::confirm(fmt(tr("{} was only for {}. Delete it?"), {u8string(e.file.filename()), character}), true)) {
        std::error_code ec;
        fs::remove(e.file, ec);
        if (ec) return ui::warn(fmt(tr("Could not delete {}: {}"), {u8string(e.file), ec.message()})), false;
        ui::line(fmt(tr("Deleted {}."), {u8string(e.file.filename())}));
      } else {
        ui::warn(fmt(tr("{} still lists {}; the preset tried first by file name wins."), {u8string(e.file.filename()), character}));
      }
      continue;
    }
    const auto next = with_binding(e.text, rest, saves);
    if (!next || !write_file(e.file, *next, error)) return ui::warn(fmt(tr("Could not update {}: {}"), {u8string(e.file), error})), false;
    ui::line(fmt(tr("{} no longer applies to {}."), {u8string(e.file.filename()), character}));
  }
  return true;
}

}  // namespace

bool applies(const Existing& e, const Who& w) { return gdc::score::binds(e.preset, w.name, w.save); }

std::vector<Existing> setup_presets(const fs::path& dir) {
  std::vector<Existing> out;
  std::error_code ec;
  for (const auto& e : fs::directory_iterator(dir, ec)) {
    if (e.path().extension() != ".toml") continue;
    auto text = read_file(e.path());
    if (!text || !made_by_setup(*text)) continue;
    std::string error;
    const auto p = gdc::score::parse(*text, error);
    out.push_back({e.path(), *text, p ? *p : gdc::score::Preset{}});
  }
  std::sort(out.begin(), out.end(), [](const Existing& a, const Existing& b) { return a.file < b.file; });
  return out;
}

bool save(const Ctx& c, const BuildAnswers& a, const Who& who) {
  const std::string text = preset_text(a);
  std::string error;
  if (!gdc::score::parse(text, error)) {
    ui::warn(fmt(tr("The preset did not pass the check ({}); nothing was saved. Please report this."), {error}));
    return false;
  }
  // One file per character and build: "winterofhell-dw-cold-acid-3".
  std::string stem = preset_stem(who.name + (is_backup(who.save) ? "-backup-" : "-") + (a.source == "custom" ? a.title : a.source));
  for (;;) {
    stem = preset_stem(ui::ask(tr("File name"), stem));
    const fs::path p = c.presets / (stem + ".toml");
    if (const auto old = read_file(p)) {
      if (!made_by_setup(*old)) {
        ui::warn(fmt(tr("{} exists and was not made by setup. Pick another name."), {u8string(p.filename())}));
        stem += "-2";
        continue;
      }
      std::string error_old;
      const auto old_preset = gdc::score::parse(*old, error_old);
      if (old_preset && !old_preset->match_characters.empty() && !gdc::score::binds(*old_preset, who.name, who.save)) {
        ui::warn(fmt(tr("{} is in use for another character. Pick another name."), {u8string(p.filename())}));
        stem += "-2";
        continue;
      }
      if (!ui::confirm(fmt(tr("{} exists. Replace it?"), {u8string(p.filename())}), true)) continue;
    }
    if (!unbind_elsewhere(c, who, p)) return false;
    if (!write_file(p, text, error)) return ui::warn(error), false;
    ui::title(fmt(tr("Saved {}"), {u8string(p)}));
    std::size_t n = 0;
    std::error_code ec;
    for (const auto& e : fs::directory_iterator(c.presets, ec)) n += e.path().extension() == ".toml";
    if (n > kAdapterMaxPresets)
      ui::warn(fmt(tr("GDCompare.presets has {} presets; the mod reads only {} of them. Delete the ones you no longer use."),
                   {std::to_string(n), std::to_string(kAdapterMaxPresets)}));
    ui::wrap(fmt(tr("GrimCompare now scores items for {} with \"{}\". The mod picks up preset changes on its own, even while "
                    "the game runs. Run setup again any time to change it or use it for another character."),
                 {who_label(who), a.title}));
    return true;
  }
}

bool bind_existing(const Ctx& c, const Existing& e, const Who& who) {
  const std::string character = who_label(who);
  if (applies(e, who)) {
    if (!unbind_elsewhere(c, who, e.file)) return false;
    ui::line(fmt(tr("{} stays in use for {}."), {u8string(e.file.filename()), character}));
    return true;
  }
  auto characters = e.preset.match_characters;
  auto saves = e.preset.match_saves;
  if (std::find(characters.begin(), characters.end(), who.name) == characters.end()) characters.push_back(who.name);
  if (!who.save.empty()) saves.push_back(who.save);
  const auto next = with_binding(e.text, characters, saves);
  std::string error;
  if (!next) return ui::warn(tr("That preset's [match] section was edited by hand; bind it by editing the file.")), false;
  if (!unbind_elsewhere(c, who, e.file) || !write_file(e.file, *next, error)) return error.empty() || (ui::warn(error), false);
  ui::line(fmt(tr("{} now applies to {}."), {u8string(e.file.filename()), character}));
  return true;
}

}  // namespace setup::wiz
