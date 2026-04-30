/**
 * @file HexGrid.cpp
 * @brief HexGrid initialization.
 */

#include "aoc/map/HexGrid.hpp"

namespace aoc::map {

void HexGrid::initialize(int32_t width, int32_t height, MapTopology topology) {
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
    this->m_naturalWonder.assign(count, NaturalWonderType::None);
    this->m_chokepoint.assign(count, ChokepointType::None);
    this->m_falloutTurns.assign(count, 0);
    this->m_preFalloutFeature.assign(count, FeatureType::None);
    // m_plateId is lazy-allocated by setPlateId. Reset it on every
    // initialize so a regenerate with a different grid size doesn't
    // leave a stale vector sized to the previous tile count — which
    // would cause out-of-bounds writes in setPlateId and heap corruption.
    this->m_plateId.clear();
    this->m_hotspots.clear();
    this->m_plateMotion.clear();
    this->m_plateCenter.clear();
}

} // namespace aoc::map
