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
 */

#include <cstdint>

namespace aoc::save {

/// Current save format version. Bump only per the procedure above.
inline constexpr uint32_t CURRENT_SAVE_VERSION = 14;

} // namespace aoc::save
