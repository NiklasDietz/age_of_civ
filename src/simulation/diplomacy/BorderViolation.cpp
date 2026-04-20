/**
 * @file BorderViolation.cpp
 * @brief Soft border violation detection and escalation cascade.
 */

#include "aoc/simulation/diplomacy/BorderViolation.hpp"

#include "aoc/simulation/diplomacy/DiplomacyState.hpp"
#include "aoc/simulation/citystate/CityState.hpp"
#include "aoc/simulation/unit/UnitTypes.hpp"
#include "aoc/game/GameState.hpp"
#include "aoc/game/Player.hpp"
#include "aoc/game/Unit.hpp"
#include "aoc/game/City.hpp"
#include "aoc/map/HexGrid.hpp"
#include "aoc/map/HexCoord.hpp"
#include "aoc/ui/GameNotifications.hpp"
#include "aoc/core/Log.hpp"

#include <cstdint>

namespace aoc::sim {

namespace {

/// Military units are those that are not civilian/trader/settler/scout/religious.
bool isMilitaryUnit(UnitClass uc) {
    switch (uc) {
        case UnitClass::Settler:
        case UnitClass::Civilian:
        case UnitClass::Trader:
        case UnitClass::Scout:
        case UnitClass::Religious:
            return false;
        default:
            return true;
    }
}

/// Check if any city owned by `owner` is within `radius` tiles of `pos`.
bool isNearCity(const aoc::game::GameState& gameState,
                const aoc::map::HexGrid& grid,
                PlayerId owner,
                aoc::hex::AxialCoord pos, int32_t radius) {
    const aoc::game::Player* player = gameState.player(owner);
    if (player == nullptr) { return false; }
    for (const std::unique_ptr<aoc::game::City>& city : player->cities()) {
        if (grid.distance(pos, city->location()) <= radius) {
            return true;
        }
    }
    return false;
}

} // anonymous namespace

void updateBorderViolations(aoc::game::GameState& gameState,
                             const aoc::map::HexGrid& grid,
                             DiplomacyManager& diplomacy) {
    uint8_t playerCount = diplomacy.playerCount();

    // Reset unit counts for this turn (turnsWithViolation persists).
    for (uint8_t a = 0; a < playerCount; ++a) {
        for (uint8_t b = 0; b < playerCount; ++b) {
            if (a == b) { continue; }
            diplomacy.relation(a, b).unitsInTerritory = 0;
        }
    }

    // Count military units in foreign territory without Open Borders.
    for (const std::unique_ptr<aoc::game::Player>& player : gameState.players()) {
        PlayerId violator = player->id();
        if (violator == BARBARIAN_PLAYER) { continue; }

        for (const std::unique_ptr<aoc::game::Unit>& unit : player->units()) {
            if (!isMilitaryUnit(unit->typeDef().unitClass)) { continue; }

            aoc::hex::AxialCoord pos = unit->position();
            if (!grid.isValid(pos)) { continue; }

            int32_t tileIdx = grid.toIndex(pos);
            PlayerId tileOwner = grid.owner(tileIdx);
            if (tileOwner == INVALID_PLAYER || tileOwner == violator) { continue; }
            if (tileOwner == BARBARIAN_PLAYER) { continue; }
            // City-states use a separate diplomacy channel (envoys/suzerainty);
            // they are not in the DiplomacyManager major-player matrix.
            if (tileOwner >= aoc::sim::CITY_STATE_PLAYER_BASE) { continue; }

            // Open Borders or war: no violation (war has its own mechanics)
            const PairwiseRelation& rel = diplomacy.relation(violator, tileOwner);
            if (rel.hasOpenBorders || rel.isAtWar) { continue; }

            // Count this unit as a violator
            PairwiseRelation& ownerRel = diplomacy.relation(violator, tileOwner);
            ++ownerRel.unitsInTerritory;
        }
    }

    // Escalation cascade for each player pair with violations.
    // Warnings fire immediately (turn 0) — no constant needed.
    constexpr int32_t REPUTATION_TURN    = 3;   // -3/turn reputation penalty starts
    constexpr int32_t CASUS_BELLI_TURN   = 10;  // Casus belli granted
    constexpr int32_t NEAR_CITY_RADIUS   = 3;   // Units near cities escalate 2x

    for (uint8_t a = 0; a < playerCount; ++a) {
        for (uint8_t b = 0; b < playerCount; ++b) {
            if (a == b) { continue; }
            PairwiseRelation& rel = diplomacy.relation(a, b);
            // a = violator, b = territory owner

            if (rel.unitsInTerritory > 0) {
                ++rel.turnsWithViolation;

                // Near-city check: if any violating unit is near an owner city,
                // escalation speed doubles (counted via effective turns).
                int32_t effectiveTurns = rel.turnsWithViolation;
                const aoc::game::Player* violatorPlayer = gameState.player(a);
                if (violatorPlayer != nullptr) {
                    for (const std::unique_ptr<aoc::game::Unit>& unit : violatorPlayer->units()) {
                        if (!isMilitaryUnit(unit->typeDef().unitClass)) { continue; }
                        aoc::hex::AxialCoord pos = unit->position();
                        if (!grid.isValid(pos)) { continue; }
                        int32_t idx = grid.toIndex(pos);
                        if (grid.owner(idx) != b) { continue; }
                        if (isNearCity(gameState, grid, b, pos, NEAR_CITY_RADIUS)) {
                            effectiveTurns = rel.turnsWithViolation * 2;
                            break;
                        }
                    }
                }

                // Stage 1: Warning (first turn of violation)
                if (!rel.warningIssued) {
                    rel.warningIssued = true;
                    LOG_INFO("Border violation: Player %u has %d military units in Player %u's territory",
                             static_cast<unsigned>(a), rel.unitsInTerritory,
                             static_cast<unsigned>(b));

                    aoc::ui::pushNotification({
                        aoc::ui::NotificationCategory::Diplomacy,
                        "Border Violation",
                        "Player " + std::to_string(a)
                            + " has moved military units into your territory without permission.",
                        b,
                        5
                    });
                }

                // Stage 2: Reputation penalty (-3/turn after threshold)
                if (effectiveTurns >= REPUTATION_TURN) {
                    diplomacy.addReputationModifier(a, b, -3, 10);
                }

                // Stage 3: Casus belli after sustained violation
                if (effectiveTurns >= CASUS_BELLI_TURN && !rel.casusBelliGranted) {
                    rel.casusBelliGranted = true;
                    LOG_INFO("Casus belli granted: Player %u can declare war on Player %u "
                             "without diplomatic penalty (border violation)",
                             static_cast<unsigned>(b), static_cast<unsigned>(a));

                    aoc::ui::pushNotification({
                        aoc::ui::NotificationCategory::Diplomacy,
                        "Casus Belli Earned",
                        "Player " + std::to_string(a)
                            + "'s prolonged border violation grants you justification for war.",
                        b,
                        7
                    });
                }
            } else {
                // No units this turn: reset escalation if previously violated
                if (rel.turnsWithViolation > 0) {
                    // Withdrawal reputation bonus: +5 if they pulled out
                    if (rel.warningIssued) {
                        diplomacy.addReputationModifier(a, b, 5, 20);
                    }
                    rel.turnsWithViolation = 0;
                    rel.warningIssued = false;
                    // Casus belli persists for 20 turns after withdrawal
                    // (not reset here — decayed separately or via peace)
                }
            }
        }
    }
}

} // namespace aoc::sim
