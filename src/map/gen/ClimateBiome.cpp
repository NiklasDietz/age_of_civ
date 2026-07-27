/**
 * @file ClimateBiome.cpp
 * @brief Climate + biome implementation.
 */

#include "aoc/map/gen/ClimateBiome.hpp"

#include "aoc/map/HexGrid.hpp"
#include "aoc/map/Terrain.hpp"
#include "aoc/map/gen/Noise.hpp"
#include "aoc/map/gen/Relief.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace aoc::map::gen {

namespace {

constexpr float DEG_TO_RAD = 0.01745329252f;

// ============================================================================
// Physical climate calibration
// ============================================================================
//
// Until 2026-07-27 `temperature` here was a dimensionless 0-1 knob compared
// against thresholds (0.12 Snow, 0.25 Tundra, 0.45 / 0.65 warm bands) that had
// been tuned while latitude itself was wrong. The old code reconstructed
// latitude as `2 * |row/height - 0.5|`, which under a projection where row is
// linear in sin(latitude) equals sin|lat|, not |lat| / 90. Feeding
// `cos(latFrac * pi/2)` with sin|lat| falls off far faster than feeding it
// |lat| / 90, so those thresholds landed in plausible places for the wrong
// reason -- Tundra began near 57 deg and Snow near 65 deg. When the projection
// work supplied a true latitude the same numbers moved to 75.5 deg and 83 deg,
// which is what put Grassland above 66 deg and collapsed Tundra to 0.1 % of
// land.
//
// The fix is not to re-tune the knob. `temperature` is now mean annual
// near-surface air temperature in DEGREES CELSIUS and every threshold below is
// a real climatological isotherm, so the calibration is citable rather than
// fitted, and the next latitude change cannot silently move a biome band.

/// Zonal mean annual near-surface air temperature at sea level, Celsius:
///     T(phi) = MAT_POLE_C + (MAT_EQUATOR_C - MAT_POLE_C) * cos^n(|phi|)
///
/// Fitted to the observed hemisphere-averaged zonal annual mean -- 26 / 20 /
/// 14 / 7.5 / 0 / -8 / -16 / -22 C at 0 / 30 / 40 / 50 / 60 / 70 / 80 / 90 deg
/// -- which it reproduces to within 1.7 C in every 10-degree band, well inside
/// the north-south asymmetry it averages over. The residual is the equatorial
/// ITCZ plateau, which this monotone form cannot represent and which does not
/// matter here because both latitudes it affects are far inside one biome band.
constexpr float MAT_EQUATOR_C    = 26.0f;
constexpr float MAT_POLE_C       = -22.0f;
constexpr float MAT_COS_EXPONENT = 1.1f;

// The biome isotherms themselves live in ClimateBiome.hpp so a unit test can
// pin them. What they mean:
//
// The terrain palette has no boreal-forest type, so the cold sequence is
// permanent ice / tundra / vegetated. ISO_TREE_LINE_C (-7 C) is where the 10 C
// warmest-month tree line runs on Earth (~70 deg at sea level);
// ISO_PERMANENT_ICE_C (-15 C) is ~80 deg. ISO_SUBTROPICAL_C (15 C) separates
// the subtropical-and-warmer belt -- equatorward of ~38 deg, which is where
// Earth's hot deserts sit -- from the temperate belt, and ISO_TEMPERATE_C (2 C)
// separates temperate from boreal at ~58 deg.

/// Hills threshold: local relief in metres per 100 km of ground distance.
///
/// The floor of rolling hill country. Cratonic interiors and coastal plains sit
/// well below it (measured p50 of non-mountain land is 35-107 m/100km across
/// seeds 42/7/100/200/777), dissected uplands and mountain aprons well above
/// (p90 306-404, p99 1170-2006). 200 selects roughly the top quarter of
/// non-mountain land, which is where Earth's ~25 % hill share sits.
constexpr float HILL_RELIEF_M_PER_100KM = 200.0f;

/// Environmental lapse rate, ICAO standard atmosphere.
constexpr float LAPSE_RATE_C_PER_KM = 6.5f;

/// `elevationMap` is `surfaceElevationM / 5000` (MapGenerator's world-frame
/// sample), so one unitless elevation step is this many metres.
constexpr float ELEV_UNIT_TO_M = 5000.0f;

/// Palaeoclimate state offsets applied to the whole field. Magnitudes are the
/// observed global-mean anomalies: the Cretaceous greenhouse ran ~+5 C above
/// modern, the Last Glacial Maximum ~-6 C.
constexpr float GREENHOUSE_ANOMALY_C = 5.0f;
constexpr float ICEHOUSE_ANOMALY_C   = -6.0f;

/// Milankovitch orbital forcing, peak regional excursion.
constexpr float MILANKOVITCH_AMPLITUDE_C = 5.0f;

/// Residual regional deviation from the zonal mean, i.e. everything this pass
/// does not model explicitly. Smaller than the +/-5.3 C the old dimensionless
/// coefficient implied, because continentality, ocean currents, orography and
/// monsoon are now separate explicit terms rather than folded into the noise.
constexpr float REGIONAL_NOISE_C = 3.0f;

/// Continentality: annual-mean shift for a deep continental interior relative
/// to a maritime tile at the same latitude. Positive equatorward (interiors
/// bake), negative poleward (interiors lose more in winter than they gain in
/// summer).
constexpr float CONTINENTALITY_WARM_C = 3.0f;
constexpr float CONTINENTALITY_COLD_C = -5.0f;

/// Obliquity sensitivity: Celsius of polar temperature shift per unit relative
/// change in annual-mean polar insolation. Annual-mean insolation at a pole
/// scales as sin(tilt) -- the pole is sunlit for half the year with peak solar
/// elevation equal to the tilt -- while equatorial annual-mean insolation is
/// nearly tilt-independent, so obliquity flattens the gradient from the polar
/// end. The insolation ratio is physical; this conversion to a temperature
/// shift is a stated parameterisation, and it is a no-op at Earth's 23.5 deg.
constexpr float POLE_TILT_SENSITIVITY_C = 30.0f;
constexpr float EARTH_AXIAL_TILT_DEG    = 23.5f;

/// The zonal temperature profile evaluated between explicit endpoints, so the
/// obliquity-adjusted pole can be substituted for the Earth default.
///
/// Takes |latitude| itself rather than trusting the caller to: clamping a
/// signed latitude into [0, 90] would silently return the EQUATORIAL
/// temperature for anything in the southern hemisphere, which is a far worse
/// failure than a wrong sign.
[[nodiscard]] float zonalTempFromEndpoints(float latDeg, float poleC, float rangeC) {
    const float absLatDeg = std::clamp(std::abs(latDeg), 0.0f, 90.0f);
    const float cosLat    = std::max(0.0f, std::cos(absLatDeg * DEG_TO_RAD));
    return poleC + rangeC * std::pow(cosLat, MAT_COS_EXPONENT);
}

} // namespace

