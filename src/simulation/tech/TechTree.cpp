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
    // 2026-05-03: Bronze Working moved Classical → Ancient (matches Civ6;
    // it's a Mining-tier metallurgy tech, not Classical). Cost dropped 60→35.
    techs.push_back({TechId{4}, "Bronze Working", EraId{0}, 35, {{TechId{0}}}, {}, {}, {UnitTypeId{0}}});
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

    // Era 5: Modern. Costs restored to 1500-1800 band — bulk era-5 cuts
    // backfired in audit. Late-era pacing now adjusted at era-6/7 only.
    techs.push_back({TechId{14}, "Electricity", EraId{5}, 1500, {{TechId{11}}},
        {}, {BuildingId{4}}, {}});
    techs.push_back({TechId{15}, "Mass Production", EraId{5}, 1700, {{TechId{19}, TechId{12}}},
        {}, {BuildingId{5}}, {}});

    // Era 6: Information -- costs ~1300-1500 (was 2600-2800 → 1800-2000 → 1300-1500).
    // 2026-05-03: pushed lower again so IR #3 Digital Age fires beyond once-per-audit.
    techs.push_back({TechId{16}, "Computers", EraId{6}, 950, {{TechId{14}, TechId{23}}},
        {}, {BuildingId{12}}, {}});
    techs.push_back({TechId{17}, "Nuclear Fission", EraId{6}, 1100, {{TechId{14}}}, {}, {}, {}});

    // ================================================================
    // NEW TECHS (18-27)
    // ================================================================

    // Era 4: Industrial -- precision manufacturing chain.
    // 2026-05-02: cost 1000 → 700 → 500 to reach Steam Age inside game
    // length. Audit: with 700 only 21% of civs hit all 3 era-4 rev gates.
    techs.push_back({TechId{18}, "Surface Plate", EraId{4}, 500,
        {{TechId{8}, TechId{11}}},  // Metallurgy + Industrialization
        {}, {BuildingId{10}}, {}});  // Unlocks Precision Workshop

    // 2026-05-03: Interchangeable Parts Industrial → Modern (era 4 → 5) to
    // match Civ6. Whitney's interchangeable-parts breakthrough was 1801,
    // but the tech is conceptually a Modern manufacturing-era enabler.
    // Cost bumped 1200 → 1500 (Modern band 1500-1800).
    techs.push_back({TechId{19}, "Interchangeable Parts", EraId{5}, 1500,
        {{TechId{18}}},  // Surface Plate
        {}, {}, {}});

    // Era 2: Medieval -- textiles (pre-industrial, unchanged).
    techs.push_back({TechId{20}, "Textiles", EraId{2}, 200,
        {{TechId{7}}},  // Apprenticeship
        {}, {BuildingId{8}}, {}});  // Unlocks Textile Mill

    // Era 4: Industrial -- food preservation.
    // 2026-05-02: cost 1120 → 750 → 500. Rev gate intact; cheaper to fit
    // inside game length.
    techs.push_back({TechId{21}, "Food Preservation", EraId{4}, 500,
        {{TechId{11}}},  // Industrialization
        {}, {BuildingId{9}}, {}});  // Unlocks Food Processing Plant

    // Era 5: Modern -- precision instruments.
    // 2026-05-03: 1850 → 900 → 600. Audit showed only 46% civs reached it.
    techs.push_back({TechId{22}, "Precision Instruments", EraId{5}, 600,
        {{TechId{19}, TechId{14}}},  // Interchangeable Parts + Electricity
        {}, {}, {}});

    // Semiconductors: Information era. 2026-05-03: 2700 → 1900 → 1400.
    techs.push_back({TechId{23}, "Semiconductors", EraId{6}, 1050,
        {{TechId{22}}},  // Precision Instruments
        {}, {BuildingId{11}}, {}});  // Unlocks Semiconductor Fab

    techs.push_back({TechId{24}, "Advanced Chemistry", EraId{5}, 1600,
        {{TechId{12}}},  // Refining
        {}, {}, {}});

    // Era 5: Modern -- telecommunications. Civ6 puts this in Information era
    // but our game gates the Electric Age IR on it.
    // 2026-05-03: 1850 → 1100 to clear the IR #2 bottleneck (was 22% reach).
    techs.push_back({TechId{25}, "Telecommunications", EraId{5}, 1100,
        {{TechId{14}}},  // Electricity
        {}, {BuildingId{13}}, {}});  // Unlocks Telecom Hub

    // Era 5: Modern -- aviation.
    techs.push_back({TechId{26}, "Aviation", EraId{5}, 1900,
        {{TechId{11}, TechId{12}}},  // Industrialization + Refining
        {}, {BuildingId{14}}, {}});  // Unlocks Airport

    // Era 6: Information -- internet.
    // 2026-05-03: 3120 → 2200 → 1650 so IR #4 fires regularly.
    techs.push_back({TechId{27}, "Internet", EraId{6}, 1650,
        {{TechId{16}, TechId{25}}},  // Computers + Telecommunications
        {}, {}, {}});

    // Era 7: Future -- fusion power. 2026-05-03: 4000 → 2400 so IR #5
    // Post-Industrial becomes reachable in 2000-turn audits.
    techs.push_back({TechId{28}, "Fusion Power", EraId{7}, 2400,
        {{TechId{17}, TechId{27}}},  // Nuclear Fission + Internet
        {}, {BuildingId{35}}, {}});  // Unlocks Fusion Reactor

    // Era 6: Atomic -- ecology (biofuel).
    techs.push_back({TechId{29}, "Ecology", EraId{6}, 2400,
        {{TechId{24}}},  // Advanced Chemistry
        {}, {BuildingId{33}}, {}});  // Unlocks Biofuel Plant

    // Era 3: Medieval/Renaissance -- Navigation. Unlocks ocean tile traversal
    // for embarked land units and naval units. Coastal / shallow-water
    // travel works from game start (canoes, rafts, coasters); deep ocean
    // requires astronomical instruments + dead-reckoning. Real-history
    // analogue: Polynesian / Norse coastal travel was prehistoric, but
    // open-ocean blue-water navigation needed magnetic compass + astrolabe
    // + cartography (~12-15th century). Civ6 puts Cartography in Medieval.
    // Cost matches Banking (350) era-3 baseline. Prereq Apprenticeship (7).
    techs.push_back({TechId{30}, "Navigation", EraId{3}, 320,
        {{TechId{7}}},
        {}, {}, {}});

    // ================================================================
    // Civ6-parity additions (IDs 31-50). 2026-05-03.
    // The expanded-tech array's Ancient/Classical entries collided with
    // base IDs and were silently skipped, so the tree was missing core
    // Civ6 techs (Sailing, Astrology, Masonry, The Wheel, Archery, etc.).
    // Add them explicitly here. Prereqs reference base IDs only.
    // Costs follow band: Ancient 20-40, Classical 50-80, Medieval 160-240.
    // ================================================================

    // Era 0: Ancient -- naval bootstrap
    techs.push_back({TechId{31}, "Sailing", EraId{0}, 30, {}, {}, {}, {}});
    // Era 0: Ancient -- shrine, religion seed
    techs.push_back({TechId{32}, "Astrology", EraId{0}, 30, {}, {}, {}, {}});
    // Era 0: Ancient -- masonry / walls
    techs.push_back({TechId{33}, "Masonry", EraId{0}, 35,
        {{TechId{0}}}, {}, {}, {}});  // Mining
    // Era 0: Ancient -- wheel / chariots
    techs.push_back({TechId{34}, "The Wheel", EraId{0}, 35,
        {{TechId{0}}}, {}, {}, {}});  // Mining
    // Era 0: Ancient -- archery (unlocks Archer 36)
    techs.push_back({TechId{35}, "Archery", EraId{0}, 30,
        {{TechId{1}}}, {}, {}, {UnitTypeId{36}}});
    // Era 0: Ancient -- plantation / irrigation
    techs.push_back({TechId{36}, "Irrigation", EraId{0}, 30,
        {{TechId{2}}}, {}, {}, {}});  // Pottery
    // Era 0: Ancient -- iron-grade swords (unlocks Swordsman 10)
    techs.push_back({TechId{37}, "Iron Working", EraId{0}, 40,
        {{TechId{4}}}, {}, {}, {UnitTypeId{10}}});

    // Era 1: Classical -- math
    techs.push_back({TechId{38}, "Mathematics", EraId{1}, 70,
        {{TechId{5}}}, {}, {}, {}});  // Currency
    // Era 1: Classical -- construction
    techs.push_back({TechId{39}, "Construction", EraId{1}, 75,
        {{TechId{33}}}, {}, {}, {}});  // Masonry
    // Era 1: Classical -- mounted units (unlocks Horseman 4)
    techs.push_back({TechId{40}, "Horseback Riding", EraId{1}, 60,
        {{TechId{1}}}, {}, {}, {UnitTypeId{4}}});
    // Era 1: Classical -- celestial nav (Galley range bump in Civ6)
    techs.push_back({TechId{41}, "Celestial Navigation", EraId{1}, 70,
        {{TechId{31}}}, {}, {}, {}});  // Sailing
    // Era 1: Classical -- larger ships
    techs.push_back({TechId{42}, "Shipbuilding", EraId{1}, 75,
        {{TechId{31}}}, {}, {}, {}});  // Sailing
    // Era 2: Medieval -- military engineering
    techs.push_back({TechId{43}, "Military Engineering", EraId{2}, 200,
        {{TechId{6}, TechId{39}}}, {}, {}, {}});  // Engineering + Construction
    // Era 2: Medieval -- university branch
    techs.push_back({TechId{44}, "Education", EraId{2}, 220,
        {{TechId{38}, TechId{7}}}, {}, {}, {}});  // Math + Apprenticeship
    // Era 3: Renaissance -- astronomy
    techs.push_back({TechId{45}, "Astronomy", EraId{3}, 360,
        {{TechId{44}}}, {}, {}, {}});  // Education
    // Era 3: Renaissance -- larger sail rigs
    techs.push_back({TechId{46}, "Square Rigging", EraId{3}, 380,
        {{TechId{30}}}, {}, {}, {}});  // Navigation
    // Era 5: Modern -- steel
    techs.push_back({TechId{47}, "Steel", EraId{5}, 1700,
        {{TechId{8}}}, {}, {}, {}});  // Metallurgy
    // Era 5: Modern -- replaceable parts (interchangeable assembly line)
    techs.push_back({TechId{48}, "Replaceable Parts", EraId{5}, 1700,
        {{TechId{19}}}, {}, {}, {}});  // Interchangeable Parts
    // Era 6: Atomic -- lasers
    techs.push_back({TechId{49}, "Lasers", EraId{6}, 2700,
        {{TechId{17}}}, {}, {}, {}});  // Nuclear Fission
    // Era 6: Atomic -- composites
    techs.push_back({TechId{50}, "Composites", EraId{6}, 2600,
        {{TechId{24}}}, {}, {}, {}});  // Advanced Chemistry

    // ================================================================
    // Civ6 medieval / industrial / modern parity (IDs 51-71). 2026-05-03.
    // Previously these came from EXPANDED_TECHS array but got skipped
    // when maxBaseId rose past 30. Defining explicitly so AI can research
    // and prereq chains stay deterministic.
    // ================================================================
    // Era 2: Medieval (with unit unlocks)
    techs.push_back({TechId{51}, "Machinery", EraId{2}, 200,
        {{TechId{37}, TechId{6}}}, {}, {}, {UnitTypeId{11}}});  // Crossbowman
    techs.push_back({TechId{52}, "Stirrups", EraId{2}, 180,
        {{TechId{40}, TechId{37}}}, {}, {}, {UnitTypeId{12}}});  // Knight
    techs.push_back({TechId{53}, "Military Tactics", EraId{2}, 200,
        {{TechId{38}}}, {}, {}, {UnitTypeId{26}, UnitTypeId{24}}});  // Pikeman, Trebuchet
    techs.push_back({TechId{54}, "Castles", EraId{2}, 230,
        {{TechId{39}, TechId{53}}}, {}, {}, {UnitTypeId{33}}});  // Man-at-Arms
    // Era 3: Renaissance
    techs.push_back({TechId{55}, "Printing", EraId{3}, 320,
        {{TechId{44}}}, {}, {}, {}});  // Education
    techs.push_back({TechId{56}, "Cartography", EraId{3}, 320,
        {{TechId{30}}}, {}, {}, {}});  // Navigation
    techs.push_back({TechId{57}, "Siege Tactics", EraId{3}, 380,
        {{TechId{53}, TechId{54}}}, {}, {}, {}});  // MilTac + Castles
    techs.push_back({TechId{58}, "Metal Casting", EraId{3}, 360,
        {{TechId{37}, TechId{10}}}, {}, {}, {UnitTypeId{25}, UnitTypeId{40}}});  // Bombard, Cuirassier
    // Era 4: Industrial
    techs.push_back({TechId{59}, "Scientific Theory", EraId{4}, 950,
        {{TechId{45}}}, {}, {}, {}});  // Astronomy
    techs.push_back({TechId{60}, "Ballistics", EraId{4}, 1020,
        {{TechId{58}}}, {}, {}, {UnitTypeId{37}, UnitTypeId{16}}});  // Field Cannon, Field Artillery
    techs.push_back({TechId{61}, "Steam Power", EraId{4}, 1100,
        {{TechId{11}}}, {}, {}, {}});  // Industrialization
    techs.push_back({TechId{62}, "Sanitation", EraId{4}, 980,
        {{TechId{59}}}, {}, {}, {}});  // Scientific Theory
    techs.push_back({TechId{63}, "Rifling", EraId{4}, 1050,
        {{TechId{60}, TechId{10}}}, {}, {}, {}});  // Ballistics + Gunpowder
    // Era 5: Modern
    techs.push_back({TechId{64}, "Flight", EraId{5}, 1700,
        {{TechId{61}, TechId{59}}}, {}, {}, {}});  // SteamPower + SciTheory
    techs.push_back({TechId{65}, "Chemistry", EraId{5}, 1600,
        {{TechId{62}}}, {}, {}, {}});  // Sanitation
    techs.push_back({TechId{66}, "Combustion", EraId{5}, 1700,
        {{TechId{12}, TechId{65}}}, {}, {}, {UnitTypeId{17}}});  // Tank
    // Era 6: Atomic / Information
    techs.push_back({TechId{67}, "Plastics", EraId{6}, 2400,
        {{TechId{12}}}, {}, {}, {}});  // Refining
    techs.push_back({TechId{68}, "Synthetic Materials", EraId{6}, 2600,
        {{TechId{67}}}, {}, {}, {}});  // Plastics
    techs.push_back({TechId{69}, "Combined Arms", EraId{6}, 2700,
        {{TechId{47}, TechId{66}}}, {}, {}, {UnitTypeId{42}}});  // Modern Armor
    techs.push_back({TechId{70}, "Robotics", EraId{6}, 2800,
        {{TechId{16}, TechId{68}}}, {}, {}, {}});  // Computers + SyntMat
    techs.push_back({TechId{71}, "Satellites", EraId{6}, 2700,
        {{TechId{64}}}, {}, {}, {}});  // Flight

    // ================================================================
    // Future-era techs (IDs 72-76). 2026-05-03 cost cuts (was 3500-3800).
    // ================================================================
    techs.push_back({TechId{72}, "Bioengineering", EraId{7}, 2200,
        {{TechId{65}}}, {}, {}, {}});  // Chemistry
    techs.push_back({TechId{73}, "Cybernetics", EraId{7}, 2300,
        {{TechId{70}}}, {}, {}, {}});  // Robotics
    techs.push_back({TechId{74}, "Quantum Computing", EraId{7}, 2400,
        {{TechId{16}, TechId{49}}}, {}, {}, {}});  // Computers + Lasers
    techs.push_back({TechId{75}, "Genetic Engineering", EraId{7}, 2200,
        {{TechId{72}}}, {}, {}, {}});  // Bioengineering
    techs.push_back({TechId{76}, "Smart Materials", EraId{7}, 2300,
        {{TechId{50}, TechId{68}}}, {}, {}, {}});  // Composites + SyntMat

    // ================================================================
    // 2026-05-03: chain-step unlock additions to existing techs
    // (Option B from sweep design discussion). Existing techs absorb
    // good-unlocks rather than creating dedicated chain techs. Keeps the
    // tree at ~80 techs instead of 110+.
    // ================================================================
    // Pottery → Glass + Clay (kiln tech historically does both).
    {
        for (TechDef& t : techs) {
            if (t.id.value == 2) {  // Pottery
                t.unlockedGoods.push_back(76);  // GLASS
                t.unlockedGoods.push_back(47);  // CLAY
                break;
            }
        }
    }
    // Mining → Charcoal (kiln burning charcoal from wood).
    {
        for (TechDef& t : techs) {
            if (t.id.value == 0) {  // Mining
                t.unlockedGoods.push_back(79);  // CHARCOAL
                break;
            }
        }
    }
    // Bronze Working → Lumber (better tools = sawn lumber not just wood).
    {
        for (TechDef& t : techs) {
            if (t.id.value == 4) {  // Bronze Working
                t.unlockedGoods.push_back(62);  // LUMBER
                break;
            }
        }
    }
    // Ecology → Biofuel.
    {
        for (TechDef& t : techs) {
            if (t.id.value == 29) {  // Ecology
                t.unlockedGoods.push_back(81);  // BIOFUEL
                break;
            }
        }
    }

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
