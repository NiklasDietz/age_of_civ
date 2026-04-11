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
#include "aoc/simulation/economy/TradeAgreement.hpp"
#include "aoc/simulation/resource/ResourceComponent.hpp"

#include <cstdint>
#include <memory>
#include <string>
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

    // ========================================================================
    // Victory
    // ========================================================================

    [[nodiscard]] aoc::sim::VictoryTrackerComponent& victoryTracker() { return this->m_victoryTracker; }
    [[nodiscard]] const aoc::sim::VictoryTrackerComponent& victoryTracker() const { return this->m_victoryTracker; }

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
    CurrencyAmount m_treasury = 100;
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

    // Victory
    aoc::sim::VictoryTrackerComponent m_victoryTracker;

    // Owned entities
    std::vector<std::unique_ptr<City>> m_cities;
    std::vector<std::unique_ptr<Unit>> m_units;
};

} // namespace aoc::game
