#pragma once

/**
 * @file TechTree.hpp
 * @brief Technology DAG definition, per-player research state, and research logic.
 *
 * Technologies are a directed acyclic graph where edges represent prerequisites.
 * Each player has independent research progress. A city's science yield contributes
 * to the active research each turn.
 */

#include "aoc/core/Types.hpp"

#include <array>
#include <cstdint>
#include <string_view>
#include <vector>

namespace aoc::sim {

// ============================================================================
// Static tech definitions
// ============================================================================

struct TechDef {
    TechId                  id;
    std::string_view        name;
    EraId                   era;
    int32_t                 researchCost;    ///< Science points needed
    std::vector<TechId>     prerequisites;   ///< Techs that must be researched first
    std::vector<uint16_t>   unlockedGoods;   ///< Good IDs unlocked by this tech
    std::vector<BuildingId> unlockedBuildings;
    std::vector<UnitTypeId> unlockedUnits;
};

/// Get all tech definitions. Built once on first call.
[[nodiscard]] const std::vector<TechDef>& allTechs();

/// Get a specific tech definition.
[[nodiscard]] const TechDef& techDef(TechId id);

/// Total number of technologies.
[[nodiscard]] uint16_t techCount();

// ============================================================================
// Per-player research state (ECS component)
// ============================================================================

struct PlayerTechComponent {
    PlayerId owner = INVALID_PLAYER;

    TechId   currentResearch;              ///< What is being researched (INVALID = nothing)
    float    researchProgress = 0.0f;      ///< Accumulated science toward current tech

    /// Bitfield: which techs have been completed.
    std::vector<bool> completedTechs;

    /// Initialize with the right number of tech slots.
    void initialize() {
        this->completedTechs.resize(techCount(), false);
        this->currentResearch = TechId{};
    }

    [[nodiscard]] bool hasResearched(TechId tech) const {
        if (!tech.isValid() || tech.value >= this->completedTechs.size()) {
            return false;
        }
        return this->completedTechs[tech.value];
    }

    /// Check if all prerequisites for a tech are met.
    [[nodiscard]] bool canResearch(TechId tech) const {
        if (this->hasResearched(tech)) {
            return false;
        }
        const TechDef& def = techDef(tech);
        for (TechId prereq : def.prerequisites) {
            if (!this->hasResearched(prereq)) {
                return false;
            }
        }
        return true;
    }

    /// Get all currently researchable technologies.
    [[nodiscard]] std::vector<TechId> availableTechs() const {
        std::vector<TechId> result;
        uint16_t count = techCount();
        for (uint16_t i = 0; i < count; ++i) {
            TechId id{i};
            if (this->canResearch(id)) {
                result.push_back(id);
            }
        }
        return result;
    }

    /// Complete current research, mark as done.
    void completeResearch() {
        if (this->currentResearch.isValid() &&
            this->currentResearch.value < this->completedTechs.size()) {
            this->completedTechs[this->currentResearch.value] = true;
        }
        this->currentResearch = TechId{};
        this->researchProgress = 0.0f;
    }
};

// ============================================================================
// Research system functions
// ============================================================================

/**
 * @brief Add science points to a player's current research.
 *
 * If the research completes, marks it done and logs the event.
 * The player must manually select the next research.
 *
 * @param tech  Player's tech component.
 * @param sciencePoints  Science generated this turn (from cities).
 * @return true if a tech was completed this turn.
 */
bool advanceResearch(PlayerTechComponent& tech, float sciencePoints);

} // namespace aoc::sim