float zonalMeanAnnualTempC(float absLatDeg) {
    return zonalTempFromEndpoints(absLatDeg, MAT_POLE_C, MAT_EQUATOR_C - MAT_POLE_C);
}

/// Zonal mean precipitation as a 0-1 moisture index, following the general
/// circulation instead of a single V-shaped ramp.
///
/// The old profile put its dry minimum at one latitude (28.8 deg) with a wet
/// ramp either side, so the driest 10-degree BAND came out at 30-40 deg and
/// subtropical deserts were under-represented -- measured 8.1 % of land in
/// 20-30 deg against 10.5 % in 30-40 deg, the wrong way round. Earth's arid
/// belt is a plateau, not a point: the Sahara, Arabia, the Kalahari, the
/// Australian interior and the Atacama all sit inside 18-32 deg.
///
///   0-8 deg     ITCZ, ascending branch           wet  0.85
///   8-18 deg    Hadley descent                   ramp
///   18-32 deg   subtropical high, arid plateau   dry  0.22
///   32-45 deg   storm-track margin               ramp
///   45-62 deg   midlatitude westerlies           wet  0.62
///   62-90 deg   polar cell, cold and dry         ramp to 0.28
float zonalMoisture(float latDeg) {
    struct Node {
        float latDeg;
        float moisture;
    };
    static constexpr Node PROFILE[] = {
        {0.0f, 0.85f},  {8.0f, 0.85f},  {18.0f, 0.22f}, {32.0f, 0.22f},
        {45.0f, 0.62f}, {62.0f, 0.62f}, {90.0f, 0.28f},
    };
    constexpr std::size_t COUNT = sizeof(PROFILE) / sizeof(PROFILE[0]);
    // |latitude| taken here, not trusted from the caller, for the same reason as
    // zonalTempFromEndpoints: clamping a signed latitude would silently hand
    // back the ITCZ value for the southern hemisphere.
    const float lat = std::clamp(std::abs(latDeg), 0.0f, 90.0f);
    for (std::size_t k = 1; k < COUNT; ++k) {
        if (lat <= PROFILE[k].latDeg) {
            const float span = PROFILE[k].latDeg - PROFILE[k - 1].latDeg;
            const float t    = (span > 0.0f) ? (lat - PROFILE[k - 1].latDeg) / span : 0.0f;
            return PROFILE[k - 1].moisture + t * (PROFILE[k].moisture - PROFILE[k - 1].moisture);
        }
    }
    return PROFILE[COUNT - 1].moisture;
}

