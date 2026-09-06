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
#include "aoc/ui/StyleTokens.hpp"
#include "aoc/simulation/city/CitySiege.hpp"
#include "aoc/ui/UIManager.hpp"
#include "aoc/ui/IconAtlas.hpp"
#include "aoc/game/GameState.hpp"
#include "aoc/game/Player.hpp"
#include "aoc/game/City.hpp"
#include "aoc/game/Unit.hpp"
#include "aoc/map/HexGrid.hpp"
#include "aoc/simulation/city/CityComponent.hpp"
#include "aoc/simulation/city/ProductionQueue.hpp"
#include "aoc/simulation/city/ProductionSystem.hpp"
#include "aoc/simulation/city/CityActions.hpp"
#include "aoc/simulation/city/CityGrowth.hpp"
#include "aoc/simulation/city/Governor.hpp"
#include "aoc/simulation/government/GovernmentComponent.hpp"
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
    // Style guide §9.1: parchment surface for the city detail screen.
    // Header bar uses mahogany so titles read like leather banding atop
    // the page; resource accents use the parchment-tuned hues.
    constexpr float kContentWidth = 330.0f;
    constexpr Color kHeaderBg        = tokens::SURFACE_MAHOGANY;
    constexpr Color kHeaderTextColor = tokens::TEXT_GILT;
    constexpr Color kDimTextColor    = tokens::TEXT_DISABLED;
    constexpr Color kBodyTextColor   = tokens::TEXT_INK;
    constexpr Color kSeparatorColor  = tokens::BRONZE_DARK;
    constexpr Color kFoodColor       = tokens::RES_FOOD;
    constexpr Color kProdColor       = tokens::RES_PRODUCTION;
    constexpr Color kGoldColor       = tokens::RES_GOLD;
    constexpr Color kSciColor        = tokens::RES_SCIENCE;
    constexpr Color kCultColor       = tokens::RES_CULTURE;

    const aoc::game::City* city = resolveCityByLocation(this->m_gameState, this->m_player, this->m_cityLocation);
    if (city == nullptr) {
        (void)ui.createLabel(contentPanel, {0.0f, 0.0f, kContentWidth, 16.0f},
            LabelData{"City not found", tokens::STATE_DANGER, 13.0f});
        return;
    }

    // Scrollable area for tab content
    WidgetId scrollArea = ui.createScrollList(
        contentPanel, {0.0f, 0.0f, kContentWidth, 520.0f},
        ScrollListData{tokens::SURFACE_PARCHMENT, 0.0f, 0.0f});
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
            PanelData{tokens::SURFACE_PARCHMENT_DIM, tokens::CORNER_BUTTON});
        {
            Widget* yr = ui.getWidget(yieldRow);
            if (yr != nullptr) {
                yr->layoutDirection = LayoutDirection::HorizontalWrap;
                yr->padding = {4.0f, 6.0f, 4.0f, 6.0f};
                yr->childSpacing = 4.0f;
            }
        }

        struct YieldEntry {
            const char* iconKey;
            const char* tooltip;
            int32_t value;
            Color color;
        };
        const std::array<YieldEntry, 5> yieldEntries = {{
            {"yields.food",       "Food",       totalFood,            kFoodColor},
            {"yields.production", "Production", totalProd + bldgProd, kProdColor},
            {"yields.gold",       "Gold",       totalGold + bldgGold, kGoldColor},
            {"yields.science",    "Science",    totalSci + bldgSci,   kSciColor},
            {"yields.culture",    "Culture",    totalCult,            kCultColor},
        }};

        IconAtlas& atlas = IconAtlas::instance();
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
            IconData yieldIcon;
            yieldIcon.spriteId      = atlas.id(entry.iconKey);
            yieldIcon.fallbackColor = entry.color;
            (void)ui.createIcon(entryPanel, {0.0f, 0.0f, 14.0f, 14.0f},
                                std::move(yieldIcon));
            (void)ui.createLabel(
                entryPanel, {0.0f, 0.0f, kChipW - 22.0f, 14.0f},
                LabelData{std::to_string(entry.value), entry.color, 12.0f});
            ui.setWidgetTooltip(entryPanel,
                std::string(entry.tooltip) + ": "
                + std::to_string(entry.value) + " per turn");
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
        Color loyaltyBarColor = tokens::STATE_SUCCESS;
        switch (loyaltyStatus) {
            case aoc::sim::LoyaltyStatus::Loyal:
                loyaltyTextColor = tokens::STATE_SUCCESS;
                loyaltyBarColor = tokens::STATE_SUCCESS;
                break;
            case aoc::sim::LoyaltyStatus::Content:
                loyaltyTextColor = tokens::TEXT_INK;
                loyaltyBarColor = tokens::STATE_SUCCESS;
                break;
            case aoc::sim::LoyaltyStatus::Disloyal:
                loyaltyTextColor = tokens::STATE_WARN;
                loyaltyBarColor = tokens::STATE_WARN;
                break;
            case aoc::sim::LoyaltyStatus::Unrest:
                loyaltyTextColor = tokens::DIPLO_UNFRIENDLY;
                loyaltyBarColor = tokens::DIPLO_UNFRIENDLY;
                break;
            case aoc::sim::LoyaltyStatus::Revolt:
                loyaltyTextColor = tokens::STATE_DANGER;
                loyaltyBarColor = tokens::STATE_DANGER;
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
            PanelData{tokens::SURFACE_INK, tokens::CORNER_BUTTON});
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

        Color happyIndicatorColor = tokens::STATE_SUCCESS;
        if (netHappy < 0) {
            happyIndicatorColor = tokens::STATE_DANGER;
        } else if (netHappy == 0) {
            happyIndicatorColor = tokens::STATE_WARN;
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
            PanelData{tokens::SURFACE_INK, tokens::CORNER_BUTTON});
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

    // -- Housing, amenities, growth and completion (Civ VI readouts, 2026-09-05) --
    (void)ui.createPanel(scrollArea, {0.0f, 0.0f, kListWidth, 20.0f},
        PanelData{kHeaderBg, 3.0f});
    (void)ui.createLabel(scrollArea, {0.0f, -19.0f, kListWidth, 16.0f},
        LabelData{"  Housing & Growth", kHeaderTextColor, 11.0f});
    {
        const aoc::game::Player* ownerPlayer = this->m_gameState->player(this->m_player);
        const int32_t housing = (this->m_grid != nullptr)
            ? aoc::sim::computeCityHousing(*city, *this->m_grid) : 0;
        char housingBuf[128];
        std::snprintf(housingBuf, sizeof(housingBuf), "  Housing: %d / %d%s",
            city->population(), housing,
            (city->population() >= housing) ? "   (growth capped)" : "");
        (void)ui.createLabel(scrollArea, {0.0f, 0.0f, kListWidth, 14.0f},
            LabelData{std::string(housingBuf), kBodyTextColor, 11.0f});

        const aoc::sim::CityHappinessComponent& happy = city->happiness();
        char amenityBuf[128];
        std::snprintf(amenityBuf, sizeof(amenityBuf), "  Amenities: %.0f of %.0f needed   (%.*s)",
            static_cast<double>(happy.amenities), static_cast<double>(happy.demand),
            static_cast<int>(aoc::sim::amenityTierName(happy.happiness).size()),
            aoc::sim::amenityTierName(happy.happiness).data());
        (void)ui.createLabel(scrollArea, {0.0f, 0.0f, kListWidth, 14.0f},
            LabelData{std::string(amenityBuf), kBodyTextColor, 11.0f});

        char growthBuf[128];
        std::snprintf(growthBuf, sizeof(growthBuf), "  Food stored: %.0f / %.0f to grow",
            static_cast<double>(city->foodSurplus()),
            static_cast<double>(aoc::sim::foodForGrowth(city->population())));
        (void)ui.createLabel(scrollArea, {0.0f, 0.0f, kListWidth, 14.0f},
            LabelData{std::string(growthBuf), kBodyTextColor, 11.0f});

        if (ownerPlayer != nullptr && this->m_grid != nullptr) {
            const float perTurn = aoc::sim::cityProductionPerTurn(
                *ownerPlayer, *city, *this->m_grid, *this->m_gameState);
            const int32_t turns = aoc::sim::turnsToComplete(*city, perTurn);
            char turnsBuf[128];
            if (turns > 0) {
                std::snprintf(turnsBuf, sizeof(turnsBuf), "  Production: %.1f per turn, %d turns to complete",
                    static_cast<double>(perTurn), turns);
            } else {
                std::snprintf(turnsBuf, sizeof(turnsBuf), "  Production: %.1f per turn",
                    static_cast<double>(perTurn));
            }
            (void)ui.createLabel(scrollArea, {0.0f, 0.0f, kListWidth, 14.0f},
                LabelData{std::string(turnsBuf), kBodyTextColor, 11.0f});
        }
    }

    // -- Governor seat --
    (void)ui.createPanel(scrollArea, {0.0f, 0.0f, kListWidth, 20.0f},
        PanelData{kHeaderBg, 3.0f});
    (void)ui.createLabel(scrollArea, {0.0f, -19.0f, kListWidth, 16.0f},
        LabelData{"  Governor", kHeaderTextColor, 11.0f});
    {
        const aoc::game::Player* ownerPlayer = this->m_gameState->player(this->m_player);
        const aoc::sim::CityGovernorComponent& seat = city->governor();
        const int32_t titles = (ownerPlayer != nullptr)
            ? aoc::sim::governorTitlesAvailable(*ownerPlayer) : 0;
        std::string seatText = "  Seated: ";
        seatText += (seat.assignedGovernor == aoc::sim::GovernorType::None)
            ? std::string("nobody")
            : std::string(aoc::sim::governorTypeName(seat.assignedGovernor));
        seatText += "   Titles available: " + std::to_string(titles);
        (void)ui.createLabel(scrollArea, {0.0f, 0.0f, kListWidth, 14.0f},
            LabelData{std::move(seatText), kBodyTextColor, 11.0f});
        if (titles > 0) {
            aoc::game::GameState* gsPtr = this->m_gameState;
            const PlayerId playerId     = this->m_player;
            const hex::AxialCoord loc   = city->location();
            for (uint8_t g = 1; g < static_cast<uint8_t>(aoc::sim::GovernorType::Count); ++g) {
                const aoc::sim::GovernorType type = static_cast<aoc::sim::GovernorType>(g);
                if (type == seat.assignedGovernor) { continue; }
                ButtonData seatBtn;
                seatBtn.label        = "Seat " + std::string(aoc::sim::governorTypeName(type));
                seatBtn.fontSize     = 10.0f;
                seatBtn.cornerRadius = 3.0f;
                seatBtn.normalColor  = tokens::BRONZE_BASE;
                seatBtn.hoverColor   = tokens::BRONZE_LIGHT;
                seatBtn.pressedColor = tokens::BRONZE_DARK;
                seatBtn.onClick      = [gsPtr, playerId, loc, type]() {
                    const ErrorCode rc = aoc::sim::requestAssignGovernor(*gsPtr, playerId, loc, type);
                    if (rc != ErrorCode::Ok) {
                        LOG_WARN("Seating governor rejected: %.*s",
                                 static_cast<int>(describeError(rc).size()), describeError(rc).data());
                    }
                };
                (void)ui.createButton(scrollArea, {0.0f, 0.0f, kListWidth, 18.0f}, std::move(seatBtn));
            }
        }
    }

    // -- Fate of a conquered city: keep, raze or hand it back --
    // Only for a city someone else founded. Razing and liberating shipped with
    // a route and an MCP tool but no way to reach them while playing.
    if (city->originalOwner() != this->m_player && city->originalOwner() != INVALID_PLAYER
        && this->m_onCityDisposition) {
        (void)ui.createPanel(scrollArea, {0.0f, 0.0f, kListWidth, 1.0f},
            PanelData{tokens::BRONZE_DARK, 0.0f});
        (void)ui.createLabel(scrollArea, {0.0f, 0.0f, kListWidth, 14.0f},
            LabelData{"  Conquered city", kHeaderTextColor, 11.0f});

        const hex::AxialCoord fateLoc = city->location();
        CityDispositionCallback fate  = this->m_onCityDisposition;

        // A capital is never burned, so the button that cannot work is absent
        // rather than present and refusing.
        if (!city->isOriginalCapital()) {
            ButtonData razeBtn;
            razeBtn.label        = "Raze this city";
            razeBtn.fontSize     = 10.0f;
            razeBtn.cornerRadius = 3.0f;
            razeBtn.normalColor  = tokens::STATE_DANGER;
            razeBtn.hoverColor   = tokens::STATE_DANGER;
            razeBtn.pressedColor = tokens::BRONZE_DARK;
            razeBtn.onClick      = [fate, fateLoc]() {
                fate(fateLoc, static_cast<uint8_t>(aoc::sim::CityDisposition::Raze));
            };
            (void)ui.createButton(scrollArea, {0.0f, 0.0f, kListWidth, 18.0f}, std::move(razeBtn));
        }

        ButtonData freeBtn;
        freeBtn.label        = "Liberate to its founder";
        freeBtn.fontSize     = 10.0f;
        freeBtn.cornerRadius = 3.0f;
        freeBtn.normalColor  = tokens::BRONZE_BASE;
        freeBtn.hoverColor   = tokens::BRONZE_LIGHT;
        freeBtn.pressedColor = tokens::BRONZE_DARK;
        freeBtn.onClick      = [fate, fateLoc]() {
            fate(fateLoc, static_cast<uint8_t>(aoc::sim::CityDisposition::Liberate));
        };
        (void)ui.createButton(scrollArea, {0.0f, 0.0f, kListWidth, 18.0f}, std::move(freeBtn));
    }

    // -- Purchase hint --
    (void)ui.createPanel(scrollArea, {0.0f, 0.0f, kListWidth, 1.0f},
        PanelData{tokens::BRONZE_DARK, 0.0f});
    (void)ui.createLabel(scrollArea, {0.0f, 0.0f, kListWidth, 14.0f},
        LabelData{"  Right-click unowned tiles on the map to purchase them", kDimTextColor, 10.0f});
}

