/**
 * @file AIDiplomacyController.cpp
 * @brief AI diplomacy turn-step. Extracted from AIController.cpp on
 *        2026-05-04 to reduce the 3364-line god-file.
 */

#include "aoc/game/City.hpp"
#include "aoc/game/GameState.hpp"
#include "aoc/game/Player.hpp"
#include "aoc/game/Unit.hpp"
#include "aoc/simulation/ai/AIController.hpp"
#include "aoc/simulation/ai/LeaderPersonality.hpp"
#include "aoc/simulation/city/CityComponent.hpp"
#include "aoc/simulation/city/District.hpp"
#include "aoc/simulation/city/ProductionQueue.hpp"
#include "aoc/simulation/diplomacy/DiplomacyState.hpp"
#include "aoc/simulation/diplomacy/DealTerms.hpp"
#include "aoc/simulation/diplomacy/EspionageSystem.hpp"
#include "aoc/simulation/economy/Market.hpp"
#include "aoc/simulation/economy/TradeAgreement.hpp"
#include "aoc/simulation/economy/TradeRoute.hpp"
#include "aoc/simulation/economy/TradeRouteSystem.hpp"
#include "aoc/simulation/resource/ResourceComponent.hpp"
#include "aoc/simulation/resource/ResourceTypes.hpp"
#include "aoc/simulation/tech/TechGating.hpp"
#include "aoc/simulation/unit/UnitTypes.hpp"
#include "aoc/map/HexGrid.hpp"
#include "aoc/core/Log.hpp"

#include <algorithm>
#include <array>
#include <limits>
#include <unordered_map>

