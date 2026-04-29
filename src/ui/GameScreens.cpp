/**
 * @file GameScreens.cpp
 * @brief Implementation of modal game screens (production, tech, government, economy, city detail).
 */

#include "aoc/ui/GameScreens.hpp"
#include "aoc/ui/StyleTokens.hpp"
#include "aoc/ui/UIManager.hpp"
#include "aoc/simulation/tech/CivicTree.hpp"
#include "aoc/ui/IconAtlas.hpp"
#include "aoc/game/GameState.hpp"
#include "aoc/game/Player.hpp"
#include "aoc/game/City.hpp"
#include "aoc/map/HexGrid.hpp"
#include "aoc/simulation/city/CityComponent.hpp"
#include "aoc/simulation/city/ProductionQueue.hpp"
#include "aoc/simulation/city/District.hpp"
#include "aoc/simulation/city/Happiness.hpp"
#include "aoc/simulation/city/CityLoyalty.hpp"
#include "aoc/simulation/tech/TechTree.hpp"
#include "aoc/simulation/tech/TechGating.hpp"
#include "aoc/simulation/tech/EurekaBoost.hpp"
#include "aoc/simulation/government/Government.hpp"
#include "aoc/simulation/government/GovernmentComponent.hpp"
#include "aoc/simulation/monetary/MonetarySystem.hpp"
#include "aoc/simulation/monetary/CurrencyTrust.hpp"
#include "aoc/simulation/resource/ResourceComponent.hpp"
#include "aoc/simulation/resource/ResourceTypes.hpp"
#include "aoc/simulation/economy/Market.hpp"
#include "aoc/simulation/economy/TradeRoute.hpp"
#include "aoc/simulation/economy/DomesticCourier.hpp"
#include "aoc/game/Unit.hpp"
#include "aoc/simulation/resource/EconomySimulation.hpp"
#include "aoc/map/Pathfinding.hpp"
#include "aoc/simulation/wonder/Wonder.hpp"
#include "aoc/simulation/map/Improvement.hpp"
#include "aoc/simulation/unit/UnitTypes.hpp"
#include "aoc/core/Log.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace aoc::ui {

// ============================================================================
// Icon-mapping helpers (TechCard, ProductionCard, …)
// Maps strong-typed game IDs to IconAtlas string keys. Keys live in
// IconAtlas::seedBuiltIns(); when the requested key is missing the
// atlas falls back to a neutral grey rect so the UI still draws.
// ============================================================================
namespace {

const char* eraIconKey(uint16_t era) {
    switch (era) {
        case 0: return "techs.ancient";
        case 1: return "techs.classical";
        case 2: return "techs.medieval";
        case 3: return "techs.renaissance";
        case 4: return "techs.industrial";
        case 5: return "techs.modern";
        case 6: return "techs.atomic";
        case 7: return "techs.information";
        default: return "techs.unknown";
    }
}

const char* techIconKey(const aoc::sim::TechDef& def) {
    // Specific overrides take priority for the iconic techs; everything
    // else falls back to its era band.
    using namespace std::string_view_literals;
    if (def.name == "Mining"sv)        { return "techs.mining"; }
    if (def.name == "Iron Working"sv)  { return "techs.iron-working"; }
    if (def.name == "Electricity"sv)   { return "techs.electricity"; }
    if (def.name == "Computers"sv)     { return "techs.computers"; }
    return eraIconKey(def.era.value);
}

const char* unitClassIconKey(aoc::sim::UnitClass cls) {
    using UC = aoc::sim::UnitClass;
    switch (cls) {
        case UC::Melee:       return "units.melee";
        case UC::Ranged:      return "units.ranged";
        case UC::Cavalry:     return "units.cavalry";
        case UC::Armor:       return "units.armor";
        case UC::Artillery:   return "units.artillery";
        case UC::AntiCavalry: return "units.anticavalry";
        case UC::Air:         return "units.air-fighter";
        case UC::Helicopter:  return "units.air-helicopter";
        case UC::Naval:       return "units.naval-melee";
        case UC::Settler:     return "units.settler";
        case UC::Scout:       return "units.recon";
        case UC::Civilian:    return "units.builder";
        case UC::Religious:   return "units.missionary";
        case UC::Trader:      return "units.trader";
        case UC::Logistics:   return "units.support";
        default:              return "units.unknown";
    }
}

const char* buildableIconKey(const aoc::sim::BuildableItem& item) {
    using PT = aoc::sim::ProductionItemType;
    switch (item.type) {
        case PT::Unit: {
            // Item id is the UnitTypeId.value packed into uint16.
            return unitClassIconKey(
                aoc::sim::unitTypeDef(UnitTypeId{item.id}).unitClass);
        }
        case PT::Building: {
            // For buildings, requiredDistrict drives the silhouette.
            const aoc::sim::BuildingDef& def = aoc::sim::buildingDef(BuildingId{item.id});
            switch (def.requiredDistrict) {
                case aoc::sim::DistrictType::CityCenter:  return "buildings.citycenter";
                case aoc::sim::DistrictType::Campus:      return "buildings.campus";
                case aoc::sim::DistrictType::Commercial:  return "buildings.commercial";
                case aoc::sim::DistrictType::Encampment:  return "buildings.encampment";
                case aoc::sim::DistrictType::Industrial:  return "buildings.industrial";
                case aoc::sim::DistrictType::HolySite:    return "buildings.holysite";
                case aoc::sim::DistrictType::Theatre:     return "buildings.theatre";
                case aoc::sim::DistrictType::Harbor:      return "buildings.harbor";
                default:                                  return "buildings.unknown";
            }
        }
        case PT::Wonder:
            return "wonders.generic";
        case PT::District:
            return "districts.citycenter";
    }
    return "buildings.unknown";
}

Color buildableAccent(aoc::sim::ProductionItemType t) {
    using PT = aoc::sim::ProductionItemType;
    switch (t) {
        case PT::Unit:     return tokens::DIPLO_HOSTILE;
        case PT::Building: return tokens::RES_PRODUCTION;
        case PT::Wonder:   return tokens::RES_GOLD;
        case PT::District: return tokens::RES_CULTURE;
    }
    return tokens::BRONZE_BASE;
}

const char* buildableTypeLabel(aoc::sim::ProductionItemType t) {
    using PT = aoc::sim::ProductionItemType;
    switch (t) {
        case PT::Unit:     return "Unit";
        case PT::Building: return "Building";
        case PT::Wonder:   return "Wonder";
        case PT::District: return "District";
    }
    return "?";
}

const char* buildingDistrictIconKey(aoc::sim::DistrictType dt) {
    using DT = aoc::sim::DistrictType;
    switch (dt) {
        case DT::CityCenter:  return "buildings.citycenter";
        case DT::Campus:      return "buildings.campus";
        case DT::Commercial:  return "buildings.commercial";
        case DT::Encampment:  return "buildings.encampment";
        case DT::Industrial:  return "buildings.industrial";
        case DT::HolySite:    return "buildings.holysite";
        case DT::Theatre:     return "buildings.theatre";
        case DT::Harbor:      return "buildings.harbor";
        default:              return "buildings.unknown";
    }
}

/// Find the eureka boost matching a given tech (linear scan, ~30 entries).
/// Returns nullptr when no boost is registered for the tech.
const aoc::sim::EurekaBoostDef* findTechEureka(TechId id) {
    if (!id.isValid()) { return nullptr; }
    for (const aoc::sim::EurekaBoostDef& b : aoc::sim::getEurekaBoosts()) {
        if (b.techId.isValid() && b.techId.value == id.value) {
            return &b;
        }
    }
    return nullptr;
}

/// Build the rich tooltip body for a tech card (name + era + cost +
/// unlock list). Multi-line via "\n" — TooltipManager handles wrapping.
std::string formatTechTooltip(const aoc::sim::TechDef& def) {
    std::string out;
    out.reserve(256);
    out.append(def.name);
    out += "\nEra ";
    out += std::to_string(def.era.value);
    out += "  ·  ";
    out += std::to_string(def.researchCost);
    out += " science";
    if (!def.unlockedUnits.empty()) {
        out += "\nUnlocks units: ";
        for (std::size_t i = 0; i < def.unlockedUnits.size(); ++i) {
            if (i > 0) { out += ", "; }
            out.append(aoc::sim::unitTypeDef(def.unlockedUnits[i]).name);
        }
    }
    if (!def.unlockedBuildings.empty()) {
        out += "\nUnlocks buildings: ";
        for (std::size_t i = 0; i < def.unlockedBuildings.size(); ++i) {
            if (i > 0) { out += ", "; }
            out.append(aoc::sim::buildingDef(def.unlockedBuildings[i]).name);
        }
    }
    if (!def.unlockedGoods.empty()) {
        out += "\nReveals ";
        out += std::to_string(def.unlockedGoods.size());
        out += " resource(s)";
    }
    if (const aoc::sim::EurekaBoostDef* eb = findTechEureka(def.id)) {
        out += "\nEureka: ";
        out.append(eb->description);
        out += "  (+";
        out += std::to_string(static_cast<int>(eb->boostFraction * 100.0f));
        out += "% progress)";
    }
    return out;
}

} // anonymous namespace

// ============================================================================
// ScreenBase
// ============================================================================

void ScreenBase::toggle(UIManager& ui) {
    if (this->m_isOpen) {
        this->close(ui);
    } else {
        this->open(ui);
    }
}

void ScreenBase::onResize(UIManager& ui, float width, float height) {
    this->m_screenW = width;
    this->m_screenH = height;
    // Rebuild only if currently open: the rebuild pattern lets each
    // concrete screen recompute layout using the new dimensions without
    // per-widget resize plumbing.
    if (this->m_isOpen) {
        this->close(ui);
        this->open(ui);
    }
}

WidgetId ScreenBase::createScreenFrame(UIManager& ui, const std::string& title,
                                        float width, float height,
                                        float screenW, float screenH) {
    // Dark semi-transparent full-screen overlay as root.
    //
    // Scissor clip intentionally NOT enabled here: in-game screen
    // rendering runs through `uiManager.transformBounds` so widget
    // bounds are world-space, but `pushScissor` expects screen-space
    // pixels. Pushing world-space bounds to the Vulkan scissor clips
    // away the whole panel (observed: inner panel + labels visible,
    // background missing). The layout-level `clampChildren` pass
    // already prevents overflow; scissor would be belt-and-suspenders
    // but needs screen-space coords first.
    // Frost-dim full-screen overlay (style guide: SURFACE_FROST_DIM under modals).
    this->m_rootPanel = ui.createPanel(
        {0.0f, 0.0f, screenW, screenH},
        PanelData{tokens::SURFACE_FROST_DIM, 0.0f});

    // Open-animation: fade in over 150ms. Starts at alpha 0 so the
    // overlay + inner panel appear smoothly rather than popping.
    {
        Widget* rw = ui.getWidget(this->m_rootPanel);
        if (rw != nullptr) {
            rw->alpha = 0.0f;
        }
    }
    ui.tweenAlpha(this->m_rootPanel, 1.0f, 0.15f);

    // Centered inner panel — rich chrome: vertical gradient from
    // slate-blue top to near-black bottom, subtle gold border, 1px
    // bright top highlight + dark bottom shadow, and a gold accent
    // ribbon on the left edge. Gives modal screens the Civ-6
    // "framed window" look without any texture art.
    const float panelX = (screenW - width) * 0.5f;
    const float panelY = (screenH - height) * 0.5f;
    // Modal panel: parchment surface, bronze border, gilt highlight,
    // mahogany-shadow underline, bronze accent ribbon on the left edge.
    PanelData innerBg;
    innerBg.backgroundColor = tokens::SURFACE_PARCHMENT;
    innerBg.gradientBottom  = tokens::SURFACE_PARCHMENT_DIM;
    innerBg.borderColor     = tokens::BRONZE_BASE;
    innerBg.borderWidth     = 1.5f;
    innerBg.topHighlight    = tokens::BRONZE_LIGHT;
    innerBg.bottomShadow    = tokens::SURFACE_MAHOGANY;
    innerBg.accentBarColor  = tokens::BRONZE_DARK;
    innerBg.accentBarWidth  = 3.0f;
    innerBg.cornerRadius    = tokens::CORNER_PANEL;
    WidgetId innerPanel = ui.createPanel(
        this->m_rootPanel,
        {panelX, panelY, width, height},
        std::move(innerBg));

    Widget* inner = ui.getWidget(innerPanel);
    if (inner != nullptr) {
        inner->padding = {10.0f, 12.0f, 10.0f, 12.0f};
        inner->childSpacing = 6.0f;
    }

    // Title label at top. Header tone with parchment outline so titles
    // read whether the modal sits over the map or on parchment fill.
    {
        LabelData ld;
        ld.text         = title;
        ld.color        = tokens::TEXT_HEADER;
        ld.fontSize     = 18.0f;
        ld.outlineColor = tokens::SURFACE_PARCHMENT;
        (void)ui.createLabel(innerPanel, {0.0f, 0.0f, width - 24.0f, 22.0f},
                       std::move(ld));
    }

    // "Close [ESC]" button at bottom-right (danger / cancel).
    ButtonData closeBtn;
    closeBtn.label = "Close [ESC]";
    closeBtn.fontSize = 12.0f;
    closeBtn.normalColor  = tokens::STATE_DANGER;
    closeBtn.hoverColor   = tokens::DIPLO_HOSTILE;
    closeBtn.pressedColor = tokens::DIPLO_AT_WAR;
    closeBtn.labelColor   = tokens::TEXT_PARCHMENT;
    closeBtn.cornerRadius = tokens::CORNER_BUTTON;
    closeBtn.onClick = [this, &ui]() {
        this->close(ui);
    };

    // Position in absolute coords relative to inner panel (bottom-right area)
    (void)ui.createButton(innerPanel,
                    {width - 124.0f, height - 50.0f, 100.0f, 28.0f},
                    std::move(closeBtn));

    return innerPanel;
}

