#pragma once

/**
 * @file SaveVersioning.hpp
 * @brief Save format version and the deliberately absent migration policy.
 *
 * Policy (pinned 2026-09-04): there is no save migration. A file loads only
 * when its header version equals CURRENT_SAVE_VERSION; loadGame rejects
 * anything older or newer with SaveVersionMismatch instead of half-loading
 * it. Unknown sections inside a current-version file are skipped by size,
 * which is the only forward compatibility the format offers.
 *
 * Bumping the version (any change to a section's byte layout):
 *   1. Raise CURRENT_SAVE_VERSION here. SAVE_VERSION in Serializer.hpp is an
 *      alias, so the header saveGame writes follows automatically.
 *   2. Regenerate the known-good corpus that test_save_roundtrip loads and
 *      that also seeds the save fuzzer, then delete the previous file:
 *        AOC_WRITE_CORPUS=tests/data/saves/basic_v<N>.sav build/release/test_save_roundtrip
 *   3. State in the commit message that every existing save becomes
 *      unloadable.
 *
 * v11 (2026-09-04): SectionId::MapLayers carries every HexGrid layer by name
 * (MapFile.hpp), so natural wonders, soil, ore grades and the rest survive a
 * load; TurnState carries the human player id. Saves grew by the layer dump
 * (about 3.4 MB for a 140x90 map).
 *
 * v12 (2026-09-04): MapLayers carries only the 16 game-state layers
 * (MapFile.hpp isGameGridLayer). The ~170 worldgen-only layers were 90% of a
 * 17 MB Tutorial save and are never read by the simulation; they remain in
 * the headless map cache (.aocmap), which is lossless.
 *
 * v13 (2026-09-05): the Entities unit record carries AirUnitComponent
 * (sortiesRemaining, maxSorties, operationalRange, isIntercepting) after the
 * pending path. Without it a loaded fighter forgot its patrol state and a
 * bomber its spent sorties.
 *
 * v14 (2026-09-05): a GreatWorks section (per-city housed works: type, creator,
 * named person, turn) and a 17th game layer, the sparse antiquitySite map.
 * Tourism now counts placed works instead of empty slots.
 *
 * v15 (2026-09-05): the Entities unit record carries the FormationLevel (Corps /
 * Army, Fleet / Armada) after the air state; a load reset every formation before.
 *
 * v16 (2026-09-05): the Entities city record carries the CityGovernorComponent
 * (focus, flags, named governor, up to three titles, turns active); a load
 * dropped every governor before.
 *
 * v17 (2026-09-05): the save fidelity bundle. Unit record: SpyComponent (level,
 * promotions, idle spies) and GreatPersonComponent. City record: walls, loyalty,
 * happiness, stage, aqueduct link, locked tiles, religious pressure. TechProgress:
 * knownTechs and the research queue. GovernmentState: autoPolicies. PlayerState:
 * era score, age type, thresholds, Historic Moments + lifetime era score
 * (none of the age state was saved before). Diplomacy: hasMet, metOnTurn,
 * turnsSincePeace, passiveBonus, lastWarAggressor, intel and embargoes per
 * direction. New sections CityStates (components + seats with cities and units),
 * ReligionState (tracker + faith) and WorldCongressState (congress + favor).
 * Still not carried: GlobalDealTracker, AllianceObligationTracker and goody huts
 * (not reachable from loadGame's signature), the barbarian seat (reset by design),
 * city-state districts / queues / stockpiles.
 *
 * v18 (2026-09-05): GovernmentState carries unlockedPolicies as 64 bits (36
 * cards overflowed the u32; card 35 aliased card 3), policySwapFree and
 * lastGovernmentChangeTurn.
 *
 * v19 (2026-09-05): MapLayers carries the 18th game layer "pillaged" (a
 * pillaged improvement yields nothing until a Builder repairs it).
 *
 * v20 (2026-09-05): GovernmentState carries the per-player envoy pool
 * (available, lifetime). Envoys come from civics and are spent on
 * city-states; passive accrual is gone.
 *
 * v21 (2026-09-06): Diplomacy carries the war start turn, friendship and
 * open-borders expiry per pair, and denouncement turn, delegation and
 * embassy per direction (DiplomacyActions.hpp).
 *
 * v22 (2026-09-06): a DealProposals section, the deal proposals waiting for
 * the human's answer (DealProposals.hpp). Active deals themselves still live
 * outside GameState and are not saved.
 *
 * v23 (2026-09-06): ProductionQueues carries the tile a queued district will
 * stand on (targetTile + hasTargetTile), so the human's placement choice
 * survives a save (DistrictPlacement.hpp).
 *
 * v24 (2026-09-06): the Entities city record carries the holder -- the player
 * whose vector the object lives in -- after the owner. Cities now really move
 * between players on conquest, secession and cession, and a free city (owner
 * INVALID_PLAYER) keeps its holder instead of sizing the roster to 256 seats
 * and failing the load outright.
 */

#include <cstdint>

namespace aoc::save {

/// Current save format version. Bump only per the procedure above.
inline constexpr uint32_t CURRENT_SAVE_VERSION = 24;

} // namespace aoc::save
