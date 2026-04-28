/**
 * @file CityState.cpp
 * @brief City-state spawning, envoy processing, and per-turn bonuses.
 */

#include "aoc/simulation/citystate/CityState.hpp"

#include "aoc/game/GameState.hpp"
#include "aoc/game/Player.hpp"
#include "aoc/game/City.hpp"
#include "aoc/game/Unit.hpp"
#include "aoc/simulation/city/District.hpp"
#include "aoc/simulation/city/BorderExpansion.hpp"
#include "aoc/simulation/city/ProductionQueue.hpp"
#include "aoc/simulation/city/ProductionSystem.hpp"
#include "aoc/simulation/unit/UnitTypes.hpp"
#include "aoc/simulation/resource/ResourceComponent.hpp"
#include "aoc/simulation/diplomacy/Grievance.hpp"
#include "aoc/core/Log.hpp"
#include "aoc/map/HexGrid.hpp"
#include "aoc/map/HexCoord.hpp"
#include "aoc/map/Terrain.hpp"

#include <algorithm>
#include <vector>

namespace aoc::sim {

void spawnCityStates(aoc::game::GameState& gameState, aoc::map::HexGrid& grid,
                      int32_t count, aoc::Random& rng) {
    const int32_t toSpawn = std::min(count, static_cast<int32_t>(CITY_STATE_COUNT));

    // Allocate the Player* slots for the city-states BEFORE placing them.
    // Without this, the metadata gets stored but cities/units never spawn
    // because gameState.player(CS_id) returns nullptr below.
    gameState.initializeCityStateSlots(toSpawn);

    // Collect existing city and unit positions to enforce minimum spacing.
    std::vector<hex::AxialCoord> occupiedPositions;
    for (const std::unique_ptr<aoc::game::Player>& playerPtr : gameState.players()) {
        for (const std::unique_ptr<aoc::game::City>& city : playerPtr->cities()) {
            occupiedPositions.push_back(city->location());
        }
        for (const std::unique_ptr<aoc::game::Unit>& unit : playerPtr->units()) {
            occupiedPositions.push_back(unit->position());
        }
    }
    // Include already-spawned city-state locations stored in gameState.
    for (const CityStateComponent& cs : gameState.cityStates()) {
        occupiedPositions.push_back(cs.location);
    }

    const int32_t width = grid.width();
    const int32_t height = grid.height();
    constexpr int32_t MIN_DISTANCE = 8;

    for (int32_t csIdx = 0; csIdx < toSpawn; ++csIdx) {
        const CityStateDef& csDef = CITY_STATE_DEFS[static_cast<std::size_t>(csIdx)];
        const PlayerId csPlayer = static_cast<PlayerId>(CITY_STATE_PLAYER_BASE + csIdx);

        hex::AxialCoord bestPos{0, 0};
        bool found = false;

        for (int32_t attempt = 0; attempt < 200; ++attempt) {
            const int32_t col = rng.nextInt(2, width - 3);
            const int32_t row = rng.nextInt(2, height - 3);
            const int32_t index = row * width + col;

            if (aoc::map::isWater(grid.terrain(index)) ||
                aoc::map::isImpassable(grid.terrain(index))) {
                continue;
            }

            const hex::AxialCoord candidate = hex::offsetToAxial({col, row});

            bool tooClose = false;
            for (const hex::AxialCoord& occupied : occupiedPositions) {
                if (grid.distance(candidate, occupied) < MIN_DISTANCE) {
                    tooClose = true;
                    break;
                }
            }
            if (tooClose) {
                continue;
            }

            bestPos = candidate;
            found = true;
            break;
        }

        if (!found) {
            LOG_INFO("Could not place city-state %.*s",
                     static_cast<int>(csDef.name.size()), csDef.name.data());
            continue;
        }

        // Register city-state metadata in the GameState collection.
        CityStateComponent csComp{};
        csComp.defId    = csDef.id;
        csComp.type     = csDef.type;
        csComp.location = bestPos;
        csComp.envoys.fill(0);
        csComp.suzerain = INVALID_PLAYER;
        gameState.cityStates().push_back(csComp);

        // Create the city-state's city via the player object model.
        // City-state players are allocated in the player list by the caller;
        // if that player slot exists we use it, otherwise we skip city creation.
        aoc::game::Player* csPlayerObj = gameState.player(csPlayer);
        if (csPlayerObj != nullptr) {
            aoc::game::City& csCity = csPlayerObj->addCity(bestPos, std::string(csDef.name));
            csCity.setPopulation(3);

            // Seed city-center district.
            CityDistrictsComponent::PlacedDistrict center;
            center.type     = DistrictType::CityCenter;
            center.location = bestPos;
            csCity.districts().districts.push_back(std::move(center));

            claimInitialTerritory(grid, bestPos, csPlayer);

            // Spawn a warrior adjacent to the city-state.
            const std::array<hex::AxialCoord, 6> neighbors = hex::neighbors(bestPos);
            hex::AxialCoord warriorPos = bestPos;
            for (const hex::AxialCoord& nbr : neighbors) {
                if (grid.isValid(nbr) && grid.movementCost(grid.toIndex(nbr)) > 0) {
                    warriorPos = nbr;
                    break;
                }
            }
            csPlayerObj->addUnit(UnitTypeId{0}, warriorPos);
        }

        occupiedPositions.push_back(bestPos);

        LOG_INFO("Spawned city-state %.*s at (%d,%d) player=%u",
                 static_cast<int>(csDef.name.size()), csDef.name.data(),
                 bestPos.q, bestPos.r, static_cast<unsigned>(csPlayer));
    }
}

void processCityStateBonuses(aoc::game::GameState& gameState, PlayerId player) {
    const std::vector<CityStateComponent>& cityStates = gameState.cityStates();
    if (cityStates.empty()) {
        return;
    }

    aoc::game::Player* gsPlayer = gameState.player(player);
    if (gsPlayer == nullptr) {
        return;
    }
    PlayerEconomyComponent& econ = gsPlayer->economy();

    for (const CityStateComponent& cs : cityStates) {
        if (player >= MAX_PLAYERS) {
            continue;
        }
        const int8_t envoyCount = cs.envoys[player];
        if (envoyCount <= 0) {
            continue;
        }

        // Bonus magnitude: 1 envoy = +1, 3 = +2, 6 = +3.
        int32_t bonusMagnitude = 0;
        if (envoyCount >= 6) {
            bonusMagnitude = 3;
        } else if (envoyCount >= 3) {
            bonusMagnitude = 2;
        } else {
            bonusMagnitude = 1;
        }

        const CurrencyAmount bonus = static_cast<CurrencyAmount>(bonusMagnitude);
        const float magF = static_cast<float>(bonusMagnitude);
        switch (cs.type) {
            case CityStateType::Militaristic:
                // Production bonus routed to every city that has a build queue.
                // Each envoy tier gives +magF production to first queue item.
                for (const std::unique_ptr<aoc::game::City>& city : gsPlayer->cities()) {
                    if (!city->production().queue.empty()) {
                        city->production().queue.front().progress += magF * 2.0f;
                    }
                }
                break;
            case CityStateType::Scientific:
                // Science: accelerate current research.
                gsPlayer->tech().researchProgress += magF * 4.0f;
                break;
            case CityStateType::Cultural:
                // Culture: accelerate current civic research.
                gsPlayer->civics().researchProgress += magF * 4.0f;
                break;
            case CityStateType::Trade:
                econ.treasury += bonus * 3;
                break;
            case CityStateType::Religious:
                gsPlayer->faith().faith += magF * 3.0f;
                break;
            case CityStateType::Industrial:
                // Hammers to every city's current production.
                for (const std::unique_ptr<aoc::game::City>& city : gsPlayer->cities()) {
                    if (!city->production().queue.empty()) {
                        city->production().queue.front().progress += magF * 3.0f;
                    }
                }
                break;
            default:
                break;
        }
    }
}

// ============================================================================
// Per-turn CS diplomacy: meet-check, envoy accrual, suzerain, levy expiry
// ============================================================================
namespace {
constexpr int32_t CS_MEET_RADIUS       = 6;   ///< hexes to auto-meet
constexpr int32_t CS_PASSIVE_TURNS     = 10;  ///< +1 envoy every N turns per met player
constexpr int32_t CS_PASSIVE_MAX       = 6;   ///< cap from passive-only accrual
} // namespace

void processCityStateDiplomacy(aoc::game::GameState& gameState,
                                const aoc::map::HexGrid& grid,
                                int32_t currentTurn) {
    std::vector<CityStateComponent>& cityStates = gameState.cityStates();
    if (cityStates.empty()) { return; }

    for (CityStateComponent& cs : cityStates) {
        // Meet-check: any major-player unit or city within CS_MEET_RADIUS of
        // the CS location marks the player as "met". Met players start
        // accruing passive envoys.
        for (const std::unique_ptr<aoc::game::Player>& playerPtr : gameState.players()) {
            if (playerPtr == nullptr) { continue; }
            const PlayerId pid = playerPtr->id();
            if (cs.hasMet(pid)) { continue; }

            bool discovered = false;
            for (const std::unique_ptr<aoc::game::City>& city : playerPtr->cities()) {
                if (city == nullptr) { continue; }
                if (grid.distance(city->location(), cs.location) <= CS_MEET_RADIUS) {
                    discovered = true;
                    break;
                }
            }
            if (!discovered) {
                for (const std::unique_ptr<aoc::game::Unit>& unit : playerPtr->units()) {
                    if (unit == nullptr) { continue; }
                    if (grid.distance(unit->position(), cs.location) <= CS_MEET_RADIUS) {
                        discovered = true;
                        break;
                    }
                }
            }
            if (discovered) {
                cs.setMet(pid);
                cs.addEnvoys(pid, 1);  // First-contact envoy
                LOG_INFO("Player %u met city-state at (%d,%d)",
                         static_cast<unsigned>(pid),
                         cs.location.q, cs.location.r);
            }
        }

        // Passive accrual every CS_PASSIVE_TURNS for each met player,
        // but capped so passive alone cannot promote to suzerain by itself
        // beyond a reasonable bound.
        if (currentTurn > 0 && (currentTurn % CS_PASSIVE_TURNS) == 0) {
            for (const std::unique_ptr<aoc::game::Player>& playerPtr : gameState.players()) {
                if (playerPtr == nullptr) { continue; }
                const PlayerId pid = playerPtr->id();
                if (!cs.hasMet(pid)) { continue; }
                if (cs.envoys[pid] >= CS_PASSIVE_MAX) { continue; }
                cs.addEnvoys(pid, 1);
            }
        }

        // Recompute suzerain. Log only on change.
        const PlayerId newSuzerain = cs.computeSuzerain();
        if (newSuzerain != cs.suzerain) {
            LOG_INFO("City-state at (%d,%d) suzerain: player %u -> %u",
                     cs.location.q, cs.location.r,
                     static_cast<unsigned>(cs.suzerain),
                     static_cast<unsigned>(newSuzerain));
            cs.suzerain = newSuzerain;
        }

        // Levy expiry
        if (cs.levyTurnsLeft > 0) {
            --cs.levyTurnsLeft;
            if (cs.levyTurnsLeft == 0 && cs.levyPlayer != INVALID_PLAYER) {
                LOG_INFO("Levy expired at CS (%d,%d); units returned",
                         cs.location.q, cs.location.r);
                cs.levyPlayer = INVALID_PLAYER;
            }
        }

        if (cs.turnsSinceBully < 1000) { ++cs.turnsSinceBully; }
    }
}

// ============================================================================
// CS AI: queue defender units when empty; never settlers/wonders. Also
// advances production for CS cities because CS players are not in
// TurnContext::allPlayers, so the standard per-player tick skips them.
// ============================================================================
namespace {
/// Ordered list of defender unit types CS will try to build, best first.
/// Uses UnitTypeId sentinels rather than raw numbers; falls back to Warrior.
constexpr std::array<uint16_t, 5> CS_DEFENDER_CANDIDATES = {
    /*Musketman*/ 3, /*Rifleman*/ 7, /*Pikeman*/ 5,
    /*Swordsman*/ 1, /*Warrior*/  0
};
} // namespace

void processCityStateAI(aoc::game::GameState& gameState,
                         const aoc::map::HexGrid& grid) {
    const std::vector<std::unique_ptr<aoc::game::Player>>& csPlayers =
        gameState.cityStatePlayers();
    for (const std::unique_ptr<aoc::game::Player>& playerPtr : csPlayers) {
        if (playerPtr == nullptr) { continue; }

        // Queue defender if empty.
        for (const std::unique_ptr<aoc::game::City>& cityPtr : playerPtr->cities()) {
            if (cityPtr == nullptr) { continue; }
            if (!cityPtr->production().queue.empty()) { continue; }

            ProductionQueueItem item{};
            for (uint16_t candidate : CS_DEFENDER_CANDIDATES) {
                const UnitTypeDef& def = unitTypeDef(UnitTypeId{candidate});
                if (!def.name.empty()) {
                    item.type      = ProductionItemType::Unit;
                    item.itemId    = candidate;
                    item.name      = std::string(def.name);
                    item.totalCost = static_cast<float>(def.productionCost);
                    item.progress  = 0.0f;
                    break;
                }
            }
            if (!item.name.empty()) {
                cityPtr->production().queue.push_back(std::move(item));
            }
        }

        // Advance production for this CS's cities.
        processProductionQueues(gameState, grid, playerPtr->id());
    }
}

// ============================================================================
// Player actions
// ============================================================================
bool bullyCityState(aoc::game::GameState& gameState, PlayerId player,
                     std::size_t cityStateIndex) {
    std::vector<CityStateComponent>& cityStates = gameState.cityStates();
    if (cityStateIndex >= cityStates.size()) { return false; }
    CityStateComponent& cs = cityStates[cityStateIndex];

    if (!cs.hasMet(player)) { return false; }
    if (cs.suzerain != INVALID_PLAYER && cs.suzerain != player) {
        return false;  // protected by another suzerain
    }
    if (cs.turnsSinceBully < 5) { return false; }  // rate limit

    aoc::game::Player* gsPlayer = gameState.player(player);
    if (gsPlayer == nullptr) { return false; }

    // Bullying: player gains 50 gold, loses 2 envoys at this CS, every
    // OTHER major player that has at least one envoy here gets a grievance
    // against the bully — reputation cost baked in, not caller-dependent.
    gsPlayer->addGold(50);
    cs.addEnvoys(player, -2);
    cs.turnsSinceBully = 0;

    for (const std::unique_ptr<aoc::game::Player>& otherPtr : gameState.players()) {
        if (otherPtr == nullptr) { continue; }
        const PlayerId other = otherPtr->id();
        if (other == player) { continue; }
        if (other >= MAX_PLAYERS) { continue; }
        if (cs.envoys[other] <= 0) { continue; }
        otherPtr->grievances().addGrievance(
            GrievanceType::BulliedCityState, player);
    }

    LOG_INFO("Player %u bullied CS at (%d,%d) for 50 gold",
             static_cast<unsigned>(player),
             cs.location.q, cs.location.r);
    return true;
}

bool levyCityStateMilitary(aoc::game::GameState& gameState, PlayerId player,
                             std::size_t cityStateIndex) {
    std::vector<CityStateComponent>& cityStates = gameState.cityStates();
    if (cityStateIndex >= cityStates.size()) { return false; }
    CityStateComponent& cs = cityStates[cityStateIndex];

    if (cs.suzerain != player) { return false; }
    if (cs.levyPlayer != INVALID_PLAYER) { return false; }

    aoc::game::Player* gsPlayer = gameState.player(player);
    if (gsPlayer == nullptr) { return false; }
    if (gsPlayer->treasury() < 200) { return false; }
    gsPlayer->addGold(-200);

    cs.levyPlayer    = player;
    cs.levyTurnsLeft = 15;  // roughly a half-era

    LOG_INFO("Player %u levied CS at (%d,%d) military for 15 turns",
             static_cast<unsigned>(player),
             cs.location.q, cs.location.r);
    return true;
}

// ============================================================================
// Quest system (merged from former CityStateQuest.cpp)
// ============================================================================
namespace {

/// Snapshot of the assignee's objective counter at generation time.
int32_t computeQuestSnapshot(const aoc::game::Player& p,
                              const GlobalWonderTracker& wt,
                              CityStateQuestType type) {
    switch (type) {
        case CityStateQuestType::BuildWonder: {
            int32_t count = 0;
            for (const PlayerId owner : wt.builtBy) {
                if (owner == p.id()) { ++count; }
            }
            return count;
        }
        case CityStateQuestType::ResearchTech: {
            int32_t count = 0;
            for (const bool done : p.tech().completedTechs) {
                if (done) { ++count; }
            }
            return count;
        }
        case CityStateQuestType::TrainUnit:
            return static_cast<int32_t>(p.units().size());
        default:
            return 0;
    }
}

/// Default quest type per city-state specialization.
CityStateQuestType pickQuestForType(CityStateType type) {
    switch (type) {
        case CityStateType::Militaristic: return CityStateQuestType::TrainUnit;
        case CityStateType::Scientific:   return CityStateQuestType::ResearchTech;
        case CityStateType::Cultural:     return CityStateQuestType::BuildWonder;
        case CityStateType::Trade:        return CityStateQuestType::SendTradeRoute;
        case CityStateType::Religious:    return CityStateQuestType::ConvertToReligion;
        case CityStateType::Industrial:   return CityStateQuestType::BuildWonder;
        default:                          return CityStateQuestType::BuildWonder;
    }
}

/// First met player with at least one city. INVALID_PLAYER if none.
PlayerId pickQuestTarget(const aoc::game::GameState& gs,
                          const CityStateComponent& cs) {
    for (const std::unique_ptr<aoc::game::Player>& pp : gs.players()) {
        if (pp == nullptr) { continue; }
        const PlayerId pid = pp->id();
        if (!cs.hasMet(pid)) { continue; }
        if (pp->cities().empty()) { continue; }
        return pid;
    }
    return INVALID_PLAYER;
}

bool questCompleted(const aoc::game::GameState& gs,
                     const CityStateComponent& cs,
                     std::size_t csIndex,
                     const CityStateQuest& q) {
    const aoc::game::Player* p = gs.player(q.assignedTo);
    if (p == nullptr) { return false; }

    switch (q.type) {
        case CityStateQuestType::BuildWonder: {
            int32_t count = 0;
            for (const PlayerId owner : gs.wonderTracker().builtBy) {
                if (owner == p->id()) { ++count; }
            }
            return count > q.snapshot;
        }
        case CityStateQuestType::ResearchTech: {
            int32_t count = 0;
            for (const bool done : p->tech().completedTechs) {
                if (done) { ++count; }
            }
            return count > q.snapshot;
        }
        case CityStateQuestType::TrainUnit:
            return static_cast<int32_t>(p->units().size()) > q.snapshot;
        case CityStateQuestType::SendTradeRoute: {
            const PlayerId csPlayer =
                static_cast<PlayerId>(CITY_STATE_PLAYER_BASE + csIndex);
            for (const TradeRouteComponent& tr : gs.tradeRoutes()) {
                if (tr.sourcePlayer == p->id() && tr.destPlayer == csPlayer) {
                    return true;
                }
            }
            return false;
        }
        case CityStateQuestType::ConvertToReligion: {
            // Heuristic: player has founded a religion and CS is religious.
            return p->faith().foundedReligion != NO_RELIGION &&
                   cs.type == CityStateType::Religious;
        }
        case CityStateQuestType::DefeatBarbarian:
        default:
            return false;
    }
}

} // namespace

void generateCityStateQuest(aoc::game::GameState& gameState,
                              std::size_t cityStateIndex,
                              PlayerId targetPlayer) {
    if (cityStateIndex >= gameState.cityStates().size()) { return; }
    CityStateComponent& cs = gameState.cityStates()[cityStateIndex];
    const aoc::game::Player* p = gameState.player(targetPlayer);
    if (p == nullptr) { return; }

    CityStateQuest q;
    q.type           = pickQuestForType(cs.type);
    q.assignedTo     = targetPlayer;
    q.isCompleted    = false;
    q.isActive       = true;
    q.turnsRemaining = 30;
    q.envoyReward    = 2;
    q.snapshot       = computeQuestSnapshot(*p, gameState.wonderTracker(), q.type);
    cs.activeQuest   = q;
}

void checkCityStateQuests(aoc::game::GameState& gameState) {
    auto& css = gameState.cityStates();
    for (std::size_t i = 0; i < css.size(); ++i) {
        CityStateComponent& cs = css[i];

        if (!cs.activeQuest.isActive) {
            const PlayerId target = pickQuestTarget(gameState, cs);
            if (target == INVALID_PLAYER) { continue; }
            generateCityStateQuest(gameState, i, target);
            continue;
        }

        if (questCompleted(gameState, cs, i, cs.activeQuest)) {
            // Streak-scaled reward: same player completing consecutive
            // quests gets +1 envoy per streak step (cap +3) and a gold bonus
            // at streak ≥3. Different player resets the streak.
            const PlayerId assignee = cs.activeQuest.assignedTo;
            if (cs.questStreak.player == assignee) {
                ++cs.questStreak.streak;
            } else {
                cs.questStreak.player = assignee;
                cs.questStreak.streak = 0;
            }
            const int32_t bonus = std::min(3, cs.questStreak.streak);
            const int32_t envoys = cs.activeQuest.envoyReward + bonus;
            cs.addEnvoys(assignee, envoys);
            if (cs.questStreak.streak >= 3) {
                aoc::game::Player* p = gameState.player(assignee);
                if (p != nullptr) {
                    p->monetary().treasury += 25;
                }
            }
            LOG_INFO("City-state quest completed: player %d +%d envoys (streak %d)",
                     static_cast<int>(assignee), envoys, cs.questStreak.streak);
            cs.activeQuest = CityStateQuest{};
            continue;
        }

        --cs.activeQuest.turnsRemaining;
        if (cs.activeQuest.turnsRemaining <= 0) {
            // Failed quest breaks streak.
            cs.questStreak = CityStateQuestStreak{};
            cs.activeQuest = CityStateQuest{};
        }
    }
}

} // namespace aoc::sim
