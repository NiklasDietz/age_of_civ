/**
 * @file Serializer.cpp
 * @brief Binary save/load implementation using the GameState object model.
 *
 * All serialization iterates GameState->Player->City/Unit rather than ECS pools.
 * Player-level components are accessed via Player member accessors.
 * City-level components are accessed via City member accessors.
 */

#include "aoc/save/Serializer.hpp"
#include "aoc/core/Log.hpp"
#include "aoc/game/GameState.hpp"
#include "aoc/game/Player.hpp"
#include "aoc/game/City.hpp"
#include "aoc/game/Unit.hpp"
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
#include "aoc/simulation/victory/Prestige.hpp"
#include "aoc/simulation/victory/SpaceRace.hpp"
#include "aoc/simulation/culture/Tourism.hpp"
#include "aoc/simulation/diplomacy/Grievance.hpp"
#include "aoc/simulation/diplomacy/WarWeariness.hpp"
#include "aoc/simulation/economy/StockMarket.hpp"

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
    section.writeU8(static_cast<uint8_t>(grid.topology()));

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

/**
 * @brief Serialize all units and cities across all players into the Entities section.
 *
 * Units and cities are written in player order, then within-player list order.
 * This pool ordering is used by later sections (production queues, districts, etc.)
 * which reference cities by their index in write order.
 */
