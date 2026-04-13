#pragma once

/**
 * @file Governor.hpp
 * @brief City governor system for automated city management.
 *
 * Instead of micromanaging every city every turn, players can assign a
 * "focus" to each city. The governor AI then makes building, district,
 * tile, and production decisions automatically based on the focus.
 *
 * Focus types:
 *   - Growth:     Prioritize food, farms, granary. Grow population fast.
 *   - Production: Prioritize mines, industrial buildings. Maximize hammers.
 *   - Science:    Prioritize campus, library, university. Maximize beakers.
 *   - Gold:       Prioritize commercial, trade, luxury. Maximize income.
 *   - Military:   Prioritize barracks, walls, military units. Defend/attack.
 *   - Balanced:   Default. Equal weight across all categories.
 *   - Manual:     Player controls everything. Governor does nothing.
 *
 * The governor also handles:
 *   - Auto-assigning worked tiles (best tiles for the focus)
 *   - Auto-queueing production when the queue is empty
 *   - Auto-expanding borders toward valuable tiles
 */

#include "aoc/core/Types.hpp"

#include <cstdint>

namespace aoc::game { class GameState; }
namespace aoc::game { class City; }
namespace aoc::map { class HexGrid; }

namespace aoc::sim {

enum class CityFocus : uint8_t {
    Balanced,    ///< Equal priority across all yields
    Growth,      ///< Maximize food and population growth
    Production,  ///< Maximize production (hammers)
    Science,     ///< Maximize science (beakers)
    Gold,        ///< Maximize gold income
    Military,    ///< Prioritize military buildings and units

    Count
};

[[nodiscard]] constexpr const char* cityFocusName(CityFocus focus) {
    switch (focus) {
        case CityFocus::Balanced:   return "Balanced";
        case CityFocus::Growth:     return "Growth";
        case CityFocus::Production: return "Production";
        case CityFocus::Science:    return "Science";
        case CityFocus::Gold:       return "Gold";
        case CityFocus::Military:   return "Military";
        default:                    return "Unknown";
    }
}

// ============================================================================
// Named Governors (Civ 6 style, with promotion trees)
// ============================================================================

/// Unique named governors with specialized bonuses.
enum class GovernorType : uint8_t {
    None           = 0,
    Financier      = 1,  ///< +20% gold, trade bonuses
    Industrialist  = 2,  ///< +15% production, factory bonuses
    Diplomat       = 3,  ///< +8 loyalty, spy resistance
    General        = 4,  ///< +5 combat strength garrison, wall bonuses
    Scholar        = 5,  ///< +15% science, great scientist points
    Merchant       = 6,  ///< +1 trade route, +30% trade yield
    Environmentalist = 7, ///< -50% pollution, clean energy bonuses

    Count
};

inline constexpr int32_t NAMED_GOVERNOR_COUNT = 7;

/// Governor promotion IDs (5 per governor = 35 total).
enum class GovernorPromotion : uint8_t {
    None = 0,
    // Financier
    TaxHaven, ForeignInvestment, MintMaster, TreasuryGuard, BondMarket,
    // Industrialist
    AutomatedFactory, ZoningCommissioner, ResourceProcessor, PowerSurge, PollutionControl,
    // Diplomat
    SpyMaster, CulturalAttache, PeaceKeeper, TradeEnvoy, GarrisonBoost,
    // General
    Citadel, Militia, WarHero, SupplyDepot, Fortifier,
    // Scholar
    ResearchGrant, EurekaBoost, Innovation, TechTransfer, LibraryBonus,
    // Merchant
    FreeMarket, Smuggler, MarketMaker, Monopolist, PortAuthority,
    // Environmentalist
    GreenEnergy, NationalPark, ReforestationGrant, WasteRecycling, CarbonCredit,

    Count
};

/// Per-city governor state (ECS component).
struct CityGovernorComponent {
    CityFocus focus = CityFocus::Balanced;

    /// Whether the governor is active (false = manual control).
    bool isActive = false;

    /// Auto-queue production when queue empties.
    bool autoQueueProduction = true;

    /// Auto-assign best worked tiles for the focus.
    bool autoAssignTiles = true;

    // --- Named governor (Civ 6 style) ---
    GovernorType assignedGovernor = GovernorType::None;
    GovernorPromotion promotions[3] = {GovernorPromotion::None,
                                        GovernorPromotion::None,
                                        GovernorPromotion::None};
    int32_t promotionCount = 0;
    int32_t turnsActive = 0;

    [[nodiscard]] bool hasNamedGovernor() const {
        return this->assignedGovernor != GovernorType::None;
    }

    [[nodiscard]] bool hasPromotion(GovernorPromotion promo) const {
        for (int32_t i = 0; i < this->promotionCount; ++i) {
            if (this->promotions[i] == promo) { return true; }
        }
        return false;
    }

    bool addPromotion(GovernorPromotion promo) {
        if (this->promotionCount >= 3) { return false; }
        this->promotions[this->promotionCount++] = promo;
        return true;
    }

    /// Governor bonuses (base, before promotions).
    [[nodiscard]] float goldMultiplier() const {
        if (this->assignedGovernor == GovernorType::Financier) { return 1.20f; }
        if (this->assignedGovernor == GovernorType::Merchant) { return 1.10f; }
        return 1.0f;
    }
    [[nodiscard]] float productionMultiplier() const {
        if (this->assignedGovernor == GovernorType::Industrialist) { return 1.15f; }
        return 1.0f;
    }
    [[nodiscard]] float scienceMultiplier() const {
        if (this->assignedGovernor == GovernorType::Scholar) { return 1.15f; }
        return 1.0f;
    }
    [[nodiscard]] float loyaltyBonus() const {
        if (this->assignedGovernor == GovernorType::Diplomat) { return 8.0f; }
        if (this->assignedGovernor != GovernorType::None) { return 4.0f; }
        return 0.0f;
    }
};

/**
 * @brief Run the governor for a city: auto-queue production based on focus.
 *
 * Called when a city's production queue is empty and the governor is active.
 * Selects the best item to produce based on the city's focus and build constraints.
 *
 * @param gameState  Full game state (needed for tech/civic checks).
 * @param grid       Hex grid.
 * @param city       The city to manage.
 * @param player     Owning player.
 */
void governorAutoQueue(aoc::game::GameState& gameState,
                        const aoc::map::HexGrid& grid,
                        aoc::game::City& city,
                        PlayerId player);

/**
 * @brief Run all governors for a player's cities.
 *
 * Called once per turn during per-player processing.
 * For each city with an active governor, handles auto-queuing
 * and tile assignment.
 */
void processGovernors(aoc::game::GameState& gameState,
                       const aoc::map::HexGrid& grid,
                       PlayerId player);

} // namespace aoc::sim
