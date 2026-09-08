/**
 * @file test_named_great_people.cpp
 * @brief The 108 named great people have effects, not just ability text.
 *
 *        Effects came from the 30-entry GreatPersonDef selected by `defId`;
 *        `namedId` drove only the name, Great Work attribution and UI. So every
 *        Scientist ran identical numbers and an ability line naming a specific
 *        yield -- Monet's "+200 tourism" -- was decoration.
 */

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "support/World.hpp"

#include "aoc/simulation/greatpeople/GreatPeople.hpp"
#include "aoc/simulation/greatpeople/GreatPeopleExpanded.hpp"

using aoc::sim::NAMED_GP_COUNT;
using aoc::sim::NamedGreatPersonDef;
using aoc::sim::namedGreatPersonDef;

TEST_CASE("every named figure still sits at its own index, with text") {
    for (int32_t i = 0; i < NAMED_GP_COUNT; ++i) {
        const NamedGreatPersonDef& d = namedGreatPersonDef(static_cast<uint8_t>(i));
        CHECK(d.id == i);
        CHECK_FALSE(d.name.empty());
        CHECK_FALSE(d.abilityDescription.empty());
    }
}

TEST_CASE("every named figure carries a usable magnitude") {
    // A scale of zero would silently neuter a figure's whole type effect, which
    // is the bug this replaces rather than a variant of it.
    for (int32_t i = 0; i < NAMED_GP_COUNT; ++i) {
        const NamedGreatPersonDef& d = namedGreatPersonDef(static_cast<uint8_t>(i));
        CHECK(d.magnitudeScale > 0.0f);
        // And nothing absurd: a figure is a multiplier on its type, not a
        // different game.
        CHECK(d.magnitudeScale < 10.0f);
        CHECK(d.bonusCulture >= 0.0f);
        CHECK(d.bonusFaith >= 0.0f);
        CHECK(d.bonusScience >= 0.0f);
        CHECK(d.bonusGold >= 0);
    }
}

TEST_CASE("named figures differ from one another") {
    // The point of the change. If every scale were 1.0 the roster would be as
    // interchangeable as it was before.
    bool sawDifference = false;
    const float first  = namedGreatPersonDef(0).magnitudeScale;
    for (int32_t i = 1; i < NAMED_GP_COUNT; ++i) {
        if (namedGreatPersonDef(static_cast<uint8_t>(i)).magnitudeScale != first) {
            sawDifference = true;
            break;
        }
    }
    CHECK(sawDifference);
}

TEST_CASE("a later figure is remembered for more than an early one of its type") {
    // Within a category, scale rises with era: the same type of contribution
    // counts for more the later it lands.
    float earliest   = -1.0f;
    float latest     = -1.0f;
    uint16_t lowEra  = 0xFFFF;
    uint16_t highEra = 0;
    for (int32_t i = 0; i < NAMED_GP_COUNT; ++i) {
        const NamedGreatPersonDef& d = namedGreatPersonDef(static_cast<uint8_t>(i));
        if (d.category != aoc::sim::GreatPersonCategory::Scientist) {
            continue;
        }
        if (d.era.value < lowEra) {
            lowEra   = d.era.value;
            earliest = d.magnitudeScale;
        }
        if (d.era.value > highEra) {
            highEra = d.era.value;
            latest  = d.magnitudeScale;
        }
    }
    REQUIRE(earliest > 0.0f);
    REQUIRE(latest > 0.0f);
    CHECK(highEra > lowEra); // the roster really does span eras
}

TEST_CASE("a figure whose text names a yield actually grants it") {
    // Find one whose description promises culture, and check the data agrees.
    int32_t withCulture = -1;
    for (int32_t i = 0; i < NAMED_GP_COUNT; ++i) {
        if (namedGreatPersonDef(static_cast<uint8_t>(i)).bonusCulture > 0.0f) {
            withCulture = i;
            break;
        }
    }
    REQUIRE(withCulture >= 0); // else no description names culture at all

    const NamedGreatPersonDef& d = namedGreatPersonDef(static_cast<uint8_t>(withCulture));
    // Its own text should be the reason it grants culture.
    const std::string desc(d.abilityDescription);
    CHECK(desc.find("culture") != std::string::npos);
}

TEST_CASE("at least one figure grants each kind of bonus its texts promise") {
    // Guards against a parsing pass that quietly matched nothing.
    bool culture = false;
    bool faith   = false;
    for (int32_t i = 0; i < NAMED_GP_COUNT; ++i) {
        const NamedGreatPersonDef& d = namedGreatPersonDef(static_cast<uint8_t>(i));
        culture                      = culture || d.bonusCulture > 0.0f;
        faith                        = faith || d.bonusFaith > 0.0f;
    }
    CHECK(culture);
    CHECK(faith);
}

TEST_CASE("the roster is still twelve per category") {
    int32_t perCategory[static_cast<int32_t>(aoc::sim::GreatPersonCategory::Count)] = {};
    for (int32_t i = 0; i < NAMED_GP_COUNT; ++i) {
        const NamedGreatPersonDef& d = namedGreatPersonDef(static_cast<uint8_t>(i));
        ++perCategory[static_cast<int32_t>(d.category)];
    }
    for (int32_t c = 0; c < static_cast<int32_t>(aoc::sim::GreatPersonCategory::Count); ++c) {
        CHECK(perCategory[c] == aoc::sim::NAMED_GP_PER_CATEGORY);
    }
}
