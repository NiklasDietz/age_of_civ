/**
 * @file DiplomacyScreen.cpp
 * @brief Diplomacy screen implementation.
 */

#include "aoc/ui/DiplomacyScreen.hpp"
#include "aoc/ui/UIManager.hpp"
#include "aoc/game/GameState.hpp"
#include "aoc/game/Player.hpp"
#include "aoc/game/City.hpp"
#include "aoc/map/HexGrid.hpp"
#include "aoc/map/HexCoord.hpp"
#include "aoc/simulation/civilization/Civilization.hpp"
#include "aoc/simulation/diplomacy/DiplomacyState.hpp"
#include "aoc/simulation/diplomacy/DealTerms.hpp"
#include "aoc/simulation/economy/TradeAgreement.hpp"
#include "aoc/simulation/monetary/Bonds.hpp"
#include "aoc/core/Log.hpp"

#include <array>
#include <climits>
#include <memory>
#include <string>

namespace aoc::ui {

void DiplomacyScreen::setContext(aoc::game::GameState* gameState, PlayerId humanPlayer,
                                  aoc::sim::DiplomacyManager* diplomacy,
                                  aoc::map::HexGrid* grid,
                                  aoc::sim::GlobalDealTracker* dealTracker) {
    this->m_gameState   = gameState;
    this->m_player      = humanPlayer;
    this->m_diplomacy   = diplomacy;
    this->m_grid        = grid;
    this->m_dealTracker = dealTracker;
}

void DiplomacyScreen::open(UIManager& ui) {
    if (this->m_isOpen) {
        return;
    }

    this->m_isOpen = true;

    WidgetId innerPanel = this->createScreenFrame(
        ui, "Diplomacy", 550.0f, 500.0f, this->m_screenW, this->m_screenH);

    // Player list
    this->m_playerList = ui.createScrollList(
        innerPanel, {0.0f, 0.0f, 520.0f, 400.0f});

    Widget* listWidget = ui.getWidget(this->m_playerList);
    if (listWidget != nullptr) {
        listWidget->padding = {4.0f, 4.0f, 4.0f, 4.0f};
        listWidget->childSpacing = 6.0f;
    }

    if (this->m_gameState == nullptr || this->m_diplomacy == nullptr) {
        ui.layout();
        return;
    }

    // Iterate over known players
    for (const std::unique_ptr<aoc::game::Player>& playerPtr : this->m_gameState->players()) {
        const PlayerId otherId = playerPtr->id();
        if (otherId == this->m_player || otherId == BARBARIAN_PLAYER) {
            continue;
        }

        const aoc::sim::CivilizationDef& civDefRef = aoc::sim::civDef(playerPtr->civId());
        const aoc::sim::PairwiseRelation& rel = this->m_diplomacy->relation(this->m_player, otherId);
        const int32_t score = rel.totalScore();
        const aoc::sim::DiplomaticStance stance = rel.stance();

        // Player info panel
        WidgetId playerPanel = ui.createPanel(
            this->m_playerList, {0.0f, 0.0f, 510.0f, 110.0f},
            PanelData{{0.12f, 0.12f, 0.16f, 0.9f}, 4.0f});
        Widget* ppWidget = ui.getWidget(playerPanel);
        if (ppWidget != nullptr) {
            ppWidget->padding = {6.0f, 8.0f, 6.0f, 8.0f};
            ppWidget->childSpacing = 4.0f;
        }

        // Civ name and leader name
        std::string nameText = std::string(civDefRef.name) + " - " + std::string(civDefRef.leaderName);
        (void)ui.createLabel(playerPanel, {0.0f, 0.0f, 490.0f, 16.0f},
                              LabelData{std::move(nameText), {1.0f, 0.9f, 0.5f, 1.0f}, 13.0f});

        // Leader ability
        std::string abilityText = "Ability: " + std::string(civDefRef.abilityName);
        (void)ui.createLabel(playerPanel, {0.0f, 0.0f, 490.0f, 12.0f},
                              LabelData{std::move(abilityText), {0.7f, 0.8f, 0.9f, 1.0f}, 10.0f});

        // Relation and stance info
        std::string relationText = "Score: " + std::to_string(score)
                                 + "  Stance: " + std::string(aoc::sim::stanceName(stance));
        if (rel.isAtWar) {
            relationText += "  [AT WAR]";
        }
        if (rel.hasOpenBorders) {
            relationText += "  [Open Borders]";
        }
        if (rel.hasDefensiveAlliance) {
            relationText += "  [Alliance]";
        }
        if (rel.hasMilitaryAlliance) {
            relationText += "  [Military Alliance]";
        }
        if (rel.hasResearchAgreement) {
            relationText += "  [Research Agreement]";
        }
        if (rel.hasEconomicAlliance) {
            relationText += "  [Economic Alliance]";
        }

        Color relationColor = {0.7f, 0.7f, 0.7f, 1.0f};
        if (stance == aoc::sim::DiplomaticStance::Hostile) {
            relationColor = {1.0f, 0.3f, 0.3f, 1.0f};
        } else if (stance == aoc::sim::DiplomaticStance::Friendly ||
                   stance == aoc::sim::DiplomaticStance::Allied) {
            relationColor = {0.3f, 1.0f, 0.3f, 1.0f};
        }
        (void)ui.createLabel(playerPanel, {0.0f, 0.0f, 490.0f, 14.0f},
                              LabelData{std::move(relationText), relationColor, 11.0f});

        // Active modifiers
        if (!rel.modifiers.empty()) {
            std::string modText = "Modifiers: ";
            bool first = true;
            for (const aoc::sim::RelationModifier& mod : rel.modifiers) {
                if (!first) {
                    modText += ", ";
                }
                modText += mod.reason + " (" + std::to_string(mod.amount) + ")";
                first = false;
            }
            (void)ui.createLabel(playerPanel, {0.0f, 0.0f, 490.0f, 14.0f},
                                  LabelData{std::move(modText), {0.6f, 0.6f, 0.7f, 1.0f}, 10.0f});
        }

        // Action buttons row
        WidgetId btnRow = ui.createPanel(
            playerPanel, {0.0f, 0.0f, 490.0f, 26.0f},
            PanelData{{0.0f, 0.0f, 0.0f, 0.0f}, 0.0f});
        Widget* btnRowWidget = ui.getWidget(btnRow);
        if (btnRowWidget != nullptr) {
            btnRowWidget->layoutDirection = LayoutDirection::Horizontal;
            btnRowWidget->childSpacing = 6.0f;
        }

        aoc::sim::DiplomacyManager* diplomacy = this->m_diplomacy;
        const PlayerId humanPlayer = this->m_player;

        if (!rel.isAtWar) {
            // Declare War button
            ButtonData warBtn;
            warBtn.label = "Declare War";
            warBtn.fontSize = 11.0f;
            warBtn.normalColor  = {0.4f, 0.15f, 0.15f, 0.9f};
            warBtn.hoverColor   = {0.55f, 0.2f, 0.2f, 0.9f};
            warBtn.pressedColor = {0.3f, 0.1f, 0.1f, 0.9f};
            warBtn.cornerRadius = 3.0f;
            warBtn.onClick = [diplomacy, humanPlayer, otherId, &ui, this]() {
                diplomacy->declareWar(humanPlayer, otherId,
                                       aoc::sim::CasusBelliType::SurpriseWar,
                                       nullptr, this->m_gameState,
                                       this->m_gameState != nullptr
                                           ? this->m_gameState->currentTurn()
                                           : 0);
                LOG_INFO("Declared war on player %u", static_cast<unsigned>(otherId));
                this->close(ui);
            };
            (void)ui.createButton(btnRow, {0.0f, 0.0f, 100.0f, 22.0f}, std::move(warBtn));
        } else {
            // Propose Peace button
            ButtonData peaceBtn;
            peaceBtn.label = "Propose Peace";
            peaceBtn.fontSize = 11.0f;
            peaceBtn.normalColor  = {0.15f, 0.35f, 0.15f, 0.9f};
            peaceBtn.hoverColor   = {0.20f, 0.50f, 0.20f, 0.9f};
            peaceBtn.pressedColor = {0.10f, 0.25f, 0.10f, 0.9f};
            peaceBtn.cornerRadius = 3.0f;
            peaceBtn.onClick = [diplomacy, humanPlayer, otherId, &ui, this]() {
                diplomacy->makePeace(humanPlayer, otherId);
                LOG_INFO("Made peace with player %u", static_cast<unsigned>(otherId));
                this->close(ui);
            };
            (void)ui.createButton(btnRow, {0.0f, 0.0f, 110.0f, 22.0f}, std::move(peaceBtn));
        }

        if (!rel.hasOpenBorders && !rel.isAtWar) {
            // Open Borders button
            ButtonData bordersBtn;
            bordersBtn.label = "Open Borders";
            bordersBtn.fontSize = 11.0f;
            bordersBtn.normalColor  = {0.2f, 0.25f, 0.35f, 0.9f};
            bordersBtn.hoverColor   = {0.3f, 0.35f, 0.50f, 0.9f};
            bordersBtn.pressedColor = {0.15f, 0.18f, 0.25f, 0.9f};
            bordersBtn.cornerRadius = 3.0f;
            bordersBtn.onClick = [diplomacy, humanPlayer, otherId, &ui, this]() {
                diplomacy->grantOpenBorders(humanPlayer, otherId);
                LOG_INFO("Granted open borders with player %u", static_cast<unsigned>(otherId));
                this->close(ui);
            };
            (void)ui.createButton(btnRow, {0.0f, 0.0f, 110.0f, 22.0f}, std::move(bordersBtn));
        }

        // Alliance buttons (only available when relations > 20 and not at war)
        if (!rel.isAtWar && score > 20) {
            if (!rel.hasMilitaryAlliance) {
                ButtonData milBtn;
                milBtn.label = "Military Alliance";
                milBtn.fontSize = 10.0f;
                milBtn.normalColor  = {0.15f, 0.25f, 0.40f, 0.9f};
                milBtn.hoverColor   = {0.20f, 0.35f, 0.55f, 0.9f};
                milBtn.pressedColor = {0.10f, 0.18f, 0.30f, 0.9f};
                milBtn.cornerRadius = 3.0f;
                const int32_t turn = this->m_gameState->currentTurn();
                milBtn.onClick = [diplomacy, humanPlayer, otherId, turn, &ui, this]() {
                    const aoc::ErrorCode ec =
                        diplomacy->formMilitaryAlliance(humanPlayer, otherId, turn);
                    if (ec == aoc::ErrorCode::Ok) {
                        LOG_INFO("Military alliance with player %u", static_cast<unsigned>(otherId));
                    } else {
                        LOG_INFO("Military alliance rejected (player %u): %s",
                                 static_cast<unsigned>(otherId),
                                 std::string(aoc::describeError(ec)).c_str());
                    }
                    this->close(ui);
                };
                (void)ui.createButton(btnRow, {0.0f, 0.0f, 120.0f, 22.0f}, std::move(milBtn));
            }

            if (!rel.hasResearchAgreement) {
                ButtonData resBtn;
                resBtn.label = "Research Pact";
                resBtn.fontSize = 10.0f;
                resBtn.normalColor  = {0.15f, 0.30f, 0.15f, 0.9f};
                resBtn.hoverColor   = {0.20f, 0.40f, 0.20f, 0.9f};
                resBtn.pressedColor = {0.10f, 0.22f, 0.10f, 0.9f};
                resBtn.cornerRadius = 3.0f;
                const int32_t turn = this->m_gameState->currentTurn();
                resBtn.onClick = [diplomacy, humanPlayer, otherId, turn, &ui, this]() {
                    const aoc::ErrorCode ec =
                        diplomacy->formResearchAgreement(humanPlayer, otherId, turn);
                    if (ec == aoc::ErrorCode::Ok) {
                        LOG_INFO("Research agreement with player %u", static_cast<unsigned>(otherId));
                    } else {
                        LOG_INFO("Research agreement rejected (player %u): %s",
                                 static_cast<unsigned>(otherId),
                                 std::string(aoc::describeError(ec)).c_str());
                    }
                    this->close(ui);
                };
                (void)ui.createButton(btnRow, {0.0f, 0.0f, 110.0f, 22.0f}, std::move(resBtn));
            }

            if (!rel.hasEconomicAlliance) {
                ButtonData econBtn;
                econBtn.label = "Economic Pact";
                econBtn.fontSize = 10.0f;
                econBtn.normalColor  = {0.30f, 0.25f, 0.10f, 0.9f};
                econBtn.hoverColor   = {0.40f, 0.35f, 0.15f, 0.9f};
                econBtn.pressedColor = {0.22f, 0.18f, 0.08f, 0.9f};
                econBtn.cornerRadius = 3.0f;
                const int32_t turn = this->m_gameState->currentTurn();
                econBtn.onClick = [diplomacy, humanPlayer, otherId, turn, &ui, this]() {
                    const aoc::ErrorCode ec =
                        diplomacy->formEconomicAlliance(humanPlayer, otherId, turn);
                    if (ec == aoc::ErrorCode::Ok) {
                        LOG_INFO("Economic alliance with player %u", static_cast<unsigned>(otherId));
                    } else {
                        LOG_INFO("Economic alliance rejected (player %u): %s",
                                 static_cast<unsigned>(otherId),
                                 std::string(aoc::describeError(ec)).c_str());
                    }
                    this->close(ui);
                };
                (void)ui.createButton(btnRow, {0.0f, 0.0f, 110.0f, 22.0f}, std::move(econBtn));
            }
        }

        // Embargo button (only when not at war and not already embargoed)
        if (!rel.isAtWar && !rel.hasEmbargo) {
            ButtonData embargoBtn;
            embargoBtn.label = "Embargo";
            embargoBtn.fontSize = 11.0f;
            embargoBtn.normalColor  = {0.35f, 0.20f, 0.10f, 0.9f};
            embargoBtn.hoverColor   = {0.50f, 0.30f, 0.15f, 0.9f};
            embargoBtn.pressedColor = {0.25f, 0.15f, 0.08f, 0.9f};
            embargoBtn.cornerRadius = 3.0f;
            embargoBtn.onClick = [diplomacy, humanPlayer, otherId, &ui, this]() {
                diplomacy->setEmbargo(humanPlayer, otherId, true);
                LOG_INFO("Imposed trade embargo on player %u", static_cast<unsigned>(otherId));
                this->close(ui);
            };
            (void)ui.createButton(btnRow, {0.0f, 0.0f, 90.0f, 22.0f}, std::move(embargoBtn));
        }

        // --------------------------------------------------------------
        // Trade / economic action buttons (human-side counterparts to
        // AIController's offer paths). Gated by relation + resources.
        // AI counterparty "auto-accepts" (direct call) since there's no
        // pending-proposal queue yet.
        // --------------------------------------------------------------
        if (!rel.isAtWar && score > 10) {
            aoc::game::GameState* gsState = this->m_gameState;
            aoc::game::Player* selfPlayer = gsState->player(humanPlayer);
            aoc::game::Player* otherPlayer = gsState->player(otherId);
            const CurrencyAmount myGold = selfPlayer != nullptr ? selfPlayer->treasury() : 0;
            const CurrencyAmount theirGold = otherPlayer != nullptr ? otherPlayer->treasury() : 0;

            // Bilateral trade deal
            bool alreadyPaired = false;
            if (selfPlayer != nullptr) {
                for (const aoc::sim::TradeAgreementDef& agr :
                     selfPlayer->tradeAgreements().agreements) {
                    if (!agr.isActive) { continue; }
                    if (agr.type != aoc::sim::TradeAgreementType::BilateralDeal) { continue; }
                    for (PlayerId m : agr.members) {
                        if (m == otherId) { alreadyPaired = true; break; }
                    }
                    if (alreadyPaired) { break; }
                }
            }
            if (!alreadyPaired && score > 15) {
                ButtonData bdBtn;
                bdBtn.label = "Bilateral Deal";
                bdBtn.fontSize = 10.0f;
                bdBtn.normalColor  = {0.20f, 0.30f, 0.35f, 0.9f};
                bdBtn.hoverColor   = {0.28f, 0.42f, 0.48f, 0.9f};
                bdBtn.pressedColor = {0.14f, 0.22f, 0.26f, 0.9f};
                bdBtn.cornerRadius = 3.0f;
                bdBtn.onClick = [gsState, humanPlayer, otherId, &ui, this]() {
                    const ErrorCode rc = aoc::sim::proposeBilateralDeal(
                        *gsState, humanPlayer, otherId);
                    if (rc == ErrorCode::Ok) {
                        LOG_INFO("Human proposed bilateral deal with player %u",
                                 static_cast<unsigned>(otherId));
                    }
                    this->close(ui);
                };
                (void)ui.createButton(btnRow, {0.0f, 0.0f, 110.0f, 22.0f}, std::move(bdBtn));
            }

            // Offer Loan: lend up to 25% of treasury if flush and partner broke.
            if (myGold > 400 && theirGold < 150) {
                const CurrencyAmount principal = std::min(myGold / 4,
                    static_cast<CurrencyAmount>(500));
                ButtonData lnBtn;
                lnBtn.label = "Offer Loan";
                lnBtn.fontSize = 10.0f;
                lnBtn.normalColor  = {0.30f, 0.25f, 0.10f, 0.9f};
                lnBtn.hoverColor   = {0.42f, 0.35f, 0.15f, 0.9f};
                lnBtn.pressedColor = {0.22f, 0.18f, 0.07f, 0.9f};
                lnBtn.cornerRadius = 3.0f;
                lnBtn.onClick = [gsState, humanPlayer, otherId, principal, &ui, this]() {
                    const ErrorCode rc = aoc::sim::createIOU(
                        *gsState, humanPlayer, otherId, principal, 0.08f, 15);
                    if (rc == ErrorCode::Ok) {
                        LOG_INFO("Human offered loan to player %u: %d gold @ 8%% for 15 turns",
                                 static_cast<unsigned>(otherId),
                                 static_cast<int>(principal));
                    }
                    this->close(ui);
                };
                (void)ui.createButton(btnRow, {0.0f, 0.0f, 100.0f, 22.0f}, std::move(lnBtn));
            }

            // Sell Smallest City: requires 3+ cities, buyer with enough gold.
            if (this->m_dealTracker != nullptr && this->m_grid != nullptr
                && selfPlayer != nullptr && otherPlayer != nullptr
                && selfPlayer->cities().size() >= 3) {
                aoc::game::City* victim = nullptr;
                int32_t smallestPop = INT_MAX;
                for (const std::unique_ptr<aoc::game::City>& c : selfPlayer->cities()) {
                    if (c->population() < smallestPop) {
                        smallestPop = c->population();
                        victim = c.get();
                    }
                }
                const int32_t price = 200 + smallestPop * 50;
                if (victim != nullptr && theirGold > price + 100) {
                    const aoc::hex::AxialCoord loc = victim->location();
                    aoc::sim::GlobalDealTracker* tracker = this->m_dealTracker;
                    aoc::map::HexGrid* grid = this->m_grid;
                    std::string cityName = victim->name();
                    ButtonData scBtn;
                    scBtn.label = "Sell City (" + cityName + ", " + std::to_string(price) + "g)";
                    scBtn.fontSize = 10.0f;
                    scBtn.normalColor  = {0.35f, 0.18f, 0.25f, 0.9f};
                    scBtn.hoverColor   = {0.48f, 0.24f, 0.34f, 0.9f};
                    scBtn.pressedColor = {0.25f, 0.12f, 0.18f, 0.9f};
                    scBtn.cornerRadius = 3.0f;
                    scBtn.onClick = [gsState, grid, tracker, humanPlayer, otherId,
                                      loc, price, cityName, &ui, this]() {
                        aoc::sim::DiplomaticDeal deal{};
                        deal.playerA = humanPlayer;
                        deal.playerB = otherId;
                        aoc::sim::DealTerm cede{};
                        cede.type = aoc::sim::DealTermType::CedeCity;
                        cede.fromPlayer = humanPlayer;
                        cede.toPlayer   = otherId;
                        cede.tileCoord  = loc;
                        deal.terms.push_back(cede);
                        aoc::sim::DealTerm pay{};
                        pay.type = aoc::sim::DealTermType::GoldLump;
                        pay.fromPlayer = otherId;
                        pay.toPlayer   = humanPlayer;
                        pay.goldLump   = price;
                        deal.terms.push_back(pay);
                        const std::size_t idx = tracker->activeDeals.size();
                        if (aoc::sim::proposeDeal(*gsState, *tracker, deal) == ErrorCode::Ok) {
                            if (aoc::sim::acceptDeal(*gsState, *grid, *tracker,
                                                     static_cast<int32_t>(idx)) == ErrorCode::Ok) {
                                LOG_INFO("Human sold city %s to player %u for %d gold",
                                         cityName.c_str(),
                                         static_cast<unsigned>(otherId), price);
                            }
                        }
                        this->close(ui);
                    };
                    (void)ui.createButton(btnRow, {0.0f, 0.0f, 160.0f, 22.0f}, std::move(scBtn));
                }
            }

            // Cede Tile: scan for border-adjacent owned hex. 100 gold.
            if (this->m_dealTracker != nullptr && this->m_grid != nullptr
                && theirGold > 150) {
                aoc::map::HexGrid* grid = this->m_grid;
                const int32_t tileCount = grid->tileCount();
                aoc::hex::AxialCoord foundTile{0, 0};
                bool haveTile = false;
                for (int32_t idx = 0; idx < tileCount && !haveTile; ++idx) {
                    if (grid->owner(idx) != humanPlayer) { continue; }
                    const aoc::hex::AxialCoord c = grid->toAxial(idx);
                    const std::array<aoc::hex::AxialCoord, 6> nbrs = aoc::hex::neighbors(c);
                    for (const aoc::hex::AxialCoord& n : nbrs) {
                        const int32_t nIdx = grid->toIndex(n);
                        if (nIdx < 0 || nIdx >= tileCount) { continue; }
                        if (grid->owner(nIdx) == otherId) {
                            foundTile = c;
                            haveTile = true;
                            break;
                        }
                    }
                }
                if (haveTile) {
                    aoc::sim::GlobalDealTracker* tracker = this->m_dealTracker;
                    ButtonData ctBtn;
                    ctBtn.label = "Cede Tile (100g)";
                    ctBtn.fontSize = 10.0f;
                    ctBtn.normalColor  = {0.20f, 0.30f, 0.20f, 0.9f};
                    ctBtn.hoverColor   = {0.28f, 0.42f, 0.28f, 0.9f};
                    ctBtn.pressedColor = {0.14f, 0.22f, 0.14f, 0.9f};
                    ctBtn.cornerRadius = 3.0f;
                    ctBtn.onClick = [gsState, grid, tracker, humanPlayer, otherId,
                                      foundTile, &ui, this]() {
                        aoc::sim::DiplomaticDeal deal{};
                        deal.playerA = humanPlayer;
                        deal.playerB = otherId;
                        aoc::sim::DealTerm cede{};
                        cede.type = aoc::sim::DealTermType::CedeTile;
                        cede.fromPlayer = humanPlayer;
                        cede.toPlayer   = otherId;
                        cede.tileCoord  = foundTile;
                        deal.terms.push_back(cede);
                        aoc::sim::DealTerm pay{};
                        pay.type = aoc::sim::DealTermType::GoldLump;
                        pay.fromPlayer = otherId;
                        pay.toPlayer   = humanPlayer;
                        pay.goldLump   = 100;
                        deal.terms.push_back(pay);
                        const std::size_t idx = tracker->activeDeals.size();
                        if (aoc::sim::proposeDeal(*gsState, *tracker, deal) == ErrorCode::Ok) {
                            if (aoc::sim::acceptDeal(*gsState, *grid, *tracker,
                                                     static_cast<int32_t>(idx)) == ErrorCode::Ok) {
                                LOG_INFO("Human ceded tile (%d,%d) to player %u for 100 gold",
                                         foundTile.q, foundTile.r,
                                         static_cast<unsigned>(otherId));
                            }
                        }
                        this->close(ui);
                    };
                    (void)ui.createButton(btnRow, {0.0f, 0.0f, 120.0f, 22.0f}, std::move(ctBtn));
                }
            }
        }
    }

    ui.layout();
}

void DiplomacyScreen::close(UIManager& ui) {
    if (!this->m_isOpen) {
        return;
    }
    this->m_isOpen = false;
    if (this->m_rootPanel != INVALID_WIDGET) {
        ui.removeWidget(this->m_rootPanel);
        this->m_rootPanel = INVALID_WIDGET;
    }
    this->m_playerList = INVALID_WIDGET;
}

void DiplomacyScreen::refresh(UIManager& /*ui*/) {
    // Diplomacy screen is static once opened; re-open to refresh.
}

} // namespace aoc::ui
