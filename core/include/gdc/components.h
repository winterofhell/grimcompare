#pragma once
// Moves each component's getter output between items.
#include <string>
#include <vector>

#include "gdc/arz.h"
#include "gdc/snapshot.h"

namespace gdc::components {

// An artifact keeps its own completion bonus.
snap::Item strip(const snap::Item& it, const arz::Database* db, std::vector<std::string>& notes);

// Completion bonus moves with the relic; rolled stats untraced.
snap::Item attach(const snap::Item& base, const snap::Item& donor, bool with_relic, bool with_augment,
                  const arz::Database* db, std::vector<std::string>& notes);

// Type 8 by class: Shield or Offhand; weapons unmapped.
bool fits(const std::string& component_record, const std::string& item_record, int item_type,
          const arz::Database& db);

}  // namespace gdc::components
