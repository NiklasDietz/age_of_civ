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
    Railway,    ///< Requires Steel + Coal. 0.5 MP cost, 5x trade capacity.
    Highway,    ///< Requires Plastics + Steel. 0.33 MP cost, 8x trade capacity.
    Dam,        ///< River-only. Prevents flooding, enables hydroelectric.

    // -- Cultivated export improvements (no natural resource required) --
    Vineyard,   ///< Grassland/Plains. Produces wine (luxury). Renewable.
    SilkFarm,   ///< Grassland. Produces silk (luxury). Renewable.
    SpiceFarm,  ///< Plains/Jungle edge. Produces spices (luxury). Renewable.
    DyeWorks,   ///< Plains/Forest edge. Produces dyes (luxury). Renewable.
    CottonField,///< Grassland/Plains. Produces cotton (strategic). Renewable.
    Workshop,   ///< Any land. Produces tools/consumer goods (+2 production, +1 gold).

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

    // -- Resource reserves (how much extractable material remains) --
    /// Remaining reserves for the tile's resource. 0 = exhausted.
    [[nodiscard]] int16_t reserves(int32_t index) const { this->assertIndex(index); return this->m_reserves[static_cast<std::size_t>(index)]; }
    void setReserves(int32_t index, int16_t amount) { this->assertIndex(index); this->m_reserves[static_cast<std::size_t>(index)] = amount; }
    /// Consume 1 unit of reserves. Returns true if reserves remain, false if exhausted.
    bool consumeReserve(int32_t index) {
        this->assertIndex(index);
        int16_t& r = this->m_reserves[static_cast<std::size_t>(index)];
        if (r <= 0) { return false; }
        --r;
        if (r <= 0) {
            // Resource exhausted: remove it from the tile
            this->m_resource[static_cast<std::size_t>(index)] = ResourceId{};
            return false;
        }
        return true;
    }

    // -- Prospect cooldown (turns until this tile can be prospected again) --
    [[nodiscard]] int8_t prospectCooldown(int32_t index) const { this->assertIndex(index); return this->m_prospectCooldown[static_cast<std::size_t>(index)]; }
    void setProspectCooldown(int32_t index, int8_t turns) { this->assertIndex(index); this->m_prospectCooldown[static_cast<std::size_t>(index)] = turns; }
    /// Tick all prospect cooldowns by 1 turn. Call once per turn globally.
    void tickProspectCooldowns() {
        for (std::size_t i = 0; i < this->m_prospectCooldown.size(); ++i) {
            if (this->m_prospectCooldown[i] > 0) { --this->m_prospectCooldown[i]; }
        }
    }

    // -- Owning player --
    [[nodiscard]] PlayerId owner(int32_t index) const { this->assertIndex(index); return this->m_owner[static_cast<std::size_t>(index)]; }
    void setOwner(int32_t index, PlayerId player) { this->assertIndex(index); this->m_owner[static_cast<std::size_t>(index)] = player; }

    // -- Natural wonder --
    [[nodiscard]] NaturalWonderType naturalWonder(int32_t index) const { this->assertIndex(index); return this->m_naturalWonder[static_cast<std::size_t>(index)]; }
    void setNaturalWonder(int32_t index, NaturalWonderType type) { this->assertIndex(index); this->m_naturalWonder[static_cast<std::size_t>(index)] = type; }

    // -- Tile improvement --
    [[nodiscard]] ImprovementType improvement(int32_t index) const { this->assertIndex(index); return this->m_improvement[static_cast<std::size_t>(index)]; }
    void setImprovement(int32_t index, ImprovementType type) {
        this->assertIndex(index);
        this->m_improvement[static_cast<std::size_t>(index)] = type;
        if (type == ImprovementType::Road
            || type == ImprovementType::Railway
            || type == ImprovementType::Highway) {
            this->m_road[static_cast<std::size_t>(index)] = 1;
        }
    }
    /// True if the tile has any road-type infrastructure (road, railway, highway).
    [[nodiscard]] bool hasRoad(int32_t index) const { this->assertIndex(index); return this->m_road[static_cast<std::size_t>(index)] != 0; }

    /// Get the infrastructure tier: 0=none, 1=road, 2=railway, 3=highway.
    [[nodiscard]] int32_t infrastructureTier(int32_t index) const {
        this->assertIndex(index);
        ImprovementType imp = this->m_improvement[static_cast<std::size_t>(index)];
        if (imp == ImprovementType::Highway) { return 3; }
        if (imp == ImprovementType::Railway) { return 2; }
        if (imp == ImprovementType::Road)    { return 1; }
        if (this->m_road[static_cast<std::size_t>(index)] != 0) { return 1; }
        return 0;
    }

    // ========================================================================
    // Computed properties
    // ========================================================================

    /// Get the total yield for a tile (terrain + feature + improvement + natural wonder).
    /// Fallout tiles yield nothing.
    [[nodiscard]] TileYield tileYield(int32_t index) const {
        if (this->feature(index) == FeatureType::Fallout) {
            return {0, 0, 0, 0, 0, 0};
        }
        TileYield base = baseTerrainYield(this->terrain(index));
        TileYield feat = featureYieldModifier(this->feature(index));
        TileYield imp  = improvementYieldBonus(this->improvement(index));
        TileYield nw   = naturalWonderYieldBonus(this->naturalWonder(index));
        return {
            static_cast<int8_t>(base.food + feat.food + imp.food + nw.food),
            static_cast<int8_t>(base.production + feat.production + imp.production + nw.production),
            static_cast<int8_t>(base.gold + feat.gold + imp.gold + nw.gold),
            static_cast<int8_t>(base.science + feat.science + imp.science + nw.science),
            static_cast<int8_t>(base.culture + feat.culture + imp.culture + nw.culture),
            static_cast<int8_t>(base.faith + feat.faith + imp.faith + nw.faith)
        };
    }

    /// Movement cost for a naval unit (0 = impassable for ships).
    /// All water tiles cost 1 MP. Land is impassable.
    [[nodiscard]] int32_t navalMovementCost(int32_t index) const {
        TerrainType t = this->terrain(index);
        if (aoc::map::isWater(t)) {
            return 1;
        }
        return 0;  // Land tiles are impassable for naval units
    }

    /// Movement cost for early naval units (no Navigation tech).
    /// Can only traverse Coast and ShallowWater, not deep Ocean.
    [[nodiscard]] int32_t shallowNavalMovementCost(int32_t index) const {
        TerrainType t = this->terrain(index);
        if (aoc::map::isShallowWater(t)) {
            return 1;
        }
        return 0;  // Deep ocean and land are impassable
    }

    /// Movement cost for a land unit moving FROM fromIndex TO index.
    /// Includes river crossing penalty. Use the single-arg overload when direction is unknown.
    [[nodiscard]] int32_t movementCost(int32_t fromIndex, int32_t index) const {
        int32_t base = this->movementCost(index);
        if (base <= 0) {
            return 0;  // Impassable
        }
        // River crossing penalty: +1 MP
        hex::AxialCoord fromAxial = this->toAxial(fromIndex);
        hex::AxialCoord toAxial = this->toAxial(index);
        std::array<hex::AxialCoord, 6> nbrs = hex::neighbors(fromAxial);
        for (int dir = 0; dir < 6; ++dir) {
            if (nbrs[static_cast<std::size_t>(dir)] == toAxial) {
                if (this->hasRiverOnEdge(fromIndex, dir)) {
                    base += 1;  // River crossing cost
                }
                break;
            }
        }
        return base;
    }

    /// Movement cost for a land unit entering this tile (no directional info).
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

        // Infrastructure reduces movement cost
        int32_t tier = this->infrastructureTier(index);
        if (tier >= 1 && cost > 1) {
            cost = 1;  // Road: all terrain costs 1 MP
        }
        // Railway and Highway are even faster but handled via
        // the 2-arg movementCost overload (fractional MP not supported here)
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
    std::vector<int16_t>         m_reserves;         ///< Extractable units remaining (-1 = infinite/renewable)
    std::vector<int8_t>          m_prospectCooldown; ///< Turns until tile can be prospected again (0 = available)
    std::vector<PlayerId>        m_owner;
    std::vector<ImprovementType>  m_improvement;
    std::vector<uint8_t>          m_road;            ///< 1 if tile has road, 0 otherwise
    std::vector<NaturalWonderType> m_naturalWonder;

    // Nuclear fallout tracking
    std::vector<int16_t>     m_falloutTurns;     ///< Turns of fallout remaining (0 = no fallout)
    std::vector<FeatureType> m_preFalloutFeature; ///< Feature before fallout (restored after decay)

