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
 */

#include "aoc/core/ErrorCodes.hpp"

#include <cstdint>
#include <string>

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
