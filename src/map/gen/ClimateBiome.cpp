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
    {
        std::vector<std::pair<float, int32_t>> landOroIdx;
        landOroIdx.reserve(static_cast<std::size_t>(totalTiles));
        for (int32_t i = 0; i < totalTiles; ++i) {
            if (elevationMap[static_cast<std::size_t>(i)] >= waterThreshold) {
                landOroIdx.emplace_back(
                    orogeny[static_cast<std::size_t>(i)], i);
            }
        }
        if (!landOroIdx.empty()) {
            const std::size_t n = landOroIdx.size();
            const std::size_t mountainQuota =
                static_cast<std::size_t>(static_cast<double>(n) * 0.05);
            if (mountainQuota > 0 && mountainQuota < n) {
                std::nth_element(
                    landOroIdx.begin(),
                    landOroIdx.begin()
                        + static_cast<std::ptrdiff_t>(mountainQuota),
                    landOroIdx.end(),
                    [](const std::pair<float, int32_t>& a,
                       const std::pair<float, int32_t>& b) {
                        return a.first > b.first;
                    });
                for (std::size_t i = 0; i < mountainQuota; ++i) {
                    if (landOroIdx[i].first >= 0.08f) {
                        isMountainTile[static_cast<std::size_t>(
                            landOroIdx[i].second)] = 1u;
                    }
                }
            }
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
            const float oroValue = orogeny[indexU];
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

                // (1) Foothill belt: same rule as before -- orogeny
                // > 0.10, OR moderate orogeny with a neighbour > 0.12.
                if (oroValue > 0.06f) {
                    if (oroValue > 0.10f) {
                        placeHill = true;
                    } else {
                        const int32_t dr_even[6] = {0, 0, -1, -1, +1, +1};
                        const int32_t dc_even[6] = {-1, +1, -1,  0, -1,  0};
                        const int32_t dc_odd[6]  = {-1, +1,  0, +1,  0, +1};
                        const bool evenRow = ((row & 1) == 0);
                        for (int32_t k = 0; k < 6; ++k) {
                            int32_t nr = row + dr_even[k];
                            int32_t nc = col +
                                (evenRow ? dc_even[k] : dc_odd[k]);
                            if (nr < 0 || nr >= height) { continue; }
                            if (cylClim) {
                                nc = ((nc % width) + width) % width;
                            } else if (nc < 0 || nc >= width) {
                                continue;
                            }
                            if (orogeny[static_cast<std::size_t>(
                                    nr * width + nc)] > 0.12f) {
                                placeHill = true;
                                break;
                            }
                        }
                    }
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
