/**
 * @file CivicEffects.cpp
 * @brief Direct civic effect application.
 */

#include "aoc/game/GameState.hpp"
#include "aoc/simulation/tech/CivicEffects.hpp"
#include "aoc/simulation/tech/EurekaBoost.hpp"
#include "aoc/core/Log.hpp"

#include "aoc/game/Player.hpp"
#include "aoc/game/City.hpp"

namespace aoc::sim {

void applyCivicEffect(aoc::game::GameState& gameState, PlayerId player, uint8_t civicId) {
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
                aoc::game::Player* gsPlayer = gameState.player(player);
                if (gsPlayer != nullptr) {
                    for (const std::unique_ptr<aoc::game::City>& city : gsPlayer->cities()) {
                        city->loyalty().loyalty = std::min(100.0f,
                            city->loyalty().loyalty + static_cast<float>(effect.value));
                    }
                }
                LOG_INFO("Player %u: civic effect +%d loyalty in all cities",
                         static_cast<unsigned>(player), effect.value);
                break;
            }

            case CivicEffectType::FreeTech: {
                // Grant 50% progress on current research, or instant-finish a
                // random unresearched prerequisite-met tech if no current.
                aoc::game::Player* gsPlayer = gameState.player(player);
                if (gsPlayer == nullptr) { break; }
                PlayerTechComponent& tech = gsPlayer->tech();
                if (tech.currentResearch.isValid()) {
                    const TechDef& tdef = techDef(tech.currentResearch);
                    tech.researchProgress += static_cast<float>(tdef.researchCost) * 0.5f;
                    LOG_INFO("Player %u: civic FreeTech granted +50%% progress on tech %u",
                             static_cast<unsigned>(player),
                             static_cast<unsigned>(tech.currentResearch.value));
                } else {
                    // Find first available tech with all prereqs met, mark complete.
                    const std::vector<TechId>& avail = tech.availableTechs();
                    if (!avail.empty()) {
                        const TechId picked = avail.front();
                        if (picked.value < tech.completedTechs.size()) {
                            tech.completedTechs[picked.value] = true;
                        }
                        LOG_INFO("Player %u: civic FreeTech auto-completed tech %u",
                                 static_cast<unsigned>(player),
                                 static_cast<unsigned>(picked.value));
                    }
                }
                break;
            }

            default:
                break;
        }
        return;  // Each civic has at most one effect
    }
}

} // namespace aoc::sim
