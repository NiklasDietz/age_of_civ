/**
 * @file Encyclopedia.cpp
 * @brief In-game encyclopedia: content generation from game data + UI screen.
 */

#include "aoc/ui/Encyclopedia.hpp"
#include "aoc/ui/UIManager.hpp"

// Game data includes for content generation
#include "aoc/simulation/unit/UnitTypes.hpp"
#include "aoc/simulation/city/District.hpp"
#include "aoc/simulation/map/Improvement.hpp"
#include "aoc/simulation/resource/ResourceTypes.hpp"
#include "aoc/simulation/tech/TechTree.hpp"
#include "aoc/simulation/government/Government.hpp"
#include "aoc/simulation/wonder/Wonder.hpp"
#include "aoc/simulation/civilization/Civilization.hpp"
#include "aoc/simulation/monetary/MonetarySystem.hpp"
#include "aoc/simulation/monetary/CurrencyCrisis.hpp"
#include "aoc/simulation/monetary/Bonds.hpp"
#include "aoc/simulation/monetary/CurrencyWar.hpp"
#include "aoc/simulation/economy/IndustrialRevolution.hpp"
#include "aoc/simulation/economy/NavalTrade.hpp"
#include "aoc/simulation/empire/CommunicationSpeed.hpp"
#include "aoc/simulation/production/PowerGrid.hpp"
#include "aoc/simulation/production/QualityTier.hpp"
#include "aoc/simulation/production/Waste.hpp"
#include "aoc/simulation/production/BuildingCapacity.hpp"
#include "aoc/simulation/production/Automation.hpp"
#include "aoc/map/Terrain.hpp"

#include <algorithm>
#include <string>

