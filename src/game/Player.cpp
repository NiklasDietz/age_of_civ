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
    // Every component tagged with a PlayerId owner must be initialised here.
    // Leaving any of them as default (INVALID_PLAYER) was the root cause of
    // a heap-corruption bug where junk owner values were passed into
    // DiplomacyManager::relation() as array indices.
    this->m_tech.owner             = id;
    this->m_civics.owner           = id;
    this->m_monetary.owner         = id;
    this->m_government.owner       = id;
    this->m_faith.owner            = id;
    this->m_era.owner              = id;
    this->m_eraScore.owner         = id;
    this->m_warWeariness.owner     = id;
    this->m_eureka.owner           = id;
    this->m_greatPeople.owner      = id;
    this->m_economy.owner          = id;
    this->m_tradeAgreements.owner  = id;
    this->m_tariffs.owner          = id;
    this->m_victoryTracker.owner   = id;
    this->m_prestige.owner         = id;
    this->m_spaceRace.owner        = id;
    this->m_tourism.owner          = id;
    this->m_bonds.owner            = id;
    this->m_stockPortfolio.owner   = id;
    this->m_futures.owner          = id;
    this->m_ious.owner             = id;
    this->m_insurance.owner        = id;
    this->m_bubble.owner           = id;
    this->m_migration.owner        = id;
    this->m_blackMarket.owner      = id;
    this->m_energy.owner           = id;
    this->m_humanCapital.owner     = id;
    this->m_supplyChain.owner      = id;
    this->m_industrial.owner       = id;
    this->m_banking.owner          = id;
    this->m_grievances.owner       = id;
    this->m_currencyTrust.owner    = id;
    this->m_currencyExchange.owner = id;
    this->m_currencyDevaluation.owner = id;
    this->m_currencyCrisis.owner   = id;
    this->m_communication.owner    = id;
    this->m_events.owner           = id;
    this->m_researchQueue.owner    = id;
    this->m_tradeAutoRenew.owner   = id;

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

    // Initialize spy component for Diplomat (100) and Spy (101) units.
    // IDs 55/56 were reassigned to 100/101 after colliding with Frigate/Ironclad.
    if (typeId.value == 100 || typeId.value == 101) {
        newUnit.spy().owner = this->m_id;
        newUnit.spy().location = position;
        newUnit.spy().level = (typeId.value == 101)
            ? aoc::sim::SpyLevel::Agent : aoc::sim::SpyLevel::Recruit;
    }

    return newUnit;
}

void Player::removeUnit(Unit* unit) {
    if (unit == nullptr) {
        return;
    }
    std::vector<std::unique_ptr<Unit>>::iterator it = std::find_if(
        this->m_units.begin(), this->m_units.end(),
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
    // Civilization ability: gold multiplier.
    const float goldMult = aoc::sim::civDef(this->m_civId).modifiers.goldMultiplier;
    return static_cast<CurrencyAmount>(static_cast<float>(total) * goldMult);
}

} // namespace aoc::game
