/**
 * @file GreatPeopleScreen.cpp
 * @brief Read-only Great People screen implementation.
 */

#include "aoc/ui/GreatPeopleScreen.hpp"
#include "aoc/ui/UIManager.hpp"
#include "aoc/ui/StyleTokens.hpp"
#include "aoc/game/GameState.hpp"
#include "aoc/game/Player.hpp"
#include "aoc/game/City.hpp"
#include "aoc/game/Unit.hpp"
#include "aoc/simulation/city/District.hpp"
#include "aoc/simulation/greatpeople/GreatPeople.hpp"
#include "aoc/simulation/greatpeople/GreatPeopleExpanded.hpp"
#include "aoc/simulation/culture/GreatWorks.hpp"
#include "aoc/core/Log.hpp"

#include <array>
#include <cmath>
#include <memory>
#include <string>
#include <utility>

namespace aoc::ui {

namespace {

constexpr float PANEL_W = 700.0f;
constexpr float PANEL_H = 560.0f;
constexpr float LIST_W  = PANEL_W - 30.0f;
constexpr float LIST_H  = PANEL_H - 130.0f;
constexpr float ROW_W   = LIST_W - 10.0f;
constexpr float ROW_H   = 16.0f;
constexpr aoc::UnitTypeId GREAT_PERSON_UNIT{102};
constexpr aoc::BuildingId LIBRARY{7};

[[nodiscard]] bool isGreatPerson(const aoc::game::Unit& unit) {
    return unit.typeId() == GREAT_PERSON_UNIT && unit.greatPerson().owner != INVALID_PLAYER;
}

[[nodiscard]] const char* typeName(aoc::sim::GreatPersonType type) {
    return aoc::sim::greatPersonCategoryName(aoc::sim::categoryForGreatPersonType(type));
}

[[nodiscard]] std::string coordText(aoc::hex::AxialCoord c) {
    return "(" + std::to_string(c.q) + "," + std::to_string(c.r) + ")";
}

[[nodiscard]] std::string wholeNumber(float v) {
    return std::to_string(static_cast<int32_t>(std::floor(v)));
}

/// The districts and buildings that feed one type, mirroring
/// `accumulateGreatPeoplePoints`: a district gives 1 point when empty and 2 per
/// building; Libraries additionally feed Artists.
struct PointSources {
    int32_t districts = 0;
    int32_t buildings = 0;
};

[[nodiscard]] PointSources sourcesFor(const aoc::game::Player& player,
                                      aoc::sim::GreatPersonType type) {
    using aoc::sim::DistrictType;
    using aoc::sim::GreatPersonType;
    PointSources out{};
    for (const std::unique_ptr<aoc::game::City>& city : player.cities()) {
        for (const aoc::sim::CityDistrictsComponent::PlacedDistrict& d :
             city->districts().districts) {
            if (type == GreatPersonType::Artist) {
                for (aoc::BuildingId b : d.buildings) {
                    if (b == LIBRARY) { ++out.buildings; }
                }
                continue;
            }
            const bool feeds = (type == GreatPersonType::Scientist && d.type == DistrictType::Campus) ||
                               (type == GreatPersonType::Engineer && d.type == DistrictType::Industrial) ||
                               (type == GreatPersonType::General && d.type == DistrictType::Encampment) ||
                               (type == GreatPersonType::Merchant && d.type == DistrictType::Commercial) ||
                               (type == GreatPersonType::Admiral && d.type == DistrictType::Harbor) ||
                               (type == GreatPersonType::Prophet && d.type == DistrictType::HolySite) ||
                               (type == GreatPersonType::Writer && d.type == DistrictType::Theatre) ||
                               (type == GreatPersonType::Musician && d.type == DistrictType::Theatre);
            if (feeds) {
                ++out.districts;
                out.buildings += static_cast<int32_t>(d.buildings.size());
            }
        }
    }
    return out;
}

void mixHash(uint64_t& hash, uint64_t value) {
    hash ^= value;
    hash *= 1099511628211ULL;
}

} // namespace

void GreatPeopleScreen::setContext(aoc::game::GameState* gameState, aoc::map::HexGrid* grid,
                                   PlayerId humanPlayer) {
    this->m_gameState = gameState;
    this->m_grid      = grid;
    this->m_player    = humanPlayer;
    this->setBaseContext(gameState, grid, humanPlayer);
}

void GreatPeopleScreen::open(UIManager& ui) {
    if (this->m_isOpen) {
        return;
    }
    this->m_isOpen = true;

    const WidgetId innerPanel = this->createScreenFrame(ui, "Great People", PANEL_W, PANEL_H,
                                                        this->m_screenW, this->m_screenH);
    LabelData summary;
    summary.text         = "";
    summary.color        = tokens::TEXT_INK;
    summary.fontSize     = 13.0f;
    this->m_summaryLabel = ui.createLabel(innerPanel, {0.0f, 0.0f, LIST_W, 20.0f},
                                          std::move(summary));

    this->m_list = ui.createScrollList(innerPanel, {0.0f, 0.0f, LIST_W, LIST_H});
    if (Widget* list = ui.getWidget(this->m_list); list != nullptr) {
        list->padding      = {4.0f, 4.0f, 4.0f, 4.0f};
        list->childSpacing = 2.0f;
    }

    this->buildRows(ui);
    ui.layout();
}

void GreatPeopleScreen::close(UIManager& ui) {
    if (!this->m_isOpen) {
        return;
    }
    this->m_isOpen = false;
    if (this->m_rootPanel != INVALID_WIDGET) {
        ui.removeWidget(this->m_rootPanel);
        this->m_rootPanel = INVALID_WIDGET;
    }
    this->m_summaryLabel = INVALID_WIDGET;
    this->m_list         = INVALID_WIDGET;
}

void GreatPeopleScreen::refresh(UIManager& ui) {
    if (!this->m_isOpen || this->m_gameState == nullptr) {
        return;
    }
    if (this->stateFingerprint() == this->m_shownFingerprint) {
        return;
    }
    this->close(ui);
    this->open(ui);
}

void GreatPeopleScreen::buildRows(UIManager& ui) {
    this->m_shownFingerprint = this->stateFingerprint();
    const aoc::game::Player* self =
        this->m_gameState != nullptr ? this->m_gameState->player(this->m_player) : nullptr;
    if (self == nullptr) {
        ui.setLabelText(this->m_summaryLabel, "No player context");
        return;
    }

    const aoc::sim::PlayerGreatPeopleComponent& gp = self->greatPeople();
    int32_t recruitedTotal = 0;
    int32_t exhaustedTypes = 0;
    for (std::size_t i = 0; i < gp.recruited.size(); ++i) {
        recruitedTotal += gp.recruited[i];
        if (gp.exhausted[i]) { ++exhaustedTypes; }
    }
    int32_t living = 0;
    for (const std::unique_ptr<aoc::game::Unit>& unit : self->units()) {
        if (isGreatPerson(*unit) && !unit->greatPerson().isActivated) { ++living; }
    }
    const aoc::sim::GreatWorkTally works = aoc::sim::tallyGreatWorks(*self);
    ui.setLabelText(this->m_summaryLabel,
                    "Recruited: " + std::to_string(recruitedTotal) + "   Waiting to act: " +
                        std::to_string(living) + "   Types exhausted: " +
                        std::to_string(exhaustedTypes) + "   Great Works: " +
                        std::to_string(works.works) + " of " + std::to_string(works.capacity) +
                        " slots");

    this->addHeader(ui, "PROGRESS");
    this->addProgressRows(ui, *self);
    this->addHeader(ui, "YOUR GREAT PEOPLE");
    this->addRecruitedRows(ui, *self);
    this->addHeader(ui, "HOW TO USE");
    this->addLine(ui, "Right-click a Great Person on the tile where it appeared to activate it.", true);
}

void GreatPeopleScreen::addProgressRows(UIManager& ui, const aoc::game::Player& player) {
    const aoc::sim::PlayerGreatPeopleComponent& gp = player.greatPeople();
    for (uint8_t t = 0; t < static_cast<uint8_t>(aoc::sim::GreatPersonType::Count); ++t) {
        const aoc::sim::GreatPersonType type = static_cast<aoc::sim::GreatPersonType>(t);
        std::string row = std::string(typeName(type)) + "  ";
        if (gp.exhausted[t]) {
            row += "EXHAUSTED  |  recruited " + std::to_string(gp.recruited[t]) + " of " +
                   std::to_string(aoc::sim::MAX_GP_PER_TYPE);
        } else {
            row += wholeNumber(gp.points[t]) + " / " + wholeNumber(gp.threshold(type)) +
                   " points  |  recruited " + std::to_string(gp.recruited[t]) + " of " +
                   std::to_string(aoc::sim::MAX_GP_PER_TYPE);
        }
        this->addLine(ui, std::move(row), false);

        const PointSources src = sourcesFor(player, type);
        std::string why = "    Sources: ";
        if (type == aoc::sim::GreatPersonType::Artist) {
            why += std::to_string(src.buildings) + " Libraries";
        } else {
            why += std::to_string(src.districts) + " districts, " +
                   std::to_string(src.buildings) + " buildings";
        }
        if (src.districts == 0 && src.buildings == 0) {
            why += "  (none yet)";
        }
        this->addLine(ui, std::move(why), true);
    }
}

void GreatPeopleScreen::addRecruitedRows(UIManager& ui, const aoc::game::Player& player) {
    int32_t shown = 0;
    for (const std::unique_ptr<aoc::game::Unit>& unit : player.units()) {
        if (!isGreatPerson(*unit)) {
            continue;
        }
        const aoc::sim::GreatPersonComponent& gp = unit->greatPerson();
        const aoc::sim::NamedGreatPersonDef& named = aoc::sim::namedGreatPersonDef(gp.namedId);
        std::string head = std::string(aoc::sim::greatPersonCategoryName(named.category)) + " " +
                           std::string(named.name) + "  at " + coordText(unit->position());
        if (gp.isActivated) {
            head += "  |  used";
        }
        this->addLine(ui, std::move(head), false);
        // The named roster carries flavour text; the generic def is what activation does.
        std::string does = "    " + std::string(named.abilityName) + "  |  does: ";
        if (gp.defId < aoc::sim::GREAT_PERSON_COUNT) {
            does += std::string(aoc::sim::allGreatPersonDefs()[gp.defId].abilityDescription);
        } else {
            does += "unknown";
        }
        this->addLine(ui, std::move(does), true);
        if (!gp.isActivated && this->m_grid != nullptr) {
            // Same request as the unit panel, the right-click and the debug route;
            // the fingerprint sees the unit disappear and rebuilds the rows.
            ButtonData btn;
            btn.label        = "Activate " + std::string(named.name);
            btn.fontSize     = 11.0f;
            btn.normalColor  = tokens::BRONZE_BASE;
            btn.hoverColor   = tokens::BRONZE_LIGHT;
            btn.pressedColor = tokens::STATE_PRESSED;
            btn.labelColor   = tokens::TEXT_GILT;
            btn.cornerRadius = tokens::CORNER_BUTTON;
            aoc::game::GameState* gs        = this->m_gameState;
            aoc::map::HexGrid* grid         = this->m_grid;
            const PlayerId owner            = this->m_player;
            const aoc::hex::AxialCoord tile = unit->position();
            btn.onClick                     = [gs, grid, owner, tile]() {
                const ErrorCode result =
                    aoc::sim::requestGreatPersonActivation(*gs, *grid, owner, tile);
                if (result != ErrorCode::Ok) {
                    LOG_WARN("Great People screen: activation at (%d,%d) rejected: %.*s", tile.q,
                             tile.r, static_cast<int>(describeError(result).size()),
                             describeError(result).data());
                }
            };
            static_cast<void>(
                ui.createButton(this->m_list, {0.0f, 0.0f, ROW_W - 12.0f, 22.0f}, std::move(btn)));
        }
        ++shown;
    }
    if (shown == 0) {
        this->addLine(ui, "None yet. Build Campuses, Industrial Zones, Encampments and Commercial Hubs.", true);
    }
}

void GreatPeopleScreen::addHeader(UIManager& ui, const std::string& text) {
    LabelData ld;
    ld.text     = text;
    ld.color    = tokens::TEXT_HEADER;
    ld.fontSize = 12.0f;
    static_cast<void>(ui.createLabel(this->m_list, {0.0f, 0.0f, ROW_W, 20.0f}, std::move(ld)));
}

void GreatPeopleScreen::addLine(UIManager& ui, std::string text, bool dim) {
    LabelData ld;
    ld.text     = std::move(text);
    ld.color    = dim ? tokens::TEXT_DISABLED : tokens::TEXT_INK;
    ld.fontSize = 11.0f;
    static_cast<void>(ui.createLabel(this->m_list, {0.0f, 0.0f, ROW_W, ROW_H}, std::move(ld)));
}

uint64_t GreatPeopleScreen::stateFingerprint() const {
    if (this->m_gameState == nullptr) {
        return 0;
    }
    const aoc::game::Player* self = this->m_gameState->player(this->m_player);
    if (self == nullptr) {
        return 0;
    }
    uint64_t hash = 14695981039346656037ULL;
    mixHash(hash, static_cast<uint64_t>(this->m_gameState->currentTurn()));
    const aoc::sim::PlayerGreatPeopleComponent& gp = self->greatPeople();
    for (std::size_t i = 0; i < gp.points.size(); ++i) {
        mixHash(hash, static_cast<uint64_t>(std::floor(gp.points[i])));
        mixHash(hash, static_cast<uint64_t>(gp.recruited[i]));
        mixHash(hash, gp.exhausted[i] ? 1u : 0u);
    }
    for (const std::unique_ptr<aoc::game::Unit>& unit : self->units()) {
        if (!isGreatPerson(*unit)) {
            continue;
        }
        mixHash(hash, static_cast<uint64_t>(unit->greatPerson().namedId));
        mixHash(hash, unit->greatPerson().isActivated ? 1u : 0u);
        mixHash(hash, static_cast<uint64_t>(unit->position().q));
        mixHash(hash, static_cast<uint64_t>(unit->position().r));
    }
    return hash;
}

} // namespace aoc::ui
