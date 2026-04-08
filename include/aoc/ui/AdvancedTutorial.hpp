#pragma once

/**
 * @file AdvancedTutorial.hpp
 * @brief Advanced tutorial steps for unique game mechanics.
 *
 * The base tutorial covers 10 steps (move, found city, build, research, etc.)
 * This adds 15 more steps covering the advanced economic and political systems:
 *
 *   11. Monetary System: Explains barter -> commodity -> gold standard -> fiat
 *   12. Minting Coins: Build a Mint, produce copper/silver/gold coins
 *   13. Trade Routes: Establish domestic and international trade
 *   14. Production Chains: Raw -> Processed -> Advanced goods
 *   15. Building Capacity: Upgrade buildings for more throughput
 *   16. Power Grid: Build power plants, manage energy demand
 *   17. Industrial Revolution: Requirements and benefits
 *   18. Government & Policies: Choose government, slot policy cards
 *   19. Corruption & Empire Size: Communication speed, regional capitals
 *   20. Bonds & Financial Warfare: Issue bonds, financial leverage
 *   21. Sanctions & Currency Wars: Economic weapons
 *   22. Victory Conditions: CSI scoring, era evaluations, integration project
 *   23. Quality & Specialization: Production experience, quality tiers
 *   24. Naval Trade: Merchant ships, navigable rivers
 *   25. Barbarian Clans: Bribe, hire, or convert
 */

#include <cstdint>
#include <string_view>

namespace aoc::sim {

struct AdvancedTutorialStep {
    uint8_t          stepId;
    std::string_view title;
    std::string_view description;
    std::string_view hint;
};

inline constexpr AdvancedTutorialStep ADVANCED_TUTORIAL_STEPS[] = {
    {11, "The Monetary System",
     "Your economy evolves through 4 stages: Barter, Commodity Money, Gold Standard, and Fiat Money.\n"
     "Each stage unlocks more trade capacity and economic tools, but also more risk.\n"
     "Build a Mint and mine metals to start minting coins.",
     "Build a Mint in a city with a Commercial Hub district."},

    {12, "Minting Coins",
     "Coins are produced from raw metal ore at a Mint building.\n"
     "Copper coins enable local trade. Silver extends to regional trade. Gold enables international trade.\n"
     "Your effective coin tier depends on which metals you actually hold.",
     "Mine copper, silver, or gold ore and the Mint will produce coins automatically."},

    {13, "Trade Routes",
     "Domestic trade automatically balances surplus goods between your cities.\n"
     "International trade requires bilateral agreements with other players.\n"
     "Sea and river trade carries 5-10x more cargo than land trade.",
     "Build a Merchant Barge to boost river trade capacity."},

    {14, "Production Chains",
     "Raw resources are processed through multi-tier production chains.\n"
     "Iron Ore -> Iron Ingots -> Tools -> Machinery -> Advanced Machinery\n"
     "Each step requires a specific building and input goods.",
     "Check the Encyclopedia (W key) for all production recipes."},

    {15, "Building Capacity & Quality",
     "Each building has a maximum throughput (batches per turn).\n"
     "Upgrade buildings (Lv1->Lv2->Lv3) to increase capacity.\n"
     "Higher building levels also improve output quality (Standard -> High -> Premium).",
     "Specialized cities with experience produce better quality goods."},

    {16, "Power Grid",
     "Advanced industrial buildings require energy to operate.\n"
     "Without sufficient power, buildings run at reduced efficiency (brownout).\n"
     "Build power plants: Coal (cheap, polluting) or Hydroelectric (free, requires river).",
     "Check your city's energy demand vs supply in the city detail screen."},

    {17, "Industrial Revolutions",
     "Five transformative economic eras unlock as you research techs and gather resources.\n"
     "1st (Steam): Railways, +50% production. 2nd (Electric): +25% prod, global trade.\n"
     "3rd (Digital): Highways, automation. 4th (Information): +30% science. 5th: Clean energy.",
     "The 1st Industrial Revolution requires Coal + Iron + Steam tech."},

    {18, "Government & Policies",
     "Your government type determines policy slots, corruption rate, and a unique action.\n"
     "22 policy cards cover military, economic, diplomatic, and wildcard effects.\n"
     "Changing government causes 5 turns of anarchy -- choose carefully!",
     "Press G to open the Government screen and review your options."},

    {19, "Empire Size & Communication",
     "Large empires face communication penalties: distant cities have less loyalty,\n"
     "higher corruption, and lower productivity. Better communication tech reduces this.\n"
     "Regional capitals (cities with Banks) serve as local hubs.",
     "Build roads and research communication techs to maintain distant cities."},

    {20, "Bonds & Financial Warfare",
     "Issue government bonds to raise cash. Other players buy them as investments.\n"
     "Holding 30%+ of another player's bonds gives you financial leverage.\n"
     "You can dump bonds to crash their economy -- a powerful but hostile action.",
     "Be careful with debt: if you can't pay interest, you default!"},

    {21, "Sanctions & Currency Wars",
     "Economic sanctions restrict trade: embargoes, financial sanctions, asset freezes.\n"
     "Currency devaluation makes your exports cheaper but imports more expensive.\n"
     "If 3+ players devalue simultaneously: Race to the Bottom (global trade -20%).",
     "Sanctions are expensive for both sides -- use them strategically."},

    {22, "Victory Conditions",
     "There's no single 'first to X' win. Your Civilization Score Index (CSI) is\n"
     "scored across 8 categories, with multipliers for trade and diplomacy.\n"
     "Isolating yourself HURTS your score. Engagement is rewarded.",
     "Era evaluations every 30 turns award Victory Points to top performers."},

    {23, "Waste & Pollution",
     "Industrial production creates waste that accumulates in cities.\n"
     "High pollution: -food, -amenities, -growth. Build a Waste Treatment Plant.\n"
     "Pollution flows downstream via rivers, affecting neighboring cities.",
     "Balance industrial output with environmental management."},

    {24, "Naval Trade & Rivers",
     "Rivers with 3+ upstream tiles are navigable by ships.\n"
     "River cities are valuable trade hubs. Build barges for early river trade.\n"
     "One merchant ship carries as much as 10 road wagons.",
     "Build a Harbor district in coastal/river cities for maximum trade."},

    {25, "Barbarian Clans",
     "Barbarian encampments belong to named clans that can be interacted with.\n"
     "Bribe them for 20 turns of peace. Hire them to attack your enemies.\n"
     "Or pay to convert them into a friendly city-state.",
     "Destroying a barbarian encampment gives gold and XP rewards."},
};

inline constexpr int32_t ADVANCED_TUTORIAL_STEP_COUNT = 15;

} // namespace aoc::sim
