/**
 * @file Application_CityControl.cpp
 * @brief The five city routes (purchase, focus, lock-tile, queue/remove, project)
 *        and their command executors. Every
 *        route queues a command; the executors forward to the validated
 *        requests in CityActions.hpp (Civ VI plan Phase 2.2, 2026-09-05).
 */

#include "aoc/app/Application.hpp"

#include "aoc/core/Log.hpp"
#include "aoc/debug/DebugServer.hpp"
#include "aoc/debug/GameControlCommand.hpp"
#include "aoc/simulation/city/CityActions.hpp"
#include "aoc/simulation/unit/BuilderActions.hpp"
#include "aoc/simulation/unit/UnitOrders.hpp"
#include "aoc/simulation/unit/Promotion.hpp"
#include "aoc/simulation/city/DistrictPlacement.hpp"
#include "aoc/simulation/citystate/CityState.hpp"
#include "aoc/simulation/diplomacy/DealProposals.hpp"
#include "aoc/simulation/diplomacy/DealTerms.hpp"
#include "aoc/simulation/diplomacy/DiplomacyActions.hpp"
#include "aoc/simulation/diplomacy/DiplomacyState.hpp"
#include "aoc/simulation/culture/GreatWorks.hpp"
#include "aoc/simulation/religion/Religion.hpp"
#include "aoc/game/Player.hpp"
#include "aoc/game/Unit.hpp"

#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace aoc::app {

namespace {

/// `{"error":"missing NAME"}` when the query lacks an integer `name`.
bool readIntParam(const std::unordered_map<std::string, std::string>& query, const char* name,
                  int32_t& out, std::string& errorJson) {
    const std::unordered_map<std::string, std::string>::const_iterator it = query.find(name);
    if (it == query.end()) {
        errorJson = std::string("{\"error\":\"missing ") + name + "\"}";
        return false;
    }
    try {
        out = std::stoi(it->second);
    } catch (const std::exception&) {
        errorJson = std::string("{\"error\":\"bad ") + name + "\"}";
        return false;
    }
    return true;
}

} // namespace

void Application::registerCityControlRoutes() {
    using DSM   = aoc::debug::DebugServer::Method;
    using Query = std::unordered_map<std::string, std::string>;

    // Every city route names the city by its centre tile: player, q, r.
    const auto readCity = [](const Query& q, int32_t& player, aoc::hex::AxialCoord& at,
                             std::string& err) -> bool {
        int32_t cq = 0;
        int32_t cr = 0;
        if (!readIntParam(q, "player", player, err) || !readIntParam(q, "q", cq, err) ||
            !readIntParam(q, "r", cr, err)) {
            return false;
        }
        at = aoc::hex::AxialCoord{cq, cr};
        return true;
    };
    const auto queued = [this](const aoc::debug::GameControlCommand& cmd) -> std::string {
        std::lock_guard<std::mutex> guard(this->m_pendingCommandsMutex);
        this->m_pendingCommands.push_back(cmd);
        return std::string("{\"queued\":true}");
    };

    // POST /game/city/purchase?player=&q=&r=&type=&item=&faith=   (type 0 unit, 1 building)
    this->m_debugServer->routeJson(
        DSM::Post, "/game/city/purchase",
        [this, readCity, queued](const Query& q, const std::string&) -> std::string {
            if (this->m_appState != AppState::InGame) {
                throw aoc::debug::ServiceUnavailableError("no active game");
            }
            int32_t player = 0;
            int32_t type   = 0;
            int32_t item   = 0;
            int32_t faith  = 0;
            aoc::hex::AxialCoord at{};
            std::string err;
            if (!readCity(q, player, at, err) || !readIntParam(q, "type", type, err) ||
                !readIntParam(q, "item", item, err)) {
                return err;
            }
            if (q.count("faith") != 0 && !readIntParam(q, "faith", faith, err)) {
                return err;
            }
            if (type < 0 || type > 1 || item < 0 || item > 255) {
                return std::string("{\"error\":\"type or item out of range\"}");
            }
            aoc::debug::CityPurchaseCommand cmd{};
            cmd.player    = static_cast<aoc::PlayerId>(player);
            cmd.at        = at;
            cmd.type      = static_cast<aoc::sim::ProductionItemType>(type);
            cmd.itemId    = static_cast<uint16_t>(item);
            cmd.withFaith = faith != 0;
            return queued(cmd);
        });

    // POST /game/city/focus?player=&q=&r=&focus=   (0 Balanced .. 5 Military)
    this->m_debugServer->routeJson(
        DSM::Post, "/game/city/focus",
        [this, readCity, queued](const Query& q, const std::string&) -> std::string {
            if (this->m_appState != AppState::InGame) {
                throw aoc::debug::ServiceUnavailableError("no active game");
            }
            int32_t player = 0;
            int32_t focus  = 0;
            aoc::hex::AxialCoord at{};
            std::string err;
            if (!readCity(q, player, at, err) || !readIntParam(q, "focus", focus, err)) {
                return err;
            }
            if (focus < 0 || focus >= static_cast<int32_t>(aoc::sim::CityFocus::Count)) {
                return std::string("{\"error\":\"focus out of range\"}");
            }
            aoc::debug::SetCityFocusCommand cmd{};
            cmd.player = static_cast<aoc::PlayerId>(player);
            cmd.at     = at;
            cmd.focus  = static_cast<aoc::sim::CityFocus>(focus);
            return queued(cmd);
        });

    // POST /game/city/lock-tile?player=&q=&r=&tq=&tr=
    this->m_debugServer->routeJson(
        DSM::Post, "/game/city/lock-tile",
        [this, readCity, queued](const Query& q, const std::string&) -> std::string {
            if (this->m_appState != AppState::InGame) {
                throw aoc::debug::ServiceUnavailableError("no active game");
            }
            int32_t player = 0;
            int32_t tq     = 0;
            int32_t tr     = 0;
            aoc::hex::AxialCoord at{};
            std::string err;
            if (!readCity(q, player, at, err) || !readIntParam(q, "tq", tq, err) ||
                !readIntParam(q, "tr", tr, err)) {
                return err;
            }
            aoc::debug::ToggleTileLockCommand cmd{};
            cmd.player = static_cast<aoc::PlayerId>(player);
            cmd.at     = at;
            cmd.tile   = aoc::hex::AxialCoord{tq, tr};
            return queued(cmd);
        });

    // POST /game/city/queue/remove?player=&q=&r=&index=
    this->m_debugServer->routeJson(
        DSM::Post, "/game/city/queue/remove",
        [this, readCity, queued](const Query& q, const std::string&) -> std::string {
            if (this->m_appState != AppState::InGame) {
                throw aoc::debug::ServiceUnavailableError("no active game");
            }
            int32_t player = 0;
            int32_t index  = 0;
            aoc::hex::AxialCoord at{};
            std::string err;
            if (!readCity(q, player, at, err) || !readIntParam(q, "index", index, err)) {
                return err;
            }
            aoc::debug::RemoveQueueItemCommand cmd{};
            cmd.player = static_cast<aoc::PlayerId>(player);
            cmd.at     = at;
            cmd.index  = index;
            return queued(cmd);
        });

    // POST /game/city/project?player=&q=&r=&project=   (0 Bread and Circuses .. 5 Military
    // Training)
    this->m_debugServer->routeJson(
        DSM::Post, "/game/city/project",
        [this, readCity, queued](const Query& q, const std::string&) -> std::string {
            if (this->m_appState != AppState::InGame) {
                throw aoc::debug::ServiceUnavailableError("no active game");
            }
            int32_t player  = 0;
            int32_t project = 0;
            aoc::hex::AxialCoord at{};
            std::string err;
            if (!readCity(q, player, at, err) || !readIntParam(q, "project", project, err)) {
                return err;
            }
            if (project < 0 || project >= static_cast<int32_t>(aoc::sim::CityProjectType::Count)) {
                return std::string("{\"error\":\"project out of range\"}");
            }
            aoc::debug::QueueProjectCommand cmd{};
            cmd.player  = static_cast<aoc::PlayerId>(player);
            cmd.at      = at;
            cmd.project = static_cast<aoc::sim::CityProjectType>(project);
            return queued(cmd);
        });
}

