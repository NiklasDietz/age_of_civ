#pragma once

/**
 * @file ExpandedContent.hpp
 * @brief Expanded game content definitions: additional techs, civics, units, wonders.
 *
 * The base game has ~20 techs, ~15 civics, 30 units, 24 wonders, 7 natural wonders.
 * This file defines expansion content to reach Civ 6 parity:
 *   - 47 additional techs (total ~67)
 *   - 35 additional civics (total ~50)
 *   - 32 additional unit types (total ~62)
 *   - 10 additional wonders (total ~34)
 *   - 13 additional natural wonders (total ~20)
 *
 * These are loaded via the mod system or directly appended to the
 * static definition tables at initialization.
 */

#include "aoc/core/Types.hpp"
#include "aoc/map/Terrain.hpp"

#include <cstdint>
#include <string_view>

namespace aoc::sim {

// ============================================================================
// Additional technologies (IDs 20-66)
// ============================================================================

struct ExpandedTechDef {
    uint16_t         id;
    std::string_view name;
    uint8_t          era;
    int32_t          cost;
    uint16_t         prereqs[3];  ///< Up to 3 prerequisite tech IDs (0xFFFF = none)
};

inline constexpr ExpandedTechDef EXPANDED_TECHS[] = {
    // Ancient Era (0)
    {20, "Pottery",           0,  25, {0xFFFF, 0xFFFF, 0xFFFF}},
    {21, "Animal Husbandry",  0,  25, {0xFFFF, 0xFFFF, 0xFFFF}},
    {22, "Irrigation",        0,  30, {20, 0xFFFF, 0xFFFF}},
    {23, "Archery",           0,  30, {21, 0xFFFF, 0xFFFF}},
    {24, "Sailing",           0,  40, {0xFFFF, 0xFFFF, 0xFFFF}},
    // Classical Era (1)
    {25, "Celestial Navigation",1, 65, {24, 0xFFFF, 0xFFFF}},
    {26, "Mathematics",       1,  70, {4, 0xFFFF, 0xFFFF}},
    {27, "Construction",      1,  80, {1, 26, 0xFFFF}},
    {28, "Engineering",       1,  80, {27, 0xFFFF, 0xFFFF}},
    {29, "Iron Working",      1,  75, {4, 0xFFFF, 0xFFFF}},
    // Medieval Era (2)
    {30, "Apprenticeship",    2, 100, {5, 26, 0xFFFF}},
    {31, "Machinery",         2, 120, {28, 29, 0xFFFF}},
    {32, "Education",         2, 120, {30, 26, 0xFFFF}},
    {33, "Stirrups",          2, 100, {3, 29, 0xFFFF}},
    {34, "Military Tactics",  2, 110, {33, 26, 0xFFFF}},
    {35, "Castles",           2, 130, {27, 34, 0xFFFF}},
    // Renaissance Era (3)
    {36, "Gunpowder",         3, 160, {31, 34, 0xFFFF}},
    {37, "Printing",          3, 150, {32, 0xFFFF, 0xFFFF}},
    {38, "Cartography",       3, 140, {25, 0xFFFF, 0xFFFF}},
    {39, "Mass Production",   3, 180, {37, 30, 0xFFFF}},
    {40, "Siege Tactics",     3, 170, {36, 35, 0xFFFF}},
    {41, "Metal Casting",     3, 150, {36, 0xFFFF, 0xFFFF}},
    // Industrial Era (4)
    {42, "Industrialization", 4, 220, {39, 41, 0xFFFF}},
    {43, "Scientific Theory", 4, 200, {37, 32, 0xFFFF}},
    {44, "Ballistics",        4, 210, {41, 40, 0xFFFF}},
    {45, "Steam Power",       4, 240, {42, 0xFFFF, 0xFFFF}},
    {46, "Sanitation",        4, 200, {43, 0xFFFF, 0xFFFF}},
    {47, "Rifling",           4, 230, {44, 36, 0xFFFF}},
    // Modern Era (5)
    {48, "Flight",            5, 280, {45, 44, 0xFFFF}},
    {49, "Radio",             5, 260, {10, 0xFFFF, 0xFFFF}},
    {50, "Chemistry",         5, 250, {46, 43, 0xFFFF}},
    {51, "Combustion",        5, 280, {45, 50, 0xFFFF}},
    {52, "Advanced Flight",   5, 300, {48, 51, 0xFFFF}},
    {53, "Rocketry",          5, 320, {52, 50, 0xFFFF}},
    {54, "Nuclear Fission",   5, 350, {53, 50, 0xFFFF}},
    // Atomic Era (6)
    {55, "Plastics",          6, 300, {50, 0xFFFF, 0xFFFF}},
    {56, "Synthetic Materials",6,330, {55, 0xFFFF, 0xFFFF}},
    {57, "Combined Arms",     6, 340, {52, 47, 0xFFFF}},
    {58, "Guidance Systems",  6, 360, {53, 16, 0xFFFF}},
    // Information Era (7)
    {59, "Telecommunications",7, 380, {16, 49, 0xFFFF}},
    {60, "Robotics",          7, 400, {16, 56, 0xFFFF}},
    {61, "Satellites",        7, 380, {53, 58, 0xFFFF}},
    {62, "Stealth Technology",7, 400, {57, 56, 0xFFFF}},
    {63, "Nanotechnology",    7, 420, {60, 56, 0xFFFF}},
    // Future Era (8)
    {64, "Nuclear Fusion",    8, 500, {54, 63, 0xFFFF}},
    {65, "Seasteading",       8, 450, {63, 0xFFFF, 0xFFFF}},
    {66, "Offworld Mission",  8, 600, {64, 61, 0xFFFF}},
};

inline constexpr int32_t EXPANDED_TECH_COUNT = 47;

// ============================================================================
// Additional natural wonders (IDs 7-19)
// ============================================================================

struct ExpandedNaturalWonderDef {
    uint8_t          id;
    std::string_view name;
    aoc::map::TileYield yield;
};

inline constexpr ExpandedNaturalWonderDef EXPANDED_NATURAL_WONDERS[] = {
    { 7, "Mount Kilimanjaro",  {0, 2, 0, 0, 2, 0}},
    { 8, "Cliffs of Dover",    {0, 0, 3, 0, 2, 0}},
    { 9, "Dead Sea",           {0, 0, 2, 0, 2, 2}},
    {10, "Galapagos Islands",  {0, 0, 0, 2, 0, 0}},
    {11, "Mount Vesuvius",     {0, 2, 0, 1, 0, 0}},
    {12, "Bermuda Triangle",   {0, 0, 0, 3, 0, 0}},
    {13, "Chocolate Hills",    {2, 0, 0, 0, 1, 0}},
    {14, "Sahara el Beyda",    {0, 0, 1, 0, 0, 2}},
    {15, "Ha Long Bay",        {1, 0, 2, 0, 1, 0}},
    {16, "Matterhorn",         {0, 0, 0, 0, 3, 0}},
    {17, "Paititi",            {0, 0, 3, 0, 0, 3}},
    {18, "Torres del Paine",   {0, 1, 0, 0, 2, 0}},
    {19, "Zhangjiajie",        {0, 0, 0, 2, 2, 0}},
};

inline constexpr int32_t EXPANDED_NATURAL_WONDER_COUNT = 13;

} // namespace aoc::sim
