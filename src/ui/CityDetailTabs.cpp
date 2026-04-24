/**
 * @file CityDetailTabs.cpp
 * @brief Tab-builder methods for `CityDetailScreen`, split out of
 *        GameScreens.cpp so that 2300-line monolith becomes two
 *        1300-line translation units instead of one 2300.
 *
 * The five tabs — Overview, Production, Buildings, Citizens, Couriers —
 * each add ~100-330 widgets to the content panel. They share the same
 * `resolveCityByLocation` helper, duplicated here as a static free
 * function (12 lines) to avoid cross-TU ceremony. If further tabs
 * appear they should land in this file rather than in GameScreens.cpp.
 */

#include "aoc/ui/GameScreens.hpp"
#include "aoc/ui/UIManager.hpp"
#include "aoc/game/GameState.hpp"
#include "aoc/game/Player.hpp"
#include "aoc/game/City.hpp"
#include "aoc/game/Unit.hpp"
#include "aoc/map/HexGrid.hpp"
#include "aoc/simulation/city/CityComponent.hpp"
#include "aoc/simulation/city/ProductionQueue.hpp"
#include "aoc/simulation/city/ProductionSystem.hpp"
#include "aoc/simulation/city/District.hpp"
#include "aoc/simulation/city/Happiness.hpp"
#include "aoc/simulation/city/CityLoyalty.hpp"
#include "aoc/simulation/tech/TechTree.hpp"
#include "aoc/simulation/tech/TechGating.hpp"
#include "aoc/simulation/resource/ResourceComponent.hpp"
#include "aoc/simulation/resource/ResourceTypes.hpp"
#include "aoc/simulation/economy/Market.hpp"
#include "aoc/simulation/resource/EconomySimulation.hpp"
#include "aoc/simulation/economy/TradeRoute.hpp"
#include "aoc/simulation/economy/DomesticCourier.hpp"
#include "aoc/simulation/wonder/Wonder.hpp"
#include "aoc/simulation/map/Improvement.hpp"
#include "aoc/simulation/government/Government.hpp"
#include "aoc/simulation/government/GovernmentComponent.hpp"
#include "aoc/simulation/monetary/MonetarySystem.hpp"
#include "aoc/core/Log.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

