#pragma once
// Each falls back to the internal name when the database or text is missing.
#include <string>

#include "gdc/arz.h"
#include "gdc/snapshot.h"
#include "gdc/text.h"

namespace gdc {

class Labels {
 public:
  Labels() = default;
  Labels(const arz::Database* db, const text::Table* text) : db_(db), text_(text) {}

  // Item-text tag "Defense<name>" (tags_ui.txt), "Resistance" shortened to "Resist".
  std::string resist(int type) const;
  // Skill_BuffRadius and SkillSecondary_PetModifier keep the name on their buff / pet record.
  std::string skill(const std::string& record) const;
  std::string set(const std::string& record) const;
  // Components and augments use `description`; gear uses `itemNameTag`.
  std::string item(const std::string& record) const;
  // Order from tagItemNameOrder: prefix, quality, style, name, suffix.
  std::string item(const snap::Item& it) const;
  // "" for anything but a class skill.
  std::string mastery(const std::string& skill_record) const;

  const arz::Database* db() const { return db_; }

 private:
  std::string tag(std::string_view tag) const;
  const arz::Database* db_ = nullptr;
  const text::Table* text_ = nullptr;
};

std::string record_stem(const std::string& record);

}  // namespace gdc
