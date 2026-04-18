/**
 * @file AIEconomicStrategy.cpp
 * @brief AI economic strategy: bonds, sanctions, devaluation, infrastructure, crises.
 */

#include "aoc/game/GameState.hpp"
#include "aoc/game/Player.hpp"
#include "aoc/game/City.hpp"
#include "aoc/simulation/ai/AIEconomicStrategy.hpp"
#include "aoc/simulation/monetary/MonetarySystem.hpp"
#include "aoc/simulation/monetary/CentralBank.hpp"
#include "aoc/simulation/monetary/CurrencyCrisis.hpp"
#include "aoc/simulation/monetary/CurrencyWar.hpp"
#include "aoc/simulation/monetary/Bonds.hpp"
#include "aoc/simulation/monetary/CurrencyTrust.hpp"
#include "aoc/simulation/economy/Sanctions.hpp"
#include "aoc/simulation/economy/Speculation.hpp"
#include "aoc/simulation/economy/EconomicDepth.hpp"
#include "aoc/simulation/economy/IndustrialRevolution.hpp"
#include "aoc/simulation/economy/Market.hpp"
#include "aoc/simulation/economy/TradeRoute.hpp"
#include "aoc/simulation/production/PowerGrid.hpp"
#include "aoc/simulation/city/CityComponent.hpp"
#include "aoc/simulation/city/District.hpp"
#include "aoc/simulation/diplomacy/DiplomacyState.hpp"
#include "aoc/simulation/resource/ResourceComponent.hpp"
#include "aoc/simulation/resource/ResourceTypes.hpp"
#include "aoc/simulation/tech/TechTree.hpp"
#include "aoc/map/HexGrid.hpp"
#include "aoc/core/Log.hpp"

#include <algorithm>
#include <cmath>

