/**
 * @file NavalPassage.cpp
 * @brief Naval passage violation detection and escalation cascade.
 *
 * Mirrors BorderViolation.cpp for water tiles. Naval military units in
 * owned waters without Open Borders trigger diplomatic consequences.
 */

#include "aoc/simulation/diplomacy/NavalPassage.hpp"

#include "aoc/simulation/diplomacy/DiplomacyState.hpp"
#include "aoc/simulation/unit/UnitTypes.hpp"
#include "aoc/game/GameState.hpp"
#include "aoc/game/Player.hpp"
#include "aoc/game/Unit.hpp"
#include "aoc/game/City.hpp"
#include "aoc/map/HexGrid.hpp"
#include "aoc/map/HexCoord.hpp"
#include "aoc/map/Terrain.hpp"
#include "aoc/ui/GameNotifications.hpp"
#include "aoc/core/Log.hpp"

#include <cstdint>

namespace aoc::sim {

namespace {

/// Naval military units are Naval-class units (warships, submarines).
/// Trader-class units (merchant ships) are handled by the toll system, not here.
bool isNavalMilitaryUnit(UnitClass unitClass) {
    return unitClass == UnitClass::Naval;
}

/// Check if any city owned by `owner` has a harbor within `radius` tiles of `pos`.
/// Harbors are city-adjacent water tiles, so we check proximity to coastal cities.
bool isNearHarbor(const aoc::game::GameState& gameState, PlayerId owner,
                  aoc::hex::AxialCoord pos, int32_t radius) {
    const aoc::game::Player* player = gameState.player(owner);
    if (player == nullptr) { return false; }
    for (const std::unique_ptr<aoc::game::City>& city : player->cities()) {
        if (aoc::hex::distance(pos, city->location()) <= radius) {
            return true;
        }
    }
    return false;
}

} // anonymous namespace

void updateNavalPassageViolations(aoc::game::GameState& gameState,
                                   const aoc::map::HexGrid& grid,
                                   DiplomacyManager& diplomacy) {
    uint8_t playerCount = diplomacy.playerCount();

    // Reset naval unit counts for this turn (turnsWithNavalViolation persists).
    for (uint8_t a = 0; a < playerCount; ++a) {
        for (uint8_t b = 0; b < playerCount; ++b) {
            if (a == b) { continue; }
            diplomacy.relation(a, b).navalUnitsInWaters = 0;
        }
    }

    // Count naval military units in foreign-owned waters without Open Borders.
    for (const std::unique_ptr<aoc::game::Player>& player : gameState.players()) {
        PlayerId violator = player->id();
        if (violator == BARBARIAN_PLAYER) { continue; }

        for (const std::unique_ptr<aoc::game::Unit>& unit : player->units()) {
            if (!isNavalMilitaryUnit(unit->typeDef().unitClass)) { continue; }

            aoc::hex::AxialCoord pos = unit->position();
            if (!grid.isValid(pos)) { continue; }

            int32_t tileIdx = grid.toIndex(pos);

            // Applies to water tiles and canal tiles (both are navigable)
            if (!aoc::map::isWater(grid.terrain(tileIdx)) && !grid.hasCanal(tileIdx)) { continue; }

            PlayerId tileOwner = grid.owner(tileIdx);
            if (tileOwner == INVALID_PLAYER || tileOwner == violator) { continue; }
            if (tileOwner == BARBARIAN_PLAYER) { continue; }

            // Open Borders or war: no violation
            const PairwiseRelation& rel = diplomacy.relation(violator, tileOwner);
            if (rel.hasOpenBorders || rel.isAtWar) { continue; }

            PairwiseRelation& ownerRel = diplomacy.relation(violator, tileOwner);
            ++ownerRel.navalUnitsInWaters;
        }
    }

    // Escalation cascade (same constants as land border violations).
    constexpr int32_t WARNING_TURN       = 0;
    constexpr int32_t REPUTATION_TURN    = 3;
    constexpr int32_t CASUS_BELLI_TURN   = 10;
    constexpr int32_t NEAR_HARBOR_RADIUS = 2;  // Tighter than land (3) since harbors are specific

    (void)WARNING_TURN;  // Used implicitly (first turn = warning)

    for (uint8_t a = 0; a < playerCount; ++a) {
        for (uint8_t b = 0; b < playerCount; ++b) {
            if (a == b) { continue; }
            PairwiseRelation& rel = diplomacy.relation(a, b);
            // a = violator, b = waters owner

            if (rel.navalUnitsInWaters > 0) {
                ++rel.turnsWithNavalViolation;

                // Near-harbor check: escalation speed doubles
                int32_t effectiveTurns = rel.turnsWithNavalViolation;
                const aoc::game::Player* violatorPlayer = gameState.player(a);
                if (violatorPlayer != nullptr) {
                    for (const std::unique_ptr<aoc::game::Unit>& unit : violatorPlayer->units()) {
                        if (!isNavalMilitaryUnit(unit->typeDef().unitClass)) { continue; }
                        aoc::hex::AxialCoord pos = unit->position();
                        if (!grid.isValid(pos)) { continue; }
                        int32_t idx = grid.toIndex(pos);
                        if (!aoc::map::isWater(grid.terrain(idx)) && !grid.hasCanal(idx)) { continue; }
                        if (grid.owner(idx) != b) { continue; }
                        if (isNearHarbor(gameState, b, pos, NEAR_HARBOR_RADIUS)) {
                            effectiveTurns = rel.turnsWithNavalViolation * 2;
                            break;
                        }
                    }
                }

                // Stage 1: Warning
                if (!rel.navalWarningIssued) {
                    rel.navalWarningIssued = true;
                    LOG_INFO("Naval passage violation: Player %u has %d warships in Player %u's waters",
                             static_cast<unsigned>(a), rel.navalUnitsInWaters,
                             static_cast<unsigned>(b));

                    aoc::ui::pushNotification({
                        aoc::ui::NotificationCategory::Diplomacy,
                        "Naval Passage Violation",
                        "Player " + std::to_string(a)
                            + " has moved warships into your territorial waters without permission.",
                        b,
                        5
                    });
                }

                // Stage 2: Reputation penalty (-3/turn)
                if (effectiveTurns >= REPUTATION_TURN) {
                    diplomacy.addReputationModifier(a, b, -3, 10);
                }

                // Stage 3: Casus belli (reuses land casus belli flag since it's per-pair)
                if (effectiveTurns >= CASUS_BELLI_TURN && !rel.casusBelliGranted) {
                    rel.casusBelliGranted = true;
                    LOG_INFO("Casus belli granted: Player %u can declare war on Player %u "
                             "without diplomatic penalty (naval passage violation)",
                             static_cast<unsigned>(b), static_cast<unsigned>(a));

                    aoc::ui::pushNotification({
                        aoc::ui::NotificationCategory::Diplomacy,
                        "Casus Belli Earned",
                        "Player " + std::to_string(a)
                            + "'s prolonged naval incursion grants you justification for war.",
                        b,
                        7
                    });
                }
            } else {
                // No naval units this turn: reset escalation
                if (rel.turnsWithNavalViolation > 0) {
                    if (rel.navalWarningIssued) {
                        diplomacy.addReputationModifier(a, b, 5, 20);
                    }
                    rel.turnsWithNavalViolation = 0;
                    rel.navalWarningIssued = false;
                }
            }
        }
    }
}

} // namespace aoc::sim
