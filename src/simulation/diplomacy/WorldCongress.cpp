/**
 * @file WorldCongress.cpp
 * @brief World Congress: favor-funded proposals, utility-scored voting,
 *        timed resolution effects.
 */

#include "aoc/game/GameState.hpp"
#include "aoc/game/Player.hpp"
#include "aoc/game/Unit.hpp"
#include "aoc/simulation/diplomacy/WorldCongress.hpp"
#include "aoc/simulation/diplomacy/DiplomacyState.hpp"
#include "aoc/simulation/diplomacy/DiplomaticFavor.hpp"
#include "aoc/simulation/diplomacy/Grievance.hpp"
#include "aoc/simulation/citystate/CityState.hpp"
#include "aoc/ui/GameNotifications.hpp"
#include "aoc/core/Log.hpp"

#include <algorithm>
#include <array>
#include <string>

namespace aoc::sim {

namespace {

constexpr int32_t kProposalCost        = 30;
constexpr int32_t kExtraVoteCost       = 10;
constexpr int32_t kMaxExtraVotes       = 3;
constexpr int32_t kMinProposerFavor    = kProposalCost;
constexpr int32_t kSessionInterval     = 30;
constexpr int32_t kSanctionsDuration   = 20;
constexpr int32_t kCultureBoostTurns   = 10;
constexpr int32_t kClimateAccordTurns  = 20;
constexpr float   kBoostPrestige       = 30.0f;

using aoc::ui::NotificationCategory;

// ---------------------------------------------------------------------------
// Favor accrual helpers
// ---------------------------------------------------------------------------

int32_t countAlliances(const DiplomacyManager& diplomacy, PlayerId pid, int32_t playerCount) {
    int32_t n = 0;
    for (int32_t o = 0; o < playerCount; ++o) {
        const PlayerId other = static_cast<PlayerId>(o);
        if (other == pid) { continue; }
        const PairwiseRelation& rel = diplomacy.relation(pid, other);
        if (rel.hasDefensiveAlliance || rel.hasMilitaryAlliance) { ++n; }
    }
    return n;
}

int32_t countSuzeraintyFor(const aoc::game::GameState& gs, PlayerId pid) {
    int32_t n = 0;
    for (const CityStateComponent& cs : gs.cityStates()) {
        if (cs.suzerain == pid) { ++n; }
    }
    return n;
}

int32_t grievancesAgainst(const aoc::game::GameState& gs, PlayerId pid) {
    int32_t n = 0;
    for (const std::unique_ptr<aoc::game::Player>& other : gs.players()) {
        if (other == nullptr || other->id() == pid) { continue; }
        for (const Grievance& g : other->grievances().grievances) {
            if (g.against == pid) { ++n; }
        }
    }
    return n;
}

void accruePerPlayerFavor(aoc::game::GameState& gs, const DiplomacyManager* diplomacy) {
    const int32_t playerCount = gs.playerCount();
    for (const std::unique_ptr<aoc::game::Player>& p : gs.players()) {
        if (p == nullptr || p->victoryTracker().isEliminated) { continue; }
        const int32_t alliances = (diplomacy != nullptr)
            ? countAlliances(*diplomacy, p->id(), playerCount) : 0;
        const int32_t suze  = countSuzeraintyFor(gs, p->id());
        const int32_t griev = grievancesAgainst(gs, p->id());
        const int32_t perTurn = computeDiplomaticFavor(*p, alliances, suze, griev);
        p->diplomaticFavor().owner = p->id();
        p->diplomaticFavor().favorPerTurn = perTurn;
        p->diplomaticFavor().favor += perTurn;
    }
}

// ---------------------------------------------------------------------------
// Scoring (per-player utility for a resolution)
// ---------------------------------------------------------------------------

int32_t scoreResolution(const aoc::game::Player& voter,
                         const aoc::game::GameState& gs,
                         const DiplomacyManager* diplomacy,
                         Resolution res,
                         PlayerId proposer,
                         PlayerId target) {
    switch (res) {
        case Resolution::BanNuclearWeapons:
            // Everyone prefers nobody else gets nukes. Slight preference yes.
            return 6;

        case Resolution::GlobalSanctions: {
            if (voter.id() == target) { return -50; }
            if (diplomacy == nullptr)  { return 0; }
            const int32_t rel = diplomacy->relation(voter.id(), target).totalScore();
            if (rel >= 40)  { return -20; }   // Ally — protect them
            if (rel >= 10)  { return -5;  }   // Friend
            if (rel <= -40) { return 18; }    // Hostile — happy to sanction
            if (rel <= -10) { return 8;  }
            return 0;
        }

        case Resolution::WorldsFair:
        case Resolution::InternationalGames:
        case Resolution::ClimateAccord: {
            if (voter.id() == proposer) { return 30; }
            if (diplomacy == nullptr)    { return 0; }
            const int32_t rel = diplomacy->relation(voter.id(), proposer).totalScore();
            if (rel >= 40)  { return  8;  }    // Ally — happy to grant prestige
            if (rel >= 10)  { return  3;  }    // Friend — mild support
            if (rel <= -40) { return -12; }    // Hostile — block hard
            if (rel <= -10) { return -5;  }
            return 0;                          // Neutral abstain
        }

        case Resolution::ArmsReduction: {
            // Compare military count to global average. Above avg → hates; below → likes.
            int32_t sum = 0; int32_t count = 0;
            for (const std::unique_ptr<aoc::game::Player>& p : gs.players()) {
                if (p == nullptr || p->victoryTracker().isEliminated) { continue; }
                sum += p->militaryUnitCount();
                ++count;
            }
            const float avg = (count > 0)
                ? static_cast<float>(sum) / static_cast<float>(count) : 0.0f;
            const float mine = static_cast<float>(voter.militaryUnitCount());
            if (mine > avg + 3.0f) { return -15; }
            if (mine < avg - 3.0f) { return 10; }
            return 0;
        }

        default:
            return 0;
    }
}

// ---------------------------------------------------------------------------
// Proposer choice + target selection
// ---------------------------------------------------------------------------

PlayerId selectProposer(const aoc::game::GameState& gs) {
    PlayerId best = INVALID_PLAYER;
    int32_t  bestFavor = kMinProposerFavor - 1;
    for (const std::unique_ptr<aoc::game::Player>& p : gs.players()) {
        if (p == nullptr || p->victoryTracker().isEliminated) { continue; }
        const int32_t f = p->diplomaticFavor().favor;
        if (f > bestFavor) {
            best = p->id();
            bestFavor = f;
        }
    }
    return best;  // INVALID_PLAYER if nobody has enough favor
}

PlayerId selectSanctionsTarget(const aoc::game::GameState& gs,
                                const DiplomacyManager* diplomacy,
                                PlayerId proposer) {
    if (diplomacy == nullptr) { return INVALID_PLAYER; }
    PlayerId worst = INVALID_PLAYER;
    int32_t  worstScore = 0;  // only target players with relation < 0
    for (const std::unique_ptr<aoc::game::Player>& p : gs.players()) {
        if (p == nullptr || p->id() == proposer || p->victoryTracker().isEliminated) {
            continue;
        }
        const int32_t s = diplomacy->relation(proposer, p->id()).totalScore();
        if (s < worstScore) {
            worst = p->id();
            worstScore = s;
        }
    }
    return worst;
}

Resolution selectResolutionForProposer(const aoc::game::Player& proposer,
                                        const aoc::game::GameState& gs,
                                        const DiplomacyManager* diplomacy,
                                        aoc::Random& rng,
                                        PlayerId& outTarget) {
    Resolution best = Resolution::Count;
    int32_t    bestScore = -999;
    PlayerId   bestTarget = INVALID_PLAYER;

    // Shuffle order for tie-breaking variety so ties (e.g. boost resolutions
    // all scoring 30 for proposer) don't always collapse to the first enum.
    std::array<uint8_t, static_cast<std::size_t>(Resolution::Count)> order{};
    for (uint8_t i = 0; i < order.size(); ++i) { order[i] = i; }
    for (int32_t i = static_cast<int32_t>(order.size()) - 1; i > 0; --i) {
        const int32_t j = rng.nextInt(0, i);
        std::swap(order[i], order[j]);
    }

    for (uint8_t idx : order) {
        const Resolution res = static_cast<Resolution>(idx);
        PlayerId tgt = INVALID_PLAYER;
        if (res == Resolution::GlobalSanctions) {
            tgt = selectSanctionsTarget(gs, diplomacy, proposer.id());
            if (tgt == INVALID_PLAYER) { continue; }  // nobody to sanction
        } else if (res == Resolution::WorldsFair
                || res == Resolution::InternationalGames
                || res == Resolution::ClimateAccord) {
            tgt = proposer.id();
        }
        const int32_t s = scoreResolution(proposer, gs, diplomacy, res, proposer.id(), tgt);
        if (s > bestScore) {
            best       = res;
            bestScore  = s;
            bestTarget = tgt;
        }
    }

    if (best == Resolution::Count || bestScore <= 0) {
        // Fallback: random pick, prevents idle congress if proposer is detached.
        best = static_cast<Resolution>(
            rng.nextInt(0, static_cast<int32_t>(Resolution::Count) - 1));
        bestTarget = (best == Resolution::GlobalSanctions)
            ? selectSanctionsTarget(gs, diplomacy, proposer.id())
            : ((best == Resolution::WorldsFair
                || best == Resolution::InternationalGames
                || best == Resolution::ClimateAccord) ? proposer.id() : INVALID_PLAYER);
    }
    outTarget = bestTarget;
    return best;
}

// ---------------------------------------------------------------------------
// Effects
// ---------------------------------------------------------------------------

void applySanctionsBegin(DiplomacyManager* diplomacy,
                          const aoc::game::GameState& gs,
                          PlayerId target) {
    if (diplomacy == nullptr || target == INVALID_PLAYER) { return; }
    for (const std::unique_ptr<aoc::game::Player>& p : gs.players()) {
        if (p == nullptr || p->id() == target) { continue; }
        diplomacy->setEmbargo(p->id(), target, true);
    }
}

void applySanctionsEnd(DiplomacyManager* diplomacy,
                        const aoc::game::GameState& gs,
                        PlayerId target) {
    if (diplomacy == nullptr || target == INVALID_PLAYER) { return; }
    for (const std::unique_ptr<aoc::game::Player>& p : gs.players()) {
        if (p == nullptr || p->id() == target) { continue; }
        diplomacy->setEmbargo(p->id(), target, false);
    }
}

void applyArmsReduction(aoc::game::GameState& gs) {
    for (const std::unique_ptr<aoc::game::Player>& p : gs.players()) {
        if (p == nullptr || p->victoryTracker().isEliminated) { continue; }

        // Collect military units (ptr, combat strength) and pick 2 weakest.
        std::vector<aoc::game::Unit*> mil;
        mil.reserve(p->units().size());
        for (const std::unique_ptr<aoc::game::Unit>& u : p->units()) {
            if (u != nullptr && u->isMilitary()) { mil.push_back(u.get()); }
        }
        std::sort(mil.begin(), mil.end(),
                  [](const aoc::game::Unit* a, const aoc::game::Unit* b) {
                      return a->combatStrength() < b->combatStrength();
                  });
        const std::size_t disbandCount = std::min<std::size_t>(2, mil.size());
        for (std::size_t i = 0; i < disbandCount; ++i) {
            p->removeUnit(mil[i]);
        }
    }
}

void applyBoostPrestige(aoc::game::GameState& gs, PlayerId recipient) {
    if (recipient == INVALID_PLAYER) { return; }
    aoc::game::Player* p = gs.player(recipient);
    if (p == nullptr) { return; }
    p->prestige().diplomacy += kBoostPrestige;
    p->prestige().total     += kBoostPrestige;
}

void applyResolutionEffect(WorldCongressComponent& congress,
                            aoc::game::GameState& gs,
                            DiplomacyManager* diplomacy,
                            Resolution res,
                            PlayerId target) {
    switch (res) {
        case Resolution::BanNuclearWeapons:
            // Binary flag; queried via isResolutionActive(). No active effect needed.
            break;

        case Resolution::GlobalSanctions:
            applySanctionsBegin(diplomacy, gs, target);
            congress.activeEffects.push_back({Resolution::GlobalSanctions, target, kSanctionsDuration});
            break;

        case Resolution::WorldsFair:
            applyBoostPrestige(gs, target);
            congress.activeEffects.push_back({Resolution::WorldsFair, target, kCultureBoostTurns});
            break;

        case Resolution::InternationalGames:
            applyBoostPrestige(gs, target);
            congress.activeEffects.push_back({Resolution::InternationalGames, target, kCultureBoostTurns});
            break;

        case Resolution::ArmsReduction:
            applyArmsReduction(gs);
            break;

        case Resolution::ClimateAccord:
            applyBoostPrestige(gs, target);
            congress.activeEffects.push_back({Resolution::ClimateAccord, target, kClimateAccordTurns});
            break;

        default: break;
    }
}

void tickActiveEffects(WorldCongressComponent& congress,
                        aoc::game::GameState& gs,
                        DiplomacyManager* diplomacy) {
    auto it = congress.activeEffects.begin();
    while (it != congress.activeEffects.end()) {
        --it->turnsRemaining;
        if (it->turnsRemaining <= 0) {
            if (it->type == Resolution::GlobalSanctions) {
                applySanctionsEnd(diplomacy, gs, it->target);
            }
            it = congress.activeEffects.erase(it);
        } else {
            ++it;
        }
    }
}

// ---------------------------------------------------------------------------
// Notifications
// ---------------------------------------------------------------------------

void notifyBroadcast(NotificationCategory cat, std::string title, std::string body,
                      int32_t priority) {
    aoc::ui::GameNotification n;
    n.category = cat;
    n.title    = std::move(title);
    n.body     = std::move(body);
    n.priority = priority;
    aoc::ui::pushNotification(n);
}

void notifyTargeted(NotificationCategory cat, PlayerId target, std::string title,
                     std::string body, int32_t priority) {
    aoc::ui::GameNotification n;
    n.category       = cat;
    n.title          = std::move(title);
    n.body           = std::move(body);
    n.relevantPlayer = target;
    n.priority       = priority;
    aoc::ui::pushNotification(n);
}

} // namespace

// ===========================================================================
// Component methods
// ===========================================================================

void WorldCongressComponent::proposeResolution(Resolution res,
                                                PlayerId prop,
                                                PlayerId target) {
    this->currentProposal  = res;
    this->proposer         = prop;
    this->proposalTarget   = target;
    this->votes.fill(0);
    LOG_INFO("World Congress: Player %u proposes '%s' (target=%d)",
             static_cast<unsigned>(prop), resolutionName(res),
             static_cast<int>(target));
}

void WorldCongressComponent::castVote(PlayerId player, int16_t weight) {
    if (player < this->votes.size()) {
        this->votes[player] = weight;
    }
}

bool WorldCongressComponent::resolveVotes() {
    int32_t yes = 0; int32_t no = 0;
    for (int16_t v : this->votes) {
        if (v > 0)      { yes += v; }
        else if (v < 0) { no  += -v; }
    }
    const bool passed = (yes > no);
    if (passed && this->currentProposal != Resolution::Count) {
        this->passedResolutions.push_back(this->currentProposal);
        LOG_INFO("World Congress: '%s' PASSED (%d yes, %d no)",
                 resolutionName(this->currentProposal), yes, no);
    } else {
        LOG_INFO("World Congress: '%s' FAILED (%d yes, %d no)",
                 resolutionName(this->currentProposal), yes, no);
    }
    return passed;
}

bool WorldCongressComponent::isResolutionActive(Resolution res) const {
    return std::find(this->passedResolutions.begin(),
                     this->passedResolutions.end(), res)
        != this->passedResolutions.end();
}

bool WorldCongressComponent::isEffectActive(Resolution res, PlayerId target) const {
    for (const ActiveResolution& a : this->activeEffects) {
        if (a.type != res) { continue; }
        if (target == INVALID_PLAYER || a.target == target) { return true; }
    }
    return false;
}

// ===========================================================================
// Process
// ===========================================================================

void processWorldCongress(aoc::game::GameState& gameState,
                           TurnNumber /*turn*/,
                           aoc::Random& rng,
                           DiplomacyManager* diplomacy) {
    WorldCongressComponent& congress = gameState.worldCongress();

    // Tick active timed effects first (expires before new session effects).
    tickActiveEffects(congress, gameState, diplomacy);

    // Favor accrual once per turn for every major player.
    accruePerPlayerFavor(gameState, diplomacy);

    if (!congress.isActive) {
        if (congress.turnsUntilNextSession > 0) {
            --congress.turnsUntilNextSession;
        }
        if (congress.turnsUntilNextSession <= 0) {
            congress.isActive = true;
            congress.turnsUntilNextSession = kSessionInterval;
            notifyBroadcast(NotificationCategory::Diplomacy,
                             "World Congress Convened",
                             "The World Congress is now in session. Earn Diplomatic Favor to propose and vote on resolutions.",
                             6);
            LOG_INFO("World Congress is now active");
        }
        return;
    }

    // Two-phase: first turn propose+vote, next turn resolve.
    if (congress.currentProposal == Resolution::Count) {
        --congress.turnsUntilNextSession;
        if (congress.turnsUntilNextSession > 0) { return; }

        const PlayerId proposerId = selectProposer(gameState);
        if (proposerId == INVALID_PLAYER) {
            congress.turnsUntilNextSession = 5;  // retry soon; nobody has favor yet
            return;
        }
        aoc::game::Player* proposer = gameState.player(proposerId);
        if (proposer == nullptr) {
            congress.turnsUntilNextSession = kSessionInterval;
            return;
        }

        PlayerId target = INVALID_PLAYER;
        const Resolution res = selectResolutionForProposer(*proposer, gameState,
                                                            diplomacy, rng, target);
        proposer->diplomaticFavor().spendFavor(kProposalCost);
        congress.proposeResolution(res, proposerId, target);

        // Cast utility votes (proposer auto-yes with full weight).
        for (const std::unique_ptr<aoc::game::Player>& p : gameState.players()) {
            if (p == nullptr || p->victoryTracker().isEliminated) { continue; }
            int32_t score = scoreResolution(*p, gameState, diplomacy, res, proposerId, target);
            if (p->id() == proposerId) { score = std::max(score, 30); }
            int16_t weight = 0;
            if (score > 0)      { weight =  1; }
            else if (score < 0) { weight = -1; }
            if (weight != 0) {
                const int32_t extrasDesired = std::min(kMaxExtraVotes,
                                                        std::abs(score) / 20);
                PlayerDiplomaticFavorComponent& fc = p->diplomaticFavor();
                int32_t extras = 0;
                while (extras < extrasDesired
                       && fc.spendFavor(kExtraVoteCost)) {
                    ++extras;
                }
                weight = static_cast<int16_t>(weight * (1 + extras));
            }
            congress.castVote(p->id(), weight);
        }

        notifyBroadcast(NotificationCategory::Diplomacy,
                         std::string("Resolution Proposed: ") + resolutionName(res),
                         std::string("Player ") + std::to_string(proposerId)
                             + " has proposed " + resolutionName(res)
                             + ". Votes will be tallied next turn.",
                         5);
        if (res == Resolution::GlobalSanctions && target != INVALID_PLAYER) {
            notifyTargeted(NotificationCategory::Diplomacy, target,
                            "Sanctions Vote Targets You",
                            "The World Congress is voting on sanctions against your civilization.",
                            8);
        }
    } else {
        const Resolution res    = congress.currentProposal;
        const PlayerId   target = congress.proposalTarget;
        const bool passed       = congress.resolveVotes();

        if (passed) {
            applyResolutionEffect(congress, gameState, diplomacy, res, target);
            notifyBroadcast(NotificationCategory::Diplomacy,
                             std::string("Resolution Passed: ") + resolutionName(res),
                             std::string(resolutionName(res)) + " has been ratified by the World Congress.",
                             7);
            if (res == Resolution::GlobalSanctions && target != INVALID_PLAYER) {
                notifyTargeted(NotificationCategory::Diplomacy, target,
                                "SANCTIONS IMPOSED",
                                "All civilizations have embargoed your trade for 20 turns.",
                                9);
            }
        } else {
            notifyBroadcast(NotificationCategory::Diplomacy,
                             std::string("Resolution Rejected: ") + resolutionName(res),
                             std::string(resolutionName(res)) + " failed to pass.",
                             4);
        }

        congress.currentProposal = Resolution::Count;
        congress.proposer        = INVALID_PLAYER;
        congress.proposalTarget  = INVALID_PLAYER;
        congress.votes.fill(0);
        congress.turnsUntilNextSession = kSessionInterval;
    }
}

} // namespace aoc::sim
