/**
 * @file TradeScreen.cpp
 * @brief Trade deal screen implementation.
 */

#include "aoc/ui/TradeScreen.hpp"
#include "aoc/ui/UIManager.hpp"
#include "aoc/game/GameState.hpp"
#include "aoc/game/Player.hpp"
#include "aoc/game/City.hpp"
#include "aoc/simulation/civilization/Civilization.hpp"
#include "aoc/simulation/economy/Market.hpp"
#include "aoc/simulation/diplomacy/DiplomacyState.hpp"
#include "aoc/simulation/resource/ResourceTypes.hpp"
#include "aoc/core/Log.hpp"

#include <cstring>
#include <string>
#include <unordered_map>

namespace aoc::ui {

void TradeScreen::setContext(aoc::game::GameState* gameState, PlayerId humanPlayer,
                              const aoc::sim::Market* market,
                              aoc::sim::DiplomacyManager* diplomacy) {
    this->m_gameState = gameState;
    this->m_player    = humanPlayer;
    this->m_market    = market;
    this->m_diplomacy = diplomacy;
}

void TradeScreen::open(UIManager& ui) {
    if (this->m_isOpen) {
        return;
    }

    this->m_isOpen = true;
    this->m_partner = INVALID_PLAYER;
    std::memset(this->m_offerAmounts, 0, sizeof(this->m_offerAmounts));
    std::memset(this->m_requestAmounts, 0, sizeof(this->m_requestAmounts));
    this->m_offerGold   = 0;
    this->m_requestGold = 0;

    WidgetId innerPanel = this->createScreenFrame(
        ui, "Trade", 550.0f, 500.0f, this->m_screenW, this->m_screenH);

    // Status label
    this->m_statusLabel = ui.createLabel(
        innerPanel, {0.0f, 0.0f, 520.0f, 16.0f},
        LabelData{"Select a trade partner:", {0.8f, 0.9f, 0.8f, 1.0f}, 13.0f});

    // Partner selection list
    this->m_partnerList = ui.createScrollList(
        innerPanel, {0.0f, 0.0f, 520.0f, 380.0f});

    Widget* listWidget = ui.getWidget(this->m_partnerList);
    if (listWidget != nullptr) {
        listWidget->padding = {4.0f, 4.0f, 4.0f, 4.0f};
        listWidget->childSpacing = 3.0f;
    }

    // Show ONLY MET players (not self, not barbarians). Without the
    // `haveMet` gate the list leaked every civ at turn 0, which let
    // players trade with unseen rivals.
    if (this->m_gameState != nullptr) {
        for (const std::unique_ptr<aoc::game::Player>& playerPtr : this->m_gameState->players()) {
            const PlayerId otherId = playerPtr->id();
            if (otherId == this->m_player || otherId == BARBARIAN_PLAYER) {
                continue;
            }
            if (this->m_diplomacy == nullptr
                || !this->m_diplomacy->haveMet(this->m_player, otherId)) {
                continue;
            }

            const aoc::sim::CivilizationDef& civDefRef = aoc::sim::civDef(playerPtr->civId());
            std::string label = std::string(civDefRef.name) + " (" +
                                std::string(civDefRef.leaderName) + ")";

            ButtonData btn;
            btn.label = std::move(label);
            btn.fontSize = 12.0f;
            btn.normalColor  = {0.2f, 0.2f, 0.28f, 0.9f};
            btn.hoverColor   = {0.3f, 0.3f, 0.38f, 0.9f};
            btn.pressedColor = {0.15f, 0.15f, 0.2f, 0.9f};
            btn.cornerRadius = 3.0f;

            const PlayerId partnerId = otherId;
            btn.onClick = [this, &ui, innerPanel, partnerId]() {
                this->m_partner = partnerId;
                // Remove old partner list and build trade columns
                if (this->m_partnerList != INVALID_WIDGET) {
                    ui.removeWidget(this->m_partnerList);
                    this->m_partnerList = INVALID_WIDGET;
                }
                this->buildTradeColumns(ui, innerPanel, partnerId);
            };

            (void)ui.createButton(this->m_partnerList, {0.0f, 0.0f, 510.0f, 24.0f},
                                   std::move(btn));
        }
    }

    ui.layout();
}

void TradeScreen::buildTradeColumns(UIManager& ui, WidgetId innerPanel, PlayerId partner) {
    // Find partner civ name
    std::string partnerName = "Player " + std::to_string(static_cast<unsigned>(partner));
    if (this->m_gameState != nullptr) {
        const aoc::game::Player* partnerPlayer = this->m_gameState->player(partner);
        if (partnerPlayer != nullptr) {
            partnerName = std::string(aoc::sim::civDef(partnerPlayer->civId()).name);
        }
    }

    ui.setLabelText(this->m_statusLabel, "Trading with " + partnerName);

    // Trade panel with two columns
    this->m_tradePanel = ui.createScrollList(
        innerPanel, {0.0f, 0.0f, 520.0f, 340.0f});

    Widget* tradePanelWidget = ui.getWidget(this->m_tradePanel);
    if (tradePanelWidget != nullptr) {
        tradePanelWidget->padding = {4.0f, 4.0f, 4.0f, 4.0f};
        tradePanelWidget->childSpacing = 3.0f;
    }

    // Header row
    (void)ui.createLabel(this->m_tradePanel, {0.0f, 0.0f, 510.0f, 16.0f},
                          LabelData{"You Offer  |  You Request", {1.0f, 0.9f, 0.5f, 1.0f}, 13.0f});

    // Gold row
    {
        WidgetId goldRow = ui.createPanel(
            this->m_tradePanel, {0.0f, 0.0f, 510.0f, 26.0f},
            PanelData{{0.0f, 0.0f, 0.0f, 0.0f}, 0.0f});
        Widget* goldRowWidget = ui.getWidget(goldRow);
        if (goldRowWidget != nullptr) {
            goldRowWidget->layoutDirection = LayoutDirection::Horizontal;
            goldRowWidget->childSpacing = 4.0f;
        }

        // Offer gold -/+
        ButtonData offerMinusBtn;
        offerMinusBtn.label = "Gold-";
        offerMinusBtn.fontSize = 10.0f;
        offerMinusBtn.normalColor = {0.3f, 0.2f, 0.2f, 0.9f};
        offerMinusBtn.hoverColor  = {0.4f, 0.3f, 0.3f, 0.9f};
        offerMinusBtn.pressedColor = {0.2f, 0.15f, 0.15f, 0.9f};
        offerMinusBtn.cornerRadius = 2.0f;
        offerMinusBtn.onClick = [this]() {
            this->m_offerGold = (this->m_offerGold > 10) ? this->m_offerGold - 10 : 0;
        };
        (void)ui.createButton(goldRow, {0.0f, 0.0f, 55.0f, 22.0f}, std::move(offerMinusBtn));

        ButtonData offerPlusBtn;
        offerPlusBtn.label = "Gold+";
        offerPlusBtn.fontSize = 10.0f;
        offerPlusBtn.normalColor = {0.2f, 0.3f, 0.2f, 0.9f};
        offerPlusBtn.hoverColor  = {0.3f, 0.4f, 0.3f, 0.9f};
        offerPlusBtn.pressedColor = {0.15f, 0.2f, 0.15f, 0.9f};
        offerPlusBtn.cornerRadius = 2.0f;
        offerPlusBtn.onClick = [this]() {
            this->m_offerGold += 10;
        };
        (void)ui.createButton(goldRow, {0.0f, 0.0f, 55.0f, 22.0f}, std::move(offerPlusBtn));

        // Spacer
        (void)ui.createPanel(goldRow, {0.0f, 0.0f, 40.0f, 22.0f},
                              PanelData{{0.0f, 0.0f, 0.0f, 0.0f}, 0.0f});

        // Request gold -/+
        ButtonData reqMinusBtn;
        reqMinusBtn.label = "Gold-";
        reqMinusBtn.fontSize = 10.0f;
        reqMinusBtn.normalColor = {0.3f, 0.2f, 0.2f, 0.9f};
        reqMinusBtn.hoverColor  = {0.4f, 0.3f, 0.3f, 0.9f};
        reqMinusBtn.pressedColor = {0.2f, 0.15f, 0.15f, 0.9f};
        reqMinusBtn.cornerRadius = 2.0f;
        reqMinusBtn.onClick = [this]() {
            this->m_requestGold = (this->m_requestGold > 10) ? this->m_requestGold - 10 : 0;
        };
        (void)ui.createButton(goldRow, {0.0f, 0.0f, 55.0f, 22.0f}, std::move(reqMinusBtn));

        ButtonData reqPlusBtn;
        reqPlusBtn.label = "Gold+";
        reqPlusBtn.fontSize = 10.0f;
        reqPlusBtn.normalColor = {0.2f, 0.3f, 0.2f, 0.9f};
        reqPlusBtn.hoverColor  = {0.3f, 0.4f, 0.3f, 0.9f};
        reqPlusBtn.pressedColor = {0.15f, 0.2f, 0.15f, 0.9f};
        reqPlusBtn.cornerRadius = 2.0f;
        reqPlusBtn.onClick = [this]() {
            this->m_requestGold += 10;
        };
        (void)ui.createButton(goldRow, {0.0f, 0.0f, 55.0f, 22.0f}, std::move(reqPlusBtn));
    }

    // Goods rows -- show the first several goods with market prices
    const uint16_t totalGoods = (this->m_market != nullptr) ? this->m_market->goodsCount() : 0;
    const uint16_t goodsToShow = (totalGoods < MAX_TRADE_GOODS) ? totalGoods : MAX_TRADE_GOODS;

    for (uint16_t g = 0; g < goodsToShow; ++g) {
        const aoc::sim::GoodDef& gDef = aoc::sim::goodDef(g);
        if (gDef.basePrice <= 0) {
            continue;
        }

        WidgetId goodRow = ui.createPanel(
            this->m_tradePanel, {0.0f, 0.0f, 510.0f, 24.0f},
            PanelData{{0.0f, 0.0f, 0.0f, 0.0f}, 0.0f});
        Widget* goodRowWidget = ui.getWidget(goodRow);
        if (goodRowWidget != nullptr) {
            goodRowWidget->layoutDirection = LayoutDirection::Horizontal;
            goodRowWidget->childSpacing = 3.0f;
        }

        // Good name
        (void)ui.createLabel(goodRow, {0.0f, 0.0f, 100.0f, 20.0f},
                              LabelData{std::string(gDef.name), {0.7f, 0.7f, 0.8f, 1.0f}, 10.0f});

        // Offer -/+
        const uint16_t goodId = g;
        ButtonData offerMinus;
        offerMinus.label = "-";
        offerMinus.fontSize = 10.0f;
        offerMinus.normalColor = {0.3f, 0.2f, 0.2f, 0.9f};
        offerMinus.hoverColor  = {0.4f, 0.3f, 0.3f, 0.9f};
        offerMinus.pressedColor = {0.2f, 0.15f, 0.15f, 0.9f};
        offerMinus.cornerRadius = 2.0f;
        offerMinus.onClick = [this, goodId]() {
            if (this->m_offerAmounts[goodId] > 0) {
                --this->m_offerAmounts[goodId];
            }
        };
        (void)ui.createButton(goodRow, {0.0f, 0.0f, 28.0f, 20.0f}, std::move(offerMinus));

        ButtonData offerPlus;
        offerPlus.label = "+";
        offerPlus.fontSize = 10.0f;
        offerPlus.normalColor = {0.2f, 0.3f, 0.2f, 0.9f};
        offerPlus.hoverColor  = {0.3f, 0.4f, 0.3f, 0.9f};
        offerPlus.pressedColor = {0.15f, 0.2f, 0.15f, 0.9f};
        offerPlus.cornerRadius = 2.0f;
        offerPlus.onClick = [this, goodId]() {
            ++this->m_offerAmounts[goodId];
        };
        (void)ui.createButton(goodRow, {0.0f, 0.0f, 28.0f, 20.0f}, std::move(offerPlus));

        // Spacer
        (void)ui.createPanel(goodRow, {0.0f, 0.0f, 30.0f, 20.0f},
                              PanelData{{0.0f, 0.0f, 0.0f, 0.0f}, 0.0f});

        // Request -/+
        ButtonData reqMinus;
        reqMinus.label = "-";
        reqMinus.fontSize = 10.0f;
        reqMinus.normalColor = {0.3f, 0.2f, 0.2f, 0.9f};
        reqMinus.hoverColor  = {0.4f, 0.3f, 0.3f, 0.9f};
        reqMinus.pressedColor = {0.2f, 0.15f, 0.15f, 0.9f};
        reqMinus.cornerRadius = 2.0f;
        reqMinus.onClick = [this, goodId]() {
            if (this->m_requestAmounts[goodId] > 0) {
                --this->m_requestAmounts[goodId];
            }
        };
        (void)ui.createButton(goodRow, {0.0f, 0.0f, 28.0f, 20.0f}, std::move(reqMinus));

        ButtonData reqPlus;
        reqPlus.label = "+";
        reqPlus.fontSize = 10.0f;
        reqPlus.normalColor = {0.2f, 0.3f, 0.2f, 0.9f};
        reqPlus.hoverColor  = {0.3f, 0.4f, 0.3f, 0.9f};
        reqPlus.pressedColor = {0.15f, 0.2f, 0.15f, 0.9f};
        reqPlus.cornerRadius = 2.0f;
        reqPlus.onClick = [this, goodId]() {
            ++this->m_requestAmounts[goodId];
        };
        (void)ui.createButton(goodRow, {0.0f, 0.0f, 28.0f, 20.0f}, std::move(reqPlus));
    }

    // Check for embargo and show warning if active
    const bool embargoActive = (this->m_diplomacy != nullptr) &&
        this->m_diplomacy->hasEmbargo(this->m_player, partner);
    if (embargoActive) {
        (void)ui.createLabel(this->m_tradePanel, {0.0f, 0.0f, 510.0f, 20.0f},
                              LabelData{"Embargo in effect -- trade prohibited",
                                        {1.0f, 0.3f, 0.3f, 1.0f}, 13.0f});
    }

    // "Propose Trade" button
    ButtonData proposeBtn;
    proposeBtn.label = embargoActive ? "Embargo Active" : "Propose Trade";
    proposeBtn.fontSize = 13.0f;
    if (embargoActive) {
        proposeBtn.normalColor  = {0.3f, 0.3f, 0.3f, 0.9f};
        proposeBtn.hoverColor   = {0.3f, 0.3f, 0.3f, 0.9f};
        proposeBtn.pressedColor = {0.3f, 0.3f, 0.3f, 0.9f};
    } else {
        proposeBtn.normalColor  = {0.15f, 0.35f, 0.15f, 0.9f};
        proposeBtn.hoverColor   = {0.20f, 0.50f, 0.20f, 0.9f};
        proposeBtn.pressedColor = {0.10f, 0.25f, 0.10f, 0.9f};
    }
    proposeBtn.cornerRadius = 4.0f;
    const bool localEmbargoActive = embargoActive;
    proposeBtn.onClick = [this, &ui, localEmbargoActive]() {
        if (localEmbargoActive) {
            ui.setLabelText(this->m_statusLabel, "Embargo in effect -- cannot trade!");
            return;
        }
        // AI evaluation: sum market value of what AI receives vs what AI gives.
        // Scarcity modifier: goods the AI has 0 of are valued at 2x market price
        // (they desperately need them). Goods in surplus are valued at 0.5x
        // (they don't need more). This creates mutually beneficial trades.
        float aiReceivesValue = static_cast<float>(this->m_offerGold);
        float aiGivesValue    = static_cast<float>(this->m_requestGold);

        if (this->m_market != nullptr && this->m_gameState != nullptr) {
            // Aggregate AI partner's total stockpile across all their cities
            std::unordered_map<uint16_t, int32_t> partnerStockpile;
            const aoc::game::Player* partnerPlayer = this->m_gameState->player(this->m_partner);
            if (partnerPlayer != nullptr) {
                for (const std::unique_ptr<aoc::game::City>& city : partnerPlayer->cities()) {
                    for (const std::pair<const uint16_t, int32_t>& entry : city->stockpile().goods) {
                        partnerStockpile[entry.first] += entry.second;
                    }
                }
            }

            const uint16_t evalGoodsCount = this->m_market->goodsCount();
            const uint16_t maxGoods = (evalGoodsCount < MAX_TRADE_GOODS) ? evalGoodsCount : MAX_TRADE_GOODS;
            for (uint16_t g = 0; g < maxGoods; ++g) {
                const int32_t marketPrice = this->m_market->price(g);
                const float baseValue = static_cast<float>(marketPrice);

                // Compute scarcity multiplier for the AI partner
                const int32_t partnerAmount = partnerStockpile[g];
                constexpr int32_t SURPLUS_THRESHOLD = 5;
                float scarcityMultiplier = 1.0f;
                if (partnerAmount == 0) {
                    scarcityMultiplier = 2.0f;   // Desperately needed
                } else if (partnerAmount >= SURPLUS_THRESHOLD) {
                    scarcityMultiplier = 0.5f;   // Already has plenty
                }

                // What AI receives (our offer): valued with scarcity modifier
                aiReceivesValue += static_cast<float>(this->m_offerAmounts[g])
                                 * baseValue * scarcityMultiplier;
                // What AI gives (our request): valued at straight market price
                aiGivesValue += static_cast<float>(this->m_requestAmounts[g])
                              * baseValue;
            }
        }

        // AI accepts if they receive >= 80% of what they give
        constexpr float ACCEPTANCE_THRESHOLD = 0.8f;
        const bool accepted = (aiGivesValue <= 0.0f) ||
                              (aiReceivesValue >= aiGivesValue * ACCEPTANCE_THRESHOLD);

        if (accepted) {
            LOG_INFO("Trade deal accepted by player %u", static_cast<unsigned>(this->m_partner));
            ui.setLabelText(this->m_statusLabel, "Trade ACCEPTED!");
        } else {
            LOG_INFO("Trade deal rejected by player %u", static_cast<unsigned>(this->m_partner));
            ui.setLabelText(this->m_statusLabel, "Trade REJECTED - offer more!");
        }
    };

    (void)ui.createButton(this->m_tradePanel, {0.0f, 0.0f, 200.0f, 30.0f},
                            std::move(proposeBtn));

    ui.layout();
}

void TradeScreen::close(UIManager& ui) {
    if (!this->m_isOpen) {
        return;
    }
    this->m_isOpen = false;
    if (this->m_rootPanel != INVALID_WIDGET) {
        ui.removeWidget(this->m_rootPanel);
        this->m_rootPanel = INVALID_WIDGET;
    }
    this->m_statusLabel = INVALID_WIDGET;
    this->m_partnerList = INVALID_WIDGET;
    this->m_tradePanel  = INVALID_WIDGET;
    this->m_partner     = INVALID_PLAYER;
}

void TradeScreen::refresh(UIManager& /*ui*/) {
    // Trade screen is static once opened; no dynamic refresh needed.
}

} // namespace aoc::ui
