#pragma once

/**
 * @file Serializer.hpp
 * @brief Versioned binary save/load for the complete game state.
 *
 * Save format:
 *   [Header]  magic(4) + version(4) + flags(4) + dataSize(4)
 *   [Sections] each section: sectionId(2) + sectionSize(4) + data(...)
 *
 * Sections are self-describing by size so unknown sections (from newer
 * versions) can be skipped. This provides forward compatibility.
 *
 * All numeric values are little-endian. Strings are length-prefixed (uint16).
 */

#include "aoc/core/ErrorCodes.hpp"
#include "aoc/core/Types.hpp"
#include "aoc/core/Random.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace aoc::game {
class GameState;
}

namespace aoc::map {
class HexGrid;
class FogOfWar;
}

namespace aoc::sim {
class TurnManager;
class EconomySimulation;
class DiplomacyManager;
}

namespace aoc::save {

/// File format magic bytes: "AOC\0"
inline constexpr uint32_t SAVE_MAGIC   = 0x00434F41;
inline constexpr uint32_t SAVE_VERSION = 7;

/// Section IDs for each chunk of game state.
enum class SectionId : uint16_t {
    MapGrid          = 0x0001,
    Entities         = 0x0002,
    TurnState        = 0x0003,
    Diplomacy        = 0x0004,
    Market           = 0x0005,
    FogOfWar         = 0x0006,
    RandomState      = 0x0007,
    Improvements     = 0x0008,  ///< Tile improvements + roads
    TechProgress     = 0x0009,  ///< Per-player tech/civic state
    ProductionQueues = 0x000A,  ///< City production queues
    Districts        = 0x000B,  ///< City districts and buildings
    MonetaryState    = 0x000C,  ///< Per-player monetary state
    GovernmentState  = 0x000D,  ///< Per-player government + policies
    VictoryState     = 0x000E,  ///< Victory trackers
    Stockpiles       = 0x000F,  ///< City stockpiles
    PlayerState      = 0x0010,  ///< Per-player civ, era, economy, great people, eureka
    WonderState      = 0x0012,  ///< Global wonder tracker + per-city wonders
    MiscEntities     = 0x0013,  ///< Barbarians, great people, spies, unit experience
    CurrencyTrust    = 0x0014,  ///< Per-player fiat currency trust state
    CrisisState      = 0x0015,  ///< Per-player currency crisis state
    BondState        = 0x0016,  ///< Per-player bond portfolios
    DevaluationState = 0x0017,  ///< Per-player devaluation state
    HoardState       = 0x0018,  ///< Per-player commodity hoards
    ProductionExp    = 0x0019,  ///< Per-city recipe experience
    BuildingLevels   = 0x001A,  ///< Per-city building upgrade levels
    QualityState     = 0x001B,  ///< Per-city good quality breakdowns
    PollutionState   = 0x001C,  ///< Per-city waste/pollution
    AutomationState  = 0x001D,  ///< Per-city robot worker state
    IndustrialState  = 0x001E,  ///< Per-player industrial revolution progress
    // v7 sections ---------------------------------------------------------
    PrestigeState    = 0x001F,  ///< Per-player prestige axes (science/culture/etc.)
    TourismState     = 0x0020,  ///< Per-player tourism accumulation + tourist counts
    SpaceRaceState   = 0x0021,  ///< Per-player space project progress/completion
    GrievanceState   = 0x0022,  ///< Per-player grievance list
    WarWearinessState = 0x0023, ///< Per-player war weariness + turns-at-war map
    StockPortfolioState = 0x0024, ///< Per-player equity investments (inward + outward)
};

/// Low-level binary write buffer.
class WriteBuffer {
public:
    void writeU8(uint8_t v);
    void writeU16(uint16_t v);
    void writeU32(uint32_t v);
    void writeU64(uint64_t v);
    void writeI32(int32_t v);
    void writeI64(int64_t v);
    void writeF32(float v);
    void writeString(std::string_view str);
    void writeBytes(const void* data, std::size_t size);

    [[nodiscard]] const std::vector<uint8_t>& data() const { return this->m_data; }
    [[nodiscard]] std::size_t size() const { return this->m_data.size(); }

private:
    std::vector<uint8_t> m_data;
};

/// Low-level binary read cursor.
class ReadBuffer {
public:
    explicit ReadBuffer(const std::vector<uint8_t>& data);

    [[nodiscard]] uint8_t readU8();
    [[nodiscard]] uint16_t readU16();
    [[nodiscard]] uint32_t readU32();
    [[nodiscard]] uint64_t readU64();
    [[nodiscard]] int32_t readI32();
    [[nodiscard]] int64_t readI64();
    [[nodiscard]] float readF32();
    [[nodiscard]] std::string readString();
    void readBytes(void* dst, std::size_t size);
    void skip(std::size_t bytes);

    [[nodiscard]] bool hasRemaining(std::size_t bytes) const;
    [[nodiscard]] std::size_t remaining() const;

private:
    const std::vector<uint8_t>& m_data;
    std::size_t m_offset = 0;
};

/**
 * @brief Save the complete game state to a file.
 *
 * @param filepath    Output file path.
 * @param gameState   Game state with all player/city/unit data.
 * @param grid        Hex map data.
 * @param turnManager Turn state.
 * @param economy     Market prices and production chain state.
 * @param diplomacy   Pairwise relations.
 * @param fogOfWar    Per-player visibility.
 * @param rng         Game PRNG state (for determinism).
 * @return Ok on success, SaveFailed on I/O error.
 */
[[nodiscard]] ErrorCode saveGame(
    const std::string& filepath,
    const aoc::game::GameState& gameState,
    const aoc::map::HexGrid& grid,
    const aoc::sim::TurnManager& turnManager,
    const aoc::sim::EconomySimulation& economy,
    const aoc::sim::DiplomacyManager& diplomacy,
    const aoc::map::FogOfWar& fogOfWar,
    const aoc::Random& rng);

/**
 * @brief Load a complete game state from a file.
 *
 * @return Ok on success, LoadFailed on I/O error, SaveVersionMismatch
 *         on version incompatibility, SaveCorrupted on data integrity failure.
 */
[[nodiscard]] ErrorCode loadGame(
    const std::string& filepath,
    aoc::game::GameState& gameState,
    aoc::map::HexGrid& grid,
    aoc::sim::TurnManager& turnManager,
    aoc::sim::EconomySimulation& economy,
    aoc::sim::DiplomacyManager& diplomacy,
    aoc::map::FogOfWar& fogOfWar,
    aoc::Random& rng);

} // namespace aoc::save
