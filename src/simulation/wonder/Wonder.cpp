/**
 * @file Wonder.cpp
 * @brief World wonder definitions.
 */

#include "aoc/simulation/wonder/Wonder.hpp"

#include <algorithm>
#include <cassert>
#include <cmath>

namespace aoc::sim {

// ============================================================================
// 12 wonder definitions
// ============================================================================

static const std::array<WonderDef, WONDER_COUNT> s_wonderDefs = {{
    // Ancient era (EraId 0)
    { 0, "Pyramids",        EraId{0}, 180, TechId{}, CivicId{}, {.requiresDesert=true},
      {1.15f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f},
      "+15% builder production, +1 builder charge."},

    { 1, "Stonehenge",      EraId{0}, 150, TechId{}, CivicId{}, {.requiresHill=true},
      {1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 2.0f},
      "+2 faith per turn."},

    // Classical era (EraId 1)
    { 2, "Colosseum",       EraId{1}, 250, TechId{}, CivicId{}, WonderAdjacencyReq{},
      {1.0f, 0.0f, 0.0f, 0.0f, 2.0f, 0.0f},
      "+2 amenities to all cities within 6 tiles."},

    { 3, "Great Library",   EraId{1}, 300, TechId{}, CivicId{}, WonderAdjacencyReq{},
      {1.0f, 2.0f, 0.0f, 0.0f, 0.0f, 0.0f},
      "+2 science, boost ancient/classical techs."},

    { 4, "Petra",           EraId{1}, 280, TechId{}, CivicId{}, {.requiresDesert=true},
      {1.0f, 0.0f, 0.0f, 2.0f, 0.0f, 0.0f},
      "+2 food/prod/gold on desert tiles in this city."},

    { 5, "Colossus",        EraId{1}, 260, TechId{}, CivicId{}, {.requiresCoast=true},
      {1.0f, 0.0f, 0.0f, 3.0f, 0.0f, 0.0f},
      "+1 trade route, +3 gold."},

    // Medieval era (EraId 2)
    { 6, "Great Wall",      EraId{2}, 350, TechId{}, CivicId{}, WonderAdjacencyReq{},
      {1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f},
      "+6 combat strength for defenders on your territory."},

    { 7, "Machu Picchu",    EraId{2}, 380, TechId{}, CivicId{}, {.requiresMountain=true},
      {1.0f, 0.0f, 0.0f, 4.0f, 0.0f, 0.0f},
      "+4 gold from mountains adjacent to city."},

    // Renaissance era (EraId 3)
    { 8, "Forbidden City",  EraId{3}, 420, TechId{}, CivicId{}, WonderAdjacencyReq{},
      {1.0f, 0.0f, 2.0f, 0.0f, 0.0f, 0.0f},
      "+1 wildcard policy slot, +2 culture."},

    // Industrial era (EraId 4)
    { 9, "Big Ben",         EraId{4}, 500, TechId{}, CivicId{}, WonderAdjacencyReq{},
      {1.0f, 0.0f, 0.0f, 6.0f, 0.0f, 0.0f},
      "+6 gold, double gold on market buildings."},

    // Modern era (EraId 5)
    {10, "Eiffel Tower",    EraId{5}, 600, TechId{}, CivicId{}, WonderAdjacencyReq{},
      {1.0f, 0.0f, 10.0f, 0.0f, 0.0f, 0.0f},
      "+10 culture, +2 tourism."},

    // Atomic era (EraId 6)
    {11, "Manhattan Project", EraId{6}, 700, TechId{}, CivicId{}, WonderAdjacencyReq{},
      {1.0f, 5.0f, 0.0f, 0.0f, 0.0f, 0.0f},
      "Enables nuclear weapons. +5 science."},

    // --- Batch B wonders ---

    // Ancient era (EraId 0)
    {12, "Hanging Gardens",  EraId{0}, 180, TechId{}, CivicId{}, {.requiresRiver=true},
      {1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f},
      "+2 food all cities."},

    {13, "Oracle",           EraId{0}, 160, TechId{}, CivicId{}, WonderAdjacencyReq{},
      {1.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f},
      "+1 culture all cities, free civic."},

    // Medieval era (EraId 2)
    {14, "Alhambra",         EraId{2}, 350, TechId{}, CivicId{}, WonderAdjacencyReq{},
      {1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f},
      "+1 military policy slot."},

    {15, "Chichen Itza",     EraId{2}, 380, TechId{}, CivicId{}, {.requiresJungle=true},
      {1.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f},
      "+1 culture, +1 production to jungle tiles."},

    // Renaissance era (EraId 3)
    {16, "Taj Mahal",        EraId{3}, 450, TechId{}, CivicId{}, WonderAdjacencyReq{},
      {1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f},
      "+1 era score for each golden age."},

    {17, "Venetian Arsenal", EraId{3}, 420, TechId{}, CivicId{}, {.requiresCoast=true},
      {1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f},
      "Double naval unit production."},

    // Industrial era (EraId 4)
    {18, "Ruhr Valley",      EraId{4}, 520, TechId{}, CivicId{}, WonderAdjacencyReq{},
      {1.3f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f},
      "+30% production in this city."},

    {19, "Oxford University", EraId{4}, 480, TechId{}, CivicId{}, WonderAdjacencyReq{},
      {1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f},
      "+20% science in this city."},

    // Modern era (EraId 5)
    {20, "Statue of Liberty", EraId{5}, 580, TechId{}, CivicId{}, WonderAdjacencyReq{},
      {1.0f, 0.0f, 0.0f, 0.0f, 4.0f, 0.0f},
      "+4 appeal, settlers produced 50% faster."},

    {21, "Broadway",          EraId{5}, 560, TechId{}, CivicId{}, WonderAdjacencyReq{},
      {1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f},
      "+20% culture in this city."},

    // Atomic era (EraId 6)
    {22, "Sydney Opera House", EraId{6}, 650, TechId{}, CivicId{}, WonderAdjacencyReq{},
      {1.0f, 0.0f, 5.0f, 3.0f, 0.0f, 0.0f},
      "+5 culture, +3 gold."},

    // Information era (EraId 7)
    {23, "International Space Station", EraId{7}, 750, TechId{}, CivicId{}, WonderAdjacencyReq{},
      {1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f},
      "+10% science all cities."},
}};

// One row per wonder, in id order, saying which kind of great person it draws.
// A table rather than a switch in the accumulator, so a new wonder is one line
// here beside its definition instead of an edit in another subsystem.
static constexpr std::array<GreatPersonType, WONDER_COUNT> WONDER_GREAT_PERSON = {{
    GreatPersonType::Engineer,   //  0 Pyramids: the archetypal feat of building
    GreatPersonType::Prophet,    //  1 Stonehenge
    GreatPersonType::Artist,     //  2 Colosseum
    GreatPersonType::Writer,     //  3 Great Library
    GreatPersonType::Merchant,   //  4 Petra
    GreatPersonType::Admiral,    //  5 Colossus
    GreatPersonType::General,    //  6 Great Wall
    GreatPersonType::Merchant,   //  7 Machu Picchu
    GreatPersonType::Merchant,   //  8 Forbidden City
    GreatPersonType::Merchant,   //  9 Big Ben
    GreatPersonType::Engineer,   // 10 Eiffel Tower
    GreatPersonType::Scientist,  // 11 Manhattan Project
    GreatPersonType::Engineer,   // 12 Hanging Gardens
    GreatPersonType::Prophet,    // 13 Oracle
    GreatPersonType::General,    // 14 Alhambra
    GreatPersonType::Artist,     // 15 Chichen Itza
    GreatPersonType::Artist,     // 16 Taj Mahal
    GreatPersonType::Admiral,    // 17 Venetian Arsenal
    GreatPersonType::Engineer,   // 18 Ruhr Valley
    GreatPersonType::Scientist,  // 19 Oxford University
    GreatPersonType::Writer,     // 20 Statue of Liberty
    GreatPersonType::Musician,   // 21 Broadway
    GreatPersonType::Musician,   // 22 Sydney Opera House
    GreatPersonType::Scientist,  // 23 International Space Station
}};

GreatPersonType greatPersonForWonder(WonderId id) {
    const std::size_t idx = static_cast<std::size_t>(id);
    if (idx >= WONDER_GREAT_PERSON.size()) {
        return GreatPersonType::Engineer;
    }
    return WONDER_GREAT_PERSON[idx];
}

const std::array<WonderDef, WONDER_COUNT>& allWonderDefs() {
    return s_wonderDefs;
}

const WonderDef& wonderDef(WonderId id) {
    assert(id < WONDER_COUNT);
    return s_wonderDefs[id];
}

float wonderEraDecayFactor(const WonderDef& wdef, EraId currentEra) {
    const int32_t delta = std::max(0,
        static_cast<int32_t>(currentEra.value) - static_cast<int32_t>(wdef.era.value));
    if (delta == 0) {
        return 1.0f;
    }
    // Full obsolescence after 4 eras gap (e.g. Ancient wonder in Atomic era):
    // wonder yields drop to 0. Models technology + culture moving on.
    if (delta >= 4) {
        return 0.0f;
    }
    const float factor = std::pow(0.85f, static_cast<float>(delta));
    return std::max(0.35f, factor);
}

} // namespace aoc::sim
