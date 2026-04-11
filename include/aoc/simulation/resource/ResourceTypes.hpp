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
 *   Coal + Iron Ore -> Steel -> Advanced Machinery -> Aircraft
 *   Copper Ore -> Copper Wire -> Electronics -> Microchips -> Computers -> Software
 *   Stone -> Glass -> Precision Instruments -> Semiconductors
 *   Silk/Cotton -> Textiles -> Clothing -> Advanced Consumer Goods
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
    Monetary,        ///< Coined metals used as currency (copper/silver/gold coins)

    Count
};

/// Static definition of a good (raw resource or processed item).
struct GoodDef {
    uint16_t         id;
    std::string_view name;
    GoodCategory     category;
    int32_t          basePrice;        ///< Default market price in currency units
    bool             isStrategic;      ///< Required for military units / key buildings
    float            priceElasticity;  ///< How much price changes with supply/demand ratio.
                                       ///< Low (0.2) = stable prices (necessities).
                                       ///< High (0.8) = volatile prices (luxuries/advanced).
};

// ============================================================================
// Good ID constants (stable numeric IDs for serialization)
// ============================================================================

/// Reserve amount for a resource when placed on the map.
/// -1 = infinite (renewable). Positive = finite extractable units.
[[nodiscard]] inline constexpr int16_t defaultReserves(uint16_t goodId) {
    // Strategic minerals: finite, 50-150 units depending on rarity
    if (goodId <= 11) {
        switch (goodId) {
            case 0:  return 100;  // Iron ore: abundant
            case 1:  return 80;   // Copper ore: common
            case 2:  return 120;  // Coal: large deposits
            case 3:  return 60;   // Oil: moderate
            case 4:  return -1;   // Horses: renewable (breeding)
            case 5:  return 50;   // Niter: scarce
            case 6:  return 30;   // Uranium: very scarce
            case 7:  return 40;   // Aluminum: moderate
            case 8:  return -1;   // Cotton: renewable (crop)
            case 9:  return -1;   // Rubber: renewable (trees)
            case 10: return 60;   // Tin: moderate
            case 11: return 70;   // Silver ore: moderate
            default: return 80;
        }
    }
    // Luxury resources: mix of renewable and finite
    if (goodId >= 20 && goodId <= 28) {
        switch (goodId) {
            case 20: return 50;   // Gold ore: finite, scarce
            case 21: return 40;   // Gems: finite, very scarce
            case 22: return -1;   // Spices: renewable (plants)
            case 23: return -1;   // Silk: renewable (silkworms)
            case 24: return 60;   // Ivory: finite (elephants decline)
            case 25: return -1;   // Wine: renewable (grapes)
            case 26: return -1;   // Dyes: renewable (plants)
            case 27: return -1;   // Furs: renewable (animals, slowly)
            case 28: return -1;   // Incense: renewable (trees)
            default: return -1;
        }
    }
    // Bonus resources: all renewable (food, wood, fish)
    if (goodId >= 40 && goodId <= 47) {
        return -1;  // Renewable
    }
    return 80;  // Default for unknown resources
}

/// Whether a resource is renewable (regrows/restocks naturally).
[[nodiscard]] inline constexpr bool isRenewableResource(uint16_t goodId) {
    return defaultReserves(goodId) < 0;
}

/// Technology required to reveal a resource on the map.
/// Returns an invalid TechId if the resource is always visible (luxury/bonus resources).
/// Strategic resources are hidden until the player researches the required tech.
///
/// Based on Civ 6 resource visibility:
///   Mining (0)            → Copper, Tin, Stone
///   Animal Husbandry (1)  → Horses
///   Bronze Working (4)    → Iron
///   Apprenticeship (7)    → Niter
///   Industrialization (11)→ Coal
///   Refining (13)         → Oil
///   Electricity (14)      → Aluminum
///   Nuclear Fission (18)  → Uranium
///   (none)                → Rubber, Cotton, Silver, Gold ore (always visible)
[[nodiscard]] inline constexpr TechId resourceRevealTech(uint16_t goodId) {
    switch (goodId) {
        case 0:  return TechId{4};   // Iron ore → Bronze Working
        case 1:  return TechId{0};   // Copper ore → Mining
        case 2:  return TechId{11};  // Coal → Industrialization
        case 3:  return TechId{13};  // Oil → Refining
        case 4:  return TechId{1};   // Horses → Animal Husbandry
        case 5:  return TechId{7};   // Niter → Apprenticeship
        case 6:  return TechId{18};  // Uranium → Nuclear Fission
        case 7:  return TechId{14};  // Aluminum → Electricity
        case 10: return TechId{0};   // Tin → Mining
        default: return TechId{};    // Always visible (luxury, bonus, cotton, rubber, silver, gold)
    }
}

