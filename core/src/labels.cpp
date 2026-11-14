#include "gdc/labels.h"

#include "gdc/attribute_names.h"

namespace gdc {

std::string record_stem(const std::string& record) {
  const auto slash = record.rfind('/');
  std::string s = slash == std::string::npos ? record : record.substr(slash + 1);
  if (s.ends_with(".dbr")) s.resize(s.size() - 4);
  return s;
}

std::string Labels::tag(std::string_view t) const {
  if (!text_ || t.empty()) return {};
  return text::plain(text_->get(t));
}

std::string Labels::resist(int type) const {
  const std::string internal(names::combat_attribute(type));
  std::string s = tag("Defense" + internal);
  if (s.empty()) return type == 9 ? "Vitality" : internal;  // DamageAttributeAbsBase_Life: tagDamageBaseVitality
  constexpr std::string_view kLong = " Resistance";
  if (s.ends_with(kLong)) s.replace(s.size() - kLong.size(), kLong.size(), " Resist");
  return s;
}

std::string Labels::skill(const std::string& record) const {
  if (db_)
    if (const arz::Record* r = db_->record(record)) {
      std::string s = tag(r->string("skillDisplayName"));
      if (s.empty())
        if (const arz::Record* b = db_->record(r->string("buffSkillName"))) s = tag(b->string("skillDisplayName"));
      if (!s.empty()) return s;
    }
  return record_stem(record);
}

std::string Labels::set(const std::string& record) const {
  if (db_)
    if (const arz::Record* r = db_->record(record)) {
      std::string s = tag(r->string("setName"));
      if (!s.empty()) return s;
    }
  return record_stem(record);
}

std::string Labels::item(const std::string& record) const {
  if (db_)
    if (const arz::Record* r = db_->record(record)) {
      std::string s = tag(r->string("description"));
      if (!s.empty()) return s;
    }
  return record_stem(record);
}

}  // namespace gdc
