#include "aoc/debug/GameSnapshot.hpp"

#include "aoc/game/City.hpp"
#include "aoc/game/GameState.hpp"
#include "aoc/game/Player.hpp"
#include "aoc/game/Unit.hpp"
#include "aoc/simulation/city/ProductionQueue.hpp"
#include "aoc/simulation/tech/TechTree.hpp"
#include "aoc/simulation/turn/TurnManager.hpp"
#include "aoc/simulation/turn/TurnPhases.hpp"

#include <memory>
#include <sstream>

namespace aoc::debug {

namespace {

// Local JSON string escaper — avoids depending on DebugServer.cpp (which
// is excluded from headless builds). Escapes only the characters JSON requires.
std::string escapeForJson(std::string_view raw) {
    std::string out;
    out.reserve(raw.size());
    for (const char c : raw) {
        if (c == '"') {
            out += "\\\"";
        } else if (c == '\\') {
            out += "\\\\";
        } else {
            out += c;
        }
    }
    return out;
}

std::string_view unitStateName(aoc::sim::UnitState s) {
    switch (s) {
    case aoc::sim::UnitState::Idle:
        return "Idle";
    case aoc::sim::UnitState::Moving:
        return "Moving";
    case aoc::sim::UnitState::Fortified:
        return "Fortified";
    case aoc::sim::UnitState::Sleeping:
        return "Sleeping";
    default:
        return "Unknown";
    }
}

UnitSnapshot snapshotUnit(const aoc::game::Unit& unit) {
    const aoc::sim::UnitTypeDef& def = unit.typeDef();
    return UnitSnapshot{
        .ownerId           = static_cast<int32_t>(unit.owner()),
        .q                 = unit.position().q,
        .r                 = unit.position().r,
        .typeName          = std::string(def.name),
        .hitPoints         = unit.hitPoints(),
        .maxHitPoints      = def.maxHitPoints,
        .movementRemaining = unit.movementRemaining(),
        .maxMovement       = def.movementPoints,
        .combatStrength    = def.combatStrength,
        .rangedStrength    = def.rangedStrength,
        .state             = std::string(unitStateName(unit.state())),
    };
}

CitySnapshot snapshotCity(const aoc::game::City& city) {
    CitySnapshot cs{
        .ownerId         = static_cast<int32_t>(city.owner()),
        .name            = city.name(),
        .q               = city.location().q,
        .r               = city.location().r,
        .population      = city.population(),
        .foodSurplus     = city.foodSurplus(),
        .productionQueue = {},
    };
    for (const aoc::sim::ProductionQueueItem& item : city.production().queue) {
        cs.productionQueue.push_back(ProductionItemSnapshot{
            .name      = item.name,
            .progress  = item.progress,
            .totalCost = item.totalCost,
        });
    }
    return cs;
}

} // namespace

GameSnapshot buildGameSnapshot(const aoc::game::GameState& gameState,
                               const aoc::sim::TurnManager& turnManager) {
    GameSnapshot gs{
        .turnNumber     = static_cast<int32_t>(turnManager.currentTurn()),
        .phase          = std::string(aoc::sim::turnPhaseName(turnManager.currentPhase())),
        .activePlayerId = static_cast<int32_t>(turnManager.activePlayer()),
        .players        = {},
    };

    for (const std::unique_ptr<aoc::game::Player>& playerPtr : gameState.players()) {
        const aoc::game::Player& player           = *playerPtr;
        const aoc::sim::PlayerTechComponent& tech = player.tech();

        PlayerSnapshot ps{
            .id                    = static_cast<int32_t>(player.id()),
            .isHuman               = player.isHuman(),
            .civId                 = static_cast<int32_t>(player.civId()),
            .treasury              = player.treasury(),
            .incomePerTurn         = player.incomePerTurn(),
            .currentResearchTechId = tech.currentResearch.isValid()
                                         ? static_cast<int32_t>(tech.currentResearch.value)
                                         : -1,
            .researchProgress      = tech.researchProgress,
            .units                 = {},
            .cities                = {},
        };

        for (const std::unique_ptr<aoc::game::Unit>& unitPtr : player.units()) {
            ps.units.push_back(snapshotUnit(*unitPtr));
        }
        for (const std::unique_ptr<aoc::game::City>& cityPtr : player.cities()) {
            ps.cities.push_back(snapshotCity(*cityPtr));
        }

        gs.players.push_back(std::move(ps));
    }

    return gs;
}

// ---------------------------------------------------------------------------
// JSON serializers
// ---------------------------------------------------------------------------

std::string toJson(const std::vector<UnitSnapshot>& units) {
    std::ostringstream o;
    o << '[';
    for (std::size_t i = 0; i < units.size(); ++i) {
        if (i > 0) {
            o << ',';
        }
        const UnitSnapshot& u = units[i];
        o << "{\"ownerId\":" << u.ownerId << ",\"q\":" << u.q << ",\"r\":" << u.r
          << ",\"typeName\":\"" << escapeForJson(u.typeName) << '"'
          << ",\"hitPoints\":" << u.hitPoints << ",\"maxHitPoints\":" << u.maxHitPoints
          << ",\"movementRemaining\":" << u.movementRemaining
          << ",\"maxMovement\":" << u.maxMovement << ",\"combatStrength\":" << u.combatStrength
          << ",\"rangedStrength\":" << u.rangedStrength << ",\"state\":\"" << escapeForJson(u.state)
          << '"' << '}';
    }
    o << ']';
    return o.str();
}

std::string toJson(const std::vector<CitySnapshot>& cities) {
    std::ostringstream o;
    o << '[';
    for (std::size_t i = 0; i < cities.size(); ++i) {
        if (i > 0) {
            o << ',';
        }
        const CitySnapshot& c = cities[i];
        o << "{\"ownerId\":" << c.ownerId << ",\"name\":\"" << escapeForJson(c.name) << '"'
          << ",\"q\":" << c.q << ",\"r\":" << c.r << ",\"population\":" << c.population
          << ",\"foodSurplus\":" << c.foodSurplus << ",\"productionQueue\":[";
        for (std::size_t j = 0; j < c.productionQueue.size(); ++j) {
            if (j > 0) {
                o << ',';
            }
            const ProductionItemSnapshot& pi = c.productionQueue[j];
            o << "{\"name\":\"" << escapeForJson(pi.name) << '"' << ",\"progress\":" << pi.progress
              << ",\"totalCost\":" << pi.totalCost << '}';
        }
        o << "]}";
    }
    o << ']';
    return o.str();
}

std::string toJson(const PlayerSnapshot& p) {
    std::ostringstream o;
    o << "{\"id\":" << p.id << ",\"isHuman\":" << (p.isHuman ? "true" : "false")
      << ",\"civId\":" << p.civId << ",\"treasury\":" << p.treasury
      << ",\"incomePerTurn\":" << p.incomePerTurn
      << ",\"currentResearchTechId\":" << p.currentResearchTechId
      << ",\"researchProgress\":" << p.researchProgress << ",\"units\":" << toJson(p.units)
      << ",\"cities\":" << toJson(p.cities) << '}';
    return o.str();
}

std::string toJson(const GameSnapshot& gs) {
    std::ostringstream o;
    o << "{\"turnNumber\":" << gs.turnNumber << ",\"phase\":\"" << escapeForJson(gs.phase) << '"'
      << ",\"activePlayerId\":" << gs.activePlayerId << ",\"players\":[";
    for (std::size_t i = 0; i < gs.players.size(); ++i) {
        if (i > 0) {
            o << ',';
        }
        o << toJson(gs.players[i]);
    }
    o << "]}";
    return o.str();
}

} // namespace aoc::debug
