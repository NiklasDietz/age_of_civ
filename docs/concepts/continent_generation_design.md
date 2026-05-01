# Continent Generation — Design Mapping

How real plate tectonics map to our procedural model. Reference for tuning.

## Model overview

We simulate plate tectonics in **plate-local coordinates** so geological features
travel with the plate (Variscan-style root rock preservation, Hawaiian-Emperor
hotspot chains).

### Per-plate state

```
Plate {
    cx, cy        // world position (drifts each epoch)
    vx, vy        // velocity (driven by initial state + slab pull + collisions)
    rot, baseRot  // rotation; baseRot fixed at creation, oscillation per epoch
    aspect        // anisotropy of Voronoi cell
    seedX, seedY  // crust mask noise seed (fixed for plate's lifetime)
    landFraction  // 0 = oceanic, 1 = continental, mixed in between
    isLand        // initial classification (rough; per-tile crust mask is truth)
    ageEpochs     // total epochs since creation
    orogenyLocal  // 64×64 grid of mountain stress in PLATE-LOCAL frame
    hotspotTrail  // list of plate-local positions where a hotspot has sat
}
```

### Per-tile elevation calculation

```
For each world tile (col, row):
    1. Find nearest plate via Voronoi (with anisotropy + cylindrical wrap)
    2. Compute plate-local coords (lx, ly) of this tile
    3. Sample plate's crust mask: crust = noise(lx*4.5 + seedX, ly*4.5 + seedY)
       isLand = crust > (1 - landFraction)
    4. Sample plate's orogenyLocal at (lx, ly) → orogeny value
    5. Sample plate's hotspotTrail nearby → island bumps
    6. Apply rebound for polar regions (isostatic)
    7. elevation = base(isLand) + noise(small) + orogeny + hotspots + rebound
```

## Mapping table

| Real Earth | Our Sim | Tuning |
|------------|---------|--------|
| **Mantle convection** | Implicit via plate motion | Initial v + slab pull |
| **Slab pull** | `slabPullX/Y` per plate, gain 0.012/epoch | Velocity nudge toward trenches |
| **Ridge push** | Initial v + rift push | Children pushed perpendicular to rift |
| **Mantle drag** | `v *= 0.995` per epoch | Velocity damping |
| **Plate** | `Plate` struct in MapGenerator | 64×64 plate-local grid |
| **Continental crust** | High `landFraction` plates (0.45-0.65 cratons → grow) | Crust mask says LAND most of plate |
| **Oceanic crust** | Low `landFraction` plates (0.05-0.18) | Crust mask says OCEAN most of plate |
| **Mixed plate** | Most plates have local land + ocean patches | Per-tile crust mask check |
| **Cratons** | Initial small landFraction; orogeny preserved | Root rock erosion floor 0.15%/epoch |
| **Continental growth** | Subduction arcs uplift coast tiles | Mask gate allows land creation |
| **Subduction zone (ocean→cont)** | A_land + !B_land + closing convergent | +0.13 orogeny on A side; trench on B; outer rise on B; forearc wedge inland from arc; backarc spread inland |
| **Subduction zone (ocean→ocean)** | !A_land + !B_land + closing convergent | Denser plate (lower landFraction) subducts; arc on overriding |
| **Continental collision** | Both isLand + closing + close | Multi-stage: contact (decelerate), suture (merge), slab break-off (rebound) |
| **Continental rifting** | Per-CYCLE rift event; plume-induced (prefers near hotspots) | Children pushed perpendicular to rift line; new seeds |
| **Mid-ocean ridge** | !A_land + !B_land + diverging | +0.05 uplift on ocean floor |
| **Triple junction** | 35 % of rifts | Three children at 120° |
| **Microplates** | 20 % chance per rift epoch | Small plate spawned between two close major plates |
| **Hotspot** | `Hotspot` struct, 5–8 placed in deep ocean at init | Mantle-frame fixed |
| **Hotspot track** | Per-plate `hotspotTrail` of plate-local points | Volcanic island chain bumps |
| **LIP (flood basalt)** | 1–3 events per sim, deterministic from seed | Circular basalt province on random plate |
| **Mountain erosion** | Tiered per-epoch decay | 1.5%/epoch for tall, 0.7% mature, 0.15% root |
| **Foothills / Hills** | Orogeny in 0.06–0.20 range | FeatureType::Hills |
| **Polar wander** | Per-epoch global rotation 0.05° | Around map center |
| **Isostatic rebound** | Polar tile elevation boost +0.03 | Latitude > 0.55 |
| **Tectonic escape** | Lateral velocity nudge to nearby plates during contact | ±perp axis 0.004/epoch |
| **Slab break-off** | Post-merge broad uplift | +0.02 over 0.18 plate-local-radius |
| **Forearc wedge** | At ocean→cont subduction, inward-of-arc bump | +0.04 × stress, 0.10 toward seam |
| **Strain-rate rheology** | Cap stress at 1.6 | Prevents runaway orogeny |
| **Trench rollback** | Overriding plate retreats slowly | -0.005/epoch back from trench |

