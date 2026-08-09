#pragma once

#include "aoc/simulation/city/ProductionQueue.hpp" // ProductionItemType
#include "aoc/simulation/tech/TechTree.hpp"        // PlayerTechComponent, techCount()

#include <cstdint>

namespace aoc::debug {

/// Returns true if `itemId` is within the static bounds table for `type`.
/// Does NOT check buildability prereqs (lock reasons, wonder adjacency, etc.)
/// — bounds-only validation, matching the scope of the HTTP POST /game/city/production route.
[[nodiscard]] bool isProductionItemValid(aoc::sim::ProductionItemType type, int32_t itemId);

/// Returns true if `techId` is in [0, techCount()) AND
/// `tech.canResearch(TechId{techId})` passes (i.e. prerequisites are met and
/// the tech is not already completed).
/// The bounds check runs first so `techDef()` is never called with an
/// out-of-range value (techDef only asserts in debug builds, not release).
[[nodiscard]] bool isResearchValid(const aoc::sim::PlayerTechComponent& tech, uint16_t techId);

} // namespace aoc::debug
