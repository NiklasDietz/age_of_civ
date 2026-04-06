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
    Ocean,
    Coast,
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
    constexpr std::array<std::string_view, TERRAIN_COUNT> NAMES = {{
        "Ocean", "Coast", "Desert", "Plains",
        "Grassland", "Tundra", "Snow", "Mountain"
    }};
    return NAMES[static_cast<uint8_t>(type)];
}

[[nodiscard]] constexpr bool isWater(TerrainType type) {
    return type == TerrainType::Ocean || type == TerrainType::Coast;
}

[[nodiscard]] constexpr bool isImpassable(TerrainType type) {
    return type == TerrainType::Mountain || type == TerrainType::Ocean;
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
    Hills,        ///< Elevation feature, combinable with terrain

    Count
};

static constexpr uint8_t FEATURE_COUNT = static_cast<uint8_t>(FeatureType::Count);

[[nodiscard]] constexpr std::string_view featureName(FeatureType type) {
    constexpr std::array<std::string_view, FEATURE_COUNT> NAMES = {{
        "None", "Forest", "Jungle", "Marsh", "Floodplains",
        "Oasis", "Reef", "Ice", "Hills"
    }};
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
        case TerrainType::Ocean:     return {1, 0, 0, 0, 0, 0};
        case TerrainType::Coast:     return {1, 0, 1, 0, 0, 0};
        case TerrainType::Desert:    return {0, 0, 0, 0, 0, 0};
        case TerrainType::Plains:    return {1, 1, 0, 0, 0, 0};
        case TerrainType::Grassland: return {2, 0, 0, 0, 0, 0};
        case TerrainType::Tundra:    return {1, 0, 0, 0, 0, 0};
        case TerrainType::Snow:      return {0, 0, 0, 0, 0, 0};
        case TerrainType::Mountain:  return {0, 0, 0, 0, 0, 0};
        default:                     return {};
    }
}

/// Yield modifier from features (added to terrain base).
[[nodiscard]] constexpr TileYield featureYieldModifier(FeatureType type) {
    switch (type) {
        case FeatureType::Forest:      return {0, 1, 0, 0, 0, 0};
        case FeatureType::Jungle:      return {1, 0, 0, 0, 0, 0};
        case FeatureType::Marsh:       return {1, 0, 0, 0, 0, 0};
        case FeatureType::Floodplains: return {3, 0, 0, 0, 0, 0};
        case FeatureType::Oasis:       return {3, 0, 1, 0, 0, 0};
        case FeatureType::Reef:        return {1, 0, 1, 0, 0, 0};
        case FeatureType::Hills:       return {0, 1, 0, 0, 0, 0};
        default:                       return {};
    }
}

// ============================================================================
// Terrain rendering colors (RGBA, premultiplied)
// ============================================================================

struct TerrainColor {
    float r, g, b;
};

[[nodiscard]] constexpr TerrainColor terrainColor(TerrainType type) {
    switch (type) {
        case TerrainType::Ocean:     return {0.10f, 0.20f, 0.55f};
        case TerrainType::Coast:     return {0.20f, 0.45f, 0.70f};
        case TerrainType::Desert:    return {0.82f, 0.75f, 0.50f};
        case TerrainType::Plains:    return {0.65f, 0.70f, 0.35f};
        case TerrainType::Grassland: return {0.30f, 0.65f, 0.30f};
        case TerrainType::Tundra:    return {0.55f, 0.60f, 0.55f};
        case TerrainType::Snow:      return {0.85f, 0.88f, 0.90f};
        case TerrainType::Mountain:  return {0.45f, 0.40f, 0.35f};
        default:                     return {0.50f, 0.50f, 0.50f};
    }
}

