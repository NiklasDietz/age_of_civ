#pragma once

/**
 * @file EconomicDepth.hpp
 * @brief Advanced economic mechanics: stock market, labor, insurance, espionage, migration.
 *
 * === Stock Market / Futures Trading ===
 * Players can buy/sell goods futures -- contracts to buy/sell a good at a
 * fixed price in the future. Profits if the price moves in your favor.
 *   - Buy future: pay now, receive goods in 5 turns at today's price
 *   - Sell future: promise to deliver goods in 5 turns, receive gold now
 *   - Speculation: if you expect iron prices to rise, buy iron futures
 *   - Hedging: if you depend on oil, buy oil futures to lock in price
 *
 * === Labor Unions / Strikes ===
 * Cities with high industrial output but low amenities risk strikes:
 *   - Trigger: amenities < 0 AND industrial buildings > 3 in a city
 *   - Effect: all industrial buildings in the city stop working for 3 turns
 *   - Prevention: keep amenities positive, or use government policies
 *   - Communism government: strikes are suppressed (-loyalty instead)
 *   - Democracy government: strikes resolved faster (2 turns instead of 3)
 *
 * === Insurance System ===
 * Players can pay insurance premiums to protect against losses:
 *   - War Damage Insurance: 5 gold/turn, covers 50% of building replacement cost
 *   - Trade Insurance: 3 gold/turn, covers plundered trade route cargo
 *   - Disaster Insurance: 4 gold/turn, covers 50% of disaster improvement damage
 *   - Premiums scale with number of insured cities/routes.
 *
 * === Economic Espionage ===
 * Spy missions targeting economic systems:
 *   - Steal Recipe: Copy a production recipe the target has but you don't
 *   - Market Manipulation: Artificially inflate/deflate a good's price for 5 turns
 *   - Insider Trading: See all market prices 3 turns into the future
 *   - Counterfeit: Create fake coins, damaging target's monetary trust
 *
 * === Migration ===
 * Citizens migrate between civilizations based on quality of life:
 *   - Each turn, compare QoL between neighboring cities (across borders)
 *   - If QoL difference > 3: 1 citizen migrates per 10 turns
 *   - Brain drain: migrating scientists reduce source's science output
 *   - Immigration policy: Open (fast growth, loyalty risk), Controlled, Closed
 *   - Refugees: war creates refugees that migrate to peaceful neighbors
 */

#include "aoc/core/Types.hpp"
#include "aoc/core/ErrorCodes.hpp"

#include <cstdint>
#include <vector>

namespace aoc::game { class GameState; }
namespace aoc::map { class HexGrid; }

namespace aoc::sim {

class Market;

// ============================================================================
// Futures Trading
// ============================================================================

struct FuturesContract {
    PlayerId buyer;
    PlayerId seller;     ///< INVALID_PLAYER if market-sold
    uint16_t goodId;
    int32_t  amount;
    int32_t  contractPrice;  ///< Price locked in at creation
    int32_t  turnsToSettlement;
};

struct PlayerFuturesComponent {
    PlayerId owner = INVALID_PLAYER;
    std::vector<FuturesContract> contracts;
};

[[nodiscard]] ErrorCode buyFuture(aoc::game::GameState& gameState, const Market& market,
                                   PlayerId buyer, uint16_t goodId, int32_t amount);
[[nodiscard]] ErrorCode sellFuture(aoc::game::GameState& gameState, const Market& market,
                                    PlayerId seller, uint16_t goodId, int32_t amount);
void settleFutures(aoc::game::GameState& gameState, Market& market);

// ============================================================================
// Labor Strikes
// ============================================================================

struct CityStrikeComponent {
    int32_t strikeTurnsRemaining = 0;
    bool    isOnStrike = false;
};

/// Check and trigger strikes in cities with low amenities + high industry.
void checkLaborStrikes(aoc::game::GameState& gameState);

/// Tick down active strikes.
void processStrikes(aoc::game::GameState& gameState);

// ============================================================================
// Insurance
// ============================================================================

enum class InsuranceType : uint8_t {
    WarDamage,     ///< 5 gold/turn, covers 50% building replacement
    TradeRoute,    ///< 3 gold/turn, covers plundered cargo
    Disaster,      ///< 4 gold/turn, covers improvement damage

    Count
};

struct PlayerInsuranceComponent {
    PlayerId owner = INVALID_PLAYER;
    bool     hasWarInsurance = false;
    bool     hasTradeInsurance = false;
    bool     hasDisasterInsurance = false;

    [[nodiscard]] int32_t totalPremium() const {
        int32_t cost = 0;
        if (this->hasWarInsurance)      { cost += 5; }
        if (this->hasTradeInsurance)    { cost += 3; }
        if (this->hasDisasterInsurance) { cost += 4; }
        return cost;
    }
};

/// Process insurance premium payments per turn.
void processInsurancePremiums(aoc::game::GameState& gameState);

// ============================================================================
// Economic Espionage
// ============================================================================

// ============================================================================
// Migration
// ============================================================================

enum class ImmigrationPolicy : uint8_t {
    Open,        ///< +2 growth from migration, -5 loyalty
    Controlled,  ///< +1 growth, 0 loyalty effect
    Closed,      ///< No migration growth, +5 loyalty
};

struct PlayerMigrationComponent {
    PlayerId          owner = INVALID_PLAYER;
    ImmigrationPolicy policy = ImmigrationPolicy::Controlled;
    int32_t           totalImmigrants = 0;
    int32_t           totalEmigrants = 0;
};

/// Process migration between civilizations based on QoL differences.
void processMigration(aoc::game::GameState& gameState, const aoc::map::HexGrid& grid);

} // namespace aoc::sim
