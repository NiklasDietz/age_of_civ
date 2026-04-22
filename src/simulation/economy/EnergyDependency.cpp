/**
 * @file EnergyDependency.cpp
 * @brief Oil/gas scarcity, energy dependency, and peak oil mechanics.
 */

#include "aoc/game/GameState.hpp"
#include "aoc/game/Player.hpp"
#include "aoc/game/City.hpp"
#include "aoc/simulation/economy/EnergyDependency.hpp"
#include "aoc/simulation/city/District.hpp"
#include "aoc/simulation/diplomacy/DiplomacyState.hpp"
#include "aoc/simulation/monetary/MonetarySystem.hpp"
#include "aoc/map/HexGrid.hpp"
#include "aoc/core/Log.hpp"

#include <algorithm>

namespace aoc::sim {

void updateEnergyDependency(PlayerEnergyComponent& energy,
                             int32_t oilConsumed,
                             int32_t renewableBuildingCount) {
    energy.oilConsumedThisTurn = oilConsumed;
    energy.renewableCapacity   = renewableBuildingCount;

    if (oilConsumed > 0) {
        float growthRate = static_cast<float>(oilConsumed) * 0.01f;
        energy.oilDependency += growthRate;
    } else {
        energy.oilDependency -= 0.02f;
    }

    float renewableReduction  = energy.renewableOffset() * 0.01f;
    energy.oilDependency     -= renewableReduction;
    energy.oilDependency      = std::clamp(energy.oilDependency, 0.0f, 1.0f);
}

void updateGlobalOilReserves(const aoc::map::HexGrid& grid,
                              GlobalOilReserves& reserves) {
    int64_t totalOil = 0;

    for (int32_t i = 0; i < grid.tileCount(); ++i) {
        ResourceId res = grid.resource(i);
        if (res.isValid() && res.value == goods::OIL) {
            int16_t tileReserves = grid.reserves(i);
            if (tileReserves > 0) {
                totalOil += static_cast<int64_t>(tileReserves);
            }
        }
    }

    if (reserves.initialTotal == 0 && totalOil > 0) {
        reserves.initialTotal = totalOil;
    }

    reserves.totalRemaining = totalOil;

    if (!reserves.peakOilReached && reserves.initialTotal > 0) {
        float remaining = static_cast<float>(reserves.totalRemaining)
                        / static_cast<float>(reserves.initialTotal);
        if (remaining < 0.50f) {
            reserves.peakOilReached    = true;
            reserves.turnsSincePeakOil = 0;
            LOG_INFO("PEAK OIL reached! Global reserves at %.0f%% of initial",
                     static_cast<double>(remaining) * 100.0);
        }
    }

    if (reserves.peakOilReached) {
        ++reserves.turnsSincePeakOil;
    }
}

void processOilShock(PlayerEnergyComponent& energy) {
    if (energy.inOilShock) {
        --energy.oilShockTurnsRemaining;
        if (energy.oilShockTurnsRemaining <= 0) {
            energy.inOilShock = false;
            LOG_INFO("Player %u recovered from oil shock",
                     static_cast<unsigned>(energy.owner));
        }
    }
}

int32_t countRenewableBuildings(const aoc::game::GameState& gameState, PlayerId player) {
    const aoc::game::Player* playerObj = gameState.player(player);
    if (playerObj == nullptr) {
        return 0;
    }

    int32_t count = 0;
    for (const std::unique_ptr<aoc::game::City>& cityPtr : playerObj->cities()) {
        if (cityPtr == nullptr) { continue; }
        for (const CityDistrictsComponent::PlacedDistrict& district : cityPtr->districts().districts) {
            for (BuildingId bid : district.buildings) {
                // Renewable energy buildings: Hydroelectric(28), Nuclear(29), Solar(30), Wind(31)
                if (bid.value == 28 || bid.value == 29
                    || bid.value == 30 || bid.value == 31) {
                    ++count;
                }
            }
        }
    }

    return count;
}

// ============================================================================
// Bilateral electricity agreements
// ============================================================================

namespace {

uint32_t nextElectricityAgreementId(const aoc::game::GameState& gameState) {
    uint32_t maxId = 0;
    for (const ElectricityAgreementComponent& a : gameState.electricityAgreements()) {
        if (a.id > maxId) { maxId = a.id; }
    }
    return maxId + 1;
}

} // namespace

ErrorCode proposeElectricityImport(aoc::game::GameState& gameState,
                                     PlayerId buyer,
                                     PlayerId seller,
                                     int32_t energyPerTurn,
                                     int32_t goldPerTurn,
                                     int32_t currentTurn,
                                     int32_t durationTurns) {
    if (buyer == seller || buyer == INVALID_PLAYER || seller == INVALID_PLAYER) {
        return ErrorCode::InvalidArgument;
    }
    if (energyPerTurn <= 0 || goldPerTurn < 0) {
        return ErrorCode::InvalidArgument;
    }

    aoc::game::Player* buyerPlayer  = gameState.player(buyer);
    aoc::game::Player* sellerPlayer = gameState.player(seller);
    if (buyerPlayer == nullptr || sellerPlayer == nullptr) {
        return ErrorCode::InvalidArgument;
    }
    if (buyerPlayer->victoryTracker().isEliminated
        || sellerPlayer->victoryTracker().isEliminated) {
        return ErrorCode::InvalidArgument;
    }

    // Reject duplicate buyer/seller direction (seller→buyer already exists).
    for (const ElectricityAgreementComponent& a : gameState.electricityAgreements()) {
        if (!a.isActive) { continue; }
        if (a.seller == seller && a.buyer == buyer) {
            return ErrorCode::AllianceExists;
        }
    }

    ElectricityAgreementComponent agr;
    agr.id             = nextElectricityAgreementId(gameState);
    agr.seller         = seller;
    agr.buyer          = buyer;
    agr.energyPerTurn  = energyPerTurn;
    agr.goldPerTurn    = goldPerTurn;
    agr.formedTurn     = currentTurn;
    agr.endTurn        = (durationTurns > 0) ? (currentTurn + durationTurns) : 0;
    agr.isActive       = true;
    gameState.electricityAgreements().push_back(agr);

    LOG_INFO("Electricity agreement %u: p%u → p%u (%d MW/turn for %dg/turn)",
             static_cast<unsigned>(agr.id),
             static_cast<unsigned>(seller),
             static_cast<unsigned>(buyer),
             energyPerTurn, goldPerTurn);
    return ErrorCode::Ok;
}

void processElectricityAgreements(aoc::game::GameState& gameState,
                                   const DiplomacyManager& diplomacy,
                                   int32_t currentTurn) {
    for (ElectricityAgreementComponent& a : gameState.electricityAgreements()) {
        if (!a.isActive) { continue; }

        // War-break: active war between counterparts kills the contract.
        if (diplomacy.isAtWar(a.buyer, a.seller)) {
            a.isActive = false;
            LOG_INFO("Electricity agreement %u broken by war (p%u ⇄ p%u)",
                     static_cast<unsigned>(a.id),
                     static_cast<unsigned>(a.buyer),
                     static_cast<unsigned>(a.seller));
            continue;
        }

        // Expiry.
        if (a.endTurn > 0 && currentTurn >= a.endTurn) {
            a.isActive = false;
            continue;
        }

        // Gold settlement. Buyer pays seller. If buyer can't afford, contract
        // is suspended for the turn — but stays active so one bad turn
        // doesn't tear up a long-term deal. Seller sees no revenue and no
        // delivery (lastDeliveredEnergy set to 0 below).
        aoc::game::Player* buyerPlayer  = gameState.player(a.buyer);
        aoc::game::Player* sellerPlayer = gameState.player(a.seller);
        if (buyerPlayer == nullptr || sellerPlayer == nullptr) {
            a.isActive = false;
            continue;
        }

        if (buyerPlayer->monetary().treasury
            < static_cast<CurrencyAmount>(a.goldPerTurn)) {
            a.lastDeliveredEnergy = 0;
            continue;
        }
        buyerPlayer->monetary().treasury  -= static_cast<CurrencyAmount>(a.goldPerTurn);
        sellerPlayer->monetary().treasury += static_cast<CurrencyAmount>(a.goldPerTurn);

        // Delivery is recorded here; consumption side is applied inside
        // computeCityPower so the per-city import cap can gate it.
        a.lastDeliveredEnergy = a.energyPerTurn;
    }
}

void breakElectricityAgreementsFor(aoc::game::GameState& gameState, PlayerId player) {
    for (ElectricityAgreementComponent& a : gameState.electricityAgreements()) {
        if (!a.isActive) { continue; }
        if (a.buyer == player || a.seller == player) {
            a.isActive = false;
        }
    }
}

} // namespace aoc::sim