namespace aoc::sim {

// ============================================================================
// Bond strategy
// ============================================================================

static void aiBondStrategy(aoc::game::GameState& gameState, PlayerId player,
                           int32_t difficulty) {
    // Bond investment is restricted to hard difficulty (>= 2) to prevent
    // AI flooding the market with bonds on every turn at lower difficulties.
    if (difficulty < 2) { return; }

    // Cooldown: only evaluate bond purchases every 10 turns to avoid
    // issuing ~27 bonds per turn across 12 players (Bug 7).
    if (gameState.currentTurn() % 10 != 0) { return; }

    aoc::game::Player* myPlayer = gameState.player(player);
    if (myPlayer == nullptr) { return; }
    MonetaryStateComponent& myState = myPlayer->monetary();

    // Require a healthy treasury before locking gold into bonds.
    if (myState.treasury <= 500) { return; }

    const PlayerBondComponent& myBonds = myPlayer->bonds();

    for (const std::unique_ptr<aoc::game::Player>& otherPtr : gameState.players()) {
        if (otherPtr == nullptr || otherPtr->id() == player) { continue; }

        // Limit total bond holdings to 3 per player pair to prevent
        // the AI from accumulating unlimited leverage over a single civ.
        int32_t bondsHeld = 0;
        for (const BondIssue& bond : myBonds.heldBonds) {
            if (bond.issuer == otherPtr->id()) {
                ++bondsHeld;
            }
        }
        if (bondsHeld >= 3) { continue; }

        const MonetaryStateComponent& other = otherPtr->monetary();

        // Buy bonds when we have surplus treasury and the target has lower GDP
        // (weaker economy = higher yield, better investment return).
        if (other.gdp < myState.gdp) {
            CurrencyAmount investAmount = std::min(
                myState.treasury / 4,
                static_cast<CurrencyAmount>(100));
            if (investAmount > 20) {
                // Return ignored: failed bond purchase (insufficient funds on
                // the buyer's side) just means this tick's investment skips;
                // the strategy retries next turn.
                (void)issueBond(gameState, other.owner, player, investAmount);
            }
        }
    }
}

// ============================================================================
// Sanctions strategy
// ============================================================================

static void aiSanctionStrategy(aoc::game::GameState& gameState,
                                DiplomacyManager& diplomacy,
                                PlayerId player,
                                int32_t difficulty) {
    if (difficulty < 2) { return; }  // Only hard AI uses sanctions

    for (const std::unique_ptr<aoc::game::Player>& otherPtr : gameState.players()) {
        if (otherPtr == nullptr || otherPtr->id() == player) { continue; }
        PlayerId other = otherPtr->id();

        if (diplomacy.isAtWar(player, other)) {
            // Impose financial sanctions if we haven't already
            // (would need access to the global sanction tracker from EconomySimulation)
            LOG_INFO("AI player %u considering sanctions against player %u",
                     static_cast<unsigned>(player), static_cast<unsigned>(other));
        }
    }
}

// ============================================================================
// Insurance strategy
// ============================================================================

static void aiInsuranceStrategy(aoc::game::GameState& gameState, PlayerId player) {
    aoc::game::Player* myPlayer = gameState.player(player);
    if (myPlayer == nullptr) { return; }

    MonetaryStateComponent& myState = myPlayer->monetary();
    if (myState.treasury < 100) { return; }

    PlayerInsuranceComponent& ins = myPlayer->insurance();

    // Buy all insurance if treasury is healthy
    if (myState.treasury > 300) {
        ins.hasWarInsurance = true;
        ins.hasTradeInsurance = true;
        ins.hasDisasterInsurance = true;
    } else if (myState.treasury > 150) {
        ins.hasTradeInsurance = true;
    }
}

// ============================================================================
// Immigration policy
// ============================================================================

static void aiImmigrationPolicy(aoc::game::GameState& gameState, PlayerId player) {
    aoc::game::Player* myPlayer = gameState.player(player);
    if (myPlayer == nullptr) { return; }

    int32_t cityCount = myPlayer->cityCount();
    PlayerMigrationComponent& mig = myPlayer->migration();

    if (cityCount < 4) {
        mig.policy = ImmigrationPolicy::Open;
    } else if (cityCount < 8) {
        mig.policy = ImmigrationPolicy::Controlled;
    } else {
        mig.policy = ImmigrationPolicy::Closed;
    }
}

// ============================================================================
// Power grid management
// ============================================================================

void aiManagePowerGrid(aoc::game::GameState& gameState,
                       const aoc::map::HexGrid& grid,
                       PlayerId player) {
    aoc::game::Player* myPlayer = gameState.player(player);
    if (myPlayer == nullptr) { return; }

    for (const std::unique_ptr<aoc::game::City>& cityPtr : myPlayer->cities()) {
        if (cityPtr == nullptr) { continue; }

        // computeCityPower requires a legacy EntityId; defer to the legacy path
        // via the CityComponent stored in the legacy world for now.
        // The city location identifies the right CityComponent in the legacy pool.
        (void)cityPtr;
        (void)grid;
        // NOTE: computeCityPower still takes an EntityId from the legacy world.
        // When that function is migrated, pass the City* directly.
    }
}

// ============================================================================
// Infrastructure management
// ============================================================================

void aiManageInfrastructure(aoc::game::GameState& gameState,
                            aoc::map::HexGrid& /*grid*/,
                            PlayerId player) {
    aoc::game::Player* myPlayer = gameState.player(player);
    if (myPlayer == nullptr) { return; }

    PlayerIndustrialComponent& ind = myPlayer->industrial();
    if (ind.hasRailways()) {
        LOG_INFO("AI player %u: railway construction priority active",
                 static_cast<unsigned>(player));
    }
}

// ============================================================================
// Crisis response
// ============================================================================

void aiCrisisResponse(aoc::game::GameState& gameState, PlayerId player) {
    aoc::game::Player* myPlayer = gameState.player(player);
    if (myPlayer == nullptr) { return; }

    MonetaryStateComponent& myState = myPlayer->monetary();
    CurrencyCrisisComponent& crisis = myPlayer->currencyCrisis();

    if (crisis.activeCrisis == CrisisType::None) { return; }

    switch (crisis.activeCrisis) {
        case CrisisType::BankRun:
            // Raise taxes, cut spending
            myState.taxRate = std::min(0.40f, myState.taxRate + 0.05f);
            myState.governmentSpending = myState.governmentSpending * 3 / 4;
            LOG_INFO("AI player %u: crisis response - raising taxes, cutting spending",
                     static_cast<unsigned>(player));
            break;

        case CrisisType::Hyperinflation:
            // Raise interest rates to maximum
            setInterestRate(myState, 0.25f);
            myState.governmentSpending = 0;
            LOG_INFO("AI player %u: crisis response - max interest rates, zero spending",
                     static_cast<unsigned>(player));
            break;

        case CrisisType::SovereignDefault:
            // Cut spending to minimum, raise taxes
            myState.governmentSpending = 0;
            myState.taxRate = std::min(0.50f, myState.taxRate + 0.10f);
            LOG_INFO("AI player %u: crisis response - austerity measures",
                     static_cast<unsigned>(player));
            break;

        default:
            break;
    }
}

// ============================================================================
// Industrial revolution preparation
// ============================================================================

void aiPrepareIndustrialRevolution(aoc::game::GameState& gameState,
                                    const Market& /*market*/,
                                    PlayerId player) {
    aoc::game::Player* myPlayer = gameState.player(player);
    if (myPlayer == nullptr) { return; }

    PlayerIndustrialComponent& ind = myPlayer->industrial();
    PlayerTechComponent& techComp = myPlayer->tech();

    uint8_t nextRev = static_cast<uint8_t>(ind.currentRevolution) + 1;
    if (nextRev > 5) { return; }

    const RevolutionDef& rev = REVOLUTION_DEFS[nextRev - 1];

    // If we're not researching a required tech, suggest it
    for (int32_t r = 0; r < 3; ++r) {
        TechId reqTech = rev.requirements.requiredTechs[r];
        if (reqTech.isValid() && !techComp.hasResearched(reqTech)) {
            LOG_INFO("AI player %u: should prioritize tech %u for %.*s",
                     static_cast<unsigned>(player),
                     static_cast<unsigned>(reqTech.value),
                     static_cast<int>(rev.name.size()), rev.name.data());
        }
    }
}

// ============================================================================
// Gold spending: prevent treasury runaway
// ============================================================================

static void aiSpendExcessGold(aoc::game::GameState& gameState, PlayerId player) {
    aoc::game::Player* myPlayer = gameState.player(player);
    if (myPlayer == nullptr) { return; }

    MonetaryStateComponent& myState = myPlayer->monetary();

    // Cap treasury to prevent integer overflow.
    // Excess gold is "spent" on public works (not tracked individually).
    constexpr CurrencyAmount MAX_TREASURY = 50000;
    if (myState.treasury > MAX_TREASURY) {
        myState.treasury = MAX_TREASURY;
    }
}

// ============================================================================
// Master economic strategy
// ============================================================================

void aiEconomicStrategy(aoc::game::GameState& gameState,
                        aoc::map::HexGrid& grid,
                        const Market& market,
                        DiplomacyManager& diplomacy,
                        PlayerId player,
                        int32_t difficulty) {
    // Run all sub-strategies
    aiBondStrategy(gameState, player, difficulty);
    aiSanctionStrategy(gameState, diplomacy, player, difficulty);
    aiInsuranceStrategy(gameState, player);
    aiImmigrationPolicy(gameState, player);
    aiManagePowerGrid(gameState, grid, player);
    aiManageInfrastructure(gameState, grid, player);
    aiCrisisResponse(gameState, player);
    aiSpendExcessGold(gameState, player);
    aiPrepareIndustrialRevolution(gameState, market, player);
}

} // namespace aoc::sim
