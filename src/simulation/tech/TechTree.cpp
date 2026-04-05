/**
 * @file TechTree.cpp
 * @brief Tech definitions and research advancement logic.
 */

#include "aoc/simulation/tech/TechTree.hpp"
#include "aoc/core/Log.hpp"

#include <cassert>

namespace aoc::sim {

namespace {

std::vector<TechDef> buildTechDefs() {
    std::vector<TechDef> techs;

    // Era 0: Ancient
    techs.push_back({TechId{0}, "Mining", EraId{0}, 25, {}, {}, {BuildingId{0}}, {}});
    techs.push_back({TechId{1}, "Animal Husbandry", EraId{0}, 25, {}, {}, {}, {UnitTypeId{4}}});
    techs.push_back({TechId{2}, "Pottery", EraId{0}, 25, {}, {}, {BuildingId{1}}, {}});
    techs.push_back({TechId{3}, "Writing", EraId{0}, 50, {{TechId{2}}}, {}, {BuildingId{7}}, {}});

    // Era 1: Classical
    techs.push_back({TechId{4}, "Bronze Working", EraId{1}, 60, {{TechId{0}}}, {}, {}, {UnitTypeId{0}}});
    techs.push_back({TechId{5}, "Currency", EraId{1}, 60, {{TechId{3}}}, {}, {BuildingId{6}}, {}});
    techs.push_back({TechId{6}, "Engineering", EraId{1}, 80, {{TechId{0}, TechId{2}}}, {}, {}, {}});

    // Era 2: Medieval
    techs.push_back({TechId{7}, "Apprenticeship", EraId{2}, 120, {{TechId{5}, TechId{6}}}, {}, {BuildingId{1}}, {}});
    techs.push_back({TechId{8}, "Metallurgy", EraId{2}, 140, {{TechId{4}}}, {}, {BuildingId{3}}, {}});

    // Era 3: Renaissance
    techs.push_back({TechId{9}, "Banking", EraId{3}, 200, {{TechId{5}, TechId{7}}}, {}, {}, {}});
    techs.push_back({TechId{10}, "Gunpowder", EraId{3}, 200, {{TechId{8}}}, {}, {}, {}});

    // Era 4: Industrial
    techs.push_back({TechId{11}, "Industrialization", EraId{4}, 300, {{TechId{8}, TechId{9}}},
        {}, {BuildingId{3}}, {}});
    techs.push_back({TechId{12}, "Refining", EraId{4}, 280, {{TechId{11}}},
        {}, {BuildingId{2}}, {}});
    techs.push_back({TechId{13}, "Economics", EraId{4}, 250, {{TechId{9}}}, {}, {}, {}});

    // Era 5: Modern
    techs.push_back({TechId{14}, "Electricity", EraId{5}, 400, {{TechId{11}}},
        {}, {BuildingId{4}}, {}});
    // Mass Production: now requires Interchangeable Parts(19) + Refining(12)
    techs.push_back({TechId{15}, "Mass Production", EraId{5}, 420, {{TechId{19}, TechId{12}}},
        {}, {BuildingId{5}}, {}});

    // Era 6: Information
    // Computers: now requires Electricity(14) + Semiconductors(23), unlocks Research Lab(12)
    techs.push_back({TechId{16}, "Computers", EraId{6}, 600, {{TechId{14}, TechId{23}}},
        {}, {BuildingId{12}}, {}});
    techs.push_back({TechId{17}, "Nuclear Fission", EraId{6}, 700, {{TechId{14}}}, {}, {}, {}});

    // ================================================================
    // NEW TECHS (18-27)
    // ================================================================

    // Era 4: Industrial -- precision manufacturing chain
    techs.push_back({TechId{18}, "Surface Plate", EraId{4}, 260,
        {{TechId{8}, TechId{11}}},  // Metallurgy + Industrialization
        {}, {BuildingId{10}}, {}});  // Unlocks Precision Workshop

    techs.push_back({TechId{19}, "Interchangeable Parts", EraId{4}, 320,
        {{TechId{18}}},  // Surface Plate
        {}, {}, {}});

    // Era 2: Medieval -- textiles
    techs.push_back({TechId{20}, "Textiles", EraId{2}, 100,
        {{TechId{7}}},  // Apprenticeship
        {}, {BuildingId{8}}, {}});  // Unlocks Textile Mill

    // Era 4: Industrial -- food preservation
    techs.push_back({TechId{21}, "Food Preservation", EraId{4}, 240,
        {{TechId{11}}},  // Industrialization
        {}, {BuildingId{9}}, {}});  // Unlocks Food Processing Plant

    // Era 5: Modern -- precision instruments
    techs.push_back({TechId{22}, "Precision Instruments", EraId{5}, 380,
        {{TechId{19}, TechId{14}}},  // Interchangeable Parts + Electricity
        {}, {}, {}});

    // Era 5: Modern -- semiconductors
    techs.push_back({TechId{23}, "Semiconductors", EraId{5}, 450,
        {{TechId{22}}},  // Precision Instruments
        {}, {BuildingId{11}}, {}});  // Unlocks Semiconductor Fab

    // (ID 24 reserved for future use)
    techs.push_back({TechId{24}, "Advanced Chemistry", EraId{5}, 350,
        {{TechId{12}}},  // Refining
        {}, {}, {}});

    // Era 5: Modern -- telecommunications
    techs.push_back({TechId{25}, "Telecommunications", EraId{5}, 380,
        {{TechId{14}}},  // Electricity
        {}, {BuildingId{13}}, {}});  // Unlocks Telecom Hub

    // Era 5: Modern -- aviation
    techs.push_back({TechId{26}, "Aviation", EraId{5}, 400,
        {{TechId{11}, TechId{12}}},  // Industrialization + Refining
        {}, {BuildingId{14}}, {}});  // Unlocks Airport

    // Era 6: Information -- internet
    techs.push_back({TechId{27}, "Internet", EraId{6}, 700,
        {{TechId{16}, TechId{25}}},  // Computers + Telecommunications
        {}, {}, {}});

    return techs;
}

const std::vector<TechDef>& getTechs() {
    static const std::vector<TechDef> techs = buildTechDefs();
    return techs;
}

} // anonymous namespace

const std::vector<TechDef>& allTechs() {
    return getTechs();
}

const TechDef& techDef(TechId id) {
    assert(id.isValid() && id.value < getTechs().size());
    return getTechs()[id.value];
}

uint16_t techCount() {
    return static_cast<uint16_t>(getTechs().size());
}

bool advanceResearch(PlayerTechComponent& tech, float sciencePoints) {
    if (!tech.currentResearch.isValid()) {
        return false;
    }

    tech.researchProgress += sciencePoints;

    const TechDef& def = techDef(tech.currentResearch);
    if (tech.researchProgress >= static_cast<float>(def.researchCost)) {
        LOG_INFO("Player %u researched: %.*s",
                 static_cast<unsigned>(tech.owner),
                 static_cast<int>(def.name.size()), def.name.data());
        tech.completeResearch();
        return true;
    }

    return false;
}

} // namespace aoc::sim
