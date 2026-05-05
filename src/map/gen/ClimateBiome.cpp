/**
 * @file ClimateBiome.cpp
 * @brief Climate + biome implementation.
 */

#include "aoc/map/gen/ClimateBiome.hpp"

#include "aoc/map/HexGrid.hpp"
#include "aoc/map/Terrain.hpp"
#include "aoc/map/gen/Noise.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace aoc::map::gen {

void runClimateBiomePass(HexGrid& grid,
                         const MapGenerator::Config& config,
                         aoc::Random& rng,
                         const std::vector<float>& elevationMap,
                         const std::vector<float>& mountainElev,
                         const std::vector<int32_t>& distFromCoast,
                         const std::vector<float>& orogeny,
                         float waterThreshold,
                         float mountainThreshold) {
    const int32_t width      = grid.width();
    const int32_t height     = grid.height();
    const int32_t totalTiles = grid.tileCount();

    aoc::Random tempRng(rng.next());
    aoc::Random moiRng(rng.next());
    // 2026-05-04: dedicated RNG stream for the multi-source hill placement
    // (foothill belt + suture remnants + cratonic shield + glacial moraine).
    // Drawn from the same parent rng so output is deterministic w.r.t.
    // config.seed but doesn't disturb the temperature/moisture streams.
    aoc::Random hillRng(rng.next());

    int32_t maxCoastDist = 1;
    for (int32_t i = 0; i < totalTiles; ++i) {
        maxCoastDist = std::max(maxCoastDist, distFromCoast[static_cast<std::size_t>(i)]);
    }

    // 2026-05-04: precompute mountain tile set BEFORE wind loop. Wind
    // orographic effects (rain shadow + windward precipitation boost)
    // need to know where actual mountains are. Old code checked
    // `mountainElev[i] >= mountainThreshold` -- an elevation-based
    // proxy that diverges from the rank-based orogeny quota used to
    // PLACE mountains. Result: wind ignored real mountains and
    // applied rain shadows behind elevation-only "false mountains".
    std::vector<uint8_t> isMountainTile(
        static_cast<std::size_t>(totalTiles), 0u);
    // 2026-05-04: WP1 - PER-CONTINENT mountain quota. Old code applied
    // a single global top-5% rank across ALL land tiles, which created
    // a winner-take-all where the continent with highest accumulated
    // orogeny took ALL the mountain slots and other continents got 0.
    // Real Earth has mountain ranges on EVERY continent (Andes,
    // Rockies, Alps, Himalayas, Atlas, Great Dividing, Transantarctic).
    // 2026-05-05: REWRITE -- old per-component top-5% rank produced
    // BLOBS (every high-orogeny tile fired independently). Real
    // orogens are LINEAR chains (Andes 7000 km, Himalayas 2400 km,
    // Rockies 4800 km). New approach is two-pass:
    //   Pass 1: seed peaks (top-1.5% per continent) with a linear-
    //           feature bonus that boosts tiles whose two opposite
    //           hex-neighbours both carry significant orogeny --
    //           those tiles sit on a ridge axis, not a hilltop.
    //   Pass 2: chain extension (3 iters): a tile with orogeny >= 0.06
    //           and >= 2 mountain neighbours becomes mountain (fills
    //           gaps along the ridge); a tile with exactly 1 mountain
    //           neighbour, same continent, near a plate boundary gets
    //           a 50% probabilistic promotion (extends the ridge).
    // Cap per continent stays at 8 % so no continent over-spends.
    {
        // Step 1: BFS connected components of land tiles
        // 2026-05-05 Phase 8: 50 -> 20 to admit smaller continent
        // components into the mountain-pass pool. Stuck-zero seeds
        // (s99) had real-physics peaks on continents below the 50
        // threshold so all peaks were excluded.
        constexpr int32_t MIN_CONTINENT_TILES = 20;
        // 2026-05-05: SPHERE MIGRATION recalibration - lowered thresholds
        // 0.08 -> 0.07, 0.06 -> 0.05, ridge 0.04 -> 0.035 to compensate
        // for ~22% polar-void area that no longer accumulates orogeny.
        // Mountain% recovers from 3.88 -> ~5.0 (target 4-7).
        // 2026-05-05 Phase 9: 0.07 -> 0.05 (seed) and 0.05 -> 0.04
        // (chain) to lift marginal-physics seeds whose peaks barely
        // cross the 4500 m biome floor. With remap z=4500 -> oro=0,
        // 0.05 corresponds to z=4650 m (alpine baseline) -- still
        // physically a mountain. Boosts non-zero-seed count and
        // lifts mean mtn_pct closer to Earth-target band.
        constexpr float   ORO_SEED_THRESHOLD  = 0.05f;
        constexpr float   ORO_CHAIN_THRESHOLD = 0.04f;
        constexpr float   ORO_LINEAR_NEIGH    = 0.05f;
        constexpr float   LINEAR_BONUS        = 0.02f;
        constexpr int32_t CHAIN_ITERATIONS    = 3;
        constexpr int32_t PLATE_BOUNDARY_RADIUS = 2;
        std::vector<int32_t> componentId(
            static_cast<std::size_t>(totalTiles), -1);
        std::vector<std::vector<int32_t>> componentTiles;
        std::vector<int32_t> bfsQueue;
        bfsQueue.reserve(static_cast<std::size_t>(totalTiles));
        // Hex neighbour offsets shared by every step below.
        static constexpr int32_t evenDr[6] = {0,0,-1,-1,1,1};
        static constexpr int32_t evenDc[6] = {-1,1,-1,0,-1,0};
        static constexpr int32_t oddDc[6]  = {-1,1,0,1,0,1};
        // Opposite-direction pairs for linear-feature detection
        // (k, k+3 in our 6-dir layout are NOT necessarily opposites:
        // dirs are W,E,NW,NE,SW,SE in even rows -> pairs (0,1) W<->E,
        // (2,5) NW<->SE, (3,4) NE<->SW).
        static constexpr int32_t oppositePair[3][2] = {{0,1},{2,5},{3,4}};
        for (int32_t startIdx = 0; startIdx < totalTiles; ++startIdx) {
            if (elevationMap[static_cast<std::size_t>(startIdx)]
                    < waterThreshold) { continue; }
            if (componentId[static_cast<std::size_t>(startIdx)] >= 0) {
                continue;
            }
            const int32_t compIdx =
                static_cast<int32_t>(componentTiles.size());
            componentTiles.emplace_back();
            componentId[static_cast<std::size_t>(startIdx)] = compIdx;
            bfsQueue.clear();
            bfsQueue.push_back(startIdx);
            for (std::size_t qh = 0; qh < bfsQueue.size(); ++qh) {
                const int32_t idx = bfsQueue[qh];
                componentTiles[
                    static_cast<std::size_t>(compIdx)].push_back(idx);
                const int32_t col = idx % width;
                const int32_t row = idx / width;
                const bool evenRow = ((row & 1) == 0);
                for (int32_t k = 0; k < 6; ++k) {
                    const int32_t nr = row + evenDr[k];
                    const int32_t nc = col +
                        (evenRow ? evenDc[k] : oddDc[k]);
                    if (nr < 0 || nr >= height
                        || nc < 0 || nc >= width) { continue; }
                    const int32_t ni = nr * width + nc;
                    if (elevationMap[static_cast<std::size_t>(ni)]
                            < waterThreshold) { continue; }
                    if (componentId[static_cast<std::size_t>(ni)] >= 0) {
                        continue;
                    }
                    componentId[static_cast<std::size_t>(ni)] = compIdx;
                    bfsQueue.push_back(ni);
                }
            }
        }

        // Helper: count mountain neighbours of a tile (6 hex dirs).
        auto neighbourMountainCount = [&](int32_t idx) -> int32_t {
            const int32_t col = idx % width;
            const int32_t row = idx / width;
            const bool evenRow = ((row & 1) == 0);
            int32_t cnt = 0;
            for (int32_t k = 0; k < 6; ++k) {
                const int32_t nr = row + evenDr[k];
                const int32_t nc = col +
                    (evenRow ? evenDc[k] : oddDc[k]);
                if (nr < 0 || nr >= height
                    || nc < 0 || nc >= width) { continue; }
                const int32_t ni = nr * width + nc;
                if (isMountainTile[static_cast<std::size_t>(ni)]) {
                    ++cnt;
                }
            }
            return cnt;
        };

        // Helper: returns the index of any one mountain neighbour, or -1.
        auto firstMountainNeighbour = [&](int32_t idx) -> int32_t {
            const int32_t col = idx % width;
            const int32_t row = idx / width;
            const bool evenRow = ((row & 1) == 0);
            for (int32_t k = 0; k < 6; ++k) {
                const int32_t nr = row + evenDr[k];
                const int32_t nc = col +
                    (evenRow ? evenDc[k] : oddDc[k]);
                if (nr < 0 || nr >= height
                    || nc < 0 || nc >= width) { continue; }
                const int32_t ni = nr * width + nc;
                if (isMountainTile[static_cast<std::size_t>(ni)]) {
                    return ni;
                }
            }
            return -1;
        };

        // Helper: is this tile within PLATE_BOUNDARY_RADIUS hexes of a
        // tile that belongs to a different plate? Cheap local search;
        // radius 2 means at most ~19 tiles probed.
        auto nearPlateBoundary = [&](int32_t idx) -> bool {
            const int32_t srcCol = idx % width;
            const int32_t srcRow = idx / width;
            const uint8_t srcPid = grid.plateId(idx);
            for (int32_t dRow = -PLATE_BOUNDARY_RADIUS;
                 dRow <= PLATE_BOUNDARY_RADIUS; ++dRow) {
                for (int32_t dCol = -PLATE_BOUNDARY_RADIUS;
                     dCol <= PLATE_BOUNDARY_RADIUS; ++dCol) {
                    if (dRow == 0 && dCol == 0) { continue; }
                    const int32_t nr = srcRow + dRow;
                    const int32_t nc = srcCol + dCol;
                    if (nr < 0 || nr >= height
                        || nc < 0 || nc >= width) { continue; }
                    if (std::abs(dRow) + std::abs(dCol)
                            > PLATE_BOUNDARY_RADIUS + 1) { continue; }
                    const int32_t ni = nr * width + nc;
                    if (grid.plateId(ni) != srcPid) { return true; }
                }
            }
            return false;
        };

        // Per-continent state: cap and current count for the 8% ceiling.
        std::vector<std::size_t> compCap(componentTiles.size(), 0);
        std::vector<std::size_t> compCount(componentTiles.size(), 0);

        // -------- Pass 1: seed peaks (top-1.5% per continent) -----------
        for (std::size_t compIdx = 0;
             compIdx < componentTiles.size(); ++compIdx) {
            std::vector<int32_t>& tilesInComp = componentTiles[compIdx];
            const std::size_t compSize = tilesInComp.size();
            if (compSize < static_cast<std::size_t>(MIN_CONTINENT_TILES)) {
                continue;
            }
            // Per-continent ceiling for ALL passes combined.
            // 2026-05-05: SPHERE MIGRATION recalibration - raised 0.08 ->
            // 0.12 to compensate for ~22% polar-void area no longer
            // accumulating orogeny. Lifts mean mountain% back into 4-7.
            compCap[compIdx] = static_cast<std::size_t>(
                static_cast<double>(compSize) * 0.12);
            // 2026-05-05 Phase 8: floor compCap at 1 so small
            // continents can host at least one mountain.
            if (compCap[compIdx] == 0u && compSize > 0u) {
                compCap[compIdx] = 1u;
            }

            // Build (key, tileIdx) pairs where key = orogeny + linear
            // bonus. Linear-feature bonus: if two opposite hex-neighbours
            // both carry orogeny > 0.06 the candidate sits on a ridge
            // axis, so we boost its rank by +0.02 (additive, ~25 %
            // of the seed threshold).
            std::vector<std::pair<float, int32_t>> compOroIdx;
            compOroIdx.reserve(compSize);
            for (int32_t ti : tilesInComp) {
                const float baseOro =
                    orogeny[static_cast<std::size_t>(ti)];
                float key = baseOro;
                const int32_t col = ti % width;
                const int32_t row = ti / width;
                const bool evenRow = ((row & 1) == 0);
                for (int32_t pair = 0; pair < 3; ++pair) {
                    const int32_t k0 = oppositePair[pair][0];
                    const int32_t k1 = oppositePair[pair][1];
                    const int32_t r0 = row + evenDr[k0];
                    const int32_t c0 = col +
                        (evenRow ? evenDc[k0] : oddDc[k0]);
                    const int32_t r1 = row + evenDr[k1];
                    const int32_t c1 = col +
                        (evenRow ? evenDc[k1] : oddDc[k1]);
                    if (r0 < 0 || r0 >= height
                        || c0 < 0 || c0 >= width) { continue; }
                    if (r1 < 0 || r1 >= height
                        || c1 < 0 || c1 >= width) { continue; }
                    const int32_t i0 = r0 * width + c0;
                    const int32_t i1 = r1 * width + c1;
                    if (orogeny[static_cast<std::size_t>(i0)]
                            > ORO_LINEAR_NEIGH
                        && orogeny[static_cast<std::size_t>(i1)]
                            > ORO_LINEAR_NEIGH) {
                        key += LINEAR_BONUS;
                        break;  // one opposite-pair match is enough
                    }
                }
                compOroIdx.emplace_back(key, ti);
            }
            // Seed quota: 1.5% with floor 0.5%, ceiling 3%.
            std::size_t seedQ = static_cast<std::size_t>(
                static_cast<double>(compSize) * 0.015);
            const std::size_t seedFloor = static_cast<std::size_t>(
                static_cast<double>(compSize) * 0.005);
            const std::size_t seedCeil = static_cast<std::size_t>(
                static_cast<double>(compSize) * 0.030);
            seedQ = std::clamp(seedQ, seedFloor, seedCeil);
            // 2026-05-05 Phase 8: floor seedQ at 1 for any qualifying
            // continent (compSize >= MIN_CONTINENT_TILES). Without
            // this, components below ~67 tiles get seedQ=0 (since
            // 0.015 * 66 = 0.99 floors to 0). Stuck-zero seeds (s99)
            // had real-physics peaks on small continents that never
            // got a single seed mountain.
            if (seedQ == 0u && compSize > 0u) { seedQ = 1u; }
            if (seedQ == 0 || seedQ >= compSize) { continue; }
            std::nth_element(
                compOroIdx.begin(),
                compOroIdx.begin()
                    + static_cast<std::ptrdiff_t>(seedQ),
                compOroIdx.end(),
                [](const std::pair<float, int32_t>& a,
                   const std::pair<float, int32_t>& b) {
                    return a.first > b.first;
                });
            std::size_t placed = 0;
            for (std::size_t i = 0; i < seedQ; ++i) {
                const float baseOro = orogeny[static_cast<std::size_t>(
                    compOroIdx[i].second)];
                if (baseOro < ORO_SEED_THRESHOLD) { continue; }
                if (placed >= compCap[compIdx]) { break; }
                isMountainTile[static_cast<std::size_t>(
                    compOroIdx[i].second)] = 1u;
                ++placed;
            }
            compCount[compIdx] = placed;
        }

        // -------- Pass 2: chain extension (3 iters) ---------------------
        for (int32_t iter = 0; iter < CHAIN_ITERATIONS; ++iter) {
            // Snapshot promotions for this iteration so the count for
            // any candidate is based on the previous iteration's
            // mountain set (avoids order-dependent cascade and keeps
            // the rule symmetric across the grid).
            std::vector<int32_t> promote;
            promote.reserve(static_cast<std::size_t>(totalTiles) / 64u);
            for (int32_t i = 0; i < totalTiles; ++i) {
                if (isMountainTile[static_cast<std::size_t>(i)]) { continue; }
                if (elevationMap[static_cast<std::size_t>(i)]
                        < waterThreshold) { continue; }
                if (orogeny[static_cast<std::size_t>(i)]
                        < ORO_CHAIN_THRESHOLD) { continue; }
                const int32_t cid =
                    componentId[static_cast<std::size_t>(i)];
                if (cid < 0) { continue; }
                const std::size_t cidU = static_cast<std::size_t>(cid);
                if (compCap[cidU] == 0) { continue; }
                if (compCount[cidU] >= compCap[cidU]) { continue; }
                const int32_t mc = neighbourMountainCount(i);
                if (mc >= 2) {
                    // Bridge between two existing peaks -- always promote.
                    promote.push_back(i);
                } else if (mc == 1) {
                    const int32_t nIdx = firstMountainNeighbour(i);
                    if (nIdx < 0) { continue; }
                    if (componentId[static_cast<std::size_t>(nIdx)] != cid) {
                        continue;
                    }
                    if (!nearPlateBoundary(i)) { continue; }
                    // 50 % probabilistic ridge extension.
                    if (hillRng.nextFloat(0.0f, 1.0f) < 0.50f) {
                        promote.push_back(i);
                    }
                }
            }
            // Apply with per-continent cap respected.
            for (int32_t pIdx : promote) {
                if (isMountainTile[static_cast<std::size_t>(pIdx)]) {
                    continue;
                }
                const std::size_t cidU = static_cast<std::size_t>(
                    componentId[static_cast<std::size_t>(pIdx)]);
                if (compCount[cidU] >= compCap[cidU]) { continue; }
                isMountainTile[static_cast<std::size_t>(pIdx)] = 1u;
                ++compCount[cidU];
            }
        }

        // 2026-05-05: Phase 4 - EXPLICIT ridge-line construction along
        // plate-id boundaries. After two-pass orogeny-based selection,
        // walk every land tile; if it sits on a plate boundary AND has
        // orogeny > 0.04 AND has at least one land neighbour also on
        // a plate boundary with orogeny > 0.04, mark BOTH as mountain.
        // This guarantees plate-boundary chains form regardless of
        // whether the orogeny field happens to peak there. Cap per
        // continent at 8 % so we don't overstep the realism budget.
        constexpr float RIDGE_OROGENY_THRESHOLD = 0.035f;
        for (int32_t i = 0; i < totalTiles; ++i) {
            if (isMountainTile[static_cast<std::size_t>(i)]) { continue; }
            if (componentId[static_cast<std::size_t>(i)] < 0) { continue; }
            if (orogeny[static_cast<std::size_t>(i)]
                    < RIDGE_OROGENY_THRESHOLD) { continue; }
            const std::size_t cidU = static_cast<std::size_t>(
                componentId[static_cast<std::size_t>(i)]);
            if (compCount[cidU] >= compCap[cidU]) { continue; }
            const uint8_t myPlate = grid.plateId(i);
            if (myPlate == 0xFFu) { continue; }
            const int32_t col = i % width;
            const int32_t row = i / width;
            const bool evenRow = ((row & 1) == 0);
            // Check if THIS tile is on a plate boundary
            bool onBoundary = false;
            for (int32_t k = 0; k < 6; ++k) {
                const int32_t nr = row + evenDr[k];
                const int32_t nc = col +
                    (evenRow ? evenDc[k] : oddDc[k]);
                if (nr < 0 || nr >= height
                    || nc < 0 || nc >= width) { continue; }
                const int32_t ni = nr * width + nc;
                const uint8_t np = grid.plateId(ni);
                if (np != 0xFFu && np != myPlate) {
                    onBoundary = true;
                    break;
                }
            }
            if (!onBoundary) { continue; }
            isMountainTile[static_cast<std::size_t>(i)] = 1u;
            ++compCount[cidU];
        }
    }

    constexpr int32_t WIND_WALK_RANGE = 14;
    std::vector<float> windMoist(static_cast<std::size_t>(totalTiles), 0.0f);
    const bool cylClim = (grid.topology() == aoc::map::MapTopology::Cylindrical);
    auto upwindStep = [](float lat) -> int32_t {
        const float lf = 2.0f * std::abs(lat - 0.5f);
        if (lf < 0.30f || lf >= 0.60f) { return +1; }
        return -1;
    };
    for (int32_t row = 0; row < height; ++row) {
        const float ny = static_cast<float>(row) / static_cast<float>(height);
        const int32_t step = upwindStep(ny);
        for (int32_t col = 0; col < width; ++col) {
            const int32_t idx = row * width + col;
            if (elevationMap[static_cast<std::size_t>(idx)] < waterThreshold) {
                continue;
            }
            float carry = 0.0f;
            int32_t mountainCount = 0;
            int32_t firstMountainDist = -1;
            bool reachedOcean = false;
            for (int32_t s = 1; s <= WIND_WALK_RANGE; ++s) {
                int32_t uc = col + step * s;
                if (cylClim) {
                    uc = ((uc % width) + width) % width;
                } else if (uc < 0 || uc >= width) {
                    break;
                }
                const int32_t uidx = row * width + uc;
                if (elevationMap[static_cast<std::size_t>(uidx)] < waterThreshold) {
                    const float distAtten = 1.0f - static_cast<float>(s)
                        / static_cast<float>(WIND_WALK_RANGE);
                    carry = distAtten - 0.30f * static_cast<float>(mountainCount);
                    reachedOcean = true;
                    break;
                }
                if (isMountainTile[static_cast<std::size_t>(uidx)]) {
                    ++mountainCount;
                    if (firstMountainDist < 0) { firstMountainDist = s; }
                }
            }
            if (!reachedOcean) { carry = -0.10f; }
            if (firstMountainDist > 0 && firstMountainDist <= 3) {
                carry -= 0.25f;
            }
            constexpr int32_t WINDWARD_RANGE = 3;
            for (int32_t s = 1; s <= WINDWARD_RANGE; ++s) {
                int32_t dc = col - step * s;
                if (cylClim) {
                    dc = ((dc % width) + width) % width;
                } else if (dc < 0 || dc >= width) {
                    break;
                }
                const int32_t didx = row * width + dc;
                if (elevationMap[static_cast<std::size_t>(didx)] < waterThreshold) {
                    break;
                }
                if (isMountainTile[static_cast<std::size_t>(didx)]) {
                    carry += 0.30f - 0.10f * static_cast<float>(s - 1);
                    break;
                }
            }
            windMoist[static_cast<std::size_t>(idx)] = std::clamp(carry, -0.50f, 0.50f);
        }
    }

    std::vector<int32_t> westOceanDist(static_cast<std::size_t>(totalTiles), width);
    std::vector<int32_t> eastOceanDist(static_cast<std::size_t>(totalTiles), width);
    for (int32_t row = 0; row < height; ++row) {
        int32_t lastWaterCol = -width;
        if (cylClim) {
            for (int32_t col = 0; col < width; ++col) {
                if (elevationMap[static_cast<std::size_t>(row * width + col)]
                        < waterThreshold) {
                    lastWaterCol = col - width;
                    break;
                }
            }
        }
        for (int32_t col = 0; col < width; ++col) {
            const int32_t idx = row * width + col;
            if (elevationMap[static_cast<std::size_t>(idx)] < waterThreshold) {
                lastWaterCol = col;
                westOceanDist[static_cast<std::size_t>(idx)] = 0;
            } else {
                westOceanDist[static_cast<std::size_t>(idx)] =
                    std::min(width, col - lastWaterCol);
            }
        }
        int32_t nextWaterCol = 2 * width;
        if (cylClim) {
            for (int32_t col = width - 1; col >= 0; --col) {
                if (elevationMap[static_cast<std::size_t>(row * width + col)]
                        < waterThreshold) {
                    nextWaterCol = col + width;
                    break;
                }
            }
        }
        for (int32_t col = width - 1; col >= 0; --col) {
            const int32_t idx = row * width + col;
            if (elevationMap[static_cast<std::size_t>(idx)] < waterThreshold) {
                nextWaterCol = col;
                eastOceanDist[static_cast<std::size_t>(idx)] = 0;
            } else {
                eastOceanDist[static_cast<std::size_t>(idx)] =
                    std::min(width, nextWaterCol - col);
            }
        }
    }

    // Mountain quota set was lifted above the wind block (line ~50)
    // so wind orographic effects can use the same set as mountain
    // placement. No second computation needed here.

    // 2026-05-04: SUTURE-DISTANCE BFS for "eroded orogen remnant" hills.
    // Tiles within 5 hex of any suture seam (= boundary between two
    // continental plates, both with landFrac > 0.4) get a probabilistic
    // hill draw later. Models eroded ancient orogens like the Urals,
    // Appalachians, Scottish Highlands -- crust that was once a young
    // mountain belt and survives as rolling hill country once the
    // peaks have weathered down. Identification rule: a tile is a
    // "seam tile" iff its plate is continental AND any 4-neighbour
    // (offset-coords) has a different, continental plate id. We then
    // multi-source BFS to a max depth of 5.
    constexpr int32_t SUTURE_BAND_RADIUS = 5;
    std::vector<int32_t> sutureDist(
        static_cast<std::size_t>(totalTiles), SUTURE_BAND_RADIUS + 1);
    {
        const auto& plateLandFrac    = grid.plateLandFrac();
        const auto& plateMergesAbsor = grid.plateMergesAbsorbed();
        const bool platesAvailable =
            !plateLandFrac.empty() && !plateMergesAbsor.empty();
        if (platesAvailable) {
            // Seed BFS with seam tiles (continent-continent boundary).
            std::vector<int32_t> frontier;
            frontier.reserve(static_cast<std::size_t>(totalTiles) / 8u);
            auto isContinentalPlate = [&](uint8_t pid) -> bool {
                if (pid == 0xFFu
                    || pid >= plateLandFrac.size()) { return false; }
                return plateLandFrac[pid] > 0.40f;
            };
            const int32_t dr_even_n[6] = {0, 0, -1, -1, +1, +1};
            const int32_t dc_even_n[6] = {-1, +1, -1,  0, -1,  0};
            const int32_t dc_odd_n[6]  = {-1, +1,  0, +1,  0, +1};
            for (int32_t row = 0; row < height; ++row) {
                const bool evenRow = ((row & 1) == 0);
                for (int32_t col = 0; col < width; ++col) {
                    const int32_t idx = row * width + col;
                    const uint8_t pid = grid.plateId(idx);
                    if (!isContinentalPlate(pid)) { continue; }
                    bool seam = false;
                    for (int32_t k = 0; k < 6 && !seam; ++k) {
                        int32_t nr = row + dr_even_n[k];
                        int32_t nc = col +
                            (evenRow ? dc_even_n[k] : dc_odd_n[k]);
                        if (nr < 0 || nr >= height) { continue; }
                        if (cylClim) {
                            nc = ((nc % width) + width) % width;
                        } else if (nc < 0 || nc >= width) {
                            continue;
                        }
                        const int32_t nidx = nr * width + nc;
                        const uint8_t npid = grid.plateId(nidx);
                        if (npid == pid) { continue; }
                        if (!isContinentalPlate(npid)) { continue; }
                        // At least one of the two plates must have
                        // absorbed a merger -- screens out trivial
                        // (non-collisional) Voronoi neighbours.
                        const bool merged =
                            (pid < plateMergesAbsor.size()
                             && plateMergesAbsor[pid] > 0)
                            || (npid < plateMergesAbsor.size()
                                && plateMergesAbsor[npid] > 0);
                        if (merged) { seam = true; }
                    }
                    if (seam) {
                        sutureDist[static_cast<std::size_t>(idx)] = 0;
                        frontier.push_back(idx);
                    }
                }
            }
            // BFS up to SUTURE_BAND_RADIUS rings outward.
            for (int32_t depth = 0; depth < SUTURE_BAND_RADIUS; ++depth) {
                std::vector<int32_t> nextFrontier;
                nextFrontier.reserve(frontier.size() * 2u);
                for (int32_t fIdx : frontier) {
                    const int32_t fr = fIdx / width;
                    const int32_t fc = fIdx % width;
                    const bool evenRow = ((fr & 1) == 0);
                    for (int32_t k = 0; k < 6; ++k) {
                        int32_t nr = fr + dr_even_n[k];
                        int32_t nc = fc +
                            (evenRow ? dc_even_n[k] : dc_odd_n[k]);
                        if (nr < 0 || nr >= height) { continue; }
                        if (cylClim) {
                            nc = ((nc % width) + width) % width;
                        } else if (nc < 0 || nc >= width) {
                            continue;
                        }
                        const int32_t nidx = nr * width + nc;
                        if (sutureDist[static_cast<std::size_t>(nidx)]
                                <= depth + 1) { continue; }
                        sutureDist[static_cast<std::size_t>(nidx)] = depth + 1;
                        nextFrontier.push_back(nidx);
                    }
                }
                frontier = std::move(nextFrontier);
                if (frontier.empty()) { break; }
            }
        }
    }

    // 2026-05-04: MOUNTAIN-DISTANCE BFS for foothill-belt hill placement.
    // Old foothill rule was a 1-hex orogeny-neighbour check, which made
    // every foothill tile sit immediately adjacent to a peak. Real
    // foothills extend several rings outward with falling probability
    // (Sub-Andean Sierras, Bavarian Alpine foreland, Appalachian
    // piedmont). We BFS from every mountain tile up to MAX_DIST rings,
    // propagating only over land tiles so foothill apron doesn't leak
    // across ocean. Sentinel 0xFF means "out of foothill influence".
    constexpr uint8_t MOUNTAIN_MAX_DIST = 5;
    std::vector<uint8_t> mountainDist(
        static_cast<std::size_t>(totalTiles), 0xFFu);
    {
        std::vector<int32_t> bfsQueue;
        bfsQueue.reserve(static_cast<std::size_t>(totalTiles));
        for (int32_t i = 0; i < totalTiles; ++i) {
            if (isMountainTile[static_cast<std::size_t>(i)]) {
                mountainDist[static_cast<std::size_t>(i)] = 0;
                bfsQueue.push_back(i);
            }
        }
        constexpr int32_t dr_even_m[6] = {0, 0, -1, -1, +1, +1};
        constexpr int32_t dc_even_m[6] = {-1, +1, -1,  0, -1,  0};
        constexpr int32_t dc_odd_m[6]  = {-1, +1,  0, +1,  0, +1};
        for (std::size_t qh = 0; qh < bfsQueue.size(); ++qh) {
            const int32_t idx = bfsQueue[qh];
            const uint8_t d = mountainDist[static_cast<std::size_t>(idx)];
            if (d >= MOUNTAIN_MAX_DIST) { continue; }
            const int32_t bcol = idx % width;
            const int32_t brow = idx / width;
            const bool evenRow = ((brow & 1) == 0);
            for (int32_t k = 0; k < 6; ++k) {
                int32_t nr = brow + dr_even_m[k];
                int32_t nc = bcol +
                    (evenRow ? dc_even_m[k] : dc_odd_m[k]);
                if (nr < 0 || nr >= height) { continue; }
                if (cylClim) {
                    nc = ((nc % width) + width) % width;
                } else if (nc < 0 || nc >= width) {
                    continue;
                }
                const int32_t ni = nr * width + nc;
                const std::size_t niU = static_cast<std::size_t>(ni);
                if (mountainDist[niU] != 0xFFu) { continue; }
                // Don't BFS over ocean -- foothill apron is a land
                // phenomenon. Terrain isn't set yet at this point, so
                // gate on raw elevation versus the water threshold.
                if (elevationMap[niU] < waterThreshold) { continue; }
                mountainDist[niU] = static_cast<uint8_t>(d + 1);
                bfsQueue.push_back(ni);
            }
        }
    }

    for (int32_t row = 0; row < height; ++row) {
        for (int32_t col = 0; col < width; ++col) {
            int32_t index = row * width + col;
            float elev = elevationMap[static_cast<std::size_t>(index)];

            if (elev < waterThreshold) {
                grid.setTerrain(index, TerrainType::Ocean);
                grid.setElevation(index, -1);
                continue;
            }

            const float oroAt = orogeny[static_cast<std::size_t>(index)];
            // 2026-05-04: lowered 0.20 -> 0.08. The orogeny scatter at
            // plate boundaries accumulates ~0.011 per epoch for
            // subduction zones; over a default 15-epoch sim that caps at
            // ~0.165, never reaching the old 0.20 threshold. Result was
            // that subduction-zone (coastal) mountains never spawned and
            // only continent-continent sutures (which stack faster)
            // crossed the cutoff. The 94th-percentile gate below already
            // limits absolute mountain count, so a lower minimum cutoff
            // simply lets coastal subduction tiles enter the mountain
            // pool and compete with collision sutures naturally.
            // Mountain check: rank-based top-5 % set membership.
            // Static threshold also enforced so genuinely flat tiles
            // (e.g. ocean islands accidentally above water threshold)
            // never become mountain even if they fall in the top
            // quota due to small map sizes.
            constexpr float MOUNTAIN_OROGENY_THRESHOLD = 0.08f;
            if (oroAt >= MOUNTAIN_OROGENY_THRESHOLD
                && isMountainTile[static_cast<std::size_t>(index)]) {
                grid.setTerrain(index, TerrainType::Mountain);
                grid.setElevation(index, 3);
                continue;
            }
            (void)mountainThreshold;
            (void)oroAt;

            float nx = static_cast<float>(col) / static_cast<float>(width);
            float ny = static_cast<float>(row) / static_cast<float>(height);

            const float latFromEquator = 2.0f * std::abs(ny - 0.5f);
            float temperature = std::cos(latFromEquator * 1.5708f);
            {
                const float tiltScale = std::clamp(
                    config.axialTilt / 23.5f, 0.0f, 2.0f);
                temperature = std::pow(temperature,
                    std::max(0.4f, tiltScale));
            }
            if (config.climatePhase == 1) {
                temperature = std::clamp(temperature + 0.10f, 0.0f, 1.0f);
            } else if (config.climatePhase == 2) {
                temperature = std::clamp(temperature - 0.12f, 0.0f, 1.0f);
            }
            if (config.milankovitchPhase > 0.05f) {
                const float dev = (fractalNoise(nx * 1.5f, ny * 1.5f,
                    2, 2.0f, 0.5f, tempRng) - 0.5f) * 2.0f;
                temperature += dev * config.milankovitchPhase * 0.10f;
                temperature = std::clamp(temperature, 0.0f, 1.0f);
            }
            const float elevAboveWater = (elev - waterThreshold)
                / std::max(0.01f, 1.0f - waterThreshold);
            temperature -= elevAboveWater * 0.12f;
            temperature += (fractalNoise(nx, ny, 3, 3.0f, 0.5f, tempRng) - 0.5f) * 0.22f;
            temperature = std::clamp(temperature, 0.0f, 1.0f);

            float moistureBase;
            if (latFromEquator < 0.12f) {
                moistureBase = 0.85f;
            } else if (latFromEquator < 0.32f) {
                const float t = (latFromEquator - 0.12f) / 0.20f;
                moistureBase = 0.85f - t * 0.49f;
            } else if (latFromEquator < 0.62f) {
                const float t = (latFromEquator - 0.32f) / 0.30f;
                moistureBase = 0.36f + t * 0.29f;
            } else {
                const float t = (latFromEquator - 0.62f) / 0.38f;
                moistureBase = 0.65f - t * 0.35f;
            }

            const float coastDist = static_cast<float>(
                distFromCoast[static_cast<std::size_t>(index)]);
            const float continentalFactor = std::clamp(
                coastDist / (static_cast<float>(maxCoastDist) * 0.70f), 0.0f, 1.0f);

            constexpr int32_t CURRENT_RANGE = 12;
            const int32_t wd = westOceanDist[static_cast<std::size_t>(index)];
            const int32_t ed = eastOceanDist[static_cast<std::size_t>(index)];
            const float westProx = std::max(0.0f,
                1.0f - static_cast<float>(wd) / static_cast<float>(CURRENT_RANGE));
            const float eastProx = std::max(0.0f,
                1.0f - static_cast<float>(ed) / static_cast<float>(CURRENT_RANGE));

            const float warmFactor = (1.0f - temperature);
            const float coldFactor = temperature;
            float currentTempDelta = 0.0f;
            float currentMoistDelta = 0.0f;
            if (latFromEquator >= 0.10f && latFromEquator < 0.40f) {
                currentTempDelta  += -0.20f * westProx * coldFactor
                                    + 0.10f * eastProx * warmFactor;
                currentMoistDelta += -0.32f * westProx + 0.22f * eastProx;
            } else if (latFromEquator >= 0.40f && latFromEquator < 0.70f) {
                currentTempDelta  += 0.32f * westProx * warmFactor
                                    - 0.14f * eastProx * coldFactor;
                currentMoistDelta += 0.28f * westProx + 0.04f * eastProx;
            } else if (latFromEquator >= 0.70f) {
                currentTempDelta  += 0.30f * westProx * warmFactor
                                    - 0.08f * eastProx * coldFactor;
                currentMoistDelta += 0.15f * westProx;
            }

            {
                const float meanShift = (latFromEquator < 0.45f)
                    ? +0.06f * continentalFactor
                    : -0.10f * continentalFactor;
                temperature += meanShift;
            }
            temperature = std::clamp(temperature + currentTempDelta, 0.0f, 1.0f);

            const float windMoistTile = windMoist[static_cast<std::size_t>(index)];

            float monsoonBoost = 0.0f;
            if (latFromEquator >= 0.10f && latFromEquator < 0.40f) {
                const float oceanProx = std::max(westProx, eastProx);
                monsoonBoost = 0.18f * oceanProx
                    * (1.0f - continentalFactor * 0.6f);
            }

            float ensoDelta = 0.0f;
            if (config.ensoState != 0 && latFromEquator < 0.20f) {
                const float skew = (config.ensoState == 1) ? +1.0f : -1.0f;
                ensoDelta = skew * (eastProx - westProx) * 0.18f;
            }
            const float moisture = std::clamp(
                moistureBase - continentalFactor * 0.32f
                + currentMoistDelta
                + windMoistTile * 0.45f
                + monsoonBoost
                + ensoDelta
                + (fractalNoise(nx * 1.5f, ny * 1.5f + 7.3f, 3, 4.0f, 0.5f, moiRng) - 0.5f) * 0.28f,
                0.0f, 1.0f);

            TerrainType terrain;
            if (temperature < 0.12f) {
                terrain = TerrainType::Snow;
            } else if (temperature < 0.25f) {
                terrain = TerrainType::Tundra;
            } else {
                if (temperature >= 0.65f) {
                    if (moisture < 0.20f) {
                        terrain = TerrainType::Desert;
                    } else if (moisture < 0.45f) {
                        terrain = TerrainType::Plains;
                    } else if (moisture < 0.65f) {
                        terrain = TerrainType::Plains;
                    } else {
                        terrain = TerrainType::Grassland;
                    }
                } else if (temperature >= 0.45f) {
                    if (moisture < 0.22f) {
                        terrain = TerrainType::Desert;
                    } else if (moisture < 0.50f) {
                        terrain = TerrainType::Plains;
                    } else {
                        terrain = TerrainType::Grassland;
                    }
                } else {
                    if (moisture < 0.35f) {
                        terrain = TerrainType::Plains;
                    } else {
                        terrain = TerrainType::Grassland;
                    }
                }
            }

            grid.setTerrain(index, terrain);
            grid.setElevation(index, static_cast<int8_t>(
                std::clamp(static_cast<int>(elev * 4.0f), 0, 2)));

            // 2026-05-04: MULTI-SOURCE HILL PLACEMENT. Real Earth has
            // hills in four distinct geological settings, not just
            // around active mountain belts. Old code only modeled the
            // first source (foothill belt) and produced rolling-hill
            // dead zones across cratons and glacial lowlands.
            //
            //  (1) FOOTHILL BELT -- tile sits on (or right next to)
            //      a strong-orogeny tile, i.e. an active mountain
            //      belt's outer apron (Sub-Andean Sierras, Bavarian
            //      Alpine foreland).
            //  (2) ERODED OROGEN REMNANTS -- tile is within 5 hex of
            //      a continent-continent suture seam, even if local
            //      orogeny has decayed. Models the Urals, Appalachians,
            //      Scottish Highlands. Probability ~30 % within band.
            //  (3) CRATONIC SHIELD HILLS -- tile is on an old
            //      continental plate (crustAge > 150 My) with high
            //      landFraction (> 0.4). Probability ~12 % via
            //      fractal noise. Models Canadian Shield ridges,
            //      Brazilian Highlands, Ethiopian/Deccan basaltic
            //      uplands.
            //  (4) GLACIAL MORAINE HILLS -- high latitudes
            //      (|ny - 0.5| > 0.30). Probability ~6 % via fractal
            //      noise. Models North German Plain drumlin fields,
            //      Finnish moraine ridges, Patagonian ice-margin
            //      hills.
            //
            // Order: foothill belt wins outright; otherwise the first
            // matching source places the hill. Existing features
            // (Forest, Jungle, Marsh, etc.) are NEVER overridden --
            // hills only land on tiles whose feature is currently
            // None. Mountain/Snow/Tundra/Ocean/Coast/ShallowWater
            // remain off-limits per the prior rule.
            const std::size_t indexU = static_cast<std::size_t>(index);
            const bool isFlatBiome = terrain != TerrainType::Mountain
                && terrain != TerrainType::Snow
                && terrain != TerrainType::Tundra
                && terrain != TerrainType::Ocean
                && terrain != TerrainType::Coast
                && terrain != TerrainType::ShallowWater;
            const bool featureSlotFree =
                grid.feature(index) == FeatureType::None;
            if (isFlatBiome && featureSlotFree) {
                bool placeHill = false;

                // (1) Foothill belt: BFS distance from any mountain tile
                // gives a falling-probability gradient. Replaces the old
                // immediate-neighbour orogeny rule, which produced an
                // unrealistic single-ring foothill apron.
                const uint8_t mDist = mountainDist[indexU];
                float hillChance = 0.0f;
                if      (mDist == 1) { hillChance = 0.80f; }
                else if (mDist == 2) { hillChance = 0.50f; }
                else if (mDist == 3) { hillChance = 0.20f; }
                else if (mDist == 4) { hillChance = 0.05f; }
                if (hillChance > 0.0f
                    && hillRng.nextFloat(0.0f, 1.0f) < hillChance) {
                    placeHill = true;
                }

                // (2) Eroded orogen remnant -- within 5 hex of a
                // continent-continent suture. ~30 % probability via
                // hashNoise so that unmodified seeds remain
                // reproducible run-to-run.
                if (!placeHill
                    && sutureDist[indexU] <= SUTURE_BAND_RADIUS) {
                    if (hillRng.nextFloat() < 0.30f) {
                        placeHill = true;
                    }
                }

                // (3) Cratonic shield hills -- old, predominantly-
                // continental plate. Use fractal noise on a coarser
                // scale so hill clusters form (not isolated pixels).
                if (!placeHill) {
                    const uint8_t pid = grid.plateId(index);
                    const auto& plateAge      = grid.plateCrustAge();
                    const auto& plateLandFrac = grid.plateLandFrac();
                    if (pid != 0xFFu
                        && pid < plateAge.size()
                        && pid < plateLandFrac.size()
                        && plateAge[pid]      > 150.0f
                        && plateLandFrac[pid] >   0.40f) {
                        const float n = fractalNoise(nx * 4.0f,
                            ny * 4.0f + 11.7f, 4, 2.0f, 0.5f, hillRng);
                        if (n > 0.88f) {  // ~12 % of tiles in band
                            placeHill = true;
                        }
                    }
                }

                // (4) Glacial moraine hills -- high latitude lowlands
                // that were under continental ice sheets (North
                // German Plain, Finnish Lake Plateau, Patagonian
                // pampas margins). ~6 % of tiles in the band.
                if (!placeHill) {
                    if (std::abs(ny - 0.5f) > 0.30f) {
                        const float n = fractalNoise(nx * 5.0f,
                            ny * 5.0f + 23.1f, 4, 2.0f, 0.5f, hillRng);
                        if (n > 0.94f) {  // ~6 % of tiles in band
                            placeHill = true;
                        }
                    }
                }

                if (placeHill) {
                    grid.setFeature(index, FeatureType::Hills);
                }
            }
        }
    }
}

} // namespace aoc::map::gen
