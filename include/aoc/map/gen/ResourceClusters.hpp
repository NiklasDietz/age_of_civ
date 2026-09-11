/**
 * @file ResourceClusters.hpp
 * @brief Realistic luxury geography: each luxury occurs in a few clusters
 *        where climate and geology allow it, so that regional monopolies
 *        (silk, spices, tin, amber, silver) emerge from the map instead of a
 *        uniform sprinkle within a climate band.
 */
#pragma once

#include "aoc/core/Random.hpp"
#include "aoc/map/HexCoord.hpp"
#include "aoc/map/HexGrid.hpp"

#include <cstdint>
#include <vector>

namespace aoc::map {

/// Clusters of one good: `seedsPer4000Land` seeds on a map with 4000 land
/// tiles (scaled, at least one), farthest-point sampled among eligible tiles
/// and never closer than CLUSTER_MIN_SEPARATION, each grown breadth-first
/// through eligible tiles up to `tilesPerCluster` tiles.
struct ClusterSpec {
    uint16_t goodId         = 0;
    int32_t seedsPer4000Land = 3;
    int32_t tilesPerCluster  = 5;
};

inline constexpr int32_t CLUSTER_MIN_SEPARATION = 12;

/// Place the clusters; returns the seed tiles. `eligible` is per tile. With
/// `starts`, a seed never lands within RESOURCE_REACH_RADIUS of a start that
/// an earlier seed of this good already serves, so the good spreads between
/// civs instead of piling up on one.
std::vector<int32_t> placeClusters(HexGrid& grid, aoc::Random& rng, const ClusterSpec& spec,
                                   const std::vector<bool>& eligible, int32_t landTiles,
                                   const std::vector<hex::AxialCoord>* starts = nullptr);

/// Whether `goodId` may occur on tile `index` by climate and geology: the
/// one predicate per luxury type.
[[nodiscard]] bool luxuryEligible(const HexGrid& grid, uint16_t goodId, int32_t index);

/// Whether tile `index` lies in the good's climate band by latitude alone:
/// the fallback when a map has no tile that satisfies the full predicate,
/// so every luxury still exists somewhere.
[[nodiscard]] bool luxuryBandEligible(const HexGrid& grid, uint16_t goodId, int32_t index);

/// Realistic placement of every luxury type as clusters, run once the starts
/// are known. A start left with fewer than REGION_MIN_LUXURY_TYPES types in
/// reach gets a small cluster of a luxury its own surroundings allow, chosen
/// so that neighbouring starts end up with different ones.
void placeLuxuryClusters(HexGrid& grid, const std::vector<hex::AxialCoord>& starts, aoc::Random& rng);

/// Whether the strategic `goodId` may occur on tile `index`; only HORSES
/// (steppe and prairie) are clustered so far, the rest stay with geology.
[[nodiscard]] bool strategicEligible(const HexGrid& grid, uint16_t goodId, int32_t index);

/// Share of starts that must have horses within reach: the mounted line
/// should be open to half the world, not to whoever drew the steppe.
inline constexpr float HORSES_START_SHARE = 0.5f;

/// Horse clusters on steppe and prairie, then enough small ones near starts
/// that HORSES_START_SHARE of them have horses within reach.
void placeStrategicClusters(HexGrid& grid, const std::vector<hex::AxialCoord>& starts, aoc::Random& rng);

} // namespace aoc::map
