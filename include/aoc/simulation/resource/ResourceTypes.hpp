#pragma once

/**
 * @file ResourceTypes.hpp
 * @brief Definitions for raw resources, processed goods, and production recipes.
 *
 * The economic model is built on multi-tier processing chains:
 *   Raw Resource (mined from tiles) -> Processed Good -> Advanced Good -> ...
 * Early-game resources remain relevant in late-game because they feed
 * into advanced production chains.
 *
 * Example chains:
 *   Iron Ore -> Iron Ingots -> Tools -> Machinery -> Industrial Equipment
 *   Coal + Iron Ore -> Steel -> Advanced Machinery
 *   Copper Ore -> Copper Wire -> Electronics
 *   Oil -> Fuel / Plastics
 *   Wood -> Lumber -> Furniture / Construction Materials
 */

#include "aoc/core/Types.hpp"

#include <array>
#include <cstdint>
#include <string_view>
#include <vector>

namespace aoc::sim {

// ============================================================================
// Good identifier -- unified ID for both raw resources and processed goods.
// The production chain doesn't distinguish; a "good" is anything that can be
// produced, consumed, traded, or stockpiled.
// ============================================================================

/// Category helps the UI group goods and determines tile placement rules.
enum class GoodCategory : uint8_t {
    RawStrategic,    ///< Mined from strategic resource tiles (iron, coal, oil, ...)
    RawLuxury,       ///< Harvested from luxury resource tiles (gold, gems, spices, ...)
    RawBonus,        ///< From bonus resource tiles (wheat, cattle, fish, ...)
    Processed,       ///< Output of a production recipe (tools, steel, electronics, ...)
    Advanced,        ///< High-tier processed good (machinery, industrial equipment, ...)

    Count
};

/// Static definition of a good (raw resource or processed item).
struct GoodDef {
    uint16_t         id;
    std::string_view name;
    GoodCategory     category;
    int32_t          basePrice;    ///< Default market price in currency units
    bool             isStrategic;  ///< Required for military units / key buildings
};

// ============================================================================
// Good ID constants (stable numeric IDs for serialization)
// ============================================================================

namespace goods {
    // -- Raw strategic resources (0-19) --
    inline constexpr uint16_t IRON_ORE    = 0;
    inline constexpr uint16_t COPPER_ORE  = 1;
    inline constexpr uint16_t COAL        = 2;
    inline constexpr uint16_t OIL         = 3;
    inline constexpr uint16_t HORSES      = 4;
    inline constexpr uint16_t NITER       = 5;
    inline constexpr uint16_t URANIUM     = 6;
    inline constexpr uint16_t ALUMINUM    = 7;

    // -- Raw luxury resources (20-39) --
    inline constexpr uint16_t GOLD_ORE    = 20;
    inline constexpr uint16_t GEMS        = 21;
    inline constexpr uint16_t SPICES      = 22;
    inline constexpr uint16_t SILK        = 23;
    inline constexpr uint16_t IVORY       = 24;
    inline constexpr uint16_t WINE        = 25;

    // -- Raw bonus resources (40-59) --
    inline constexpr uint16_t WHEAT       = 40;
    inline constexpr uint16_t CATTLE      = 41;
    inline constexpr uint16_t FISH        = 42;
    inline constexpr uint16_t WOOD        = 43;
    inline constexpr uint16_t STONE       = 44;

    // -- Processed goods (60-99) --
    inline constexpr uint16_t IRON_INGOTS   = 60;
    inline constexpr uint16_t COPPER_WIRE   = 61;
    inline constexpr uint16_t LUMBER        = 62;
    inline constexpr uint16_t TOOLS         = 63;
    inline constexpr uint16_t STEEL         = 64;
    inline constexpr uint16_t FUEL          = 65;
    inline constexpr uint16_t PLASTICS      = 66;
    inline constexpr uint16_t BRICKS        = 67;

    // -- Advanced goods (100-139) --
    inline constexpr uint16_t MACHINERY           = 100;
    inline constexpr uint16_t ELECTRONICS         = 101;
    inline constexpr uint16_t ADVANCED_MACHINERY  = 102;
    inline constexpr uint16_t INDUSTRIAL_EQUIP    = 103;
    inline constexpr uint16_t CONSTRUCTION_MAT    = 104;
    inline constexpr uint16_t CONSUMER_GOODS      = 105;

    inline constexpr uint16_t GOOD_COUNT = 106;
} // namespace goods

// ============================================================================
// Good definitions table
// ============================================================================

/// Get the static definition for a good.
[[nodiscard]] const GoodDef& goodDef(uint16_t goodId);

/// Total number of defined goods.
[[nodiscard]] uint16_t goodCount();

// ============================================================================
// Production recipe -- one transformation step in the chain
// ============================================================================

struct RecipeInput {
    uint16_t goodId;
    int32_t  amount;
};

/**
 * @brief A single production recipe: consumes inputs, produces output.
 *
 * Recipes are processed by cities that have the required building.
 * Each turn, a city with the right building and sufficient input goods
 * consumes the inputs and produces the output.
 */
struct ProductionRecipe {
    uint16_t                  recipeId;
    std::string_view          name;
    std::vector<RecipeInput>  inputs;         ///< What is consumed
    uint16_t                  outputGoodId;   ///< What is produced
    int32_t                   outputAmount;
    BuildingId                requiredBuilding; ///< Building needed in the city
    int32_t                   turnsToProcess;   ///< Production time (1 = instant next turn)
};

/// Get all production recipes.
[[nodiscard]] const std::vector<ProductionRecipe>& allRecipes();

} // namespace aoc::sim
