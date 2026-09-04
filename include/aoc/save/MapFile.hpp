#pragma once

/**
 * @file MapFile.hpp
 * @brief Standalone full-fidelity map file: every HexGrid layer, by name.
 *
 * The in-save MapGrid section keeps six layers per tile; the generator
 * produces about 170. This format walks HexGrid::visitLayers(), so a map
 * loaded from it plays exactly like the freshly generated one (headless
 * `--map-cache`). Layers are matched by name: a newer file's unknown layers
 * are skipped, an older file's missing layers stay in their fresh state.
 *
 * The game save (SectionId::MapLayers, v12) writes the same block restricted
 * to isGameGridLayer(): the 16 layers HexGrid::initialize() sizes, which is
 * all the simulation ever reads back. The other ~170 layers are worldgen
 * products (about 15 MB on a 400x200 map, 1 MB of it the fixed-size sphere
 * snapshot); they stay in the map file only.
 */

#include "aoc/core/ErrorCodes.hpp"

#include <cstdint>
#include <string>
#include <string_view>

namespace aoc::map {
class HexGrid;
}

namespace aoc::save {

class ReadBuffer;
class WriteBuffer;

/// Layer block shared with the save format (SectionId::MapLayers): u32 record
/// count, then per record a string name, u8 element kind, u32 element count and
/// the elements. Array layers become one record per slice ("name[i]").
void writeGridLayers(WriteBuffer& out, const aoc::map::HexGrid& grid);

/// True for the per-tile state layers HexGrid::initialize() sizes (terrain,
/// feature, elevation, riverEdges, resource, reserves, prospectCooldown, owner,
/// improvement, road, tileInfra, greenhouseCrop, naturalWonder, chokepoint,
/// falloutTurns, preFalloutFeature). Everything else is worldgen output.
[[nodiscard]] bool isGameGridLayer(std::string_view name);

/// writeGridLayers() restricted to isGameGridLayer(): the body of the save
/// format's SectionId::MapLayers since v12.
void writeGameGridLayers(WriteBuffer& out, const aoc::map::HexGrid& grid);

/// Reads a writeGridLayers() block into an initialised grid. Unknown names are
/// skipped, every count is checked against the buffer before it allocates.
/// `source` names the file in log lines.
[[nodiscard]] ErrorCode readGridLayers(ReadBuffer& in, aoc::map::HexGrid& grid,
                                       const char* source);

/// Provenance recorded in the header; informational, never required to match.
struct MapFileInfo {
    uint64_t generatorSeed = 0;
};

/// Writes `<filepath>.tmp` and renames it into place.
[[nodiscard]] ErrorCode saveMapFile(const std::string& filepath, const aoc::map::HexGrid& grid,
                                    const MapFileInfo& info = {});

/// Replaces `grid` only on success; a rejected file leaves it untouched.
[[nodiscard]] ErrorCode loadMapFile(const std::string& filepath, aoc::map::HexGrid& grid,
                                    MapFileInfo* info = nullptr);

} // namespace aoc::save
