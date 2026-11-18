#pragma once
// Shared by the core tooltip sources only.
#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "gdc/requirements.h"
#include "gdc/tooltip.h"

namespace gdc::tooltip {

// tooltip.cpp: number and key text.
std::string num(double v, int decimals);
std::string signed_num(double v, int decimals);
std::string pretty(std::string key);

// tooltip_sheet.cpp
std::string resist_stat(int type);

// tooltip_result.cpp: lines from a score result.
std::string terms(const score::Result& r);
std::vector<std::string> top(const score::Result& r, const Labels& labels, const std::string& indent, const Screen& screen);
std::vector<Line> rr_lines(const score::Result& r, const Labels& labels);
std::vector<Line> target_set(const snap::Snapshot& s, const snap::Item& candidate, const score::Preset& p,
                             const arz::Database* db, const Labels& labels);
void unused_lines(std::vector<Line>& out, const std::map<std::string, std::uint32_t>& unused, bool detail, const Labels& labels,
                  const Screen& screen);

// tooltip_slots.cpp: which slots an item replaces.
struct Plan {
  int location = -1;
  int also_off = -1;
  std::string title;
};

const snap::Item* at(const snap::Snapshot& s, int location);
bool weapon(const snap::Item* it, const arz::Database* db);
std::vector<Plan> plans(const snap::Snapshot& s, const snap::Item& candidate, int item_type, const arz::Database* db);
model::Prediction take_off(const snap::Snapshot& s, int location, const arz::Database* db);

// An item the swap makes unequippable is scored as removed.
struct Broken {
  int location = -1;
  std::string record;
  std::vector<requirements::Unmet> why;
};
std::vector<Broken> take_off_broken(const snap::Snapshot& before, snap::Snapshot& after, int location,
                                    const arz::Database* db, std::vector<std::string>& warnings);

}  // namespace gdc::tooltip
