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

    for (auto& d : defs) {
        d = {0, "Unknown", GoodCategory::RawBonus, 10, false, 0.5f};
    }

    // Raw strategic (0-19) -- medium elasticity (0.4)
    defs[goods::IRON_ORE]   = {goods::IRON_ORE,   "Iron Ore",    GoodCategory::RawStrategic, 15, true,  0.4f};
    defs[goods::COPPER_ORE] = {goods::COPPER_ORE,  "Copper Ore",  GoodCategory::RawStrategic, 12, true,  0.4f};
    defs[goods::COAL]       = {goods::COAL,        "Coal",        GoodCategory::RawStrategic, 14, true,  0.4f};
    defs[goods::OIL]        = {goods::OIL,         "Oil",         GoodCategory::RawStrategic, 30, true,  0.4f};
    defs[goods::HORSES]     = {goods::HORSES,      "Horses",      GoodCategory::RawStrategic, 20, true,  0.4f};
    defs[goods::NITER]      = {goods::NITER,       "Niter",       GoodCategory::RawStrategic, 18, true,  0.4f};
    defs[goods::URANIUM]    = {goods::URANIUM,     "Uranium",     GoodCategory::RawStrategic, 50, true,  0.4f};
    defs[goods::ALUMINUM]   = {goods::ALUMINUM,    "Aluminum",    GoodCategory::RawStrategic, 35, true,  0.4f};
    defs[goods::COTTON]     = {goods::COTTON,      "Cotton",      GoodCategory::RawStrategic, 10, false, 0.4f};
    defs[goods::RUBBER]     = {goods::RUBBER,      "Rubber",      GoodCategory::RawStrategic, 18, true,  0.4f};
    defs[goods::TIN]        = {goods::TIN,         "Tin",         GoodCategory::RawStrategic, 12, true,  0.4f};

    // Raw luxury (20-39) -- high elasticity (0.7)
    defs[goods::GOLD_ORE]   = {goods::GOLD_ORE,   "Gold Ore",    GoodCategory::RawLuxury, 25, false, 0.7f};
    defs[goods::GEMS]       = {goods::GEMS,        "Gems",        GoodCategory::RawLuxury, 30, false, 0.7f};
    defs[goods::SPICES]     = {goods::SPICES,      "Spices",      GoodCategory::RawLuxury, 18, false, 0.7f};
    defs[goods::SILK]       = {goods::SILK,        "Silk",        GoodCategory::RawLuxury, 20, false, 0.7f};
    defs[goods::IVORY]      = {goods::IVORY,       "Ivory",       GoodCategory::RawLuxury, 22, false, 0.7f};
    defs[goods::WINE]       = {goods::WINE,        "Wine",        GoodCategory::RawLuxury, 16, false, 0.7f};
    defs[goods::DYES]       = {goods::DYES,        "Dyes",        GoodCategory::RawLuxury, 15, false, 0.7f};
    defs[goods::FURS]       = {goods::FURS,        "Furs",        GoodCategory::RawLuxury, 18, false, 0.7f};
    defs[goods::INCENSE]    = {goods::INCENSE,     "Incense",     GoodCategory::RawLuxury, 14, false, 0.7f};

    // Raw bonus (40-59) -- low elasticity (0.2), necessities
    defs[goods::WHEAT]      = {goods::WHEAT,       "Wheat",       GoodCategory::RawBonus, 5, false, 0.2f};
    defs[goods::CATTLE]     = {goods::CATTLE,      "Cattle",      GoodCategory::RawBonus, 8, false, 0.2f};
    defs[goods::FISH]       = {goods::FISH,        "Fish",        GoodCategory::RawBonus, 6, false, 0.2f};
    defs[goods::WOOD]       = {goods::WOOD,        "Wood",        GoodCategory::RawBonus, 7, false, 0.2f};
    defs[goods::STONE]      = {goods::STONE,       "Stone",       GoodCategory::RawBonus, 8, false, 0.2f};
    defs[goods::RICE]       = {goods::RICE,        "Rice",        GoodCategory::RawBonus, 5, false, 0.2f};
    defs[goods::SUGAR]      = {goods::SUGAR,       "Sugar",       GoodCategory::RawBonus, 7, false, 0.2f};
    defs[goods::CLAY]       = {goods::CLAY,        "Clay",        GoodCategory::RawBonus, 6, false, 0.2f};

    // Processed (60-99) -- medium elasticity (0.5)
    defs[goods::IRON_INGOTS]          = {goods::IRON_INGOTS,          "Iron Ingots",          GoodCategory::Processed, 25, true,  0.5f};
    defs[goods::COPPER_WIRE]          = {goods::COPPER_WIRE,          "Copper Wire",          GoodCategory::Processed, 22, true,  0.5f};
    defs[goods::LUMBER]               = {goods::LUMBER,               "Lumber",               GoodCategory::Processed, 12, false, 0.5f};
    defs[goods::TOOLS]                = {goods::TOOLS,                "Tools",                GoodCategory::Processed, 35, true,  0.5f};
    defs[goods::STEEL]                = {goods::STEEL,                "Steel",                GoodCategory::Processed, 45, true,  0.5f};
    defs[goods::FUEL]                 = {goods::FUEL,                 "Fuel",                 GoodCategory::Processed, 40, true,  0.5f};
    defs[goods::PLASTICS]             = {goods::PLASTICS,             "Plastics",             GoodCategory::Processed, 35, false, 0.5f};
    defs[goods::BRICKS]               = {goods::BRICKS,              "Bricks",               GoodCategory::Processed, 15, false, 0.5f};
    defs[goods::TEXTILES]             = {goods::TEXTILES,             "Textiles",             GoodCategory::Processed, 20, false, 0.5f};
    defs[goods::CLOTHING]             = {goods::CLOTHING,             "Clothing",             GoodCategory::Processed, 30, false, 0.5f};
    defs[goods::PROCESSED_FOOD]       = {goods::PROCESSED_FOOD,       "Processed Food",       GoodCategory::Processed, 18, false, 0.5f};
    defs[goods::AMMUNITION]           = {goods::AMMUNITION,           "Ammunition",           GoodCategory::Processed, 40, true,  0.5f};
    defs[goods::SURFACE_PLATE]        = {goods::SURFACE_PLATE,        "Surface Plate",        GoodCategory::Processed, 50, true,  0.5f};
    defs[goods::PRECISION_INSTRUMENTS]= {goods::PRECISION_INSTRUMENTS,"Precision Instruments", GoodCategory::Processed, 65, true,  0.5f};
    defs[goods::INTERCHANGEABLE_PARTS]= {goods::INTERCHANGEABLE_PARTS,"Interchangeable Parts",GoodCategory::Processed, 55, true,  0.5f};
    defs[goods::SEMICONDUCTORS]       = {goods::SEMICONDUCTORS,       "Semiconductors",       GoodCategory::Processed, 80, true,  0.5f};
    defs[goods::GLASS]                = {goods::GLASS,                "Glass",                GoodCategory::Processed, 15, false, 0.5f};
    defs[goods::RUBBER_GOODS]         = {goods::RUBBER_GOODS,         "Rubber Goods",         GoodCategory::Processed, 25, false, 0.5f};
    defs[goods::BRONZE]               = {goods::BRONZE,               "Bronze",               GoodCategory::Processed, 20, true,  0.5f};

    // Advanced (100+) -- high elasticity (0.8)
    defs[goods::MACHINERY]            = {goods::MACHINERY,            "Machinery",            GoodCategory::Advanced, 70, true,  0.8f};
    defs[goods::ELECTRONICS]          = {goods::ELECTRONICS,          "Electronics",          GoodCategory::Advanced, 80, true,  0.8f};
    defs[goods::ADVANCED_MACHINERY]   = {goods::ADVANCED_MACHINERY,   "Advanced Machinery",   GoodCategory::Advanced, 120, true,  0.8f};
    defs[goods::INDUSTRIAL_EQUIP]     = {goods::INDUSTRIAL_EQUIP,     "Industrial Equipment", GoodCategory::Advanced, 150, true,  0.8f};
    defs[goods::CONSTRUCTION_MAT]     = {goods::CONSTRUCTION_MAT,     "Construction Mat.",    GoodCategory::Advanced, 55, false, 0.8f};
    defs[goods::CONSUMER_GOODS]       = {goods::CONSUMER_GOODS,       "Consumer Goods",       GoodCategory::Advanced, 45, false, 0.8f};
    defs[goods::MICROCHIPS]           = {goods::MICROCHIPS,           "Microchips",           GoodCategory::Advanced, 160, true,  0.8f};
    defs[goods::COMPUTERS_GOOD]       = {goods::COMPUTERS_GOOD,       "Computers",            GoodCategory::Advanced, 250, true,  0.8f};
    defs[goods::SOFTWARE]             = {goods::SOFTWARE,             "Software",             GoodCategory::Advanced, 200, false, 0.8f};
    defs[goods::AIRCRAFT_COMPONENTS]  = {goods::AIRCRAFT_COMPONENTS,  "Aircraft Components",  GoodCategory::Advanced, 180, true,  0.8f};
    defs[goods::AIRCRAFT]             = {goods::AIRCRAFT,             "Aircraft",             GoodCategory::Advanced, 300, true,  0.8f};
    defs[goods::ARMORED_VEHICLES]     = {goods::ARMORED_VEHICLES,     "Armored Vehicles",     GoodCategory::Advanced, 200, true,  0.8f};
    defs[goods::TELECOM_EQUIPMENT]    = {goods::TELECOM_EQUIPMENT,    "Telecom Equipment",    GoodCategory::Advanced, 140, true,  0.8f};
    defs[goods::ADV_CONSUMER_GOODS]   = {goods::ADV_CONSUMER_GOODS,   "Adv. Consumer Goods",  GoodCategory::Advanced, 90, false, 0.8f};

    return defs;
}();
// clang-format on

