/**
 * @file test_tax_rate_clamp.cpp
 * @brief One ceiling on the tax rate. The UI clamped at 1.0, setTaxRate at
 *        0.60, the AI at 0.50 and 0.40, and the bankruptcy branch at 0.40,
 *        so what "maximum tax" meant depended on who last touched the field.
 */

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "aoc/simulation/monetary/FiscalPolicy.hpp"
#include "aoc/simulation/monetary/MonetarySystem.hpp"

TEST_CASE("the tax rate is clamped to the one ceiling") {
    aoc::sim::MonetaryStateComponent s{};
    aoc::sim::setTaxRate(s, 1.0f);
    CHECK(s.taxRate == doctest::Approx(aoc::sim::MAX_TAX_RATE));
    CHECK(aoc::sim::MAX_TAX_RATE == doctest::Approx(0.60f));
    aoc::sim::setTaxRate(s, -0.1f);
    CHECK(s.taxRate == doctest::Approx(0.0f));
    aoc::sim::setTaxRate(s, 0.25f);
    CHECK(s.taxRate == doctest::Approx(0.25f));
}
