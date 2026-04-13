/**
 * @file Player.cpp
 * @brief Per-player game state implementation.
 */

#include "aoc/game/Player.hpp"
#include "aoc/game/City.hpp"
#include "aoc/game/Unit.hpp"
#include "aoc/map/HexGrid.hpp"
#include "aoc/map/Terrain.hpp"
#include "aoc/simulation/resource/ResourceTypes.hpp"

#include <algorithm>

namespace aoc::game {

Player::Player(PlayerId id)
    : m_id(id)
{
    this->m_tech.owner = id;
    this->m_civics.owner = id;
    this->m_monetary.owner = id;
    this->m_faith.owner = id;
    this->m_bubble.owner = id;

    this->m_tech.initialize();
    this->m_civics.initialize();
}

Player::~Player() = default;
Player::Player(Player&&) noexcept = default;
Player& Player::operator=(Player&&) noexcept = default;

bool Player::canSeeResource(uint16_t goodId) const {
    TechId revealTech = aoc::sim::resourceRevealTech(goodId);
    if (!revealTech.isValid()) {
        return true;  // No tech requirement -- always visible
    }
    return this->m_tech.hasResearched(revealTech);
}

bool Player::spendGold(CurrencyAmount amount) {
    if (this->m_treasury < amount) {
        return false;
    }
    this->m_treasury -= amount;
    return true;
}

City* Player::cityAt(aoc::hex::AxialCoord location) {
    for (const std::unique_ptr<City>& city : this->m_cities) {
        if (city->location() == location) {
            return city.get();
        }
    }
    return nullptr;
}

const City* Player::cityAt(aoc::hex::AxialCoord location) const {
    for (const std::unique_ptr<City>& city : this->m_cities) {
        if (city->location() == location) {
            return city.get();
        }
    }
    return nullptr;
}

City& Player::addCity(aoc::hex::AxialCoord location, const std::string& name) {
    this->m_cities.push_back(
        std::make_unique<City>(this->m_id, location, name));
    return *this->m_cities.back();
}

Unit* Player::unitAt(aoc::hex::AxialCoord location) {
    for (const std::unique_ptr<Unit>& unit : this->m_units) {
        if (unit->position() == location) {
            return unit.get();
        }
    }
    return nullptr;
}

const Unit* Player::unitAt(aoc::hex::AxialCoord location) const {
    for (const std::unique_ptr<Unit>& unit : this->m_units) {
        if (unit->position() == location) {
            return unit.get();
        }
    }
    return nullptr;
}

Unit& Player::addUnit(UnitTypeId typeId, aoc::hex::AxialCoord position) {
    this->m_units.push_back(
        std::make_unique<Unit>(this->m_id, typeId, position));
    Unit& newUnit = *this->m_units.back();

    // Initialize spy component for Diplomat (55) and Spy (56) units
    if (typeId.value == 55 || typeId.value == 56) {
        newUnit.spy().owner = this->m_id;
        newUnit.spy().location = position;
        newUnit.spy().level = (typeId.value == 56)
            ? aoc::sim::SpyLevel::Agent : aoc::sim::SpyLevel::Recruit;
    }

    return newUnit;
}

void Player::removeUnit(Unit* unit) {
    if (unit == nullptr) {
        return;
    }
    auto it = std::find_if(this->m_units.begin(), this->m_units.end(),
        [unit](const std::unique_ptr<Unit>& owned) {
            return owned.get() == unit;
        });
    if (it != this->m_units.end()) {
        this->m_units.erase(it);
    }
}

int32_t Player::militaryUnitCount() const {
    int32_t count = 0;
    for (const std::unique_ptr<Unit>& unit : this->m_units) {
        if (unit->isMilitary()) {
            ++count;
        }
    }
    return count;
}

int32_t Player::totalPopulation() const {
    int32_t total = 0;
    for (const std::unique_ptr<City>& city : this->m_cities) {
        total += city->population();
    }
    return total;
}

float Player::sciencePerTurn(const aoc::map::HexGrid& grid) const {
    float total = 0.0f;
    for (const std::unique_ptr<City>& city : this->m_cities) {
        for (const aoc::hex::AxialCoord& tile : city->workedTiles()) {
            if (!grid.isValid(tile)) {
                continue;
            }
            int32_t tileIndex = grid.toIndex(tile);
            aoc::map::TileYield yield = grid.tileYield(tileIndex);
            total += static_cast<float>(yield.science);
        }
    }
    return total;
}

float Player::culturePerTurn(const aoc::map::HexGrid& grid) const {
    float total = 0.0f;
    for (const std::unique_ptr<City>& city : this->m_cities) {
        for (const aoc::hex::AxialCoord& tile : city->workedTiles()) {
            if (!grid.isValid(tile)) {
                continue;
            }
            int32_t tileIndex = grid.toIndex(tile);
            aoc::map::TileYield yield = grid.tileYield(tileIndex);
            total += static_cast<float>(yield.culture);
        }
    }
    return total;
}

CurrencyAmount Player::goldIncome(const aoc::map::HexGrid& grid) const {
    CurrencyAmount total = 0;
    for (const std::unique_ptr<City>& city : this->m_cities) {
        for (const aoc::hex::AxialCoord& tile : city->workedTiles()) {
            if (!grid.isValid(tile)) {
                continue;
            }
            int32_t tileIndex = grid.toIndex(tile);
            aoc::map::TileYield yield = grid.tileYield(tileIndex);
            total += static_cast<CurrencyAmount>(yield.gold);
        }
    }
    return total;
}

} // namespace aoc::game
