#pragma once

/**
 * @file HexGrid.hpp
 * @brief SoA flat-array storage for the hex tile map.
 *
 * Tiles are stored in parallel vectors indexed by (row * width + col) using
 * offset coordinates (odd-r). Each property is a separate contiguous array
 * for cache-friendly iteration when processing a single property across
 * the entire map (e.g., rendering all terrain, computing all yields).
 */

#include "aoc/map/HexCoord.hpp"
#include "aoc/map/Terrain.hpp"
#include "aoc/core/Types.hpp"

#include <cassert>
#include <cstdint>
#include <vector>

namespace aoc::map {

/// Tile improvement built by a Builder unit.
enum class ImprovementType : uint8_t {
    None,
    Farm,
    Mine,
    Plantation,
    Quarry,
    LumberMill,
    Camp,
    Pasture,
    FishingBoats,
    Fort,
    Road,

    Count
};

class HexGrid {
public:
    HexGrid() = default;

    /**
     * @brief Initialize the grid with the given dimensions.
     *
     * All tiles are set to Ocean with no features or resources.
     */
    void initialize(int32_t width, int32_t height);

    [[nodiscard]] int32_t width() const { return this->m_width; }
    [[nodiscard]] int32_t height() const { return this->m_height; }
    [[nodiscard]] int32_t tileCount() const { return this->m_width * this->m_height; }

    // ========================================================================
    // Coordinate validation and indexing
    // ========================================================================

    /// Check if an offset coordinate is within bounds.
    [[nodiscard]] bool isValid(hex::OffsetCoord c) const {
        return c.col >= 0 && c.col < this->m_width
            && c.row >= 0 && c.row < this->m_height;
    }

    /// Check if an axial coordinate is within bounds.
    [[nodiscard]] bool isValid(hex::AxialCoord a) const {
        return this->isValid(hex::axialToOffset(a));
    }

    /// Convert offset coordinate to flat array index. Asserts in-bounds.
    [[nodiscard]] int32_t toIndex(hex::OffsetCoord c) const {
        assert(this->isValid(c));
        return c.row * this->m_width + c.col;
    }

    /// Convert axial coordinate to flat array index. Asserts in-bounds.
    [[nodiscard]] int32_t toIndex(hex::AxialCoord a) const {
        return this->toIndex(hex::axialToOffset(a));
    }

    /// Convert flat index back to offset coordinate.
    [[nodiscard]] hex::OffsetCoord toOffset(int32_t index) const {
        assert(index >= 0 && index < this->tileCount());
        return {index % this->m_width, index / this->m_width};
    }

    /// Convert flat index to axial coordinate.
    [[nodiscard]] hex::AxialCoord toAxial(int32_t index) const {
        return hex::offsetToAxial(this->toOffset(index));
    }

    // ========================================================================
    // Tile property access (SoA arrays)
    // All accessors assert index is in [0, tileCount).
    // ========================================================================

    // -- Terrain --
    [[nodiscard]] TerrainType terrain(int32_t index) const { this->assertIndex(index); return this->m_terrain[static_cast<std::size_t>(index)]; }
    void setTerrain(int32_t index, TerrainType type) { this->assertIndex(index); this->m_terrain[static_cast<std::size_t>(index)] = type; }

    // -- Feature --
    [[nodiscard]] FeatureType feature(int32_t index) const { this->assertIndex(index); return this->m_feature[static_cast<std::size_t>(index)]; }
    void setFeature(int32_t index, FeatureType type) { this->assertIndex(index); this->m_feature[static_cast<std::size_t>(index)] = type; }

    // -- Elevation --
    [[nodiscard]] int8_t elevation(int32_t index) const { this->assertIndex(index); return this->m_elevation[static_cast<std::size_t>(index)]; }
    void setElevation(int32_t index, int8_t elev) { this->assertIndex(index); this->m_elevation[static_cast<std::size_t>(index)] = elev; }

    // -- River edges (6-bit mask, one bit per hex edge) --
    [[nodiscard]] uint8_t riverEdges(int32_t index) const { this->assertIndex(index); return this->m_riverEdges[static_cast<std::size_t>(index)]; }
    void setRiverEdges(int32_t index, uint8_t mask) { this->assertIndex(index); this->m_riverEdges[static_cast<std::size_t>(index)] = mask; }
    [[nodiscard]] bool hasRiverOnEdge(int32_t index, int direction) const {
        this->assertIndex(index);
        assert(direction >= 0 && direction < 6);
        return (this->m_riverEdges[static_cast<std::size_t>(index)] & (1u << direction)) != 0;
    }