void Application::registerBuilderControlRoutes() {
    using DSM           = aoc::debug::DebugServer::Method;
    using Query         = std::unordered_map<std::string, std::string>;
    const auto readUnit = [](const Query& q, int32_t& player, aoc::hex::AxialCoord& at,
                             std::string& err) -> bool {
        int32_t uq = 0;
        int32_t ur = 0;
        if (!readIntParam(q, "player", player, err) || !readIntParam(q, "q", uq, err) ||
            !readIntParam(q, "r", ur, err)) {
            return false;
        }
        at = aoc::hex::AxialCoord{uq, ur};
        return true;
    };
    const auto queued = [this](const aoc::debug::GameControlCommand& cmd) -> std::string {
        std::lock_guard<std::mutex> guard(this->m_pendingCommandsMutex);
        this->m_pendingCommands.push_back(cmd);
        return std::string("{\"queued\":true}");
    };

    // POST /game/builder/improve?player=&q=&r=&type=   (ImprovementType value)
    this->m_debugServer->routeJson(
        DSM::Post, "/game/builder/improve",
        [this, readUnit, queued](const Query& q, const std::string&) -> std::string {
            if (this->m_appState != AppState::InGame) {
                throw aoc::debug::ServiceUnavailableError("no active game");
            }
            int32_t player = 0;
            int32_t type   = 0;
            aoc::hex::AxialCoord at{};
            std::string err;
            if (!readUnit(q, player, at, err) || !readIntParam(q, "type", type, err)) {
                return err;
            }
            if (type <= 0 || type >= static_cast<int32_t>(aoc::map::ImprovementType::Count)) {
                return std::string("{\"error\":\"type out of range\"}");
            }
            aoc::debug::PlaceImprovementCommand cmd{};
            cmd.player = static_cast<aoc::PlayerId>(player);
            cmd.at     = at;
            cmd.type   = static_cast<aoc::map::ImprovementType>(type);
            return queued(cmd);
        });

    // POST /game/builder/chop?player=&q=&r=
    this->m_debugServer->routeJson(
        DSM::Post, "/game/builder/chop",
        [this, readUnit, queued](const Query& q, const std::string&) -> std::string {
            if (this->m_appState != AppState::InGame) {
                throw aoc::debug::ServiceUnavailableError("no active game");
            }
            int32_t player = 0;
            aoc::hex::AxialCoord at{};
            std::string err;
            if (!readUnit(q, player, at, err)) {
                return err;
            }
            aoc::debug::BuilderChopCommand cmd{};
            cmd.player = static_cast<aoc::PlayerId>(player);
            cmd.at     = at;
            return queued(cmd);
        });

    // POST /game/builder/harvest?player=&q=&r=
    this->m_debugServer->routeJson(
        DSM::Post, "/game/builder/harvest",
        [this, readUnit, queued](const Query& q, const std::string&) -> std::string {
            if (this->m_appState != AppState::InGame) {
                throw aoc::debug::ServiceUnavailableError("no active game");
            }
            int32_t player = 0;
            aoc::hex::AxialCoord at{};
            std::string err;
            if (!readUnit(q, player, at, err)) {
                return err;
            }
            aoc::debug::BuilderHarvestCommand cmd{};
            cmd.player = static_cast<aoc::PlayerId>(player);
            cmd.at     = at;
            return queued(cmd);
        });
}