void writeEntitySection(WriteBuffer& out, const aoc::game::GameState& gameState) {
    WriteBuffer section;

    // --- Units (all players, in player order) ---
    uint32_t unitCount = 0;
    for (const std::unique_ptr<aoc::game::Player>& player : gameState.players()) {
        unitCount += static_cast<uint32_t>(player->units().size());
    }
    section.writeU32(unitCount);

    for (const std::unique_ptr<aoc::game::Player>& player : gameState.players()) {
        for (const std::unique_ptr<aoc::game::Unit>& unit : player->units()) {
            section.writeU8(unit->owner());
            section.writeU16(unit->typeId().value);
            section.writeI32(unit->position().q);
            section.writeI32(unit->position().r);
            section.writeI32(unit->hitPoints());
            section.writeI32(unit->movementRemaining());
            section.writeU8(static_cast<uint8_t>(unit->state()));
            // v4: chargesRemaining, cargoCapacity (always 0 in object model), pendingPath
            section.writeU8(static_cast<uint8_t>(unit->chargesRemaining()));
            section.writeU8(static_cast<uint8_t>(0));  // cargoCapacity: not stored in Unit object
            section.writeU16(static_cast<uint16_t>(unit->pendingPath().size()));
            for (const aoc::hex::AxialCoord& coord : unit->pendingPath()) {
                section.writeI32(coord.q);
                section.writeI32(coord.r);
            }
        }
    }

    // --- Cities (all players, in player order) ---
    uint32_t cityCount = 0;
    for (const std::unique_ptr<aoc::game::Player>& player : gameState.players()) {
        cityCount += static_cast<uint32_t>(player->cities().size());
    }
    section.writeU32(cityCount);

    for (const std::unique_ptr<aoc::game::Player>& player : gameState.players()) {
        for (const std::unique_ptr<aoc::game::City>& city : player->cities()) {
            section.writeU8(city->owner());
            section.writeI32(city->location().q);
            section.writeI32(city->location().r);
            section.writeString(city->name());
            section.writeI32(city->population());
            section.writeF32(city->foodSurplus());
            section.writeF32(city->productionProgress());

            section.writeU32(static_cast<uint32_t>(city->workedTiles().size()));
            for (const aoc::hex::AxialCoord& tile : city->workedTiles()) {
                section.writeI32(tile.q);
                section.writeI32(tile.r);
            }

            // v4: cultureBorderProgress, tilesClaimedCount, isOriginalCapital, originalOwner
            section.writeF32(city->cultureBorderProgress());
            section.writeI32(city->tilesClaimedCount());
            section.writeU8(city->isOriginalCapital() ? uint8_t{1} : uint8_t{0});
            section.writeU8(city->originalOwner());
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

/**
 * @brief Serialize tech and civic progress for all players.
 *
 * Uses Player::tech() and Player::civics() accessors which contain the
 * PlayerTechComponent and PlayerCivicComponent directly.
 */
void writeTechProgressSection(WriteBuffer& out, const aoc::game::GameState& gameState) {
    WriteBuffer section;

    // Tech components (one per player)
    uint32_t playerCount = static_cast<uint32_t>(gameState.players().size());
    section.writeU32(playerCount);
    for (const std::unique_ptr<aoc::game::Player>& player : gameState.players()) {
        const aoc::sim::PlayerTechComponent& tech = player->tech();
        section.writeU8(static_cast<uint8_t>(player->id()));
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

    // Civic components (one per player)
    section.writeU32(playerCount);
    for (const std::unique_ptr<aoc::game::Player>& player : gameState.players()) {
        const aoc::sim::PlayerCivicComponent& civic = player->civics();
        section.writeU8(static_cast<uint8_t>(player->id()));
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

    writeSection(out, SectionId::TechProgress, section);
}

/**
 * @brief Serialize production queues for all cities, keyed by global city index.
 *
 * City index is the sequential order of cities when iterating players in order,
 * matching the order written by writeEntitySection.
 */
void writeProductionQueuesSection(WriteBuffer& out, const aoc::game::GameState& gameState) {
    WriteBuffer section;

    // Count non-empty queues first
    uint32_t count = 0;
    for (const std::unique_ptr<aoc::game::Player>& player : gameState.players()) {
        for (const std::unique_ptr<aoc::game::City>& city : player->cities()) {
            if (!city->production().isEmpty()) {
                ++count;
            }
        }
    }

    section.writeU32(count);

    uint32_t cityIndex = 0;
    for (const std::unique_ptr<aoc::game::Player>& player : gameState.players()) {
        for (const std::unique_ptr<aoc::game::City>& city : player->cities()) {
            const aoc::sim::ProductionQueueComponent& queue = city->production();
            if (!queue.isEmpty()) {
                section.writeU32(cityIndex);
                section.writeU32(static_cast<uint32_t>(queue.queue.size()));
                for (const aoc::sim::ProductionQueueItem& item : queue.queue) {
                    section.writeU8(static_cast<uint8_t>(item.type));
                    section.writeU16(item.itemId);
                    section.writeString(item.name);
                    section.writeF32(item.totalCost);
                    section.writeF32(item.progress);
                }
            }
            ++cityIndex;
        }
    }

    writeSection(out, SectionId::ProductionQueues, section);
}

/// Serialize city districts and buildings for all cities.
void writeDistrictsSection(WriteBuffer& out, const aoc::game::GameState& gameState) {
    WriteBuffer section;

    uint32_t count = 0;
    for (const std::unique_ptr<aoc::game::Player>& player : gameState.players()) {
        for (const std::unique_ptr<aoc::game::City>& city : player->cities()) {
            if (!city->districts().districts.empty()) {
                ++count;
            }
        }
    }

    section.writeU32(count);

    uint32_t cityIndex = 0;
    for (const std::unique_ptr<aoc::game::Player>& player : gameState.players()) {
        for (const std::unique_ptr<aoc::game::City>& city : player->cities()) {
            const aoc::sim::CityDistrictsComponent& districts = city->districts();
            if (!districts.districts.empty()) {
                section.writeU32(cityIndex);
                section.writeU32(static_cast<uint32_t>(districts.districts.size()));
                for (const aoc::sim::CityDistrictsComponent::PlacedDistrict& dist : districts.districts) {
                    section.writeU8(static_cast<uint8_t>(dist.type));
                    section.writeI32(dist.location.q);
                    section.writeI32(dist.location.r);
                    section.writeU32(static_cast<uint32_t>(dist.buildings.size()));
                    for (BuildingId bid : dist.buildings) {
                        section.writeU16(bid.value);
                    }
                }
            }
            ++cityIndex;
        }
    }

    writeSection(out, SectionId::Districts, section);
}

/// Serialize per-player monetary state.
void writeMonetarySection(WriteBuffer& out, const aoc::game::GameState& gameState) {
    WriteBuffer section;

    uint32_t playerCount = static_cast<uint32_t>(gameState.players().size());
    section.writeU32(playerCount);
    for (const std::unique_ptr<aoc::game::Player>& player : gameState.players()) {
        const aoc::sim::MonetaryStateComponent& m = player->monetary();
        section.writeU8(static_cast<uint8_t>(player->id()));
        section.writeU8(static_cast<uint8_t>(m.system));
        section.writeI64(m.moneySupply);
        section.writeI64(m.treasury);
        section.writeI32(m.copperCoinReserves);
        section.writeI32(m.silverCoinReserves);
        section.writeI32(m.goldBarReserves);
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

    writeSection(out, SectionId::MonetaryState, section);
}

/// Serialize per-player government and policy state.
void writeGovernmentSection(WriteBuffer& out, const aoc::game::GameState& gameState) {
    WriteBuffer section;

    uint32_t playerCount = static_cast<uint32_t>(gameState.players().size());
    section.writeU32(playerCount);
    for (const std::unique_ptr<aoc::game::Player>& player : gameState.players()) {
        const aoc::sim::PlayerGovernmentComponent& gov = player->government();
        section.writeU8(static_cast<uint8_t>(player->id()));
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

    writeSection(out, SectionId::GovernmentState, section);
}

/// Serialize per-player victory tracker data.
void writeVictorySection(WriteBuffer& out, const aoc::game::GameState& gameState) {
    WriteBuffer section;

    uint32_t playerCount = static_cast<uint32_t>(gameState.players().size());
    section.writeU32(playerCount);
    for (const std::unique_ptr<aoc::game::Player>& player : gameState.players()) {
        const aoc::sim::VictoryTrackerComponent& v = player->victoryTracker();
        section.writeU8(static_cast<uint8_t>(player->id()));
        section.writeI32(v.scienceProgress);
        section.writeF32(v.totalCultureAccumulated);
        section.writeI32(v.score);
        for (int32_t c = 0; c < aoc::sim::CSI_CATEGORY_COUNT; ++c) {
            section.writeF32(v.categoryScores[c]);
        }
        section.writeF32(v.tradeNetworkMultiplier);
        section.writeF32(v.financialIntegrationMult);
        section.writeF32(v.diplomaticWebMult);
        section.writeF32(v.compositeCSI);
        section.writeI32(v.eraVictoryPoints);
        section.writeI32(v.erasEvaluated);
        // Legacy integration-project fields removed; write zeros for back-compat.
        section.writeI32(0);
        section.writeU8(0);
        section.writeU8(static_cast<uint8_t>(v.activeCollapse));
        section.writeI32(v.peakGDP);
        section.writeI32(v.turnsGDPBelowHalf);
        section.writeI32(v.turnsLowLoyalty);
        section.writeU8(v.isEliminated ? 1 : 0);
    }

    writeSection(out, SectionId::VictoryState, section);
}

/// Serialize per-city resource stockpiles.
void writeStockpilesSection(WriteBuffer& out, const aoc::game::GameState& gameState) {
    WriteBuffer section;

    uint32_t count = 0;
    for (const std::unique_ptr<aoc::game::Player>& player : gameState.players()) {
        for (const std::unique_ptr<aoc::game::City>& city : player->cities()) {
            if (!city->stockpile().goods.empty()) {
                ++count;
            }
        }
    }

    section.writeU32(count);

    uint32_t cityIndex = 0;
    for (const std::unique_ptr<aoc::game::Player>& player : gameState.players()) {
        for (const std::unique_ptr<aoc::game::City>& city : player->cities()) {
            const aoc::sim::CityStockpileComponent& stockpile = city->stockpile();
            if (!stockpile.goods.empty()) {
                section.writeU32(cityIndex);
                section.writeU32(static_cast<uint32_t>(stockpile.goods.size()));
                for (const std::pair<const uint16_t, int32_t>& entry : stockpile.goods) {
                    section.writeU16(entry.first);
                    section.writeI32(entry.second);
                }
            }
            ++cityIndex;
        }
    }

    writeSection(out, SectionId::Stockpiles, section);
}

// ============================================================================
// v4 section writers
// ============================================================================

/**
 * @brief Serialize the PlayerState section: civ, era, economy, great people, eureka, wars.
 */
void writePlayerStateSection(WriteBuffer& out, const aoc::game::GameState& gameState) {
    WriteBuffer section;
    uint32_t playerCount = static_cast<uint32_t>(gameState.players().size());

    // --- PlayerCivilizationComponent ---
    section.writeU32(playerCount);
    for (const std::unique_ptr<aoc::game::Player>& player : gameState.players()) {
        section.writeU8(static_cast<uint8_t>(player->id()));
        section.writeU8(static_cast<uint8_t>(player->civId()));
    }

    // --- PlayerEraComponent ---
    section.writeU32(playerCount);
    for (const std::unique_ptr<aoc::game::Player>& player : gameState.players()) {
        section.writeU8(static_cast<uint8_t>(player->id()));
        section.writeU16(player->era().currentEra.value);
    }

    // --- PlayerEconomyComponent ---
    section.writeU32(playerCount);
    for (const std::unique_ptr<aoc::game::Player>& player : gameState.players()) {
        const aoc::sim::PlayerEconomyComponent& econ = player->economy();
        section.writeU8(static_cast<uint8_t>(player->id()));
        section.writeI64(econ.treasury);
        section.writeI64(econ.incomePerTurn);
    }

    // --- PlayerGreatPeopleComponent ---
    constexpr std::size_t GP_TYPE_COUNT =
        static_cast<std::size_t>(aoc::sim::GreatPersonType::Count);
    section.writeU32(playerCount);
    for (const std::unique_ptr<aoc::game::Player>& player : gameState.players()) {
        const aoc::sim::PlayerGreatPeopleComponent& gp = player->greatPeople();
        section.writeU8(static_cast<uint8_t>(player->id()));
        for (std::size_t t = 0; t < GP_TYPE_COUNT; ++t) {
            section.writeF32(gp.points[t]);
        }
        for (std::size_t t = 0; t < GP_TYPE_COUNT; ++t) {
            section.writeI32(gp.recruited[t]);
        }
        // H3.8: exhausted flags (SAVE_VERSION 6+). Preserves "this roster is
        // done, stop accumulating" across save/load; otherwise points would
        // silently start draining again on the reloaded save.
        for (std::size_t t = 0; t < GP_TYPE_COUNT; ++t) {
            section.writeU8(gp.exhausted[t] ? 1 : 0);
        }
    }

    // --- PlayerEurekaComponent ---
    // v8+: pending-boost bitfield follows the triggered bitfield per player.
    section.writeU32(playerCount);
    for (const std::unique_ptr<aoc::game::Player>& player : gameState.players()) {
        const aoc::sim::PlayerEurekaComponent& eureka = player->eureka();
        section.writeU8(static_cast<uint8_t>(player->id()));
        constexpr uint16_t bitCount = aoc::sim::MAX_EUREKA_BOOSTS;
        constexpr uint16_t byteCount = (bitCount + 7) / 8;
        section.writeU16(bitCount);
        for (uint16_t b = 0; b < byteCount; ++b) {
            uint8_t byte = 0;
            for (uint8_t bit = 0; bit < 8; ++bit) {
                uint16_t idx = static_cast<uint16_t>(b * 8 + bit);
                if (idx < bitCount && eureka.triggeredBoosts.test(idx)) {
                    byte |= static_cast<uint8_t>(1u << bit);
                }
            }
            section.writeU8(byte);
        }
        for (uint16_t b = 0; b < byteCount; ++b) {
            uint8_t byte = 0;
            for (uint8_t bit = 0; bit < 8; ++bit) {
                uint16_t idx = static_cast<uint16_t>(b * 8 + bit);
                if (idx < bitCount && eureka.pendingBoosts.test(idx)) {
                    byte |= static_cast<uint8_t>(1u << bit);
                }
            }
            section.writeU8(byte);
        }
    }

    // --- PlayerWarComponent: not yet in Player object model, write zero count ---
    section.writeU32(0);

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
            // v5: remaining 5 alliance bools packed + per-type level state + cooldowns
            const uint8_t allianceBits = static_cast<uint8_t>(
                  (rel.hasMilitaryAlliance  ? 0x01 : 0)
                | (rel.hasResearchAgreement ? 0x02 : 0)
                | (rel.hasEconomicAlliance  ? 0x04 : 0)
                | (rel.hasCulturalAlliance  ? 0x08 : 0)
                | (rel.hasReligiousAlliance ? 0x10 : 0));
            section.writeU8(allianceBits);
            for (const aoc::sim::AllianceState& st : rel.alliances) {
                section.writeU8(static_cast<uint8_t>(st.type));
                section.writeU8(static_cast<uint8_t>(st.level));
                section.writeI32(st.turnsActive);
            }
            section.writeI32(rel.lastAllianceFormTurn);
            section.writeI32(rel.allianceBreakWarningTurns);
            section.writeU8(static_cast<uint8_t>(rel.lastCasusBelli));
            section.writeU32(static_cast<uint32_t>(rel.modifiers.size()));
            for (const aoc::sim::RelationModifier& mod : rel.modifiers) {
                section.writeString(mod.reason);
                section.writeI32(mod.amount);
                section.writeI32(mod.turnsRemaining);
            }
            // Reputation modifiers (political reputation system)
            section.writeU32(static_cast<uint32_t>(rel.reputationModifiers.size()));
            for (const aoc::sim::ReputationModifier& mod : rel.reputationModifiers) {
                section.writeI32(mod.amount);
                section.writeI32(mod.turnsRemaining);
            }
            // Border violation state
            section.writeI32(rel.unitsInTerritory);
            section.writeI32(rel.turnsWithViolation);
            // Single byte encodes land CB (bit 0) and naval CB (bit 1).
            // Legacy saves used only bit 0; bit 1 defaults to 0 on load.
            const uint8_t cbBits = static_cast<uint8_t>(
                (rel.casusBelliLand ? 0x1 : 0x0) | (rel.casusBelliNaval ? 0x2 : 0x0));
            section.writeU8(cbBits);
            section.writeU8(rel.warningIssued ? uint8_t{1} : uint8_t{0});
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

/// Serialize wonders: global tracker from GameState, per-city from City::wonders().
void writeWonderSection(WriteBuffer& out, const aoc::game::GameState& gameState) {
    WriteBuffer section;

    // GlobalWonderTracker
    const aoc::sim::GlobalWonderTracker& tracker = gameState.wonderTracker();
    section.writeU8(uint8_t{1});  // always present in object model
    for (uint8_t w = 0; w < aoc::sim::WONDER_COUNT; ++w) {
        section.writeU8(tracker.builtBy[w]);
    }

    // Per-city wonders
    uint32_t cityWonderCount = 0;
    for (const std::unique_ptr<aoc::game::Player>& player : gameState.players()) {
        for (const std::unique_ptr<aoc::game::City>& city : player->cities()) {
            if (!city->wonders().wonders.empty()) {
                ++cityWonderCount;
            }
        }
    }

    section.writeU32(cityWonderCount);
    uint32_t cityIndex = 0;
    for (const std::unique_ptr<aoc::game::Player>& player : gameState.players()) {
        for (const std::unique_ptr<aoc::game::City>& city : player->cities()) {
            const aoc::sim::CityWondersComponent& wonders = city->wonders();
            if (!wonders.wonders.empty()) {
                section.writeU32(cityIndex);
                section.writeU32(static_cast<uint32_t>(wonders.wonders.size()));
                for (aoc::sim::WonderId wid : wonders.wonders) {
                    section.writeU8(wid);
                }
            }
            ++cityIndex;
        }
    }

    writeSection(out, SectionId::WonderState, section);
}

/**
 * @brief Serialize misc entities: barbarian encampments, great persons, spies.
 *
 * Unit experience is intentionally omitted here since UnitExperienceComponent
 * is not yet part of the Unit object model (write zero count for forward compat).
 * Barbarians, great persons, and spies from GameState global collections.
 */
void writeMiscEntitiesSection(WriteBuffer& out, const aoc::game::GameState& gameState) {
    WriteBuffer section;

    // --- BarbarianEncampmentComponent (not yet in GameState, write 0) ---
    section.writeU32(0);

    // --- GreatPersonComponent (not yet in GameState global list, write 0) ---
    section.writeU32(0);

    // --- SpyComponent: spies are stored on Unit objects, iterate all players ---
    uint32_t spyCount = 0;
    for (const std::unique_ptr<aoc::game::Player>& player : gameState.players()) {
        for (const std::unique_ptr<aoc::game::Unit>& unit : player->units()) {
            if (unit->spy().turnsRemaining > 0) {
                ++spyCount;
            }
        }
    }
    section.writeU32(spyCount);
    for (const std::unique_ptr<aoc::game::Player>& player : gameState.players()) {
        for (const std::unique_ptr<aoc::game::Unit>& unit : player->units()) {
            const aoc::sim::SpyComponent& spy = unit->spy();
            if (spy.turnsRemaining > 0) {
                section.writeU8(spy.owner);
                section.writeI32(spy.location.q);
                section.writeI32(spy.location.r);
                section.writeU8(static_cast<uint8_t>(spy.currentMission));
                section.writeI32(spy.turnsRemaining);
                section.writeI32(spy.experience);
                section.writeU8(spy.isRevealed ? uint8_t{1} : uint8_t{0});
            }
        }
    }

    // --- UnitExperienceComponent: not yet in Unit object model, write 0 ---
    section.writeU32(0);

    writeSection(out, SectionId::MiscEntities, section);
}

/// Serialize per-player currency trust state.
void writeCurrencyTrustSection(WriteBuffer& out, const aoc::game::GameState& gameState) {
    WriteBuffer section;

    uint32_t playerCount = static_cast<uint32_t>(gameState.players().size());
    section.writeU32(playerCount);
    for (const std::unique_ptr<aoc::game::Player>& player : gameState.players()) {
        const aoc::sim::CurrencyTrustComponent& ct = player->currencyTrust();
        section.writeU8(static_cast<uint8_t>(player->id()));
        section.writeF32(ct.trustScore);
        section.writeI32(ct.turnsOnFiat);
        section.writeI32(ct.turnsStable);
        section.writeU8(ct.isReserveCurrency ? 1 : 0);
        section.writeI32(ct.turnsAsReserve);
        for (int32_t p = 0; p < aoc::sim::CurrencyTrustComponent::MAX_PLAYERS; ++p) {
            section.writeF32(ct.bilateralTrust[p]);
        }
    }

    writeSection(out, SectionId::CurrencyTrust, section);
}

/// Serialize per-player currency crisis state.
void writeCrisisSection(WriteBuffer& out, const aoc::game::GameState& gameState) {
    WriteBuffer section;

    uint32_t playerCount = static_cast<uint32_t>(gameState.players().size());
    section.writeU32(playerCount);
    for (const std::unique_ptr<aoc::game::Player>& player : gameState.players()) {
        const aoc::sim::CurrencyCrisisComponent& c = player->currencyCrisis();
        section.writeU8(static_cast<uint8_t>(player->id()));
        section.writeU8(static_cast<uint8_t>(c.activeCrisis));
        section.writeI32(c.turnsRemaining);
        section.writeI32(c.turnsHighInflation);
        section.writeU8(c.hasDefaulted ? 1 : 0);
        section.writeI32(c.defaultCooldown);
        // G4 post-reform penalties (introduced in SAVE_VERSION 6).
        section.writeI32(c.reformLockoutTurns);
        section.writeI32(c.reformTrustCapTurns);
    }

    writeSection(out, SectionId::CrisisState, section);
}

/// Serialize per-player bond portfolios.
void writeBondSection(WriteBuffer& out, const aoc::game::GameState& gameState) {
    WriteBuffer section;

    uint32_t playerCount = static_cast<uint32_t>(gameState.players().size());
    section.writeU32(playerCount);
    for (const std::unique_ptr<aoc::game::Player>& player : gameState.players()) {
        const aoc::sim::PlayerBondComponent& pb = player->bonds();
        section.writeU8(static_cast<uint8_t>(player->id()));
        section.writeU32(static_cast<uint32_t>(pb.issuedBonds.size()));
        for (const aoc::sim::BondIssue& b : pb.issuedBonds) {
            section.writeU64(b.id);
            section.writeU8(b.issuer);
            section.writeU8(b.holder);
            section.writeI64(b.principal);
            section.writeF32(b.yieldRate);
            section.writeI32(b.turnsToMaturity);
            section.writeI64(b.accruedInterest);
        }
        section.writeU32(static_cast<uint32_t>(pb.heldBonds.size()));
        for (const aoc::sim::BondIssue& b : pb.heldBonds) {
            section.writeU64(b.id);
            section.writeU8(b.issuer);
            section.writeU8(b.holder);
            section.writeI64(b.principal);
            section.writeF32(b.yieldRate);
            section.writeI32(b.turnsToMaturity);
            section.writeI64(b.accruedInterest);
        }
    }
    // Persist bond id counter so ids stay unique across load cycles.
    section.writeU64(aoc::sim::peekNextBondId());

    writeSection(out, SectionId::BondState, section);
}

/// Serialize per-player currency devaluation state.
void writeDevaluationSection(WriteBuffer& out, const aoc::game::GameState& gameState) {
    WriteBuffer section;

    uint32_t playerCount = static_cast<uint32_t>(gameState.players().size());
    section.writeU32(playerCount);
    for (const std::unique_ptr<aoc::game::Player>& player : gameState.players()) {
        const aoc::sim::CurrencyDevaluationComponent& d = player->currencyDevaluation();
        section.writeU8(static_cast<uint8_t>(player->id()));
        section.writeU8(d.isDevalued ? 1 : 0);
        section.writeI32(d.devaluationTurnsLeft);
        section.writeF32(d.exportBonus);
        section.writeF32(d.importPenalty);
        section.writeI32(d.devaluationCount);
    }

    writeSection(out, SectionId::DevaluationState, section);
}

/// Serialize commodity hoards from GameState global collection.
void writeHoardSection(WriteBuffer& out, const aoc::game::GameState& gameState) {
    WriteBuffer section;

    const std::vector<aoc::sim::CommodityHoardComponent>& hoards = gameState.commodityHoards();
    section.writeU32(static_cast<uint32_t>(hoards.size()));
    for (const aoc::sim::CommodityHoardComponent& h : hoards) {
        section.writeU8(h.owner);
        section.writeU32(static_cast<uint32_t>(h.positions.size()));
        for (const aoc::sim::CommodityHoardComponent::HoardPosition& pos : h.positions) {
            section.writeU16(pos.goodId);
            section.writeI32(pos.amount);
            section.writeI32(pos.purchasePrice);
        }
    }

    writeSection(out, SectionId::HoardState, section);
}

/// Serialize per-city production recipe experience.
void writeProductionExpSection(WriteBuffer& out, const aoc::game::GameState& gameState) {
    WriteBuffer section;

    uint32_t count = 0;
    for (const std::unique_ptr<aoc::game::Player>& player : gameState.players()) {
        for (const std::unique_ptr<aoc::game::City>& city : player->cities()) {
            if (!city->productionExperience().recipeExperience.empty()) {
                ++count;
            }
        }
    }

    section.writeU32(count);

    uint32_t cityIndex = 0;
    for (const std::unique_ptr<aoc::game::Player>& player : gameState.players()) {
        for (const std::unique_ptr<aoc::game::City>& city : player->cities()) {
            const aoc::sim::CityProductionExperienceComponent& exp = city->productionExperience();
            if (!exp.recipeExperience.empty()) {
                section.writeU32(cityIndex);
                section.writeU32(static_cast<uint32_t>(exp.recipeExperience.size()));
                for (const std::pair<const uint16_t, int32_t>& entry : exp.recipeExperience) {
                    section.writeU16(entry.first);
                    section.writeI32(entry.second);
                }
            }
            ++cityIndex;
        }
    }

    writeSection(out, SectionId::ProductionExp, section);
}

/// Serialize per-city building upgrade levels.
void writeBuildingLevelsSection(WriteBuffer& out, const aoc::game::GameState& gameState) {
    WriteBuffer section;

    uint32_t count = 0;
    for (const std::unique_ptr<aoc::game::Player>& player : gameState.players()) {
        for (const std::unique_ptr<aoc::game::City>& city : player->cities()) {
            if (!city->buildingLevels().levels.empty()) {
                ++count;
            }
        }
    }

    section.writeU32(count);

    uint32_t cityIndex = 0;
    for (const std::unique_ptr<aoc::game::Player>& player : gameState.players()) {
        for (const std::unique_ptr<aoc::game::City>& city : player->cities()) {
            const aoc::sim::CityBuildingLevelsComponent& levels = city->buildingLevels();
            if (!levels.levels.empty()) {
                section.writeU32(cityIndex);
                section.writeU32(static_cast<uint32_t>(levels.levels.size()));
                for (const std::pair<const uint16_t, int32_t>& entry : levels.levels) {
                    section.writeU16(entry.first);
                    section.writeI32(entry.second);
                }
            }
            ++cityIndex;
        }
    }

    writeSection(out, SectionId::BuildingLevels, section);
}

/// Serialize per-city pollution state.
void writePollutionSection(WriteBuffer& out, const aoc::game::GameState& gameState) {
    WriteBuffer section;

    uint32_t count = 0;
    for (const std::unique_ptr<aoc::game::Player>& player : gameState.players()) {
        for (const std::unique_ptr<aoc::game::City>& city : player->cities()) {
            const aoc::sim::CityPollutionComponent& pol = city->pollution();
            if (pol.wasteAccumulated != 0 || pol.co2ContributionPerTurn != 0) {
                ++count;
            }
        }
    }

    section.writeU32(count);

    uint32_t cityIndex = 0;
    for (const std::unique_ptr<aoc::game::Player>& player : gameState.players()) {
        for (const std::unique_ptr<aoc::game::City>& city : player->cities()) {
            const aoc::sim::CityPollutionComponent& pol = city->pollution();
            if (pol.wasteAccumulated != 0 || pol.co2ContributionPerTurn != 0) {
                section.writeU32(cityIndex);
                section.writeI32(pol.wasteAccumulated);
                section.writeI32(pol.co2ContributionPerTurn);
            }
            ++cityIndex;
        }
    }

    writeSection(out, SectionId::PollutionState, section);
}

/// Serialize per-city automation (robot worker) state.
void writeAutomationSection(WriteBuffer& out, const aoc::game::GameState& gameState) {
    WriteBuffer section;

    uint32_t count = 0;
    for (const std::unique_ptr<aoc::game::Player>& player : gameState.players()) {
        for (const std::unique_ptr<aoc::game::City>& city : player->cities()) {
            const aoc::sim::CityAutomationComponent& aut = city->automation();
            if (aut.robotWorkers != 0 || aut.turnsSinceLastMaintenance != 0) {
                ++count;
            }
        }
    }

    section.writeU32(count);

    uint32_t cityIndex = 0;
    for (const std::unique_ptr<aoc::game::Player>& player : gameState.players()) {
        for (const std::unique_ptr<aoc::game::City>& city : player->cities()) {
            const aoc::sim::CityAutomationComponent& aut = city->automation();
            if (aut.robotWorkers != 0 || aut.turnsSinceLastMaintenance != 0) {
                section.writeU32(cityIndex);
                section.writeI32(aut.robotWorkers);
                section.writeI32(aut.turnsSinceLastMaintenance);
            }
            ++cityIndex;
        }
    }

    writeSection(out, SectionId::AutomationState, section);
}

/// Serialize per-player industrial revolution progress.
void writeIndustrialSection(WriteBuffer& out, const aoc::game::GameState& gameState) {
    WriteBuffer section;

    uint32_t playerCount = static_cast<uint32_t>(gameState.players().size());
    section.writeU32(playerCount);
    for (const std::unique_ptr<aoc::game::Player>& player : gameState.players()) {
        const aoc::sim::PlayerIndustrialComponent& ind = player->industrial();
        section.writeU8(static_cast<uint8_t>(player->id()));
        section.writeU8(static_cast<uint8_t>(ind.currentRevolution));
        for (int32_t r = 0; r < 6; ++r) {
            section.writeI32(ind.turnAchieved[r]);
        }
    }

    writeSection(out, SectionId::IndustrialState, section);
}

// ---------------------------------------------------------------------------
// v7 sections: Prestige, Tourism, SpaceRace, Grievances, WarWeariness,
//              StockPortfolio.  Everything here is long-accumulating state
//              that the per-turn systems assume survives save/load.
// ---------------------------------------------------------------------------

void writePrestigeSection(WriteBuffer& out, const aoc::game::GameState& gameState) {
    WriteBuffer section;
    section.writeU32(static_cast<uint32_t>(gameState.players().size()));
    for (const std::unique_ptr<aoc::game::Player>& player : gameState.players()) {
        const aoc::sim::PlayerPrestigeComponent& p = player->prestige();
        section.writeU8(static_cast<uint8_t>(player->id()));
        section.writeF32(p.science);
        section.writeF32(p.culture);
        section.writeF32(p.faith);
        section.writeF32(p.trade);
        section.writeF32(p.diplomacy);
        section.writeF32(p.military);
        section.writeF32(p.governance);
        section.writeF32(p.total);
    }
    writeSection(out, SectionId::PrestigeState, section);
}

void writeTourismSection(WriteBuffer& out, const aoc::game::GameState& gameState) {
    WriteBuffer section;
    section.writeU32(static_cast<uint32_t>(gameState.players().size()));
    for (const std::unique_ptr<aoc::game::Player>& player : gameState.players()) {
        const aoc::sim::PlayerTourismComponent& t = player->tourism();
        section.writeU8(static_cast<uint8_t>(player->id()));
        section.writeF32(t.tourismPerTurn);
        section.writeF32(t.cumulativeTourism);
        section.writeI32(t.foreignTourists);
        section.writeI32(t.domesticTourists);
        section.writeI32(t.greatWorkCount);
        section.writeI32(t.wonderCount);
        section.writeI32(t.nationalParkCount);
    }
    writeSection(out, SectionId::TourismState, section);
}

void writeSpaceRaceSection(WriteBuffer& out, const aoc::game::GameState& gameState) {
    WriteBuffer section;
    section.writeU32(static_cast<uint32_t>(gameState.players().size()));
    for (const std::unique_ptr<aoc::game::Player>& player : gameState.players()) {
        const aoc::sim::PlayerSpaceRaceComponent& sr = player->spaceRace();
        section.writeU8(static_cast<uint8_t>(player->id()));
        section.writeU8(static_cast<uint8_t>(aoc::sim::SPACE_PROJECT_COUNT));
        for (int32_t i = 0; i < aoc::sim::SPACE_PROJECT_COUNT; ++i) {
            section.writeU8(sr.completed[i] ? 1u : 0u);
            section.writeF32(sr.progress[i]);
        }
    }
    writeSection(out, SectionId::SpaceRaceState, section);
}

void writeGrievanceSection(WriteBuffer& out, const aoc::game::GameState& gameState) {
    WriteBuffer section;
    section.writeU32(static_cast<uint32_t>(gameState.players().size()));
    for (const std::unique_ptr<aoc::game::Player>& player : gameState.players()) {
        const aoc::sim::PlayerGrievanceComponent& g = player->grievances();
        section.writeU8(static_cast<uint8_t>(player->id()));
        section.writeU32(static_cast<uint32_t>(g.grievances.size()));
        for (const aoc::sim::Grievance& gr : g.grievances) {
            section.writeU8(static_cast<uint8_t>(gr.type));
            section.writeU8(static_cast<uint8_t>(gr.against));
            section.writeI32(gr.severity);
            section.writeI32(gr.turnsRemaining);
        }
    }
    writeSection(out, SectionId::GrievanceState, section);
}

void writeWarWearinessSection(WriteBuffer& out, const aoc::game::GameState& gameState) {
    WriteBuffer section;
    section.writeU32(static_cast<uint32_t>(gameState.players().size()));
    for (const std::unique_ptr<aoc::game::Player>& player : gameState.players()) {
        const aoc::sim::PlayerWarWearinessComponent& w = player->warWeariness();
        section.writeU8(static_cast<uint8_t>(player->id()));
        section.writeF32(w.weariness);
        section.writeU32(static_cast<uint32_t>(w.turnsAtWar.size()));
        for (const std::pair<const PlayerId, int32_t>& kv : w.turnsAtWar) {
            section.writeU8(static_cast<uint8_t>(kv.first));
            section.writeI32(kv.second);
        }
    }
    writeSection(out, SectionId::WarWearinessState, section);
}

static void writeEquityInvestment(WriteBuffer& section,
                                   const aoc::sim::EquityInvestment& inv) {
    section.writeU8(static_cast<uint8_t>(inv.investor));
    section.writeU8(static_cast<uint8_t>(inv.target));
    section.writeI64(inv.principalInvested);
    section.writeI64(inv.currentValue);
    section.writeI64(inv.totalDividends);
    section.writeI32(inv.turnsHeld);
}

void writeStockPortfolioSection(WriteBuffer& out, const aoc::game::GameState& gameState) {
    WriteBuffer section;
    section.writeU32(static_cast<uint32_t>(gameState.players().size()));
    for (const std::unique_ptr<aoc::game::Player>& player : gameState.players()) {
        const aoc::sim::PlayerStockPortfolioComponent& p = player->stockPortfolio();
        section.writeU8(static_cast<uint8_t>(player->id()));
        section.writeU32(static_cast<uint32_t>(p.investments.size()));
        for (const aoc::sim::EquityInvestment& inv : p.investments) {
            writeEquityInvestment(section, inv);
        }
        section.writeU32(static_cast<uint32_t>(p.foreignInvestments.size()));
        for (const aoc::sim::EquityInvestment& inv : p.foreignInvestments) {
            writeEquityInvestment(section, inv);
        }
    }
    writeSection(out, SectionId::StockPortfolioState, section);
}

/// Serialize active confederations (Staatenbund blocs).
void writeConfederationSection(WriteBuffer& out, const aoc::game::GameState& gameState) {
    WriteBuffer section;
    const std::vector<aoc::sim::ConfederationComponent>& bands = gameState.confederations();
    section.writeU32(static_cast<uint32_t>(bands.size()));
    for (const aoc::sim::ConfederationComponent& c : bands) {
        section.writeU32(c.id);
        section.writeI32(c.formedTurn);
        section.writeU8(c.isActive ? 1u : 0u);
        section.writeU32(static_cast<uint32_t>(c.members.size()));
        for (PlayerId m : c.members) {
            section.writeU8(static_cast<uint8_t>(m));
        }
    }
    writeSection(out, SectionId::ConfederationState, section);
}

/// Serialize bilateral electricity import agreements.
void writeElectricityAgreementSection(WriteBuffer& out,
                                       const aoc::game::GameState& gameState) {
    WriteBuffer section;
    const std::vector<aoc::sim::ElectricityAgreementComponent>& agrs =
        gameState.electricityAgreements();
    section.writeU32(static_cast<uint32_t>(agrs.size()));
    for (const aoc::sim::ElectricityAgreementComponent& a : agrs) {
        section.writeU32(a.id);
        section.writeU8(static_cast<uint8_t>(a.seller));
        section.writeU8(static_cast<uint8_t>(a.buyer));
        section.writeI32(a.energyPerTurn);
        section.writeI32(a.goldPerTurn);
        section.writeI32(a.formedTurn);
        section.writeI32(a.endTurn);
        section.writeU8(a.isActive ? 1u : 0u);
        section.writeI32(a.lastDeliveredEnergy);
    }
    writeSection(out, SectionId::ElectricityAgreementState, section);
}

} // anonymous namespace

// ============================================================================
// Save
// ============================================================================

ErrorCode saveGame(const std::string& filepath,
                    const aoc::game::GameState& gameState,
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
    writeEntitySection(buf, gameState);
    writeRandomSection(buf, rng);
    writeImprovementsSection(buf, grid);
    writeTechProgressSection(buf, gameState);
    writeProductionQueuesSection(buf, gameState);
    writeDistrictsSection(buf, gameState);
    writeMonetarySection(buf, gameState);
    writeGovernmentSection(buf, gameState);
    writeVictorySection(buf, gameState);
    writeStockpilesSection(buf, gameState);
    // v4 sections
    writePlayerStateSection(buf, gameState);
    writeDiplomacySection(buf, diplomacy);
    writeMarketSection(buf, economy);
    writeWonderSection(buf, gameState);
    writeMiscEntitiesSection(buf, gameState);
    writeCurrencyTrustSection(buf, gameState);
    writeCrisisSection(buf, gameState);
    writeBondSection(buf, gameState);
    writeDevaluationSection(buf, gameState);
    writeHoardSection(buf, gameState);
    writeProductionExpSection(buf, gameState);
    writeBuildingLevelsSection(buf, gameState);
    writePollutionSection(buf, gameState);
    writeAutomationSection(buf, gameState);
    writeIndustrialSection(buf, gameState);
    // v7 sections
    writePrestigeSection(buf, gameState);
    writeTourismSection(buf, gameState);
    writeSpaceRaceSection(buf, gameState);
    writeGrievanceSection(buf, gameState);
    writeWarWearinessSection(buf, gameState);
    writeStockPortfolioSection(buf, gameState);
    writeConfederationSection(buf, gameState);
    writeElectricityAgreementSection(buf, gameState);

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

/**
 * @brief Load a complete game state from a file into the GameState object model.
 *
 * Units and cities are collected into loadedCities / loadedUnits (in write order)
 * so that later sections can reference them by index without ECS entity handles.
 */
ErrorCode loadGame(const std::string& filepath,
                    aoc::game::GameState& gameState,
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

    // Ordered collections of cities and units matching write order (player order, then list order).
    // Later sections (queues, districts, stockpiles, etc.) reference these by index.
    std::vector<aoc::game::City*> loadedCities;
    std::vector<aoc::game::Unit*> loadedUnits;

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
                aoc::map::MapTopology topology = static_cast<aoc::map::MapTopology>(buf.readU8());
                grid.initialize(width, height, topology);
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
                loadedUnits.reserve(unitCount);

                // First pass: collect (owner, typeId, pos, ...) to build Player->Unit
                // We need to know how many players exist. Use the max owner id + 1.
                // Initialize gameState lazily when we first encounter an owner.
                // We call gameState.initialize() once after reading all units/cities
                // if it hasn't been called yet, but since players were already
                // created by the caller (or we must create them), we do it here.

                struct UnitData {
                    PlayerId owner;
                    UnitTypeId typeId;
                    aoc::hex::AxialCoord pos;
                    int32_t hp;
                    int32_t mp;
                    aoc::sim::UnitState state;
                    int8_t charges;
                    std::vector<aoc::hex::AxialCoord> pendingPath;
                };
                std::vector<UnitData> unitDataList;
                unitDataList.reserve(unitCount);

                PlayerId maxOwner = 0;
                for (uint32_t i = 0; i < unitCount; ++i) {
                    UnitData ud{};
                    ud.owner = buf.readU8();
                    ud.typeId = UnitTypeId{buf.readU16()};
                    ud.pos = {buf.readI32(), buf.readI32()};
                    ud.hp = buf.readI32();
                    ud.mp = buf.readI32();
                    ud.state = static_cast<aoc::sim::UnitState>(buf.readU8());
                    ud.charges = static_cast<int8_t>(buf.readU8());
                    [[maybe_unused]] uint8_t cargoCapacity = buf.readU8();
                    uint16_t pathSize = buf.readU16();
                    ud.pendingPath.reserve(pathSize);
                    for (uint16_t p = 0; p < pathSize; ++p) {
                        ud.pendingPath.push_back({buf.readI32(), buf.readI32()});
                    }
                    if (ud.owner > maxOwner) { maxOwner = ud.owner; }
                    unitDataList.push_back(std::move(ud));
                }

                // Cities
                uint32_t cityCount = buf.readU32();

                struct CityData {
                    PlayerId owner;
                    aoc::hex::AxialCoord loc;
                    std::string name;
                    int32_t population;
                    float foodSurplus;
                    float productionProgress;
                    std::vector<aoc::hex::AxialCoord> workedTiles;
                    float cultureBorderProgress;
                    int32_t tilesClaimedCount;
                    bool isOriginalCapital;
                    PlayerId originalOwner;
                };
                std::vector<CityData> cityDataList;
                cityDataList.reserve(cityCount);

                for (uint32_t i = 0; i < cityCount; ++i) {
                    CityData cd{};
                    cd.owner = buf.readU8();
                    cd.loc = {buf.readI32(), buf.readI32()};
                    cd.name = buf.readString();
                    cd.population = buf.readI32();
                    cd.foodSurplus = buf.readF32();
                    cd.productionProgress = buf.readF32();

                    uint32_t workedCount = buf.readU32();
                    cd.workedTiles.reserve(workedCount);
                    for (uint32_t j = 0; j < workedCount; ++j) {
                        cd.workedTiles.push_back({buf.readI32(), buf.readI32()});
                    }

                    cd.cultureBorderProgress = buf.readF32();
                    cd.tilesClaimedCount = buf.readI32();
                    cd.isOriginalCapital = buf.readU8() != 0;
                    cd.originalOwner = buf.readU8();

                    if (cd.owner > maxOwner) { maxOwner = cd.owner; }
                    cityDataList.push_back(std::move(cd));
                }

                // Initialize (or re-initialize) GameState with the correct player count.
                // This clears any existing player data, which is the correct behavior
                // for a load operation that must fully replace the game state.
                int32_t requiredPlayers = static_cast<int32_t>(maxOwner) + 1;
                gameState.initialize(requiredPlayers);

                // Populate Player objects with cities
                for (const CityData& cd : cityDataList) {
                    aoc::game::Player* player = gameState.player(cd.owner);
                    if (player == nullptr) {
                        LOG_ERROR("Serializer.cpp: loadGame: invalid owner %u in Entities section",
                                  static_cast<unsigned>(cd.owner));
                        return ErrorCode::SaveCorrupted;
                    }
                    aoc::game::City& city = player->addCity(cd.loc, cd.name);
                    city.setPopulation(cd.population);
                    city.setFoodSurplus(cd.foodSurplus);
                    city.setProductionProgress(cd.productionProgress);
                    city.workedTiles() = cd.workedTiles;
                    city.setCultureBorderProgress(cd.cultureBorderProgress);
                    for (int32_t t = 0; t < cd.tilesClaimedCount; ++t) {
                        city.incrementTilesClaimed();
                    }
                    city.setOriginalCapital(cd.isOriginalCapital);
                    city.setOriginalOwner(cd.originalOwner);
                    loadedCities.push_back(&city);
                }

                // Populate Player objects with units
                for (const UnitData& ud : unitDataList) {
                    aoc::game::Player* player = gameState.player(ud.owner);
                    if (player == nullptr) {
                        LOG_ERROR("Serializer.cpp: loadGame: invalid owner %u in Entities section",
                                  static_cast<unsigned>(ud.owner));
                        return ErrorCode::SaveCorrupted;
                    }
                    aoc::game::Unit& unit = player->addUnit(ud.typeId, ud.pos);
                    unit.setHitPoints(ud.hp);
                    unit.setMovementRemaining(ud.mp);
                    unit.setState(ud.state);
                    // Note: chargesRemaining is not yet settable via Unit public API.
                    unit.pendingPath() = ud.pendingPath;
                    loadedUnits.push_back(&unit);
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
                // Tech components (one per player)
                uint32_t techCompCount = buf.readU32();
                for (uint32_t i = 0; i < techCompCount; ++i) {
                    PlayerId owner = buf.readU8();
                    uint16_t currentResearchVal = buf.readU16();
                    float progress = buf.readF32();
                    uint16_t totalTechs = buf.readU16();
                    uint16_t byteCount = static_cast<uint16_t>((totalTechs + 7) / 8);

                    aoc::game::Player* player = gameState.player(owner);
                    if (player != nullptr) {
                        aoc::sim::PlayerTechComponent& tech = player->tech();
                        tech.initialize();
                        tech.currentResearch = TechId{currentResearchVal};
                        tech.researchProgress = progress;

                        for (uint16_t b = 0; b < byteCount; ++b) {
                            uint8_t byte = buf.readU8();
                            for (uint8_t bit = 0; bit < 8; ++bit) {
                                uint16_t techIdx = static_cast<uint16_t>(b * 8 + bit);
                                if (techIdx < totalTechs && techIdx < tech.completedTechs.size()) {
                                    tech.completedTechs[techIdx] = ((byte >> bit) & 1u) != 0;
                                }
                            }
                        }
                    } else {
                        buf.skip(byteCount);
                    }
                }

                // Civic components (one per player)
                uint32_t civicCompCount = buf.readU32();
                for (uint32_t i = 0; i < civicCompCount; ++i) {
                    PlayerId owner = buf.readU8();
                    uint16_t currentResearchVal = buf.readU16();
                    float progress = buf.readF32();
                    uint16_t totalCivics = buf.readU16();
                    uint16_t byteCount = static_cast<uint16_t>((totalCivics + 7) / 8);

                    aoc::game::Player* player = gameState.player(owner);
                    if (player != nullptr) {
                        aoc::sim::PlayerCivicComponent& civic = player->civics();
                        civic.initialize();
                        civic.currentResearch = CivicId{currentResearchVal};
                        civic.researchProgress = progress;

                        for (uint16_t b = 0; b < byteCount; ++b) {
                            uint8_t byte = buf.readU8();
                            for (uint8_t bit = 0; bit < 8; ++bit) {
                                uint16_t civicIdx = static_cast<uint16_t>(b * 8 + bit);
                                if (civicIdx < totalCivics && civicIdx < civic.completedCivics.size()) {
                                    civic.completedCivics[civicIdx] = ((byte >> bit) & 1u) != 0;
                                }
                            }
                        }
                    } else {
                        buf.skip(byteCount);
                    }
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

                    if (cityIndex < static_cast<uint32_t>(loadedCities.size())) {
                        loadedCities[cityIndex]->production().queue = std::move(queue.queue);
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

                    if (cityIndex < static_cast<uint32_t>(loadedCities.size())) {
                        loadedCities[cityIndex]->districts() = std::move(districts);
                    }
                }
                break;
            }
            case SectionId::MonetaryState: {
                uint32_t count = buf.readU32();
                for (uint32_t i = 0; i < count; ++i) {
                    PlayerId owner = buf.readU8();
                    aoc::game::Player* player = gameState.player(owner);
                    aoc::sim::MonetaryStateComponent m{};
                    m.owner = owner;
                    m.system = static_cast<aoc::sim::MonetarySystemType>(buf.readU8());
                    m.moneySupply = buf.readI64();
                    m.treasury = buf.readI64();
                    m.copperCoinReserves = buf.readI32();
                    m.silverCoinReserves = buf.readI32();
                    m.goldBarReserves = buf.readI32();
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
                    if (player != nullptr) {
                        player->monetary() = std::move(m);
                    }
                }
                break;
            }
            case SectionId::GovernmentState: {
                uint32_t count = buf.readU32();
                for (uint32_t i = 0; i < count; ++i) {
                    PlayerId owner = buf.readU8();
                    aoc::game::Player* player = gameState.player(owner);
                    aoc::sim::PlayerGovernmentComponent gov{};
                    gov.owner = owner;
                    gov.government = static_cast<aoc::sim::GovernmentType>(buf.readU8());
                    for (uint8_t s = 0; s < aoc::sim::MAX_POLICY_SLOTS; ++s) {
                        gov.activePolicies[s] = static_cast<int8_t>(buf.readU8());
                    }
                    gov.unlockedGovernments = buf.readU16();
                    gov.unlockedPolicies = buf.readU32();
                    gov.anarchyTurnsRemaining = buf.readI32();
                    gov.activeAction = static_cast<aoc::sim::GovernmentAction>(buf.readU8());
                    gov.actionTurnsRemaining = buf.readI32();
                    if (player != nullptr) {
                        player->government() = std::move(gov);
                    }
                }
                break;
            }
            case SectionId::VictoryState: {
                uint32_t count = buf.readU32();
                for (uint32_t i = 0; i < count; ++i) {
                    PlayerId owner = buf.readU8();
                    aoc::game::Player* player = gameState.player(owner);
                    aoc::sim::VictoryTrackerComponent v{};
                    v.owner = owner;
                    v.scienceProgress = buf.readI32();
                    v.totalCultureAccumulated = buf.readF32();
                    v.score = buf.readI32();
                    for (int32_t c = 0; c < aoc::sim::CSI_CATEGORY_COUNT; ++c) {
                        v.categoryScores[c] = buf.readF32();
                    }
                    v.tradeNetworkMultiplier = buf.readF32();
                    v.financialIntegrationMult = buf.readF32();
                    v.diplomaticWebMult = buf.readF32();
                    v.compositeCSI = buf.readF32();
                    v.eraVictoryPoints = buf.readI32();
                    v.erasEvaluated = buf.readI32();
                    (void)buf.readI32();   // legacy integrationProgress
                    (void)buf.readU8();    // legacy integrationComplete
                    v.activeCollapse = static_cast<aoc::sim::CollapseType>(buf.readU8());
                    v.peakGDP = buf.readI32();
                    v.turnsGDPBelowHalf = buf.readI32();
                    v.turnsLowLoyalty = buf.readI32();
                    v.isEliminated = buf.readU8() != 0;
                    if (player != nullptr) {
                        player->victoryTracker() = std::move(v);
                    }
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

                    if (cityIndex < static_cast<uint32_t>(loadedCities.size())) {
                        loadedCities[cityIndex]->stockpile() = std::move(stockpile);
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
                    PlayerId owner = buf.readU8();
                    uint8_t civId = buf.readU8();
                    aoc::game::Player* player = gameState.player(owner);
                    if (player != nullptr) {
                        player->setCivId(static_cast<aoc::sim::CivId>(civId));
                    }
                }

                // --- PlayerEraComponent ---
                uint32_t eraCount = buf.readU32();
                for (uint32_t i = 0; i < eraCount; ++i) {
                    PlayerId owner = buf.readU8();
                    EraId eraId{buf.readU16()};
                    aoc::game::Player* player = gameState.player(owner);
                    if (player != nullptr) {
                        player->era().currentEra = eraId;
                    }
                }

                // --- PlayerEconomyComponent ---
                uint32_t econCount = buf.readU32();
                for (uint32_t i = 0; i < econCount; ++i) {
                    PlayerId owner = buf.readU8();
                    int64_t treasury = buf.readI64();
                    int64_t incomePerTurn = buf.readI64();
                    aoc::game::Player* player = gameState.player(owner);
                    if (player != nullptr) {
                        player->economy().treasury = treasury;
                        player->economy().incomePerTurn = incomePerTurn;
                    }
                }

                // --- PlayerGreatPeopleComponent ---
                constexpr std::size_t GP_TYPE_COUNT =
                    static_cast<std::size_t>(aoc::sim::GreatPersonType::Count);
                uint32_t gpCount = buf.readU32();
                for (uint32_t i = 0; i < gpCount; ++i) {
                    PlayerId owner = buf.readU8();
                    aoc::game::Player* player = gameState.player(owner);
                    for (std::size_t t = 0; t < GP_TYPE_COUNT; ++t) {
                        float points = buf.readF32();
                        if (player != nullptr) { player->greatPeople().points[t] = points; }
                    }
                    for (std::size_t t = 0; t < GP_TYPE_COUNT; ++t) {
                        int32_t recruited = buf.readI32();
                        if (player != nullptr) { player->greatPeople().recruited[t] = recruited; }
                    }
                    // H3.8 exhausted flags (introduced in SAVE_VERSION 6).
                    for (std::size_t t = 0; t < GP_TYPE_COUNT; ++t) {
                        uint8_t flag = buf.readU8();
                        if (player != nullptr) {
                            player->greatPeople().exhausted[t] = (flag != 0);
                        }
                    }
                }

                // --- PlayerEurekaComponent ---
                // v8+: pending-boost bitfield follows the triggered bitfield.
                uint32_t eurekaCount = buf.readU32();
                for (uint32_t i = 0; i < eurekaCount; ++i) {
                    PlayerId owner = buf.readU8();
                    uint16_t bitCount = buf.readU16();
                    uint16_t byteCount = static_cast<uint16_t>((bitCount + 7) / 8);
                    aoc::game::Player* player = gameState.player(owner);
                    for (uint16_t b = 0; b < byteCount; ++b) {
                        uint8_t byte = buf.readU8();
                        for (uint8_t bit = 0; bit < 8; ++bit) {
                            uint16_t idx = static_cast<uint16_t>(b * 8 + bit);
                            if (idx < bitCount && idx < aoc::sim::MAX_EUREKA_BOOSTS
                                && ((byte >> bit) & 1u) != 0 && player != nullptr) {
                                player->eureka().triggeredBoosts.set(idx);
                            }
                        }
                    }
                    for (uint16_t b = 0; b < byteCount; ++b) {
                        uint8_t byte = buf.readU8();
                        for (uint8_t bit = 0; bit < 8; ++bit) {
                            uint16_t idx = static_cast<uint16_t>(b * 8 + bit);
                            if (idx < bitCount && idx < aoc::sim::MAX_EUREKA_BOOSTS
                                && ((byte >> bit) & 1u) != 0 && player != nullptr) {
                                player->eureka().pendingBoosts.set(idx);
                            }
                        }
                    }
                }

                // --- PlayerWarComponent (legacy: skip) ---
                uint32_t warCompCount = buf.readU32();
                for (uint32_t i = 0; i < warCompCount; ++i) {
                    [[maybe_unused]] uint8_t owner = buf.readU8();
                    uint32_t warCount = buf.readU32();
                    for (uint32_t w = 0; w < warCount; ++w) {
                        (void)buf.readU8();  // aggressor
                        (void)buf.readU8();  // defender
                        (void)buf.readU8();  // casusBelli
                        (void)buf.readU32(); // startTurn
                        (void)buf.readI32(); // aggressorWarScore
                        (void)buf.readI32(); // defenderWarScore
                    }
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
                        // v5: alliance bitmask + per-type state + cooldowns
                        const uint8_t allianceBits = buf.readU8();
                        rel.hasMilitaryAlliance  = (allianceBits & 0x01) != 0;
                        rel.hasResearchAgreement = (allianceBits & 0x02) != 0;
                        rel.hasEconomicAlliance  = (allianceBits & 0x04) != 0;
                        rel.hasCulturalAlliance  = (allianceBits & 0x08) != 0;
                        rel.hasReligiousAlliance = (allianceBits & 0x10) != 0;
                        for (aoc::sim::AllianceState& st : rel.alliances) {
                            st.type        = static_cast<aoc::sim::AllianceType>(buf.readU8());
                            st.level       = static_cast<aoc::sim::AllianceLevel>(buf.readU8());
                            st.turnsActive = buf.readI32();
                        }
                        rel.lastAllianceFormTurn      = buf.readI32();
                        rel.allianceBreakWarningTurns = buf.readI32();
                        rel.lastCasusBelli            =
                            static_cast<aoc::sim::CasusBelliType>(buf.readU8());
                        // Mirror new fields to (b,a) direction so the matrix is
                        // fully symmetric right after load. Runtime code relies on
                        // both directions holding the same alliance state.
                        aoc::sim::PairwiseRelation& mirror = diplomacy.relation(b, a);
                        mirror.hasDefensiveAlliance    = rel.hasDefensiveAlliance;
                        mirror.hasMilitaryAlliance     = rel.hasMilitaryAlliance;
                        mirror.hasResearchAgreement    = rel.hasResearchAgreement;
                        mirror.hasEconomicAlliance     = rel.hasEconomicAlliance;
                        mirror.hasCulturalAlliance     = rel.hasCulturalAlliance;
                        mirror.hasReligiousAlliance    = rel.hasReligiousAlliance;
                        mirror.alliances               = rel.alliances;
                        mirror.lastAllianceFormTurn    = rel.lastAllianceFormTurn;
                        mirror.allianceBreakWarningTurns = rel.allianceBreakWarningTurns;
                        mirror.lastCasusBelli          = rel.lastCasusBelli;
                        uint32_t modCount = buf.readU32();
                        rel.modifiers.reserve(modCount);
                        for (uint32_t m = 0; m < modCount; ++m) {
                            aoc::sim::RelationModifier mod{};
                            mod.reason = buf.readString();
                            mod.amount = buf.readI32();
                            mod.turnsRemaining = buf.readI32();
                            rel.modifiers.push_back(std::move(mod));
                        }
                        // Reputation modifiers (political reputation system)
                        uint32_t repModCount = buf.readU32();
                        rel.reputationModifiers.reserve(repModCount);
                        for (uint32_t m = 0; m < repModCount; ++m) {
                            aoc::sim::ReputationModifier repMod{};
                            repMod.amount = buf.readI32();
                            repMod.turnsRemaining = buf.readI32();
                            rel.reputationModifiers.push_back(repMod);
                        }
                        // Border violation state
                        rel.unitsInTerritory = buf.readI32();
                        rel.turnsWithViolation = buf.readI32();
                        const uint8_t cbBits = buf.readU8();
                        rel.casusBelliLand = (cbBits & 0x1) != 0;
                        rel.casusBelliNaval = (cbBits & 0x2) != 0;
                        rel.warningIssued = buf.readU8() != 0;
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
                buf.skip(sectionSize);
                break;
            }
            case SectionId::WonderState: {
                // GlobalWonderTracker
                uint8_t hasTracker = buf.readU8();
                if (hasTracker != 0) {
                    aoc::sim::GlobalWonderTracker& tracker = gameState.wonderTracker();
                    for (uint8_t w = 0; w < aoc::sim::WONDER_COUNT; ++w) {
                        tracker.builtBy[w] = buf.readU8();
                    }
                }

                // Per-city wonders
                uint32_t cityWonderCount = buf.readU32();
                for (uint32_t i = 0; i < cityWonderCount; ++i) {
                    uint32_t cityIndex = buf.readU32();
                    uint32_t wonderCount = buf.readU32();

                    aoc::sim::CityWondersComponent wonders{};
                    wonders.wonders.reserve(wonderCount);
                    for (uint32_t w = 0; w < wonderCount; ++w) {
                        wonders.wonders.push_back(buf.readU8());
                    }

                    if (cityIndex < static_cast<uint32_t>(loadedCities.size())) {
                        loadedCities[cityIndex]->wonders() = std::move(wonders);
                    }
                }
                break;
            }
            case SectionId::MiscEntities: {
                // --- BarbarianEncampmentComponent (not yet in GameState object model: skip) ---
                uint32_t barbCount = buf.readU32();
                for (uint32_t i = 0; i < barbCount; ++i) {
                    (void)buf.readI32(); (void)buf.readI32();  // location q, r
                    (void)buf.readI32(); (void)buf.readI32();  // spawnCooldown, unitsSpawned
                }

                // --- GreatPersonComponent (not yet in GameState object model: skip) ---
                uint32_t gpPersonCount = buf.readU32();
                for (uint32_t i = 0; i < gpPersonCount; ++i) {
                    (void)buf.readU8();  // owner
                    (void)buf.readU8();  // defId
                    (void)buf.readI32(); (void)buf.readI32();  // position q, r
                    (void)buf.readU8();  // isActivated
                }

                // --- SpyComponent: stored on Unit objects ---
                uint32_t spyCount = buf.readU32();
                for (uint32_t i = 0; i < spyCount; ++i) {
                    PlayerId owner = buf.readU8();
                    aoc::sim::SpyComponent comp{};
                    comp.owner = owner;
                    comp.location.q = buf.readI32();
                    comp.location.r = buf.readI32();
                    comp.currentMission = static_cast<aoc::sim::SpyMission>(buf.readU8());
                    comp.turnsRemaining = buf.readI32();
                    comp.experience = buf.readI32();
                    comp.isRevealed = buf.readU8() != 0;

                    // Find the matching spy unit by owner and location
                    aoc::game::Player* player = gameState.player(owner);
                    if (player != nullptr) {
                        for (const std::unique_ptr<aoc::game::Unit>& unit : player->units()) {
                            if (unit->position() == comp.location) {
                                unit->spy() = comp;
                                break;
                            }
                        }
                    }
                }

                // --- UnitExperienceComponent (not yet in Unit object model: skip) ---
                uint32_t expCount = buf.readU32();
                for (uint32_t i = 0; i < expCount; ++i) {
                    (void)buf.readU32();  // unitIndex
                    (void)buf.readI32(); (void)buf.readI32();  // experience, level
                    uint32_t promoCount = buf.readU32();
                    for (uint32_t p = 0; p < promoCount; ++p) {
                        (void)buf.readU16();  // PromotionId
                    }
                }
                break;
            }
            case SectionId::CurrencyTrust: {
                uint32_t count = buf.readU32();
                for (uint32_t i = 0; i < count; ++i) {
                    PlayerId owner = buf.readU8();
                    aoc::game::Player* player = gameState.player(owner);
                    aoc::sim::CurrencyTrustComponent ct{};
                    ct.owner = owner;
                    ct.trustScore = buf.readF32();
                    ct.turnsOnFiat = buf.readI32();
                    ct.turnsStable = buf.readI32();
                    ct.isReserveCurrency = buf.readU8() != 0;
                    ct.turnsAsReserve = buf.readI32();
                    for (int32_t p = 0; p < aoc::sim::CurrencyTrustComponent::MAX_PLAYERS; ++p) {
                        ct.bilateralTrust[p] = buf.readF32();
                    }
                    if (player != nullptr) {
                        player->currencyTrust() = std::move(ct);
                    }
                }
                break;
            }
            case SectionId::CrisisState: {
                uint32_t count = buf.readU32();
                for (uint32_t i = 0; i < count; ++i) {
                    PlayerId owner = buf.readU8();
                    aoc::game::Player* player = gameState.player(owner);
                    aoc::sim::CurrencyCrisisComponent c{};
                    c.owner = owner;
                    c.activeCrisis = static_cast<aoc::sim::CrisisType>(buf.readU8());
                    c.turnsRemaining = buf.readI32();
                    c.turnsHighInflation = buf.readI32();
                    c.hasDefaulted = buf.readU8() != 0;
                    c.defaultCooldown = buf.readI32();
                    c.reformLockoutTurns  = buf.readI32();
                    c.reformTrustCapTurns = buf.readI32();
                    if (player != nullptr) {
                        player->currencyCrisis() = std::move(c);
                    }
                }
                break;
            }
            case SectionId::BondState: {
                uint32_t count = buf.readU32();
                for (uint32_t i = 0; i < count; ++i) {
                    PlayerId owner = buf.readU8();
                    aoc::game::Player* player = gameState.player(owner);
                    aoc::sim::PlayerBondComponent pb{};
                    pb.owner = owner;
                    uint32_t issuedCount = buf.readU32();
                    pb.issuedBonds.reserve(issuedCount);
                    for (uint32_t j = 0; j < issuedCount; ++j) {
                        aoc::sim::BondIssue b{};
                        b.id = buf.readU64();
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
                        b.id = buf.readU64();
                        b.issuer = buf.readU8();
                        b.holder = buf.readU8();
                        b.principal = buf.readI64();
                        b.yieldRate = buf.readF32();
                        b.turnsToMaturity = buf.readI32();
                        b.accruedInterest = buf.readI64();
                        pb.heldBonds.push_back(b);
                    }
                    if (player != nullptr) {
                        player->bonds() = std::move(pb);
                    }
                }
                // Restore bond id counter so new bonds keep unique ids.
                aoc::sim::setNextBondId(buf.readU64());
                break;
            }
            case SectionId::DevaluationState: {
                uint32_t count = buf.readU32();
                for (uint32_t i = 0; i < count; ++i) {
                    PlayerId owner = buf.readU8();
                    aoc::game::Player* player = gameState.player(owner);
                    aoc::sim::CurrencyDevaluationComponent d{};
                    d.owner = owner;
                    d.isDevalued = buf.readU8() != 0;
                    d.devaluationTurnsLeft = buf.readI32();
                    d.exportBonus = buf.readF32();
                    d.importPenalty = buf.readF32();
                    d.devaluationCount = buf.readI32();
                    if (player != nullptr) {
                        player->currencyDevaluation() = std::move(d);
                    }
                }
                break;
            }
            case SectionId::HoardState: {
                uint32_t count = buf.readU32();
                gameState.commodityHoards().reserve(count);
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
                    gameState.commodityHoards().push_back(std::move(h));
                }
                break;
            }
            case SectionId::ProductionExp: {
                uint32_t count = buf.readU32();
                for (uint32_t i = 0; i < count; ++i) {
                    uint32_t cityIndex = buf.readU32();
                    uint32_t mapSize = buf.readU32();
                    aoc::sim::CityProductionExperienceComponent comp{};
                    for (uint32_t j = 0; j < mapSize; ++j) {
                        uint16_t recipeId = buf.readU16();
                        int32_t exp = buf.readI32();
                        comp.recipeExperience[recipeId] = exp;
                    }
                    if (cityIndex < static_cast<uint32_t>(loadedCities.size())) {
                        loadedCities[cityIndex]->productionExperience() = std::move(comp);
                    }
                }
                break;
            }
            case SectionId::BuildingLevels: {
                uint32_t count = buf.readU32();
                for (uint32_t i = 0; i < count; ++i) {
                    uint32_t cityIndex = buf.readU32();
                    uint32_t mapSize = buf.readU32();
                    aoc::sim::CityBuildingLevelsComponent comp{};
                    for (uint32_t j = 0; j < mapSize; ++j) {
                        uint16_t bid = buf.readU16();
                        int32_t lvl = buf.readI32();
                        comp.levels[bid] = lvl;
                    }
                    if (cityIndex < static_cast<uint32_t>(loadedCities.size())) {
                        loadedCities[cityIndex]->buildingLevels() = std::move(comp);
                    }
                }
                break;
            }
            case SectionId::PollutionState: {
                uint32_t count = buf.readU32();
                for (uint32_t i = 0; i < count; ++i) {
                    uint32_t cityIndex = buf.readU32();
                    int32_t wasteAccumulated = buf.readI32();
                    int32_t co2PerTurn = buf.readI32();
                    if (cityIndex < static_cast<uint32_t>(loadedCities.size())) {
                        loadedCities[cityIndex]->pollution().wasteAccumulated = wasteAccumulated;
                        loadedCities[cityIndex]->pollution().co2ContributionPerTurn = co2PerTurn;
                    }
                }
                break;
            }
            case SectionId::AutomationState: {
                uint32_t count = buf.readU32();
                for (uint32_t i = 0; i < count; ++i) {
                    uint32_t cityIndex = buf.readU32();
                    int32_t robotWorkers = buf.readI32();
                    int32_t maintTurns = buf.readI32();
                    if (cityIndex < static_cast<uint32_t>(loadedCities.size())) {
                        loadedCities[cityIndex]->automation().robotWorkers = robotWorkers;
                        loadedCities[cityIndex]->automation().turnsSinceLastMaintenance = maintTurns;
                    }
                }
                break;
            }
            case SectionId::IndustrialState: {
                uint32_t count = buf.readU32();
                for (uint32_t i = 0; i < count; ++i) {
                    PlayerId owner = buf.readU8();
                    aoc::game::Player* player = gameState.player(owner);
                    aoc::sim::PlayerIndustrialComponent ind{};
                    ind.owner = owner;
                    ind.currentRevolution = static_cast<aoc::sim::IndustrialRevolutionId>(buf.readU8());
                    for (int32_t r = 0; r < 6; ++r) {
                        ind.turnAchieved[r] = buf.readI32();
                    }
                    if (player != nullptr) {
                        player->industrial() = std::move(ind);
                    }
                }
                break;
            }
            case SectionId::PrestigeState: {
                uint32_t count = buf.readU32();
                for (uint32_t i = 0; i < count; ++i) {
                    PlayerId owner = buf.readU8();
                    aoc::sim::PlayerPrestigeComponent p{};
                    p.owner      = owner;
                    p.science    = buf.readF32();
                    p.culture    = buf.readF32();
                    p.faith      = buf.readF32();
                    p.trade      = buf.readF32();
                    p.diplomacy  = buf.readF32();
                    p.military   = buf.readF32();
                    p.governance = buf.readF32();
                    p.total      = buf.readF32();
                    aoc::game::Player* player = gameState.player(owner);
                    if (player != nullptr) { player->prestige() = p; }
                }
                break;
            }
            case SectionId::TourismState: {
                uint32_t count = buf.readU32();
                for (uint32_t i = 0; i < count; ++i) {
                    PlayerId owner = buf.readU8();
                    aoc::sim::PlayerTourismComponent t{};
                    t.owner             = owner;
                    t.tourismPerTurn    = buf.readF32();
                    t.cumulativeTourism = buf.readF32();
                    t.foreignTourists   = buf.readI32();
                    t.domesticTourists  = buf.readI32();
                    t.greatWorkCount    = buf.readI32();
                    t.wonderCount       = buf.readI32();
                    t.nationalParkCount = buf.readI32();
                    aoc::game::Player* player = gameState.player(owner);
                    if (player != nullptr) { player->tourism() = t; }
                }
                break;
            }
            case SectionId::SpaceRaceState: {
                uint32_t count = buf.readU32();
                for (uint32_t i = 0; i < count; ++i) {
                    PlayerId owner = buf.readU8();
                    aoc::sim::PlayerSpaceRaceComponent sr{};
                    sr.owner = owner;
                    uint8_t storedCount = buf.readU8();
                    const int32_t limit = std::min<int32_t>(
                        static_cast<int32_t>(storedCount), aoc::sim::SPACE_PROJECT_COUNT);
                    for (int32_t j = 0; j < limit; ++j) {
                        sr.completed[j] = (buf.readU8() != 0);
                        sr.progress[j]  = buf.readF32();
                    }
                    // Older save with more projects than the current build
                    // supports: consume and discard the extras.
                    for (int32_t j = limit; j < static_cast<int32_t>(storedCount); ++j) {
                        (void)buf.readU8();
                        (void)buf.readF32();
                    }
                    aoc::game::Player* player = gameState.player(owner);
                    if (player != nullptr) { player->spaceRace() = sr; }
                }
                break;
            }
            case SectionId::GrievanceState: {
                uint32_t count = buf.readU32();
                for (uint32_t i = 0; i < count; ++i) {
                    PlayerId owner = buf.readU8();
                    uint32_t n = buf.readU32();
                    aoc::sim::PlayerGrievanceComponent g{};
                    g.owner = owner;
                    g.grievances.reserve(n);
                    for (uint32_t j = 0; j < n; ++j) {
                        aoc::sim::Grievance gr{};
                        gr.type            = static_cast<aoc::sim::GrievanceType>(buf.readU8());
                        gr.against         = buf.readU8();
                        gr.severity        = buf.readI32();
                        gr.turnsRemaining  = buf.readI32();
                        g.grievances.push_back(gr);
                    }
                    aoc::game::Player* player = gameState.player(owner);
                    if (player != nullptr) { player->grievances() = std::move(g); }
                }
                break;
            }
            case SectionId::WarWearinessState: {
                uint32_t count = buf.readU32();
                for (uint32_t i = 0; i < count; ++i) {
                    PlayerId owner = buf.readU8();
                    aoc::sim::PlayerWarWearinessComponent w{};
                    w.owner     = owner;
                    w.weariness = buf.readF32();
                    uint32_t mapSize = buf.readU32();
                    for (uint32_t j = 0; j < mapSize; ++j) {
                        PlayerId key = buf.readU8();
                        int32_t  val = buf.readI32();
                        w.turnsAtWar[key] = val;
                    }
                    aoc::game::Player* player = gameState.player(owner);
                    if (player != nullptr) { player->warWeariness() = std::move(w); }
                }
                break;
            }
            case SectionId::StockPortfolioState: {
                uint32_t count = buf.readU32();
                for (uint32_t i = 0; i < count; ++i) {
                    PlayerId owner = buf.readU8();
                    aoc::sim::PlayerStockPortfolioComponent p{};
                    p.owner = owner;
                    uint32_t invCount = buf.readU32();
                    p.investments.reserve(invCount);
                    for (uint32_t j = 0; j < invCount; ++j) {
                        aoc::sim::EquityInvestment inv{};
                        inv.investor          = buf.readU8();
                        inv.target            = buf.readU8();
                        inv.principalInvested = buf.readI64();
                        inv.currentValue      = buf.readI64();
                        inv.totalDividends    = buf.readI64();
                        inv.turnsHeld         = buf.readI32();
                        p.investments.push_back(inv);
                    }
                    uint32_t foreignCount = buf.readU32();
                    p.foreignInvestments.reserve(foreignCount);
                    for (uint32_t j = 0; j < foreignCount; ++j) {
                        aoc::sim::EquityInvestment inv{};
                        inv.investor          = buf.readU8();
                        inv.target            = buf.readU8();
                        inv.principalInvested = buf.readI64();
                        inv.currentValue      = buf.readI64();
                        inv.totalDividends    = buf.readI64();
                        inv.turnsHeld         = buf.readI32();
                        p.foreignInvestments.push_back(inv);
                    }
                    aoc::game::Player* player = gameState.player(owner);
                    if (player != nullptr) { player->stockPortfolio() = std::move(p); }
                }
                break;
            }
            case SectionId::ConfederationState: {
                uint32_t count = buf.readU32();
                std::vector<aoc::sim::ConfederationComponent>& bands =
                    gameState.confederations();
                bands.clear();
                bands.reserve(count);
                for (uint32_t i = 0; i < count; ++i) {
                    aoc::sim::ConfederationComponent c{};
                    c.id         = buf.readU32();
                    c.formedTurn = buf.readI32();
                    c.isActive   = (buf.readU8() != 0u);
                    uint32_t memberCount = buf.readU32();
                    c.members.reserve(memberCount);
                    for (uint32_t m = 0; m < memberCount; ++m) {
                        c.members.push_back(buf.readU8());
                    }
                    bands.push_back(std::move(c));
                }
                break;
            }
            case SectionId::ElectricityAgreementState: {
                uint32_t count = buf.readU32();
                std::vector<aoc::sim::ElectricityAgreementComponent>& agrs =
                    gameState.electricityAgreements();
                agrs.clear();
                agrs.reserve(count);
                for (uint32_t i = 0; i < count; ++i) {
                    aoc::sim::ElectricityAgreementComponent a{};
                    a.id                  = buf.readU32();
                    a.seller              = buf.readU8();
                    a.buyer               = buf.readU8();
                    a.energyPerTurn       = buf.readI32();
                    a.goldPerTurn         = buf.readI32();
                    a.formedTurn          = buf.readI32();
                    a.endTurn             = buf.readI32();
                    a.isActive            = (buf.readU8() != 0u);
                    a.lastDeliveredEnergy = buf.readI32();
                    agrs.push_back(a);
                }
                break;
            }
            default:
                // Unknown section: skip for forward compatibility
                buf.skip(sectionSize);
                break;
        }
    }

    LOG_INFO("Game loaded from %s", filepath.c_str());
    return ErrorCode::Ok;
}

} // namespace aoc::save
