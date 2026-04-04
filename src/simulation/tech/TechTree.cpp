/**
 * @file TechTree.cpp
 * @brief Tech definitions and research advancement logic.
 */

#include "aoc/simulation/tech/TechTree.hpp"

#include <cassert>
#include <cstdio>

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
    techs.push_back({TechId{15}, "Mass Production", EraId{5}, 420, {{TechId{11}, TechId{12}}},
        {}, {BuildingId{5}}, {}});

    // Era 6: Atomic/Information
    techs.push_back({TechId{16}, "Computers", EraId{6}, 600, {{TechId{14}}}, {}, {BuildingId{4}}, {}});
    techs.push_back({TechId{17}, "Nuclear Fission", EraId{6}, 700, {{TechId{14}}}, {}, {}, {}});

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
        std::fprintf(stdout, "[Tech] Player %u researched: %.*s\n",
                     static_cast<unsigned>(tech.owner),
                     static_cast<int>(def.name.size()), def.name.data());
        tech.completeResearch();
        return true;
    }

    return false;
}

} // namespace aoc::sim
