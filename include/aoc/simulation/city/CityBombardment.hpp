#pragma once

/**
 * @file CityBombardment.hpp
 * @brief City ranged bombardment and destructible wall system.
 *
 * Cities with walls can shoot at enemy units within range. Walls have HP
 * that must be reduced by siege/bombard units before the city can be captured.
 *
 * Wall tiers:
 *   Ancient Walls:      100 HP, +25 ranged strength, range 2
 *   Medieval Walls:     200 HP, +40 ranged strength, range 2
 *   Renaissance Walls:  300 HP, +55 ranged strength, range 2
 *   Steel Fortress:     400 HP, +70 ranged strength, range 3
 *
 * Only bombard-class units (Artillery, Siege) deal full damage to walls.
 * Melee units deal 15% of their strength as wall damage.
 * Cities with intact walls cannot be captured by melee attack — the
 * attacker is repelled and takes damage.
 *
 * Walls repair 10 HP per turn when not under siege (no enemy within 3 tiles).
 */

#include "aoc/core/Types.hpp"
#include "aoc/core/Random.hpp"

#include <cstdint>

namespace aoc::game {
class GameState;
class City;
class Unit;
}

namespace aoc::map {
class HexGrid;
}

namespace aoc::sim {

/// Wall tier determines HP, ranged strength, and range.
enum class WallTier : uint8_t {
    None        = 0,
    Ancient     = 1,  ///< BuildingId for Ancient Walls
    Medieval    = 2,  ///< BuildingId for Medieval Walls
    Renaissance = 3,  ///< BuildingId for Renaissance Walls
    Steel       = 4,  ///< BuildingId for Steel Fortress
};

/// Per-city wall state.
struct CityWallState {
    WallTier tier       = WallTier::None;
    int32_t  currentHP  = 0;
    int32_t  maxHP      = 0;
    int32_t  rangedStrength = 0;
    int32_t  range      = 0;

    [[nodiscard]] bool hasWalls() const { return this->tier != WallTier::None; }
    [[nodiscard]] bool isIntact() const { return this->currentHP > 0; }
    [[nodiscard]] float hpFraction() const {
        return (this->maxHP > 0)
            ? static_cast<float>(this->currentHP) / static_cast<float>(this->maxHP)
            : 0.0f;
    }

    /// Set wall stats from tier.
    void setTier(WallTier newTier) {
        this->tier = newTier;
        switch (newTier) {
            case WallTier::Ancient:     this->maxHP = 100; this->rangedStrength = 25; this->range = 2; break;
            case WallTier::Medieval:    this->maxHP = 200; this->rangedStrength = 40; this->range = 2; break;
            case WallTier::Renaissance: this->maxHP = 300; this->rangedStrength = 55; this->range = 2; break;
            case WallTier::Steel:       this->maxHP = 400; this->rangedStrength = 70; this->range = 3; break;
            default:                    this->maxHP = 0;   this->rangedStrength = 0;  this->range = 0; break;
        }
        this->currentHP = this->maxHP;
    }

    /// Repair walls (10 HP/turn when not under siege).
    void repair() {
        if (this->currentHP < this->maxHP) {
            this->currentHP = std::min(this->currentHP + 10, this->maxHP);
        }
    }

    /// Take damage. Returns actual damage dealt.
    int32_t takeDamage(int32_t damage) {
        const int32_t actual = std::min(damage, this->currentHP);
        this->currentHP -= actual;
        return actual;
    }
};

/// Process city bombardment: cities with Walls shoot at enemies within range.
void processCityBombardment(aoc::game::GameState& gameState,
                             const aoc::map::HexGrid& grid,
                             PlayerId player, aoc::Random& rng);

/// Check if a melee unit can capture this city (walls must be destroyed).
[[nodiscard]] bool canCaptureCity(const aoc::game::City& city);

/// Deal siege damage to a city's walls from a bombard unit.
int32_t dealSiegeDamage(aoc::game::City& city, const aoc::game::Unit& attacker);

} // namespace aoc::sim
