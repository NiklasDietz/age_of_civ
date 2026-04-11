#pragma once

/// @file AdvancedEconomics.hpp
/// @brief Advanced economic systems: tariffs, transport costs, trade blocs,
///        tech spillover, labor market, infrastructure, banking, and currency exchange.

#include "aoc/core/Types.hpp"
#include "aoc/map/HexCoord.hpp"

#include <array>
#include <cstdint>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace aoc::game { class GameState; }
namespace aoc::map { class HexGrid; }

namespace aoc::sim {

class Market;

// ============================================================================
// Tariffs
// ============================================================================

/// Per-player tariff settings (ECS component).
struct PlayerTariffComponent {
    PlayerId owner = INVALID_PLAYER;
    float importTariffRate = 0.0f;  ///< 0.0 to 0.5 (0% to 50%)
    float exportTariffRate = 0.0f;
    /// Per-player tariff overrides (e.g., higher tariff on specific rival).
    std::unordered_map<PlayerId, float> perPlayerTariffs;

    /// Get the effective import tariff for goods coming from a specific player.
    [[nodiscard]] float effectiveImportTariff(PlayerId from) const;
};

/// Apply tariffs to a trade deal value. Returns the post-tariff value.
[[nodiscard]] float applyTariffs(const PlayerTariffComponent& importer,
                                 PlayerId exporter, float baseValue);

// ============================================================================
// Transport Costs
// ============================================================================

/// Compute transport cost between two hex coordinates based on distance
/// and infrastructure. Cities with roads/harbors between them have lower costs.
[[nodiscard]] float computeTransportCost(const aoc::map::HexGrid& grid,
                                         hex::AxialCoord from, hex::AxialCoord to,
                                         float baseGoodValue);

// ============================================================================
// Trade Blocs
// ============================================================================

/// A trade bloc is a group of players with shared tariff policies.
struct TradeBloc {
    uint8_t id = 0;
    std::string_view name;
    std::vector<PlayerId> members;
    float internalTariff = 0.0f;   ///< Tariff between members (usually 0 = free trade)
    float externalTariff = 0.10f;  ///< Common external tariff
};

/// Global tracker for trade blocs (one per game).
struct GlobalTradeBlocTracker {
    std::vector<TradeBloc> blocs;

    /// Check if two players are in the same trade bloc.
    [[nodiscard]] bool areInSameBloc(PlayerId a, PlayerId b) const;

    /// Get the effective tariff between two players considering blocs.
    [[nodiscard]] float effectiveTariff(PlayerId importer, PlayerId exporter) const;
};

// ============================================================================
// Technology Spillover
// ============================================================================

/// When trading with a more advanced player, gain a small science bonus.
/// Bonus = max(0, partnerTechs - myTechs) * spilloverRate
[[nodiscard]] float computeTechSpillover(const aoc::game::GameState& gameState,
                                         PlayerId player, PlayerId tradePartner);

/// Process tech spillover for all active trade routes.
void processTechSpillover(aoc::game::GameState& gameState);

// ============================================================================
// Labor Market
// ============================================================================

/// Per-city labor allocation (simplified model).
struct CityLaborComponent {
    PlayerId owner = INVALID_PLAYER;
    int32_t farmers    = 0;  ///< Citizens working food tiles
    int32_t miners     = 0;  ///< Citizens working production tiles
    int32_t merchants  = 0;  ///< Citizens generating gold
    int32_t scientists = 0;  ///< Citizens generating science

    /// Auto-assign citizens based on city needs.
    void autoAssign(int32_t totalPopulation);

    /// Yield multipliers based on specialization (more of one type = diminishing returns).
    [[nodiscard]] float foodMultiplier() const;
    [[nodiscard]] float productionMultiplier() const;
    [[nodiscard]] float goldMultiplier() const;
    [[nodiscard]] float scienceMultiplier() const;
};

// ============================================================================
// Diminishing Returns
// ============================================================================

/// Apply diminishing returns to a production amount based on how much
/// of that good the player already has in stockpile.
/// More surplus = lower marginal value.
[[nodiscard]] float diminishingReturns(int32_t currentStockpile, int32_t newProduction);

// ============================================================================
// Infrastructure
// ============================================================================

/// Compute infrastructure bonus for a city based on nearby roads, harbors, markets.
/// Returns a multiplier (1.0 = no bonus, up to 1.5 with full infrastructure).
[[nodiscard]] float computeInfrastructureBonus(const aoc::game::GameState& gameState,
                                               const aoc::map::HexGrid& grid,
                                               EntityId cityEntity);

// ============================================================================
// Credit / Banking
// ============================================================================

/// Per-player banking state.
struct PlayerBankingComponent {
    PlayerId owner = INVALID_PLAYER;
    CurrencyAmount totalLoans = 0;           ///< Outstanding loan principal
    CurrencyAmount loanInterestRate = 5;     ///< Annual interest rate (5 = 5%)
    int32_t turnsUntilPayment = 0;           ///< Turns until next interest payment
    bool hasBankingCrisis = false;           ///< True if over-leveraged
    int32_t crisisTurnsRemaining = 0;

    /// Take a loan. Adds to treasury immediately, adds to totalLoans.
    void takeLoan(CurrencyAmount amount);

    /// Process interest payments and crisis checks.
    void processPayments(CurrencyAmount& treasury, CurrencyAmount gdp);
};

// ============================================================================
// Currency Exchange
// ============================================================================

/// Compute exchange rate between two players based on their monetary systems.
/// Players on gold standard trade at 1:1. Fiat vs gold has variable rate.
[[nodiscard]] float computeExchangeRate(const aoc::game::GameState& gameState,
                                        PlayerId playerA, PlayerId playerB);

// ============================================================================
// Debt Crisis
// ============================================================================

/// Check if a player is in a debt crisis (debt > 2x GDP).
/// Returns true and applies penalties: -20% production, -10% science, -3 amenities.
[[nodiscard]] bool checkDebtCrisis(aoc::game::GameState& gameState, PlayerId player);

// ============================================================================
// Master function: process all advanced economics per turn.
// ============================================================================

void processAdvancedEconomics(aoc::game::GameState& gameState, const aoc::map::HexGrid& grid,
                              PlayerId player, Market& market);

} // namespace aoc::sim