// ============================================================================
// ProductionScreen
// ============================================================================

// Forward declaration — defined after CityDetailScreen below.
static aoc::game::City* resolveCityByLocation(aoc::game::GameState*, PlayerId, aoc::hex::AxialCoord);

void ProductionScreen::setContext(aoc::game::GameState* gameState, aoc::map::HexGrid* grid,
                                   aoc::hex::AxialCoord cityLocation, PlayerId player) {
    this->m_gameState  = gameState;
    this->m_grid       = grid;
    this->m_cityLocation = cityLocation;
    this->m_player     = player;
}

void ProductionScreen::open(UIManager& ui) {
    if (this->m_isOpen) {
        return;
    }

    assert(this->m_gameState != nullptr);
    this->m_isOpen = true;

    constexpr float SCREEN_W = 720.0f;
    constexpr float SCREEN_H = 600.0f;
    WidgetId innerPanel = this->createScreenFrame(
        ui, "Production", SCREEN_W, SCREEN_H, this->m_screenW, this->m_screenH);

    aoc::game::Player* owningPlayer = this->m_gameState->player(this->m_player);
    aoc::game::City* city = nullptr;
    if (owningPlayer != nullptr) {
        for (const std::unique_ptr<aoc::game::City>& c : owningPlayer->cities()) {
            if (c->location() == this->m_cityLocation) {
                city = c.get();
                break;
            }
        }
    }

    IconAtlas& atlas = IconAtlas::instance();

    // ----- Current production banner with progress bar -----
    {
        WidgetId banner = ui.createPanel(innerPanel,
            {0.0f, 0.0f, SCREEN_W - 24.0f, 56.0f},
            [&]() {
                PanelData pd;
                pd.backgroundColor = tokens::SURFACE_PARCHMENT;
                pd.gradientBottom  = tokens::SURFACE_PARCHMENT_DIM;
                pd.borderColor     = tokens::BRONZE_DARK;
                pd.borderWidth     = 1.0f;
                pd.cornerRadius    = tokens::CORNER_PANEL;
                pd.accentBarColor  = tokens::RES_PRODUCTION;
                pd.accentBarWidth  = 4.0f;
                return pd;
            }());
        {
            Widget* bw = ui.getWidget(banner);
            if (bw != nullptr) {
                bw->padding = {6.0f, 8.0f, 6.0f, 8.0f};
                bw->childSpacing = 3.0f;
            }
        }

        std::string title = "Idle";
        std::string costLine = "No active production";
        float fillFrac = 0.0f;
        if (city != nullptr) {
            const aoc::sim::ProductionQueueItem* current = city->production().currentItem();
            if (current != nullptr) {
                title = "Producing: " + current->name;
                costLine = std::to_string(static_cast<int>(current->progress)) + " / "
                         + std::to_string(static_cast<int>(current->totalCost)) + " prod";
                if (current->totalCost > 0.0f) {
                    fillFrac = std::clamp(current->progress / current->totalCost, 0.0f, 1.0f);
                }
            }
        }
        this->m_queueLabel = ui.createLabel(
            banner, {0.0f, 0.0f, SCREEN_W - 40.0f, 18.0f},
            LabelData{std::move(title), tokens::TEXT_HEADER, 14.0f});
        (void)ui.createLabel(
            banner, {0.0f, 0.0f, SCREEN_W - 40.0f, 14.0f},
            LabelData{std::move(costLine), tokens::TEXT_INK, 11.0f});
        ProgressBarData pb;
        pb.fillFraction = fillFrac;
        pb.cornerRadius = 3.0f;
        (void)ui.createProgressBar(banner,
            {0.0f, 0.0f, SCREEN_W - 40.0f, 8.0f}, std::move(pb));
    }

    // ----- Queue rows (drag-to-reorder kept) -----
    if (city != nullptr) {
        std::vector<aoc::sim::ProductionQueueItem>& queue = city->production().queue;
        if (queue.size() > 1) {
            (void)ui.createLabel(innerPanel, {0.0f, 0.0f, SCREEN_W - 24.0f, 14.0f},
                LabelData{"Queued (drag to reorder)", tokens::TEXT_HEADER, 12.0f});
            for (std::size_t qi = 1; qi < queue.size(); ++qi) {
                ListRowData qrow;
                qrow.title       = queue[qi].name;
                qrow.subtitle    = std::to_string(static_cast<int>(queue[qi].totalCost)) + " prod";
                qrow.rightValue  = "#" + std::to_string(qi);
                qrow.iconSpriteId = atlas.id("yields.production");
                qrow.iconSize    = 18.0f;
                qrow.titleColor    = tokens::TEXT_INK;
                qrow.subtitleColor = tokens::TEXT_DISABLED;
                qrow.valueColor    = tokens::TEXT_HEADER;
                qrow.accentColor   = tokens::RES_PRODUCTION;
                qrow.hoverBg       = tokens::SURFACE_PARCHMENT_DIM;
                qrow.pressedBg     = tokens::BRONZE_DARK;

                WidgetId rid = ui.createListRow(
                    innerPanel, {0.0f, 0.0f, SCREEN_W - 24.0f, 26.0f}, std::move(qrow));
                Widget* rw = ui.getWidget(rid);
                if (rw != nullptr) {
                    rw->canDrag     = true;
                    rw->dragPayload = static_cast<uint32_t>(qi);
                    rw->acceptsDrop = true;
                }
                std::vector<aoc::sim::ProductionQueueItem>* qPtr = &queue;
                const std::size_t targetIdx = qi;
                ui.onDrop(rid, [qPtr, targetIdx](uint32_t fromIdx) {
                    if (fromIdx == 0 || fromIdx >= qPtr->size()) { return; }
                    if (targetIdx == fromIdx) { return; }
                    std::swap((*qPtr)[fromIdx], (*qPtr)[targetIdx]);
                });
            }
        }
    }

    // ----- Buildable cards: 2-col grid -----
    (void)ui.createLabel(innerPanel, {0.0f, 0.0f, SCREEN_W - 24.0f, 14.0f},
        LabelData{"Available", tokens::TEXT_HEADER, 12.0f});

    this->m_itemList = ui.createScrollList(
        innerPanel, {0.0f, 0.0f, SCREEN_W - 24.0f, SCREEN_H - 240.0f});
    {
        Widget* listWidget = ui.getWidget(this->m_itemList);
        if (listWidget != nullptr) {
            listWidget->padding = {6.0f, 6.0f, 6.0f, 6.0f};
            listWidget->childSpacing = 8.0f;
            listWidget->gridColumns = 2;
        }
    }

    constexpr float CARD_W = 330.0f;
    constexpr float CARD_H = 78.0f;

    aoc::game::City* cityPtr = city;
    aoc::game::City* resolved = resolveCityByLocation(this->m_gameState, this->m_player, this->m_cityLocation);
    if (resolved != nullptr) {
        const std::vector<aoc::sim::BuildableItem> buildableItems =
            aoc::sim::getBuildableItems(*this->m_gameState, this->m_player, *resolved);

        for (const aoc::sim::BuildableItem& buildable : buildableItems) {
            const Color accent = buildableAccent(buildable.type);
            const Color rim    = buildable.locked ? tokens::TEXT_DISABLED : accent;

            PanelData cardBg;
            cardBg.backgroundColor = tokens::SURFACE_PARCHMENT;
            cardBg.gradientBottom  = tokens::SURFACE_PARCHMENT_DIM;
            cardBg.borderColor     = rim;
            cardBg.borderWidth     = 1.0f;
            cardBg.cornerRadius    = tokens::CORNER_PANEL;
            cardBg.accentBarColor  = rim;
            cardBg.accentBarWidth  = 3.0f;
            cardBg.topHighlight    = tokens::BRONZE_LIGHT;
            cardBg.bottomShadow    = tokens::SURFACE_MAHOGANY;

            WidgetId card = ui.createPanel(this->m_itemList,
                {0.0f, 0.0f, CARD_W, CARD_H}, std::move(cardBg));
            {
                Widget* cw = ui.getWidget(card);
                if (cw != nullptr) {
                    cw->padding = {6.0f, 8.0f, 6.0f, 8.0f};
                    cw->childSpacing = 3.0f;
                    cw->layoutDirection = LayoutDirection::Horizontal;
                }
            }
            ui.setWidgetTooltip(card,
                std::string(buildable.name) + "\n"
                + buildableTypeLabel(buildable.type) + "  ·  "
                + std::to_string(static_cast<int>(buildable.cost)) + " production"
                + (buildable.locked ? "\n(locked: prereq unmet)" : std::string{}));

            // Left: portrait icon column
            IconData portrait;
            portrait.spriteId      = atlas.id(buildableIconKey(buildable));
            portrait.fallbackColor = accent;
            portrait.tint          = buildable.locked
                ? Color{0.6f, 0.6f, 0.6f, 1.0f} : Color{1.0f, 1.0f, 1.0f, 1.0f};
            (void)ui.createIcon(card, {0.0f, 0.0f, 48.0f, 48.0f}, std::move(portrait));

            // Right: vertical text column inside its own sub-panel
            WidgetId textCol = ui.createPanel(card,
                {0.0f, 0.0f, CARD_W - 80.0f, 60.0f},
                PanelData{{0.0f, 0.0f, 0.0f, 0.0f}, 0.0f});
            {
                Widget* tc = ui.getWidget(textCol);
                if (tc != nullptr) {
                    tc->layoutDirection = LayoutDirection::Vertical;
                    tc->childSpacing = 2.0f;
                    tc->padding = {0.0f, 4.0f, 0.0f, 4.0f};
                }
            }
            const Color titleColor = buildable.locked
                ? tokens::TEXT_DISABLED : tokens::TEXT_HEADER;
            (void)ui.createLabel(textCol,
                {0.0f, 0.0f, CARD_W - 80.0f, 16.0f},
                LabelData{std::string(buildable.name), titleColor, 13.0f});

            // Type chip + cost line in a horizontal sub-row
            WidgetId metaRow = ui.createPanel(textCol,
                {0.0f, 0.0f, CARD_W - 80.0f, 16.0f},
                PanelData{{0.0f, 0.0f, 0.0f, 0.0f}, 0.0f});
            {
                Widget* mr = ui.getWidget(metaRow);
                if (mr != nullptr) {
                    mr->layoutDirection = LayoutDirection::Horizontal;
                    mr->childSpacing = 6.0f;
                }
            }
            (void)ui.createLabel(metaRow, {0.0f, 0.0f, 60.0f, 14.0f},
                LabelData{buildableTypeLabel(buildable.type), accent, 11.0f});
            IconData hammer;
            hammer.spriteId      = atlas.id("yields.production");
            hammer.fallbackColor = tokens::RES_PRODUCTION;
            (void)ui.createIcon(metaRow, {0.0f, 0.0f, 12.0f, 12.0f}, std::move(hammer));
            (void)ui.createLabel(metaRow, {0.0f, 0.0f, 60.0f, 14.0f},
                LabelData{std::to_string(static_cast<int>(buildable.cost)),
                          tokens::TEXT_HEADER, 12.0f});
            if (buildable.locked) {
                (void)ui.createLabel(metaRow, {0.0f, 0.0f, 60.0f, 14.0f},
                    LabelData{"LOCKED", tokens::STATE_DANGER, 10.0f});
            }

            // Build action — child button at bottom of text column.
            if (!buildable.locked) {
                const aoc::sim::ProductionItemType itemType = buildable.type;
                const uint16_t itemId = buildable.id;
                const float itemCost = buildable.cost;
                const std::string itemName(buildable.name);
                ButtonData btn;
                btn.label        = "Add to Queue";
                btn.fontSize     = 11.0f;
                btn.normalColor  = tokens::BRONZE_BASE;
                btn.hoverColor   = tokens::BRONZE_LIGHT;
                btn.pressedColor = tokens::STATE_PRESSED;
                btn.labelColor   = tokens::TEXT_GILT;
                btn.cornerRadius = tokens::CORNER_BUTTON;
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
                (void)ui.createButton(textCol,
                    {0.0f, 0.0f, CARD_W - 80.0f, 18.0f}, std::move(btn));
            }
        }
    }

    ui.layout();
}

void ProductionScreen::close(UIManager& ui) {
    if (!this->m_isOpen) {
        return;
    }
    this->m_isOpen = false;
    if (this->m_rootPanel != INVALID_WIDGET) {
        ui.removeWidget(this->m_rootPanel);
        this->m_rootPanel = INVALID_WIDGET;
    }
    this->m_queueLabel = INVALID_WIDGET;
    this->m_itemList = INVALID_WIDGET;
}

void ProductionScreen::refresh(UIManager& ui) {
    if (!this->m_isOpen || this->m_gameState == nullptr) {
        return;
    }

    aoc::game::Player* owningPlayer = this->m_gameState->player(this->m_player);
    aoc::game::City* city = nullptr;
    if (owningPlayer != nullptr) {
        for (const std::unique_ptr<aoc::game::City>& c : owningPlayer->cities()) {
            if (c->location() == this->m_cityLocation) {
                city = c.get();
                break;
            }
        }
    }

    std::string queueText = "Queue: empty";
    if (city != nullptr) {
        const aoc::sim::ProductionQueueItem* current = city->production().currentItem();
        if (current != nullptr) {
            queueText = "Building: " + current->name + " ("
                      + std::to_string(static_cast<int>(current->progress)) + "/"
                      + std::to_string(static_cast<int>(current->totalCost)) + ")";
        }
    }
    ui.setLabelText(this->m_queueLabel, std::move(queueText));
}

// ============================================================================
// TechScreen
// ============================================================================

void TechScreen::setContext(aoc::game::GameState* gameState, PlayerId player) {
    this->m_gameState = gameState;
    this->m_player    = player;
}

