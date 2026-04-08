/**
 * @file GameNotifications.cpp
 * @brief Notification generation for all game systems.
 */

#include "aoc/ui/GameNotifications.hpp"
#include "aoc/simulation/monetary/MonetarySystem.hpp"
#include "aoc/simulation/monetary/CurrencyCrisis.hpp"
#include "aoc/simulation/monetary/CurrencyTrust.hpp"
#include "aoc/simulation/economy/IndustrialRevolution.hpp"
#include "aoc/simulation/economy/EconomicDepth.hpp"
#include "aoc/simulation/city/CityComponent.hpp"
#include "aoc/simulation/victory/VictoryCondition.hpp"
#include "aoc/ecs/World.hpp"
#include "aoc/core/Log.hpp"

#include <vector>

namespace aoc::ui {

namespace {
    std::vector<GameNotification> g_pendingNotifications;
}

void pushNotification(const GameNotification& notification) {
    g_pendingNotifications.push_back(notification);
    LOG_INFO("[NOTIFY:%s] %s",
             notification.title.c_str(), notification.body.c_str());
}

void generateTurnNotifications(const aoc::ecs::World& world, PlayerId player) {
    g_pendingNotifications.clear();

    // Currency crises
    const aoc::ecs::ComponentPool<aoc::sim::CurrencyCrisisComponent>* crisisPool =
        world.getPool<aoc::sim::CurrencyCrisisComponent>();
    if (crisisPool != nullptr) {
        for (uint32_t i = 0; i < crisisPool->size(); ++i) {
            const aoc::sim::CurrencyCrisisComponent& crisis = crisisPool->data()[i];
            if (crisis.owner != player || crisis.activeCrisis == aoc::sim::CrisisType::None) {
                continue;
            }

            GameNotification n;
            n.category = NotificationCategory::Economy;
            n.relevantPlayer = player;
            n.priority = 10;

            switch (crisis.activeCrisis) {
                case aoc::sim::CrisisType::BankRun:
                    n.title = "BANK RUN!";
                    n.body = "Gold reserves draining 20%/turn. Consider raising taxes.";
                    break;
                case aoc::sim::CrisisType::Hyperinflation:
                    n.title = "HYPERINFLATION!";
                    n.body = "Production -30%, Science -20%. Currency reform needed.";
                    break;
                case aoc::sim::CrisisType::SovereignDefault:
                    n.title = "SOVEREIGN DEFAULT!";
                    n.body = "Cannot pay debt. No loans for 10 turns. Trade -30%.";
                    break;
                default:
                    break;
            }
            pushNotification(n);
        }
    }

    // Industrial revolutions
    const aoc::ecs::ComponentPool<aoc::sim::PlayerIndustrialComponent>* indPool =
        world.getPool<aoc::sim::PlayerIndustrialComponent>();
    if (indPool != nullptr) {
        for (uint32_t i = 0; i < indPool->size(); ++i) {
            if (indPool->data()[i].owner != player) { continue; }
            uint8_t rev = static_cast<uint8_t>(indPool->data()[i].currentRevolution);
            if (rev > 0 && indPool->data()[i].turnAchieved[rev] > 0) {
                // Check if achieved this turn (would need turn comparison)
                // For now, just check if revolution level changed recently
            }
        }
    }

    // Labor strikes
    const aoc::ecs::ComponentPool<aoc::sim::CityStrikeComponent>* strikePool =
        world.getPool<aoc::sim::CityStrikeComponent>();
    if (strikePool != nullptr) {
        const aoc::ecs::ComponentPool<aoc::sim::CityComponent>* cityPool =
            world.getPool<aoc::sim::CityComponent>();
        for (uint32_t i = 0; i < strikePool->size(); ++i) {
            if (!strikePool->data()[i].isOnStrike) { continue; }
            if (cityPool != nullptr) {
                EntityId cityEntity = strikePool->entities()[i];
                const aoc::sim::CityComponent* city =
                    world.tryGetComponent<aoc::sim::CityComponent>(cityEntity);
                if (city != nullptr && city->owner == player) {
                    GameNotification n;
                    n.category = NotificationCategory::City;
                    n.title = "LABOR STRIKE in " + city->name;
                    n.body = "Industrial buildings shut down. Improve amenities!";
                    n.priority = 5;
                    pushNotification(n);
                }
            }
        }
    }

    // Reserve currency status
    const aoc::ecs::ComponentPool<aoc::sim::CurrencyTrustComponent>* trustPool =
        world.getPool<aoc::sim::CurrencyTrustComponent>();
    if (trustPool != nullptr) {
        for (uint32_t i = 0; i < trustPool->size(); ++i) {
            if (trustPool->data()[i].owner == player && trustPool->data()[i].isReserveCurrency) {
                if (trustPool->data()[i].turnsAsReserve == 1) {
                    GameNotification n;
                    n.category = NotificationCategory::Economy;
                    n.title = "RESERVE CURRENCY!";
                    n.body = "Your currency is now the global reserve. +5% trade bonus.";
                    n.priority = 8;
                    pushNotification(n);
                }
            }
        }
    }

    // Collapse warnings
    const aoc::ecs::ComponentPool<aoc::sim::VictoryTrackerComponent>* victoryPool =
        world.getPool<aoc::sim::VictoryTrackerComponent>();
    if (victoryPool != nullptr) {
        for (uint32_t i = 0; i < victoryPool->size(); ++i) {
            if (victoryPool->data()[i].owner != player) { continue; }
            const aoc::sim::VictoryTrackerComponent& vt = victoryPool->data()[i];

            if (vt.turnsGDPBelowHalf >= 5) {
                GameNotification n;
                n.category = NotificationCategory::Economy;
                n.title = "ECONOMIC DECLINE!";
                n.body = "GDP below 50% of peak for " + std::to_string(vt.turnsGDPBelowHalf)
                       + " turns. Collapse at 10!";
                n.priority = 9;
                pushNotification(n);
            }
            if (vt.turnsLowLoyalty >= 3) {
                GameNotification n;
                n.category = NotificationCategory::Government;
                n.title = "REVOLUTION RISK!";
                n.body = "Average loyalty below 30 for " + std::to_string(vt.turnsLowLoyalty)
                       + " turns. Revolution at 5!";
                n.priority = 9;
                pushNotification(n);
            }
        }
    }
}

} // namespace aoc::ui