// ----------------------------------------------------------------------------
// buildProductionTab -- queue management and buildable item list
// ----------------------------------------------------------------------------

void CityDetailScreen::buildProductionTab(UIManager& ui, WidgetId contentPanel) {
    constexpr float kListWidth = 310.0f;
    constexpr float kContentWidth = 330.0f;
    constexpr Color kHeaderBg        = tokens::SURFACE_MAHOGANY;
    constexpr Color kHeaderTextColor = tokens::TEXT_GILT;
    constexpr Color kDimTextColor    = tokens::TEXT_DISABLED;
    constexpr Color kBodyTextColor   = tokens::TEXT_INK;
    constexpr Color kSeparatorColor  = tokens::BRONZE_DARK;
    constexpr Color kProdColor       = tokens::RES_PRODUCTION;

    aoc::game::City* city = resolveCityByLocation(this->m_gameState, this->m_player, this->m_cityLocation);
    if (city == nullptr) {
        (void)ui.createLabel(contentPanel, {0.0f, 0.0f, kContentWidth, 16.0f},
            LabelData{"City not found", tokens::STATE_DANGER, 13.0f});
        return;
    }

    WidgetId scrollArea = ui.createScrollList(
        contentPanel, {0.0f, 0.0f, kContentWidth, 520.0f},
        ScrollListData{tokens::SURFACE_PARCHMENT, 0.0f, 0.0f});
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
            {
                // Drop this entry (its progress is lost); through the shared request.
                const int32_t queueIndex = static_cast<int32_t>(&item - queue.queue.data());
                aoc::game::GameState* gsPtr = this->m_gameState;
                const PlayerId playerId     = this->m_player;
                const hex::AxialCoord loc   = city->location();
                ButtonData removeBtn;
                removeBtn.label        = "Remove from queue";
                removeBtn.fontSize     = 9.0f;
                removeBtn.cornerRadius = 3.0f;
                removeBtn.normalColor  = tokens::SURFACE_PARCHMENT_DIM;
                removeBtn.hoverColor   = tokens::BRONZE_LIGHT;
                removeBtn.pressedColor = tokens::BRONZE_DARK;
                removeBtn.onClick      = [gsPtr, playerId, loc, queueIndex]() {
                    const ErrorCode rc = aoc::sim::requestRemoveQueueItem(*gsPtr, playerId, loc, queueIndex);
                    if (rc != ErrorCode::Ok) {
                        LOG_WARN("Queue removal rejected: %.*s",
                                 static_cast<int>(describeError(rc).size()), describeError(rc).data());
                    }
                };
                (void)ui.createButton(scrollArea, {0.0f, 0.0f, 140.0f, 14.0f}, std::move(removeBtn));
                if (item.type == aoc::sim::ProductionItemType::District
                    && this->m_onPlaceDistrict) {
                    // Pick the tile on the map; the choice rides on this queue
                    // item until the district is finished.
                    const aoc::sim::DistrictType districtType =
                        static_cast<aoc::sim::DistrictType>(item.itemId);
                    ButtonData placeBtn;
                    placeBtn.label = item.hasTargetTile
                                         ? "Move site (" + std::to_string(item.targetTile.q) + ","
                                               + std::to_string(item.targetTile.r) + ")"
                                         : "Choose a site";
                    placeBtn.fontSize     = 9.0f;
                    placeBtn.cornerRadius = 3.0f;
                    placeBtn.normalColor  = tokens::BRONZE_BASE;
                    placeBtn.hoverColor   = tokens::BRONZE_LIGHT;
                    placeBtn.pressedColor = tokens::BRONZE_DARK;
                    const PlaceDistrictCallback callback = this->m_onPlaceDistrict;
                    placeBtn.onClick = [callback, loc, districtType]() { callback(loc, districtType); };
                    (void)ui.createButton(scrollArea, {0.0f, 0.0f, 160.0f, 14.0f}, std::move(placeBtn));
                }
            }

            // Progress bar per queue item
            constexpr float kProdBarWidth = 470.0f;
            constexpr float kProdBarHeight = 8.0f;
            WidgetId barBg = ui.createPanel(
                scrollArea, {0.0f, 0.0f, kProdBarWidth, kProdBarHeight},
                PanelData{tokens::SURFACE_INK, tokens::CORNER_BUTTON});
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
                btn.normalColor  = tokens::SURFACE_PARCHMENT_DIM;
                btn.hoverColor   = tokens::BRONZE_LIGHT;
                btn.pressedColor = tokens::BRONZE_DARK;
                break;
            case aoc::sim::ProductionItemType::Building:
                btn.normalColor  = tokens::BRONZE_BASE;
                btn.hoverColor   = tokens::BRONZE_LIGHT;
                btn.pressedColor = tokens::BRONZE_DARK;
                break;
            case aoc::sim::ProductionItemType::Wonder:
                btn.normalColor  = tokens::BRONZE_BASE;
                btn.hoverColor   = tokens::BRONZE_LIGHT;
                btn.pressedColor = tokens::BRONZE_DARK;
                break;
            case aoc::sim::ProductionItemType::Project:
            case aoc::sim::ProductionItemType::District:
                btn.normalColor  = tokens::SURFACE_PARCHMENT_DIM;
                btn.hoverColor   = tokens::BRONZE_LIGHT;
                btn.pressedColor = tokens::BRONZE_DARK;
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

        (void)ui.createButton(scrollArea, {0.0f, 0.0f, kListWidth - 120.0f, 22.0f}, std::move(btn));

        // "Build Now" — interrupt current production. Active item keeps its
        // progress field, just gets shifted to index 1; resumes after the
        // urgent item completes.
        ButtonData nowBtn;
        nowBtn.label        = "Now";
        nowBtn.fontSize     = 10.0f;
        nowBtn.cornerRadius = 3.0f;
        nowBtn.normalColor  = tokens::STATE_DANGER;
        nowBtn.hoverColor   = tokens::BRONZE_LIGHT;
        nowBtn.pressedColor = tokens::BRONZE_DARK;
        nowBtn.labelColor   = tokens::TEXT_GILT;
        nowBtn.onClick = [cityPtr, itemType, itemId, itemCost, itemName]() {
            if (cityPtr == nullptr) { return; }
            aoc::sim::ProductionQueueItem item{};
            item.type      = itemType;
            item.itemId    = itemId;
            item.name      = itemName;
            item.totalCost = itemCost;
            item.progress  = 0.0f;
            std::vector<aoc::sim::ProductionQueueItem>& q =
                cityPtr->production().queue;
            q.insert(q.begin(), std::move(item));
            LOG_INFO("Interrupted production -- now building: %s", itemName.c_str());
        };
        (void)ui.createButton(scrollArea, {0.0f, 0.0f, 56.0f, 22.0f}, std::move(nowBtn));

        // Buy with gold (units and buildings only; the request re-checks the gates).
        if (itemType == aoc::sim::ProductionItemType::Unit
            || itemType == aoc::sim::ProductionItemType::Building) {
            ButtonData buyBtn;
            buyBtn.label        = "Buy " + std::to_string(aoc::sim::purchaseCost(itemCost)) + "g";
            buyBtn.fontSize     = 10.0f;
            buyBtn.cornerRadius = 3.0f;
            buyBtn.normalColor  = tokens::BRONZE_BASE;
            buyBtn.hoverColor   = tokens::BRONZE_LIGHT;
            buyBtn.pressedColor = tokens::BRONZE_DARK;
            buyBtn.labelColor   = tokens::TEXT_GILT;
            aoc::game::GameState* gsPtr    = this->m_gameState;
            const aoc::map::HexGrid* grid  = this->m_grid;
            const PlayerId playerId        = this->m_player;
            const hex::AxialCoord loc      = city->location();
            buyBtn.onClick = [gsPtr, grid, playerId, loc, itemType, itemId]() {
                const ErrorCode rc = aoc::sim::requestPurchase(*gsPtr, grid, playerId, loc, itemType, itemId);
                if (rc != ErrorCode::Ok) {
                    LOG_WARN("Purchase rejected: %.*s",
                             static_cast<int>(describeError(rc).size()), describeError(rc).data());
                }
            };
            (void)ui.createButton(scrollArea, {0.0f, 0.0f, 60.0f, 22.0f}, std::move(buyBtn));
        }
    }

    // -- Faith and city projects --
    (void)ui.createPanel(scrollArea, {0.0f, 0.0f, kListWidth, 20.0f},
        PanelData{kHeaderBg, 3.0f});
    (void)ui.createLabel(scrollArea, {0.0f, -19.0f, kListWidth, 16.0f},
        LabelData{"  Faith & Projects", kHeaderTextColor, 11.0f});
    {
        aoc::game::GameState* gsPtr   = this->m_gameState;
        const PlayerId playerId       = this->m_player;
        const hex::AxialCoord loc     = city->location();
        const aoc::game::Player* ownerPlayer = this->m_gameState->player(this->m_player);
        const float faith = (ownerPlayer != nullptr) ? ownerPlayer->faith().faith : 0.0f;
        char faithBuf[96];
        std::snprintf(faithBuf, sizeof(faithBuf), "  Faith: %.0f", static_cast<double>(faith));
        (void)ui.createLabel(scrollArea, {0.0f, 0.0f, kListWidth, 14.0f},
            LabelData{std::string(faithBuf), kBodyTextColor, 11.0f});

        const aoc::sim::ProductionQueueItem* head = city->production().currentItem();
        if (head != nullptr && head->type == aoc::sim::ProductionItemType::Building) {
            ButtonData rushBtn;
            rushBtn.label        = "Finish " + head->name + " with faith";
            rushBtn.fontSize     = 10.0f;
            rushBtn.cornerRadius = 3.0f;
            rushBtn.normalColor  = tokens::BRONZE_BASE;
            rushBtn.hoverColor   = tokens::BRONZE_LIGHT;
            rushBtn.pressedColor = tokens::BRONZE_DARK;
            rushBtn.onClick      = [gsPtr, playerId, loc]() {
                const ErrorCode rc = aoc::sim::requestFaithRush(*gsPtr, playerId, loc);
                if (rc != ErrorCode::Ok) {
                    LOG_WARN("Faith rush rejected: %.*s",
                             static_cast<int>(describeError(rc).size()), describeError(rc).data());
                }
            };
            (void)ui.createButton(scrollArea, {0.0f, 0.0f, kListWidth, 20.0f}, std::move(rushBtn));
        }
        for (uint16_t unitId : {uint16_t{19}, uint16_t{20}, uint16_t{21}}) {
            const aoc::sim::UnitTypeDef& udef = aoc::sim::unitTypeDef(aoc::UnitTypeId{unitId});
            const int32_t cost =
                static_cast<int32_t>(aoc::sim::religiousUnitFaithCost(aoc::UnitTypeId{unitId}));
            ButtonData faithBtn;
            faithBtn.label        = "Buy " + std::string(udef.name) + " (" + std::to_string(cost) + " faith)";
            faithBtn.fontSize     = 10.0f;
            faithBtn.cornerRadius = 3.0f;
            faithBtn.normalColor  = tokens::SURFACE_PARCHMENT_DIM;
            faithBtn.hoverColor   = tokens::BRONZE_LIGHT;
            faithBtn.pressedColor = tokens::BRONZE_DARK;
            faithBtn.onClick      = [gsPtr, playerId, loc, unitId]() {
                const ErrorCode rc = aoc::sim::requestFaithPurchase(*gsPtr, playerId, loc, aoc::UnitTypeId{unitId});
                if (rc != ErrorCode::Ok) {
                    LOG_WARN("Faith purchase rejected: %.*s",
                             static_cast<int>(describeError(rc).size()), describeError(rc).data());
                }
            };
            (void)ui.createButton(scrollArea, {0.0f, 0.0f, kListWidth, 18.0f}, std::move(faithBtn));
        }

        for (const aoc::sim::CityProjectDef& pdef : aoc::sim::CITY_PROJECT_DEFS) {
            if (!aoc::sim::cityProjectAvailable(*city, pdef.type)) { continue; }
            const aoc::sim::CityProjectType project = pdef.type;
            ButtonData projBtn;
            projBtn.label        = "Queue " + std::string(pdef.name) + " (" + std::to_string(pdef.productionCost) + ")";
            projBtn.fontSize     = 10.0f;
            projBtn.cornerRadius = 3.0f;
            projBtn.normalColor  = tokens::SURFACE_PARCHMENT_DIM;
            projBtn.hoverColor   = tokens::BRONZE_LIGHT;
            projBtn.pressedColor = tokens::BRONZE_DARK;
            projBtn.onClick      = [gsPtr, playerId, loc, project]() {
                const ErrorCode rc = aoc::sim::requestQueueProject(*gsPtr, playerId, loc, project);
                if (rc != ErrorCode::Ok) {
                    LOG_WARN("Project rejected: %.*s",
                             static_cast<int>(describeError(rc).size()), describeError(rc).data());
                }
            };
            (void)ui.createButton(scrollArea, {0.0f, 0.0f, kListWidth, 20.0f}, std::move(projBtn));
        }
    }
}

