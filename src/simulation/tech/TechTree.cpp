/**
 * @file TechTree.cpp
 * @brief Tech definitions and research advancement logic.
 */

#include "aoc/simulation/tech/TechTree.hpp"
#include "aoc/simulation/tech/ExpandedContent.hpp"
#include "aoc/simulation/turn/GameLength.hpp"
#include "aoc/core/Log.hpp"

#include <cassert>
#include <unordered_map>

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

    // Era 4: Industrial -- costs ~900-1200 (raised from 480-650 so the Steam Age
    // fires near turn ~250/500 instead of turn ~120, matching Civ 6 pacing).
    techs.push_back({TechId{11}, "Industrialization", EraId{4}, 1150, {{TechId{8}, TechId{9}}},
        {}, {BuildingId{3}}, {}});
    techs.push_back({TechId{12}, "Refining", EraId{4}, 1040, {{TechId{11}}},
        {}, {BuildingId{2}}, {}});
    techs.push_back({TechId{13}, "Economics", EraId{4}, 960, {{TechId{9}}}, {}, {}, {}});

    // Era 5: Modern -- costs ~1500-1800 (raised from 750-950).
    techs.push_back({TechId{14}, "Electricity", EraId{5}, 1600, {{TechId{11}}},
        {}, {BuildingId{4}}, {}});
    techs.push_back({TechId{15}, "Mass Production", EraId{5}, 1800, {{TechId{19}, TechId{12}}},
        {}, {BuildingId{5}}, {}});

    // Era 6: Information -- costs ~2600-2800 (raised from 1100-1400).
    techs.push_back({TechId{16}, "Computers", EraId{6}, 2600, {{TechId{14}, TechId{23}}},
        {}, {BuildingId{12}}, {}});
    techs.push_back({TechId{17}, "Nuclear Fission", EraId{6}, 2800, {{TechId{14}}}, {}, {}, {}});

    // ================================================================
    // NEW TECHS (18-27)
    // ================================================================

    // Era 4: Industrial -- precision manufacturing chain (costs scaled to era 4).
    techs.push_back({TechId{18}, "Surface Plate", EraId{4}, 1000,
        {{TechId{8}, TechId{11}}},  // Metallurgy + Industrialization
        {}, {BuildingId{10}}, {}});  // Unlocks Precision Workshop

    techs.push_back({TechId{19}, "Interchangeable Parts", EraId{4}, 1200,
        {{TechId{18}}},  // Surface Plate
        {}, {}, {}});

    // Era 2: Medieval -- textiles (pre-industrial, unchanged).
    techs.push_back({TechId{20}, "Textiles", EraId{2}, 200,
        {{TechId{7}}},  // Apprenticeship
        {}, {BuildingId{8}}, {}});  // Unlocks Textile Mill

    // Era 4: Industrial -- food preservation.
    techs.push_back({TechId{21}, "Food Preservation", EraId{4}, 1120,
        {{TechId{11}}},  // Industrialization
        {}, {BuildingId{9}}, {}});  // Unlocks Food Processing Plant

    // Era 5: Modern -- precision instruments.
    techs.push_back({TechId{22}, "Precision Instruments", EraId{5}, 1850,
        {{TechId{19}, TechId{14}}},  // Interchangeable Parts + Electricity
        {}, {}, {}});

    // Era 5: Modern -- semiconductors.
    techs.push_back({TechId{23}, "Semiconductors", EraId{5}, 2100,
        {{TechId{22}}},  // Precision Instruments
        {}, {BuildingId{11}}, {}});  // Unlocks Semiconductor Fab

    techs.push_back({TechId{24}, "Advanced Chemistry", EraId{5}, 1700,
        {{TechId{12}}},  // Refining
        {}, {}, {}});

    // Era 5: Modern -- telecommunications.
    techs.push_back({TechId{25}, "Telecommunications", EraId{5}, 1850,
        {{TechId{14}}},  // Electricity
        {}, {BuildingId{13}}, {}});  // Unlocks Telecom Hub

    // Era 5: Modern -- aviation.
    techs.push_back({TechId{26}, "Aviation", EraId{5}, 2000,
        {{TechId{11}, TechId{12}}},  // Industrialization + Refining
        {}, {BuildingId{14}}, {}});  // Unlocks Airport

    // Era 6: Information -- internet.
    techs.push_back({TechId{27}, "Internet", EraId{6}, 3120,
        {{TechId{16}, TechId{25}}},  // Computers + Telecommunications
        {}, {}, {}});

    // Era 7: Future -- fusion power.
    techs.push_back({TechId{28}, "Fusion Power", EraId{7}, 4000,
        {{TechId{17}, TechId{27}}},  // Nuclear Fission + Internet
        {}, {BuildingId{35}}, {}});  // Unlocks Fusion Reactor

    // Era 6: Atomic -- ecology (biofuel).
    techs.push_back({TechId{29}, "Ecology", EraId{6}, 2400,
        {{TechId{24}}},  // Advanced Chemistry
        {}, {BuildingId{33}}, {}});  // Unlocks Biofuel Plant

    // Append expanded techs (only IDs > max base ID to avoid overlap).
    // H3.1: expanded techs reference prereqs by their *original* expanded IDs,
    // but the appended TechDef gets a fresh `techs.size()` index. Without a
    // remap, expanded-to-expanded prereq edges silently point at the wrong
    // vector slots and parts of the tech DAG become unreachable. Build a
    // two-pass {originalExpandedId -> newIndex} map, then rewrite prereqs.
    uint16_t maxBaseId = 0;
    for (const TechDef& t : techs) {
        if (t.id.value > maxBaseId) { maxBaseId = t.id.value; }
    }

    std::unordered_map<uint16_t, uint16_t> expandedRemap;
    expandedRemap.reserve(EXPANDED_TECH_COUNT);
    for (int32_t i = 0; i < EXPANDED_TECH_COUNT; ++i) {
        const ExpandedTechDef& et = EXPANDED_TECHS[i];
        if (et.id <= maxBaseId) {
            continue;  // Skip IDs that overlap with base techs
        }
        const uint16_t newIndex = static_cast<uint16_t>(techs.size());
        expandedRemap.emplace(et.id, newIndex);
        TechDef def{};
        def.id = TechId{newIndex};
        def.name = et.name;
        def.era = EraId{et.era};
        def.researchCost = et.cost;
        techs.push_back(std::move(def));
    }

    // Second pass: fill in prerequisites using the remap. Base-tech prereq IDs
    // (<= maxBaseId) pass through unchanged because base IDs equal their vector
    // slots. Expanded-to-expanded edges look up the remapped index.
    for (int32_t i = 0; i < EXPANDED_TECH_COUNT; ++i) {
        const ExpandedTechDef& et = EXPANDED_TECHS[i];
        auto it = expandedRemap.find(et.id);
        if (it == expandedRemap.end()) { continue; }
        TechDef& def = techs[it->second];
        for (int32_t p = 0; p < 3; ++p) {
            const uint16_t raw = et.prereqs[p];
            if (raw == 0xFFFF) { continue; }
            if (raw <= maxBaseId) {
                def.prerequisites.push_back(TechId{raw});
            } else {
                auto rit = expandedRemap.find(raw);
                if (rit != expandedRemap.end()) {
                    def.prerequisites.push_back(TechId{rit->second});
                }
                // Else: dangling reference — silently drop so the tech can
                // still be researched rather than trapping it permanently.
            }
        }
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
    // Recursive tech cost scaling: techs with deeper dependency trees cost more.
    // Each already-researched tech adds 5% to the base cost.
    int32_t numPrereqs = 0;
    for (uint16_t ti = 0; ti < techCount(); ++ti) {
        if (tech.hasResearched(TechId{ti})) {
            ++numPrereqs;
        }
    }
    float depthMultiplier = 1.0f + static_cast<float>(numPrereqs) * 0.05f;

    // H3.2: early-era tech discount removed. The depthMultiplier above already
    // makes early techs cheap relative to late techs (low prereq count ->
    // multiplier ~1.0, vs 3.5+ for late techs). Stacking a flat 30% era
    // discount on top produced a sub-50% early price and an overpowered early
    // spiral. Research pace is now handled by the bumped per-citizen science
    // output in CityScience.cpp instead.
    float scaledCost = static_cast<float>(def.researchCost)
                     * GamePace::instance().costMultiplier
                     * depthMultiplier;
    if (tech.researchProgress >= scaledCost) {
        LOG_INFO("Player %u researched: %.*s",
                 static_cast<unsigned>(tech.owner),
                 static_cast<int>(def.name.size()), def.name.data());
        tech.completeResearch();
        return true;
    }

    return false;
}

void acquireTechFromTrade(PlayerTechComponent& tech, TechId techId) {
    if (!techId.isValid() || techId.value >= tech.knownTechs.size()) { return; }
    tech.knownTechs[techId.value] = true;
    // If all prereqs already met, promote to completed immediately.
    const TechDef& def = techDef(techId);
    bool allPrereqsMet = true;
    for (TechId p : def.prerequisites) {
        if (!tech.hasResearched(p)) { allPrereqsMet = false; break; }
    }
    if (allPrereqsMet && techId.value < tech.completedTechs.size()) {
        tech.completedTechs[techId.value] = true;
    }
}

void promotePendingTechs(PlayerTechComponent& tech) {
    bool changed = true;
    while (changed) {
        changed = false;
        for (uint16_t i = 0; i < tech.knownTechs.size(); ++i) {
            if (!tech.knownTechs[i]) { continue; }
            if (tech.completedTechs[i]) { continue; }
            const TechDef& def = techDef(TechId{i});
            bool allMet = true;
            for (TechId p : def.prerequisites) {
                if (!tech.hasResearched(p)) { allMet = false; break; }
            }
            if (allMet) {
                tech.completedTechs[i] = true;
                changed = true;
            }
        }
    }
}

} // namespace aoc::sim
