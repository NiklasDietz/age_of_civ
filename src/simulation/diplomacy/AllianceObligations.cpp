/**
 * @file AllianceObligations.cpp
 * @brief Alliance obligation tracking and enforcement.
 *
 * When a player with defensive or military alliance partners is attacked,
 * each allied player receives an obligation to respond within 5 turns.
 * Valid responses: declare war on attacker, impose embargo, or be at war
 * with the attacker through other means. Failure incurs reputation loss
 * with ALL alliance members.
 */

#include "aoc/simulation/diplomacy/AllianceObligations.hpp"

#include "aoc/simulation/diplomacy/DiplomacyState.hpp"
#include "aoc/simulation/diplomacy/Grievance.hpp"
#include "aoc/game/GameState.hpp"
#include "aoc/game/Player.hpp"
#include "aoc/core/Log.hpp"

#include <algorithm>

namespace aoc::sim {

void AllianceObligationTracker::onWarDeclared(PlayerId aggressor, PlayerId target,
                                               const DiplomacyManager& diplomacy) {
    uint8_t playerCount = diplomacy.playerCount();

    for (uint8_t p = 0; p < playerCount; ++p) {
        PlayerId candidate = static_cast<PlayerId>(p);
        if (candidate == aggressor || candidate == target) { continue; }

        const PairwiseRelation& rel = diplomacy.relation(candidate, target);
        if (!rel.hasDefensiveAlliance && !rel.hasMilitaryAlliance) { continue; }

        // Don't create duplicate obligations for the same conflict
        bool alreadyObligated = false;
        for (const AllianceObligation& existing : this->pendingObligations) {
            if (existing.obligatedPlayer == candidate
                && existing.attacker == aggressor
                && existing.defender == target) {
                alreadyObligated = true;
                break;
            }
        }
        if (alreadyObligated) { continue; }

        AllianceObligation obligation{};
        obligation.defender = target;
        obligation.attacker = aggressor;
        obligation.obligatedPlayer = candidate;
        obligation.turnsToRespond = 5;
        obligation.fulfilled = false;

        this->pendingObligations.push_back(obligation);

        LOG_INFO("Alliance obligation created: Player %u must respond to attack on ally Player %u "
                 "by Player %u within 5 turns",
                 static_cast<unsigned>(candidate),
                 static_cast<unsigned>(target),
                 static_cast<unsigned>(aggressor));
    }
}

void AllianceObligationTracker::checkFulfillment(const DiplomacyManager& diplomacy) {
    for (AllianceObligation& obligation : this->pendingObligations) {
        if (obligation.fulfilled) { continue; }

        const PairwiseRelation& rel = diplomacy.relation(obligation.obligatedPlayer,
                                                          obligation.attacker);

        // Fulfilled if: at war with attacker OR has embargo on attacker
        if (rel.isAtWar || rel.hasEmbargo) {
            obligation.fulfilled = true;
            LOG_INFO("Alliance obligation fulfilled: Player %u responded to attack on Player %u",
                     static_cast<unsigned>(obligation.obligatedPlayer),
                     static_cast<unsigned>(obligation.defender));
        }
    }
}

void AllianceObligationTracker::tickObligations(DiplomacyManager& diplomacy,
                                                 aoc::game::GameState& gameState) {
    // First check if any obligations have been newly fulfilled
    this->checkFulfillment(diplomacy);

    // Tick timers and apply penalties for expired unfulfilled obligations
    std::vector<AllianceObligation>::iterator it = this->pendingObligations.begin();
    while (it != this->pendingObligations.end()) {
        if (it->fulfilled) {
            it = this->pendingObligations.erase(it);
            continue;
        }

        --it->turnsToRespond;

        if (it->turnsToRespond <= 0) {
            // Obligation expired unfulfilled — apply penalties
            PlayerId failedPlayer = it->obligatedPlayer;
            PlayerId defender = it->defender;

            // -20 reputation with the defender
            diplomacy.addReputationModifier(failedPlayer, defender, -20, 60);

            // -10 reputation with all other alliance members of the defender
            uint8_t playerCount = diplomacy.playerCount();
            for (uint8_t p = 0; p < playerCount; ++p) {
                PlayerId other = static_cast<PlayerId>(p);
                if (other == failedPlayer || other == defender) { continue; }

                const PairwiseRelation& allianceRel = diplomacy.relation(other, defender);
                if (allianceRel.hasDefensiveAlliance || allianceRel.hasMilitaryAlliance) {
                    diplomacy.addReputationModifier(failedPlayer, other, -10, 40);
                }
            }

            // Add grievance to the defender
            aoc::game::Player* defenderPlayer = gameState.player(defender);
            if (defenderPlayer != nullptr) {
                defenderPlayer->grievances().addGrievance(
                    GrievanceType::FailedAllianceObligation, failedPlayer);
            }

            LOG_INFO("Alliance obligation FAILED: Player %u did not respond to attack on ally "
                     "Player %u. Reputation penalties applied.",
                     static_cast<unsigned>(failedPlayer),
                     static_cast<unsigned>(defender));

            it = this->pendingObligations.erase(it);
        } else {
            ++it;
        }
    }
}

} // namespace aoc::sim
