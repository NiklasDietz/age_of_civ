/**
 * @file Unit.cpp
 * @brief Per-unit game state implementation.
 */

#include "aoc/game/Unit.hpp"

#include <algorithm>

namespace aoc::game {

Unit::Unit(PlayerId owner, UnitTypeId typeId, aoc::hex::AxialCoord position)
    : m_owner(owner)
    , m_typeId(typeId)
    , m_position(position)
    , m_hitPoints(this->typeDef().maxHitPoints)
    , m_movementRemaining(this->typeDef().movementPoints)
{
    const aoc::sim::UnitTypeDef& def = this->typeDef();
    if (def.unitClass == aoc::sim::UnitClass::Civilian
        || def.unitClass == aoc::sim::UnitClass::Settler
        || def.unitClass == aoc::sim::UnitClass::Trader) {
        this->m_chargesRemaining = 3;
    }
}

bool Unit::consumeMovement(int32_t cost) {
    if (this->m_movementRemaining < cost) {
        return false;
    }
    this->m_movementRemaining -= cost;
    return true;
}

void Unit::takeDamage(int32_t damage) {
    this->m_hitPoints = std::max(0, this->m_hitPoints - damage);
}

void Unit::heal(int32_t amount) {
    int32_t maxHp = this->typeDef().maxHitPoints;
    this->m_hitPoints = std::min(maxHp, this->m_hitPoints + amount);
}

} // namespace aoc::game
