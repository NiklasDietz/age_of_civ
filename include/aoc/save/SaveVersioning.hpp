#pragma once

/**
 * @file SaveVersioning.hpp
 * @brief Save file version migration system.
 *
 * Each save file has a version number. When the game loads an older save,
 * it applies migration steps to convert the data to the current format.
 *
 * Version history:
 *   v1: Initial save format (base game)
 *   v2: Added diplomacy, market, wonder sections
 *   v3: Added stockpiles, player state, misc entities
 *   v4: Added monetary coin reserves, debasement, trust
 *   v5: Added currency crisis, bonds, devaluation, hoards
 *   v6: Added production experience, building levels, pollution, automation
 *   v7: Added industrial revolution, expanded content
 *
 * Migration approach:
 *   - Each version step has a migrate() function that reads the old format
 *     and writes default values for new fields.
 *   - Migrations are chained: v3 -> v4 -> v5 -> v6 -> v7 (current)
 *   - If a section is missing (not in older save), it's created with defaults.
 */

#include <cstdint>

namespace aoc::save {

/// Current save format version.
inline constexpr uint32_t CURRENT_SAVE_VERSION = 8;

/// Minimum supported save version (older saves cannot be loaded).
inline constexpr uint32_t MIN_SUPPORTED_VERSION = 1;

/**
 * @brief Check if a save file version can be loaded.
 *
 * @param version  The version number from the save file header.
 * @return true if the version is supported (can be migrated to current).
 */
[[nodiscard]] constexpr bool isVersionSupported(uint32_t version) {
    return version >= MIN_SUPPORTED_VERSION && version <= CURRENT_SAVE_VERSION;
}

/**
 * @brief Check if a save file needs migration.
 *
 * @param version  The version number from the save file header.
 * @return true if the version is older than current and needs migration.
 */
[[nodiscard]] constexpr bool needsMigration(uint32_t version) {
    return version < CURRENT_SAVE_VERSION;
}

} // namespace aoc::save
