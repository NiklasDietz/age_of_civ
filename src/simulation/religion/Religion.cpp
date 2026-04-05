/**
 * @file Religion.cpp
 * @brief Religion system: faith accumulation, religious spread, belief bonuses,
 *        and religious victory evaluation.
 */

#include "aoc/simulation/religion/Religion.hpp"
#include "aoc/simulation/city/CityComponent.hpp"
#include "aoc/simulation/city/District.hpp"
#include "aoc/simulation/city/Happiness.hpp"
#include "aoc/simulation/monetary/MonetarySystem.hpp"
#include "aoc/ecs/World.hpp"
#include "aoc/map/HexGrid.hpp"
#include "aoc/core/Log.hpp"

namespace aoc::sim {

// ============================================================================
// Beliefs table (16 beliefs, 4 per type)
// ============================================================================

static constexpr std::array<BeliefDef, BELIEF_COUNT> s_beliefs = {{
    // Founder beliefs (0-3)
    {0,  "Tithe",           BeliefType::Founder, "+1 gold per follower city",
         1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f},
    {1,  "Church Property", BeliefType::Founder, "+2 gold per follower city",
         2.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f},
    {2,  "World Church",    BeliefType::Founder, "+1 science per follower city",
         0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f},
    {3,  "Papal Primacy",   BeliefType::Founder, "+1 faith per follower city",
         0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f},

    // Follower beliefs (4-7)
    {4,  "Divine Inspiration",  BeliefType::Follower, "+1 amenity",
         0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f},
    {5,  "Zen Meditation",      BeliefType::Follower, "+1 amenity, +1 science",
         0.0f, 0.0f, 1.0f, 0.0f, 1.0f, 0.0f},
    {6,  "Feed the World",      BeliefType::Follower, "+2 food",
         0.0f, 0.0f, 0.0f, 2.0f, 0.0f, 0.0f},
    {7,  "Religious Community", BeliefType::Follower, "+1 amenity, +1 food",
         0.0f, 0.0f, 1.0f, 1.0f, 0.0f, 0.0f},

    // Worship beliefs (8-11)
    {8,  "Cathedral", BeliefType::Worship, "+3 faith",
         0.0f, 0.0f, 0.0f, 0.0f, 3.0f, 0.0f},
    {9,  "Mosque",    BeliefType::Worship, "+2 faith, +1 science",
         0.0f, 0.0f, 0.0f, 0.0f, 2.0f, 0.0f},
    {10, "Pagoda",    BeliefType::Worship, "+2 faith, +1 amenity",
         0.0f, 0.0f, 1.0f, 0.0f, 2.0f, 0.0f},
    {11, "Monastery", BeliefType::Worship, "+2 faith, +1 food",
         0.0f, 0.0f, 0.0f, 1.0f, 2.0f, 0.0f},

    // Enhancer beliefs (12-15)
    {12, "Missionary Zeal",     BeliefType::Enhancer, "+50% spread strength",
         0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.5f},
    {13, "Holy Order",          BeliefType::Enhancer, "+25% spread, +1 faith",
         0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.25f},
    {14, "Itinerant Preachers", BeliefType::Enhancer, "Spread to cities 2 tiles further",
         0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f},
    {15, "Scripture",           BeliefType::Enhancer, "+25% spread, +1 science per follower city",
         0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.25f},
}};

const std::array<BeliefDef, BELIEF_COUNT>& allBeliefs() {
    return s_beliefs;
}

// ============================================================================
// accumulateFaith
// ============================================================================

void accumulateFaith(aoc::ecs::World& world, const aoc::map::HexGrid& grid, PlayerId player) {
    aoc::ecs::ComponentPool<PlayerFaithComponent>* faithPool =
        world.getPool<PlayerFaithComponent>();
    if (faithPool == nullptr) {
        return;
    }

    // Find the player's faith component
    PlayerFaithComponent* playerFaith = nullptr;
    for (uint32_t i = 0; i < faithPool->size(); ++i) {
        if (faithPool->data()[i].owner == player) {
            playerFaith = &faithPool->data()[i];
            break;
        }
    }
    if (playerFaith == nullptr) {
        return;
    }

    float faithGain = 0.0f;

    // Sum faith yield from worked tiles in player's cities
    const aoc::ecs::ComponentPool<CityComponent>* cityPool =
        world.getPool<CityComponent>();
    if (cityPool != nullptr) {
        for (uint32_t i = 0; i < cityPool->size(); ++i) {
            const CityComponent& city = cityPool->data()[i];
            if (city.owner != player) {
                continue;
            }

            // Faith from worked tiles
            for (const hex::AxialCoord& tile : city.workedTiles) {
                if (grid.isValid(tile)) {
                    const int32_t tileIdx = grid.toIndex(tile);
                    const aoc::map::TileYield yield = grid.tileYield(tileIdx);
                    faithGain += static_cast<float>(yield.faith);
                }
            }

            // Faith from Holy Site district (+2 base)
            EntityId cityEntity = cityPool->entities()[i];
            const CityDistrictsComponent* districts =
                world.tryGetComponent<CityDistrictsComponent>(cityEntity);
            if (districts != nullptr && districts->hasDistrict(DistrictType::HolySite)) {
                faithGain += 2.0f;
            }
        }
    }

    playerFaith->faith += faithGain;
}

// ============================================================================
// processReligiousSpread
// ============================================================================

void processReligiousSpread(aoc::ecs::World& world, const aoc::map::HexGrid& /*grid*/) {
    aoc::ecs::ComponentPool<CityComponent>* cityPool =
        world.getPool<CityComponent>();
    aoc::ecs::ComponentPool<CityReligionComponent>* religionPool =
        world.getPool<CityReligionComponent>();
    if (cityPool == nullptr || religionPool == nullptr) {
        return;
    }

    // Gather city locations and their dominant religions
    struct CityInfo {
        hex::AxialCoord location;
        ReligionId dominantReligion;
        bool hasHolySite;
        EntityId entity;
    };
    std::vector<CityInfo> cities;
    cities.reserve(cityPool->size());

    for (uint32_t i = 0; i < cityPool->size(); ++i) {
        const CityComponent& city = cityPool->data()[i];
        EntityId cityEntity = cityPool->entities()[i];

        if (city.owner == BARBARIAN_PLAYER) {
            continue;
        }

        const CityReligionComponent* cityReligion =
            world.tryGetComponent<CityReligionComponent>(cityEntity);
        ReligionId dominant = NO_RELIGION;
        if (cityReligion != nullptr) {
            dominant = cityReligion->dominantReligion();
        }

        bool hasHolySite = false;
        const CityDistrictsComponent* districts =
            world.tryGetComponent<CityDistrictsComponent>(cityEntity);
        if (districts != nullptr) {
            hasHolySite = districts->hasDistrict(DistrictType::HolySite);
        }

        cities.push_back({city.location, dominant, hasHolySite, cityEntity});
    }

    // For each city with a dominant religion, apply passive pressure to nearby cities
    constexpr int32_t SPREAD_RANGE = 3;
    constexpr float BASE_PASSIVE_PRESSURE = 0.5f;

    for (const CityInfo& source : cities) {
        if (source.dominantReligion == NO_RELIGION) {
            continue;
        }

        float pressure = BASE_PASSIVE_PRESSURE;
        if (source.hasHolySite) {
            pressure *= 2.0f;
        }

        for (const CityInfo& target : cities) {
            if (target.entity == source.entity) {
                continue;
            }
            const int32_t dist = hex::distance(source.location, target.location);
            if (dist > SPREAD_RANGE) {
                continue;
            }

            CityReligionComponent* targetReligion =
                world.tryGetComponent<CityReligionComponent>(target.entity);
            if (targetReligion != nullptr) {
                targetReligion->addPressure(source.dominantReligion, pressure);
            }
        }
    }
}

// ============================================================================
// applyReligionBonuses
// ============================================================================

void applyReligionBonuses(aoc::ecs::World& world, PlayerId player) {
    // Find player's faith component
    const aoc::ecs::ComponentPool<PlayerFaithComponent>* faithPool =
        world.getPool<PlayerFaithComponent>();
    if (faithPool == nullptr) {
        return;
    }

    const PlayerFaithComponent* playerFaith = nullptr;
    for (uint32_t i = 0; i < faithPool->size(); ++i) {
        if (faithPool->data()[i].owner == player) {
            playerFaith = &faithPool->data()[i];
            break;
        }
    }
    if (playerFaith == nullptr || playerFaith->foundedReligion == NO_RELIGION) {
        return;
    }

    const ReligionId religionId = playerFaith->foundedReligion;

    // Find the religion definition
    const aoc::ecs::ComponentPool<GlobalReligionTracker>* trackerPool =
        world.getPool<GlobalReligionTracker>();
    if (trackerPool == nullptr || trackerPool->size() == 0) {
        return;
    }
    const GlobalReligionTracker& tracker = trackerPool->data()[0];
    if (religionId >= tracker.religionsFoundedCount) {
        return;
    }
    const ReligionDef& religion = tracker.religions[religionId];

    // Count follower cities (cities where this religion is dominant)
    int32_t followerCityCount = 0;
    const aoc::ecs::ComponentPool<CityComponent>* cityPool =
        world.getPool<CityComponent>();
    const aoc::ecs::ComponentPool<CityReligionComponent>* cityReligionPool =
        world.getPool<CityReligionComponent>();

    if (cityPool != nullptr && cityReligionPool != nullptr) {
        for (uint32_t i = 0; i < cityPool->size(); ++i) {
            EntityId cityEntity = cityPool->entities()[i];
            const CityReligionComponent* cityRel =
                world.tryGetComponent<CityReligionComponent>(cityEntity);
            if (cityRel != nullptr && cityRel->dominantReligion() == religionId) {
                ++followerCityCount;
            }
        }
    }

    if (followerCityCount == 0) {
        return;
    }

    const std::array<BeliefDef, BELIEF_COUNT>& beliefs = allBeliefs();

    // Apply founder belief bonuses
    if (religion.founderBelief < BELIEF_COUNT) {
        const BeliefDef& founderBelief = beliefs[religion.founderBelief];
        const float goldBonus = founderBelief.goldPerFollowerCity * static_cast<float>(followerCityCount);
        const float faithBonus = founderBelief.faithBonus * static_cast<float>(followerCityCount);

        // Add gold to player treasury
        if (goldBonus > 0.0f) {
            aoc::ecs::ComponentPool<MonetaryStateComponent>* monetaryPool =
                world.getPool<MonetaryStateComponent>();
            if (monetaryPool != nullptr) {
                for (uint32_t i = 0; i < monetaryPool->size(); ++i) {
                    MonetaryStateComponent& ms = monetaryPool->data()[i];
                    if (ms.owner == player) {
                        ms.goldReserves += static_cast<CurrencyAmount>(goldBonus);
                        break;
                    }
                }
            }
        }

        // Add faith bonus from founder belief
        if (faithBonus > 0.0f) {
            aoc::ecs::ComponentPool<PlayerFaithComponent>* mutableFaithPool =
                world.getPool<PlayerFaithComponent>();
            if (mutableFaithPool != nullptr) {
                for (uint32_t i = 0; i < mutableFaithPool->size(); ++i) {
                    if (mutableFaithPool->data()[i].owner == player) {
                        mutableFaithPool->data()[i].faith += faithBonus;
                        break;
                    }
                }
            }
        }
    }

    // Apply follower belief bonuses to each city with this religion
    if (religion.followerBelief < BELIEF_COUNT && cityPool != nullptr && cityReligionPool != nullptr) {
        const BeliefDef& followerBelief = beliefs[religion.followerBelief];

        for (uint32_t i = 0; i < cityPool->size(); ++i) {
            EntityId cityEntity = cityPool->entities()[i];
            const CityReligionComponent* cityRel =
                world.tryGetComponent<CityReligionComponent>(cityEntity);
            if (cityRel == nullptr || cityRel->dominantReligion() != religionId) {
                continue;
            }

            // Apply amenity bonus to happiness
            if (followerBelief.amenityBonus > 0.0f) {
                CityHappinessComponent* happiness =
                    world.tryGetComponent<CityHappinessComponent>(cityEntity);
                if (happiness != nullptr) {
                    happiness->amenities += followerBelief.amenityBonus;
                    happiness->happiness = happiness->amenities - happiness->demand + happiness->modifiers;
                }
            }
        }
    }
}

// ============================================================================
// checkReligiousVictory
// ============================================================================

bool checkReligiousVictory(const aoc::ecs::World& world, PlayerId& outWinner) {
    const aoc::ecs::ComponentPool<GlobalReligionTracker>* trackerPool =
        world.getPool<GlobalReligionTracker>();
    if (trackerPool == nullptr || trackerPool->size() == 0) {
        return false;
    }
    const GlobalReligionTracker& tracker = trackerPool->data()[0];

    const aoc::ecs::ComponentPool<CityComponent>* cityPool =
        world.getPool<CityComponent>();
    const aoc::ecs::ComponentPool<CityReligionComponent>* cityReligionPool =
        world.getPool<CityReligionComponent>();
    if (cityPool == nullptr || cityReligionPool == nullptr) {
        return false;
    }

    // Count total non-barbarian cities and per-religion dominant counts
    int32_t totalCities = 0;
    std::array<int32_t, MAX_RELIGIONS> dominantCounts = {};

    for (uint32_t i = 0; i < cityPool->size(); ++i) {
        const CityComponent& city = cityPool->data()[i];
        if (city.owner == BARBARIAN_PLAYER) {
            continue;
        }
        ++totalCities;

        EntityId cityEntity = cityPool->entities()[i];
        const CityReligionComponent* cityRel =
            world.tryGetComponent<CityReligionComponent>(cityEntity);
        if (cityRel != nullptr) {
            ReligionId dominant = cityRel->dominantReligion();
            if (dominant < MAX_RELIGIONS) {
                ++dominantCounts[dominant];
            }
        }
    }

    if (totalCities == 0) {
        return false;
    }

    // Check if any religion is dominant in >50% of all cities
    for (uint8_t r = 0; r < tracker.religionsFoundedCount; ++r) {
        if (dominantCounts[r] * 2 > totalCities) {
            outWinner = tracker.religions[r].founder;
            LOG_INFO("Player %u achieved RELIGIOUS victory! Religion '%s' dominates %d/%d cities.",
                     static_cast<unsigned>(outWinner),
                     tracker.religions[r].name.c_str(),
                     dominantCounts[r], totalCities);
            return true;
        }
    }

    return false;
}

} // namespace aoc::sim
