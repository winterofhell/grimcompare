#pragma once
// Shared by the effects sources only.
#include <algorithm>
#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <vector>

#include "gdc/effects.h"

namespace gdc::effects {

// Past the per-level array's end, the last value holds.
inline double at(const arz::Record& r, std::string_view key, std::uint32_t level) {
  const auto it = r.fields.find(key);
  if (it == r.fields.end() || it->second.numbers.empty() || level == 0) return 0;
  const auto& n = it->second.numbers;
  return n[std::min<std::size_t>(level, n.size()) - 1];
}

// Devotions keep cooldown and autocast on the buff (tier2_32d_skill.dbr).
double own_or_buff(const arz::Record& r, std::string_view key, std::uint32_t level, const arz::Database& db);
std::string_view own_or_buff(const arz::Record& r, std::string_view key, const arz::Database& db);

std::string fmt(double v, int decimals);

// Builds the effect list; each effect adds its resist reductions.
struct Builder {
  const snap::Snapshot& s;
  const arz::Database& db;
  const Rates& rates;
  const std::map<std::string, double>& overrides;
  const std::vector<int>& resists;
  Collected out;

  double cdr;

  double recharge(double cooldown, bool own) const;
  std::string recharge_text(double cooldown, bool own) const;
  double summed(const Effect& e, const arz::Record& skill, std::string_view key) const;
  void trigger(Effect& e, const arz::Record& skill, double chance, std::string_view trigger, bool own);
  void proc(Effect& e, const arz::Record& skill, std::string_view controller_record, bool own);
  bool own_trigger(Effect& e, const arz::Record& skill, double chance, bool own);
  void cast(Effect& e, const arz::Record& skill, const char* how, bool own);
  std::optional<Effect> granted(const std::string& skill, std::uint32_t level, std::string note,
                                std::string_view controller, const std::string& source);
  // effects_pets.cpp. `summon` by value: add() grows out.effects.
  void pets(const Effect summon, const arz::Record& skill);
  void add(Effect e);
};

}  // namespace gdc::effects
