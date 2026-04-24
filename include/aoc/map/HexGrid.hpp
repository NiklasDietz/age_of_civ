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
#include <unordered_map>
#include <vector>

namespace aoc::map {

/// Map topology: how the grid handles edges.
enum class MapTopology : uint8_t {
    Flat,         ///< Bounded rectangle (original behavior)
    Cylindrical,  ///< East-west wrap, north/south poles are hard boundaries
};

/// Strategic chokepoint type (computed once at map generation).
enum class ChokepointType : uint8_t {
    None,            ///< Not a chokepoint
    LandChokepoint,  ///< Walkable tile with 4+ impassable neighbors
    MountainPass,    ///< Walkable tile surrounded by 3+ mountain tiles
    WaterStrait,     ///< Coast/shallow water between two ocean bodies
    Isthmus,         ///< 1-2 tile wide land bridge between water bodies
};

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
    Canal,      ///< Artificial waterway: ships can traverse. Built via city terrain project.
    MountainMine, ///< Mountain-only metal extraction. Requires mountain tile with a metal
                  ///< resource and at least one adjacent non-mountain tile owned by the
                  ///< builder's player. Does not make the mountain passable for units.

    // -- Yield improvements for science / culture / faith --
    Observatory,  ///< +2 science. Hills only.
    Monastery,    ///< +1 faith, +1 culture. Any land.
    HeritageSite, ///< +2 culture. Any land.

    // -- Modern / industrial improvements (late-era, tech-gated) --
    TerraceFarm,       ///< +1 food on Hills. Early tech (Masonry).
    BiogasPlant,       ///< +2 production, consumes adjacent farm food. Industrial era.
    SolarFarm,         ///< Desert: +1 science, +2 gold. Requires Electricity.
    WindFarm,          ///< Hills/Plains: +2 production. Requires Electricity.
    OffshorePlatform,  ///< Coast + oil resource: +2 production, +2 gold. Requires Plastics.
    RecyclingCenter,   ///< Any land: +2 production, -1 food. Reduces pollution. Late tech.

    // -- Extended modern set --
    GeothermalVent,      ///< Mountain-adjacent flag tile: +1 faith, +1 prod. Enables GeothermalPlant building.
    DesalinationPlant,   ///< Coast tile: +3 food, buffs adjacent desert tiles. Requires Plastics.
    VerticalFarm,        ///< Any owned land: +3 food, -1 prod. Requires Electricity.
    DataCenter,          ///< Any owned land: +3 science, -1 food. Requires Computers.
    TradingPost,         ///< Desert/Plains: +2 gold, bonus gold near foreign border. Classical.
    MangroveNursery,     ///< Marsh or Coast adjacent to Marsh: +1 food +1 culture. Biology.
    KelpFarm,            ///< Coast, no resource: +2 food +1 science. Biology.
    FishFarm,            ///< Coast or ShallowWater, no resource: +2 food +1 gold. Masonry.

    // WP-C4: Greenhouse improvement. Any owned non-water tile. Allows crop
    // growth on off-climate tiles at reduced yield (50% of native-zone yield
    // when the full climate-metadata layer lands). For now: flat +2 food.
    Greenhouse,          ///< Any land: +2 food. Requires Advanced Chemistry (Biology-analog).

    Count
};

class HexGrid {
public:
    HexGrid() = default;

    /**
     * @brief Initialize the grid with the given dimensions.
     *
     * All tiles are set to Ocean with no features or resources.
     * Topology defaults to Flat for backward compatibility.
     */
    void initialize(int32_t width, int32_t height,
                    MapTopology topology = MapTopology::Flat);

    [[nodiscard]] int32_t width() const { return this->m_width; }
    [[nodiscard]] int32_t height() const { return this->m_height; }
    [[nodiscard]] int32_t tileCount() const { return this->m_width * this->m_height; }
    [[nodiscard]] MapTopology topology() const { return this->m_topology; }

    // ========================================================================
    // Coordinate validation and indexing
    // ========================================================================

    /// Wrap an offset coordinate for cylindrical topology.
    /// For Flat topology, returns the coordinate unchanged.
    [[nodiscard]] hex::OffsetCoord wrapOffset(hex::OffsetCoord c) const {
        if (this->m_topology == MapTopology::Cylindrical) {
            c.col = ((c.col % this->m_width) + this->m_width) % this->m_width;
        }
        return c;
    }

