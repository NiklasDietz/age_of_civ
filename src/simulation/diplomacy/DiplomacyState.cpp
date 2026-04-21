/**
 * @file DiplomacyState.cpp
 * @brief Diplomacy relation matrix management.
 */

#include "aoc/simulation/diplomacy/DiplomacyState.hpp"
#include "aoc/simulation/diplomacy/AllianceObligations.hpp"
#include "aoc/ui/GameNotifications.hpp"
#include "aoc/core/Log.hpp"

#include <algorithm>
#include <cassert>

namespace aoc::sim {

void DiplomacyManager::initialize(uint8_t playerCount) {
    this->m_playerCount = playerCount;
    this->m_relations.resize(
        static_cast<std::size_t>(playerCount) * static_cast<std::size_t>(playerCount));
}

PairwiseRelation& DiplomacyManager::relation(PlayerId a, PlayerId b) {
    assert(a < this->m_playerCount && b < this->m_playerCount);
    return this->m_relations[static_cast<std::size_t>(a) * this->m_playerCount + b];
}

const PairwiseRelation& DiplomacyManager::relation(PlayerId a, PlayerId b) const {
    assert(a < this->m_playerCount && b < this->m_playerCount);
    return this->m_relations[static_cast<std::size_t>(a) * this->m_playerCount + b];
}

void DiplomacyManager::meetPlayers(PlayerId a, PlayerId b, int32_t currentTurn) {
    if (a == b) { return; }
    PairwiseRelation& relAB = this->relation(a, b);
    PairwiseRelation& relBA = this->relation(b, a);
    if (!relAB.hasMet) {
        relAB.hasMet = true;
        relAB.metOnTurn = currentTurn;
        relBA.hasMet = true;
        relBA.metOnTurn = currentTurn;
        LOG_INFO("First contact: Player %u met Player %u on turn %d",
                 static_cast<unsigned>(a), static_cast<unsigned>(b), currentTurn);
    }
}

bool DiplomacyManager::haveMet(PlayerId a, PlayerId b) const {
    if (a == b) { return true; }
    return this->relation(a, b).hasMet;
}

void DiplomacyManager::addModifier(PlayerId a, PlayerId b, RelationModifier modifier) {
    this->relation(a, b).modifiers.push_back(modifier);
    // Mirror: symmetric relation
    if (a != b) {
        this->relation(b, a).modifiers.push_back(std::move(modifier));
    }
}

void DiplomacyManager::declareWar(PlayerId aggressor, PlayerId target,
                                   CasusBelliType cb,
                                   AllianceObligationTracker* allianceTracker) {
    PairwiseRelation& relAB = this->relation(aggressor, target);
    PairwiseRelation& relBA = this->relation(target, aggressor);

    // War implies meeting
    if (!relAB.hasMet) {
        relAB.hasMet = true;
        relAB.metOnTurn = 0;
        relBA.hasMet = true;
        relBA.metOnTurn = 0;
    }

    relAB.isAtWar = true;
    relBA.isAtWar = true;

    // Track aggressor for treaty compliance (NonAggression enforcement)
    relAB.lastWarAggressor = aggressor;
    relBA.lastWarAggressor = aggressor;
    relAB.lastCasusBelli   = cb;
    relBA.lastCasusBelli   = cb;

    // Wipe accumulated peace-time warming
    relAB.passiveBonus = 0;
    relBA.passiveBonus = 0;

    // War modifier scaled by CB multiplier (H1.5). Liberation/Reconquest/
    // Protectorate CBs (multiplier 0) produce no relation penalty; Surprise War
    // retains the full -50.
    const int32_t warPenalty = -static_cast<int32_t>(
        casusBelliDef(cb).grievanceMultiplier * 50.0f);
    RelationModifier warMod{"Declared war", warPenalty, 0};  // Permanent until peace
    relAB.modifiers.push_back(warMod);
    relBA.modifiers.push_back(warMod);

    // Break open borders and alliances
    relAB.hasOpenBorders = false;
    relBA.hasOpenBorders = false;
    relAB.hasDefensiveAlliance = false;
    relBA.hasDefensiveAlliance = false;
    relAB.hasMilitaryAlliance = false;
    relBA.hasMilitaryAlliance = false;
    relAB.hasResearchAgreement = false;
    relBA.hasResearchAgreement = false;
    relAB.hasEconomicAlliance = false;
    relBA.hasEconomicAlliance = false;
    relAB.hasCulturalAlliance = false;
    relBA.hasCulturalAlliance = false;
    relAB.hasReligiousAlliance = false;
    relBA.hasReligiousAlliance = false;
    for (std::size_t i = 0; i < relAB.alliances.size(); ++i) {
        relAB.alliances[i] = AllianceState{};
        relBA.alliances[i] = AllianceState{};
    }
    relAB.allianceBreakWarningTurns = 0;
    relBA.allianceBreakWarningTurns = 0;

    // Generate alliance obligations for the target's allies
    if (allianceTracker != nullptr) {
        allianceTracker->onWarDeclared(aggressor, target, *this);
    }

    LOG_INFO("Player %u declared war on Player %u",
             static_cast<unsigned>(aggressor), static_cast<unsigned>(target));

    {
        aoc::ui::GameNotification n;
        n.category = aoc::ui::NotificationCategory::Diplomacy;
        n.title = "WAR DECLARED!";
        n.body = "Player " + std::to_string(aggressor)
               + " declared war on Player " + std::to_string(target) + ".";
        n.relevantPlayer = aggressor;
        n.otherPlayer    = target;
        n.priority = 10;
        aoc::ui::pushNotification(n);
    }
}

void DiplomacyManager::makePeace(PlayerId a, PlayerId b) {
    PairwiseRelation& relAB = this->relation(a, b);
    PairwiseRelation& relBA = this->relation(b, a);

    relAB.isAtWar = false;
    relBA.isAtWar = false;
    relAB.turnsSincePeace = 0;
    relBA.turnsSincePeace = 0;

    // auto required: lambda type is unnameable
    auto removeWar = [](std::vector<RelationModifier>& mods) {
        mods.erase(
            std::remove_if(mods.begin(), mods.end(),
                [](const RelationModifier& m) { return m.reason == "Declared war"; }),
            mods.end());
    };
    removeWar(relAB.modifiers);
    removeWar(relBA.modifiers);

    // Post-war cooldown
    RelationModifier peaceMod{"Recent peace", -20, 30};
    relAB.modifiers.push_back(peaceMod);
    relBA.modifiers.push_back(peaceMod);

    LOG_INFO("Peace between Player %u and Player %u",
             static_cast<unsigned>(a), static_cast<unsigned>(b));

    {
        aoc::ui::GameNotification n;
        n.category = aoc::ui::NotificationCategory::Diplomacy;
        n.title = "Peace Treaty";
        n.body = "Player " + std::to_string(a) + " and Player "
               + std::to_string(b) + " have made peace.";
        n.relevantPlayer = a;
        n.otherPlayer    = b;
        n.priority = 6;
        aoc::ui::pushNotification(n);
    }
}

namespace {

void pushDiplomaticNotification(PlayerId a, PlayerId b,
                                 const std::string& title,
                                 const std::string& body,
                                 int32_t priority) {
    aoc::ui::GameNotification n;
    n.category = aoc::ui::NotificationCategory::Diplomacy;
    n.title = title;
    n.body = body;
    n.relevantPlayer = a;
    n.otherPlayer    = b;
    n.priority = priority;
    aoc::ui::pushNotification(n);
}

} // namespace

void DiplomacyManager::grantOpenBorders(PlayerId a, PlayerId b) {
    this->relation(a, b).hasOpenBorders = true;
    this->relation(b, a).hasOpenBorders = true;
    this->addModifier(a, b, {"Open borders", 5, 0});
    pushDiplomaticNotification(a, b, "Open Borders Granted",
        "Player " + std::to_string(a) + " and Player " + std::to_string(b)
        + " opened their borders.", 4);
}

namespace {

/// 20-turn lockout after the most recent alliance formation on a pair.
/// Prevents rapid cycling through alliance types.
constexpr int32_t ALLIANCE_FORM_COOLDOWN_TURNS = 20;

/// Common preconditions for every form*Alliance function (H1.3 + H1.6).
/// Returns Ok if the alliance may be formed, or AllianceExists otherwise.
[[nodiscard]] ErrorCode checkAllianceFormable(const PairwiseRelation& rel,
                                               int32_t currentTurn) {
    if (rel.hasAnyAlliance()) {
        return ErrorCode::AllianceExists;
    }
    if (rel.lastAllianceFormTurn >= 0
        && (currentTurn - rel.lastAllianceFormTurn) < ALLIANCE_FORM_COOLDOWN_TURNS) {
        return ErrorCode::AllianceExists;
    }
    return ErrorCode::Ok;
}

/// Seed AllianceState for both directions after a successful form call.
void seedAllianceState(PairwiseRelation& relAB, PairwiseRelation& relBA,
                       AllianceType type, int32_t currentTurn) {
    const std::size_t slot = static_cast<std::size_t>(type);
    relAB.alliances[slot].type        = type;
    relAB.alliances[slot].level       = AllianceLevel::Level1;
    relAB.alliances[slot].turnsActive = 0;
    relBA.alliances[slot]             = relAB.alliances[slot];
    relAB.lastAllianceFormTurn = currentTurn;
    relBA.lastAllianceFormTurn = currentTurn;
    relAB.allianceBreakWarningTurns = 0;
    relBA.allianceBreakWarningTurns = 0;
}

} // namespace

ErrorCode DiplomacyManager::formDefensiveAlliance(PlayerId a, PlayerId b, int32_t currentTurn) {
    PairwiseRelation& relAB = this->relation(a, b);
    PairwiseRelation& relBA = this->relation(b, a);
    const ErrorCode ec = checkAllianceFormable(relAB, currentTurn);
    if (ec != ErrorCode::Ok) { return ec; }

    relAB.hasDefensiveAlliance = true;
    relBA.hasDefensiveAlliance = true;
    // Defensive alliance has no entry in ALLIANCE_TYPE_DEFS; level tracking is
    // skipped. The boolean alone drives AllianceObligations.
    relAB.lastAllianceFormTurn = currentTurn;
    relBA.lastAllianceFormTurn = currentTurn;
    relAB.allianceBreakWarningTurns = 0;
    relBA.allianceBreakWarningTurns = 0;

    this->addModifier(a, b, {"Defensive alliance", 15, 0});
    pushDiplomaticNotification(a, b, "Defensive Alliance",
        "Player " + std::to_string(a) + " and Player " + std::to_string(b)
        + " formed a defensive alliance.", 7);
    return ErrorCode::Ok;
}

ErrorCode DiplomacyManager::formMilitaryAlliance(PlayerId a, PlayerId b, int32_t currentTurn) {
    PairwiseRelation& relAB = this->relation(a, b);
    PairwiseRelation& relBA = this->relation(b, a);
    const ErrorCode ec = checkAllianceFormable(relAB, currentTurn);
    if (ec != ErrorCode::Ok) { return ec; }

    relAB.hasMilitaryAlliance = true;
    relBA.hasMilitaryAlliance = true;
    seedAllianceState(relAB, relBA, AllianceType::Military, currentTurn);

    this->addModifier(a, b, {"Military alliance", 10, 0});
    LOG_INFO("Military alliance formed between Player %u and Player %u",
             static_cast<unsigned>(a), static_cast<unsigned>(b));
    pushDiplomaticNotification(a, b, "Military Alliance",
        "Player " + std::to_string(a) + " and Player " + std::to_string(b)
        + " formed a military alliance.", 8);
    return ErrorCode::Ok;
}

ErrorCode DiplomacyManager::formResearchAgreement(PlayerId a, PlayerId b, int32_t currentTurn) {
    PairwiseRelation& relAB = this->relation(a, b);
    PairwiseRelation& relBA = this->relation(b, a);
    const ErrorCode ec = checkAllianceFormable(relAB, currentTurn);
    if (ec != ErrorCode::Ok) { return ec; }

    relAB.hasResearchAgreement = true;
    relBA.hasResearchAgreement = true;
    seedAllianceState(relAB, relBA, AllianceType::Research, currentTurn);

    this->addModifier(a, b, {"Research agreement", 5, 0});
    LOG_INFO("Research agreement formed between Player %u and Player %u",
             static_cast<unsigned>(a), static_cast<unsigned>(b));
    pushDiplomaticNotification(a, b, "Research Agreement",
        "Player " + std::to_string(a) + " and Player " + std::to_string(b)
        + " signed a research agreement.", 5);
    return ErrorCode::Ok;
}

ErrorCode DiplomacyManager::formEconomicAlliance(PlayerId a, PlayerId b, int32_t currentTurn) {
    PairwiseRelation& relAB = this->relation(a, b);
    PairwiseRelation& relBA = this->relation(b, a);
    const ErrorCode ec = checkAllianceFormable(relAB, currentTurn);
    if (ec != ErrorCode::Ok) { return ec; }

    relAB.hasEconomicAlliance = true;
    relBA.hasEconomicAlliance = true;
    seedAllianceState(relAB, relBA, AllianceType::Economic, currentTurn);

    this->addModifier(a, b, {"Economic alliance", 5, 0});
    LOG_INFO("Economic alliance formed between Player %u and Player %u",
             static_cast<unsigned>(a), static_cast<unsigned>(b));
    pushDiplomaticNotification(a, b, "Economic Alliance",
        "Player " + std::to_string(a) + " and Player " + std::to_string(b)
        + " formed an economic alliance.", 5);
    return ErrorCode::Ok;
}

ErrorCode DiplomacyManager::formCulturalAlliance(PlayerId a, PlayerId b, int32_t currentTurn) {
    PairwiseRelation& relAB = this->relation(a, b);
    PairwiseRelation& relBA = this->relation(b, a);
    const ErrorCode ec = checkAllianceFormable(relAB, currentTurn);
    if (ec != ErrorCode::Ok) { return ec; }

    relAB.hasCulturalAlliance = true;
    relBA.hasCulturalAlliance = true;
    seedAllianceState(relAB, relBA, AllianceType::Cultural, currentTurn);

    this->addModifier(a, b, {"Cultural alliance", 5, 0});
    LOG_INFO("Cultural alliance formed between Player %u and Player %u",
             static_cast<unsigned>(a), static_cast<unsigned>(b));
    pushDiplomaticNotification(a, b, "Cultural Alliance",
        "Player " + std::to_string(a) + " and Player " + std::to_string(b)
        + " formed a cultural alliance.", 5);
    return ErrorCode::Ok;
}

ErrorCode DiplomacyManager::formReligiousAlliance(PlayerId a, PlayerId b, int32_t currentTurn) {
    PairwiseRelation& relAB = this->relation(a, b);
    PairwiseRelation& relBA = this->relation(b, a);
    const ErrorCode ec = checkAllianceFormable(relAB, currentTurn);
    if (ec != ErrorCode::Ok) { return ec; }

    relAB.hasReligiousAlliance = true;
    relBA.hasReligiousAlliance = true;
    seedAllianceState(relAB, relBA, AllianceType::Religious, currentTurn);

    this->addModifier(a, b, {"Religious alliance", 5, 0});
    LOG_INFO("Religious alliance formed between Player %u and Player %u",
             static_cast<unsigned>(a), static_cast<unsigned>(b));
    pushDiplomaticNotification(a, b, "Religious Alliance",
        "Player " + std::to_string(a) + " and Player " + std::to_string(b)
        + " formed a religious alliance.", 5);
    return ErrorCode::Ok;
}

void DiplomacyManager::addReputationModifier(PlayerId a, PlayerId b,
                                               int32_t amount, int32_t decayTurns) {
    ReputationModifier mod{amount, decayTurns};
    this->relation(a, b).reputationModifiers.push_back(mod);
    // NOT symmetric: reputation is directional. "A's reputation with B" is
    // independent of "B's reputation with A". You can be trustworthy toward
    // one player and a backstabber toward another.
}

void DiplomacyManager::tickModifiers() {
    constexpr int32_t PASSIVE_WARMING_CAP = 20;
    constexpr int32_t PASSIVE_WARMING_PER_TURN = 1;
    constexpr int32_t PASSIVE_WARMING_GRACE_TURNS = 5;  // wait a bit after peace
    for (PairwiseRelation& rel : this->m_relations) {
        // Increment peace cooldown timer
        if (!rel.isAtWar && rel.turnsSincePeace < 1000) {
            ++rel.turnsSincePeace;
        }
        // Passive warming: met + at peace past grace window => slow warming toward cap.
        // Breaks the chicken-and-egg where open-borders gate (>10) can never be reached.
        if (rel.hasMet && !rel.isAtWar && rel.turnsSincePeace > PASSIVE_WARMING_GRACE_TURNS) {
            if (rel.passiveBonus < PASSIVE_WARMING_CAP) {
                rel.passiveBonus += PASSIVE_WARMING_PER_TURN;
            }
        }
        // Decay relation modifiers
        for (std::vector<RelationModifier>::iterator it = rel.modifiers.begin(); it != rel.modifiers.end(); ) {
            if (it->turnsRemaining > 0) {
                --it->turnsRemaining;
                if (it->turnsRemaining == 0) {
                    it = rel.modifiers.erase(it);
                    continue;
                }
            }
            ++it;
        }
        // Decay reputation modifiers
        for (std::vector<ReputationModifier>::iterator it = rel.reputationModifiers.begin();
             it != rel.reputationModifiers.end(); ) {
            if (it->turnsRemaining > 0) {
                --it->turnsRemaining;
                if (it->turnsRemaining == 0) {
                    it = rel.reputationModifiers.erase(it);
                    continue;
                }
            }
            ++it;
        }
    }

    // Pair-level pass: alliance level progression (H1.1) + auto-break (H1.4).
    // Iterate only a<b to avoid double-ticking the mirrored directions.
    constexpr int32_t AUTO_BREAK_SCORE_THRESHOLD = -30;
    constexpr int32_t AUTO_BREAK_WARNING_TURNS   = 2;
    constexpr int32_t AUTO_BREAK_REP_PENALTY     = -20;
    constexpr int32_t AUTO_BREAK_REP_DECAY_TURNS = 40;
    for (uint8_t a = 0; a < this->m_playerCount; ++a) {
        for (uint8_t b = static_cast<uint8_t>(a + 1); b < this->m_playerCount; ++b) {
            PairwiseRelation& relAB = this->relation(
                static_cast<PlayerId>(a), static_cast<PlayerId>(b));
            PairwiseRelation& relBA = this->relation(
                static_cast<PlayerId>(b), static_cast<PlayerId>(a));

            if (!relAB.hasAnyAlliance()) {
                relAB.allianceBreakWarningTurns = 0;
                relBA.allianceBreakWarningTurns = 0;
                continue;
            }

            for (std::size_t i = 1; i < relAB.alliances.size(); ++i) {
                if (relAB.alliances[i].isActive()) {
                    relAB.alliances[i].tick();
                    relBA.alliances[i] = relAB.alliances[i];
                }
            }

            if (relAB.totalScore() < AUTO_BREAK_SCORE_THRESHOLD) {
                ++relAB.allianceBreakWarningTurns;
                relBA.allianceBreakWarningTurns = relAB.allianceBreakWarningTurns;
                if (relAB.allianceBreakWarningTurns >= AUTO_BREAK_WARNING_TURNS) {
                    relAB.hasDefensiveAlliance = false;
                    relBA.hasDefensiveAlliance = false;
                    relAB.hasMilitaryAlliance  = false;
                    relBA.hasMilitaryAlliance  = false;
                    relAB.hasResearchAgreement = false;
                    relBA.hasResearchAgreement = false;
                    relAB.hasEconomicAlliance  = false;
                    relBA.hasEconomicAlliance  = false;
                    relAB.hasCulturalAlliance  = false;
                    relBA.hasCulturalAlliance  = false;
                    relAB.hasReligiousAlliance = false;
                    relBA.hasReligiousAlliance = false;
                    for (std::size_t i = 0; i < relAB.alliances.size(); ++i) {
                        relAB.alliances[i] = AllianceState{};
                        relBA.alliances[i] = AllianceState{};
                    }
                    relAB.allianceBreakWarningTurns = 0;
                    relBA.allianceBreakWarningTurns = 0;

                    this->addReputationModifier(
                        static_cast<PlayerId>(a), static_cast<PlayerId>(b),
                        AUTO_BREAK_REP_PENALTY, AUTO_BREAK_REP_DECAY_TURNS);
                    this->addReputationModifier(
                        static_cast<PlayerId>(b), static_cast<PlayerId>(a),
                        AUTO_BREAK_REP_PENALTY, AUTO_BREAK_REP_DECAY_TURNS);

                    LOG_INFO("Alliance auto-broken between Player %u and Player %u (score %d)",
                             static_cast<unsigned>(a), static_cast<unsigned>(b),
                             relAB.totalScore());
                    pushDiplomaticNotification(
                        static_cast<PlayerId>(a), static_cast<PlayerId>(b),
                        "Alliance Broken",
                        "Relations between Player " + std::to_string(a)
                            + " and Player " + std::to_string(b)
                            + " collapsed; all alliances dissolved.", 8);
                }
            } else {
                relAB.allianceBreakWarningTurns = 0;
                relBA.allianceBreakWarningTurns = 0;
            }
        }
    }
}

void DiplomacyManager::setEmbargo(PlayerId a, PlayerId b, bool embargo) {
    PairwiseRelation& relAB = this->relation(a, b);
    PairwiseRelation& relBA = this->relation(b, a);

    relAB.hasEmbargo = embargo;
    relBA.hasEmbargo = embargo;

    if (embargo) {
        RelationModifier embargoMod{"Trade Embargo", -15, 20};
        relAB.modifiers.push_back(embargoMod);
        relBA.modifiers.push_back(embargoMod);

        LOG_INFO("Trade embargo set between Player %u and Player %u",
                 static_cast<unsigned>(a), static_cast<unsigned>(b));
    } else {
        // Remove embargo modifiers
        // auto required: lambda type is unnameable
        auto removeEmbargo = [](std::vector<RelationModifier>& mods) {
            mods.erase(
                std::remove_if(mods.begin(), mods.end(),
                    [](const RelationModifier& m) { return m.reason == "Trade Embargo"; }),
                mods.end());
        };
        removeEmbargo(relAB.modifiers);
        removeEmbargo(relBA.modifiers);

        LOG_INFO("Trade embargo lifted between Player %u and Player %u",
                 static_cast<unsigned>(a), static_cast<unsigned>(b));
    }
}

bool DiplomacyManager::hasEmbargo(PlayerId a, PlayerId b) const {
    return this->relation(a, b).hasEmbargo;
}

void DiplomacyManager::setResourceEmbargo(PlayerId a, PlayerId b,
                                            uint16_t goodId, bool embargo) {
    PairwiseRelation& relAB = this->relation(a, b);
    PairwiseRelation& relBA = this->relation(b, a);

    if (embargo) {
        // Add to both directions (symmetric)
        relAB.embargoedGoods.push_back(goodId);
        relBA.embargoedGoods.push_back(goodId);
        LOG_INFO("Resource embargo set: Player %u <-> Player %u, good %u",
                 static_cast<unsigned>(a), static_cast<unsigned>(b),
                 static_cast<unsigned>(goodId));
    } else {
        // Remove from both directions
        // auto required: lambda type is unnameable
        auto removeGood = [goodId](std::vector<uint16_t>& goods) {
            goods.erase(std::remove(goods.begin(), goods.end(), goodId), goods.end());
        };
        removeGood(relAB.embargoedGoods);
        removeGood(relBA.embargoedGoods);
        LOG_INFO("Resource embargo lifted: Player %u <-> Player %u, good %u",
                 static_cast<unsigned>(a), static_cast<unsigned>(b),
                 static_cast<unsigned>(goodId));
    }
}

bool DiplomacyManager::hasResourceEmbargo(PlayerId a, PlayerId b, uint16_t goodId) const {
    return this->relation(a, b).isGoodEmbargoed(goodId);
}

void DiplomacyManager::broadcastReputationPenalty(PlayerId violator, int32_t amount,
                                                    int32_t decayTurns) {
    for (uint8_t p = 0; p < this->m_playerCount; ++p) {
        PlayerId other = static_cast<PlayerId>(p);
        if (other == violator) { continue; }
        if (!this->haveMet(violator, other)) { continue; }
        this->addReputationModifier(violator, other, amount, decayTurns);
    }
}

bool DiplomacyManager::isAtWar(PlayerId a, PlayerId b) const {
    // Barbarians are always at war with everyone.
    if (a == BARBARIAN_PLAYER || b == BARBARIAN_PLAYER) {
        return true;
    }
    return this->relation(a, b).isAtWar;
}

} // namespace aoc::sim
