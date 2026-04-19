#pragma once

/**
 * @file Player.hpp
 * @brief Per-player game state using composition.
 *
 * Each Player object owns all data related to that player:
 * tech tree, economy, cities, units, diplomacy, etc.
 *
 * This replaces 35+ scattered ECS components with a single cohesive object.
 * Player isolation is structural: you can only access your own data through
 * your own Player object. No more ownership checks at every access site.
 */

#include "aoc/core/Types.hpp"
#include "aoc/map/HexCoord.hpp"
#include "aoc/simulation/tech/TechTree.hpp"
#include "aoc/simulation/tech/CivicTree.hpp"
#include "aoc/simulation/tech/EraProgression.hpp"
#include "aoc/simulation/tech/EraScore.hpp"
#include "aoc/simulation/tech/EurekaBoost.hpp"
#include "aoc/simulation/monetary/MonetarySystem.hpp"
#include "aoc/simulation/government/GovernmentComponent.hpp"
#include "aoc/simulation/religion/Religion.hpp"
#include "aoc/simulation/civilization/Civilization.hpp"
#include "aoc/simulation/diplomacy/WarWeariness.hpp"
#include "aoc/simulation/greatpeople/GreatPeople.hpp"
#include "aoc/simulation/victory/VictoryCondition.hpp"
#include "aoc/simulation/victory/SpaceRace.hpp"
#include "aoc/simulation/culture/Tourism.hpp"
#include "aoc/simulation/economy/TradeAgreement.hpp"
#include "aoc/simulation/resource/ResourceComponent.hpp"
#include "aoc/simulation/monetary/Bonds.hpp"
#include "aoc/simulation/economy/StockMarket.hpp"
#include "aoc/simulation/economy/EconomicDepth.hpp"
#include "aoc/simulation/economy/SpeculationBubble.hpp"
#include "aoc/simulation/economy/BlackMarket.hpp"
#include "aoc/simulation/economy/EnergyDependency.hpp"
#include "aoc/simulation/economy/HumanCapital.hpp"
#include "aoc/simulation/economy/SupplyChain.hpp"
#include "aoc/simulation/economy/AdvancedEconomics.hpp"
#include "aoc/simulation/economy/IndustrialRevolution.hpp"
#include "aoc/simulation/diplomacy/Grievance.hpp"
#include "aoc/simulation/monetary/CurrencyTrust.hpp"
#include "aoc/simulation/monetary/ForexMarket.hpp"
#include "aoc/simulation/monetary/CurrencyWar.hpp"
#include "aoc/simulation/monetary/CurrencyCrisis.hpp"
#include "aoc/simulation/empire/CommunicationSpeed.hpp"
#include "aoc/simulation/event/WorldEvents.hpp"
#include "aoc/simulation/automation/Automation.hpp"
#include "aoc/simulation/ai/AIBlackboard.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace aoc::map { class HexGrid; }

namespace aoc::game {

class City;
class Unit;

/**
 * @brief Complete per-player game state.
 *
 * Owns cities, units, tech tree, economy, and all other player-specific data.
 * All queries are O(1) or O(n) where n = that player's entities, never
 * searching through all players' entities.
 */
class Player {
public:
    explicit Player(PlayerId id);
    ~Player();

    Player(const Player&) = delete;
    Player& operator=(const Player&) = delete;
    Player(Player&&) noexcept;
    Player& operator=(Player&&) noexcept;

    // ========================================================================
    // Identity
    // ========================================================================

    [[nodiscard]] PlayerId id() const { return this->m_id; }
    [[nodiscard]] bool isHuman() const { return this->m_isHuman; }
    void setHuman(bool human) { this->m_isHuman = human; }

    [[nodiscard]] aoc::sim::CivId civId() const { return this->m_civId; }
    void setCivId(aoc::sim::CivId cid) { this->m_civId = cid; }

    // ========================================================================
    // Tech & Civics
    // ========================================================================

    [[nodiscard]] aoc::sim::PlayerTechComponent& tech() { return this->m_tech; }
    [[nodiscard]] const aoc::sim::PlayerTechComponent& tech() const { return this->m_tech; }

    [[nodiscard]] aoc::sim::PlayerCivicComponent& civics() { return this->m_civics; }
    [[nodiscard]] const aoc::sim::PlayerCivicComponent& civics() const { return this->m_civics; }

    /// Check if a specific tech has been researched.
    [[nodiscard]] bool hasResearched(TechId techId) const {
        return this->m_tech.hasResearched(techId);
    }

