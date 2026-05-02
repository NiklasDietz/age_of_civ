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

#include <array>
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

    /// WP-S: military supply depot. Holds food + fuel buffer; military units
    /// within 5 hex draw from the encampment first, fall back to city stockpile.
    /// Buildable on owned land or neutral land within 8 hex of own city.
    /// Pillage-able. Tech: Engineering.
    Encampment,

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

    // -- WP-C3 stacked infrastructure lanes (PowerPole + Pipeline + Aqueduct) --
    static constexpr uint8_t INFRA_POWER_POLE = 1u << 0;
    static constexpr uint8_t INFRA_PIPELINE   = 1u << 1;
    static constexpr uint8_t INFRA_AQUEDUCT   = 1u << 2;

    [[nodiscard]] bool hasPowerPole(int32_t index) const {
        this->assertIndex(index);
        return (this->m_tileInfra[static_cast<std::size_t>(index)] & INFRA_POWER_POLE) != 0;
    }
    [[nodiscard]] bool hasPipeline(int32_t index) const {
        this->assertIndex(index);
        return (this->m_tileInfra[static_cast<std::size_t>(index)] & INFRA_PIPELINE) != 0;
    }
    [[nodiscard]] bool hasAqueduct(int32_t index) const {
        this->assertIndex(index);
        return (this->m_tileInfra[static_cast<std::size_t>(index)] & INFRA_AQUEDUCT) != 0;
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
    void setAqueduct(int32_t index, bool on) {
        this->assertIndex(index);
        uint8_t& bits = this->m_tileInfra[static_cast<std::size_t>(index)];
        bits = on ? static_cast<uint8_t>(bits | INFRA_AQUEDUCT)
                  : static_cast<uint8_t>(bits & ~INFRA_AQUEDUCT);
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
public:
    /// Tectonic plate id assigned by the Continents map generator.
    /// 0xFF = unset / not applicable (non-Continents map). Used by the
    /// tectonic-overlay UI mode.
    /// Hotspot world positions (normalised 0..1 coords) — set by
    /// MapGenerator on Continents maps. Empty otherwise. Used by the
    /// Hotspots overlay to render dark red dots at mantle plumes.
    [[nodiscard]] const std::vector<std::pair<float, float>>& hotspots() const {
        return this->m_hotspots;
    }
    void setHotspots(std::vector<std::pair<float, float>> hs) {
        this->m_hotspots = std::move(hs);
    }

    /// Per-plate velocity in normalised world units (vx, vy).
    /// Indexed by plate id. Used by the plate-overlay renderer to
    /// classify each boundary as convergent / divergent / transform
    /// based on relative motion of the two adjacent plates.
    [[nodiscard]] const std::vector<std::pair<float, float>>& plateMotions() const {
        return this->m_plateMotion;
    }
    void setPlateMotions(std::vector<std::pair<float, float>> v) {
        this->m_plateMotion = std::move(v);
    }
    /// Per-plate centre position (cx, cy) in normalised coords.
    /// Used to compute the boundary normal direction between two plates.
    [[nodiscard]] const std::vector<std::pair<float, float>>& plateCenters() const {
        return this->m_plateCenter;
    }
    void setPlateCenters(std::vector<std::pair<float, float>> c) {
        this->m_plateCenter = std::move(c);
    }
    /// Per-plate landFraction (0 = oceanic, 1 = continental). Used by
    /// the plate overlay to distinguish ocean-cont subduction from
    /// continental collision when classifying convergent boundaries.
    [[nodiscard]] const std::vector<float>& plateLandFrac() const {
        return this->m_plateLandFrac;
    }
    void setPlateLandFrac(std::vector<float> v) {
        this->m_plateLandFrac = std::move(v);
    }

    [[nodiscard]] uint8_t plateId(int32_t index) const {
        const int32_t total = this->tileCount();
        if (index < 0 || index >= total) { return 0xFFu; }
        if (this->m_plateId.empty()) { return 0xFFu; }
        return this->m_plateId[static_cast<std::size_t>(index)];
    }
    void setPlateId(int32_t index, uint8_t pid) {
        const int32_t total = this->tileCount();
        if (this->m_plateId.empty()) {
            this->m_plateId.assign(static_cast<std::size_t>(total), 0xFFu);
        }
        if (index >= 0 && index < total) {
            this->m_plateId[static_cast<std::size_t>(index)] = pid;
        }
    }

    /// Per-tile crust age in epoch units. 0 = freshly-formed at ridge,
    /// high = old continental craton. Drives the CrustAge overlay (red
    /// young → blue ancient) and matches real-Earth seafloor age maps.
    [[nodiscard]] const std::vector<float>& crustAgeTile() const {
        return this->m_crustAgeTile;
    }
    void setCrustAgeTile(std::vector<float> v) {
        this->m_crustAgeTile = std::move(v);
    }

    /// Per-tile sediment depth in normalised orogeny units. Built up by
    /// erosion of nearby mountains (mass conservation: eroded mass goes
    /// here). Foreland basins, alluvial plains, abyssal fans are all
    /// elevated-sediment tiles.
    [[nodiscard]] const std::vector<float>& sedimentDepth() const {
        return this->m_sedimentDepth;
    }
    void setSedimentDepth(std::vector<float> v) {
        this->m_sedimentDepth = std::move(v);
    }

    /// Per-tile rock type tag (Sedimentary / Igneous / Metamorphic).
    /// Drives mineral resource availability + geological visualisation.
    /// 0 = sedimentary (default), 1 = igneous, 2 = metamorphic, 3 = ophiolite.
    [[nodiscard]] const std::vector<uint8_t>& rockType() const {
        return this->m_rockType;
    }
    void setRockType(std::vector<uint8_t> v) {
        this->m_rockType = std::move(v);
    }

    /// Per-tile margin classification: 0 = interior, 1 = passive margin
    /// (sediment-rich, wide shelf), 2 = active margin (arc/trench, narrow).
    [[nodiscard]] const std::vector<uint8_t>& marginType() const {
        return this->m_marginType;
    }
    void setMarginType(std::vector<uint8_t> v) {
        this->m_marginType = std::move(v);
    }

    /// Per-tile soil fertility 0..1. 0 = barren (laterite/podzol/desert),
    /// 0.5 = average, 1.0 = excellent (chernozem/volcanic/alluvial/loess).
    /// Drives agricultural yield.
    [[nodiscard]] const std::vector<float>& soilFertility() const {
        return this->m_soilFertility;
    }
    void setSoilFertility(std::vector<float> v) {
        this->m_soilFertility = std::move(v);
    }

    /// Per-tile volcanism tag.
    /// 0 = none, 1 = subduction arc volcano, 2 = hotspot volcano,
    /// 3 = LIP / flood basalt province, 4 = continental rift volcanic.
    [[nodiscard]] const std::vector<uint8_t>& volcanism() const {
        return this->m_volcanism;
    }
    void setVolcanism(std::vector<uint8_t> v) {
        this->m_volcanism = std::move(v);
    }

    /// Per-tile seismic hazard. 0 = stable interior, 1 = moderate
    /// (passive margin, intraplate), 2 = high (transform fault),
    /// 3 = severe (convergent / subduction zone).
    [[nodiscard]] const std::vector<uint8_t>& seismicHazard() const {
        return this->m_seismicHazard;
    }
    void setSeismicHazard(std::vector<uint8_t> v) {
        this->m_seismicHazard = std::move(v);
    }

    /// Per-tile permafrost flag. 1 = perma-frozen ground (cold soil
    /// limits agriculture, rare during summer thaw).
    [[nodiscard]] const std::vector<uint8_t>& permafrost() const {
        return this->m_permafrost;
    }
    void setPermafrost(std::vector<uint8_t> v) {
        this->m_permafrost = std::move(v);
    }

    /// Per-tile lake flag — 1 if this water tile is a lake (positive
    /// generation: tectonic rift, endorheic, glacial, volcanic), 0 if
    /// open ocean. Lakes still use TerrainType::ShallowWater so existing
    /// game code treats them as fishable water.
    [[nodiscard]] const std::vector<uint8_t>& lakeFlag() const {
        return this->m_lakeFlag;
    }
    void setLakeFlag(std::vector<uint8_t> v) {
        this->m_lakeFlag = std::move(v);
    }

    /// Per-tile upwelling flag — 1 if coastal water with active deep-
    /// water upwelling (high fishery productivity), 2 if fjord.
    [[nodiscard]] const std::vector<uint8_t>& upwelling() const {
        return this->m_upwelling;
    }
    void setUpwelling(std::vector<uint8_t> v) {
        this->m_upwelling = std::move(v);
    }

    /// Per-tile flag — 1 if owning plate is biogeographically isolated
    /// (never merged + old enough to develop endemic species).
    /// Australia, Madagascar, Antarctica analogues.
    [[nodiscard]] const std::vector<uint8_t>& isolatedRealm() const {
        return this->m_isolatedRealm;
    }
    void setIsolatedRealm(std::vector<uint8_t> v) {
        this->m_isolatedRealm = std::move(v);
    }

    /// Per-tile flag — 1 if shallow water tile sits between two
    /// continental landmasses (potential ice-age land bridge — Beringia,
    /// Sahul shelf, Doggerland).
    [[nodiscard]] const std::vector<uint8_t>& landBridge() const {
        return this->m_landBridge;
    }
    void setLandBridge(std::vector<uint8_t> v) {
        this->m_landBridge = std::move(v);
    }

    /// Per-tile flag — 1 if tile is a glacial refugium (mid-lat near
    /// mountains, high biodiversity, ice-age species refuge).
    [[nodiscard]] const std::vector<uint8_t>& refugium() const {
        return this->m_refugium;
    }
    void setRefugium(std::vector<uint8_t> v) {
        this->m_refugium = std::move(v);
    }

    /// Per-tile climate-hazard bitfield.
    /// Bit 0 = hurricane belt, Bit 1 = tornado alley,
    /// Bit 2 = mid-lat storm track, Bit 3 = jet-stream zone.
    [[nodiscard]] const std::vector<uint8_t>& climateHazard() const {
        return this->m_climateHazard;
    }
    void setClimateHazard(std::vector<uint8_t> v) {
        this->m_climateHazard = std::move(v);
    }

    /// Per-tile glacial-feature tag.
    /// 0 = none, 1 = moraine, 2 = U-shaped valley, 3 = cave system,
    /// 4 = drumlin field, 5 = esker.
    [[nodiscard]] const std::vector<uint8_t>& glacialFeature() const {
        return this->m_glacialFeature;
    }
    void setGlacialFeature(std::vector<uint8_t> v) {
        this->m_glacialFeature = std::move(v);
    }

    /// Per-tile ocean property bitfield.
    /// Bits 0-1 = tidal range (0 micro / 1 meso / 2 macro / 3 mega),
    /// Bits 2-3 = salinity (0 brackish / 1 normal / 2 hypersaline / 3 fresh).
    [[nodiscard]] const std::vector<uint8_t>& oceanZone() const {
        return this->m_oceanZone;
    }
    void setOceanZone(std::vector<uint8_t> v) {
        this->m_oceanZone = std::move(v);
    }

    /// Per-tile cloud-cover proxy 0..1. Computed from moisture × temp;
    /// not used for any game effect, drives a visual overlay only.
    [[nodiscard]] const std::vector<float>& cloudCover() const {
        return this->m_cloudCover;
    }
    void setCloudCover(std::vector<float> v) {
        this->m_cloudCover = std::move(v);
    }

    /// Per-tile flow-direction byte (0-5 hex neighbour index that this
    /// tile drains toward, 0xFF = sink / endorheic). Drives drainage
    /// divides + watershed grouping.
    [[nodiscard]] const std::vector<uint8_t>& flowDir() const {
        return this->m_flowDir;
    }
    void setFlowDir(std::vector<uint8_t> v) {
        this->m_flowDir = std::move(v);
    }

    /// Per-tile natural-hazard bitfield (16-bit).
    /// b0 wildfire, b1 flood, b2 drought, b3 avalanche, b4 landslide,
    /// b5 ash fall, b6 lahar, b7 sinkhole, b8 storm surge, b9 dust storm.
    [[nodiscard]] const std::vector<uint16_t>& naturalHazard() const {
        return this->m_naturalHazard;
    }
    void setNaturalHazard(std::vector<uint16_t> v) {
        this->m_naturalHazard = std::move(v);
    }

    /// Per-tile biome subtype tag.
    /// 0 none, 1 Mediterranean (Cs), 2 cloud forest, 3 temperate
    /// rainforest, 4 mangrove, 5 taiga, 6 alpine tundra, 7 polar
    /// desert, 8 cold desert, 9 steppe, 10 prairie, 11 atoll, 12 kelp
    /// forest, 13 estuary, 14 carbonate platform.
    [[nodiscard]] const std::vector<uint8_t>& biomeSubtype() const {
        return this->m_biomeSubtype;
    }
    void setBiomeSubtype(std::vector<uint8_t> v) {
        this->m_biomeSubtype = std::move(v);
    }

    /// Per-tile marine depth zone.
    /// 0 land, 1 continental shelf (<200 m), 2 slope (200-2000 m),
    /// 3 rise (2000-4000 m), 4 abyssal plain (4000-6000 m),
    /// 5 trench (>6000 m), 6 submarine canyon.
    [[nodiscard]] const std::vector<uint8_t>& marineDepth() const {
        return this->m_marineDepth;
    }
    void setMarineDepth(std::vector<uint8_t> v) {
        this->m_marineDepth = std::move(v);
    }

    /// Per-tile wildlife class.
    /// 0 none, 1 big game (savanna/forest), 2 fur game (boreal/cold),
    /// 3 marine mammals (cold ocean), 4 salmon (cold river+coast),
    /// 5 migratory bird stopover.
    [[nodiscard]] const std::vector<uint8_t>& wildlife() const {
        return this->m_wildlife;
    }
    void setWildlife(std::vector<uint8_t> v) {
        this->m_wildlife = std::move(v);
    }

    /// Per-tile disease bitfield.
    /// b0 malaria, b1 yellow fever, b2 sleeping sickness, b3 typhus,
    /// b4 plague reservoir, b5 cholera-prone delta.
    [[nodiscard]] const std::vector<uint8_t>& disease() const {
        return this->m_disease;
    }
    void setDisease(std::vector<uint8_t> v) {
        this->m_disease = std::move(v);
    }

    /// Per-tile renewable-energy potential 0-255.
    [[nodiscard]] const std::vector<uint8_t>& windEnergy() const {
        return this->m_windEnergy;
    }
    void setWindEnergy(std::vector<uint8_t> v) {
        this->m_windEnergy = std::move(v);
    }
    [[nodiscard]] const std::vector<uint8_t>& solarEnergy() const {
        return this->m_solarEnergy;
    }
    void setSolarEnergy(std::vector<uint8_t> v) {
        this->m_solarEnergy = std::move(v);
    }
    [[nodiscard]] const std::vector<uint8_t>& hydroEnergy() const {
        return this->m_hydroEnergy;
    }
    void setHydroEnergy(std::vector<uint8_t> v) {
        this->m_hydroEnergy = std::move(v);
    }
    [[nodiscard]] const std::vector<uint8_t>& geothermalEnergy() const {
        return this->m_geothermalEnergy;
    }
    void setGeothermalEnergy(std::vector<uint8_t> v) {
        this->m_geothermalEnergy = std::move(v);
    }
    [[nodiscard]] const std::vector<uint8_t>& tidalEnergy() const {
        return this->m_tidalEnergy;
    }
    void setTidalEnergy(std::vector<uint8_t> v) {
        this->m_tidalEnergy = std::move(v);
    }
    [[nodiscard]] const std::vector<uint8_t>& waveEnergy() const {
        return this->m_waveEnergy;
    }
    void setWaveEnergy(std::vector<uint8_t> v) {
        this->m_waveEnergy = std::move(v);
    }

    /// Per-tile atmospheric-extras bitfield.
    /// b0 föhn, b1 katabatic, b2 high pressure cell, b3 polar vortex,
    /// b4 ITCZ migrant, b5 monsoon belt explicit.
    [[nodiscard]] const std::vector<uint8_t>& atmosphericExtras() const {
        return this->m_atmosphericExtras;
    }
    void setAtmosphericExtras(std::vector<uint8_t> v) {
        this->m_atmosphericExtras = std::move(v);
    }

    /// Per-tile hydrological-extras bitfield.
    /// b0 aquifer, b1 spring, b2 crater lake, b3 tarn, b4 thermokarst,
    /// b5 oxbow, b6 salt lake, b7 fresh lake.
    [[nodiscard]] const std::vector<uint8_t>& hydroExtras() const {
        return this->m_hydroExtras;
    }
    void setHydroExtras(std::vector<uint8_t> v) {
        this->m_hydroExtras = std::move(v);
    }

    /// Per-tile event marker.
    /// 0 none, 1 historical eruption, 2 impact crater, 3 supervolcano
    /// caldera, 4 ancient battlefield (game-state), 5 prehistoric site.
    [[nodiscard]] const std::vector<uint8_t>& eventMarker() const {
        return this->m_eventMarker;
    }
    void setEventMarker(std::vector<uint8_t> v) {
        this->m_eventMarker = std::move(v);
    }

    /// Per-tile mountain-pass flag — 1 if saddle between two mountain
    /// massifs (movement chokepoint, low elevation surrounded by high).
    [[nodiscard]] const std::vector<uint8_t>& mountainPass() const {
        return this->m_mountainPass;
    }
    void setMountainPass(std::vector<uint8_t> v) {
        this->m_mountainPass = std::move(v);
    }

    /// Per-tile defensibility score 0-255 (elevation advantage + flank
    /// protection from rivers/water/mountains).
    [[nodiscard]] const std::vector<uint8_t>& defensibility() const {
        return this->m_defensibility;
    }
    void setDefensibility(std::vector<uint8_t> v) {
        this->m_defensibility = std::move(v);
    }

    /// Per-tile domesticable-species bitfield.
    /// b0 cattle/buffalo, b1 horses, b2 sheep, b3 goats, b4 llamas,
    /// b5 camels, b6 reindeer, b7 pigs.
    [[nodiscard]] const std::vector<uint8_t>& domesticable() const {
        return this->m_domesticable;
    }
    void setDomesticable(std::vector<uint8_t> v) {
        this->m_domesticable = std::move(v);
    }

    /// Per-tile trade-route potential 0-255.
    /// High where mountain passes / rivers / coast / flat terrain
    /// connect distant settlements with low effective transport cost.
    [[nodiscard]] const std::vector<uint8_t>& tradeRoutePotential() const {
        return this->m_tradeRoutePotential;
    }
    void setTradeRoutePotential(std::vector<uint8_t> v) {
        this->m_tradeRoutePotential = std::move(v);
    }

    /// Per-tile habitability score 0-255 (composite: fertility + soft
    /// climate + freshwater + low hazard + biome quality). Game uses
    /// for AI city-placement scoring.
    [[nodiscard]] const std::vector<uint8_t>& habitability() const {
        return this->m_habitability;
    }
    void setHabitability(std::vector<uint8_t> v) {
        this->m_habitability = std::move(v);
    }

    /// Per-tile wetland subtype.
    /// 0 none/Marsh-generic, 1 peat bog, 2 swamp, 3 fen, 4 floodplain.
    [[nodiscard]] const std::vector<uint8_t>& wetlandSubtype() const {
        return this->m_wetlandSubtype;
    }
    void setWetlandSubtype(std::vector<uint8_t> v) {
        this->m_wetlandSubtype = std::move(v);
    }

    /// Per-tile coral reef tier.
    /// 0 none, 1 fringing reef, 2 barrier reef, 3 atoll, 4 patch reef.
    [[nodiscard]] const std::vector<uint8_t>& reefTier() const {
        return this->m_reefTier;
    }
    void setReefTier(std::vector<uint8_t> v) {
        this->m_reefTier = std::move(v);
    }

    // ----- Dynamic per-tile game state (post-generation) -----
    // These fields track world changes during play. Map-gen leaves
    // them zero; gameplay systems read/write them.

    /// Per-tile war-damage value 0-255 (pillage / scorched-earth /
    /// fallout intensity). 0 = pristine, 255 = totally devastated.
    /// Gameplay systems decay this slowly each turn.
    [[nodiscard]] uint8_t warDamage(int32_t index) const {
        if (this->m_warDamage.empty()) { return 0; }
        if (index < 0 || index >= this->tileCount()) { return 0; }
        return this->m_warDamage[static_cast<std::size_t>(index)];
    }
    void setWarDamage(int32_t index, uint8_t v) {
        const int32_t total = this->tileCount();
        if (this->m_warDamage.empty()) {
            this->m_warDamage.assign(static_cast<std::size_t>(total), 0);
        }
        if (index >= 0 && index < total) {
            this->m_warDamage[static_cast<std::size_t>(index)] = v;
        }
    }
    [[nodiscard]] const std::vector<uint8_t>& warDamageAll() const {
        return this->m_warDamage;
    }

    /// Per-tile anthropogenic-modification level 0-255.
    /// Low = wild, mid = farmed/forest cleared, high = urban / heavy
    /// industrialization. Updated by improvement build / city growth.
    [[nodiscard]] uint8_t anthropogenic(int32_t index) const {
        if (this->m_anthropogenic.empty()) { return 0; }
        if (index < 0 || index >= this->tileCount()) { return 0; }
        return this->m_anthropogenic[static_cast<std::size_t>(index)];
    }
    void setAnthropogenic(int32_t index, uint8_t v) {
        const int32_t total = this->tileCount();
        if (this->m_anthropogenic.empty()) {
            this->m_anthropogenic.assign(static_cast<std::size_t>(total), 0);
        }
        if (index >= 0 && index < total) {
            this->m_anthropogenic[static_cast<std::size_t>(index)] = v;
        }
    }
    [[nodiscard]] const std::vector<uint8_t>& anthropogenicAll() const {
        return this->m_anthropogenic;
    }

    /// Per-tile abandoned-settlement marker. 1 = ancient ruins from a
    /// formerly-active city that fell. Decays slowly (centuries) back
    /// to 0 as nature reclaims.
    [[nodiscard]] uint8_t settlementRuin(int32_t index) const {
        if (this->m_settlementRuin.empty()) { return 0; }
        if (index < 0 || index >= this->tileCount()) { return 0; }
        return this->m_settlementRuin[static_cast<std::size_t>(index)];
    }
    void setSettlementRuin(int32_t index, uint8_t v) {
        const int32_t total = this->tileCount();
        if (this->m_settlementRuin.empty()) {
            this->m_settlementRuin.assign(static_cast<std::size_t>(total), 0);
        }
        if (index >= 0 && index < total) {
            this->m_settlementRuin[static_cast<std::size_t>(index)] = v;
        }
    }
    [[nodiscard]] const std::vector<uint8_t>& settlementRuinAll() const {
        return this->m_settlementRuin;
    }

    /// Per-tile active trade-route flag. Bitfield where each bit
    /// represents a trade-route lane (limited to 8 simultaneous routes
    /// passing through any one tile). Game systems set/clear bits as
    /// trade routes form / collapse.
    [[nodiscard]] uint8_t activeTradeRoute(int32_t index) const {
        if (this->m_activeTradeRoute.empty()) { return 0; }
        if (index < 0 || index >= this->tileCount()) { return 0; }
        return this->m_activeTradeRoute[static_cast<std::size_t>(index)];
    }
    void setActiveTradeRoute(int32_t index, uint8_t v) {
        const int32_t total = this->tileCount();
        if (this->m_activeTradeRoute.empty()) {
            this->m_activeTradeRoute.assign(static_cast<std::size_t>(total), 0);
        }
        if (index >= 0 && index < total) {
            this->m_activeTradeRoute[static_cast<std::size_t>(index)] = v;
        }
    }
    [[nodiscard]] const std::vector<uint8_t>& activeTradeRouteAll() const {
        return this->m_activeTradeRoute;
    }

    /// Per-tile Köppen full classification (5 bits = 32 codes).
    /// 0 = unset, 1-30 = Köppen codes (Af, Am, Aw, BWh, BWk, BSh, BSk,
    /// Csa, Csb, Cwa, Cwb, Cfa, Cfb, Dsa, Dsb, Dsc, Dsd, Dwa, Dwb, Dwc,
    /// Dwd, Dfa, Dfb, Dfc, Dfd, ET, EF, alpine, polar-night, hot-summer-
    /// arctic).
    [[nodiscard]] const std::vector<uint8_t>& koppen() const {
        return this->m_koppen;
    }
    void setKoppen(std::vector<uint8_t> v) {
        this->m_koppen = std::move(v);
    }

    /// Per-Mountain-tile structural type.
    /// 0 = none/non-mountain, 1 = folded (Alpine collisional),
    /// 2 = faulted (Basin/Range extensional), 3 = volcanic (arc/hotspot),
    /// 4 = uplifted block (Sierra Nevada), 5 = dome (Black Hills),
    /// 6 = eroded ancient (Appalachian-class).
    [[nodiscard]] const std::vector<uint8_t>& mountainStructure() const {
        return this->m_mountainStructure;
    }
    void setMountainStructure(std::vector<uint8_t> v) {
        this->m_mountainStructure = std::move(v);
    }

    /// Per-tile ore-grade modifier 0-255.
    /// Drives extraction yield: high = bonanza grade (Bingham, Rio
    /// Tinto), low = marginal deposit. Independent of resource type.
    [[nodiscard]] const std::vector<uint8_t>& oreGrade() const {
        return this->m_oreGrade;
    }
    void setOreGrade(std::vector<uint8_t> v) {
        this->m_oreGrade = std::move(v);
    }

    /// Per-tile strait / chokepoint flag — 1 if narrow water passage
    /// connecting two larger ocean basins (Hormuz / Malacca / Bosphorus
    /// / Gibraltar / Bering / Magellan / Hudson).
    [[nodiscard]] const std::vector<uint8_t>& strait() const {
        return this->m_strait;
    }
    void setStrait(std::vector<uint8_t> v) {
        this->m_strait = std::move(v);
    }

    /// Per-tile natural-harbor score 0-255 (coastal indentation +
    /// island protection). High = sheltered deepwater harbor.
    [[nodiscard]] const std::vector<uint8_t>& harborScore() const {
        return this->m_harborScore;
    }
    void setHarborScore(std::vector<uint8_t> v) {
        this->m_harborScore = std::move(v);
    }

    /// Per-river-tile channel pattern.
    /// 0 = none, 1 = straight (steep gradient), 2 = meandering (low
    /// gradient + sediment), 3 = braided (high sediment + variable
    /// flow — glacial outwash), 4 = anastomosing (low-gradient
    /// stable multi-channel — Okavango).
    [[nodiscard]] const std::vector<uint8_t>& channelPattern() const {
        return this->m_channelPattern;
    }
    void setChannelPattern(std::vector<uint8_t> v) {
        this->m_channelPattern = std::move(v);
    }

    /// Per-tile vegetation density 0-255 (% canopy / biomass).
    [[nodiscard]] const std::vector<uint8_t>& vegetationDensity() const {
        return this->m_vegetationDensity;
    }
    void setVegetationDensity(std::vector<uint8_t> v) {
        this->m_vegetationDensity = std::move(v);
    }

    /// Per-tile coastal-feature tag.
    /// 0 = none, 1 = cape / peninsula (protruding land), 2 = bay /
    /// inlet (indented coast), 3 = isthmus (narrow land between two
    /// waters), 4 = headland.
    [[nodiscard]] const std::vector<uint8_t>& coastalFeature() const {
        return this->m_coastalFeature;
    }
    void setCoastalFeature(std::vector<uint8_t> v) {
        this->m_coastalFeature = std::move(v);
    }

    /// Per-tile submarine vent / cold seep flag.
    /// 0 = none, 1 = mid-ocean ridge hydrothermal vent (black smoker),
    /// 2 = continental-margin cold seep (methane-driven), 3 = white
    /// smoker (lower-temp serpentinite).
    [[nodiscard]] const std::vector<uint8_t>& submarineVent() const {
        return this->m_submarineVent;
    }
    void setSubmarineVent(std::vector<uint8_t> v) {
        this->m_submarineVent = std::move(v);
    }

    /// Per-eruption-site VEI (Volcanic Explosivity Index) 0-8 +
    /// magma-type bits in upper nibble (high 4 bits: 0=basaltic,
    /// 1=andesitic, 2=rhyolitic, 3=phreatomagmatic).
    [[nodiscard]] const std::vector<uint8_t>& volcanicProfile() const {
        return this->m_volcanicProfile;
    }
    void setVolcanicProfile(std::vector<uint8_t> v) {
        this->m_volcanicProfile = std::move(v);
    }

    /// Per-tile karst subtype.
    /// 0 = none, 1 = doline (sinkhole), 2 = polje (large flat plain),
    /// 3 = tower karst, 4 = pavement, 5 = dry valley.
    [[nodiscard]] const std::vector<uint8_t>& karstSubtype() const {
        return this->m_karstSubtype;
    }
    void setKarstSubtype(std::vector<uint8_t> v) {
        this->m_karstSubtype = std::move(v);
    }

    /// Per-tile desert subtype.
    /// 0 = none, 1 = erg (sand sea), 2 = reg (gravel/stony desert),
    /// 3 = hammada (rocky bedrock desert), 4 = playa (dry lakebed),
    /// 5 = badlands (dissected sediment).
    [[nodiscard]] const std::vector<uint8_t>& desertSubtype() const {
        return this->m_desertSubtype;
    }
    void setDesertSubtype(std::vector<uint8_t> v) {
        this->m_desertSubtype = std::move(v);
    }

    /// Per-tile mass-wasting type.
    /// 0 = none, 1 = rockfall, 2 = slump, 3 = debris flow, 4 = mudflow,
    /// 5 = solifluction (periglacial), 6 = rock glacier.
    [[nodiscard]] const std::vector<uint8_t>& massWasting() const {
        return this->m_massWasting;
    }
    void setMassWasting(std::vector<uint8_t> v) {
        this->m_massWasting = std::move(v);
    }

    /// Per-tile named regional wind tag.
    /// 0 = none, 1 = Mistral, 2 = Bora, 3 = Sirocco, 4 = Harmattan,
    /// 5 = Chinook / Föhn, 6 = Williwaw, 7 = Khamsin, 8 = Santa Ana.
    [[nodiscard]] const std::vector<uint8_t>& namedWind() const {
        return this->m_namedWind;
    }
    void setNamedWind(std::vector<uint8_t> v) {
        this->m_namedWind = std::move(v);
    }

    /// Per-tile forest age class.
    /// 0 = no forest, 1 = scrub, 2 = secondary regrowth,
    /// 3 = mature, 4 = old growth.
    [[nodiscard]] const std::vector<uint8_t>& forestAgeClass() const {
        return this->m_forestAgeClass;
    }
    void setForestAgeClass(std::vector<uint8_t> v) {
        this->m_forestAgeClass = std::move(v);
    }

    /// Per-tile soil moisture regime.
    /// 0 = unset, 1 = aridic (dry most of year), 2 = xeric (dry summer
    /// only), 3 = ustic (intermittent moisture), 4 = udic (humid),
    /// 5 = aquic (waterlogged), 6 = peraquic (permanently saturated).
    [[nodiscard]] const std::vector<uint8_t>& soilMoistureRegime() const {
        return this->m_soilMoistureRegime;
    }
    void setSoilMoistureRegime(std::vector<uint8_t> v) {
        this->m_soilMoistureRegime = std::move(v);
    }

    /// Per-tile specific lithology (rock type fine-grained).
    /// 0 unset, 1 granite, 2 gabbro, 3 basalt, 4 rhyolite, 5 andesite,
    /// 6 peridotite, 7 sandstone, 8 shale, 9 limestone, 10 dolostone,
    /// 11 chert, 12 conglomerate, 13 schist, 14 gneiss, 15 quartzite,
    /// 16 marble, 17 slate, 18 amphibolite, 19 serpentinite,
    /// 20 evaporite, 21 coal-bearing, 22 chalk, 23 ignimbrite tuff.
    [[nodiscard]] const std::vector<uint8_t>& lithology() const {
        return this->m_lithology;
    }
    void setLithology(std::vector<uint8_t> v) {
        this->m_lithology = std::move(v);
    }

    /// Per-tile USDA soil order (12 orders + alpine + bare).
    /// 0 unset, 1 entisol, 2 inceptisol, 3 mollisol (chernozem),
    /// 4 oxisol, 5 ultisol, 6 alfisol, 7 spodosol (podzol),
    /// 8 aridisol, 9 vertisol, 10 andisol (volcanic), 11 histosol
    /// (peat), 12 gelisol (permafrost), 13 alpine bare rock,
    /// 14 anthrosol.
    [[nodiscard]] const std::vector<uint8_t>& soilOrder() const {
        return this->m_soilOrder;
    }
    void setSoilOrder(std::vector<uint8_t> v) {
        this->m_soilOrder = std::move(v);
    }

    /// Per-tile crustal thickness 0-255 (= 0-100 km depth).
    /// Continental ~30-70 km, oceanic ~5-10 km, orogenic root ~70+.
    [[nodiscard]] const std::vector<uint8_t>& crustalThickness() const {
        return this->m_crustalThickness;
    }
    void setCrustalThickness(std::vector<uint8_t> v) {
        this->m_crustalThickness = std::move(v);
    }

    /// Per-tile geothermal heat-flux gradient 0-255 (mW/m² scaled).
    /// Volcanic / arc / hotspot high; cratonic low; oceanic mid.
    [[nodiscard]] const std::vector<uint8_t>& geothermalGradient() const {
        return this->m_geothermalGradient;
    }
    void setGeothermalGradient(std::vector<uint8_t> v) {
        this->m_geothermalGradient = std::move(v);
    }

    /// Per-tile albedo 0-255 (= 0.00-0.95 reflectivity).
    /// Snow/ice ~0.85, desert ~0.40, forest ~0.10, ocean ~0.07.
    [[nodiscard]] const std::vector<uint8_t>& albedo() const {
        return this->m_albedo;
    }
    void setAlbedo(std::vector<uint8_t> v) {
        this->m_albedo = std::move(v);
    }

    /// Per-tile vegetation type.
    /// 0 none/grass, 1 deciduous broadleaf, 2 evergreen broadleaf
    /// (tropical), 3 evergreen needleleaf (boreal), 4 deciduous
    /// needleleaf (larch), 5 mixed forest, 6 savanna, 7 shrubland,
    /// 8 mangrove forest.
    [[nodiscard]] const std::vector<uint8_t>& vegetationType() const {
        return this->m_vegetationType;
    }
    void setVegetationType(std::vector<uint8_t> v) {
        this->m_vegetationType = std::move(v);
    }

    /// Per-tile atmospheric-river-band flag — 1 if mid-latitude tile
    /// in a concentrated moisture-transport corridor.
    [[nodiscard]] const std::vector<uint8_t>& atmosphericRiver() const {
        return this->m_atmosphericRiver;
    }
    void setAtmosphericRiver(std::vector<uint8_t> v) {
        this->m_atmosphericRiver = std::move(v);
    }

    /// Per-tile tropical cyclone basin classification.
    /// 0 none, 1 N Atlantic, 2 NE Pacific, 3 NW Pacific, 4 N Indian,
    /// 5 SW Indian, 6 SE Indian/Australian, 7 S Pacific.
    [[nodiscard]] const std::vector<uint8_t>& cycloneBasin() const {
        return this->m_cycloneBasin;
    }
    void setCycloneBasin(std::vector<uint8_t> v) {
        this->m_cycloneBasin = std::move(v);
    }

    /// Per-tile sea surface temperature 0-255 (= -2 to +32 °C).
    /// Used for fishery suitability + cyclone formation potential +
    /// climate dynamics.
    [[nodiscard]] const std::vector<uint8_t>& seaSurfaceTemp() const {
        return this->m_seaSurfaceTemp;
    }
    void setSeaSurfaceTemp(std::vector<uint8_t> v) {
        this->m_seaSurfaceTemp = std::move(v);
    }

    /// Per-tile shelf-ice / iceberg-source flag.
    /// 0 none, 1 ice shelf (floating extension of continental ice),
    /// 2 iceberg-spawning zone (calving margin), 3 fast ice (frozen
    /// to coast).
    [[nodiscard]] const std::vector<uint8_t>& iceShelfZone() const {
        return this->m_iceShelfZone;
    }
    void setIceShelfZone(std::vector<uint8_t> v) {
        this->m_iceShelfZone = std::move(v);
    }

    /// Per-tile bedrock lithology — same enum as lithology(); separate
    /// vector lets surface and subsurface differ (sediment cover over
    /// granitic basement = sandstone+shale on top of granite).
    [[nodiscard]] const std::vector<uint8_t>& bedrockLithology() const {
        return this->m_bedrockLithology;
    }
    void setBedrockLithology(std::vector<uint8_t> v) {
        this->m_bedrockLithology = std::move(v);
    }

    /// Per-tile permafrost active-layer thickness 0-255 (= 0-2.55 m).
    /// 0 = no frozen ground, 255 = >2.5 m thaw depth in summer
    /// (warm permafrost). Depth correlates inversely with severity.
    [[nodiscard]] const std::vector<uint8_t>& permafrostDepth() const {
        return this->m_permafrostDepth;
    }
    void setPermafrostDepth(std::vector<uint8_t> v) {
        this->m_permafrostDepth = std::move(v);
    }

    /// Per-tile cliff-coast classification.
    /// 0 = none / beach / passable, 1 = hard rock cliff (sheer drop —
    /// units cannot embark/disembark, ships cannot land here),
    /// 2 = fjord wall (glaciated cliff coast — sheltered deep harbor
    /// but vertical sides), 3 = wave-cut headland (lower cliff, harder
    /// landing but possible at low tide), 4 = barrier ice cliff (polar
    /// ice-shelf wall).
    [[nodiscard]] uint8_t cliffCoast(int32_t index) const {
        if (this->m_cliffCoast.empty()) { return 0; }
        if (index < 0 || index >= this->tileCount()) { return 0; }
        return this->m_cliffCoast[static_cast<std::size_t>(index)];
    }
    [[nodiscard]] const std::vector<uint8_t>& cliffCoastAll() const {
        return this->m_cliffCoast;
    }
    void setCliffCoast(std::vector<uint8_t> v) {
        this->m_cliffCoast = std::move(v);
    }
    /// Helper: is the boundary between THIS tile and a water neighbour
    /// passable (can a unit cross water-land at this edge)?
    [[nodiscard]] bool coastIsPassable(int32_t index) const {
        const uint8_t c = this->cliffCoast(index);
        return (c == 0 || c == 3); // headland passable with penalty
    }

    /// Per-tile coastal landform.
    /// 0 none, 1 sea stack, 2 spit, 3 sandbar/barrier, 4 tombolo,
    /// 5 lagoon, 6 tidal flat, 7 cuspate foreland, 8 hooked spit.
    [[nodiscard]] const std::vector<uint8_t>& coastalLandform() const {
        return this->m_coastalLandform;
    }
    void setCoastalLandform(std::vector<uint8_t> v) {
        this->m_coastalLandform = std::move(v);
    }

    /// Per-tile river regime.
    /// 0 no river, 1 perennial, 2 intermittent (seasonal), 3 ephemeral
    /// (flash-flood wadi), 4 glacier-fed, 5 snow-fed.
    [[nodiscard]] const std::vector<uint8_t>& riverRegime() const {
        return this->m_riverRegime;
    }
    void setRiverRegime(std::vector<uint8_t> v) {
        this->m_riverRegime = std::move(v);
    }

    /// Per-tile arid-erosion landform.
    /// 0 none, 1 mesa, 2 butte, 3 plateau, 4 yardang, 5 hoodoo,
    /// 6 pediment, 7 slot canyon.
    [[nodiscard]] const std::vector<uint8_t>& aridLandform() const {
        return this->m_aridLandform;
    }
    void setAridLandform(std::vector<uint8_t> v) {
        this->m_aridLandform = std::move(v);
    }

    /// Per-tile transform-fault subtype.
    /// 0 not transform, 1 pull-apart basin (transtensional),
    /// 2 restraining bend (transpressional), 3 plain transform.
    [[nodiscard]] const std::vector<uint8_t>& transformFaultType() const {
        return this->m_transformFaultType;
    }
    void setTransformFaultType(std::vector<uint8_t> v) {
        this->m_transformFaultType = std::move(v);
    }

    /// Per-tile lake-effect-snow flag (downwind of large lake in cold
    /// air mass — Great Lakes effect).
    [[nodiscard]] const std::vector<uint8_t>& lakeEffectSnow() const {
        return this->m_lakeEffectSnow;
    }
    void setLakeEffectSnow(std::vector<uint8_t> v) {
        this->m_lakeEffectSnow = std::move(v);
    }

    /// Per-tile drumlin alignment direction (0-5 hex direction of
    /// paleo ice flow). 0xFF = no drumlins.
    [[nodiscard]] const std::vector<uint8_t>& drumlinDirection() const {
        return this->m_drumlinDirection;
    }
    void setDrumlinDirection(std::vector<uint8_t> v) {
        this->m_drumlinDirection = std::move(v);
    }

    /// Per-tile reactivated-suture flag — 1 if tile lies on an
    /// ophiolite suture that has been re-deformed by later collision
    /// (Atlas, Pyrenees, Variscan). Drives potential mineral riches +
    /// seismic anomalies.
    [[nodiscard]] const std::vector<uint8_t>& sutureReactivated() const {
        return this->m_sutureReactivated;
    }
    void setSutureReactivated(std::vector<uint8_t> v) {
        this->m_sutureReactivated = std::move(v);
    }

    /// Per-tile annual-mean solar insolation 0-255 (= 0 to ~400 W/m²).
    /// Function of latitude × axial tilt × altitude (less atmosphere
    /// at altitude → higher TOA flux at surface).
    [[nodiscard]] const std::vector<uint8_t>& solarInsolation() const {
        return this->m_solarInsolation;
    }
    void setSolarInsolation(std::vector<uint8_t> v) {
        this->m_solarInsolation = std::move(v);
    }

    /// Per-tile topographic aspect (0-5 hex direction the slope faces;
    /// 0xFF = flat). Used for solar exposure (S-facing in N hemisphere
    /// = sunny / warmer; opposite in S hemisphere).
    [[nodiscard]] const std::vector<uint8_t>& topographicAspect() const {
        return this->m_topographicAspect;
    }
    void setTopographicAspect(std::vector<uint8_t> v) {
        this->m_topographicAspect = std::move(v);
    }

    /// Per-tile slope angle 0-255 (= 0..90°). Computed from max
    /// elevation difference with hex neighbours.
    [[nodiscard]] const std::vector<uint8_t>& slopeAngle() const {
        return this->m_slopeAngle;
    }
    void setSlopeAngle(std::vector<uint8_t> v) {
        this->m_slopeAngle = std::move(v);
    }

    /// Per-tile ecotone flag — 1 if tile sits on a biome transition
    /// boundary (different terrain / feature on adjacent tiles). High
    /// ecological diversity, edge-effect species.
    [[nodiscard]] const std::vector<uint8_t>& ecotone() const {
        return this->m_ecotone;
    }
    void setEcotone(std::vector<uint8_t> v) {
        this->m_ecotone = std::move(v);
    }

    /// Per-tile pelagic primary productivity 0-255 (chlorophyll proxy).
    /// High in mid-lat upwelling zones, river-mouth nutrient plumes,
    /// continental shelf carbonate platforms. Low in tropical open
    /// ocean (oligotrophic gyres).
    [[nodiscard]] const std::vector<uint8_t>& pelagicProductivity() const {
        return this->m_pelagicProductivity;
    }
    void setPelagicProductivity(std::vector<uint8_t> v) {
        this->m_pelagicProductivity = std::move(v);
    }

    /// Per-tile continental shelf sediment thickness 0-255 (= 0 to
    /// ~10 km, scaled). Hosts hydrocarbon basins. High at passive-
    /// margin, river-fed shelves; low at active margins.
    [[nodiscard]] const std::vector<uint8_t>& shelfSedimentThickness() const {
        return this->m_shelfSedimentThickness;
    }
    void setShelfSedimentThickness(std::vector<uint8_t> v) {
        this->m_shelfSedimentThickness = std::move(v);
    }

    /// Per-tile glacial isostatic rebound rate 0-255 (proxy mm/yr).
    /// High in formerly-ice-loaded high-latitude continental areas
    /// (Scandinavia, Hudson Bay).
    [[nodiscard]] const std::vector<uint8_t>& glacialRebound() const {
        return this->m_glacialRebound;
    }
    void setGlacialRebound(std::vector<uint8_t> v) {
        this->m_glacialRebound = std::move(v);
    }

    /// Per-tile sediment transport direction (0-5 hex direction;
    /// 0xFF = no net transport). Land tiles use flow direction; coastal
    /// water uses longshore drift.
    [[nodiscard]] const std::vector<uint8_t>& sedimentTransportDir() const {
        return this->m_sedimentTransportDir;
    }
    void setSedimentTransportDir(std::vector<uint8_t> v) {
        this->m_sedimentTransportDir = std::move(v);
    }

    /// Per-tile coastal change classification.
    /// 0 = inland or stable, 1 = accreting (gaining land), 2 = eroding
    /// (losing land), 3 = neutral coast.
    [[nodiscard]] const std::vector<uint8_t>& coastalChange() const {
        return this->m_coastalChange;
    }
    void setCoastalChange(std::vector<uint8_t> v) {
        this->m_coastalChange = std::move(v);
    }

    /// Per-river-tile Strahler stream order (1 headwater → 7+ trunk).
    /// 0 if no river. Drives navigability (≥ 4 ocean-going).
    [[nodiscard]] const std::vector<uint8_t>& streamOrder() const {
        return this->m_streamOrder;
    }
    void setStreamOrder(std::vector<uint8_t> v) {
        this->m_streamOrder = std::move(v);
    }
    /// Per-tile river navigability flag — 1 if ≥ Strahler order 4 +
    /// perennial regime + low slope.
    [[nodiscard]] const std::vector<uint8_t>& navigable() const {
        return this->m_navigable;
    }
    void setNavigable(std::vector<uint8_t> v) {
        this->m_navigable = std::move(v);
    }
    /// Per-tile reservoir-dam-site score 0-255 (narrow valley + perennial
    /// + downstream basin).
    [[nodiscard]] const std::vector<uint8_t>& damSite() const {
        return this->m_damSite;
    }
    void setDamSite(std::vector<uint8_t> v) {
        this->m_damSite = std::move(v);
    }
    /// Per-tile riparian buffer flag — 1 if 1-tile band along a river.
    [[nodiscard]] const std::vector<uint8_t>& riparian() const {
        return this->m_riparian;
    }
    void setRiparian(std::vector<uint8_t> v) {
        this->m_riparian = std::move(v);
    }
    /// Per-tile aquifer recharge rate 0-255. Higher in humid climates
    /// + permeable lithology.
    [[nodiscard]] const std::vector<uint8_t>& aquiferRecharge() const {
        return this->m_aquiferRecharge;
    }
    void setAquiferRecharge(std::vector<uint8_t> v) {
        this->m_aquiferRecharge = std::move(v);
    }

    /// Per-tile per-crop suitability scores 0-255. Indexed:
    /// 0 wheat, 1 rice, 2 maize, 3 potato, 4 banana, 5 coffee,
    /// 6 wine grape, 7 cotton.
    [[nodiscard]] const std::vector<uint8_t>& cropSuitability(int32_t crop) const {
        if (crop < 0 || crop >= 8) {
            static const std::vector<uint8_t> empty;
            return empty;
        }
        return this->m_cropSuitability[static_cast<std::size_t>(crop)];
    }
    void setCropSuitability(int32_t crop, std::vector<uint8_t> v) {
        if (crop >= 0 && crop < 8) {
            this->m_cropSuitability[static_cast<std::size_t>(crop)] = std::move(v);
        }
    }
    /// Per-tile pasture suitability 0-255 (Grassland temperate good).
    [[nodiscard]] const std::vector<uint8_t>& pastureScore() const {
        return this->m_pastureScore;
    }
    void setPastureScore(std::vector<uint8_t> v) {
        this->m_pastureScore = std::move(v);
    }
    /// Per-tile forestry sustainable yield 0-255 (Forest density × climate).
    [[nodiscard]] const std::vector<uint8_t>& forestryYield() const {
        return this->m_forestryYield;
    }
    void setForestryYield(std::vector<uint8_t> v) {
        this->m_forestryYield = std::move(v);
    }

    /// Per-Mountain-tile fold axis direction 0-5 (perpendicular to
    /// compression axis). 0xFF if not folded.
    [[nodiscard]] const std::vector<uint8_t>& foldAxis() const {
        return this->m_foldAxis;
    }
    void setFoldAxis(std::vector<uint8_t> v) {
        this->m_foldAxis = std::move(v);
    }
    /// Per-tile metamorphic facies tier.
    /// 0 unmetamorphosed, 1 zeolite, 2 greenschist, 3 amphibolite,
    /// 4 granulite, 5 blueschist, 6 eclogite.
    [[nodiscard]] const std::vector<uint8_t>& metamorphicFacies() const {
        return this->m_metamorphicFacies;
    }
    void setMetamorphicFacies(std::vector<uint8_t> v) {
        this->m_metamorphicFacies = std::move(v);
    }
    /// Per-tile plate stress proxy 0-255 (sampled from owning plate's
    /// orogenyLocal grid at this tile's plate-local coords).
    [[nodiscard]] const std::vector<uint8_t>& plateStress() const {
        return this->m_plateStress;
    }
    void setPlateStress(std::vector<uint8_t> v) {
        this->m_plateStress = std::move(v);
    }

    /// Per-cyclone-tile Saffir-Simpson intensity 1-5 (0 if not cyclone).
    [[nodiscard]] const std::vector<uint8_t>& cycloneIntensity() const {
        return this->m_cycloneIntensity;
    }
    void setCycloneIntensity(std::vector<uint8_t> v) {
        this->m_cycloneIntensity = std::move(v);
    }
    /// Per-tile drought severity tier 0-4.
    [[nodiscard]] const std::vector<uint8_t>& droughtSeverity() const {
        return this->m_droughtSeverity;
    }
    void setDroughtSeverity(std::vector<uint8_t> v) {
        this->m_droughtSeverity = std::move(v);
    }
    /// Per-tile storm wave height 0-255 (storm-track ocean tiles).
    [[nodiscard]] const std::vector<uint8_t>& stormWaveHeight() const {
        return this->m_stormWaveHeight;
    }
    void setStormWaveHeight(std::vector<uint8_t> v) {
        this->m_stormWaveHeight = std::move(v);
    }
    /// Per-tile snow-line flag — 1 if elevation above current snow line
    /// for this latitude.
    [[nodiscard]] const std::vector<uint8_t>& snowLine() const {
        return this->m_snowLine;
    }
    void setSnowLine(std::vector<uint8_t> v) {
        this->m_snowLine = std::move(v);
    }

    /// Per-tile habitat fragmentation index 0-255 (lower = continuous
    /// habitat, higher = fragmented).
    [[nodiscard]] const std::vector<uint8_t>& habitatFragmentation() const {
        return this->m_habitatFragmentation;
    }
    void setHabitatFragmentation(std::vector<uint8_t> v) {
        this->m_habitatFragmentation = std::move(v);
    }
    /// Per-tile endemism index 0-255 (replaces binary isolatedRealm
    /// with a graded score).
    [[nodiscard]] const std::vector<uint8_t>& endemismIndex() const {
        return this->m_endemismIndex;
    }
    void setEndemismIndex(std::vector<uint8_t> v) {
        this->m_endemismIndex = std::move(v);
    }
    /// Per-tile species richness proxy 0-255.
    [[nodiscard]] const std::vector<uint8_t>& speciesRichness() const {
        return this->m_speciesRichness;
    }
    void setSpeciesRichness(std::vector<uint8_t> v) {
        this->m_speciesRichness = std::move(v);
    }

    /// Per-tile net primary productivity 0-255 (composite biomass-
    /// production capacity from temperature × moisture × growing
    /// season). Drives ag yield, fishery, forestry capacity.
    [[nodiscard]] const std::vector<uint8_t>& netPrimaryProductivity() const {
        return this->m_netPrimaryProductivity;
    }
    void setNetPrimaryProductivity(std::vector<uint8_t> v) {
        this->m_netPrimaryProductivity = std::move(v);
    }
    /// Per-tile growing season length 0-255 (= 0-365 frost-free days).
    [[nodiscard]] const std::vector<uint8_t>& growingSeasonDays() const {
        return this->m_growingSeasonDays;
    }
    void setGrowingSeasonDays(std::vector<uint8_t> v) {
        this->m_growingSeasonDays = std::move(v);
    }
    /// Per-tile frost days per year 0-255 (= 0-365 frost days).
    [[nodiscard]] const std::vector<uint8_t>& frostDays() const {
        return this->m_frostDays;
    }
    void setFrostDays(std::vector<uint8_t> v) {
        this->m_frostDays = std::move(v);
    }
    /// Per-tile carrying-capacity proxy 0-255 (max sustainable population
    /// composite: NPP + freshwater + climate hospitability).
    [[nodiscard]] const std::vector<uint8_t>& carryingCapacity() const {
        return this->m_carryingCapacity;
    }
    void setCarryingCapacity(std::vector<uint8_t> v) {
        this->m_carryingCapacity = std::move(v);
    }

    /// Per-tile soil texture fractions 0-255 (sums to ~255 across the
    /// three components). Drives drainage, nutrient retention, tilth.
    [[nodiscard]] const std::vector<uint8_t>& soilClayPct() const {
        return this->m_soilClayPct;
    }
    void setSoilClayPct(std::vector<uint8_t> v) {
        this->m_soilClayPct = std::move(v);
    }
    [[nodiscard]] const std::vector<uint8_t>& soilSiltPct() const {
        return this->m_soilSiltPct;
    }
    void setSoilSiltPct(std::vector<uint8_t> v) {
        this->m_soilSiltPct = std::move(v);
    }
    [[nodiscard]] const std::vector<uint8_t>& soilSandPct() const {
        return this->m_soilSandPct;
    }
    void setSoilSandPct(std::vector<uint8_t> v) {
        this->m_soilSandPct = std::move(v);
    }

    /// Per-tile annual temperature range (seasonal swing) 0-255
    /// (= 0-50 °C amplitude). Drives continentality realism.
    [[nodiscard]] const std::vector<uint8_t>& seasonalTempRange() const {
        return this->m_seasonalTempRange;
    }
    void setSeasonalTempRange(std::vector<uint8_t> v) {
        this->m_seasonalTempRange = std::move(v);
    }
    /// Per-tile diurnal temperature range 0-255 (day-night swing,
    /// = 0-25 °C). High in deserts, low in marine climates.
    [[nodiscard]] const std::vector<uint8_t>& diurnalTempRange() const {
        return this->m_diurnalTempRange;
    }
    void setDiurnalTempRange(std::vector<uint8_t> v) {
        this->m_diurnalTempRange = std::move(v);
    }

    /// Per-tile UV-index proxy 0-255 (lat + altitude + ozone).
    [[nodiscard]] const std::vector<uint8_t>& uvIndex() const {
        return this->m_uvIndex;
    }
    void setUvIndex(std::vector<uint8_t> v) {
        this->m_uvIndex = std::move(v);
    }

    /// Per-tile coral bleaching risk 0-255 (high SST + low pH proxy).
    [[nodiscard]] const std::vector<uint8_t>& coralBleachRisk() const {
        return this->m_coralBleachRisk;
    }
    void setCoralBleachRisk(std::vector<uint8_t> v) {
        this->m_coralBleachRisk = std::move(v);
    }

    /// Per-ocean-tile magnetic anomaly stripe id 0-255. Banded oceanic
    /// crust ages in stripes parallel to ridge axis (Vine-Matthews).
    [[nodiscard]] const std::vector<uint8_t>& magneticAnomaly() const {
        return this->m_magneticAnomaly;
    }
    void setMagneticAnomaly(std::vector<uint8_t> v) {
        this->m_magneticAnomaly = std::move(v);
    }

    /// Per-tile heat flow refined 0-255 (refined per-tile mW/m² blending
    /// crust age + lithology + volcanism).
    [[nodiscard]] const std::vector<uint8_t>& heatFlow() const {
        return this->m_heatFlow;
    }
    void setHeatFlow(std::vector<uint8_t> v) {
        this->m_heatFlow = std::move(v);
    }

    /// Per-tile volcano return period 0-255 (= 0-1000 yr scaled).
    /// Lower = more frequent eruptions.
    [[nodiscard]] const std::vector<uint8_t>& volcanoReturnPeriod() const {
        return this->m_volcanoReturnPeriod;
    }
    void setVolcanoReturnPeriod(std::vector<uint8_t> v) {
        this->m_volcanoReturnPeriod = std::move(v);
    }
    /// Per-coastal-tile tsunami runup elevation 0-255 (= 0-30 m).
    /// 0 if not tsunami-prone.
    [[nodiscard]] const std::vector<uint8_t>& tsunamiRunup() const {
        return this->m_tsunamiRunup;
    }
    void setTsunamiRunup(std::vector<uint8_t> v) {
        this->m_tsunamiRunup = std::move(v);
    }

    /// Per-tile Topographic Position Index.
    /// 0 flat, 1 valley/depression, 2 slope, 3 ridge/hilltop.
    [[nodiscard]] const std::vector<uint8_t>& topoPositionIndex() const {
        return this->m_topoPositionIndex;
    }
    void setTopoPositionIndex(std::vector<uint8_t> v) {
        this->m_topoPositionIndex = std::move(v);
    }
    /// Per-tile Topographic Wetness Index 0-255 (water accumulation
    /// potential — drainage convergence × low slope).
    [[nodiscard]] const std::vector<uint8_t>& topoWetnessIndex() const {
        return this->m_topoWetnessIndex;
    }
    void setTopoWetnessIndex(std::vector<uint8_t> v) {
        this->m_topoWetnessIndex = std::move(v);
    }
    /// Per-tile effective relief / roughness 0-255 (max-min elev in
    /// 3-hex neighbourhood).
    [[nodiscard]] const std::vector<uint8_t>& roughness() const {
        return this->m_roughness;
    }
    void setRoughness(std::vector<uint8_t> v) {
        this->m_roughness = std::move(v);
    }
    /// Per-tile topographic curvature.
    /// 0 flat, 1 concave (bowl, accumulation), 2 convex (ridge, divergence).
    [[nodiscard]] const std::vector<uint8_t>& curvature() const {
        return this->m_curvature;
    }
    void setCurvature(std::vector<uint8_t> v) {
        this->m_curvature = std::move(v);
    }

    /// Per-river-tile discharge estimate 0-255 (m³/s scaled).
    /// Computed from drainage-area accumulation × precipitation proxy.
    [[nodiscard]] const std::vector<uint8_t>& riverDischarge() const {
        return this->m_riverDischarge;
    }
    void setRiverDischarge(std::vector<uint8_t> v) {
        this->m_riverDischarge = std::move(v);
    }
    /// Per-tile accumulated drainage basin area 0-255 (upstream tile
    /// count, log-scaled).
    [[nodiscard]] const std::vector<uint8_t>& drainageBasinArea() const {
        return this->m_drainageBasinArea;
    }
    void setDrainageBasinArea(std::vector<uint8_t> v) {
        this->m_drainageBasinArea = std::move(v);
    }
    /// Per-tile watershed id 0-255 (tile cluster sharing common
    /// drainage destination — Mississippi basin / Amazon basin / etc).
    [[nodiscard]] const std::vector<uint8_t>& watershedId() const {
        return this->m_watershedId;
    }
    void setWatershedId(std::vector<uint8_t> v) {
        this->m_watershedId = std::move(v);
    }

    /// Per-tile per-livestock suitability 0-255. Indexed:
    /// 0 cattle, 1 pig, 2 sheep, 3 horse, 4 goat, 5 chicken.
    [[nodiscard]] const std::vector<uint8_t>& livestockSuit(int32_t species) const {
        if (species < 0 || species >= 6) {
            static const std::vector<uint8_t> empty;
            return empty;
        }
        return this->m_livestockSuit[static_cast<std::size_t>(species)];
    }
    void setLivestockSuit(int32_t species, std::vector<uint8_t> v) {
        if (species >= 0 && species < 6) {
            this->m_livestockSuit[static_cast<std::size_t>(species)] = std::move(v);
        }
    }

    /// Per-tile fault-trace flag.
    /// 0 none, 1 active fault (current convergent/transform/divergent),
    /// 2 inactive fossil fault scar.
    [[nodiscard]] const std::vector<uint8_t>& faultTrace() const {
        return this->m_faultTrace;
    }
    void setFaultTrace(std::vector<uint8_t> v) {
        this->m_faultTrace = std::move(v);
    }
    /// Per-coastal-tile reef terrace level 0-255 (Quaternary glacio-
    /// eustatic step, higher = older / higher elevation paleoshore).
    [[nodiscard]] const std::vector<uint8_t>& reefTerrace() const {
        return this->m_reefTerrace;
    }
    void setReefTerrace(std::vector<uint8_t> v) {
        this->m_reefTerrace = std::move(v);
    }

    /// Per-tile mine suitability bitfield.
    /// b0 open-pit (low slope + shallow deposit),
    /// b1 underground (deep deposit + stable rock).
    [[nodiscard]] const std::vector<uint8_t>& mineSuitability() const {
        return this->m_mineSuitability;
    }
    void setMineSuitability(std::vector<uint8_t> v) {
        this->m_mineSuitability = std::move(v);
    }
    /// Per-tile coal seam thickness 0-255 (= 0-25 m scaled). 0 if no
    /// coal-bearing lithology.
    [[nodiscard]] const std::vector<uint8_t>& coalSeamThickness() const {
        return this->m_coalSeamThickness;
    }
    void setCoalSeamThickness(std::vector<uint8_t> v) {
        this->m_coalSeamThickness = std::move(v);
    }

    /// Per-tile soil pH 0-255 (= pH 4.0..9.0 scaled). 0 = unset, 100
    /// ≈ pH 5.5 acid, 130 ≈ pH 6.5 neutral, 180 ≈ pH 8.0 alkaline.
    [[nodiscard]] const std::vector<uint8_t>& soilPh() const {
        return this->m_soilPh;
    }
    void setSoilPh(std::vector<uint8_t> v) {
        this->m_soilPh = std::move(v);
    }

    /// Per-lake-tile ice-cover duration 0-255 (months scaled,
    /// 0-12 months / year).
    [[nodiscard]] const std::vector<uint8_t>& iceCoverDuration() const {
        return this->m_iceCoverDuration;
    }
    void setIceCoverDuration(std::vector<uint8_t> v) {
        this->m_iceCoverDuration = std::move(v);
    }

    /// Per-tile hydropower reservoir capacity 0-255 (composite of
    /// dam-site score × river discharge × upstream basin area).
    [[nodiscard]] const std::vector<uint8_t>& hydropowerCapacity() const {
        return this->m_hydropowerCapacity;
    }
    void setHydropowerCapacity(std::vector<uint8_t> v) {
        this->m_hydropowerCapacity = std::move(v);
    }

    /// Per-tile potential evapotranspiration 0-255 (Hargreaves proxy:
    /// insolation × temperature). Higher in hot sunny zones.
    [[nodiscard]] const std::vector<uint8_t>& petIndex() const {
        return this->m_petIndex;
    }
    void setPetIndex(std::vector<uint8_t> v) {
        this->m_petIndex = std::move(v);
    }
    /// Per-tile aridity index 0-255 (PET / precipitation ratio).
    /// 0 hyper-humid, 255 hyper-arid.
    [[nodiscard]] const std::vector<uint8_t>& aridityIndex() const {
        return this->m_aridityIndex;
    }
    void setAridityIndex(std::vector<uint8_t> v) {
        this->m_aridityIndex = std::move(v);
    }
    /// Per-tile erosion potential 0-255 (RUSLE-like: slope × rainfall
    /// × soil erodibility). Drives long-term soil loss.
    [[nodiscard]] const std::vector<uint8_t>& erosionPotential() const {
        return this->m_erosionPotential;
    }
    void setErosionPotential(std::vector<uint8_t> v) {
        this->m_erosionPotential = std::move(v);
    }
    /// Per-tile carbon stock 0-255 (vegetation + soil organic carbon).
    /// Tropical jungles + boreal forests + peat bogs hold most.
    [[nodiscard]] const std::vector<uint8_t>& carbonStock() const {
        return this->m_carbonStock;
    }
    void setCarbonStock(std::vector<uint8_t> v) {
        this->m_carbonStock = std::move(v);
    }
    /// Per-tile pristine wilderness flag — 1 if undisturbed (low
    /// anthropogenic + high NPP + high biodiversity).
    [[nodiscard]] const std::vector<uint8_t>& wilderness() const {
        return this->m_wilderness;
    }
    void setWilderness(std::vector<uint8_t> v) {
        this->m_wilderness = std::move(v);
    }
    /// Per-tile floodplain recurrence frequency 0-255.
    /// Higher = more frequent flooding (lower return period).
    /// 0 no flooding, 255 annual flooding.
    [[nodiscard]] const std::vector<uint8_t>& floodFrequency() const {
        return this->m_floodFrequency;
    }
    void setFloodFrequency(std::vector<uint8_t> v) {
        this->m_floodFrequency = std::move(v);
    }
    /// Per-tile forest canopy stratification.
    /// 0 not forest, 1 single-storey scrub, 2 simple canopy,
    /// 3 canopy + understory, 4 emergent + canopy + understory + floor.
    [[nodiscard]] const std::vector<uint8_t>& canopyStratification() const {
        return this->m_canopyStratification;
    }
    void setCanopyStratification(std::vector<uint8_t> v) {
        this->m_canopyStratification = std::move(v);
    }
    /// Per-tile riparian forest extent 0-255 (gallery forest along
    /// rivers in arid zones, intermittent in humid).
    [[nodiscard]] const std::vector<uint8_t>& riparianForest() const {
        return this->m_riparianForest;
    }
    void setRiparianForest(std::vector<uint8_t> v) {
        this->m_riparianForest = std::move(v);
    }
    /// Per-tile magnetic field intensity 0-255.
    /// Latitudinal gradient: high at poles (vertical), low at equator.
    [[nodiscard]] const std::vector<uint8_t>& magneticIntensity() const {
        return this->m_magneticIntensity;
    }
    void setMagneticIntensity(std::vector<uint8_t> v) {
        this->m_magneticIntensity = std::move(v);
    }
    /// Per-tile groundwater depth 0-255 (= 0-50 m below surface).
    /// 0 = at-surface (wetland), 255 = very deep.
    [[nodiscard]] const std::vector<uint8_t>& groundwaterDepth() const {
        return this->m_groundwaterDepth;
    }
    void setGroundwaterDepth(std::vector<uint8_t> v) {
        this->m_groundwaterDepth = std::move(v);
    }
private:
    std::vector<uint8_t>          m_plateId;
    std::vector<std::pair<float, float>> m_hotspots;
    std::vector<std::pair<float, float>> m_plateMotion;
    std::vector<std::pair<float, float>> m_plateCenter;
    std::vector<float>                   m_plateLandFrac;
    std::vector<float>                   m_crustAgeTile;
    std::vector<float>                   m_sedimentDepth;
    std::vector<uint8_t>                 m_rockType;
    std::vector<uint8_t>                 m_marginType;
    std::vector<float>                   m_soilFertility;
    std::vector<uint8_t>                 m_volcanism;
    std::vector<uint8_t>                 m_seismicHazard;
    std::vector<uint8_t>                 m_permafrost;
    std::vector<uint8_t>                 m_lakeFlag;
    std::vector<uint8_t>                 m_upwelling;
    std::vector<uint8_t>                 m_isolatedRealm;
    std::vector<uint8_t>                 m_landBridge;
    std::vector<uint8_t>                 m_refugium;
    std::vector<uint8_t>                 m_climateHazard;
    std::vector<uint8_t>                 m_glacialFeature;
    std::vector<uint8_t>                 m_oceanZone;
    std::vector<float>                   m_cloudCover;
    std::vector<uint8_t>                 m_flowDir;
    std::vector<uint16_t>                m_naturalHazard;
    std::vector<uint8_t>                 m_biomeSubtype;
    std::vector<uint8_t>                 m_marineDepth;
    std::vector<uint8_t>                 m_wildlife;
    std::vector<uint8_t>                 m_disease;
    std::vector<uint8_t>                 m_windEnergy;
    std::vector<uint8_t>                 m_solarEnergy;
    std::vector<uint8_t>                 m_hydroEnergy;
    std::vector<uint8_t>                 m_geothermalEnergy;
    std::vector<uint8_t>                 m_tidalEnergy;
    std::vector<uint8_t>                 m_waveEnergy;
    std::vector<uint8_t>                 m_atmosphericExtras;
    std::vector<uint8_t>                 m_hydroExtras;
    std::vector<uint8_t>                 m_eventMarker;
    std::vector<uint8_t>                 m_mountainPass;
    std::vector<uint8_t>                 m_defensibility;
    std::vector<uint8_t>                 m_domesticable;
    std::vector<uint8_t>                 m_tradeRoutePotential;
    std::vector<uint8_t>                 m_habitability;
    std::vector<uint8_t>                 m_wetlandSubtype;
    std::vector<uint8_t>                 m_reefTier;
    std::vector<uint8_t>                 m_warDamage;
    std::vector<uint8_t>                 m_anthropogenic;
    std::vector<uint8_t>                 m_settlementRuin;
    std::vector<uint8_t>                 m_activeTradeRoute;
    std::vector<uint8_t>                 m_koppen;
    std::vector<uint8_t>                 m_mountainStructure;
    std::vector<uint8_t>                 m_oreGrade;
    std::vector<uint8_t>                 m_strait;
    std::vector<uint8_t>                 m_harborScore;
    std::vector<uint8_t>                 m_channelPattern;
    std::vector<uint8_t>                 m_vegetationDensity;
    std::vector<uint8_t>                 m_coastalFeature;
    std::vector<uint8_t>                 m_submarineVent;
    std::vector<uint8_t>                 m_volcanicProfile;
    std::vector<uint8_t>                 m_karstSubtype;
    std::vector<uint8_t>                 m_desertSubtype;
    std::vector<uint8_t>                 m_massWasting;
    std::vector<uint8_t>                 m_namedWind;
    std::vector<uint8_t>                 m_forestAgeClass;
    std::vector<uint8_t>                 m_soilMoistureRegime;
    std::vector<uint8_t>                 m_lithology;
    std::vector<uint8_t>                 m_soilOrder;
    std::vector<uint8_t>                 m_crustalThickness;
    std::vector<uint8_t>                 m_geothermalGradient;
    std::vector<uint8_t>                 m_albedo;
    std::vector<uint8_t>                 m_vegetationType;
    std::vector<uint8_t>                 m_atmosphericRiver;
    std::vector<uint8_t>                 m_cycloneBasin;
    std::vector<uint8_t>                 m_seaSurfaceTemp;
    std::vector<uint8_t>                 m_iceShelfZone;
    std::vector<uint8_t>                 m_bedrockLithology;
    std::vector<uint8_t>                 m_permafrostDepth;
    std::vector<uint8_t>                 m_cliffCoast;
    std::vector<uint8_t>                 m_coastalLandform;
    std::vector<uint8_t>                 m_riverRegime;
    std::vector<uint8_t>                 m_aridLandform;
    std::vector<uint8_t>                 m_transformFaultType;
    std::vector<uint8_t>                 m_lakeEffectSnow;
    std::vector<uint8_t>                 m_drumlinDirection;
    std::vector<uint8_t>                 m_sutureReactivated;
    std::vector<uint8_t>                 m_solarInsolation;
    std::vector<uint8_t>                 m_topographicAspect;
    std::vector<uint8_t>                 m_slopeAngle;
    std::vector<uint8_t>                 m_ecotone;
    std::vector<uint8_t>                 m_pelagicProductivity;
    std::vector<uint8_t>                 m_shelfSedimentThickness;
    std::vector<uint8_t>                 m_glacialRebound;
    std::vector<uint8_t>                 m_sedimentTransportDir;
    std::vector<uint8_t>                 m_coastalChange;
    std::vector<uint8_t>                 m_streamOrder;
    std::vector<uint8_t>                 m_navigable;
    std::vector<uint8_t>                 m_damSite;
    std::vector<uint8_t>                 m_riparian;
    std::vector<uint8_t>                 m_aquiferRecharge;
    std::array<std::vector<uint8_t>, 8>  m_cropSuitability;
    std::vector<uint8_t>                 m_pastureScore;
    std::vector<uint8_t>                 m_forestryYield;
    std::vector<uint8_t>                 m_foldAxis;
    std::vector<uint8_t>                 m_metamorphicFacies;
    std::vector<uint8_t>                 m_plateStress;
    std::vector<uint8_t>                 m_cycloneIntensity;
    std::vector<uint8_t>                 m_droughtSeverity;
    std::vector<uint8_t>                 m_stormWaveHeight;
    std::vector<uint8_t>                 m_snowLine;
    std::vector<uint8_t>                 m_habitatFragmentation;
    std::vector<uint8_t>                 m_endemismIndex;
    std::vector<uint8_t>                 m_speciesRichness;
    std::vector<uint8_t>                 m_netPrimaryProductivity;
    std::vector<uint8_t>                 m_growingSeasonDays;
    std::vector<uint8_t>                 m_frostDays;
    std::vector<uint8_t>                 m_carryingCapacity;
    std::vector<uint8_t>                 m_soilClayPct;
    std::vector<uint8_t>                 m_soilSiltPct;
    std::vector<uint8_t>                 m_soilSandPct;
    std::vector<uint8_t>                 m_seasonalTempRange;
    std::vector<uint8_t>                 m_diurnalTempRange;
    std::vector<uint8_t>                 m_uvIndex;
    std::vector<uint8_t>                 m_coralBleachRisk;
    std::vector<uint8_t>                 m_magneticAnomaly;
    std::vector<uint8_t>                 m_heatFlow;
    std::vector<uint8_t>                 m_volcanoReturnPeriod;
    std::vector<uint8_t>                 m_tsunamiRunup;
    std::vector<uint8_t>                 m_topoPositionIndex;
    std::vector<uint8_t>                 m_topoWetnessIndex;
    std::vector<uint8_t>                 m_roughness;
    std::vector<uint8_t>                 m_curvature;
    std::vector<uint8_t>                 m_riverDischarge;
    std::vector<uint8_t>                 m_drainageBasinArea;
    std::vector<uint8_t>                 m_watershedId;
    std::array<std::vector<uint8_t>, 6>  m_livestockSuit;
    std::vector<uint8_t>                 m_faultTrace;
    std::vector<uint8_t>                 m_reefTerrace;
    std::vector<uint8_t>                 m_mineSuitability;
    std::vector<uint8_t>                 m_coalSeamThickness;
    std::vector<uint8_t>                 m_soilPh;
    std::vector<uint8_t>                 m_iceCoverDuration;
    std::vector<uint8_t>                 m_hydropowerCapacity;
    std::vector<uint8_t>                 m_petIndex;
    std::vector<uint8_t>                 m_aridityIndex;
    std::vector<uint8_t>                 m_erosionPotential;
    std::vector<uint8_t>                 m_carbonStock;
    std::vector<uint8_t>                 m_wilderness;
    std::vector<uint8_t>                 m_floodFrequency;
    std::vector<uint8_t>                 m_canopyStratification;
    std::vector<uint8_t>                 m_riparianForest;
    std::vector<uint8_t>                 m_magneticIntensity;
    std::vector<uint8_t>                 m_groundwaterDepth;
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
