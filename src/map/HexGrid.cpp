/**
 * @file HexGrid.cpp
 * @brief HexGrid initialization.
 */

#include "aoc/map/HexGrid.hpp"

namespace aoc::map {

void HexGrid::initialize(int32_t width, int32_t height) {
    this->m_width  = width;
    this->m_height = height;

    std::size_t count = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);

    this->m_terrain.assign(count, TerrainType::Ocean);
    this->m_feature.assign(count, FeatureType::None);
    this->m_elevation.assign(count, 0);
    this->m_riverEdges.assign(count, 0);
    this->m_resource.assign(count, ResourceId{});
    this->m_owner.assign(count, INVALID_PLAYER);
}

} // namespace aoc::map
