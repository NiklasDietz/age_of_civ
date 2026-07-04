/**
 * @file test_save_finite.cpp
 * @brief Pins float sanitization in the save Serializer (aoc::save).
 *
 * WriteBuffer::writeF32 used to persist whatever float it was handed, so a NaN
 * or infinity that crept into a component (a divide-by-zero, an uninitialised
 * field) would survive the save round-trip and poison the loaded sim -- the
 * non-finite value spreads through every later comparison and arithmetic op.
 * writeF32 now coerces non-finite values to 0. These cases pin that coercion
 * and that finite values still round-trip byte-exact.
 */

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "aoc/save/Serializer.hpp"

#include <cmath>
#include <limits>

namespace {

/// Round-trip one float through the low-level buffers.
float roundTrip(float v) {
    aoc::save::WriteBuffer wb;
    wb.writeF32(v);
    aoc::save::ReadBuffer rb(wb.data());
    return rb.readF32();
}

} // namespace

TEST_CASE("writeF32 coerces non-finite floats to zero on the way out") {
    CHECK(roundTrip(std::numeric_limits<float>::quiet_NaN()) == 0.0f);
    CHECK(roundTrip(std::numeric_limits<float>::signaling_NaN()) == 0.0f);
    CHECK(roundTrip(std::numeric_limits<float>::infinity()) == 0.0f);
    CHECK(roundTrip(-std::numeric_limits<float>::infinity()) == 0.0f);
}

TEST_CASE("writeF32 leaves finite floats byte-exact through the round-trip") {
    CHECK(roundTrip(0.0f) == 0.0f);
    // -0.0f must survive with its sign bit intact -- `== -0.0f` would also
    // accept +0.0f, so assert the sign bit directly.
    CHECK(std::signbit(roundTrip(-0.0f)));
    CHECK(roundTrip(-0.0f) == 0.0f);
    CHECK(roundTrip(1.0f) == 1.0f);
    CHECK(roundTrip(-273.15f) == -273.15f);
    CHECK(roundTrip(3.1415927f) == 3.1415927f);
    CHECK(roundTrip(std::numeric_limits<float>::max()) == std::numeric_limits<float>::max());
    CHECK(roundTrip(std::numeric_limits<float>::lowest())
          == std::numeric_limits<float>::lowest());
    CHECK(roundTrip(std::numeric_limits<float>::denorm_min())
          == std::numeric_limits<float>::denorm_min());
}

TEST_CASE("a struct's worth of floats sanitizes only the non-finite fields") {
    // Interleave finite and non-finite writes as a real component snapshot
    // would, then confirm each field reads back correctly.
    aoc::save::WriteBuffer wb;
    wb.writeF32(1.5f);
    wb.writeF32(std::numeric_limits<float>::infinity());
    wb.writeF32(-2.25f);
    wb.writeF32(std::numeric_limits<float>::quiet_NaN());

    aoc::save::ReadBuffer rb(wb.data());
    CHECK(rb.readF32() == 1.5f);
    CHECK(rb.readF32() == 0.0f);   // was +Inf
    CHECK(rb.readF32() == -2.25f);
    CHECK(rb.readF32() == 0.0f);   // was NaN
    CHECK(rb.isCorrupt() == false);
}
