#pragma once

/**
 * @file Terrain.hpp
 * @brief Terrain types, features, and their base yields/properties.
 */

#include <array>
#include <cstdint>
#include <string_view>

namespace aoc::map {

// ============================================================================
// Terrain type -- base tile type
// ============================================================================

enum class TerrainType : uint8_t {
    Ocean,        ///< Deep water: requires Navigation tech for ships
    Coast,        ///< Coastal water: adjacent to land, always navigable
    ShallowWater, ///< Open shallow seas: navigable without Navigation tech
    Desert,
    Plains,
    Grassland,
    Tundra,
    Snow,
    Mountain,

    Count
};

static constexpr uint8_t TERRAIN_COUNT = static_cast<uint8_t>(TerrainType::Count);

[[nodiscard]] constexpr std::string_view terrainName(TerrainType type) {
    constexpr std::array<std::string_view, TERRAIN_COUNT> NAMES = {
        {"Ocean", "Coast", "Shallow Water", "Desert", "Plains", "Grassland", "Tundra", "Snow",
         "Mountain"}};
    return NAMES[static_cast<uint8_t>(type)];
}

[[nodiscard]] constexpr bool isWater(TerrainType type) {
    return type == TerrainType::Ocean || type == TerrainType::Coast ||
           type == TerrainType::ShallowWater;
}

/// Deep ocean requires Navigation tech to traverse.
[[nodiscard]] constexpr bool isDeepWater(TerrainType type) {
    return type == TerrainType::Ocean;
}

/// Shallow navigable water (Coast + ShallowWater) - no tech required.
[[nodiscard]] constexpr bool isShallowWater(TerrainType type) {
    return type == TerrainType::Coast || type == TerrainType::ShallowWater;
}

/// Impassable terrain for land units (mountains, deep ocean, shallow water without embarking).
[[nodiscard]] constexpr bool isImpassable(TerrainType type) {
    return type == TerrainType::Mountain || type == TerrainType::Ocean ||
           type == TerrainType::ShallowWater;
}

// ============================================================================
// Feature -- overlay on top of terrain
// ============================================================================

enum class FeatureType : uint8_t {
    None,
    Forest,
    Jungle,
    Marsh,
    Floodplains,
    Oasis,
    Reef,
    Ice,
    Hills,   ///< Elevation feature, combinable with terrain
    Fallout, ///< Nuclear fallout: 0 yields, can't build improvements, temporary

