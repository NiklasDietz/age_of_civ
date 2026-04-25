/**
 * @file Religion.cpp
 * @brief Religion system: faith accumulation, religious spread, belief bonuses.
 *
 * accumulateFaith and applyReligionBonuses migrated to GameState.
 * processReligiousSpread still uses ECS for GlobalReligionTracker (global data).
 */

#include "aoc/simulation/religion/Religion.hpp"
#include "aoc/simulation/city/CityComponent.hpp"
#include "aoc/simulation/city/District.hpp"
#include "aoc/simulation/city/Happiness.hpp"
#include "aoc/simulation/city/ProductionQueue.hpp"
#include "aoc/simulation/diplomacy/DiplomacyState.hpp"
#include "aoc/simulation/tech/TechTree.hpp"
#include "aoc/simulation/wonder/Wonder.hpp"
#include "aoc/game/GameState.hpp"
#include "aoc/game/Player.hpp"
#include "aoc/game/City.hpp"
#include "aoc/map/HexGrid.hpp"
#include "aoc/core/Log.hpp"

#include <algorithm>

namespace aoc::sim {

// ============================================================================
// Beliefs table (16 beliefs, 4 per type)
// ============================================================================

namespace {

constexpr std::array<BeliefDef, BELIEF_COUNT> BELIEFS = {{
    // {id, name, type, description, goldPerFollowerCity, sciencePerFollowerCity, amenityBonus, foodBonus, faithBonus, spreadStrength}
    // Founder beliefs (0-3)
    {0, "Tithe",                BeliefType::Founder,  "Gold from follower cities",       1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f},
    {1, "Church Property",      BeliefType::Founder,  "Gold and faith from cities",      0.5f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f},
    {2, "World Church",         BeliefType::Founder,  "More gold from cities",           2.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f},
    {3, "Papal Primacy",        BeliefType::Founder,  "Diplomatic influence",            0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f},
    // Follower beliefs (4-7)
    {4, "Choral Music",         BeliefType::Follower, "Amenities from religion",         0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f},
    {5, "Religious Community",  BeliefType::Follower, "Small amenity boost",             0.0f, 0.0f, 0.5f, 0.0f, 0.0f, 0.0f},
    {6, "Feed the World",       BeliefType::Follower, "Food bonus from shrines",         0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f},
    {7, "Zen Meditation",       BeliefType::Follower, "Large amenity boost",             0.0f, 0.0f, 1.5f, 0.0f, 0.0f, 0.0f},
    // Worship beliefs (8-11)
    {8, "Cathedral",            BeliefType::Worship,  "Culture worship building",        0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f},
    {9, "Mosque",               BeliefType::Worship,  "Faith worship building",          0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f},
    {10, "Pagoda",              BeliefType::Worship,  "Amenity worship building",        0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f},
    {11, "Synagogue",           BeliefType::Worship,  "Faith worship building",          0.0f, 0.0f, 0.0f, 0.0f, 2.0f, 0.0f},
    // Enhancer beliefs (12-15)
    {12, "Holy Order",          BeliefType::Enhancer, "Cheaper missionaries",            0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f},
    {13, "Missionary Zeal",     BeliefType::Enhancer, "Stronger missionaries",           0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.5f},
    {14, "Religious Texts",     BeliefType::Enhancer, "Faster passive spread",           0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.3f},
    {15, "Itinerant Preachers", BeliefType::Enhancer, "Wider spread range",              0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.2f},
}};

} // anonymous namespace

const std::array<BeliefDef, BELIEF_COUNT>& allBeliefs() {
    return BELIEFS;
}

// ============================================================================
// accumulateFaith - GameState native
// ============================================================================

void accumulateFaith(aoc::game::Player& player, const aoc::map::HexGrid& grid) {
    PlayerFaithComponent& playerFaith = player.faith();

    float faithGain = 0.0f;

    for (const std::unique_ptr<aoc::game::City>& city : player.cities()) {
        // Base faith income: every city produces 1 faith per turn regardless of buildings.
        // Without this floor, players never accumulate enough faith to found a pantheon
        // in the early game when tiles with faith yield are rare.
        faithGain += 1.0f;

        // Faith from worked tiles
        for (const aoc::hex::AxialCoord& tile : city->workedTiles()) {
            if (grid.isValid(tile)) {
                int32_t tileIdx = grid.toIndex(tile);
                aoc::map::TileYield yield = grid.tileYield(tileIdx);
                faithGain += static_cast<float>(yield.faith);
            }
        }

        // Faith from Holy Site district (+2 base, on top of the per-city 1)
        // plus grid-only adjacency bonus (B2). Mountains/forests/natural
        // wonders adjacent to the Holy Site add faith, mirroring the Civ 6
        // rule used in computeAdjacencyBonus.
        for (const aoc::sim::CityDistrictsComponent::PlacedDistrict& d
                : city->districts().districts) {
            if (d.type != DistrictType::HolySite) { continue; }
            faithGain += 2.0f;
            if (!grid.isValid(d.location)) { continue; }
            const std::array<aoc::hex::AxialCoord, 6> neighbors =
                aoc::hex::neighbors(d.location);
            int32_t adjMountains = 0;
            int32_t adjForests   = 0;
            int32_t adjWonders   = 0;
            for (const aoc::hex::AxialCoord& nbr : neighbors) {
                if (!grid.isValid(nbr)) { continue; }
                const int32_t nbrIdx = grid.toIndex(nbr);
                if (grid.terrain(nbrIdx) == aoc::map::TerrainType::Mountain) {
                    ++adjMountains;
                }
                if (grid.feature(nbrIdx) == aoc::map::FeatureType::Forest) {
                    ++adjForests;
                }
                if (grid.naturalWonder(nbrIdx) != aoc::map::NaturalWonderType::None) {
                    ++adjWonders;
                }
            }
            faithGain += static_cast<float>(adjMountains) * 1.0f;
            faithGain += static_cast<float>(adjForests) * 0.5f;
            faithGain += static_cast<float>(adjWonders) * 2.0f;
        }

        // Faith from buildings (Shrine/Temple/Cathedral).
        for (const aoc::sim::CityDistrictsComponent::PlacedDistrict& d
                : city->districts().districts) {
            for (const BuildingId& bid : d.buildings) {
                faithGain += static_cast<float>(buildingDef(bid).faithBonus);
            }
        }

        // Wonder faith bonus (H4.9): Stonehenge etc.
        // WP-A7: era-decay.
        for (const WonderId wid : city->wonders().wonders) {
            const WonderDef& wdef = wonderDef(wid);
            faithGain += wdef.effect.faithBonus
                       * wonderEraDecayFactor(wdef, player.era().currentEra);
        }
    }

    // Civilization ability: faith multiplier.
    faithGain *= aoc::sim::civDef(player.civId()).modifiers.faithMultiplier;

    playerFaith.faith += faithGain;
}

// ============================================================================
// processReligiousSpread - still uses ECS for GlobalReligionTracker
// ============================================================================

void processReligiousSpread(aoc::game::GameState& gameState,
                             const aoc::map::HexGrid& grid,
                             const DiplomacyManager* diplomacy) {
    // Audit 2026-04: SPREAD_RANGE was 3 (only adjacent + neighbour); pressure
    // saturated at neighbour cluster. Bumped to 5 + base 2.0 so the founder's
    // religion can radiate across 4-civ continents within the 1500t window.
    constexpr int32_t SPREAD_RANGE = 5;
    constexpr float BASE_PASSIVE_PRESSURE = 2.0f;

    // Gather city info from GameState. Owner is tracked so cross-owner spread
    // can be gated by diplomatic state (war blocks passive conversion) and the
    // founder's enhancer belief can be looked up.
    struct CityInfo {
        aoc::hex::AxialCoord location;
        ReligionId dominantReligion;
        bool hasHolySite;
        PlayerId owner;
        aoc::game::City* cityPtr;
    };
    std::vector<CityInfo> cities;

    for (const std::unique_ptr<aoc::game::Player>& player : gameState.players()) {
        for (const std::unique_ptr<aoc::game::City>& city : player->cities()) {
            ReligionId dominant = city->religion().dominantReligion();
            bool hasHolySite = city->districts().hasDistrict(DistrictType::HolySite);
            cities.push_back({city->location(), dominant, hasHolySite, city->owner(), city.get()});
        }
    }

    const GlobalReligionTracker& religions = gameState.religionTracker();
    const std::array<BeliefDef, BELIEF_COUNT>& beliefs = allBeliefs();

    // Apply passive pressure from cities with dominant religions
    for (const CityInfo& source : cities) {
        if (source.dominantReligion == NO_RELIGION) {
            continue;
        }

        float pressure = BASE_PASSIVE_PRESSURE;
        if (source.hasHolySite) {
            pressure *= 2.0f;
        }

        // Enhancer belief multiplier (e.g. Missionary Zeal x1.5, Religious Texts x1.3).
        // Looked up from the founding player's religion definition.
        if (source.dominantReligion < MAX_RELIGIONS) {
            const ReligionDef& rdef = religions.religions[source.dominantReligion];
            if (rdef.enhancerBelief < BELIEF_COUNT) {
                const float mult = beliefs[rdef.enhancerBelief].spreadStrength;
                if (mult > 0.0f) {
                    pressure *= mult;
                }
            }
        }

        for (const CityInfo& target : cities) {
            if (target.cityPtr == source.cityPtr) { continue; }
            int32_t dist = grid.distance(source.location, target.location);
            if (dist > SPREAD_RANGE) { continue; }

            // Cross-owner gate: hostile enemies don't passively adopt each
            // other's religion.  Same-owner spread is always allowed.
            if (source.owner != target.owner && diplomacy != nullptr
                && source.owner != INVALID_PLAYER && target.owner != INVALID_PLAYER
                && diplomacy->isAtWar(source.owner, target.owner)) {
                continue;
            }

            const ReligionId beforeDominant = target.cityPtr->religion().dominantReligion();
            target.cityPtr->religion().addPressure(source.dominantReligion, pressure);
            const ReligionId afterDominant = target.cityPtr->religion().dominantReligion();
            if (afterDominant != beforeDominant && afterDominant != NO_RELIGION) {
                const std::string_view afterName =
                    (afterDominant < MAX_RELIGIONS)
                        ? std::string_view(religions.religions[afterDominant].name)
                        : std::string_view("?");
                LOG_INFO("religion spread: city at (%d,%d) converted to '%.*s' (pressure from owner P%u)",
                         target.location.q, target.location.r,
                         static_cast<int>(afterName.size()), afterName.data(),
                         static_cast<unsigned>(source.owner));

                // WP-A2: converting a foreign-owned city to your religion
                // irritates the target's civ. -8 relation, decays 30 turns.
                // Only fires across distinct real players (skip city-states
                // and unowned free cities).
                if (diplomacy != nullptr
                    && target.owner != INVALID_PLAYER
                    && source.owner != INVALID_PLAYER
                    && target.owner != source.owner
                    && target.owner < aoc::sim::CITY_STATE_PLAYER_BASE
                    && source.owner < aoc::sim::CITY_STATE_PLAYER_BASE) {
                    aoc::sim::RelationModifier mod{};
                    mod.reason         = "Converted one of our cities";
                    mod.amount         = -8;
                    mod.turnsRemaining = 30;
                    const_cast<DiplomacyManager*>(diplomacy)->addModifier(
                        target.owner, source.owner, mod);
                }
            }
        }
    }
}

// ============================================================================
// applyReligionBonuses - GameState native (partially - needs GlobalReligionTracker)
// ============================================================================

void applyReligionBonuses(aoc::game::Player& player) {
    const PlayerFaithComponent& playerFaith = player.faith();
    if (playerFaith.foundedReligion == NO_RELIGION) {
        return;
    }

    // For now, apply a simplified bonus: each city with religion gets +1 faith
    // Full religion bonuses require GlobalReligionTracker which needs to move to GameState.
    // Per-city bonus: cities whose dominant religion matches the founder's
    // religion add to faith pool AND grant a small economic kickback.
    // WP-A1 synergy: religion bonds should meaningfully change production.
    // Per-city Gold delta: +1 gold per 3 citizens (spirit of tithe / alms).
    // Per-city Faith: +0.5 per turn (was the only effect previously).
    for (const std::unique_ptr<aoc::game::City>& city : player.cities()) {
        ReligionId dominant = city->religion().dominantReligion();
        if (dominant == playerFaith.foundedReligion) {
            player.faith().faith += 0.5f;
            const int32_t tithe = city->population() / 3;
            if (tithe > 0) {
                player.addGold(tithe);
            }
        }
    }
}

// ============================================================================
// rushBuildingWithFaith (WP-A1)
// ============================================================================

ErrorCode rushBuildingWithFaith(aoc::game::Player& player,
                                 aoc::game::City& city,
                                 int32_t currentTurn) {
    if (city.owner() != player.id()) {
        return ErrorCode::InvalidArgument;
    }

    ProductionQueueComponent& queue = city.production();
    if (queue.queue.empty()) {
        return ErrorCode::InvalidArgument;
    }
    ProductionQueueItem& head = queue.queue.front();
    if (head.type != ProductionItemType::Building) {
        return ErrorCode::InvalidArgument;
    }
    if (queue.lastFaithRushTurn == currentTurn) {
        return ErrorCode::InvalidArgument;
    }

    // Cost scales with remaining production cost (tier-proxy) × 0.5.
    const float remaining = std::max(0.0f, head.totalCost - head.progress);
    const float faithCost = std::max(20.0f, remaining * 0.5f);

    PlayerFaithComponent& playerFaith = player.faith();
    if (playerFaith.faith < faithCost) {
        return ErrorCode::InsufficientResources;
    }
    playerFaith.faith -= faithCost;
    head.progress = head.totalCost;
    queue.lastFaithRushTurn = currentTurn;
    LOG_INFO("Faith rush: player %u city %s completed %.*s for %.0f faith",
             static_cast<unsigned>(player.id()), city.name().c_str(),
             static_cast<int>(head.name.size()), head.name.c_str(),
             static_cast<double>(faithCost));
    return ErrorCode::Ok;
}

// ============================================================================
// processAIReligionFounding
// ============================================================================

void processAIReligionFounding(aoc::game::GameState& gameState) {
    GlobalReligionTracker& tracker = gameState.religionTracker();

    for (const std::unique_ptr<aoc::game::Player>& playerPtr : gameState.players()) {
        if (playerPtr == nullptr) { continue; }
        // Human players use the UI screen to found religions manually
        if (playerPtr->isHuman()) { continue; }

        PlayerFaithComponent& faith = playerPtr->faith();

        // Step 1: Found a pantheon once PANTHEON_FAITH_COST is reached
        if (!faith.hasPantheon && faith.faith >= PANTHEON_FAITH_COST) {
            faith.hasPantheon = true;
            faith.pantheonBelief = 4;  // "Choral Music" -- amenities (index 4 in BELIEFS)
            faith.faith -= PANTHEON_FAITH_COST;
            LOG_INFO("Player %u [Religion.cpp:processAIReligionFounding] founded pantheon "
                     "(faith remaining: %.1f)",
                     static_cast<unsigned>(playerPtr->id()),
                     static_cast<double>(faith.faith));
        }

        // Step 2: Found a religion once RELIGION_FAITH_COST is reached and slots remain
        if (faith.hasPantheon
                && faith.foundedReligion == NO_RELIGION
                && faith.faith >= RELIGION_FAITH_COST
                && tracker.canFoundReligion()) {
            // Pick religion name by cycling through the available names
            const std::string religionName(
                RELIGION_NAMES[tracker.religionsFoundedCount % RELIGION_NAMES.size()]);
            const ReligionId newId = tracker.foundReligion(religionName, playerPtr->id());
            faith.foundedReligion = newId;
            faith.faith -= RELIGION_FAITH_COST;

            // Seed initial pressure in the player's own cities
            for (const std::unique_ptr<aoc::game::City>& city : playerPtr->cities()) {
                city->religion().addPressure(newId, 5.0f);
            }

            LOG_INFO("Player %u [Religion.cpp:processAIReligionFounding] founded religion "
                     "'%s' (id %u, faith remaining: %.1f)",
                     static_cast<unsigned>(playerPtr->id()),
                     religionName.c_str(),
                     static_cast<unsigned>(newId),
                     static_cast<double>(faith.faith));
        }
    }
}

// ============================================================================
// Religion-vs-education science curve
// ============================================================================

namespace {

/// Devotion contribution per faith building / district state.  Values chosen
/// so that a city investing in the full faith chain (Holy Site + Shrine +
/// Temple + Cathedral) plus having a dominant religion scores 1 + 1 + 2 + 3 +
/// 1 = 8 devotion -- enough to meaningfully cancel a Campus with Library +
/// University (1 + 2 = 3 education) and still contribute to the science
/// penalty.  Players who only put a Shrine in their capital sit at devotion
/// 1-2, which is easily cancelled by a single Library.
constexpr float DEVOTION_SHRINE          = 1.0f;
constexpr float DEVOTION_TEMPLE          = 2.0f;
constexpr float DEVOTION_CATHEDRAL       = 3.0f;
constexpr float DEVOTION_HOLY_SITE       = 1.0f;
constexpr float DEVOTION_DOMINANT_FAITH  = 1.0f;

constexpr float EDUCATION_LIBRARY        = 1.0f;
constexpr float EDUCATION_UNIVERSITY     = 2.0f;
constexpr float EDUCATION_RESEARCH_LAB   = 3.0f;

constexpr BuildingId BUILDING_LIBRARY      = BuildingId{7};
constexpr BuildingId BUILDING_RESEARCH_LAB = BuildingId{12};
constexpr BuildingId BUILDING_UNIVERSITY   = BuildingId{19};
constexpr BuildingId BUILDING_SHRINE       = BuildingId{36};
constexpr BuildingId BUILDING_TEMPLE       = BuildingId{37};
constexpr BuildingId BUILDING_CATHEDRAL    = BuildingId{38};

} // anonymous namespace

float computeCityDevotion(const aoc::game::City& city) {
    float devotion = 0.0f;

    const CityDistrictsComponent& districts = city.districts();
    if (districts.hasDistrict(DistrictType::HolySite)) {
        devotion += DEVOTION_HOLY_SITE;
    }
    if (districts.hasBuilding(BUILDING_SHRINE))    { devotion += DEVOTION_SHRINE; }
    if (districts.hasBuilding(BUILDING_TEMPLE))    { devotion += DEVOTION_TEMPLE; }
    if (districts.hasBuilding(BUILDING_CATHEDRAL)) { devotion += DEVOTION_CATHEDRAL; }

    if (city.religion().dominantReligion() != NO_RELIGION) {
        devotion += DEVOTION_DOMINANT_FAITH;
    }

    return devotion;
}

float computeCityEducation(const aoc::game::City& city) {
    float education = 0.0f;

    const CityDistrictsComponent& districts = city.districts();
    if (districts.hasBuilding(BUILDING_LIBRARY))      { education += EDUCATION_LIBRARY; }
    if (districts.hasBuilding(BUILDING_UNIVERSITY))   { education += EDUCATION_UNIVERSITY; }
    if (districts.hasBuilding(BUILDING_RESEARCH_LAB)) { education += EDUCATION_RESEARCH_LAB; }

    return education;
}

EraId effectiveEraFromTech(const aoc::game::Player& player) {
    const PlayerTechComponent& pt = player.tech();
    const uint16_t total = techCount();
    uint8_t maxEra = 0;
    for (uint16_t ti = 0; ti < total; ++ti) {
        if (pt.hasResearched(TechId{ti})) {
            const uint8_t e = static_cast<uint8_t>(techDef(TechId{ti}).era.value);
            if (e > maxEra) { maxEra = e; }
        }
    }
    return EraId{maxEra};
}

int32_t countRenaissancePlusTechs(const aoc::game::Player& player) {
    const PlayerTechComponent& pt = player.tech();
    const uint16_t total = techCount();
    int32_t count = 0;
    for (uint16_t ti = 0; ti < total; ++ti) {
        if (pt.hasResearched(TechId{ti}) && techDef(TechId{ti}).era.value >= 3) {
            ++count;
        }
    }
    return count;
}

float religionScienceCoefficient(EraId era, int32_t techsResearchedRenaissancePlus) {
    float baseline;
    switch (era.value) {
        case 0:
        case 1:  baseline =  0.50f; break;   // Ancient/Classical: boon
        case 2:  baseline =  0.00f; break;   // Medieval: neutral
        case 3:  baseline = -0.30f; break;   // Renaissance: friction
        default: baseline = -0.70f; break;   // Industrial+: drag
    }

    // Each Renaissance-or-later tech adds -0.05 to the coefficient.  Clamped
    // so one civ sprinting through the entire tech tree cannot drive the
    // coefficient absurdly negative and instantly destroy its own science.
    if (techsResearchedRenaissancePlus > 0) {
        constexpr float PER_TECH_KICK = -0.05f;
        constexpr float MAX_KICK      = -1.50f;
        float kick = static_cast<float>(techsResearchedRenaissancePlus) * PER_TECH_KICK;
        if (kick < MAX_KICK) { kick = MAX_KICK; }
        baseline += kick;
    }

    return baseline;
}

float religionLoyaltyCoefficient(EraId era) {
    // Ancient through Medieval: religion is the state-stabilising force.
    // Renaissance onward: secular institutions replace it, so Devotion no
    // longer props up loyalty.
    return (era.value <= 2) ? 0.30f : 0.0f;
}

} // namespace aoc::sim