void TechScreen::open(UIManager& ui) {
    if (this->m_isOpen) {
        return;
    }

    assert(this->m_gameState != nullptr);
    this->m_isOpen = true;

    // Civ-6-style horizontal tech graph. Eight era columns, prereq lines
    // from each tech to its dependents. Cards laid out absolutely so the
    // connector lines can land precisely on card centres. No scroll —
    // 31 techs × max 7 per era fit at 100×56 cards.
    constexpr int   ERA_COUNT = 8;
    constexpr float CARD_W    = 124.0f;
    constexpr float CARD_H    = 110.0f;  // fits topRow + cost + eureka + unlocks + donut
    constexpr float COL_GAP   = 50.0f;   // horizontal gap (room for prereq lines)
    constexpr float ROW_GAP   = 8.0f;
    constexpr float ROW_PAD   = 12.0f;
    constexpr float COL_PAD   = 16.0f;
    constexpr float COL_W     = CARD_W + COL_GAP;
    constexpr float ROW_H     = CARD_H + ROW_GAP;
    const float graphW = COL_PAD * 2.0f + COL_W * static_cast<float>(ERA_COUNT) - COL_GAP;
    // Reserve canvas space for ~16 rows; the bigger graph (up to 32 rows
    // after expanded techs) lives off-screen and is reachable via pan /
    // wheel scroll on the canvas.
    const float graphH = ROW_PAD * 2.0f + ROW_H * 16.0f;

    // Wider modal so all 8 eras fit. Height capped to fit the screen;
    // the graph extends beyond visible bounds and is reachable via pan
    // / mouse-wheel scroll.
    const float SCREEN_W = std::min(graphW + 36.0f, this->m_screenW - 40.0f);
    const float SCREEN_H = std::min(graphH + 110.0f, this->m_screenH - 40.0f);
    WidgetId innerPanel = this->createScreenFrame(
        ui, "Technology", SCREEN_W, SCREEN_H, this->m_screenW, this->m_screenH);

    const aoc::game::Player* owningPlayer = this->m_gameState->player(this->m_player);
    const aoc::sim::PlayerTechComponent* playerTech =
        (owningPlayer != nullptr) ? &owningPlayer->tech() : nullptr;
    const aoc::sim::PlayerEurekaComponent* playerEureka =
        (owningPlayer != nullptr) ? &owningPlayer->eureka() : nullptr;
    // Snapshot science output once so per-card turn estimates are stable
    // while the modal is open. No grid available → 0 → labels show "?".
    float sciencePerTurn = 0.0f;
    if (owningPlayer != nullptr && this->m_grid != nullptr) {
        sciencePerTurn = owningPlayer->sciencePerTurn(*this->m_grid);
    }

    // ----- Current research banner -----
    std::string currentText = "No active research";
    if (playerTech != nullptr && playerTech->currentResearch.isValid()) {
        const aoc::sim::TechDef& def = aoc::sim::techDef(playerTech->currentResearch);
        currentText = "Researching: " + std::string(def.name)
                    + "  (" + std::to_string(static_cast<int>(playerTech->researchProgress))
                    + "/" + std::to_string(def.researchCost) + ")";
    }
    this->m_currentLabel = ui.createLabel(
        innerPanel, {0.0f, 0.0f, SCREEN_W - 24.0f, 18.0f},
        LabelData{std::move(currentText), tokens::RES_SCIENCE, 14.0f});

    // ----- Graph canvas (absolute positioning) -----
    PanelData canvasBg;
    canvasBg.backgroundColor = tokens::SURFACE_PARCHMENT_DIM;
    canvasBg.cornerRadius    = tokens::CORNER_PANEL;
    canvasBg.borderColor     = tokens::BRONZE_DARK;
    canvasBg.borderWidth     = 1.0f;
    // Cap rendered canvas size at the screen frame so wide graphs are
    // viewport-clipped; canPan + edge-scroll let the user reach the
    // hidden columns. The canvas's intrinsic content size still drives
    // child layout via panX/panY shifting.
    const float canvasW = std::min(graphW, SCREEN_W - 36.0f);
    const float canvasH = std::min(graphH, SCREEN_H - 110.0f);
    this->m_techList = ui.createPanel(innerPanel,
        {0.0f, 0.0f, canvasW, canvasH}, std::move(canvasBg));
    {
        Widget* lw = ui.getWidget(this->m_techList);
        if (lw != nullptr) {
            lw->layoutDirection = LayoutDirection::None;
            lw->padding = {0.0f, 0.0f, 0.0f, 0.0f};
            lw->canPan  = true;
            // Cards / lines are positioned absolutely beyond the
            // canvas bounds when the graph is wider than the modal;
            // clampChildren would shrink them to zero. Keep absolute
            // sizes intact and rely on panning to reach hidden cards.
            lw->clampChildren = false;
        }
    }

    // Era column labels along the top inside the canvas.
    constexpr std::array<const char*, ERA_COUNT> kEraNames = {
        "Ancient", "Classical", "Medieval", "Renaissance",
        "Industrial", "Modern", "Atomic", "Information",
    };
    for (int e = 0; e < ERA_COUNT; ++e) {
        const float colX = COL_PAD + static_cast<float>(e) * COL_W;
        WidgetId hdr = ui.createLabel(this->m_techList,
            {colX, 2.0f, CARD_W, 14.0f},
            LabelData{kEraNames[static_cast<std::size_t>(e)],
                      tokens::TEXT_HEADER, 11.0f});
        Widget* hw = ui.getWidget(hdr);
        if (hw != nullptr) { hw->anchor = Anchor::None; }
    }

    IconAtlas& atlas = IconAtlas::instance();
    const std::vector<aoc::sim::TechDef>& techs = aoc::sim::allTechs();

    // First pass: compute (eraIdx, rowIdx) per tech.
    struct CardPos { float cx = 0; float cy = 0; float left = 0; float right = 0; float yMid = 0; int row = -1; };
    std::vector<CardPos> pos(techs.size());

    // Layout pass: place each tech in its era column at a row that
    // tries to align with its primary prerequisite. Cross-era branches
    // therefore stay roughly horizontal, with vertical jogs only when
    // a slot collision forces a step. Output looks like Civ-6 with
    // multiple paths that occasionally cross instead of stacked
    // vertical columns. MAX_ROWS sized for the worst-case era density
    // after expanded techs (~15 in era 5, with headroom for growth).
    constexpr int MAX_ROWS = 32;
    std::array<std::array<bool, MAX_ROWS>, ERA_COUNT> slotTaken{};
    for (std::size_t i = 0; i < techs.size(); ++i) {
        const int eraIdx = std::clamp<int>(techs[i].era.value, 0, ERA_COUNT - 1);

        // Preferred row: average of valid prereq rows; 0 if no prereqs.
        int preferredRow = 0;
        if (!techs[i].prerequisites.empty()) {
            int rowSum = 0;
            int rowCount = 0;
            for (TechId pid : techs[i].prerequisites) {
                if (pid.isValid() && pid.value < pos.size()
                    && pos[pid.value].row >= 0) {
                    rowSum += pos[pid.value].row;
                    ++rowCount;
                }
            }
            if (rowCount > 0) {
                preferredRow = rowSum / rowCount;
            }
        }
        if (preferredRow < 0) { preferredRow = 0; }
        if (preferredRow >= MAX_ROWS) { preferredRow = MAX_ROWS - 1; }

        // Find nearest free row in the era column. Try preferred first,
        // then ±1, ±2, ... outward.
        int chosenRow = preferredRow;
        if (slotTaken[static_cast<std::size_t>(eraIdx)][static_cast<std::size_t>(chosenRow)]) {
            for (int delta = 1; delta < MAX_ROWS; ++delta) {
                const int up = preferredRow - delta;
                const int dn = preferredRow + delta;
                if (dn >= 0 && dn < MAX_ROWS
                    && !slotTaken[static_cast<std::size_t>(eraIdx)][static_cast<std::size_t>(dn)]) {
                    chosenRow = dn;
                    break;
                }
                if (up >= 0 && up < MAX_ROWS
                    && !slotTaken[static_cast<std::size_t>(eraIdx)][static_cast<std::size_t>(up)]) {
                    chosenRow = up;
                    break;
                }
            }
        }
        slotTaken[static_cast<std::size_t>(eraIdx)][static_cast<std::size_t>(chosenRow)] = true;

        const float x = COL_PAD + static_cast<float>(eraIdx) * COL_W;
        const float y = ROW_PAD + 18.0f + static_cast<float>(chosenRow) * ROW_H;
        CardPos p;
        p.cx    = x + CARD_W * 0.5f;
        p.cy    = y + CARD_H * 0.5f;
        p.left  = x;
        p.right = x + CARD_W;
        p.yMid  = y + CARD_H * 0.5f;
        p.row   = chosenRow;
        pos[i]  = p;
    }

    // Second pass: draw prereq connector lines BEFORE cards so cards
    // overdraw the line endpoints. Each edge = three thin panels:
    //   horizontal stub from prereq.right → midX,
    //   vertical from prereq.yMid → target.yMid,
    //   horizontal stub from midX → target.left.
    // Connector pass. Thick high-contrast strokes so the dependency
    // structure is impossible to miss against the dark canvas.
    constexpr float LINE_T = 4.0f;
    for (std::size_t i = 0; i < techs.size(); ++i) {
        const aoc::sim::TechDef& tech = techs[i];
        // Skip techs that didn't get a slot (out of MAX_ROWS) so we
        // don't draw lines from / to (0,0).
        if (pos[i].row < 0) { continue; }
        for (TechId prereq : tech.prerequisites) {
            if (!prereq.isValid() || prereq.value >= techs.size()) { continue; }
            if (pos[prereq.value].row < 0) { continue; }
            const CardPos& src = pos[prereq.value];
            const CardPos& dst = pos[i];

            // Color encodes player progress against this prereq edge.
            // Researched edges glow olive, the next-research bronze
            // gets the hot gilt highlight, locked stays warm bronze.
            Color lineColor = tokens::BRONZE_BASE;
            if (playerTech != nullptr) {
                if (playerTech->hasResearched(tech.id)
                    || playerTech->hasResearched(prereq)) {
                    lineColor = tokens::STATE_SUCCESS;
                } else if (playerTech->canResearch(tech.id)) {
                    lineColor = tokens::TEXT_GILT;
                }
            }
            // Backing stroke (darker shadow) drawn first to make the
            // colored fore-stroke pop against the canvas regardless
            // of the bg shade.
            const Color shadow = tokens::SURFACE_INK;
            const float SHADOW_T = LINE_T + 2.0f;

            const float midX = (src.right + dst.left) * 0.5f;
            const float vy0 = std::min(src.yMid, dst.yMid);
            const float vy1 = std::max(src.yMid, dst.yMid);
            const float hy_src = src.yMid;
            const float hy_dst = dst.yMid;

            // Helper: thin filled rect as a child of the canvas.
            const auto stroke = [&](float x, float y, float w, float h,
                                     const Color& c) {
                if (w <= 0.0f || h <= 0.0f) { return; }
                (void)ui.createPanel(this->m_techList,
                    {x, y, w, h}, PanelData{c, 0.0f});
            };

            // Shadow strokes (dark backing).
            stroke(src.right, hy_src - SHADOW_T * 0.5f,
                   midX - src.right + SHADOW_T * 0.5f, SHADOW_T, shadow);
            stroke(midX - SHADOW_T * 0.5f, vy0 - SHADOW_T * 0.5f,
                   SHADOW_T, vy1 - vy0 + SHADOW_T, shadow);
            stroke(midX - SHADOW_T * 0.5f, hy_dst - SHADOW_T * 0.5f,
                   dst.left - midX + SHADOW_T * 0.5f, SHADOW_T, shadow);

            // Foreground colored strokes on top of the shadow.
            stroke(src.right, hy_src - LINE_T * 0.5f,
                   midX - src.right, LINE_T, lineColor);
            stroke(midX - LINE_T * 0.5f, vy0,
                   LINE_T, vy1 - vy0, lineColor);
            stroke(midX, hy_dst - LINE_T * 0.5f,
                   dst.left - midX, LINE_T, lineColor);
        }
    }

    // Third pass: draw the cards themselves. Skip any tech that the
    // layout couldn't place (>32 in the same era). That's a soft
    // failure — the user-visible part stays clean even if obscure
    // expanded-content branches lose a placeholder.
    for (std::size_t i = 0; i < techs.size(); ++i) {
        if (pos[i].row < 0) { continue; }
        const aoc::sim::TechDef& tech = techs[i];
        const bool researched = playerTech && playerTech->hasResearched(tech.id);
        const bool tradeKnown = playerTech && !researched && playerTech->knows(tech.id);
        const bool available  = playerTech && !researched && !tradeKnown
                                && playerTech->canResearch(tech.id);

        Color rim;
        Color titleColor;
        if (researched)      { rim = tokens::STATE_SUCCESS;  titleColor = tokens::STATE_SUCCESS; }
        else if (tradeKnown) { rim = tokens::DIPLO_ALLIED;   titleColor = tokens::DIPLO_ALLIED; }
        else if (available)  { rim = tokens::BRONZE_BASE;    titleColor = tokens::TEXT_HEADER; }
        else                 { rim = tokens::TEXT_DISABLED;  titleColor = tokens::TEXT_DISABLED; }

        PanelData cardBg;
        cardBg.backgroundColor = tokens::SURFACE_PARCHMENT;
        cardBg.gradientBottom  = tokens::SURFACE_PARCHMENT_DIM;
        cardBg.borderColor     = rim;
        cardBg.borderWidth     = 1.0f;
        cardBg.cornerRadius    = tokens::CORNER_PANEL;
        cardBg.accentBarColor  = rim;
        cardBg.accentBarWidth  = 3.0f;
        cardBg.topHighlight    = tokens::BRONZE_LIGHT;
        cardBg.bottomShadow    = tokens::SURFACE_MAHOGANY;

        const CardPos& p = pos[i];
        WidgetId card = ui.createPanel(this->m_techList,
            {p.left, p.yMid - CARD_H * 0.5f, CARD_W, CARD_H}, std::move(cardBg));
        {
            Widget* cw = ui.getWidget(card);
            if (cw != nullptr) {
                cw->padding = {4.0f, 4.0f, 4.0f, 4.0f};
                cw->childSpacing = 2.0f;
                cw->layoutDirection = LayoutDirection::Vertical;
            }
        }
        ui.setWidgetTooltip(card, formatTechTooltip(tech));

        // Top row: icon + name (truncated to fit narrow card).
        WidgetId topRow = ui.createPanel(card,
            {0.0f, 0.0f, CARD_W - 8.0f, 18.0f},
            PanelData{{0.0f, 0.0f, 0.0f, 0.0f}, 0.0f});
        {
            Widget* tr = ui.getWidget(topRow);
            if (tr != nullptr) {
                tr->layoutDirection = LayoutDirection::Horizontal;
                tr->childSpacing = 3.0f;
            }
        }
        IconData techIcon;
        techIcon.spriteId      = atlas.id(techIconKey(tech));
        techIcon.fallbackColor = rim;
        (void)ui.createIcon(topRow, {0.0f, 0.0f, 14.0f, 14.0f}, std::move(techIcon));
        {
            LabelData ld;
            ld.text         = std::string(tech.name);
            ld.color        = titleColor;
            ld.fontSize     = 10.0f;
            ld.outlineColor = tokens::SURFACE_PARCHMENT;
            (void)ui.createLabel(topRow, {0.0f, 0.0f, CARD_W - 28.0f, 14.0f},
                                 std::move(ld));
        }

        // Cost + turn-count line.
        std::string costLabel = std::to_string(tech.researchCost) + " sci";
        if (researched) {
            costLabel = std::string(tech.name.size() > 0 ? "Researched" : "");
        } else if (sciencePerTurn > 0.5f) {
            const float remaining =
                static_cast<float>(tech.researchCost) - playerTech->researchProgress
                    * (playerTech->currentResearch.value == tech.id.value ? 1.0f : 0.0f);
            const int turns = static_cast<int>(std::ceil(remaining / sciencePerTurn));
            costLabel += "  ·  " + std::to_string(std::max(turns, 1)) + " turns";
        }
        (void)ui.createLabel(card, {0.0f, 0.0f, CARD_W - 8.0f, 12.0f},
            LabelData{std::move(costLabel), tokens::RES_SCIENCE, 9.0f});

        // Eureka chip: which condition triggers the boost + indicator
        // (lit when already triggered or pending). Skip when no boost
        // exists for this tech.
        const aoc::sim::EurekaBoostDef* eb = findTechEureka(tech.id);
        if (eb != nullptr) {
            const bool achieved = playerEureka != nullptr
                && (playerEureka->hasTriggered(eb->boostIndex)
                    || playerEureka->isPending(eb->boostIndex));
            WidgetId eRow = ui.createPanel(card,
                {0.0f, 0.0f, CARD_W - 8.0f, 12.0f},
                PanelData{{0.0f, 0.0f, 0.0f, 0.0f}, 0.0f});
            {
                Widget* er = ui.getWidget(eRow);
                if (er != nullptr) {
                    er->layoutDirection = LayoutDirection::Horizontal;
                    er->childSpacing = 3.0f;
                }
            }
            IconData eIcon;
            eIcon.spriteId      = atlas.id(achieved ? "actions.eureka" : "actions.boost");
            eIcon.fallbackColor = achieved ? tokens::TEXT_GILT : tokens::TEXT_DISABLED;
            (void)ui.createIcon(eRow, {0.0f, 0.0f, 10.0f, 10.0f}, std::move(eIcon));
            const Color textCol = achieved ? tokens::TEXT_GILT : tokens::TEXT_DISABLED;
            (void)ui.createLabel(eRow, {0.0f, 0.0f, CARD_W - 26.0f, 11.0f},
                LabelData{std::string(eb->description), textCol, 8.0f});
        }

        // Donut-shaped progress for the currently-researched tech.
        // Built from a ring of N=24 small dot-panels. Each dot is
        // either filled-azure (regular progress), filled-gilt (eureka-
        // covered portion), or muted track. The eureka segment is the
        // first `eurekaFrac * N` dots so the brighter colour reads
        // first. Sits inside a None-layout container right of the
        // unlock row so it doesn't push the stack height further.
        const bool isCurrent =
            playerTech && playerTech->currentResearch.isValid()
            && playerTech->currentResearch.value == tech.id.value;
        if (isCurrent && tech.researchCost > 0) {
            constexpr int   DOT_COUNT  = 24;
            constexpr float DOT_SIZE   = 4.0f;
            constexpr float RING_R     = 14.0f;
            constexpr float RING_BOX_W = (RING_R + DOT_SIZE) * 2.0f;
            const float frac = std::clamp(
                playerTech->researchProgress / static_cast<float>(tech.researchCost),
                0.0f, 1.0f);
            float eurekaFrac = 0.0f;
            if (eb != nullptr && playerEureka != nullptr
                && playerEureka->hasTriggered(eb->boostIndex)) {
                eurekaFrac = std::clamp(eb->boostFraction, 0.0f, frac);
            }
            const int filledDots = static_cast<int>(std::round(frac * DOT_COUNT));
            const int eurekaDots = static_cast<int>(std::round(eurekaFrac * DOT_COUNT));

            WidgetId ring = ui.createPanel(card,
                {0.0f, 0.0f, RING_BOX_W, RING_BOX_W},
                PanelData{{0.0f, 0.0f, 0.0f, 0.0f}, 0.0f});
            {
                Widget* rw = ui.getWidget(ring);
                if (rw != nullptr) {
                    rw->layoutDirection = LayoutDirection::None;
                    rw->clampChildren   = false;
                    rw->padding = {0.0f, 0.0f, 0.0f, 0.0f};
                }
            }
            const float cx = RING_R;
            const float cy = RING_R;
            for (int d = 0; d < DOT_COUNT; ++d) {
                // Start at top (-pi/2) and walk clockwise so progress
                // fills like a clock from 12 o'clock onward.
                const float ang = (static_cast<float>(d) / DOT_COUNT) * 6.2831853f
                                  - 1.5707963f;
                const float dx = cx + RING_R * std::cos(ang) - DOT_SIZE * 0.5f;
                const float dy = cy + RING_R * std::sin(ang) - DOT_SIZE * 0.5f;
                Color dotCol = tokens::SURFACE_PARCHMENT_DIM;
                if (d < eurekaDots) {
                    dotCol = tokens::TEXT_GILT;       // brighter eureka segment
                } else if (d < filledDots) {
                    dotCol = tokens::RES_SCIENCE;     // regular progress
                }
                (void)ui.createPanel(ring,
                    {dx, dy, DOT_SIZE, DOT_SIZE},
                    PanelData{dotCol, DOT_SIZE * 0.5f});
            }
        }

        // Unlock-icon row (single row of small pips).
        WidgetId unlockRow = ui.createPanel(card,
            {0.0f, 0.0f, CARD_W - 8.0f, 14.0f},
            PanelData{{0.0f, 0.0f, 0.0f, 0.0f}, 0.0f});
        {
            Widget* ur = ui.getWidget(unlockRow);
            if (ur != nullptr) {
                ur->layoutDirection = LayoutDirection::Horizontal;
                ur->childSpacing = 2.0f;
            }
        }
        constexpr float UNLOCK_ICON = 12.0f;
        for (UnitTypeId uid : tech.unlockedUnits) {
            const aoc::sim::UnitTypeDef& udef = aoc::sim::unitTypeDef(uid);
            IconData ic;
            ic.spriteId      = atlas.id(unitClassIconKey(udef.unitClass));
            ic.fallbackColor = tokens::DIPLO_HOSTILE;
            WidgetId iw = ui.createIcon(unlockRow,
                {0.0f, 0.0f, UNLOCK_ICON, UNLOCK_ICON}, std::move(ic));
            ui.setWidgetTooltip(iw,
                "Unit: " + std::string(udef.name)
                + "\nCombat " + std::to_string(udef.combatStrength)
                + (udef.rangedStrength > 0
                       ? "  · Ranged " + std::to_string(udef.rangedStrength)
                       : std::string{})
                + "\nCost " + std::to_string(udef.productionCost) + " prod");
        }
        for (BuildingId bid : tech.unlockedBuildings) {
            const aoc::sim::BuildingDef& bdef = aoc::sim::buildingDef(bid);
            IconData ic;
            ic.spriteId      = atlas.id(buildingDistrictIconKey(bdef.requiredDistrict));
            ic.fallbackColor = tokens::RES_PRODUCTION;
            WidgetId iw = ui.createIcon(unlockRow,
                {0.0f, 0.0f, UNLOCK_ICON, UNLOCK_ICON}, std::move(ic));
            ui.setWidgetTooltip(iw,
                "Building: " + std::string(bdef.name)
                + "\nCost " + std::to_string(bdef.productionCost) + " prod"
                + "  · Maint " + std::to_string(bdef.maintenanceCost) + " gold");
        }
        if (!tech.unlockedGoods.empty()) {
            IconData ic;
            ic.spriteId      = atlas.id("resources.unknown");
            ic.fallbackColor = tokens::RES_GOLD;
            WidgetId iw = ui.createIcon(unlockRow,
                {0.0f, 0.0f, UNLOCK_ICON, UNLOCK_ICON}, std::move(ic));
            ui.setWidgetTooltip(iw,
                "Reveals " + std::to_string(tech.unlockedGoods.size())
                + " resource(s)");
        }

        // Click-to-research pill on available cards.
        if (available) {
            const uint16_t techValue = tech.id.value;
            aoc::game::GameState* gsPtr = this->m_gameState;
            const PlayerId player = this->m_player;
            ButtonData rb;
            rb.label        = "Research";
            rb.fontSize     = 9.0f;
            rb.normalColor  = tokens::BRONZE_BASE;
            rb.hoverColor   = tokens::BRONZE_LIGHT;
            rb.pressedColor = tokens::STATE_PRESSED;
            rb.labelColor   = tokens::TEXT_GILT;
            rb.cornerRadius = tokens::CORNER_BUTTON;
            rb.onClick = [gsPtr, player, techValue]() {
                aoc::game::Player* p = gsPtr->player(player);
                if (p == nullptr) { return; }
                p->tech().currentResearch = TechId{techValue};
                p->tech().researchProgress = 0.0f;
                const aoc::sim::TechDef& def = aoc::sim::techDef(TechId{techValue});
                LOG_INFO("Now researching: %.*s",
                         static_cast<int>(def.name.size()), def.name.data());
            };
            (void)ui.createButton(card, {0.0f, 0.0f, CARD_W - 8.0f, 14.0f},
                                  std::move(rb));
        }
    }

    ui.layout();
}

