/**
 * @file StartPlacement.cpp
 * @brief Largest-landmass-first start placement.
 */

#include "aoc/map/StartPlacement.hpp"

#include "aoc/core/Log.hpp"
#include "aoc/core/Random.hpp"
#include "aoc/map/HexGrid.hpp"
#include "aoc/map/LandmassMetrics.hpp"
#include "aoc/map/Terrain.hpp"

#include <algorithm>
#include <cmath>
#include <array>
#include <cstddef>

namespace aoc::map {

namespace {

struct Candidate {
    hex::AxialCoord coord;
    float score;
};

[[nodiscard]] bool isStartableLand(const HexGrid& grid, int32_t index) {
    const TerrainType t = grid.terrain(index);
    return !isWater(t) && !isImpassable(t);
}

/// Food-weighted yield of the tile and its land neighbours, plus a bonus per
/// adjacent resource. Same shape the headless sim scored with before.
[[nodiscard]] float scoreStart(const HexGrid& grid, hex::AxialCoord coord, int32_t index,
                               int32_t& landNeighbors) {
    const TileYield centre = grid.tileYield(index);
    float score = static_cast<float>(std::max(centre.food, static_cast<int8_t>(2))) * 2.0f;
    score += static_cast<float>(centre.production);

    landNeighbors = 0;
    for (const hex::AxialCoord& n : hex::neighbors(coord)) {
        if (!grid.isValid(n)) {
            continue;
        }
        const int32_t ni = grid.toIndex(n);
        if (!isStartableLand(grid, ni)) {
            continue;
        }
        ++landNeighbors;
        const TileYield y = grid.tileYield(ni);
        score += static_cast<float>(y.food) * 1.5f;
        score += static_cast<float>(y.production);
        if (grid.resource(ni).isValid()) {
            score += 3.0f;
        }
    }
    return score;
}

/// Startable tiles grouped by landmass, largest landmass first.
[[nodiscard]] std::vector<std::vector<Candidate>>
candidatesByLandmass(const HexGrid& grid, const LandmassMap& landmasses,
                     const StartPlacementOptions& options, std::vector<int32_t>& order) {
    const int32_t width  = grid.width();
    const int32_t height = grid.height();
    std::vector<std::vector<Candidate>> buckets(landmasses.componentSize.size());

    for (int32_t row = options.edgeMargin; row < height - options.edgeMargin; ++row) {
        for (int32_t col = options.edgeMargin; col < width - options.edgeMargin; ++col) {
            const int32_t index = row * width + col;
            const int32_t cid   = landmasses.componentId[static_cast<std::size_t>(index)];
            if (cid < 0 || !isStartableLand(grid, index)) {
                continue;
            }
            if (landmasses.componentSize[static_cast<std::size_t>(cid)] <
                options.minLandmassTiles) {
                continue;
            }
            const hex::AxialCoord coord = hex::offsetToAxial({col, row});
            int32_t landNeighbors       = 0;
            const float score           = scoreStart(grid, coord, index, landNeighbors);
            if (landNeighbors < 3) {
                continue;
            }
            buckets[static_cast<std::size_t>(cid)].push_back(Candidate{coord, score});
        }
    }

    order.clear();
    for (std::size_t cid = 0; cid < buckets.size(); ++cid) {
        if (!buckets[cid].empty()) {
            order.push_back(static_cast<int32_t>(cid));
        }
    }
    std::stable_sort(order.begin(), order.end(), [&landmasses](int32_t a, int32_t b) {
        return landmasses.componentSize[static_cast<std::size_t>(a)] >
               landmasses.componentSize[static_cast<std::size_t>(b)];
    });
    return buckets;
}

[[nodiscard]] int32_t distanceToNearestStart(const HexGrid& grid, hex::AxialCoord coord,
                                             const std::vector<hex::AxialCoord>& starts) {
    int32_t nearest = 1 << 20;
    for (const hex::AxialCoord& s : starts) {
        nearest = std::min(nearest, grid.distance(coord, s));
    }
    return nearest;
}

/// Best sampled candidate on one landmass that keeps `minDistance` from every
/// placed start; `found` is false when none does.
[[nodiscard]] hex::AxialCoord pickOnLandmass(const HexGrid& grid,
                                             const std::vector<Candidate>& bucket,
                                             const std::vector<hex::AxialCoord>& starts,
                                             int32_t minDistance, int32_t samples, Random& rng,
                                             bool& found) {
    constexpr float SPREAD_WEIGHT = 0.5f; // reward distance from rivals, capped below
    constexpr int32_t SPREAD_CAP  = 24;
    // Reward a different latitude band from the placed starts: capitals in
    // one temperate belt all hold the same luxuries and have nothing to trade.
    constexpr float CLIMATE_WEIGHT = 30.0f;
    constexpr float CLIMATE_CAP    = 0.3f;

    found                   = false;
    hex::AxialCoord best    = {0, 0};
    float bestScore         = -1.0f;
    const int32_t bucketMax = static_cast<int32_t>(bucket.size()) - 1;
    for (int32_t i = 0; i < samples; ++i) {
        const Candidate& c    = bucket[static_cast<std::size_t>(rng.nextInt(0, bucketMax))];
        const int32_t nearest = distanceToNearestStart(grid, c.coord, starts);
        if (nearest < minDistance) {
            continue;
        }
        float climate = starts.empty() ? 0.0f : CLIMATE_CAP;
        for (const hex::AxialCoord& s : starts) {
            const float mine   = grid.latitudeFraction(hex::axialToOffset(c.coord).row);
            const float theirs = grid.latitudeFraction(hex::axialToOffset(s).row);
            climate            = std::min(climate, std::fabs(mine - theirs));
        }
        const float score = c.score + SPREAD_WEIGHT * static_cast<float>(std::min(nearest, SPREAD_CAP)) +
                            CLIMATE_WEIGHT * climate;
        if (score > bestScore) {
            bestScore = score;
            best      = c.coord;
            found     = true;
        }
    }
    return best;
}

} // namespace

std::vector<hex::AxialCoord> chooseStartPositions(const HexGrid& grid, int32_t playerCount,
                                                  Random& rng,
                                                  const StartPlacementOptions& options) {
    std::vector<hex::AxialCoord> starts;
    if (playerCount <= 0 || grid.width() <= 0 || grid.height() <= 0) {
        return starts;
    }
    const LandmassMap landmasses = computeLandmasses(grid);
    std::vector<int32_t> order;
    const std::vector<std::vector<Candidate>> buckets =
        candidatesByLandmass(grid, landmasses, options, order);
    if (order.empty()) {
        LOG_WARN("StartPlacement: no startable land on a %dx%d map", grid.width(), grid.height());
        return starts;
    }

    starts.reserve(static_cast<std::size_t>(playerCount));
    for (int32_t p = 0; p < playerCount; ++p) {
        bool found = false;
        hex::AxialCoord pick{0, 0};
        // Full spacing on the largest landmass first, then the others; if no
        // landmass can keep the spacing, halve it rather than fail the game.
        for (int32_t minDistance = options.minStartDistance; !found && minDistance >= 1;
             minDistance /= 2) {
            for (const int32_t cid : order) {
                pick = pickOnLandmass(grid, buckets[static_cast<std::size_t>(cid)], starts,
                                      minDistance, options.samplesPerStart, rng, found);
                if (found) {
                    break;
                }
            }
        }
        if (!found) {
            pick = buckets[static_cast<std::size_t>(order.front())].front().coord;
            LOG_WARN("StartPlacement: player %d could not keep any spacing; stacking on the "
                     "largest landmass",
                     p);
        }
        const int32_t landmass = landmasses.componentId[static_cast<std::size_t>(grid.toIndex(pick))];
        LOG_INFO("StartPlacement: player %d at (%d,%d) on landmass %d (%d tiles)", p, pick.q, pick.r,
                 landmass, landmasses.componentSize[static_cast<std::size_t>(landmass)]);
        starts.push_back(pick);
    }
    return starts;
}

} // namespace aoc::map