/// Color tint applied on top of terrain color for features.
[[nodiscard]] constexpr TerrainColor featureColorTint(FeatureType type) {
    switch (type) {
        case FeatureType::Forest:      return {-0.05f, 0.10f, -0.05f};
        case FeatureType::Jungle:      return {-0.05f, 0.05f, -0.10f};
        case FeatureType::Marsh:       return {-0.05f, -0.05f, 0.05f};
        case FeatureType::Floodplains: return {0.05f, 0.10f, -0.05f};
        case FeatureType::Oasis:       return {0.05f, 0.15f, 0.00f};
        case FeatureType::Reef:        return {0.05f, 0.10f, 0.10f};
        case FeatureType::Ice:         return {0.20f, 0.20f, 0.25f};
        case FeatureType::Hills:       return {0.05f, 0.02f, -0.02f};
        default:                       return {0.00f, 0.00f, 0.00f};
    }
}

// ============================================================================
// Natural wonders
// ============================================================================

enum class NaturalWonderType : uint8_t {
    None,
    MountainOfGods,    ///< +2 faith, +1 culture
    GrandCanyon,       ///< +1 science, +2 gold
    GreatBarrierReef,  ///< +2 food, +2 science (coast)
    KillerVolcano,     ///< +2 production, +1 science
    SacredForest,      ///< +2 faith, +2 food
    CrystalCave,       ///< +3 gold
    Count
};

static constexpr uint8_t NATURAL_WONDER_COUNT = static_cast<uint8_t>(NaturalWonderType::Count);

[[nodiscard]] constexpr std::string_view naturalWonderName(NaturalWonderType type) {
    constexpr std::array<std::string_view, NATURAL_WONDER_COUNT> NAMES = {{
        "None", "Mountain of Gods", "Grand Canyon", "Great Barrier Reef",
        "Killer Volcano", "Sacred Forest", "Crystal Cave"
    }};
    return NAMES[static_cast<uint8_t>(type)];
}

/// Yield bonus granted by a natural wonder.
[[nodiscard]] constexpr TileYield naturalWonderYieldBonus(NaturalWonderType type) {
    switch (type) {
        case NaturalWonderType::MountainOfGods:   return {0, 0, 0, 0, 1, 2};
        case NaturalWonderType::GrandCanyon:       return {0, 0, 2, 1, 0, 0};
        case NaturalWonderType::GreatBarrierReef:  return {2, 0, 0, 2, 0, 0};
        case NaturalWonderType::KillerVolcano:     return {0, 2, 0, 1, 0, 0};
        case NaturalWonderType::SacredForest:      return {2, 0, 0, 0, 0, 2};
        case NaturalWonderType::CrystalCave:       return {0, 0, 3, 0, 0, 0};
        default:                                   return {};
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
        case 1:  return {1, 0, 0, 0, 0, 0};  // Farm:         +1 food
        case 2:  return {0, 1, 0, 0, 0, 0};  // Mine:         +1 production
        case 3:  return {0, 0, 1, 0, 0, 0};  // Plantation:   +1 gold
        case 4:  return {0, 1, 0, 0, 0, 0};  // Quarry:       +1 production
        case 5:  return {0, 1, 0, 0, 0, 0};  // LumberMill:   +1 production
        case 6:  return {0, 0, 1, 0, 0, 0};  // Camp:         +1 gold
        case 7:  return {1, 0, 0, 0, 0, 0};  // Pasture:      +1 food
        case 8:  return {1, 0, 1, 0, 0, 0};  // FishingBoats: +1 food, +1 gold
        case 9:  return {0, 0, 0, 0, 0, 0};  // Fort:         no yield bonus
        case 10: return {0, 0, 0, 0, 0, 0};  // Road:         no yield bonus
        case 11: return {0, 1, 0, 0, 0, 0};  // Railway:      +1 production
        case 12: return {0, 0, 1, 0, 0, 0};  // Highway:      +1 gold
        case 13: return {0, 1, 0, 0, 0, 0};  // Dam:          +1 production
        default: return {};                   // None / unknown
    }
}

} // namespace aoc::map