void TechScreen::close(UIManager& ui) {
    if (!this->m_isOpen) {
        return;
    }
    this->m_isOpen = false;
    if (this->m_rootPanel != INVALID_WIDGET) {
        ui.removeWidget(this->m_rootPanel);
        this->m_rootPanel = INVALID_WIDGET;
    }
    this->m_currentLabel = INVALID_WIDGET;
    this->m_techList = INVALID_WIDGET;
}

void TechScreen::refresh(UIManager& ui) {
    if (!this->m_isOpen || this->m_gameState == nullptr) {
        return;
    }

    const aoc::game::Player* owningPlayer = this->m_gameState->player(this->m_player);
    const aoc::sim::PlayerTechComponent* playerTech =
        (owningPlayer != nullptr) ? &owningPlayer->tech() : nullptr;

    std::string currentText = "No active research";
    if (playerTech != nullptr && playerTech->currentResearch.isValid()) {
        const aoc::sim::TechDef& def = aoc::sim::techDef(playerTech->currentResearch);
        currentText = "Researching: " + std::string(def.name)
                    + " (" + std::to_string(static_cast<int>(playerTech->researchProgress))
                    + "/" + std::to_string(def.researchCost) + ")";
    }
    ui.setLabelText(this->m_currentLabel, std::move(currentText));
}

// ============================================================================
// GovernmentScreen
// ============================================================================

void GovernmentScreen::setContext(aoc::game::GameState* gameState, PlayerId player) {
    this->m_gameState = gameState;
    this->m_player    = player;
}