// ----------------------------------------------------------------------------
// buildBuildingsTab -- districts and their buildings with bonuses
// ----------------------------------------------------------------------------

void CityDetailScreen::buildBuildingsTab(UIManager& ui, WidgetId contentPanel) {
    constexpr float kListWidth = 310.0f;
    constexpr float kContentWidth = 330.0f;
    constexpr Color kHeaderBg        = tokens::SURFACE_MAHOGANY;
    constexpr Color kHeaderTextColor = tokens::TEXT_GILT;
    constexpr Color kDimTextColor    = tokens::TEXT_DISABLED;

    const aoc::game::City* city = resolveCityByLocation(this->m_gameState, this->m_player, this->m_cityLocation);
    if (city == nullptr) {
        (void)ui.createLabel(contentPanel, {0.0f, 0.0f, kContentWidth, 16.0f},
            LabelData{"City not found", tokens::STATE_DANGER, 13.0f});
        return;
    }

    WidgetId scrollArea = ui.createScrollList(
        contentPanel, {0.0f, 0.0f, kContentWidth, 520.0f},
        ScrollListData{tokens::SURFACE_PARCHMENT, 0.0f, 0.0f});
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
        IconAtlas& atlas = IconAtlas::instance();
        const auto districtIconKey = [](aoc::sim::DistrictType dt) -> const char* {
            using DT = aoc::sim::DistrictType;
            switch (dt) {
                case DT::CityCenter: return "districts.citycenter";
                case DT::Campus:     return "districts.campus";
                case DT::Commercial: return "districts.commercial";
                case DT::Encampment: return "districts.encampment";
                case DT::Industrial: return "districts.industrial";
                case DT::HolySite:   return "districts.holysite";
                case DT::Theatre:    return "districts.theatre";
                case DT::Harbor:     return "districts.harbor";
                default:             return "buildings.unknown";
            }
        };
        const auto districtAccent = [](aoc::sim::DistrictType dt) -> Color {
            using DT = aoc::sim::DistrictType;
            switch (dt) {
                case DT::CityCenter: return tokens::BRONZE_BASE;
                case DT::Campus:     return tokens::RES_SCIENCE;
                case DT::Commercial: return tokens::RES_GOLD;
                case DT::Encampment: return tokens::DIPLO_HOSTILE;
                case DT::Industrial: return tokens::RES_PRODUCTION;
                case DT::HolySite:   return tokens::RES_FAITH;
                case DT::Theatre:    return tokens::RES_CULTURE;
                case DT::Harbor:     return tokens::DIPLO_ALLIED;
                default:             return tokens::BRONZE_BASE;
            }
        };

        int32_t bldgProd = 0;
        int32_t bldgSci  = 0;
        int32_t bldgGold = 0;

        for (const aoc::sim::CityDistrictsComponent::PlacedDistrict& district : districts.districts) {
            const Color accentColor = districtAccent(district.type);

            // District header: icon + colored bar + name
            WidgetId dhRow = ui.createPanel(scrollArea,
                {0.0f, 0.0f, kListWidth, 22.0f},
                PanelData{tokens::SURFACE_PARCHMENT_DIM, 3.0f});
            {
                Widget* dr = ui.getWidget(dhRow);
                if (dr != nullptr) {
                    dr->layoutDirection = LayoutDirection::Horizontal;
                    dr->padding = {2.0f, 4.0f, 2.0f, 4.0f};
                    dr->childSpacing = 4.0f;
                }
            }
            IconData distIcon;
            distIcon.spriteId      = atlas.id(districtIconKey(district.type));
            distIcon.fallbackColor = accentColor;
            (void)ui.createIcon(dhRow, {0.0f, 0.0f, 16.0f, 16.0f}, std::move(distIcon));
            (void)ui.createLabel(dhRow, {0.0f, 0.0f, kListWidth - 30.0f, 14.0f},
                LabelData{std::string(aoc::sim::districtTypeName(district.type)),
                          accentColor, 11.0f});

            for (BuildingId bid : district.buildings) {
                const aoc::sim::BuildingDef& bdef = aoc::sim::buildingDef(bid);
                bldgProd += bdef.productionBonus;
                bldgSci  += bdef.scienceBonus;
                bldgGold += bdef.goldBonus;

                // Building chip: icon + name + small yield strip
                WidgetId chip = ui.createPanel(scrollArea,
                    {0.0f, 0.0f, kListWidth, 18.0f},
                    PanelData{{0.0f, 0.0f, 0.0f, 0.0f}, 0.0f});
                {
                    Widget* cw = ui.getWidget(chip);
                    if (cw != nullptr) {
                        cw->layoutDirection = LayoutDirection::Horizontal;
                        cw->padding = {12.0f, 2.0f, 4.0f, 2.0f};
                        cw->childSpacing = 4.0f;
                    }
                }
                IconData bIcon;
                bIcon.spriteId      = atlas.id("buildings.commercial"); // gets overridden below
                {
                    using DT = aoc::sim::DistrictType;
                    switch (bdef.requiredDistrict) {
                        case DT::CityCenter: bIcon.spriteId = atlas.id("buildings.citycenter"); break;
                        case DT::Campus:     bIcon.spriteId = atlas.id("buildings.campus");     break;
                        case DT::Commercial: bIcon.spriteId = atlas.id("buildings.commercial"); break;
                        case DT::Encampment: bIcon.spriteId = atlas.id("buildings.encampment"); break;
                        case DT::Industrial: bIcon.spriteId = atlas.id("buildings.industrial"); break;
                        case DT::HolySite:   bIcon.spriteId = atlas.id("buildings.holysite");   break;
                        case DT::Theatre:    bIcon.spriteId = atlas.id("buildings.theatre");    break;
                        case DT::Harbor:     bIcon.spriteId = atlas.id("buildings.harbor");     break;
                        default:             bIcon.spriteId = atlas.id("buildings.unknown");    break;
                    }
                }
                bIcon.fallbackColor = accentColor;
                (void)ui.createIcon(chip, {0.0f, 0.0f, 12.0f, 12.0f}, std::move(bIcon));
                (void)ui.createLabel(chip, {0.0f, 0.0f, 130.0f, 13.0f},
                    LabelData{std::string(bdef.name), tokens::TEXT_INK, 10.0f});

                // Yield pips (icon + value, only non-zero bonuses).
                const auto yieldPip = [&](const char* iconKey, int32_t v, Color col) {
                    if (v == 0) { return; }
                    IconData yi;
                    yi.spriteId      = atlas.id(iconKey);
                    yi.fallbackColor = col;
                    (void)ui.createIcon(chip, {0.0f, 0.0f, 10.0f, 10.0f}, std::move(yi));
                    (void)ui.createLabel(chip, {0.0f, 0.0f, 26.0f, 12.0f},
                        LabelData{"+" + std::to_string(v), col, 10.0f});
                };
                yieldPip("yields.production", bdef.productionBonus, tokens::RES_PRODUCTION);
                yieldPip("yields.science",    bdef.scienceBonus,    tokens::RES_SCIENCE);
                yieldPip("yields.gold",       bdef.goldBonus,       tokens::RES_GOLD);

                // Tooltip on whole chip with full breakdown.
                ui.setWidgetTooltip(chip,
                    std::string(bdef.name)
                    + "\nDistrict: " + std::string(aoc::sim::districtTypeName(district.type))
                    + "\nMaint: " + std::to_string(bdef.maintenanceCost) + " gold");

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
                        prefBtn.normalColor  = tokens::SURFACE_PARCHMENT_DIM;
                        prefBtn.hoverColor   = tokens::BRONZE_LIGHT;
                        prefBtn.pressedColor = tokens::BRONZE_DARK;
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
            LabelData{std::string(bldgTotalBuf), tokens::TEXT_DISABLED, 10.0f});
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
    constexpr Color kHeaderBg        = tokens::SURFACE_MAHOGANY;
    constexpr Color kHeaderTextColor = tokens::TEXT_GILT;
    constexpr Color kBodyTextColor   = tokens::TEXT_INK;
    constexpr Color kSeparatorColor  = tokens::BRONZE_DARK;

    aoc::game::City* city = resolveCityByLocation(this->m_gameState, this->m_player, this->m_cityLocation);
    if (city == nullptr) {
        (void)ui.createLabel(contentPanel, {0.0f, 0.0f, kContentWidth, 16.0f},
            LabelData{"City not found", tokens::STATE_DANGER, 13.0f});
        return;
    }

    WidgetId scrollArea = ui.createScrollList(
        contentPanel, {0.0f, 0.0f, kContentWidth, 520.0f},
        ScrollListData{tokens::SURFACE_PARCHMENT, 0.0f, 0.0f});
    {
        Widget* listWidget = ui.getWidget(scrollArea);
        if (listWidget != nullptr) {
            listWidget->padding = {4.0f, 6.0f, 4.0f, 6.0f};
            listWidget->childSpacing = 3.0f;
        }
    }

    // -- Citizen focus (re-assigns worked tiles through the request) --
    (void)ui.createPanel(scrollArea, {0.0f, 0.0f, kListWidth, 20.0f},
        PanelData{kHeaderBg, 3.0f});
    (void)ui.createLabel(scrollArea, {0.0f, -19.0f, kListWidth, 16.0f},
        LabelData{"  Citizen Focus", kHeaderTextColor, 11.0f});
    {
        const aoc::sim::CityFocus current = city->governor().focus;
        (void)ui.createLabel(scrollArea, {0.0f, 0.0f, kListWidth, 14.0f},
            LabelData{"  Focus: " + std::string(aoc::sim::cityFocusName(current)), kBodyTextColor, 11.0f});
        aoc::game::GameState* gsPtr   = this->m_gameState;
        const aoc::map::HexGrid* grid = this->m_grid;
        const PlayerId playerId       = this->m_player;
        const hex::AxialCoord loc     = city->location();
        for (uint8_t f = 0; f < static_cast<uint8_t>(aoc::sim::CityFocus::Count); ++f) {
            const aoc::sim::CityFocus focus = static_cast<aoc::sim::CityFocus>(f);
            if (focus == current) { continue; }
            ButtonData focusBtn;
            focusBtn.label        = "Focus " + std::string(aoc::sim::cityFocusName(focus));
            focusBtn.fontSize     = 10.0f;
            focusBtn.cornerRadius = 3.0f;
            focusBtn.normalColor  = tokens::SURFACE_PARCHMENT_DIM;
            focusBtn.hoverColor   = tokens::BRONZE_LIGHT;
            focusBtn.pressedColor = tokens::BRONZE_DARK;
            focusBtn.onClick      = [gsPtr, grid, playerId, loc, focus]() {
                if (grid == nullptr) { return; }
                const ErrorCode rc = aoc::sim::requestSetCityFocus(*gsPtr, *grid, playerId, loc, focus);
                if (rc != ErrorCode::Ok) {
                    LOG_WARN("Focus rejected: %.*s",
                             static_cast<int>(describeError(rc).size()), describeError(rc).data());
                }
            };
            (void)ui.createButton(scrollArea, {0.0f, 0.0f, kListWidth, 18.0f}, std::move(focusBtn));
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
                ? tokens::STATE_SUCCESS
                : tokens::SURFACE_PARCHMENT_DIM;
            citizenBtn.hoverColor = {citizenBtn.normalColor.r + 0.08f,
                                      citizenBtn.normalColor.g + 0.08f,
                                      citizenBtn.normalColor.b + 0.08f, 0.95f};
            citizenBtn.pressedColor = {citizenBtn.normalColor.r - 0.04f,
                                        citizenBtn.normalColor.g - 0.04f,
                                        citizenBtn.normalColor.b - 0.04f, 0.9f};
            citizenBtn.cornerRadius = 3.0f;

            const hex::AxialCoord toggleTile = tile;
            aoc::game::GameState* gsPtr      = this->m_gameState;
            const aoc::map::HexGrid* gridPtr = this->m_grid;
            const PlayerId playerId          = this->m_player;
            const hex::AxialCoord loc        = city->location();
            citizenBtn.onClick = [gsPtr, gridPtr, playerId, loc, toggleTile]() {
                if (gridPtr == nullptr) { return; }
                const ErrorCode rc = aoc::sim::requestToggleWorkedTile(*gsPtr, *gridPtr, playerId, loc, toggleTile);
                if (rc != ErrorCode::Ok) {
                    LOG_INFO("Citizen toggle on (%d,%d) refused: %.*s", toggleTile.q, toggleTile.r,
                             static_cast<int>(describeError(rc).size()), describeError(rc).data());
                    return;
                }
                LOG_INFO("Citizen toggled on tile (%d,%d)", toggleTile.q, toggleTile.r);
            };

            (void)ui.createButton(scrollArea, {0.0f, 0.0f, kListWidth, 20.0f},
                                   std::move(citizenBtn));

            // Pin the tile against re-assignment (Civ VI tile lock).
            {
                ButtonData lockBtn;
                lockBtn.label        = city->isTileLocked(tile) ? "   Unlock tile" : "   Lock tile";
                lockBtn.fontSize     = 9.0f;
                lockBtn.cornerRadius = 3.0f;
                lockBtn.normalColor  = city->isTileLocked(tile) ? tokens::BRONZE_BASE : tokens::SURFACE_PARCHMENT_DIM;
                lockBtn.hoverColor   = tokens::BRONZE_LIGHT;
                lockBtn.pressedColor = tokens::BRONZE_DARK;
                lockBtn.onClick      = [gsPtr, playerId, loc, toggleTile]() {
                    const ErrorCode rc = aoc::sim::requestToggleTileLock(*gsPtr, playerId, loc, toggleTile);
                    if (rc != ErrorCode::Ok) {
                        LOG_WARN("Tile lock rejected: %.*s",
                                 static_cast<int>(describeError(rc).size()), describeError(rc).data());
                    }
                };
                (void)ui.createButton(scrollArea, {0.0f, 0.0f, kListWidth, 14.0f}, std::move(lockBtn));
            }

            // Reassign-to-city cycle button: nearest -> city0 -> city1 -> ... -> nearest
            ButtonData assignBtn;
            assignBtn.label = std::string("   Reassign (") + mgmtLabel + ")";
            assignBtn.fontSize = 9.0f;
            assignBtn.normalColor = tokens::SURFACE_PARCHMENT_DIM;
            assignBtn.hoverColor = tokens::BRONZE_LIGHT;
            assignBtn.pressedColor = tokens::BRONZE_DARK;
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
    constexpr Color kHeaderBg        = tokens::SURFACE_MAHOGANY;
    constexpr Color kHeaderTextColor = tokens::TEXT_GILT;
    constexpr Color kBodyTextColor   = tokens::TEXT_INK;
    constexpr Color kDimTextColor    = tokens::TEXT_DISABLED;
    constexpr Color kSeparatorColor  = tokens::BRONZE_DARK;

    aoc::game::City* city = resolveCityByLocation(this->m_gameState, this->m_player, this->m_cityLocation);
    if (city == nullptr || this->m_gameState == nullptr || this->m_grid == nullptr) {
        (void)ui.createLabel(contentPanel, {0.0f, 0.0f, kContentWidth, 16.0f},
            LabelData{"City not found", tokens::STATE_DANGER, 13.0f});
        return;
    }

    aoc::game::Player* playerPtr = this->m_gameState->player(this->m_player);
    if (playerPtr == nullptr) { return; }

    WidgetId scrollArea = ui.createScrollList(
        contentPanel, {0.0f, 0.0f, kContentWidth, 520.0f},
        ScrollListData{tokens::SURFACE_PARCHMENT, 0.0f, 0.0f});
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
        rmBtn.normalColor  = tokens::STATE_DANGER;
        rmBtn.hoverColor   = {0.767f, 0.272f, 0.197f, 1.0f};
        rmBtn.pressedColor = {0.511f, 0.182f, 0.131f, 1.0f};
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

    if (goods.empty() || playerPtr->ownedCityCount() < 2) {
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
            dispatchBtn.normalColor  = tokens::BRONZE_BASE;
            dispatchBtn.hoverColor   = tokens::BRONZE_LIGHT;
            dispatchBtn.pressedColor = tokens::BRONZE_DARK;
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
            standBtn.normalColor  = tokens::STATE_SUCCESS;
            standBtn.hoverColor   = {0.432f, 0.654f, 0.292f, 1.0f};
            standBtn.pressedColor = {0.288f, 0.436f, 0.194f, 1.0f};
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
