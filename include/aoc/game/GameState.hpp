#pragma once

/**
 * @file GameState.hpp
 * @brief Top-level game state container using object model instead of raw ECS.
 *
 * This is the new architecture replacing scattered ECS components with a
 * clean ownership hierarchy:
 *
 *   GameState
 *   ├── Players[MAX_PLAYERS]  (each player owns their data)
 *   │   ├── Tech, Civics, Economy, Government, Religion
 *   │   ├── Cities[] (production, citizens, buildings, loyalty)
 *   │   └── Units[] (movement, combat, automation)
 *   ├── Map (HexGrid - terrain, resources, improvements)
 *   ├── Market (global trade prices)
 *   ├── Climate (global temperature, CO2)
 *   └── Diplomacy (relations matrix)
 *
 * The ECS World is kept for backward compatibility during migration.
 * New code should use GameState directly. Old code will be migrated gradually.
 */

#include "aoc/core/Types.hpp"
#include "aoc/simulation/climate/Climate.hpp"
#include "aoc/simulation/economy/EnergyDependency.hpp"
#include "aoc/simulation/economy/MonopolyPricing.hpp"
#include "aoc/simulation/economy/Sanctions.hpp"
#include "aoc/simulation/economy/Speculation.hpp"
#include "aoc/simulation/economy/TradeRoute.hpp"
#include "aoc/simulation/wonder/Wonder.hpp"
#include "aoc/simulation/diplomacy/WorldCongress.hpp"
#include "aoc/simulation/diplomacy/Confederation.hpp"
#include "aoc/simulation/barbarian/BarbarianClans.hpp"
#include "aoc/simulation/citystate/CityState.hpp"
#include "aoc/simulation/religion/Religion.hpp"
#include "aoc/simulation/event/VisibilityEvents.hpp"

#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>

namespace aoc::map { class HexGrid; }

namespace aoc::game {

// Forward declarations for the object model
class Player;
class City;
class Unit;

/**
 * @brief Top-level game state container.
 *
 * Owns all game data. Provides typed access to players, cities, units.
 * Replaces the pattern of querying the ECS World for scattered components.
 */
class GameState {
public:
    GameState();
    ~GameState();

    GameState(const GameState&) = delete;
    GameState& operator=(const GameState&) = delete;
    GameState(GameState&&) noexcept;
    GameState& operator=(GameState&&) noexcept;

    /// Initialize game state for a new game.
    void initialize(int32_t playerCount);

    /// Allocate Player objects for city-states in the 200..200+count range.
    /// Safe to call after initialize(); player IDs above the major-player
    /// count are routed to the city-state slots by player().
    void initializeCityStateSlots(int32_t count);

    /// Get a player by ID. Returns nullptr if invalid.
    /// IDs 0..majorCount-1 resolve into the major-players vector; IDs
    /// CITY_STATE_PLAYER_BASE..+ resolve into the city-state slots.
    [[nodiscard]] Player* player(PlayerId id);
    [[nodiscard]] const Player* player(PlayerId id) const;

    /// Get the player currently under human control. Defaults to
    /// PlayerId{0}; WP-H takeover changes this via `setHumanPlayerId`.
    [[nodiscard]] Player* humanPlayer();
    [[nodiscard]] const Player* humanPlayer() const;

    /// PlayerId of the currently human-controlled slot.
    [[nodiscard]] PlayerId humanPlayerId() const { return this->m_humanPlayerId; }

    /// WP-H: switch which player the UI / fog-of-war / rendering follows.
    /// Flips `isHuman` flags: target becomes human, previous holder
    /// becomes AI-controlled.
    void setHumanPlayerId(PlayerId id);

    /// All active *major* players. City-state players are NOT included here;
    /// use cityStatePlayers() to iterate those.
    [[nodiscard]] const std::vector<std::unique_ptr<Player>>& players() const { return this->m_players; }