void Application::registerUnitOrderRoutes() {
    using DSM           = aoc::debug::DebugServer::Method;
    using Query         = std::unordered_map<std::string, std::string>;
    const auto readUnit = [](const Query& q, int32_t& player, aoc::hex::AxialCoord& at,
                             std::string& err) -> bool {
        int32_t uq = 0;
        int32_t ur = 0;
        if (!readIntParam(q, "player", player, err) || !readIntParam(q, "q", uq, err) ||
            !readIntParam(q, "r", ur, err)) {
            return false;
        }
        at = aoc::hex::AxialCoord{uq, ur};
        return true;
    };
    const auto queued = [this](const aoc::debug::GameControlCommand& cmd) -> std::string {
        std::lock_guard<std::mutex> guard(this->m_pendingCommandsMutex);
        this->m_pendingCommands.push_back(cmd);
        return std::string("{\"queued\":true}");
    };
    const auto simpleRoute = [this, readUnit, queued](const char* path, auto makeCommand) {
        this->m_debugServer->routeJson(
            DSM::Post, path,
            [this, readUnit, queued, makeCommand](const Query& q,
                                                  const std::string&) -> std::string {
                if (this->m_appState != AppState::InGame) {
                    throw aoc::debug::ServiceUnavailableError("no active game");
                }
                int32_t player = 0;
                aoc::hex::AxialCoord at{};
                std::string err;
                if (!readUnit(q, player, at, err)) {
                    return err;
                }
                return queued(makeCommand(static_cast<aoc::PlayerId>(player), at));
            });
    };
    // POST /game/unit/pillage?player=&q=&r=
    simpleRoute("/game/unit/pillage", [](aoc::PlayerId player, aoc::hex::AxialCoord at) {
        return aoc::debug::GameControlCommand{aoc::debug::PillageCommand{player, at}};
    });
    // POST /game/builder/repair?player=&q=&r=
    simpleRoute("/game/builder/repair", [](aoc::PlayerId player, aoc::hex::AxialCoord at) {
        return aoc::debug::GameControlCommand{aoc::debug::RepairCommand{player, at}};
    });
    // POST /game/unit/delete?player=&q=&r=
    simpleRoute("/game/unit/delete", [](aoc::PlayerId player, aoc::hex::AxialCoord at) {
        return aoc::debug::GameControlCommand{aoc::debug::DeleteUnitCommand{player, at}};
    });
    // POST /game/unit/promote?player=&q=&r=&promotion=
    this->m_debugServer->routeJson(
        DSM::Post, "/game/unit/promote",
        [this, readUnit, queued](const Query& q, const std::string&) -> std::string {
            if (this->m_appState != AppState::InGame) {
                throw aoc::debug::ServiceUnavailableError("no active game");
            }
            int32_t player    = 0;
            int32_t promotion = 0;
            aoc::hex::AxialCoord at{};
            std::string err;
            if (!readUnit(q, player, at, err) || !readIntParam(q, "promotion", promotion, err)) {
                return err;
            }
            if (promotion < 0 ||
                promotion >= static_cast<int32_t>(aoc::sim::PROMOTION_DEFS.size())) {
                return std::string("{\"error\":\"promotion out of range\"}");
            }
            return queued(
                aoc::debug::PromoteUnitCommand{static_cast<aoc::PlayerId>(player), at,
                                               aoc::PromotionId{static_cast<uint8_t>(promotion)}});
        });

    // POST /game/unit/alert?player=&q=&r=&on=
    this->m_debugServer->routeJson(
        DSM::Post, "/game/unit/alert",
        [this, readUnit, queued](const Query& q, const std::string&) -> std::string {
            if (this->m_appState != AppState::InGame) {
                throw aoc::debug::ServiceUnavailableError("no active game");
            }
            int32_t player = 0;
            int32_t on     = 1;
            aoc::hex::AxialCoord at{};
            std::string err;
            if (!readUnit(q, player, at, err)) {
                return err;
            }
            if (q.count("on") != 0 && !readIntParam(q, "on", on, err)) {
                return err;
            }
            return queued(
                aoc::debug::SetAlertCommand{static_cast<aoc::PlayerId>(player), at, on != 0});
        });
}

void Application::registerReligionRoutes() {
    using DSM         = aoc::debug::DebugServer::Method;
    using Query       = std::unordered_map<std::string, std::string>;
    const auto queued = [this](const aoc::debug::GameControlCommand& cmd) -> std::string {
        std::lock_guard<std::mutex> guard(this->m_pendingCommandsMutex);
        this->m_pendingCommands.push_back(cmd);
        return std::string("{\"queued\":true}");
    };
    // POST /game/religion/pantheon?player=&belief=
    this->m_debugServer->routeJson(
        DSM::Post, "/game/religion/pantheon",
        [this, queued](const Query& q, const std::string&) -> std::string {
            if (this->m_appState != AppState::InGame) {
                throw aoc::debug::ServiceUnavailableError("no active game");
            }
            int32_t player = 0;
            int32_t belief = 0;
            std::string err;
            if (!readIntParam(q, "player", player, err) ||
                !readIntParam(q, "belief", belief, err)) {
                return err;
            }
            if (belief < 0 || belief >= static_cast<int32_t>(aoc::sim::BELIEF_COUNT)) {
                return std::string("{\"error\":\"belief out of range\"}");
            }
            return queued(aoc::debug::FoundPantheonCommand{static_cast<aoc::PlayerId>(player),
                                                           static_cast<uint8_t>(belief)});
        });
    // POST /game/religion/found?player=&founder=&worship=&enhancer=
    this->m_debugServer->routeJson(
        DSM::Post, "/game/religion/found",
        [this, queued](const Query& q, const std::string&) -> std::string {
            if (this->m_appState != AppState::InGame) {
                throw aoc::debug::ServiceUnavailableError("no active game");
            }
            int32_t player   = 0;
            int32_t founder  = 0;
            int32_t worship  = 0;
            int32_t enhancer = 0;
            std::string err;
            if (!readIntParam(q, "player", player, err) ||
                !readIntParam(q, "founder", founder, err) ||
                !readIntParam(q, "worship", worship, err) ||
                !readIntParam(q, "enhancer", enhancer, err)) {
                return err;
            }
            const int32_t count = static_cast<int32_t>(aoc::sim::BELIEF_COUNT);
            if (founder < 0 || founder >= count || worship < 0 || worship >= count ||
                enhancer < 0 || enhancer >= count) {
                return std::string("{\"error\":\"belief out of range\"}");
            }
            return queued(aoc::debug::FoundReligionCommand{
                static_cast<aoc::PlayerId>(player), static_cast<uint8_t>(founder),
                static_cast<uint8_t>(worship), static_cast<uint8_t>(enhancer)});
        });
}

namespace {

void warnRejected(const char* what, aoc::PlayerId player, aoc::hex::AxialCoord at, ErrorCode rc) {
    LOG_WARN("%s for player %u at (%d,%d) rejected: %.*s", what, static_cast<unsigned>(player),
             at.q, at.r, static_cast<int>(describeError(rc).size()), describeError(rc).data());
}

} // namespace

void Application::executeGameControlCommand(const aoc::debug::CityPurchaseCommand& cmd) {
    const ErrorCode rc = cmd.withFaith
                             ? aoc::sim::requestFaithPurchase(this->m_gameState, cmd.player, cmd.at,
                                                              aoc::UnitTypeId{cmd.itemId})
                             : aoc::sim::requestPurchase(this->m_gameState, &this->m_hexGrid,
                                                         cmd.player, cmd.at, cmd.type, cmd.itemId);
    if (rc != ErrorCode::Ok) {
        warnRejected("Purchase", cmd.player, cmd.at, rc);
    }
}

void Application::executeGameControlCommand(const aoc::debug::SetCityFocusCommand& cmd) {
    const ErrorCode rc = aoc::sim::requestSetCityFocus(this->m_gameState, this->m_hexGrid,
                                                       cmd.player, cmd.at, cmd.focus);
    if (rc != ErrorCode::Ok) {
        warnRejected("City focus", cmd.player, cmd.at, rc);
    }
}

void Application::executeGameControlCommand(const aoc::debug::ToggleTileLockCommand& cmd) {
    const ErrorCode rc =
        aoc::sim::requestToggleTileLock(this->m_gameState, cmd.player, cmd.at, cmd.tile);
    if (rc != ErrorCode::Ok) {
        warnRejected("Tile lock", cmd.player, cmd.at, rc);
    }
}

void Application::executeGameControlCommand(const aoc::debug::RemoveQueueItemCommand& cmd) {
    const ErrorCode rc =
        aoc::sim::requestRemoveQueueItem(this->m_gameState, cmd.player, cmd.at, cmd.index);
    if (rc != ErrorCode::Ok) {
        warnRejected("Queue removal", cmd.player, cmd.at, rc);
    }
}

