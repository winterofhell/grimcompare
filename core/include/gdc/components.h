#pragma once
// Item stats include relic and augment; each component's own getter output is subtracted or added per row. A
// component's flat damage is not in its own stats but in the parent's offensive_damage, so it is matched by value
// from the component's record. Absorption rows are left alone (averaged per item, stat_model.cpp).
#include <string>
#include <vector>

#include "gdc/arz.h"
#include "gdc/snapshot.h"

namespace gdc::components {

// An artifact keeps its own completion bonus.
snap::Item strip(const snap::Item& it, const arz::Database* db, std::vector<std::string>& notes);

// The relic's completion bonus record moves with it; its rolled stats do not (not traced).
snap::Item attach(const snap::Item& base, const snap::Item& donor, bool with_relic, bool with_augment,
                  const arz::Database* db, std::vector<std::string>& notes);

// Type 8 is split by record class: WeaponArmor_Shield / WeaponArmor_Offhand, the only two in 1.3.1.1. Weapon
// types are not mapped.
bool fits(const std::string& component_record, const std::string& item_record, int item_type,
          const arz::Database& db);

}  // namespace gdc::components
