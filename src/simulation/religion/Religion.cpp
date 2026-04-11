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
#include "aoc/simulation/monetary/MonetarySystem.hpp"
#include "aoc/game/GameState.hpp"
#include "aoc/game/Player.hpp"
#include "aoc/game/City.hpp"
#include "aoc/ecs/World.hpp"
#include "aoc/map/HexGrid.hpp"
#include "aoc/core/Log.hpp"

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
        // Faith from worked tiles
        for (const aoc::hex::AxialCoord& tile : city->workedTiles()) {
            if (grid.isValid(tile)) {
                int32_t tileIdx = grid.toIndex(tile);
                aoc::map::TileYield yield = grid.tileYield(tileIdx);
                faithGain += static_cast<float>(yield.faith);
            }
        }

        // Faith from Holy Site district (+2 base)
        if (city->districts().hasDistrict(DistrictType::HolySite)) {
            faithGain += 2.0f;
        }
    }

    playerFaith.faith += faithGain;
}

// ============================================================================
// processReligiousSpread - still uses ECS for GlobalReligionTracker
// ============================================================================

void processReligiousSpread(aoc::game::GameState& gameState,
                             const aoc::map::HexGrid& /*grid*/) {
    constexpr int32_t SPREAD_RANGE = 3;
    constexpr float BASE_PASSIVE_PRESSURE = 0.5f;

    // Gather city info from GameState
    struct CityInfo {
        aoc::hex::AxialCoord location;
        ReligionId dominantReligion;
        bool hasHolySite;
        aoc::game::City* cityPtr;
    };
    std::vector<CityInfo> cities;

    for (const std::unique_ptr<aoc::game::Player>& player : gameState.players()) {
        for (const std::unique_ptr<aoc::game::City>& city : player->cities()) {
            ReligionId dominant = city->religion().dominantReligion();
            bool hasHolySite = city->districts().hasDistrict(DistrictType::HolySite);
            cities.push_back({city->location(), dominant, hasHolySite, city.get()});
        }
    }

    // Apply passive pressure from cities with dominant religions
    for (const CityInfo& source : cities) {
        if (source.dominantReligion == NO_RELIGION) {
            continue;
        }

        float pressure = BASE_PASSIVE_PRESSURE;
        if (source.hasHolySite) {
            pressure *= 2.0f;
        }

        for (const CityInfo& target : cities) {
            if (target.cityPtr == source.cityPtr) { continue; }
            int32_t dist = aoc::hex::distance(source.location, target.location);
            if (dist > SPREAD_RANGE) { continue; }

            target.cityPtr->religion().addPressure(source.dominantReligion, pressure);
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
    for (const std::unique_ptr<aoc::game::City>& city : player.cities()) {
        ReligionId dominant = city->religion().dominantReligion();
        if (dominant == playerFaith.foundedReligion) {
            player.faith().faith += 0.5f;
        }
    }
}

// ============================================================================
// checkReligiousVictory
// ============================================================================

bool checkReligiousVictory(const aoc::game::GameState& gameState, PlayerId player) {
    const aoc::game::Player* gsPlayer = gameState.player(player);
    if (gsPlayer == nullptr) {
        return false;
    }

    if (gsPlayer->faith().foundedReligion == NO_RELIGION) {
        return false;
    }

    ReligionId playerReligion = gsPlayer->faith().foundedReligion;

    // Check if this religion is dominant in the majority of all cities
    int32_t totalCities = 0;
    int32_t followerCities = 0;

    for (const std::unique_ptr<aoc::game::Player>& otherPlayer : gameState.players()) {
        for (const std::unique_ptr<aoc::game::City>& city : otherPlayer->cities()) {
            ++totalCities;
            if (city->religion().dominantReligion() == playerReligion) {
                ++followerCities;
            }
        }
    }

    // Win if religion is dominant in > 50% of all cities
    return (totalCities > 0 && followerCities * 2 > totalCities);
}

} // namespace aoc::sim
