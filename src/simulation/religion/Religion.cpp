/**
 * @file Religion.cpp
 * @brief Religion system: faith accumulation, religious spread, belief bonuses.
 *
 * accumulateFaith and applyReligionBonuses migrated to GameState.
 * processReligiousSpread still uses ECS for GlobalReligionTracker (global data).
 */

#include "aoc/simulation/religion/Religion.hpp"
#include "aoc/simulation/city/DistrictAdjacency.hpp"
#include "aoc/simulation/government/Government.hpp"
#include "aoc/simulation/government/GovernmentComponent.hpp"
#include "aoc/simulation/city/CityComponent.hpp"
#include "aoc/simulation/city/District.hpp"
#include "aoc/simulation/city/Happiness.hpp"
#include "aoc/simulation/city/ProductionQueue.hpp"
#include "aoc/simulation/diplomacy/DiplomacyState.hpp"
#include "aoc/simulation/ai/LeaderPersonality.hpp"
#include "aoc/simulation/tech/TechTree.hpp"
#include "aoc/simulation/unit/UnitTypes.hpp"
#include "aoc/simulation/wonder/Wonder.hpp"
#include "aoc/game/GameState.hpp"
#include "aoc/game/Unit.hpp"
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
    // {id, name, type, description, goldPerFollowerCity, sciencePerFollowerCity, amenityBonus,
    // foodBonus, faithBonus, spreadStrength}
    // Founder beliefs (0-3)
    {0, "Tithe", BeliefType::Founder, "Gold from follower cities", 1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
     0.0f},
    {1, "Church Property", BeliefType::Founder, "Gold and faith from cities", 0.5f, 0.0f, 0.0f,
     0.0f, 1.0f, 0.0f},
    {2, "World Church", BeliefType::Founder, "More gold from cities", 2.0f, 0.0f, 0.0f, 0.0f, 0.0f,
     0.0f},
    // Papal Primacy, Cathedral and Holy Order carried no effect values at all:
    // three of the sixteen beliefs were a name and a sentence. Their flavour
    // says what they should do, so each now pays in the nearest currency the
    // belief system already has.
    {3, "Papal Primacy", BeliefType::Founder, "Gold from every city that follows you",
     2.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f},
    // Follower beliefs (4-7)
    {4, "Choral Music", BeliefType::Follower, "Amenities from religion", 0.0f, 0.0f, 1.0f, 0.0f,
     0.0f, 0.0f},
    {5, "Religious Community", BeliefType::Follower, "Small amenity boost", 0.0f, 0.0f, 0.5f, 0.0f,
     0.0f, 0.0f},
    {6, "Feed the World", BeliefType::Follower, "Food bonus from shrines", 0.0f, 0.0f, 0.0f, 1.0f,
     0.0f, 0.0f},
    {7, "Zen Meditation", BeliefType::Follower, "Large amenity boost", 0.0f, 0.0f, 1.5f, 0.0f, 0.0f,
     0.0f},
    // Worship beliefs (8-11)
    {8, "Cathedral", BeliefType::Worship, "A grand church: contentment and faith",
     0.0f, 0.0f, 1.0f, 0.0f, 1.0f, 0.0f},
    {9, "Mosque", BeliefType::Worship, "Faith worship building", 0.0f, 0.0f, 0.0f, 0.0f, 1.0f,
     0.0f},
    {10, "Pagoda", BeliefType::Worship, "Amenity worship building", 0.0f, 0.0f, 1.0f, 0.0f, 0.0f,
     0.0f},
    {11, "Synagogue", BeliefType::Worship, "Faith worship building", 0.0f, 0.0f, 0.0f, 0.0f, 2.0f,
     0.0f},
    // Enhancer beliefs (12-15)
    {12, "Holy Order", BeliefType::Enhancer, "Missionaries who travel further",
     0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.5f},
    {13, "Missionary Zeal", BeliefType::Enhancer, "Stronger missionaries", 0.0f, 0.0f, 0.0f, 0.0f,
     0.0f, 1.5f},
    {14, "Religious Texts", BeliefType::Enhancer, "Faster passive spread", 0.0f, 0.0f, 0.0f, 0.0f,
     0.0f, 1.3f},
    {15, "Itinerant Preachers", BeliefType::Enhancer, "Wider spread range", 0.0f, 0.0f, 0.0f, 0.0f,
     0.0f, 1.2f},

    // --- Second rank (16-39) ---
    // Sixteen beliefs meant four per type: with several religions in a game the
    // choice was picked over before the last civ founded anything, and two
    // faiths often ran identical doctrine. Twenty-four more spread the choice
    // out. Every one carries an effect the simulation reads; the belief-effects
    // test fails on any row that does not.
    // Founder (16-23)
    {16, "Crusade", BeliefType::Founder, "Gold from the faithful abroad", 1.5f, 0.0f, 0.0f, 0.0f,
     0.0f, 0.0f},
    {17, "Scholasticism", BeliefType::Founder, "Science from follower cities", 0.0f, 1.5f, 0.0f,
     0.0f, 0.0f, 0.0f},
    {18, "Monastic Isolation", BeliefType::Founder, "Quiet study in every follower city", 0.0f,
     2.0f, 0.0f, 0.0f, 0.0f, 0.0f},
    {19, "Pilgrimage Routes", BeliefType::Founder, "Gold and faith from pilgrims", 1.0f, 0.0f, 0.0f,
     0.0f, 1.0f, 0.0f},
    {20, "Tithe of Learning", BeliefType::Founder, "Gold and science together", 0.8f, 0.8f, 0.0f,
     0.0f, 0.0f, 0.0f},
    {21, "Almsgiving", BeliefType::Founder, "Faith from every follower city", 0.0f, 0.0f, 0.0f,
     0.0f, 1.5f, 0.0f},
    {22, "Sacred Treasury", BeliefType::Founder, "A rich church", 2.5f, 0.0f, 0.0f, 0.0f, 0.0f,
     0.0f},
    {23, "Illuminated Manuscripts", BeliefType::Founder, "Copied knowledge", 0.0f, 1.0f, 0.0f, 0.0f,
     0.5f, 0.0f},
    // Follower (24-31)
    {24, "Harvest Festival", BeliefType::Follower, "Food from the faith", 0.0f, 0.0f, 0.0f, 1.5f,
     0.0f, 0.0f},
    {25, "Sacred Groves", BeliefType::Follower, "Contentment among the trees", 0.0f, 0.0f, 1.2f,
     0.0f, 0.0f, 0.0f},
    {26, "Communal Kitchens", BeliefType::Follower, "Bread shared out", 0.0f, 0.0f, 0.5f, 1.0f,
     0.0f, 0.0f},
    {27, "Ancestor Veneration", BeliefType::Follower, "Faith at every hearth", 0.0f, 0.0f, 0.0f,
     0.0f, 1.2f, 0.0f},
    {28, "Fasting and Feast", BeliefType::Follower, "Lean months, glad ones", 0.0f, 0.0f, 1.0f,
     0.5f, 0.0f, 0.0f},
    {29, "Sabbath Rest", BeliefType::Follower, "A day set aside", 0.0f, 0.0f, 2.0f, 0.0f, 0.0f,
     0.0f},
    {30, "Hospitallers", BeliefType::Follower, "Care for the sick", 0.0f, 0.0f, 0.8f, 0.8f, 0.0f,
     0.0f},
    {31, "Divine Inspiration", BeliefType::Follower, "Faith and gladness", 0.0f, 0.0f, 0.6f, 0.0f,
     1.0f, 0.0f},
    // Worship (32-35)
    {32, "Stupa", BeliefType::Worship, "A dome of quiet", 0.0f, 0.0f, 1.2f, 0.0f, 0.5f, 0.0f},
    {33, "Wat", BeliefType::Worship, "A temple complex", 0.0f, 0.0f, 0.8f, 0.0f, 1.2f, 0.0f},
    {34, "Gurdwara", BeliefType::Worship, "A kitchen and a hall", 0.0f, 0.0f, 0.5f, 1.0f, 0.5f,
     0.0f},
    {35, "Meeting House", BeliefType::Worship, "Plain walls, full benches", 0.0f, 0.0f, 1.0f, 0.5f,
     0.0f, 0.0f},
    // Enhancer (36-39)
    {36, "Printed Sermons", BeliefType::Enhancer, "The word travels on paper", 0.0f, 0.0f, 0.0f,
     0.0f, 0.0f, 1.6f},
    {37, "Martyrdom", BeliefType::Enhancer, "Blood is seed", 0.0f, 0.0f, 0.0f, 0.0f, 0.5f, 1.4f},
    {38, "Trade Missions", BeliefType::Enhancer, "Faith follows the caravans", 0.0f, 0.0f, 0.0f,
     0.0f, 0.0f, 1.35f},
    {39, "Charismatic Preachers", BeliefType::Enhancer, "Crowds gather", 0.0f, 0.0f, 0.0f, 0.0f,
     0.0f, 1.8f},
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

    const ReligionId myRel = playerFaith.foundedReligion;
    aoc::sim::DistrictIndex districtIndex;
    districtIndex.build(player);
    for (const std::unique_ptr<aoc::game::City>& city : player.cities()) {
        // Religious-spread asymmetry: a city whose dominant religion no
        // longer matches our founded religion contributes only 30% of its
        // faith (residual sympathizers). Cities pre-pantheon (myRel == NO)
        // still get full credit so early-game accumulation works.
        float cityFaithMult = 1.0f;
        if (myRel != NO_RELIGION) {
            const ReligionId cityRel = city->religion().dominantReligion();
            if (cityRel != NO_RELIGION && cityRel != myRel) {
                cityFaithMult = 0.30f;
            }
        }
        const float faithBeforeCity = faithGain;
        faithGain                   = 0.0f;

        // Base faith income: every city produces 1 faith per turn regardless of buildings.
        // Without this floor, players never accumulate enough faith to found a pantheon
        // in the early game when tiles with faith yield are rare.
        faithGain += 1.0f;

        // Faith from worked tiles
        for (const aoc::hex::AxialCoord& tile : city->workedTiles()) {
            if (grid.isValid(tile)) {
                int32_t tileIdx           = grid.toIndex(tile);
                aoc::map::TileYield yield = grid.tileYield(tileIdx);
                faithGain += static_cast<float>(yield.faith);
            }
        }

        // Faith from Holy Site district (+2 base, on top of the per-city 1).
        for (const aoc::sim::CityDistrictsComponent::PlacedDistrict& d :
             city->districts().districts) {
            if (d.type != DistrictType::HolySite) {
                continue;
            }
            faithGain += 2.0f;
        }

        // District adjacency faith, through the one shared path: mountains,
        // forests and natural wonders beside a Holy Site.
        faithGain += aoc::sim::cityAdjacencyYields(grid, districtIndex, *city).faith;

        // Faith from buildings (Shrine/Temple/Cathedral).
        for (const aoc::sim::CityDistrictsComponent::PlacedDistrict& d :
             city->districts().districts) {
            for (const BuildingId& bid : d.buildings) {
                faithGain += static_cast<float>(buildingDef(bid).faithBonus);
            }
        }

        // Wonder faith bonus (H4.9): Stonehenge etc.
        // WP-A7: era-decay.
        for (const WonderId wid : city->wonders().wonders) {
            const WonderDef& wdef = wonderDef(wid);
            faithGain +=
                wdef.effect.faithBonus * wonderEraDecayFactor(wdef, player.era().currentEra);
        }

        // Apply per-city religious match multiplier and re-add prior cities.
        faithGain = faithBeforeCity + faithGain * cityFaithMult;
    }

    // Civilization ability: faith multiplier.
    faithGain *= aoc::sim::civDef(player.civId()).modifiers.faithMultiplier;

    // Government faith multiplier (policy cards)
    {
        const GovernmentModifiers gov = computeGovernmentModifiers(player.government());
        faithGain *= gov.faithMultiplier;
    }

    // Civ ability: +N faith per active trade route. Flat, after the multipliers,
    // like the science and culture siblings. Authored for three civs and never
    // read until 2026-09-04.
    {
        const int32_t perRoute = aoc::sim::civDef(player.civId()).modifiers.faithFromTradeRoute;
        if (perRoute > 0) {
            faithGain += static_cast<float>(player.activeTradeRouteCount() * perRoute);
        }
    }

    playerFaith.faith += faithGain;
}