    // -- Strategic resource on tile --
    [[nodiscard]] ResourceId resource(int32_t index) const { this->assertIndex(index); return this->m_resource[static_cast<std::size_t>(index)]; }
    void setResource(int32_t index, ResourceId id) { this->assertIndex(index); this->m_resource[static_cast<std::size_t>(index)] = id; }

    // -- Owning player --
    [[nodiscard]] PlayerId owner(int32_t index) const { this->assertIndex(index); return this->m_owner[static_cast<std::size_t>(index)]; }
    void setOwner(int32_t index, PlayerId player) { this->assertIndex(index); this->m_owner[static_cast<std::size_t>(index)] = player; }

    // -- Tile improvement --
    [[nodiscard]] ImprovementType improvement(int32_t index) const { this->assertIndex(index); return this->m_improvement[static_cast<std::size_t>(index)]; }
    void setImprovement(int32_t index, ImprovementType type) {
        this->assertIndex(index);
        this->m_improvement[static_cast<std::size_t>(index)] = type;
        if (type == ImprovementType::Road) {
            this->m_road[static_cast<std::size_t>(index)] = 1;
        }
    }
    [[nodiscard]] bool hasRoad(int32_t index) const { this->assertIndex(index); return this->m_road[static_cast<std::size_t>(index)] != 0; }

    // ========================================================================
    // Computed properties
    // ========================================================================

    /// Get the total yield for a tile (terrain + feature + improvement).
    [[nodiscard]] TileYield tileYield(int32_t index) const {
        TileYield base = baseTerrainYield(this->terrain(index));
        TileYield feat = featureYieldModifier(this->feature(index));
        TileYield imp  = improvementYieldBonus(this->improvement(index));
        return {
            static_cast<int8_t>(base.food + feat.food + imp.food),
            static_cast<int8_t>(base.production + feat.production + imp.production),
            static_cast<int8_t>(base.gold + feat.gold + imp.gold),
            static_cast<int8_t>(base.science + feat.science + imp.science),
            static_cast<int8_t>(base.culture + feat.culture + imp.culture),
            static_cast<int8_t>(base.faith + feat.faith + imp.faith)
        };
    }

    /// Movement cost for a naval unit (0 = impassable water/land for ships).
    [[nodiscard]] int32_t navalMovementCost(int32_t index) const {
        TerrainType t = this->terrain(index);
        if (t == TerrainType::Coast || t == TerrainType::Ocean) {
            return 1;
        }
        return 0;  // Land tiles are impassable for naval units
    }

    /// Movement cost for a land unit (0 = impassable).
    [[nodiscard]] int32_t movementCost(int32_t index) const {
        TerrainType t = this->terrain(index);
        if (isImpassable(t)) {
            return 0;
        }
        if (isWater(t)) {
            return 0;  // Land units cannot enter water (without embarkation)
        }

        int32_t cost = 1;
        FeatureType f = this->feature(index);
        if (f == FeatureType::Forest || f == FeatureType::Jungle || f == FeatureType::Marsh) {
            cost = 2;
        }
        if (f == FeatureType::Hills) {
            cost = 2;
        }

        // Roads reduce movement cost to 1
        if (this->hasRoad(index) && cost > 1) {
            cost = 1;
        }
        return cost;
    }

    // ========================================================================
    // Raw array access (for bulk operations / rendering)
    // ========================================================================

    [[nodiscard]] const TerrainType* terrainData() const { return this->m_terrain.data(); }
    [[nodiscard]] const FeatureType* featureData() const { return this->m_feature.data(); }
    [[nodiscard]] const int8_t*      elevationData() const { return this->m_elevation.data(); }
    [[nodiscard]] const uint8_t*     riverEdgesData() const { return this->m_riverEdges.data(); }

private:
    void assertIndex(int32_t index) const {
        assert(index >= 0 && index < this->tileCount());
    }

    int32_t m_width  = 0;
    int32_t m_height = 0;

    // SoA tile storage -- one entry per tile, indexed by (row * width + col)
    std::vector<TerrainType> m_terrain;
    std::vector<FeatureType> m_feature;
    std::vector<int8_t>      m_elevation;
    std::vector<uint8_t>     m_riverEdges;   ///< 6-bit mask per tile
    std::vector<ResourceId>      m_resource;
    std::vector<PlayerId>        m_owner;
    std::vector<ImprovementType> m_improvement;
    std::vector<uint8_t>         m_road;         ///< 1 if tile has road, 0 otherwise
};

} // namespace aoc::map