    Count
};

static constexpr uint8_t FEATURE_COUNT = static_cast<uint8_t>(FeatureType::Count);

[[nodiscard]] constexpr std::string_view featureName(FeatureType type) {
    constexpr std::array<std::string_view, FEATURE_COUNT> NAMES = {
        {"None", "Forest", "Jungle", "Marsh", "Floodplains", "Oasis", "Reef", "Ice", "Hills",
         "Fallout"}};
    return NAMES[static_cast<uint8_t>(type)];
}

// ============================================================================
// Yields -- base resource production per tile
// ============================================================================

struct TileYield {
    int8_t food       = 0;
    int8_t production = 0;
    int8_t gold       = 0;
    int8_t science    = 0;
    int8_t culture    = 0;
    int8_t faith      = 0;
};

/// Base yields per terrain type (before features/improvements).
[[nodiscard]] constexpr TileYield baseTerrainYield(TerrainType type) {
    switch (type) {
    case TerrainType::Ocean:
        return {1, 0, 0, 0, 0, 0};
    case TerrainType::Coast:
        return {1, 0, 1, 0, 0, 0};
    case TerrainType::ShallowWater:
        return {1, 0, 0, 0, 0, 0};
    case TerrainType::Desert:
        return {0, 0, 0, 0, 0, 0};
    case TerrainType::Plains:
        return {1, 1, 0, 0, 0, 0};
    case TerrainType::Grassland:
        return {2, 0, 0, 0, 0, 0};
    case TerrainType::Tundra:
        return {1, 0, 0, 0, 0, 0};
    case TerrainType::Snow:
        return {0, 0, 0, 0, 0, 0};
    case TerrainType::Mountain:
        return {0, 0, 0, 0, 0, 0};
    default:
        return {};
    }
}

/// Yield modifier from features (added to terrain base).
[[nodiscard]] constexpr TileYield featureYieldModifier(FeatureType type) {
    switch (type) {
    case FeatureType::Forest:
        return {0, 1, 0, 0, 0, 0};
    case FeatureType::Jungle:
        return {1, 0, 0, 0, 0, 0};
    case FeatureType::Marsh:
        return {1, 0, 0, 0, 0, 0};
    case FeatureType::Floodplains:
        return {3, 0, 0, 0, 0, 0};
    case FeatureType::Oasis:
        return {3, 0, 1, 0, 0, 0};
    case FeatureType::Reef:
        return {1, 0, 1, 0, 0, 0};
    case FeatureType::Hills:
        return {0, 1, 0, 0, 0, 0};
    default:
        return {};
    }
}

// ============================================================================
// Terrain rendering colors (RGBA, premultiplied)
// ============================================================================

struct TerrainColor {
    float r, g, b;
};

// Terrain palette, retuned after the sRGB fix.
//
// These values were originally eyeballed against a renderer that never
// linearised colour, so everything reached the screen washed out and the
// constants were pushed to compensate. With the transfer function correct they
// arrive at full strength, and the old values read as a saturated colour-coded
// diagram rather than a map. Chroma is pulled down across the board toward a
// natural-atlas feel that also stops the map fighting the UI chrome.
[[nodiscard]] constexpr TerrainColor terrainColor(TerrainType type) {
    switch (type) {
    // Ocean covers most of a typical view, so it sets the mood for the
    // whole screen. Deep, desaturated slate-teal rather than the previous
    // saturated navy (#0F2980), which vibrated against the land.
    case TerrainType::Ocean:
        return {0.086f, 0.196f, 0.290f}; // #16324A abyssal
    case TerrainType::Coast:
        return {0.243f, 0.478f, 0.620f}; // #3E7A9E shelf
    case TerrainType::ShallowWater:
        return {0.243f, 0.478f, 0.620f}; // #3E7A9E shelf
    case TerrainType::Desert:
        return {0.780f, 0.694f, 0.514f}; // #C7B183 sand
    case TerrainType::Plains:
        return {0.604f, 0.643f, 0.373f}; // #9AA45F olive
    case TerrainType::Grassland:
        return {0.427f, 0.604f, 0.306f}; // #6D9A4E meadow
    case TerrainType::Tundra:
        return {0.557f, 0.588f, 0.537f}; // #8E9689 lichen
    case TerrainType::Snow:
        return {0.867f, 0.890f, 0.902f}; // #DDE3E6 snow
    case TerrainType::Mountain:
        return {0.420f, 0.392f, 0.349f}; // #6B6459 rock
    default:
        return {0.500f, 0.500f, 0.500f};
    }
}

/// Additive tint applied on top of the terrain colour for features.
/// Deltas are deliberately small: they must READ as a variation of the
/// underlying terrain, not replace it.
[[nodiscard]] constexpr TerrainColor featureColorTint(FeatureType type) {
    switch (type) {
    case FeatureType::Forest:
        return {-0.060f, 0.050f, -0.050f};
    // Jungle: darker and denser than forest. The old {-0.20,+0.20,-0.25}
    // swung so far it produced a neon green unrelated to its terrain.
    case FeatureType::Jungle:
        return {-0.120f, 0.060f, -0.140f};
    case FeatureType::Marsh:
        return {-0.050f, -0.030f, 0.040f};
    case FeatureType::Floodplains:
        return {0.020f, 0.060f, -0.030f};
    case FeatureType::Oasis:
        return {0.020f, 0.090f, 0.010f};
    case FeatureType::Reef:
        return {0.040f, 0.090f, 0.060f};
    case FeatureType::Ice:
        return {0.160f, 0.160f, 0.180f};
    // Hills: a warm shading cue, not a hue change. The old
    // {+0.20,-0.10,-0.18} turned Plains (#A6B359) into #D99A2B -- the
    // bright orange that dominated the map.
    case FeatureType::Hills:
        return {0.080f, -0.030f, -0.080f};
    default:
        return {0.000f, 0.000f, 0.000f};
    }
}

// ============================================================================
// Natural wonders
// ============================================================================

enum class NaturalWonderType : uint8_t {
    None,
    MountainOfGods,   ///< +2 faith, +1 culture
    GrandCanyon,      ///< +1 science, +2 gold
    GreatBarrierReef, ///< +2 food, +2 science (coast)
    KillerVolcano,    ///< +2 production, +1 science
    SacredForest,     ///< +2 faith, +2 food
    CrystalCave,      ///< +3 gold
    Count
};

static constexpr uint8_t NATURAL_WONDER_COUNT = static_cast<uint8_t>(NaturalWonderType::Count);

[[nodiscard]] constexpr std::string_view naturalWonderName(NaturalWonderType type) {
    constexpr std::array<std::string_view, NATURAL_WONDER_COUNT> NAMES = {
        {"None", "Mountain of Gods", "Grand Canyon", "Great Barrier Reef", "Killer Volcano",
         "Sacred Forest", "Crystal Cave"}};
    return NAMES[static_cast<uint8_t>(type)];
}

/// Yield bonus granted by a natural wonder.
[[nodiscard]] constexpr TileYield naturalWonderYieldBonus(NaturalWonderType type) {
    switch (type) {
    case NaturalWonderType::MountainOfGods:
        return {0, 0, 0, 0, 1, 2};
    case NaturalWonderType::GrandCanyon:
        return {0, 0, 2, 1, 0, 0};
    case NaturalWonderType::GreatBarrierReef:
        return {2, 0, 0, 2, 0, 0};
    case NaturalWonderType::KillerVolcano:
        return {0, 2, 0, 1, 0, 0};
    case NaturalWonderType::SacredForest:
        return {2, 0, 0, 0, 0, 2};
    case NaturalWonderType::CrystalCave:
        return {0, 0, 3, 0, 0, 0};
    default:
        return {};
    }
}

// ============================================================================
// Forward-declared improvement type (defined in HexGrid.hpp)
// Yield bonus is declared here so HexGrid::tileYield() can call it inline.
// ============================================================================

// ImprovementType is defined in HexGrid.hpp; forward-declare here for the
// constexpr yield function. We use a plain enum-class forward declaration.
enum class ImprovementType : uint8_t;

/// Yield bonus granted by a tile improvement.
[[nodiscard]] constexpr TileYield improvementYieldBonus(ImprovementType type) {
    // Must cast to uint8_t because the enum is forward-declared.
    switch (static_cast<uint8_t>(type)) {
    case 1:
        return {1, 0, 0, 0, 0, 0}; // Farm:         +1 food
    case 2:
        return {0, 1, 0, 0, 0, 0}; // Mine:         +1 production
    case 3:
        return {0, 0, 1, 0, 0, 0}; // Plantation:   +1 gold
    case 4:
        return {0, 1, 0, 0, 0, 0}; // Quarry:       +1 production
    case 5:
        return {0, 1, 0, 0, 0, 0}; // LumberMill:   +1 production
    case 6:
        return {0, 0, 1, 0, 0, 0}; // Camp:         +1 gold
    case 7:
        return {1, 0, 0, 0, 0, 0}; // Pasture:      +1 food
    case 8:
        return {1, 0, 1, 0, 0, 0}; // FishingBoats: +1 food, +1 gold
    case 9:
        return {0, 0, 0, 0, 0, 0}; // Fort:         no yield bonus
    case 10:
        return {0, 0, 0, 0, 0, 0}; // Road:         no yield bonus
    case 11:
        return {0, 1, 0, 0, 0, 0}; // Railway:      +1 production
    case 12:
        return {0, 0, 1, 0, 0, 0}; // Highway:      +1 gold
    case 13:
        return {0, 1, 0, 0, 0, 0}; // Dam:          +1 production
    case 20:
        return {0, 0, 2, 0, 0, 0}; // Canal:        +2 gold (toll revenue)
    case 21:
        return {0, 1, 0, 0, 0, 0}; // MountainMine: +1 production
    case 22:
        return {0, 0, 0, 2, 0, 0}; // Observatory:  +2 science
    case 23:
        return {0, 0, 0, 0, 1, 1}; // Monastery:    +1 culture, +1 faith
    case 24:
        return {0, 0, 0, 0, 2, 0}; // HeritageSite: +2 culture
    case 25:
        return {1, 0, 0, 0, 0, 0}; // TerraceFarm:  +1 food
    case 26:
        return {-1, 2, 0, 0, 0, 0}; // BiogasPlant:  +2 prod, -1 food (consumes)
    case 27:
        return {0, 0, 2, 1, 0, 0}; // SolarFarm:    +2 gold, +1 science
    case 28:
        return {0, 2, 0, 0, 0, 0}; // WindFarm:     +2 production
    case 29:
        return {0, 2, 2, 0, 0, 0}; // OffshorePlatform: +2 prod, +2 gold
    case 30:
        return {-1, 2, 0, 0, 0, 0}; // RecyclingCenter: +2 prod, -1 food
    case 31:
        return {0, 1, 0, 0, 0, 1}; // GeothermalVent:   +1 prod, +1 faith
    case 32:
        return {3, 0, 0, 0, 0, 0}; // DesalinationPlant:+3 food
    case 33:
        return {3, -1, 0, 0, 0, 0}; // VerticalFarm:     +3 food, -1 prod
    case 34:
        return {-1, 0, 0, 3, 0, 0}; // DataCenter:       +3 science, -1 food
    case 35:
        return {0, 0, 2, 0, 0, 0}; // TradingPost:      +2 gold
    case 36:
        return {1, 0, 0, 0, 1, 0}; // MangroveNursery:  +1 food, +1 culture
    case 37:
        return {2, 0, 0, 1, 0, 0}; // KelpFarm:         +2 food, +1 science
    case 38:
        return {2, 0, 1, 0, 0, 0}; // FishFarm:         +2 food, +1 gold
    case 39:
        return {2, 0, 0, 0, 0, 0}; // Greenhouse:       +2 food (WP-C4)
    default:
        return {}; // None / unknown
    }
}

} // namespace aoc::map
