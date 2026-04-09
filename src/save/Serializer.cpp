/**
 * @file Serializer.cpp
 * @brief Binary save/load implementation.
 */

#include "aoc/save/Serializer.hpp"
#include "aoc/core/Log.hpp"
#include "aoc/ecs/World.hpp"
#include "aoc/map/HexGrid.hpp"
#include "aoc/map/FogOfWar.hpp"
#include "aoc/map/Terrain.hpp"
#include "aoc/simulation/turn/TurnManager.hpp"
#include "aoc/simulation/resource/EconomySimulation.hpp"
#include "aoc/simulation/diplomacy/DiplomacyState.hpp"
#include "aoc/simulation/diplomacy/Espionage.hpp"
#include "aoc/simulation/diplomacy/WarState.hpp"
#include "aoc/simulation/unit/UnitComponent.hpp"
#include "aoc/simulation/unit/Promotion.hpp"
#include "aoc/simulation/city/CityComponent.hpp"
#include "aoc/simulation/city/ProductionQueue.hpp"
#include "aoc/simulation/city/District.hpp"
#include "aoc/simulation/monetary/MonetarySystem.hpp"
#include "aoc/simulation/resource/ResourceComponent.hpp"
#include "aoc/simulation/tech/TechTree.hpp"
#include "aoc/simulation/tech/CivicTree.hpp"
#include "aoc/simulation/tech/EurekaBoost.hpp"
#include "aoc/simulation/tech/EraProgression.hpp"
#include "aoc/simulation/government/GovernmentComponent.hpp"
#include "aoc/simulation/victory/VictoryCondition.hpp"
#include "aoc/simulation/civilization/Civilization.hpp"
#include "aoc/simulation/greatpeople/GreatPeople.hpp"
#include "aoc/simulation/barbarian/BarbarianController.hpp"
#include "aoc/simulation/wonder/Wonder.hpp"
#include "aoc/simulation/monetary/CurrencyTrust.hpp"
#include "aoc/simulation/monetary/CurrencyCrisis.hpp"
#include "aoc/simulation/monetary/CurrencyWar.hpp"
#include "aoc/simulation/monetary/Bonds.hpp"
#include "aoc/simulation/economy/Speculation.hpp"
#include "aoc/simulation/production/ProductionEfficiency.hpp"
#include "aoc/simulation/production/BuildingCapacity.hpp"
#include "aoc/simulation/production/Waste.hpp"
#include "aoc/simulation/production/Automation.hpp"
#include "aoc/simulation/economy/IndustrialRevolution.hpp"

#include <cassert>
#include <cstring>
#include <fstream>

