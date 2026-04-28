# Confederation (Staatenbund / EU-style co-win)

Removed 2026-04-27. Kept here for future reference.

## Concept

Persistent N-way alliance bloc, distinct from six pairwise alliance types.
Members keep own state, units, decisions; bond is diplomatic, not state merger.

## Effects (when active)

- Co-win at turn limit. Shared-prestige gate uses `sqrt(N)` diminishing
  factor so 5-member bloc cannot trivially outvote lone front-runner.
- Attack on any member fans out war obligation across all other members
  (same mechanism as bilateral Military Alliance, broadcast).
- Members forbidden to declare war on each other for life of confederation.

## Formation prerequisites

- Non-eliminated, non-city-state players only.
- Every pair: stance >= Friendly AND has met.
- Every pair: connected by at least one completed trade route at formation
  (economic interdependence).
- Industrial era or later (era >= 4).
- Min size 2 (pairs allowed; co-win needs size >= 3, audit pushed to 4).
- Max size 5. Caps "everyone but leader" lock-in.
- Player can belong to at most one active confederation.

## Tuning constants (last values before removal)

- `CONFEDERATION_MAX_MEMBERS = 5`
- `CONFEDERATION_COWIN_MIN = 3` (audit pushed to 4)
- `CONFEDERATION_PRESTIGE_SQRT_EXP = 0.5f`

## Why removed

AI never formed qualifying 3+ blocs in audit (only ephemeral 2-member pacts
via existing alliance types). Co-win path was always inert. Pairwise
alliance system already covers most of the diplomatic gameplay loop. Cost
of maintenance > player-facing value at this stage.

## If reintroducing

- Need AI bloc-formation policy: trigger when 3+ Friendly+TradeRoute
  neighbours sit just under co-win prestige threshold.
- Need UI surface (formation flow, bloc roster, dissolution warnings).
- Reuse `AllianceObligationTracker` for war fan-out; no separate
  obligation type needed.
- Score victory becomes the natural fallback for turn-limit ties; no
  co-win mechanic strictly required.

## Files that held it (pre-removal)

- `include/aoc/simulation/diplomacy/Confederation.hpp`
- `src/simulation/diplomacy/Confederation.cpp`
- VictoryCondition.cpp had a co-win branch (already disabled 2026-04-25).
- DiplomacyState/GameState held the `std::vector<ConfederationComponent>`.
- HUD/Score/AI/Save/HeadlessSimulation had small touchpoints.
- VICTORY_MASK_CONFEDERATION bit + name in VictoryCondition mask table.
