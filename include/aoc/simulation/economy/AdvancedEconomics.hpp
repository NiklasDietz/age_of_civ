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

/// Per-player tariff and toll settings (ECS component).
struct PlayerTariffComponent {
    PlayerId owner = INVALID_PLAYER;
    float importTariffRate = 0.0f;  ///< 0.0 to 0.5 (0% to 50%)
    float exportTariffRate = 0.0f;
    /// Per-player tariff overrides (e.g., higher tariff on specific rival).
    std::unordered_map<PlayerId, float> perPlayerTariffs;

    // -- Territory toll rates (charged on trade routes passing through) --
    float defaultTollRate = 0.10f;  ///< Global toll rate for all players (0.0 to 0.5)
    /// Per-player toll overrides (e.g., allies=0%, rivals=0.30+).
    std::unordered_map<PlayerId, float> perPlayerTollRates;

    // -- Canal toll rates (premium charged for canal transit, on top of territory toll) --
    float defaultCanalTollRate = 0.25f;  ///< Default canal transit fee per tile (0.0 to 0.5)
    /// Per-player canal toll overrides (e.g., allies=0.10, rivals=0.50).
    std::unordered_map<PlayerId, float> perPlayerCanalTollRates;

    /// Auto-tariff manager: set by player to delegate tariff/toll tuning to
    /// the automation system. When enabled, the per-turn processor rewrites
    /// importTariffRate and perPlayerTariffs/perPlayerTollRates based on
    /// treasury health and diplomatic stance.
    bool autoTariffs = false;

    /// Get the effective import tariff for goods coming from a specific player.
    [[nodiscard]] float effectiveImportTariff(PlayerId from) const;

    /// Get the effective toll rate charged to a specific trader passing through territory.
    /// Checks per-player override first, then default rate. Clamped to [0.0, 0.5].
    /// Free Trade Zone / Customs Union membership results in 0% toll (checked externally).
    [[nodiscard]] float effectiveTollRate(PlayerId trader) const;

    /// Get the effective canal toll rate for a specific trader.
    /// Canal tolls are a premium on top of territory tolls, reflecting the
    /// infrastructure investment. Per-player overrides allow diplomatic pricing.
    [[nodiscard]] float effectiveCanalTollRate(PlayerId trader) const;
};


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
                                               PlayerId cityOwner,
                                               aoc::hex::AxialCoord cityLocation);

// ============================================================================
// Currency Exchange
// ============================================================================

/// Compute exchange rate between two players based on their monetary systems.
/// Players on gold standard trade at 1:1. Fiat vs gold has variable rate.
[[nodiscard]] float computeExchangeRate(const aoc::game::GameState& gameState,
                                        PlayerId playerA, PlayerId playerB);


} // namespace aoc::sim
