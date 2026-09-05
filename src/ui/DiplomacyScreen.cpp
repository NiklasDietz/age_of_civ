/**
 * @file DiplomacyScreen.cpp
 * @brief Diplomacy screen implementation.
 */

#include "aoc/ui/DiplomacyScreen.hpp"
#include "aoc/ui/UIManager.hpp"
#include "aoc/ui/StyleTokens.hpp"
#include "aoc/ui/Theme.hpp"
#include "aoc/game/GameState.hpp"
#include "aoc/game/Player.hpp"
#include "aoc/game/City.hpp"
#include "aoc/map/HexGrid.hpp"
#include "aoc/map/HexCoord.hpp"
#include "aoc/simulation/civilization/Civilization.hpp"
#include "aoc/simulation/diplomacy/DiplomacyActions.hpp"
#include "aoc/simulation/diplomacy/DiplomacyState.hpp"
#include "aoc/simulation/ai/LeaderPersonality.hpp"
#include "aoc/simulation/diplomacy/DealProposals.hpp"
#include "aoc/simulation/diplomacy/DealTerms.hpp"
#include "aoc/simulation/economy/TradeAgreement.hpp"
#include "aoc/simulation/monetary/Bonds.hpp"
#include "aoc/core/Log.hpp"

#include <array>
#include <climits>
#include <functional>
#include <memory>
#include <string>