// ============================================================================
// processReligiousSpread - still uses ECS for GlobalReligionTracker
// ============================================================================

void processReligiousSpread(aoc::game::GameState& gameState, const aoc::map::HexGrid& grid,
                            const DiplomacyManager* diplomacy) {
    // Audit 2026-04: SPREAD_RANGE was 3, then 5; pressure rose to 2.0. Now 7
    // + base 3.0 so dominant religion saturates rivals within 800t. Religion
    // wins were 1-3/12; pushing to 3-4/12.
    constexpr int32_t SPREAD_RANGE        = 7;
    constexpr float BASE_PASSIVE_PRESSURE = 3.0f;

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
            bool hasHolySite    = city->districts().hasDistrict(DistrictType::HolySite);
            cities.push_back({city->location(), dominant, hasHolySite, city->owner(), city.get()});
        }
    }

    const GlobalReligionTracker& religions             = gameState.religionTracker();
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
        // Enhancer belief: the whole point of the type is to spread further and
        // faster, and until 2026-09-07 `spreadStrength` was a number the screen
        // printed and nothing else ever read.
        if (source.dominantReligion != NO_RELIGION
            && source.dominantReligion < gameState.religionTracker().religionsFoundedCount) {
            const ReligionDef& faith =
                gameState.religionTracker().religions[source.dominantReligion];
            if (faith.enhancerBelief < BELIEF_COUNT) {
                const float strength = allBeliefs()[faith.enhancerBelief].spreadStrength;
                if (strength > 0.0f) { pressure *= strength; }
            }
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
            if (target.cityPtr == source.cityPtr) {
                continue;
            }
            int32_t dist = grid.distance(source.location, target.location);
            if (dist > SPREAD_RANGE) {
                continue;
            }

            // Cross-owner gate: hostile enemies don't passively adopt each
            // other's religion.  Same-owner spread is always allowed.
            if (source.owner != target.owner && diplomacy != nullptr &&
                source.owner != INVALID_PLAYER && target.owner != INVALID_PLAYER &&
                diplomacy->isAtWar(source.owner, target.owner)) {
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
                LOG_INFO("religion spread: city at (%d,%d) converted to '%.*s' (pressure from "
                         "owner P%u)",
                         target.location.q, target.location.r, static_cast<int>(afterName.size()),
                         afterName.data(), static_cast<unsigned>(source.owner));

                // WP-A2: converting a foreign-owned city to your religion
                // irritates the target's civ. -8 relation, decays 30 turns.
                // Only fires across distinct real players (skip city-states
                // and unowned free cities).
                if (diplomacy != nullptr && target.owner != INVALID_PLAYER &&
                    source.owner != INVALID_PLAYER && target.owner != source.owner &&
                    target.owner < aoc::sim::CITY_STATE_PLAYER_BASE &&
                    source.owner < aoc::sim::CITY_STATE_PLAYER_BASE) {
                    aoc::sim::RelationModifier mod{};
                    mod.reason         = "Converted one of our cities";
                    mod.amount         = -8;
                    mod.turnsRemaining = 30;
                    const_cast<DiplomacyManager*>(diplomacy)->addModifier(target.owner,
                                                                          source.owner, mod);
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

ErrorCode rushBuildingWithFaith(aoc::game::Player& player, aoc::game::City& city,
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
    head.progress           = head.totalCost;
    queue.lastFaithRushTurn = currentTurn;
    LOG_INFO("Faith rush: player %u city %s completed %.*s for %.0f faith",
             static_cast<unsigned>(player.id()), city.name().c_str(),
             static_cast<int>(head.name.size()), head.name.c_str(), static_cast<double>(faithCost));
    return ErrorCode::Ok;
}

// ============================================================================
// processAIReligionFounding
// ============================================================================

uint8_t firstFreeBelief(const aoc::game::GameState& gameState, BeliefType type) {
    const GlobalReligionTracker& tracker = gameState.religionTracker();
    for (const BeliefDef& belief : BELIEFS) {
        if (belief.type != type) {
            continue;
        }
        bool taken = false;
        for (uint8_t r = 0; r < tracker.religionsFoundedCount && !taken; ++r) {
            const ReligionDef& def = tracker.religions[r];
            taken = def.founderBelief == belief.id || def.followerBelief == belief.id ||
                    def.worshipBelief == belief.id || def.enhancerBelief == belief.id;
        }
        for (const std::unique_ptr<aoc::game::Player>& other : gameState.players()) {
            if (other->faith().hasPantheon && other->faith().pantheonBelief == belief.id) {
                taken = true;
            }
        }
        if (!taken) {
            return belief.id;
        }
    }
    return 255;
}

bool beliefIsFree(const aoc::game::GameState& gameState, uint8_t belief, BeliefType type) {
    if (belief >= BELIEF_COUNT || BELIEFS[belief].type != type) {
        return false;
    }
    const GlobalReligionTracker& tracker = gameState.religionTracker();
    for (uint8_t r = 0; r < tracker.religionsFoundedCount; ++r) {
        const ReligionDef& def = tracker.religions[r];
        if (def.founderBelief == belief || def.followerBelief == belief ||
            def.worshipBelief == belief || def.enhancerBelief == belief) {
            return false;
        }
    }
    for (const std::unique_ptr<aoc::game::Player>& other : gameState.players()) {
        if (other->faith().hasPantheon && other->faith().pantheonBelief == belief) {
            return false;
        }
    }
    return true;
}

namespace {

void foundPantheonWith(aoc::game::Player& gsPlayer, uint8_t belief) {
    PlayerFaithComponent& faith = gsPlayer.faith();
    faith.hasPantheon           = true;
    faith.pantheonBelief        = belief;
    faith.faith -= PANTHEON_FAITH_COST;
    LOG_INFO("Player %u founded pantheon with belief %.*s (faith remaining: %.1f)",
             static_cast<unsigned>(gsPlayer.id()), static_cast<int>(BELIEFS[belief].name.size()),
             BELIEFS[belief].name.data(), static_cast<double>(faith.faith));
}

} // namespace

bool foundPantheonFor(aoc::game::GameState& gameState, PlayerId player) {
    aoc::game::Player* gsPlayer = gameState.player(player);
    if (gsPlayer == nullptr || gsPlayer->faith().hasPantheon ||
        gsPlayer->faith().faith < PANTHEON_FAITH_COST) {
        return false;
    }
    uint8_t belief = firstFreeBelief(gameState, BeliefType::Follower);
    if (belief == 255) {
        belief = 4; // every follower belief is claimed: share the first one
    }
    foundPantheonWith(*gsPlayer, belief);
    return true;
}

ErrorCode requestFoundPantheon(aoc::game::GameState& gameState, PlayerId player, uint8_t belief) {
    aoc::game::Player* gsPlayer = gameState.player(player);
    if (gsPlayer == nullptr || !beliefIsFree(gameState, belief, BeliefType::Follower)) {
        return ErrorCode::InvalidArgument;
    }
    if (gsPlayer->faith().hasPantheon) {
        return ErrorCode::InvalidState;
    }
    if (gsPlayer->faith().faith < PANTHEON_FAITH_COST) {
        return ErrorCode::InsufficientResources;
    }
    foundPantheonWith(*gsPlayer, belief);
    return ErrorCode::Ok;
}

/// The free belief of `type` this leader gets most from, or `fallback` when the
/// type is exhausted. Weighted by the leader's own priorities, so a warmonger
/// and a merchant prince do not converge on the same doctrine.
[[nodiscard]] uint8_t bestFreeBelief(const aoc::game::GameState& gameState,
                                     const aoc::game::Player& player, BeliefType type,
                                     uint8_t fallback) {
    const aoc::sim::LeaderBehavior& beh = leaderPersonality(player.civId()).behavior;
    const std::array<BeliefDef, BELIEF_COUNT>& all = allBeliefs();

    uint8_t best      = 255;
    float   bestScore = -1.0f;
    for (uint8_t i = 0; i < BELIEF_COUNT; ++i) {
        const BeliefDef& b = all[i];
        if (b.type != type || !beliefIsFree(gameState, i, type)) { continue; }
        // Each effect is worth what this leader cares about. Faith and spread
        // both serve religion, so both key off religiousZeal.
        const float score = b.goldPerFollowerCity    * beh.economicFocus
                          + b.sciencePerFollowerCity * beh.scienceFocus
                          + b.amenityBonus           * beh.cultureFocus
                          + b.foodBonus              * beh.expansionism
                          + b.faithBonus             * beh.religiousZeal
                          + b.spreadStrength         * beh.religiousZeal;
        if (score > bestScore) {
            bestScore = score;
            best      = i;
        }
    }
    return (best == 255) ? fallback : best;
}

ReligionId foundReligionFor(aoc::game::GameState& gameState, PlayerId player) {
    aoc::game::Player* gsPlayer    = gameState.player(player);
    GlobalReligionTracker& tracker = gameState.religionTracker();
    if (gsPlayer == nullptr || !gsPlayer->faith().hasPantheon ||
        gsPlayer->faith().foundedReligion != NO_RELIGION ||
        gsPlayer->faith().faith < RELIGION_FAITH_COST || !tracker.canFoundReligion()) {
        return NO_RELIGION;
    }
    // Pick the beliefs before founding so the new religion does not block itself.
    // Scored, not first-come: `firstFreeBelief` meant every civ took whatever
    // sat lowest in the table, so with forty beliefs to choose from the AI
    // reliably founded the same doctrine as everyone before it.
    const uint8_t founder  = bestFreeBelief(gameState, *gsPlayer, BeliefType::Founder, 0);
    const uint8_t worship  = bestFreeBelief(gameState, *gsPlayer, BeliefType::Worship, 8);
    const uint8_t enhancer = bestFreeBelief(gameState, *gsPlayer, BeliefType::Enhancer, 13);
    return foundReligionWith(gameState, *gsPlayer, founder, worship, enhancer);
}

ErrorCode requestFoundReligion(aoc::game::GameState& gameState, PlayerId player, uint8_t founder,
                               uint8_t worship, uint8_t enhancer, ReligionId* outId) {
    aoc::game::Player* gsPlayer    = gameState.player(player);
    GlobalReligionTracker& tracker = gameState.religionTracker();
    if (gsPlayer == nullptr || !beliefIsFree(gameState, founder, BeliefType::Founder) ||
        !beliefIsFree(gameState, worship, BeliefType::Worship) ||
        !beliefIsFree(gameState, enhancer, BeliefType::Enhancer)) {
        return ErrorCode::InvalidArgument;
    }
    if (!gsPlayer->faith().hasPantheon || gsPlayer->faith().foundedReligion != NO_RELIGION ||
        !tracker.canFoundReligion()) {
        return ErrorCode::InvalidState;
    }
    if (gsPlayer->faith().faith < RELIGION_FAITH_COST) {
        return ErrorCode::InsufficientResources;
    }
    const ReligionId id = foundReligionWith(gameState, *gsPlayer, founder, worship, enhancer);
    if (outId != nullptr) {
        *outId = id;
    }
    return ErrorCode::Ok;
}

ReligionId foundReligionWith(aoc::game::GameState& gameState, aoc::game::Player& owner,
                             uint8_t founder, uint8_t worship, uint8_t enhancer) {
    aoc::game::Player* gsPlayer    = &owner;
    const PlayerId player          = owner.id();
    GlobalReligionTracker& tracker = gameState.religionTracker();
    const std::string religionName(
        RELIGION_NAMES[tracker.religionsFoundedCount % RELIGION_NAMES.size()]);
    const ReligionId newId = tracker.foundReligion(religionName, player);
    ReligionDef& def       = tracker.religions[newId];
    def.founderBelief      = founder;
    def.followerBelief     = gsPlayer->faith().pantheonBelief;
    def.worshipBelief      = worship;
    def.enhancerBelief     = enhancer;

    PlayerFaithComponent& faith = gsPlayer->faith();
    faith.foundedReligion       = newId;
    faith.faith -= RELIGION_FAITH_COST;

    // Seed pressure in the founder's own cities so the religion exists on the map.
    // The faith takes a seat: its first city becomes the holy city, which is
    // what a rival can later take from it.
    for (const std::unique_ptr<aoc::game::City>& city : gsPlayer->cities()) {
        if (city != nullptr) {
            def.holyCity    = city->location();
            def.hasHolyCity = true;
            break;
        }
    }

    for (const std::unique_ptr<aoc::game::City>& city : gsPlayer->cities()) {
        city->religion().addPressure(newId, 5.0f);
    }
    LOG_INFO("Player %u founded religion '%s' (id %u, beliefs %u/%u/%u/%u, faith remaining: %.1f)",
             static_cast<unsigned>(player), religionName.c_str(), static_cast<unsigned>(newId),
             static_cast<unsigned>(founder), static_cast<unsigned>(def.followerBelief),
             static_cast<unsigned>(worship), static_cast<unsigned>(enhancer),
             static_cast<double>(faith.faith));
    return newId;
}

void processAIReligionFounding(aoc::game::GameState& gameState) {
    for (const std::unique_ptr<aoc::game::Player>& playerPtr : gameState.players()) {
        if (playerPtr == nullptr) {
            continue;
        }
        // Human players found through the Religion screen (same two functions).
        if (playerPtr->isHuman()) {
            continue;
        }
        static_cast<void>(foundPantheonFor(gameState, playerPtr->id()));
        static_cast<void>(foundReligionFor(gameState, playerPtr->id()));
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
constexpr float DEVOTION_SHRINE         = 1.0f;
constexpr float DEVOTION_TEMPLE         = 2.0f;
constexpr float DEVOTION_CATHEDRAL      = 3.0f;
constexpr float DEVOTION_HOLY_SITE      = 1.0f;
constexpr float DEVOTION_DOMINANT_FAITH = 1.0f;

constexpr float EDUCATION_LIBRARY      = 1.0f;
constexpr float EDUCATION_UNIVERSITY   = 2.0f;
constexpr float EDUCATION_RESEARCH_LAB = 3.0f;

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
    if (districts.hasBuilding(BUILDING_SHRINE)) {
        devotion += DEVOTION_SHRINE;
    }
    if (districts.hasBuilding(BUILDING_TEMPLE)) {
        devotion += DEVOTION_TEMPLE;
    }
    if (districts.hasBuilding(BUILDING_CATHEDRAL)) {
        devotion += DEVOTION_CATHEDRAL;
    }

    if (city.religion().dominantReligion() != NO_RELIGION) {
        devotion += DEVOTION_DOMINANT_FAITH;
    }

    return devotion;
}

float computeCityEducation(const aoc::game::City& city) {
    float education = 0.0f;

    const CityDistrictsComponent& districts = city.districts();
    if (districts.hasBuilding(BUILDING_LIBRARY)) {
        education += EDUCATION_LIBRARY;
    }
    if (districts.hasBuilding(BUILDING_UNIVERSITY)) {
        education += EDUCATION_UNIVERSITY;
    }
    if (districts.hasBuilding(BUILDING_RESEARCH_LAB)) {
        education += EDUCATION_RESEARCH_LAB;
    }

    return education;
}

EraId effectiveEraFromTech(const aoc::game::Player& player) {
    const PlayerTechComponent& pt = player.tech();
    const uint16_t total          = techCount();
    uint8_t maxEra                = 0;
    for (uint16_t ti = 0; ti < total; ++ti) {
        if (pt.hasResearched(TechId{ti})) {
            const uint8_t e = static_cast<uint8_t>(techDef(TechId{ti}).era.value);
            if (e > maxEra) {
                maxEra = e;
            }
        }
    }
    return EraId{maxEra};
}

int32_t countRenaissancePlusTechs(const aoc::game::Player& player) {
    const PlayerTechComponent& pt = player.tech();
    const uint16_t total          = techCount();
    int32_t count                 = 0;
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
    case 1:
        baseline = 0.50f;
        break; // Ancient/Classical: boon
    case 2:
        baseline = 0.00f;
        break; // Medieval: neutral
    case 3:
        baseline = -0.30f;
        break; // Renaissance: friction
    default:
        baseline = -0.70f;
        break; // Industrial+: drag
    }

    // Each Renaissance-or-later tech adds -0.05 to the coefficient.  Clamped
    // so one civ sprinting through the entire tech tree cannot drive the
    // coefficient absurdly negative and instantly destroy its own science.
    if (techsResearchedRenaissancePlus > 0) {
        constexpr float PER_TECH_KICK = -0.05f;
        constexpr float MAX_KICK      = -1.50f;
        float kick = static_cast<float>(techsResearchedRenaissancePlus) * PER_TECH_KICK;
        if (kick < MAX_KICK) {
            kick = MAX_KICK;
        }
        baseline += kick;
    }

    return baseline;
}

float religionLoyaltyCoefficient(EraId era) {
    // Ancient through Medieval: religion is the state-stabilising force.
    // Renaissance onward: secular institutions take over, so its grip weakens
    // -- but it does not vanish. This returned exactly 0.0f from era 3, which
    // switched religion's hold on an empire off at the very point empires grow
    // large enough to need holding, and left the whole devotion-to-loyalty
    // path inert for most of a long game.
    return (era.value <= 2) ? 0.30f : 0.12f;
}

float religionLoyaltyAlignment(const aoc::game::City& city, const aoc::game::Player& owner) {
    const ReligionId cityFaith = city.religion().dominantReligion();
    if (cityFaith == NO_RELIGION) {
        return 0.0f; // nothing to pull either way
    }
    const ReligionId ownerFaith = owner.faith().foundedReligion;
    if (ownerFaith != NO_RELIGION && cityFaith == ownerFaith) {
        return 1.0f; // shared faith holds the city
    }
    // Someone else's church. If the owner has no religion of their own this is
    // still a rival institution with the citizens' allegiance.
    return -1.0f;
}

void processHolyCityAndDecay(aoc::game::GameState& gameState) {
    const GlobalReligionTracker& tracker = gameState.religionTracker();

    for (const std::unique_ptr<aoc::game::Player>& player : gameState.players()) {
        if (player == nullptr) { continue; }
        for (const std::unique_ptr<aoc::game::City>& city : player->cities()) {
            if (city == nullptr) { continue; }
            CityReligionComponent& rel = city->religion();

            // Everything fades a little. Without this, pressure only ever
            // climbed: a faith that reached a city once held it forever.
            for (uint8_t r = 0; r < MAX_RELIGIONS; ++r) {
                if (rel.pressure[r] <= 0.0f) { continue; }
                rel.pressure[r] *= (1.0f - PRESSURE_DECAY_PER_TURN);
                if (rel.pressure[r] < PRESSURE_FLOOR) { rel.pressure[r] = 0.0f; }
            }

            // A holy city keeps its own faith burning, for whoever holds it.
            for (uint8_t r = 0; r < tracker.religionsFoundedCount && r < MAX_RELIGIONS; ++r) {
                const ReligionDef& faith = tracker.religions[r];
                if (faith.hasHolyCity && faith.holyCity == city->location()) {
                    rel.addPressure(r, HOLY_CITY_PRESSURE);
                }
            }
        }
    }
}

void processFounderBeliefs(aoc::game::GameState& gameState) {
    const GlobalReligionTracker& tracker = gameState.religionTracker();
    if (tracker.religionsFoundedCount == 0) {
        return;
    }

    // Count the cities each religion holds, across the whole world: a founder
    // is paid for reach, including reach into rival empires.
    std::array<int32_t, MAX_RELIGIONS> followerCities{};
    for (const std::unique_ptr<aoc::game::Player>& player : gameState.players()) {
        if (player == nullptr) { continue; }
        for (const std::unique_ptr<aoc::game::City>& city : player->cities()) {
            if (city == nullptr) { continue; }
            const ReligionId dominant = city->religion().dominantReligion();
            if (dominant != NO_RELIGION && dominant < MAX_RELIGIONS) {
                ++followerCities[dominant];
            }
        }
    }

    for (uint8_t r = 0; r < tracker.religionsFoundedCount && r < MAX_RELIGIONS; ++r) {
        const ReligionDef& faith = tracker.religions[r];
        if (faith.founderBelief >= BELIEF_COUNT || followerCities[r] == 0) { continue; }
        aoc::game::Player* founder = gameState.player(faith.founder);
        if (founder == nullptr) { continue; }

        const BeliefDef& belief = allBeliefs()[faith.founderBelief];
        const float cities      = static_cast<float>(followerCities[r]);
        if (belief.goldPerFollowerCity > 0.0f) {
            founder->monetary().treasury +=
                static_cast<CurrencyAmount>(belief.goldPerFollowerCity * cities);
        }
        if (belief.sciencePerFollowerCity > 0.0f) {
            founder->tech().researchProgress += belief.sciencePerFollowerCity * cities;
        }
    }
}

float theologicalStrength(const aoc::game::Unit& unit) {
    const UnitTypeDef& def = unit.typeDef();
    if (def.unitClass != UnitClass::Religious) {
        return 0.0f;
    }
    // The Apostle is the one built to argue: it is the only religious unit with
    // a combat strength in the table. A Missionary carries the word and cannot
    // defend it; an Inquisitor is formidable at home and poor abroad, which is
    // what the halving below expresses.
    const float base = static_cast<float>(def.combatStrength);
    if (base <= 0.0f) {
        return 4.0f; // a Missionary is not defenceless, only feeble
    }
    return base;
}

ErrorCode requestTheologicalCombat(aoc::game::GameState& gameState, aoc::Random& rng,
                                   const aoc::map::HexGrid& grid, PlayerId player,
                                   hex::AxialCoord from, hex::AxialCoord to) {
    aoc::game::Player* owner = gameState.player(player);
    if (owner == nullptr) {
        return ErrorCode::InvalidArgument;
    }
    aoc::game::Unit* attacker = owner->unitAt(from);
    if (attacker == nullptr || !grid.isValid(to)) {
        return ErrorCode::InvalidArgument;
    }
    if (attacker->typeDef().unitClass != UnitClass::Religious) {
        return ErrorCode::InvalidUnitAction;
    }
    if (grid.distance(from, to) != 1) {
        return ErrorCode::InvalidUnitAction;
    }
    if (attacker->movementRemaining() <= 0) {
        return ErrorCode::InvalidState;
    }

    // Find the defender: any other player's religious unit on the target tile.
    aoc::game::Player* defenderOwner = nullptr;
    aoc::game::Unit*   defender      = nullptr;
    for (const std::unique_ptr<aoc::game::Player>& other : gameState.players()) {
        if (other == nullptr || other->id() == player) { continue; }
        aoc::game::Unit* candidate = other->unitAt(to);
        if (candidate != nullptr
            && candidate->typeDef().unitClass == UnitClass::Religious) {
            defenderOwner = other.get();
            defender      = candidate;
            break;
        }
    }
    if (defender == nullptr || defenderOwner == nullptr) {
        return ErrorCode::InvalidArgument;
    }

    // Two units of the same faith have nothing to argue about.
    if (attacker->spreadingReligion == defender->spreadingReligion) {
        return ErrorCode::InvalidState;
    }

    // Strength scaled by how much conviction each has left, the same shape unit
    // combat uses, plus a roll so a weaker apostle is not simply doomed.
    const auto conviction = [](const aoc::game::Unit& u) {
        return static_cast<float>(u.hitPoints())
               / static_cast<float>(std::max(1, u.typeDef().maxHitPoints));
    };
    const float atk = theologicalStrength(*attacker) * conviction(*attacker)
                      * (0.8f + rng.nextFloat() * 0.4f);
    const float def_ = theologicalStrength(*defender) * conviction(*defender)
                       * (0.8f + rng.nextFloat() * 0.4f);

    attacker->setMovementRemaining(0);

    const bool attackerWins = atk >= def_;
    aoc::game::Unit* winner  = attackerWins ? attacker : defender;
    aoc::game::Unit* loser   = attackerWins ? defender : attacker;
    aoc::game::Player* loserOwner = attackerWins ? defenderOwner : owner;
    const ReligionId winningFaith = winner->spreadingReligion;
    const hex::AxialCoord where   = attackerWins ? to : from;

    // The winner is not unscathed: a contested conversion costs conviction.
    winner->setHitPoints(std::max(1, winner->hitPoints() - 30));

    LOG_INFO("Theological combat at (%d,%d): player %u prevails", where.q, where.r,
             static_cast<unsigned>(attackerWins ? player : defenderOwner->id()));
    loserOwner->removeUnit(loser);

    // The argument is won where people live, so the nearest city hears it.
    if (winningFaith != NO_RELIGION) {
        aoc::game::City* nearest = nullptr;
        int32_t bestDist = std::numeric_limits<int32_t>::max();
        for (const std::unique_ptr<aoc::game::Player>& p : gameState.players()) {
            if (p == nullptr) { continue; }
            for (const std::unique_ptr<aoc::game::City>& c : p->cities()) {
                if (c == nullptr) { continue; }
                const int32_t d = grid.distance(where, c->location());
                if (d < bestDist) { bestDist = d; nearest = c.get(); }
            }
        }
        if (nearest != nullptr) {
            nearest->religion().addPressure(winningFaith, THEOLOGICAL_WIN_PRESSURE);
        }
    }
    return ErrorCode::Ok;
}


ErrorCode requestPurgeReligion(aoc::game::GameState& gameState, PlayerId player,
                               hex::AxialCoord at) {
    aoc::game::Player* owner = gameState.player(player);
    if (owner == nullptr) {
        return ErrorCode::InvalidArgument;
    }
    // The city must be the player's own: an Inquisitor tends its own flock.
    aoc::game::City* city = owner->cityAt(at);
    if (city == nullptr) {
        return ErrorCode::InvalidArgument;
    }
    // And an Inquisitor with a charge must be standing in it.
    aoc::game::Unit* inquisitor = nullptr;
    for (const std::unique_ptr<aoc::game::Unit>& u : owner->units()) {
        if (u == nullptr || u->position() != at) {
            continue;
        }
        if (u->typeId() == INQUISITOR_UNIT_ID && u->hasCharges()) {
            inquisitor = u.get();
            break;
        }
    }
    if (inquisitor == nullptr) {
        return ErrorCode::InvalidArgument;
    }

    const ReligionId ownFaith = owner->faith().foundedReligion;
    CityReligionComponent& rel = city->religion();
    float purged = 0.0f;
    for (uint8_t r = 0; r < MAX_RELIGIONS; ++r) {
        if (ownFaith == NO_RELIGION || r != static_cast<uint8_t>(ownFaith)) {
            purged += rel.pressure[r];
            rel.pressure[r] = 0.0f;
        }
    }
    if (purged <= 0.0f) {
        return ErrorCode::InvalidState; // nothing foreign to purge
    }

    inquisitor->useCharge();
    LOG_INFO("Player %u purged %.0f foreign religious pressure from %s",
             static_cast<unsigned>(player), static_cast<double>(purged), city->name().c_str());
    if (!inquisitor->hasCharges()) {
        owner->removeUnit(inquisitor);
    }
    return ErrorCode::Ok;
}

} // namespace aoc::sim