void Application::executeGameControlCommand(const aoc::debug::QueueProjectCommand& cmd) {
    const ErrorCode rc =
        aoc::sim::requestQueueProject(this->m_gameState, cmd.player, cmd.at, cmd.project);
    if (rc != ErrorCode::Ok) {
        warnRejected("City project", cmd.player, cmd.at, rc);
    }
}

void Application::executeGameControlCommand(const aoc::debug::PlaceImprovementCommand& cmd) {
    const ErrorCode rc       = aoc::sim::requestPlaceImprovement(this->m_gameState, this->m_hexGrid,
                                                                 cmd.player, cmd.at, cmd.type);
    if (rc != ErrorCode::Ok) {
        warnRejected("Improvement", cmd.player, cmd.at, rc);
        return;
    }
    this->finishBuilderAction(cmd.player, cmd.at);
}

void Application::executeGameControlCommand(const aoc::debug::BuilderChopCommand& cmd) {
    const ErrorCode rc =
        aoc::sim::requestChop(this->m_gameState, this->m_hexGrid, cmd.player, cmd.at);
    if (rc != ErrorCode::Ok) {
        warnRejected("Chop", cmd.player, cmd.at, rc);
        return;
    }
    this->finishBuilderAction(cmd.player, cmd.at);
}

void Application::executeGameControlCommand(const aoc::debug::BuilderHarvestCommand& cmd) {
    const ErrorCode rc =
        aoc::sim::requestHarvest(this->m_gameState, this->m_hexGrid, cmd.player, cmd.at);
    if (rc != ErrorCode::Ok) {
        warnRejected("Harvest", cmd.player, cmd.at, rc);
        return;
    }
    this->finishBuilderAction(cmd.player, cmd.at);
}

void Application::executeGameControlCommand(const aoc::debug::PillageCommand& cmd) {
    const ErrorCode rc = aoc::sim::requestPillage(this->m_gameState, this->m_hexGrid, cmd.player,
                                                  cmd.at, &this->m_diplomacy);
    if (rc != ErrorCode::Ok) {
        warnRejected("Pillage", cmd.player, cmd.at, rc);
    }
}

void Application::executeGameControlCommand(const aoc::debug::RepairCommand& cmd) {
    const ErrorCode rc =
        aoc::sim::requestRepair(this->m_gameState, this->m_hexGrid, cmd.player, cmd.at);
    if (rc != ErrorCode::Ok) {
        warnRejected("Repair", cmd.player, cmd.at, rc);
        return;
    }
    this->finishBuilderAction(cmd.player, cmd.at);
}

void Application::executeGameControlCommand(const aoc::debug::DeleteUnitCommand& cmd) {
    aoc::game::Player* owner = this->m_gameState.player(cmd.player);
    aoc::game::Unit* unit    = owner != nullptr ? owner->unitAt(cmd.at) : nullptr;
    if (unit != nullptr && unit == this->m_selectedUnit) {
        this->m_selectedUnit    = nullptr;
        this->m_actionPanelUnit = nullptr;
    }
    const ErrorCode rc =
        aoc::sim::requestDeleteUnit(this->m_gameState, this->m_hexGrid, cmd.player, cmd.at);
    if (rc != ErrorCode::Ok) {
        warnRejected("Delete", cmd.player, cmd.at, rc);
    }
}

void Application::executeGameControlCommand(const aoc::debug::SetAlertCommand& cmd) {
    const ErrorCode rc =
        aoc::sim::requestSetAlert(this->m_gameState, cmd.player, cmd.at, cmd.alert);
    if (rc != ErrorCode::Ok) {
        warnRejected("Alert", cmd.player, cmd.at, rc);
    }
}

void Application::executeGameControlCommand(const aoc::debug::PromoteUnitCommand& cmd) {
    const ErrorCode rc =
        aoc::sim::requestPromotion(this->m_gameState, cmd.player, cmd.at, cmd.promotion);
    if (rc != ErrorCode::Ok) {
        warnRejected("Promotion", cmd.player, cmd.at, rc);
    }
}

void Application::executeGameControlCommand(const aoc::debug::FoundPantheonCommand& cmd) {
    const ErrorCode rc = aoc::sim::requestFoundPantheon(this->m_gameState, cmd.player, cmd.belief);
    if (rc != ErrorCode::Ok) {
        LOG_WARN("Pantheon for player %u rejected: %.*s", static_cast<unsigned>(cmd.player),
                 static_cast<int>(describeError(rc).size()), describeError(rc).data());
    }
}

void Application::executeGameControlCommand(const aoc::debug::FoundReligionCommand& cmd) {
    const ErrorCode rc = aoc::sim::requestFoundReligion(this->m_gameState, cmd.player, cmd.founder,
                                                        cmd.worship, cmd.enhancer);
    if (rc != ErrorCode::Ok) {
        LOG_WARN("Religion for player %u rejected: %.*s", static_cast<unsigned>(cmd.player),
                 static_cast<int>(describeError(rc).size()), describeError(rc).data());
    }
}

void Application::registerCultureRoutes() {
    using DSM   = aoc::debug::DebugServer::Method;
    using Query = std::unordered_map<std::string, std::string>;

    this->m_debugServer->routeJson(
        DSM::Post, "/game/greatwork/move",
        [this](const Query& q, const std::string&) -> std::string {
            if (this->m_appState != AppState::InGame) {
                throw aoc::debug::ServiceUnavailableError("no active game");
            }
            int32_t player = 0;
            int32_t fromQ  = 0;
            int32_t fromR  = 0;
            int32_t index  = 0;
            int32_t toQ    = 0;
            int32_t toR    = 0;
            std::string err;
            if (!readIntParam(q, "player", player, err) || !readIntParam(q, "q", fromQ, err) ||
                !readIntParam(q, "r", fromR, err) || !readIntParam(q, "index", index, err) ||
                !readIntParam(q, "toQ", toQ, err) || !readIntParam(q, "toR", toR, err)) {
                return err;
            }
            std::lock_guard<std::mutex> guard(this->m_pendingCommandsMutex);
            this->m_pendingCommands.push_back(aoc::debug::MoveGreatWorkCommand{
                static_cast<aoc::PlayerId>(player), {fromQ, fromR}, index, {toQ, toR}});
            return std::string("{\"queued\":true}");
        });
}

void Application::executeGameControlCommand(const aoc::debug::MoveGreatWorkCommand& cmd) {
    const ErrorCode rc =
        aoc::sim::requestMoveGreatWork(this->m_gameState, cmd.player, cmd.from, cmd.index, cmd.to);
    if (rc != ErrorCode::Ok) {
        LOG_WARN("Great Work move for player %u rejected: %.*s", static_cast<unsigned>(cmd.player),
                 static_cast<int>(describeError(rc).size()), describeError(rc).data());
    }
}

