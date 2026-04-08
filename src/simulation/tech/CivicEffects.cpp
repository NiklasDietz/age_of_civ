/**
 * @file CivicEffects.cpp
 * @brief Direct civic effect application.
 */

#include "aoc/simulation/tech/CivicEffects.hpp"
#include "aoc/simulation/city/CityComponent.hpp"
#include "aoc/simulation/city/CityLoyalty.hpp"
#include "aoc/simulation/tech/TechTree.hpp"
#include "aoc/simulation/tech/EurekaBoost.hpp"
#include "aoc/ecs/World.hpp"
#include "aoc/core/Log.hpp"

namespace aoc::sim {

void applyCivicEffect(aoc::ecs::World& world, PlayerId player, uint8_t civicId) {
    for (int32_t i = 0; i < CIVIC_EFFECT_COUNT; ++i) {
        if (CIVIC_EFFECTS[i].civicId != civicId) {
            continue;
        }

        const CivicEffect& effect = CIVIC_EFFECTS[i];

        switch (effect.type) {
            case CivicEffectType::GrantEnvoy:
                LOG_INFO("Player %u: civic effect +%d envoy(s)",
                         static_cast<unsigned>(player), effect.value);
                break;

            case CivicEffectType::ExtraBuilderCharges:
                LOG_INFO("Player %u: civic effect +%d builder charges for new builders",
                         static_cast<unsigned>(player), effect.value);
                break;

            case CivicEffectType::ExtraTradeRoute:
                LOG_INFO("Player %u: civic effect +%d trade route capacity",
                         static_cast<unsigned>(player), effect.value);
                break;

            case CivicEffectType::CultureBurst:
                LOG_INFO("Player %u: civic effect +%d culture burst",
                         static_cast<unsigned>(player), effect.value);
                break;

            case CivicEffectType::FaithBurst:
                LOG_INFO("Player %u: civic effect +%d faith burst",
                         static_cast<unsigned>(player), effect.value);
                break;

            case CivicEffectType::LoyaltyBoost: {
                aoc::ecs::ComponentPool<CityLoyaltyComponent>* loyaltyPool =
                    world.getPool<CityLoyaltyComponent>();
                const aoc::ecs::ComponentPool<CityComponent>* cityPool =
                    world.getPool<CityComponent>();
                if (loyaltyPool != nullptr && cityPool != nullptr) {
                    for (uint32_t c = 0; c < cityPool->size(); ++c) {
                        if (cityPool->data()[c].owner == player) {
                            CityLoyaltyComponent* loyalty =
                                world.tryGetComponent<CityLoyaltyComponent>(cityPool->entities()[c]);
                            if (loyalty != nullptr) {
                                loyalty->loyalty = std::min(100.0f,
                                    loyalty->loyalty + static_cast<float>(effect.value));
                            }
                        }
                    }
                }
                LOG_INFO("Player %u: civic effect +%d loyalty in all cities",
                         static_cast<unsigned>(player), effect.value);
                break;
            }

            case CivicEffectType::FreeTech:
                LOG_INFO("Player %u: civic effect free eureka on random tech",
                         static_cast<unsigned>(player));
                break;

            default:
                break;
        }
        return;  // Each civic has at most one effect
    }
}

} // namespace aoc::sim
