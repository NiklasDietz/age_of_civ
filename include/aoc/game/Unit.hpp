#pragma once

/**
 * @file Unit.hpp
 * @brief Per-unit game state replacing UnitComponent.
 *
 * Each Unit is owned by a Player and contains all unit-specific state:
 * position, health, movement, path, automation flags, combat state.
 */

#include "aoc/core/Types.hpp"
#include "aoc/map/HexCoord.hpp"
#include "aoc/simulation/unit/UnitTypes.hpp"

#include <cstdint>
#include <vector>

namespace aoc::game {

/**
 * @brief Complete per-unit state.
 *
 * Replaces: UnitComponent, UnitAutomationComponent, UnitFormationComponent,
 * UnitExperienceComponent, UnitSupplyComponent, AirUnitComponent.
 */
class Unit {
public:
    Unit(PlayerId owner, UnitTypeId typeId, aoc::hex::AxialCoord position);
    ~Unit() = default;

    Unit(const Unit&) = delete;
    Unit& operator=(const Unit&) = delete;
    Unit(Unit&&) noexcept = default;
    Unit& operator=(Unit&&) noexcept = default;

    // ========================================================================
    // Identity
    // ========================================================================

    [[nodiscard]] PlayerId owner() const { return this->m_owner; }
    [[nodiscard]] UnitTypeId typeId() const { return this->m_typeId; }
    [[nodiscard]] const aoc::sim::UnitTypeDef& typeDef() const {
        return aoc::sim::unitTypeDef(this->m_typeId);
    }

    [[nodiscard]] bool isMilitary() const { return aoc::sim::isMilitary(this->typeDef().unitClass); }
    [[nodiscard]] bool isNaval() const { return aoc::sim::isNaval(this->typeDef().unitClass); }
    [[nodiscard]] bool isCivilian() const {
        return this->typeDef().unitClass == aoc::sim::UnitClass::Civilian
            || this->typeDef().unitClass == aoc::sim::UnitClass::Settler
            || this->typeDef().unitClass == aoc::sim::UnitClass::Trader;
    }

    // ========================================================================
    // Position & Movement
    // ========================================================================

    [[nodiscard]] aoc::hex::AxialCoord position() const { return this->m_position; }
    void setPosition(aoc::hex::AxialCoord pos) { this->m_position = pos; }

    [[nodiscard]] int32_t movementRemaining() const { return this->m_movementRemaining; }
    void setMovementRemaining(int32_t mp) { this->m_movementRemaining = mp; }
    void refreshMovement() { this->m_movementRemaining = this->typeDef().movementPoints; }
    bool consumeMovement(int32_t cost);  ///< Returns false if insufficient

    [[nodiscard]] const std::vector<aoc::hex::AxialCoord>& pendingPath() const { return this->m_pendingPath; }
    std::vector<aoc::hex::AxialCoord>& pendingPath() { return this->m_pendingPath; }
    void clearPath() { this->m_pendingPath.clear(); }

    // ========================================================================
    // Health & Combat
    // ========================================================================

    [[nodiscard]] int32_t hitPoints() const { return this->m_hitPoints; }
    void setHitPoints(int32_t hp) { this->m_hitPoints = hp; }
    void takeDamage(int32_t damage);
    [[nodiscard]] bool isDead() const { return this->m_hitPoints <= 0; }
    void heal(int32_t amount);

    [[nodiscard]] int32_t combatStrength() const { return this->typeDef().combatStrength; }
    [[nodiscard]] int32_t rangedStrength() const { return this->typeDef().rangedStrength; }

    // ========================================================================
    // State
    // ========================================================================

    [[nodiscard]] aoc::sim::UnitState state() const { return this->m_state; }
    void setState(aoc::sim::UnitState state) { this->m_state = state; }

    [[nodiscard]] bool isFortified() const { return this->m_state == aoc::sim::UnitState::Fortified; }
    [[nodiscard]] bool isSleeping() const { return this->m_state == aoc::sim::UnitState::Sleeping; }

    // ========================================================================
    // Builder charges
    // ========================================================================

    [[nodiscard]] int32_t chargesRemaining() const { return this->m_chargesRemaining; }
    void useCharge() { if (this->m_chargesRemaining > 0) { --this->m_chargesRemaining; } }
    [[nodiscard]] bool hasCharges() const { return this->m_chargesRemaining > 0; }

    // ========================================================================
    // Automation
    // ========================================================================

    bool autoExplore = false;
    bool alertStance = false;
    int32_t alertRadius = 3;
    bool autoImprove = false;
    bool autoRenewRoute = false;

    // ========================================================================
    // Animation (for rendering, not gameplay)
    // ========================================================================

    bool isAnimating = false;
    float animProgress = 0.0f;
    aoc::hex::AxialCoord animFrom;
    aoc::hex::AxialCoord animTo;

    // ========================================================================
    // Upgrade
    // ========================================================================

    /// Get the next unit type this unit can upgrade to.
    [[nodiscard]] UnitTypeId upgradeTarget() const {
        return this->typeDef().upgradesTo;
    }
    [[nodiscard]] bool canUpgrade() const {
        return this->upgradeTarget().isValid();
    }
    [[nodiscard]] int32_t upgradeCost() const {
        return this->typeDef().upgradeCost;
    }

private:
    PlayerId m_owner;
    UnitTypeId m_typeId;
    aoc::hex::AxialCoord m_position;

    int32_t m_hitPoints;
    int32_t m_movementRemaining;
    aoc::sim::UnitState m_state = aoc::sim::UnitState::Idle;
    int32_t m_chargesRemaining = 0;

    std::vector<aoc::hex::AxialCoord> m_pendingPath;
};

} // namespace aoc::game
