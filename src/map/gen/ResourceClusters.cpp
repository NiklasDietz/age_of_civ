/**
 * @file ResourceClusters.cpp
 * @brief Cluster placement for luxuries under Realistic resource placement.
 */

#include "aoc/map/gen/ResourceClusters.hpp"

#include "aoc/core/Log.hpp"
#include "aoc/map/LandmassMetrics.hpp"
#include "aoc/map/MapGenerator.hpp"
#include "aoc/map/Terrain.hpp"
#include "aoc/simulation/resource/ResourceTypes.hpp"

#include <algorithm>
#include <climits>
#include <cmath>
#include <deque>

namespace aoc::map {

namespace {

[[nodiscard]] uint8_t at(const std::vector<uint8_t>& v, int32_t index) {
    return static_cast<std::size_t>(index) < v.size() ? v[static_cast<std::size_t>(index)] : 0u;
}

[[nodiscard]] bool isLand(const HexGrid& grid, int32_t index) {
    const TerrainType t = grid.terrain(index);
    return !isWater(t) && !isImpassable(t) && t != TerrainType::Mountain;
}

[[nodiscard]] bool canHold(const HexGrid& grid, int32_t index) {
    return isLand(grid, index) && !grid.resource(index).isValid() &&
           grid.naturalWonder(index) == NaturalWonderType::None;
}

[[nodiscard]] bool nearWater(const HexGrid& grid, int32_t index) {
    for (const hex::AxialCoord& n : hex::neighbors(grid.toAxial(index))) {
        if (grid.isValid(n) && isWater(grid.terrain(grid.toIndex(n)))) {
            return true;
        }
    }
    return false;
}

/// A convergent boundary on the tile or within two steps of it.
[[nodiscard]] bool nearConvergent(const HexGrid& grid, int32_t index) {
    if (grid.boundaryTypeTile(index) == 1u) {
        return true;
    }
    for (const hex::AxialCoord& n : hex::neighbors(grid.toAxial(index))) {
        if (!grid.isValid(n)) {
            continue;
        }
        if (grid.boundaryTypeTile(grid.toIndex(n)) == 1u) {
            return true;
        }
        for (const hex::AxialCoord& m : hex::neighbors(n)) {
            if (grid.isValid(m) && grid.boundaryTypeTile(grid.toIndex(m)) == 1u) {
                return true;
            }
        }
    }
    return false;
}

[[nodiscard]] bool openLand(TerrainType t) {
    return t == TerrainType::Grassland || t == TerrainType::Plains;
}

/// What the predicates read, gathered once per tile.
struct TileFacts {
    TerrainType terrain = TerrainType::Ocean;
    FeatureType feature = FeatureType::None;
    float lat           = 0.0f; ///< 0 equator, 1 pole
    bool coast          = false;
    bool river          = false;
    bool hills          = false;
    bool arid           = false;
    bool humid          = false;
    int8_t elev         = 0;
    uint8_t boundary    = 0; ///< 1 convergent, 2 divergent, 3 transform
    uint8_t margin      = 0; ///< 1 active, 2 passive
    uint8_t rock        = 0; ///< 1 igneous, 2 metamorphic
    uint8_t volcanism   = 0; ///< 1 subduction arc
    uint8_t wildlife    = 0; ///< 1 big game, 2 fur game
    uint8_t biome       = 0; ///< 1 Mediterranean, 5 taiga
};

[[nodiscard]] TileFacts factsOf(const HexGrid& grid, int32_t index) {
    TileFacts f;
    f.terrain           = grid.terrain(index);
    f.feature           = grid.feature(index);
    f.lat               = grid.latitudeFraction(grid.toOffset(index).row);
    f.coast             = nearWater(grid, index);
    f.river             = grid.riverEdges(index) != 0;
    f.hills             = f.feature == FeatureType::Hills;
    const uint8_t arid  = at(grid.aridityIndex(), index);
    f.arid              = f.terrain == TerrainType::Desert || arid > 170;
    f.humid             = arid < 120;
    f.elev              = grid.elevation(index);
    f.boundary          = grid.boundaryTypeTile(index);
    f.margin            = at(grid.marginType(), index);
    f.rock              = at(grid.rockType(), index);
    f.volcanism         = at(grid.volcanism(), index);
    f.wildlife          = at(grid.wildlife(), index);
    f.biome             = at(grid.biomeSubtype(), index);
    return f;
}

/// How many clusters of each luxury a 4000-land-tile map carries, and how big.
[[nodiscard]] ClusterSpec specFor(uint16_t goodId) {
    using namespace aoc::sim::goods;
    switch (goodId) {
        case PLATINUM:      return {goodId, 1, 3};
        case GEMS:          return {goodId, 2, 4};
        case GOLD_ORE:      return {goodId, 2, 4};
        case ALLUVIAL_GOLD: return {goodId, 2, 4};
        case PEARLS:        return {goodId, 2, 4};
        case SALT:          return {goodId, 4, 5};
        case FURS:          return {goodId, 4, 6};
        default:            return {goodId, 3, 5};
    }
}

/// Breadth-first growth from `seed` through eligible tiles.
int32_t grow(HexGrid& grid, const ClusterSpec& spec, const std::vector<bool>& eligible, int32_t seed) {
    std::vector<bool> seen(static_cast<std::size_t>(grid.tileCount()), false);
    std::deque<int32_t> queue;
    queue.push_back(seed);
    seen[static_cast<std::size_t>(seed)] = true;
    int32_t placed                        = 0;
    while (!queue.empty() && placed < spec.tilesPerCluster) {
        const int32_t index = queue.front();
        queue.pop_front();
        if (canHold(grid, index)) {
            grid.setResource(index, ResourceId{spec.goodId});
            grid.setReserves(index, aoc::sim::defaultReserves(spec.goodId));
            ++placed;
        }
        for (const hex::AxialCoord& n : hex::neighbors(grid.toAxial(index))) {
            if (!grid.isValid(n)) {
                continue;
            }
            const int32_t ni = grid.toIndex(n);
            if (seen[static_cast<std::size_t>(ni)] || !eligible[static_cast<std::size_t>(ni)]) {
                continue;
            }
            seen[static_cast<std::size_t>(ni)] = true;
            queue.push_back(ni);
        }
    }
    return placed;
}

[[nodiscard]] bool withinReach(const HexGrid& grid, int32_t index, hex::AxialCoord start) {
    return grid.distance(grid.toAxial(index), start) <= RESOURCE_REACH_RADIUS;
}

[[nodiscard]] bool goodWithinReach(const HexGrid& grid, hex::AxialCoord start, uint16_t goodId) {
    for (int32_t i = 0; i < grid.tileCount(); ++i) {
        const ResourceId r = grid.resource(i);
        if (r.isValid() && r.value == goodId && withinReach(grid, i, start)) {
            return true;
        }
    }
    return false;
}

/// One small cluster of `goodId` on an eligible open tile within reach of
/// `start`; false when its surroundings allow none.
bool clusterNearStart(HexGrid& grid, aoc::Random& rng, const ClusterSpec& spec, hex::AxialCoord start,
                      bool (*eligibleFn)(const HexGrid&, uint16_t, int32_t)) {
    std::vector<bool> eligible(static_cast<std::size_t>(grid.tileCount()), false);
    std::vector<int32_t> spots;
    for (int32_t i = 0; i < grid.tileCount(); ++i) {
        eligible[static_cast<std::size_t>(i)] = eligibleFn(grid, spec.goodId, i);
        if (eligible[static_cast<std::size_t>(i)] && withinReach(grid, i, start) && canHold(grid, i)) {
            spots.push_back(i);
        }
    }
    if (spots.empty()) {
        return false;
    }
    grow(grid, spec, eligible, spots[static_cast<std::size_t>(rng.nextInt(0, static_cast<int32_t>(spots.size()) - 1))]);
    return true;
}

/// Luxury types with a tile within reach of `start`.
[[nodiscard]] std::vector<bool> typesNear(const HexGrid& grid, hex::AxialCoord start,
                                          const std::vector<uint16_t>& luxuries) {
    std::vector<bool> near(luxuries.size(), false);
    for (int32_t i = 0; i < grid.tileCount(); ++i) {
        const ResourceId r = grid.resource(i);
        if (!r.isValid() || !withinReach(grid, i, start)) {
            continue;
        }
        const std::vector<uint16_t>::const_iterator it = std::find(luxuries.begin(), luxuries.end(), r.value);
        if (it != luxuries.end()) {
            near[static_cast<std::size_t>(it - luxuries.begin())] = true;
        }
    }
    return near;
}

} // namespace

bool luxuryEligible(const HexGrid& grid, uint16_t goodId, int32_t index) {
    if (!isLand(grid, index)) {
        return false;
    }
    const TileFacts f = factsOf(grid, index);
    using namespace aoc::sim::goods;
    switch (goodId) {
        case SPICES: // tropical humid coasts and jungle
            return f.lat < 0.28f && !f.arid && (f.feature == FeatureType::Jungle || f.coast);
        case SILK: // temperate humid interior
            return f.lat >= 0.28f && f.lat < 0.55f && openLand(f.terrain) && !f.coast &&
                   (f.feature == FeatureType::Forest || f.river);
        case IVORY: // savanna big game
            return f.lat < 0.35f && openLand(f.terrain) && f.feature == FeatureType::None &&
                   (f.wildlife == 1u || f.terrain == TerrainType::Plains);
        case WINE: // dry-summer temperate hills or coasts
            return f.biome == 1u || (f.lat >= 0.33f && f.lat < 0.5f && f.hills && (f.coast || f.river));
        case DYES: // warm coasts, jungle rivers
            return f.lat < 0.4f && !f.arid && (f.coast || (f.feature == FeatureType::Jungle && f.river));
        case FURS: // boreal
            return f.terrain == TerrainType::Tundra || f.biome == 5u || f.wildlife == 2u ||
                   (f.lat > 0.6f && f.feature == FeatureType::Forest);
        case INCENSE: // arid subtropical hills
            return f.lat >= 0.15f && f.lat < 0.45f && f.arid && f.hills;
        case SALT: // arid basins, passive-margin lowlands
            return f.elev <= 0 && (f.terrain == TerrainType::Desert || (f.margin == 2u && f.coast));
        case MARBLE: // collision-belt hills
            return f.hills && (f.boundary == 1u || f.rock == 2u);
        case PEARLS: // warm shallow coasts
            return f.coast && f.lat < 0.35f && f.terrain != TerrainType::Desert;
        case TEA: // subtropical humid highlands
            return f.lat >= 0.25f && f.lat < 0.45f && f.hills && openLand(f.terrain) && f.humid;
        case COFFEE: // tropical highlands
            return f.lat < 0.25f && f.hills && !f.arid;
        case TOBACCO: // subtropical humid lowlands
            return f.lat >= 0.2f && f.lat < 0.4f && !f.hills && openLand(f.terrain) &&
                   (f.river || f.feature == FeatureType::Floodplains || f.feature == FeatureType::Marsh);
        case GOLD_ORE: // convergent arcs
            return f.boundary == 1u || f.volcanism == 1u;
        case GEMS: // stable interior hills
            return f.hills && f.boundary == 0u && f.margin == 0u && (f.rock == 1u || f.rock == 2u);
        case PLATINUM: // stable interior igneous hills
            return f.hills && f.boundary == 0u && f.margin == 0u && f.rock == 1u;
        case ALLUVIAL_GOLD: // rivers below orogens
            return f.river && nearConvergent(grid, index);
        default:
            return false;
    }
}

bool luxuryBandEligible(const HexGrid& grid, uint16_t goodId, int32_t index) {
    if (!isLand(grid, index)) {
        return false;
    }
    const float lat = grid.latitudeFraction(grid.toOffset(index).row);
    switch (aoc::sim::goodDef(goodId).climateBand) {
        case aoc::sim::ClimateBand::Tropical:    return lat < 0.3f;
        case aoc::sim::ClimateBand::Subtropical: return lat >= 0.2f && lat < 0.45f;
        case aoc::sim::ClimateBand::Temperate:   return lat >= 0.35f && lat < 0.65f;
        case aoc::sim::ClimateBand::Cold:        return lat >= 0.6f;
        default:                                 return true;
    }
}

std::vector<int32_t> placeClusters(HexGrid& grid, aoc::Random& rng, const ClusterSpec& spec,
                                   const std::vector<bool>& eligible, int32_t landTiles,
                                   const std::vector<hex::AxialCoord>* starts) {
    std::vector<int32_t> candidates;
    for (int32_t i = 0; i < grid.tileCount(); ++i) {
        if (eligible[static_cast<std::size_t>(i)] && canHold(grid, i)) {
            candidates.push_back(i);
        }
    }
    std::vector<int32_t> seeds;
    if (candidates.empty()) {
        return seeds;
    }
    const int32_t wanted = std::max(
        1, static_cast<int32_t>(std::lround(static_cast<double>(spec.seedsPer4000Land) * landTiles / 4000.0)));
    std::vector<bool> served(starts != nullptr ? starts->size() : 0, false);
    const auto serve = [&](int32_t seed) {
        for (std::size_t s = 0; s < served.size(); ++s) {
            if (withinReach(grid, seed, (*starts)[s])) {
                served[s] = true;
            }
        }
    };
    const auto nearServedStart = [&](int32_t index) {
        for (std::size_t s = 0; s < served.size(); ++s) {
            if (served[s] && withinReach(grid, index, (*starts)[s])) {
                return true;
            }
        }
        return false;
    };
    seeds.push_back(candidates[static_cast<std::size_t>(rng.nextInt(0, static_cast<int32_t>(candidates.size()) - 1))]);
    serve(seeds.back());
    // Farthest-point sampling: the next seed is the candidate farthest from
    // every seed so far; stop when even that is too close.
    while (static_cast<int32_t>(seeds.size()) < wanted) {
        int32_t best     = -1;
        int32_t bestDist = -1;
        for (const int32_t c : candidates) {
            if (nearServedStart(c)) {
                continue;
            }
            const hex::AxialCoord at = grid.toAxial(c);
            int32_t nearest          = INT_MAX;
            for (const int32_t s : seeds) {
                nearest = std::min(nearest, grid.distance(at, grid.toAxial(s)));
            }
            if (nearest > bestDist) {
                bestDist = nearest;
                best     = c;
            }
        }
        if (best < 0 || bestDist < CLUSTER_MIN_SEPARATION) {
            break;
        }
        seeds.push_back(best);
        serve(best);
    }
    for (const int32_t seed : seeds) {
        grow(grid, spec, eligible, seed);
    }
    return seeds;
}

void placeLuxuryClusters(HexGrid& grid, const std::vector<hex::AxialCoord>& starts, aoc::Random& rng) {
    const int32_t tiles = grid.tileCount();
    int32_t landTiles   = 0;
    for (int32_t i = 0; i < tiles; ++i) {
        landTiles += isLand(grid, i) ? 1 : 0;
    }
    const std::vector<uint16_t>& luxuries = aoc::sim::luxuryGoodIds();
    int32_t clusters                      = 0;
    int32_t fallbacks                     = 0;
    std::vector<bool> eligible(static_cast<std::size_t>(tiles), false);
    for (const uint16_t lux : luxuries) {
        const ClusterSpec spec = specFor(lux);
        int32_t count          = 0;
        for (int32_t i = 0; i < tiles; ++i) {
            eligible[static_cast<std::size_t>(i)] = luxuryEligible(grid, lux, i);
            count += eligible[static_cast<std::size_t>(i)] ? 1 : 0;
        }
        if (count < spec.tilesPerCluster) { // nowhere on this map fits: settle for the climate band
            for (int32_t i = 0; i < tiles; ++i) {
                eligible[static_cast<std::size_t>(i)] = luxuryBandEligible(grid, lux, i);
            }
            ++fallbacks;
        }
        clusters += static_cast<int32_t>(placeClusters(grid, rng, spec, eligible, landTiles, &starts).size());
    }
    // A start with too little variety in reach gets small clusters of
    // luxuries its own surroundings allow: types no other start has in reach
    // first, then the rest in an order rotated by start, so two neighbours
    // topped up from the same list still end up complementary.
    int32_t topped = 0;
    for (std::size_t s = 0; s < starts.size(); ++s) {
        std::vector<bool> near = typesNear(grid, starts[s], luxuries);
        std::vector<bool> elsewhere(luxuries.size(), false);
        for (std::size_t o = 0; o < starts.size(); ++o) {
            if (o == s) {
                continue;
            }
            const std::vector<bool> theirs = typesNear(grid, starts[o], luxuries);
            for (std::size_t k = 0; k < luxuries.size(); ++k) {
                elsewhere[k] = elsewhere[k] || theirs[k];
            }
        }
        std::vector<std::size_t> order;
        for (std::size_t r = 0; r < luxuries.size(); ++r) {
            order.push_back((r + s * 5) % luxuries.size());
        }
        std::stable_sort(order.begin(), order.end(),
                         [&elsewhere](std::size_t a, std::size_t b) { return !elsewhere[a] && elsewhere[b]; });
        int32_t have = static_cast<int32_t>(std::count(near.begin(), near.end(), true));
        for (std::size_t n = 0; n < order.size() && have < REGION_MIN_LUXURY_TYPES; ++n) {
            const std::size_t k = order[n];
            if (near[k]) {
                continue;
            }
            ClusterSpec small     = specFor(luxuries[k]);
            small.tilesPerCluster = 3;
            if (clusterNearStart(grid, rng, small, starts[s], &luxuryEligible)) {
                ++have;
                ++topped;
            }
        }
    }
    LOG_INFO("Luxury clusters: %d land tiles, %d clusters over %zu types (%d on climate band alone), "
             "%d placed for variety near starts",
             landTiles, clusters, luxuries.size(), fallbacks, topped);
}

bool strategicEligible(const HexGrid& grid, uint16_t goodId, int32_t index) {
    if (!isLand(grid, index) || goodId != aoc::sim::goods::HORSES) {
        return false;
    }
    const TileFacts f = factsOf(grid, index);
    // Steppe and prairie: open grass or plains in the temperate belt, no
    // forest or hills, neither desert nor rainforest-wet.
    return f.biome == 9u || f.biome == 10u ||
           (openLand(f.terrain) && f.feature == FeatureType::None && f.lat >= 0.25f && f.lat < 0.6f &&
            !f.arid && at(grid.aridityIndex(), index) >= 60u);
}

void placeStrategicClusters(HexGrid& grid, const std::vector<hex::AxialCoord>& starts, aoc::Random& rng) {
    const int32_t tiles = grid.tileCount();
    int32_t landTiles   = 0;
    std::vector<bool> eligible(static_cast<std::size_t>(tiles), false);
    int32_t count = 0;
    for (int32_t i = 0; i < tiles; ++i) {
        landTiles += isLand(grid, i) ? 1 : 0;
        eligible[static_cast<std::size_t>(i)] = strategicEligible(grid, aoc::sim::goods::HORSES, i);
        count += eligible[static_cast<std::size_t>(i)] ? 1 : 0;
    }
    const ClusterSpec spec{aoc::sim::goods::HORSES, 5, 6};
    if (count < spec.tilesPerCluster) { // no steppe at all: open land in the belt
        for (int32_t i = 0; i < tiles; ++i) {
            const TileFacts f                     = factsOf(grid, i);
            eligible[static_cast<std::size_t>(i)] = isLand(grid, i) && openLand(f.terrain) && f.lat < 0.6f;
        }
    }
    const int32_t clusters = static_cast<int32_t>(placeClusters(grid, rng, spec, eligible, landTiles, &starts).size());
    // Half the starts, in player order, get horses within reach.
    int32_t served = 0;
    for (const hex::AxialCoord& start : starts) {
        served += goodWithinReach(grid, start, aoc::sim::goods::HORSES) ? 1 : 0;
    }
    const int32_t wanted = static_cast<int32_t>(std::ceil(HORSES_START_SHARE * static_cast<float>(starts.size())));
    int32_t added        = 0;
    ClusterSpec small    = spec;
    small.tilesPerCluster = 3;
    for (std::size_t s = 0; s < starts.size() && served < wanted; ++s) {
        if (goodWithinReach(grid, starts[s], aoc::sim::goods::HORSES)) {
            continue;
        }
        if (clusterNearStart(grid, rng, small, starts[s], &strategicEligible)) {
            ++served;
            ++added;
        }
    }
    LOG_INFO("Horse clusters: %d on steppe, %d added near starts (%d of %zu starts have horses in reach)",
             clusters, added, served, starts.size());
}

} // namespace aoc::map
