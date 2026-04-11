#pragma once

/**
 * @file City.hpp
 * @brief Per-city game state with composition-based subsystems.
 *
 * Each City owns its production queue, citizen management, building list,
 * loyalty tracking, and happiness state. No more scattered components.
 *
 * City lifecycle:
 *   1. Created via Player::addCity() when a settler founds a city
 *   2. Grows population from food surplus
 *   3. Produces units/buildings/wonders via the production queue
 *   4. Can flip ownership via loyalty system
 *   5. Can be captured via military conquest
 */

#include "aoc/core/Types.hpp"
#include "aoc/map/HexCoord.hpp"
#include "aoc/simulation/city/CityComponent.hpp"
#include "aoc/simulation/city/ProductionQueue.hpp"
#include "aoc/simulation/city/CityLoyalty.hpp"
#include "aoc/simulation/city/Happiness.hpp"
#include "aoc/simulation/city/District.hpp"
#include "aoc/simulation/city/Governor.hpp"
#include "aoc/simulation/resource/ResourceComponent.hpp"
#include "aoc/simulation/religion/Religion.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace aoc::map { class HexGrid; }

namespace aoc::game {

/**
 * @brief Complete per-city state.
 *
 * Replaces: CityComponent, CityLoyaltyComponent, CityDistrictsComponent,
 * CityGovernorComponent, CityHappinessComponent, ProductionQueueComponent,
 * CityStockpileComponent, CityReligionComponent, CityUnemploymentComponent,
 * CityWageComponent, CityStrikeComponent, CityLaborComponent, etc.
 */
class City {
public:
    City(PlayerId owner, aoc::hex::AxialCoord location, const std::string& name);
    ~City() = default;

    City(const City&) = delete;
    City& operator=(const City&) = delete;
    City(City&&) noexcept = default;
    City& operator=(City&&) noexcept = default;

    // ========================================================================
    // Identity
    // ========================================================================

    [[nodiscard]] PlayerId owner() const { return this->m_owner; }
    void setOwner(PlayerId newOwner) { this->m_owner = newOwner; }

    [[nodiscard]] const std::string& name() const { return this->m_name; }
    [[nodiscard]] aoc::hex::AxialCoord location() const { return this->m_location; }

    [[nodiscard]] bool isOriginalCapital() const { return this->m_isOriginalCapital; }
    void setOriginalCapital(bool val) { this->m_isOriginalCapital = val; }

    [[nodiscard]] PlayerId originalOwner() const { return this->m_originalOwner; }
    void setOriginalOwner(PlayerId owner) { this->m_originalOwner = owner; }

    // ========================================================================
    // Population & Growth
    // ========================================================================

    [[nodiscard]] int32_t population() const { return this->m_population; }
    void setPopulation(int32_t pop) { this->m_population = pop; }
    void growPopulation(int32_t amount = 1) { this->m_population += amount; }

    [[nodiscard]] float foodSurplus() const { return this->m_foodSurplus; }
    void setFoodSurplus(float surplus) { this->m_foodSurplus = surplus; }

    /// Available citizen slots (population, since center tile is free).
    [[nodiscard]] int32_t availableCitizens() const;

    // ========================================================================
    // Citizen Management (worked tiles)
    // ========================================================================

    [[nodiscard]] const std::vector<aoc::hex::AxialCoord>& workedTiles() const { return this->m_workedTiles; }
    [[nodiscard]] std::vector<aoc::hex::AxialCoord>& workedTiles() { return this->m_workedTiles; }

    [[nodiscard]] bool isTileWorked(aoc::hex::AxialCoord tile) const;
    void assignWorker(aoc::hex::AxialCoord tile);
    void removeWorker(aoc::hex::AxialCoord tile);
    void toggleWorker(aoc::hex::AxialCoord tile);

    [[nodiscard]] bool isTileLocked(aoc::hex::AxialCoord tile) const;
    void toggleTileLock(aoc::hex::AxialCoord tile);

    /// Auto-assign workers based on focus priority.
    void autoAssignWorkers(const aoc::map::HexGrid& grid,
                            aoc::sim::WorkerFocus focus = aoc::sim::WorkerFocus::Balanced);

    // ========================================================================
    // Production
    // ========================================================================

    [[nodiscard]] aoc::sim::ProductionQueueComponent& production() { return this->m_production; }
    [[nodiscard]] const aoc::sim::ProductionQueueComponent& production() const { return this->m_production; }

    // ========================================================================
    // Districts & Buildings
    // ========================================================================

    [[nodiscard]] aoc::sim::CityDistrictsComponent& districts() { return this->m_districts; }
    [[nodiscard]] const aoc::sim::CityDistrictsComponent& districts() const { return this->m_districts; }

    [[nodiscard]] bool hasDistrict(aoc::sim::DistrictType type) const {
        return this->m_districts.hasDistrict(type);
    }
    [[nodiscard]] bool hasBuilding(BuildingId id) const {
        return this->m_districts.hasBuilding(id);
    }

    // ========================================================================
    // Loyalty
    // ========================================================================

    [[nodiscard]] aoc::sim::CityLoyaltyComponent& loyalty() { return this->m_loyalty; }
    [[nodiscard]] const aoc::sim::CityLoyaltyComponent& loyalty() const { return this->m_loyalty; }

    // ========================================================================
    // Happiness
    // ========================================================================

    [[nodiscard]] aoc::sim::CityHappinessComponent& happiness() { return this->m_happiness; }
    [[nodiscard]] const aoc::sim::CityHappinessComponent& happiness() const { return this->m_happiness; }

    // ========================================================================
    // Governor
    // ========================================================================

    [[nodiscard]] aoc::sim::CityGovernorComponent& governor() { return this->m_governor; }
    [[nodiscard]] const aoc::sim::CityGovernorComponent& governor() const { return this->m_governor; }

    // ========================================================================
    // Stockpile (resources stored in this city)
    // ========================================================================

    [[nodiscard]] aoc::sim::CityStockpileComponent& stockpile() { return this->m_stockpile; }
    [[nodiscard]] const aoc::sim::CityStockpileComponent& stockpile() const { return this->m_stockpile; }

    // ========================================================================
    // Religion
    // ========================================================================

    [[nodiscard]] aoc::sim::CityReligionComponent& religion() { return this->m_religion; }
    [[nodiscard]] const aoc::sim::CityReligionComponent& religion() const { return this->m_religion; }

    // ========================================================================
    // Tile management
    // ========================================================================

    [[nodiscard]] int32_t tilesClaimedCount() const { return this->m_tilesClaimedCount; }
    void incrementTilesClaimed() { ++this->m_tilesClaimedCount; }

private:
    // Identity
    PlayerId m_owner;
    aoc::hex::AxialCoord m_location;
    std::string m_name;
    bool m_isOriginalCapital = false;
    PlayerId m_originalOwner = INVALID_PLAYER;

    // Population
    int32_t m_population = 1;
    float m_foodSurplus = 0.0f;

    // Citizens
    std::vector<aoc::hex::AxialCoord> m_workedTiles;
    std::vector<aoc::hex::AxialCoord> m_lockedTiles;

    // Subsystems (owned by composition)
    aoc::sim::ProductionQueueComponent m_production;
    aoc::sim::CityDistrictsComponent m_districts;
    aoc::sim::CityLoyaltyComponent m_loyalty;
    aoc::sim::CityHappinessComponent m_happiness;
    aoc::sim::CityGovernorComponent m_governor;
    aoc::sim::CityStockpileComponent m_stockpile;
    aoc::sim::CityReligionComponent m_religion;

    int32_t m_tilesClaimedCount = 0;
};

} // namespace aoc::game