## Realism quantitative targets

| Metric | Target | Tuning lever |
|--------|--------|--------------|
| Mountain coverage | 7-12 % of land | `MOUNTAIN_OROGENY_THRESHOLD` |
| Hills coverage | 20-30 % of land | Hills threshold (0.06) |
| Plains coverage | 60-70 % of land | (residual) |
| Ocean / land ratio | 60/40 | `effectiveWaterRatio` 0.40-0.55 |
| Plates total | ~15 | `maxPlates` cap; rift cycle |
| Plate motion | 1-10 cm/yr equivalent | `DT × v_max` per epoch |
| Wilson cycle | ~500 My total | `EPOCHS` defines simulated time |
| Mountain belt width | 100-300 km (~5-15 hex tiles) | `boundary` band tightness |
| Mountain belt length | 5000+ km (cross-continental) | Boundary line forms naturally |

## Key challenges

1. **Voronoi cells produce blob-shaped territories** even with anisotropy. Real plates have JAGGED boundaries (faults, microplates, accretionary wedges). Domain warp helps; could improve with multi-scale fault noise.

2. **Stress accumulation as percentile vs threshold**. Currently use absolute orogeny threshold for Mountain. Need careful tuning so a 40-epoch sim and a 200-epoch sim both produce ~10% mountain coverage.

3. **Plate-local grid resolution**. 64×64 = ~0.06 plate-local units per cell. With anisotropic distance and warp, may not capture all features. Could increase to 128×128 if needed.

4. **Erosion balance**. Mountains erode but hills are root-preserved. Need to ensure inactive boundaries DO erode below mountain threshold over geologic time. Currently 1.5%/epoch for active.

5. **Bilinear scatter spreading**. Each stress contribution writes to 4 cells. Footprint per contribution ~5x5 after 1 blur pass. May be too smeared; could switch to nearest-cell scatter for sharper boundaries.

6. **Cratonic stability**. Real cratons are STABLE for billions of years — no orogeny, no sediment except shallow cover. Our cratons can absorb stress at their margins (correct) but interior should stay quiet. Currently the per-plate orogeny grid only stores boundary stress, so center cells ARE quiet.

## Tuning checklist (keep in sync with code)

- `STRESS_GATE` 0.30 — stress threshold for orogeny accumulation
- Boundary band cutoff `d1/d2 > 0.93` — only seam tiles
- Arc orogeny contribution 0.13/epoch (ocean→cont)
- Collision orogeny 0.09/epoch (cont→cont)
- Trench orogeny -0.07/epoch (subducting side)
- Erosion: 1.5% (>0.20), 0.7% (0.10–0.20), 0.15% (<0.10)
- Orogeny cap ±0.32 / -0.20
- `MOUNTAIN_OROGENY_THRESHOLD` 0.20 (Mountain tier)
- Hills threshold 0.06 (Hills feature)
- Initial landFraction 0.45-0.65 (cratons), 0.05-0.18 (oceanic)
- DT = clamp(0.86×driftFrac / (EPOCHS × v_max), 0.001, 0.040)
- Drift default 0.6 (60% map width total)
- maxPlates = max(20, initial × 2)
- Rift CYCLE = 5 epochs, 1 split per epoch
- Rift offsetMag 0.05–0.10