namespace aoc::save {

// ============================================================================
// WriteBuffer
// ============================================================================

void WriteBuffer::writeU8(uint8_t v) { this->m_data.push_back(v); }

void WriteBuffer::writeU16(uint16_t v) {
    this->m_data.push_back(static_cast<uint8_t>(v & 0xFF));
    this->m_data.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
}

void WriteBuffer::writeU32(uint32_t v) {
    for (int i = 0; i < 4; ++i) {
        this->m_data.push_back(static_cast<uint8_t>((v >> (i * 8)) & 0xFF));
    }
}

void WriteBuffer::writeU64(uint64_t v) {
    for (int i = 0; i < 8; ++i) {
        this->m_data.push_back(static_cast<uint8_t>((v >> (i * 8)) & 0xFF));
    }
}

void WriteBuffer::writeI32(int32_t v) {
    uint32_t u;
    std::memcpy(&u, &v, sizeof(u));
    this->writeU32(u);
}

void WriteBuffer::writeI64(int64_t v) {
    uint64_t u;
    std::memcpy(&u, &v, sizeof(u));
    this->writeU64(u);
}

void WriteBuffer::writeF32(float v) {
    uint32_t u;
    std::memcpy(&u, &v, sizeof(u));
    this->writeU32(u);
}

void WriteBuffer::writeString(std::string_view str) {
    this->writeU16(static_cast<uint16_t>(str.size()));
    this->writeBytes(str.data(), str.size());
}

void WriteBuffer::writeBytes(const void* data, std::size_t size) {
    const uint8_t* ptr = static_cast<const uint8_t*>(data);
    this->m_data.insert(this->m_data.end(), ptr, ptr + size);
}

// ============================================================================
// ReadBuffer
// ============================================================================

ReadBuffer::ReadBuffer(const std::vector<uint8_t>& data) : m_data(data) {}

uint8_t ReadBuffer::readU8() {
    uint8_t v = this->m_data[this->m_offset];
    ++this->m_offset;
    return v;
}

uint16_t ReadBuffer::readU16() {
    uint16_t v = 0;
    v |= static_cast<uint16_t>(this->m_data[this->m_offset]);
    v |= static_cast<uint16_t>(static_cast<uint16_t>(this->m_data[this->m_offset + 1]) << 8);
    this->m_offset += 2;
    return v;
}

uint32_t ReadBuffer::readU32() {
    uint32_t v = 0;
    for (int i = 0; i < 4; ++i) {
        v |= static_cast<uint32_t>(this->m_data[this->m_offset + static_cast<std::size_t>(i)]) << (i * 8);
    }
    this->m_offset += 4;
    return v;
}

uint64_t ReadBuffer::readU64() {
    uint64_t v = 0;
    for (int i = 0; i < 8; ++i) {
        v |= static_cast<uint64_t>(this->m_data[this->m_offset + static_cast<std::size_t>(i)]) << (i * 8);
    }
    this->m_offset += 8;
    return v;
}

int32_t ReadBuffer::readI32() {
    uint32_t u = this->readU32();
    int32_t v;
    std::memcpy(&v, &u, sizeof(v));
    return v;
}

int64_t ReadBuffer::readI64() {
    uint64_t u = this->readU64();
    int64_t v;
    std::memcpy(&v, &u, sizeof(v));
    return v;
}

float ReadBuffer::readF32() {
    uint32_t u = this->readU32();
    float v;
    std::memcpy(&v, &u, sizeof(v));
    return v;
}

std::string ReadBuffer::readString() {
    uint16_t len = this->readU16();
    std::string str(this->m_data.begin() + static_cast<std::ptrdiff_t>(this->m_offset),
                    this->m_data.begin() + static_cast<std::ptrdiff_t>(this->m_offset + len));
    this->m_offset += len;
    return str;
}

void ReadBuffer::readBytes(void* dst, std::size_t size) {
    std::memcpy(dst, this->m_data.data() + this->m_offset, size);
    this->m_offset += size;
}

void ReadBuffer::skip(std::size_t bytes) { this->m_offset += bytes; }

bool ReadBuffer::hasRemaining(std::size_t bytes) const {
    return this->m_offset + bytes <= this->m_data.size();
}

std::size_t ReadBuffer::remaining() const {
    return this->m_data.size() - this->m_offset;
}

// ============================================================================
// Section writers
// ============================================================================

namespace {

void writeSection(WriteBuffer& buf, SectionId id, const WriteBuffer& sectionData) {
    buf.writeU16(static_cast<uint16_t>(id));
    buf.writeU32(static_cast<uint32_t>(sectionData.size()));
    buf.writeBytes(sectionData.data().data(), sectionData.size());
}

void writeMapSection(WriteBuffer& out, const aoc::map::HexGrid& grid) {
    WriteBuffer section;
    section.writeI32(grid.width());
    section.writeI32(grid.height());

    int32_t count = grid.tileCount();
    for (int32_t i = 0; i < count; ++i) {
        section.writeU8(static_cast<uint8_t>(grid.terrain(i)));
        section.writeU8(static_cast<uint8_t>(grid.feature(i)));
        section.writeU8(static_cast<uint8_t>(grid.elevation(i)));
        section.writeU8(grid.riverEdges(i));
        section.writeU16(grid.resource(i).value);
        section.writeU8(grid.owner(i));
    }

    writeSection(out, SectionId::MapGrid, section);
}

void writeTurnSection(WriteBuffer& out, const aoc::sim::TurnManager& tm) {
    WriteBuffer section;
    section.writeU32(tm.currentTurn());
    section.writeU8(static_cast<uint8_t>(tm.currentPhase()));
    writeSection(out, SectionId::TurnState, section);
}

void writeRandomSection(WriteBuffer& out, const aoc::Random& rng) {
    WriteBuffer section;
    std::array<uint64_t, 4> state = rng.state();
    for (uint64_t s : state) {
        section.writeU64(s);
    }
    writeSection(out, SectionId::RandomState, section);
}

void writeEntitySection(WriteBuffer& out, const aoc::ecs::World& world) {
    WriteBuffer section;

    // Write units
    const aoc::ecs::ComponentPool<aoc::sim::UnitComponent>* unitPool =
        world.getPool<aoc::sim::UnitComponent>();
    uint32_t unitCount = unitPool ? unitPool->size() : 0;
    section.writeU32(unitCount);
    if (unitPool != nullptr) {
        for (uint32_t i = 0; i < unitPool->size(); ++i) {
            const aoc::sim::UnitComponent& unit = unitPool->data()[i];
            section.writeU8(unit.owner);
            section.writeU16(unit.typeId.value);
            section.writeI32(unit.position.q);
            section.writeI32(unit.position.r);
            section.writeI32(unit.hitPoints);
            section.writeI32(unit.movementRemaining);
            section.writeU8(static_cast<uint8_t>(unit.state));
            // v4: chargesRemaining, cargoCapacity, pendingPath
            section.writeU8(static_cast<uint8_t>(unit.chargesRemaining));
            section.writeU8(static_cast<uint8_t>(unit.cargoCapacity));
            section.writeU16(static_cast<uint16_t>(unit.pendingPath.size()));
            for (const hex::AxialCoord& coord : unit.pendingPath) {
                section.writeI32(coord.q);
                section.writeI32(coord.r);
            }
        }
    }

    // Write cities
    const aoc::ecs::ComponentPool<aoc::sim::CityComponent>* cityPool =
        world.getPool<aoc::sim::CityComponent>();
    uint32_t cityCount = cityPool ? cityPool->size() : 0;
    section.writeU32(cityCount);
    if (cityPool != nullptr) {
        for (uint32_t i = 0; i < cityPool->size(); ++i) {
            const aoc::sim::CityComponent& city = cityPool->data()[i];
            section.writeU8(city.owner);
            section.writeI32(city.location.q);
            section.writeI32(city.location.r);
            section.writeString(city.name);
            section.writeI32(city.population);
            section.writeF32(city.foodSurplus);
            section.writeF32(city.productionProgress);

            section.writeU32(static_cast<uint32_t>(city.workedTiles.size()));
            for (const hex::AxialCoord& tile : city.workedTiles) {
                section.writeI32(tile.q);
                section.writeI32(tile.r);
            }

            // v4: cultureBorderProgress, tilesClaimedCount, isOriginalCapital, originalOwner
            section.writeF32(city.cultureBorderProgress);
            section.writeI32(city.tilesClaimedCount);
            section.writeU8(city.isOriginalCapital ? uint8_t{1} : uint8_t{0});
            section.writeU8(city.originalOwner);
        }
    }

    writeSection(out, SectionId::Entities, section);
}

void writeImprovementsSection(WriteBuffer& out, const aoc::map::HexGrid& grid) {
    WriteBuffer section;
    int32_t count = grid.tileCount();
    section.writeI32(count);
    for (int32_t i = 0; i < count; ++i) {
        section.writeU8(static_cast<uint8_t>(grid.improvement(i)));
        section.writeU8(grid.hasRoad(i) ? uint8_t{1} : uint8_t{0});
    }
    writeSection(out, SectionId::Improvements, section);
}

void writeTechProgressSection(WriteBuffer& out, const aoc::ecs::World& world) {
    WriteBuffer section;

    // Tech components
    const aoc::ecs::ComponentPool<aoc::sim::PlayerTechComponent>* techPool =
        world.getPool<aoc::sim::PlayerTechComponent>();
    uint32_t techCompCount = techPool ? techPool->size() : 0;
    section.writeU32(techCompCount);
    if (techPool != nullptr) {
        for (uint32_t i = 0; i < techPool->size(); ++i) {
            const aoc::sim::PlayerTechComponent& tech = techPool->data()[i];
            section.writeU8(tech.owner);
            section.writeU16(tech.currentResearch.value);
            section.writeF32(tech.researchProgress);

            uint16_t totalTechs = aoc::sim::techCount();
            section.writeU16(totalTechs);
            uint16_t byteCount = static_cast<uint16_t>((totalTechs + 7) / 8);
            for (uint16_t b = 0; b < byteCount; ++b) {
                uint8_t byte = 0;
                for (uint8_t bit = 0; bit < 8; ++bit) {
                    uint16_t techIdx = static_cast<uint16_t>(b * 8 + bit);
                    if (techIdx < totalTechs && tech.completedTechs[techIdx]) {
                        byte |= static_cast<uint8_t>(1u << bit);
                    }
                }
                section.writeU8(byte);
            }
        }
    }

    // Civic components
    const aoc::ecs::ComponentPool<aoc::sim::PlayerCivicComponent>* civicPool =
        world.getPool<aoc::sim::PlayerCivicComponent>();
    uint32_t civicCompCount = civicPool ? civicPool->size() : 0;
    section.writeU32(civicCompCount);
    if (civicPool != nullptr) {
        for (uint32_t i = 0; i < civicPool->size(); ++i) {
            const aoc::sim::PlayerCivicComponent& civic = civicPool->data()[i];
            section.writeU8(civic.owner);
            section.writeU16(civic.currentResearch.value);
            section.writeF32(civic.researchProgress);

            uint16_t totalCivics = aoc::sim::civicCount();
            section.writeU16(totalCivics);
            uint16_t byteCount = static_cast<uint16_t>((totalCivics + 7) / 8);
            for (uint16_t b = 0; b < byteCount; ++b) {
                uint8_t byte = 0;
                for (uint8_t bit = 0; bit < 8; ++bit) {
                    uint16_t civicIdx = static_cast<uint16_t>(b * 8 + bit);
                    if (civicIdx < totalCivics && civic.completedCivics[civicIdx]) {
                        byte |= static_cast<uint8_t>(1u << bit);
                    }
                }
                section.writeU8(byte);
            }
        }
    }

    writeSection(out, SectionId::TechProgress, section);
}

void writeProductionQueuesSection(WriteBuffer& out, const aoc::ecs::World& world) {
    WriteBuffer section;

    const aoc::ecs::ComponentPool<aoc::sim::CityComponent>* cityPool =
        world.getPool<aoc::sim::CityComponent>();
    if (cityPool == nullptr) {
        section.writeU32(0);
        writeSection(out, SectionId::ProductionQueues, section);
        return;
    }

    uint32_t count = 0;
    for (uint32_t i = 0; i < cityPool->size(); ++i) {
        EntityId cityEntity = cityPool->entities()[i];
        const aoc::sim::ProductionQueueComponent* queue =
            world.tryGetComponent<aoc::sim::ProductionQueueComponent>(cityEntity);
        if (queue != nullptr && !queue->isEmpty()) {
            ++count;
        }
    }

    section.writeU32(count);
    for (uint32_t i = 0; i < cityPool->size(); ++i) {
        EntityId cityEntity = cityPool->entities()[i];
        const aoc::sim::ProductionQueueComponent* queue =
            world.tryGetComponent<aoc::sim::ProductionQueueComponent>(cityEntity);
        if (queue != nullptr && !queue->isEmpty()) {
            section.writeU32(i);  // city index in pool order
            section.writeU32(static_cast<uint32_t>(queue->queue.size()));
            for (const aoc::sim::ProductionQueueItem& item : queue->queue) {
                section.writeU8(static_cast<uint8_t>(item.type));
                section.writeU16(item.itemId);
                section.writeString(item.name);
                section.writeF32(item.totalCost);
                section.writeF32(item.progress);
            }
        }
    }

    writeSection(out, SectionId::ProductionQueues, section);
}

void writeDistrictsSection(WriteBuffer& out, const aoc::ecs::World& world) {
    WriteBuffer section;

    const aoc::ecs::ComponentPool<aoc::sim::CityComponent>* cityPool =
        world.getPool<aoc::sim::CityComponent>();
    if (cityPool == nullptr) {
        section.writeU32(0);
        writeSection(out, SectionId::Districts, section);
        return;
    }

    uint32_t count = 0;
    for (uint32_t i = 0; i < cityPool->size(); ++i) {
        EntityId cityEntity = cityPool->entities()[i];
        if (world.tryGetComponent<aoc::sim::CityDistrictsComponent>(cityEntity) != nullptr) {
            ++count;
        }
    }

    section.writeU32(count);
    for (uint32_t i = 0; i < cityPool->size(); ++i) {
        EntityId cityEntity = cityPool->entities()[i];
        const aoc::sim::CityDistrictsComponent* districts =
            world.tryGetComponent<aoc::sim::CityDistrictsComponent>(cityEntity);
        if (districts != nullptr) {
            section.writeU32(i);  // city index in pool order
            section.writeU32(static_cast<uint32_t>(districts->districts.size()));
            for (const aoc::sim::CityDistrictsComponent::PlacedDistrict& dist : districts->districts) {
                section.writeU8(static_cast<uint8_t>(dist.type));
                section.writeI32(dist.location.q);
                section.writeI32(dist.location.r);
                section.writeU32(static_cast<uint32_t>(dist.buildings.size()));
                for (BuildingId bid : dist.buildings) {
                    section.writeU16(bid.value);
                }
            }
        }
    }

    writeSection(out, SectionId::Districts, section);
}

void writeMonetarySection(WriteBuffer& out, const aoc::ecs::World& world) {
    WriteBuffer section;

    const aoc::ecs::ComponentPool<aoc::sim::MonetaryStateComponent>* pool =
        world.getPool<aoc::sim::MonetaryStateComponent>();
    uint32_t count = pool ? pool->size() : 0;
    section.writeU32(count);
    if (pool != nullptr) {
        for (uint32_t i = 0; i < pool->size(); ++i) {
            const aoc::sim::MonetaryStateComponent& m = pool->data()[i];
            section.writeU8(m.owner);
            section.writeU8(static_cast<uint8_t>(m.system));
            section.writeI64(m.moneySupply);
            section.writeI64(m.treasury);
            section.writeI32(m.copperCoinReserves);
            section.writeI32(m.silverCoinReserves);
            section.writeI32(m.goldCoinReserves);
            section.writeU8(static_cast<uint8_t>(m.effectiveCoinTier));
            section.writeF32(m.goldBackingRatio);
            section.writeF32(m.inflationRate);
            section.writeF32(m.priceLevel);
            section.writeF32(m.interestRate);
            section.writeF32(m.reserveRequirement);
            section.writeF32(m.taxRate);
            section.writeI64(m.governmentSpending);
            section.writeI64(m.governmentDebt);
            section.writeI64(m.taxRevenue);
            section.writeI64(m.deficit);
            section.writeI64(m.gdp);
            section.writeF32(m.velocityOfMoney);
            section.writeF32(m.debasement.debasementRatio);
            section.writeI32(m.debasement.turnsDebased);
            section.writeU8(m.debasement.discoveredByPartners ? 1 : 0);
            section.writeI32(m.turnsInCurrentSystem);
        }
    }

    writeSection(out, SectionId::MonetaryState, section);
}

void writeGovernmentSection(WriteBuffer& out, const aoc::ecs::World& world) {
    WriteBuffer section;

    const aoc::ecs::ComponentPool<aoc::sim::PlayerGovernmentComponent>* pool =
        world.getPool<aoc::sim::PlayerGovernmentComponent>();
    uint32_t count = pool ? pool->size() : 0;
    section.writeU32(count);
    if (pool != nullptr) {
        for (uint32_t i = 0; i < pool->size(); ++i) {
            const aoc::sim::PlayerGovernmentComponent& gov = pool->data()[i];
            section.writeU8(gov.owner);
            section.writeU8(static_cast<uint8_t>(gov.government));
            for (uint8_t s = 0; s < aoc::sim::MAX_POLICY_SLOTS; ++s) {
                section.writeU8(static_cast<uint8_t>(gov.activePolicies[s]));
            }
            section.writeU16(gov.unlockedGovernments);
            section.writeU32(gov.unlockedPolicies);
            section.writeI32(gov.anarchyTurnsRemaining);
            section.writeU8(static_cast<uint8_t>(gov.activeAction));
            section.writeI32(gov.actionTurnsRemaining);
        }
    }

    writeSection(out, SectionId::GovernmentState, section);
}

void writeVictorySection(WriteBuffer& out, const aoc::ecs::World& world) {
    WriteBuffer section;

    const aoc::ecs::ComponentPool<aoc::sim::VictoryTrackerComponent>* pool =
        world.getPool<aoc::sim::VictoryTrackerComponent>();
    uint32_t count = pool ? pool->size() : 0;
    section.writeU32(count);
    if (pool != nullptr) {
        for (uint32_t i = 0; i < pool->size(); ++i) {
            const aoc::sim::VictoryTrackerComponent& v = pool->data()[i];
            section.writeU8(v.owner);
            section.writeI32(v.scienceProgress);
            section.writeF32(v.totalCultureAccumulated);
            section.writeI32(v.score);
            // CSI data
            for (int32_t c = 0; c < aoc::sim::CSI_CATEGORY_COUNT; ++c) {
                section.writeF32(v.categoryScores[c]);
            }
            section.writeF32(v.tradeNetworkMultiplier);
            section.writeF32(v.financialIntegrationMult);
            section.writeF32(v.diplomaticWebMult);
            section.writeF32(v.compositeCSI);
            section.writeI32(v.eraVictoryPoints);
            section.writeI32(v.erasEvaluated);
            section.writeI32(v.integrationProgress);
            section.writeU8(v.integrationComplete ? 1 : 0);
            section.writeU8(static_cast<uint8_t>(v.activeCollapse));
            section.writeI32(v.peakGDP);
            section.writeI32(v.turnsGDPBelowHalf);
            section.writeI32(v.turnsLowLoyalty);
            section.writeU8(v.isEliminated ? 1 : 0);
        }
    }

    writeSection(out, SectionId::VictoryState, section);
}

void writeStockpilesSection(WriteBuffer& out, const aoc::ecs::World& world) {
    WriteBuffer section;

    const aoc::ecs::ComponentPool<aoc::sim::CityComponent>* cityPool =
        world.getPool<aoc::sim::CityComponent>();
    if (cityPool == nullptr) {
        section.writeU32(0);
        writeSection(out, SectionId::Stockpiles, section);
        return;
    }

    uint32_t count = 0;
    for (uint32_t i = 0; i < cityPool->size(); ++i) {
        EntityId cityEntity = cityPool->entities()[i];
        if (world.tryGetComponent<aoc::sim::CityStockpileComponent>(cityEntity) != nullptr) {
            ++count;
        }
    }

    section.writeU32(count);
    for (uint32_t i = 0; i < cityPool->size(); ++i) {
        EntityId cityEntity = cityPool->entities()[i];
        const aoc::sim::CityStockpileComponent* stockpile =
            world.tryGetComponent<aoc::sim::CityStockpileComponent>(cityEntity);
        if (stockpile != nullptr) {
            section.writeU32(i);  // city index in pool order
            section.writeU32(static_cast<uint32_t>(stockpile->goods.size()));
            for (const std::pair<const uint16_t, int32_t>& entry : stockpile->goods) {
                section.writeU16(entry.first);
                section.writeI32(entry.second);
            }
        }
    }

    writeSection(out, SectionId::Stockpiles, section);
}

// ============================================================================
// v4 section writers: PlayerState, Diplomacy, Market, Wonders, MiscEntities
// ============================================================================

void writePlayerStateSection(WriteBuffer& out, const aoc::ecs::World& world) {
    WriteBuffer section;

    // --- PlayerCivilizationComponent ---
    const aoc::ecs::ComponentPool<aoc::sim::PlayerCivilizationComponent>* civPool =
        world.getPool<aoc::sim::PlayerCivilizationComponent>();
    uint32_t civCount = civPool ? civPool->size() : 0;
    section.writeU32(civCount);
    if (civPool != nullptr) {
        for (uint32_t i = 0; i < civPool->size(); ++i) {
            const aoc::sim::PlayerCivilizationComponent& comp = civPool->data()[i];
            section.writeU8(comp.owner);
            section.writeU8(comp.civId);
        }
    }

    // --- PlayerEraComponent ---
    const aoc::ecs::ComponentPool<aoc::sim::PlayerEraComponent>* eraPool =
        world.getPool<aoc::sim::PlayerEraComponent>();
    uint32_t eraCount = eraPool ? eraPool->size() : 0;
    section.writeU32(eraCount);
    if (eraPool != nullptr) {
        for (uint32_t i = 0; i < eraPool->size(); ++i) {
            const aoc::sim::PlayerEraComponent& comp = eraPool->data()[i];
            section.writeU8(comp.owner);
            section.writeU16(comp.currentEra.value);
        }
    }

    // --- PlayerEconomyComponent ---
    const aoc::ecs::ComponentPool<aoc::sim::PlayerEconomyComponent>* econPool =
        world.getPool<aoc::sim::PlayerEconomyComponent>();
    uint32_t econCount = econPool ? econPool->size() : 0;
    section.writeU32(econCount);
    if (econPool != nullptr) {
        for (uint32_t i = 0; i < econPool->size(); ++i) {
            const aoc::sim::PlayerEconomyComponent& comp = econPool->data()[i];
            section.writeU8(comp.owner);
            section.writeI64(comp.treasury);
            section.writeI64(comp.incomePerTurn);
        }
    }

    // --- PlayerGreatPeopleComponent ---
    const aoc::ecs::ComponentPool<aoc::sim::PlayerGreatPeopleComponent>* gpPool =
        world.getPool<aoc::sim::PlayerGreatPeopleComponent>();
    uint32_t gpCount = gpPool ? gpPool->size() : 0;
    section.writeU32(gpCount);
    if (gpPool != nullptr) {
        constexpr std::size_t GP_TYPE_COUNT =
            static_cast<std::size_t>(aoc::sim::GreatPersonType::Count);
        for (uint32_t i = 0; i < gpPool->size(); ++i) {
            const aoc::sim::PlayerGreatPeopleComponent& comp = gpPool->data()[i];
            section.writeU8(comp.owner);
            for (std::size_t t = 0; t < GP_TYPE_COUNT; ++t) {
                section.writeF32(comp.points[t]);
            }
            for (std::size_t t = 0; t < GP_TYPE_COUNT; ++t) {
                section.writeI32(comp.recruited[t]);
            }
        }
    }

    // --- PlayerEurekaComponent ---
    const aoc::ecs::ComponentPool<aoc::sim::PlayerEurekaComponent>* eurekaPool =
        world.getPool<aoc::sim::PlayerEurekaComponent>();
    uint32_t eurekaCount = eurekaPool ? eurekaPool->size() : 0;
    section.writeU32(eurekaCount);
    if (eurekaPool != nullptr) {
        for (uint32_t i = 0; i < eurekaPool->size(); ++i) {
            const aoc::sim::PlayerEurekaComponent& comp = eurekaPool->data()[i];
            section.writeU8(comp.owner);
            // Serialize the bitset as a fixed number of bytes
            constexpr uint16_t bitCount = aoc::sim::MAX_EUREKA_BOOSTS;
            constexpr uint16_t byteCount = (bitCount + 7) / 8;
            section.writeU16(bitCount);
            for (uint16_t b = 0; b < byteCount; ++b) {
                uint8_t byte = 0;
                for (uint8_t bit = 0; bit < 8; ++bit) {
                    uint16_t idx = static_cast<uint16_t>(b * 8 + bit);
                    if (idx < bitCount && comp.triggeredBoosts.test(idx)) {
                        byte |= static_cast<uint8_t>(1u << bit);
                    }
                }
                section.writeU8(byte);
            }
        }
    }

    // --- PlayerWarComponent ---
    const aoc::ecs::ComponentPool<aoc::sim::PlayerWarComponent>* warPool =
        world.getPool<aoc::sim::PlayerWarComponent>();
    uint32_t warCompCount = warPool ? warPool->size() : 0;
    section.writeU32(warCompCount);
    if (warPool != nullptr) {
        for (uint32_t i = 0; i < warPool->size(); ++i) {
            const aoc::sim::PlayerWarComponent& comp = warPool->data()[i];
            section.writeU8(comp.owner);
            section.writeU32(static_cast<uint32_t>(comp.activeWars.size()));
            for (const aoc::sim::ActiveWar& war : comp.activeWars) {
                section.writeU8(war.aggressor);
                section.writeU8(war.defender);
                section.writeU8(static_cast<uint8_t>(war.casusBelli));
                section.writeU32(war.startTurn);
                section.writeI32(war.aggressorWarScore);
                section.writeI32(war.defenderWarScore);
            }
        }
    }

    writeSection(out, SectionId::PlayerState, section);
}

void writeDiplomacySection(WriteBuffer& out, const aoc::sim::DiplomacyManager& diplomacy) {
    WriteBuffer section;

    uint8_t playerCount = diplomacy.playerCount();
    section.writeU8(playerCount);

    for (uint8_t a = 0; a < playerCount; ++a) {
        for (uint8_t b = a + 1; b < playerCount; ++b) {
            const aoc::sim::PairwiseRelation& rel = diplomacy.relation(a, b);
            section.writeI32(rel.baseScore);
            section.writeU8(rel.isAtWar ? uint8_t{1} : uint8_t{0});
            section.writeU8(rel.hasOpenBorders ? uint8_t{1} : uint8_t{0});
            section.writeU8(rel.hasDefensiveAlliance ? uint8_t{1} : uint8_t{0});
            section.writeU32(static_cast<uint32_t>(rel.modifiers.size()));
            for (const aoc::sim::RelationModifier& mod : rel.modifiers) {
                section.writeString(mod.reason);
                section.writeI32(mod.amount);
                section.writeI32(mod.turnsRemaining);
            }
        }
    }

    writeSection(out, SectionId::Diplomacy, section);
}

void writeMarketSection(WriteBuffer& out, const aoc::sim::EconomySimulation& economy) {
    WriteBuffer section;

    const aoc::sim::Market& market = economy.market();
    uint16_t count = market.goodsCount();
    section.writeU16(count);
    for (uint16_t i = 0; i < count; ++i) {
        const aoc::sim::Market::GoodMarketData& data = market.marketData(i);
        section.writeI32(data.currentPrice);
        section.writeI32(data.basePrice);
    }

    writeSection(out, SectionId::Market, section);
}

void writeWonderSection(WriteBuffer& out, const aoc::ecs::World& world) {
    WriteBuffer section;

    // GlobalWonderTracker
    const aoc::ecs::ComponentPool<aoc::sim::GlobalWonderTracker>* trackerPool =
        world.getPool<aoc::sim::GlobalWonderTracker>();
    uint8_t hasTracker = (trackerPool != nullptr && trackerPool->size() > 0) ? uint8_t{1} : uint8_t{0};
    section.writeU8(hasTracker);
    if (hasTracker != 0) {
        const aoc::sim::GlobalWonderTracker& tracker = trackerPool->data()[0];
        for (uint8_t w = 0; w < aoc::sim::WONDER_COUNT; ++w) {
            section.writeU8(tracker.builtBy[w]);
        }
    }

    // CityWondersComponent (indexed by city pool order)
    const aoc::ecs::ComponentPool<aoc::sim::CityComponent>* cityPool =
        world.getPool<aoc::sim::CityComponent>();
    uint32_t cityWonderCount = 0;
    if (cityPool != nullptr) {
        for (uint32_t i = 0; i < cityPool->size(); ++i) {
            EntityId cityEntity = cityPool->entities()[i];
            if (world.tryGetComponent<aoc::sim::CityWondersComponent>(cityEntity) != nullptr) {
                ++cityWonderCount;
            }
        }
    }

    section.writeU32(cityWonderCount);
    if (cityPool != nullptr) {
        for (uint32_t i = 0; i < cityPool->size(); ++i) {
            EntityId cityEntity = cityPool->entities()[i];
            const aoc::sim::CityWondersComponent* wonders =
                world.tryGetComponent<aoc::sim::CityWondersComponent>(cityEntity);
            if (wonders != nullptr) {
                section.writeU32(i);  // city index in pool order
                section.writeU32(static_cast<uint32_t>(wonders->wonders.size()));
                for (aoc::sim::WonderId wid : wonders->wonders) {
                    section.writeU8(wid);
                }
            }
        }
    }

    writeSection(out, SectionId::WonderState, section);
}

void writeMiscEntitiesSection(WriteBuffer& out, const aoc::ecs::World& world) {
    WriteBuffer section;

    // --- BarbarianEncampmentComponent ---
    const aoc::ecs::ComponentPool<aoc::sim::BarbarianEncampmentComponent>* barbPool =
        world.getPool<aoc::sim::BarbarianEncampmentComponent>();
    uint32_t barbCount = barbPool ? barbPool->size() : 0;
    section.writeU32(barbCount);
    if (barbPool != nullptr) {
        for (uint32_t i = 0; i < barbPool->size(); ++i) {
            const aoc::sim::BarbarianEncampmentComponent& comp = barbPool->data()[i];
            section.writeI32(comp.location.q);
            section.writeI32(comp.location.r);
            section.writeI32(comp.spawnCooldown);
            section.writeI32(comp.unitsSpawned);
        }
    }

    // --- GreatPersonComponent ---
    const aoc::ecs::ComponentPool<aoc::sim::GreatPersonComponent>* gpPersonPool =
        world.getPool<aoc::sim::GreatPersonComponent>();
    uint32_t gpPersonCount = gpPersonPool ? gpPersonPool->size() : 0;
    section.writeU32(gpPersonCount);
    if (gpPersonPool != nullptr) {
        for (uint32_t i = 0; i < gpPersonPool->size(); ++i) {
            const aoc::sim::GreatPersonComponent& comp = gpPersonPool->data()[i];
            section.writeU8(comp.owner);
            section.writeU8(comp.defId);
            section.writeI32(comp.position.q);
            section.writeI32(comp.position.r);
            section.writeU8(comp.isActivated ? uint8_t{1} : uint8_t{0});
        }
    }

    // --- SpyComponent ---
    const aoc::ecs::ComponentPool<aoc::sim::SpyComponent>* spyPool =
        world.getPool<aoc::sim::SpyComponent>();
    uint32_t spyCount = spyPool ? spyPool->size() : 0;
    section.writeU32(spyCount);
    if (spyPool != nullptr) {
        for (uint32_t i = 0; i < spyPool->size(); ++i) {
            const aoc::sim::SpyComponent& comp = spyPool->data()[i];
            section.writeU8(comp.owner);
            section.writeI32(comp.location.q);
            section.writeI32(comp.location.r);
            section.writeU8(static_cast<uint8_t>(comp.currentMission));
            section.writeI32(comp.turnsRemaining);
            section.writeI32(comp.experience);
            section.writeU8(comp.isRevealed ? uint8_t{1} : uint8_t{0});
        }
    }

    // --- UnitExperienceComponent (linked to unit pool order) ---
    const aoc::ecs::ComponentPool<aoc::sim::UnitComponent>* unitPool =
        world.getPool<aoc::sim::UnitComponent>();
    uint32_t expCount = 0;
    if (unitPool != nullptr) {
        for (uint32_t i = 0; i < unitPool->size(); ++i) {
            EntityId unitEntity = unitPool->entities()[i];
            if (world.tryGetComponent<aoc::sim::UnitExperienceComponent>(unitEntity) != nullptr) {
                ++expCount;
            }
        }
    }

    section.writeU32(expCount);
    if (unitPool != nullptr) {
        for (uint32_t i = 0; i < unitPool->size(); ++i) {
            EntityId unitEntity = unitPool->entities()[i];
            const aoc::sim::UnitExperienceComponent* exp =
                world.tryGetComponent<aoc::sim::UnitExperienceComponent>(unitEntity);
            if (exp != nullptr) {
                section.writeU32(i);  // unit index in pool order
                section.writeI32(exp->experience);
                section.writeI32(exp->level);
                section.writeU32(static_cast<uint32_t>(exp->promotions.size()));
                for (PromotionId pid : exp->promotions) {
                    section.writeU16(pid.value);
                }
            }
        }
    }

    writeSection(out, SectionId::MiscEntities, section);
}

void writeCurrencyTrustSection(WriteBuffer& out, const aoc::ecs::World& world) {
    WriteBuffer section;

    const aoc::ecs::ComponentPool<aoc::sim::CurrencyTrustComponent>* pool =
        world.getPool<aoc::sim::CurrencyTrustComponent>();
    uint32_t count = (pool != nullptr) ? pool->size() : 0;
    section.writeU32(count);
    if (pool != nullptr) {
        for (uint32_t i = 0; i < pool->size(); ++i) {
            const aoc::sim::CurrencyTrustComponent& ct = pool->data()[i];
            section.writeU8(ct.owner);
            section.writeF32(ct.trustScore);
            section.writeI32(ct.turnsOnFiat);
            section.writeI32(ct.turnsStable);
            section.writeU8(ct.isReserveCurrency ? 1 : 0);
            section.writeI32(ct.turnsAsReserve);
            for (int32_t p = 0; p < aoc::sim::CurrencyTrustComponent::MAX_PLAYERS; ++p) {
                section.writeF32(ct.bilateralTrust[p]);
            }
        }
    }

    writeSection(out, SectionId::CurrencyTrust, section);
}

void writeCrisisSection(WriteBuffer& out, const aoc::ecs::World& world) {
    WriteBuffer section;
    const aoc::ecs::ComponentPool<aoc::sim::CurrencyCrisisComponent>* pool =
        world.getPool<aoc::sim::CurrencyCrisisComponent>();
    uint32_t count = (pool != nullptr) ? pool->size() : 0;
    section.writeU32(count);
    if (pool != nullptr) {
        for (uint32_t i = 0; i < pool->size(); ++i) {
            const aoc::sim::CurrencyCrisisComponent& c = pool->data()[i];
            section.writeU8(c.owner);
            section.writeU8(static_cast<uint8_t>(c.activeCrisis));
            section.writeI32(c.turnsRemaining);
            section.writeI32(c.turnsHighInflation);
            section.writeU8(c.hasDefaulted ? 1 : 0);
            section.writeI32(c.defaultCooldown);
        }
    }
    writeSection(out, SectionId::CrisisState, section);
}

void writeBondSection(WriteBuffer& out, const aoc::ecs::World& world) {
    WriteBuffer section;
    const aoc::ecs::ComponentPool<aoc::sim::PlayerBondComponent>* pool =
        world.getPool<aoc::sim::PlayerBondComponent>();
    uint32_t count = (pool != nullptr) ? pool->size() : 0;
    section.writeU32(count);
    if (pool != nullptr) {
        for (uint32_t i = 0; i < pool->size(); ++i) {
            const aoc::sim::PlayerBondComponent& pb = pool->data()[i];
            section.writeU8(pb.owner);
            section.writeU32(static_cast<uint32_t>(pb.issuedBonds.size()));
            for (const aoc::sim::BondIssue& b : pb.issuedBonds) {
                section.writeU8(b.issuer);
                section.writeU8(b.holder);
                section.writeI64(b.principal);
                section.writeF32(b.yieldRate);
                section.writeI32(b.turnsToMaturity);
                section.writeI64(b.accruedInterest);
            }
            section.writeU32(static_cast<uint32_t>(pb.heldBonds.size()));
            for (const aoc::sim::BondIssue& b : pb.heldBonds) {
                section.writeU8(b.issuer);
                section.writeU8(b.holder);
                section.writeI64(b.principal);
                section.writeF32(b.yieldRate);
                section.writeI32(b.turnsToMaturity);
                section.writeI64(b.accruedInterest);
            }
        }
    }
    writeSection(out, SectionId::BondState, section);
}

void writeDevaluationSection(WriteBuffer& out, const aoc::ecs::World& world) {
    WriteBuffer section;
    const aoc::ecs::ComponentPool<aoc::sim::CurrencyDevaluationComponent>* pool =
        world.getPool<aoc::sim::CurrencyDevaluationComponent>();
    uint32_t count = (pool != nullptr) ? pool->size() : 0;
    section.writeU32(count);
    if (pool != nullptr) {
        for (uint32_t i = 0; i < pool->size(); ++i) {
            const aoc::sim::CurrencyDevaluationComponent& d = pool->data()[i];
            section.writeU8(d.owner);
            section.writeU8(d.isDevalued ? 1 : 0);
            section.writeI32(d.devaluationTurnsLeft);
            section.writeF32(d.exportBonus);
            section.writeF32(d.importPenalty);
            section.writeI32(d.devaluationCount);
        }
    }
    writeSection(out, SectionId::DevaluationState, section);
}

void writeHoardSection(WriteBuffer& out, const aoc::ecs::World& world) {
    WriteBuffer section;
    const aoc::ecs::ComponentPool<aoc::sim::CommodityHoardComponent>* pool =
        world.getPool<aoc::sim::CommodityHoardComponent>();
    uint32_t count = (pool != nullptr) ? pool->size() : 0;
    section.writeU32(count);
    if (pool != nullptr) {
        for (uint32_t i = 0; i < pool->size(); ++i) {
            const aoc::sim::CommodityHoardComponent& h = pool->data()[i];
            section.writeU8(h.owner);
            section.writeU32(static_cast<uint32_t>(h.positions.size()));
            for (const aoc::sim::CommodityHoardComponent::HoardPosition& pos : h.positions) {
                section.writeU16(pos.goodId);
                section.writeI32(pos.amount);
                section.writeI32(pos.purchasePrice);
            }
        }
    }
    writeSection(out, SectionId::HoardState, section);
}

void writeProductionExpSection(WriteBuffer& out, const aoc::ecs::World& world) {
    WriteBuffer section;
    const aoc::ecs::ComponentPool<aoc::sim::CityProductionExperienceComponent>* pool =
        world.getPool<aoc::sim::CityProductionExperienceComponent>();
    uint32_t count = (pool != nullptr) ? pool->size() : 0;
    section.writeU32(count);
    if (pool != nullptr) {
        for (uint32_t i = 0; i < pool->size(); ++i) {
            const aoc::sim::CityProductionExperienceComponent& c = pool->data()[i];
            section.writeU32(pool->entities()[i].index);
            section.writeU32(static_cast<uint32_t>(c.recipeExperience.size()));
            for (const std::pair<const uint16_t, int32_t>& entry : c.recipeExperience) {
                section.writeU16(entry.first);
                section.writeI32(entry.second);
            }
        }
    }
    writeSection(out, SectionId::ProductionExp, section);
}

void writeBuildingLevelsSection(WriteBuffer& out, const aoc::ecs::World& world) {
    WriteBuffer section;
    const aoc::ecs::ComponentPool<aoc::sim::CityBuildingLevelsComponent>* pool =
        world.getPool<aoc::sim::CityBuildingLevelsComponent>();
    uint32_t count = (pool != nullptr) ? pool->size() : 0;
    section.writeU32(count);
    if (pool != nullptr) {
        for (uint32_t i = 0; i < pool->size(); ++i) {
            const aoc::sim::CityBuildingLevelsComponent& c = pool->data()[i];
            section.writeU32(pool->entities()[i].index);
            section.writeU32(static_cast<uint32_t>(c.levels.size()));
            for (const std::pair<const uint16_t, int32_t>& entry : c.levels) {
                section.writeU16(entry.first);
                section.writeI32(entry.second);
            }
        }
    }
    writeSection(out, SectionId::BuildingLevels, section);
}

void writePollutionSection(WriteBuffer& out, const aoc::ecs::World& world) {
    WriteBuffer section;
    const aoc::ecs::ComponentPool<aoc::sim::CityPollutionComponent>* pool =
        world.getPool<aoc::sim::CityPollutionComponent>();
    uint32_t count = (pool != nullptr) ? pool->size() : 0;
    section.writeU32(count);
    if (pool != nullptr) {
        for (uint32_t i = 0; i < pool->size(); ++i) {
            const aoc::sim::CityPollutionComponent& c = pool->data()[i];
            section.writeU32(pool->entities()[i].index);
            section.writeI32(c.wasteAccumulated);
            section.writeI32(c.co2ContributionPerTurn);
        }
    }
    writeSection(out, SectionId::PollutionState, section);
}

void writeAutomationSection(WriteBuffer& out, const aoc::ecs::World& world) {
    WriteBuffer section;
    const aoc::ecs::ComponentPool<aoc::sim::CityAutomationComponent>* pool =
        world.getPool<aoc::sim::CityAutomationComponent>();
    uint32_t count = (pool != nullptr) ? pool->size() : 0;
    section.writeU32(count);
    if (pool != nullptr) {
        for (uint32_t i = 0; i < pool->size(); ++i) {
            const aoc::sim::CityAutomationComponent& c = pool->data()[i];
            section.writeU32(pool->entities()[i].index);
            section.writeI32(c.robotWorkers);
            section.writeI32(c.turnsSinceLastMaintenance);
        }
    }
    writeSection(out, SectionId::AutomationState, section);
}

void writeIndustrialSection(WriteBuffer& out, const aoc::ecs::World& world) {
    WriteBuffer section;
    const aoc::ecs::ComponentPool<aoc::sim::PlayerIndustrialComponent>* pool =
        world.getPool<aoc::sim::PlayerIndustrialComponent>();
    uint32_t count = (pool != nullptr) ? pool->size() : 0;
    section.writeU32(count);
    if (pool != nullptr) {
        for (uint32_t i = 0; i < pool->size(); ++i) {
            const aoc::sim::PlayerIndustrialComponent& ind = pool->data()[i];
            section.writeU8(ind.owner);
            section.writeU8(static_cast<uint8_t>(ind.currentRevolution));
            for (int32_t r = 0; r < 6; ++r) {
                section.writeI32(ind.turnAchieved[r]);
            }
        }
    }
    writeSection(out, SectionId::IndustrialState, section);
}

} // anonymous namespace

// ============================================================================
// Save
// ============================================================================

ErrorCode saveGame(const std::string& filepath,
                    const aoc::ecs::World& world,
                    const aoc::map::HexGrid& grid,
                    const aoc::sim::TurnManager& turnManager,
                    const aoc::sim::EconomySimulation& economy,
                    const aoc::sim::DiplomacyManager& diplomacy,
                    const aoc::map::FogOfWar& /*fogOfWar*/,
                    const aoc::Random& rng) {
    WriteBuffer buf;

    // Header
    buf.writeU32(SAVE_MAGIC);
    buf.writeU32(SAVE_VERSION);
    buf.writeU32(0);  // flags (reserved)
    buf.writeU32(0);  // dataSize placeholder (filled after)

    // Sections
    writeMapSection(buf, grid);
    writeTurnSection(buf, turnManager);
    writeEntitySection(buf, world);
    writeRandomSection(buf, rng);
    writeImprovementsSection(buf, grid);
    writeTechProgressSection(buf, world);
    writeProductionQueuesSection(buf, world);
    writeDistrictsSection(buf, world);
    writeMonetarySection(buf, world);
    writeGovernmentSection(buf, world);
    writeVictorySection(buf, world);
    writeStockpilesSection(buf, world);
    // v4 sections
    writePlayerStateSection(buf, world);
    writeDiplomacySection(buf, diplomacy);
    writeMarketSection(buf, economy);
    writeWonderSection(buf, world);
    writeMiscEntitiesSection(buf, world);
    writeCurrencyTrustSection(buf, world);
    writeCrisisSection(buf, world);
    writeBondSection(buf, world);
    writeDevaluationSection(buf, world);
    writeHoardSection(buf, world);
    writeProductionExpSection(buf, world);
    writeBuildingLevelsSection(buf, world);
    writePollutionSection(buf, world);
    writeAutomationSection(buf, world);
    writeIndustrialSection(buf, world);

    // Write to file
    std::ofstream file(filepath, std::ios::binary);
    if (!file.is_open()) {
        LOG_ERROR("Failed to open file for writing: %s", filepath.c_str());
        return ErrorCode::SaveFailed;
    }

    file.write(reinterpret_cast<const char*>(buf.data().data()),
               static_cast<std::streamsize>(buf.size()));
    if (!file.good()) {
        return ErrorCode::SaveFailed;
    }

    LOG_INFO("Game saved to %s (%zu bytes)", filepath.c_str(), buf.size());
    return ErrorCode::Ok;
}

// ============================================================================
// Load
// ============================================================================

ErrorCode loadGame(const std::string& filepath,
                    aoc::ecs::World& world,
                    aoc::map::HexGrid& grid,
                    aoc::sim::TurnManager& turnManager,
                    aoc::sim::EconomySimulation& economy,
                    aoc::sim::DiplomacyManager& diplomacy,
                    aoc::map::FogOfWar& /*fogOfWar*/,
                    aoc::Random& rng) {
    // Read entire file
    std::ifstream file(filepath, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        LOG_ERROR("Failed to open file: %s", filepath.c_str());
        return ErrorCode::LoadFailed;
    }

    std::streamsize fileSize = file.tellg();
    file.seekg(0, std::ios::beg);
    std::vector<uint8_t> fileData(static_cast<std::size_t>(fileSize));
    file.read(reinterpret_cast<char*>(fileData.data()), fileSize);
    if (!file.good()) {
        return ErrorCode::LoadFailed;
    }

    ReadBuffer buf(fileData);

    // Validate header
    if (!buf.hasRemaining(16)) {
        return ErrorCode::SaveCorrupted;
    }
    uint32_t magic = buf.readU32();
    if (magic != SAVE_MAGIC) {
        return ErrorCode::SaveCorrupted;
    }
    uint32_t version = buf.readU32();
    if (version != SAVE_VERSION) {
        return ErrorCode::SaveVersionMismatch;
    }
    [[maybe_unused]] uint32_t flags    = buf.readU32();
    [[maybe_unused]] uint32_t dataSize = buf.readU32();

    // City entities are collected during the Entities section so that later
    // sections can reference them by pool index.
    std::vector<EntityId> loadedCityEntities;

    // Unit entities collected for the MiscEntities section (experience).
    std::vector<EntityId> loadedUnitEntities;

    // Read sections (skip unknown ones)
    while (buf.hasRemaining(6)) {
        uint16_t sectionId = buf.readU16();
        uint32_t sectionSize = buf.readU32();

        if (!buf.hasRemaining(sectionSize)) {
            return ErrorCode::SaveCorrupted;
        }

        switch (static_cast<SectionId>(sectionId)) {
            case SectionId::MapGrid: {
                int32_t width  = buf.readI32();
                int32_t height = buf.readI32();
                grid.initialize(width, height);
                int32_t count = width * height;
                for (int32_t i = 0; i < count; ++i) {
                    grid.setTerrain(i, static_cast<aoc::map::TerrainType>(buf.readU8()));
                    grid.setFeature(i, static_cast<aoc::map::FeatureType>(buf.readU8()));
                    grid.setElevation(i, static_cast<int8_t>(buf.readU8()));
                    grid.setRiverEdges(i, buf.readU8());
                    grid.setResource(i, ResourceId{buf.readU16()});
                    grid.setOwner(i, buf.readU8());
                }
                break;
            }
            case SectionId::TurnState: {
                uint32_t turnNum = buf.readU32();
                uint8_t phase    = buf.readU8();
                turnManager.setTurnNumber(turnNum);
                turnManager.setPhase(static_cast<aoc::sim::TurnPhase>(phase));
                break;
            }
            case SectionId::Entities: {
                // Units
                uint32_t unitCount = buf.readU32();
                loadedUnitEntities.reserve(unitCount);
                for (uint32_t i = 0; i < unitCount; ++i) {
                    PlayerId owner = buf.readU8();
                    UnitTypeId typeId{buf.readU16()};
                    hex::AxialCoord pos{buf.readI32(), buf.readI32()};
                    int32_t hp = buf.readI32();
                    int32_t mp = buf.readI32();
                    uint8_t stateVal = buf.readU8();
                    // v4: chargesRemaining, cargoCapacity, pendingPath
                    int8_t charges = static_cast<int8_t>(buf.readU8());
                    int8_t cargo   = static_cast<int8_t>(buf.readU8());
                    uint16_t pathSize = buf.readU16();
                    std::vector<hex::AxialCoord> pendingPath;
                    pendingPath.reserve(pathSize);
                    for (uint16_t p = 0; p < pathSize; ++p) {
                        pendingPath.push_back({buf.readI32(), buf.readI32()});
                    }

                    EntityId entity = world.createEntity();
                    aoc::sim::UnitComponent unit = aoc::sim::UnitComponent::create(owner, typeId, pos);
                    unit.hitPoints = hp;
                    unit.movementRemaining = mp;
                    unit.state = static_cast<aoc::sim::UnitState>(stateVal);
                    unit.chargesRemaining = charges;
                    unit.cargoCapacity = cargo;
                    unit.pendingPath = std::move(pendingPath);
                    world.addComponent<aoc::sim::UnitComponent>(entity, std::move(unit));
                    loadedUnitEntities.push_back(entity);
                }

                // Cities
                uint32_t cityCount = buf.readU32();
                loadedCityEntities.reserve(cityCount);
                for (uint32_t i = 0; i < cityCount; ++i) {
                    PlayerId owner = buf.readU8();
                    hex::AxialCoord loc{buf.readI32(), buf.readI32()};
                    std::string name = buf.readString();
                    int32_t pop = buf.readI32();
                    float food = buf.readF32();
                    float prod = buf.readF32();

                    uint32_t workedCount = buf.readU32();
                    std::vector<hex::AxialCoord> worked;
                    worked.reserve(workedCount);
                    for (uint32_t j = 0; j < workedCount; ++j) {
                        worked.push_back({buf.readI32(), buf.readI32()});
                    }

                    // v4: cultureBorderProgress, tilesClaimedCount, isOriginalCapital, originalOwner
                    float cultureBorder = buf.readF32();
                    int32_t tilesClaimed = buf.readI32();
                    bool isOrigCapital = buf.readU8() != 0;
                    PlayerId origOwner = buf.readU8();

                    EntityId entity = world.createEntity();
                    aoc::sim::CityComponent city = aoc::sim::CityComponent::create(owner, loc, std::move(name));
                    city.population = pop;
                    city.foodSurplus = food;
                    city.productionProgress = prod;
                    city.workedTiles = std::move(worked);
                    city.cultureBorderProgress = cultureBorder;
                    city.tilesClaimedCount = tilesClaimed;
                    city.isOriginalCapital = isOrigCapital;
                    city.originalOwner = origOwner;
                    world.addComponent<aoc::sim::CityComponent>(entity, std::move(city));

                    // Ensure every city has a production queue component
                    world.addComponent<aoc::sim::ProductionQueueComponent>(
                        entity, aoc::sim::ProductionQueueComponent{});

                    loadedCityEntities.push_back(entity);
                }
                break;
            }
            case SectionId::RandomState: {
                std::array<uint64_t, 4> state;
                for (uint64_t& s : state) {
                    s = buf.readU64();
                }
                rng.setState(state);
                break;
            }
            case SectionId::Improvements: {
                int32_t count = buf.readI32();
                for (int32_t i = 0; i < count; ++i) {
                    uint8_t improvementVal = buf.readU8();
                    uint8_t roadVal = buf.readU8();
                    if (i < grid.tileCount()) {
                        grid.setImprovement(i, static_cast<aoc::map::ImprovementType>(improvementVal));
                        if (roadVal != 0 && !grid.hasRoad(i)) {
                            grid.setImprovement(i, aoc::map::ImprovementType::Road);
                        }
                    }
                }
                break;
            }
            case SectionId::TechProgress: {
                // Tech components
                uint32_t techCompCount = buf.readU32();
                for (uint32_t i = 0; i < techCompCount; ++i) {
                    PlayerId owner = buf.readU8();
                    uint16_t currentResearchVal = buf.readU16();
                    float progress = buf.readF32();
                    uint16_t totalTechs = buf.readU16();
                    uint16_t byteCount = static_cast<uint16_t>((totalTechs + 7) / 8);

                    // Find or create the player tech component
                    aoc::sim::PlayerTechComponent techComp{};
                    techComp.owner = owner;
                    techComp.initialize();
                    techComp.currentResearch = TechId{currentResearchVal};
                    techComp.researchProgress = progress;

                    for (uint16_t b = 0; b < byteCount; ++b) {
                        uint8_t byte = buf.readU8();
                        for (uint8_t bit = 0; bit < 8; ++bit) {
                            uint16_t techIdx = static_cast<uint16_t>(b * 8 + bit);
                            if (techIdx < totalTechs && techIdx < techComp.completedTechs.size()) {
                                techComp.completedTechs[techIdx] = ((byte >> bit) & 1u) != 0;
                            }
                        }
                    }

                    // Attach to a new entity (player state entity)
                    EntityId playerEntity = world.createEntity();
                    world.addComponent<aoc::sim::PlayerTechComponent>(playerEntity, std::move(techComp));
                }

                // Civic components
                uint32_t civicCompCount = buf.readU32();
                for (uint32_t i = 0; i < civicCompCount; ++i) {
                    PlayerId owner = buf.readU8();
                    uint16_t currentResearchVal = buf.readU16();
                    float progress = buf.readF32();
                    uint16_t totalCivics = buf.readU16();
                    uint16_t byteCount = static_cast<uint16_t>((totalCivics + 7) / 8);

                    aoc::sim::PlayerCivicComponent civicComp{};
                    civicComp.owner = owner;
                    civicComp.initialize();
                    civicComp.currentResearch = CivicId{currentResearchVal};
                    civicComp.researchProgress = progress;

                    for (uint16_t b = 0; b < byteCount; ++b) {
                        uint8_t byte = buf.readU8();
                        for (uint8_t bit = 0; bit < 8; ++bit) {
                            uint16_t civicIdx = static_cast<uint16_t>(b * 8 + bit);
                            if (civicIdx < totalCivics && civicIdx < civicComp.completedCivics.size()) {
                                civicComp.completedCivics[civicIdx] = ((byte >> bit) & 1u) != 0;
                            }
                        }
                    }

                    EntityId playerEntity = world.createEntity();
                    world.addComponent<aoc::sim::PlayerCivicComponent>(playerEntity, std::move(civicComp));
                }
                break;
            }
            case SectionId::ProductionQueues: {
                uint32_t count = buf.readU32();
                for (uint32_t i = 0; i < count; ++i) {
                    uint32_t cityIndex = buf.readU32();
                    uint32_t queueSize = buf.readU32();

                    aoc::sim::ProductionQueueComponent queue{};
                    for (uint32_t j = 0; j < queueSize; ++j) {
                        aoc::sim::ProductionQueueItem item{};
                        item.type = static_cast<aoc::sim::ProductionItemType>(buf.readU8());
                        item.itemId = buf.readU16();
                        item.name = buf.readString();
                        item.totalCost = buf.readF32();
                        item.progress = buf.readF32();
                        queue.queue.push_back(std::move(item));
                    }

                    if (cityIndex < static_cast<uint32_t>(loadedCityEntities.size())) {
                        EntityId cityEntity = loadedCityEntities[cityIndex];
                        // Replace the default empty queue with loaded data
                        aoc::sim::ProductionQueueComponent* existingQueue =
                            world.tryGetComponent<aoc::sim::ProductionQueueComponent>(cityEntity);
                        if (existingQueue != nullptr) {
                            existingQueue->queue = std::move(queue.queue);
                        }
                    }
                }
                break;
            }
            case SectionId::Districts: {
                uint32_t count = buf.readU32();
                for (uint32_t i = 0; i < count; ++i) {
                    uint32_t cityIndex = buf.readU32();
                    uint32_t districtCount = buf.readU32();

                    aoc::sim::CityDistrictsComponent districts{};
                    for (uint32_t d = 0; d < districtCount; ++d) {
                        aoc::sim::CityDistrictsComponent::PlacedDistrict dist{};
                        dist.type = static_cast<aoc::sim::DistrictType>(buf.readU8());
                        dist.location.q = buf.readI32();
                        dist.location.r = buf.readI32();
                        uint32_t buildingCount = buf.readU32();
                        dist.buildings.reserve(buildingCount);
                        for (uint32_t b = 0; b < buildingCount; ++b) {
                            dist.buildings.push_back(BuildingId{buf.readU16()});
                        }
                        districts.districts.push_back(std::move(dist));
                    }

                    if (cityIndex < static_cast<uint32_t>(loadedCityEntities.size())) {
                        EntityId cityEntity = loadedCityEntities[cityIndex];
                        world.addComponent<aoc::sim::CityDistrictsComponent>(
                            cityEntity, std::move(districts));
                    }
                }
                break;
            }
            case SectionId::MonetaryState: {
                uint32_t count = buf.readU32();
                for (uint32_t i = 0; i < count; ++i) {
                    aoc::sim::MonetaryStateComponent m{};
                    m.owner = buf.readU8();
                    m.system = static_cast<aoc::sim::MonetarySystemType>(buf.readU8());
                    m.moneySupply = buf.readI64();
                    m.treasury = buf.readI64();
                    m.copperCoinReserves = buf.readI32();
                    m.silverCoinReserves = buf.readI32();
                    m.goldCoinReserves = buf.readI32();
                    m.effectiveCoinTier = static_cast<aoc::sim::CoinTier>(buf.readU8());
                    m.goldBackingRatio = buf.readF32();
                    m.inflationRate = buf.readF32();
                    m.priceLevel = buf.readF32();
                    m.interestRate = buf.readF32();
                    m.reserveRequirement = buf.readF32();
                    m.taxRate = buf.readF32();
                    m.governmentSpending = buf.readI64();
                    m.governmentDebt = buf.readI64();
                    m.taxRevenue = buf.readI64();
                    m.deficit = buf.readI64();
                    m.gdp = buf.readI64();
                    m.velocityOfMoney = buf.readF32();
                    m.debasement.debasementRatio = buf.readF32();
                    m.debasement.turnsDebased = buf.readI32();
                    m.debasement.discoveredByPartners = buf.readU8() != 0;
                    m.turnsInCurrentSystem = buf.readI32();

                    EntityId entity = world.createEntity();
                    world.addComponent<aoc::sim::MonetaryStateComponent>(entity, std::move(m));
                }
                break;
            }
            case SectionId::GovernmentState: {
                uint32_t count = buf.readU32();
                for (uint32_t i = 0; i < count; ++i) {
                    aoc::sim::PlayerGovernmentComponent gov{};
                    gov.owner = buf.readU8();
                    gov.government = static_cast<aoc::sim::GovernmentType>(buf.readU8());
                    for (uint8_t s = 0; s < aoc::sim::MAX_POLICY_SLOTS; ++s) {
                        gov.activePolicies[s] = static_cast<int8_t>(buf.readU8());
                    }
                    gov.unlockedGovernments = buf.readU16();
                    gov.unlockedPolicies = buf.readU32();
                    gov.anarchyTurnsRemaining = buf.readI32();
                    gov.activeAction = static_cast<aoc::sim::GovernmentAction>(buf.readU8());
                    gov.actionTurnsRemaining = buf.readI32();

                    EntityId entity = world.createEntity();
                    world.addComponent<aoc::sim::PlayerGovernmentComponent>(entity, std::move(gov));
                }
                break;
            }
            case SectionId::VictoryState: {
                uint32_t count = buf.readU32();
                for (uint32_t i = 0; i < count; ++i) {
                    aoc::sim::VictoryTrackerComponent v{};
                    v.owner = buf.readU8();
                    v.scienceProgress = buf.readI32();
                    v.totalCultureAccumulated = buf.readF32();
                    v.score = buf.readI32();
                    // CSI data
                    for (int32_t c = 0; c < aoc::sim::CSI_CATEGORY_COUNT; ++c) {
                        v.categoryScores[c] = buf.readF32();
                    }
                    v.tradeNetworkMultiplier = buf.readF32();
                    v.financialIntegrationMult = buf.readF32();
                    v.diplomaticWebMult = buf.readF32();
                    v.compositeCSI = buf.readF32();
                    v.eraVictoryPoints = buf.readI32();
                    v.erasEvaluated = buf.readI32();
                    v.integrationProgress = buf.readI32();
                    v.integrationComplete = buf.readU8() != 0;
                    v.activeCollapse = static_cast<aoc::sim::CollapseType>(buf.readU8());
                    v.peakGDP = buf.readI32();
                    v.turnsGDPBelowHalf = buf.readI32();
                    v.turnsLowLoyalty = buf.readI32();
                    v.isEliminated = buf.readU8() != 0;

                    EntityId entity = world.createEntity();
                    world.addComponent<aoc::sim::VictoryTrackerComponent>(entity, std::move(v));
                }
                break;
            }
            case SectionId::Stockpiles: {
                uint32_t count = buf.readU32();
                for (uint32_t i = 0; i < count; ++i) {
                    uint32_t cityIndex = buf.readU32();
                    uint32_t goodsCount = buf.readU32();

                    aoc::sim::CityStockpileComponent stockpile{};
                    for (uint32_t g = 0; g < goodsCount; ++g) {
                        uint16_t goodId = buf.readU16();
                        int32_t amount = buf.readI32();
                        stockpile.goods[goodId] = amount;
                    }

                    if (cityIndex < static_cast<uint32_t>(loadedCityEntities.size())) {
                        EntityId cityEntity = loadedCityEntities[cityIndex];
                        world.addComponent<aoc::sim::CityStockpileComponent>(
                            cityEntity, std::move(stockpile));
                    }
                }
                break;
            }
            // ==============================================================
            // v4 section readers
            // ==============================================================
            case SectionId::PlayerState: {
                // --- PlayerCivilizationComponent ---
                uint32_t civCount = buf.readU32();
                for (uint32_t i = 0; i < civCount; ++i) {
                    aoc::sim::PlayerCivilizationComponent comp{};
                    comp.owner = buf.readU8();
                    comp.civId = buf.readU8();

                    EntityId entity = world.createEntity();
                    world.addComponent<aoc::sim::PlayerCivilizationComponent>(entity, std::move(comp));
                }

                // --- PlayerEraComponent ---
                uint32_t eraCount = buf.readU32();
                for (uint32_t i = 0; i < eraCount; ++i) {
                    aoc::sim::PlayerEraComponent comp{};
                    comp.owner = buf.readU8();
                    comp.currentEra = EraId{buf.readU16()};

                    EntityId entity = world.createEntity();
                    world.addComponent<aoc::sim::PlayerEraComponent>(entity, std::move(comp));
                }

                // --- PlayerEconomyComponent ---
                uint32_t econCount = buf.readU32();
                for (uint32_t i = 0; i < econCount; ++i) {
                    aoc::sim::PlayerEconomyComponent comp{};
                    comp.owner = buf.readU8();
                    comp.treasury = buf.readI64();
                    comp.incomePerTurn = buf.readI64();

                    EntityId entity = world.createEntity();
                    world.addComponent<aoc::sim::PlayerEconomyComponent>(entity, std::move(comp));
                }

                // --- PlayerGreatPeopleComponent ---
                constexpr std::size_t GP_TYPE_COUNT =
                    static_cast<std::size_t>(aoc::sim::GreatPersonType::Count);
                uint32_t gpCount = buf.readU32();
                for (uint32_t i = 0; i < gpCount; ++i) {
                    aoc::sim::PlayerGreatPeopleComponent comp{};
                    comp.owner = buf.readU8();
                    for (std::size_t t = 0; t < GP_TYPE_COUNT; ++t) {
                        comp.points[t] = buf.readF32();
                    }
                    for (std::size_t t = 0; t < GP_TYPE_COUNT; ++t) {
                        comp.recruited[t] = buf.readI32();
                    }

                    EntityId entity = world.createEntity();
                    world.addComponent<aoc::sim::PlayerGreatPeopleComponent>(entity, std::move(comp));
                }

                // --- PlayerEurekaComponent ---
                uint32_t eurekaCount = buf.readU32();
                for (uint32_t i = 0; i < eurekaCount; ++i) {
                    aoc::sim::PlayerEurekaComponent comp{};
                    comp.owner = buf.readU8();
                    uint16_t bitCount = buf.readU16();
                    uint16_t byteCount = static_cast<uint16_t>((bitCount + 7) / 8);
                    for (uint16_t b = 0; b < byteCount; ++b) {
                        uint8_t byte = buf.readU8();
                        for (uint8_t bit = 0; bit < 8; ++bit) {
                            uint16_t idx = static_cast<uint16_t>(b * 8 + bit);
                            if (idx < bitCount && idx < aoc::sim::MAX_EUREKA_BOOSTS &&
                                ((byte >> bit) & 1u) != 0) {
                                comp.triggeredBoosts.set(idx);
                            }
                        }
                    }

                    EntityId entity = world.createEntity();
                    world.addComponent<aoc::sim::PlayerEurekaComponent>(entity, std::move(comp));
                }

                // --- PlayerWarComponent ---
                uint32_t warCompCount = buf.readU32();
                for (uint32_t i = 0; i < warCompCount; ++i) {
                    aoc::sim::PlayerWarComponent comp{};
                    comp.owner = buf.readU8();
                    uint32_t warCount = buf.readU32();
                    comp.activeWars.reserve(warCount);
                    for (uint32_t w = 0; w < warCount; ++w) {
                        aoc::sim::ActiveWar war{};
                        war.aggressor = buf.readU8();
                        war.defender = buf.readU8();
                        war.casusBelli = static_cast<aoc::sim::CasusBelli>(buf.readU8());
                        war.startTurn = buf.readU32();
                        war.aggressorWarScore = buf.readI32();
                        war.defenderWarScore = buf.readI32();
                        comp.activeWars.push_back(std::move(war));
                    }

                    EntityId entity = world.createEntity();
                    world.addComponent<aoc::sim::PlayerWarComponent>(entity, std::move(comp));
                }
                break;
            }
            case SectionId::Diplomacy: {
                uint8_t playerCount = buf.readU8();
                diplomacy.initialize(playerCount);

                for (uint8_t a = 0; a < playerCount; ++a) {
                    for (uint8_t b = a + 1; b < playerCount; ++b) {
                        aoc::sim::PairwiseRelation& rel = diplomacy.relation(a, b);
                        rel.baseScore = buf.readI32();
                        rel.isAtWar = buf.readU8() != 0;
                        rel.hasOpenBorders = buf.readU8() != 0;
                        rel.hasDefensiveAlliance = buf.readU8() != 0;
                        uint32_t modCount = buf.readU32();
                        rel.modifiers.reserve(modCount);
                        for (uint32_t m = 0; m < modCount; ++m) {
                            aoc::sim::RelationModifier mod{};
                            mod.reason = buf.readString();
                            mod.amount = buf.readI32();
                            mod.turnsRemaining = buf.readI32();
                            rel.modifiers.push_back(std::move(mod));
                        }
                    }
                }
                break;
            }
            case SectionId::Market: {
                economy.initialize();
                uint16_t count = buf.readU16();
                aoc::sim::Market& market = economy.market();
                for (uint16_t i = 0; i < count; ++i) {
                    int32_t currentPrice = buf.readI32();
                    [[maybe_unused]] int32_t basePrice = buf.readI32();
                    market.setPrice(i, currentPrice);
                }
                break;
            }
            case SectionId::FogOfWar: {
                // Fog of war is recomputed from unit/city positions after load.
                // Skip any stored data for forward compatibility.
                buf.skip(sectionSize);
                break;
            }
            case SectionId::WonderState: {
                // GlobalWonderTracker
                uint8_t hasTracker = buf.readU8();
                if (hasTracker != 0) {
                    aoc::sim::GlobalWonderTracker tracker{};
                    for (uint8_t w = 0; w < aoc::sim::WONDER_COUNT; ++w) {
                        tracker.builtBy[w] = buf.readU8();
                    }
                    EntityId entity = world.createEntity();
                    world.addComponent<aoc::sim::GlobalWonderTracker>(entity, std::move(tracker));
                }

                // CityWondersComponent
                uint32_t cityWonderCount = buf.readU32();
                for (uint32_t i = 0; i < cityWonderCount; ++i) {
                    uint32_t cityIndex = buf.readU32();
                    uint32_t wonderCount = buf.readU32();

                    aoc::sim::CityWondersComponent wonders{};
                    wonders.wonders.reserve(wonderCount);
                    for (uint32_t w = 0; w < wonderCount; ++w) {
                        wonders.wonders.push_back(buf.readU8());
                    }

                    if (cityIndex < static_cast<uint32_t>(loadedCityEntities.size())) {
                        EntityId cityEntity = loadedCityEntities[cityIndex];
                        world.addComponent<aoc::sim::CityWondersComponent>(
                            cityEntity, std::move(wonders));
                    }
                }
                break;
            }
            case SectionId::MiscEntities: {
                // --- BarbarianEncampmentComponent ---
                uint32_t barbCount = buf.readU32();
                for (uint32_t i = 0; i < barbCount; ++i) {
                    aoc::sim::BarbarianEncampmentComponent comp{};
                    comp.location.q = buf.readI32();
                    comp.location.r = buf.readI32();
                    comp.spawnCooldown = buf.readI32();
                    comp.unitsSpawned = buf.readI32();

                    EntityId entity = world.createEntity();
                    world.addComponent<aoc::sim::BarbarianEncampmentComponent>(entity, std::move(comp));
                }

                // --- GreatPersonComponent ---
                uint32_t gpPersonCount = buf.readU32();
                for (uint32_t i = 0; i < gpPersonCount; ++i) {
                    aoc::sim::GreatPersonComponent comp{};
                    comp.owner = buf.readU8();
                    comp.defId = buf.readU8();
                    comp.position.q = buf.readI32();
                    comp.position.r = buf.readI32();
                    comp.isActivated = buf.readU8() != 0;

                    EntityId entity = world.createEntity();
                    world.addComponent<aoc::sim::GreatPersonComponent>(entity, std::move(comp));
                }

                // --- SpyComponent ---
                uint32_t spyCount = buf.readU32();
                for (uint32_t i = 0; i < spyCount; ++i) {
                    aoc::sim::SpyComponent comp{};
                    comp.owner = buf.readU8();
                    comp.location.q = buf.readI32();
                    comp.location.r = buf.readI32();
                    comp.currentMission = static_cast<aoc::sim::SpyMission>(buf.readU8());
                    comp.turnsRemaining = buf.readI32();
                    comp.experience = buf.readI32();
                    comp.isRevealed = buf.readU8() != 0;

                    EntityId entity = world.createEntity();
                    world.addComponent<aoc::sim::SpyComponent>(entity, std::move(comp));
                }

                // --- UnitExperienceComponent ---
                uint32_t expCount = buf.readU32();
                for (uint32_t i = 0; i < expCount; ++i) {
                    uint32_t unitIndex = buf.readU32();
                    aoc::sim::UnitExperienceComponent exp{};
                    exp.experience = buf.readI32();
                    exp.level = buf.readI32();
                    uint32_t promoCount = buf.readU32();
                    exp.promotions.reserve(promoCount);
                    for (uint32_t p = 0; p < promoCount; ++p) {
                        exp.promotions.push_back(PromotionId{buf.readU16()});
                    }

                    if (unitIndex < static_cast<uint32_t>(loadedUnitEntities.size())) {
                        EntityId unitEntity = loadedUnitEntities[unitIndex];
                        world.addComponent<aoc::sim::UnitExperienceComponent>(
                            unitEntity, std::move(exp));
                    }
                }
                break;
            }
            case SectionId::CurrencyTrust: {
                uint32_t count = buf.readU32();
                for (uint32_t i = 0; i < count; ++i) {
                    aoc::sim::CurrencyTrustComponent ct{};
                    ct.owner = buf.readU8();
                    ct.trustScore = buf.readF32();
                    ct.turnsOnFiat = buf.readI32();
                    ct.turnsStable = buf.readI32();
                    ct.isReserveCurrency = buf.readU8() != 0;
                    ct.turnsAsReserve = buf.readI32();
                    for (int32_t p = 0; p < aoc::sim::CurrencyTrustComponent::MAX_PLAYERS; ++p) {
                        ct.bilateralTrust[p] = buf.readF32();
                    }

                    EntityId entity = world.createEntity();
                    world.addComponent<aoc::sim::CurrencyTrustComponent>(entity, std::move(ct));
                }
                break;
            }
            case SectionId::CrisisState: {
                uint32_t count = buf.readU32();
                for (uint32_t i = 0; i < count; ++i) {
                    aoc::sim::CurrencyCrisisComponent c{};
                    c.owner = buf.readU8();
                    c.activeCrisis = static_cast<aoc::sim::CrisisType>(buf.readU8());
                    c.turnsRemaining = buf.readI32();
                    c.turnsHighInflation = buf.readI32();
                    c.hasDefaulted = buf.readU8() != 0;
                    c.defaultCooldown = buf.readI32();

                    EntityId entity = world.createEntity();
                    world.addComponent<aoc::sim::CurrencyCrisisComponent>(entity, std::move(c));
                }
                break;
            }
            case SectionId::BondState: {
                uint32_t count = buf.readU32();
                for (uint32_t i = 0; i < count; ++i) {
                    aoc::sim::PlayerBondComponent pb{};
                    pb.owner = buf.readU8();
                    uint32_t issuedCount = buf.readU32();
                    pb.issuedBonds.reserve(issuedCount);
                    for (uint32_t j = 0; j < issuedCount; ++j) {
                        aoc::sim::BondIssue b{};
                        b.issuer = buf.readU8();
                        b.holder = buf.readU8();
                        b.principal = buf.readI64();
                        b.yieldRate = buf.readF32();
                        b.turnsToMaturity = buf.readI32();
                        b.accruedInterest = buf.readI64();
                        pb.issuedBonds.push_back(b);
                    }
                    uint32_t heldCount = buf.readU32();
                    pb.heldBonds.reserve(heldCount);
                    for (uint32_t j = 0; j < heldCount; ++j) {
                        aoc::sim::BondIssue b{};
                        b.issuer = buf.readU8();
                        b.holder = buf.readU8();
                        b.principal = buf.readI64();
                        b.yieldRate = buf.readF32();
                        b.turnsToMaturity = buf.readI32();
                        b.accruedInterest = buf.readI64();
                        pb.heldBonds.push_back(b);
                    }

                    EntityId entity = world.createEntity();
                    world.addComponent<aoc::sim::PlayerBondComponent>(entity, std::move(pb));
                }
                break;
            }
            case SectionId::DevaluationState: {
                uint32_t count = buf.readU32();
                for (uint32_t i = 0; i < count; ++i) {
                    aoc::sim::CurrencyDevaluationComponent d{};
                    d.owner = buf.readU8();
                    d.isDevalued = buf.readU8() != 0;
                    d.devaluationTurnsLeft = buf.readI32();
                    d.exportBonus = buf.readF32();
                    d.importPenalty = buf.readF32();
                    d.devaluationCount = buf.readI32();

                    EntityId entity = world.createEntity();
                    world.addComponent<aoc::sim::CurrencyDevaluationComponent>(entity, std::move(d));
                }
                break;
            }
            case SectionId::HoardState: {
                uint32_t count = buf.readU32();
                for (uint32_t i = 0; i < count; ++i) {
                    aoc::sim::CommodityHoardComponent h{};
                    h.owner = buf.readU8();
                    uint32_t posCount = buf.readU32();
                    h.positions.reserve(posCount);
                    for (uint32_t j = 0; j < posCount; ++j) {
                        aoc::sim::CommodityHoardComponent::HoardPosition pos{};
                        pos.goodId = buf.readU16();
                        pos.amount = buf.readI32();
                        pos.purchasePrice = buf.readI32();
                        h.positions.push_back(pos);
                    }

                    EntityId entity = world.createEntity();
                    world.addComponent<aoc::sim::CommodityHoardComponent>(entity, std::move(h));
                }
                break;
            }
            case SectionId::ProductionExp: {
                uint32_t count = buf.readU32();
                for (uint32_t i = 0; i < count; ++i) {
                    uint32_t entityIndex = buf.readU32();
                    aoc::sim::CityProductionExperienceComponent comp{};
                    uint32_t mapSize = buf.readU32();
                    for (uint32_t j = 0; j < mapSize; ++j) {
                        uint16_t recipeId = buf.readU16();
                        int32_t exp = buf.readI32();
                        comp.recipeExperience[recipeId] = exp;
                    }
                    // Attach to existing city entity if possible
                    EntityId target{entityIndex & 0xFFFFFu, 0};
                    if (world.isAlive(target)) {
                        world.addComponent<aoc::sim::CityProductionExperienceComponent>(
                            target, std::move(comp));
                    }
                }
                break;
            }
            case SectionId::BuildingLevels: {
                uint32_t count = buf.readU32();
                for (uint32_t i = 0; i < count; ++i) {
                    uint32_t entityIndex = buf.readU32();
                    aoc::sim::CityBuildingLevelsComponent comp{};
                    uint32_t mapSize = buf.readU32();
                    for (uint32_t j = 0; j < mapSize; ++j) {
                        uint16_t bid = buf.readU16();
                        int32_t lvl = buf.readI32();
                        comp.levels[bid] = lvl;
                    }
                    EntityId target{entityIndex & 0xFFFFFu, 0};
                    if (world.isAlive(target)) {
                        world.addComponent<aoc::sim::CityBuildingLevelsComponent>(
                            target, std::move(comp));
                    }
                }
                break;
            }
            case SectionId::PollutionState: {
                uint32_t count = buf.readU32();
                for (uint32_t i = 0; i < count; ++i) {
                    uint32_t entityIndex = buf.readU32();
                    aoc::sim::CityPollutionComponent comp{};
                    comp.wasteAccumulated = buf.readI32();
                    comp.co2ContributionPerTurn = buf.readI32();
                    EntityId target{entityIndex & 0xFFFFFu, 0};
                    if (world.isAlive(target)) {
                        world.addComponent<aoc::sim::CityPollutionComponent>(
                            target, std::move(comp));
                    }
                }
                break;
            }
            case SectionId::AutomationState: {
                uint32_t count = buf.readU32();
                for (uint32_t i = 0; i < count; ++i) {
                    uint32_t entityIndex = buf.readU32();
                    aoc::sim::CityAutomationComponent comp{};
                    comp.robotWorkers = buf.readI32();
                    comp.turnsSinceLastMaintenance = buf.readI32();
                    EntityId target{entityIndex & 0xFFFFFu, 0};
                    if (world.isAlive(target)) {
                        world.addComponent<aoc::sim::CityAutomationComponent>(
                            target, std::move(comp));
                    }
                }
                break;
            }
            case SectionId::IndustrialState: {
                uint32_t count = buf.readU32();
                for (uint32_t i = 0; i < count; ++i) {
                    aoc::sim::PlayerIndustrialComponent ind{};
                    ind.owner = buf.readU8();
                    ind.currentRevolution = static_cast<aoc::sim::IndustrialRevolutionId>(buf.readU8());
                    for (int32_t r = 0; r < 6; ++r) {
                        ind.turnAchieved[r] = buf.readI32();
                    }

                    EntityId entity = world.createEntity();
                    world.addComponent<aoc::sim::PlayerIndustrialComponent>(entity, std::move(ind));
                }
                break;
            }
            default:
                // Unknown section: skip
                buf.skip(sectionSize);
                break;
        }
    }

    LOG_INFO("Game loaded from %s", filepath.c_str());
    return ErrorCode::Ok;
}

} // namespace aoc::save