namespace aoc::ui {

/// Resolve the City object matching m_cityLocation for this screen.
/// Returns nullptr when the city is no longer available. Duplicated
/// from GameScreens.cpp so this translation unit is self-contained.
static aoc::game::City* resolveCityByLocation(aoc::game::GameState* gs,
                                              PlayerId owner,
                                              aoc::hex::AxialCoord cityLocation) {
    if (gs == nullptr) { return nullptr; }
    aoc::game::Player* p = gs->player(owner);
    if (p == nullptr) { return nullptr; }
    for (const std::unique_ptr<aoc::game::City>& c : p->cities()) {
        if (c->location() == cityLocation) {
            return c.get();
        }
    }
    return nullptr;
}

// ----------------------------------------------------------------------------
// buildOverviewTab -- summary: yields, loyalty, happiness, current production
// ----------------------------------------------------------------------------

void CityDetailScreen::buildOverviewTab(UIManager& ui, WidgetId contentPanel) {
    constexpr float kListWidth = 310.0f;
    constexpr float kContentWidth = 330.0f;
    constexpr Color kHeaderBg = {0.18f, 0.20f, 0.28f, 0.95f};
    constexpr Color kHeaderTextColor = {0.9f, 0.85f, 0.6f, 1.0f};
    constexpr Color kDimTextColor = {0.55f, 0.55f, 0.60f, 0.8f};
    constexpr Color kBodyTextColor = {0.78f, 0.80f, 0.82f, 1.0f};
    constexpr Color kSeparatorColor = {0.22f, 0.24f, 0.30f, 0.6f};
    constexpr Color kFoodColor = {0.3f, 0.8f, 0.3f, 1.0f};
    constexpr Color kProdColor = {0.9f, 0.55f, 0.2f, 1.0f};
    constexpr Color kGoldColor = {0.95f, 0.85f, 0.2f, 1.0f};
    constexpr Color kSciColor = {0.3f, 0.55f, 0.95f, 1.0f};
    constexpr Color kCultColor = {0.7f, 0.35f, 0.85f, 1.0f};

    const aoc::game::City* city = resolveCityByLocation(this->m_gameState, this->m_player, this->m_cityLocation);
    if (city == nullptr) {
        (void)ui.createLabel(contentPanel, {0.0f, 0.0f, kContentWidth, 16.0f},
            LabelData{"City not found", {0.8f, 0.4f, 0.4f, 1.0f}, 13.0f});
        return;
    }

    // Scrollable area for tab content
    WidgetId scrollArea = ui.createScrollList(
        contentPanel, {0.0f, 0.0f, kContentWidth, 520.0f},
        ScrollListData{{0.12f, 0.14f, 0.18f, 0.0f}, 0.0f, 0.0f});
    {
        Widget* listWidget = ui.getWidget(scrollArea);
        if (listWidget != nullptr) {
            listWidget->padding = {4.0f, 6.0f, 4.0f, 6.0f};
            listWidget->childSpacing = 3.0f;
        }
    }

    // -- Yield summary row --
    int32_t totalFood = 0;
    int32_t totalProd = 0;
    int32_t totalGold = 0;
    int32_t totalSci  = 0;
    int32_t totalCult = 0;

    for (const hex::AxialCoord& tile : city->workedTiles()) {
        if (!this->m_grid->isValid(tile)) {
            continue;
        }
        const int32_t tileIndex = this->m_grid->toIndex(tile);
        const aoc::map::TileYield ty = this->m_grid->tileYield(tileIndex);
        totalFood += ty.food;
        totalProd += ty.production;
        totalGold += ty.gold;
        totalSci  += ty.science;
        totalCult += ty.culture;
    }

    // Building bonuses for yield totals
    int32_t bldgProd = 0;
    int32_t bldgSci  = 0;
    int32_t bldgGold = 0;
    const aoc::sim::CityDistrictsComponent& districts = city->districts();
    for (const aoc::sim::CityDistrictsComponent::PlacedDistrict& district : districts.districts) {
        for (BuildingId bid : district.buildings) {
            const aoc::sim::BuildingDef& bdef = aoc::sim::buildingDef(bid);
            bldgProd += bdef.productionBonus;
            bldgSci  += bdef.scienceBonus;
            bldgGold += bdef.goldBonus;
        }
    }

    {
        // Row uses HorizontalWrap so the 5 yield chips flow to a second
        // line automatically if the panel narrows. Height grows to fit.
        WidgetId yieldRow = ui.createPanel(
            scrollArea, {0.0f, 0.0f, kListWidth, 58.0f},
            PanelData{{0.15f, 0.17f, 0.22f, 0.95f}, 4.0f});
        {
            Widget* yr = ui.getWidget(yieldRow);
            if (yr != nullptr) {
                yr->layoutDirection = LayoutDirection::HorizontalWrap;
                yr->padding = {4.0f, 6.0f, 4.0f, 6.0f};
                yr->childSpacing = 4.0f;
            }
        }

        struct YieldEntry {
            const char* name;
            int32_t value;
            Color dotColor;
        };
        const std::array<YieldEntry, 5> yieldEntries = {{
            {"Food",    totalFood,              kFoodColor},
            {"Prod",    totalProd + bldgProd,    kProdColor},
            {"Gold",    totalGold + bldgGold,    kGoldColor},
            {"Sci",     totalSci + bldgSci,      kSciColor},
            {"Cult",    totalCult,               kCultColor},
        }};

        constexpr float kChipW = 72.0f;
        constexpr float kChipH = 22.0f;

        for (const YieldEntry& entry : yieldEntries) {
            WidgetId entryPanel = ui.createPanel(
                yieldRow, {0.0f, 0.0f, kChipW, kChipH},
                PanelData{{0.0f, 0.0f, 0.0f, 0.0f}, 0.0f});
            {
                Widget* ep = ui.getWidget(entryPanel);
                if (ep != nullptr) {
                    ep->layoutDirection = LayoutDirection::Horizontal;
                    ep->padding = {2.0f, 4.0f, 2.0f, 4.0f};
                    ep->childSpacing = 4.0f;
                }
            }
            (void)ui.createPanel(
                entryPanel, {0.0f, 2.0f, 10.0f, 10.0f},
                PanelData{entry.dotColor, 5.0f});
            std::string valueText = std::to_string(entry.value) + " " + entry.name;
            (void)ui.createLabel(
                entryPanel, {0.0f, 0.0f, kChipW - 18.0f, 14.0f},
                LabelData{std::move(valueText), {0.92f, 0.92f, 0.92f, 1.0f}, 11.0f});
        }
    }

    (void)ui.createPanel(scrollArea, {0.0f, 0.0f, kListWidth, 1.0f},
        PanelData{kSeparatorColor, 0.0f});

    // -- Loyalty section --
    (void)ui.createPanel(scrollArea, {0.0f, 0.0f, kListWidth, 20.0f},
        PanelData{kHeaderBg, 3.0f});
    (void)ui.createLabel(scrollArea, {0.0f, -19.0f, kListWidth, 16.0f},
        LabelData{"  Loyalty", kHeaderTextColor, 11.0f});

    {
        const aoc::sim::CityLoyaltyComponent& loyaltyComp = city->loyalty();
        const aoc::sim::LoyaltyStatus loyaltyStatus = loyaltyComp.status();
        const char* statusName = aoc::sim::loyaltyStatusName(loyaltyStatus);

        Color loyaltyTextColor = {1.0f, 1.0f, 1.0f, 1.0f};
        Color loyaltyBarColor = {0.3f, 0.9f, 0.3f, 0.9f};
        switch (loyaltyStatus) {
            case aoc::sim::LoyaltyStatus::Loyal:
                loyaltyTextColor = {0.3f, 0.9f, 0.3f, 1.0f};
                loyaltyBarColor = {0.2f, 0.7f, 0.2f, 0.9f};
                break;
            case aoc::sim::LoyaltyStatus::Content:
                loyaltyTextColor = {0.85f, 0.85f, 0.85f, 1.0f};
                loyaltyBarColor = {0.5f, 0.7f, 0.3f, 0.9f};
                break;
            case aoc::sim::LoyaltyStatus::Disloyal:
                loyaltyTextColor = {0.9f, 0.9f, 0.2f, 1.0f};
                loyaltyBarColor = {0.8f, 0.8f, 0.15f, 0.9f};
                break;
            case aoc::sim::LoyaltyStatus::Unrest:
                loyaltyTextColor = {0.9f, 0.4f, 0.2f, 1.0f};
                loyaltyBarColor = {0.85f, 0.35f, 0.15f, 0.9f};
                break;
            case aoc::sim::LoyaltyStatus::Revolt:
                loyaltyTextColor = {0.9f, 0.1f, 0.1f, 1.0f};
                loyaltyBarColor = {0.8f, 0.1f, 0.1f, 0.9f};
                break;
        }

        char loyaltyBuf[128];
        const char* signStr = (loyaltyComp.loyaltyPerTurn >= 0.0f) ? "+" : "";
        std::snprintf(loyaltyBuf, sizeof(loyaltyBuf),
            "  %.0f / 100  %s    %s%.1f per turn",
            static_cast<double>(loyaltyComp.loyalty), statusName,
            signStr, static_cast<double>(loyaltyComp.loyaltyPerTurn));
        (void)ui.createLabel(scrollArea, {0.0f, 0.0f, kListWidth, 14.0f},
            LabelData{std::string(loyaltyBuf), loyaltyTextColor, 11.0f});

        constexpr float kBarWidth = 470.0f;
        constexpr float kBarHeight = 10.0f;
        WidgetId barBg = ui.createPanel(
            scrollArea, {0.0f, 0.0f, kBarWidth, kBarHeight},
            PanelData{{0.08f, 0.08f, 0.10f, 0.9f}, 3.0f});
        {
            const float fillFraction = std::clamp(loyaltyComp.loyalty / 100.0f, 0.0f, 1.0f);
            const float fillWidth = std::max(fillFraction * kBarWidth, 2.0f);
            (void)ui.createPanel(
                barBg, {0.0f, 0.0f, fillWidth, kBarHeight},
                PanelData{loyaltyBarColor, 3.0f});
        }

        char factorBuf[128];
        std::snprintf(factorBuf, sizeof(factorBuf),
            "  Base: +%.0f   Own pressure: +%.1f   Foreign: %.1f",
            static_cast<double>(loyaltyComp.baseLoyalty),
            static_cast<double>(loyaltyComp.ownCityPressure),
            static_cast<double>(loyaltyComp.foreignCityPressure));
        (void)ui.createLabel(scrollArea, {0.0f, 0.0f, kListWidth, 12.0f},
            LabelData{std::string(factorBuf), kBodyTextColor, 9.0f});

        std::string bonusLine = "  ";
        if (loyaltyComp.governorBonus > 0.0f) {
            char buf[48];
            std::snprintf(buf, sizeof(buf), "Governor: +%.0f  ", static_cast<double>(loyaltyComp.governorBonus));
            bonusLine += buf;
        }
        if (loyaltyComp.garrisonBonus > 0.0f) {
            char buf[48];
            std::snprintf(buf, sizeof(buf), "Garrison: +%.0f  ", static_cast<double>(loyaltyComp.garrisonBonus));
            bonusLine += buf;
        }
        if (loyaltyComp.monumentBonus > 0.0f) {
            char buf[48];
            std::snprintf(buf, sizeof(buf), "Monument: +%.0f  ", static_cast<double>(loyaltyComp.monumentBonus));
            bonusLine += buf;
        }
        if (loyaltyComp.ageEffect != 0.0f) {
            char buf[48];
            const char* ageSign = (loyaltyComp.ageEffect >= 0.0f) ? "+" : "";
            std::snprintf(buf, sizeof(buf), "Age: %s%.0f  ", ageSign, static_cast<double>(loyaltyComp.ageEffect));
            bonusLine += buf;
        }
        if (loyaltyComp.happinessEffect != 0.0f) {
            char buf[48];
            std::snprintf(buf, sizeof(buf), "Happiness: %.0f  ", static_cast<double>(loyaltyComp.happinessEffect));
            bonusLine += buf;
        }
        if (loyaltyComp.capturedPenalty != 0.0f) {
            char buf[48];
            std::snprintf(buf, sizeof(buf), "Captured: %.0f", static_cast<double>(loyaltyComp.capturedPenalty));
            bonusLine += buf;
        }
        if (bonusLine.size() > 2) {
            (void)ui.createLabel(scrollArea, {0.0f, 0.0f, kListWidth, 12.0f},
                LabelData{std::move(bonusLine), kBodyTextColor, 9.0f});
        }
    }

    (void)ui.createPanel(scrollArea, {0.0f, 0.0f, kListWidth, 1.0f},
        PanelData{kSeparatorColor, 0.0f});

    // -- Happiness section --
    (void)ui.createPanel(scrollArea, {0.0f, 0.0f, kListWidth, 20.0f},
        PanelData{kHeaderBg, 3.0f});
    (void)ui.createLabel(scrollArea, {0.0f, -19.0f, kListWidth, 16.0f},
        LabelData{"  Happiness", kHeaderTextColor, 11.0f});

    {
        const aoc::sim::CityHappinessComponent& happiness = city->happiness();
        const int32_t netHappy = static_cast<int32_t>(happiness.amenities - happiness.demand + happiness.modifiers);

        Color happyIndicatorColor = {0.3f, 0.8f, 0.3f, 1.0f};
        if (netHappy < 0) {
            happyIndicatorColor = {0.85f, 0.3f, 0.3f, 1.0f};
        } else if (netHappy == 0) {
            happyIndicatorColor = {0.8f, 0.8f, 0.3f, 1.0f};
        }

        WidgetId happyRow = ui.createPanel(
            scrollArea, {0.0f, 0.0f, kListWidth, 18.0f},
            PanelData{{0.0f, 0.0f, 0.0f, 0.0f}, 0.0f});
        {
            Widget* hr = ui.getWidget(happyRow);
            if (hr != nullptr) {
                hr->layoutDirection = LayoutDirection::Horizontal;
                hr->childSpacing = 6.0f;
                hr->padding = {2.0f, 4.0f, 2.0f, 4.0f};
            }
        }

        (void)ui.createPanel(
            happyRow, {0.0f, 1.0f, 12.0f, 12.0f},
            PanelData{happyIndicatorColor, 6.0f});

        char happyBuf[128];
        std::snprintf(happyBuf, sizeof(happyBuf),
            "%d amenities - %d demand + %d modifiers = %d net",
            static_cast<int>(happiness.amenities),
            static_cast<int>(happiness.demand),
            static_cast<int>(happiness.modifiers),
            netHappy);
        (void)ui.createLabel(
            happyRow, {0.0f, 0.0f, 420.0f, 14.0f},
            LabelData{std::string(happyBuf), kBodyTextColor, 11.0f});
    }

    (void)ui.createPanel(scrollArea, {0.0f, 0.0f, kListWidth, 1.0f},
        PanelData{kSeparatorColor, 0.0f});

    // -- Current production with progress bar --
    (void)ui.createPanel(scrollArea, {0.0f, 0.0f, kListWidth, 20.0f},
        PanelData{kHeaderBg, 3.0f});
    (void)ui.createLabel(scrollArea, {0.0f, -19.0f, kListWidth, 16.0f},
        LabelData{"  Current Production", kHeaderTextColor, 11.0f});

    {
        std::string prodName = "Nothing";
        float prodProgress = 0.0f;
        float prodTotal = 1.0f;
        const aoc::sim::ProductionQueueItem* current = city->production().currentItem();
        if (current != nullptr) {
            prodName    = current->name;
            prodProgress = current->progress;
            prodTotal   = current->totalCost;
        }

        char prodBuf[128];
        std::snprintf(prodBuf, sizeof(prodBuf), "  %s   (%d / %d)",
            prodName.c_str(),
            static_cast<int>(prodProgress),
            static_cast<int>(prodTotal));
        (void)ui.createLabel(scrollArea, {0.0f, 0.0f, kListWidth, 14.0f},
            LabelData{std::string(prodBuf), kBodyTextColor, 11.0f});

        constexpr float kProdBarWidth = 470.0f;
        constexpr float kProdBarHeight = 10.0f;
        WidgetId prodBarBg = ui.createPanel(
            scrollArea, {0.0f, 0.0f, kProdBarWidth, kProdBarHeight},
            PanelData{{0.08f, 0.08f, 0.10f, 0.9f}, 3.0f});
        {
            const float fraction = (prodTotal > 0.0f)
                ? std::clamp(prodProgress / prodTotal, 0.0f, 1.0f)
                : 0.0f;
            const float fillWidth = std::max(fraction * kProdBarWidth, 2.0f);
            (void)ui.createPanel(
                prodBarBg, {0.0f, 0.0f, fillWidth, kProdBarHeight},
                PanelData{kProdColor, 3.0f});
        }
    }

    (void)ui.createPanel(scrollArea, {0.0f, 0.0f, kListWidth, 1.0f},
        PanelData{kSeparatorColor, 0.0f});

    // -- Purchase hint --
    (void)ui.createPanel(scrollArea, {0.0f, 0.0f, kListWidth, 1.0f},
        PanelData{{0.3f, 0.3f, 0.4f, 0.3f}, 0.0f});
    (void)ui.createLabel(scrollArea, {0.0f, 0.0f, kListWidth, 14.0f},
        LabelData{"  Right-click unowned tiles on the map to purchase them", kDimTextColor, 10.0f});
}

// ----------------------------------------------------------------------------
// buildProductionTab -- queue management and buildable item list
// ----------------------------------------------------------------------------

void CityDetailScreen::buildProductionTab(UIManager& ui, WidgetId contentPanel) {
    constexpr float kListWidth = 310.0f;
    constexpr float kContentWidth = 330.0f;
    constexpr Color kHeaderBg = {0.18f, 0.20f, 0.28f, 0.95f};
    constexpr Color kHeaderTextColor = {0.9f, 0.85f, 0.6f, 1.0f};
    constexpr Color kDimTextColor = {0.55f, 0.55f, 0.60f, 0.8f};
    constexpr Color kBodyTextColor = {0.78f, 0.80f, 0.82f, 1.0f};
    constexpr Color kSeparatorColor = {0.22f, 0.24f, 0.30f, 0.6f};
    constexpr Color kProdColor = {0.9f, 0.55f, 0.2f, 1.0f};

    aoc::game::City* city = resolveCityByLocation(this->m_gameState, this->m_player, this->m_cityLocation);
    if (city == nullptr) {
        (void)ui.createLabel(contentPanel, {0.0f, 0.0f, kContentWidth, 16.0f},
            LabelData{"City not found", {0.8f, 0.4f, 0.4f, 1.0f}, 13.0f});
        return;
    }

    WidgetId scrollArea = ui.createScrollList(
        contentPanel, {0.0f, 0.0f, kContentWidth, 520.0f},
        ScrollListData{{0.12f, 0.14f, 0.18f, 0.0f}, 0.0f, 0.0f});
    {
        Widget* listWidget = ui.getWidget(scrollArea);
        if (listWidget != nullptr) {
            listWidget->padding = {4.0f, 6.0f, 4.0f, 6.0f};
            listWidget->childSpacing = 3.0f;
        }
    }

    // -- Current production queue --
    (void)ui.createPanel(scrollArea, {0.0f, 0.0f, kListWidth, 20.0f},
        PanelData{kHeaderBg, 3.0f});
    (void)ui.createLabel(scrollArea, {0.0f, -19.0f, kListWidth, 16.0f},
        LabelData{"  Production Queue", kHeaderTextColor, 11.0f});

    const aoc::sim::ProductionQueueComponent& queue = city->production();
    if (queue.queue.empty()) {
        (void)ui.createLabel(scrollArea, {0.0f, 0.0f, kListWidth, 14.0f},
            LabelData{"  (empty)", kDimTextColor, 11.0f});
    } else {
        for (const aoc::sim::ProductionQueueItem& item : queue.queue) {
            char itemBuf[128];
            std::snprintf(itemBuf, sizeof(itemBuf), "  %s   (%d / %d)",
                item.name.c_str(),
                static_cast<int>(item.progress),
                static_cast<int>(item.totalCost));
            (void)ui.createLabel(scrollArea, {0.0f, 0.0f, kListWidth, 14.0f},
                LabelData{std::string(itemBuf), kBodyTextColor, 11.0f});

            // Progress bar per queue item
            constexpr float kProdBarWidth = 470.0f;
            constexpr float kProdBarHeight = 8.0f;
            WidgetId barBg = ui.createPanel(
                scrollArea, {0.0f, 0.0f, kProdBarWidth, kProdBarHeight},
                PanelData{{0.08f, 0.08f, 0.10f, 0.9f}, 3.0f});
            {
                const float fraction = (item.totalCost > 0.0f)
                    ? std::clamp(item.progress / item.totalCost, 0.0f, 1.0f)
                    : 0.0f;
                const float fillWidth = std::max(fraction * kProdBarWidth, 2.0f);
                (void)ui.createPanel(
                    barBg, {0.0f, 0.0f, fillWidth, kProdBarHeight},
                    PanelData{kProdColor, 3.0f});
            }
        }
    }

    (void)ui.createPanel(scrollArea, {0.0f, 0.0f, kListWidth, 1.0f},
        PanelData{kSeparatorColor, 0.0f});

    // -- Buildable items --
    (void)ui.createPanel(scrollArea, {0.0f, 0.0f, kListWidth, 20.0f},
        PanelData{kHeaderBg, 3.0f});
    (void)ui.createLabel(scrollArea, {0.0f, -19.0f, kListWidth, 16.0f},
        LabelData{"  Available Items", kHeaderTextColor, 11.0f});

    const std::vector<aoc::sim::BuildableItem> buildableItems =
        aoc::sim::getBuildableItems(*this->m_gameState, this->m_player, *resolveCityByLocation(this->m_gameState, this->m_player, this->m_cityLocation));

    if (buildableItems.empty()) {
        (void)ui.createLabel(scrollArea, {0.0f, 0.0f, kListWidth, 14.0f},
            LabelData{"  Nothing available to build", kDimTextColor, 11.0f});
    }

    for (const aoc::sim::BuildableItem& buildable : buildableItems) {
        std::string itemLabel = std::string(buildable.name) + " ("
                              + std::to_string(static_cast<int>(buildable.cost)) + ")";
        if (buildable.type == aoc::sim::ProductionItemType::Wonder) {
            itemLabel += " [Wonder]";
        }

        ButtonData btn;
        btn.label = std::move(itemLabel);
        btn.fontSize = 10.0f;
        btn.cornerRadius = 3.0f;

        switch (buildable.type) {
            case aoc::sim::ProductionItemType::Unit:
                btn.normalColor  = {0.2f, 0.2f, 0.28f, 0.9f};
                btn.hoverColor   = {0.3f, 0.3f, 0.38f, 0.9f};
                btn.pressedColor = {0.15f, 0.15f, 0.2f, 0.9f};
                break;
            case aoc::sim::ProductionItemType::Building:
                btn.normalColor  = {0.2f, 0.25f, 0.2f, 0.9f};
                btn.hoverColor   = {0.3f, 0.35f, 0.3f, 0.9f};
                btn.pressedColor = {0.15f, 0.18f, 0.15f, 0.9f};
                break;
            case aoc::sim::ProductionItemType::Wonder:
                btn.normalColor  = {0.28f, 0.22f, 0.15f, 0.9f};
                btn.hoverColor   = {0.40f, 0.32f, 0.20f, 0.9f};
                btn.pressedColor = {0.20f, 0.15f, 0.10f, 0.9f};
                break;
            case aoc::sim::ProductionItemType::District:
                btn.normalColor  = {0.2f, 0.2f, 0.25f, 0.9f};
                btn.hoverColor   = {0.3f, 0.3f, 0.35f, 0.9f};
                btn.pressedColor = {0.15f, 0.15f, 0.18f, 0.9f};
                break;
        }

        const aoc::sim::ProductionItemType itemType = buildable.type;
        const uint16_t itemId = buildable.id;
        const float itemCost = buildable.cost;
        const std::string itemName(buildable.name);
        aoc::game::City* cityPtr = city;
        btn.onClick = [cityPtr, itemType, itemId, itemCost, itemName]() {
            if (cityPtr == nullptr) { return; }
            aoc::sim::ProductionQueueItem item{};
            item.type      = itemType;
            item.itemId    = itemId;
            item.name      = itemName;
            item.totalCost = itemCost;
            item.progress  = 0.0f;
            cityPtr->production().queue.push_back(std::move(item));
            LOG_INFO("Enqueued: %s", itemName.c_str());
        };

        (void)ui.createButton(scrollArea, {0.0f, 0.0f, kListWidth, 22.0f}, std::move(btn));
    }
}

// ----------------------------------------------------------------------------
// buildBuildingsTab -- districts and their buildings with bonuses
// ----------------------------------------------------------------------------

void CityDetailScreen::buildBuildingsTab(UIManager& ui, WidgetId contentPanel) {
    constexpr float kListWidth = 310.0f;
    constexpr float kContentWidth = 330.0f;
    constexpr Color kHeaderBg = {0.18f, 0.20f, 0.28f, 0.95f};
    constexpr Color kHeaderTextColor = {0.9f, 0.85f, 0.6f, 1.0f};
    constexpr Color kDimTextColor = {0.55f, 0.55f, 0.60f, 0.8f};

    const aoc::game::City* city = resolveCityByLocation(this->m_gameState, this->m_player, this->m_cityLocation);
    if (city == nullptr) {
        (void)ui.createLabel(contentPanel, {0.0f, 0.0f, kContentWidth, 16.0f},
            LabelData{"City not found", {0.8f, 0.4f, 0.4f, 1.0f}, 13.0f});
        return;
    }

    WidgetId scrollArea = ui.createScrollList(
        contentPanel, {0.0f, 0.0f, kContentWidth, 520.0f},
        ScrollListData{{0.12f, 0.14f, 0.18f, 0.0f}, 0.0f, 0.0f});
    {
        Widget* listWidget = ui.getWidget(scrollArea);
        if (listWidget != nullptr) {
            listWidget->padding = {4.0f, 6.0f, 4.0f, 6.0f};
            listWidget->childSpacing = 3.0f;
        }
    }

    (void)ui.createPanel(scrollArea, {0.0f, 0.0f, kListWidth, 20.0f},
        PanelData{kHeaderBg, 3.0f});
    (void)ui.createLabel(scrollArea, {0.0f, -19.0f, kListWidth, 16.0f},
        LabelData{"  Districts & Buildings", kHeaderTextColor, 11.0f});

    const aoc::sim::CityDistrictsComponent& districts = city->districts();

    if (!districts.districts.empty()) {
        constexpr std::array<Color, 4> kDistrictAccents = {{
            {0.20f, 0.24f, 0.38f, 0.9f},
            {0.26f, 0.20f, 0.35f, 0.9f},
            {0.20f, 0.30f, 0.24f, 0.9f},
            {0.30f, 0.24f, 0.20f, 0.9f},
        }};

        int32_t bldgProd = 0;
        int32_t bldgSci  = 0;
        int32_t bldgGold = 0;
        std::size_t districtIdx = 0;

        for (const aoc::sim::CityDistrictsComponent::PlacedDistrict& district : districts.districts) {
            const Color accentColor = kDistrictAccents[districtIdx % kDistrictAccents.size()];
            ++districtIdx;

            (void)ui.createPanel(scrollArea, {0.0f, 0.0f, kListWidth, 18.0f},
                PanelData{accentColor, 2.0f});
            const std::string districtName = std::string(aoc::sim::districtTypeName(district.type));
            (void)ui.createLabel(scrollArea, {0.0f, -17.0f, kListWidth, 14.0f},
                LabelData{"   " + districtName, {0.9f, 0.9f, 0.95f, 1.0f}, 10.0f});

            for (BuildingId bid : district.buildings) {
                const aoc::sim::BuildingDef& bdef = aoc::sim::buildingDef(bid);
                bldgProd += bdef.productionBonus;
                bldgSci  += bdef.scienceBonus;
                bldgGold += bdef.goldBonus;

                std::string bldgLine = "      " + std::string(bdef.name);
                bool hasBonus = false;
                if (bdef.productionBonus != 0) {
                    bldgLine += (hasBonus ? ", " : "  ");
                    bldgLine += "+" + std::to_string(bdef.productionBonus) + " prod";
                    hasBonus = true;
                }
                if (bdef.scienceBonus != 0) {
                    bldgLine += (hasBonus ? ", " : "  ");
                    bldgLine += "+" + std::to_string(bdef.scienceBonus) + " sci";
                    hasBonus = true;
                }
                if (bdef.goldBonus != 0) {
                    bldgLine += (hasBonus ? ", " : "  ");
                    bldgLine += "+" + std::to_string(bdef.goldBonus) + " gold";
                    hasBonus = true;
                }
                (void)ui.createLabel(scrollArea, {0.0f, 0.0f, kListWidth, 13.0f},
                    LabelData{std::move(bldgLine), {0.72f, 0.76f, 0.82f, 1.0f}, 10.0f});

                // WP-C5: recipe-preference cycle button on industrial
                // buildings that host more than one matching recipe.
                // Click cycles Auto → recipe1 → recipe2 → ... → Auto.
                if (bdef.requiredDistrict == aoc::sim::DistrictType::Industrial
                    && this->m_economy != nullptr) {
                    std::vector<uint16_t> candidates;
                    for (const aoc::sim::ProductionRecipe& r
                            : aoc::sim::allRecipes()) {
                        if (r.requiredBuilding == bid) {
                            candidates.push_back(r.recipeId);
                        }
                    }
                    if (candidates.size() >= 2) {
                        const aoc::hex::AxialCoord loc = city->location();
                        const uint32_t locHash =
                            (static_cast<uint32_t>(static_cast<uint16_t>(loc.q)) << 16)
                          | static_cast<uint32_t>(static_cast<uint16_t>(loc.r));
                        const uint16_t current = this->m_economy->recipePreference(
                            city->owner(), locHash, bid.value);

                        std::string prefLabel = "        Recipe: ";
                        if (current == 0xFFFFu) {
                            prefLabel += "Auto";
                        } else {
                            // Find the recipe name by id.
                            for (const aoc::sim::ProductionRecipe& r
                                    : aoc::sim::allRecipes()) {
                                if (r.recipeId == current) {
                                    prefLabel += std::string(r.name);
                                    break;
                                }
                            }
                        }

                        ButtonData prefBtn;
                        prefBtn.label = std::move(prefLabel);
                        prefBtn.fontSize = 9.0f;
                        prefBtn.cornerRadius = 2.0f;
                        prefBtn.normalColor  = {0.18f, 0.22f, 0.28f, 0.9f};
                        prefBtn.hoverColor   = {0.26f, 0.32f, 0.40f, 0.9f};
                        prefBtn.pressedColor = {0.14f, 0.18f, 0.22f, 0.9f};
                        aoc::sim::EconomySimulation* econ = this->m_economy;
                        const PlayerId cityOwner = city->owner();
                        const uint16_t buildingIdVal = bid.value;
                        std::vector<uint16_t> candidatesCopy = candidates;
                        const uint32_t capturedHash = locHash;
                        prefBtn.onClick = [econ, cityOwner, buildingIdVal,
                                           capturedHash, candidatesCopy]() {
                            if (econ == nullptr) { return; }
                            const uint16_t curr = econ->recipePreference(
                                cityOwner, capturedHash, buildingIdVal);
                            uint16_t next = 0xFFFFu;  // Default: Auto.
                            if (curr == 0xFFFFu) {
                                next = candidatesCopy.front();
                            } else {
                                for (std::size_t i = 0;
                                        i < candidatesCopy.size(); ++i) {
                                    if (candidatesCopy[i] == curr) {
                                        next = (i + 1 < candidatesCopy.size())
                                             ? candidatesCopy[i + 1]
                                             : 0xFFFFu;
                                        break;
                                    }
                                }
                            }
                            econ->setRecipePreference(
                                cityOwner, capturedHash, buildingIdVal, next);
                        };
                        (void)ui.createButton(scrollArea,
                            {0.0f, 0.0f, kListWidth, 16.0f}, std::move(prefBtn));
                    }
                }
            }

            if (district.buildings.empty()) {
                (void)ui.createLabel(scrollArea, {0.0f, 0.0f, kListWidth, 13.0f},
                    LabelData{"      (no buildings)", kDimTextColor, 10.0f});
            }
        }

        // Building totals summary at the bottom
        char bldgTotalBuf[128];
        std::snprintf(bldgTotalBuf, sizeof(bldgTotalBuf),
            "  Building totals: +%d prod, +%d sci, +%d gold",
            bldgProd, bldgSci, bldgGold);
        (void)ui.createLabel(scrollArea, {0.0f, 0.0f, kListWidth, 14.0f},
            LabelData{std::string(bldgTotalBuf), {0.65f, 0.7f, 0.8f, 1.0f}, 10.0f});
    } else {
        (void)ui.createLabel(scrollArea, {0.0f, 0.0f, kListWidth, 14.0f},
            LabelData{"  No districts built", kDimTextColor, 10.0f});
    }
}

// ----------------------------------------------------------------------------
// buildCitizensTab -- worked tiles, population, food surplus
// ----------------------------------------------------------------------------

void CityDetailScreen::buildCitizensTab(UIManager& ui, WidgetId contentPanel) {
    constexpr float kListWidth = 310.0f;
    constexpr float kContentWidth = 330.0f;
    constexpr Color kHeaderBg = {0.18f, 0.20f, 0.28f, 0.95f};
    constexpr Color kHeaderTextColor = {0.9f, 0.85f, 0.6f, 1.0f};
    constexpr Color kBodyTextColor = {0.78f, 0.80f, 0.82f, 1.0f};
    constexpr Color kSeparatorColor = {0.22f, 0.24f, 0.30f, 0.6f};

    aoc::game::City* city = resolveCityByLocation(this->m_gameState, this->m_player, this->m_cityLocation);
    if (city == nullptr) {
        (void)ui.createLabel(contentPanel, {0.0f, 0.0f, kContentWidth, 16.0f},
            LabelData{"City not found", {0.8f, 0.4f, 0.4f, 1.0f}, 13.0f});
        return;
    }

    WidgetId scrollArea = ui.createScrollList(
        contentPanel, {0.0f, 0.0f, kContentWidth, 520.0f},
        ScrollListData{{0.12f, 0.14f, 0.18f, 0.0f}, 0.0f, 0.0f});
    {
        Widget* listWidget = ui.getWidget(scrollArea);
        if (listWidget != nullptr) {
            listWidget->padding = {4.0f, 6.0f, 4.0f, 6.0f};
            listWidget->childSpacing = 3.0f;
        }
    }

    // -- Population and growth info --
    (void)ui.createPanel(scrollArea, {0.0f, 0.0f, kListWidth, 20.0f},
        PanelData{kHeaderBg, 3.0f});
    (void)ui.createLabel(scrollArea, {0.0f, -19.0f, kListWidth, 16.0f},
        LabelData{"  Population & Growth", kHeaderTextColor, 11.0f});

    {
        char popBuf[128];
        std::snprintf(popBuf, sizeof(popBuf),
            "  Population: %d   |   Worked tiles: %d   |   Food surplus: %.1f",
            city->population(),
            static_cast<int>(city->workedTiles().size()),
            static_cast<double>(city->foodSurplus()));
        (void)ui.createLabel(scrollArea, {0.0f, 0.0f, kListWidth, 14.0f},
            LabelData{std::string(popBuf), kBodyTextColor, 11.0f});
    }

    (void)ui.createPanel(scrollArea, {0.0f, 0.0f, kListWidth, 1.0f},
        PanelData{kSeparatorColor, 0.0f});

    // -- Manage Citizens: tile assignment --
    (void)ui.createPanel(scrollArea, {0.0f, 0.0f, kListWidth, 20.0f},
        PanelData{kHeaderBg, 3.0f});
    (void)ui.createLabel(scrollArea, {0.0f, -19.0f, kListWidth, 16.0f},
        LabelData{"  Manage Citizens", kHeaderTextColor, 11.0f});

    {
        aoc::game::Player* playerMut = this->m_gameState->player(this->m_player);

        std::vector<hex::AxialCoord> borderTiles;
        hex::spiral(city->location(), aoc::sim::CITY_WORK_RADIUS, std::back_inserter(borderTiles));

        for (const hex::AxialCoord& tile : borderTiles) {
            if (!this->m_grid->isValid(tile)) {
                continue;
            }
            const int32_t tileIdx = this->m_grid->toIndex(tile);
            if (this->m_grid->owner(tileIdx) != this->m_player) {
                continue;
            }
            if (this->m_grid->movementCost(tileIdx) == 0) {
                continue;
            }

            const aoc::map::TileYield ty = this->m_grid->tileYield(tileIdx);

            const bool isWorked = city->isTileWorked(tile);

            // Current managing city for this tile (override or nearest-city fallback).
            std::string mgmtLabel = "[auto]";
            if (playerMut != nullptr) {
                const aoc::hex::AxialCoord* ov = playerMut->tileCityOverride(tileIdx);
                if (ov != nullptr) {
                    aoc::game::City* mgmtCity = playerMut->cityAt(*ov);
                    mgmtLabel = mgmtCity ? mgmtCity->name() : std::string("?");
                }
            }

            char tileBuf[160];
            std::snprintf(tileBuf, sizeof(tileBuf),
                "%s (%d,%d)  F:%d P:%d G:%d  [%s]",
                isWorked ? "[W]" : "[ ]",
                tile.q, tile.r, ty.food, ty.production, ty.gold,
                mgmtLabel.c_str());

            ButtonData citizenBtn;
            citizenBtn.label = std::string(tileBuf);
            citizenBtn.fontSize = 10.0f;
            citizenBtn.normalColor = isWorked
                ? Color{0.12f, 0.28f, 0.14f, 0.9f}
                : Color{0.20f, 0.22f, 0.28f, 0.9f};
            citizenBtn.hoverColor = {citizenBtn.normalColor.r + 0.08f,
                                      citizenBtn.normalColor.g + 0.08f,
                                      citizenBtn.normalColor.b + 0.08f, 0.95f};
            citizenBtn.pressedColor = {citizenBtn.normalColor.r - 0.04f,
                                        citizenBtn.normalColor.g - 0.04f,
                                        citizenBtn.normalColor.b - 0.04f, 0.9f};
            citizenBtn.cornerRadius = 3.0f;

            aoc::game::City* cityPtr = city;
            const hex::AxialCoord toggleTile = tile;
            citizenBtn.onClick = [cityPtr, toggleTile]() {
                if (cityPtr == nullptr) { return; }
                cityPtr->toggleWorker(toggleTile);
                LOG_INFO("Citizen toggled on tile (%d,%d)", toggleTile.q, toggleTile.r);
            };

            (void)ui.createButton(scrollArea, {0.0f, 0.0f, kListWidth, 20.0f},
                                   std::move(citizenBtn));

            // Reassign-to-city cycle button: nearest -> city0 -> city1 -> ... -> nearest
            ButtonData assignBtn;
            assignBtn.label = std::string("   Reassign (") + mgmtLabel + ")";
            assignBtn.fontSize = 9.0f;
            assignBtn.normalColor = {0.16f, 0.18f, 0.24f, 0.85f};
            assignBtn.hoverColor = {0.24f, 0.26f, 0.34f, 0.9f};
            assignBtn.pressedColor = {0.12f, 0.14f, 0.20f, 0.85f};
            assignBtn.cornerRadius = 3.0f;

            aoc::game::Player* playerCap = playerMut;
            const int32_t captureTileIdx = tileIdx;
            assignBtn.onClick = [playerCap, captureTileIdx]() {
                if (playerCap == nullptr) { return; }
                const auto& cities = playerCap->cities();
                if (cities.empty()) { return; }
                const aoc::hex::AxialCoord* curr = playerCap->tileCityOverride(captureTileIdx);
                int32_t nextIdx = 0;
                if (curr != nullptr) {
                    for (std::size_t i = 0; i < cities.size(); ++i) {
                        if (cities[i] && cities[i]->location() == *curr) {
                            nextIdx = static_cast<int32_t>(i) + 1;
                            break;
                        }
                    }
                }
                if (nextIdx >= static_cast<int32_t>(cities.size())) {
                    playerCap->clearTileCity(captureTileIdx);
                    LOG_INFO("Tile %d -> [auto]", captureTileIdx);
                } else {
                    const aoc::hex::AxialCoord loc = cities[nextIdx]->location();
                    playerCap->setTileCity(captureTileIdx, loc);
                    LOG_INFO("Tile %d -> city at (%d,%d)", captureTileIdx, loc.q, loc.r);
                }
            };

            (void)ui.createButton(scrollArea, {0.0f, 0.0f, kListWidth, 14.0f},
                                   std::move(assignBtn));
        }
    }
}

// ----------------------------------------------------------------------------
// buildCouriersTab -- dispatch goods to other own cities via domestic couriers
// ----------------------------------------------------------------------------

void CityDetailScreen::buildCouriersTab(UIManager& ui, WidgetId contentPanel) {
    constexpr float kListWidth = 310.0f;
    constexpr float kContentWidth = 330.0f;
    constexpr Color kHeaderBg        = {0.18f, 0.20f, 0.28f, 0.95f};
    constexpr Color kHeaderTextColor = {0.9f, 0.85f, 0.6f, 1.0f};
    constexpr Color kBodyTextColor   = {0.78f, 0.80f, 0.82f, 1.0f};
    constexpr Color kDimTextColor    = {0.55f, 0.55f, 0.60f, 0.8f};
    constexpr Color kSeparatorColor  = {0.22f, 0.24f, 0.30f, 0.6f};

    aoc::game::City* city = resolveCityByLocation(this->m_gameState, this->m_player, this->m_cityLocation);
    if (city == nullptr || this->m_gameState == nullptr || this->m_grid == nullptr) {
        (void)ui.createLabel(contentPanel, {0.0f, 0.0f, kContentWidth, 16.0f},
            LabelData{"City not found", {0.8f, 0.4f, 0.4f, 1.0f}, 13.0f});
        return;
    }

    aoc::game::Player* playerPtr = this->m_gameState->player(this->m_player);
    if (playerPtr == nullptr) { return; }

    WidgetId scrollArea = ui.createScrollList(
        contentPanel, {0.0f, 0.0f, kContentWidth, 520.0f},
        ScrollListData{{0.12f, 0.14f, 0.18f, 0.0f}, 0.0f, 0.0f});
    {
        Widget* listWidget = ui.getWidget(scrollArea);
        if (listWidget != nullptr) {
            listWidget->padding = {4.0f, 6.0f, 4.0f, 6.0f};
            listWidget->childSpacing = 3.0f;
        }
    }

    const int32_t slots  = aoc::sim::courierSlots(*city);
    const int32_t active = aoc::sim::countActiveCouriersFrom(*this->m_gameState, this->m_player, city->location());
    const int32_t cap    = aoc::sim::stockpileCap(*city);

    // -- Header: slots + cap --
    (void)ui.createPanel(scrollArea, {0.0f, 0.0f, kListWidth, 20.0f}, PanelData{kHeaderBg, 3.0f});
    (void)ui.createLabel(scrollArea, {0.0f, -19.0f, kListWidth, 16.0f},
        LabelData{"  Courier Dispatch", kHeaderTextColor, 11.0f});
    {
        char buf[128];
        std::snprintf(buf, sizeof(buf),
            "  Slots: %d/%d   |   Stockpile cap: %d   |   Stage: %d",
            active, slots, cap, static_cast<int>(city->stage()));
        (void)ui.createLabel(scrollArea, {0.0f, 0.0f, kListWidth, 14.0f},
            LabelData{std::string(buf), kBodyTextColor, 11.0f});
    }

    (void)ui.createPanel(scrollArea, {0.0f, 0.0f, kListWidth, 1.0f},
        PanelData{kSeparatorColor, 0.0f});

    // -- Active outbound couriers --
    (void)ui.createPanel(scrollArea, {0.0f, 0.0f, kListWidth, 20.0f}, PanelData{kHeaderBg, 3.0f});
    (void)ui.createLabel(scrollArea, {0.0f, -19.0f, kListWidth, 16.0f},
        LabelData{"  Active Outbound", kHeaderTextColor, 11.0f});

    bool anyActive = false;
    for (const std::unique_ptr<aoc::game::Unit>& unitPtr : playerPtr->units()) {
        if (unitPtr == nullptr) { continue; }
        if (unitPtr->typeId().value != 32) { continue; }
        const aoc::sim::DomesticCourierComponent& cc = unitPtr->courier();
        if (cc.delivered) { continue; }
        if (cc.originCityLocation != city->location()) { continue; }
        anyActive = true;

        const aoc::game::City* dst = playerPtr->cityAt(cc.destCityLocation);
        const std::string_view goodName = aoc::sim::goodDef(cc.goodId).name;
        const int32_t totalHops = static_cast<int32_t>(cc.path.size());
        const int32_t progress  = cc.pathIndex;
        char buf[160];
        std::snprintf(buf, sizeof(buf),
            "  %.*s x%d -> %s  (%d/%d)",
            static_cast<int>(goodName.size()), goodName.data(), cc.quantity,
            dst ? dst->name().c_str() : "?",
            progress, totalHops - 1);
        (void)ui.createLabel(scrollArea, {0.0f, 0.0f, kListWidth, 14.0f},
            LabelData{std::string(buf), kBodyTextColor, 11.0f});
    }
    if (!anyActive) {
        (void)ui.createLabel(scrollArea, {0.0f, 0.0f, kListWidth, 14.0f},
            LabelData{"  (none)", kDimTextColor, 11.0f});
    }

    (void)ui.createPanel(scrollArea, {0.0f, 0.0f, kListWidth, 1.0f},
        PanelData{kSeparatorColor, 0.0f});

    // -- Stockpile --
    (void)ui.createPanel(scrollArea, {0.0f, 0.0f, kListWidth, 20.0f}, PanelData{kHeaderBg, 3.0f});
    (void)ui.createLabel(scrollArea, {0.0f, -19.0f, kListWidth, 16.0f},
        LabelData{"  Stockpile", kHeaderTextColor, 11.0f});

    // Snapshot goods with amount > 0. Sort by amount desc for stable display.
    std::vector<std::pair<uint16_t, int32_t>> goods;
    goods.reserve(city->stockpile().goods.size());
    for (const auto& kv : city->stockpile().goods) {
        if (kv.second > 0) { goods.emplace_back(kv.first, kv.second); }
    }
    std::sort(goods.begin(), goods.end(),
        [](const auto& a, const auto& b) { return a.second > b.second; });

    if (goods.empty()) {
        (void)ui.createLabel(scrollArea, {0.0f, 0.0f, kListWidth, 14.0f},
            LabelData{"  (empty)", kDimTextColor, 11.0f});
    }
    for (const auto& kv : goods) {
        const std::string_view goodName = aoc::sim::goodDef(kv.first).name;
        char buf[96];
        std::snprintf(buf, sizeof(buf), "  %.*s: %d / %d",
            static_cast<int>(goodName.size()), goodName.data(), kv.second, cap);
        (void)ui.createLabel(scrollArea, {0.0f, 0.0f, kListWidth, 14.0f},
            LabelData{std::string(buf), kBodyTextColor, 11.0f});
    }

    (void)ui.createPanel(scrollArea, {0.0f, 0.0f, kListWidth, 1.0f},
        PanelData{kSeparatorColor, 0.0f});

    // -- Standing orders: persistent dispatch rules for this city --
    (void)ui.createPanel(scrollArea, {0.0f, 0.0f, kListWidth, 20.0f}, PanelData{kHeaderBg, 3.0f});
    (void)ui.createLabel(scrollArea, {0.0f, -19.0f, kListWidth, 16.0f},
        LabelData{"  Standing Orders", kHeaderTextColor, 11.0f});

    if (city->standingOrders().empty()) {
        (void)ui.createLabel(scrollArea, {0.0f, 0.0f, kListWidth, 14.0f},
            LabelData{"  (none -- click + Standing below)", kDimTextColor, 11.0f});
    }
    for (std::size_t i = 0; i < city->standingOrders().size(); ++i) {
        const aoc::sim::StandingOrder& order = city->standingOrders()[i];
        const aoc::game::City* dst = playerPtr->cityAt(order.destCityLocation);
        const std::string_view goodName = aoc::sim::goodDef(order.goodId).name;
        char buf[160];
        std::snprintf(buf, sizeof(buf),
            "  Remove: %.*s x%d -> %s",
            static_cast<int>(goodName.size()), goodName.data(),
            order.batchSize,
            dst ? dst->name().c_str() : "?");

        ButtonData rmBtn;
        rmBtn.label = std::string(buf);
        rmBtn.fontSize = 10.0f;
        rmBtn.normalColor  = {0.32f, 0.18f, 0.18f, 0.9f};
        rmBtn.hoverColor   = {0.44f, 0.26f, 0.26f, 0.95f};
        rmBtn.pressedColor = {0.26f, 0.14f, 0.14f, 0.9f};
        rmBtn.cornerRadius = 3.0f;

        aoc::game::City* cityMut = city;
        const std::size_t idxCopy = i;
        rmBtn.onClick = [cityMut, idxCopy]() {
            if (cityMut != nullptr) { cityMut->removeStandingOrder(idxCopy); }
        };

        (void)ui.createButton(scrollArea, {0.0f, 0.0f, kListWidth, 18.0f},
                               std::move(rmBtn));
    }

    (void)ui.createPanel(scrollArea, {0.0f, 0.0f, kListWidth, 1.0f},
        PanelData{kSeparatorColor, 0.0f});

    // -- Dispatch buttons: one per (good, destination city) pair --
    (void)ui.createPanel(scrollArea, {0.0f, 0.0f, kListWidth, 20.0f}, PanelData{kHeaderBg, 3.0f});
    (void)ui.createLabel(scrollArea, {0.0f, -19.0f, kListWidth, 16.0f},
        LabelData{"  Dispatch", kHeaderTextColor, 11.0f});

    if (goods.empty() || playerPtr->cityCount() < 2) {
        (void)ui.createLabel(scrollArea, {0.0f, 0.0f, kListWidth, 14.0f},
            LabelData{"  (need stockpile and 2+ cities)", kDimTextColor, 11.0f});
    }

    // Per-dispatch batch size. Covers the full stockpile up to a sane cap so
    // the player doesn't spam-click for large moves.
    constexpr int32_t kBatchSize = 10;

    for (const auto& kv : goods) {
        const uint16_t goodId = kv.first;
        const int32_t available = kv.second;
        const int32_t qtyToSend = (available < kBatchSize) ? available : kBatchSize;
        const std::string_view goodName = aoc::sim::goodDef(goodId).name;

        for (const std::unique_ptr<aoc::game::City>& otherCity : playerPtr->cities()) {
            if (otherCity == nullptr) { continue; }
            if (otherCity->location() == city->location()) { continue; }

            char btnBuf[160];
            std::snprintf(btnBuf, sizeof(btnBuf),
                "  Send %d %.*s -> %s",
                qtyToSend,
                static_cast<int>(goodName.size()), goodName.data(),
                otherCity->name().c_str());

            ButtonData dispatchBtn;
            dispatchBtn.label = std::string(btnBuf);
            dispatchBtn.fontSize = 10.0f;
            dispatchBtn.normalColor  = {0.18f, 0.26f, 0.32f, 0.9f};
            dispatchBtn.hoverColor   = {0.26f, 0.36f, 0.44f, 0.95f};
            dispatchBtn.pressedColor = {0.14f, 0.20f, 0.26f, 0.9f};
            dispatchBtn.cornerRadius = 3.0f;

            aoc::game::GameState* gsPtr = this->m_gameState;
            const aoc::map::HexGrid* gridPtr = this->m_grid;
            const PlayerId owner = this->m_player;
            const aoc::hex::AxialCoord sourceLoc = city->location();
            const aoc::hex::AxialCoord destLoc   = otherCity->location();
            const int32_t qtyCap = qtyToSend;

            dispatchBtn.onClick = [gsPtr, gridPtr, owner, sourceLoc, destLoc, goodId, qtyCap]() {
                if (gsPtr == nullptr || gridPtr == nullptr) { return; }
                const bool ok = aoc::sim::dispatchCourier(
                    *gsPtr, *gridPtr, owner, sourceLoc, destLoc, goodId, qtyCap);
                if (!ok) {
                    LOG_INFO("Courier dispatch failed (no slots / no goods / no path)");
                }
            };

            (void)ui.createButton(scrollArea, {0.0f, 0.0f, kListWidth, 18.0f},
                                   std::move(dispatchBtn));

            // Companion "Make Standing" button below. Adds persistent rule so
            // the city re-dispatches this (good, batch) to the same destination
            // each turn when stockpile + slot permit.
            char standBuf[160];
            std::snprintf(standBuf, sizeof(standBuf),
                "    + Standing %d %.*s -> %s",
                qtyToSend,
                static_cast<int>(goodName.size()), goodName.data(),
                otherCity->name().c_str());

            ButtonData standBtn;
            standBtn.label = std::string(standBuf);
            standBtn.fontSize = 10.0f;
            standBtn.normalColor  = {0.20f, 0.28f, 0.20f, 0.9f};
            standBtn.hoverColor   = {0.28f, 0.38f, 0.28f, 0.95f};
            standBtn.pressedColor = {0.14f, 0.22f, 0.14f, 0.9f};
            standBtn.cornerRadius = 3.0f;

            aoc::game::City* cityMut = city;
            standBtn.onClick = [cityMut, destLoc, goodId, qtyCap]() {
                if (cityMut == nullptr) { return; }
                // Dedupe: skip if an identical order already exists.
                for (const aoc::sim::StandingOrder& o : cityMut->standingOrders()) {
                    if (o.destCityLocation == destLoc && o.goodId == goodId && o.batchSize == qtyCap) {
                        return;
                    }
                }
                cityMut->addStandingOrder(aoc::sim::StandingOrder{destLoc, goodId, qtyCap});
            };

            (void)ui.createButton(scrollArea, {0.0f, 0.0f, kListWidth, 16.0f},
                                   std::move(standBtn));
        }
    }
}

} // namespace aoc::ui
