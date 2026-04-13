#pragma once

/**
 * @file CasusBelli.hpp
 * @brief War justification types that modify diplomatic penalties.
 *
 * Before declaring war, a player may claim a Casus Belli (cause for war).
 * Having a valid justification reduces the grievance penalty with other civs.
 * Without justification, a "Surprise War" carries maximum penalty.
 *
 * Each type has prerequisites and a grievance multiplier (1.0 = full penalty,
 * 0.0 = no penalty). Prerequisites checked against the game state.
 */

#include "aoc/core/Types.hpp"

#include <cstdint>
#include <string_view>
#include <array>

namespace aoc::sim {

enum class CasusBelliType : uint8_t {
    SurpriseWar,       ///< No justification. Maximum grievance penalty.
    FormalWar,         ///< Denounced the target first. Reduced penalty.
    HolyWar,           ///< Target converted one of your cities. Half penalty.
    LiberationWar,     ///< Recapturing an ally's conquered city. No penalty.
    ReconquestWar,     ///< Recapturing your own conquered city. No penalty.
    ColonialWar,       ///< Target is 2+ eras behind in tech. Reduced penalty.
    EconomicWar,       ///< Response to embargo/sanctions against you. Reduced penalty.
    ProtectorateWar,   ///< Defending a city-state you are suzerain of. No penalty.

    Count
};

inline constexpr int32_t CASUS_BELLI_COUNT = static_cast<int32_t>(CasusBelliType::Count);

struct CasusBelliDef {
    CasusBelliType   type;
    std::string_view name;
    std::string_view description;
    float            grievanceMultiplier;  ///< 0.0 = no penalty, 1.0 = full penalty
    int32_t          minRelationScore;     ///< Must have relations below this to use (-999 = always)
};

inline constexpr std::array<CasusBelliDef, CASUS_BELLI_COUNT> CASUS_BELLI_DEFS = {{
    {CasusBelliType::SurpriseWar,    "Surprise War",     "No justification",           1.0f,  -999},
    {CasusBelliType::FormalWar,      "Formal War",       "Denounced the target",       0.75f, -20},
    {CasusBelliType::HolyWar,        "Holy War",         "Religious conversion",       0.50f, -999},
    {CasusBelliType::LiberationWar,  "Liberation War",   "Free an ally's city",        0.0f,  -999},
    {CasusBelliType::ReconquestWar,  "Reconquest War",   "Recapture your own city",    0.0f,  -999},
    {CasusBelliType::ColonialWar,    "Colonial War",     "Target 2+ eras behind",      0.50f, -999},
    {CasusBelliType::EconomicWar,    "Economic War",     "Response to embargo",        0.50f, -999},
    {CasusBelliType::ProtectorateWar,"Protectorate War", "Defend your city-state",     0.0f,  -999},
}};

[[nodiscard]] inline const CasusBelliDef& casusBelliDef(CasusBelliType type) {
    return CASUS_BELLI_DEFS[static_cast<std::size_t>(type)];
}

} // namespace aoc::sim
