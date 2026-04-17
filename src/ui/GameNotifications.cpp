/**
 * @file GameNotifications.cpp
 * @brief Notification generation for all game systems.
 */

#include "aoc/ui/GameNotifications.hpp"
#include "aoc/game/GameState.hpp"
#include "aoc/game/Player.hpp"
#include "aoc/game/City.hpp"
#include "aoc/simulation/monetary/MonetarySystem.hpp"
#include "aoc/simulation/monetary/CurrencyCrisis.hpp"
#include "aoc/simulation/monetary/CurrencyTrust.hpp"
#include "aoc/simulation/economy/IndustrialRevolution.hpp"
#include "aoc/simulation/economy/EconomicDepth.hpp"
#include "aoc/simulation/victory/VictoryCondition.hpp"
#include "aoc/core/Log.hpp"

#include <vector>

namespace aoc::ui {

namespace {
    /// Thread-local queue so parallel headless simulations (GA fitness eval)
    /// don't race on a shared vector. The game UI runs single-threaded, so
    /// thread_local behaves identically to a global for interactive play.
    thread_local std::vector<GameNotification> g_pendingNotifications;
}

void pushNotification(const GameNotification& notification) {
    g_pendingNotifications.push_back(notification);
    LOG_INFO("[NOTIFY:%s] %s",
             notification.title.c_str(), notification.body.c_str());
}

void generateTurnNotifications(const aoc::game::GameState& gameState, PlayerId player) {
    g_pendingNotifications.clear();

    const aoc::game::Player* playerObj = gameState.player(player);
    if (playerObj == nullptr) {
        return;
    }

    // Currency crises
    {
        const aoc::sim::CurrencyCrisisComponent& crisis = playerObj->currencyCrisis();
        if (crisis.activeCrisis != aoc::sim::CrisisType::None) {
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

            if (!n.title.empty()) {
                pushNotification(n);
            }
        }
    }

    // Industrial revolutions (placeholder - check if revolution level changed)
    {
        // NOTE: Turn-diff detection would require storing previous turn's revolution
        // level. For now just note industrial state is available via player->industrial().
        (void)playerObj->industrial();
    }

    // Labor strikes in the player's cities
    for (const std::unique_ptr<aoc::game::City>& city : playerObj->cities()) {
        if (city->strike().isOnStrike) {
            GameNotification n;
            n.category = NotificationCategory::City;
            n.title = "LABOR STRIKE in " + city->name();
            n.body = "Industrial buildings shut down. Improve amenities!";
            n.priority = 5;
            pushNotification(n);
        }
    }

    // Reserve currency status
    {
        const aoc::sim::CurrencyTrustComponent& trust = playerObj->currencyTrust();
        if (trust.isReserveCurrency && trust.turnsAsReserve == 1) {
            GameNotification n;
            n.category = NotificationCategory::Economy;
            n.title = "RESERVE CURRENCY!";
            n.body = "Your currency is now the global reserve. +5% trade bonus.";
            n.priority = 8;
            pushNotification(n);
        }
    }

    // Collapse warnings from victory tracker
    {
        const aoc::sim::VictoryTrackerComponent& vt = playerObj->victoryTracker();

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

} // namespace aoc::ui
