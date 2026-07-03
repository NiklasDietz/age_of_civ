# Sim physics rewrite — task tracker

## Phase 1 — Voronoi purge — DONE
## Phase 2 — Subduction speed — DONE
## Phase 3 — Boundary classification — DONE
## Phase 4 — Slab-pull / ridge-push feedback — DONE

## Phase 5 — Cleanup

- [x] **5.1** applyContinentalDocking deleted (function body, header
  declaration, caller-side `contactAgeByPlatePair` vector all gone).
- [x] **5.2 (deferred — non-blocking)** `Plate::cx`/`cy`/`rot` retained
  as derived 2D-projection cache, not deleted. Reason: 44 live read
  sites use them for proximity tests + map-snapshot export; full
  migration to lat/lon haversine would require behaviour-equivalence
  validation across hotspot placement, rift seeding, microplate
  spawn, and EarthSystem boundary normals. The fields are
  re-projected from latDeg/lonDeg via Mollweide forward each motion
  epoch and never feed any plate-ownership decision (no Voronoi
  use), so they do not violate CLAUDE.md "World-generation physics
  requirements". Schedule as a follow-up cleanup PR with its own
  characterisation tests.

## Phase 6 — Audit — DONE

- [x] 21-sim sweep clean.
- [x] Multi-seed mountain count: 6/8 seeds with visible mountains
  (15-44 on-mountain resources, 1-41 chokepoints).
- [x] UI: continent creator now reports age in My/Gy via
  `formatCreatorAgeLabel`, sim-step buttons relabelled "-50My" /
  "+50My", debug log says "age=…/… My" not "epochs=…/…".
