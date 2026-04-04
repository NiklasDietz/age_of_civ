/**
 * @file CivicTree.cpp
 * @brief Civic definitions and culture research logic.
 */

#include "aoc/simulation/tech/CivicTree.hpp"

#include <cassert>
#include <cstdio>

namespace aoc::sim {

namespace {

std::vector<CivicDef> buildCivicDefs() {
    std::vector<CivicDef> civics;

    // Era 0: Ancient
    civics.push_back({CivicId{0}, "Code of Laws", EraId{0}, 20, {}});
    civics.push_back({CivicId{1}, "Craftsmanship", EraId{0}, 25, {}});
    civics.push_back({CivicId{2}, "Foreign Trade", EraId{0}, 30, {{CivicId{0}}}});

    // Era 1: Classical
    civics.push_back({CivicId{3}, "Political Philosophy", EraId{1}, 60, {{CivicId{0}}}});
    civics.push_back({CivicId{4}, "Trade Routes", EraId{1}, 55, {{CivicId{2}}}});
    civics.push_back({CivicId{5}, "Military Tradition", EraId{1}, 50, {{CivicId{1}}}});

    // Era 2: Medieval
    civics.push_back({CivicId{6}, "Feudalism", EraId{2}, 110, {{CivicId{3}}}});
    civics.push_back({CivicId{7}, "Guilds", EraId{2}, 120, {{CivicId{4}, CivicId{1}}}});

    // Era 3: Renaissance
    civics.push_back({CivicId{8}, "Mercantilism", EraId{3}, 200, {{CivicId{7}}}});
    civics.push_back({CivicId{9}, "Exploration", EraId{3}, 180, {{CivicId{4}}}});

    // Era 4: Industrial
    civics.push_back({CivicId{10}, "Capitalism", EraId{4}, 290, {{CivicId{8}}}});
    civics.push_back({CivicId{11}, "Nationalism", EraId{4}, 260, {{CivicId{6}}}});

    // Era 5: Modern
    civics.push_back({CivicId{12}, "Suffrage", EraId{5}, 380, {{CivicId{10}}}});
    civics.push_back({CivicId{13}, "Globalization", EraId{5}, 400, {{CivicId{10}, CivicId{9}}}});

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
        std::fprintf(stdout, "[Civic] Player %u completed: %.*s\n",
                     static_cast<unsigned>(civic.owner),
                     static_cast<int>(def.name.size()), def.name.data());
        civic.completeResearch();
        return true;
    }
    return false;
}

} // namespace aoc::sim
