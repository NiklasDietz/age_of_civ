/**
 * @file Session17.cpp
 * @brief SESSION 17 implementation -- terminal Earth-system analytics.
 */

#include "aoc/map/gen/EcoAnalytics.hpp"

#include "aoc/map/HexGrid.hpp"

namespace aoc::map::gen {

void runEcoAnalytics(HexGrid& grid) {
    // All previously-computed analytics (PET, aridity, erosion potential,
    // carbon stock, wilderness, flood frequency, canopy stratification,
    // riparian forest, magnetic intensity, groundwater depth) were write-only
    // map-generation metadata with no readers and have been removed. This
    // terminal pass now has no work to do.
    (void)grid;
}

} // namespace aoc::map::gen
