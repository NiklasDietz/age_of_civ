/**
 * @file ModLoader.cpp
 * @brief Mod loader stub implementation.
 *
 * All methods currently log a warning and return false. Actual JSON
 * parsing will be implemented when a JSON library dependency is added.
 */

#include "aoc/mod/ModLoader.hpp"
#include "aoc/core/Log.hpp"

namespace aoc::mod {

bool ModLoader::loadUnitDefs(const std::string& filepath) {
    LOG_WARN("ModLoader::loadUnitDefs('%s') -- JSON parsing not yet implemented",
             filepath.c_str());
    return false;
}

bool ModLoader::loadBuildingDefs(const std::string& filepath) {
    LOG_WARN("ModLoader::loadBuildingDefs('%s') -- JSON parsing not yet implemented",
             filepath.c_str());
    return false;
}

bool ModLoader::loadTechDefs(const std::string& filepath) {
    LOG_WARN("ModLoader::loadTechDefs('%s') -- JSON parsing not yet implemented",
             filepath.c_str());
    return false;
}

bool ModLoader::loadCivDefs(const std::string& filepath) {
    LOG_WARN("ModLoader::loadCivDefs('%s') -- JSON parsing not yet implemented",
             filepath.c_str());
    return false;
}

} // namespace aoc::mod
