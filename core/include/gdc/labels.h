#pragma once
// Display names from the game's own text (Text_EN tags, gdc::text) and database records. Each falls
// back to the internal name when the database or text is missing (both may be null).
#include <string>

#include "gdc/arz.h"
#include "gdc/text.h"

namespace gdc {

class Labels {
 public:
  Labels() = default;
  Labels(const arz::Database* db, const text::Table* text) : db_(db), text_(text) {}

  // Resistance of CombatAttributeType `type`: the item-text tag "Defense<name>" (tags_ui.txt, e.g.
  // DefenseManaLeach = "{%.0f0}% {^E}Energy Leech Resistance"; <name> is the type's suffix from
  // re/CombatAttributeType.tsv, the same names the engine's Display= tags use), trailing "Resistance"
  // shortened to "Resist": "Energy Leech Resist", "Reduced Stun Duration".
  std::string resist(int type) const;
  // Skill record: its skillDisplayName tag; a Skill_BuffRadius-style record without one: its
  // buffSkillName record's. Fallback: the file stem.
  std::string skill(const std::string& record) const;
  // Set record (ItemEquipment::GetItemSetName): its setName tag. Fallback: the file stem.
  std::string set(const std::string& record) const;
  // Component (ItemRelic) or augment (ItemEnchantment) record: its `description` tag (compa_frozenheart:
  // tagCompA047Name). Fallback: the file stem.
  std::string item(const std::string& record) const;

  const arz::Database* db() const { return db_; }

 private:
  std::string tag(std::string_view tag) const;  // plain text of a tag, "" if absent
  const arz::Database* db_ = nullptr;
  const text::Table* text_ = nullptr;
};

// "records/a/b_c.dbr" -> "b_c".
std::string record_stem(const std::string& record);

}  // namespace gdc