    /// Check if an offset coordinate is within bounds.
    /// For Cylindrical topology, columns always wrap (only row bounds matter).
    [[nodiscard]] bool isValid(hex::OffsetCoord c) const {
        if (this->m_topology == MapTopology::Cylindrical) {
            return c.row >= 0 && c.row < this->m_height;
        }
        return c.col >= 0 && c.col < this->m_width
            && c.row >= 0 && c.row < this->m_height;
    }

    /// Check if an axial coordinate is within bounds.
    [[nodiscard]] bool isValid(hex::AxialCoord a) const {
        return this->isValid(hex::axialToOffset(a));
    }

    /// Convert offset coordinate to flat array index. Wraps for Cylindrical.
    [[nodiscard]] int32_t toIndex(hex::OffsetCoord c) const {
        c = this->wrapOffset(c);
        assert(c.col >= 0 && c.col < this->m_width);
        assert(c.row >= 0 && c.row < this->m_height);
        return c.row * this->m_width + c.col;
    }

    /// Convert axial coordinate to flat array index. Wraps for Cylindrical.
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

    /// Distance between two axial coordinates, accounting for wrapping.
    /// Use this instead of hex::distance() for gameplay logic.
    [[nodiscard]] int32_t distance(hex::AxialCoord a, hex::AxialCoord b) const {
        if (this->m_topology == MapTopology::Cylindrical) {
            return hex::wrappedDistance(a, b, this->m_width);
        }
        return hex::distance(a, b);
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
        // Keep the road flag coherent with the current improvement. Previously
        // the setter only raised the flag for road-class improvements and
        // never lowered it, so clearing an improvement (nuke blast, flood
        // destruction, bombing run) left hasRoad() and infrastructureTier()
        // reporting stale road infrastructure on a tile that no longer has any.
        const bool isRoadClass = type == ImprovementType::Road
                              || type == ImprovementType::Railway
                              || type == ImprovementType::Highway;
        this->m_road[static_cast<std::size_t>(index)] = isRoadClass ? 1 : 0;
    }
    /// True if the tile has any road-type infrastructure (road, railway, highway).
    [[nodiscard]] bool hasRoad(int32_t index) const { this->assertIndex(index); return this->m_road[static_cast<std::size_t>(index)] != 0; }

    // -- WP-C4 Greenhouse planted-crop (sparse, keyed by tile index) --
    // Value = good id of the crop the Greenhouse on that tile is planted
    // with. 0xFFFF (default, no entry) = unplanted. Only meaningful when
    // the tile's improvement is Greenhouse.
    [[nodiscard]] uint16_t greenhouseCrop(int32_t index) const {
        this->assertIndex(index);
        auto it = this->m_greenhouseCrop.find(index);
        return (it == this->m_greenhouseCrop.end()) ? uint16_t{0xFFFF} : it->second;
    }
    void setGreenhouseCrop(int32_t index, uint16_t cropId) {
        this->assertIndex(index);
        if (cropId == 0xFFFF) {
            this->m_greenhouseCrop.erase(index);
        } else {
            this->m_greenhouseCrop[index] = cropId;
        }
    }

    // -- WP-C3 stacked infrastructure lanes (PowerPole + Pipeline) --
    static constexpr uint8_t INFRA_POWER_POLE = 1u << 0;
    static constexpr uint8_t INFRA_PIPELINE   = 1u << 1;