void Application::registerCityStateRoutes() {
    using DSM   = aoc::debug::DebugServer::Method;
    using Query = std::unordered_map<std::string, std::string>;

    // player + index, then one command type per route.
    const auto csRoute = [this](const char* path, auto makeCommand) {
        this->m_debugServer->routeJson(
            DSM::Post, path,
            [this, makeCommand](const Query& q, const std::string&) -> std::string {
                if (this->m_appState != AppState::InGame) {
                    throw aoc::debug::ServiceUnavailableError("no active game");
                }
                int32_t player = 0;
                int32_t index  = 0;
                std::string err;
                if (!readIntParam(q, "player", player, err) ||
                    !readIntParam(q, "index", index, err)) {
                    return err;
                }
                if (index < 0 ||
                    static_cast<std::size_t>(index) >= this->m_gameState.cityStates().size()) {
                    return std::string("{\"error\":\"index out of range\"}");
                }
                std::lock_guard<std::mutex> guard(this->m_pendingCommandsMutex);
                this->m_pendingCommands.push_back(
                    makeCommand(static_cast<aoc::PlayerId>(player), index));
                return std::string("{\"queued\":true}");
            });
    };
    csRoute("/game/citystate/envoy",
            [](aoc::PlayerId p, int32_t i) -> aoc::debug::GameControlCommand {
                return aoc::debug::SendEnvoyCommand{p, i};
            });
    csRoute("/game/citystate/levy",
            [](aoc::PlayerId p, int32_t i) -> aoc::debug::GameControlCommand {
                return aoc::debug::LevyCityStateCommand{p, i};
            });
    csRoute("/game/citystate/bully",
            [](aoc::PlayerId p, int32_t i) -> aoc::debug::GameControlCommand {
                return aoc::debug::BullyCityStateCommand{p, i};
            });

    this->m_debugServer->routeJson(
        DSM::Get, "/game/citystates", [this](const Query& q, const std::string&) -> std::string {
            if (this->m_appState != AppState::InGame) {
                throw aoc::debug::ServiceUnavailableError("no active game");
            }
            int32_t player = 0;
            std::string err;
            if (!readIntParam(q, "player", player, err)) {
                return err;
            }
            const aoc::PlayerId pid     = static_cast<aoc::PlayerId>(player);
            const aoc::game::Player* me = this->m_gameState.player(pid);
            std::string json =
                "{\"available\":" + std::to_string(me != nullptr ? me->envoys().available : 0) +
                ",\"cityStates\":[";
            const std::vector<aoc::sim::CityStateComponent>& states =
                this->m_gameState.cityStates();
            for (std::size_t i = 0; i < states.size(); ++i) {
                const aoc::sim::CityStateComponent& cs = states[i];
                const int32_t mine                     = pid < MAX_PLAYERS ? cs.envoys[pid] : 0;
                const std::string_view name = cs.defId < aoc::sim::CITY_STATE_DEFS.size()
                                                  ? aoc::sim::CITY_STATE_DEFS[cs.defId].name
                                                  : std::string_view("?");
                if (i > 0) {
                    json += ",";
                }
                json += "{\"index\":" + std::to_string(i) + ",\"name\":\"" + std::string(name) +
                        "\",\"type\":\"" + std::string(aoc::sim::cityStateTypeName(cs.type)) +
                        "\",\"met\":" + (cs.hasMet(pid) ? "true" : "false") +
                        ",\"envoys\":" + std::to_string(mine) +
                        ",\"suzerain\":" + std::to_string(static_cast<unsigned>(cs.suzerain)) +
                        ",\"q\":" + std::to_string(cs.location.q) +
                        ",\"r\":" + std::to_string(cs.location.r) + ",\"questActive\":" +
                        (cs.activeQuest.isActive && cs.activeQuest.assignedTo == pid ? "true"
                                                                                     : "false") +
                        ",\"levyPlayer\":" + std::to_string(static_cast<unsigned>(cs.levyPlayer)) +
                        "}";
            }
            json += "]}";
            return json;
        });
}

void Application::executeGameControlCommand(const aoc::debug::SendEnvoyCommand& cmd) {
    const ErrorCode rc = aoc::sim::requestSendEnvoy(this->m_gameState, cmd.player,
                                                    static_cast<std::size_t>(cmd.index));
    if (rc != ErrorCode::Ok) {
        LOG_WARN("Envoy from player %u rejected: %.*s", static_cast<unsigned>(cmd.player),
                 static_cast<int>(describeError(rc).size()), describeError(rc).data());
    }
}

void Application::executeGameControlCommand(const aoc::debug::LevyCityStateCommand& cmd) {
    const ErrorCode rc = aoc::sim::requestLevyCityState(this->m_gameState, cmd.player,
                                                        static_cast<std::size_t>(cmd.index));
    if (rc != ErrorCode::Ok) {
        LOG_WARN("Levy by player %u rejected: %.*s", static_cast<unsigned>(cmd.player),
                 static_cast<int>(describeError(rc).size()), describeError(rc).data());
    }
}

void Application::executeGameControlCommand(const aoc::debug::BullyCityStateCommand& cmd) {
    const ErrorCode rc = aoc::sim::requestBullyCityState(this->m_gameState, cmd.player,
                                                         static_cast<std::size_t>(cmd.index));
    if (rc != ErrorCode::Ok) {
        LOG_WARN("Bully by player %u rejected: %.*s", static_cast<unsigned>(cmd.player),
                 static_cast<int>(describeError(rc).size()), describeError(rc).data());
    }
}

