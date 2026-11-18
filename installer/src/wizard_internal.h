#pragma once
// Shared by the build wizard sources only.
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "buildpreset.h"
#include "gdc/arz.h"
#include "gdc/labels.h"
#include "gdc/score.h"
#include "gdc/text.h"

namespace setup::wiz {

namespace fs = std::filesystem;

inline constexpr const char* kDefaultAttack = "records/skills/default/defaultweaponattack.dbr";

struct Ctx {
  const gdc::arz::Database& db;
  const gdc::text::Table& text;
  gdc::Labels labels;
  fs::path presets;  // <run folder>/GDCompare.presets
};

struct Who {
  std::string name;
  std::vector<int> masteries;
  bool from_save = false;
  std::string save;  // gdc::snap::save_key; empty for a typed name
};

// A preset file setup wrote earlier.
struct Existing {
  fs::path file;
  std::string text;
  gdc::score::Preset preset;  // default when it does not parse: binds no one
};

// wizard.cpp
std::optional<std::string> read_file(const fs::path& p);
bool write_file(const fs::path& p, const std::string& text, std::string& error);
std::string mastery_name(const Ctx& c, int m);
std::string class_label(const Ctx& c, const std::vector<int>& masteries);
std::string skill_name(const Ctx& c, const std::string& record);
std::string damage_words(const std::vector<std::string>& names);
bool is_backup(const std::string& save);
std::string who_label(const Who& w);

// wizard_custom.cpp: the questions for a build of your own.
std::optional<BuildAnswers> custom(const Ctx& c, const std::vector<int>& masteries, const std::string& character);

// wizard_save.cpp: preset files and which character they bind.
bool applies(const Existing& e, const Who& w);
std::vector<Existing> setup_presets(const fs::path& dir);
bool save(const Ctx& c, const BuildAnswers& a, const Who& who);
bool bind_existing(const Ctx& c, const Existing& e, const Who& who);

}  // namespace setup::wiz
