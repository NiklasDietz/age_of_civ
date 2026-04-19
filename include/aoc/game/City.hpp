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
#include "aoc/simulation/city/CityBombardment.hpp"
#include "aoc/simulation/resource/ResourceComponent.hpp"
#include "aoc/simulation/religion/Religion.hpp"
#include "aoc/simulation/wonder/Wonder.hpp"
#include "aoc/simulation/production/Waste.hpp"
#include "aoc/simulation/production/PowerGrid.hpp"
#include "aoc/simulation/production/BuildingCapacity.hpp"
#include "aoc/simulation/production/ProductionEfficiency.hpp"
#include "aoc/simulation/production/QualityTier.hpp"
#include "aoc/simulation/economy/EconomicDepth.hpp"
#include "aoc/simulation/economy/DomesticCourier.hpp"
#include "aoc/simulation/production/Automation.hpp"
#include "aoc/simulation/economy/TechUnemployment.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace aoc::map { class HexGrid; }

namespace aoc::game {

class Player;

/// Settlement stage. Cities grow through these as population + buildings unlock.
/// Hamlet: founded by Settler, pop 1-2, 1 work ring, no districts, no production queue items beyond improvements.
/// Village: pop 3-5, 2 rings, basic buildings (Granary, Shrine, Barracks), still no districts.
/// Town: pop 6-10 + Granary built, all districts unlock, combat units + walls.
/// City: pop 11+ + any Tier-2 building (Library, Market, Temple), full late-game access.
enum class CitySize : uint8_t {
    Hamlet = 0,
    Village,
    Town,
    City,
};

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

    [[nodiscard]] float cultureBorderProgress() const { return this->m_cultureBorderProgress; }
    void setCultureBorderProgress(float prog) { this->m_cultureBorderProgress = prog; }
    void addCultureBorderProgress(float amount) { this->m_cultureBorderProgress += amount; }

    [[nodiscard]] float productionProgress() const { return this->m_productionProgress; }
    void setProductionProgress(float prog) { this->m_productionProgress = prog; }
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
    ///
    /// When `owner` is non-null, this city competes with the owner's other
    /// cities for shared-border tiles (nearest-city rule, overridden by the
    /// per-tile assignment map on the Player). When null, the city greedily
    /// scores every tile it owns within sanity range -- used by bootstrap code
    /// that runs before the owning Player has all cities wired up.
    void autoAssignWorkers(const aoc::map::HexGrid& grid,
                            aoc::sim::WorkerFocus focus = aoc::sim::WorkerFocus::Balanced,
                            const Player* owner = nullptr);

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
    // Walls (destructible fortifications)
    // ========================================================================

    [[nodiscard]] aoc::sim::CityWallState& walls() { return this->m_walls; }
    [[nodiscard]] const aoc::sim::CityWallState& walls() const { return this->m_walls; }

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
    // Extended subsystems (formerly ECS-only)
    // ========================================================================

    [[nodiscard]] aoc::sim::CityPollutionComponent& pollution() { return this->m_pollution; }
    [[nodiscard]] const aoc::sim::CityPollutionComponent& pollution() const { return this->m_pollution; }

    [[nodiscard]] aoc::sim::CityWondersComponent& wonders() { return this->m_wonders; }
    [[nodiscard]] const aoc::sim::CityWondersComponent& wonders() const { return this->m_wonders; }

    [[nodiscard]] aoc::sim::CityBuildingLevelsComponent& buildingLevels() { return this->m_buildingLevels; }
    [[nodiscard]] const aoc::sim::CityBuildingLevelsComponent& buildingLevels() const { return this->m_buildingLevels; }

    [[nodiscard]] aoc::sim::CityProductionExperienceComponent& productionExperience() { return this->m_productionExperience; }
    [[nodiscard]] const aoc::sim::CityProductionExperienceComponent& productionExperience() const { return this->m_productionExperience; }

