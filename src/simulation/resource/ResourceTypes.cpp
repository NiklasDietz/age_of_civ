/**
 * @file ResourceTypes.cpp
 * @brief Good definitions table and production recipe data.
 */

#include "aoc/simulation/resource/ResourceTypes.hpp"

#include <cassert>

namespace aoc::sim {

namespace {

// ============================================================================
// Static goods table
// ============================================================================

// clang-format off
constexpr std::array<GoodDef, goods::GOOD_COUNT> GOOD_DEFS = []{
    std::array<GoodDef, goods::GOOD_COUNT> defs{};

    // Initialize all to a default
    for (auto& d : defs) {
        d = {0, "Unknown", GoodCategory::RawBonus, 10, false};
    }

    // Raw strategic
    defs[goods::IRON_ORE]   = {goods::IRON_ORE,   "Iron Ore",    GoodCategory::RawStrategic, 15, true};
    defs[goods::COPPER_ORE] = {goods::COPPER_ORE,  "Copper Ore",  GoodCategory::RawStrategic, 12, true};
    defs[goods::COAL]       = {goods::COAL,        "Coal",        GoodCategory::RawStrategic, 14, true};
    defs[goods::OIL]        = {goods::OIL,         "Oil",         GoodCategory::RawStrategic, 30, true};
    defs[goods::HORSES]     = {goods::HORSES,      "Horses",      GoodCategory::RawStrategic, 20, true};
    defs[goods::NITER]      = {goods::NITER,       "Niter",       GoodCategory::RawStrategic, 18, true};
    defs[goods::URANIUM]    = {goods::URANIUM,     "Uranium",     GoodCategory::RawStrategic, 50, true};
    defs[goods::ALUMINUM]   = {goods::ALUMINUM,    "Aluminum",    GoodCategory::RawStrategic, 35, true};

    // Raw luxury
    defs[goods::GOLD_ORE]   = {goods::GOLD_ORE,   "Gold Ore",    GoodCategory::RawLuxury, 25, false};
    defs[goods::GEMS]       = {goods::GEMS,        "Gems",        GoodCategory::RawLuxury, 30, false};
    defs[goods::SPICES]     = {goods::SPICES,      "Spices",      GoodCategory::RawLuxury, 18, false};
    defs[goods::SILK]       = {goods::SILK,        "Silk",        GoodCategory::RawLuxury, 20, false};
    defs[goods::IVORY]      = {goods::IVORY,       "Ivory",       GoodCategory::RawLuxury, 22, false};
    defs[goods::WINE]       = {goods::WINE,        "Wine",        GoodCategory::RawLuxury, 16, false};

    // Raw bonus
    defs[goods::WHEAT]      = {goods::WHEAT,       "Wheat",       GoodCategory::RawBonus, 5, false};
    defs[goods::CATTLE]     = {goods::CATTLE,      "Cattle",      GoodCategory::RawBonus, 8, false};
    defs[goods::FISH]       = {goods::FISH,        "Fish",        GoodCategory::RawBonus, 6, false};
    defs[goods::WOOD]       = {goods::WOOD,        "Wood",        GoodCategory::RawBonus, 7, false};
    defs[goods::STONE]      = {goods::STONE,       "Stone",       GoodCategory::RawBonus, 8, false};

    // Processed
    defs[goods::IRON_INGOTS]  = {goods::IRON_INGOTS,  "Iron Ingots",  GoodCategory::Processed, 25, true};
    defs[goods::COPPER_WIRE]  = {goods::COPPER_WIRE,  "Copper Wire",  GoodCategory::Processed, 22, true};
    defs[goods::LUMBER]       = {goods::LUMBER,       "Lumber",       GoodCategory::Processed, 12, false};
    defs[goods::TOOLS]        = {goods::TOOLS,        "Tools",        GoodCategory::Processed, 35, true};
    defs[goods::STEEL]        = {goods::STEEL,        "Steel",        GoodCategory::Processed, 45, true};
    defs[goods::FUEL]         = {goods::FUEL,         "Fuel",         GoodCategory::Processed, 40, true};
    defs[goods::PLASTICS]     = {goods::PLASTICS,     "Plastics",     GoodCategory::Processed, 35, false};
    defs[goods::BRICKS]       = {goods::BRICKS,       "Bricks",       GoodCategory::Processed, 15, false};

    // Advanced
    defs[goods::MACHINERY]          = {goods::MACHINERY,          "Machinery",           GoodCategory::Advanced, 70, true};
    defs[goods::ELECTRONICS]        = {goods::ELECTRONICS,        "Electronics",         GoodCategory::Advanced, 80, true};
    defs[goods::ADVANCED_MACHINERY] = {goods::ADVANCED_MACHINERY, "Advanced Machinery",  GoodCategory::Advanced, 120, true};
    defs[goods::INDUSTRIAL_EQUIP]   = {goods::INDUSTRIAL_EQUIP,   "Industrial Equipment",GoodCategory::Advanced, 150, true};
    defs[goods::CONSTRUCTION_MAT]   = {goods::CONSTRUCTION_MAT,   "Construction Mat.",   GoodCategory::Advanced, 55, false};
    defs[goods::CONSUMER_GOODS]     = {goods::CONSUMER_GOODS,     "Consumer Goods",      GoodCategory::Advanced, 45, false};

    return defs;
}();
// clang-format on

// ============================================================================
// Production recipes (built once, static lifetime)
// ============================================================================

std::vector<ProductionRecipe> buildRecipes() {
    std::vector<ProductionRecipe> recipes;

    // Tier 1: Raw -> Basic processed
    recipes.push_back({0, "Smelt Iron",
        {{goods::IRON_ORE, 2}},
        goods::IRON_INGOTS, 1, BuildingId{0}, 1});

    recipes.push_back({1, "Draw Copper Wire",
        {{goods::COPPER_ORE, 2}},
        goods::COPPER_WIRE, 1, BuildingId{0}, 1});

    recipes.push_back({2, "Mill Lumber",
        {{goods::WOOD, 2}},
        goods::LUMBER, 2, BuildingId{1}, 1});

    recipes.push_back({3, "Make Bricks",
        {{goods::STONE, 2}},
        goods::BRICKS, 2, BuildingId{1}, 1});

    recipes.push_back({4, "Refine Fuel",
        {{goods::OIL, 2}},
        goods::FUEL, 1, BuildingId{2}, 1});

    recipes.push_back({5, "Produce Plastics",
        {{goods::OIL, 1}},
        goods::PLASTICS, 1, BuildingId{2}, 1});

    // Tier 2: Basic processed -> Tools / Steel
    recipes.push_back({6, "Forge Tools",
        {{goods::IRON_INGOTS, 1}, {goods::WOOD, 1}},
        goods::TOOLS, 1, BuildingId{0}, 1});

    recipes.push_back({7, "Produce Steel",
        {{goods::IRON_ORE, 1}, {goods::COAL, 2}},
        goods::STEEL, 1, BuildingId{3}, 1});

    recipes.push_back({8, "Construction Materials",
        {{goods::LUMBER, 1}, {goods::BRICKS, 1}},
        goods::CONSTRUCTION_MAT, 1, BuildingId{1}, 1});

    // Tier 3: Tools/Steel -> Machinery / Electronics
    recipes.push_back({9, "Build Machinery",
        {{goods::TOOLS, 1}, {goods::STEEL, 1}},
        goods::MACHINERY, 1, BuildingId{3}, 2});

    recipes.push_back({10, "Produce Electronics",
        {{goods::COPPER_WIRE, 2}, {goods::PLASTICS, 1}},
        goods::ELECTRONICS, 1, BuildingId{4}, 2});

    recipes.push_back({11, "Consumer Goods",
        {{goods::PLASTICS, 1}, {goods::LUMBER, 1}},
        goods::CONSUMER_GOODS, 2, BuildingId{1}, 1});

    // Tier 4: Advanced goods
    recipes.push_back({12, "Advanced Machinery",
        {{goods::MACHINERY, 1}, {goods::ELECTRONICS, 1}},
        goods::ADVANCED_MACHINERY, 1, BuildingId{4}, 3});

    recipes.push_back({13, "Industrial Equipment",
        {{goods::ADVANCED_MACHINERY, 1}, {goods::STEEL, 2}},
        goods::INDUSTRIAL_EQUIP, 1, BuildingId{5}, 3});

    return recipes;
}

const std::vector<ProductionRecipe>& getRecipes() {
    static const std::vector<ProductionRecipe> recipes = buildRecipes();
    return recipes;
}

} // anonymous namespace

const GoodDef& goodDef(uint16_t goodId) {
    assert(goodId < goods::GOOD_COUNT);
    return GOOD_DEFS[goodId];
}

uint16_t goodCount() {
    return goods::GOOD_COUNT;
}

const std::vector<ProductionRecipe>& allRecipes() {
    return getRecipes();
}

} // namespace aoc::sim