// ============================================================================
// Production recipes (built once, static lifetime)
// ============================================================================

std::vector<ProductionRecipe> buildRecipes() {
    std::vector<ProductionRecipe> recipes;

    // ================================================================
    // Tier 1: Raw -> Basic processed
    // ================================================================
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

    // ================================================================
    // Tier 2: Basic processed -> Tools / Steel / Construction
    // ================================================================
    recipes.push_back({6, "Forge Tools",
        {{goods::IRON_INGOTS, 1}, {goods::WOOD, 1}},
        goods::TOOLS, 1, BuildingId{0}, 1});

    recipes.push_back({7, "Produce Steel",
        {{goods::IRON_ORE, 1}, {goods::COAL, 2}},
        goods::STEEL, 1, BuildingId{3}, 1});

    recipes.push_back({8, "Construction Materials",
        {{goods::LUMBER, 1}, {goods::BRICKS, 1}},
        goods::CONSTRUCTION_MAT, 1, BuildingId{1}, 1});

    // ================================================================
    // Tier 3: Machinery / Electronics / Consumer Goods
    // ================================================================
    recipes.push_back({9, "Build Machinery",
        {{goods::TOOLS, 1}, {goods::STEEL, 1}},
        goods::MACHINERY, 1, BuildingId{3}, 2});

    recipes.push_back({10, "Produce Electronics",
        {{goods::COPPER_WIRE, 2}, {goods::PLASTICS, 1}},
        goods::ELECTRONICS, 1, BuildingId{4}, 2});

    recipes.push_back({11, "Consumer Goods",
        {{goods::PLASTICS, 1}, {goods::LUMBER, 1}},
        goods::CONSUMER_GOODS, 2, BuildingId{1}, 1});

    // ================================================================
    // Tier 4: Advanced goods (original)
    // ================================================================
    recipes.push_back({12, "Advanced Machinery",
        {{goods::MACHINERY, 1}, {goods::ELECTRONICS, 1}},
        goods::ADVANCED_MACHINERY, 1, BuildingId{4}, 3});

    recipes.push_back({13, "Industrial Equipment",
        {{goods::ADVANCED_MACHINERY, 1}, {goods::STEEL, 2}},
        goods::INDUSTRIAL_EQUIP, 1, BuildingId{5}, 3});

    // ================================================================
    // NEW: Textiles & clothing chain (keeps Silk, Cotton, Dyes relevant)
    // ================================================================
    recipes.push_back({14, "Weave Silk Textiles",
        {{goods::SILK, 1}},
        goods::TEXTILES, 1, BuildingId{8}, 1});

    recipes.push_back({15, "Produce Clothing",
        {{goods::TEXTILES, 2}, {goods::DYES, 1}},
        goods::CLOTHING, 1, BuildingId{8}, 1});

    // ================================================================
    // NEW: Food processing (keeps Wheat, Cattle relevant in late game)
    // ================================================================
    recipes.push_back({16, "Process Food",
        {{goods::WHEAT, 2}, {goods::CATTLE, 1}},
        goods::PROCESSED_FOOD, 2, BuildingId{9}, 1});

    // ================================================================
    // NEW: Military supply chain (keeps Niter, Steel relevant)
    // ================================================================
    recipes.push_back({17, "Manufacture Ammunition",
        {{goods::STEEL, 1}, {goods::NITER, 1}},
        goods::AMMUNITION, 2, BuildingId{3}, 1});

    // ================================================================
    // NEW: Precision manufacturing chain (Surface Plate -> Instruments -> Parts)
    // ================================================================
    recipes.push_back({18, "Grind Surface Plate",
        {{goods::IRON_INGOTS, 2}, {goods::STONE, 1}},
        goods::SURFACE_PLATE, 1, BuildingId{10}, 2});

    recipes.push_back({19, "Build Precision Instruments",
        {{goods::SURFACE_PLATE, 1}, {goods::COPPER_WIRE, 1}, {goods::GLASS, 1}},
        goods::PRECISION_INSTRUMENTS, 1, BuildingId{10}, 2});

    recipes.push_back({20, "Standardize Parts",
        {{goods::PRECISION_INSTRUMENTS, 1}, {goods::STEEL, 1}, {goods::MACHINERY, 1}},
        goods::INTERCHANGEABLE_PARTS, 2, BuildingId{10}, 2});

    // ================================================================
    // NEW: Semiconductor -> Microchip -> Computer -> Software chain
    // ================================================================
    recipes.push_back({21, "Fabricate Semiconductors",
        {{goods::PRECISION_INSTRUMENTS, 1}, {goods::COPPER_WIRE, 2}},
        goods::SEMICONDUCTORS, 1, BuildingId{11}, 3});

    recipes.push_back({22, "Produce Microchips",
        {{goods::SEMICONDUCTORS, 2}, {goods::ELECTRONICS, 1}},
        goods::MICROCHIPS, 1, BuildingId{11}, 3});

    recipes.push_back({23, "Assemble Computers",
        {{goods::MICROCHIPS, 1}, {goods::PLASTICS, 1}, {goods::ELECTRONICS, 1}},
        goods::COMPUTERS_GOOD, 1, BuildingId{4}, 2});

    // Software: Computers good is required but NOT consumed (knowledge economy)
    recipes.push_back({24, "Develop Software",
        {{goods::COMPUTERS_GOOD, 1, false}},
        goods::SOFTWARE, 2, BuildingId{12}, 1});

    // ================================================================
    // NEW: Aviation chain (keeps Aluminum relevant)
    // ================================================================
    recipes.push_back({25, "Build Aircraft Components",
        {{goods::ALUMINUM, 2}, {goods::ADVANCED_MACHINERY, 1}},
        goods::AIRCRAFT_COMPONENTS, 1, BuildingId{5}, 3});

    recipes.push_back({26, "Assemble Aircraft",
        {{goods::AIRCRAFT_COMPONENTS, 1}, {goods::ELECTRONICS, 1}, {goods::FUEL, 1}},
        goods::AIRCRAFT, 1, BuildingId{5}, 4});

    // ================================================================
    // NEW: Armored vehicles (keeps Steel, Fuel, Machinery relevant)
    // ================================================================
    recipes.push_back({27, "Build Armored Vehicles",
        {{goods::STEEL, 2}, {goods::FUEL, 1}, {goods::MACHINERY, 1}},
        goods::ARMORED_VEHICLES, 1, BuildingId{3}, 3});

    // ================================================================
    // NEW: Telecommunications (keeps Copper Wire relevant in info age)
    // ================================================================
    recipes.push_back({28, "Build Telecom Equipment",
        {{goods::COPPER_WIRE, 2}, {goods::ELECTRONICS, 1}, {goods::PLASTICS, 1}},
        goods::TELECOM_EQUIPMENT, 1, BuildingId{4}, 2});

    // ================================================================
    // NEW: Advanced consumer goods (keeps Clothing, Electronics relevant)
    // ================================================================
    recipes.push_back({29, "Adv. Consumer Goods",
        {{goods::CLOTHING, 1}, {goods::ELECTRONICS, 1}, {goods::PLASTICS, 1}},
        goods::ADV_CONSUMER_GOODS, 1, BuildingId{3}, 1});

    // ================================================================
    // NEW: Glass, Rubber, Bronze (foundation materials)
    // ================================================================
    recipes.push_back({30, "Make Glass",
        {{goods::STONE, 1}, {goods::COAL, 1}},
        goods::GLASS, 2, BuildingId{0}, 1});

    recipes.push_back({31, "Process Rubber",
        {{goods::RUBBER, 2}},
        goods::RUBBER_GOODS, 1, BuildingId{2}, 1});

    recipes.push_back({32, "Smelt Bronze",
        {{goods::COPPER_ORE, 1}, {goods::TIN, 1}},
        goods::BRONZE, 1, BuildingId{0}, 1});

    recipes.push_back({33, "Weave Cotton Textiles",
        {{goods::COTTON, 2}},
        goods::TEXTILES, 2, BuildingId{8}, 1});

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
