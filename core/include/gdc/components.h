#pragma once
// An item without its component (ItemRelic) and augment (ItemEnchantment), and the same base with another
// item's component and augment attached: the tooltip's "base vs base" and "your component moved" lines.
// Item stats (snap::ItemStats) are the item's own getter output with both included; each component's own
// getter output (snap::Component::stats) is subtracted or added per accumulator row. A component's flat
// damage is not in its own stats (no damage slot) but inside the parent's offensive_damage, so it is
// matched by value from the component's database record. Absorption rows are left alone (the swap model
// averages them per item, stat_model.cpp).
#include <string>
#include <vector>

#include "gdc/arz.h"
#include "gdc/snapshot.h"

namespace gdc::components {

// `it` with relic, augment and their replica records removed (an artifact keeps its own completion bonus).
// A change that could not be applied exactly is appended to `notes`.
snap::Item strip(const snap::Item& it, const arz::Database* db, std::vector<std::string>& notes);

// `base` (from strip()) with `donor`'s relic and / or augment attached (each only when `with_relic` /
// `with_augment` and the donor has one). The relic's completion bonus record moves with it (its skill grants);
// its rolled stats do not (not traced; notes says so when the bonus has stats).
snap::Item attach(const snap::Item& base, const snap::Item& donor, bool with_relic, bool with_augment,
                  const arz::Database* db, std::vector<std::string>& notes);

// Whether a component / augment record can go into item `item_record` of engine type `item_type`
// (Item::GetItemType): the component's slot flag (amulet, ring, medal, head, chest, shoulders, hands, legs,
// feet, waist; type 8 by the item's record class: WeaponArmor_Shield -> shield, WeaponArmor_Offhand ->
// offhand, the only two WeaponArmor classes in 1.3.1.1; every ItemRelic / ItemEnchantment record in 1.3.1.1
// has these fields). Weapon types are not mapped (not compared). False when a record or the type is unknown.
bool fits(const std::string& component_record, const std::string& item_record, int item_type,
          const arz::Database& db);

}  // namespace gdc::components