    /// City-state Player slots. Index = CityStateComponent index.
    [[nodiscard]] const std::vector<std::unique_ptr<Player>>& cityStatePlayers() const { return this->m_cityStatePlayers; }
    [[nodiscard]] std::vector<std::unique_ptr<Player>>& cityStatePlayers() { return this->m_cityStatePlayers; }

    /// Number of active players.
    [[nodiscard]] int32_t playerCount() const { return static_cast<int32_t>(this->m_players.size()); }

    /// Current turn number.
    [[nodiscard]] int32_t currentTurn() const { return this->m_currentTurn; }
    void advanceTurn() { ++this->m_currentTurn; }
    void setCurrentTurn(int32_t turn) { this->m_currentTurn = turn; }

    // WP-L1: per-tile event log. Lightweight append-only stream for
    // post-sim analysis. Recorded at hot sites:
    //   TerritoryClaimed / CityFoundedTile / CityCapturedTile /
    //   ImprovementBuilt / ResourceDiscovered / TerrainConverted.
    enum class TileEventType : uint8_t {
        TerritoryClaimed,
        CityFoundedTile,
        CityCapturedTile,
        ImprovementBuilt,
        ResourceDiscovered,
        TerrainConverted,
    };
    struct TileEvent {
        int32_t       turn;
        int32_t       tileIndex;
        TileEventType type;
        PlayerId      actor;
        int32_t       payload;  ///< type-specific (improvement id, good id, terrain id...)
    };
    void recordTileEvent(int32_t tileIndex, TileEventType type,
                         PlayerId actor, int32_t payload = 0) {
        this->m_tileEvents.push_back(TileEvent{
            this->m_currentTurn, tileIndex, type, actor, payload});
    }
    [[nodiscard]] const std::vector<TileEvent>& tileEvents() const { return this->m_tileEvents; }

    // ========================================================================
    // Global state (singletons)
    // ========================================================================

    [[nodiscard]] aoc::sim::GlobalClimateComponent& climate() { return this->m_climate; }
    [[nodiscard]] const aoc::sim::GlobalClimateComponent& climate() const { return this->m_climate; }

    [[nodiscard]] aoc::sim::GlobalOilReserves& oilReserves() { return this->m_oilReserves; }
    [[nodiscard]] const aoc::sim::GlobalOilReserves& oilReserves() const { return this->m_oilReserves; }

    [[nodiscard]] aoc::sim::GlobalMonopolyComponent& monopoly() { return this->m_monopoly; }
    [[nodiscard]] const aoc::sim::GlobalMonopolyComponent& monopoly() const { return this->m_monopoly; }

    [[nodiscard]] aoc::sim::GlobalSanctionTracker& sanctions() { return this->m_sanctions; }
    [[nodiscard]] const aoc::sim::GlobalSanctionTracker& sanctions() const { return this->m_sanctions; }

    [[nodiscard]] aoc::sim::GlobalWonderTracker& wonderTracker() { return this->m_wonderTracker; }
    [[nodiscard]] const aoc::sim::GlobalWonderTracker& wonderTracker() const { return this->m_wonderTracker; }

    [[nodiscard]] aoc::sim::WorldCongressComponent& worldCongress() { return this->m_worldCongress; }
    [[nodiscard]] const aoc::sim::WorldCongressComponent& worldCongress() const { return this->m_worldCongress; }

    [[nodiscard]] aoc::sim::GlobalReligionTracker& religionTracker() { return this->m_religionTracker; }
    [[nodiscard]] const aoc::sim::GlobalReligionTracker& religionTracker() const { return this->m_religionTracker; }

    [[nodiscard]] aoc::sim::VisibilityEventBus& visibilityBus() { return this->m_visibilityBus; }
    [[nodiscard]] const aoc::sim::VisibilityEventBus& visibilityBus() const { return this->m_visibilityBus; }

    // ========================================================================
    // Global collections
    // ========================================================================

    [[nodiscard]] std::vector<aoc::sim::TradeRouteComponent>& tradeRoutes() { return this->m_tradeRoutes; }
    [[nodiscard]] const std::vector<aoc::sim::TradeRouteComponent>& tradeRoutes() const { return this->m_tradeRoutes; }

