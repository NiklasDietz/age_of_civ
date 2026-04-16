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
    this->m_naturalWonder.assign(count, NaturalWonderType::None);
    this->m_chokepoint.assign(count, ChokepointType::None);
    this->m_falloutTurns.assign(count, 0);
    this->m_preFalloutFeature.assign(count, FeatureType::None);
}

} // namespace aoc::map