namespace aoc::sim::ai {

void AIController::executeDiplomacyActions(aoc::game::GameState& gameState,
                                            aoc::map::HexGrid& grid,
                                            DiplomacyManager& diplomacy,
                                            const Market& market,
                                            GlobalDealTracker* dealTracker) {
    constexpr uint8_t MAX_PLAYER_COUNT = 16;
    std::array<int32_t, MAX_PLAYER_COUNT> militaryCounts{};
    for (const std::unique_ptr<aoc::game::Player>& p : gameState.players()) {
        const PlayerId pid = p->id();
        if (pid < MAX_PLAYER_COUNT) {
            militaryCounts[static_cast<std::size_t>(pid)] =
                static_cast<int32_t>(p->militaryUnitCount());
        }
    }

    const int32_t ourMilitary = militaryCounts[static_cast<std::size_t>(this->m_player)];
    const uint8_t playerCount = diplomacy.playerCount();

    // Leader personality drives war/peace thresholds.
    // Aggressive leaders (Montezuma: aggression=1.7, warThreshold=1.0) declare
    // war easily; peaceful leaders (Gandhi: aggression=0.2, warThreshold=5.0)
    // almost never do.
    const aoc::game::Player* gsPlayer = gameState.player(this->m_player);
    const LeaderPersonalityDef& personality =
        leaderPersonality(gsPlayer != nullptr ? gsPlayer->civId() : CivId{0});
    const LeaderBehavior& beh = personality.behavior;

    for (uint8_t other = 0; other < playerCount; ++other) {
        if (other == this->m_player) { continue; }

        PairwiseRelation& rel = diplomacy.relation(this->m_player, other);

        // Cannot interact with players we haven't met yet
        if (!rel.hasMet) { continue; }

        const int32_t theirMilitary = militaryCounts[static_cast<std::size_t>(other)];
        const int32_t relationScore = rel.totalScore();

        if (rel.isAtWar) {
            // Peace threshold: leaders with high peaceAcceptanceThreshold accept
            // peace readily; grudge-holding leaders fight on even when outmatched.
            const float peaceMilRatio = (ourMilitary > 0)
                ? static_cast<float>(theirMilitary) / static_cast<float>(ourMilitary)
                : 10.0f;
            // Gandhi (peace=0.2, grudge=0.2) sues for peace at ratio ~1.3
            // Montezuma (peace=0.8, grudge=0.9) fights until ratio 2.5+
            // Base bumped 1.0 -> 1.5 so wars last long enough for nuke-tech
            // civs to actually find at-war targets. Short wars also made
            // secession, attrition, and spy missions under-trigger.
            const float peaceThreshold = 1.5f + beh.grudgeHolding - beh.peaceAcceptanceThreshold;
            if (peaceMilRatio > std::max(peaceThreshold, 0.8f)) {
                // War reparations: the weaker side (proposing peace) pays 10% of
                // their treasury to the stronger side. This makes war economically
                // meaningful — winning wars pays for the military investment.
                aoc::game::Player* loser = gameState.player(this->m_player);
                aoc::game::Player* winner = gameState.player(other);
                if (loser != nullptr && winner != nullptr && loser->treasury() > 0) {
                    const CurrencyAmount reparations = std::max(
                        static_cast<CurrencyAmount>(1),
                        loser->treasury() / 10);
                    loser->addGold(-reparations);
                    winner->addGold(reparations);
                    LOG_INFO("AI %u paid %lld gold in war reparations to player %u",
                             static_cast<unsigned>(this->m_player),
                             static_cast<long long>(reparations),
                             static_cast<unsigned>(other));
                }
                diplomacy.makePeace(this->m_player, other);
                LOG_INFO("AI %u Proposed peace with player %u (ratio %.2f > threshold %.2f)",
                         static_cast<unsigned>(this->m_player),
                         static_cast<unsigned>(other),
                         static_cast<double>(peaceMilRatio),
                         static_cast<double>(peaceThreshold));
            }
        } else {
            const bool easyAI = (this->m_difficulty == aoc::ui::AIDifficulty::Easy);
            const bool hardAI = (this->m_difficulty == aoc::ui::AIDifficulty::Hard);

            // War declaration threshold: personality-driven.
            // Low warDeclarationThreshold (Montezuma=1.0) = easy to trigger war.
            // High warDeclarationThreshold (Gandhi=5.0) = almost never declares war.
            // militaryAggression lowers the required advantage, but always needs
            // at least 1.2:1 even for the most aggressive leaders.
            // Gandhi (0.2): needs 1.5/sqrt(0.2)=3.35:1 -- almost never.
            // Montezuma (1.7): needs 1.5/sqrt(1.7)=1.15:1 -- at slight advantage.
            // Frederick (1.5): needs 1.5/sqrt(1.5)=1.22:1.
            const float baseMilRatio = hardAI ? 1.15f : 1.3f;
            const float milRatioThreshold = std::max(1.1f,
                baseMilRatio / std::sqrt(std::max(beh.militaryAggression, 0.1f)));
            // Relation threshold: aggressive leaders tolerate worse relations less.
            // Gandhi needs relations below -100; Montezuma triggers at -30.
            const int32_t baseRelThreshold = hardAI ? -5 : -15;
            const int32_t relationThreshold = static_cast<int32_t>(
                static_cast<float>(baseRelThreshold)
                / std::sqrt(std::max(beh.militaryAggression, 0.1f)));
            // War chance per turn: tuned down so 1000-turn games don't see
            // ~140 declarations. Montezuma: 2 * 1.7 = 3.4 (~34%/elig turn);
            // Gandhi still ~0.
            const int32_t baseWarChance = hardAI ? 3 : 2;
            const int32_t warChanceThreshold = static_cast<int32_t>(
                static_cast<float>(baseWarChance) * beh.militaryAggression);

            // Peace cooldown: cannot re-declare war within 40 turns of a peace
            // treaty. Was 15 — too short for 1000-turn games (allowed up to
            // ~66 wars between same pair).
            constexpr int32_t WAR_COOLDOWN_TURNS = 40;
            if (rel.turnsSincePeace < WAR_COOLDOWN_TURNS) { continue; }

            // Periphery gate: refuse wars against civs beyond our projection
            // range. Low-peripheryTolerance leaders (isolationists) restrict
            // themselves to wars with near neighbours; high-tolerance
            // (colonial) leaders happily declare across oceans.
            //
            // Range scales 10..30 hexes by gene (baseline 20 at 1.0).
            {
                const int32_t maxWarRange = static_cast<int32_t>(std::clamp(
                    20.0f * beh.peripheryTolerance, 10.0f, 30.0f));
                const aoc::game::Player* ourPlayerPtr   = gameState.player(this->m_player);
                const aoc::game::Player* theirPlayerPtr = gameState.player(other);
                if (ourPlayerPtr == nullptr || theirPlayerPtr == nullptr) { continue; }
                if (ourPlayerPtr->cities().empty() || theirPlayerPtr->cities().empty()) {
                    continue;
                }
                int32_t closestCityDist = std::numeric_limits<int32_t>::max();
                for (const std::unique_ptr<aoc::game::City>& ourCity : ourPlayerPtr->cities()) {
                    for (const std::unique_ptr<aoc::game::City>& theirCity : theirPlayerPtr->cities()) {
                        const int32_t d = aoc::hex::distance(
                            ourCity->location(), theirCity->location());
                        if (d < closestCityDist) { closestCityDist = d; }
                    }
                }
                if (closestCityDist > maxWarRange) {
                    continue;  // Too far -- would overextend supply + yield nothing.
                }
            }

            // Standard war declaration: military advantage + strained relations.
            if (!easyAI && !rel.isAtWar && ourMilitary > 0 && theirMilitary >= 0 &&
                static_cast<float>(ourMilitary) >
                    milRatioThreshold * static_cast<float>(std::max(1, theirMilitary)) &&
                relationScore < relationThreshold) {
                const int32_t warChance =
                    ((ourMilitary * 7 + theirMilitary * 13 +
                      static_cast<int32_t>(this->m_player) * 31 +
                      gameState.currentTurn() * 53 +
                      static_cast<int32_t>(other) * 71) % 100);
                if (warChance < warChanceThreshold) {
                    diplomacy.declareWar(this->m_player, other,
                                         rel.casusBelliGranted()
                                             ? aoc::sim::CasusBelliType::FormalWar
                                             : aoc::sim::CasusBelliType::SurpriseWar,
                                         nullptr, &gameState,
                                         gameState.currentTurn());
                    LOG_INFO("AI %u Declared war on player %u (military %d vs %d, "
                             "relations %d, aggression %.2f)",
                             static_cast<unsigned>(this->m_player),
                             static_cast<unsigned>(other),
                             ourMilitary, theirMilitary, relationScore,
                             static_cast<double>(beh.militaryAggression));
                }
            }

            // Opportunistic war: personality modulates the threshold.
            // Aggressive leaders (aggression > 1.5) attack at ~1.5:1 ratio.
            // Peaceful leaders (aggression < 0.5) never attack opportunistically.
            if (beh.militaryAggression < 0.5f) {
                // Peaceful leaders skip opportunistic wars entirely.
            } else {
                const float oppoRatioThreshold = std::max(1.3f,
                    2.0f / std::sqrt(beh.militaryAggression));
                const int32_t oppoMinUnits = 3;
                if (!easyAI && !rel.isAtWar && ourMilitary >= oppoMinUnits &&
                    static_cast<float>(ourMilitary) >=
                        oppoRatioThreshold * static_cast<float>(std::max(1, theirMilitary))) {
                    const int32_t warChance =
                        ((ourMilitary * 11 + theirMilitary * 17 +
                          static_cast<int32_t>(this->m_player) * 37 +
                          gameState.currentTurn() * 59 +
                          static_cast<int32_t>(other) * 73) % 100);
                    const int32_t threshold = hardAI ? 3 : 2;
                    if (warChance < threshold) {
                        diplomacy.declareWar(this->m_player, other,
                                             aoc::sim::CasusBelliType::SurpriseWar,
                                             nullptr, &gameState,
                                             gameState.currentTurn());
                        LOG_INFO("AI %u Declared opportunistic war on player %u "
                                 "(%.1f:1 advantage: %d vs %d, aggression %.2f)",
                                 static_cast<unsigned>(this->m_player),
                                 static_cast<unsigned>(other),
                                 static_cast<double>(
                                     static_cast<float>(ourMilitary) /
                                     static_cast<float>(std::max(1, theirMilitary))),
                                 ourMilitary, theirMilitary,
                                 static_cast<double>(beh.militaryAggression));
                    }
                }
            }

            if (!rel.hasOpenBorders && relationScore > 10) {
                diplomacy.grantOpenBorders(this->m_player, other);
                LOG_INFO("AI %u Opened borders with player %u (relations %d)",
                         static_cast<unsigned>(this->m_player),
                         static_cast<unsigned>(other), relationScore);
            }

            // H6.4: alliance formation is now personality-gated. Without this,
            // every AI with relations > 20 plus 3 complementary goods formed an
            // economic alliance on turn 1, flattening behavioral distinction.
            // Common gate: alliance desire / diplomatic openness above 0.5.
            // Per-type gate: matching focus > 0.5 so a warmonger doesn't chase
            // a cultural alliance and a zealot doesn't chase a research one.
            const bool openToAlliance =
                beh.allianceDesire > 0.5f && beh.diplomaticOpenness > 0.5f;

            if (openToAlliance && !rel.hasEconomicAlliance && relationScore > 20
                && beh.economicFocus > 0.5f) {
                int32_t complementaryGoods = 0;
                const uint16_t totalGoods = market.goodsCount();
                for (uint16_t g = 0; g < totalGoods; ++g) {
                    const int32_t currentPrice = market.price(g);
                    const int32_t basePrice = goodDef(g).basePrice;
                    if (basePrice > 0) {
                        const float priceRatio = static_cast<float>(currentPrice) /
                                                 static_cast<float>(basePrice);
                        if (priceRatio > 1.3f || priceRatio < 0.7f) {
                            ++complementaryGoods;
                        }
                    }
                }
                if (complementaryGoods >= 3) {
                    const aoc::ErrorCode ec = diplomacy.formEconomicAlliance(
                        this->m_player, other, gameState.currentTurn());
                    if (ec == aoc::ErrorCode::Ok) {
                        LOG_INFO("AI %u Formed economic alliance with player %u "
                                 "(relations %d, %d complementary goods)",
                                 static_cast<unsigned>(this->m_player),
                                 static_cast<unsigned>(other),
                                 relationScore, complementaryGoods);
                    }
                }
            }

            // H6.4: research agreement — science-focused leaders at warm relations.
            if (openToAlliance && !rel.hasResearchAgreement && relationScore > 25
                && beh.scienceFocus > 0.8f) {
                const aoc::ErrorCode ec = diplomacy.formResearchAgreement(
                    this->m_player, other, gameState.currentTurn());
                if (ec == aoc::ErrorCode::Ok) {
                    LOG_INFO("AI %u Formed research agreement with player %u "
                             "(relations %d, scienceFocus %.2f)",
                             static_cast<unsigned>(this->m_player),
                             static_cast<unsigned>(other),
                             relationScore,
                             static_cast<double>(beh.scienceFocus));
                }
            }

            // H6.4: military alliance — requires aggressive or defensive profile
            // AND strong trust. Warmongers seek allies; peaceniks don't.
            if (openToAlliance && !rel.hasMilitaryAlliance && relationScore > 35
                && beh.militaryAggression > 0.8f) {
                const aoc::ErrorCode ec = diplomacy.formMilitaryAlliance(
                    this->m_player, other, gameState.currentTurn());
                if (ec == aoc::ErrorCode::Ok) {
                    LOG_INFO("AI %u Formed military alliance with player %u "
                             "(relations %d, aggression %.2f)",
                             static_cast<unsigned>(this->m_player),
                             static_cast<unsigned>(other),
                             relationScore,
                             static_cast<double>(beh.militaryAggression));
                }
            }

            // Defensive alliance — low-aggression / diplomatic profiles that
            // want the war-deterrent of mutual defense without the force
            // projection of a full military alliance. A lower aggression
            // gate (< 0.4) complements the militaryAggression > 0.8 path
            // above, so warmongers and peaceniks pick different alliance
            // types instead of competing for the same slot.
            if (openToAlliance && !rel.hasDefensiveAlliance
                && !rel.hasMilitaryAlliance && relationScore > 30
                && beh.militaryAggression < 0.4f
                && beh.diplomaticOpenness > 0.7f) {
                const aoc::ErrorCode ec = diplomacy.formDefensiveAlliance(
                    this->m_player, other, gameState.currentTurn());
                if (ec == aoc::ErrorCode::Ok) {
                    LOG_INFO("AI %u Formed defensive alliance with player %u "
                             "(relations %d, aggression %.2f, openness %.2f)",
                             static_cast<unsigned>(this->m_player),
                             static_cast<unsigned>(other),
                             relationScore,
                             static_cast<double>(beh.militaryAggression),
                             static_cast<double>(beh.diplomaticOpenness));
                }
            }

            // H6.4: cultural alliance — culture-focused leaders.
            if (openToAlliance && !rel.hasCulturalAlliance && relationScore > 25
                && beh.cultureFocus > 0.8f) {
                const aoc::ErrorCode ec = diplomacy.formCulturalAlliance(
                    this->m_player, other, gameState.currentTurn());
                if (ec == aoc::ErrorCode::Ok) {
                    LOG_INFO("AI %u Formed cultural alliance with player %u "
                             "(relations %d, cultureFocus %.2f)",
                             static_cast<unsigned>(this->m_player),
                             static_cast<unsigned>(other),
                             relationScore,
                             static_cast<double>(beh.cultureFocus));
                }
            }

            // H6.4: religious alliance — religious-zealot leaders only.
            if (openToAlliance && !rel.hasReligiousAlliance && relationScore > 25
                && beh.religiousZeal > 0.8f) {
                const aoc::ErrorCode ec = diplomacy.formReligiousAlliance(
                    this->m_player, other, gameState.currentTurn());
                if (ec == aoc::ErrorCode::Ok) {
                    LOG_INFO("AI %u Formed religious alliance with player %u "
                             "(relations %d, religiousZeal %.2f)",
                             static_cast<unsigned>(this->m_player),
                             static_cast<unsigned>(other),
                             relationScore,
                             static_cast<double>(beh.religiousZeal));
                }
            }

            // ----------------------------------------------------------------
            // Bilateral trade deal proposal (AI-only path).
            // ----------------------------------------------------------------
            // Lighter commitment than an alliance: -20% tariffs and an auto
            // spawned Trader every 5 turns along the pair. Fire when relations
            // are warm and we do not already share one. Uses the same partner
            // loop, so every AI reconsiders each turn.
            if (relationScore > 15) {
                aoc::game::Player* selfPlayer = gameState.player(this->m_player);
                bool alreadyPaired = false;
                if (selfPlayer != nullptr) {
                    for (const aoc::sim::TradeAgreementDef& agr :
                         selfPlayer->tradeAgreements().agreements) {
                        if (!agr.isActive) { continue; }
                        if (agr.type != aoc::sim::TradeAgreementType::BilateralDeal) { continue; }
                        for (PlayerId m : agr.members) {
                            if (m == other) { alreadyPaired = true; break; }
                        }
                        if (alreadyPaired) { break; }
                    }
                }
                if (!alreadyPaired) {
                    const ErrorCode rc = aoc::sim::proposeBilateralDeal(
                        gameState, this->m_player, other);
                    if (rc == ErrorCode::Ok) {
                        LOG_INFO("AI %u proposed bilateral trade deal with player %u "
                                 "(relations %d)",
                                 static_cast<unsigned>(this->m_player),
                                 static_cast<unsigned>(other), relationScore);
                    }
                }
            }

            // ----------------------------------------------------------------
            // Loan offers / requests (IOUContract path).
            // ----------------------------------------------------------------
            // Flush AIs lend to friendly partners who are short on cash. The
            // processIOUPayments turn tick amortises the loan with interest,
            // which is Civ 6's "gold per turn" mechanic.
            {
                aoc::game::Player* selfPlayer = gameState.player(this->m_player);
                aoc::game::Player* partnerPlayer = gameState.player(other);
                if (selfPlayer != nullptr && partnerPlayer != nullptr
                    && relationScore > 10) {
                    const CurrencyAmount myTreas = selfPlayer->treasury();
                    const CurrencyAmount theirTreas = partnerPlayer->treasury();
                    // Do not stack too many loans with the same partner.
                    int32_t existingWithPartner = 0;
                    for (const aoc::sim::IOUContract& c : selfPlayer->ious().loansGiven) {
                        if (c.debtor == other && c.remaining > 0) { ++existingWithPartner; }
                    }
                    // Treasury scale in-sim is ~0-2000 most of the game.
                    // Flush: at least 4x the partner's shortfall and > 300 floor.
                    const bool flush = myTreas > 300 && myTreas > theirTreas * 4;
                    const bool partnerBroke = theirTreas < 50;
                    if (flush && partnerBroke && existingWithPartner < 2) {
                        // Lend up to 25% of our treasury, capped at 500.
                        // 8% per-turn interest, 15-turn term.
                        const CurrencyAmount principal = std::min(
                            myTreas / 4,
                            static_cast<CurrencyAmount>(500));
                        if (principal > 50) {
                            ErrorCode rc = aoc::sim::createIOU(
                                gameState, this->m_player, other,
                                principal, 0.08f, 15);
                            if (rc == ErrorCode::Ok) {
                                LOG_INFO("AI %u offered loan to player %u: %d gold @ 8%% for 15 turns (relation %d)",
                                         static_cast<unsigned>(this->m_player),
                                         static_cast<unsigned>(other),
                                         static_cast<int>(principal),
                                         relationScore);
                            }
                        }
                    }
                }
            }

            // ----------------------------------------------------------------
            // City sale (CedeCity for GoldLump).
            // ----------------------------------------------------------------
            // A civ deep in debt with a spare city proposes: "Take this city,
            // here's the transfer; pay me X gold." Fires every ~30 turns per
            // pair. Requires warm relations and a willing buyer.
            if (dealTracker != nullptr && relationScore > 15) {
                const int32_t curTurn = gameState.currentTurn();
                const int32_t saleTick = curTurn + this->m_player * 7 + other * 11;
                if (saleTick % 25 == 0) {
                    aoc::game::Player* seller = gameState.player(this->m_player);
                    aoc::game::Player* buyer  = gameState.player(other);
                    // Empire trimming: 5+ cities OR broke with 3+ cities.
                    const std::size_t numCities = seller != nullptr ? seller->cities().size() : 0;
                    const CurrencyAmount sellerGold = seller != nullptr ? seller->treasury() : 0;
                    const bool surplus = numCities >= 5;
                    const bool broke   = numCities >= 3 && sellerGold < 100;
                    if (seller != nullptr && buyer != nullptr && (surplus || broke)) {
                        // Pick the smallest city (cheapest to part with).
                        aoc::game::City* victim = nullptr;
                        int32_t smallestPop = INT32_MAX;
                        for (const std::unique_ptr<aoc::game::City>& c : seller->cities()) {
                            if (c->population() < smallestPop) {
                                smallestPop = c->population();
                                victim = c.get();
                            }
                        }
                        const int32_t price = 200 + smallestPop * 50;
                        if (victim != nullptr && buyer->treasury() > price + 100) {
                            const aoc::hex::AxialCoord loc = victim->location();

                            DiplomaticDeal deal{};
                            deal.playerA = this->m_player;
                            deal.playerB = other;
                            deal.turnsRemaining = 0;

                            DealTerm cede{};
                            cede.type = DealTermType::CedeCity;
                            cede.fromPlayer = this->m_player;
                            cede.toPlayer   = other;
                            cede.tileCoord  = loc;
                            deal.terms.push_back(cede);

                            DealTerm payment{};
                            payment.type = DealTermType::GoldLump;
                            payment.fromPlayer = other;
                            payment.toPlayer   = this->m_player;
                            payment.goldLump   = price;
                            deal.terms.push_back(payment);

                            const std::size_t dealIdx = dealTracker->activeDeals.size();
                            ErrorCode rcP = aoc::sim::proposeDeal(gameState, *dealTracker, deal);
                            if (rcP == ErrorCode::Ok) {
                                ErrorCode rcA = aoc::sim::acceptDeal(
                                    gameState, grid, *dealTracker,
                                    static_cast<int32_t>(dealIdx));
                                if (rcA == ErrorCode::Ok) {
                                    LOG_INFO("AI %u sold city %s to player %u for %d gold (relation %d)",
                                             static_cast<unsigned>(this->m_player),
                                             victim->name().c_str(),
                                             static_cast<unsigned>(other),
                                             price, relationScore);
                                }
                            }
                        }
                    }
                }
            }

            // ----------------------------------------------------------------
            // Border tile sale (CedeTile for GoldLump).
            // ----------------------------------------------------------------
            // Transfer one of our hexes that is adjacent to a neighbour's
            // territory. Small price, ~every 20 turns per pair.
            if (dealTracker != nullptr && relationScore > 15) {
                const int32_t curTurn = gameState.currentTurn();
                const int32_t tileTick = curTurn + this->m_player * 3 + other * 5;
                if (tileTick % 60 == 0) {
                    aoc::game::Player* seller = gameState.player(this->m_player);
                    aoc::game::Player* buyer  = gameState.player(other);
                    // Only sell if seller is short on gold: avoids border thrash
                    // where both sides keep re-buying from each other.
                    if (seller != nullptr && buyer != nullptr
                        && seller->treasury() < 300
                        && buyer->treasury() > 150) {
                        // Scan grid for a self-owned tile adjacent to `other`.
                        const int32_t tileCount = grid.tileCount();
                        aoc::hex::AxialCoord foundTile{0, 0};
                        bool have = false;
                        for (int32_t idx = 0; idx < tileCount && !have; ++idx) {
                            if (grid.owner(idx) != this->m_player) { continue; }
                            const aoc::hex::AxialCoord c = grid.toAxial(idx);
                            const std::array<aoc::hex::AxialCoord, 6> nbrs = aoc::hex::neighbors(c);
                            for (const aoc::hex::AxialCoord& n : nbrs) {
                                const int32_t nIdx = grid.toIndex(n);
                                if (nIdx < 0 || nIdx >= tileCount) { continue; }
                                if (grid.owner(nIdx) == other) {
                                    foundTile = c;
                                    have = true;
                                    break;
                                }
                            }
                        }
                        if (have) {
                            DiplomaticDeal deal{};
                            deal.playerA = this->m_player;
                            deal.playerB = other;
                            deal.turnsRemaining = 0;

                            DealTerm cede{};
                            cede.type = DealTermType::CedeTile;
                            cede.fromPlayer = this->m_player;
                            cede.toPlayer   = other;
                            cede.tileCoord  = foundTile;
                            deal.terms.push_back(cede);

                            DealTerm payment{};
                            payment.type = DealTermType::GoldLump;
                            payment.fromPlayer = other;
                            payment.toPlayer   = this->m_player;
                            payment.goldLump   = 100;
                            deal.terms.push_back(payment);

                            const std::size_t dealIdx = dealTracker->activeDeals.size();
                            ErrorCode rcP = aoc::sim::proposeDeal(gameState, *dealTracker, deal);
                            if (rcP == ErrorCode::Ok) {
                                ErrorCode rcA = aoc::sim::acceptDeal(
                                    gameState, grid, *dealTracker,
                                    static_cast<int32_t>(dealIdx));
                                if (rcA == ErrorCode::Ok) {
                                    LOG_INFO("AI %u ceded tile (%d,%d) to player %u for 100 gold (relation %d)",
                                             static_cast<unsigned>(this->m_player),
                                             foundTile.q, foundTile.r,
                                             static_cast<unsigned>(other),
                                             relationScore);
                                }
                            }
                        }
                    }
                }
            }

            // ----------------------------------------------------------------
            // Border violation response (personality-driven)
            // ----------------------------------------------------------------
            // Check if 'other' has units in OUR territory (we are territory owner).
            const PairwiseRelation& violatorRel = diplomacy.relation(other, this->m_player);
            if (violatorRel.unitsInTerritory > 0 && violatorRel.turnsWithViolation > 0) {
                // Tolerance thresholds vary by aggression:
                //   Aggressive (>1.2): 0-2 turns tolerance -> ultimatum -> war
                //   Defensive (0.6-1.2): 3-5 turns -> demand withdrawal, fortify
                //   Diplomatic (0.3-0.6): 5-10 turns -> protest, seek allies
                //   Passive (<0.3): 10+ turns -> complain but tolerate
                int32_t baseTolerance = 5;
                if (beh.militaryAggression > 1.2f) {
                    baseTolerance = 2;
                } else if (beh.militaryAggression > 0.6f) {
                    baseTolerance = 4;
                } else if (beh.militaryAggression > 0.3f) {
                    baseTolerance = 8;
                } else {
                    baseTolerance = 12;
                }

                // Power ratio modulates tolerance: if violator is 2x+ stronger,
                // double tolerance. Small nations endure what they must.
                const float powerRatio = (ourMilitary > 0)
                    ? static_cast<float>(theirMilitary) / static_cast<float>(ourMilitary)
                    : 10.0f;
                if (powerRatio > 2.0f) {
                    baseTolerance *= 2;
                }

                const int32_t violationTurns = violatorRel.turnsWithViolation;

                if (violationTurns > baseTolerance && violatorRel.casusBelliGranted()) {
                    // Beyond tolerance and casus belli granted: declare war
                    // (if not already at war and we have military capability)
                    if (ourMilitary > 0 && beh.militaryAggression > 0.3f) {
                        diplomacy.declareWar(this->m_player, other,
                                             aoc::sim::CasusBelliType::FormalWar,
                                             nullptr, &gameState,
                                             gameState.currentTurn());
                        LOG_INFO("AI %u Declared war on Player %u for border violation "
                                 "(%d turns, tolerance %d, aggression %.2f)",
                                 static_cast<unsigned>(this->m_player),
                                 static_cast<unsigned>(other),
                                 violationTurns, baseTolerance,
                                 static_cast<double>(beh.militaryAggression));
                    }
                } else if (violationTurns > baseTolerance / 2) {
                    // Past half-tolerance: add relation penalty
                    diplomacy.addModifier(this->m_player, other,
                        {"Troops in our territory", -5, 10});
                }

                // Set higher toll rates against violators
                aoc::game::Player* ourPlayer = gameState.player(this->m_player);
                if (ourPlayer != nullptr) {
                    float violatorToll = 0.25f + beh.militaryAggression * 0.10f;
                    violatorToll = std::min(violatorToll, 0.50f);
                    ourPlayer->tariffs().perPlayerTollRates[other] = violatorToll;
                }
            }

            // ----------------------------------------------------------------
            // Hostile economic actions: resource embargo + bond dump.
            // ----------------------------------------------------------------
            // Fire when war is declared OR relations are deeply negative.
            // These are one-shots (per turn per pair), not continuous, and
            // gated on the reputation/aggression personality to keep
            // peaceful AIs from torching economic ties.
            if ((rel.isAtWar || relationScore < -40) && beh.militaryAggression > 0.5f) {
                // Resource embargo: blanket the largest export flow to
                // this rival. MVP picks the first good we produce that
                // they are price-dependent on (market price > 1.2x base).
                aoc::game::Player* selfP = gameState.player(this->m_player);
                if (selfP != nullptr) {
                    const uint16_t totalGoods = market.goodsCount();
                    uint16_t targetGood = 0xFFFF;
                    for (uint16_t g = 0; g < totalGoods; ++g) {
                        if (diplomacy.hasResourceEmbargo(this->m_player, other, g)) { continue; }
                        // We-hold check: at least one of our cities has
                        // a non-trivial stockpile of this good (proxy
                        // for "we could deny it to them").
                        bool weHold = false;
                        for (const std::unique_ptr<aoc::game::City>& c : selfP->cities()) {
                            if (c != nullptr && c->stockpile().getAmount(g) >= 10) {
                                weHold = true; break;
                            }
                        }
                        if (!weHold) { continue; }
                        const int32_t currentPrice = market.price(g);
                        const int32_t basePrice    = goodDef(g).basePrice;
                        if (basePrice <= 0) { continue; }
                        const float ratio =
                            static_cast<float>(currentPrice) / static_cast<float>(basePrice);
                        if (ratio > 1.2f) {
                            targetGood = g;
                            break;
                        }
                    }
                    if (targetGood != 0xFFFF) {
                        diplomacy.setResourceEmbargo(this->m_player, other, targetGood, true);
                        LOG_INFO("AI %u imposed resource embargo on player %u (good %u, relation %d)",
                                 static_cast<unsigned>(this->m_player),
                                 static_cast<unsigned>(other),
                                 static_cast<unsigned>(targetGood), relationScore);
                    }
                }

                // Bond dump: if we hold bonds issued by the rival, sell
                // them all to spike their yields. Fires on the turn war
                // is declared / relations cross the threshold; the
                // dumpBonds implementation itself handles single-shot
                // semantics when no bonds are held.
                if (selfP != nullptr) {
                    CurrencyAmount held = selfP->bonds().bondsHeldFrom(other);
                    if (held > 0) {
                        ErrorCode bc = aoc::sim::dumpBonds(gameState, this->m_player, other);
                        if (bc == ErrorCode::Ok) {
                            LOG_INFO("AI %u dumped %lld bond debt of player %u (relation %d)",
                                     static_cast<unsigned>(this->m_player),
                                     static_cast<long long>(held),
                                     static_cast<unsigned>(other), relationScore);
                        }
                    }
                }
            }

            // ----------------------------------------------------------------
            // AI toll rate management (based on relation + reputation)
            // ----------------------------------------------------------------
            {
                aoc::game::Player* ourPlayer = gameState.player(this->m_player);
                if (ourPlayer != nullptr && violatorRel.unitsInTerritory == 0) {
                    const int32_t repScore = rel.reputationScore();
                    float tollRate = 0.10f;  // Neutral default
                    if (relationScore > 10) {
                        tollRate = 0.05f;    // Friendly
                    }
                    if (relationScore > 40 || rel.hasDefensiveAlliance) {
                        tollRate = 0.0f;     // Allied
                    }
                    if (relationScore < -10) {
                        tollRate = 0.20f;    // Unfriendly
                    }
                    if (relationScore < -40) {
                        tollRate = 0.35f + beh.militaryAggression * 0.05f;
                        tollRate = std::min(tollRate, 0.50f);  // Hostile
                    }
                    // Reputation modulates: untrustworthy players pay more
                    if (repScore < -20) {
                        tollRate += 0.05f;
                        tollRate = std::min(tollRate, 0.50f);
                    }
                    // C25: reciprocal tariff — mirror partner's rate if they
                    // chose a higher one. Without this, trade wars are
                    // one-sided: AI A hikes tolls, AI B keeps being nice.
                    // Add a small premium above their rate so retaliation is
                    // visible and converges to escalation signal.
                    const aoc::game::Player* theirPlayer = gameState.player(other);
                    if (theirPlayer != nullptr) {
                        std::unordered_map<PlayerId, float>::const_iterator mirrorIt =
                            theirPlayer->tariffs().perPlayerTollRates.find(this->m_player);
                        if (mirrorIt != theirPlayer->tariffs().perPlayerTollRates.end()
                            && mirrorIt->second > tollRate + 0.05f) {
                            tollRate = std::min(0.50f, mirrorIt->second + 0.02f);
                        }
                    }
                    ourPlayer->tariffs().perPlayerTollRates[other] = tollRate;

                    // Canal toll: premium pricing for canal transit.
                    // Economically-focused AIs charge more (canals are investments),
                    // allies get discounts, hostiles pay maximum.
                    float canalToll = 0.20f + beh.economicFocus * 0.05f;
                    if (relationScore > 10) {
                        canalToll = 0.10f;   // Friendly discount
                    }
                    if (relationScore > 40 || rel.hasDefensiveAlliance) {
                        canalToll = 0.05f;   // Allied: near-free access
                    }
                    if (relationScore < -10) {
                        canalToll = 0.30f + beh.economicFocus * 0.05f;
                    }
                    if (relationScore < -40) {
                        canalToll = 0.45f;   // Hostile: near-maximum
                    }
                    canalToll = std::min(canalToll, 0.50f);
                    ourPlayer->tariffs().perPlayerCanalTollRates[other] = canalToll;
                }
            }
        }
    }

    // ------------------------------------------------------------------
    // Free-trade-zone / customs-union formation.
    // ------------------------------------------------------------------
    // Warm, open-trading AIs gather 2+ economic partners into a free
    // trade zone (-50% tariff) or customs union (common external
    // tariff). Gate on economicFocus and diplomaticOpenness; rate-limit
    // to one attempt every ~50 turns per AI.
    {
        const aoc::game::Player* me = gameState.player(this->m_player);
        const bool openTrader =
            beh.economicFocus > 0.6f && beh.diplomaticOpenness > 0.5f;
        const int32_t ftzTick =
            gameState.currentTurn() + static_cast<int32_t>(this->m_player) * 11;
        if (me != nullptr && openTrader && (ftzTick % 50 == 0)) {
            std::vector<PlayerId> members;
            members.push_back(this->m_player);
            for (uint8_t other = 0; other < playerCount; ++other) {
                if (other == this->m_player) { continue; }
                const aoc::game::Player* o = gameState.player(other);
                if (o == nullptr) { continue; }
                if (o->victoryTracker().isEliminated) { continue; }
                const PairwiseRelation& r = diplomacy.relation(this->m_player, other);
                if (!r.hasMet || r.isAtWar) { continue; }
                if (r.totalScore() < 15) { continue; }
                // Skip if already in any FTZ/customs with us.
                bool alreadyInBloc = false;
                for (const aoc::sim::TradeAgreementDef& agr : me->tradeAgreements().agreements) {
                    if (!agr.isActive) { continue; }
                    if (agr.type == aoc::sim::TradeAgreementType::BilateralDeal) { continue; }
                    for (PlayerId mem : agr.members) {
                        if (mem == other) { alreadyInBloc = true; break; }
                    }
                    if (alreadyInBloc) { break; }
                }
                if (alreadyInBloc) { continue; }
                members.push_back(other);
                if (members.size() >= 4) { break; }  // Keep blocs tractable.
            }
            if (members.size() >= 3) {
                ErrorCode ec = ErrorCode::InvalidArgument;
                const char* kind = "FTZ";
                // Protectionist + high-aggression personalities prefer a
                // customs union (projects power via common tariff).
                // Everyone else forms a softer FTZ.
                if (beh.militaryAggression > 0.8f) {
                    ec = aoc::sim::formCustomsUnion(gameState, members, 0.15f);
                    kind = "Customs Union";
                } else {
                    ec = aoc::sim::createFreeTradeZone(gameState, members);
                }
                if (ec == ErrorCode::Ok) {
                    LOG_INFO("AI %u formed %s with %zu members (economicFocus %.2f)",
                             static_cast<unsigned>(this->m_player), kind,
                             members.size(),
                             static_cast<double>(beh.economicFocus));
                }
            }
        }
    }

    // ------------------------------------------------------------------
    // Electricity import proposals.
    // ------------------------------------------------------------------
    // If our total district power demand exceeds domestic supply, try to
    // buy the deficit from a neighbour whose supply comfortably exceeds
    // their own demand. Per-city 40% import cap in computeCityPower
    // keeps this a complement, not a crutch. Firing limited to once per
    // ~25 turns per AI to avoid hammering the agreement list.
    {
        const aoc::game::Player* me = gameState.player(this->m_player);
        const int32_t elecTick =
            gameState.currentTurn() + static_cast<int32_t>(this->m_player) * 3;
        const bool industrial = (me != nullptr && aoc::sim::effectiveEraFromTech(*me).value >= 4);
        if (industrial && (elecTick % 25 == 0)) {
            // Crude per-player energy balance — supply = sum of
            // power-plant building outputs regardless of fuel gating (the
            // tick on processElectricityAgreements uses lastDelivered so
            // this over-estimates at worst, which is fine for gating).
            auto energyBalance = [&](const aoc::game::Player* p) -> std::pair<int32_t,int32_t> {
                int32_t sup = 0;
                int32_t dem = 0;
                if (p == nullptr) { return {0, 0}; }
                for (const std::unique_ptr<aoc::game::City>& c : p->cities()) {
                    if (c == nullptr) { continue; }
                    for (const CityDistrictsComponent::PlacedDistrict& d
                             : c->districts().districts) {
                        for (BuildingId bid : d.buildings) {
                            dem += buildingEnergyDemand(bid);
                            for (const PowerPlantDef& pd : POWER_PLANT_DEFS) {
                                if (pd.buildingId == bid) {
                                    sup += pd.energyOutput;
                                    break;
                                }
                            }
                        }
                    }
                }
                return {sup, dem};
            };

            auto [mySup, myDem] = energyBalance(me);
            const int32_t myDeficit = myDem - mySup;
            if (myDeficit > 0) {
                // Find best seller candidate: largest positive surplus,
                // met, not at war, no existing agreement in this direction.
                PlayerId bestSeller = INVALID_PLAYER;
                int32_t  bestSurplus = 0;
                for (uint8_t other = 0; other < playerCount; ++other) {
                    if (other == this->m_player) { continue; }
                    const aoc::game::Player* o = gameState.player(other);
                    if (o == nullptr) { continue; }
                    if (o->victoryTracker().isEliminated) { continue; }
                    const PairwiseRelation& r = diplomacy.relation(this->m_player, other);
                    if (!r.hasMet || r.isAtWar) { continue; }
                    if (r.totalScore() < 0) { continue; }  // Hostile sellers refuse

                    bool duplicateDir = false;
                    for (const aoc::sim::ElectricityAgreementComponent& a
                             : gameState.electricityAgreements()) {
                        if (a.isActive && a.buyer == this->m_player && a.seller == other) {
                            duplicateDir = true; break;
                        }
                    }
                    if (duplicateDir) { continue; }

                    auto [oSup, oDem] = energyBalance(o);
                    const int32_t surplus = oSup - oDem;
                    if (surplus > bestSurplus) {
                        bestSurplus = surplus;
                        bestSeller  = other;
                    }
                }

                if (bestSeller != INVALID_PLAYER && bestSurplus > 0) {
                    // Cover the deficit but no more than the seller's
                    // surplus. Gold: 2 gold per MW per turn — MVP price
                    // anchor; market tuning can come later.
                    const int32_t mw = std::min(myDeficit, bestSurplus);
                    const int32_t goldPerTurn = std::max(1, mw * 2);
                    const int32_t duration    = 30;
                    const aoc::ErrorCode ec = aoc::sim::proposeElectricityImport(
                        gameState, this->m_player, bestSeller,
                        mw, goldPerTurn, gameState.currentTurn(), duration);
                    if (ec == aoc::ErrorCode::Ok) {
                        LOG_INFO("AI %u bought %d MW electricity from player %u "
                                 "(deficit %d, surplus %d, %d gold/turn, %d turns)",
                                 static_cast<unsigned>(this->m_player),
                                 mw, static_cast<unsigned>(bestSeller),
                                 myDeficit, bestSurplus, goldPerTurn, duration);
                    }
                }
            }
        }
    }

    // City-state interactions: bully when desperate for gold + aggressive,
    // levy when suzerain and at war. Rate-limited by CS cooldowns.
    {
        const aoc::game::Player* me = gameState.player(this->m_player);
        if (me != nullptr) {
            const CurrencyAmount treasury = me->treasury();
            bool atWar = false;
            for (uint8_t other = 0; other < playerCount; ++other) {
                if (other == this->m_player) { continue; }
                if (diplomacy.relation(this->m_player, other).isAtWar) {
                    atWar = true; break;
                }
            }
            auto& cityStates = gameState.cityStates();
            for (std::size_t i = 0; i < cityStates.size(); ++i) {
                CityStateComponent& cs = cityStates[i];
                if (!cs.hasMet(this->m_player)) { continue; }

                // Bully: only if we have no envoys stake and low treasury.
                const bool weAreSuzerain = (cs.suzerain == this->m_player);
                const bool hasOtherSuzerain =
                    (cs.suzerain != INVALID_PLAYER && !weAreSuzerain);
                const bool lowTreasury = (treasury < 100);
                const bool aggressive  = (beh.militaryAggression > 1.2f);
                if (!weAreSuzerain && !hasOtherSuzerain &&
                    lowTreasury && aggressive) {
                    (void)bullyCityState(gameState, this->m_player, i);
                }

                // Levy: suzerain at war with full treasury.
                if (weAreSuzerain && atWar && treasury > 300 &&
                    cs.levyPlayer == INVALID_PLAYER) {
                    (void)levyCityStateMilitary(gameState, this->m_player, i);
                }
            }
        }
    }
}
} // namespace aoc::sim::ai
