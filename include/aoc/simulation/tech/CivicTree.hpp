#pragma once

/**
 * @file CivicTree.hpp
 * @brief Civic tree DAG -- culture-driven progression unlocking governments,
 *        policies, diplomatic options, and monetary system transitions.
 *
 * Mirrors the tech tree structure but uses culture points instead of science.
 */

#include "aoc/core/Types.hpp"

#include <cstdint>
#include <string_view>
#include <vector>

namespace aoc::sim {

struct CivicDef {
    CivicId              id;
    std::string_view     name;
    EraId                era;
    int32_t              cultureCost;
    std::vector<CivicId> prerequisites;
    std::vector<uint8_t> unlockedGovernmentIds;  ///< Government type IDs unlocked by this civic.
    std::vector<uint8_t> unlockedPolicyIds;      ///< Policy card IDs unlocked by this civic.
};

[[nodiscard]] const std::vector<CivicDef>& allCivics();
[[nodiscard]] const CivicDef& civicDef(CivicId id);
[[nodiscard]] uint16_t civicCount();

/// Per-player civic research state (ECS component).
struct PlayerCivicComponent {
    PlayerId owner = INVALID_PLAYER;
    CivicId  currentResearch;
    float    researchProgress = 0.0f;
    std::vector<bool> completedCivics;

    void initialize() {
        this->completedCivics.resize(civicCount(), false);
        this->currentResearch = CivicId{};
    }

    [[nodiscard]] bool hasCompleted(CivicId civic) const {
        if (!civic.isValid() || civic.value >= this->completedCivics.size()) {
            return false;
        }
        return this->completedCivics[civic.value];
    }

    [[nodiscard]] bool canResearch(CivicId civic) const {
        if (this->hasCompleted(civic)) {
            return false;
        }
        const CivicDef& def = civicDef(civic);
        for (CivicId prereq : def.prerequisites) {
            if (!this->hasCompleted(prereq)) {
                return false;
            }
        }
        return true;
    }

    void completeResearch() {
        if (this->currentResearch.isValid() &&
            this->currentResearch.value < this->completedCivics.size()) {
            this->completedCivics[this->currentResearch.value] = true;
        }
        this->currentResearch = CivicId{};
        this->researchProgress = 0.0f;
    }
};

struct PlayerGovernmentComponent;

/// Advance civic research. If gov is non-null, unlocks governments/policies on completion.
bool advanceCivicResearch(PlayerCivicComponent& civic, float culturePoints,
                          PlayerGovernmentComponent* gov = nullptr);

} // namespace aoc::sim
