/**
 * @file CivicTree.cpp
 * @brief Civic definitions and culture research logic.
 */

#include "aoc/simulation/tech/CivicTree.hpp"
#include "aoc/core/Log.hpp"

#include <cassert>

namespace aoc::sim {

namespace {

std::vector<CivicDef> buildCivicDefs() {
    std::vector<CivicDef> civics;

    // Era 0: Ancient
    //                  id, name, era, cost, prereqs, govUnlocks, policyUnlocks
    civics.push_back({CivicId{0}, "Code of Laws", EraId{0}, 20, {}, {}, {3, 8}});
    civics.push_back({CivicId{1}, "Craftsmanship", EraId{0}, 25, {}, {}, {1}});
    civics.push_back({CivicId{2}, "Foreign Trade", EraId{0}, 30, {{CivicId{0}}}, {}, {7}});

    // Era 1: Classical
    // Political Philosophy unlocks Autocracy + Oligarchy, policy: Charismatic Leader
    civics.push_back({CivicId{3}, "Political Philosophy", EraId{1}, 60, {{CivicId{0}}},
                      {1, 2}, {6}});
    civics.push_back({CivicId{4}, "Trade Routes", EraId{1}, 55, {{CivicId{2}}}, {}, {4}});
    civics.push_back({CivicId{5}, "Military Tradition", EraId{1}, 50, {{CivicId{1}}}, {}, {0}});

    // Era 2: Medieval
    // Feudalism unlocks Monarchy
    civics.push_back({CivicId{6}, "Feudalism", EraId{2}, 110, {{CivicId{3}}}, {3}, {}});
    civics.push_back({CivicId{7}, "Guilds", EraId{2}, 120, {{CivicId{4}, CivicId{1}}}, {}, {}});

    // Era 3: Renaissance
    civics.push_back({CivicId{8}, "Mercantilism", EraId{3}, 200, {{CivicId{7}}}, {}, {9}});
    civics.push_back({CivicId{9}, "Exploration", EraId{3}, 180, {{CivicId{4}}}, {}, {}});

    // Era 4: Industrial
    civics.push_back({CivicId{10}, "Capitalism", EraId{4}, 290, {{CivicId{8}}}, {}, {5}});
    // Nationalism unlocks Communism + Fascism, policy: Conscription
    civics.push_back({CivicId{11}, "Nationalism", EraId{4}, 260, {{CivicId{6}}}, {5, 6}, {2}});

    // Era 5: Modern
    // Suffrage unlocks Democracy
    civics.push_back({CivicId{12}, "Suffrage", EraId{5}, 380, {{CivicId{10}}}, {4}, {}});
    civics.push_back({CivicId{13}, "Globalization", EraId{5}, 400, {{CivicId{10}, CivicId{9}}}, {}, {}});

    return civics;
}

const std::vector<CivicDef>& getCivics() {
    static const std::vector<CivicDef> civics = buildCivicDefs();
    return civics;
}

} // anonymous namespace

const std::vector<CivicDef>& allCivics() { return getCivics(); }

const CivicDef& civicDef(CivicId id) {
    assert(id.isValid() && id.value < getCivics().size());
    return getCivics()[id.value];
}

uint16_t civicCount() {
    return static_cast<uint16_t>(getCivics().size());
}

bool advanceCivicResearch(PlayerCivicComponent& civic, float culturePoints) {
    if (!civic.currentResearch.isValid()) {
        return false;
    }
    civic.researchProgress += culturePoints;
    const CivicDef& def = civicDef(civic.currentResearch);
    if (civic.researchProgress >= static_cast<float>(def.cultureCost)) {
        LOG_INFO("Player %u completed: %.*s",
                 static_cast<unsigned>(civic.owner),
                 static_cast<int>(def.name.size()), def.name.data());
        civic.completeResearch();
        return true;
    }
    return false;
}

} // namespace aoc::sim