void runClimateBiomePass(HexGrid& grid, const MapGenerator::Config& config, aoc::Random& rng,
                         const std::vector<float>& elevationMap,
                         const std::vector<int32_t>& distFromCoast,
                         const std::vector<float>& orogeny, const std::vector<uint8_t>& isWater,
                         float waterThreshold) {
    auto isW                 = [&](std::size_t idx) -> bool { return isWater[idx] != 0u; };
    const int32_t width      = grid.width();
    const int32_t height     = grid.height();
    const int32_t totalTiles = grid.tileCount();

    // One lattice seed per noise FIELD, drawn once here instead of once per
    // call (see the note in Noise.hpp -- drawing inside the per-tile call gave
    // every tile its own lattice, so these were white noise rather than
    // fields). Separate seeds keep the fields independent of each other; the
    // fixed draw order keeps all of them deterministic w.r.t. config.seed.
    const uint64_t milankovitchSeed = rng.next();
    const uint64_t temperatureSeed  = rng.next();
    const uint64_t moistureSeed     = rng.next();
    // 2026-05-04: dedicated RNG stream for the multi-source hill placement
    // (foothill belt + suture remnants + cratonic shield + glacial moraine).
    // Drawn from the same parent rng so output is deterministic w.r.t.
    // config.seed but doesn't disturb the temperature/moisture streams. Still a
    // stream rather than a seed because the foothill and suture rules make
    // probability draws, not field samples.
    aoc::Random hillRng(rng.next());

    int32_t maxCoastDist = 1;
    for (int32_t i = 0; i < totalTiles; ++i) {
        maxCoastDist = std::max(maxCoastDist, distFromCoast[static_cast<std::size_t>(i)]);
    }

    constexpr int32_t WIND_WALK_RANGE = 14;
    std::vector<float> windMoist(static_cast<std::size_t>(totalTiles), 0.0f);
    const bool cylClim = (grid.topology() == aoc::map::MapTopology::Cylindrical);
    // Surface wind belt: which way is UPWIND. Easterly trades equatorward of
    // 30 deg, westerlies 30-60 deg, polar easterlies poleward of 60 deg -- so
    // upwind is east (+1) in the trade and polar cells and west (-1) in the
    // westerlies.
    //
    // 2026-07-27: this was the 47th pseudo-latitude site and the projection
    // migration missed it, because it took `row / height` as a parameter named
    // `lat` rather than reading the grid. Under Lambert `row / height` is
    // (sin(lat) + 1) / 2, so the belt boundaries sat at 17.5 deg and 37 deg
    // instead of 30 deg and 60 deg -- the westerlies covered less than half
    // their real span and the polar easterlies started in the temperate zone.
    auto upwindStep = [](float absLatDeg) -> int32_t {
        const bool westerlies = (absLatDeg >= 30.0f && absLatDeg < 60.0f);
        return westerlies ? -1 : +1;
    };
    for (int32_t row = 0; row < height; ++row) {
        const int32_t step = upwindStep(std::abs(grid.rowLatitudeDeg(row)));
        for (int32_t col = 0; col < width; ++col) {
            const int32_t idx = row * width + col;
            if (isW(static_cast<std::size_t>(idx))) {
                continue;
            }
            float carry       = 0.0f;
            bool reachedOcean = false;
            for (int32_t s = 1; s <= WIND_WALK_RANGE; ++s) {
                int32_t uc = col + step * s;
                if (cylClim) {
                    uc = ((uc % width) + width) % width;
                } else if (uc < 0 || uc >= width) {
                    break;
                }
                const int32_t uidx = row * width + uc;
                if (isW(static_cast<std::size_t>(uidx))) {
                    carry = 1.0f - static_cast<float>(s) / static_cast<float>(WIND_WALK_RANGE);
                    reachedOcean = true;
                    break;
                }
            }
            if (!reachedOcean) {
                carry = -0.10f;
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
                if (isW(static_cast<std::size_t>(row * width + col))) {
                    lastWaterCol = col - width;
                    break;
                }
            }
        }
        for (int32_t col = 0; col < width; ++col) {
            const int32_t idx = row * width + col;
            if (isW(static_cast<std::size_t>(idx))) {
                lastWaterCol                                 = col;
                westOceanDist[static_cast<std::size_t>(idx)] = 0;
            } else {
                westOceanDist[static_cast<std::size_t>(idx)] = std::min(width, col - lastWaterCol);
            }
        }
        int32_t nextWaterCol = 2 * width;
        if (cylClim) {
            for (int32_t col = width - 1; col >= 0; --col) {
                if (isW(static_cast<std::size_t>(row * width + col))) {
                    nextWaterCol = col + width;
                    break;
                }
            }
        }
        for (int32_t col = width - 1; col >= 0; --col) {
            const int32_t idx = row * width + col;
            if (isW(static_cast<std::size_t>(idx))) {
                nextWaterCol                                 = col;
                eastOceanDist[static_cast<std::size_t>(idx)] = 0;
            } else {
                eastOceanDist[static_cast<std::size_t>(idx)] = std::min(width, nextWaterCol - col);
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
    std::vector<int32_t> sutureDist(static_cast<std::size_t>(totalTiles), SUTURE_BAND_RADIUS + 1);
    {
        const auto& plateLandFrac    = grid.plateLandFrac();
        const auto& plateMergesAbsor = grid.plateMergesAbsorbed();
        const bool platesAvailable   = !plateLandFrac.empty() && !plateMergesAbsor.empty();
        if (platesAvailable) {
            // Seed BFS with seam tiles (continent-continent boundary).
            std::vector<int32_t> frontier;
            frontier.reserve(static_cast<std::size_t>(totalTiles) / 8u);
            auto isContinentalPlate = [&](uint8_t pid) -> bool {
                if (pid == 0xFFu || pid >= plateLandFrac.size()) {
                    return false;
                }
                return plateLandFrac[pid] > 0.40f;
            };
            const int32_t dr_even_n[6] = {0, 0, -1, -1, +1, +1};
            const int32_t dc_even_n[6] = {-1, +1, -1, 0, -1, 0};
            const int32_t dc_odd_n[6]  = {-1, +1, 0, +1, 0, +1};
            for (int32_t row = 0; row < height; ++row) {
                const bool evenRow = ((row & 1) == 0);
                for (int32_t col = 0; col < width; ++col) {
                    const int32_t idx = row * width + col;
                    const uint8_t pid = grid.plateId(idx);
                    if (!isContinentalPlate(pid)) {
                        continue;
                    }
                    bool seam = false;
                    for (int32_t k = 0; k < 6 && !seam; ++k) {
                        int32_t nr = row + dr_even_n[k];
                        int32_t nc = col + (evenRow ? dc_even_n[k] : dc_odd_n[k]);
                        if (nr < 0 || nr >= height) {
                            continue;
                        }
                        if (cylClim) {
                            nc = ((nc % width) + width) % width;
                        } else if (nc < 0 || nc >= width) {
                            continue;
                        }
                        const int32_t nidx = nr * width + nc;
                        const uint8_t npid = grid.plateId(nidx);
                        if (npid == pid) {
                            continue;
                        }
                        if (!isContinentalPlate(npid)) {
                            continue;
                        }
                        // At least one of the two plates must have
                        // absorbed a merger -- screens out trivial
                        // (non-collisional) Voronoi neighbours.
                        const bool merged =
                            (pid < plateMergesAbsor.size() && plateMergesAbsor[pid] > 0) ||
                            (npid < plateMergesAbsor.size() && plateMergesAbsor[npid] > 0);
                        if (merged) {
                            seam = true;
                        }
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
                    const int32_t fr   = fIdx / width;
                    const int32_t fc   = fIdx % width;
                    const bool evenRow = ((fr & 1) == 0);
                    for (int32_t k = 0; k < 6; ++k) {
                        int32_t nr = fr + dr_even_n[k];
                        int32_t nc = fc + (evenRow ? dc_even_n[k] : dc_odd_n[k]);
                        if (nr < 0 || nr >= height) {
                            continue;
                        }
                        if (cylClim) {
                            nc = ((nc % width) + width) % width;
                        } else if (nc < 0 || nc >= width) {
                            continue;
                        }
                        const int32_t nidx = nr * width + nc;
                        if (sutureDist[static_cast<std::size_t>(nidx)] <= depth + 1) {
                            continue;
                        }
                        sutureDist[static_cast<std::size_t>(nidx)] = depth + 1;
                        nextFrontier.push_back(nidx);
                    }
                }
                frontier = std::move(nextFrontier);
                if (frontier.empty()) {
                    break;
                }
            }
        }
    }

    // Obliquity shifts the pole and leaves the equator alone (see
    // POLE_TILT_SENSITIVITY_C). Loop-invariant, so resolve the profile's two
    // endpoints once.
    const float tiltSinRatio = std::sin(std::clamp(config.axialTilt, 0.0f, 90.0f) * DEG_TO_RAD) /
                               std::sin(EARTH_AXIAL_TILT_DEG * DEG_TO_RAD);
    const float poleC        = MAT_POLE_C + (tiltSinRatio - 1.0f) * POLE_TILT_SENSITIVITY_C;
    const float rangeC       = MAT_EQUATOR_C - poleC;

    // The lapse rate wants height above sea level, but continental freeboard is
    // currently 3.7-4.5 km too high: the Airy mantle datum was calibrated from
    // the oceanic branch alone, so every craton sits on a plateau that the
    // fixed-volume sea-level solve cannot remove. Feeding 6.5 C/km an absolute
    // elevation would put every continental interior ~24 C below its zonal
    // baseline and freeze the planet.
    //
    // So until the elevation law is replaced, the lapse rate is applied to
    // relief above the MEDIAN land elevation. That is the same relief-relative
    // bridge the mountain criterion needs, it is correct for the
    // upland-vs-lowland contrast the biome map actually depends on, and it
    // becomes an absolute elevation for free once freeboard is right -- at
    // which point the median land elevation is a few hundred metres and this
    // term stops mattering.
    float landElevReference = waterThreshold;
    {
        std::vector<float> landElev;
        landElev.reserve(static_cast<std::size_t>(totalTiles));
        for (int32_t i = 0; i < totalTiles; ++i) {
            if (!isW(static_cast<std::size_t>(i))) {
                landElev.push_back(elevationMap[static_cast<std::size_t>(i)]);
            }
        }
        if (!landElev.empty()) {
            const std::ptrdiff_t mid = static_cast<std::ptrdiff_t>(landElev.size() / 2u);
            std::nth_element(landElev.begin(), landElev.begin() + mid, landElev.end());
            landElevReference = landElev[static_cast<std::size_t>(mid)];
        }
    }

    for (int32_t row = 0; row < height; ++row) {
        for (int32_t col = 0; col < width; ++col) {
            int32_t index = row * width + col;
            float elev    = elevationMap[static_cast<std::size_t>(index)];

            if (isW(static_cast<std::size_t>(index))) {
                grid.setTerrain(index, TerrainType::Ocean);
                grid.setElevation(index, -1);
                continue;
            }

            // Mountain: orogeny[] is 1.0 exactly where the world-frame elevation
            // pass found the tile's peak SphereField sample above
            // MOUNTAIN_THRESHOLD_M, so the decision is already made upstream and
            // this is a straight relabel. The percentile gate that used to
            // participate here is gone, which is why the pass no longer takes a
            // mountain threshold or a mountain-elevation array.
            const float oroAt = orogeny[static_cast<std::size_t>(index)];
            if (oroAt > 0.5f) {
                grid.setTerrain(index, TerrainType::Mountain);
                grid.setElevation(index, 3);
                continue;
            }

            float nx = static_cast<float>(col) / static_cast<float>(width);
            float ny = static_cast<float>(row) / static_cast<float>(height);

            const float absLatDeg = std::abs(grid.rowLatitudeDeg(row));

            // Zonal baseline: mean annual near-surface air temperature at sea
            // level, Celsius.
            const float zonalBase = zonalTempFromEndpoints(absLatDeg, poleC, rangeC);
            float temperature     = zonalBase;

            if (config.climatePhase == 1) {
                temperature += GREENHOUSE_ANOMALY_C;
            } else if (config.climatePhase == 2) {
                temperature += ICEHOUSE_ANOMALY_C;
            }
            if (config.milankovitchPhase > 0.05f) {
                const float dev =
                    (fractalNoise(nx * 1.5f, ny * 1.5f, 2, 2.0f, 0.5f, milankovitchSeed) - 0.5f) *
                    2.0f;
                temperature += dev * config.milankovitchPhase * MILANKOVITCH_AMPLITUDE_C;
            }
            // Environmental lapse rate on relief above the land median (see
            // landElevReference above for why it is not absolute elevation).
            const float reliefKm =
                std::max(0.0f, elev - landElevReference) * ELEV_UNIT_TO_M * 0.001f;
            temperature -= reliefKm * LAPSE_RATE_C_PER_KM;
            temperature += (fractalNoise(nx, ny, 3, 3.0f, 0.5f, temperatureSeed) - 0.5f) * 2.0f *
                           REGIONAL_NOISE_C;

            // Position in the equator-to-pole range, 0 at the pole and 1 at the
            // equator. The ocean-current terms below scale by this because
            // advected heat changes a cold place more than a warm one; it keeps
            // the exact meaning the old dimensionless `temperature` had in those
            // expressions.
            const float zonalNorm  = std::clamp((zonalBase - poleC) / rangeC, 0.0f, 1.0f);
            const float warmFactor = 1.0f - zonalNorm;
            const float coldFactor = zonalNorm;

            const float moistureBase = zonalMoisture(absLatDeg);

            const float coastDist =
                static_cast<float>(distFromCoast[static_cast<std::size_t>(index)]);
            const float continentalFactor =
                std::clamp(coastDist / (static_cast<float>(maxCoastDist) * 0.70f), 0.0f, 1.0f);

            constexpr int32_t CURRENT_RANGE = 12;
            const int32_t wd                = westOceanDist[static_cast<std::size_t>(index)];
            const int32_t ed                = eastOceanDist[static_cast<std::size_t>(index)];
            const float westProx =
                std::max(0.0f, 1.0f - static_cast<float>(wd) / static_cast<float>(CURRENT_RANGE));
            const float eastProx =
                std::max(0.0f, 1.0f - static_cast<float>(ed) / static_cast<float>(CURRENT_RANGE));

            // Ocean-current advection, in Celsius. Magnitudes are the observed
            // same-latitude contrasts: the Humboldt and Benguela cold upwellings
            // hold subtropical west coasts 5-10 C below their zonal mean, while
            // the North Atlantic Drift puts the Norwegian coast 10-15 C above
            // Labrador at the same latitude.
            float currentTempDelta  = 0.0f;
            float currentMoistDelta = 0.0f;
            if (absLatDeg >= 9.0f && absLatDeg < 36.0f) {
                currentTempDelta += -10.0f * westProx * coldFactor + 5.0f * eastProx * warmFactor;
                currentMoistDelta += -0.32f * westProx + 0.22f * eastProx;
            } else if (absLatDeg >= 36.0f && absLatDeg < 63.0f) {
                currentTempDelta += 15.0f * westProx * warmFactor - 7.0f * eastProx * coldFactor;
                currentMoistDelta += 0.28f * westProx + 0.04f * eastProx;
            } else if (absLatDeg >= 63.0f) {
                currentTempDelta += 14.0f * westProx * warmFactor - 4.0f * eastProx * coldFactor;
                currentMoistDelta += 0.15f * westProx;
            }

            // Continentality. Interiors bake equatorward of ~40 deg and lose
            // more to winter than they gain in summer poleward of it.
            temperature += continentalFactor *
                           ((absLatDeg < 40.0f) ? CONTINENTALITY_WARM_C : CONTINENTALITY_COLD_C);
            temperature += currentTempDelta;
            // Physical envelope, not a normalisation: keeps the field finite
            // without pinning any biome boundary. Earth's record range is
            // -89 C to +57 C for instantaneous surface air; an annual MEAN
            // outside [-80, +60] is a bug upstream, not a climate.
            temperature = std::clamp(temperature, -80.0f, 60.0f);

            const float windMoistTile = windMoist[static_cast<std::size_t>(index)];

            float monsoonBoost = 0.0f;
            if (absLatDeg >= 9.0f && absLatDeg < 36.0f) {
                const float oceanProx = std::max(westProx, eastProx);
                monsoonBoost          = 0.18f * oceanProx * (1.0f - continentalFactor * 0.6f);
            }

            float ensoDelta = 0.0f;
            if (config.ensoState != 0 && absLatDeg < 18.0f) {
                const float skew = (config.ensoState == 1) ? +1.0f : -1.0f;
                ensoDelta        = skew * (eastProx - westProx) * 0.18f;
            }
            const float moisture = std::clamp(
                moistureBase - continentalFactor * 0.32f + currentMoistDelta +
                    windMoistTile * 0.45f + monsoonBoost + ensoDelta +
                    (fractalNoise(nx * 1.5f, ny * 1.5f + 7.3f, 3, 4.0f, 0.5f, moistureSeed) -
                     0.5f) *
                        0.28f,
                0.0f, 1.0f);

            // Biome from mean annual temperature x moisture. The temperature
            // cuts are the isotherms declared at the top of this file; the
            // moisture cuts stay dimensionless because moisture here is still an
            // index, not a precipitation rate.
            TerrainType terrain;
            if (temperature < ISO_PERMANENT_ICE_C) {
                terrain = TerrainType::Snow;
            } else if (temperature < ISO_TREE_LINE_C) {
                terrain = TerrainType::Tundra;
            } else if (temperature >= ISO_SUBTROPICAL_C) {
                // Subtropical and warmer: the arid threshold is lower than in
                // the temperate band because the same rainfall supports less
                // vegetation once evaporation rises.
                if (moisture < 0.20f) {
                    terrain = TerrainType::Desert;
                } else if (moisture < 0.65f) {
                    terrain = TerrainType::Plains;
                } else {
                    terrain = TerrainType::Grassland;
                }
            } else if (temperature >= ISO_TEMPERATE_C) {
                if (moisture < 0.22f) {
                    terrain = TerrainType::Desert;
                } else if (moisture < 0.50f) {
                    terrain = TerrainType::Plains;
                } else {
                    terrain = TerrainType::Grassland;
                }
            } else {
                // Boreal, -7 to 2 C (~58-70 deg). Always Plains, never
                // Grassland: temperate grassland is a warm-climate biome that
                // stops around 55 deg on Earth -- the Canadian prairies, the
                // Eurasian steppe and the Pampas all sit equatorward of it --
                // and what actually covers 58-70 deg is boreal forest. The
                // palette has no taiga type, so the honest composition is Plains
                // carrying the Forest feature the vegetation pass already places
                // on 30-47 % of this band. Grassland here was the other half of
                // the reported "Grassland above 66 degrees".
                terrain = TerrainType::Plains;
            }

            grid.setTerrain(index, terrain);
            grid.setElevation(index,
                              static_cast<int8_t>(std::clamp(static_cast<int>(elev * 4.0f), 0, 2)));

            // HILL PLACEMENT.
            //
            // 2026-07-27: rewritten onto measured local relief. The previous
            // version had four sources and three of them could not fire:
            //
            //  (1) FOOTHILL BELT keyed on `mountainDist`, a BFS distance field
            //      that was sentinel-filled to 0xFF and never populated, so
            //      every probability branch read "no mountain nearby".
            //  (3) CRATONIC SHIELD and (4) GLACIAL MORAINE were `fractalNoise`
            //      draws, and fractalNoise redrew its lattice per call, so they
            //      were per-tile coin flips rather than the hill CLUSTERS their
            //      comments described.
            //  (4) also gated on |lat| > 54 deg, where the tiles are mostly
            //      Snow or Tundra and therefore excluded two lines below --
            //      and then the ice-sheet pass cleared any hill it did place.
            //
            // Measured result: 1.1 % of land carried Hills against a ~25 %
            // Earth target, and the 60-90 deg bands had exactly zero.
            //
            // Hills are now what they are on the ground: land with real local
            // relief. `localReliefMPer100Km` measures it in metres per 100 km of
            // GROUND distance (not per tile, which would vary with latitude and
            // map width), and the threshold is the floor of rolling hill country.
            // The foothill belt falls out for free -- tiles beside a mountain
            // front have the steepest relief on the map by construction -- so the
            // dead BFS is deleted rather than revived.
            //
            // The suture-remnant source is kept, and is the one rule relief
            // cannot express: an eroded orogen like the Urals or the Scottish
            // Highlands is hill country precisely because it has been worn flat,
            // so no gradient survives to detect it.
            //
            // Not reinstated: glacial moraine fields need a real ice-sheet extent
            // (Pleistocene ice reached ~40 deg N in North America, nowhere near
            // the 54 deg the old rule used), and no such field exists yet.
            // Cratonic shield relief is now covered by the relief rule wherever
            // the shield actually has relief.
            const std::size_t indexU = static_cast<std::size_t>(index);
            const bool isFlatBiome =
                terrain != TerrainType::Mountain && terrain != TerrainType::Snow &&
                terrain != TerrainType::Tundra && terrain != TerrainType::Ocean &&
                terrain != TerrainType::Coast && terrain != TerrainType::ShallowWater;
            const bool featureSlotFree = grid.feature(index) == FeatureType::None;
            if (isFlatBiome && featureSlotFree) {
                bool placeHill = localReliefMPer100Km(grid, elevationMap, waterThreshold, col,
                                                      row) > HILL_RELIEF_M_PER_100KM;

                // Eroded orogen remnant -- within 5 hex of a continent-continent
                // suture. Deliberately probabilistic: the belt is hill COUNTRY,
                // not a solid band of hills.
                if (!placeHill && sutureDist[indexU] <= SUTURE_BAND_RADIUS) {
                    if (hillRng.nextFloat() < 0.30f) {
                        placeHill = true;
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
