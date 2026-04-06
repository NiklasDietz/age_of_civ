#pragma once

/**
 * @file ModLoader.hpp
 * @brief Framework for loading game definitions from external JSON files.
 *
 * Provides the interface for data-driven modding. The actual JSON parsing
 * will be implemented when a JSON library (e.g. nlohmann/json) is added.
 * For now, the methods log a warning and return false.
 */

#include <string>

namespace aoc::mod {

class ModLoader {
public:
    /// Load unit definitions from a JSON file, overriding hardcoded ones.
    /// @return true if loading succeeded, false otherwise.
    static bool loadUnitDefs(const std::string& filepath);

    /// Load building definitions from a JSON file.
    /// @return true if loading succeeded, false otherwise.
    static bool loadBuildingDefs(const std::string& filepath);

    /// Load tech tree definitions from a JSON file.
    /// @return true if loading succeeded, false otherwise.
    static bool loadTechDefs(const std::string& filepath);

    /// Load civilization definitions from a JSON file.
    /// @return true if loading succeeded, false otherwise.
    static bool loadCivDefs(const std::string& filepath);
};

} // namespace aoc::mod
