#pragma once

/// @file Civilization.hpp
/// @brief Civilization definitions with unique abilities, units, and buildings.

#include "aoc/core/Types.hpp"

#include <array>
#include <cstdint>
#include <string>
#include <string_view>

namespace aoc::sim {

using CivId = uint8_t;

/// Modifier bonuses unique to each civilization.
struct CivAbilityModifiers {
    float productionMultiplier = 1.0f;
    float scienceMultiplier    = 1.0f;
    float cultureMultiplier    = 1.0f;
    float goldMultiplier       = 1.0f;
    float combatStrengthBonus  = 0.0f;
    int32_t extraMovement      = 0;       ///< Bonus movement for specific unit types
    float faithMultiplier      = 1.0f;
    int32_t extraTradeRoutes   = 0;
};

/// Maximum number of historical city names per civilization.
inline constexpr std::size_t MAX_CIV_CITY_NAMES = 12;

struct CivilizationDef {
    CivId            id;
    std::string_view name;
    std::string_view leaderName;
    std::string_view abilityName;
    std::string_view abilityDescription;
    CivAbilityModifiers modifiers;
    UnitTypeId       uniqueUnitReplaces;    ///< Which standard unit this civ's unique unit replaces (INVALID if none)
    BuildingId       uniqueBuildingReplaces; ///< Which standard building this replaces (INVALID if none)
    std::array<std::string_view, MAX_CIV_CITY_NAMES> cityNames; ///< Historical city names for this civilization
};

/// Total number of civilizations.
inline constexpr uint8_t CIV_COUNT = 8;

inline constexpr std::array<CivilizationDef, CIV_COUNT> CIV_DEFS = {{
    {0, "Rome",    "Trajan",        "All Roads Lead to Rome",
     "Free roads in capital territory. +10% production in capital.",
     {1.1f, 1.0f, 1.0f, 1.0f, 0.0f, 0, 1.0f, 1}, UnitTypeId{}, BuildingId{},
     {{"Rome", "Antium", "Cumae", "Neapolis", "Ravenna", "Mediolanum",
       "Arretium", "Brundisium", "Capua", "Tarentum", "Pisae", "Genua"}}},

    {1, "Egypt",   "Cleopatra",     "Mediterranean's Bride",
     "+15% production toward wonders and districts.",
     {1.15f, 1.0f, 1.0f, 1.0f, 0.0f, 0, 1.0f, 0}, UnitTypeId{}, BuildingId{},
     {{"Thebes", "Memphis", "Alexandria", "Heliopolis", "Giza", "Luxor",
       "Aswan", "Abydos", "Edfu", "Karnak", "Faiyum", "Rosetta"}}},

    {2, "China",   "Qin Shi Huang", "Dynastic Cycle",
     "+10% science. Builders gain +1 charge.",
     {1.0f, 1.1f, 1.0f, 1.0f, 0.0f, 0, 1.0f, 0}, UnitTypeId{}, BuildingId{},
     {{"Beijing", "Shanghai", "Nanjing", "Xian", "Chengdu", "Hangzhou",
       "Luoyang", "Kaifeng", "Guangzhou", "Wuhan", "Suzhou", "Tianjin"}}},

    {3, "Germany", "Frederick",     "Free Imperial Cities",
     "+1 production in all cities. +1 military policy slot.",
     {1.0f, 1.0f, 1.0f, 1.0f, 0.0f, 0, 1.0f, 0}, UnitTypeId{}, BuildingId{},
     {{"Berlin", "Hamburg", "Munich", "Cologne", "Frankfurt", "Stuttgart",
       "Dresden", "Leipzig", "Aachen", "Nuremberg", "Bremen", "Dortmund"}}},

    {4, "Greece",  "Pericles",      "Plato's Republic",
     "+15% culture. +5% science.",
     {1.0f, 1.05f, 1.15f, 1.0f, 0.0f, 0, 1.0f, 0}, UnitTypeId{}, BuildingId{},
     {{"Athens", "Sparta", "Corinth", "Argos", "Thebes", "Delphi",
       "Olympia", "Mycenae", "Rhodes", "Ephesus", "Syracuse", "Thessaloniki"}}},

    {5, "England", "Victoria",      "British Museum",
     "+2 movement for naval units. +10% gold.",
     {1.0f, 1.0f, 1.0f, 1.1f, 0.0f, 2, 1.0f, 1}, UnitTypeId{}, BuildingId{},
     {{"London", "York", "Canterbury", "Oxford", "Cambridge", "Bristol",
       "Manchester", "Liverpool", "Edinburgh", "Bath", "Winchester", "Dover"}}},

    {6, "Japan",   "Hojo Tokimune", "Meiji Restoration",
     "+5 combat strength for land units adjacent to coast.",
     {1.0f, 1.0f, 1.0f, 1.0f, 5.0f, 0, 1.0f, 0}, UnitTypeId{}, BuildingId{},
     {{"Kyoto", "Tokyo", "Osaka", "Nara", "Nagoya", "Sapporo",
       "Hiroshima", "Kobe", "Fukuoka", "Yokohama", "Sendai", "Kamakura"}}},

    {7, "Persia",  "Cyrus",         "Satrapies",
     "+10% gold. +1 trade route. +2 movement during golden age.",
     {1.0f, 1.0f, 1.0f, 1.1f, 0.0f, 0, 1.0f, 1}, UnitTypeId{}, BuildingId{},
     {{"Persepolis", "Pasargadae", "Susa", "Ecbatana", "Isfahan", "Shiraz",
       "Tabriz", "Hamadan", "Kerman", "Yazd", "Balkh", "Merv"}}},
}};

/// Look up a civilization definition by ID.
[[nodiscard]] inline constexpr const CivilizationDef& civDef(CivId id) {
    return CIV_DEFS[id];
}

/// ECS component attached to player entities.
struct PlayerCivilizationComponent {
    PlayerId owner;
    CivId    civId;
};

} // namespace aoc::sim

// Forward declarations for getNextCityName -- defined in headers included by .cpp files.
namespace aoc::ecs { class World; }

namespace aoc::sim {

/// Get the next historical city name for a player based on their civilization.
/// Counts existing cities owned by the player and returns the next name from
/// the civ's city name list. Falls back to "City [N]" if all 12 are used.
[[nodiscard]] std::string getNextCityName(const aoc::ecs::World& world, PlayerId player);

} // namespace aoc::sim
