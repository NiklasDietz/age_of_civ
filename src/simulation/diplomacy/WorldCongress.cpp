/**
 * @file WorldCongress.cpp
 * @brief World Congress voting system implementation.
 */

#include "aoc/simulation/diplomacy/WorldCongress.hpp"
#include "aoc/ecs/World.hpp"
#include "aoc/core/Log.hpp"

#include <algorithm>

namespace aoc::sim {

void WorldCongressComponent::proposeResolution(Resolution res) {
    this->currentProposal = res;
    this->votes.fill(0);
    LOG_INFO("World Congress: proposed '%s'", resolutionName(res));
}

void WorldCongressComponent::castVote(PlayerId player, int8_t vote) {
    if (player < this->votes.size()) {
        this->votes[player] = vote;
    }
}

bool WorldCongressComponent::resolveVotes() {
    int32_t yesVotes = 0;
    int32_t noVotes  = 0;

    for (int8_t v : this->votes) {
        if (v > 0) {
            ++yesVotes;
        } else if (v < 0) {
            ++noVotes;
        }
    }

    const bool passed = (yesVotes > noVotes);
    if (passed && this->currentProposal != Resolution::Count) {
        this->passedResolutions.push_back(this->currentProposal);
        LOG_INFO("World Congress: '%s' PASSED (%d yes, %d no)",
                 resolutionName(this->currentProposal), yesVotes, noVotes);
    } else {
        LOG_INFO("World Congress: '%s' FAILED (%d yes, %d no)",
                 resolutionName(this->currentProposal), yesVotes, noVotes);
    }

    this->currentProposal = Resolution::Count;
    this->votes.fill(0);
    return passed;
}

bool WorldCongressComponent::isResolutionActive(Resolution res) const {
    return std::find(this->passedResolutions.begin(),
                     this->passedResolutions.end(), res)
           != this->passedResolutions.end();
}

void processWorldCongress(aoc::ecs::World& world, TurnNumber /*turn*/,
                           aoc::Random& rng) {
    aoc::ecs::ComponentPool<WorldCongressComponent>* pool =
        world.getPool<WorldCongressComponent>();
    if (pool == nullptr) {
        return;
    }

    for (uint32_t i = 0; i < pool->size(); ++i) {
        WorldCongressComponent& congress = pool->data()[i];

        if (!congress.isActive) {
            if (congress.turnsUntilNextSession > 0) {
                --congress.turnsUntilNextSession;
            }
            if (congress.turnsUntilNextSession == 0) {
                congress.isActive = true;
                LOG_INFO("World Congress is now active");
            }
            continue;
        }

        // If no active proposal, propose a new one every 30 turns
        if (congress.currentProposal == Resolution::Count) {
            --congress.turnsUntilNextSession;
            if (congress.turnsUntilNextSession <= 0) {
                // Pick a random resolution
                const uint8_t resIdx = static_cast<uint8_t>(
                    rng.nextInt(0, static_cast<int32_t>(Resolution::Count) - 1));
                congress.proposeResolution(static_cast<Resolution>(resIdx));

                // AI players vote randomly (simple heuristic)
                for (uint8_t p = 0; p < 16; ++p) {
                    const int8_t aiVote = static_cast<int8_t>(rng.nextInt(-1, 1));
                    congress.castVote(p, aiVote);
                }

                congress.turnsUntilNextSession = 30;
            }
        } else {
            // Resolve the current proposal
            congress.resolveVotes();
        }
    }
}

} // namespace aoc::sim