    /// Check if a resource is revealed by this player's tech.
    [[nodiscard]] bool canSeeResource(uint16_t goodId) const;

    // ========================================================================
    // Economy
    // ========================================================================

    [[nodiscard]] CurrencyAmount treasury() const { return this->m_treasury; }
    void setTreasury(CurrencyAmount amount) { this->m_treasury = amount; }
    void addGold(CurrencyAmount amount) { this->m_treasury += amount; }
    bool spendGold(CurrencyAmount amount);  ///< Returns false if insufficient

    [[nodiscard]] CurrencyAmount incomePerTurn() const { return this->m_incomePerTurn; }
    void setIncomePerTurn(CurrencyAmount income) { this->m_incomePerTurn = income; }

    [[nodiscard]] aoc::sim::MonetaryStateComponent& monetary() { return this->m_monetary; }
    [[nodiscard]] const aoc::sim::MonetaryStateComponent& monetary() const { return this->m_monetary; }

    // ========================================================================
    // Government
    // ========================================================================

    [[nodiscard]] aoc::sim::PlayerGovernmentComponent& government() { return this->m_government; }
    [[nodiscard]] const aoc::sim::PlayerGovernmentComponent& government() const { return this->m_government; }

    // ========================================================================
    // Religion
    // ========================================================================

    [[nodiscard]] aoc::sim::PlayerFaithComponent& faith() { return this->m_faith; }
    [[nodiscard]] const aoc::sim::PlayerFaithComponent& faith() const { return this->m_faith; }

    // ========================================================================
    // Era & History
    // ========================================================================

    [[nodiscard]] aoc::sim::PlayerEraComponent& era() { return this->m_era; }
    [[nodiscard]] const aoc::sim::PlayerEraComponent& era() const { return this->m_era; }

    [[nodiscard]] aoc::sim::PlayerEraScoreComponent& eraScore() { return this->m_eraScore; }
    [[nodiscard]] const aoc::sim::PlayerEraScoreComponent& eraScore() const { return this->m_eraScore; }

    [[nodiscard]] aoc::sim::PlayerWarWearinessComponent& warWeariness() { return this->m_warWeariness; }
    [[nodiscard]] const aoc::sim::PlayerWarWearinessComponent& warWeariness() const { return this->m_warWeariness; }

    [[nodiscard]] aoc::sim::PlayerEurekaComponent& eureka() { return this->m_eureka; }
    [[nodiscard]] const aoc::sim::PlayerEurekaComponent& eureka() const { return this->m_eureka; }

    [[nodiscard]] aoc::sim::PlayerGreatPeopleComponent& greatPeople() { return this->m_greatPeople; }
    [[nodiscard]] const aoc::sim::PlayerGreatPeopleComponent& greatPeople() const { return this->m_greatPeople; }

    // ========================================================================
    // Economy (extended)
    // ========================================================================

    [[nodiscard]] aoc::sim::PlayerEconomyComponent& economy() { return this->m_economy; }
    [[nodiscard]] const aoc::sim::PlayerEconomyComponent& economy() const { return this->m_economy; }

    [[nodiscard]] aoc::sim::PlayerTradeAgreementsComponent& tradeAgreements() { return this->m_tradeAgreements; }
    [[nodiscard]] const aoc::sim::PlayerTradeAgreementsComponent& tradeAgreements() const { return this->m_tradeAgreements; }

    [[nodiscard]] aoc::sim::PlayerTariffComponent& tariffs() { return this->m_tariffs; }
    [[nodiscard]] const aoc::sim::PlayerTariffComponent& tariffs() const { return this->m_tariffs; }

    // ========================================================================
    // Victory
    // ========================================================================

    [[nodiscard]] aoc::sim::VictoryTrackerComponent& victoryTracker() { return this->m_victoryTracker; }
    [[nodiscard]] const aoc::sim::VictoryTrackerComponent& victoryTracker() const { return this->m_victoryTracker; }

    [[nodiscard]] aoc::sim::PlayerSpaceRaceComponent& spaceRace() { return this->m_spaceRace; }
    [[nodiscard]] const aoc::sim::PlayerSpaceRaceComponent& spaceRace() const { return this->m_spaceRace; }

    [[nodiscard]] aoc::sim::PlayerTourismComponent& tourism() { return this->m_tourism; }
    [[nodiscard]] const aoc::sim::PlayerTourismComponent& tourism() const { return this->m_tourism; }

    // ========================================================================
    // Monetary instruments
    // ========================================================================

    [[nodiscard]] aoc::sim::PlayerBondComponent& bonds() { return this->m_bonds; }
    [[nodiscard]] const aoc::sim::PlayerBondComponent& bonds() const { return this->m_bonds; }

