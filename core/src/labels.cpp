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
  if (s.empty()) return type == combat_attr::kLife ? "Vitality" : internal;  // tagDamageBaseVitality
  constexpr std::string_view kLong = " Resistance";
  if (s.ends_with(kLong)) s.replace(s.size() - kLong.size(), kLong.size(), " Resist");
  // 0.9.2 wrapped "Poison & Acid"; "Poison/Acid" never wraps.
  if (const auto amp = s.find(" & "); amp != std::string::npos) s.replace(amp, 3, "/");
  return s;
}

std::string Labels::skill(const std::string& record) const {
  if (db_)
    if (const arz::Record* r = db_->record(record)) {
      std::string s = tag(r->string("skillDisplayName"));
      for (const char* link : {"buffSkillName", "petSkillName"})
        if (s.empty())
          if (const arz::Record* b = db_->record(r->string(link))) s = tag(b->string("skillDisplayName"));
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
      if (s.empty()) s = tag(r->string("itemNameTag"));
      if (!s.empty()) return s;
    }
  return record_stem(record);
}

std::string Labels::item(const snap::Item& it) const {
  const arz::Record* base = db_ ? db_->record(it.record) : nullptr;
  if (!base || !it.replica) return item(it.record);
  std::string out;
  auto add = [&](std::string part) {
    if (!part.empty()) out += (out.empty() ? "" : " ") + part;
  };
  auto affix = [&](const std::string& record) {
    const arz::Record* r = db_->record(record);
    return r ? tag(r->string("lootRandomizerName")) : std::string{};
  };
  if (base->number("hidePrefixName") == 0) add(affix(it.replica->prefix));
  add(tag(base->string("itemQualityTag")));
  add(tag(base->string("itemStyleTag")));
  add(item(it.record));
  if (base->number("hideSuffixName") == 0) add(affix(it.replica->suffix));
  return out;
}

std::string Labels::mastery(const std::string& skill_record) const {
  constexpr std::string_view kDir = "records/skills/playerclass";
  if (!db_ || !skill_record.starts_with(kDir) || skill_record.size() < kDir.size() + 3) return {};
  const std::string nn = skill_record.substr(kDir.size(), 2);
  const arz::Record* r = db_->record(std::string(kDir) + nn + "/_classtraining_class" + nn + ".dbr");
  return r ? tag(r->string("skillDisplayName")) : std::string{};
}

}  // namespace gdc