void GovernmentScreen::open(UIManager& ui) {
    if (this->m_isOpen) {
        return;
    }

    assert(this->m_gameState != nullptr);
    this->m_isOpen = true;

    // Wider panel to fit the new Civic Research section.
    WidgetId innerPanel = this->createScreenFrame(
        ui, "Government & Civics", 480.0f, 540.0f, this->m_screenW, this->m_screenH);

    // Find player government component through object model
    aoc::game::Player* owningPlayer = this->m_gameState->player(this->m_player);
    aoc::sim::PlayerGovernmentComponent* playerGov =
        (owningPlayer != nullptr) ? &owningPlayer->government() : nullptr;

    // Current government label
    std::string currentText = "Current: Unknown";
    if (playerGov != nullptr) {
        const aoc::sim::GovernmentDef& def = aoc::sim::governmentDef(playerGov->government);
        currentText = "Current: " + std::string(def.name);
    }
    this->m_currentGovLabel = ui.createLabel(
        innerPanel, {0.0f, 0.0f, 450.0f, 16.0f},
        LabelData{std::move(currentText), tokens::TEXT_HEADER, 14.0f});

    // Available governments section
    (void)ui.createLabel(innerPanel, {0.0f, 0.0f, 450.0f, 14.0f},
                   LabelData{"-- Available Governments --", tokens::TEXT_HEADER, 12.0f});

    this->m_govList = ui.createScrollList(
        innerPanel, {0.0f, 0.0f, 450.0f, 140.0f});

    Widget* listWidget = ui.getWidget(this->m_govList);
    if (listWidget != nullptr) {
        listWidget->padding = {4.0f, 4.0f, 4.0f, 4.0f};
        listWidget->childSpacing = 3.0f;
    }

    for (uint8_t i = 0; i < aoc::sim::GOVERNMENT_COUNT; ++i) {
        const aoc::sim::GovernmentType govType = static_cast<aoc::sim::GovernmentType>(i);
        const aoc::sim::GovernmentDef& govDef = aoc::sim::governmentDef(govType);

        if (playerGov != nullptr && playerGov->isGovernmentUnlocked(govType)) {
            ButtonData btn;
            btn.label = std::string(govDef.name);
            btn.fontSize = 12.0f;
            btn.normalColor  = tokens::BRONZE_BASE;
            btn.hoverColor   = tokens::BRONZE_LIGHT;
            btn.pressedColor = tokens::STATE_PRESSED;
            btn.labelColor   = tokens::TEXT_GILT;
            btn.cornerRadius = tokens::CORNER_BUTTON;

            aoc::game::GameState* gsPtr = this->m_gameState;
            const PlayerId player = this->m_player;
            btn.onClick = [gsPtr, player, govType]() {
                aoc::game::Player* p = gsPtr->player(player);
                if (p == nullptr) { return; }
                p->government().government = govType;
                const aoc::sim::GovernmentDef& def = aoc::sim::governmentDef(govType);
                LOG_INFO("Switched government to: %.*s",
                         static_cast<int>(def.name.size()), def.name.data());
            };

            // w=0 → auto-fill parent content width; layout clamp
            // keeps rows inside the govList regardless of re-layout.
            (void)ui.createButton(this->m_govList, {0.0f, 0.0f, 0.0f, 24.0f}, std::move(btn));
        } else {
            std::string label = std::string(govDef.name) + " (locked)";
            (void)ui.createLabel(this->m_govList, {0.0f, 0.0f, 0.0f, 18.0f},
                           LabelData{std::move(label), tokens::TEXT_DISABLED, 12.0f});
        }
    }

    // Active policies section
    (void)ui.createLabel(innerPanel, {0.0f, 0.0f, 450.0f, 14.0f},
                   LabelData{"-- Active Policies --", tokens::TEXT_HEADER, 12.0f});

    if (playerGov != nullptr) {
        // Slot count is gated by current government tier (style guide §9 +
        // sweep-2 fix): only show slots actually granted.
        const aoc::sim::GovernmentDef& govSlots =
            aoc::sim::governmentDef(playerGov->government);
        const uint8_t availableSlots = static_cast<uint8_t>(
            govSlots.militarySlots + govSlots.economicSlots
            + govSlots.diplomaticSlots + govSlots.wildcardSlots);
        for (uint8_t slot = 0; slot < availableSlots; ++slot) {
            std::string policyText;
            if (playerGov->activePolicies[slot] != aoc::sim::EMPTY_POLICY_SLOT) {
                uint8_t polId = static_cast<uint8_t>(playerGov->activePolicies[slot]);
                const aoc::sim::PolicyCardDef& polDef = aoc::sim::policyCardDef(polId);
                policyText = "Slot " + std::to_string(slot + 1) + ": "
                           + std::string(polDef.name);
            } else {
                policyText = "Slot " + std::to_string(slot + 1) + ": [Empty]";
            }
            (void)ui.createLabel(innerPanel, {0.0f, 0.0f, 450.0f, 16.0f},
                           LabelData{std::move(policyText), tokens::TEXT_INK, 12.0f});
        }
    }

    // ----------------------------------------------------------------
    // Civic Research section (style guide §9.5: civic tree, culture-purple
    // ribbon variant of the tech tree).
    // ----------------------------------------------------------------
    aoc::sim::PlayerCivicComponent* playerCivics =
        (owningPlayer != nullptr) ? &owningPlayer->civics() : nullptr;

    (void)ui.createLabel(innerPanel, {0.0f, 0.0f, 450.0f, 14.0f},
                   LabelData{"-- Civic Research --", tokens::TEXT_HEADER, 12.0f});

    std::string currentCivicText = "No active civic";
    if (playerCivics != nullptr && playerCivics->currentResearch.isValid()) {
        const aoc::sim::CivicDef& cdef = aoc::sim::civicDef(playerCivics->currentResearch);
        currentCivicText = "Researching: " + std::string(cdef.name)
                         + " (" + std::to_string(static_cast<int>(playerCivics->researchProgress))
                         + "/" + std::to_string(cdef.cultureCost) + ")";
    }
    (void)ui.createLabel(innerPanel, {0.0f, 0.0f, 450.0f, 16.0f},
                   LabelData{std::move(currentCivicText), tokens::RES_CULTURE, 13.0f});

    // Civic graph: same era-column layout as the tech tree, palette
    // shifted to mulberry/RES_CULTURE so the two trees read distinctly.
    if (playerCivics != nullptr) {
        constexpr int   ERA_COUNT = 8;
        constexpr float CARD_W    = 124.0f;
        constexpr float CARD_H    = 80.0f;
        constexpr float COL_GAP   = 50.0f;
        constexpr float ROW_GAP   = 8.0f;
        constexpr float ROW_PAD   = 12.0f;
        constexpr float COL_PAD   = 16.0f;
        constexpr float COL_W     = CARD_W + COL_GAP;
        constexpr float ROW_H     = CARD_H + ROW_GAP;
        constexpr int   MAX_ROWS  = 16;
        const float graphW =
            COL_PAD * 2.0f + COL_W * static_cast<float>(ERA_COUNT) - COL_GAP;
        const float graphH = ROW_PAD * 2.0f + ROW_H * 12.0f;

        PanelData canvasBg;
        canvasBg.backgroundColor = tokens::SURFACE_PARCHMENT_DIM;
        canvasBg.cornerRadius    = tokens::CORNER_PANEL;
        canvasBg.borderColor     = tokens::BRONZE_DARK;
        canvasBg.borderWidth     = 1.0f;
        const float canvasW = std::min(graphW, 450.0f);
        const float canvasH = std::min(graphH, 280.0f);
        WidgetId civicCanvas = ui.createPanel(innerPanel,
            {0.0f, 0.0f, canvasW, canvasH}, std::move(canvasBg));
        {
            Widget* lw = ui.getWidget(civicCanvas);
            if (lw != nullptr) {
                lw->layoutDirection = LayoutDirection::None;
                lw->padding = {0.0f, 0.0f, 0.0f, 0.0f};
                lw->canPan  = true;
                lw->clampChildren = false;
            }
        }

        constexpr std::array<const char*, ERA_COUNT> kEraNames = {
            "Ancient", "Classical", "Medieval", "Renaissance",
            "Industrial", "Modern", "Atomic", "Information",
        };
        for (int e = 0; e < ERA_COUNT; ++e) {
            const float colX = COL_PAD + static_cast<float>(e) * COL_W;
            (void)ui.createLabel(civicCanvas, {colX, 2.0f, CARD_W, 14.0f},
                LabelData{kEraNames[static_cast<std::size_t>(e)],
                          tokens::TEXT_HEADER, 11.0f});
        }

        IconAtlas& atlas = IconAtlas::instance();
        const std::vector<aoc::sim::CivicDef>& civics = aoc::sim::allCivics();

        struct CivicPos { float cx=0,cy=0,left=0,right=0,yMid=0; int row=-1; };
        std::vector<CivicPos> pos(civics.size());
        std::array<std::array<bool, MAX_ROWS>, ERA_COUNT> slotTaken{};

        for (std::size_t i = 0; i < civics.size(); ++i) {
            const int eraIdx =
                std::clamp<int>(civics[i].era.value, 0, ERA_COUNT - 1);
            int preferredRow = 0;
            if (!civics[i].prerequisites.empty()) {
                int sum = 0, n = 0;
                for (CivicId p : civics[i].prerequisites) {
                    if (p.isValid() && p.value < pos.size() && pos[p.value].row >= 0) {
                        sum += pos[p.value].row; ++n;
                    }
                }
                if (n > 0) { preferredRow = sum / n; }
            }
            preferredRow = std::clamp(preferredRow, 0, MAX_ROWS - 1);
            int chosenRow = preferredRow;
            if (slotTaken[static_cast<std::size_t>(eraIdx)][static_cast<std::size_t>(chosenRow)]) {
                for (int delta = 1; delta < MAX_ROWS; ++delta) {
                    const int dn = preferredRow + delta;
                    const int up = preferredRow - delta;
                    if (dn >= 0 && dn < MAX_ROWS
                        && !slotTaken[static_cast<std::size_t>(eraIdx)][static_cast<std::size_t>(dn)]) {
                        chosenRow = dn; break;
                    }
                    if (up >= 0 && up < MAX_ROWS
                        && !slotTaken[static_cast<std::size_t>(eraIdx)][static_cast<std::size_t>(up)]) {
                        chosenRow = up; break;
                    }
                }
            }
            slotTaken[static_cast<std::size_t>(eraIdx)][static_cast<std::size_t>(chosenRow)] = true;
            const float x = COL_PAD + static_cast<float>(eraIdx) * COL_W;
            const float y = ROW_PAD + 18.0f + static_cast<float>(chosenRow) * ROW_H;
            CivicPos p;
            p.cx = x + CARD_W * 0.5f; p.cy = y + CARD_H * 0.5f;
            p.left = x; p.right = x + CARD_W; p.yMid = y + CARD_H * 0.5f;
            p.row = chosenRow;
            pos[i] = p;
        }

        // Connector pass.
        constexpr float LINE_T  = 4.0f;
        constexpr float SHADOW_T = LINE_T + 2.0f;
        for (std::size_t i = 0; i < civics.size(); ++i) {
            if (pos[i].row < 0) { continue; }
            for (CivicId prereq : civics[i].prerequisites) {
                if (!prereq.isValid() || prereq.value >= civics.size()) { continue; }
                if (pos[prereq.value].row < 0) { continue; }
                const CivicPos& src = pos[prereq.value];
                const CivicPos& dst = pos[i];
                Color lineColor = tokens::RES_CULTURE;
                if (playerCivics->hasCompleted(civics[i].id)
                    || playerCivics->hasCompleted(prereq)) {
                    lineColor = tokens::STATE_SUCCESS;
                } else if (playerCivics->canResearch(civics[i].id)) {
                    lineColor = tokens::TEXT_GILT;
                }
                const float midX = (src.right + dst.left) * 0.5f;
                const float vy0 = std::min(src.yMid, dst.yMid);
                const float vy1 = std::max(src.yMid, dst.yMid);
                const auto stroke = [&](float x, float y, float w, float h, const Color& c) {
                    if (w <= 0.0f || h <= 0.0f) { return; }
                    (void)ui.createPanel(civicCanvas, {x, y, w, h},
                        PanelData{c, 0.0f});
                };
                // Shadow + colored.
                stroke(src.right, src.yMid - SHADOW_T * 0.5f,
                       midX - src.right + SHADOW_T * 0.5f, SHADOW_T,
                       tokens::SURFACE_INK);
                stroke(midX - SHADOW_T * 0.5f, vy0 - SHADOW_T * 0.5f,
                       SHADOW_T, vy1 - vy0 + SHADOW_T, tokens::SURFACE_INK);
                stroke(midX - SHADOW_T * 0.5f, dst.yMid - SHADOW_T * 0.5f,
                       dst.left - midX + SHADOW_T * 0.5f, SHADOW_T,
                       tokens::SURFACE_INK);
                stroke(src.right, src.yMid - LINE_T * 0.5f,
                       midX - src.right, LINE_T, lineColor);
                stroke(midX - LINE_T * 0.5f, vy0,
                       LINE_T, vy1 - vy0, lineColor);
                stroke(midX, dst.yMid - LINE_T * 0.5f,
                       dst.left - midX, LINE_T, lineColor);
            }
        }

        // Cards.
        for (std::size_t i = 0; i < civics.size(); ++i) {
            if (pos[i].row < 0) { continue; }
            const aoc::sim::CivicDef& cdef = civics[i];
            const bool completed   = playerCivics->hasCompleted(cdef.id);
            const bool researchable = !completed && playerCivics->canResearch(cdef.id);

            Color rim, titleColor;
            if (completed)        { rim = tokens::STATE_SUCCESS; titleColor = tokens::STATE_SUCCESS; }
            else if (researchable){ rim = tokens::RES_CULTURE;   titleColor = tokens::TEXT_HEADER;   }
            else                  { rim = tokens::TEXT_DISABLED; titleColor = tokens::TEXT_DISABLED; }

            PanelData cardBg;
            cardBg.backgroundColor = tokens::SURFACE_PARCHMENT;
            cardBg.gradientBottom  = tokens::SURFACE_PARCHMENT_DIM;
            cardBg.borderColor     = rim;
            cardBg.borderWidth     = 1.0f;
            cardBg.cornerRadius    = tokens::CORNER_PANEL;
            cardBg.accentBarColor  = rim;
            cardBg.accentBarWidth  = 3.0f;
            cardBg.topHighlight    = tokens::BRONZE_LIGHT;
            cardBg.bottomShadow    = tokens::SURFACE_MAHOGANY;

            const CivicPos& p = pos[i];
            WidgetId card = ui.createPanel(civicCanvas,
                {p.left, p.yMid - CARD_H * 0.5f, CARD_W, CARD_H},
                std::move(cardBg));
            {
                Widget* cw = ui.getWidget(card);
                if (cw != nullptr) {
                    cw->padding = {4.0f, 4.0f, 4.0f, 4.0f};
                    cw->childSpacing = 2.0f;
                    cw->layoutDirection = LayoutDirection::Vertical;
                }
            }
            ui.setWidgetTooltip(card,
                std::string(cdef.name) + "\nEra " + std::to_string(cdef.era.value)
                + "  ·  " + std::to_string(cdef.cultureCost) + " culture");

            // Top row: icon + name.
            WidgetId topRow = ui.createPanel(card,
                {0.0f, 0.0f, CARD_W - 8.0f, 18.0f},
                PanelData{{0.0f, 0.0f, 0.0f, 0.0f}, 0.0f});
            {
                Widget* tr = ui.getWidget(topRow);
                if (tr != nullptr) {
                    tr->layoutDirection = LayoutDirection::Horizontal;
                    tr->childSpacing = 3.0f;
                }
            }
            IconData civicIcon;
            civicIcon.spriteId      = atlas.id("civics.unknown");
            civicIcon.fallbackColor = rim;
            (void)ui.createIcon(topRow, {0.0f, 0.0f, 14.0f, 14.0f},
                                std::move(civicIcon));
            {
                LabelData ld;
                ld.text         = std::string(cdef.name);
                ld.color        = titleColor;
                ld.fontSize     = 10.0f;
                ld.outlineColor = tokens::SURFACE_PARCHMENT;
                (void)ui.createLabel(topRow, {0.0f, 0.0f, CARD_W - 28.0f, 14.0f},
                                     std::move(ld));
            }

            // Cost line.
            std::string costLabel = std::to_string(cdef.cultureCost) + " cul";
            (void)ui.createLabel(card, {0.0f, 0.0f, CARD_W - 8.0f, 12.0f},
                LabelData{std::move(costLabel), tokens::RES_CULTURE, 9.0f});

            // Status line.
            std::string statusText;
            if (completed)         { statusText = "Era " + std::to_string(cdef.era.value) + "   COMPLETED"; }
            else if (researchable) { statusText = "Era " + std::to_string(cdef.era.value) + "   AVAILABLE"; }
            else                   { statusText = "Era " + std::to_string(cdef.era.value) + "   LOCKED"; }
            (void)ui.createLabel(card, {0.0f, 0.0f, CARD_W - 8.0f, 11.0f},
                LabelData{std::move(statusText), titleColor, 9.0f});

            // Research action.
            if (researchable) {
                aoc::game::GameState* gsPtr = this->m_gameState;
                const PlayerId pid = this->m_player;
                const uint16_t civicValue = cdef.id.value;
                ButtonData rb;
                rb.label        = "Research";
                rb.fontSize     = 9.0f;
                rb.normalColor  = tokens::RES_CULTURE;
                rb.hoverColor   = tokens::BRONZE_LIGHT;
                rb.pressedColor = tokens::STATE_PRESSED;
                rb.labelColor   = tokens::TEXT_GILT;
                rb.cornerRadius = tokens::CORNER_BUTTON;
                rb.onClick = [gsPtr, pid, civicValue]() {
                    aoc::game::Player* pl = gsPtr->player(pid);
                    if (pl == nullptr) { return; }
                    pl->civics().currentResearch = CivicId{civicValue};
                    pl->civics().researchProgress = 0.0f;
                    const aoc::sim::CivicDef& def = aoc::sim::civicDef(CivicId{civicValue});
                    LOG_INFO("Now researching civic: %.*s",
                             static_cast<int>(def.name.size()), def.name.data());
                };
                (void)ui.createButton(card, {0.0f, 0.0f, CARD_W - 8.0f, 14.0f},
                                       std::move(rb));
            }
        }
    }

    ui.layout();
}

