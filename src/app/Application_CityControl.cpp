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

#include <mutex>
#include <string>
#include <unordered_map>

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
    using DSM = aoc::debug::DebugServer::Method;
    using Query = std::unordered_map<std::string, std::string>;

    // Every city route names the city by its centre tile: player, q, r.
    const auto readCity = [](const Query& q, int32_t& player, aoc::hex::AxialCoord& at,
                             std::string& err) -> bool {
        int32_t cq = 0;
        int32_t cr = 0;
        if (!readIntParam(q, "player", player, err) || !readIntParam(q, "q", cq, err)
            || !readIntParam(q, "r", cr, err)) {
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
            if (!readCity(q, player, at, err) || !readIntParam(q, "type", type, err)
                || !readIntParam(q, "item", item, err)) {
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
            if (!readCity(q, player, at, err) || !readIntParam(q, "tq", tq, err)
                || !readIntParam(q, "tr", tr, err)) {
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

    // POST /game/city/project?player=&q=&r=&project=   (0 Bread and Circuses .. 5 Military Training)
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
        : aoc::sim::requestPurchase(this->m_gameState, &this->m_hexGrid, cmd.player, cmd.at,
                                    cmd.type, cmd.itemId);
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

} // namespace aoc::app
