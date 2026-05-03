/**
 * @file CivicTree.cpp
 * @brief Civic definitions and culture research logic.
 */

#include "aoc/simulation/tech/CivicTree.hpp"
#include "aoc/simulation/government/GovernmentComponent.hpp"
#include "aoc/simulation/turn/GameLength.hpp"
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

    // ================================================================
    // Civ6 parity additions (IDs 14-43). 2026-05-03.
    // Mirrors Civ6 civic tree across all eras. Costs scale per-era.
    // ================================================================

    // Era 0: Ancient — three more civics (Civ6 has 6 total Ancient).
    civics.push_back({CivicId{14}, "State Workforce", EraId{0}, 35, {{CivicId{0}}}, {}, {}});
    civics.push_back({CivicId{15}, "Early Empire", EraId{0}, 40, {{CivicId{0}}}, {}, {}});
    civics.push_back({CivicId{16}, "Mysticism", EraId{0}, 35, {{CivicId{1}}}, {}, {}});

    // Era 1: Classical
    civics.push_back({CivicId{17}, "Drama and Poetry", EraId{1}, 60, {{CivicId{15}}}, {}, {}});
    civics.push_back({CivicId{18}, "Recorded History", EraId{1}, 70, {{CivicId{17}, CivicId{3}}}, {}, {}});
    civics.push_back({CivicId{19}, "Defensive Tactics", EraId{1}, 70, {{CivicId{5}}}, {}, {}});
    civics.push_back({CivicId{20}, "Games and Recreation", EraId{1}, 60, {{CivicId{14}}}, {}, {}});
    civics.push_back({CivicId{21}, "Theology", EraId{1}, 70, {{CivicId{16}, CivicId{17}}}, {}, {}});
    civics.push_back({CivicId{22}, "Naval Tradition", EraId{1}, 50, {{CivicId{2}}}, {}, {}});

    // Era 2: Medieval
    civics.push_back({CivicId{23}, "Civil Service", EraId{2}, 130, {{CivicId{3}}}, {}, {}});
    civics.push_back({CivicId{24}, "Divine Right", EraId{2}, 140, {{CivicId{21}}}, {}, {}});
    civics.push_back({CivicId{25}, "Mercenaries", EraId{2}, 130, {{CivicId{19}, CivicId{6}}}, {}, {}});
    civics.push_back({CivicId{26}, "Medieval Faires", EraId{2}, 120, {{CivicId{7}}}, {}, {}});

    // Era 3: Renaissance
    civics.push_back({CivicId{27}, "Humanism", EraId{3}, 200, {{CivicId{18}}}, {}, {}});
    civics.push_back({CivicId{28}, "Diplomatic Service", EraId{3}, 220, {{CivicId{23}}}, {}, {}});
    civics.push_back({CivicId{29}, "Reformed Church", EraId{3}, 200, {{CivicId{24}}}, {}, {}});
    civics.push_back({CivicId{30}, "Enlightenment", EraId{3}, 240, {{CivicId{27}, CivicId{29}}}, {}, {}});

    // Era 4: Industrial
    civics.push_back({CivicId{31}, "Civil Engineering", EraId{4}, 280, {{CivicId{30}, CivicId{26}}}, {}, {}});
    civics.push_back({CivicId{32}, "Colonialism", EraId{4}, 270, {{CivicId{9}}}, {}, {}});
    civics.push_back({CivicId{33}, "Operations Research", EraId{4}, 290, {{CivicId{31}, CivicId{25}}}, {}, {}});
    civics.push_back({CivicId{34}, "Urbanization", EraId{4}, 280, {{CivicId{31}}}, {}, {}});

    // Era 5: Modern
    civics.push_back({CivicId{35}, "Conservation", EraId{5}, 380, {{CivicId{34}}}, {}, {}});
    civics.push_back({CivicId{36}, "Mass Media", EraId{5}, 390, {{CivicId{34}}}, {}, {}});
    civics.push_back({CivicId{37}, "Mobilization", EraId{5}, 400, {{CivicId{11}, CivicId{33}}}, {}, {}});
    civics.push_back({CivicId{38}, "Ideology", EraId{5}, 410, {{CivicId{36}}}, {}, {}});

    // Era 6: Atomic
    civics.push_back({CivicId{39}, "Cultural Heritage", EraId{6}, 500, {{CivicId{35}}}, {}, {}});
    civics.push_back({CivicId{40}, "Cold War", EraId{6}, 520, {{CivicId{38}}}, {}, {}});
    civics.push_back({CivicId{41}, "Rapid Deployment", EraId{6}, 510, {{CivicId{37}}}, {}, {}});
    civics.push_back({CivicId{42}, "Space Race", EraId{6}, 530, {{CivicId{40}}}, {}, {}});

    // Era 7: Information
    civics.push_back({CivicId{43}, "Social Media", EraId{7}, 660, {{CivicId{36}}}, {}, {}});
    civics.push_back({CivicId{44}, "Environmentalism", EraId{7}, 680, {{CivicId{35}}}, {}, {}});
    civics.push_back({CivicId{45}, "Cultural Hegemony", EraId{7}, 700, {{CivicId{39}, CivicId{13}}}, {}, {}});
    civics.push_back({CivicId{46}, "Information Warfare", EraId{7}, 720, {{CivicId{40}, CivicId{43}}}, {}, {}});

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

bool advanceCivicResearch(PlayerCivicComponent& civic, float culturePoints,
                          PlayerGovernmentComponent* gov) {
    if (!civic.currentResearch.isValid()) {
        return false;
    }
    civic.researchProgress += culturePoints;
    const CivicDef& def = civicDef(civic.currentResearch);
    float scaledCost = static_cast<float>(def.cultureCost) * GamePace::instance().costMultiplier;
    if (civic.researchProgress >= scaledCost) {
        LOG_INFO("Player %u completed civic: %.*s",
                 static_cast<unsigned>(civic.owner),
                 static_cast<int>(def.name.size()), def.name.data());

        // Unlock governments and policies from this civic
        if (gov != nullptr) {
            for (uint8_t gid : def.unlockedGovernmentIds) {
                if (gid < GOVERNMENT_COUNT) {
                    gov->unlockGovernment(static_cast<GovernmentType>(gid));
                    LOG_INFO("Player %u unlocked government: %.*s",
                             static_cast<unsigned>(civic.owner),
                             static_cast<int>(governmentDef(static_cast<GovernmentType>(gid)).name.size()),
                             governmentDef(static_cast<GovernmentType>(gid)).name.data());
                }
            }
            for (uint8_t pid : def.unlockedPolicyIds) {
                if (pid < POLICY_CARD_COUNT) {
                    gov->unlockPolicy(pid);
                }
            }
        }

        civic.completeResearch();
        return true;
    }
    return false;
}

} // namespace aoc::sim
