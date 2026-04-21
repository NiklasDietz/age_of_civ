/**
 * @file Tourism.cpp
 * @brief Tourism system implementation for Cultural Victory.
 *
 * Tourism output per turn sums:
 *   - Wonders in cities: 3 each
 *   - Theatre building great-work slots: 2 per slot (treat slots as filled)
 *   - Holy Site presence: 2 each (proxy for "holy city")
 *   - National Parks: not yet modelled (count stays 0)
 *
 * Multipliers vs each target civ:
 *   - Active trade agreement with target: +25% tourism against that civ
 *
 * For simplicity we compute a single global tourismPerTurn then let
 * cumulativeTourism accumulate. Foreign/domestic tourist counts derive
 * from cumulative tourism and the VictoryTracker's totalCultureAccumulated.
 */

#include "aoc/simulation/culture/Tourism.hpp"
#include "aoc/game/GameState.hpp"
#include "aoc/game/Player.hpp"
#include "aoc/game/City.hpp"
#include "aoc/simulation/city/District.hpp"
#include "aoc/simulation/wonder/Wonder.hpp"
#include "aoc/core/Types.hpp"

#include <algorithm>

namespace aoc::sim {

namespace {

constexpr float FOREIGN_TOURIST_COST  = 150.0f;
constexpr float DOMESTIC_TOURIST_COST = 100.0f;

/// Count great-work slots from all Theatre/Culture buildings placed in a city.
/// Assumes slots are filled for tourism purposes (simplification).
[[nodiscard]] int32_t cityGreatWorkSlots(const aoc::game::City& city) {
    int32_t total = 0;
    for (const CityDistrictsComponent::PlacedDistrict& district :
             city.districts().districts) {
        for (const BuildingId b : district.buildings) {
            if (b.value < BUILDING_DEFS.size()) {
                total += static_cast<int32_t>(buildingDef(b).greatWorksSlots);
            }
        }
    }
    return total;
}

/// True if city has any HolySite building (Shrine/Temple/Cathedral).
[[nodiscard]] bool cityIsHolySite(const aoc::game::City& city) {
    return city.hasBuilding(BuildingId{36})   // Shrine
        || city.hasBuilding(BuildingId{37})   // Temple
        || city.hasBuilding(BuildingId{38});  // Cathedral
}

} // namespace

void computeTourism(aoc::game::GameState& gameState, PlayerId playerId,
                    const aoc::map::HexGrid& /*grid*/) {
    aoc::game::Player* player = gameState.player(playerId);
    if (player == nullptr) { return; }

    PlayerTourismComponent& t = player->tourism();
    t.owner = playerId;

    int32_t wonderTotal   = 0;
    int32_t greatWorkSlot = 0;
    int32_t holySites     = 0;

    for (const std::unique_ptr<aoc::game::City>& city : player->cities()) {
        wonderTotal   += static_cast<int32_t>(city->wonders().wonders.size());
        greatWorkSlot += cityGreatWorkSlots(*city);
        if (cityIsHolySite(*city)) { ++holySites; }
    }

    t.wonderCount    = wonderTotal;
    t.greatWorkCount = greatWorkSlot;

    // Base tourism per turn.
    float base = 3.0f * static_cast<float>(wonderTotal)
               + 2.0f * static_cast<float>(greatWorkSlot)
               + 2.0f * static_cast<float>(holySites);

    // Trade-agreement multiplier: any active agreement treats us as having
    // trade-route tourism bonus against at least one civ. Average +12% per
    // active agreement (capped at +50%).
    int32_t activeAgreements = 0;
    for (const TradeAgreementDef& a : player->tradeAgreements().agreements) {
        if (a.isActive) { ++activeAgreements; }
    }
    const float mult = 1.0f + std::min(0.5f, 0.12f * static_cast<float>(activeAgreements));
    base *= mult;

    t.tourismPerTurn     = base;
    t.cumulativeTourism += base;

    // Derived counts.
    t.foreignTourists  = static_cast<int32_t>(
        t.cumulativeTourism / FOREIGN_TOURIST_COST);
    t.domesticTourists = static_cast<int32_t>(
        player->victoryTracker().totalCultureAccumulated / DOMESTIC_TOURIST_COST);
}

PlayerId checkCulturalVictory(const aoc::game::GameState& gameState) {
    // Cultural victory: a player's foreign-tourist count (as seen by all
    // rivals) exceeds every other civ's domestic-tourist count, AND the
    // foreign-tourist count clears a minimum absolute floor.
    //
    // The floor prevents an early-game accidental win where every civ has
    // near-zero tourism: the first civ to reach 1 foreign tourist would
    // otherwise "beat" everyone at 0 domestic tourists. Requiring >= 500
    // ensures the winner has actually generated a sustained cultural draw.
    constexpr int32_t CULTURAL_VICTORY_FOREIGN_FLOOR = 500;

    PlayerId winner = INVALID_PLAYER;
    int32_t  maxForeign = 0;

    for (const std::unique_ptr<aoc::game::Player>& p : gameState.players()) {
        if (p == nullptr) { continue; }
        const PlayerTourismComponent& tp = p->tourism();
        if (tp.foreignTourists < CULTURAL_VICTORY_FOREIGN_FLOOR) { continue; }

        // Check against every other civ's domestic tourists.
        bool beatsAll = true;
        for (const std::unique_ptr<aoc::game::Player>& q : gameState.players()) {
            if (q == nullptr || q->id() == p->id()) { continue; }
            if (tp.foreignTourists <= q->tourism().domesticTourists) {
                beatsAll = false;
                break;
            }
        }
        if (beatsAll && tp.foreignTourists > maxForeign) {
            maxForeign = tp.foreignTourists;
            winner = p->id();
        }
    }
    return winner;
}

} // namespace aoc::sim