    [[nodiscard]] bool hasPowerPole(int32_t index) const {
        this->assertIndex(index);
        return (this->m_tileInfra[static_cast<std::size_t>(index)] & INFRA_POWER_POLE) != 0;
    }
    [[nodiscard]] bool hasPipeline(int32_t index) const {
        this->assertIndex(index);
        return (this->m_tileInfra[static_cast<std::size_t>(index)] & INFRA_PIPELINE) != 0;
    }
    void setPowerPole(int32_t index, bool on) {
        this->assertIndex(index);
        uint8_t& bits = this->m_tileInfra[static_cast<std::size_t>(index)];
        bits = on ? static_cast<uint8_t>(bits | INFRA_POWER_POLE)
                  : static_cast<uint8_t>(bits & ~INFRA_POWER_POLE);
    }
    void setPipeline(int32_t index, bool on) {
        this->assertIndex(index);
        uint8_t& bits = this->m_tileInfra[static_cast<std::size_t>(index)];
        bits = on ? static_cast<uint8_t>(bits | INFRA_PIPELINE)
                  : static_cast<uint8_t>(bits & ~INFRA_PIPELINE);
    }
    [[nodiscard]] uint8_t tileInfraBits(int32_t index) const {
        this->assertIndex(index);
        return this->m_tileInfra[static_cast<std::size_t>(index)];
    }
    void setTileInfraBits(int32_t index, uint8_t bits) {
        this->assertIndex(index);
        this->m_tileInfra[static_cast<std::size_t>(index)] = bits;
    }

    /// True if the tile has a canal improvement (navigable by ships).
    [[nodiscard]] bool hasCanal(int32_t index) const { this->assertIndex(index); return this->m_improvement[static_cast<std::size_t>(index)] == ImprovementType::Canal; }

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

    // -- Chokepoint (computed at map generation, read-only at runtime) --
    [[nodiscard]] ChokepointType chokepoint(int32_t index) const { this->assertIndex(index); return this->m_chokepoint[static_cast<std::size_t>(index)]; }
    void setChokepoint(int32_t index, ChokepointType type) { this->assertIndex(index); this->m_chokepoint[static_cast<std::size_t>(index)] = type; }
    [[nodiscard]] bool isChokepoint(int32_t index) const { return this->chokepoint(index) != ChokepointType::None; }

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
    /// All water tiles and canal tiles cost 1 MP. Other land is impassable.
    [[nodiscard]] int32_t navalMovementCost(int32_t index) const {
        TerrainType t = this->terrain(index);
        if (aoc::map::isWater(t)) {
            return 1;
        }
        if (this->improvement(index) == ImprovementType::Canal) {
            return 1;  // Canal creates a navigable waterway through land
        }
        return 0;  // Land tiles are impassable for naval units
    }

    /// Naval movement cost excluding canals (water-only pathfinding).
    /// Used to find alternative routes that avoid canal tolls.
    [[nodiscard]] int32_t navalMovementCostNoCanals(int32_t index) const {
        return aoc::map::isWater(this->terrain(index)) ? 1 : 0;
    }

    /// Movement cost for early naval units (no Navigation tech).
    /// Can only traverse Coast, ShallowWater, and canals (not deep Ocean).
    [[nodiscard]] int32_t shallowNavalMovementCost(int32_t index) const {
        TerrainType t = this->terrain(index);
        if (aoc::map::isShallowWater(t)) {
            return 1;
        }
        if (this->improvement(index) == ImprovementType::Canal) {
            return 1;  // Canals are always navigable regardless of tech
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
    void assertIndex([[maybe_unused]] int32_t index) const {
        assert(index >= 0 && index < this->tileCount());
    }

    int32_t m_width  = 0;
    int32_t m_height = 0;
    MapTopology m_topology = MapTopology::Flat;

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
    /// WP-C3: per-tile infrastructure lanes that stack with `m_improvement`.
    /// Bit 0 = PowerPole (transmits electricity), Bit 1 = Pipeline (boosts
    /// oil/gas/fuel trade throughput). Kept out of ImprovementType so a tile
    /// can have, e.g., Farm + PowerPole + Pipeline simultaneously.
    std::vector<uint8_t>          m_tileInfra;
    /// WP-C4 Greenhouse planted-crop map. Sparse — only tiles with a
    /// Greenhouse improvement actively populate. Tile index → good id.
    std::unordered_map<int32_t, uint16_t> m_greenhouseCrop;
    std::vector<NaturalWonderType> m_naturalWonder;

    // Strategic chokepoints (computed at map generation)
    std::vector<ChokepointType> m_chokepoint;

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
        // WP-C3: fallout wipes power poles + pipelines too.
        this->m_tileInfra[idx] = 0;
        // WP-C4: and any Greenhouse crop planted on the tile.
        this->m_greenhouseCrop.erase(index);
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