/// Check if a player can see a resource based on their researched techs.
[[nodiscard]] inline bool canSeeResource(uint16_t goodId, const void* playerTechPtr) {
    TechId revealTech = resourceRevealTech(goodId);
    if (!revealTech.isValid()) {
        return true;  // Always visible
    }
    // playerTechPtr is a PlayerTechComponent* but we use void* to avoid circular include
    // Caller must cast and check hasResearched
    (void)playerTechPtr;
    return false;  // Caller should use resourceRevealTech() directly
}

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
    inline constexpr uint16_t COTTON      = 8;
    inline constexpr uint16_t RUBBER      = 9;
    inline constexpr uint16_t TIN         = 10;
    inline constexpr uint16_t SILVER_ORE  = 11;

    // -- Raw luxury resources (20-39) --
    inline constexpr uint16_t GOLD_ORE    = 20;
    inline constexpr uint16_t GEMS        = 21;
    inline constexpr uint16_t SPICES      = 22;
    inline constexpr uint16_t SILK        = 23;
    inline constexpr uint16_t IVORY       = 24;
    inline constexpr uint16_t WINE        = 25;
    inline constexpr uint16_t DYES        = 26;
    inline constexpr uint16_t FURS        = 27;
    inline constexpr uint16_t INCENSE     = 28;

    // -- Raw bonus resources (40-59) --
    inline constexpr uint16_t WHEAT       = 40;
    inline constexpr uint16_t CATTLE      = 41;
    inline constexpr uint16_t FISH        = 42;
    inline constexpr uint16_t WOOD        = 43;
    inline constexpr uint16_t STONE       = 44;
    inline constexpr uint16_t RICE        = 45;
    inline constexpr uint16_t SUGAR       = 46;
    inline constexpr uint16_t CLAY        = 47;

    // -- Processed goods (60-99) --
    inline constexpr uint16_t IRON_INGOTS          = 60;
    inline constexpr uint16_t COPPER_WIRE          = 61;
    inline constexpr uint16_t LUMBER               = 62;
    inline constexpr uint16_t TOOLS                = 63;
    inline constexpr uint16_t STEEL                = 64;
    inline constexpr uint16_t FUEL                 = 65;
    inline constexpr uint16_t PLASTICS             = 66;
    inline constexpr uint16_t BRICKS               = 67;
    inline constexpr uint16_t TEXTILES             = 68;
    inline constexpr uint16_t CLOTHING             = 69;
    inline constexpr uint16_t PROCESSED_FOOD       = 70;
    inline constexpr uint16_t AMMUNITION           = 71;
    inline constexpr uint16_t SURFACE_PLATE        = 72;
    inline constexpr uint16_t PRECISION_INSTRUMENTS = 73;
    inline constexpr uint16_t INTERCHANGEABLE_PARTS = 74;
    inline constexpr uint16_t SEMICONDUCTORS       = 75;
    inline constexpr uint16_t GLASS                = 76;
    inline constexpr uint16_t RUBBER_GOODS         = 77;
    inline constexpr uint16_t BRONZE               = 78;
    inline constexpr uint16_t CHARCOAL            = 79;  ///< Wood-based fuel, early coal substitute

    // -- Advanced goods (100-139) --
    inline constexpr uint16_t MACHINERY            = 100;
    inline constexpr uint16_t ELECTRONICS          = 101;
    inline constexpr uint16_t ADVANCED_MACHINERY   = 102;
    inline constexpr uint16_t INDUSTRIAL_EQUIP     = 103;
    inline constexpr uint16_t CONSTRUCTION_MAT     = 104;
    inline constexpr uint16_t CONSUMER_GOODS       = 105;
    inline constexpr uint16_t MICROCHIPS           = 106;
    inline constexpr uint16_t COMPUTERS_GOOD       = 107;
    inline constexpr uint16_t SOFTWARE             = 108;
    inline constexpr uint16_t AIRCRAFT_COMPONENTS  = 109;
    inline constexpr uint16_t AIRCRAFT             = 110;
    inline constexpr uint16_t ARMORED_VEHICLES     = 111;
    inline constexpr uint16_t TELECOM_EQUIPMENT    = 112;
    inline constexpr uint16_t ADV_CONSUMER_GOODS   = 113;

    // -- Monetary goods (140-159) -- produced at Mint from raw metals
    inline constexpr uint16_t COPPER_COINS         = 140;
    inline constexpr uint16_t SILVER_COINS         = 141;
    inline constexpr uint16_t GOLD_COINS           = 142;

    // -- Automation goods (160-169) --
    inline constexpr uint16_t ROBOT_WORKERS        = 143;

    inline constexpr uint16_t GOOD_COUNT = 145;
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
    bool     consumed = true;  ///< If false, the input is required but not consumed (e.g., Computers for Software).
};

/**
 * @brief A single production recipe: consumes inputs, produces output.
 *
 * Recipes are processed by cities that have the required building.
 * Each turn, a city with the right building and sufficient input goods
 * consumes the consumed inputs and produces the output.
 */
struct ProductionRecipe {
    uint16_t                  recipeId;
    std::string_view          name;
    std::vector<RecipeInput>  inputs;         ///< What is required (and consumed unless consumed==false)
    uint16_t                  outputGoodId;   ///< What is produced
    int32_t                   outputAmount;
    BuildingId                requiredBuilding; ///< Building needed in the city
    int32_t                   turnsToProcess;   ///< Production time (1 = instant next turn)
};

/// Get all production recipes.
[[nodiscard]] const std::vector<ProductionRecipe>& allRecipes();

} // namespace aoc::sim
