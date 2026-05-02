/**
 * @file Resources.cpp
 * @brief Resource-placement passes (geology / basic / random / fair).
 *        Extracted 2026-05-02 from MapGenerator.cpp during the gen/ split.
 *        Definitions remain MapGenerator:: members so the public class API
 *        is unchanged — they just live in their own translation unit.
 */

#include "aoc/map/MapGenerator.hpp"
#include "aoc/map/HexCoord.hpp"
#include "aoc/map/gen/Noise.hpp"
#include "aoc/map/gen/PlateBoundary.hpp"
#include "aoc/core/Log.hpp"
#include "aoc/simulation/resource/ResourceTypes.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

namespace aoc::map {

using gen::hashNoise;
using gen::smoothstep;
using gen::lerp;

// Realistic map: geology-based resource placement
// ============================================================================

void MapGenerator::placeGeologyResources(const Config& config, HexGrid& grid,
                                          aoc::Random& rng) {
    const int32_t width  = grid.width();
    const int32_t height = grid.height();
    const int32_t tileCount = width * height;

    // Use REAL tectonic data captured by the Continents generator:
    // plateId per tile, plateMotions, plateCenters, plateLandFrac,
    // rockType, marginType, sedimentDepth, crustAgeTile. Earlier this
    // function rebuilt 4-6 fake plate seeds and ignored the actual
    // simulated tectonics — resources were placed against bogus
    // boundaries. Now we classify each tile's nearest plate-boundary
    // type by looking at the velocity-relative-to-neighbor direction
    // (same logic the renderer's PlateBoundaries overlay uses) and
    // place resources by real geology + rock type + margin type.
    const auto& realMotions = grid.plateMotions();
    const auto& realCenters = grid.plateCenters();
    const auto& realLandFr  = grid.plateLandFrac();
    const auto& realRock    = grid.rockType();
    const auto& realMargin  = grid.marginType();
    const auto& realSed     = grid.sedimentDepth();
    const auto& realAge     = grid.crustAgeTile();
    const bool  cylRes = (grid.topology() == aoc::map::MapTopology::Cylindrical);

    std::vector<BoundaryType> boundary(
        static_cast<std::size_t>(tileCount), BoundaryType::None);
    for (int32_t row = 0; row < height; ++row) {
        for (int32_t col = 0; col < width; ++col) {
            const int32_t index = row * width + col;
            const uint8_t myPid = grid.plateId(index);
            if (myPid == 0xFFu) { continue; }
            const hex::AxialCoord axial = hex::offsetToAxial({col, row});
            const std::array<hex::AxialCoord, 6> nbrs = hex::neighbors(axial);
            for (const hex::AxialCoord& n : nbrs) {
                if (!grid.isValid(n)) { continue; }
                const int32_t nIndex = grid.toIndex(n);
                const uint8_t nPid = grid.plateId(nIndex);
                if (nPid == 0xFFu || nPid == myPid) { continue; }
                if (myPid >= realMotions.size()
                    || nPid >= realMotions.size()) { continue; }
                const std::pair<float, float>& vA = realMotions[myPid];
                const std::pair<float, float>& vB = realMotions[nPid];
                const std::pair<float, float>& cA = realCenters[myPid];
                const std::pair<float, float>& cB = realCenters[nPid];
                float bnx = cB.first  - cA.first;
                float bny = cB.second - cA.second;
                if (cylRes) {
                    if (bnx >  0.5f) { bnx -= 1.0f; }
                    if (bnx < -0.5f) { bnx += 1.0f; }
                }
                const float bnLen = std::sqrt(bnx * bnx + bny * bny);
                if (bnLen < 1e-4f) { continue; }
                bnx /= bnLen; bny /= bnLen;
                const float relVx = vA.first  - vB.first;
                const float relVy = vA.second - vB.second;
                const float normProj = relVx * bnx + relVy * bny;
                const float tangProj = -relVx * bny + relVy * bnx;
                const float aN = std::abs(normProj);
                const float aT = std::abs(tangProj);
                if (aN > aT && aN > 0.02f) {
                    boundary[static_cast<std::size_t>(index)] =
                        (normProj > 0.0f)
                            ? BoundaryType::Convergent
                            : BoundaryType::Divergent;
                } else if (aT > 0.02f) {
                    boundary[static_cast<std::size_t>(index)] =
                        BoundaryType::Transform;
                }
                break;
            }
        }
    }

    // ---- Place resources based on geology zones ----

    aoc::Random resRng(rng.next());
    int32_t totalPlaced = 0;

    // auto required: lambda type is unnameable
    const auto isNearCoast = [&](int32_t row, int32_t col) -> bool {
        const hex::AxialCoord axial = hex::offsetToAxial({col, row});
        const std::array<hex::AxialCoord, 6> nbrs = hex::neighbors(axial);
        for (const hex::AxialCoord& n : nbrs) {
            if (grid.isValid(n) && isWater(grid.terrain(grid.toIndex(n)))) {
                return true;
            }
        }
        return false;
    };

    for (int32_t row = 0; row < height; ++row) {
        for (int32_t col = 0; col < width; ++col) {
            const int32_t index = row * width + col;
            const TerrainType terrain = grid.terrain(index);

            if (isWater(terrain) || terrain == TerrainType::Mountain) {
                continue;
            }

            // Already has a resource (from natural wonders, etc.)
            if (grid.resource(index).isValid()) {
                continue;
            }

            const BoundaryType bType = boundary[static_cast<std::size_t>(index)];
            const int8_t elev = grid.elevation(index);
            const float latitudeT = static_cast<float>(row) / static_cast<float>(height);
            const float temperature = 1.0f - 2.0f * std::abs(latitudeT - 0.5f);
            const bool nearCoast = isNearCoast(row, col);

            // Real-tectonic context per tile.
            const std::size_t sIdx = static_cast<std::size_t>(index);
            const uint8_t rType   = (sIdx < realRock.size())   ? realRock[sIdx]   : 0;
            const uint8_t mType   = (sIdx < realMargin.size()) ? realMargin[sIdx] : 0;
            const float   sedDep  = (sIdx < realSed.size())    ? realSed[sIdx]    : 0.0f;
            const float   tileAge = (sIdx < realAge.size())    ? realAge[sIdx]    : 0.0f;
            const uint8_t myPid   = grid.plateId(index);
            const float   landFr  = (myPid != 0xFFu && myPid < realLandFr.size())
                ? realLandFr[myPid] : 0.5f;

            ResourceId placed{};

            // Volcanic arc — convergent + mountain elevation. Cu+Au
            // porphyry deposits cluster on subduction arcs (Andes,
            // Carpathians). Rare-earth on alkaline intrusives.
            if (bType == BoundaryType::Convergent && elev >= 2) {
                if (resRng.chance(0.07f)) {
                    placed = ResourceId{aoc::sim::goods::COPPER_ORE};
                } else if (resRng.chance(0.05f)) {
                    placed = ResourceId{aoc::sim::goods::GOLD_ORE};
                } else if (resRng.chance(0.04f)) {
                    placed = ResourceId{aoc::sim::goods::SILVER_ORE};
                } else if (resRng.chance(0.04f)) {
                    placed = ResourceId{aoc::sim::goods::ALUMINUM};
                } else if (resRng.chance(0.03f)) {
                    placed = ResourceId{aoc::sim::goods::RARE_EARTH};
                }
            }
            // Lower-elevation convergent: foothills / forearc accretionary
            // wedge — tin (greisens), copper (volcanic-hosted massive
            // sulfide), gold (orogenic), silver.
            else if (bType == BoundaryType::Convergent) {
                if (resRng.chance(0.06f)) {
                    placed = ResourceId{aoc::sim::goods::COPPER_ORE};
                } else if (resRng.chance(0.05f)) {
                    placed = ResourceId{aoc::sim::goods::TIN};
                } else if (resRng.chance(0.04f)) {
                    placed = ResourceId{aoc::sim::goods::GOLD_ORE};
                } else if (resRng.chance(0.04f)) {
                    placed = ResourceId{aoc::sim::goods::SILVER_ORE};
                }
            }
            // Divergent boundary — continental rift basins (East African
            // Rift accumulates oil + gas in graben sediments). Mid-ocean
            // ridges proper are submarine (already filtered out: water).
            else if (bType == BoundaryType::Divergent) {
                if (elev <= 0 && resRng.chance(0.06f)) {
                    placed = ResourceId{aoc::sim::goods::OIL};
                } else if (resRng.chance(0.05f)) {
                    placed = ResourceId{aoc::sim::goods::NATURAL_GAS};
                } else if (resRng.chance(0.04f)) {
                    placed = ResourceId{aoc::sim::goods::COPPER_ORE};
                }
            }
            // Passive margin — wide sediment apron, prolific oil + gas.
            // Real Earth: Gulf of Mexico, North Sea, West African margin
            // host most offshore-onshore hydrocarbon basins. Salt domes
            // from old evaporite layers trap oil.
            else if (mType == 1 || mType == 2) {
                // Active margin (1) → uplifted, less sediment → fewer
                // oil traps. Passive (2) → sediment pile + salt domes.
                if (mType == 2) {
                    if (resRng.chance(0.13f)) {
                        placed = ResourceId{aoc::sim::goods::OIL};
                    } else if (resRng.chance(0.08f)) {
                        placed = ResourceId{aoc::sim::goods::NATURAL_GAS};
                    } else if (resRng.chance(0.05f)) {
                        placed = ResourceId{aoc::sim::goods::SALT};
                    } else if (resRng.chance(0.04f)) {
                        placed = ResourceId{aoc::sim::goods::COAL};
                    }
                } else { // active margin
                    if (resRng.chance(0.05f)) {
                        placed = ResourceId{aoc::sim::goods::COPPER_ORE};
                    } else if (resRng.chance(0.04f)) {
                        placed = ResourceId{aoc::sim::goods::GOLD_ORE};
                    }
                }
            }
            // Continental interior. Branch by rock type + age:
            //   Old craton (age > 100, metamorphic + igneous shield) →
            //     iron-ore (BIF), gold (orogenic gold in greenstone),
            //     stone, marble, gemstones (kimberlite pipes).
            //   Young / sediment-rich basin → oil, gas, coal, niter.
            else if (bType == BoundaryType::None) {
                const bool oldCraton = (tileAge > 100.0f
                    && (rType == 1 || rType == 2));
                const bool sedBasin  = (sedDep > 0.04f
                    || (rType == 0 && elev <= 1));
                if (oldCraton) {
                    // Cratonic kimberlite pipes host diamonds (S Africa,
                    // Botswana, Russia, Canada). Slot GEMS resource onto
                    // the oldest craton tiles as a proxy for diamonds.
                    if (tileAge > 130.0f && resRng.chance(0.025f)) {
                        placed = ResourceId{aoc::sim::goods::GEMS};
                    } else if (resRng.chance(0.10f)) {
                        placed = ResourceId{aoc::sim::goods::IRON_ORE};
                    } else if (resRng.chance(0.06f)) {
                        placed = ResourceId{aoc::sim::goods::GOLD_ORE};
                    } else if (resRng.chance(0.05f)) {
                        placed = ResourceId{aoc::sim::goods::STONE};
                    } else if (resRng.chance(0.04f)) {
                        placed = ResourceId{aoc::sim::goods::SILVER_ORE};
                    }
                } else if (sedBasin) {
                    if (resRng.chance(0.10f)) {
                        placed = ResourceId{aoc::sim::goods::OIL};
                    } else if (resRng.chance(0.06f)) {
                        placed = ResourceId{aoc::sim::goods::NATURAL_GAS};
                    } else if (resRng.chance(0.07f)) {
                        placed = ResourceId{aoc::sim::goods::COAL};
                    } else if (resRng.chance(0.03f)) {
                        placed = ResourceId{aoc::sim::goods::NITER};
                    }
                } else if (elev >= 2) {
                    if (resRng.chance(0.07f)) {
                        placed = ResourceId{aoc::sim::goods::IRON_ORE};
                    } else if (resRng.chance(0.05f)) {
                        placed = ResourceId{aoc::sim::goods::STONE};
                    }
                }
            }
            // Ophiolite-rock tiles (suture lines): chromite, copper,
            // platinum-group metals real Earth: Oman, Cyprus.
            if (!placed.isValid() && rType == 3) {
                if (resRng.chance(0.10f)) {
                    placed = ResourceId{aoc::sim::goods::COPPER_ORE};
                } else if (resRng.chance(0.06f)) {
                    placed = ResourceId{aoc::sim::goods::IRON_ORE};
                } else if (resRng.chance(0.03f)) {
                    placed = ResourceId{aoc::sim::goods::ALUMINUM};
                }
            }
            // BAUXITE / ALUMINUM via lateritic weathering: tropical lat
            // + Hills feature on old igneous bedrock. Real: Jamaica,
            // Guinea, Australia (Weipa), Brazil.
            if (!placed.isValid() && grid.feature(index)
                    == aoc::map::FeatureType::Hills) {
                const float laterite_lat =
                    static_cast<float>(row) / static_cast<float>(height);
                const float lat = 2.0f * std::abs(laterite_lat - 0.5f);
                if (lat < 0.25f && tileAge > 30.0f
                    && (rType == 1 || rType == 2)
                    && resRng.chance(0.06f)) {
                    placed = ResourceId{aoc::sim::goods::ALUMINUM};
                }
            }
            // URANIUM: sandstone-hosted (sediment basin + age) OR
            // IOCG-style at old craton + igneous host.
            if (!placed.isValid()) {
                const bool sandstone = (rType == 0
                    && sedDep > 0.05f
                    && tileAge > 40.0f);
                const bool iocg = (tileAge > 110.0f
                    && (rType == 1 || rType == 2));
                if ((sandstone || iocg) && resRng.chance(0.025f)) {
                    placed = ResourceId{aoc::sim::goods::URANIUM};
                }
            }
            // LITHIUM: salar brine in arid endorheic OR cratonic
            // pegmatite. Real: Salar de Uyuni / Atacama / Greenbushes.
            if (!placed.isValid()
                && grid.terrain(index) == aoc::map::TerrainType::Desert) {
                bool nearLake = false;
                const aoc::hex::AxialCoord axL =
                    aoc::hex::offsetToAxial({col, row});
                const std::array<aoc::hex::AxialCoord, 6> nbsL =
                    aoc::hex::neighbors(axL);
                for (const auto& n : nbsL) {
                    if (!grid.isValid(n)) { continue; }
                    const int32_t nIdxL = grid.toIndex(n);
                    if (grid.lakeFlag().size()
                            > static_cast<std::size_t>(nIdxL)
                        && grid.lakeFlag()[
                            static_cast<std::size_t>(nIdxL)] != 0) {
                        nearLake = true; break;
                    }
                }
                if (nearLake && resRng.chance(0.10f)) {
                    placed = ResourceId{aoc::sim::goods::LITHIUM};
                }
            }
            // RARE_EARTH bonus on continental rift volcanics
            // (carbonatite-hosted, Mountain Pass, Bayan Obo).
            if (!placed.isValid()
                && grid.volcanism().size() > sIdx
                && grid.volcanism()[sIdx] == 4
                && resRng.chance(0.025f)) {
                placed = ResourceId{aoc::sim::goods::RARE_EARTH};
            }
            // ----- Session 8 geology-driven specialty placement -----
            // NICKEL: laterite weathering (tropical Hills + age) OR
            // magmatic Ni-Cu (igneous + craton).
            if (!placed.isValid()) {
                const float ny0 = static_cast<float>(row)
                                / static_cast<float>(height);
                const float lat0 = 2.0f * std::abs(ny0 - 0.5f);
                if (lat0 < 0.20f
                    && grid.feature(index)
                            == aoc::map::FeatureType::Hills
                    && rType == 1
                    && resRng.chance(0.04f)) {
                    placed = ResourceId{aoc::sim::goods::NICKEL};
                } else if (rType == 1
                    && tileAge > 100.0f
                    && resRng.chance(0.025f)) {
                    placed = ResourceId{aoc::sim::goods::NICKEL};
                }
            }
            // COBALT: sed-Cu basins + magmatic. Old craton + igneous.
            if (!placed.isValid()
                && (rType == 1 || rType == 2)
                && tileAge > 100.0f
                && resRng.chance(0.02f)) {
                placed = ResourceId{aoc::sim::goods::COBALT};
            }
            // HELIUM: co-produced with natural-gas in old continental
            // basins. Sedimentary + age > 50.
            if (!placed.isValid()
                && rType == 0
                && tileAge > 50.0f
                && sedDep > 0.05f
                && resRng.chance(0.012f)) {
                placed = ResourceId{aoc::sim::goods::HELIUM};
            }
            // PLATINUM: ophiolite (PGM) + layered intrusion (igneous +
            // craton).
            if (!placed.isValid()) {
                if (rType == 3 && resRng.chance(0.04f)) {
                    placed = ResourceId{aoc::sim::goods::PLATINUM};
                } else if (rType == 1 && tileAge > 130.0f
                    && resRng.chance(0.02f)) {
                    placed = ResourceId{aoc::sim::goods::PLATINUM};
                }
            }
            // SULFUR: volcanic fumaroles + evaporite.
            if (!placed.isValid()) {
                const auto& vc = grid.volcanism();
                if (sIdx < vc.size()
                    && (vc[sIdx] == 1 || vc[sIdx] == 5)
                    && resRng.chance(0.05f)) {
                    placed = ResourceId{aoc::sim::goods::SULFUR};
                }
            }
            // GYPSUM: evaporite basin (passive margin or arid sed
            // basin).
            if (!placed.isValid()
                && rType == 0
                && (mType == 2
                    || grid.terrain(index) == aoc::map::TerrainType::Desert)
                && resRng.chance(0.04f)) {
                placed = ResourceId{aoc::sim::goods::GYPSUM};
            }
            // FLUORITE: hydrothermal vein. Convergent + Hills tier.
            if (!placed.isValid()
                && bType == BoundaryType::Convergent
                && grid.feature(index) == aoc::map::FeatureType::Hills
                && resRng.chance(0.025f)) {
                placed = ResourceId{aoc::sim::goods::FLUORITE};
            }
            // DOLOMITE: tropical carbonate / shelf platform — but we
            // skip water tiles (water already filtered out at top).
            // Place on temperate sediment + age (diagenetic).
            if (!placed.isValid()
                && rType == 0 && tileAge > 30.0f
                && grid.feature(index) == aoc::map::FeatureType::Hills
                && resRng.chance(0.03f)) {
                placed = ResourceId{aoc::sim::goods::DOLOMITE};
            }
            // BARITE: bedded sedimentary + hydrothermal at convergent.
            if (!placed.isValid()
                && (bType == BoundaryType::Convergent || rType == 0)
                && resRng.chance(0.02f)) {
                placed = ResourceId{aoc::sim::goods::BARITE};
            }
            // ALLUVIAL_GOLD: river-edge tile + cratonic source upstream.
            if (!placed.isValid()
                && grid.riverEdges(index) != 0
                && tileAge > 80.0f
                && resRng.chance(0.025f)) {
                placed = ResourceId{aoc::sim::goods::ALLUVIAL_GOLD};
            }
            // BEACH_PLACER: coastal land tile (heavy mineral sands).
            if (!placed.isValid() && nearCoast
                && grid.terrain(index) == aoc::map::TerrainType::Plains
                && resRng.chance(0.03f)) {
                placed = ResourceId{aoc::sim::goods::BEACH_PLACER};
            }
            // PYRITE: hydrothermal sulfide + sed.
            if (!placed.isValid()
                && (rType == 0 || bType == BoundaryType::Convergent)
                && resRng.chance(0.02f)) {
                placed = ResourceId{aoc::sim::goods::PYRITE};
            }
            // PHOSPHATE: biogenic — coastal land tiles in arid zones
            // adjacent to upwelling water.
            if (!placed.isValid()
                && nearCoast
                && grid.terrain(index) == aoc::map::TerrainType::Desert
                && resRng.chance(0.05f)) {
                placed = ResourceId{aoc::sim::goods::PHOSPHATE};
            }
            // VMS_ORE: volcanic massive sulfide — ophiolite-region rare.
            if (!placed.isValid()
                && rType == 3
                && resRng.chance(0.06f)) {
                placed = ResourceId{aoc::sim::goods::VMS_ORE};
            }
            // SKARN_ORE: contact metamorphic at intrusion-sediment
            // boundary. Mountain edge with rockType=2 (metamorphic) +
            // adjacent sed.
            if (!placed.isValid()
                && rType == 2
                && bType == BoundaryType::Convergent
                && resRng.chance(0.03f)) {
                placed = ResourceId{aoc::sim::goods::SKARN_ORE};
            }
            // MVT_ORE: Mississippi-Valley Pb-Zn — sediment + age,
            // continental interior carbonate platform proxy.
            if (!placed.isValid()
                && rType == 0 && tileAge > 80.0f
                && bType == BoundaryType::None
                && resRng.chance(0.025f)) {
                placed = ResourceId{aoc::sim::goods::MVT_ORE};
            }
            (void)landFr;

            // Climate-based resources (only if no geology resource was placed).
            // WP-C2 cut: INCENSE/IVORY/COFFEE/TOBACCO/TEA lines stripped —
            // those goods were dead-end luxuries with no downstream recipe.
            // Remaining lines pick up the probability mass.
            if (!placed.isValid()) {
                if (terrain == TerrainType::Desert) {
                    if (elev <= 0 && resRng.chance(0.04f)) {
                        placed = ResourceId{aoc::sim::goods::OIL};
                    } else if (resRng.chance(0.04f)) {
                        placed = ResourceId{aoc::sim::goods::NATURAL_GAS};
                    }
                } else if (temperature > 0.65f && terrain != TerrainType::Desert) {
                    // Tropical (wet, hot). Cotton / Rubber / Spices / Sugar only.
                    const bool jungle = grid.feature(index) == FeatureType::Jungle;
                    if (resRng.chance(0.04f)) {
                        placed = ResourceId{aoc::sim::goods::COTTON};
                    } else if (jungle && resRng.chance(0.05f)) {
                        placed = ResourceId{aoc::sim::goods::RUBBER};
                    } else if (resRng.chance(0.06f)) {
                        placed = ResourceId{aoc::sim::goods::SPICES};
                    } else if (resRng.chance(0.05f)) {
                        placed = ResourceId{aoc::sim::goods::SUGAR};
                    }
                } else if (temperature >= 0.55f && temperature <= 0.70f) {
                    // Subtropical band. SILK + WINE only.
                    if (resRng.chance(0.05f)) {
                        placed = ResourceId{aoc::sim::goods::SILK};
                    } else if (resRng.chance(0.05f)) {
                        placed = ResourceId{aoc::sim::goods::WINE};
                    }
                } else if (temperature >= 0.30f && temperature < 0.55f) {
                    // Temperate: bonus + luxury resources
                    if (resRng.chance(0.06f)) {
                        placed = ResourceId{aoc::sim::goods::WHEAT};
                    } else if (resRng.chance(0.05f)) {
                        placed = ResourceId{aoc::sim::goods::WOOD};
                    } else if (resRng.chance(0.04f)) {
                        placed = ResourceId{aoc::sim::goods::CATTLE};
                    } else if (resRng.chance(0.03f)) {
                        placed = ResourceId{aoc::sim::goods::SALT};
                    } else if (resRng.chance(0.03f)) {
                        placed = ResourceId{aoc::sim::goods::DYES};
                    } else if (grid.feature(index) == FeatureType::Hills && resRng.chance(0.04f)) {
                        placed = ResourceId{aoc::sim::goods::MARBLE};
                    } else if (grid.riverEdges(index) != 0 && resRng.chance(0.06f)) {
                        placed = ResourceId{aoc::sim::goods::RICE};
                    } else if (resRng.chance(0.03f)) {
                        placed = ResourceId{aoc::sim::goods::RICE};
                    } else if (resRng.chance(0.03f)) {
                        placed = ResourceId{aoc::sim::goods::CLAY};
                    }
                } else if (temperature < 0.30f) {
                    // Cold: furs. WP-C2 cut GEMS (dead-end).
                    if (resRng.chance(0.06f)) {
                        placed = ResourceId{aoc::sim::goods::FURS};
                    } else if (nearCoast && resRng.chance(0.04f)) {
                        placed = ResourceId{aoc::sim::goods::FISH};
                    }
                }

                // Desert also gets salt
                if (!placed.isValid() && terrain == TerrainType::Desert) {
                    if (resRng.chance(0.04f)) {
                        placed = ResourceId{aoc::sim::goods::SALT};
                    }
                }
            }

            // Coast adjacency resources (only if still nothing placed).
            // WP-C2 cut PEARLS (dead-end).
            if (!placed.isValid() && nearCoast) {
                if (resRng.chance(0.06f)) {
                    placed = ResourceId{aoc::sim::goods::FISH};
                } else if (resRng.chance(0.02f)) {
                    placed = ResourceId{aoc::sim::goods::SUGAR};
                }
            }

            if (placed.isValid()) {
                grid.setResource(index, placed);
                grid.setReserves(index, aoc::sim::defaultReserves(placed.value));
                ++totalPlaced;
                if (placed.value == aoc::sim::goods::OIL
                    || placed.value == aoc::sim::goods::NATURAL_GAS) {
                    LOG_INFO("Strategic resource placed: %.*s at (%d,%d) terrain=%.*s elev=%d",
                             static_cast<int>(aoc::sim::goodDef(placed.value).name.size()),
                             aoc::sim::goodDef(placed.value).name.data(),
                             col, row,
                             static_cast<int>(terrainName(terrain).size()),
                             terrainName(terrain).data(),
                             static_cast<int>(grid.elevation(index)));
                }
            }
        }
    }

    // ---- Mountain-metal pass: a fraction of metal deposits spawn on mountains
    // that are accessible from an adjacent non-mountain land tile. This keeps
    // mountains impassable while rewarding players who explore rugged terrain
    // via the Mountain Mine improvement.
    int32_t mountainMetalsPlaced = 0;
    for (int32_t row = 0; row < height; ++row) {
        for (int32_t col = 0; col < width; ++col) {
            const int32_t index = row * width + col;
            if (grid.terrain(index) != TerrainType::Mountain) {
                continue;
            }
            if (grid.resource(index).isValid()) {
                continue;
            }
            if (grid.naturalWonder(index) != NaturalWonderType::None) {
                continue;
            }

            // Require at least one non-mountain, non-water neighbour so the
            // tile is reachable by a Builder on adjacent land.
            const hex::AxialCoord axial = hex::offsetToAxial({col, row});
            const std::array<hex::AxialCoord, 6> nbrs = hex::neighbors(axial);
            bool hasAccessibleNeighbour = false;
            for (const hex::AxialCoord& n : nbrs) {
                if (!grid.isValid(n)) { continue; }
                const int32_t nIndex = grid.toIndex(n);
                const TerrainType nt = grid.terrain(nIndex);
                if (nt == TerrainType::Mountain) { continue; }
                if (isWater(nt)) { continue; }
                hasAccessibleNeighbour = true;
                break;
            }
            if (!hasAccessibleNeighbour) {
                continue;
            }

            // Volcanic / convergent mountains are the richest; others still have
            // a small chance. Total expected metal rate on accessible mountains
            // is roughly 15%.
            const BoundaryType bType = boundary[static_cast<std::size_t>(index)];
            const bool isVolcanic = (bType == BoundaryType::Convergent);

            ResourceId placed{};
            if (isVolcanic) {
                if      (resRng.chance(0.15f)) { placed = ResourceId{aoc::sim::goods::IRON_ORE}; }
                else if (resRng.chance(0.12f)) { placed = ResourceId{aoc::sim::goods::COPPER_ORE}; }
                else if (resRng.chance(0.07f)) { placed = ResourceId{aoc::sim::goods::SILVER_ORE}; }
                else if (resRng.chance(0.04f)) { placed = ResourceId{aoc::sim::goods::GOLD_ORE}; }
            } else {
                if      (resRng.chance(0.10f)) { placed = ResourceId{aoc::sim::goods::IRON_ORE}; }
                else if (resRng.chance(0.07f)) { placed = ResourceId{aoc::sim::goods::COPPER_ORE}; }
                else if (resRng.chance(0.04f)) { placed = ResourceId{aoc::sim::goods::SILVER_ORE}; }
                else if (resRng.chance(0.02f)) { placed = ResourceId{aoc::sim::goods::GOLD_ORE}; }
            }

            if (placed.isValid()) {
                grid.setResource(index, placed);
                grid.setReserves(index, aoc::sim::defaultReserves(placed.value));
                ++mountainMetalsPlaced;
                LOG_INFO("Mountain metal placed at (%d,%d): %.*s",
                         col, row,
                         static_cast<int>(aoc::sim::goodDef(placed.value).name.size()),
                         aoc::sim::goodDef(placed.value).name.data());
            }
        }
    }
    totalPlaced += mountainMetalsPlaced;

    // Guaranteed strategic-energy pass: every map must seed at least a few
    // Oil + Natural Gas + Niter tiles on accessible land.  Without this,
    // probabilistic placement can leave a whole continent dry — which is
    // what killed the OIL chain (no oil) AND the Ammunition chain (no
    // niter) in 5-seed batches.  Tin added too so Bronze recipe has raw.
    {
        const int32_t minOilTiles = std::max(6, (width * height) / 400);
        const int32_t minGasTiles = std::max(3, (width * height) / 800);
        const int32_t minNiterTiles = std::max(4, (width * height) / 600);
        const int32_t minTinTiles   = std::max(3, (width * height) / 800);

        std::vector<int32_t> oilCandidates;
        oilCandidates.reserve(static_cast<size_t>(width * height));
        for (int32_t r = 0; r < height; ++r) {
            for (int32_t c = 0; c < width; ++c) {
                const int32_t idx = r * width + c;
                const TerrainType tt = grid.terrain(idx);
                if (isWater(tt) || tt == TerrainType::Mountain) { continue; }
                if (grid.resource(idx).isValid())               { continue; }
                if (grid.naturalWonder(idx) != NaturalWonderType::None) { continue; }
                oilCandidates.push_back(idx);
            }
        }

        aoc::Random fillRng(resRng);
        for (size_t i = oilCandidates.size(); i > 1; --i) {
            const size_t j = static_cast<size_t>(fillRng.nextInt(0, static_cast<int32_t>(i) - 1));
            std::swap(oilCandidates[i - 1], oilCandidates[j]);
        }

        int32_t oilPlaced = 0, gasPlaced = 0, niterPlaced = 0, tinPlaced = 0;
        for (const int32_t idx : oilCandidates) {
            if (oilPlaced >= minOilTiles && gasPlaced >= minGasTiles
                && niterPlaced >= minNiterTiles && tinPlaced >= minTinTiles) { break; }
            uint16_t res = 0xFFFFu;
            if      (oilPlaced   < minOilTiles)   { res = aoc::sim::goods::OIL;         ++oilPlaced; }
            else if (gasPlaced   < minGasTiles)   { res = aoc::sim::goods::NATURAL_GAS; ++gasPlaced; }
            else if (niterPlaced < minNiterTiles) { res = aoc::sim::goods::NITER;       ++niterPlaced; }
            else                                  { res = aoc::sim::goods::TIN;         ++tinPlaced; }
            grid.setResource(idx, ResourceId{res});
            grid.setReserves(idx, aoc::sim::defaultReserves(res));
            ++totalPlaced;
        }
        LOG_INFO("Strategic fill: oil=%d gas=%d niter=%d tin=%d (targets %d/%d/%d/%d)",
                 oilPlaced, gasPlaced, niterPlaced, tinPlaced,
                 minOilTiles, minGasTiles, minNiterTiles, minTinTiles);
    }

    (void)config;  // mapSize/type already used indirectly
    LOG_INFO("Geology-based resource placement: %d resources placed (%d on mountains)",
             totalPlaced, mountainMetalsPlaced);
}

// ============================================================================
// Basic resource placement for non-Realistic map types
// ============================================================================

void MapGenerator::placeBasicResources(const Config& config, HexGrid& grid,
                                        aoc::Random& rng) {
    const int32_t width = grid.width();
    const int32_t height = grid.height();
    int32_t totalPlaced = 0;

    for (int32_t row = 0; row < height; ++row) {
        for (int32_t col = 0; col < width; ++col) {
            int32_t index = row * width + col;
            TerrainType terrain = grid.terrain(index);
            FeatureType feature = grid.feature(index);

            if (isWater(terrain) || isImpassable(terrain)) {
                continue;
            }

            ResourceId placed{};

            // Hills/mountains area: strategic metals
            if (feature == FeatureType::Hills) {
                if (rng.chance(0.08f))      { placed = ResourceId{aoc::sim::goods::IRON_ORE}; }
                else if (rng.chance(0.05f)) { placed = ResourceId{aoc::sim::goods::COPPER_ORE}; }
                else if (rng.chance(0.03f)) { placed = ResourceId{aoc::sim::goods::GOLD_ORE}; }
                else if (rng.chance(0.03f)) { placed = ResourceId{aoc::sim::goods::SILVER_ORE}; }
                else if (rng.chance(0.04f)) { placed = ResourceId{aoc::sim::goods::COAL}; }
                else if (rng.chance(0.02f)) { placed = ResourceId{aoc::sim::goods::TIN}; }
                else if (rng.chance(0.03f)) { placed = ResourceId{aoc::sim::goods::STONE}; }
            }
            // Desert: oil (high density), natural gas, incense
            else if (terrain == TerrainType::Desert) {
                if (rng.chance(0.10f))      { placed = ResourceId{aoc::sim::goods::OIL}; }
                else if (rng.chance(0.05f)) { placed = ResourceId{aoc::sim::goods::NATURAL_GAS}; }
                // WP-C2: INCENSE cut (dead-end). Lithium favors dry-lake basins.
                else if (rng.chance(0.02f)) { placed = ResourceId{aoc::sim::goods::LITHIUM}; }
            }
            // Forest/jungle: wood, rubber, spices, dyes
            else if (feature == FeatureType::Forest) {
                if (rng.chance(0.08f))      { placed = ResourceId{aoc::sim::goods::WOOD}; }
                else if (rng.chance(0.02f)) { placed = ResourceId{aoc::sim::goods::FURS}; }
            }
            else if (feature == FeatureType::Jungle) {
                if (rng.chance(0.04f))      { placed = ResourceId{aoc::sim::goods::RUBBER}; }
                else if (rng.chance(0.04f)) { placed = ResourceId{aoc::sim::goods::SPICES}; }
                else if (rng.chance(0.03f)) { placed = ResourceId{aoc::sim::goods::DYES}; }
                else if (rng.chance(0.03f)) { placed = ResourceId{aoc::sim::goods::SUGAR}; }
            }
            // Grassland: food, cotton, horses, rice (river-adjacent), clay
            else if (terrain == TerrainType::Grassland) {
                if (rng.chance(0.06f))      { placed = ResourceId{aoc::sim::goods::WHEAT}; }
                else if (rng.chance(0.04f)) { placed = ResourceId{aoc::sim::goods::CATTLE}; }
                else if (rng.chance(0.03f)) { placed = ResourceId{aoc::sim::goods::COTTON}; }
                else if (rng.chance(0.02f)) { placed = ResourceId{aoc::sim::goods::HORSES}; }
                // Rice: river-adjacent gets a higher chance (paddy field), but
                // any Grassland is also valid (upland rice) so the recipe
                // actually gets raw inputs across more seeds.
                else if (grid.riverEdges(index) != 0 && rng.chance(0.06f)) {
                    placed = ResourceId{aoc::sim::goods::RICE};
                }
                else if (rng.chance(0.03f)) { placed = ResourceId{aoc::sim::goods::RICE}; }
                else if (rng.chance(0.03f)) { placed = ResourceId{aoc::sim::goods::CLAY}; }
            }
            // Plains: food, stone, horses, niter, oil (inland basins)
            else if (terrain == TerrainType::Plains) {
                if (rng.chance(0.05f))      { placed = ResourceId{aoc::sim::goods::WHEAT}; }
                else if (rng.chance(0.03f)) { placed = ResourceId{aoc::sim::goods::HORSES}; }
                else if (rng.chance(0.04f)) { placed = ResourceId{aoc::sim::goods::STONE}; }
                else if (rng.chance(0.02f)) { placed = ResourceId{aoc::sim::goods::NITER}; }
                else if (rng.chance(0.03f)) { placed = ResourceId{aoc::sim::goods::WOOD}; }
                else if (rng.chance(0.04f)) { placed = ResourceId{aoc::sim::goods::OIL}; }
                else if (rng.chance(0.02f)) { placed = ResourceId{aoc::sim::goods::NATURAL_GAS}; }
            }
            // Tundra: furs, gems, oil (arctic basins), coal
            else if (terrain == TerrainType::Tundra) {
                if (rng.chance(0.04f))      { placed = ResourceId{aoc::sim::goods::FURS}; }
                // WP-C2: GEMS cut (dead-end luxury).
                else if (rng.chance(0.03f)) { placed = ResourceId{aoc::sim::goods::COAL}; }
                else if (rng.chance(0.05f)) { placed = ResourceId{aoc::sim::goods::OIL}; }
                else if (rng.chance(0.03f)) { placed = ResourceId{aoc::sim::goods::NATURAL_GAS}; }
                // WP-C2: Lithium also in high-altitude tundra hard rock.
                else if (rng.chance(0.02f)) { placed = ResourceId{aoc::sim::goods::LITHIUM}; }
            }

            // Coastal tiles: fish
            if (!placed.isValid() && terrain == TerrainType::Grassland) {
                hex::AxialCoord axial = hex::offsetToAxial({col, row});
                std::array<hex::AxialCoord, 6> nbrs = hex::neighbors(axial);
                for (const hex::AxialCoord& n : nbrs) {
                    if (grid.isValid(n) && isWater(grid.terrain(grid.toIndex(n)))) {
                        if (rng.chance(0.06f)) {
                            placed = ResourceId{aoc::sim::goods::FISH};
                        }
                        break;
                    }
                }
            }

            if (placed.isValid()) {
                grid.setResource(index, placed);
                int16_t reserves = aoc::sim::defaultReserves(placed.value);
                grid.setReserves(index, reserves);
                ++totalPlaced;
                if (placed.value == aoc::sim::goods::OIL
                    || placed.value == aoc::sim::goods::NATURAL_GAS) {
                    LOG_INFO("Strategic resource placed: %.*s at (%d,%d) terrain=%.*s elev=%d",
                             static_cast<int>(aoc::sim::goodDef(placed.value).name.size()),
                             aoc::sim::goodDef(placed.value).name.data(),
                             col, row,
                             static_cast<int>(terrainName(terrain).size()),
                             terrainName(terrain).data(),
                             static_cast<int>(grid.elevation(index)));
                }
            }
        }
    }

    // ---- Mountain-metal pass (basic map generator) ----
    // Drop metal deposits on mountains that have a non-mountain land neighbour,
    // so a Builder standing on adjacent land can access them via Mountain Mine.
    int32_t mountainMetalsPlaced = 0;
    for (int32_t row = 0; row < height; ++row) {
        for (int32_t col = 0; col < width; ++col) {
            int32_t index = row * width + col;
            if (grid.terrain(index) != TerrainType::Mountain) {
                continue;
            }
            if (grid.resource(index).isValid()) {
                continue;
            }
            if (grid.naturalWonder(index) != NaturalWonderType::None) {
                continue;
            }

            hex::AxialCoord axial = hex::offsetToAxial({col, row});
            std::array<hex::AxialCoord, 6> nbrs = hex::neighbors(axial);
            bool hasAccessibleNeighbour = false;
            for (const hex::AxialCoord& n : nbrs) {
                if (!grid.isValid(n)) { continue; }
                int32_t nIndex = grid.toIndex(n);
                TerrainType nt = grid.terrain(nIndex);
                if (nt == TerrainType::Mountain) { continue; }
                if (isWater(nt)) { continue; }
                hasAccessibleNeighbour = true;
                break;
            }
            if (!hasAccessibleNeighbour) {
                continue;
            }

            ResourceId placed{};
            if      (rng.chance(0.05f)) { placed = ResourceId{aoc::sim::goods::IRON_ORE}; }
            else if (rng.chance(0.04f)) { placed = ResourceId{aoc::sim::goods::COPPER_ORE}; }
            else if (rng.chance(0.02f)) { placed = ResourceId{aoc::sim::goods::SILVER_ORE}; }
            else if (rng.chance(0.02f)) { placed = ResourceId{aoc::sim::goods::GOLD_ORE}; }

            if (placed.isValid()) {
                grid.setResource(index, placed);
                grid.setReserves(index, aoc::sim::defaultReserves(placed.value));
                ++mountainMetalsPlaced;
                LOG_INFO("Mountain metal placed at (%d,%d): %.*s",
                         col, row,
                         static_cast<int>(aoc::sim::goodDef(placed.value).name.size()),
                         aoc::sim::goodDef(placed.value).name.data());
            }
        }
    }
    totalPlaced += mountainMetalsPlaced;

    // Guaranteed strategic pass (mirrors placeGeologyResources): seed
    // oil + gas + niter + tin on empty land so every production chain has
    // at least minimal raw supply.
    {
        const int32_t minOilTiles   = std::max(6, (width * height) / 400);
        const int32_t minGasTiles   = std::max(3, (width * height) / 800);
        const int32_t minNiterTiles = std::max(4, (width * height) / 600);
        const int32_t minTinTiles   = std::max(3, (width * height) / 800);
        std::vector<int32_t> oilCandidates;
        oilCandidates.reserve(static_cast<size_t>(width * height));
        for (int32_t r = 0; r < height; ++r) {
            for (int32_t c = 0; c < width; ++c) {
                const int32_t idx = r * width + c;
                const TerrainType tt = grid.terrain(idx);
                if (isWater(tt) || tt == TerrainType::Mountain) { continue; }
                if (grid.resource(idx).isValid())               { continue; }
                if (grid.naturalWonder(idx) != NaturalWonderType::None) { continue; }
                oilCandidates.push_back(idx);
            }
        }
        aoc::Random fillRng(rng);
        for (size_t i = oilCandidates.size(); i > 1; --i) {
            const size_t j = static_cast<size_t>(fillRng.nextInt(0, static_cast<int32_t>(i) - 1));
            std::swap(oilCandidates[i - 1], oilCandidates[j]);
        }
        int32_t oilPlaced = 0, gasPlaced = 0, niterPlaced = 0, tinPlaced = 0;
        for (const int32_t idx : oilCandidates) {
            if (oilPlaced >= minOilTiles && gasPlaced >= minGasTiles
                && niterPlaced >= minNiterTiles && tinPlaced >= minTinTiles) { break; }
            uint16_t res = 0xFFFFu;
            if      (oilPlaced   < minOilTiles)   { res = aoc::sim::goods::OIL;         ++oilPlaced; }
            else if (gasPlaced   < minGasTiles)   { res = aoc::sim::goods::NATURAL_GAS; ++gasPlaced; }
            else if (niterPlaced < minNiterTiles) { res = aoc::sim::goods::NITER;       ++niterPlaced; }
            else                                  { res = aoc::sim::goods::TIN;         ++tinPlaced; }
            grid.setResource(idx, ResourceId{res});
            grid.setReserves(idx, aoc::sim::defaultReserves(res));
            ++totalPlaced;
        }
        LOG_INFO("Strategic fill (basic): oil=%d gas=%d niter=%d tin=%d",
                 oilPlaced, gasPlaced, niterPlaced, tinPlaced);
    }

    (void)config;
    LOG_INFO("Basic resource placement: %d resources placed (%d on mountains)",
             totalPlaced, mountainMetalsPlaced);
}

// ============================================================================
// Random placement — uniform per-tile chance, geology-blind
// ============================================================================

void MapGenerator::placeRandomResources(const Config& config, HexGrid& grid,
                                         aoc::Random& rng) {
    const int32_t width  = grid.width();
    const int32_t height = grid.height();

    // Flat per-tile probabilities chosen so total counts land in the same
    // ballpark as placeBasicResources.  Mountain/water/impassable tiles opt
    // out of land resources; mountains get a separate metals pass below.
    struct GoodChance { uint16_t id; float chance; };
    // WP-C2: LITHIUM seeded alongside legacy strategics. Rarer than coal/oil
    // (0.006) so early-game maps still have chain variety without Lithium
    // saturating every civ.
    // WP-C2 cut GEMS + INCENSE (dead-end luxuries with no downstream).
    const std::array<GoodChance, 19> pool = {{
        {aoc::sim::goods::IRON_ORE,   0.030f},
        {aoc::sim::goods::COPPER_ORE, 0.030f},
        {aoc::sim::goods::COAL,       0.030f},
        {aoc::sim::goods::OIL,        0.020f},
        {aoc::sim::goods::NITER,      0.010f},
        {aoc::sim::goods::HORSES,     0.020f},
        {aoc::sim::goods::STONE,      0.035f},
        {aoc::sim::goods::WOOD,       0.030f},
        {aoc::sim::goods::WHEAT,      0.030f},
        {aoc::sim::goods::CATTLE,     0.020f},
        {aoc::sim::goods::COTTON,     0.015f},
        {aoc::sim::goods::SILK,       0.010f},
        {aoc::sim::goods::SPICES,     0.012f},
        {aoc::sim::goods::DYES,       0.010f},
        {aoc::sim::goods::FURS,       0.012f},
        {aoc::sim::goods::GOLD_ORE,   0.008f},
        {aoc::sim::goods::SILVER_ORE, 0.010f},
        {aoc::sim::goods::TIN,        0.010f},
        {aoc::sim::goods::LITHIUM,    0.006f},
    }};

    int32_t totalPlaced = 0;
    for (int32_t row = 0; row < height; ++row) {
        for (int32_t col = 0; col < width; ++col) {
            const int32_t index = row * width + col;
            const TerrainType terrain = grid.terrain(index);
            if (isWater(terrain) || isImpassable(terrain)) { continue; }
            if (terrain == TerrainType::Mountain)          { continue; }
            if (grid.resource(index).isValid())            { continue; }

            for (const GoodChance& gc : pool) {
                if (rng.chance(gc.chance)) {
                    grid.setResource(index, ResourceId{gc.id});
                    grid.setReserves(index, aoc::sim::defaultReserves(gc.id));
                    ++totalPlaced;
                    break;
                }
            }
        }
    }

    // Mountain metals: uniform chance, no geology check.
    int32_t mountainMetalsPlaced = 0;
    for (int32_t row = 0; row < height; ++row) {
        for (int32_t col = 0; col < width; ++col) {
            const int32_t index = row * width + col;
            if (grid.terrain(index) != TerrainType::Mountain) { continue; }
            if (grid.resource(index).isValid())               { continue; }
            if (grid.naturalWonder(index) != NaturalWonderType::None) { continue; }

            const hex::AxialCoord axial = hex::offsetToAxial({col, row});
            const std::array<hex::AxialCoord, 6> nbrs = hex::neighbors(axial);
            bool accessible = false;
            for (const hex::AxialCoord& n : nbrs) {
                if (!grid.isValid(n)) { continue; }
                const TerrainType nt = grid.terrain(grid.toIndex(n));
                if (nt != TerrainType::Mountain && !isWater(nt)) {
                    accessible = true;
                    break;
                }
            }
            if (!accessible) { continue; }

            ResourceId placed{};
            if      (rng.chance(0.05f)) { placed = ResourceId{aoc::sim::goods::IRON_ORE}; }
            else if (rng.chance(0.04f)) { placed = ResourceId{aoc::sim::goods::COPPER_ORE}; }
            else if (rng.chance(0.02f)) { placed = ResourceId{aoc::sim::goods::SILVER_ORE}; }
            else if (rng.chance(0.02f)) { placed = ResourceId{aoc::sim::goods::GOLD_ORE}; }

            if (placed.isValid()) {
                grid.setResource(index, placed);
                grid.setReserves(index, aoc::sim::defaultReserves(placed.value));
                ++mountainMetalsPlaced;
            }
        }
    }
    totalPlaced += mountainMetalsPlaced;

    (void)config;
    LOG_INFO("Random resource placement: %d resources placed (%d on mountains)",
             totalPlaced, mountainMetalsPlaced);
}

// ============================================================================
// Fair placement — redistribute strategic resources across quadrants
// ============================================================================

void MapGenerator::balanceResourcesFair(const Config& config, HexGrid& grid,
                                         aoc::Random& rng) {
    const int32_t width  = grid.width();
    const int32_t height = grid.height();
    const int32_t midCol = width / 2;
    const int32_t midRow = height / 2;

    auto quadrantOf = [&](int32_t col, int32_t row) -> int32_t {
        const int32_t qx = (col < midCol) ? 0 : 1;
        const int32_t qy = (row < midRow) ? 0 : 1;
        return qy * 2 + qx;  // 0..3
    };

    // Strategic goods that actually matter for industrial/military gates.
    const std::array<uint16_t, 6> balanced = {
        aoc::sim::goods::IRON_ORE,
        aoc::sim::goods::COPPER_ORE,
        aoc::sim::goods::COAL,
        aoc::sim::goods::OIL,
        aoc::sim::goods::HORSES,
        aoc::sim::goods::WHEAT,
    };

    int32_t totalMoved = 0;
    for (const uint16_t goodId : balanced) {
        // Count + gather tile indices per quadrant.
        std::array<std::vector<int32_t>, 4> tiles;
        for (int32_t row = 0; row < height; ++row) {
            for (int32_t col = 0; col < width; ++col) {
                const int32_t index = row * width + col;
                const ResourceId r = grid.resource(index);
                if (r.isValid() && r.value == goodId) {
                    tiles[static_cast<size_t>(quadrantOf(col, row))].push_back(index);
                }
            }
        }
        const int32_t total = static_cast<int32_t>(
            tiles[0].size() + tiles[1].size() + tiles[2].size() + tiles[3].size());
        if (total == 0) { continue; }
        const int32_t target = total / 4;

        // Pass 1: strip surplus from over-served quadrants.
        std::vector<int32_t> surplus;
        for (int32_t q = 0; q < 4; ++q) {
            while (static_cast<int32_t>(tiles[static_cast<size_t>(q)].size()) > target + 1) {
                const size_t n = tiles[static_cast<size_t>(q)].size();
                const size_t pick = static_cast<size_t>(rng.nextInt(0, static_cast<int32_t>(n) - 1));
                const int32_t idx = tiles[static_cast<size_t>(q)][pick];
                tiles[static_cast<size_t>(q)][pick] = tiles[static_cast<size_t>(q)].back();
                tiles[static_cast<size_t>(q)].pop_back();
                grid.setResource(idx, ResourceId{});
                grid.setReserves(idx, 0);
                surplus.push_back(idx);
            }
        }

        // Pass 2: place surplus on any suitable empty land tile in under-served
        // quadrants.  "Suitable" = not water, not impassable, no existing
        // resource, not a natural wonder.  Geology constraints are waived; Fair
        // mode intentionally bulldozes realism to guarantee parity.
        std::vector<int32_t> deficitQuadrants;
        for (int32_t q = 0; q < 4; ++q) {
            const int32_t have = static_cast<int32_t>(tiles[static_cast<size_t>(q)].size());
            for (int32_t need = have; need < target; ++need) {
                deficitQuadrants.push_back(q);
            }
        }

        for (int32_t q : deficitQuadrants) {
            if (surplus.empty()) { break; }

            const int32_t colLo = (q % 2 == 0) ? 0 : midCol;
            const int32_t colHi = (q % 2 == 0) ? midCol : width;
            const int32_t rowLo = (q / 2 == 0) ? 0 : midRow;
            const int32_t rowHi = (q / 2 == 0) ? midRow : height;

            // Collect candidate empty land tiles within the quadrant.
            std::vector<int32_t> candidates;
            candidates.reserve(static_cast<size_t>((colHi - colLo) * (rowHi - rowLo)));
            for (int32_t row = rowLo; row < rowHi; ++row) {
                for (int32_t col = colLo; col < colHi; ++col) {
                    const int32_t index = row * width + col;
                    const TerrainType t = grid.terrain(index);
                    if (isWater(t) || isImpassable(t))       { continue; }
                    if (t == TerrainType::Mountain)          { continue; }
                    if (grid.resource(index).isValid())      { continue; }
                    if (grid.naturalWonder(index) != NaturalWonderType::None) { continue; }
                    candidates.push_back(index);
                }
            }
            if (candidates.empty()) { continue; }

            const size_t pick = static_cast<size_t>(rng.nextInt(0, static_cast<int32_t>(candidates.size()) - 1));
            const int32_t idx = candidates[pick];
            grid.setResource(idx, ResourceId{goodId});
            grid.setReserves(idx, aoc::sim::defaultReserves(goodId));
            surplus.pop_back();
            ++totalMoved;
        }
    }

    (void)config;
    LOG_INFO("Fair-placement rebalance: %d strategic resources relocated across quadrants",
             totalMoved);
}

} // namespace aoc::map
