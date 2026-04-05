/**
 * @file Wonder.cpp
 * @brief World wonder definitions.
 */

#include "aoc/simulation/wonder/Wonder.hpp"

#include <cassert>

namespace aoc::sim {

// ============================================================================
// 12 wonder definitions
// ============================================================================

static const std::array<WonderDef, WONDER_COUNT> s_wonderDefs = {{
    // Ancient era (EraId 0)
    { 0, "Pyramids",        EraId{0}, 180, TechId{},
      {1.15f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f},
      "+15% builder production, +1 builder charge."},

    { 1, "Stonehenge",      EraId{0}, 150, TechId{},
      {1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 2.0f},
      "+2 faith per turn."},

    // Classical era (EraId 1)
    { 2, "Colosseum",       EraId{1}, 250, TechId{},
      {1.0f, 0.0f, 0.0f, 0.0f, 2.0f, 0.0f},
      "+2 amenities to all cities within 6 tiles."},

    { 3, "Great Library",   EraId{1}, 300, TechId{},
      {1.0f, 2.0f, 0.0f, 0.0f, 0.0f, 0.0f},
      "+2 science, boost ancient/classical techs."},

    { 4, "Petra",           EraId{1}, 280, TechId{},
      {1.0f, 0.0f, 0.0f, 2.0f, 0.0f, 0.0f},
      "+2 food/prod/gold on desert tiles in this city."},

    { 5, "Colossus",        EraId{1}, 260, TechId{},
      {1.0f, 0.0f, 0.0f, 3.0f, 0.0f, 0.0f},
      "+1 trade route, +3 gold."},

    // Medieval era (EraId 2)
    { 6, "Great Wall",      EraId{2}, 350, TechId{},
      {1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f},
      "+6 combat strength for defenders on your territory."},

    { 7, "Machu Picchu",    EraId{2}, 380, TechId{},
      {1.0f, 0.0f, 0.0f, 4.0f, 0.0f, 0.0f},
      "+4 gold from mountains adjacent to city."},

    // Renaissance era (EraId 3)
    { 8, "Forbidden City",  EraId{3}, 420, TechId{},
      {1.0f, 0.0f, 2.0f, 0.0f, 0.0f, 0.0f},
      "+1 wildcard policy slot, +2 culture."},

    // Industrial era (EraId 4)
    { 9, "Big Ben",         EraId{4}, 500, TechId{},
      {1.0f, 0.0f, 0.0f, 6.0f, 0.0f, 0.0f},
      "+6 gold, double gold on market buildings."},

    // Modern era (EraId 5)
    {10, "Eiffel Tower",    EraId{5}, 600, TechId{},
      {1.0f, 0.0f, 10.0f, 0.0f, 0.0f, 0.0f},
      "+10 culture, +2 tourism."},

    // Atomic era (EraId 6)
    {11, "Manhattan Project", EraId{6}, 700, TechId{},
      {1.0f, 5.0f, 0.0f, 0.0f, 0.0f, 0.0f},
      "Enables nuclear weapons. +5 science."},
}};

const std::array<WonderDef, WONDER_COUNT>& allWonderDefs() {
    return s_wonderDefs;
}

const WonderDef& wonderDef(WonderId id) {
    assert(id < WONDER_COUNT);
    return s_wonderDefs[id];
}

} // namespace aoc::sim
