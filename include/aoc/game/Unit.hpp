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
#include "aoc/simulation/unit/CombatExtensions.hpp"
#include "aoc/simulation/unit/SupplyLines.hpp"
#include "aoc/simulation/economy/TradeRouteSystem.hpp"
#include "aoc/simulation/diplomacy/Espionage.hpp"
#include "aoc/simulation/greatpeople/GreatPeople.hpp"
#include "aoc/simulation/unit/Promotion.hpp"

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

    /**
     * @brief Formation level for Corps/Army bonuses.
     *
     * Single = normal unit, Corps/Army give +10/+17 strength.
     * Managed by formCorps()/formArmy() in CombatExtensions.
     */
    [[nodiscard]] aoc::sim::FormationLevel formationLevel() const { return this->m_formationLevel; }
    void setFormationLevel(aoc::sim::FormationLevel level) { this->m_formationLevel = level; }

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
    bool autoSpreadReligion = false;

    // ========================================================================
    // Religious spread (Missionaries, Apostles, Inquisitors)
    // ========================================================================

    /// Religion index this unit spreads (255 = not a religious unit).
    uint8_t spreadingReligion = 255;

    /// Remaining spread charges (-1 = not applicable).
    int8_t spreadCharges = -1;

    // ========================================================================
    // Animation (for rendering, not gameplay)
    // ========================================================================

    bool isAnimating = false;
    float animProgress = 0.0f;
    aoc::hex::AxialCoord animFrom;
    aoc::hex::AxialCoord animTo;

    // ========================================================================
    // Extended subsystems (formerly ECS-only)
    // ========================================================================

    [[nodiscard]] aoc::sim::UnitSupplyComponent& supply() { return this->m_supply; }
    [[nodiscard]] const aoc::sim::UnitSupplyComponent& supply() const { return this->m_supply; }

    [[nodiscard]] aoc::sim::AirUnitComponent& airUnit() { return this->m_airUnit; }
    [[nodiscard]] const aoc::sim::AirUnitComponent& airUnit() const { return this->m_airUnit; }

    [[nodiscard]] aoc::sim::NuclearWeaponComponent& nuclear() { return this->m_nuclear; }
    [[nodiscard]] const aoc::sim::NuclearWeaponComponent& nuclear() const { return this->m_nuclear; }

    [[nodiscard]] aoc::sim::TraderComponent& trader() { return this->m_trader; }
    [[nodiscard]] const aoc::sim::TraderComponent& trader() const { return this->m_trader; }

    [[nodiscard]] aoc::sim::SpyComponent& spy() { return this->m_spy; }
    [[nodiscard]] const aoc::sim::SpyComponent& spy() const { return this->m_spy; }

    [[nodiscard]] aoc::sim::GreatPersonComponent& greatPerson() { return this->m_greatPerson; }
    [[nodiscard]] const aoc::sim::GreatPersonComponent& greatPerson() const { return this->m_greatPerson; }

    [[nodiscard]] aoc::sim::UnitExperienceComponent& experience() { return this->m_experience; }
    [[nodiscard]] const aoc::sim::UnitExperienceComponent& experience() const { return this->m_experience; }

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
    aoc::sim::FormationLevel m_formationLevel = aoc::sim::FormationLevel::Single;
    int32_t m_chargesRemaining = 0;

    std::vector<aoc::hex::AxialCoord> m_pendingPath;

    // Extended subsystems (formerly ECS-only)
    aoc::sim::UnitSupplyComponent m_supply;
    aoc::sim::AirUnitComponent m_airUnit;
    aoc::sim::NuclearWeaponComponent m_nuclear;
    aoc::sim::TraderComponent m_trader;
    aoc::sim::SpyComponent m_spy;
    aoc::sim::GreatPersonComponent m_greatPerson;
    aoc::sim::UnitExperienceComponent m_experience;
};

} // namespace aoc::game
