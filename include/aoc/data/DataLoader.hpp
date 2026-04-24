#pragma once

/**
 * @file DataLoader.hpp
 * @brief Loads game definitions from JSON data files at startup.
 *
 * The DataLoader reads JSON files from the data/definitions/ directory and
 * populates runtime vectors for buildings, units, techs, recipes, goods,
 * and leader personalities. If a JSON file is missing or fails to parse,
 * the loader falls back to the hardcoded constexpr arrays defined in the
 * corresponding headers.
 *
 * Game systems should prefer the runtime data from DataLoader over the
 * constexpr fallbacks. The constexpr arrays remain as compile-time defaults
 * so the game can still run without any data files.
 */

#include "aoc/core/Types.hpp"
#include "aoc/simulation/city/District.hpp"
#include "aoc/simulation/map/Improvement.hpp"
#include "aoc/simulation/unit/UnitTypes.hpp"
#include "aoc/simulation/tech/TechTree.hpp"
#include "aoc/simulation/resource/ResourceTypes.hpp"
#include "aoc/simulation/ai/LeaderPersonality.hpp"
#include "aoc/map/Terrain.hpp"

#include <string>
#include <vector>

namespace aoc::data {

/// Runtime building definition with owned string storage.
struct RuntimeBuildingDef {
    aoc::BuildingId     id;
    std::string         name;
    aoc::sim::DistrictType requiredDistrict;
    int32_t             productionCost;
    int32_t             maintenanceCost;
    int32_t             productionBonus;
    int32_t             scienceBonus;
    int32_t             goldBonus;
    float               scienceMultiplier;
};

/// Runtime unit type definition with owned string storage.
struct RuntimeUnitTypeDef {
    aoc::UnitTypeId     id;
    std::string         name;
    aoc::sim::UnitClass unitClass;
    int32_t             maxHitPoints;
    int32_t             combatStrength;
    int32_t             rangedStrength;
    int32_t             range;
    int32_t             movementPoints;
    int32_t             productionCost;
};

/// Runtime tech definition with owned string storage.
struct RuntimeTechDef {
    aoc::TechId                     id;
    std::string                     name;
    aoc::EraId                      era;
    int32_t                         researchCost;
    std::vector<aoc::TechId>        prerequisites;
    std::vector<uint16_t>           unlockedGoods;
    std::vector<aoc::BuildingId>    unlockedBuildings;
    std::vector<aoc::UnitTypeId>    unlockedUnits;
};

/// Runtime recipe input with owned storage.
struct RuntimeRecipeInput {
    uint16_t goodId;
    int32_t  amount;
    bool     consumed;
};

/// Runtime production recipe with owned string storage.
struct RuntimeProductionRecipe {
    uint16_t                            recipeId;
    std::string                         name;
    std::vector<RuntimeRecipeInput>     inputs;
    uint16_t                            outputGoodId;
    int32_t                             outputAmount;
    aoc::BuildingId                     requiredBuilding;
    int32_t                             turnsToProcess;
};

/// Runtime good definition with owned string storage.
struct RuntimeGoodDef {
    uint16_t                id;
    std::string             name;
    aoc::sim::GoodCategory  category;
    int32_t                 basePrice;
    bool                    isStrategic;
    float                   priceElasticity;
};

/// Runtime improvement definition with owned string storage. Mirrors
/// aoc::sim::ImprovementDef from the constexpr IMPROVEMENT_DEFS array.
/// WP-C7: source of truth migrated to data/definitions/improvements.json.
struct RuntimeImprovementDef {
    aoc::map::ImprovementType type;
    std::string               name;
    aoc::map::TileYield       yieldBonus;
    aoc::TechId               requiredTech;
    int32_t                   buildTurns;
};

/// Runtime leader personality with owned string storage.
struct RuntimeLeaderPersonalityDef {
    aoc::sim::CivId                 civId;
    std::string                     agendaName;
    std::string                     agendaDescription;
    aoc::sim::AgendaCondition       likeCondition;
    aoc::sim::AgendaCondition       dislikeCondition;
    aoc::sim::LeaderBehavior        behavior;
};

/**
 * @brief Central data loader that reads JSON definitions at game startup.
 *
 * Call initialize() once at startup with the path to the data directory.
 * After initialization, access the loaded definitions via the getter methods.
 * If any JSON file fails to load, the corresponding fallback from the
 * hardcoded constexpr arrays is used.
 */
class DataLoader {
public:
    /**
     * @brief Load all game definitions from JSON files.
     *
     * Reads JSON files from dataDirectory/definitions/. Falls back to hardcoded
     * defaults for any file that is missing or unparseable.
     *
     * @param dataDirectory  Path to the data/ directory (e.g., "data" or "../data").
     * @return true if all files loaded successfully, false if any fell back to defaults.
     */
    [[nodiscard]] bool initialize(const std::string& dataDirectory);

    /// Access loaded building definitions.
    [[nodiscard]] const std::vector<RuntimeBuildingDef>& buildings() const { return this->buildingDefs; }

    /// Access loaded unit type definitions.
    [[nodiscard]] const std::vector<RuntimeUnitTypeDef>& units() const { return this->unitTypeDefs; }

    /// Access loaded tech definitions.
    [[nodiscard]] const std::vector<RuntimeTechDef>& techs() const { return this->techDefs; }

    /// Access loaded production recipes.
    [[nodiscard]] const std::vector<RuntimeProductionRecipe>& recipes() const { return this->recipeDefs; }

    /// Access loaded good definitions.
    [[nodiscard]] const std::vector<RuntimeGoodDef>& goods() const { return this->goodDefs; }

    /// Access loaded leader personality definitions.
    [[nodiscard]] const std::vector<RuntimeLeaderPersonalityDef>& leaders() const { return this->leaderDefs; }

    /// Access loaded improvement definitions (WP-C7).
    [[nodiscard]] const std::vector<RuntimeImprovementDef>& improvements() const { return this->improvementDefs; }

    /// Singleton access. The DataLoader is created once and shared globally.
    [[nodiscard]] static DataLoader& instance();

private:
    bool loadBuildings(const std::string& path);
    bool loadUnits(const std::string& path);
    bool loadTechs(const std::string& path);
    bool loadRecipes(const std::string& path);
    bool loadGoods(const std::string& path);
    bool loadLeaders(const std::string& path);
    bool loadImprovements(const std::string& path);

    void fallbackBuildings();
    void fallbackUnits();
    void fallbackTechs();
    void fallbackGoods();
    void fallbackRecipes();
    void fallbackLeaders();
    void fallbackImprovements();

    std::vector<RuntimeBuildingDef>           buildingDefs;
    std::vector<RuntimeUnitTypeDef>           unitTypeDefs;
    std::vector<RuntimeTechDef>               techDefs;
    std::vector<RuntimeProductionRecipe>      recipeDefs;
    std::vector<RuntimeGoodDef>               goodDefs;
    std::vector<RuntimeLeaderPersonalityDef>  leaderDefs;
    std::vector<RuntimeImprovementDef>        improvementDefs;
};

} // namespace aoc::data
