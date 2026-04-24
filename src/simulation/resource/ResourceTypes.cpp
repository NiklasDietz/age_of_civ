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

    for (GoodDef& d : defs) {
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
    defs[goods::SILVER_ORE]  = {goods::SILVER_ORE,  "Silver Ore",  GoodCategory::RawStrategic, 22, false, 0.5f};
    defs[goods::NATURAL_GAS] = {goods::NATURAL_GAS, "Natural Gas", GoodCategory::RawStrategic, 20, true,  0.4f};

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
    defs[goods::SALT]       = {goods::SALT,        "Salt",        GoodCategory::RawBonus,   8, false, 0.3f};
    defs[goods::MARBLE]     = {goods::MARBLE,      "Marble",      GoodCategory::RawStrategic, 20, true, 0.4f};
    defs[goods::PEARLS]     = {goods::PEARLS,      "Pearls",      GoodCategory::RawLuxury, 28, false, 0.7f};
    defs[goods::TEA]        = {goods::TEA,         "Tea",         GoodCategory::RawLuxury, 16, false, 0.7f};
    defs[goods::COFFEE]     = {goods::COFFEE,      "Coffee",      GoodCategory::RawLuxury, 18, false, 0.7f};
    defs[goods::TOBACCO]    = {goods::TOBACCO,     "Tobacco",     GoodCategory::RawLuxury, 15, false, 0.7f};

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
    defs[goods::CHARCOAL]             = {goods::CHARCOAL,             "Charcoal",             GoodCategory::Processed, 10, false, 0.3f};
    defs[goods::DEUTERIUM]            = {goods::DEUTERIUM,            "Deuterium",            GoodCategory::Processed, 100, true, 0.5f};
    defs[goods::BIOFUEL]              = {goods::BIOFUEL,              "Biofuel",              GoodCategory::Processed, 30, false, 0.4f};
    defs[goods::GOLD_CONTACTS]        = {goods::GOLD_CONTACTS,        "Gold Contacts",        GoodCategory::Processed, 90, true,  0.6f};

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

    // Monetary goods (140+) -- low elasticity (stable value as currency)
    defs[goods::COPPER_COINS] = {goods::COPPER_COINS, "Copper Coins", GoodCategory::Monetary, 10, false, 0.2f};
    defs[goods::SILVER_COINS] = {goods::SILVER_COINS, "Silver Coins", GoodCategory::Monetary, 20, false, 0.2f};
    defs[goods::GOLD_BARS]   = {goods::GOLD_BARS,   "Gold Bars",   GoodCategory::Monetary, 40, false, 0.2f};

    // Automation goods
    defs[goods::ROBOT_WORKERS] = {goods::ROBOT_WORKERS, "Robot Workers", GoodCategory::Advanced, 300, false, 0.8f};
    defs[goods::HELIUM_3]       = {goods::HELIUM_3,       "Helium-3",       GoodCategory::RawStrategic, 400, true, 0.9f};
    defs[goods::TITANIUM]       = {goods::TITANIUM,       "Titanium",       GoodCategory::RawStrategic, 350, true, 0.85f};

    // WP-C2 additive goods.
    defs[goods::LITHIUM]         = {goods::LITHIUM,         "Lithium",         GoodCategory::RawStrategic, 280, true,  0.75f};
    defs[goods::BATTERIES]       = {goods::BATTERIES,       "Batteries",       GoodCategory::Processed,    160, true,  0.45f};
    defs[goods::ELECTRICITY]     = {goods::ELECTRICITY,     "Electricity",     GoodCategory::Processed,     60, true,  0.20f};
    defs[goods::PHARMACEUTICALS] = {goods::PHARMACEUTICALS, "Pharmaceuticals", GoodCategory::Advanced,     220, true,  0.50f};

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

    // Output amounts tuned up to raise market profitability so the ranked
    // recipe loop prioritises them once the Refinery is built.  Prior 1:1
    // ratios made these recipes a net wash against raw-oil sale price and
    // they sat at the bottom of the profitability ranking every turn.
    recipes.push_back({4, "Refine Fuel",
        {{goods::OIL, 2}},
        goods::FUEL, 2, BuildingId{2}, 1});

    recipes.push_back({5, "Produce Plastics",
        {{goods::OIL, 1}},
        goods::PLASTICS, 2, BuildingId{2}, 1});

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
        goods::ELECTRONICS, 2, BuildingId{4}, 2});

    recipes.push_back({11, "Consumer Goods",
        {{goods::PLASTICS, 1}, {goods::LUMBER, 1}},
        goods::CONSUMER_GOODS, 3, BuildingId{1}, 1});

    // ================================================================
    // Tier 4: Advanced goods (original)
    // ================================================================
    // Raised outputs on these downstream chain recipes so the profitability
    // ranker prefers them; previously Adv Machinery's 1:1:1 ratio against
    // two scarce inputs made it consistently lose to simpler recipes.
    recipes.push_back({12, "Advanced Machinery",
        {{goods::MACHINERY, 1}, {goods::ELECTRONICS, 1}},
        goods::ADVANCED_MACHINERY, 2, BuildingId{4}, 3});

    recipes.push_back({13, "Industrial Equipment",
        {{goods::ADVANCED_MACHINERY, 1}, {goods::STEEL, 2}},
        goods::INDUSTRIAL_EQUIP, 2, BuildingId{5}, 3});

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
    // Precision manufacturing chain.  Surface Plate used to be its own
    // tracked good, but it's a tech/flavour concept ("Precision
    // Engineering") not a tradeable commodity — so both entry recipes
    // now produce PRECISION_INSTRUMENTS directly.  Two paths: an Iron-age
    // basic version and an Industrial glass/copper-wire premium.
    // ================================================================
    recipes.push_back({18, "Build Precision Instruments (Basic)",
        {{goods::IRON_INGOTS, 2}, {goods::STONE, 1}},
        goods::PRECISION_INSTRUMENTS, 1, BuildingId{10}, 2});

    recipes.push_back({19, "Build Precision Instruments",
        {{goods::IRON_INGOTS, 1}, {goods::COPPER_WIRE, 1}, {goods::GLASS, 1}},
        goods::PRECISION_INSTRUMENTS, 2, BuildingId{10}, 2});

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

    // Software: two paths, reflecting the knowledge-economy idea that a
    // resource-poor civ with strong research infrastructure can still
    // export code.
    //   Premium: full Computers on hand (not consumed — "infrastructure")
    //             → Software 2 per batch, Research Lab.
    //   Bootstrap: Microchips consumed directly, no Computers needed
    //             → Software 1 per batch, Research Lab.  Lets a civ start
    //             a digital economy as soon as Microchips are available,
    //             without the full assembly chain.
    recipes.push_back({24, "Develop Software (Platform)",
        {{goods::COMPUTERS_GOOD, 1, false}},
        goods::SOFTWARE, 2, BuildingId{12}, 1});

    recipes.push_back({60, "Develop Software (Bootstrap)",
        {{goods::MICROCHIPS, 1}},
        goods::SOFTWARE, 1, BuildingId{12}, 1});

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

    // ================================================================
    // Minting: raw metal ore -> coins (requires Mint building)
    // ================================================================
    recipes.push_back({34, "Mint Copper Coins",
        {{goods::COPPER_ORE, 2}},
        goods::COPPER_COINS, 3, BuildingId{24}, 1});

    recipes.push_back({35, "Mint Silver Coins",
        {{goods::SILVER_ORE, 2}},
        goods::SILVER_COINS, 2, BuildingId{24}, 1,
        1, TechId{5}});  // Requires Currency tech

    recipes.push_back({36, "Smelt Gold Bars",
        {{goods::GOLD_ORE, 2}},
        goods::GOLD_BARS, 1, BuildingId{24}, 1,
        1, TechId{8}});  // Requires Metallurgy tech

    // ================================================================
    // Automation: Robot Workers (late-game)
    // ================================================================
    recipes.push_back({37, "Build Robot Workers",
        {{goods::MICROCHIPS, 1}, {goods::STEEL, 2}, {goods::ELECTRONICS, 1}},
        goods::ROBOT_WORKERS, 1, BuildingId{5}, 4});

    // ================================================================
    // Charcoal: early coal substitute from wood (less efficient)
    // Historically: charcoal was the primary fuel before coal mining.
    // 3 Wood -> 1 Charcoal (vs coal which comes from mining 1:1)
    // ================================================================
    recipes.push_back({38, "Burn Charcoal",
        {{goods::WOOD, 3}},
        goods::CHARCOAL, 1, BuildingId{0}, 1});  // Forge (kiln)

    // Charcoal-based steel (less efficient than coal: needs 3 charcoal vs 2 coal)
    recipes.push_back({39, "Produce Charcoal Steel",
        {{goods::IRON_ORE, 1}, {goods::CHARCOAL, 3}},
        goods::STEEL, 1, BuildingId{3}, 2});  // Factory, slower

    // Charcoal-based glass (less efficient: 2 charcoal vs 1 coal)
    recipes.push_back({40, "Make Glass (Charcoal)",
        {{goods::STONE, 1}, {goods::CHARCOAL, 2}},
        goods::GLASS, 1, BuildingId{0}, 1});  // Forge, less output than coal version

    // ================================================================
    // Salt-based food preservation (alternative Processed Food recipes)
    // ================================================================
    recipes.push_back({41, "Preserve Fish",
        {{goods::SALT, 1}, {goods::FISH, 2}},
        goods::PROCESSED_FOOD, 3, BuildingId{9}, 1});  // Food Proc. Plant

    recipes.push_back({42, "Salt Cure Meat",
        {{goods::SALT, 1}, {goods::CATTLE, 2}},
        goods::PROCESSED_FOOD, 3, BuildingId{9}, 1});

    // ================================================================
    // Marble construction (high-quality alternative)
    // ================================================================
    recipes.push_back({43, "Cut Marble Blocks",
        {{goods::MARBLE, 2}, {goods::LUMBER, 1}},
        goods::CONSTRUCTION_MAT, 2, BuildingId{1}, 1});  // Workshop, better ratio than brick+lumber

    // ================================================================
    // Biofuel: renewable fossil fuel substitute from crops
    // ================================================================
    // WP-D1: Wheat biofuel yields 2/run (was 1) to keep it competitive with
    // the Sugar recipe for wheat-heavy civs. Sugar still wins for sugar-heavy
    // civs on raw profit, but Wheat no longer starves.
    recipes.push_back({44, "Distill Biofuel (Wheat)",
        {{goods::WHEAT, 3}},
        goods::BIOFUEL, 2, BuildingId{33}, 1});  // Biofuel Plant

    recipes.push_back({45, "Distill Biofuel (Sugar)",
        {{goods::SUGAR, 3}},
        goods::BIOFUEL, 1, BuildingId{33}, 1});

    // ================================================================
    // Natural Gas processing — the map seeds NATURAL_GAS tiles but no
    // recipe consumed them, so gas sat unused.  Gas runs through the same
    // Refinery as oil (BuildingId{2}) but yields slightly less: 2 gas →
    // 1 fuel + 1 plastics.  This gives civs an OIL-chain alternative when
    // their territory has gas but no oil.  Gated by the same Refining
    // tech so the processing chain stays era-consistent.
    recipes.push_back({50, "Refine Natural Gas (Fuel)",
        {{goods::NATURAL_GAS, 2}},
        goods::FUEL, 2, BuildingId{2}, 1,
        1, TechId{12}});  // Refining

    recipes.push_back({51, "Crack Natural Gas (Plastics)",
        {{goods::NATURAL_GAS, 1}},
        goods::PLASTICS, 1, BuildingId{2}, 1,
        1, TechId{12}});  // Refining

    // ================================================================
    // Biogas — renewable gas substitute from livestock/food waste.
    // Available once Biofuel Plant is built; yields less than refined
    // natural gas but needs no gas tile.
    // ================================================================
    // WP-D2: Biogas output bumped 1→2 so the ranker actually picks it over
    // wheat/sugar biofuel in civs with surplus cattle + wood. Same building
    // (33) as biofuel recipes, so preference still matters — but now the
    // profit margin is competitive.
    recipes.push_back({52, "Brew Biogas",
        {{goods::CATTLE, 2}, {goods::WOOD, 1}},
        goods::NATURAL_GAS, 2, BuildingId{33}, 1});

    // ================================================================
    // Orphan-good activation: recipes that connect previously-idle raw
    // resources (Clay, Rice) into the mainstream chain, and recipes that
    // give dead-end processed goods (Rubber Goods, Industrial Equipment,
    // Aluminium, Bronze) a downstream consumer so they are worth making.
    // ================================================================

    // Clay-fired bricks — alternative to Stone bricks, available from the
    // Workshop without needing a Quarry or Mountain Mine.  Keeps CLAY tiles
    // useful before Masonry unlocks.
    recipes.push_back({53, "Fire Clay Bricks",
        {{goods::CLAY, 2}},
        goods::BRICKS, 2, BuildingId{1}, 1});

    // Rice → Processed Food: tropical/subtropical food chain entry.  Was
    // the only raw food (WHEAT, CATTLE, FISH covered) without a processing
    // recipe, so RICE tiles were pure market goods.
    recipes.push_back({54, "Process Rice",
        {{goods::RICE, 2}},
        goods::PROCESSED_FOOD, 2, BuildingId{9}, 1});

    // (Aluminium already flows directly into Aircraft Components recipe 25
    //  — no separate smelting step.  Attempted a self-loop refining recipe
    //  but that blew out the profitability ranker because the 3:2 ratio
    //  compounded infinitely.  Leaving raw → aircraft as the single step.)

    // Bronze tools — dead-end good gets a simple downstream use as a Tools
    // input branch.  3 Bronze + 1 Wood → 2 Tools, alternative to the iron
    // path (recipe 6).  Lets early bronze age civs produce tools before
    // Iron Working.
    recipes.push_back({56, "Forge Bronze Tools",
        {{goods::BRONZE, 3}, {goods::WOOD, 1}},
        goods::TOOLS, 2, BuildingId{0}, 1});

    // Rubber Goods consumer: turn the previously-dead RUBBER_GOODS output
    // into a Machinery input variant.  Rubber + Tools + Steel → Machinery
    // runs alongside the standard Tools+Steel recipe (9) but uses the
    // rubber branch so Rubber tiles have downstream value.
    recipes.push_back({57, "Rubber-Sealed Machinery",
        {{goods::RUBBER_GOODS, 1}, {goods::TOOLS, 1}, {goods::STEEL, 1}},
        goods::MACHINERY, 2, BuildingId{3}, 2});

    // Industrial Equipment → Armored Vehicles input.  Was a capstone dead
    // end (nothing consumed it).  Now acts as a "heavy machinery" input
    // that gives Armored Vehicles a 50% output bump when present.  Simpler
    // than restructuring the armored-vehicles recipe — this is an
    // independent premium variant.
    recipes.push_back({58, "Heavy-Plated Armored Vehicles",
        {{goods::INDUSTRIAL_EQUIP, 1}, {goods::STEEL, 2}, {goods::FUEL, 1}},
        goods::ARMORED_VEHICLES, 2, BuildingId{3}, 3});

    // ================================================================
    // WP-C2 additive goods: producer recipes so the ranker wakes them up.
    // Skipped ELECTRICITY (power-plant side of the graph handles it) and
    // raw LITHIUM placement (tile-seed pass, handled elsewhere).
    // ================================================================

    // Batteries: Lithium + Copper Ore → Batteries. Electronics Plant (4).
    // Gated by Electricity tech (14). Downstream consumer: future advanced
    // consumer goods + EV vehicles. Producer recipe seeds the chain.
    recipes.push_back({61, "Assemble Batteries",
        {{goods::LITHIUM, 1}, {goods::COPPER_ORE, 1}},
        goods::BATTERIES, 1, BuildingId{4}, 2,
        1, TechId{14}});

    // Pharmaceuticals: Plastics + Glass → Pharmaceuticals. Industrial
    // Complex (5). Gated by Advanced Chemistry (24).
    recipes.push_back({62, "Synthesize Pharmaceuticals",
        {{goods::PLASTICS, 1}, {goods::GLASS, 1}},
        goods::PHARMACEUTICALS, 1, BuildingId{5}, 2,
        1, TechId{24}});

    // (Uranium intentionally has no refining recipe in this game — Nuclear
    //  Plant consumes it raw.  A self-loop refiner would break the ranker
    //  the same way Aluminium did.)

    // ================================================================
    // Demonetization: melt old coins back into raw ore.
    // Copper coins become copper ore (feeds electronics chain: ore -> wire -> electronics).
    // Silver coins become silver ore (future silver processing chains).
    // Available whenever a player has coins and a Forge -- the AI decides
    // when to melt (only after the coin type is demonetized).
    // ================================================================
    recipes.push_back({46, "Melt Copper Coins",
        {{goods::COPPER_COINS, 3}},
        goods::COPPER_ORE, 2, BuildingId{0}, 1,
        1, TechId{5}, true});  // Forge, Currency tech, recycling

    recipes.push_back({47, "Melt Silver Coins",
        {{goods::SILVER_COINS, 2}},
        goods::SILVER_ORE, 1, BuildingId{0}, 1,
        1, TechId{9}, true});  // Forge, Banking tech, recycling

    // ================================================================
    // WP-C2 cut: GOLD_CONTACTS chain deprecated. Recipes 48/49 removed —
    // Gold ore no longer bottlenecks electronics. Premium microchip tier
    // can be reintroduced later keyed off an active good (e.g. LITHIUM or
    // PHARMACEUTICALS) rather than a dead-end intermediate.
    // ================================================================

    // ================================================================
    // Worker slots: advanced recipes need more educated workers per batch.
    // This is the natural "human capital bottleneck":
    //   - Tier 1 (raw processing): 1 worker slot (default)
    //   - Tier 2 (tools, steel): 1 worker slot
    //   - Tier 3 (machinery, electronics): 2 worker slots
    //   - Tier 4 (computers, semiconductors, aircraft): 3 worker slots
    //
    // A city with pop 10 has 5 worker slots. It can run:
    //   5 basic recipes, OR 2 advanced + 1 basic, OR 1 high-tech + 1 advanced
    //
    // Resource-rich cities spend workers on raw extraction → can't do advanced.
    // Education-focused cities with labs/factories → run high-tier recipes.
    // This IS the resource curse: raw materials don't make you rich, human
    // capital does. Small educated nations naturally out-produce large
    // resource-dependent ones in high-value goods.
    // ================================================================
    for (ProductionRecipe& r : recipes) {
        switch (r.recipeId) {
            // Tier 3: 2 worker slots
            case 9:   // Machinery
            case 10:  // Electronics
            case 18:  // Surface Plate
            case 19:  // Precision Instruments
            case 20:  // Interchangeable Parts
            case 23:  // Computers
            case 28:  // Telecom Equipment
            case 29:  // Advanced Consumer Goods
                r.workerSlots = 2;
                break;
            // Tier 4: 3 worker slots
            case 12:  // Advanced Machinery
            case 13:  // Industrial Equipment
            case 21:  // Semiconductors
            case 22:  // Microchips
            case 24:  // Software
            case 25:  // Aircraft Components
            case 26:  // Aircraft
            case 27:  // Armored Vehicles
            case 37:  // Robot Workers
                r.workerSlots = 3;
                break;
            default:
                break;  // Stays at 1
        }
    }

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
