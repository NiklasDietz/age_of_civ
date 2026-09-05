/**
 * @file HexGrid.cpp
 * @brief HexGrid initialization.
 */

#include "aoc/map/HexGrid.hpp"

namespace aoc::map {

void HexGrid::initialize(int32_t width, int32_t height, MapTopology topology) {
    // WHOLESALE RESET, deliberately, rather than a list of per-layer clears.
    //
    // 2026-07-27. This function used to reset 20 layers by hand. HexGrid carries
    // about 160, so roughly 140 derived layers survived an initialize() -- and
    // because generation only sizes a layer when the pass that produces it runs,
    // the survivors were not merely stale, they held THE PREVIOUS MAP'S VALUES.
    // A first generate read empty-vector fallbacks; a second generate into the
    // same object read the last map. The Continent Creator swaps and regenerates
    // into the same grids on every scrub tick, so it was already
    // non-reproducible, and a determinism test built on two FRESH grids would
    // have passed while that stood.
    //
    // Assigning a default-constructed instance makes the reset exhaustive by
    // construction, so it cannot drift when layer 161 is added -- which is how
    // the hand-written list fell 140 behind in the first place. Every caller has
    // fresh-grid semantics (MapGenerator::generate, Serializer's load, the map
    // editor, tests), so nothing depends on residual state.
    //
    // Layers left empty here are in exactly the state a FIRST generate finds
    // them in, which is the state the pipeline already handles: consumers of a
    // not-yet-produced layer take their absent-layer fallback. That equivalence
    // is the point -- regenerate now behaves identically to a fresh generate.
    *this = HexGrid{};

    this->m_width    = width;
    this->m_height   = height;
    this->m_topology = topology;

    std::size_t count = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);

    this->m_terrain.assign(count, TerrainType::Ocean);
    this->m_feature.assign(count, FeatureType::None);
    this->m_elevation.assign(count, 0);
    this->m_riverEdges.assign(count, 0);
    this->m_resource.assign(count, ResourceId{});
    this->m_reserves.assign(count, 0);
    this->m_prospectCooldown.assign(count, 0);
    this->m_owner.assign(count, INVALID_PLAYER);
    this->m_improvement.assign(count, ImprovementType::None);
    this->m_road.assign(count, 0);
    this->m_tileInfra.assign(count, 0);
    this->m_greenhouseCrop.clear();
    this->m_antiquitySite.clear();
    this->m_naturalWonder.assign(count, NaturalWonderType::None);
    this->m_chokepoint.assign(count, ChokepointType::None);
    this->m_falloutTurns.assign(count, 0);
    this->m_preFalloutFeature.assign(count, FeatureType::None);
    this->m_pillaged.assign(count, 0);
    // Everything else -- m_plateId (lazy-allocated by setPlateId), the hotspot
    // and per-plate tables, the row-latitude table, and the ~140 derived
    // geology / climate / biogeography layers -- is already empty from the reset
    // above. It used to need an explicit clear here, and only four of them got
    // one.
}

} // namespace aoc::map