    [[nodiscard]] aoc::sim::PlayerStockPortfolioComponent& stockPortfolio() { return this->m_stockPortfolio; }
    [[nodiscard]] const aoc::sim::PlayerStockPortfolioComponent& stockPortfolio() const { return this->m_stockPortfolio; }

    [[nodiscard]] aoc::sim::PlayerFuturesComponent& futures() { return this->m_futures; }
    [[nodiscard]] const aoc::sim::PlayerFuturesComponent& futures() const { return this->m_futures; }

    [[nodiscard]] aoc::sim::PlayerIOUComponent& ious() { return this->m_ious; }
    [[nodiscard]] const aoc::sim::PlayerIOUComponent& ious() const { return this->m_ious; }

    [[nodiscard]] aoc::sim::PlayerInsuranceComponent& insurance() { return this->m_insurance; }
    [[nodiscard]] const aoc::sim::PlayerInsuranceComponent& insurance() const { return this->m_insurance; }

    [[nodiscard]] aoc::sim::PlayerBubbleComponent& bubble() { return this->m_bubble; }
    [[nodiscard]] const aoc::sim::PlayerBubbleComponent& bubble() const { return this->m_bubble; }

    // ========================================================================
    // Economy (deep systems)
    // ========================================================================

    [[nodiscard]] aoc::sim::PlayerMigrationComponent& migration() { return this->m_migration; }
    [[nodiscard]] const aoc::sim::PlayerMigrationComponent& migration() const { return this->m_migration; }

    [[nodiscard]] aoc::sim::PlayerBlackMarketComponent& blackMarket() { return this->m_blackMarket; }
    [[nodiscard]] const aoc::sim::PlayerBlackMarketComponent& blackMarket() const { return this->m_blackMarket; }

    [[nodiscard]] aoc::sim::PlayerEnergyComponent& energy() { return this->m_energy; }
    [[nodiscard]] const aoc::sim::PlayerEnergyComponent& energy() const { return this->m_energy; }

    [[nodiscard]] aoc::sim::PlayerHumanCapitalComponent& humanCapital() { return this->m_humanCapital; }
    [[nodiscard]] const aoc::sim::PlayerHumanCapitalComponent& humanCapital() const { return this->m_humanCapital; }

    [[nodiscard]] aoc::sim::PlayerSupplyChainComponent& supplyChain() { return this->m_supplyChain; }
    [[nodiscard]] const aoc::sim::PlayerSupplyChainComponent& supplyChain() const { return this->m_supplyChain; }

    [[nodiscard]] aoc::sim::PlayerIndustrialComponent& industrial() { return this->m_industrial; }
    [[nodiscard]] const aoc::sim::PlayerIndustrialComponent& industrial() const { return this->m_industrial; }

    [[nodiscard]] aoc::sim::PlayerBankingComponent& banking() { return this->m_banking; }
    [[nodiscard]] const aoc::sim::PlayerBankingComponent& banking() const { return this->m_banking; }

    // ========================================================================
    // Diplomacy (extended)
    // ========================================================================

    [[nodiscard]] aoc::sim::PlayerGrievanceComponent& grievances() { return this->m_grievances; }
    [[nodiscard]] const aoc::sim::PlayerGrievanceComponent& grievances() const { return this->m_grievances; }

    // ========================================================================
    // Currency
    // ========================================================================

    [[nodiscard]] aoc::sim::CurrencyTrustComponent& currencyTrust() { return this->m_currencyTrust; }
    [[nodiscard]] const aoc::sim::CurrencyTrustComponent& currencyTrust() const { return this->m_currencyTrust; }

    [[nodiscard]] aoc::sim::CurrencyExchangeComponent& currencyExchange() { return this->m_currencyExchange; }
    [[nodiscard]] const aoc::sim::CurrencyExchangeComponent& currencyExchange() const { return this->m_currencyExchange; }

    [[nodiscard]] aoc::sim::CurrencyDevaluationComponent& currencyDevaluation() { return this->m_currencyDevaluation; }
    [[nodiscard]] const aoc::sim::CurrencyDevaluationComponent& currencyDevaluation() const { return this->m_currencyDevaluation; }

    [[nodiscard]] aoc::sim::CurrencyCrisisComponent& currencyCrisis() { return this->m_currencyCrisis; }
    [[nodiscard]] const aoc::sim::CurrencyCrisisComponent& currencyCrisis() const { return this->m_currencyCrisis; }

    // ========================================================================
    // Empire management
    // ========================================================================

