/**
 * @file DebugConsole.cpp
 * @brief In-game debug console command execution.
 */

#include "aoc/ui/DebugConsole.hpp"
#include "aoc/simulation/monetary/MonetarySystem.hpp"
#include "aoc/simulation/city/CityComponent.hpp"
#include "aoc/simulation/city/CityLoyalty.hpp"
#include "aoc/simulation/resource/ResourceComponent.hpp"
#include "aoc/simulation/tech/TechTree.hpp"
#include "aoc/simulation/unit/UnitComponent.hpp"
#include "aoc/simulation/unit/UnitTypes.hpp"
#include "aoc/map/HexGrid.hpp"
#include "aoc/map/FogOfWar.hpp"
#include "aoc/ecs/World.hpp"
#include "aoc/core/Log.hpp"

#include <algorithm>
#include <cstdlib>
#include <sstream>

namespace aoc::ui {

void DebugConsole::addChar(char c) {
    if (c >= 32 && c < 127) {
        this->m_input += c;
    }
}

void DebugConsole::backspace() {
    if (!this->m_input.empty()) {
        this->m_input.pop_back();
    }
}

void DebugConsole::log(const std::string& msg) {
    this->m_history.push_back(msg);
    // Keep history at 20 lines max
    if (this->m_history.size() > 20) {
        this->m_history.erase(this->m_history.begin());
    }
    LOG_INFO("[Console] %s", msg.c_str());
}

void DebugConsole::execute(aoc::ecs::World& world, aoc::map::HexGrid& grid,
                            aoc::map::FogOfWar& fog, PlayerId humanPlayer) {
    if (this->m_input.empty()) {
        return;
    }

    std::string cmd = this->m_input;
    this->m_input.clear();
    this->log("> " + cmd);

    // Parse command and arguments
    std::istringstream stream(cmd);
    std::string command;
    stream >> command;

    // ================================================================
    // reveal - Disable fog of war (show entire map)
    // ================================================================
    if (command == "reveal") {
        for (int32_t i = 0; i < grid.tileCount(); ++i) {
            fog.setVisibility(humanPlayer, i, aoc::map::TileVisibility::Visible);
        }
        this->log("Map revealed for player " + std::to_string(humanPlayer));
    }
    // ================================================================
    // fog - Re-enable fog of war
    // ================================================================
    else if (command == "fog") {
        fog.updateVisibility(world, grid, humanPlayer);
        this->log("Fog of war re-enabled");
    }
    // ================================================================
    // gold <amount> - Add gold to treasury
    // ================================================================
    else if (command == "gold") {
        int32_t amount = 0;
        stream >> amount;
        world.forEach<aoc::sim::PlayerEconomyComponent>(
            [humanPlayer, amount](EntityId, aoc::sim::PlayerEconomyComponent& ec) {
                if (ec.owner == humanPlayer) {
                    ec.treasury += static_cast<CurrencyAmount>(amount);
                }
            });
        this->log("Added " + std::to_string(amount) + " gold");
    }
    // ================================================================
    // tech <id> - Research a specific tech
    // ================================================================
    else if (command == "tech") {
        int32_t techId = 0;
        stream >> techId;
        aoc::ecs::ComponentPool<aoc::sim::PlayerTechComponent>* techPool =
            world.getPool<aoc::sim::PlayerTechComponent>();
        if (techPool != nullptr) {
            for (uint32_t i = 0; i < techPool->size(); ++i) {
                if (techPool->data()[i].owner == humanPlayer) {
                    if (static_cast<uint16_t>(techId) < techPool->data()[i].completedTechs.size()) {
                        techPool->data()[i].completedTechs[static_cast<uint16_t>(techId)] = true;
                    }
                    break;
                }
            }
        }
        this->log("Researched tech " + std::to_string(techId));
    }
    // ================================================================
    // techall - Research all techs
    // ================================================================
    else if (command == "techall") {
        aoc::ecs::ComponentPool<aoc::sim::PlayerTechComponent>* techPool =
            world.getPool<aoc::sim::PlayerTechComponent>();
        if (techPool != nullptr) {
            for (uint32_t i = 0; i < techPool->size(); ++i) {
                if (techPool->data()[i].owner == humanPlayer) {
                    for (uint16_t t = 0; t < aoc::sim::techCount(); ++t) {
                        techPool->data()[i].completedTechs[t] = true;
                    }
                    break;
                }
            }
        }
        this->log("All techs researched");
    }
    // ================================================================
    // advance <turns> - Skip N turns
    // ================================================================
    else if (command == "advance") {
        int32_t turns = 1;
        stream >> turns;
        if (this->onAdvanceTurns) {
            this->onAdvanceTurns(turns);
        }
        this->log("Advanced " + std::to_string(turns) + " turns");
    }
    // ================================================================
    // spawn <unitId> - Spawn unit at camera center
    // ================================================================
    else if (command == "spawn") {
        int32_t unitId = 0;
        stream >> unitId;
        // Spawn at center of map as fallback
        aoc::hex::AxialCoord center = aoc::hex::offsetToAxial(
            {grid.width() / 2, grid.height() / 2});
        EntityId unitEntity = world.createEntity();
        world.addComponent<aoc::sim::UnitComponent>(
            unitEntity,
            aoc::sim::UnitComponent::create(
                humanPlayer,
                aoc::UnitTypeId{static_cast<uint16_t>(unitId)},
                center));
        this->log("Spawned unit " + std::to_string(unitId) + " at map center");
    }
    // ================================================================
    // pop <amount> - Set nearest city population
    // ================================================================
    else if (command == "pop") {
        int32_t amount = 1;
        stream >> amount;
        aoc::ecs::ComponentPool<aoc::sim::CityComponent>* cityPool =
            world.getPool<aoc::sim::CityComponent>();
        if (cityPool != nullptr) {
            for (uint32_t i = 0; i < cityPool->size(); ++i) {
                if (cityPool->data()[i].owner == humanPlayer) {
                    cityPool->data()[i].population = amount;
                    this->log("Set " + cityPool->data()[i].name + " pop to " + std::to_string(amount));
                    break;
                }
            }
        }
    }
    // ================================================================
    // loyalty <value> - Set nearest city loyalty
    // ================================================================
    else if (command == "loyalty") {
        float value = 100.0f;
        stream >> value;
        aoc::ecs::ComponentPool<aoc::sim::CityComponent>* cityPool =
            world.getPool<aoc::sim::CityComponent>();
        if (cityPool != nullptr) {
            for (uint32_t i = 0; i < cityPool->size(); ++i) {
                if (cityPool->data()[i].owner == humanPlayer) {
                    aoc::sim::CityLoyaltyComponent* loyalty =
                        world.tryGetComponent<aoc::sim::CityLoyaltyComponent>(cityPool->entities()[i]);
                    if (loyalty != nullptr) {
                        loyalty->loyalty = value;
                        this->log("Set loyalty to " + std::to_string(static_cast<int>(value)));
                    }
                    break;
                }
            }
        }
    }
    // ================================================================
    // money <system> - Set monetary system
    // ================================================================
    else if (command == "money") {
        std::string system;
        stream >> system;
        aoc::ecs::ComponentPool<aoc::sim::MonetaryStateComponent>* monetaryPool =
            world.getPool<aoc::sim::MonetaryStateComponent>();
        if (monetaryPool != nullptr) {
            for (uint32_t i = 0; i < monetaryPool->size(); ++i) {
                if (monetaryPool->data()[i].owner == humanPlayer) {
                    if (system == "barter")    { monetaryPool->data()[i].system = aoc::sim::MonetarySystemType::Barter; }
                    if (system == "commodity") { monetaryPool->data()[i].system = aoc::sim::MonetarySystemType::CommodityMoney; }
                    if (system == "gold")      { monetaryPool->data()[i].system = aoc::sim::MonetarySystemType::GoldStandard; }
                    if (system == "fiat")      { monetaryPool->data()[i].system = aoc::sim::MonetarySystemType::FiatMoney; }
                    this->log("Set monetary system to " + system);
                    break;
                }
            }
        }
    }
    // ================================================================
    // help - Show all commands
    // ================================================================
    else if (command == "help") {
        this->log("Commands:");
        this->log("  reveal          - Show entire map");
        this->log("  fog             - Re-enable fog of war");
        this->log("  gold <N>        - Add N gold");
        this->log("  tech <id>       - Research tech by ID");
        this->log("  techall         - Research all techs");
        this->log("  advance <N>     - Skip N turns");
        this->log("  spawn <unitId>  - Spawn unit at center");
        this->log("  pop <N>         - Set city population");
        this->log("  loyalty <N>     - Set city loyalty");
        this->log("  money <system>  - barter/commodity/gold/fiat");
    }
    else {
        this->log("Unknown command: " + command + " (type 'help')");
    }
}

} // namespace aoc::ui