namespace aoc::ui {

void DiplomacyScreen::setContext(aoc::game::GameState* gameState, PlayerId humanPlayer,
                                  aoc::sim::DiplomacyManager* diplomacy,
                                  aoc::map::HexGrid* grid,
                                  aoc::sim::GlobalDealTracker* dealTracker,
                                  aoc::sim::AllianceObligationTracker* obligations) {
    this->m_gameState   = gameState;
    this->m_player      = humanPlayer;
    this->m_diplomacy   = diplomacy;
    this->m_grid        = grid;
    this->m_dealTracker = dealTracker;
    this->m_obligations = obligations;
    this->m_warTarget   = INVALID_PLAYER;
    this->m_composerTarget = INVALID_PLAYER;
    this->m_composerTerms.clear();
}

void DiplomacyScreen::toggleComposerTerm(aoc::sim::DealTermType type, PlayerId from, PlayerId to, int32_t gold) {
    // Gold: one lump per direction; the same amount again removes it.
    for (std::vector<aoc::sim::DealTerm>::iterator it = this->m_composerTerms.begin();
         it != this->m_composerTerms.end(); ++it) {
        const bool sameSlot = it->type == type && it->fromPlayer == from && it->toPlayer == to;
        if (!sameSlot) {
            continue;
        }
        const bool sameAmount = type != aoc::sim::DealTermType::GoldLump || it->goldLump == gold;
        this->m_composerTerms.erase(it);
        if (sameAmount) {
            return;
        }
        break;
    }
    aoc::sim::DealTerm term{};
    term.type       = type;
    term.fromPlayer = from;
    term.toPlayer   = to;
    term.goldLump   = gold;
    term.duration   = 30;
    this->m_composerTerms.push_back(term);
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

    // Iterate over MET players only. Unmet civs don't surface here —
    // meeting someone via unit contact populates `hasMet`, then they
    // appear in diplomacy. Prevents the earlier exploit where every
    // player was visible from turn 0.
    // Inbox: deal proposals other civs made to the human, newest last.
    {
        const std::vector<aoc::sim::PendingProposal>& inbox = this->m_gameState->pendingProposals();
        bool any = false;
        for (std::size_t i = 0; i < inbox.size(); ++i) {
            const aoc::sim::PendingProposal& p = inbox[i];
            if (p.to != this->m_player) {
                continue;
            }
            if (!any) {
                (void)ui.createLabel(this->m_playerList, {0.0f, 0.0f, 500.0f, 18.0f},
                                     LabelData{"INBOX", tokens::TEXT_HEADER, 12.0f});
                any = true;
            }
            const aoc::game::Player* fromPlayer = this->m_gameState->player(p.from);
            std::string text = "From ";
            text += fromPlayer != nullptr ? std::string(aoc::sim::civDef(fromPlayer->civId()).name)
                                          : "P" + std::to_string(static_cast<unsigned>(p.from));
            text += ": ";
            for (std::size_t t = 0; t < p.deal.terms.size(); ++t) {
                if (t > 0) { text += "; "; }
                text += aoc::sim::describeDealTerm(*this->m_gameState, p.deal.terms[t]);
            }
            text += "  (expires turn " + std::to_string(p.expiresTurn) + ")";
            const bool warDeal = this->m_diplomacy != nullptr && this->m_diplomacy->isAtWar(p.from, p.to);
            const std::string quote =
                fromPlayer != nullptr
                    ? std::string(aoc::sim::getLeaderDialogue(
                          fromPlayer->civId(), warDeal ? aoc::sim::DialogueContext::ProposePeace
                                                       : aoc::sim::DialogueContext::ProposeTrade))
                    : std::string();
            PanelData cardBg;
            cardBg.backgroundColor = tokens::SURFACE_PARCHMENT_DIM;
            cardBg.cornerRadius    = tokens::CORNER_BUTTON;
            const WidgetId card = ui.createPanel(this->m_playerList, {0.0f, 0.0f, 500.0f, 64.0f}, std::move(cardBg));
            if (Widget* cw = ui.getWidget(card); cw != nullptr) {
                cw->padding = {2.0f, 4.0f, 2.0f, 4.0f};
                cw->childSpacing = 2.0f;
            }
            (void)ui.createLabel(card, {0.0f, 0.0f, 490.0f, 14.0f}, LabelData{text, tokens::TEXT_INK, 10.0f});
            if (!quote.empty()) {
                (void)ui.createLabel(card, {0.0f, 0.0f, 490.0f, 14.0f},
                                     LabelData{"\"" + quote + "\"", tokens::TEXT_DISABLED, 10.0f});
            }
            const WidgetId answerRow = ui.createPanel(card, {0.0f, 0.0f, 490.0f, 24.0f},
                                                      PanelData{Color{0.0f, 0.0f, 0.0f, 0.0f}, 0.0f});
            if (Widget* ar = ui.getWidget(answerRow); ar != nullptr) {
                ar->layoutDirection = LayoutDirection::Horizontal;
                ar->childSpacing    = 6.0f;
            }
            for (const bool accept : {true, false}) {
                ButtonData btn;
                btn.label        = accept ? "Accept" : "Reject";
                btn.fontSize     = 11.0f;
                btn.normalColor  = accept ? tokens::STATE_SUCCESS : tokens::STATE_DANGER;
                btn.hoverColor   = tokens::BRONZE_LIGHT;
                btn.pressedColor = tokens::STATE_PRESSED;
                btn.cornerRadius = 3.0f;
                btn.onClick      = [i, accept, &ui, this]() {
                    if (this->m_gameState != nullptr && this->m_grid != nullptr && this->m_dealTracker != nullptr
                        && this->m_diplomacy != nullptr) {
                        const aoc::ErrorCode rc = aoc::sim::requestRespondToProposal(
                            *this->m_gameState, *this->m_grid, *this->m_dealTracker, *this->m_diplomacy,
                            this->m_player, i, accept, this->m_gameState->currentTurn());
                        if (rc != aoc::ErrorCode::Ok) {
                            LOG_INFO("Proposal answer rejected: %.*s", static_cast<int>(aoc::describeError(rc).size()),
                                     aoc::describeError(rc).data());
                        }
                    }
                    this->close(ui);
                };
                (void)ui.createButton(answerRow, {0.0f, 0.0f, 80.0f, 22.0f}, std::move(btn));
            }
        }
    }

    for (const std::unique_ptr<aoc::game::Player>& playerPtr : this->m_gameState->players()) {
        const PlayerId otherId = playerPtr->id();
        if (otherId == this->m_player || otherId == BARBARIAN_PLAYER) {
            continue;
        }
        if (!this->m_diplomacy->haveMet(this->m_player, otherId)) {
            continue;
        }

        const aoc::sim::CivilizationDef& civDefRef = aoc::sim::civDef(playerPtr->civId());
        const aoc::sim::PairwiseRelation& rel = this->m_diplomacy->relation(this->m_player, otherId);
        const int32_t score = rel.totalScore();
        const aoc::sim::DiplomaticStance stance = rel.stance();

        // Compute stance accent first so the card chrome can pick up
        // its colour (border + accent bar = relation hue).
        Color stanceAccent = tokens::DIPLO_NEUTRAL;
        switch (stance) {
            case aoc::sim::DiplomaticStance::Allied:     stanceAccent = tokens::DIPLO_ALLIED;     break;
            case aoc::sim::DiplomaticStance::Friendly:   stanceAccent = tokens::DIPLO_FRIENDLY;   break;
            case aoc::sim::DiplomaticStance::Neutral:    stanceAccent = tokens::DIPLO_NEUTRAL;    break;
            case aoc::sim::DiplomaticStance::Unfriendly: stanceAccent = tokens::DIPLO_UNFRIENDLY; break;
            case aoc::sim::DiplomaticStance::Hostile:    stanceAccent = tokens::DIPLO_HOSTILE;    break;
            default: break;
        }
        if (rel.isAtWar) { stanceAccent = tokens::DIPLO_AT_WAR; }

        // Player info panel — full card chrome: parchment surface,
        // stance-coloured accent bar + border, gilt highlight, mahogany
        // shadow underline. Cards visually flag at-a-glance whether the
        // civ is friend or foe before you read the labels.
        PanelData cardBg;
        cardBg.backgroundColor = tokens::SURFACE_PARCHMENT;
        cardBg.gradientBottom  = tokens::SURFACE_PARCHMENT_DIM;
        cardBg.borderColor     = stanceAccent;
        cardBg.borderWidth     = 1.5f;
        cardBg.cornerRadius    = tokens::CORNER_PANEL;
        cardBg.accentBarColor  = stanceAccent;
        cardBg.accentBarWidth  = 4.0f;
        cardBg.topHighlight    = tokens::BRONZE_LIGHT;
        cardBg.bottomShadow    = tokens::SURFACE_MAHOGANY;
        WidgetId playerPanel = ui.createPanel(
            this->m_playerList, {0.0f, 0.0f, 510.0f, 110.0f}, std::move(cardBg));
        Widget* ppWidget = ui.getWidget(playerPanel);
        if (ppWidget != nullptr) {
            ppWidget->padding = {8.0f, 8.0f, 8.0f, 8.0f};
            ppWidget->childSpacing = 4.0f;
        }
        ui.setWidgetTooltip(playerPanel,
            std::string(civDefRef.name) + " — " + std::string(civDefRef.leaderName)
            + "\nStance: " + std::string(aoc::sim::stanceName(stance))
            + (rel.isAtWar ? " (AT WAR)" : "")
            + "\nRelation score: " + std::to_string(score));

        // Portrait card: player-colour sprite + civ/leader heading +
        // inline stats (ability, score, stance). Replaces two plain
        // label rows with a richer header that matches the Civ-6-style
        // diplomacy strip up top.
        PortraitData portrait;
        portrait.title = std::string(civDefRef.name) + " - "
                        + std::string(civDefRef.leaderName);
        portrait.tint = aoc::ui::theme().playerColor(static_cast<uint8_t>(otherId));
        portrait.fallbackColor = portrait.tint;
        portrait.stats.emplace_back("Ability", std::string(civDefRef.abilityName));
        portrait.stats.emplace_back("Score", std::to_string(score));
        portrait.stats.emplace_back("Stance", std::string(aoc::sim::stanceName(stance)));
        (void)ui.createPortrait(
            playerPanel, {0.0f, 0.0f, 490.0f, 56.0f}, std::move(portrait));

        // Relation and stance info
        std::string relationText = "Score: " + std::to_string(score)
                                 + "  Stance: " + std::string(aoc::sim::stanceName(stance));
        if (rel.isAtWar) {
            relationText += "  [AT WAR]";
        }
        if (rel.hasOpenBorders) {
            relationText += "  [Open Borders]";
            if (rel.openBordersUntilTurn >= 0) {
                relationText += " until " + std::to_string(rel.openBordersUntilTurn);
            }
        }
        const int32_t nowTurn = this->m_gameState->currentTurn();
        if (rel.friendshipUntilTurn > nowTurn) {
            relationText += "  [Friends until " + std::to_string(rel.friendshipUntilTurn) + "]";
        }
        if (rel.denouncedOnTurn >= 0 && nowTurn - rel.denouncedOnTurn <= aoc::sim::DENOUNCE_TURNS) {
            relationText += "  [Denounced]";
        }
        if (rel.hasEmbassy) {
            relationText += "  [Embassy]";
        } else if (rel.hasDelegation) {
            relationText += "  [Delegation]";
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

        (void)ui.createLabel(playerPanel, {0.0f, 0.0f, 490.0f, 14.0f},
                              LabelData{std::move(relationText), stanceAccent, 11.0f});

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
                                  LabelData{std::move(modText), tokens::TEXT_DISABLED, 10.0f});
        }

        // Action buttons row
        WidgetId btnRow = ui.createPanel(
            playerPanel, {0.0f, 0.0f, 490.0f, 26.0f},
            PanelData{Color{0.0f, 0.0f, 0.0f, 0.0f}, 0.0f});
        Widget* btnRowWidget = ui.getWidget(btnRow);
        if (btnRowWidget != nullptr) {
            btnRowWidget->layoutDirection = LayoutDirection::Horizontal;
            btnRowWidget->childSpacing = 6.0f;
        }

        aoc::sim::DiplomacyManager* diplomacy = this->m_diplomacy;
        const PlayerId humanPlayer = this->m_player;
        aoc::game::GameState* gsForActions = this->m_gameState;
        aoc::sim::AllianceObligationTracker* obligations = this->m_obligations;

        // Every action goes through DiplomacyActions.hpp and closes the screen;
        // the HUD re-opens it with fresh state.
        const auto addAction = [&ui, this](WidgetId row, const char* label, float width,
                                          const Color& normal,
                                          std::function<aoc::ErrorCode()> action) {
            ButtonData btn;
            btn.label        = label;
            btn.fontSize     = 11.0f;
            btn.normalColor  = normal;
            btn.hoverColor   = tokens::BRONZE_LIGHT;
            btn.pressedColor = tokens::STATE_PRESSED;
            btn.cornerRadius = 3.0f;
            btn.onClick      = [action, label, &ui, this]() {
                const aoc::ErrorCode rc = action();
                if (rc != aoc::ErrorCode::Ok) {
                    LOG_INFO("%s rejected: %.*s", label, static_cast<int>(aoc::describeError(rc).size()),
                             aoc::describeError(rc).data());
                }
                this->m_warTarget = INVALID_PLAYER;
                this->close(ui);
            };
            (void)ui.createButton(row, {0.0f, 0.0f, width, 22.0f}, std::move(btn));
        };

        if (!rel.isAtWar && this->m_warTarget == otherId) {
            // Casus belli picker: one button per justified casus belli, plus Cancel.
            for (const aoc::sim::CasusBelliType cb :
                 aoc::sim::availableCasusBelli(*gsForActions, *diplomacy, humanPlayer, otherId, nowTurn)) {
                const aoc::sim::CasusBelliDef& def = aoc::sim::casusBelliDef(cb);
                addAction(btnRow, def.name.data(), 120.0f, tokens::STATE_DANGER,
                          [gsForActions, diplomacy, humanPlayer, otherId, cb, nowTurn, obligations]() {
                              return aoc::sim::requestDeclareWar(*gsForActions, *diplomacy, humanPlayer,
                                                                 otherId, cb, nowTurn, obligations);
                          });
            }
            ButtonData cancelBtn;
            cancelBtn.label        = "Cancel";
            cancelBtn.fontSize     = 11.0f;
            cancelBtn.normalColor  = tokens::BRONZE_BASE;
            cancelBtn.hoverColor   = tokens::BRONZE_LIGHT;
            cancelBtn.pressedColor = tokens::STATE_PRESSED;
            cancelBtn.cornerRadius = 3.0f;
            cancelBtn.onClick      = [&ui, this]() {
                this->m_warTarget = INVALID_PLAYER;
                this->close(ui);
                this->open(ui);
            };
            (void)ui.createButton(btnRow, {0.0f, 0.0f, 80.0f, 22.0f}, std::move(cancelBtn));
        } else if (!rel.isAtWar) {
            ButtonData warBtn;
            warBtn.label        = "Declare War";
            warBtn.fontSize     = 11.0f;
            warBtn.normalColor  = tokens::STATE_DANGER;
            warBtn.hoverColor   = {0.767f, 0.272f, 0.197f, 1.0f};
            warBtn.pressedColor = {0.511f, 0.182f, 0.131f, 1.0f};
            warBtn.cornerRadius = 3.0f;
            warBtn.onClick      = [otherId, &ui, this]() {
                this->m_warTarget = otherId; // second step: pick the casus belli
                this->close(ui);
                this->open(ui);
            };
            (void)ui.createButton(btnRow, {0.0f, 0.0f, 100.0f, 22.0f}, std::move(warBtn));
        } else {
            addAction(btnRow, "Propose Peace", 110.0f, tokens::STATE_SUCCESS,
                      [gsForActions, diplomacy, humanPlayer, otherId, nowTurn]() {
                          return aoc::sim::requestMakePeace(*gsForActions, *diplomacy, humanPlayer, otherId,
                                                            nowTurn);
                      });
        }

        if (!rel.hasOpenBorders && !rel.isAtWar) {
            addAction(btnRow, "Open Borders", 110.0f, tokens::BRONZE_BASE,
                      [gsForActions, diplomacy, humanPlayer, otherId, nowTurn]() {
                          return aoc::sim::requestOpenBorders(*gsForActions, *diplomacy, humanPlayer, otherId,
                                                              nowTurn);
                      });
        }

        if (!rel.isAtWar) {
            WidgetId stanceRow = ui.createPanel(playerPanel, {0.0f, 0.0f, 490.0f, 26.0f},
                                                PanelData{Color{0.0f, 0.0f, 0.0f, 0.0f}, 0.0f});
            if (Widget* sr = ui.getWidget(stanceRow); sr != nullptr) {
                sr->layoutDirection = LayoutDirection::Horizontal;
                sr->childSpacing    = 6.0f;
            }
            const bool denounced = rel.denouncedOnTurn >= 0
                                   && nowTurn - rel.denouncedOnTurn <= aoc::sim::DENOUNCE_TURNS;
            const bool friends = rel.friendshipUntilTurn > nowTurn;
            if (!denounced && !friends) {
                addAction(stanceRow, "Denounce", 90.0f, tokens::DIPLO_UNFRIENDLY,
                          [gsForActions, diplomacy, humanPlayer, otherId, nowTurn]() {
                              return aoc::sim::requestDenounce(*gsForActions, *diplomacy, humanPlayer, otherId,
                                                               nowTurn);
                          });
                addAction(stanceRow, "Friendship", 90.0f, tokens::STATE_SUCCESS,
                          [gsForActions, diplomacy, humanPlayer, otherId, nowTurn]() {
                              return aoc::sim::requestDeclareFriendship(*gsForActions, *diplomacy, humanPlayer,
                                                                        otherId, nowTurn);
                          });
            }
            if (!rel.hasDelegation && !rel.hasEmbassy) {
                addAction(stanceRow, "Delegation (25 gold)", 140.0f, tokens::BRONZE_BASE,
                          [gsForActions, diplomacy, humanPlayer, otherId]() {
                              return aoc::sim::requestSendDelegation(*gsForActions, *diplomacy, humanPlayer,
                                                                     otherId);
                          });
            }
            if (!rel.hasEmbassy) {
                addAction(stanceRow, "Embassy (50 gold)", 130.0f, tokens::BRONZE_BASE,
                          [gsForActions, diplomacy, humanPlayer, otherId]() {
                              return aoc::sim::requestEstablishEmbassy(*gsForActions, *diplomacy, humanPlayer,
                                                                       otherId);
                          });
            }
        }

        // Deal composer: toggle a few preset terms, then send through requestProposeDeal.
        if (!rel.isAtWar && this->m_dealTracker != nullptr && this->m_grid != nullptr) {
            if (this->m_composerTarget != otherId) {
                ButtonData proposeBtn;
                proposeBtn.label        = "Propose Deal";
                proposeBtn.fontSize     = 11.0f;
                proposeBtn.normalColor  = tokens::BRONZE_BASE;
                proposeBtn.hoverColor   = tokens::BRONZE_LIGHT;
                proposeBtn.pressedColor = tokens::STATE_PRESSED;
                proposeBtn.cornerRadius = 3.0f;
                proposeBtn.onClick      = [otherId, &ui, this]() {
                    this->m_composerTarget = otherId;
                    this->m_composerTerms.clear();
                    this->close(ui);
                    this->open(ui);
                };
                (void)ui.createButton(btnRow, {0.0f, 0.0f, 110.0f, 22.0f}, std::move(proposeBtn));
            } else {
                std::string summary = "Proposal: ";
                if (this->m_composerTerms.empty()) {
                    summary += "(nothing yet)";
                }
                for (std::size_t t = 0; t < this->m_composerTerms.size(); ++t) {
                    if (t > 0) { summary += "; "; }
                    summary += aoc::sim::describeDealTerm(*gsForActions, this->m_composerTerms[t]);
                }
                (void)ui.createLabel(playerPanel, {0.0f, 0.0f, 490.0f, 14.0f},
                                     LabelData{std::move(summary), tokens::TEXT_INK, 10.0f});
                const WidgetId termRow = ui.createPanel(playerPanel, {0.0f, 0.0f, 490.0f, 26.0f},
                                                        PanelData{Color{0.0f, 0.0f, 0.0f, 0.0f}, 0.0f});
                if (Widget* tr = ui.getWidget(termRow); tr != nullptr) {
                    tr->layoutDirection = LayoutDirection::Horizontal;
                    tr->childSpacing    = 4.0f;
                }
                struct Toggle {
                    const char* label;
                    aoc::sim::DealTermType type;
                    bool give;
                    int32_t gold;
                };
                const Toggle toggles[6] = {
                    {"Give 100 gold", aoc::sim::DealTermType::GoldLump, true, 100},
                    {"Give 500 gold", aoc::sim::DealTermType::GoldLump, true, 500},
                    {"Ask 100 gold", aoc::sim::DealTermType::GoldLump, false, 100},
                    {"Ask 500 gold", aoc::sim::DealTermType::GoldLump, false, 500},
                    {"Open Borders", aoc::sim::DealTermType::OpenBorders, true, 0},
                    {"Non-Aggression", aoc::sim::DealTermType::NonAggression, true, 0},
                };
                for (const Toggle& tg : toggles) {
                    ButtonData btn;
                    btn.label        = tg.label;
                    btn.fontSize     = 10.0f;
                    btn.normalColor  = tokens::SURFACE_PARCHMENT_DIM;
                    btn.hoverColor   = tokens::BRONZE_LIGHT;
                    btn.pressedColor = tokens::STATE_PRESSED;
                    btn.cornerRadius = 3.0f;
                    const aoc::sim::DealTermType type = tg.type;
                    const PlayerId from = tg.give ? humanPlayer : otherId;
                    const PlayerId to   = tg.give ? otherId : humanPlayer;
                    const int32_t gold  = tg.gold;
                    btn.onClick = [type, from, to, gold, &ui, this]() {
                        this->toggleComposerTerm(type, from, to, gold);
                        this->close(ui);
                        this->open(ui);
                    };
                    (void)ui.createButton(termRow, {0.0f, 0.0f, 76.0f, 22.0f}, std::move(btn));
                }
                const WidgetId sendRow = ui.createPanel(playerPanel, {0.0f, 0.0f, 490.0f, 26.0f},
                                                        PanelData{Color{0.0f, 0.0f, 0.0f, 0.0f}, 0.0f});
                if (Widget* sr = ui.getWidget(sendRow); sr != nullptr) {
                    sr->layoutDirection = LayoutDirection::Horizontal;
                    sr->childSpacing    = 6.0f;
                }
                aoc::sim::GlobalDealTracker* tracker = this->m_dealTracker;
                aoc::map::HexGrid* grid              = this->m_grid;
                addAction(sendRow, "Send Proposal", 120.0f, tokens::STATE_SUCCESS,
                          [gsForActions, grid, tracker, diplomacy, humanPlayer, otherId, nowTurn, this]() {
                              aoc::sim::DiplomaticDeal deal;
                              deal.playerA = humanPlayer;
                              deal.playerB = otherId;
                              deal.terms   = this->m_composerTerms;
                              this->m_composerTarget = INVALID_PLAYER;
                              this->m_composerTerms.clear();
                              return aoc::sim::requestProposeDeal(*gsForActions, *grid, *tracker, *diplomacy, deal,
                                                                  nowTurn);
                          });
                ButtonData cancelDeal;
                cancelDeal.label        = "Cancel";
                cancelDeal.fontSize     = 11.0f;
                cancelDeal.normalColor  = tokens::BRONZE_BASE;
                cancelDeal.hoverColor   = tokens::BRONZE_LIGHT;
                cancelDeal.pressedColor = tokens::STATE_PRESSED;
                cancelDeal.cornerRadius = 3.0f;
                cancelDeal.onClick      = [&ui, this]() {
                    this->m_composerTarget = INVALID_PLAYER;
                    this->m_composerTerms.clear();
                    this->close(ui);
                    this->open(ui);
                };
                (void)ui.createButton(sendRow, {0.0f, 0.0f, 80.0f, 22.0f}, std::move(cancelDeal));
            }
        }

        // Alliance buttons (only available when relations > 20 and not at war)
        if (!rel.isAtWar && score > 20) {
            if (!rel.hasMilitaryAlliance) {
                ButtonData milBtn;
                milBtn.label = "Military Alliance";
                milBtn.fontSize = 10.0f;
                milBtn.normalColor  = tokens::DIPLO_ALLIED;
                milBtn.hoverColor   = {0.296f, 0.522f, 0.789f, 1.0f};
                milBtn.pressedColor = {0.198f, 0.348f, 0.526f, 1.0f};
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
                resBtn.normalColor  = tokens::STATE_SUCCESS;
                resBtn.hoverColor   = {0.432f, 0.654f, 0.292f, 1.0f};
                resBtn.pressedColor = {0.288f, 0.436f, 0.194f, 1.0f};
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
                econBtn.normalColor  = tokens::RES_GOLD;
                econBtn.hoverColor   = tokens::BRONZE_LIGHT;
                econBtn.pressedColor = tokens::BRONZE_DARK;
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
            embargoBtn.normalColor  = tokens::DIPLO_UNFRIENDLY;
            embargoBtn.hoverColor   = {0.879f, 0.532f, 0.297f, 1.0f};
            embargoBtn.pressedColor = {0.625f, 0.328f, 0.142f, 1.0f};
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
                bdBtn.normalColor  = tokens::SURFACE_PARCHMENT_DIM;
                bdBtn.hoverColor   = tokens::BRONZE_LIGHT;
                bdBtn.pressedColor = tokens::BRONZE_DARK;
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
                lnBtn.normalColor  = tokens::RES_GOLD;
                lnBtn.hoverColor   = tokens::BRONZE_LIGHT;
                lnBtn.pressedColor = tokens::BRONZE_DARK;
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
                    scBtn.normalColor  = tokens::RES_CULTURE;
                    scBtn.hoverColor   = {0.654f, 0.348f, 0.654f, 1.0f};
                    scBtn.pressedColor = {0.408f, 0.184f, 0.408f, 1.0f};
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
                    ctBtn.normalColor  = tokens::STATE_SUCCESS;
                    ctBtn.hoverColor   = {0.432f, 0.654f, 0.292f, 1.0f};
                    ctBtn.pressedColor = {0.288f, 0.436f, 0.194f, 1.0f};
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