    [[nodiscard]] aoc::sim::PlayerCommunicationComponent& communication() { return this->m_communication; }
    [[nodiscard]] const aoc::sim::PlayerCommunicationComponent& communication() const { return this->m_communication; }

    // ========================================================================
    // Events & Automation
    // ========================================================================

    [[nodiscard]] aoc::sim::PlayerEventComponent& events() { return this->m_events; }
    [[nodiscard]] const aoc::sim::PlayerEventComponent& events() const { return this->m_events; }

    [[nodiscard]] aoc::sim::PlayerResearchQueueComponent& researchQueue() { return this->m_researchQueue; }
    [[nodiscard]] const aoc::sim::PlayerResearchQueueComponent& researchQueue() const { return this->m_researchQueue; }

    [[nodiscard]] aoc::sim::PlayerTradeAutoRenewComponent& tradeAutoRenew() { return this->m_tradeAutoRenew; }
    [[nodiscard]] const aoc::sim::PlayerTradeAutoRenewComponent& tradeAutoRenew() const { return this->m_tradeAutoRenew; }

    // ========================================================================
    // Cities
    // ========================================================================

    [[nodiscard]] std::vector<std::unique_ptr<City>>& cities() { return this->m_cities; }
    [[nodiscard]] const std::vector<std::unique_ptr<City>>& cities() const { return this->m_cities; }

    /// Find a city by location. Returns nullptr if not found.
    [[nodiscard]] City* cityAt(aoc::hex::AxialCoord location);
    [[nodiscard]] const City* cityAt(aoc::hex::AxialCoord location) const;

    /// Add a new city. Returns a reference to the new city.
    City& addCity(aoc::hex::AxialCoord location, const std::string& name);

    /// Number of cities.
    [[nodiscard]] int32_t cityCount() const { return static_cast<int32_t>(this->m_cities.size()); }

    // ========================================================================
    // Per-tile city assignment
    //
    // A tile is always owned by at most one Player (grid.owner). When the
    // player has multiple cities, each owned tile is logically managed by
    // exactly one of them for worker placement and yield attribution.
    //
    // Default behaviour (no explicit entry): the tile goes to the player's
    // nearest city. The player can override this at any time during their
    // turn via `setTileCity` -- useful when two cities share a border and
    // the player wants to steer a high-yield tile to a specific city.
    // The override key is the tile's flat grid index; the value is the
    // managing city's location (cities have no numeric ID, but their
    // location is unique and stable).
    // ========================================================================

    /// Look up the managing-city location for a tile. Returns nullptr if no
    /// override exists (fall back to nearest-city rule).
    [[nodiscard]] const aoc::hex::AxialCoord* tileCityOverride(int32_t tileIdx) const {
        auto it = this->m_tileCityAssignment.find(tileIdx);
        return (it == this->m_tileCityAssignment.end()) ? nullptr : &it->second;
    }

    /// Assign a tile to a specific city of this player. Passing a default-
    /// constructed coord clears the override and restores nearest-city logic.
    void setTileCity(int32_t tileIdx, aoc::hex::AxialCoord cityLoc) {
        this->m_tileCityAssignment[tileIdx] = cityLoc;
    }
    void clearTileCity(int32_t tileIdx) {
        this->m_tileCityAssignment.erase(tileIdx);
    }

    // ========================================================================
    // Units
    // ========================================================================

    [[nodiscard]] std::vector<std::unique_ptr<Unit>>& units() { return this->m_units; }
    [[nodiscard]] const std::vector<std::unique_ptr<Unit>>& units() const { return this->m_units; }

    /// Find a unit at a location. Returns nullptr if not found.
    [[nodiscard]] Unit* unitAt(aoc::hex::AxialCoord location);
    [[nodiscard]] const Unit* unitAt(aoc::hex::AxialCoord location) const;

    /// Add a new unit. Returns a reference to the new unit.
    Unit& addUnit(UnitTypeId typeId, aoc::hex::AxialCoord position);

    /// Remove a unit (destroyed, disbanded, etc.).
    void removeUnit(Unit* unit);

    /// Number of units.
    [[nodiscard]] int32_t unitCount() const { return static_cast<int32_t>(this->m_units.size()); }

    /// Count military units.
    [[nodiscard]] int32_t militaryUnitCount() const;

    // ========================================================================
    // AI Blackboard
    // ========================================================================

