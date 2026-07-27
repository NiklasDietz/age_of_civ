/**
 * @file test_mapgen_regenerate.cpp
 * @brief Generating a seed into a REUSED HexGrid must equal generating it into a
 *        fresh one.
 *
 * Why this test is shaped this way. `HexGrid` carries about 160 layers and
 * `initialize()` used to reset 20 of them by hand, so ~140 derived layers
 * survived a re-initialize. Worse than merely stale: generation only sizes a
 * layer when the pass that produces it runs, so on a second generate those
 * layers still held THE PREVIOUS MAP'S VALUES, and any consumer reading one
 * before its producer ran picked them up. The Continent Creator swaps and
 * regenerates into the same grids on every scrub tick, so it was already
 * non-reproducible.
 *
 * The trap the worldgen plan calls out explicitly: a determinism test built on
 * two FRESH grids passes while all of that is broken, because a fresh grid has
 * no previous map to inherit. The only version of this test that can fail is one
 * that generates a DIFFERENT map into the grid first and then regenerates the
 * map under test into that same object. That is what the reuse case below does.
 */

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "aoc/map/HexGrid.hpp"
#include "aoc/map/MapGenerator.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace {

/// Small but real: the pipeline runs end to end, including the post-sim geology
/// and biogeography passes where the stale-layer reads lived.
aoc::map::MapGenerator::Config configFor(uint64_t seed) {
    aoc::map::MapGenerator::Config c;
    c.width           = 60;
    c.height          = 40;
    c.seed            = seed;
    c.tectonicTotalMy = 500; // keep the test seconds, not minutes
    return c;
}

/// A content fingerprint spanning the whole pipeline: the player-visible layers,
/// plus the derived geology and climate layers that the read-before-write bug
/// actually corrupted (rockType / crustalThickness / geothermalGradient feed the
/// metamorphic-facies rule, and lakeFlag feeds five passes).
std::string fingerprint(const aoc::map::HexGrid& g) {
    std::string out;
    out.reserve(static_cast<std::size_t>(g.tileCount()) * 8u);

    auto addU = [&out](uint64_t v) {
        for (int32_t b = 0; b < 8; ++b) {
            out.push_back(static_cast<char>((v >> (b * 8)) & 0xFFu));
        }
    };
    auto addBytes = [&](const std::vector<uint8_t>& v) {
        addU(v.size());
        for (const uint8_t x : v) {
            out.push_back(static_cast<char>(x));
        }
    };
    auto addFloats = [&](const std::vector<float>& v) {
        addU(v.size());
        for (const float x : v) {
            // Bit pattern, not a formatted value: a formatted comparison would
            // hide a low-bit difference, which is exactly the kind of
            // non-determinism worth catching.
            uint32_t bits = 0;
            static_assert(sizeof(bits) == sizeof(x));
            __builtin_memcpy(&bits, &x, sizeof(bits));
            addU(bits);
        }
    };

    for (int32_t i = 0; i < g.tileCount(); ++i) {
        out.push_back(static_cast<char>(g.terrain(i)));
        out.push_back(static_cast<char>(g.feature(i)));
        out.push_back(static_cast<char>(g.elevation(i)));
    }
    addBytes(g.rockType());
    addBytes(g.crustalThickness());
    addBytes(g.geothermalGradient());
    addBytes(g.metamorphicFacies());
    addBytes(g.lakeFlag());
    addBytes(g.koppen());
    addBytes(g.biomeSubtype());
    addFloats(g.soilFertility());
    addFloats(g.sedimentDepth());
    return out;
}

} // namespace

TEST_CASE("regenerating into a reused grid equals generating into a fresh one") {
    aoc::map::HexGrid fresh;
    aoc::map::MapGenerator::generate(configFor(42), fresh);
    const std::string expected = fingerprint(fresh);

    // Reused: put a DIFFERENT map in the grid first, so there is a previous map
    // to inherit. Without this step the test cannot fail.
    aoc::map::HexGrid reused;
    aoc::map::MapGenerator::generate(configFor(7), reused);
    const std::string other = fingerprint(reused);
    REQUIRE(other != expected); // sanity: the two seeds really differ

    aoc::map::MapGenerator::generate(configFor(42), reused);
    CHECK(fingerprint(reused) == expected);
}

TEST_CASE("a third generate into the same grid is still stable") {
    // Guards against a layer that is only wrong on odd or even regenerations,
    // which a single reuse would miss.
    aoc::map::HexGrid grid;
    aoc::map::MapGenerator::generate(configFor(42), grid);
    const std::string first = fingerprint(grid);

    aoc::map::MapGenerator::generate(configFor(100), grid);
    aoc::map::MapGenerator::generate(configFor(42), grid);
    CHECK(fingerprint(grid) == first);

    aoc::map::MapGenerator::generate(configFor(7), grid);
    aoc::map::MapGenerator::generate(configFor(42), grid);
    CHECK(fingerprint(grid) == first);
}

TEST_CASE("regenerating at a different size into the same grid is safe and stable") {
    // Layer vectors sized to the previous tile count are an out-of-bounds hazard,
    // not merely a staleness one -- setPlateId lazily allocates and would write
    // past the end of a vector left at the old size.
    aoc::map::HexGrid grid;
    aoc::map::MapGenerator::Config big = configFor(42);
    big.width                          = 80;
    big.height                         = 52;
    aoc::map::MapGenerator::generate(big, grid);
    CHECK(grid.tileCount() == 80 * 52);

    aoc::map::MapGenerator::generate(configFor(42), grid);
    CHECK(grid.tileCount() == 60 * 40);

    aoc::map::HexGrid fresh;
    aoc::map::MapGenerator::generate(configFor(42), fresh);
    CHECK(fingerprint(grid) == fingerprint(fresh));
}
