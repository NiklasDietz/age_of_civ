/**
 * @file GreatPeople.cpp
 * @brief Great Person definitions, point accumulation, recruitment, and activation.
 */

#include "aoc/simulation/greatpeople/GreatPeople.hpp"
#include "aoc/simulation/city/CityComponent.hpp"
#include "aoc/simulation/city/District.hpp"
#include "aoc/simulation/city/ProductionQueue.hpp"
#include "aoc/simulation/resource/ResourceComponent.hpp"
#include "aoc/simulation/unit/UnitComponent.hpp"
#include "aoc/simulation/unit/UnitTypes.hpp"
#include "aoc/simulation/tech/TechTree.hpp"
#include "aoc/map/HexGrid.hpp"
#include "aoc/map/HexCoord.hpp"
#include "aoc/game/GameState.hpp"
#include "aoc/game/Player.hpp"
#include "aoc/game/City.hpp"
#include "aoc/game/Unit.hpp"
#include "aoc/core/Log.hpp"

#include <cassert>
#include <vector>

namespace aoc::sim {

// ============================================================================
// Great Person definitions (18 total, ~3-4 per type)
// ============================================================================

static const std::array<GreatPersonDef, GREAT_PERSON_COUNT> s_greatPersonDefs = {{
    // Scientists (0-3)
    { 0, "Archimedes",      GreatPersonType::Scientist, "Eureka! +50% research progress on current tech."},
    { 1, "Euclid",          GreatPersonType::Scientist, "The Elements: +50% research progress on current tech."},
    { 2, "Isaac Newton",    GreatPersonType::Scientist, "Principia: +50% research progress on current tech."},
    { 3, "Galileo Galilei", GreatPersonType::Scientist, "Telescope: +50% research progress on current tech."},

    // Engineers (4-6)
    { 4, "Leonardo da Vinci", GreatPersonType::Engineer, "Renaissance Man: +100 production to nearest city."},
    { 5, "James Watt",        GreatPersonType::Engineer, "Steam Power: +100 production to nearest city."},
    { 6, "Nikola Tesla",      GreatPersonType::Engineer, "Alternating Current: +100 production to nearest city."},

    // Generals (7-9)
    { 7, "Sun Tzu",   GreatPersonType::General, "Art of War: heal all units within 2 hexes to full."},
    { 8, "Napoleon",  GreatPersonType::General, "Grande Armee: heal all units within 2 hexes to full."},
    { 9, "Patton",    GreatPersonType::General, "Blitzkrieg: heal all units within 2 hexes to full."},

    // Artists (10-13)
    {10, "Michelangelo",         GreatPersonType::Artist, "Masterpiece: culture bomb (claim tiles within 2 hexes)."},
    {11, "William Shakespeare",  GreatPersonType::Artist, "Globe Theatre: culture bomb (claim tiles within 2 hexes)."},
    {12, "Wolfgang A. Mozart",   GreatPersonType::Artist, "Symphony: culture bomb (claim tiles within 2 hexes)."},
    {13, "Rembrandt",            GreatPersonType::Artist, "Night Watch: culture bomb (claim tiles within 2 hexes)."},

    // Merchants (14-17)
    {14, "Marco Polo",          GreatPersonType::Merchant, "Silk Road: +200 gold to treasury."},
    {15, "Adam Smith",          GreatPersonType::Merchant, "Wealth of Nations: +200 gold to treasury."},
    {16, "John D. Rockefeller", GreatPersonType::Merchant, "Standard Oil: +200 gold to treasury."},
    {17, "Mansa Musa",          GreatPersonType::Merchant, "Pilgrimage: +200 gold to treasury."},
}};

const std::array<GreatPersonDef, GREAT_PERSON_COUNT>& allGreatPersonDefs() {
    return s_greatPersonDefs;
}

// ============================================================================
// Point accumulation
// ============================================================================

void accumulateGreatPeoplePoints(aoc::game::GameState& gameState, PlayerId player) {
    aoc::game::Player* playerObj = gameState.player(player);
    if (playerObj == nullptr) {
        return;
    }

    PlayerGreatPeopleComponent& gpComp = playerObj->greatPeople();

    // Tally district/building contributions across all of the player's cities
    for (const std::unique_ptr<aoc::game::City>& cityPtr : playerObj->cities()) {
        if (cityPtr == nullptr) {
            continue;
        }

        const CityDistrictsComponent& districts = cityPtr->districts();

        for (const CityDistrictsComponent::PlacedDistrict& district : districts.districts) {
            switch (district.type) {
                case DistrictType::Campus:
                    // Campus district: +2 Scientist points per building, +1 Artist per Library
                    for (BuildingId bid : district.buildings) {
                        gpComp.points[static_cast<std::size_t>(GreatPersonType::Scientist)] += 2.0f;
                        // Library (BuildingId 7) also gives +1 Artist point
                        if (bid.value == 7) {
                            gpComp.points[static_cast<std::size_t>(GreatPersonType::Artist)] += 1.0f;
                        }
                    }
                    // Base campus contribution even without buildings
                    if (district.buildings.empty()) {
                        gpComp.points[static_cast<std::size_t>(GreatPersonType::Scientist)] += 1.0f;
                    }
                    break;

                case DistrictType::Industrial:
                    // Industrial district: +2 Engineer points per building
                    for ([[maybe_unused]] BuildingId bid : district.buildings) {
                        gpComp.points[static_cast<std::size_t>(GreatPersonType::Engineer)] += 2.0f;
                    }
                    if (district.buildings.empty()) {
                        gpComp.points[static_cast<std::size_t>(GreatPersonType::Engineer)] += 1.0f;
                    }
                    break;

                case DistrictType::Encampment:
                    // Encampment: +2 General points per building
                    for ([[maybe_unused]] BuildingId bid : district.buildings) {
                        gpComp.points[static_cast<std::size_t>(GreatPersonType::General)] += 2.0f;
                    }
                    if (district.buildings.empty()) {
                        gpComp.points[static_cast<std::size_t>(GreatPersonType::General)] += 1.0f;
                    }
                    break;

                case DistrictType::Commercial:
                    // Commercial hub: +2 Merchant points per building
                    for ([[maybe_unused]] BuildingId bid : district.buildings) {
                        gpComp.points[static_cast<std::size_t>(GreatPersonType::Merchant)] += 2.0f;
                    }
                    if (district.buildings.empty()) {
                        gpComp.points[static_cast<std::size_t>(GreatPersonType::Merchant)] += 1.0f;
                    }
                    break;

                default:
                    break;
            }
        }
    }
}

// ============================================================================
// Recruitment
// ============================================================================

void checkGreatPeopleRecruitment(aoc::game::GameState& gameState, PlayerId player) {
    aoc::game::Player* playerObj = gameState.player(player);
    if (playerObj == nullptr) {
        return;
    }

    PlayerGreatPeopleComponent& gpComp = playerObj->greatPeople();

    // Find the player's capital (first city) for spawn location
    hex::AxialCoord spawnPos = {0, 0};
    for (const std::unique_ptr<aoc::game::City>& cityPtr : playerObj->cities()) {
        if (cityPtr != nullptr) {
            spawnPos = cityPtr->location();
            break;
        }
    }

    const std::array<GreatPersonDef, GREAT_PERSON_COUNT>& defs = allGreatPersonDefs();

    for (uint8_t typeIdx = 0;
         typeIdx < static_cast<uint8_t>(GreatPersonType::Count);
         ++typeIdx) {
        const GreatPersonType type = static_cast<GreatPersonType>(typeIdx);
        const float thresh = gpComp.threshold(type);

        if (gpComp.points[typeIdx] < thresh) {
            continue;
        }

        // Find the next available great person of this type
        uint8_t defId       = 0;
        int32_t countForType = 0;
        for (uint8_t d = 0; d < GREAT_PERSON_COUNT; ++d) {
            if (defs[d].type == type) {
                if (countForType == gpComp.recruited[typeIdx]) {
                    defId = d;
                    break;
                }
                ++countForType;
            }
        }

        // Spawn the great person as a unit owned by the player
        aoc::game::Unit& gpUnit = playerObj->addUnit(UnitTypeId{50}, spawnPos);
        GreatPersonComponent& comp = gpUnit.greatPerson();
        comp.owner       = player;
        comp.defId       = defId;
        comp.position    = spawnPos;
        comp.isActivated = false;

        // Reset points and increment recruited count
        gpComp.points[typeIdx]    -= thresh;
        gpComp.recruited[typeIdx] += 1;

        LOG_INFO("Player %u recruited Great Person: %.*s",
                 static_cast<unsigned>(player),
                 static_cast<int>(defs[defId].name.size()),
                 defs[defId].name.data());
    }
}

// ============================================================================
// Activation
// ============================================================================

void activateGreatPerson(aoc::game::GameState& gameState, aoc::map::HexGrid& grid,
                          aoc::game::Unit& gpUnit) {
    GreatPersonComponent& gp = gpUnit.greatPerson();
    if (gp.isActivated) {
        return;
    }

    const std::array<GreatPersonDef, GREAT_PERSON_COUNT>& defs = allGreatPersonDefs();
    assert(gp.defId < GREAT_PERSON_COUNT);
    const GreatPersonDef& def = defs[gp.defId];

    aoc::game::Player* playerObj = gameState.player(gp.owner);
    if (playerObj == nullptr) {
        return;
    }

    switch (def.type) {
        case GreatPersonType::Scientist: {
            // Add 50% of current research cost as progress
            PlayerTechComponent& tech = playerObj->tech();
            if (tech.currentResearch.isValid()) {
                const TechDef& tdef = techDef(tech.currentResearch);
                const float bonus   = static_cast<float>(tdef.researchCost) * 0.5f;
                tech.researchProgress += bonus;
                LOG_INFO("Scientist added %.0f research progress", static_cast<double>(bonus));
            }
            break;
        }

        case GreatPersonType::Engineer: {
            // Add 100 production to the nearest owned city's queue
            aoc::game::City* nearestCity = nullptr;
            int32_t bestDist = std::numeric_limits<int32_t>::max();
            for (const std::unique_ptr<aoc::game::City>& cityPtr : playerObj->cities()) {
                if (cityPtr == nullptr) {
                    continue;
                }
                const int32_t dist = hex::distance(gp.position, cityPtr->location());
                if (dist < bestDist) {
                    bestDist    = dist;
                    nearestCity = cityPtr.get();
                }
            }
            if (nearestCity != nullptr && !nearestCity->production().isEmpty()) {
                nearestCity->production().queue.front().progress += 100.0f;
                LOG_INFO("Engineer added 100 production to city queue");
            }
            break;
        }

        case GreatPersonType::General: {
            // Heal all friendly units within 2 hexes to full
            for (const std::unique_ptr<aoc::game::Unit>& unitPtr : playerObj->units()) {
                if (unitPtr == nullptr) {
                    continue;
                }
                if (hex::distance(unitPtr->position(), gp.position) <= 2) {
                    unitPtr->setHitPoints(unitPtr->typeDef().maxHitPoints);
                }
            }
            LOG_INFO("General healed all nearby units to full");
            break;
        }

        case GreatPersonType::Artist: {
            // Culture bomb: claim all unowned tiles within 2 hexes around the GP's position
            std::vector<hex::AxialCoord> tiles;
            hex::spiral(gp.position, 2, std::back_inserter(tiles));
            int32_t claimed = 0;
            for (const hex::AxialCoord& tile : tiles) {
                if (grid.isValid(tile)) {
                    const int32_t idx = grid.toIndex(tile);
                    if (grid.owner(idx) == INVALID_PLAYER) {
                        grid.setOwner(idx, gp.owner);
                        ++claimed;
                    }
                }
            }
            LOG_INFO("Artist culture-bombed %d tiles", claimed);
            break;
        }

        case GreatPersonType::Merchant: {
            // Add 200 gold to treasury
            playerObj->economy().treasury += 200;
            LOG_INFO("Merchant added 200 gold to treasury");
            break;
        }

        default:
            break;
    }

    gp.isActivated = true;

    // Remove the unit from the player's roster after activation
    playerObj->removeUnit(&gpUnit);
}

} // namespace aoc::sim
