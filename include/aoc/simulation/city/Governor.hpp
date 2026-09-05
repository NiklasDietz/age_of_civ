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
#include "aoc/core/ErrorCodes.hpp"
#include "aoc/map/HexCoord.hpp"
#include <string_view>

namespace aoc::game { class GameState; }
namespace aoc::game { class City; }
namespace aoc::game { class Player; }
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
    /// Governor bonuses; the first title of each tree adds to them (2026-09-05):
    /// Tax Haven +10% gold, Automated Factory +10% production, Research Grant
    /// +10% science, Citadel +4 loyalty. Applied in Maintenance.cpp, ProductionSystem.cpp,
    /// CityScience.cpp and CityLoyalty.cpp.
    [[nodiscard]] float goldMultiplier() const {
        float m = 1.0f;
        if (this->assignedGovernor == GovernorType::Financier) { m = 1.20f; }
        if (this->assignedGovernor == GovernorType::Merchant) { m = 1.10f; }
        if (this->hasPromotion(GovernorPromotion::TaxHaven)) { m += 0.10f; }
        return m;
    }
    [[nodiscard]] float productionMultiplier() const {
        float m = 1.0f;
        if (this->assignedGovernor == GovernorType::Industrialist) { m = 1.15f; }
        if (this->hasPromotion(GovernorPromotion::AutomatedFactory)) { m += 0.10f; }
        return m;
    }
    [[nodiscard]] float scienceMultiplier() const {
        float m = 1.0f;
        if (this->assignedGovernor == GovernorType::Scholar) { m = 1.15f; }
        if (this->hasPromotion(GovernorPromotion::ResearchGrant)) { m += 0.10f; }
        return m;
    }
    [[nodiscard]] float loyaltyBonus() const {
        float b = 0.0f;
        if (this->assignedGovernor == GovernorType::Diplomat) { b = 8.0f; }
        else if (this->assignedGovernor != GovernorType::None) { b = 4.0f; }
        if (this->hasPromotion(GovernorPromotion::Citadel)) { b += 4.0f; }
        return b;
    }
};

[[nodiscard]] constexpr std::string_view governorTypeName(GovernorType type) {
    switch (type) {
        case GovernorType::Financier:        return "Financier";
        case GovernorType::Industrialist:    return "Industrialist";
        case GovernorType::Diplomat:         return "Diplomat";
        case GovernorType::General:          return "General";
        case GovernorType::Scholar:          return "Scholar";
        case GovernorType::Merchant:         return "Merchant";
        case GovernorType::Environmentalist: return "Environmentalist";
        default:                             return "None";
    }
}

/// The governor a promotion belongs to: five titles per tree, in enum order.
[[nodiscard]] constexpr GovernorType governorForPromotion(GovernorPromotion promo) {
    const uint8_t v = static_cast<uint8_t>(promo);
    if (v == 0 || v >= static_cast<uint8_t>(GovernorPromotion::Count)) { return GovernorType::None; }
    return static_cast<GovernorType>((v - 1) / 5 + 1);
}

/// Titles with an effect today (the others are recruitable names only, see the
/// Open debt note): the first title of the Financier, Industrialist, Scholar and
/// General trees, plus Peace Keeper (+10 favor / turn) and Carbon Credit (+5).
[[nodiscard]] constexpr bool governorPromotionHasEffect(GovernorPromotion promo) {
    switch (promo) {
        case GovernorPromotion::TaxHaven:
        case GovernorPromotion::AutomatedFactory:
        case GovernorPromotion::ResearchGrant:
        case GovernorPromotion::Citadel:
        case GovernorPromotion::PeaceKeeper:
        case GovernorPromotion::CarbonCredit:
            return true;
        default:
            return false;
    }
}

/// Titles: one per five completed civics. A title recruits a governor the player
/// does not have yet (moving one between cities is free) or buys a promotion.
inline constexpr int32_t CIVICS_PER_GOVERNOR_TITLE = 5;
[[nodiscard]] int32_t governorTitlesEarned(const aoc::game::Player& player);
[[nodiscard]] int32_t governorTitlesSpent(const aoc::game::Player& player);
[[nodiscard]] int32_t governorTitlesAvailable(const aoc::game::Player& player);

/// Favor per turn from Peace Keeper (+10) and Carbon Credit (+5) titles across cities.
[[nodiscard]] int32_t governorFavorPerTurn(const aoc::game::Player& player);

/// Assign `type` to the player's city at `cityAt`. A type already seated elsewhere
/// moves (free); a new type costs a title. InvalidArgument for an unknown seat,
/// city or type; InvalidState when no title is available.
[[nodiscard]] ErrorCode requestAssignGovernor(aoc::game::GameState& gameState, PlayerId player,
                                              hex::AxialCoord cityAt, GovernorType type);

/// Buy `promotion` for the governor seated at `cityAt`. InvalidArgument for an
/// unknown seat / city, no governor, or a title from another tree; InvalidUnitAction
/// when already held or the three slots are full; InvalidState without a title.
[[nodiscard]] ErrorCode requestPromoteGovernor(aoc::game::GameState& gameState, PlayerId player,
                                               hex::AxialCoord cityAt, GovernorPromotion promotion);

/// The AI spends every available title: seat governors (capital first, then by
/// population; Scholar for a Campus, Industrialist for an Industrial Zone,
/// Financier for the capital, Diplomat otherwise), then the first effective
/// promotion of each seated tree. Deterministic.
void aiSpendGovernorTitles(aoc::game::GameState& gameState, PlayerId player);

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