namespace aoc::ui {

// ============================================================================
// Category names
// ============================================================================

const char* wikiCategoryName(WikiCategory cat) {
    switch (cat) {
        case WikiCategory::Units:         return "Units";
        case WikiCategory::Buildings:     return "Buildings";
        case WikiCategory::Improvements:  return "Improvements";
        case WikiCategory::Resources:     return "Resources & Goods";
        case WikiCategory::Goods:         return "Production Recipes";
        case WikiCategory::Recipes:       return "Recipes";
        case WikiCategory::Technologies:  return "Technologies";
        case WikiCategory::Governments:   return "Governments";
        case WikiCategory::Policies:      return "Policy Cards";
        case WikiCategory::Wonders:       return "Wonders";
        case WikiCategory::Civilizations: return "Civilizations";
        case WikiCategory::Mechanics:     return "Game Mechanics";
        default:                          return "Unknown";
    }
}

// ============================================================================
// Helper: string building
// ============================================================================

static std::string yieldStr(const aoc::map::TileYield& y) {
    std::string s;
    if (y.food > 0)       { s += "Food:" + std::to_string(y.food) + " "; }
    if (y.production > 0) { s += "Prod:" + std::to_string(y.production) + " "; }
    if (y.gold > 0)       { s += "Gold:" + std::to_string(y.gold) + " "; }
    if (y.science > 0)    { s += "Sci:" + std::to_string(y.science) + " "; }
    if (y.culture > 0)    { s += "Cul:" + std::to_string(y.culture) + " "; }
    if (y.faith > 0)      { s += "Faith:" + std::to_string(y.faith) + " "; }
    if (s.empty()) { s = "None"; }
    return s;
}

static std::string unitClassName(aoc::sim::UnitClass c) {
    switch (c) {
        case aoc::sim::UnitClass::Melee:    return "Melee";
        case aoc::sim::UnitClass::Ranged:   return "Ranged";
        case aoc::sim::UnitClass::Cavalry:  return "Cavalry";
        case aoc::sim::UnitClass::Scout:    return "Scout";
        case aoc::sim::UnitClass::Settler:  return "Settler";
        case aoc::sim::UnitClass::Civilian: return "Civilian";
        case aoc::sim::UnitClass::Naval:    return "Naval";
        case aoc::sim::UnitClass::Religious:return "Religious";
        default:                            return "Unknown";
    }
}

// ============================================================================
// Content generation
// ============================================================================

static void buildUnitEntries(std::vector<WikiEntry>& entries) {
    for (const aoc::sim::UnitTypeDef& u : aoc::sim::UNIT_TYPE_DEFS) {
        WikiEntry entry;
        entry.category = WikiCategory::Units;
        entry.title = std::string(u.name);
        entry.statsBlock = "Class: " + unitClassName(u.unitClass)
            + "\nHP: " + std::to_string(u.maxHitPoints)
            + "\nCombat Strength: " + std::to_string(u.combatStrength)
            + (u.rangedStrength > 0 ? "\nRanged Strength: " + std::to_string(u.rangedStrength)
                                      + "\nRange: " + std::to_string(u.range) : "")
            + "\nMovement: " + std::to_string(u.movementPoints)
            + "\nProduction Cost: " + std::to_string(u.productionCost);

        if (aoc::sim::isNaval(u.unitClass)) {
            entry.body = "Naval unit. Can traverse coast and ocean tiles.";
            // Check if it's a merchant ship
            for (const aoc::sim::MerchantShipDef& ms : aoc::sim::MERCHANT_SHIP_DEFS) {
                if (ms.unitTypeId == u.id) {
                    entry.body += "\nMERCHANT VESSEL: Cargo capacity " + std::to_string(ms.cargoCapacity);
                    if (ms.canTraverseOcean) { entry.body += ", ocean-capable"; }
                    if (ms.canTraverseRiver) { entry.body += ", river-capable"; }
                    if (ms.fuelGoodId != 0xFFFF) {
                        entry.body += "\nFuel: " + std::to_string(ms.fuelPerTurn) + "/turn";
                    }
                    break;
                }
            }
        } else if (aoc::sim::isMilitary(u.unitClass)) {
            entry.body = "Military unit. Can attack enemy units and cities.";
        } else {
            entry.body = "Civilian unit. Cannot attack.";
        }

        entries.push_back(std::move(entry));
    }
}

static void buildBuildingEntries(std::vector<WikiEntry>& entries) {
    for (const aoc::sim::BuildingDef& b : aoc::sim::BUILDING_DEFS) {
        WikiEntry entry;
        entry.category = WikiCategory::Buildings;
        entry.title = std::string(b.name);
        entry.statsBlock = "District: " + std::string(aoc::sim::districtTypeName(b.requiredDistrict))
            + "\nProduction Cost: " + std::to_string(b.productionCost)
            + "\nMaintenance: " + std::to_string(b.maintenanceCost) + " gold/turn";

        std::string bonuses;
        if (b.productionBonus > 0) { bonuses += "+Prod:" + std::to_string(b.productionBonus) + " "; }
        if (b.scienceBonus > 0)    { bonuses += "+Sci:" + std::to_string(b.scienceBonus) + " "; }
        if (b.goldBonus > 0)       { bonuses += "+Gold:" + std::to_string(b.goldBonus) + " "; }
        if (b.scienceMultiplier > 1.01f) {
            int pct = static_cast<int>((b.scienceMultiplier - 1.0f) * 100.0f);
            bonuses += "+" + std::to_string(pct) + "% Science ";
        }
        entry.statsBlock += "\nBonuses: " + (bonuses.empty() ? "None" : bonuses);

        // Building capacity info
        aoc::sim::BuildingTierClass tier = aoc::sim::buildingTierClass(b.id);
        entry.body = "Throughput capacity: Lv1="
            + std::to_string(aoc::sim::CAPACITY_TABLE[static_cast<uint8_t>(tier)][0])
            + " Lv2=" + std::to_string(aoc::sim::CAPACITY_TABLE[static_cast<uint8_t>(tier)][1])
            + " Lv3=" + std::to_string(aoc::sim::CAPACITY_TABLE[static_cast<uint8_t>(tier)][2])
            + " batches/turn";

        // Energy demand
        int32_t energy = aoc::sim::buildingEnergyDemand(b.id);
        if (energy > 0) {
            entry.body += "\nEnergy demand: " + std::to_string(energy) + "/turn";
        }

        // Waste output
        aoc::sim::WasteOutput waste = aoc::sim::buildingWasteOutput(b.id);
        if (waste.amount > 0) {
            entry.body += "\nWaste: " + std::to_string(waste.amount) + "/batch";
        }

        // Power plant info
        for (const aoc::sim::PowerPlantDef& pp : aoc::sim::POWER_PLANT_DEFS) {
            if (pp.buildingId == b.id) {
                entry.body += "\nPOWER PLANT: " + std::to_string(pp.energyOutput) + " energy/turn";
                if (pp.fuelGoodId != 0xFFFF) {
                    entry.body += ", consumes " + std::to_string(pp.fuelPerTurn) + " fuel/turn";
                }
                if (pp.emissions > 0) {
                    entry.body += ", " + std::to_string(pp.emissions) + " emissions";
                }
                if (pp.requiresRiver) { entry.body += ", requires river"; }
                if (pp.meltdownRisk > 0.0f) { entry.body += ", MELTDOWN RISK 0.2%/turn"; }
                break;
            }
        }

        entries.push_back(std::move(entry));
    }
}

static void buildImprovementEntries(std::vector<WikiEntry>& entries) {
    for (const aoc::sim::ImprovementDef& imp : aoc::sim::IMPROVEMENT_DEFS) {
        if (imp.type == aoc::map::ImprovementType::None) { continue; }
        WikiEntry entry;
        entry.category = WikiCategory::Improvements;
        entry.title = std::string(imp.name);
        entry.statsBlock = "Yield Bonus: " + yieldStr(imp.yieldBonus)
            + "\nBuild Time: " + std::to_string(imp.buildTurns) + " turns";
        if (imp.requiredTech.isValid()) {
            entry.statsBlock += "\nRequires Tech: #" + std::to_string(imp.requiredTech.value);
        }

        // Infrastructure tier info
        if (imp.type == aoc::map::ImprovementType::Road) {
            entry.body = "Basic road. Reduces movement cost. Trade capacity 2x.";
        } else if (imp.type == aoc::map::ImprovementType::Railway) {
            entry.body = "Industrial-era infrastructure. 5x trade capacity, +1 production."
                         "\nMaintenance: 1 gold/tile/turn. Coal fuel: 1 per 10 tiles."
                         "\nUnlocked by 1st Industrial Revolution.";
        } else if (imp.type == aoc::map::ImprovementType::Highway) {
            entry.body = "Modern infrastructure. 8x trade capacity, +1 gold."
                         "\nMaintenance: 2 gold/tile/turn."
                         "\nUnlocked by 3rd Industrial Revolution.";
        } else if (imp.type == aoc::map::ImprovementType::Dam) {
            entry.body = "River-only. Prevents seasonal flooding on downstream floodplains."
                         "\nEnables Hydroelectric Dam power plant.";
        } else {
            entry.body = "Tile improvement built by Builder units.";
        }

        entries.push_back(std::move(entry));
    }
}

static void buildGoodEntries(std::vector<WikiEntry>& entries) {
    uint16_t count = aoc::sim::goodCount();
    for (uint16_t i = 0; i < count; ++i) {
        const aoc::sim::GoodDef& g = aoc::sim::goodDef(i);
        if (g.name == "Unknown") { continue; }
        WikiEntry entry;
        entry.category = WikiCategory::Resources;
        entry.title = std::string(g.name);

        const char* catName = "Unknown";
        switch (g.category) {
            case aoc::sim::GoodCategory::RawStrategic: catName = "Raw Strategic"; break;
            case aoc::sim::GoodCategory::RawLuxury:    catName = "Raw Luxury"; break;
            case aoc::sim::GoodCategory::RawBonus:     catName = "Raw Bonus"; break;
            case aoc::sim::GoodCategory::Processed:    catName = "Processed"; break;
            case aoc::sim::GoodCategory::Advanced:     catName = "Advanced"; break;
            case aoc::sim::GoodCategory::Monetary:     catName = "Monetary"; break;
            default: break;
        }

        const char* bandName = "Any";
        switch (g.climateBand) {
            case aoc::sim::ClimateBand::Tropical:    bandName = "Tropical"; break;
            case aoc::sim::ClimateBand::Subtropical: bandName = "Subtropical"; break;
            case aoc::sim::ClimateBand::Temperate:   bandName = "Temperate"; break;
            case aoc::sim::ClimateBand::Cold:        bandName = "Cold"; break;
            default: break;
        }

        entry.statsBlock = std::string("Category: ") + catName
            + "\nBase Price: " + std::to_string(g.basePrice)
            + "\nStrategic: " + (g.isStrategic ? "Yes" : "No")
            + "\nPrice Volatility: " + std::to_string(static_cast<int>(g.priceElasticity * 100.0f)) + "%"
            + "\nClimate Band: " + bandName;

        entry.body = "";
        // Find recipes that produce this good
        for (const aoc::sim::ProductionRecipe& r : aoc::sim::allRecipes()) {
            if (r.outputGoodId == i) {
                entry.body += "Produced by: " + std::string(r.name) + "\n";
            }
        }
        // Find recipes that consume this good
        for (const aoc::sim::ProductionRecipe& r : aoc::sim::allRecipes()) {
            for (const aoc::sim::RecipeInput& input : r.inputs) {
                if (input.goodId == i) {
                    entry.body += "Used in: " + std::string(r.name)
                        + " (" + std::to_string(input.amount) + "x"
                        + (input.consumed ? "" : ", not consumed") + ")\n";
                    break;
                }
            }
        }
        if (entry.body.empty()) {
            entry.body = "Harvested from map tiles.";
        }

        entries.push_back(std::move(entry));
    }
}

static void buildRecipeEntries(std::vector<WikiEntry>& entries) {
    for (const aoc::sim::ProductionRecipe& r : aoc::sim::allRecipes()) {
        WikiEntry entry;
        entry.category = WikiCategory::Recipes;
        entry.title = std::string(r.name);

        std::string inputs;
        for (const aoc::sim::RecipeInput& in : r.inputs) {
            inputs += std::string(aoc::sim::goodDef(in.goodId).name)
                + " x" + std::to_string(in.amount)
                + (in.consumed ? "" : " (catalyst)") + ", ";
        }
        if (!inputs.empty()) { inputs.resize(inputs.size() - 2); }

        entry.statsBlock = "Inputs: " + inputs
            + "\nOutput: " + std::string(aoc::sim::goodDef(r.outputGoodId).name)
                + " x" + std::to_string(r.outputAmount)
            + "\nBuilding: " + std::string(aoc::sim::buildingDef(r.requiredBuilding).name)
            + "\nProcess Time: " + std::to_string(r.turnsToProcess) + " turn(s)";

        entry.body = "Production recipe. City must have the required building and"
                     " sufficient input goods. Output is modified by infrastructure,"
                     " environment, experience, power efficiency, and industrial revolution bonuses.";

        entries.push_back(std::move(entry));
    }
}

static void buildTechEntries(std::vector<WikiEntry>& entries) {
    const std::vector<aoc::sim::TechDef>& techs = aoc::sim::allTechs();
    for (const aoc::sim::TechDef& t : techs) {
        WikiEntry entry;
        entry.category = WikiCategory::Technologies;
        entry.title = std::string(t.name);
        entry.statsBlock = "Era: " + std::to_string(t.era.value)
            + "\nResearch Cost: " + std::to_string(t.researchCost) + " science";

        std::string prereqs;
        for (aoc::TechId pre : t.prerequisites) {
            if (pre.isValid()) {
                prereqs += std::string(aoc::sim::techDef(pre).name) + ", ";
            }
        }
        if (!prereqs.empty()) { prereqs.resize(prereqs.size() - 2); }
        entry.statsBlock += "\nPrerequisites: " + (prereqs.empty() ? "None" : prereqs);

        entry.body = "Unlocks:";
        for (aoc::BuildingId bid : t.unlockedBuildings) {
            entry.body += "\n  Building: " + std::string(aoc::sim::buildingDef(bid).name);
        }
        for (aoc::UnitTypeId uid : t.unlockedUnits) {
            entry.body += "\n  Unit: " + std::string(aoc::sim::unitTypeDef(uid).name);
        }
        if (entry.body == "Unlocks:") { entry.body = "No direct unlocks listed."; }

        entries.push_back(std::move(entry));
    }
}

static void buildGovernmentEntries(std::vector<WikiEntry>& entries) {
    for (const aoc::sim::GovernmentDef& g : aoc::sim::GOVERNMENT_DEFS) {
        WikiEntry entry;
        entry.category = WikiCategory::Governments;
        entry.title = std::string(g.name);

        entry.statsBlock = "Slots: M:" + std::to_string(g.militarySlots)
            + " E:" + std::to_string(g.economicSlots)
            + " D:" + std::to_string(g.diplomaticSlots)
            + " W:" + std::to_string(g.wildcardSlots)
            + "\nCorruption Rate: " + std::to_string(static_cast<int>(g.corruptionRate * 100.0f)) + "% per excess city";

        entry.body = "Inherent Bonuses:";
        if (g.inherentBonuses.productionMultiplier > 1.01f) {
            entry.body += "\n  +" + std::to_string(static_cast<int>((g.inherentBonuses.productionMultiplier - 1.0f) * 100.0f)) + "% Production";
        }
        if (g.inherentBonuses.goldMultiplier > 1.01f) {
            entry.body += "\n  +" + std::to_string(static_cast<int>((g.inherentBonuses.goldMultiplier - 1.0f) * 100.0f)) + "% Gold";
        }
        if (g.inherentBonuses.scienceMultiplier > 1.01f) {
            entry.body += "\n  +" + std::to_string(static_cast<int>((g.inherentBonuses.scienceMultiplier - 1.0f) * 100.0f)) + "% Science";
        }
        if (g.inherentBonuses.cultureMultiplier > 1.01f) {
            entry.body += "\n  +" + std::to_string(static_cast<int>((g.inherentBonuses.cultureMultiplier - 1.0f) * 100.0f)) + "% Culture";
        }
        if (g.inherentBonuses.combatStrengthBonus > 0.5f) {
            entry.body += "\n  +" + std::to_string(static_cast<int>(g.inherentBonuses.combatStrengthBonus)) + " Combat Strength";
        }

        aoc::sim::GovernmentAction action = aoc::sim::governmentUniqueAction(g.type);
        if (action != aoc::sim::GovernmentAction::None) {
            const char* actionName = "Unknown";
            const char* actionDesc = "";
            switch (action) {
                case aoc::sim::GovernmentAction::Referendum:
                    actionName = "Referendum"; actionDesc = "+20 loyalty all cities for 5 turns"; break;
                case aoc::sim::GovernmentAction::FiveYearPlan:
                    actionName = "Five Year Plan"; actionDesc = "+30% production for 10 turns, -2 amenities"; break;
                case aoc::sim::GovernmentAction::Mobilization:
                    actionName = "Mobilization"; actionDesc = "Instantly conscript 3 military units at capital"; break;
                case aoc::sim::GovernmentAction::RoyalDecree:
                    actionName = "Royal Decree"; actionDesc = "+15% gold for 10 turns"; break;
                case aoc::sim::GovernmentAction::HolyWar:
                    actionName = "Holy War"; actionDesc = "+4 combat, +20% faith for 10 turns"; break;
                case aoc::sim::GovernmentAction::TradeFleet:
                    actionName = "Trade Fleet"; actionDesc = "+3 trade routes for 10 turns"; break;
                default: break;
            }
            entry.body += std::string("\n\nUnique Action: ") + actionName + "\n  " + actionDesc;
        }

        entry.body += "\n\nChanging to this government causes " + std::to_string(aoc::sim::ANARCHY_DURATION) + " turns of anarchy.";

        entries.push_back(std::move(entry));
    }
}

static void buildPolicyEntries(std::vector<WikiEntry>& entries) {
    for (const aoc::sim::PolicyCardDef& p : aoc::sim::POLICY_CARD_DEFS) {
        WikiEntry entry;
        entry.category = WikiCategory::Policies;
        entry.title = std::string(p.name);

        const char* slotName = "Unknown";
        switch (p.slotType) {
            case aoc::sim::PolicySlotType::Military:   slotName = "Military"; break;
            case aoc::sim::PolicySlotType::Economic:   slotName = "Economic"; break;
            case aoc::sim::PolicySlotType::Diplomatic: slotName = "Diplomatic"; break;
            case aoc::sim::PolicySlotType::Wildcard:   slotName = "Wildcard"; break;
        }
        entry.statsBlock = std::string("Slot Type: ") + slotName;

        entry.body = "Effects:";
        const aoc::sim::GovernmentModifiers& m = p.modifiers;
        if (m.productionMultiplier > 1.01f) { entry.body += "\n  +" + std::to_string(static_cast<int>((m.productionMultiplier - 1.0f) * 100.0f)) + "% Production"; }
        if (m.goldMultiplier > 1.01f)       { entry.body += "\n  +" + std::to_string(static_cast<int>((m.goldMultiplier - 1.0f) * 100.0f)) + "% Gold"; }
        if (m.scienceMultiplier > 1.01f)    { entry.body += "\n  +" + std::to_string(static_cast<int>((m.scienceMultiplier - 1.0f) * 100.0f)) + "% Science"; }
        if (m.cultureMultiplier > 1.01f)    { entry.body += "\n  +" + std::to_string(static_cast<int>((m.cultureMultiplier - 1.0f) * 100.0f)) + "% Culture"; }
        if (m.combatStrengthBonus > 0.5f)   { entry.body += "\n  +" + std::to_string(static_cast<int>(m.combatStrengthBonus)) + " Combat Strength"; }
        if (m.tradeRouteBonus > 0.01f)      { entry.body += "\n  +" + std::to_string(static_cast<int>(m.tradeRouteBonus)) + " Gold per trade route"; }
        if (m.unitMaintenanceReduction > 0.01f) { entry.body += "\n  -" + std::to_string(static_cast<int>(m.unitMaintenanceReduction)) + " Unit maintenance"; }
        if (m.productionPerCity > 0.01f)    { entry.body += "\n  +" + std::to_string(static_cast<int>(m.productionPerCity)) + " Production per city"; }
        if (m.extraTradeRoutes > 0)         { entry.body += "\n  +" + std::to_string(m.extraTradeRoutes) + " Trade routes"; }
        if (m.loyaltyBonus > 0.5f)          { entry.body += "\n  +" + std::to_string(static_cast<int>(m.loyaltyBonus)) + " Loyalty"; }
        if (m.warWearinessReduction < -0.01f) { entry.body += "\n  " + std::to_string(static_cast<int>(m.warWearinessReduction * 100.0f)) + "% War Weariness"; }
        if (m.faithMultiplier > 1.01f)      { entry.body += "\n  +" + std::to_string(static_cast<int>((m.faithMultiplier - 1.0f) * 100.0f)) + "% Faith"; }
        if (m.corruptionReduction > 0.001f) { entry.body += "\n  -" + std::to_string(static_cast<int>(m.corruptionReduction * 100.0f)) + "% Corruption"; }
        if (m.espionageDefense > 0.01f)     { entry.body += "\n  +" + std::to_string(static_cast<int>(m.espionageDefense * 100.0f)) + "% Espionage Defense"; }
        if (m.tariffEfficiency > 0.01f)     { entry.body += "\n  +" + std::to_string(static_cast<int>(m.tariffEfficiency * 100.0f)) + "% Tariff Efficiency"; }
        if (m.growthMultiplier > 1.01f)     { entry.body += "\n  +" + std::to_string(static_cast<int>((m.growthMultiplier - 1.0f) * 100.0f)) + "% Population Growth"; }

        entries.push_back(std::move(entry));
    }
}

static void buildWonderEntries(std::vector<WikiEntry>& entries) {
    for (uint8_t i = 0; i < aoc::sim::WONDER_COUNT; ++i) {
        const aoc::sim::WonderDef& w = aoc::sim::wonderDef(aoc::sim::WonderId{i});
        WikiEntry entry;
        entry.category = WikiCategory::Wonders;
        entry.title = std::string(w.name);
        entry.statsBlock = "Era: " + std::to_string(w.era.value)
            + "\nProduction Cost: " + std::to_string(w.productionCost);
        if (w.prerequisiteTech.isValid()) {
            entry.statsBlock += "\nRequires: " + std::string(aoc::sim::techDef(w.prerequisiteTech).name);
        }

        entry.body = "World Wonder (only one per game).\nEffects:";
        if (w.effect.productionMultiplier > 1.01f) { int pct = static_cast<int>((w.effect.productionMultiplier - 1.0f) * 100.0f); entry.body += "\n  +" + std::to_string(pct) + "% Production"; }
        if (w.effect.scienceBonus > 0.01f)    { entry.body += "\n  +" + std::to_string(static_cast<int>(w.effect.scienceBonus)) + " Science"; }
        if (w.effect.cultureBonus > 0.01f)    { entry.body += "\n  +" + std::to_string(static_cast<int>(w.effect.cultureBonus)) + " Culture"; }
        if (w.effect.goldBonus > 0.01f)       { entry.body += "\n  +" + std::to_string(static_cast<int>(w.effect.goldBonus)) + " Gold"; }
        if (w.effect.amenityBonus > 0.01f)    { entry.body += "\n  +" + std::to_string(static_cast<int>(w.effect.amenityBonus)) + " Amenities"; }
        if (w.effect.faithBonus > 0.01f)      { entry.body += "\n  +" + std::to_string(static_cast<int>(w.effect.faithBonus)) + " Faith"; }

        entries.push_back(std::move(entry));
    }
}

static void buildCivEntries(std::vector<WikiEntry>& entries) {
    for (uint8_t i = 0; i < aoc::sim::CIV_COUNT; ++i) {
        const aoc::sim::CivilizationDef& c = aoc::sim::civDef(aoc::sim::CivId{i});
        WikiEntry entry;
        entry.category = WikiCategory::Civilizations;
        entry.title = std::string(c.name) + " (" + std::string(c.leaderName) + ")";
        entry.statsBlock = "Unique Ability: " + std::string(c.abilityName);

        entry.body = std::string(c.abilityDescription) + "\n\nCities:";
        for (std::size_t cn = 0; cn < aoc::sim::MAX_CIV_CITY_NAMES; ++cn) {
            if (c.cityNames[cn].empty()) { break; }
            entry.body += "\n  " + std::string(c.cityNames[cn]);
        }

        entries.push_back(std::move(entry));
    }
}

static void buildMechanicsEntries(std::vector<WikiEntry>& entries) {
    // WP-C3 Power Grid
    entries.push_back({WikiCategory::Mechanics, "Power Grid",
        "Post-Electricity (tech 14) your builders can lay Power Poles on owned\n"
        "land tiles. Poles stack with existing improvements (farm, mine, etc.).\n\n"
        "POWERED CITY: reachable from a power-plant city via a BFS chain of\n"
        "  same-owner Power Poles (own-city centers count as hub nodes).\n"
        "  Powered cities get +25% production.\n\n"
        "UNPOWERED CITY WITH ADJACENT POLES: +5% production per adjacent pole,\n"
        "  capped at +15%. Readiness bonus until a plant-host connects.\n\n"
        "Power plants: Coal (26), Oil (27), Hydro Dam (28), Nuclear (29),\n"
        "Solar (30), Wind (31), Gas (32), Biofuel (33), Geothermal (34), Fusion (35).\n\n"
        "Fallout wipes poles from affected tiles. Poles are serialized in the\n"
        "Improvements save section.",
        ""});

    // WP-C3 Pipelines
    entries.push_back({WikiCategory::Mechanics, "Pipelines",
        "Post-Mass-Production (tech 15) your builders can lay Pipelines on\n"
        "owned OIL / NATURAL_GAS tiles (or tiles adjacent to existing\n"
        "pipelines). Pipelines stack with existing improvements.\n\n"
        "EFFECT: land traders crossing a pipeline tile double their per-turn\n"
        "movement distance. Bulk oil / gas / fuel logistics run faster.",
        ""});

    // WP-C3 Transit Treaty
    entries.push_back({WikiCategory::Mechanics, "Transit Treaty",
        "Two-player trade agreement granting zero-toll right-of-passage across\n"
        "each other's territory. No tariff relief on goods — purely transit.\n\n"
        "Traders of either member cross the other's tiles without paying the\n"
        "owner's territory toll. Use for land trade routes that would\n"
        "otherwise be blocked by hostile tariff walls.\n\n"
        "Forms via `proposeTransitTreaty`. Duplicate treaties between the\n"
        "same pair are rejected.",
        ""});

    // WP-C4 Greenhouse
    entries.push_back({WikiCategory::Mechanics, "Greenhouse Planting",
        "Greenhouse improvement (tech Advanced Chemistry, 24) lets you grow\n"
        "off-climate crops at 50% rate.\n\n"
        "WORKFLOW:\n"
        "  1. Obtain crop seeds via trade (or local production).\n"
        "  2. Build Greenhouse on any land tile you own.\n"
        "  3. Stand a Builder on the Greenhouse tile and click 'Plant Crop'.\n"
        "     Cycles through climate-banded goods your empire has stockpiled.\n"
        "     Consumes 1 seed; sets tile to produce that crop.\n"
        "  4. Each turn (at 50% rate: 1 per 2 turns) the tile produces the\n"
        "     planted crop into its city's stockpile.\n\n"
        "Re-click 'Plant Crop' to swap crops. Seed is consumed each replant.\n"
        "Serialized in the Improvements save section.",
        ""});

    // WP-H Takeover
    entries.push_back({WikiCategory::Mechanics, "AI Takeover (Spectator Mode)",
        "In an auto-AI simulation, you can take over any AI player mid-game.\n\n"
        "STEPS:\n"
        "  1. Enter spectator mode.\n"
        "  2. Use Tab to cycle to the player you want to control.\n"
        "  3. Press Ctrl+T.\n\n"
        "EFFECTS:\n"
        "  - Target player becomes human-controlled (isHuman = true).\n"
        "  - Previous human slot (default player 0) becomes AI-controlled.\n"
        "  - Fog of war, camera, and UI switch to the target's perspective.\n"
        "  - AIController for the taken-over slot is skipped each turn.\n\n"
        "Game continues from the AI's exact state: cities, units, research,\n"
        "diplomacy, treasury, everything.",
        ""});

    // Monetary System
    entries.push_back({WikiCategory::Mechanics, "Monetary System",
        "Your civilization's money evolves through 4 stages:\n\n"
        "1. BARTER: Direct good-for-good exchange. Limited trade.\n"
        "2. COMMODITY MONEY: Metal coins (copper/silver/gold). Trade efficiency depends on coin tier.\n"
        "   - Copper coins: 65% trade efficiency, local trade\n"
        "   - Silver coins: 80% efficiency, regional trade\n"
        "   - Gold coins: 95% efficiency, international trade\n"
        "   - Coins flow through trade: net importers pay in coins\n"
        "   - Debasement: mix cheap metals for quick cash, but partners discover it\n"
        "3. GOLD STANDARD: Paper currency backed by gold reserves.\n"
        "   - Issue more currency than gold (fractional banking)\n"
        "   - Risk of bank run if debt exceeds gold reserves\n"
        "4. FIAT MONEY: Unbacked government currency.\n"
        "   - Full monetary control, but requires trust from partners\n"
        "   - Trust based on GDP rank, inflation discipline, debt ratio\n"
        "   - Highest trust score = reserve currency (+5% trade bonus)\n"
        "   - Hyperinflation risk if money printing is abused\n"
        "\nTransitions are one-way and require specific prerequisites.",
        ""});

    // Currency Crises
    entries.push_back({WikiCategory::Mechanics, "Currency Crises",
        "Three types of financial crisis can devastate your economy:\n\n"
        "BANK RUN (Gold Standard):\n"
        "  Trigger: Debt > 1.5x gold reserves AND inflation > 8%\n"
        "  Effect: Gold drains 20%/turn for 3 turns. If depleted: forced devaluation.\n\n"
        "HYPERINFLATION (Fiat):\n"
        "  Trigger: Inflation > 25% for 3 consecutive turns\n"
        "  Effect: -30% production, -20% science, -30 loyalty for 5 turns\n"
        "  Resolution: Automatic currency reform (money supply halved)\n\n"
        "SOVEREIGN DEFAULT:\n"
        "  Trigger: Treasury cannot cover debt interest payments\n"
        "  Effect: No loans for 10 turns, -30% trade efficiency, -3 amenities",
        ""});

    // Trade
    entries.push_back({WikiCategory::Mechanics, "Trade System",
        "Trade happens at two levels:\n\n"
        "DOMESTIC TRADE: Automatic redistribution of surplus goods between\n"
        "your own cities. Closer cities trade more efficiently.\n\n"
        "INTERNATIONAL TRADE: Bilateral agreements with other players.\n"
        "Goods travel along physical routes on the map.\n"
        "  - Land capacity: Road 2x, Railway 5x, Highway 8x\n"
        "  - Sea capacity: 10x land (one ship = 10 wagons)\n"
        "  - River capacity: 5x land (barges are efficient)\n"
        "  - Route capacity = min(land segment, sea segment)\n\n"
        "MERCHANT SHIPS: Barge(5), Cog(10), Galleon(20), Steamer(40), Container(80)\n"
        "Ships on a sea/river route contribute their cargo capacity.\n\n"
        "TRADE EFFICIENCY: Minimum of both players' monetary systems.\n"
        "Sanctions, debasement, and trust all affect efficiency.",
        ""});

    // Production
    entries.push_back({WikiCategory::Mechanics, "Production Chains",
        "Raw resources are processed through multi-tier production chains:\n"
        "  Raw -> Processed -> Advanced\n\n"
        "Each recipe requires a specific building and input goods.\n"
        "Output is modified by:\n"
        "  - Infrastructure bonus (roads/harbor nearby)\n"
        "  - Environment modifier (terrain suitability)\n"
        "  - Power efficiency (brownout if demand > supply)\n"
        "  - Production experience (+40% max from specialization)\n"
        "  - Industrial revolution multiplier (stacking per revolution)\n\n"
        "BUILDING CAPACITY: Each building has max batches/turn (upgradeable Lv1-3)\n"
        "QUALITY TIERS: Output can be Standard, High, or Premium\n"
        "  Quality depends on building level, experience, precision instruments\n"
        "  Premium goods sell for 2x market price\n\n"
        "WASTE: Industrial buildings produce pollution\n"
        "  Waste Treatment Plant converts 5 waste/turn into Construction Materials\n"
        "  Pollution: -food, -amenities, -growth at high levels",
        ""});

    // Power Grid
    entries.push_back({WikiCategory::Mechanics, "Power Grid",
        "Advanced buildings require energy. Without power, they operate\n"
        "at reduced efficiency (brownout).\n\n"
        "Power Sources:\n"
        "  Coal Plant:     30 energy, consumes 2 Coal/turn, 3 emissions\n"
        "  Oil Plant:      40 energy, consumes 2 Oil/turn, 2 emissions\n"
        "  Hydroelectric:  25 energy, free, requires river, 0 emissions\n"
        "  Nuclear Plant:  60 energy, consumes 1 Uranium/turn, 0.2% meltdown risk\n"
        "  Solar Array:    15 energy, free, 0 emissions (5th Industrial Rev)\n"
        "  Wind Farm:      12 energy, free, 0 emissions (5th Industrial Rev)\n\n"
        "Energy Demand:\n"
        "  Factory: 5, Electronics Plant: 10, Semiconductor Fab: 15\n"
        "  Each Robot Worker adds 5 demand\n\n"
        "Brownout: efficiency = supply / demand (applied to all production)",
        ""});

    // Industrial Revolutions
    entries.push_back({WikiCategory::Mechanics, "Industrial Revolutions",
        "Five transformative economic eras. Each requires specific techs\n"
        "and resources, and permanently transforms your economy:\n\n"
        "1ST - STEAM AGE: Coal + Iron + Steam tech\n"
        "  +50% industrial production, Railways unlocked, pollution begins\n\n"
        "2ND - ELECTRIC AGE: Oil + Steel + Electricity tech\n"
        "  +25% production, +50% trade capacity, +10% science\n\n"
        "3RD - DIGITAL AGE: Semiconductors + Computers tech\n"
        "  Highways + Automation unlocked, +100% trade, +20% science\n\n"
        "4TH - INFORMATION AGE: Software + Internet tech\n"
        "  +150% trade capacity, +30% science, -20% pollution\n\n"
        "5TH - POST-INDUSTRIAL: Fusion + Quantum Computing tech\n"
        "  Clean energy unlocked, +200% trade, +50% science, -50% pollution\n\n"
        "Bonuses are cumulative and per-player.",
        ""});

    // Communication & Empire Size
    entries.push_back({WikiCategory::Mechanics, "Communication & Empire Size",
        "How fast orders travel from your capital limits empire cohesion.\n\n"
        "Communication Tiers:\n"
        "  Foot Messenger: 1 tile/turn (Ancient)\n"
        "  Horse Relay: 3 tiles/turn (Horseback Riding)\n"
        "  Road Network: 5 tiles/turn (Roads built)\n"
        "  Railway Mail: 10 tiles/turn (Railways)\n"
        "  Telegraph: Instant to connected cities (Electricity)\n"
        "  Radio: Instant to all cities (Radio tech)\n"
        "  Internet: Instant + bonuses (Computers tech)\n\n"
        "Penalties per message-turn from capital:\n"
        "  -2 Loyalty, +1% Corruption, -3% Production, -2% Science\n\n"
        "Mitigations for large low-tech empires:\n"
        "  - Regional capitals (Bank building): halves distance for nearby cities\n"
        "  - Military garrisons: -50% loyalty penalty\n"
        "  - Better infrastructure: roads/railways increase comm speed",
        ""});

    // Government
    entries.push_back({WikiCategory::Mechanics, "Government System",
        "Your government determines policy slots, inherent bonuses,\n"
        "corruption rate, and a unique action.\n\n"
        "Changing government causes 5 turns of ANARCHY (no bonuses, -3 amenities).\n\n"
        "CORRUPTION scales with empire size:\n"
        "  corruption = corruptionRate * max(0, cities - 4)\n"
        "  Democracy: 2%/city, Fascism: 7%/city\n"
        "  This fraction of gold/production is lost each turn.\n\n"
        "POLICY CARDS are slotted into matching slot types.\n"
        "22 cards covering military, economic, diplomatic, and wildcard effects.\n\n"
        "UNIQUE ACTIONS are one-time abilities per government:\n"
        "  Democracy: Referendum (+20 loyalty)\n"
        "  Communism: Five Year Plan (+30% production)\n"
        "  Fascism: Mobilization (3 instant units)\n"
        "  Monarchy: Royal Decree (+15% gold)\n"
        "  Theocracy: Holy War (+4 combat, +faith)\n"
        "  Merchant Republic: Trade Fleet (+3 routes)",
        ""});

    // Combat
    entries.push_back({WikiCategory::Mechanics, "Combat",
        "Lanchester-model combat resolution with multiple modifiers:\n\n"
        "Base: attacker strength vs defender strength\n"
        "Modifiers:\n"
        "  - Terrain defense (Hills +30%, Forest +25%)\n"
        "  - River crossing: defender gets +25% if attacker crosses river\n"
        "  - Elevation: +10% per level advantage\n"
        "  - Flanking: +10% per adjacent friendly unit\n"
        "  - Fortification: +25% if defender is fortified\n"
        "  - War weariness: penalty scales with war duration\n"
        "  - Health: damaged units fight at reduced strength\n\n"
        "Damage dealt proportional to strength difference.",
        ""});

    // Victory
    entries.push_back({WikiCategory::Mechanics, "Victory Conditions",
        "No single 'first to X' win condition. Instead:\n\n"
        "CIVILIZATION SCORE INDEX (CSI):\n"
        "  8 categories scored relative to global average:\n"
        "  Economic, Military, Cultural, Scientific, Diplomatic,\n"
        "  Quality of Life, Territorial, Financial\n\n"
        "INTERDEPENDENCE MULTIPLIERS reward engagement:\n"
        "  - Trade network: 0 partners = 0.7x CSI, 6 partners = 1.2x\n"
        "  - Financial integration: bonds + reserve currency up to 1.15x\n"
        "  - Isolating yourself HURTS your score\n\n"
        "ERA EVALUATIONS every 30 turns award Victory Points.\n\n"
        "WIN CONDITIONS:\n"
        "  1. Prestige: Highest accumulated prestige at turn limit\n"
        "     (science, culture, faith, trade, diplomacy, peace,\n"
        "     governance -- max 13/turn, scales with game length)\n"
        "  2. Last Standing: All others eliminated\n"
        "  3. Score Victory: Highest cumulative Era VP at turn limit\n\n"
        "LOSING CONDITIONS:\n"
        "  - Economic collapse (GDP < 50% peak for 10 turns)\n"
        "  - Revolution (avg loyalty < 30 for 5 turns)\n"
        "  - Conquest (capital lost + 1 city remaining)\n"
        "  - Debt spiral (default + hyperinflation simultaneously)",
        ""});

    // Bonds & Financial Warfare
    entries.push_back({WikiCategory::Mechanics, "Bonds & Financial Warfare",
        "GOVERNMENT BONDS:\n"
        "  Issue bonds to raise cash. Other players buy them.\n"
        "  Yield = base rate + debt premium. Maturity: 10 turns.\n"
        "  Holding 30%+ of someone's bonds = financial leverage.\n\n"
        "BOND DUMPING: Sell all bonds from a target at 50% (fire sale).\n"
        "  Spikes target's yields, crashes their trust.\n\n"
        "SANCTIONS:\n"
        "  Trade Embargo: No trade routes\n"
        "  Financial Sanctions (SWIFT): -30% trade efficiency\n"
        "  Asset Freeze: Seize 25% of target's coin reserves\n"
        "  Secondary Sanctions: -15% to anyone trading with target\n\n"
        "CURRENCY WARS:\n"
        "  Devalue currency: +20% money supply, exports 15% cheaper\n"
        "  If 3+ civs devalue: Race to the Bottom (-20% global trade)",
        ""});
}

// ============================================================================
// Master builder
// ============================================================================

std::vector<WikiEntry> buildEncyclopedia() {
    std::vector<WikiEntry> entries;
    entries.reserve(200);

    buildUnitEntries(entries);
    buildBuildingEntries(entries);
    buildImprovementEntries(entries);
    buildGoodEntries(entries);
    buildRecipeEntries(entries);
    buildTechEntries(entries);
    buildGovernmentEntries(entries);
    buildPolicyEntries(entries);
    buildWonderEntries(entries);
    buildCivEntries(entries);
    buildMechanicsEntries(entries);

    return entries;
}

// ============================================================================
// Encyclopedia screen UI
// ============================================================================

void EncyclopediaScreen::setCategory(WikiCategory cat) {
    this->m_currentCategory = cat;
    this->m_searchQuery.clear();
    this->m_selectedEntry = -1;
}

void EncyclopediaScreen::search(const std::string& query) {
    this->m_searchQuery = query;
    this->m_selectedEntry = -1;
}

void EncyclopediaScreen::open(UIManager& ui) {
    if (this->m_isOpen) {
        return;
    }
    this->m_isOpen = true;

    // Build entries on first open
    if (this->m_allEntries.empty()) {
        this->m_allEntries = buildEncyclopedia();
    }

    constexpr float PANEL_W = 700.0f;
    constexpr float PANEL_H = 520.0f;
    WidgetId innerPanel = this->createScreenFrame(ui, "Encyclopedia",
                                                   PANEL_W, PANEL_H,
                                                   this->m_screenW, this->m_screenH);

    // Category buttons (horizontal row)
    this->m_categoryPanel = ui.createPanel(innerPanel,
        {0.0f, 0.0f, PANEL_W - 24.0f, 28.0f},
        PanelData{{0.08f, 0.08f, 0.12f, 0.9f}, 2.0f});
    Widget* catPanel = ui.getWidget(this->m_categoryPanel);
    if (catPanel != nullptr) {
        catPanel->layoutDirection = LayoutDirection::Horizontal;
        catPanel->childSpacing = 2.0f;
    }

    constexpr float BTN_W = 78.0f;
    for (uint8_t c = 0; c < static_cast<uint8_t>(WikiCategory::Count); ++c) {
        WikiCategory cat = static_cast<WikiCategory>(c);
        ButtonData btn;
        btn.label = wikiCategoryName(cat);
        btn.fontSize = 9.0f;
        btn.normalColor = {0.20f, 0.20f, 0.25f, 0.9f};
        btn.hoverColor = {0.30f, 0.30f, 0.40f, 0.9f};
        btn.onClick = [this, cat, &ui]() {
            this->setCategory(cat);
            this->rebuildEntryList(ui);
        };
        ui.createButton(this->m_categoryPanel, {0.0f, 0.0f, BTN_W, 22.0f}, std::move(btn));
    }

    // Entry list (left side, scrollable)
    this->m_entryList = ui.createScrollList(innerPanel,
        {0.0f, 0.0f, 220.0f, 400.0f});

    // Detail panel (right side)
    this->m_detailLabel = ui.createLabel(innerPanel,
        {230.0f, 30.0f, 430.0f, 400.0f},
        LabelData{"Select an entry from the list.", {0.7f, 0.8f, 0.7f, 1.0f}, 11.0f});

    this->rebuildEntryList(ui);
}

void EncyclopediaScreen::close(UIManager& ui) {
    if (!this->m_isOpen) {
        return;
    }
    this->m_isOpen = false;
    if (this->m_rootPanel != INVALID_WIDGET) {
        ui.removeWidget(this->m_rootPanel);
        this->m_rootPanel = INVALID_WIDGET;
    }
    this->m_categoryPanel = INVALID_WIDGET;
    this->m_entryList = INVALID_WIDGET;
    this->m_detailLabel = INVALID_WIDGET;
}

void EncyclopediaScreen::refresh(UIManager& /*ui*/) {
    // Static content, no per-frame refresh needed
}

void EncyclopediaScreen::rebuildEntryList(UIManager& ui) {
    // Filter entries by current category and search query
    this->m_filteredEntries.clear();
    for (const WikiEntry& entry : this->m_allEntries) {
        if (entry.category != this->m_currentCategory) {
            continue;
        }
        if (!this->m_searchQuery.empty()) {
            // Simple case-insensitive substring search
            std::string titleLower = entry.title;
            std::string queryLower = this->m_searchQuery;
            std::transform(titleLower.begin(), titleLower.end(), titleLower.begin(),
                          [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            std::transform(queryLower.begin(), queryLower.end(), queryLower.begin(),
                          [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            if (titleLower.find(queryLower) == std::string::npos) {
                continue;
            }
        }
        this->m_filteredEntries.push_back(&entry);
    }

    // Rebuild entry list widget
    if (this->m_entryList == INVALID_WIDGET) {
        return;
    }

    // Remove old children
    Widget* listWidget = ui.getWidget(this->m_entryList);
    if (listWidget != nullptr) {
        // Remove all existing children
        std::vector<WidgetId> children = listWidget->children;
        for (WidgetId child : children) {
            ui.removeWidget(child);
        }
    }

    // Add buttons for each filtered entry
    for (int32_t i = 0; i < static_cast<int32_t>(this->m_filteredEntries.size()); ++i) {
        const WikiEntry* entry = this->m_filteredEntries[static_cast<std::size_t>(i)];
        ButtonData btn;
        btn.label = entry->title;
        btn.fontSize = 10.0f;
        btn.normalColor = {0.18f, 0.18f, 0.22f, 0.9f};
        btn.hoverColor = {0.28f, 0.28f, 0.35f, 0.9f};
        btn.onClick = [this, i, &ui]() {
            this->m_selectedEntry = i;
            if (i >= 0 && i < static_cast<int32_t>(this->m_filteredEntries.size())) {
                const WikiEntry* e = this->m_filteredEntries[static_cast<std::size_t>(i)];
                std::string detail = e->title + "\n\n";
                if (!e->statsBlock.empty()) {
                    detail += e->statsBlock + "\n\n";
                }
                detail += e->body;
                ui.setLabelText(this->m_detailLabel, std::move(detail));
            }
        };
        ui.createButton(this->m_entryList, {0.0f, 0.0f, 200.0f, 18.0f}, std::move(btn));
    }

    // Reset detail label
    ui.setLabelText(this->m_detailLabel, "Select an entry from the list.");
}

} // namespace aoc::ui