void GovernmentScreen::close(UIManager& ui) {
    if (!this->m_isOpen) {
        return;
    }
    this->m_isOpen = false;
    if (this->m_rootPanel != INVALID_WIDGET) {
        ui.removeWidget(this->m_rootPanel);
        this->m_rootPanel = INVALID_WIDGET;
    }
    this->m_currentGovLabel = INVALID_WIDGET;
    this->m_govList = INVALID_WIDGET;
}

void GovernmentScreen::refresh(UIManager& ui) {
    if (!this->m_isOpen || this->m_gameState == nullptr) {
        return;
    }

    const aoc::game::Player* owningPlayer = this->m_gameState->player(this->m_player);
    const aoc::sim::PlayerGovernmentComponent* playerGov =
        (owningPlayer != nullptr) ? &owningPlayer->government() : nullptr;

    std::string currentText = "Current: Unknown";
    if (playerGov != nullptr) {
        const aoc::sim::GovernmentDef& def = aoc::sim::governmentDef(playerGov->government);
        currentText = "Current: " + std::string(def.name);
    }
    ui.setLabelText(this->m_currentGovLabel, std::move(currentText));
}

// ============================================================================
// EconomyScreen
// ============================================================================

void EconomyScreen::setContext(aoc::game::GameState* gameState, const aoc::map::HexGrid* grid,
                                PlayerId player, const aoc::sim::Market* market) {
    this->m_gameState = gameState;
    this->m_grid      = grid;
    this->m_player    = player;
    this->m_market    = market;
}

