/**
 * @file Serializer.cpp
 * @brief Binary save/load implementation.
 */

#include "aoc/save/Serializer.hpp"
#include "aoc/ecs/World.hpp"
#include "aoc/map/HexGrid.hpp"
#include "aoc/map/FogOfWar.hpp"
#include "aoc/map/Terrain.hpp"
#include "aoc/simulation/turn/TurnManager.hpp"
#include "aoc/simulation/resource/EconomySimulation.hpp"
#include "aoc/simulation/diplomacy/DiplomacyState.hpp"
#include "aoc/simulation/unit/UnitComponent.hpp"
#include "aoc/simulation/city/CityComponent.hpp"
#include "aoc/simulation/monetary/MonetarySystem.hpp"
#include "aoc/simulation/resource/ResourceComponent.hpp"
#include "aoc/simulation/tech/TechTree.hpp"
#include "aoc/simulation/tech/CivicTree.hpp"

#include <cstring>
#include <cstdio>
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
    v |= static_cast<uint16_t>(this->m_data[this->m_offset + 1]) << 8;
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
        }
    }

    writeSection(out, SectionId::Entities, section);
}

} // anonymous namespace

// ============================================================================
// Save
// ============================================================================

ErrorCode saveGame(const std::string& filepath,
                    const aoc::ecs::World& world,
                    const aoc::map::HexGrid& grid,
                    const aoc::sim::TurnManager& turnManager,
                    const aoc::sim::EconomySimulation& /*economy*/,
                    const aoc::sim::DiplomacyManager& /*diplomacy*/,
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

    // Write to file
    std::ofstream file(filepath, std::ios::binary);
    if (!file.is_open()) {
        std::fprintf(stderr, "[Save] %s:%d Failed to open file for writing: %s\n",
                     __FILE__, __LINE__, filepath.c_str());
        return ErrorCode::SaveFailed;
    }

    file.write(reinterpret_cast<const char*>(buf.data().data()),
               static_cast<std::streamsize>(buf.size()));
    if (!file.good()) {
        return ErrorCode::SaveFailed;
    }

    std::fprintf(stdout, "[Save] Game saved to %s (%zu bytes)\n",
                 filepath.c_str(), buf.size());
    return ErrorCode::Ok;
}

// ============================================================================
// Load
// ============================================================================

ErrorCode loadGame(const std::string& filepath,
                    aoc::ecs::World& world,
                    aoc::map::HexGrid& grid,
                    aoc::sim::TurnManager& /*turnManager*/,
                    aoc::sim::EconomySimulation& /*economy*/,
                    aoc::sim::DiplomacyManager& /*diplomacy*/,
                    aoc::map::FogOfWar& /*fogOfWar*/,
                    aoc::Random& rng) {
    // Read entire file
    std::ifstream file(filepath, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        std::fprintf(stderr, "[Load] %s:%d Failed to open file: %s\n",
                     __FILE__, __LINE__, filepath.c_str());
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
                [[maybe_unused]] uint32_t turnNum = buf.readU32();
                [[maybe_unused]] uint8_t phase    = buf.readU8();
                break;
            }
            case SectionId::Entities: {
                // Units
                uint32_t unitCount = buf.readU32();
                for (uint32_t i = 0; i < unitCount; ++i) {
                    PlayerId owner = buf.readU8();
                    UnitTypeId typeId{buf.readU16()};
                    hex::AxialCoord pos{buf.readI32(), buf.readI32()};
                    int32_t hp = buf.readI32();
                    int32_t mp = buf.readI32();
                    auto state = static_cast<aoc::sim::UnitState>(buf.readU8());

                    EntityId entity = world.createEntity();
                    aoc::sim::UnitComponent unit = aoc::sim::UnitComponent::create(owner, typeId, pos);
                    unit.hitPoints = hp;
                    unit.movementRemaining = mp;
                    unit.state = state;
                    world.addComponent<aoc::sim::UnitComponent>(entity, std::move(unit));
                }

                // Cities
                uint32_t cityCount = buf.readU32();
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

                    EntityId entity = world.createEntity();
                    aoc::sim::CityComponent city = aoc::sim::CityComponent::create(owner, loc, std::move(name));
                    city.population = pop;
                    city.foodSurplus = food;
                    city.productionProgress = prod;
                    city.workedTiles = std::move(worked);
                    world.addComponent<aoc::sim::CityComponent>(entity, std::move(city));
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
            default:
                // Unknown section: skip
                buf.skip(sectionSize);
                break;
        }
    }

    std::fprintf(stdout, "[Load] Game loaded from %s\n", filepath.c_str());
    return ErrorCode::Ok;
}

} // namespace aoc::save