    [[nodiscard]] aoc::sim::CityQualityComponent& quality() { return this->m_quality; }
    [[nodiscard]] const aoc::sim::CityQualityComponent& quality() const { return this->m_quality; }

    [[nodiscard]] aoc::sim::CityStrikeComponent& strike() { return this->m_strike; }
    [[nodiscard]] const aoc::sim::CityStrikeComponent& strike() const { return this->m_strike; }

    [[nodiscard]] aoc::sim::CityAutomationComponent& automation() { return this->m_automation; }
    [[nodiscard]] const aoc::sim::CityAutomationComponent& automation() const { return this->m_automation; }

    [[nodiscard]] aoc::sim::CityUnemploymentComponent& unemployment() { return this->m_unemployment; }
    [[nodiscard]] const aoc::sim::CityUnemploymentComponent& unemployment() const { return this->m_unemployment; }

    // ========================================================================
    // Specialists
    // ========================================================================

    [[nodiscard]] int32_t entertainers() const { return this->m_entertainers; }
    [[nodiscard]] int32_t scientists() const { return this->m_scientists; }
    [[nodiscard]] int32_t taxmen() const { return this->m_taxmen; }
    void setEntertainers(int32_t n) { this->m_entertainers = n; }
    void setScientists(int32_t n) { this->m_scientists = n; }
    void setTaxmen(int32_t n) { this->m_taxmen = n; }

    // ========================================================================
    // Tile management
    // ========================================================================

    [[nodiscard]] int32_t tilesClaimedCount() const { return this->m_tilesClaimedCount; }
    void incrementTilesClaimed() { ++this->m_tilesClaimedCount; }

    // ========================================================================
    // Settlement stage (Hamlet -> Village -> Town -> City)
    // ========================================================================

    [[nodiscard]] CitySize stage() const { return this->m_stage; }
    void setStage(CitySize s) { this->m_stage = s; }

    // ========================================================================
    // Standing orders (persistent courier dispatch rules)
    // ========================================================================

    [[nodiscard]] const std::vector<aoc::sim::StandingOrder>& standingOrders() const { return this->m_standingOrders; }
    [[nodiscard]] std::vector<aoc::sim::StandingOrder>& standingOrders() { return this->m_standingOrders; }

    /// Append a standing order. Caller is responsible for deduplication.
    void addStandingOrder(const aoc::sim::StandingOrder& order) { this->m_standingOrders.push_back(order); }

    /// Remove the standing order at `index`. No-op on out-of-range.
    void removeStandingOrder(std::size_t index) {
        if (index < this->m_standingOrders.size()) {
            this->m_standingOrders.erase(this->m_standingOrders.begin() + static_cast<std::ptrdiff_t>(index));
        }
    }

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
    float m_cultureBorderProgress = 0.0f;
    float m_productionProgress = 0.0f;

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

    // Extended subsystems (formerly ECS-only, now owned by City)
    aoc::sim::CityPollutionComponent m_pollution;
    aoc::sim::CityWondersComponent m_wonders;
    aoc::sim::CityBuildingLevelsComponent m_buildingLevels;
    aoc::sim::CityProductionExperienceComponent m_productionExperience;
    aoc::sim::CityQualityComponent m_quality;
    aoc::sim::CityStrikeComponent m_strike;
    aoc::sim::CityAutomationComponent m_automation;
    aoc::sim::CityUnemploymentComponent m_unemployment;
    aoc::sim::CityWallState m_walls;

    // Specialist citizens
    int32_t m_entertainers = 0;
    int32_t m_scientists = 0;
    int32_t m_taxmen = 0;

    int32_t m_tilesClaimedCount = 0;

    // Settlement stage. New cities start as Hamlet unless constructor bumps it.
    CitySize m_stage = CitySize::Hamlet;

    // Persistent dispatch rules for domestic couriers originating from this city.
    std::vector<aoc::sim::StandingOrder> m_standingOrders;
};

} // namespace aoc::game