public:
    /// Apply nuclear fallout to a tile for the given duration.
    void applyFallout(int32_t index, int16_t turns) {
        this->assertIndex(index);
        std::size_t idx = static_cast<std::size_t>(index);
        if (this->m_falloutTurns[idx] <= 0) {
            // Save original feature before replacing with Fallout
            this->m_preFalloutFeature[idx] = this->m_feature[idx];
        }
        this->m_feature[idx] = FeatureType::Fallout;
        this->m_falloutTurns[idx] = turns;
        // Destroy improvements in fallout zone
        this->m_improvement[idx] = ImprovementType::None;
        this->m_road[idx] = 0;
    }

    /// Tick fallout decay for all tiles (call once per turn).
    void tickFallout() {
        for (std::size_t i = 0; i < this->m_falloutTurns.size(); ++i) {
            if (this->m_falloutTurns[i] > 0) {
                --this->m_falloutTurns[i];
                if (this->m_falloutTurns[i] <= 0) {
                    // Fallout has decayed -- restore original feature
                    this->m_feature[i] = this->m_preFalloutFeature[i];
                    this->m_preFalloutFeature[i] = FeatureType::None;
                }
            }
        }
    }

    /// Check if a tile has active fallout.
    [[nodiscard]] bool hasFallout(int32_t index) const {
        this->assertIndex(index);
        return this->m_falloutTurns[static_cast<std::size_t>(index)] > 0;
    }
};

} // namespace aoc::map