void Application::registerDiplomacyRoutes() {
    using DSM   = aoc::debug::DebugServer::Method;
    using Query = std::unordered_map<std::string, std::string>;

    // player + target, one command type per route; war also takes cb (CasusBelliType index).
    const auto pairRoute = [this](const char* path, bool withCasusBelli, auto makeCommand) {
        this->m_debugServer->routeJson(
            DSM::Post, path,
            [this, withCasusBelli, makeCommand](const Query& q, const std::string&) -> std::string {
                if (this->m_appState != AppState::InGame) {
                    throw aoc::debug::ServiceUnavailableError("no active game");
                }
                int32_t player = 0;
                int32_t target = 0;
                int32_t cb     = 0;
                std::string err;
                if (!readIntParam(q, "player", player, err) ||
                    !readIntParam(q, "target", target, err)) {
                    return err;
                }
                if (withCasusBelli && !readIntParam(q, "cb", cb, err)) {
                    return err;
                }
                if (cb < 0 || cb >= aoc::sim::CASUS_BELLI_COUNT) {
                    return std::string("{\"error\":\"cb out of range\"}");
                }
                if (player < 0 || target < 0 || player >= MAX_PLAYERS || target >= MAX_PLAYERS) {
                    return std::string("{\"error\":\"player or target out of range\"}");
                }
                std::lock_guard<std::mutex> guard(this->m_pendingCommandsMutex);
                this->m_pendingCommands.push_back(makeCommand(static_cast<aoc::PlayerId>(player),
                                                              static_cast<aoc::PlayerId>(target),
                                                              static_cast<uint8_t>(cb)));
                return std::string("{\"queued\":true}");
            });
    };
    pairRoute("/game/diplomacy/war", true,
              [](aoc::PlayerId p, aoc::PlayerId t, uint8_t cb) -> aoc::debug::GameControlCommand {
                  return aoc::debug::DeclareWarCommand{p, t, cb};
              });
    pairRoute("/game/diplomacy/peace", false,
              [](aoc::PlayerId p, aoc::PlayerId t, uint8_t) -> aoc::debug::GameControlCommand {
                  return aoc::debug::MakePeaceCommand{p, t};
              });
    pairRoute("/game/diplomacy/denounce", false,
              [](aoc::PlayerId p, aoc::PlayerId t, uint8_t) -> aoc::debug::GameControlCommand {
                  return aoc::debug::DenounceCommand{p, t};
              });
    pairRoute("/game/diplomacy/friendship", false,
              [](aoc::PlayerId p, aoc::PlayerId t, uint8_t) -> aoc::debug::GameControlCommand {
                  return aoc::debug::DeclareFriendshipCommand{p, t};
              });
    pairRoute("/game/diplomacy/delegation", false,
              [](aoc::PlayerId p, aoc::PlayerId t, uint8_t) -> aoc::debug::GameControlCommand {
                  return aoc::debug::SendDelegationCommand{p, t};
              });
    pairRoute("/game/diplomacy/embassy", false,
              [](aoc::PlayerId p, aoc::PlayerId t, uint8_t) -> aoc::debug::GameControlCommand {
                  return aoc::debug::EstablishEmbassyCommand{p, t};
              });
    pairRoute("/game/diplomacy/borders", false,
              [](aoc::PlayerId p, aoc::PlayerId t, uint8_t) -> aoc::debug::GameControlCommand {
                  return aoc::debug::OpenBordersCommand{p, t};
              });

    this->m_debugServer->routeJson(
        DSM::Get, "/game/diplomacy", [this](const Query& q, const std::string&) -> std::string {
            if (this->m_appState != AppState::InGame) {
                throw aoc::debug::ServiceUnavailableError("no active game");
            }
            int32_t player = 0;
            std::string err;
            if (!readIntParam(q, "player", player, err)) {
                return err;
            }
            if (player < 0 || player >= static_cast<int32_t>(this->m_diplomacy.playerCount())) {
                return std::string("{\"error\":\"player out of range\"}");
            }
            const aoc::PlayerId me = static_cast<aoc::PlayerId>(player);
            const int32_t turn     = this->m_gameState.currentTurn();
            std::string json       = "{\"turn\":" + std::to_string(turn) + ",\"relations\":[";
            bool first             = true;
            for (const std::unique_ptr<aoc::game::Player>& other : this->m_gameState.players()) {
                if (other == nullptr || other->id() == me ||
                    other->id() >= this->m_diplomacy.playerCount()) {
                    continue;
                }
                const aoc::sim::PairwiseRelation& rel = this->m_diplomacy.relation(me, other->id());
                if (!first) {
                    json += ",";
                }
                first = false;
                json += "{\"id\":" + std::to_string(static_cast<unsigned>(other->id())) +
                        ",\"met\":" + (rel.hasMet ? "true" : "false") +
                        ",\"atWar\":" + (rel.isAtWar ? "true" : "false") +
                        ",\"score\":" + std::to_string(rel.totalScore()) + ",\"stance\":\"" +
                        std::string(aoc::sim::stanceName(rel.stance())) + "\"" +
                        ",\"openBorders\":" + (rel.hasOpenBorders ? "true" : "false") +
                        ",\"openBordersUntil\":" + std::to_string(rel.openBordersUntilTurn) +
                        ",\"friendsUntil\":" + std::to_string(rel.friendshipUntilTurn) +
                        ",\"denouncedOn\":" + std::to_string(rel.denouncedOnTurn) +
                        ",\"delegation\":" + (rel.hasDelegation ? "true" : "false") +
                        ",\"embassy\":" + (rel.hasEmbassy ? "true" : "false") +
                        ",\"turnsSincePeace\":" + std::to_string(rel.turnsSincePeace) +
                        ",\"warDeclaredOn\":" + std::to_string(rel.warDeclaredOnTurn) +
                        ",\"casusBelli\":[";
                bool firstCb = true;
                for (const aoc::sim::CasusBelliType cb : aoc::sim::availableCasusBelli(
                         this->m_gameState, this->m_diplomacy, me, other->id(), turn)) {
                    if (!firstCb) {
                        json += ",";
                    }
                    firstCb = false;
                    json += std::to_string(static_cast<int>(cb));
                }
                json += "]}";
            }
            json += "]}";
            return json;
        });
}

namespace {

void logDiplomacyResult(const char* what, aoc::PlayerId player, aoc::PlayerId target,
                        ErrorCode rc) {
    if (rc != ErrorCode::Ok) {
        LOG_WARN("%s by player %u toward %u rejected: %.*s", what, static_cast<unsigned>(player),
                 static_cast<unsigned>(target), static_cast<int>(describeError(rc).size()),
                 describeError(rc).data());
    }
}

} // namespace

void Application::executeGameControlCommand(const aoc::debug::DeclareWarCommand& cmd) {
    logDiplomacyResult(
        "Declare war", cmd.player, cmd.target,
        aoc::sim::requestDeclareWar(this->m_gameState, this->m_diplomacy, cmd.player, cmd.target,
                                    static_cast<aoc::sim::CasusBelliType>(cmd.casusBelli),
                                    this->m_gameState.currentTurn(), &this->m_allianceTracker));
}

void Application::executeGameControlCommand(const aoc::debug::MakePeaceCommand& cmd) {
    logDiplomacyResult("Peace", cmd.player, cmd.target,
                       aoc::sim::requestMakePeace(this->m_gameState, this->m_diplomacy, cmd.player,
                                                  cmd.target, this->m_gameState.currentTurn()));
}

void Application::executeGameControlCommand(const aoc::debug::DenounceCommand& cmd) {
    logDiplomacyResult("Denounce", cmd.player, cmd.target,
                       aoc::sim::requestDenounce(this->m_gameState, this->m_diplomacy, cmd.player,
                                                 cmd.target, this->m_gameState.currentTurn()));
}

