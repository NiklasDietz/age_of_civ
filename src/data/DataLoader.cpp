/**
 * @file DataLoader.cpp
 * @brief Implementation of JSON data file loading for game definitions.
 */

#include "aoc/data/DataLoader.hpp"
#include "aoc/data/JsonParser.hpp"
#include "aoc/core/Log.hpp"

#include <fstream>
#include <sstream>

namespace aoc::data {

namespace {

/// Read an entire file into a string. Returns empty string on failure.
[[nodiscard]] std::string readFileContents(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        return std::string{};
    }
    std::ostringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

/// Parse a DistrictType from its string name.
[[nodiscard]] aoc::sim::DistrictType parseDistrictType(const std::string& name) {
    if (name == "CityCenter")  { return aoc::sim::DistrictType::CityCenter; }
    if (name == "Industrial")  { return aoc::sim::DistrictType::Industrial; }
    if (name == "Commercial")  { return aoc::sim::DistrictType::Commercial; }
    if (name == "Campus")      { return aoc::sim::DistrictType::Campus; }
    if (name == "HolySite")    { return aoc::sim::DistrictType::HolySite; }
    if (name == "Harbor")      { return aoc::sim::DistrictType::Harbor; }
    if (name == "Encampment")  { return aoc::sim::DistrictType::Encampment; }
    LOG_WARN("Unknown district type: '%s', defaulting to CityCenter", name.c_str());
    return aoc::sim::DistrictType::CityCenter;
}

/// Parse a UnitClass from its string name.
[[nodiscard]] aoc::sim::UnitClass parseUnitClass(const std::string& name) {
    if (name == "Melee")     { return aoc::sim::UnitClass::Melee; }
    if (name == "Ranged")    { return aoc::sim::UnitClass::Ranged; }
    if (name == "Cavalry")   { return aoc::sim::UnitClass::Cavalry; }
    if (name == "Settler")   { return aoc::sim::UnitClass::Settler; }
    if (name == "Scout")     { return aoc::sim::UnitClass::Scout; }
    if (name == "Civilian")  { return aoc::sim::UnitClass::Civilian; }
    if (name == "Naval")     { return aoc::sim::UnitClass::Naval; }
    if (name == "Religious") { return aoc::sim::UnitClass::Religious; }
    LOG_WARN("Unknown unit class: '%s', defaulting to Melee", name.c_str());
    return aoc::sim::UnitClass::Melee;
}

/// Parse a GoodCategory from its string name.
[[nodiscard]] aoc::sim::GoodCategory parseGoodCategory(const std::string& name) {
    if (name == "RawStrategic") { return aoc::sim::GoodCategory::RawStrategic; }
    if (name == "RawLuxury")    { return aoc::sim::GoodCategory::RawLuxury; }
    if (name == "RawBonus")     { return aoc::sim::GoodCategory::RawBonus; }
    if (name == "Processed")    { return aoc::sim::GoodCategory::Processed; }
    if (name == "Advanced")     { return aoc::sim::GoodCategory::Advanced; }
    if (name == "Monetary")     { return aoc::sim::GoodCategory::Monetary; }
    LOG_WARN("Unknown good category: '%s', defaulting to RawBonus", name.c_str());
    return aoc::sim::GoodCategory::RawBonus;
}

/// Parse an ImprovementType from its string name. Used by the JSON loader.
[[nodiscard]] aoc::map::ImprovementType parseImprovementType(const std::string& name) {
    using I = aoc::map::ImprovementType;
    if (name == "None")              { return I::None; }
    if (name == "Farm")              { return I::Farm; }
    if (name == "Mine")              { return I::Mine; }
    if (name == "Plantation")        { return I::Plantation; }
    if (name == "Quarry")            { return I::Quarry; }
    if (name == "LumberMill")        { return I::LumberMill; }
    if (name == "Camp")              { return I::Camp; }
    if (name == "Pasture")           { return I::Pasture; }
    if (name == "FishingBoats")      { return I::FishingBoats; }
    if (name == "Fort")              { return I::Fort; }
    if (name == "Road")              { return I::Road; }
    if (name == "Railway")           { return I::Railway; }
    if (name == "Highway")           { return I::Highway; }
    if (name == "Dam")               { return I::Dam; }
    if (name == "Vineyard")          { return I::Vineyard; }
    if (name == "SilkFarm")          { return I::SilkFarm; }
    if (name == "SpiceFarm")         { return I::SpiceFarm; }
    if (name == "DyeWorks")          { return I::DyeWorks; }
    if (name == "CottonField")       { return I::CottonField; }
    if (name == "Workshop")          { return I::Workshop; }
    if (name == "Canal")             { return I::Canal; }
    if (name == "MountainMine")      { return I::MountainMine; }
    if (name == "Observatory")       { return I::Observatory; }
    if (name == "Monastery")         { return I::Monastery; }
    if (name == "HeritageSite")      { return I::HeritageSite; }
    if (name == "TerraceFarm")       { return I::TerraceFarm; }
    if (name == "BiogasPlant")       { return I::BiogasPlant; }
    if (name == "SolarFarm")         { return I::SolarFarm; }
    if (name == "WindFarm")          { return I::WindFarm; }
    if (name == "OffshorePlatform")  { return I::OffshorePlatform; }
    if (name == "RecyclingCenter")   { return I::RecyclingCenter; }
    if (name == "GeothermalVent")    { return I::GeothermalVent; }
    if (name == "DesalinationPlant") { return I::DesalinationPlant; }
    if (name == "VerticalFarm")      { return I::VerticalFarm; }
    if (name == "DataCenter")        { return I::DataCenter; }
    if (name == "TradingPost")       { return I::TradingPost; }
    if (name == "MangroveNursery")   { return I::MangroveNursery; }
    if (name == "KelpFarm")          { return I::KelpFarm; }
    if (name == "FishFarm")          { return I::FishFarm; }
    if (name == "Greenhouse")        { return I::Greenhouse; }
    LOG_WARN("Unknown improvement type: '%s', defaulting to None", name.c_str());
    return I::None;
}

/// Parse an AgendaCondition from its string name.
[[nodiscard]] aoc::sim::AgendaCondition parseAgendaCondition(const std::string& name) {
    if (name == "None")                   { return aoc::sim::AgendaCondition::None; }
    if (name == "HasMoreMilitary")        { return aoc::sim::AgendaCondition::HasMoreMilitary; }
    if (name == "HasLessMilitary")        { return aoc::sim::AgendaCondition::HasLessMilitary; }
    if (name == "HasMoreLuxuries")        { return aoc::sim::AgendaCondition::HasMoreLuxuries; }
    if (name == "HasMoreCities")          { return aoc::sim::AgendaCondition::HasMoreCities; }
    if (name == "HasHigherScience")       { return aoc::sim::AgendaCondition::HasHigherScience; }
    if (name == "HasHigherCulture")       { return aoc::sim::AgendaCondition::HasHigherCulture; }
    if (name == "HasStrongEconomy")       { return aoc::sim::AgendaCondition::HasStrongEconomy; }
    if (name == "IsAtWarWithAnyone")      { return aoc::sim::AgendaCondition::IsAtWarWithAnyone; }
    if (name == "HasDifferentReligion")   { return aoc::sim::AgendaCondition::HasDifferentReligion; }
    if (name == "HasDifferentGovernment") { return aoc::sim::AgendaCondition::HasDifferentGovernment; }
    if (name == "IsTradePartner")         { return aoc::sim::AgendaCondition::IsTradePartner; }
    if (name == "HasNuclearWeapons")      { return aoc::sim::AgendaCondition::HasNuclearWeapons; }
    if (name == "HasColonies")            { return aoc::sim::AgendaCondition::HasColonies; }
    if (name == "IsReserveCurrency")      { return aoc::sim::AgendaCondition::IsReserveCurrency; }
    LOG_WARN("Unknown agenda condition: '%s', defaulting to None", name.c_str());
    return aoc::sim::AgendaCondition::None;
}

} // anonymous namespace

// ============================================================================
// Singleton
// ============================================================================

DataLoader& DataLoader::instance() {
    static DataLoader loader;
    return loader;
}

// ============================================================================
// Main initialization
// ============================================================================

bool DataLoader::initialize(const std::string& dataDirectory) {
    std::string defDir = dataDirectory + "/definitions";
    bool allSucceeded = true;

    LOG_INFO("DataLoader: loading game definitions from '%s'", defDir.c_str());

    if (!this->loadBuildings(defDir + "/buildings.json")) {
        LOG_WARN("DataLoader: buildings.json failed, using hardcoded fallback");
        this->fallbackBuildings();
        allSucceeded = false;
    }

    if (!this->loadUnits(defDir + "/units.json")) {
        LOG_WARN("DataLoader: units.json failed, using hardcoded fallback");
        this->fallbackUnits();
        allSucceeded = false;
    }

    if (!this->loadTechs(defDir + "/techs.json")) {
        LOG_WARN("DataLoader: techs.json failed, using hardcoded fallback");
        this->fallbackTechs();
        allSucceeded = false;
    }

    if (!this->loadRecipes(defDir + "/recipes.json")) {
        LOG_WARN("DataLoader: recipes.json failed, using hardcoded fallback");
        this->fallbackRecipes();
        allSucceeded = false;
    }

    if (!this->loadGoods(defDir + "/goods.json")) {
        LOG_WARN("DataLoader: goods.json failed, using hardcoded fallback");
        this->fallbackGoods();
        allSucceeded = false;
    }

    if (!this->loadLeaders(defDir + "/leaders.json")) {
        LOG_WARN("DataLoader: leaders.json failed, using hardcoded fallback");
        this->fallbackLeaders();
        allSucceeded = false;
    }

    if (!this->loadImprovements(defDir + "/improvements.json")) {
        LOG_WARN("DataLoader: improvements.json failed, using hardcoded fallback");
        this->fallbackImprovements();
        allSucceeded = false;
    }

    LOG_INFO("DataLoader: loaded %zu buildings, %zu units, %zu techs, %zu recipes, %zu goods, %zu leaders, %zu improvements",
             this->buildingDefs.size(), this->unitTypeDefs.size(), this->techDefs.size(),
             this->recipeDefs.size(), this->goodDefs.size(), this->leaderDefs.size(),
             this->improvementDefs.size());

    return allSucceeded;
}

// ============================================================================
// Individual loaders
// ============================================================================

bool DataLoader::loadBuildings(const std::string& path) {
    std::string content = readFileContents(path);
    if (content.empty()) {
        LOG_WARN("DataLoader::loadBuildings: could not read '%s'", path.c_str());
        return false;
    }

    JsonValue root = parseJson(content, path);
    if (!root.isArray()) {
        LOG_ERROR("DataLoader::loadBuildings: '%s' root is not an array", path.c_str());
        return false;
    }

    this->buildingDefs.clear();
    this->buildingDefs.reserve(root.size());

    for (std::size_t i = 0; i < root.size(); ++i) {
        const JsonValue& entry = root[i];
        if (!entry.isObject()) {
            LOG_WARN("DataLoader::loadBuildings: entry %zu is not an object, skipping", i);
            continue;
        }

        RuntimeBuildingDef def{};
        def.id = aoc::BuildingId{entry["id"].asUint16()};
        def.name = entry["name"].asString();
        def.requiredDistrict = parseDistrictType(entry["requiredDistrict"].asString());
        def.productionCost = entry["productionCost"].asInt32();
        def.maintenanceCost = entry["maintenanceCost"].asInt32();
        def.productionBonus = entry["productionBonus"].asInt32();
        def.scienceBonus = entry["scienceBonus"].asInt32();
        def.goldBonus = entry["goldBonus"].asInt32();
        def.scienceMultiplier = entry["scienceMultiplier"].asFloat();
        this->buildingDefs.push_back(std::move(def));
    }

    LOG_INFO("DataLoader: loaded %zu building definitions from JSON", this->buildingDefs.size());
    return true;
}

bool DataLoader::loadUnits(const std::string& path) {
    std::string content = readFileContents(path);
    if (content.empty()) {
        LOG_WARN("DataLoader::loadUnits: could not read '%s'", path.c_str());
        return false;
    }

    JsonValue root = parseJson(content, path);
    if (!root.isArray()) {
        LOG_ERROR("DataLoader::loadUnits: '%s' root is not an array", path.c_str());
        return false;
    }

    this->unitTypeDefs.clear();
    this->unitTypeDefs.reserve(root.size());

    for (std::size_t i = 0; i < root.size(); ++i) {
        const JsonValue& entry = root[i];
        if (!entry.isObject()) {
            LOG_WARN("DataLoader::loadUnits: entry %zu is not an object, skipping", i);
            continue;
        }

        RuntimeUnitTypeDef def{};
        def.id = aoc::UnitTypeId{entry["id"].asUint16()};
        def.name = entry["name"].asString();
        def.unitClass = parseUnitClass(entry["unitClass"].asString());
        def.maxHitPoints = entry["maxHitPoints"].asInt32();
        def.combatStrength = entry["combatStrength"].asInt32();
        def.rangedStrength = entry["rangedStrength"].asInt32();
        def.range = entry["range"].asInt32();
        def.movementPoints = entry["movementPoints"].asInt32();
        def.productionCost = entry["productionCost"].asInt32();
        this->unitTypeDefs.push_back(std::move(def));
    }

    LOG_INFO("DataLoader: loaded %zu unit type definitions from JSON", this->unitTypeDefs.size());
    return true;
}

bool DataLoader::loadTechs(const std::string& path) {
    std::string content = readFileContents(path);
    if (content.empty()) {
        LOG_WARN("DataLoader::loadTechs: could not read '%s'", path.c_str());
        return false;
    }

    JsonValue root = parseJson(content, path);
    if (!root.isArray()) {
        LOG_ERROR("DataLoader::loadTechs: '%s' root is not an array", path.c_str());
        return false;
    }

    this->techDefs.clear();
    this->techDefs.reserve(root.size());

    for (std::size_t i = 0; i < root.size(); ++i) {
        const JsonValue& entry = root[i];
        if (!entry.isObject()) {
            LOG_WARN("DataLoader::loadTechs: entry %zu is not an object, skipping", i);
            continue;
        }

        RuntimeTechDef def{};
        def.id = aoc::TechId{entry["id"].asUint16()};
        def.name = entry["name"].asString();
        def.era = aoc::EraId{entry["era"].asUint16()};
        def.researchCost = entry["researchCost"].asInt32();

        const JsonValue& prereqs = entry["prerequisites"];
        if (prereqs.isArray()) {
            for (std::size_t p = 0; p < prereqs.size(); ++p) {
                def.prerequisites.push_back(aoc::TechId{prereqs[p].asUint16()});
            }
        }

        const JsonValue& unlockedGoods = entry["unlockedGoods"];
        if (unlockedGoods.isArray()) {
            for (std::size_t g = 0; g < unlockedGoods.size(); ++g) {
                def.unlockedGoods.push_back(unlockedGoods[g].asUint16());
            }
        }

        const JsonValue& unlockedBuildings = entry["unlockedBuildings"];
        if (unlockedBuildings.isArray()) {
            for (std::size_t b = 0; b < unlockedBuildings.size(); ++b) {
                def.unlockedBuildings.push_back(aoc::BuildingId{unlockedBuildings[b].asUint16()});
            }
        }

        const JsonValue& unlockedUnits = entry["unlockedUnits"];
        if (unlockedUnits.isArray()) {
            for (std::size_t u = 0; u < unlockedUnits.size(); ++u) {
                def.unlockedUnits.push_back(aoc::UnitTypeId{unlockedUnits[u].asUint16()});
            }
        }

        this->techDefs.push_back(std::move(def));
    }

    LOG_INFO("DataLoader: loaded %zu tech definitions from JSON", this->techDefs.size());
    return true;
}

bool DataLoader::loadRecipes(const std::string& path) {
    std::string content = readFileContents(path);
    if (content.empty()) {
        LOG_WARN("DataLoader::loadRecipes: could not read '%s'", path.c_str());
        return false;
    }

    JsonValue root = parseJson(content, path);
    if (!root.isArray()) {
        LOG_ERROR("DataLoader::loadRecipes: '%s' root is not an array", path.c_str());
        return false;
    }

    this->recipeDefs.clear();
    this->recipeDefs.reserve(root.size());

    for (std::size_t i = 0; i < root.size(); ++i) {
        const JsonValue& entry = root[i];
        if (!entry.isObject()) {
            LOG_WARN("DataLoader::loadRecipes: entry %zu is not an object, skipping", i);
            continue;
        }

        RuntimeProductionRecipe def{};
        def.recipeId = entry["id"].asUint16();
        def.name = entry["name"].asString();
        def.outputGoodId = entry["outputGoodId"].asUint16();
        def.outputAmount = entry["outputAmount"].asInt32();
        def.requiredBuilding = aoc::BuildingId{entry["requiredBuilding"].asUint16()};
        def.turnsToProcess = entry["turnsToProcess"].asInt32();

        const JsonValue& inputs = entry["inputs"];
        if (inputs.isArray()) {
            for (std::size_t j = 0; j < inputs.size(); ++j) {
                const JsonValue& inp = inputs[j];
                RuntimeRecipeInput recipeInput{};
                recipeInput.goodId = inp["goodId"].asUint16();
                recipeInput.amount = inp["amount"].asInt32();
                recipeInput.consumed = inp.hasKey("consumed") ? inp["consumed"].asBool() : true;
                def.inputs.push_back(recipeInput);
            }
        }

        this->recipeDefs.push_back(std::move(def));
    }

    LOG_INFO("DataLoader: loaded %zu recipe definitions from JSON", this->recipeDefs.size());
    return true;
}

bool DataLoader::loadGoods(const std::string& path) {
    std::string content = readFileContents(path);
    if (content.empty()) {
        LOG_WARN("DataLoader::loadGoods: could not read '%s'", path.c_str());
        return false;
    }

    JsonValue root = parseJson(content, path);
    if (!root.isArray()) {
        LOG_ERROR("DataLoader::loadGoods: '%s' root is not an array", path.c_str());
        return false;
    }

    this->goodDefs.clear();
    this->goodDefs.reserve(root.size());

    for (std::size_t i = 0; i < root.size(); ++i) {
        const JsonValue& entry = root[i];
        if (!entry.isObject()) {
            LOG_WARN("DataLoader::loadGoods: entry %zu is not an object, skipping", i);
            continue;
        }

        RuntimeGoodDef def{};
        def.id = entry["id"].asUint16();
        def.name = entry["name"].asString();
        def.category = parseGoodCategory(entry["category"].asString());
        def.basePrice = entry["basePrice"].asInt32();
        def.isStrategic = entry["isStrategic"].asBool();
        def.priceElasticity = entry["priceElasticity"].asFloat();
        this->goodDefs.push_back(std::move(def));
    }

    LOG_INFO("DataLoader: loaded %zu good definitions from JSON", this->goodDefs.size());
    return true;
}

bool DataLoader::loadLeaders(const std::string& path) {
    std::string content = readFileContents(path);
    if (content.empty()) {
        LOG_WARN("DataLoader::loadLeaders: could not read '%s'", path.c_str());
        return false;
    }

    JsonValue root = parseJson(content, path);
    if (!root.isArray()) {
        LOG_ERROR("DataLoader::loadLeaders: '%s' root is not an array", path.c_str());
        return false;
    }

    this->leaderDefs.clear();
    this->leaderDefs.reserve(root.size());

    for (std::size_t i = 0; i < root.size(); ++i) {
        const JsonValue& entry = root[i];
        if (!entry.isObject()) {
            LOG_WARN("DataLoader::loadLeaders: entry %zu is not an object, skipping", i);
            continue;
        }

        RuntimeLeaderPersonalityDef def{};
        def.civId = entry["civId"].asUint8();
        def.agendaName = entry["agendaName"].asString();
        def.agendaDescription = entry["agendaDescription"].asString();
        def.likeCondition = parseAgendaCondition(entry["likeCondition"].asString());
        def.dislikeCondition = parseAgendaCondition(entry["dislikeCondition"].asString());

        const JsonValue& beh = entry["behavior"];
        if (beh.isObject()) {
            aoc::sim::LeaderBehavior& b = def.behavior;
            b.militaryAggression   = beh["militaryAggression"].asFloat();
            b.expansionism         = beh["expansionism"].asFloat();
            b.scienceFocus         = beh["scienceFocus"].asFloat();
            b.cultureFocus         = beh["cultureFocus"].asFloat();
            b.economicFocus        = beh["economicFocus"].asFloat();
            b.diplomaticOpenness   = beh["diplomaticOpenness"].asFloat();
            b.religiousZeal        = beh["religiousZeal"].asFloat();
            b.nukeWillingness      = beh["nukeWillingness"].asFloat();
            b.trustworthiness      = beh["trustworthiness"].asFloat();
            b.grudgeHolding        = beh["grudgeHolding"].asFloat();
            b.techMilitary         = beh["techMilitary"].asFloat();
            b.techEconomic         = beh["techEconomic"].asFloat();
            b.techIndustrial       = beh["techIndustrial"].asFloat();
            b.techNaval            = beh["techNaval"].asFloat();
            b.techInformation      = beh["techInformation"].asFloat();
            b.prodSettlers         = beh["prodSettlers"].asFloat();
            b.prodMilitary         = beh["prodMilitary"].asFloat();
            b.prodBuilders         = beh["prodBuilders"].asFloat();
            b.prodBuildings        = beh["prodBuildings"].asFloat();
            b.prodWonders          = beh["prodWonders"].asFloat();
            b.prodNaval            = beh["prodNaval"].asFloat();
            b.prodReligious        = beh["prodReligious"].asFloat();
            b.warDeclarationThreshold  = beh["warDeclarationThreshold"].asFloat();
            b.peaceAcceptanceThreshold = beh["peaceAcceptanceThreshold"].asFloat();
            b.allianceDesire       = beh["allianceDesire"].asFloat();
        }

        this->leaderDefs.push_back(std::move(def));
    }

    LOG_INFO("DataLoader: loaded %zu leader personality definitions from JSON", this->leaderDefs.size());
    return true;
}

// ============================================================================
// Fallback loaders (populate from constexpr arrays)
// ============================================================================

void DataLoader::fallbackBuildings() {
    this->buildingDefs.clear();
    this->buildingDefs.reserve(aoc::sim::BUILDING_DEFS.size());
    for (const aoc::sim::BuildingDef& src : aoc::sim::BUILDING_DEFS) {
        RuntimeBuildingDef def{};
        def.id = src.id;
        def.name = std::string(src.name);
        def.requiredDistrict = src.requiredDistrict;
        def.productionCost = src.productionCost;
        def.maintenanceCost = src.maintenanceCost;
        def.productionBonus = src.productionBonus;
        def.scienceBonus = src.scienceBonus;
        def.goldBonus = src.goldBonus;
        def.scienceMultiplier = src.scienceMultiplier;
        this->buildingDefs.push_back(std::move(def));
    }
}

void DataLoader::fallbackUnits() {
    this->unitTypeDefs.clear();
    this->unitTypeDefs.reserve(aoc::sim::UNIT_TYPE_DEFS.size());
    for (const aoc::sim::UnitTypeDef& src : aoc::sim::UNIT_TYPE_DEFS) {
        RuntimeUnitTypeDef def{};
        def.id = src.id;
        def.name = std::string(src.name);
        def.unitClass = src.unitClass;
        def.maxHitPoints = src.maxHitPoints;
        def.combatStrength = src.combatStrength;
        def.rangedStrength = src.rangedStrength;
        def.range = src.range;
        def.movementPoints = src.movementPoints;
        def.productionCost = src.productionCost;
        this->unitTypeDefs.push_back(std::move(def));
    }
}

void DataLoader::fallbackTechs() {
    this->techDefs.clear();
    const std::vector<aoc::sim::TechDef>& srcTechs = aoc::sim::allTechs();
    this->techDefs.reserve(srcTechs.size());
    for (const aoc::sim::TechDef& src : srcTechs) {
        RuntimeTechDef def{};
        def.id = src.id;
        def.name = std::string(src.name);
        def.era = src.era;
        def.researchCost = src.researchCost;
        def.prerequisites = src.prerequisites;
        def.unlockedGoods = src.unlockedGoods;
        def.unlockedBuildings = src.unlockedBuildings;
        def.unlockedUnits = src.unlockedUnits;
        this->techDefs.push_back(std::move(def));
    }
}

void DataLoader::fallbackGoods() {
    this->goodDefs.clear();
    // Goods are sparse (IDs are not contiguous), iterate all defined IDs
    for (uint16_t i = 0; i < aoc::sim::goods::GOOD_COUNT; ++i) {
        const aoc::sim::GoodDef& src = aoc::sim::goodDef(i);
        if (src.name == std::string_view("Unknown")) {
            continue;  // Skip placeholder entries
        }
        RuntimeGoodDef def{};
        def.id = src.id;
        def.name = std::string(src.name);
        def.category = src.category;
        def.basePrice = src.basePrice;
        def.isStrategic = src.isStrategic;
        def.priceElasticity = src.priceElasticity;
        this->goodDefs.push_back(std::move(def));
    }
}

void DataLoader::fallbackRecipes() {
    this->recipeDefs.clear();
    const std::vector<aoc::sim::ProductionRecipe>& srcRecipes = aoc::sim::allRecipes();
    this->recipeDefs.reserve(srcRecipes.size());
    for (const aoc::sim::ProductionRecipe& src : srcRecipes) {
        RuntimeProductionRecipe def{};
        def.recipeId = src.recipeId;
        def.name = std::string(src.name);
        def.outputGoodId = src.outputGoodId;
        def.outputAmount = src.outputAmount;
        def.requiredBuilding = src.requiredBuilding;
        def.turnsToProcess = src.turnsToProcess;
        for (const aoc::sim::RecipeInput& inp : src.inputs) {
            RuntimeRecipeInput recipeInput{};
            recipeInput.goodId = inp.goodId;
            recipeInput.amount = inp.amount;
            recipeInput.consumed = inp.consumed;
            def.inputs.push_back(recipeInput);
        }
        this->recipeDefs.push_back(std::move(def));
    }
}

void DataLoader::fallbackLeaders() {
    this->leaderDefs.clear();
    this->leaderDefs.reserve(static_cast<std::size_t>(aoc::sim::LEADER_PERSONALITY_COUNT));
    for (int32_t i = 0; i < aoc::sim::LEADER_PERSONALITY_COUNT; ++i) {
        const aoc::sim::LeaderPersonalityDef& src = aoc::sim::LEADER_PERSONALITIES[i];
        RuntimeLeaderPersonalityDef def{};
        def.civId = src.civId;
        def.agendaName = std::string(src.agendaName);
        def.agendaDescription = std::string(src.agendaDescription);
        def.likeCondition = src.likeCondition;
        def.dislikeCondition = src.dislikeCondition;
        def.behavior = src.behavior;
        this->leaderDefs.push_back(std::move(def));
    }
}

// ============================================================================
// Improvements (WP-C7)
// ============================================================================

bool DataLoader::loadImprovements(const std::string& path) {
    std::string content = readFileContents(path);
    if (content.empty()) {
        LOG_WARN("DataLoader::loadImprovements: could not read '%s'", path.c_str());
        return false;
    }

    JsonValue root = parseJson(content, path);
    if (!root.isArray()) {
        LOG_ERROR("DataLoader::loadImprovements: '%s' root is not an array", path.c_str());
        return false;
    }

    this->improvementDefs.clear();
    this->improvementDefs.reserve(root.size());

    for (std::size_t i = 0; i < root.size(); ++i) {
        const JsonValue& entry = root[i];
        if (!entry.isObject()) {
            LOG_WARN("DataLoader::loadImprovements: entry %zu is not an object, skipping", i);
            continue;
        }
        RuntimeImprovementDef def{};
        def.type = parseImprovementType(entry["type"].asString());
        def.name = entry["name"].asString();
        def.yieldBonus.food       = static_cast<int8_t>(entry["food"].asInt32());
        def.yieldBonus.production = static_cast<int8_t>(entry["production"].asInt32());
        def.yieldBonus.gold       = static_cast<int8_t>(entry["gold"].asInt32());
        def.yieldBonus.science    = static_cast<int8_t>(entry["science"].asInt32());
        def.yieldBonus.culture    = static_cast<int8_t>(entry["culture"].asInt32());
        def.yieldBonus.faith      = static_cast<int8_t>(entry["faith"].asInt32());
        def.requiredTech = aoc::TechId{entry["requiredTech"].asUint16()};
        def.buildTurns = entry["buildTurns"].asInt32();
        this->improvementDefs.push_back(std::move(def));
    }

    LOG_INFO("DataLoader: loaded %zu improvement definitions from JSON",
             this->improvementDefs.size());
    return true;
}

void DataLoader::fallbackImprovements() {
    this->improvementDefs.clear();
    this->improvementDefs.reserve(aoc::sim::IMPROVEMENT_DEFS.size());
    for (const aoc::sim::ImprovementDef& src : aoc::sim::IMPROVEMENT_DEFS) {
        RuntimeImprovementDef def{};
        def.type = src.type;
        def.name = std::string(src.name);
        def.yieldBonus = src.yieldBonus;
        def.requiredTech = src.requiredTech;
        def.buildTurns = src.buildTurns;
        this->improvementDefs.push_back(std::move(def));
    }
}

} // namespace aoc::data
