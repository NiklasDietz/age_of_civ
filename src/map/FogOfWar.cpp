/**
 * @file FogOfWar.cpp
 * @brief Per-player visibility calculation.
 */

#include "aoc/map/FogOfWar.hpp"
#include "aoc/map/HexGrid.hpp"
#include "aoc/map/HexCoord.hpp"
#include "aoc/game/GameState.hpp"
#include "aoc/game/Player.hpp"
#include "aoc/game/Unit.hpp"
#include "aoc/game/City.hpp"
#include "aoc/simulation/unit/UnitTypes.hpp"

namespace aoc::map {

void FogOfWar::initialize(int32_t tileCount, uint8_t playerCount) {
    this->m_tileCount   = tileCount;
    this->m_playerCount = playerCount;
    this->m_visibility.assign(
        static_cast<std::size_t>(tileCount) * static_cast<std::size_t>(playerCount),
        TileVisibility::Unseen);
}

void FogOfWar::updateVisibility(const aoc::game::GameState& gameState,
                                 const HexGrid& grid,
                                 PlayerId player) {
    if (player >= this->m_playerCount) {
        return;
    }

    std::size_t offset = static_cast<std::size_t>(player) * static_cast<std::size_t>(this->m_tileCount);

    // Demote all Visible tiles to Revealed (they were seen before)
    for (int32_t i = 0; i < this->m_tileCount; ++i) {
        TileVisibility& vis = this->m_visibility[offset + static_cast<std::size_t>(i)];
        if (vis == TileVisibility::Visible) {
            vis = TileVisibility::Revealed;
        }
    }

    // auto required: lambda type is unnameable
    auto revealRadius = [&](hex::AxialCoord center, int32_t radius) {
        for (int32_t q = -radius; q <= radius; ++q) {
            for (int32_t r = std::max(-radius, -q - radius);
                 r <= std::min(radius, -q + radius); ++r) {
                hex::AxialCoord tile{center.q + q, center.r + r};
                if (!grid.isValid(tile)) {
                    continue;
                }
                int32_t idx = grid.toIndex(tile);
                this->m_visibility[offset + static_cast<std::size_t>(idx)] = TileVisibility::Visible;
            }
        }
    };

    const aoc::game::Player* ownerPlayer = gameState.player(player);
    if (ownerPlayer == nullptr) {
        return;
    }

    // Reveal around units owned by this player
    for (const std::unique_ptr<aoc::game::Unit>& unitPtr : ownerPlayer->units()) {
        const aoc::game::Unit& unit = *unitPtr;
        int32_t sightRange = (unit.typeDef().unitClass == aoc::sim::UnitClass::Scout)
            ? SCOUT_SIGHT_RANGE : DEFAULT_SIGHT_RANGE;
        revealRadius(unit.position(), sightRange);
    }

    // Reveal around cities owned by this player
    for (const std::unique_ptr<aoc::game::City>& cityPtr : ownerPlayer->cities()) {
        revealRadius(cityPtr->location(), CITY_SIGHT_RANGE);
    }
}

TileVisibility FogOfWar::visibility(PlayerId player, int32_t tileIndex) const {
    if (player >= this->m_playerCount || tileIndex < 0 || tileIndex >= this->m_tileCount) {
        return TileVisibility::Unseen;
    }
    std::size_t offset = static_cast<std::size_t>(player) * static_cast<std::size_t>(this->m_tileCount);
    return this->m_visibility[offset + static_cast<std::size_t>(tileIndex)];
}

void FogOfWar::setVisibility(PlayerId player, int32_t tileIndex, TileVisibility vis) {
    if (player >= this->m_playerCount || tileIndex < 0 || tileIndex >= this->m_tileCount) {
        return;
    }
    std::size_t offset = static_cast<std::size_t>(player) * static_cast<std::size_t>(this->m_tileCount);
    this->m_visibility[offset + static_cast<std::size_t>(tileIndex)] = vis;
}

} // namespace aoc::map
