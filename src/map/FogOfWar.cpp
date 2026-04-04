/**
 * @file FogOfWar.cpp
 * @brief Per-player visibility calculation.
 */

#include "aoc/map/FogOfWar.hpp"
#include "aoc/map/HexGrid.hpp"
#include "aoc/map/HexCoord.hpp"
#include "aoc/simulation/unit/UnitComponent.hpp"
#include "aoc/simulation/unit/UnitTypes.hpp"
#include "aoc/simulation/city/CityComponent.hpp"
#include "aoc/ecs/World.hpp"

namespace aoc::map {

void FogOfWar::initialize(int32_t tileCount, uint8_t playerCount) {
    this->m_tileCount   = tileCount;
    this->m_playerCount = playerCount;
    this->m_visibility.assign(
        static_cast<std::size_t>(tileCount) * static_cast<std::size_t>(playerCount),
        TileVisibility::Unseen);
}

void FogOfWar::updateVisibility(const aoc::ecs::World& world,
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

    // Helper: mark tiles within radius as Visible
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

    // Reveal around units
    const aoc::ecs::ComponentPool<aoc::sim::UnitComponent>* unitPool =
        world.getPool<aoc::sim::UnitComponent>();
    if (unitPool != nullptr) {
        for (uint32_t i = 0; i < unitPool->size(); ++i) {
            const aoc::sim::UnitComponent& unit = unitPool->data()[i];
            if (unit.owner != player) {
                continue;
            }
            const aoc::sim::UnitTypeDef& def = aoc::sim::unitTypeDef(unit.typeId);
            int32_t sightRange = (def.unitClass == aoc::sim::UnitClass::Scout)
                ? SCOUT_SIGHT_RANGE : DEFAULT_SIGHT_RANGE;
            revealRadius(unit.position, sightRange);
        }
    }

    // Reveal around cities
    const aoc::ecs::ComponentPool<aoc::sim::CityComponent>* cityPool =
        world.getPool<aoc::sim::CityComponent>();
    if (cityPool != nullptr) {
        for (uint32_t i = 0; i < cityPool->size(); ++i) {
            const aoc::sim::CityComponent& city = cityPool->data()[i];
            if (city.owner != player) {
                continue;
            }
            revealRadius(city.location, CITY_SIGHT_RANGE);
        }
    }
}

TileVisibility FogOfWar::visibility(PlayerId player, int32_t tileIndex) const {
    if (player >= this->m_playerCount || tileIndex < 0 || tileIndex >= this->m_tileCount) {
        return TileVisibility::Unseen;
    }
    std::size_t offset = static_cast<std::size_t>(player) * static_cast<std::size_t>(this->m_tileCount);
    return this->m_visibility[offset + static_cast<std::size_t>(tileIndex)];
}

} // namespace aoc::map
