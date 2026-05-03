#pragma once

/**
 * @file IndustrialRevolution.hpp
 * @brief Industrial revolution framework: 5 transformative economic eras.
 *
 * Each industrial revolution is triggered by a combination of technologies
 * and resources, and transforms the economy when achieved:
 *
 *   1st (Steam Age):     Steam Engine + Coal + Iron
 *      -> Factories, Railways, Steam power. Production x1.5 for industrial buildings.
 *         Pollution begins. Urbanization pressure (+population growth in factory cities).
 *
 *   2nd (Electric Age):  Electricity + Steel + Oil
 *      -> Electric grid, Assembly lines, Telegraph. Mass production (+25% factory output).
 *         Global trade routes enabled. New unit types (ironclad, etc).
 *
 *   3rd (Digital Age):   Computers + Semiconductors + Telecom
 *      -> Automation begins, Highways, Containerized shipping. Services economy.
 *         Information trade (Software as export). Suburbanization.
 *
 *   4th (Information Age): Internet + Software + AI
 *      -> Digital trade, Platform economy, Fintech. Data as resource.
 *         Remote work (cities with Software produce science remotely).
 *
 *   5th (Post-Industrial): Fusion + Quantum Computing + Biotech
 *      -> Clean energy everywhere, Full automation, Space industry.
 *         Post-scarcity possible (food/production no longer limiting).
 *
 * A player achieves a revolution by having all required techs researched
 * AND all required resources accessible. The revolution is per-player --
 * one civ can be in the 3rd while another is still in the 1st.
 *
 * Each revolution grants permanent bonuses + unlocks new capabilities.
 * The bonuses stack (achieving the 3rd also includes the 1st and 2nd bonuses).
 */

#include "aoc/core/Types.hpp"

#include <array>
#include <cstdint>
#include <string_view>

namespace aoc::game { class GameState; }

namespace aoc::sim {

// ============================================================================
// Revolution definitions
// ============================================================================

enum class IndustrialRevolutionId : uint8_t {
    None        = 0,
    First       = 1,  ///< Steam Age
    Second      = 2,  ///< Electric Age
    Third       = 3,  ///< Digital Age
    Fourth      = 4,  ///< Information Age
    Fifth       = 5,  ///< Post-Industrial

    Count
};

struct RevolutionRequirement {
    TechId requiredTechs[3];     ///< Up to 3 required techs (INVALID = unused slot)
    uint16_t requiredGoods[3];   ///< Up to 3 required resources in stockpile (0xFFFF = unused)
    int32_t minCityCount;        ///< Minimum cities to trigger
};

struct RevolutionBonuses {
    float productionMultiplier;       ///< Stacking multiplier on industrial production
    float tradeCapacityMultiplier;    ///< Bonus to all trade route capacity
    float scienceMultiplier;          ///< Science output bonus
    float goldPerCitizenBonus;        ///< Extra gold per citizen (knowledge/services economy)
    float populationGrowthBonus;      ///< Growth rate boost in industrial cities
    float pollutionMultiplier;        ///< How much pollution is generated (higher = more)
    bool  unlockRailways;             ///< Enables Railway improvement
    bool  unlockHighways;             ///< Enables Highway improvement
    bool  unlockAutomation;           ///< Enables Robot Workers recipe
    bool  unlockCleanEnergy;          ///< Enables Solar/Wind power plants
};

struct RevolutionDef {
    IndustrialRevolutionId id;
    std::string_view       name;
    RevolutionRequirement  requirements;
    RevolutionBonuses      bonuses;
};

// Forward-declared good IDs to avoid circular include
namespace rev_goods {
    inline constexpr uint16_t COAL = 2;
    inline constexpr uint16_t CHARCOAL = 79;
    inline constexpr uint16_t IRON_ORE = 0;
    inline constexpr uint16_t OIL = 3;
    inline constexpr uint16_t STEEL_GOOD = 64;
    inline constexpr uint16_t SEMICONDUCTORS_GOOD = 75;
    inline constexpr uint16_t SOFTWARE_GOOD = 108;
    inline constexpr uint16_t NONE = 0xFFFF;
}

inline constexpr std::array<RevolutionDef, 5> REVOLUTION_DEFS = {{
    // 1st: Steam Age -- Industrialization(11) + Surface Plate(18) + Food Preservation(21).
    // Three era-4 techs gate the revolution. Reach-rate driven by AI research
    // prioritisation, not by removing requirements. Goods: Charcoal + Iron Ore.
    {IndustrialRevolutionId::First, "Steam Age",
     {{TechId{11}, TechId{18}, TechId{21}},
      {rev_goods::CHARCOAL, rev_goods::IRON_ORE, rev_goods::NONE}, 1},
     {1.50f, 1.0f, 1.0f, 0.5f, 1.15f, 1.5f, true, false, false, false}},

    // 2nd: Electric Age -- Electricity(14) + Precision Instruments(22) + Steel(47).
    // 2026-05-03: replaced Telecommunications(25) requirement. Telecom is
    // DOWNSTREAM of electricity historically (telegraph 1840s, telephone
    // 1876, radio 1890s — all post-electrification). Steel(47) is the right
    // companion gate: Bessemer process + electric arc furnace are the
    // hallmark Electric Age technologies. Civ6 doesn't bundle Telecom
    // into Electric Age either.
    {IndustrialRevolutionId::Second, "Electric Age",
     {{TechId{14}, TechId{22}, TechId{47}},
      {rev_goods::OIL, rev_goods::STEEL_GOOD, rev_goods::NONE}, 2},
     {1.25f, 1.5f, 1.10f, 1.0f, 1.10f, 1.3f, true, false, false, false}},

    // 3rd: Digital Age -- Computers(16) + Semiconductors tech(23) + Semiconductors good
    // Knowledge economy: each citizen generates significant gold (services sector).
    {IndustrialRevolutionId::Third, "Digital Age",
     {{TechId{16}, TechId{23}, TechId{}},
      {rev_goods::SEMICONDUCTORS_GOOD, rev_goods::NONE, rev_goods::NONE}, 2},
     {1.15f, 2.0f, 1.20f, 2.0f, 1.0f, 1.0f, true, true, true, false}},

    // 4th: Information Age -- Internet(27) + Computers(16) + Software
    // Small tech-savvy nations can become economic powerhouses (Singapore model).
    {IndustrialRevolutionId::Fourth, "Information Age",
     {{TechId{27}, TechId{16}, TechId{}},
      {rev_goods::SOFTWARE_GOOD, rev_goods::NONE, rev_goods::NONE}, 2},
     {1.10f, 2.5f, 1.30f, 3.0f, 1.0f, 0.8f, true, true, true, false}},

    // 5th: Post-Industrial -- Nuclear Fission(17) + expanded Fusion(64)
    {IndustrialRevolutionId::Fifth, "Post-Industrial",
     {{TechId{17}, TechId{64}, TechId{}},
      {rev_goods::NONE, rev_goods::NONE, rev_goods::NONE}, 2},
     {1.20f, 3.0f, 1.50f, 5.0f, 1.20f, 0.5f, true, true, true, true}},
}};

[[nodiscard]] inline constexpr const RevolutionDef& revolutionDef(IndustrialRevolutionId id) {
    return REVOLUTION_DEFS[static_cast<uint8_t>(id) - 1];
}

// ============================================================================
// Per-player industrial revolution state (ECS component)
// ============================================================================

struct PlayerIndustrialComponent {
    PlayerId owner = INVALID_PLAYER;
    IndustrialRevolutionId currentRevolution = IndustrialRevolutionId::None;
    int32_t turnAchieved[6] = {};  ///< Turn when each revolution was achieved (0 = not yet)

