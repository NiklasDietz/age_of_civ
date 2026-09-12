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
#include <limits>
#include <functional>
#include <utility>

namespace aoc::game {

namespace {

/// Process-wide hook fired just before a unit is erased. See
/// Player::setUnitRemovalObserver.
std::function<void(Unit*)> g_unitRemovalObserver;

} // namespace

void Player::setUnitRemovalObserver(std::function<void(Unit*)> observer) {
    g_unitRemovalObserver = std::move(observer);
}

Player::Player(PlayerId id) : m_id(id) {
    // Every component tagged with a PlayerId owner must be initialised here.
    // Leaving any of them as default (INVALID_PLAYER) was the root cause of
    // a heap-corruption bug where junk owner values were passed into
    // DiplomacyManager::relation() as array indices.
    this->m_tech.owner                = id;
    this->m_civics.owner              = id;
    this->m_monetary.owner            = id;
    this->m_government.owner          = id;
    this->m_faith.owner               = id;
    this->m_era.owner                 = id;
    this->m_eraScore.owner            = id;
    this->m_warWeariness.owner        = id;
    this->m_eureka.owner              = id;
    this->m_greatPeople.owner         = id;
    this->m_economy.owner             = id;
    this->m_tradeAgreements.owner     = id;
    this->m_tariffs.owner             = id;
    this->m_victoryTracker.owner      = id;
    this->m_prestige.owner            = id;
    this->m_spaceRace.owner           = id;
    this->m_tourism.owner             = id;
    this->m_bonds.owner               = id;
    this->m_stockPortfolio.owner      = id;
    this->m_futures.owner             = id;
    this->m_ious.owner                = id;
    this->m_insurance.owner           = id;
    this->m_bubble.owner              = id;
    this->m_migration.owner           = id;
    this->m_blackMarket.owner         = id;
    this->m_energy.owner              = id;
    this->m_humanCapital.owner        = id;
    this->m_supplyChain.owner         = id;
    this->m_industrial.owner          = id;
    this->m_grievances.owner          = id;
    this->m_currencyTrust.owner       = id;
    this->m_currencyExchange.owner    = id;
    this->m_currencyDevaluation.owner = id;
    this->m_currencyCrisis.owner      = id;
    this->m_communication.owner       = id;
    this->m_events.owner              = id;
    this->m_researchQueue.owner       = id;
    this->m_tradeAutoRenew.owner      = id;

    this->m_tech.initialize();
    this->m_civics.initialize();
}

Player::~Player()                            = default;
Player::Player(Player&&) noexcept            = default;
Player& Player::operator=(Player&&) noexcept = default;

bool Player::canSeeResource(uint16_t goodId) const {
    TechId revealTech = aoc::sim::resourceRevealTech(goodId);
    if (!revealTech.isValid()) {
        return true; // No tech requirement -- always visible
    }
    return this->m_tech.hasResearched(revealTech);
}

void Player::addGold(CurrencyAmount amount, aoc::sim::MoneyFlow flow) {
    this->m_monetary.treasury.m_value += amount;
    const bool ownCiv = flow.kind == aoc::sim::MoneyFlowKind::Domestic &&
                        (flow.counterparty == this->m_id || flow.counterparty == INVALID_PLAYER);
    if (ownCiv) {
        // The other side is our own private money: a tax draws it down, a
        // payment puts it back. A tax the people cannot pay is unbacked.
        if (amount > 0) {
            const CurrencyAmount backed =
                std::min(amount, std::max<CurrencyAmount>(0, this->m_monetary.privateSpecie));
            this->m_monetary.privateSpecie -= backed;
            if (backed < amount && this->m_ledger != nullptr) {
                this->m_ledger->record(this->m_id, aoc::sim::MoneyFlow::unbacked(), amount - backed);
            }
        } else {
            this->m_monetary.privateSpecie -= amount;
        }
        return;
    }
    if (this->m_ledger != nullptr) {
        this->m_ledger->record(this->m_id, flow, amount);
    }
}

bool Player::spendGold(CurrencyAmount amount, aoc::sim::MoneyFlow flow) {
    if (this->m_monetary.treasury < amount) {
        return false;
    }
    this->addGold(-amount, flow);
    return true;
}

void Player::setTreasury(CurrencyAmount amount, aoc::sim::MoneyFlow flow) {
    this->addGold(amount - this->m_monetary.treasury, flow);
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

int32_t Player::activeTradeRouteCount() const {
    int32_t routes = 0;
    for (const std::unique_ptr<Unit>& unit : this->m_units) {
        if (unit->typeDef().unitClass == aoc::sim::UnitClass::Trader &&
            unit->trader().owner != INVALID_PLAYER) {
            ++routes;
        }
    }
    return routes;
}

int32_t Player::ownedCityCount() const {
    int32_t count = 0;
    for (const std::unique_ptr<City>& city : this->m_cities) {
        if (city != nullptr && city->owner() == this->m_id) {
            ++count;
        }
    }
    return count;
}

City& Player::addCity(aoc::hex::AxialCoord location, const std::string& name) {
    this->m_cities.push_back(std::make_unique<City>(this->m_id, location, name));
    return *this->m_cities.back();
}

std::unique_ptr<City> Player::releaseCity(const City* city) {
    for (std::size_t i = 0; i < this->m_cities.size(); ++i) {
        if (this->m_cities[i].get() != city) {
            continue;
        }
        std::unique_ptr<City> taken = std::move(this->m_cities[i]);
        this->m_cities.erase(this->m_cities.begin() + static_cast<std::ptrdiff_t>(i));
        return taken;
    }
    return nullptr;
}

City& Player::adoptCity(std::unique_ptr<City> city) {
    this->m_cities.push_back(std::move(city));
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
    this->m_units.push_back(std::make_unique<Unit>(this->m_id, typeId, position));
    Unit& newUnit = *this->m_units.back();

    // Initialize spy component for Diplomat (100) and Spy (101) units.
    // IDs 55/56 were reassigned to 100/101 after colliding with Frigate/Ironclad.
    if (typeId.value == 100 || typeId.value == 101) {
        newUnit.spy().owner    = this->m_id;
        newUnit.spy().location = position;
        newUnit.spy().level =
            (typeId.value == 101) ? aoc::sim::SpyLevel::Agent : aoc::sim::SpyLevel::Recruit;
    }

    // Religious units (Missionary 19, Apostle 20, Inquisitor 21) carry their
    // owner's religion and their spread charges; AI units spread on their own.
    // Until 2026-09-05 nothing set these on the object model, so every spread
    // and purge path was dead.
    if (typeId.value == 19 || typeId.value == 20 || typeId.value == 21) {
        int8_t charges = 2; // Inquisitor
        if (typeId.value == 19) {
            charges = 3;
        }
        if (typeId.value == 20) {
            charges = 4;
        }
        newUnit.spreadCharges      = charges;
        newUnit.spreadingReligion  = this->m_faith.foundedReligion;
        newUnit.autoSpreadReligion = !this->m_isHuman;
    }

    // Aircraft fly the range of their row; until 2026-09-05 every type used the
    // component default (8) in a fresh game while the loader read the row.
    if (aoc::sim::isAirUnit(newUnit.typeDef().unitClass)) {
        newUnit.airUnit().operationalRange = newUnit.typeDef().range;
    }

    return newUnit;
}

void Player::removeUnit(Unit* unit) {
    if (unit == nullptr) {
        return;
    }
    std::vector<std::unique_ptr<Unit>>::iterator it =
        std::find_if(this->m_units.begin(), this->m_units.end(),
                     [unit](const std::unique_ptr<Unit>& owned) { return owned.get() == unit; });
    if (it != this->m_units.end()) {
        if (g_unitRemovalObserver) {
            g_unitRemovalObserver(unit);
        }
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
        if (city != nullptr && city->owner() == this->m_id) { // a free city's people are not ours
            total += city->population();
        }
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
            int32_t tileIndex         = grid.toIndex(tile);
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
            int32_t tileIndex         = grid.toIndex(tile);
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
            int32_t tileIndex         = grid.toIndex(tile);
            aoc::map::TileYield yield = grid.tileYield(tileIndex);
            total += static_cast<CurrencyAmount>(yield.gold);
        }
    }
    // Civilization ability: gold multiplier.
    const float goldMult = aoc::sim::civDef(this->m_civId).modifiers.goldMultiplier;
    return static_cast<CurrencyAmount>(static_cast<float>(total) * goldMult);
}

const City* Player::nearestCity(const aoc::map::HexGrid& grid, hex::AxialCoord at,
                                int32_t* distOut) const {
    const City* best = nullptr;
    int32_t bestDist = std::numeric_limits<int32_t>::max();
    for (const std::unique_ptr<City>& city : this->m_cities) {
        if (city == nullptr || city->owner() != this->m_id) {
            continue;
        }
        const int32_t d = grid.distance(city->location(), at);
        if (d < bestDist) {
            bestDist = d;
            best     = city.get();
        }
    }
    if (distOut != nullptr) {
        *distOut = bestDist;
    }
    return best;
}

City* Player::nearestCity(const aoc::map::HexGrid& grid, hex::AxialCoord at, int32_t* distOut) {
    return const_cast<City*>(std::as_const(*this).nearestCity(grid, at, distOut));
}

} // namespace aoc::game