void EconomyScreen::open(UIManager& ui) {
    if (this->m_isOpen) {
        return;
    }

    assert(this->m_gameState != nullptr);
    this->m_isOpen = true;

    WidgetId innerPanel = this->createScreenFrame(
        ui, "Economy", 500.0f, 500.0f, this->m_screenW, this->m_screenH);

    // Find monetary state through object model
    const aoc::game::Player* owningPlayer = this->m_gameState->player(this->m_player);
    const aoc::sim::MonetaryStateComponent* monetary =
        (owningPlayer != nullptr) ? &owningPlayer->monetary() : nullptr;

    // Economy info label
    std::string infoText = "No economic data";
    if (monetary != nullptr) {
        infoText = "System: " + std::string(aoc::sim::monetarySystemName(monetary->system))
                 + "  Coins: " + std::string(aoc::sim::coinTierName(monetary->effectiveCoinTier))
                 + "  Treasury: " + std::to_string(monetary->treasury)
                 + "  Money: " + std::to_string(monetary->moneySupply)
                 + "  Inflation: " + std::to_string(static_cast<int>(monetary->inflationRate * 100.0f)) + "%";
    }
    this->m_infoLabel = ui.createLabel(
        innerPanel, {0.0f, 0.0f, 470.0f, 16.0f},
        LabelData{std::move(infoText), tokens::STATE_SUCCESS, 12.0f});

    // Tax rate with +/- buttons
    if (monetary != nullptr) {
        std::string taxText = "Tax Rate: "
                            + std::to_string(static_cast<int>(monetary->taxRate * 100.0f)) + "%";
        (void)ui.createLabel(innerPanel, {0.0f, 0.0f, 250.0f, 16.0f},
                       LabelData{std::move(taxText), tokens::TEXT_HEADER, 12.0f});

        // Create a horizontal row for tax buttons
        WidgetId taxRow = ui.createPanel(
            innerPanel, {0.0f, 0.0f, 200.0f, 26.0f},
            PanelData{{0.0f, 0.0f, 0.0f, 0.0f}, 0.0f});

        Widget* taxRowWidget = ui.getWidget(taxRow);
        if (taxRowWidget != nullptr) {
            taxRowWidget->layoutDirection = LayoutDirection::Horizontal;
            taxRowWidget->childSpacing = 8.0f;
        }

        ButtonData minusBtn;
        minusBtn.label = "Tax -5%";
        minusBtn.fontSize = 11.0f;
        minusBtn.normalColor = tokens::STATE_DANGER;
        minusBtn.hoverColor = tokens::DIPLO_HOSTILE;
        minusBtn.pressedColor = tokens::DIPLO_AT_WAR;
        minusBtn.cornerRadius = 3.0f;

        aoc::game::GameState* gsPtr = this->m_gameState;
        const PlayerId player = this->m_player;
        minusBtn.onClick = [gsPtr, player]() {
            aoc::game::Player* p = gsPtr->player(player);
            if (p == nullptr) { return; }
            p->monetary().taxRate -= 0.05f;
            if (p->monetary().taxRate < 0.0f) {
                p->monetary().taxRate = 0.0f;
            }
            LOG_INFO("Tax rate: %d%%", static_cast<int>(p->monetary().taxRate * 100.0f));
        };

        ButtonData plusBtn;
        plusBtn.label = "Tax +5%";
        plusBtn.fontSize = 11.0f;
        plusBtn.normalColor = tokens::STATE_SUCCESS;
        plusBtn.hoverColor = {0.432f, 0.654f, 0.292f, 1.0f};
        plusBtn.pressedColor = {0.288f, 0.436f, 0.194f, 1.0f};
        plusBtn.cornerRadius = 3.0f;

        plusBtn.onClick = [gsPtr, player]() {
            aoc::game::Player* p = gsPtr->player(player);
            if (p == nullptr) { return; }
            p->monetary().taxRate += 0.05f;
            if (p->monetary().taxRate > 1.0f) {
                p->monetary().taxRate = 1.0f;
            }
            LOG_INFO("Tax rate: %d%%", static_cast<int>(p->monetary().taxRate * 100.0f));
        };

        (void)ui.createButton(taxRow, {0.0f, 0.0f, 80.0f, 22.0f}, std::move(minusBtn));
        (void)ui.createButton(taxRow, {0.0f, 0.0f, 80.0f, 22.0f}, std::move(plusBtn));
    }

    // "Create Trade Route" button
    {
        ButtonData tradeRouteBtn;
        tradeRouteBtn.label = "Create Trade Route";
        tradeRouteBtn.fontSize = 12.0f;
        tradeRouteBtn.normalColor  = tokens::BRONZE_BASE;
        tradeRouteBtn.hoverColor   = tokens::BRONZE_LIGHT;
        tradeRouteBtn.pressedColor = tokens::BRONZE_DARK;
        tradeRouteBtn.cornerRadius = 4.0f;
        tradeRouteBtn.onClick = [this, &ui, innerPanel]() {
            this->buildTradeRoutePanel(ui, innerPanel);
        };
        (void)ui.createButton(innerPanel, {0.0f, 0.0f, 180.0f, 26.0f}, std::move(tradeRouteBtn));
    }

    // Market prices section
    (void)ui.createLabel(innerPanel, {0.0f, 0.0f, 470.0f, 14.0f},
                   LabelData{"-- Market Prices (Trend | Supply | Demand) --",
                              tokens::TEXT_DISABLED, 12.0f});

    this->m_marketList = ui.createScrollList(
        innerPanel, {0.0f, 0.0f, 470.0f, 200.0f});

    Widget* listWidget = ui.getWidget(this->m_marketList);
    if (listWidget != nullptr) {
        listWidget->padding = {4.0f, 4.0f, 4.0f, 4.0f};
        listWidget->childSpacing = 2.0f;
    }

    // Find player economy component for supply/demand
    const aoc::sim::PlayerEconomyComponent* playerEcon =
        (owningPlayer != nullptr) ? &owningPlayer->economy() : nullptr;

    // Collect goods data for sorting by trade volume
    struct GoodDisplayInfo {
        uint16_t goodId;
        int32_t  supply;
        int32_t  demand;
        int32_t  currentPrice;
        int32_t  volume;  // supply + demand
        std::string_view name;
        std::string trend;
    };
    std::vector<GoodDisplayInfo> goodsInfo;

    const uint16_t totalGoods = aoc::sim::goodCount();
    for (uint16_t goodId = 0; goodId < totalGoods; ++goodId) {
        const aoc::sim::GoodDef& gDef = aoc::sim::goodDef(goodId);
        if (gDef.basePrice <= 0) {
            continue;
        }

        GoodDisplayInfo info{};
        info.goodId = goodId;
        info.name = gDef.name;

        // Get current market price from Market if available
        if (this->m_market != nullptr) {
            info.currentPrice = this->m_market->price(goodId);

            // Compute price trend from priceHistory
            const aoc::sim::Market::GoodMarketData& mdata = this->m_market->marketData(goodId);
            constexpr int32_t HISTORY_SIZE = aoc::sim::Market::GoodMarketData::HISTORY_SIZE;
            // Current price index is (historyIndex - 1), 5 turns ago is (historyIndex - 6)
            int32_t currentIdx = (mdata.historyIndex - 1 + HISTORY_SIZE) % HISTORY_SIZE;
            int32_t oldIdx = (mdata.historyIndex - 6 + HISTORY_SIZE) % HISTORY_SIZE;
            int32_t currentP = mdata.priceHistory[currentIdx];
            int32_t oldP = mdata.priceHistory[oldIdx];
            if (oldP > 0) {
                float change = static_cast<float>(currentP - oldP) / static_cast<float>(oldP);
                if (change > 0.10f) {
                    info.trend = "^";
                } else if (change < -0.10f) {
                    info.trend = "v";
                } else {
                    info.trend = "=";
                }
            } else {
                info.trend = "=";
            }
        } else {
            info.currentPrice = gDef.basePrice;
            info.trend = "=";
        }

        info.supply = 0;
        info.demand = 0;
        if (playerEcon != nullptr) {
            {
                std::unordered_map<uint16_t, int32_t>::const_iterator it = playerEcon->totalSupply.find(goodId);
                if (it != playerEcon->totalSupply.end()) {
                    info.supply = it->second;
                }
            }
            {
                std::unordered_map<uint16_t, int32_t>::const_iterator it = playerEcon->totalDemand.find(goodId);
                if (it != playerEcon->totalDemand.end()) {
                    info.demand = it->second;
                }
            }
        }
        info.volume = info.supply + info.demand;

        goodsInfo.push_back(std::move(info));
    }

    // ListRow variant: one row per good. Icon = colour-keyed resource
    // (pulled from IconAtlas if registered; falls back to generic
    // resource colour). Title = good name. Subtitle = S/D counts.
    // Right value = price + trend glyph (▲ / ▼ / ●).
    uint32_t shownCount = 0;
    for (const GoodDisplayInfo& info : goodsInfo) {
        if (shownCount >= 20) { break; }

        ListRowData row;
        row.title    = std::string(info.name);
        row.subtitle = "S:" + std::to_string(info.supply)
                     + "  D:" + std::to_string(info.demand);

        // Trend arrow colour: green up, red down, grey flat.
        Color trendColor = {0.75f, 0.75f, 0.8f, 1.0f};
        const char* trendGlyph = "=";
        if (info.trend == "^") {
            trendColor = {0.3f, 0.9f, 0.3f, 1.0f};
            trendGlyph = "^";
        } else if (info.trend == "v") {
            trendColor = {0.9f, 0.3f, 0.3f, 1.0f};
            trendGlyph = "v";
        }
        row.rightValue = std::string(trendGlyph) + " $"
                        + std::to_string(info.currentPrice);
        row.valueColor = trendColor;

        // Try to resolve the good's icon from the atlas.
        std::string key = "resources.";
        for (char c : std::string(info.name)) {
            key += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
        row.iconSpriteId = aoc::ui::IconAtlas::instance().id(key);
        if (row.iconSpriteId == 0) {
            row.iconSpriteId = aoc::ui::IconAtlas::instance().id("resources.stone");
        }

        (void)ui.createListRow(this->m_marketList, {0.0f, 0.0f, 0.0f, 26.0f},
                                std::move(row));
        ++shownCount;
    }

    // Market Detail: top 10 goods by trade volume
    std::sort(goodsInfo.begin(), goodsInfo.end(),
              [](const GoodDisplayInfo& a, const GoodDisplayInfo& b) {
                  return a.volume > b.volume;
              });

    (void)ui.createLabel(innerPanel, {0.0f, 0.0f, 470.0f, 14.0f},
                   LabelData{"-- Top 10 by Trade Volume --",
                              tokens::TEXT_DISABLED, 11.0f});

    uint32_t detailCount = 0;
    for (const GoodDisplayInfo& info : goodsInfo) {
        if (detailCount >= 10) {
            break;
        }
        if (info.volume <= 0) {
            break;
        }
        std::string detailLine = std::string(info.name)
                               + " Vol:" + std::to_string(info.volume)
                               + " $" + std::to_string(info.currentPrice);
        (void)ui.createLabel(innerPanel, {0.0f, 0.0f, 470.0f, 14.0f},
                       LabelData{std::move(detailLine), {0.7f, 0.75f, 0.8f, 1.0f}, 10.0f});
        ++detailCount;
    }

    ui.layout();
}

void EconomyScreen::buildTradeRoutePanel(UIManager& ui, WidgetId parentPanel) {
    // Remove old trade route panel if any
    if (this->m_tradeRoutePanel != INVALID_WIDGET) {
        ui.removeWidget(this->m_tradeRoutePanel);
        this->m_tradeRoutePanel = INVALID_WIDGET;
    }

    this->m_trSourcePlayerIdx = -1;
    this->m_trSourceCityIdx   = -1;
    this->m_trDestPlayerIdx   = -1;
    this->m_trDestCityIdx     = -1;

    this->m_tradeRoutePanel = ui.createPanel(
        parentPanel, {0.0f, 0.0f, 470.0f, 300.0f},
        PanelData{tokens::SURFACE_PARCHMENT_DIM, 4.0f});

    Widget* trPanel = ui.getWidget(this->m_tradeRoutePanel);
    if (trPanel != nullptr) {
        trPanel->padding = {6.0f, 6.0f, 6.0f, 6.0f};
        trPanel->childSpacing = 4.0f;
    }

    (void)ui.createLabel(this->m_tradeRoutePanel, {0.0f, 0.0f, 450.0f, 16.0f},
                   LabelData{"-- Create Trade Route --", tokens::TEXT_HEADER, 13.0f});

    // Source city selection (player's own cities)
    (void)ui.createLabel(this->m_tradeRoutePanel, {0.0f, 0.0f, 450.0f, 14.0f},
                   LabelData{"Source City (yours):", tokens::TEXT_HEADER, 11.0f});

    WidgetId sourceList = ui.createScrollList(
        this->m_tradeRoutePanel, {0.0f, 0.0f, 450.0f, 80.0f});
    Widget* srcListW = ui.getWidget(sourceList);
    if (srcListW != nullptr) {
        srcListW->padding = {2.0f, 2.0f, 2.0f, 2.0f};
        srcListW->childSpacing = 2.0f;
    }

    const aoc::game::Player* owningPlayer = this->m_gameState->player(this->m_player);
    if (owningPlayer != nullptr) {
        const int32_t playerIdx = static_cast<int32_t>(this->m_player);
        int32_t cityIdx = 0;
        for (const std::unique_ptr<aoc::game::City>& city : owningPlayer->cities()) {
            const int32_t capturedCityIdx = cityIdx;
            ButtonData srcBtn;
            srcBtn.label = city->name();
            srcBtn.fontSize = 10.0f;
            srcBtn.normalColor  = tokens::BRONZE_BASE;
            srcBtn.hoverColor   = tokens::BRONZE_LIGHT;
            srcBtn.pressedColor = tokens::BRONZE_DARK;
            srcBtn.cornerRadius = 2.0f;
            srcBtn.onClick = [this, playerIdx, capturedCityIdx]() {
                this->m_trSourcePlayerIdx = playerIdx;
                this->m_trSourceCityIdx   = capturedCityIdx;
                LOG_INFO("Trade route source city selected");
            };
            (void)ui.createButton(sourceList, {0.0f, 0.0f, 440.0f, 20.0f}, std::move(srcBtn));
            ++cityIdx;
        }
    }

    // Destination city selection (other players' cities)
    (void)ui.createLabel(this->m_tradeRoutePanel, {0.0f, 0.0f, 450.0f, 14.0f},
                   LabelData{"Destination City (other players):", tokens::TEXT_HEADER, 11.0f});

    WidgetId destList = ui.createScrollList(
        this->m_tradeRoutePanel, {0.0f, 0.0f, 450.0f, 80.0f});
    Widget* dstListW = ui.getWidget(destList);
    if (dstListW != nullptr) {
        dstListW->padding = {2.0f, 2.0f, 2.0f, 2.0f};
        dstListW->childSpacing = 2.0f;
    }

    for (const std::unique_ptr<aoc::game::Player>& otherPlayer : this->m_gameState->players()) {
        if (otherPlayer->id() == this->m_player) {
            continue;
        }
        const int32_t destPlayerIdx = static_cast<int32_t>(otherPlayer->id());
        int32_t cityIdx = 0;
        for (const std::unique_ptr<aoc::game::City>& city : otherPlayer->cities()) {
            const int32_t capturedCityIdx = cityIdx;
            std::string destLabel = city->name() + " (P" + std::to_string(static_cast<unsigned>(otherPlayer->id())) + ")";
            ButtonData dstBtn;
            dstBtn.label = std::move(destLabel);
            dstBtn.fontSize = 10.0f;
            dstBtn.normalColor  = tokens::BRONZE_BASE;
            dstBtn.hoverColor   = tokens::BRONZE_LIGHT;
            dstBtn.pressedColor = tokens::BRONZE_DARK;
            dstBtn.cornerRadius = 2.0f;
            dstBtn.onClick = [this, destPlayerIdx, capturedCityIdx]() {
                this->m_trDestPlayerIdx = destPlayerIdx;
                this->m_trDestCityIdx   = capturedCityIdx;
                LOG_INFO("Trade route destination city selected");
            };
            (void)ui.createButton(destList, {0.0f, 0.0f, 440.0f, 20.0f}, std::move(dstBtn));
            ++cityIdx;
        }
    }

    // "Establish Route" button
    ButtonData establishBtn;
    establishBtn.label = "Establish Route";
    establishBtn.fontSize = 12.0f;
    establishBtn.normalColor  = tokens::STATE_SUCCESS;
    establishBtn.hoverColor   = {0.432f, 0.654f, 0.292f, 1.0f};
    establishBtn.pressedColor = {0.288f, 0.436f, 0.194f, 1.0f};
    establishBtn.cornerRadius = 4.0f;
    establishBtn.onClick = [this]() {
        if (this->m_trSourcePlayerIdx < 0 || this->m_trSourceCityIdx < 0
            || this->m_trDestPlayerIdx < 0 || this->m_trDestCityIdx < 0) {
            LOG_INFO("Trade route: must select both source and destination cities");
            return;
        }

        const aoc::game::Player* srcPlayer =
            this->m_gameState->player(static_cast<PlayerId>(this->m_trSourcePlayerIdx));
        const aoc::game::Player* dstPlayer =
            this->m_gameState->player(static_cast<PlayerId>(this->m_trDestPlayerIdx));

        if (srcPlayer == nullptr || dstPlayer == nullptr) {
            return;
        }
        if (this->m_trSourceCityIdx >= srcPlayer->cityCount()
            || this->m_trDestCityIdx >= dstPlayer->cityCount()) {
            return;
        }

        const aoc::game::City& srcCity =
            *srcPlayer->cities()[static_cast<std::size_t>(this->m_trSourceCityIdx)];
        const aoc::game::City& dstCity =
            *dstPlayer->cities()[static_cast<std::size_t>(this->m_trDestCityIdx)];

        // Compute path between cities
        std::optional<aoc::map::PathResult> pathResult = aoc::map::findPath(
            *this->m_grid, srcCity.location(), dstCity.location());

        if (!pathResult.has_value()) {
            LOG_INFO("Trade route: no path found between cities");
            return;
        }

        // Create the trade route and add it to global state
        aoc::sim::TradeRouteComponent route{};
        route.sourceCityId   = EntityId{};  // Routes now identified by location, not legacy entity
        route.destCityId     = EntityId{};
        route.sourcePlayer   = srcPlayer->id();
        route.destPlayer     = dstPlayer->id();
        route.path           = pathResult->path;
        route.turnsRemaining = static_cast<int32_t>(pathResult->path.size()) / 5 + 1;

        // Auto-fill cargo with top 3 surplus goods from source city stockpile
        const aoc::sim::CityStockpileComponent& stockpile = srcCity.stockpile();
        std::vector<std::pair<uint16_t, int32_t>> surplusGoods;
        for (const std::pair<const uint16_t, int32_t>& entry : stockpile.goods) {
            if (entry.second > 0) {
                surplusGoods.push_back({entry.first, entry.second});
            }
        }
        std::sort(surplusGoods.begin(), surplusGoods.end(),
                  [](const std::pair<uint16_t, int32_t>& a,
                     const std::pair<uint16_t, int32_t>& b) {
                      return a.second > b.second;
                  });
        const std::size_t cargoCount = (surplusGoods.size() < 3) ? surplusGoods.size() : 3;
        for (std::size_t c = 0; c < cargoCount; ++c) {
            aoc::sim::TradeOffer offer{};
            offer.goodId = surplusGoods[c].first;
            offer.amountPerTurn = surplusGoods[c].second / 2;  // Ship half surplus
            if (offer.amountPerTurn > 0) {
                route.cargo.push_back(std::move(offer));
            }
        }

        this->m_gameState->tradeRoutes().push_back(std::move(route));

        LOG_INFO("Trade route established from %s to %s (%d turns)",
                 srcCity.name().c_str(), dstCity.name().c_str(),
                 this->m_gameState->tradeRoutes().back().turnsRemaining);
    };
    (void)ui.createButton(this->m_tradeRoutePanel, {0.0f, 0.0f, 160.0f, 26.0f},
                     std::move(establishBtn));

    ui.layout();
}

void EconomyScreen::close(UIManager& ui) {
    if (!this->m_isOpen) {
        return;
    }
    this->m_isOpen = false;
    if (this->m_rootPanel != INVALID_WIDGET) {
        ui.removeWidget(this->m_rootPanel);
        this->m_rootPanel = INVALID_WIDGET;
    }
    this->m_infoLabel = INVALID_WIDGET;
    this->m_marketList = INVALID_WIDGET;
    this->m_tradeRoutePanel = INVALID_WIDGET;
    this->m_trSourcePlayerIdx = -1;
    this->m_trSourceCityIdx   = -1;
    this->m_trDestPlayerIdx   = -1;
    this->m_trDestCityIdx     = -1;
}

void EconomyScreen::refresh(UIManager& ui) {
    if (!this->m_isOpen || this->m_gameState == nullptr) {
        return;
    }

    const aoc::game::Player* owningPlayer = this->m_gameState->player(this->m_player);
    const aoc::sim::MonetaryStateComponent* monetary =
        (owningPlayer != nullptr) ? &owningPlayer->monetary() : nullptr;

    std::string infoText = "No economic data";
    if (monetary != nullptr) {
        infoText = std::string(aoc::sim::monetarySystemName(monetary->system));

        // Coin reserves detail
        if (monetary->system == aoc::sim::MonetarySystemType::CommodityMoney
            || monetary->system == aoc::sim::MonetarySystemType::GoldStandard) {
            infoText += "  Cu:" + std::to_string(monetary->copperCoinReserves)
                      + " Ag:" + std::to_string(monetary->silverCoinReserves)
                      + " Au:" + std::to_string(monetary->goldBarReserves);
        }

        infoText += "  Tier:" + std::string(aoc::sim::coinTierName(monetary->effectiveCoinTier))
                  + "  Treasury:" + std::to_string(monetary->treasury);

        if (monetary->system != aoc::sim::MonetarySystemType::Barter) {
            infoText += "  M:" + std::to_string(monetary->moneySupply);
            int inflPct = static_cast<int>(monetary->inflationRate * 100.0f);
            infoText += "  Infl:" + std::to_string(inflPct) + "%";
        }

        // Debasement warning
        if (monetary->debasement.discoveredByPartners) {
            int debPct = static_cast<int>(monetary->debasement.debasementRatio * 100.0f);
            infoText += "  DEBASED:" + std::to_string(debPct) + "%";
        }

        // Fiat/Digital trust info
        if (monetary->system == aoc::sim::MonetarySystemType::FiatMoney
            || monetary->system == aoc::sim::MonetarySystemType::Digital) {
            const aoc::sim::CurrencyTrustComponent& trust = owningPlayer->currencyTrust();
            int trustPct = static_cast<int>(trust.trustScore * 100.0f);
            infoText += "  Trust:" + std::to_string(trustPct) + "%";
            if (trust.isReserveCurrency) {
                infoText += " [RESERVE]";
            }
        }
    }
    ui.setLabelText(this->m_infoLabel, std::move(infoText));
}

// ============================================================================
// CityDetailScreen
// ============================================================================

void CityDetailScreen::setContext(aoc::game::GameState* gameState, const aoc::map::HexGrid* grid,
                                   aoc::hex::AxialCoord cityLocation, PlayerId player,
                                   aoc::sim::EconomySimulation* economy) {
    this->m_gameState  = gameState;
    this->m_grid       = grid;
    this->m_economy    = economy;
    this->m_cityLocation = cityLocation;
    this->m_player     = player;
}

/// Resolve the City object matching m_cityEntity for this screen.
/// Returns nullptr when the city is no longer available.
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

void CityDetailScreen::open(UIManager& ui) {
    if (this->m_isOpen) {
        return;
    }

    assert(this->m_gameState != nullptr);
    this->m_isOpen = true;

    constexpr float kPanelWidth = 350.0f;
    constexpr float kContentWidth = 330.0f;

    // Right-side panel anchored to the top-right edge of the screen (no full-screen overlay).
    // Height is set to a large value; the anchor system positions X from the right edge.
    const float panelHeight = this->m_screenH;

    this->m_rootPanel = ui.createPanel(
        {0.0f, 0.0f, kPanelWidth, panelHeight},
        PanelData{tokens::SURFACE_PARCHMENT, 0.0f});
    {
        Widget* rootWidget = ui.getWidget(this->m_rootPanel);
        if (rootWidget != nullptr) {
            rootWidget->anchor = Anchor::TopRight;
            rootWidget->marginRight = 0.0f;
        }
    }

    WidgetId innerPanel = this->m_rootPanel;
    {
        Widget* inner = ui.getWidget(innerPanel);
        if (inner != nullptr) {
            inner->padding = {8.0f, 10.0f, 8.0f, 10.0f};
            inner->childSpacing = 4.0f;
        }
    }

    // Title label at top
    (void)ui.createLabel(innerPanel, {0.0f, 0.0f, kContentWidth, 22.0f},
                   LabelData{"City Detail", tokens::TEXT_HEADER, 18.0f});

    // "Close [ESC]" button at top-right corner
    {
        ButtonData closeBtn;
        closeBtn.label = "X";
        closeBtn.fontSize = 13.0f;
        closeBtn.normalColor = tokens::STATE_DANGER;
        closeBtn.hoverColor = tokens::DIPLO_HOSTILE;
        closeBtn.pressedColor = tokens::DIPLO_AT_WAR;
        closeBtn.cornerRadius = 3.0f;
        closeBtn.onClick = [this, &ui]() {
            this->close(ui);
        };
        (void)ui.createButton(innerPanel,
                        {kContentWidth - 24.0f, 0.0f, 28.0f, 22.0f},
                        std::move(closeBtn));
    }

    const aoc::game::City* city = resolveCityByLocation(this->m_gameState, this->m_player, this->m_cityLocation);

    if (city == nullptr) {
        this->m_detailLabel = ui.createLabel(
            innerPanel, {0.0f, 0.0f, kContentWidth, 16.0f},
            LabelData{"City not found", tokens::STATE_DANGER, 13.0f});
        ui.layout();
        return;
    }

    // ====================================================================
    // Header bar: city name + population
    // ====================================================================
    {
        WidgetId headerBar = ui.createPanel(
            innerPanel, {0.0f, 0.0f, kContentWidth, 36.0f},
            PanelData{tokens::SURFACE_INK, 4.0f});
        {
            Widget* hdr = ui.getWidget(headerBar);
            if (hdr != nullptr) {
                hdr->layoutDirection = LayoutDirection::Horizontal;
                hdr->padding = {6.0f, 8.0f, 6.0f, 8.0f};
                hdr->childSpacing = 0.0f;
            }
        }

        // Player color accent stripe
        (void)ui.createPanel(
            headerBar, {0.0f, 0.0f, 4.0f, 24.0f},
            PanelData{{0.3f, 0.6f, 0.95f, 1.0f}, 2.0f});

        std::string nameText = "  " + city->name();
        this->m_detailLabel = ui.createLabel(
            headerBar, {0.0f, 4.0f, 200.0f, 18.0f},
            LabelData{std::move(nameText), {1.0f, 0.95f, 0.75f, 1.0f}, 16.0f});

        std::string popText = "Pop " + std::to_string(city->population());
        (void)ui.createLabel(
            headerBar, {0.0f, 5.0f, 100.0f, 16.0f},
            LabelData{std::move(popText), {0.7f, 0.85f, 0.7f, 1.0f}, 13.0f});
    }

    // ====================================================================
    // Tab bar: 4 tab buttons in a horizontal row
    // ====================================================================
    {
        // Tab width chosen so 5 tabs + 4 gaps + 2*2 panel padding fit
        // kContentWidth (330). 5 * w + 4 * 4 + 4 = 330 → w = 62.
        constexpr float kTabWidth = 62.0f;
        constexpr float kTabHeight = 26.0f;
        constexpr float kTabGap = 4.0f;

        WidgetId tabBar = ui.createPanel(
            innerPanel, {0.0f, 0.0f, kContentWidth, kTabHeight + 6.0f},
            PanelData{{0.0f, 0.0f, 0.0f, 0.0f}, 0.0f});
        {
            Widget* tb = ui.getWidget(tabBar);
            if (tb != nullptr) {
                tb->layoutDirection = LayoutDirection::Horizontal;
                tb->padding = {2.0f, 2.0f, 2.0f, 2.0f};
                tb->childSpacing = kTabGap;
            }
        }

        constexpr std::array<const char*, TAB_COUNT> kTabNames = {
            "Overview", "Production", "Buildings", "Citizens", "Couriers"
        };
        constexpr Color kActiveTabColor   = tokens::BRONZE_BASE;
        constexpr Color kInactiveTabColor = tokens::SURFACE_PARCHMENT_DIM;

        // Icon sprite-id per tab. Uses IconAtlas placeholders until
        // real art lands. Names mirror the built-in seeds.
        constexpr std::array<const char*, TAB_COUNT> kTabIconKeys = {
            "techs.mining",        // Overview
            "resources.wood",      // Production
            "techs.electricity",   // Buildings
            "units.settler",       // Citizens
            "units.trader",        // Couriers
        };

        for (int32_t tabIdx = 0; tabIdx < TAB_COUNT; ++tabIdx) {
            const bool isActive = (tabIdx == this->m_activeTab);
            const Color baseColor = isActive ? kActiveTabColor : kInactiveTabColor;

            ButtonData tabBtn;
            tabBtn.label = kTabNames[static_cast<std::size_t>(tabIdx)];
            tabBtn.fontSize = 10.0f;
            tabBtn.normalColor = baseColor;
            tabBtn.hoverColor = {baseColor.r + 0.08f, baseColor.g + 0.08f,
                                 baseColor.b + 0.08f, 1.0f};
            tabBtn.pressedColor = {baseColor.r - 0.04f, baseColor.g - 0.04f,
                                   baseColor.b - 0.04f, 1.0f};
            tabBtn.cornerRadius = 3.0f;
            tabBtn.iconSpriteId = aoc::ui::IconAtlas::instance().id(
                kTabIconKeys[static_cast<std::size_t>(tabIdx)]);
            tabBtn.iconSize = 10.0f;

            CityDetailScreen* self = this;
            UIManager* uiPtr = &ui;
            const int32_t capturedIdx = tabIdx;
            tabBtn.onClick = [self, uiPtr, capturedIdx]() {
                self->switchTab(*uiPtr, capturedIdx);
            };

            this->m_tabButtons[static_cast<std::size_t>(tabIdx)] =
                ui.createButton(tabBar, {0.0f, 0.0f, kTabWidth, kTabHeight}, std::move(tabBtn));
        }
    }

    // ====================================================================
    // Content panel: holds tab content, swapped on tab switch
    // ====================================================================
    // Title(22) + headerBar(36) + tabBar(~32) + padding/spacing ~ 110px overhead
    const float contentHeight = panelHeight - 110.0f;
    this->m_contentPanel = ui.createPanel(
        innerPanel, {0.0f, 0.0f, kContentWidth, contentHeight},
        PanelData{{0.0f, 0.0f, 0.0f, 0.0f}, 0.0f});
    {
        Widget* cp = ui.getWidget(this->m_contentPanel);
        if (cp != nullptr) {
            cp->padding = {0.0f, 0.0f, 0.0f, 0.0f};
            cp->childSpacing = 0.0f;
        }
    }

    // Populate with the default tab
    this->m_activeTab = TAB_OVERVIEW;
    this->buildOverviewTab(ui, this->m_contentPanel);

    ui.layout();
}

// ----------------------------------------------------------------------------
// switchTab -- clear content panel and rebuild the selected tab
// ----------------------------------------------------------------------------

void CityDetailScreen::switchTab(UIManager& ui, int32_t tabIndex) {
    if (tabIndex < 0 || tabIndex >= TAB_COUNT) {
        return;
    }

    this->m_activeTab = tabIndex;

    // Remove all children of the content panel
    {
        Widget* cp = ui.getWidget(this->m_contentPanel);
        if (cp != nullptr) {
            // Copy the children vector because removeWidget mutates it
            const std::vector<WidgetId> childrenCopy = cp->children;
            for (WidgetId child : childrenCopy) {
                ui.removeWidget(child);
            }
        }
    }

    // Build the selected tab's content
    switch (tabIndex) {
        case TAB_OVERVIEW:   this->buildOverviewTab(ui, this->m_contentPanel);   break;
        case TAB_PRODUCTION: this->buildProductionTab(ui, this->m_contentPanel); break;
        case TAB_BUILDINGS:  this->buildBuildingsTab(ui, this->m_contentPanel);  break;
        case TAB_CITIZENS:   this->buildCitizensTab(ui, this->m_contentPanel);   break;
        case TAB_COURIERS:   this->buildCouriersTab(ui, this->m_contentPanel);   break;
        default: break;
    }

    // Update tab button colors to reflect active tab
    this->updateTabButtonColors(ui);

    ui.layout();
}

// ----------------------------------------------------------------------------
// updateTabButtonColors -- highlight the active tab, dim the rest
// ----------------------------------------------------------------------------

void CityDetailScreen::updateTabButtonColors(UIManager& ui) {
    constexpr Color kActiveTabColor   = tokens::BRONZE_BASE;
    constexpr Color kInactiveTabColor = tokens::SURFACE_PARCHMENT_DIM;

    for (int32_t tabIdx = 0; tabIdx < TAB_COUNT; ++tabIdx) {
        Widget* btnWidget = ui.getWidget(this->m_tabButtons[static_cast<std::size_t>(tabIdx)]);
        if (btnWidget == nullptr) {
            continue;
        }
        ButtonData* btnData = std::get_if<ButtonData>(&btnWidget->data);
        if (btnData == nullptr) {
            continue;
        }

        const bool isActive = (tabIdx == this->m_activeTab);
        const Color baseColor = isActive ? kActiveTabColor : kInactiveTabColor;
        btnData->normalColor  = baseColor;
        btnData->hoverColor   = {baseColor.r + 0.08f, baseColor.g + 0.08f,
                                 baseColor.b + 0.08f, 1.0f};
        btnData->pressedColor = {baseColor.r - 0.04f, baseColor.g - 0.04f,
                                 baseColor.b - 0.04f, 1.0f};
    }
}


void CityDetailScreen::toggleWorkerOnTile(aoc::hex::AxialCoord tile) {
    if (this->m_gameState == nullptr) {
        return;
    }
    aoc::game::City* city = resolveCityByLocation(this->m_gameState, this->m_player, this->m_cityLocation);
    if (city == nullptr) {
        return;
    }

    if (this->m_grid == nullptr || !this->m_grid->isValid(tile)) {
        return;
    }
    const int32_t tileIdx = this->m_grid->toIndex(tile);
    if (this->m_grid->owner(tileIdx) != this->m_player) {
        return;
    }
    if (this->m_grid->distance(city->location(), tile) > 3) {
        return;
    }
    if (this->m_grid->movementCost(tileIdx) == 0) {
        return;
    }

    city->toggleWorker(tile);
    LOG_INFO("Citizen toggled on tile (%d,%d) via map click", tile.q, tile.r);
}

void CityDetailScreen::close(UIManager& ui) {
    if (!this->m_isOpen) {
        return;
    }
    this->m_isOpen = false;
    if (this->m_rootPanel != INVALID_WIDGET) {
        ui.removeWidget(this->m_rootPanel);
        this->m_rootPanel = INVALID_WIDGET;
    }
    this->m_detailLabel = INVALID_WIDGET;
    this->m_contentPanel = INVALID_WIDGET;
    for (WidgetId& tabBtn : this->m_tabButtons) {
        tabBtn = INVALID_WIDGET;
    }
}

void CityDetailScreen::refresh(UIManager& ui) {
    if (!this->m_isOpen || this->m_gameState == nullptr) {
        return;
    }

    const aoc::game::City* city = resolveCityByLocation(this->m_gameState, this->m_player, this->m_cityLocation);
    if (city == nullptr) {
        ui.setLabelText(this->m_detailLabel, "City not found");
        return;
    }

    // Update header label
    std::string nameText = "  " + city->name();
    ui.setLabelText(this->m_detailLabel, std::move(nameText));

    // Rebuild current tab content to reflect latest data
    this->switchTab(ui, this->m_activeTab);
}

} // namespace aoc::ui