    /**
     * @brief Mutable access to the AI blackboard for advisor writes and reads.
     *
     * Human players own a blackboard too; it is simply never written by advisors.
     * This keeps the data layout uniform and avoids nullable pointer overhead.
     */
    [[nodiscard]] aoc::sim::ai::AIBlackboard& blackboard() { return this->m_blackboard; }
    [[nodiscard]] const aoc::sim::ai::AIBlackboard& blackboard() const { return this->m_blackboard; }

    // ========================================================================
    // Derived queries
    // ========================================================================

    /// Total population across all cities.
    [[nodiscard]] int32_t totalPopulation() const;

    /// Total science output per turn.
    [[nodiscard]] float sciencePerTurn(const aoc::map::HexGrid& grid) const;

    /// Total culture output per turn.
    [[nodiscard]] float culturePerTurn(const aoc::map::HexGrid& grid) const;

    /// Total gold income per turn (before maintenance).
    [[nodiscard]] CurrencyAmount goldIncome(const aoc::map::HexGrid& grid) const;

private:
    PlayerId m_id;
    bool m_isHuman = false;
    aoc::sim::CivId m_civId = 0;

    // Tech & Civics
    aoc::sim::PlayerTechComponent m_tech;
    aoc::sim::PlayerCivicComponent m_civics;

    // Economy
    CurrencyAmount m_treasury = 0;  ///< Starts at 0: no money at game start (barter)
    CurrencyAmount m_incomePerTurn = 0;
    aoc::sim::MonetaryStateComponent m_monetary;

    // Government
    aoc::sim::PlayerGovernmentComponent m_government;

    // Religion
    aoc::sim::PlayerFaithComponent m_faith;

    // Era & History
    aoc::sim::PlayerEraComponent m_era;
    aoc::sim::PlayerEraScoreComponent m_eraScore;
    aoc::sim::PlayerWarWearinessComponent m_warWeariness;
    aoc::sim::PlayerEurekaComponent m_eureka;
    aoc::sim::PlayerGreatPeopleComponent m_greatPeople;

    // Economy (extended)
    aoc::sim::PlayerEconomyComponent m_economy;  ///< Supply/demand/needs tracking
    aoc::sim::PlayerTradeAgreementsComponent m_tradeAgreements;
    aoc::sim::PlayerTariffComponent m_tariffs;

    // Victory
    aoc::sim::VictoryTrackerComponent m_victoryTracker;
    aoc::sim::PlayerSpaceRaceComponent m_spaceRace;
    aoc::sim::PlayerTourismComponent m_tourism;

    // Monetary instruments
    aoc::sim::PlayerBondComponent m_bonds;
    aoc::sim::PlayerStockPortfolioComponent m_stockPortfolio;
    aoc::sim::PlayerFuturesComponent m_futures;
    aoc::sim::PlayerIOUComponent m_ious;
    aoc::sim::PlayerInsuranceComponent m_insurance;
    aoc::sim::PlayerBubbleComponent m_bubble;

    // Economy (deep systems)
    aoc::sim::PlayerMigrationComponent m_migration;
    aoc::sim::PlayerBlackMarketComponent m_blackMarket;
    aoc::sim::PlayerEnergyComponent m_energy;
    aoc::sim::PlayerHumanCapitalComponent m_humanCapital;
    aoc::sim::PlayerSupplyChainComponent m_supplyChain;
    aoc::sim::PlayerIndustrialComponent m_industrial;
    aoc::sim::PlayerBankingComponent m_banking;

    // Diplomacy (extended)
    aoc::sim::PlayerGrievanceComponent m_grievances;

    // Currency
    aoc::sim::CurrencyTrustComponent m_currencyTrust;
    aoc::sim::CurrencyExchangeComponent m_currencyExchange;
    aoc::sim::CurrencyDevaluationComponent m_currencyDevaluation;
    aoc::sim::CurrencyCrisisComponent m_currencyCrisis;

    // Empire management
    aoc::sim::PlayerCommunicationComponent m_communication;

    // Events & Automation
    aoc::sim::PlayerEventComponent m_events;
    aoc::sim::PlayerResearchQueueComponent m_researchQueue;
    aoc::sim::PlayerTradeAutoRenewComponent m_tradeAutoRenew;

    // Owned entities
    std::vector<std::unique_ptr<City>> m_cities;
    std::vector<std::unique_ptr<Unit>> m_units;

    // Per-tile manual city assignment (flat tile index -> managing city's
    // location). Overrides the default nearest-city rule during worker placement.
    std::unordered_map<int32_t, aoc::hex::AxialCoord> m_tileCityAssignment;

    // AI coordination blackboard (written by advisors, read by all AI subsystems)
    aoc::sim::ai::AIBlackboard m_blackboard;
};

} // namespace aoc::game
