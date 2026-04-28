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

    /// Techs the player KNOWS (researched OR acquired via trade). Always
    /// superset of completedTechs. Known-but-not-completed techs are
    /// visible (reveal resources, show in tech tree) but cannot be used
    /// to build/produce until all prereqs are completed. Per-turn tick
    /// auto-promotes known→completed when prereqs are met.
    std::vector<bool> knownTechs;

    /// Initialize with the right number of tech slots.
    void initialize() {
        this->completedTechs.resize(techCount(), false);
        this->knownTechs.resize(techCount(), false);
        this->currentResearch = TechId{};
    }

    [[nodiscard]] bool hasResearched(TechId tech) const {
        if (!tech.isValid() || tech.value >= this->completedTechs.size()) {
            return false;
        }
        return this->completedTechs[tech.value];
    }

    /// Visibility / tree-display check: tech is known via research or trade.
    /// Use this for resource visibility, tech-tree display. NOT for build
    /// gating — that uses hasResearched().
    [[nodiscard]] bool knows(TechId tech) const {
        if (!tech.isValid()) { return false; }
        if (tech.value < this->completedTechs.size()
            && this->completedTechs[tech.value]) { return true; }
        if (tech.value < this->knownTechs.size()
            && this->knownTechs[tech.value]) { return true; }
        return false;
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
            if (this->currentResearch.value < this->knownTechs.size()) {
                this->knownTechs[this->currentResearch.value] = true;
            }
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

/// Acquire a tech via trade/gift. Marks it KNOWN. If all prereqs are already
/// completed, also marks it COMPLETED (immediately usable). Otherwise it
/// stays known-but-locked until prereqs are met (then promoted by
/// promotePendingTechs each turn).
void acquireTechFromTrade(PlayerTechComponent& tech, TechId techId);

/// Per-turn promotion: scan knownTechs and promote any whose prereqs
/// are now all completed. Cascades: completing one tech may unlock
/// another known-but-locked tech in the same pass.
void promotePendingTechs(PlayerTechComponent& tech);

} // namespace aoc::sim