    /// Cumulative production multiplier from all achieved revolutions.
    [[nodiscard]] float cumulativeProductionMultiplier() const {
        float mult = 1.0f;
        for (uint8_t r = 1; r <= static_cast<uint8_t>(this->currentRevolution); ++r) {
            mult *= REVOLUTION_DEFS[r - 1].bonuses.productionMultiplier;
        }
        return mult;
    }

    /// Cumulative trade capacity multiplier.
    [[nodiscard]] float cumulativeTradeMultiplier() const {
        float mult = 1.0f;
        for (uint8_t r = 1; r <= static_cast<uint8_t>(this->currentRevolution); ++r) {
            mult *= REVOLUTION_DEFS[r - 1].bonuses.tradeCapacityMultiplier;
        }
        return mult;
    }

    /// Cumulative science multiplier.
    [[nodiscard]] float cumulativeScienceMultiplier() const {
        float mult = 1.0f;
        for (uint8_t r = 1; r <= static_cast<uint8_t>(this->currentRevolution); ++r) {
            mult *= REVOLUTION_DEFS[r - 1].bonuses.scienceMultiplier;
        }
        return mult;
    }

    /// Cumulative gold-per-citizen bonus from all achieved revolutions.
    /// This represents the shift from land-based to knowledge/services economy:
    /// post-industrial nations generate wealth from citizens, not territory.
    [[nodiscard]] float cumulativeGoldPerCitizen() const {
        float total = 0.0f;
        for (uint8_t r = 1; r <= static_cast<uint8_t>(this->currentRevolution); ++r) {
            total += REVOLUTION_DEFS[r - 1].bonuses.goldPerCitizenBonus;
        }
        return total;
    }

    /// Whether railways are unlocked (1st revolution+).
    [[nodiscard]] bool hasRailways() const {
        return static_cast<uint8_t>(this->currentRevolution) >= 1;
    }

    /// Whether highways are unlocked (3rd revolution+).
    [[nodiscard]] bool hasHighways() const {
        return static_cast<uint8_t>(this->currentRevolution) >= 3;
    }

    /// Whether automation (Robot Workers) is unlocked (3rd revolution+).
    [[nodiscard]] bool hasAutomation() const {
        return static_cast<uint8_t>(this->currentRevolution) >= 3;
    }

    /// Whether clean energy (Solar/Wind) is unlocked (5th revolution).
    [[nodiscard]] bool hasCleanEnergy() const {
        return static_cast<uint8_t>(this->currentRevolution) >= 5;
    }
};

// ============================================================================
// Revolution checking
// ============================================================================

/**
 * @brief Check if a player qualifies for the next industrial revolution.
 *
 * Checks tech prerequisites and resource availability.
 * If qualified, advances the player's revolution state and applies bonuses.
 *
 * @param world       ECS world.
 * @param player      Player to check.
 * @param currentTurn Current turn number (for recording achievement turn).
 * @return true if a new revolution was achieved this turn.
 */
bool checkIndustrialRevolution(aoc::game::GameState& gameState, PlayerId player,
                               TurnNumber currentTurn);

/**
 * @brief Get the cumulative pollution multiplier from all achieved revolutions.
 */
[[nodiscard]] float revolutionPollutionMultiplier(const PlayerIndustrialComponent& ind);

} // namespace aoc::sim
