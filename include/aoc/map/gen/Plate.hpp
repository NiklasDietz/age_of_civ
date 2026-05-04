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

#include <cstdint>
#include <utility>
#include <vector>

namespace aoc::map::gen {

struct Plate {
    float cx;
    float cy;
    bool  isLand;
    float vx;
    float vy;
    float aspect;
    float rot;
    float baseRot;
    float baseAspect;
    float seedX;
    float seedY;
    float landFraction;
    int32_t ageEpochs = 0;
    float weight = 1.0f;
    std::vector<std::pair<float, float>> extraSeeds;
    bool isPolar = false;
    std::vector<std::pair<float, float>> hotspotTrail;
    std::vector<float> orogenyLocal;
    // 2026-05-04: WP3 - per-cell orogeny age (epochs since last positive
    // contribution). Drives age-dependent erosion: young peaks erode
    // fast (high relief), old peaks survive as eroded shields.
    std::vector<int16_t> orogenyAgeLocal;
    float crustArea        = 1.0f;
    float crustAreaInitial = 1.0f;
    int32_t mergesAbsorbed = 0;
    int8_t  slabTornThisEpoch = 0;
    float crustAge         = 0.0f;
    // 2026-05-03: Euler-pole rotation. Each plate spins around a fixed
    // axis (eulerPoleX, eulerPoleY) at angularRate rad/epoch. Real plates
    // rotate as well as translate -- pure translation produces unrealistically
    // straight hotspot trails and parallel margins. The hybrid model keeps
    // linear (vx,vy) drift and adds a small rotational component on top.
    float eulerPoleX  = 0.0f;
    float eulerPoleY  = 0.0f;
    float angularRate = 0.0f;
    // Marks plates spawned via the mid-sim microplate path (Caribbean /
    // Anatolian / Adria style). Used to cap the active microplate
    // population separately from the global plate cap so microplates
    // do not crowd inside visible continents.
    bool  isMicroplate = false;
    // 2026-05-04: rift trailing-edge oceanic crust. When a plate
    // participates in a continental rift, both pieces (parent + child)
    // grow new oceanic crust at the trailing edge facing the rift line.
    // The Atlantic between South America and Africa is on BOTH plates --
    // each carries its continent + new oceanic crust accreted at the
    // mid-Atlantic ridge. Stored in plate-local coords so it tracks the
    // plate as it drifts. Length determines how wide the oceanic wedge
    // is; (oceanWedgeNx, oceanWedgeNy) is the unit vector pointing FROM
    // continental interior TOWARD the rift edge.
    float oceanWedgeNx     = 0.0f;
    float oceanWedgeNy     = 0.0f;
    float oceanWedgeWidth  = 0.0f;  // 0 = no rift wedge
    int32_t oceanWedgeBornEpoch = -1;
    // 2026-05-04: intra-plate stress accumulator. Real plates store
    // elastic strain over many epochs and periodically release it as
    // earthquakes / sudden velocity bursts (stick-slip behaviour).
    // When stressAccum exceeds threshold, plate gets a velocity
    // perturbation + accumulator resets.
    float stressAccum = 0.0f;
    // 2026-05-04: POLYGON BOUNDARY representation. Real plates are
    // bounded by polygons of edges (spreading ridges, subduction
    // trenches, transform faults) -- not Voronoi cells. We store a
    // ring of plate-local boundary vertices sampled at sim-init from
    // the Voronoi cell. Vertices ride with the plate's Euler-pole
    // rotation each epoch so the polygon stays attached. Polygon is
    // currently used for: visualization overlay (renderer can draw
    // boundary lines) and per-edge type classification. Tile-
    // ownership still uses Voronoi for performance; full migration
    // to polygon-based ownership would be a multi-day rewrite.
    std::vector<std::pair<float, float>> boundaryVertices;
    // Per-edge boundary type: 0=unknown, 1=spreading-ridge,
    // 2=subduction-trench, 3=transform-fault, 4=collision-suture.
    // boundaryEdgeTypes[i] describes the edge from vertex[i] to
    // vertex[(i+1) % N].
    std::vector<uint8_t>                 boundaryEdgeTypes;
    // Per-edge neighbor plate id (which plate is on the OTHER side of
    // edge[i]). 0xFF = no neighbor / map edge. Used by clipping +
    // evolution to know which plates interact across each edge.
    std::vector<uint8_t>                 boundaryNeighborIds;
    // World-space axis-aligned bounding box, recomputed each epoch
    // from the world-transformed polygon vertices. Used by point-in-
    // polygon fast-reject (skip PIP if tile outside AABB).
    float polygonMinX = 0.0f;
    float polygonMinY = 0.0f;
    float polygonMaxX = 0.0f;
    float polygonMaxY = 0.0f;
};

struct SutureSeam {
    float x;
    float y;
    float r;
    // 2026-05-04: along-seam unit tangent (perpendicular to the
    // collision normal between the two merged plates). Used by the
    // ophiolite mask pass to draw a NARROW BAND along the suture
    // line instead of a circular DISC -- real ophiolite exposures
    // are linear (Indus-Tsangpo Suture, Iapetus Suture, Atlas Belt),
    // not round patches.
    float tangentX = 1.0f;
    float tangentY = 0.0f;
};

} // namespace aoc::map::gen