void Application::executeGameControlCommand(const aoc::debug::DeclareFriendshipCommand& cmd) {
    logDiplomacyResult("Friendship", cmd.player, cmd.target,
                       aoc::sim::requestDeclareFriendship(this->m_gameState, this->m_diplomacy,
                                                          cmd.player, cmd.target,
                                                          this->m_gameState.currentTurn()));
}

void Application::executeGameControlCommand(const aoc::debug::SendDelegationCommand& cmd) {
    logDiplomacyResult("Delegation", cmd.player, cmd.target,
                       aoc::sim::requestSendDelegation(this->m_gameState, this->m_diplomacy,
                                                       cmd.player, cmd.target));
}

void Application::executeGameControlCommand(const aoc::debug::EstablishEmbassyCommand& cmd) {
    logDiplomacyResult("Embassy", cmd.player, cmd.target,
                       aoc::sim::requestEstablishEmbassy(this->m_gameState, this->m_diplomacy,
                                                         cmd.player, cmd.target));
}

void Application::executeGameControlCommand(const aoc::debug::OpenBordersCommand& cmd) {
    logDiplomacyResult("Open borders", cmd.player, cmd.target,
                       aoc::sim::requestOpenBorders(this->m_gameState, this->m_diplomacy,
                                                    cmd.player, cmd.target,
                                                    this->m_gameState.currentTurn()));
}

namespace {

/// Optional integer query parameter: absent means `fallback`, malformed is an error.
bool readOptionalInt(const std::unordered_map<std::string, std::string>& query, const char* name,
                     int32_t fallback, int32_t& out, std::string& errorJson) {
    if (query.find(name) == query.end()) {
        out = fallback;
        return true;
    }
    return readIntParam(query, name, out, errorJson);
}

std::string dealTermsJson(const aoc::game::GameState& gameState,
                          const aoc::sim::DiplomaticDeal& deal) {
    std::string json = "[";
    for (std::size_t t = 0; t < deal.terms.size(); ++t) {
        if (t > 0) {
            json += ",";
        }
        json += "\"" + aoc::sim::describeDealTerm(gameState, deal.terms[t]) + "\"";
    }
    return json + "]";
}

} // namespace

void Application::registerDealRoutes() {
    using DSM   = aoc::debug::DebugServer::Method;
    using Query = std::unordered_map<std::string, std::string>;

    this->m_debugServer->routeJson(
        DSM::Post, "/game/deal/propose", [this](const Query& q, const std::string&) -> std::string {
            if (this->m_appState != AppState::InGame) {
                throw aoc::debug::ServiceUnavailableError("no active game");
            }
            int32_t player        = 0;
            int32_t target        = 0;
            int32_t giveGold      = 0;
            int32_t askGold       = 0;
            int32_t openBorders   = 0;
            int32_t nonAggression = 0;
            std::string err;
            if (!readIntParam(q, "player", player, err) ||
                !readIntParam(q, "target", target, err) ||
                !readOptionalInt(q, "giveGold", 0, giveGold, err) ||
                !readOptionalInt(q, "askGold", 0, askGold, err) ||
                !readOptionalInt(q, "openBorders", 0, openBorders, err) ||
                !readOptionalInt(q, "nonAggression", 0, nonAggression, err)) {
                return err;
            }
            if (player < 0 || target < 0 || player >= MAX_PLAYERS || target >= MAX_PLAYERS ||
                giveGold < 0 || askGold < 0) {
                return std::string("{\"error\":\"player, target or gold out of range\"}");
            }
            std::lock_guard<std::mutex> guard(this->m_pendingCommandsMutex);
            this->m_pendingCommands.push_back(aoc::debug::ProposeDealCommand{
                static_cast<aoc::PlayerId>(player), static_cast<aoc::PlayerId>(target), giveGold,
                askGold, openBorders != 0, nonAggression != 0});
            return std::string("{\"queued\":true}");
        });

    this->m_debugServer->routeJson(
        DSM::Post, "/game/deal/respond", [this](const Query& q, const std::string&) -> std::string {
            if (this->m_appState != AppState::InGame) {
                throw aoc::debug::ServiceUnavailableError("no active game");
            }
            int32_t player = 0;
            int32_t index  = 0;
            int32_t accept = 0;
            std::string err;
            if (!readIntParam(q, "player", player, err) || !readIntParam(q, "index", index, err) ||
                !readIntParam(q, "accept", accept, err)) {
                return err;
            }
            if (player < 0 || player >= MAX_PLAYERS || index < 0 ||
                static_cast<std::size_t>(index) >= this->m_gameState.pendingProposals().size()) {
                return std::string("{\"error\":\"player or index out of range\"}");
            }
            std::lock_guard<std::mutex> guard(this->m_pendingCommandsMutex);
            this->m_pendingCommands.push_back(aoc::debug::RespondProposalCommand{
                static_cast<aoc::PlayerId>(player), index, accept != 0});
            return std::string("{\"queued\":true}");
        });

    this->m_debugServer->routeJson(
        DSM::Get, "/game/deals", [this](const Query& q, const std::string&) -> std::string {
            if (this->m_appState != AppState::InGame) {
                throw aoc::debug::ServiceUnavailableError("no active game");
            }
            int32_t player = 0;
            std::string err;
            if (!readIntParam(q, "player", player, err)) {
                return err;
            }
            const aoc::PlayerId me = static_cast<aoc::PlayerId>(player);
            std::string json       = "{\"inbox\":[";
            bool first             = true;
            const std::vector<aoc::sim::PendingProposal>& inbox =
                this->m_gameState.pendingProposals();
            for (std::size_t i = 0; i < inbox.size(); ++i) {
                if (inbox[i].to != me) {
                    continue;
                }
                if (!first) {
                    json += ",";
                }
                first = false;
                json += "{\"index\":" + std::to_string(i) +
                        ",\"from\":" + std::to_string(static_cast<unsigned>(inbox[i].from)) +
                        ",\"expiresTurn\":" + std::to_string(inbox[i].expiresTurn) +
                        ",\"terms\":" + dealTermsJson(this->m_gameState, inbox[i].deal) + "}";
            }
            json += "],\"activeDeals\":[";
            first = true;
            for (const aoc::sim::DiplomaticDeal& deal : this->m_gameState.deals().activeDeals) {
                if (deal.playerA != me && deal.playerB != me) {
                    continue;
                }
                if (!first) {
                    json += ",";
                }
                first = false;
                json += "{\"with\":" +
                        std::to_string(static_cast<unsigned>(deal.playerA == me ? deal.playerB
                                                                                : deal.playerA)) +
                        ",\"accepted\":" + (deal.isAccepted ? "true" : "false") +
                        ",\"broken\":" + (deal.isBroken ? "true" : "false") +
                        ",\"turnsRemaining\":" + std::to_string(deal.turnsRemaining) +
                        ",\"terms\":" + dealTermsJson(this->m_gameState, deal) + "}";
            }
            json += "]}";
            return json;
        });
}

