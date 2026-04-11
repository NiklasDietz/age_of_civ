/**
 * @file TechTree.cpp
 * @brief Tech definitions and research advancement logic.
 */

#include "aoc/simulation/tech/TechTree.hpp"
#include "aoc/simulation/tech/ExpandedContent.hpp"
#include "aoc/simulation/turn/GameLength.hpp"
#include "aoc/core/Log.hpp"

#include <cassert>

namespace aoc::sim {

namespace {

std::vector<TechDef> buildTechDefs() {
    std::vector<TechDef> techs;

    // Era 0: Ancient -- costs 20-40 (fast early game: 2-5 turns each)
    techs.push_back({TechId{0}, "Mining", EraId{0}, 20, {}, {}, {BuildingId{0}}, {}});
    techs.push_back({TechId{1}, "Animal Husbandry", EraId{0}, 20, {}, {}, {}, {UnitTypeId{4}}});
    techs.push_back({TechId{2}, "Pottery", EraId{0}, 20, {}, {}, {BuildingId{1}}, {}});
    techs.push_back({TechId{3}, "Writing", EraId{0}, 40, {{TechId{2}}}, {}, {BuildingId{7}}, {}});

    // Era 1: Classical -- costs 50-80 (5-10 turns each)
    techs.push_back({TechId{4}, "Bronze Working", EraId{1}, 60, {{TechId{0}}}, {}, {}, {UnitTypeId{0}}});
    techs.push_back({TechId{5}, "Currency", EraId{1}, 65, {{TechId{3}}}, {}, {BuildingId{6}}, {}});
    techs.push_back({TechId{6}, "Engineering", EraId{1}, 80, {{TechId{0}, TechId{2}}}, {}, {}, {}});

    // Era 2: Medieval -- costs 160-240
    techs.push_back({TechId{7}, "Apprenticeship", EraId{2}, 190, {{TechId{5}, TechId{6}}}, {}, {BuildingId{1}}, {}});
    techs.push_back({TechId{8}, "Metallurgy", EraId{2}, 220, {{TechId{4}}}, {}, {BuildingId{3}}, {}});

    // Era 3: Renaissance -- costs 300-420
    techs.push_back({TechId{9}, "Banking", EraId{3}, 350, {{TechId{5}, TechId{7}}}, {}, {}, {}});
    techs.push_back({TechId{10}, "Gunpowder", EraId{3}, 380, {{TechId{8}}}, {}, {}, {}});

    // Era 4: Industrial -- costs 480-650
    techs.push_back({TechId{11}, "Industrialization", EraId{4}, 580, {{TechId{8}, TechId{9}}},
        {}, {BuildingId{3}}, {}});
    techs.push_back({TechId{12}, "Refining", EraId{4}, 520, {{TechId{11}}},
        {}, {BuildingId{2}}, {}});
    techs.push_back({TechId{13}, "Economics", EraId{4}, 480, {{TechId{9}}}, {}, {}, {}});

    // Era 5: Modern -- costs 750-950
    techs.push_back({TechId{14}, "Electricity", EraId{5}, 800, {{TechId{11}}},
        {}, {BuildingId{4}}, {}});
    techs.push_back({TechId{15}, "Mass Production", EraId{5}, 900, {{TechId{19}, TechId{12}}},
        {}, {BuildingId{5}}, {}});

    // Era 6: Information -- costs 1100-1400
    techs.push_back({TechId{16}, "Computers", EraId{6}, 1300, {{TechId{14}, TechId{23}}},
        {}, {BuildingId{12}}, {}});
    techs.push_back({TechId{17}, "Nuclear Fission", EraId{6}, 1400, {{TechId{14}}}, {}, {}, {}});

    // ================================================================
    // NEW TECHS (18-27)
    // ================================================================

    // Era 4: Industrial -- precision manufacturing chain
    techs.push_back({TechId{18}, "Surface Plate", EraId{4}, 500,
        {{TechId{8}, TechId{11}}},  // Metallurgy + Industrialization
        {}, {BuildingId{10}}, {}});  // Unlocks Precision Workshop

    techs.push_back({TechId{19}, "Interchangeable Parts", EraId{4}, 600,
        {{TechId{18}}},  // Surface Plate
        {}, {}, {}});

    // Era 2: Medieval -- textiles
    techs.push_back({TechId{20}, "Textiles", EraId{2}, 200,
        {{TechId{7}}},  // Apprenticeship
        {}, {BuildingId{8}}, {}});  // Unlocks Textile Mill

    // Era 4: Industrial -- food preservation
    techs.push_back({TechId{21}, "Food Preservation", EraId{4}, 560,
        {{TechId{11}}},  // Industrialization
        {}, {BuildingId{9}}, {}});  // Unlocks Food Processing Plant

    // Era 5: Modern -- precision instruments
    techs.push_back({TechId{22}, "Precision Instruments", EraId{5}, 930,
        {{TechId{19}, TechId{14}}},  // Interchangeable Parts + Electricity
        {}, {}, {}});

    // Era 5: Modern -- semiconductors
    techs.push_back({TechId{23}, "Semiconductors", EraId{5}, 1050,
        {{TechId{22}}},  // Precision Instruments
        {}, {BuildingId{11}}, {}});  // Unlocks Semiconductor Fab

    techs.push_back({TechId{24}, "Advanced Chemistry", EraId{5}, 850,
        {{TechId{12}}},  // Refining
        {}, {}, {}});

    // Era 5: Modern -- telecommunications
    techs.push_back({TechId{25}, "Telecommunications", EraId{5}, 930,
        {{TechId{14}}},  // Electricity
        {}, {BuildingId{13}}, {}});  // Unlocks Telecom Hub

    // Era 5: Modern -- aviation
    techs.push_back({TechId{26}, "Aviation", EraId{5}, 1000,
        {{TechId{11}, TechId{12}}},  // Industrialization + Refining
        {}, {BuildingId{14}}, {}});  // Unlocks Airport

    // Era 6: Information -- internet
    techs.push_back({TechId{27}, "Internet", EraId{6}, 1560,
        {{TechId{16}, TechId{25}}},  // Computers + Telecommunications
        {}, {}, {}});

    // Era 7: Future -- fusion power
    techs.push_back({TechId{28}, "Fusion Power", EraId{7}, 2000,
        {{TechId{17}, TechId{27}}},  // Nuclear Fission + Internet
        {}, {BuildingId{35}}, {}});  // Unlocks Fusion Reactor

    // Era 6: Atomic -- ecology (biofuel)
    techs.push_back({TechId{29}, "Ecology", EraId{6}, 1200,
        {{TechId{24}}},  // Advanced Chemistry
        {}, {BuildingId{33}}, {}});  // Unlocks Biofuel Plant

    // Append expanded techs (only IDs > max base ID to avoid overlap)
    uint16_t maxBaseId = 0;
    for (const TechDef& t : techs) {
        if (t.id.value > maxBaseId) { maxBaseId = t.id.value; }
    }
    for (int32_t i = 0; i < EXPANDED_TECH_COUNT; ++i) {
        const ExpandedTechDef& et = EXPANDED_TECHS[i];
        if (et.id <= maxBaseId) {
            continue;  // Skip IDs that overlap with base techs
        }
        TechDef def{};
        def.id = TechId{static_cast<uint16_t>(techs.size())};  // Use vector index as ID
        def.name = et.name;
        def.era = EraId{et.era};
        def.researchCost = et.cost;
        for (int32_t p = 0; p < 3; ++p) {
            if (et.prereqs[p] != 0xFFFF) {
                // Map expanded prereq IDs: if <= maxBaseId, use as-is (base tech).
                // If > maxBaseId, need to find the remapped index.
                def.prerequisites.push_back(TechId{et.prereqs[p]});
            }
        }
        techs.push_back(std::move(def));
    }

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
    float scaledCost = static_cast<float>(def.researchCost) * GamePace::instance().costMultiplier;
    if (tech.researchProgress >= scaledCost) {
        LOG_INFO("Player %u researched: %.*s",
                 static_cast<unsigned>(tech.owner),
                 static_cast<int>(def.name.size()), def.name.data());
        tech.completeResearch();
        return true;
    }

    return false;
}

} // namespace aoc::sim