    [[nodiscard]] std::vector<aoc::sim::CommodityHoardComponent>& commodityHoards() { return this->m_commodityHoards; }
    [[nodiscard]] const std::vector<aoc::sim::CommodityHoardComponent>& commodityHoards() const { return this->m_commodityHoards; }

    [[nodiscard]] std::vector<aoc::sim::BarbarianClanComponent>& barbarianClans() { return this->m_barbarianClans; }
    [[nodiscard]] const std::vector<aoc::sim::BarbarianClanComponent>& barbarianClans() const { return this->m_barbarianClans; }

    [[nodiscard]] std::vector<aoc::sim::CityStateComponent>& cityStates() { return this->m_cityStates; }
    [[nodiscard]] const std::vector<aoc::sim::CityStateComponent>& cityStates() const { return this->m_cityStates; }

    [[nodiscard]] std::vector<aoc::sim::ConfederationComponent>& confederations() { return this->m_confederations; }
    [[nodiscard]] const std::vector<aoc::sim::ConfederationComponent>& confederations() const { return this->m_confederations; }

    [[nodiscard]] std::vector<aoc::sim::ElectricityAgreementComponent>& electricityAgreements() { return this->m_electricityAgreements; }
    [[nodiscard]] const std::vector<aoc::sim::ElectricityAgreementComponent>& electricityAgreements() const { return this->m_electricityAgreements; }

    /// WP-S: per-encampment supply buffer keyed by tile index. food + fuel
    /// (one bundle per encampment tile). Drained by nearby military units;
    /// refilled by Logistics convoys (later WP) or by initial seed.
    struct EncampmentBuffer {
        PlayerId owner = INVALID_PLAYER;
        int32_t  food  = 0;
        int32_t  fuel  = 0;
    };
    [[nodiscard]] std::unordered_map<int32_t, EncampmentBuffer>& encampments() { return this->m_encampments; }
    [[nodiscard]] const std::unordered_map<int32_t, EncampmentBuffer>& encampments() const { return this->m_encampments; }

private:
    std::vector<std::unique_ptr<Player>> m_players;
    /// City-state Player slots. Sparse by design: index i corresponds to
    /// PlayerId = CITY_STATE_PLAYER_BASE + i. Separate from m_players so
    /// major-player iteration (victory, turn loop) does not pick them up.
    std::vector<std::unique_ptr<Player>> m_cityStatePlayers;
    int32_t m_currentTurn = 0;
    std::vector<TileEvent> m_tileEvents;
    /// WP-H takeover: which player the UI follows. Persists across turns.
    PlayerId m_humanPlayerId = PlayerId{0};

    // Global state (singletons)
    aoc::sim::GlobalClimateComponent m_climate;
    aoc::sim::GlobalOilReserves m_oilReserves;
    aoc::sim::GlobalMonopolyComponent m_monopoly;
    aoc::sim::GlobalSanctionTracker m_sanctions;
    aoc::sim::GlobalWonderTracker m_wonderTracker;
    aoc::sim::WorldCongressComponent m_worldCongress;
    aoc::sim::GlobalReligionTracker m_religionTracker;
    aoc::sim::VisibilityEventBus m_visibilityBus;

    // Global collections
    std::vector<aoc::sim::TradeRouteComponent> m_tradeRoutes;
    std::vector<aoc::sim::CommodityHoardComponent> m_commodityHoards;
    std::vector<aoc::sim::BarbarianClanComponent> m_barbarianClans;
    std::vector<aoc::sim::CityStateComponent> m_cityStates;
    std::vector<aoc::sim::ConfederationComponent> m_confederations;
    std::vector<aoc::sim::ElectricityAgreementComponent> m_electricityAgreements;

    /// WP-S: encampment supply buffers keyed by tile index.
    std::unordered_map<int32_t, EncampmentBuffer> m_encampments;
};

} // namespace aoc::game
