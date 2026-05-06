#pragma once

/**
 * @file Plate.hpp
 * @brief Tectonic-plate description shared by extracted MapGenerator passes.
 *
 * Pulled out of src/map/MapGenerator.cpp on 2026-05-03 so post-sim and
 * elevation passes can live in their own files. The struct's data layout is
 * MapGenerator-internal -- new fields can still be added without affecting
 * downstream consumers as long as they only read existing accessors.
 *
 * SutureSeam tracks plate-merger events that the post-sim ophiolite pass
 * uses to mark suture rock type.
 */

#include "aoc/map/gen/PlatePhysics.hpp"

#include <cstdint>
#include <utility>
#include <vector>

namespace aoc::map::gen {

struct Plate {
    // 2026-05-05: PHYSICS-FIRST REWRITE - per-plate Lagrangian state
    // grid (10 km cells, holds crust thickness + composition + age +
    // strain + sediment + derived elevation). Replaces orogenyLocal
    // in later phases; coexists during the migration. See PlatePhysics.hpp.
    PhysicsGrid grid;

    // 2026-05-05: SPHERE MIGRATION - lat/lon are the AUTHORITATIVE
    // plate position. The legacy (cx, cy) unit-square coords are
    // computed from (latDeg, lonDeg) via Mollweide projection at
    // sim init and after every motion step. Sphere-aware code paths
    // (haversine Voronoi in elevation/orogeny passes + PlateIdStash)
    // read latDeg/lonDeg directly; legacy 2D code paths (polygon
    // vertices, extraSeeds, hotspot trails, cylindrical wrap) read
    // (cx, cy) and rotate by p.rot. p.rot is now updated each epoch
    // by the Euler-pole local-vertical projection so plate-local 2D
    // features ride with true sphere orientation. See
    // docs/SPHERE_MIGRATION.md for the full migration scope.
    float latDeg = 0.0f;       // [-90, 90]
    float lonDeg = 0.0f;       // [-180, 180]
    // Euler-pole axis (for sphere-based motion). Plate rotates around
    // this point on the sphere by `angularVelDeg` per epoch.
    float eulerPoleLatDeg = 0.0f;
    float eulerPoleLonDeg = 0.0f;
    float angularVelDeg   = 0.0f;
    // Derived 2D fields -- reprojected from (latDeg, lonDeg) via
    // Mollweide forward each motion step. Kept for legacy 2D consumers:
    float cx;
    float cy;
    // 2026-05-06 P4.3h-c-3: isLand deleted (use landFraction > 0.40f).
    // 2026-05-06: PHYSICS-FIRST P4.3h-a -- vx + vy deleted; plate
    // motion is now lat/lon Euler-pole rotation only.
    // 2026-05-06 P4.3g-c: aspect + baseAspect deleted.
    float rot;
    // 2026-05-06: PHYSICS-FIRST P4.3g-b -- seedX/seedY deleted; noise
    // seeded from latDeg/lonDeg directly at consumer sites.
    float landFraction;
    int32_t ageEpochs = 0;
    float weight = 1.0f;
    // 2026-05-06: PHYSICS-FIRST P4.3g-a/h-c-2 -- extraSeeds + isPolar deleted.
    // 2026-05-06: PHYSICS-FIRST P4.3a/e -- orogenyLocal + orogenyAgeLocal
    // + hotspotTrail + crustAreaInitial + baseRot deleted.
    // 2026-05-06 P4.3h-c-1/4: mergesAbsorbed + crustArea deleted.
    // 2026-05-06: PHYSICS-FIRST P4.3c/h-c-5 -- slabTornThisEpoch + crustAge deleted.
    // 2026-05-06: PHYSICS-FIRST P4.3f -- legacy 2D Euler-pole fields
    // (eulerPoleX, eulerPoleY, angularRate) deleted. Plate rotation
    // handled by lat/lon Euler pole (eulerPoleLatDeg, eulerPoleLonDeg,
    // angularVelDeg) via rotateAroundEulerPole.
    // 2026-05-06: PHYSICS-FIRST P4.3c/d -- isMicroplate +
    // oceanWedgeBornEpoch + oceanWedgeNx/Ny/Width deleted. SphereField
    // physics handles ridge bathymetry directly (P6 wiring).
    // 2026-05-06: PHYSICS-FIRST P4.3e -- stressAccum deleted.
    // 2026-05-06: PHYSICS-FIRST P4.3b -- polygon-boundary fields
    // deleted (boundaryVertices, boundaryEdgeTypes,
    // boundaryNeighborIds, polygonMinX/MinY/MaxX/MaxY). Plate
    // ownership is haversine-only via PlateIdStash (P3.1); polygon
    // construction Phases A/B/C + lib files deleted in P3.3/P3.4.
};

// 2026-05-06: PHYSICS-FIRST P4.1 -- struct SutureSeam deleted along
// with the ophiolite suture pass that consumed it (see PostSim.cpp).

} // namespace aoc::map::gen