void Application::executeGameControlCommand(const aoc::debug::ProposeDealCommand& cmd) {
    aoc::sim::DiplomaticDeal deal;
    deal.playerA = cmd.player;
    deal.playerB = cmd.target;
    if (cmd.giveGold > 0) {
        aoc::sim::DealTerm t{};
        t.type       = aoc::sim::DealTermType::GoldLump;
        t.fromPlayer = cmd.player;
        t.toPlayer   = cmd.target;
        t.goldLump   = cmd.giveGold;
        deal.terms.push_back(t);
    }
    if (cmd.askGold > 0) {
        aoc::sim::DealTerm t{};
        t.type       = aoc::sim::DealTermType::GoldLump;
        t.fromPlayer = cmd.target;
        t.toPlayer   = cmd.player;
        t.goldLump   = cmd.askGold;
        deal.terms.push_back(t);
    }
    for (const aoc::sim::DealTermType pact :
         {aoc::sim::DealTermType::OpenBorders, aoc::sim::DealTermType::NonAggression}) {
        const bool wanted =
            pact == aoc::sim::DealTermType::OpenBorders ? cmd.openBorders : cmd.nonAggression;
        if (!wanted) {
            continue;
        }
        aoc::sim::DealTerm t{};
        t.type       = pact;
        t.fromPlayer = cmd.player;
        t.toPlayer   = cmd.target;
        t.duration   = 30;
        deal.terms.push_back(t);
    }
    logDiplomacyResult("Deal proposal", cmd.player, cmd.target,
                       aoc::sim::requestProposeDeal(this->m_gameState, this->m_hexGrid,
                                                    this->m_gameState.deals(), this->m_diplomacy, deal,
                                                    this->m_gameState.currentTurn()));
}

void Application::executeGameControlCommand(const aoc::debug::RespondProposalCommand& cmd) {
    const ErrorCode rc = aoc::sim::requestRespondToProposal(
        this->m_gameState, this->m_hexGrid, this->m_gameState.deals(), this->m_diplomacy, cmd.player,
        static_cast<std::size_t>(cmd.index), cmd.accept, this->m_gameState.currentTurn());
    if (rc != ErrorCode::Ok) {
        LOG_WARN("Proposal answer by player %u rejected: %.*s", static_cast<unsigned>(cmd.player),
                 static_cast<int>(describeError(rc).size()), describeError(rc).data());
    }
}

void Application::registerDistrictRoutes() {
    using DSM   = aoc::debug::DebugServer::Method;
    using Query = std::unordered_map<std::string, std::string>;

    this->m_debugServer->routeJson(
        DSM::Post, "/game/city/district/place",
        [this](const Query& q, const std::string&) -> std::string {
            if (this->m_appState != AppState::InGame) {
                throw aoc::debug::ServiceUnavailableError("no active game");
            }
            int32_t player   = 0;
            int32_t cityQ    = 0;
            int32_t cityR    = 0;
            int32_t district = 0;
            int32_t tileQ    = 0;
            int32_t tileR    = 0;
            std::string err;
            if (!readIntParam(q, "player", player, err) || !readIntParam(q, "q", cityQ, err) ||
                !readIntParam(q, "r", cityR, err) || !readIntParam(q, "district", district, err) ||
                !readIntParam(q, "tileQ", tileQ, err) || !readIntParam(q, "tileR", tileR, err)) {
                return err;
            }
            if (district < 0 || district >= static_cast<int32_t>(aoc::sim::DistrictType::Count)) {
                return std::string("{\"error\":\"district out of range\"}");
            }
            std::lock_guard<std::mutex> guard(this->m_pendingCommandsMutex);
            this->m_pendingCommands.push_back(
                aoc::debug::PlaceDistrictCommand{static_cast<aoc::PlayerId>(player),
                                                 {cityQ, cityR},
                                                 static_cast<uint8_t>(district),
                                                 {tileQ, tileR}});
            return std::string("{\"queued\":true}");
        });

    this->m_debugServer->routeJson(
        DSM::Get, "/game/city/district/sites",
        [this](const Query& q, const std::string&) -> std::string {
            if (this->m_appState != AppState::InGame) {
                throw aoc::debug::ServiceUnavailableError("no active game");
            }
            int32_t player   = 0;
            int32_t cityQ    = 0;
            int32_t cityR    = 0;
            int32_t district = 0;
            std::string err;
            if (!readIntParam(q, "player", player, err) || !readIntParam(q, "q", cityQ, err) ||
                !readIntParam(q, "r", cityR, err) || !readIntParam(q, "district", district, err)) {
                return err;
            }
            if (district < 0 || district >= static_cast<int32_t>(aoc::sim::DistrictType::Count)) {
                return std::string("{\"error\":\"district out of range\"}");
            }
            const aoc::game::Player* owner =
                this->m_gameState.player(static_cast<aoc::PlayerId>(player));
            const aoc::game::City* city =
                owner != nullptr ? owner->cityAt({cityQ, cityR}) : nullptr;
            if (city == nullptr) {
                return std::string("{\"error\":\"no such city\"}");
            }
            const aoc::sim::DistrictType type = static_cast<aoc::sim::DistrictType>(district);
            const aoc::hex::AxialCoord best =
                aoc::sim::bestDistrictTile(this->m_gameState, this->m_hexGrid, *city, type);
            std::string json = "{\"best\":{\"q\":" + std::to_string(best.q) +
                               ",\"r\":" + std::to_string(best.r) + "},\"sites\":[";
            aoc::sim::DistrictIndex districts;
            districts.build(*owner);
            bool first = true;
            for (const aoc::hex::AxialCoord& tile : aoc::sim::districtCandidateTiles(
                     this->m_gameState, this->m_hexGrid, *city, type)) {
                if (!first) {
                    json += ",";
                }
                first = false;
                json += "{\"q\":" + std::to_string(tile.q) + ",\"r\":" + std::to_string(tile.r) +
                        ",\"score\":" +
                        std::to_string(static_cast<int32_t>(
                            aoc::sim::districtTileScore(districts, this->m_hexGrid, type, tile))) +
                        "}";
            }
            json += "]}";
            return json;
        });
}

void Application::executeGameControlCommand(const aoc::debug::PlaceDistrictCommand& cmd) {
    const ErrorCode rc = aoc::sim::requestPlaceDistrict(
        this->m_gameState, this->m_hexGrid, cmd.player, cmd.cityLocation,
        static_cast<aoc::sim::DistrictType>(cmd.district), cmd.tile);
    if (rc != ErrorCode::Ok) {
        LOG_WARN("District placement for player %u rejected: %.*s",
                 static_cast<unsigned>(cmd.player), static_cast<int>(describeError(rc).size()),
                 describeError(rc).data());
    }
}

} // namespace aoc::app
