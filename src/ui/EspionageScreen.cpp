/**
 * @file EspionageScreen.cpp
 * @brief Read-only espionage screen implementation.
 */

#include "aoc/ui/EspionageScreen.hpp"
#include "aoc/ui/UIManager.hpp"
#include "aoc/ui/StyleTokens.hpp"
#include "aoc/game/GameState.hpp"
#include "aoc/game/Player.hpp"
#include "aoc/game/City.hpp"
#include "aoc/game/Unit.hpp"
#include "aoc/map/HexGrid.hpp"
#include "aoc/simulation/civilization/Civilization.hpp"
#include "aoc/simulation/diplomacy/DiplomacyState.hpp"
#include "aoc/simulation/diplomacy/Espionage.hpp"
#include "aoc/simulation/diplomacy/EspionageSystem.hpp"
#include "aoc/core/Log.hpp"

#include <array>
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
constexpr int32_t MAX_HISTORY_ROWS = 10;

[[nodiscard]] bool isSpy(const aoc::game::Unit& unit) {
    return unit.spy().owner != INVALID_PLAYER;
}

/// A timed mission is bound to the spy's espionage location; otherwise the spy
/// operates where the unit stands (the tile `requestSpyMission` will use).
[[nodiscard]] aoc::hex::AxialCoord operatingTile(const aoc::game::Unit& unit) {
    return unit.spy().turnsRemaining > 0 ? unit.spy().location : unit.position();
}

[[nodiscard]] std::string coordText(aoc::hex::AxialCoord c) {
    return "(" + std::to_string(c.q) + "," + std::to_string(c.r) + ")";
}

[[nodiscard]] std::string percentText(float fraction) {
    return std::to_string(static_cast<int32_t>(fraction * 100.0f + 0.5f)) + "%";
}

[[nodiscard]] std::string civName(const aoc::game::Player& player) {
    return std::string(aoc::sim::civDef(player.civId()).name);
}

void mixHash(uint64_t& hash, uint64_t value) {
    hash ^= value;
    hash *= 1099511628211ULL;
}

/// The rival city standing on `location`, with its owner in `ownerOut`; null when none.
[[nodiscard]] const aoc::game::City* rivalCityAt(const aoc::game::GameState& gs, PlayerId self,
                                                  aoc::hex::AxialCoord location,
                                                  const aoc::game::Player*& ownerOut) {
    for (const std::unique_ptr<aoc::game::Player>& player : gs.players()) {
        if (player->id() == self) {
            continue;
        }
        const aoc::game::City* city = player->cityAt(location);
        if (city != nullptr) {
            ownerOut = player.get();
            return city;
        }
    }
    ownerOut = nullptr;
    return nullptr;
}

} // namespace

void EspionageScreen::setContext(aoc::game::GameState* gameState, const aoc::map::HexGrid* grid,
                                 PlayerId humanPlayer,
                                 const aoc::sim::DiplomacyManager* diplomacy) {
    this->m_gameState = gameState;
    this->m_grid      = grid;
    this->m_player    = humanPlayer;
    this->m_diplomacy = diplomacy;
    this->setBaseContext(gameState, grid, humanPlayer);
}

void EspionageScreen::open(UIManager& ui) {
    if (this->m_isOpen) {
        return;
    }
    this->m_isOpen = true;

    const WidgetId innerPanel = this->createScreenFrame(ui, "Espionage", PANEL_W, PANEL_H,
                                                        this->m_screenW, this->m_screenH);

    LabelData summary;
    summary.text         = "No spies";
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

void EspionageScreen::close(UIManager& ui) {
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

void EspionageScreen::refresh(UIManager& ui) {
    if (!this->m_isOpen || this->m_gameState == nullptr) {
        return;
    }
    if (this->stateFingerprint() == this->m_shownFingerprint) {
        return;
    }
    // Spy state moved (a turn resolved, a mission was assigned): rebuild in place.
    this->close(ui);
    this->open(ui);
}

void EspionageScreen::buildRows(UIManager& ui) {
    this->m_shownFingerprint = this->stateFingerprint();
    const aoc::game::Player* self =
        this->m_gameState != nullptr ? this->m_gameState->player(this->m_player) : nullptr;
    if (self == nullptr) {
        ui.setLabelText(this->m_summaryLabel, "No player context");
        return;
    }

    int32_t spies        = 0;
    int32_t counterSpies = 0;
    int32_t revealed     = 0;
    for (const std::unique_ptr<aoc::game::Unit>& unit : self->units()) {
        if (!isSpy(*unit)) {
            continue;
        }
        ++spies;
        if (unit->spy().currentMission == aoc::sim::SpyMission::CounterIntelligence) {
            ++counterSpies;
        }
        if (unit->spy().isRevealed) {
            ++revealed;
        }
    }
    ui.setLabelText(this->m_summaryLabel,
                    "Spies: " + std::to_string(spies) +
                        "   Counter-intelligence: " + std::to_string(counterSpies) +
                        "   Revealed: " + std::to_string(revealed));

    this->addHeader(ui, "YOUR SPIES");
    if (spies == 0) {
        this->addLine(ui, "No spies. Train a Spy unit and move it into a rival city.", true);
    }
    for (const std::unique_ptr<aoc::game::Unit>& unit : self->units()) {
        if (isSpy(*unit)) {
            this->addSpyRows(ui, *unit);
        }
    }

    this->addHeader(ui, "RIVALS");
    this->addTargetRows(ui);

    this->addHeader(ui, "MISSION HISTORY");
    this->addHistoryRows(ui);
}

void EspionageScreen::addSpyRows(UIManager& ui, const aoc::game::Unit& unit) {
    const aoc::sim::SpyComponent& spy = unit.spy();
    const aoc::hex::AxialCoord at     = operatingTile(unit);
    const aoc::game::Player* host     = nullptr;
    const aoc::game::City* city = rivalCityAt(*this->m_gameState, this->m_player, at, host);

    std::string where = coordText(at);
    if (city != nullptr && host != nullptr) {
        where = city->name() + " (" + civName(*host) + ")";
    }
    const bool busy                        = spy.turnsRemaining > 0;
    const aoc::sim::SpyMissionDef& current = aoc::sim::spyMissionDef(spy.currentMission);
    std::string head = std::string(aoc::sim::spyLevelName(spy.level)) + " at " + where +
                       "  |  " + std::string(current.name);
    if (busy) {
        head += ", " + std::to_string(spy.turnsRemaining) + " turns left";
    }
    if (spy.isRevealed) {
        head += "  |  REVEALED";
    }
    this->addLine(ui, std::move(head), false);

    if (spy.promotionCount() > 0) {
        std::string promos = "    Promotions: ";
        const std::array<aoc::sim::SpyPromotion, 3> slots{spy.promotion1, spy.promotion2,
                                                          spy.promotion3};
        bool first = true;
        for (aoc::sim::SpyPromotion promo : slots) {
            if (promo == aoc::sim::SpyPromotion::None) {
                continue;
            }
            if (!first) {
                promos += ", ";
            }
            promos += std::string(aoc::sim::spyPromotionName(promo));
            first = false;
        }
        this->addLine(ui, std::move(promos), true);
    }

    // Counter-spies defending the host city lower every chance below.
    int32_t counter = 0;
    if (host != nullptr && this->m_grid != nullptr) {
        counter = aoc::sim::counterSpyLevel(*this->m_gameState, *this->m_grid, host->id(), at);
    }
    std::string label = busy ? "    Missions here (busy until this one resolves)"
                             : "    Missions here: click one to assign";
    if (counter > 0) {
        label += " (counter-spy level " + std::to_string(counter) + ")";
    }
    this->addLine(ui, std::move(label), true);
    for (const aoc::sim::SpyMissionDef& def : aoc::sim::SPY_MISSION_DEFS) {
        const float chance  = aoc::sim::missionSuccessRate(spy, def.id, counter);
        const int32_t turns = aoc::sim::adjustedMissionDuration(spy, def.id);
        std::string text = std::string(def.name) + "  " + percentText(chance) + "  " +
                           std::to_string(turns) + " turns";
        if (def.isPassive) {
            text += "  (ongoing)";
        }
        if (busy) {
            this->addLine(ui, "      " + text, false);
            continue;
        }
        // The button invokes the same action the debug route uses; the next
        // refresh sees the changed fingerprint and rebuilds the rows.
        ButtonData btn;
        btn.label        = std::move(text);
        btn.fontSize     = 11.0f;
        btn.normalColor  = tokens::BRONZE_BASE;
        btn.hoverColor   = tokens::BRONZE_LIGHT;
        btn.pressedColor = tokens::STATE_PRESSED;
        btn.labelColor   = tokens::TEXT_GILT;
        btn.cornerRadius = tokens::CORNER_BUTTON;
        btn.disabled     = def.isOffensive && city == nullptr;
        aoc::game::GameState* gs        = this->m_gameState;
        const PlayerId owner            = this->m_player;
        const aoc::hex::AxialCoord tile = unit.position();
        const aoc::sim::SpyMission id   = def.id;
        btn.onClick                     = [gs, owner, tile, id]() {
            const ErrorCode result = aoc::sim::requestSpyMission(*gs, owner, tile, id);
            if (result != ErrorCode::Ok) {
                LOG_WARN("Espionage screen: mission %d at (%d,%d) rejected: %.*s",
                         static_cast<int>(id), tile.q, tile.r,
                         static_cast<int>(describeError(result).size()),
                         describeError(result).data());
            }
        };
        static_cast<void>(
            ui.createButton(this->m_list, {0.0f, 0.0f, ROW_W - 12.0f, 22.0f}, std::move(btn)));
    }
}

void EspionageScreen::addTargetRows(UIManager& ui) {
    const aoc::game::Player* self = this->m_gameState->player(this->m_player);
    if (self == nullptr) {
        return;
    }
    bool any = false;
    for (const std::unique_ptr<aoc::game::Player>& rival : this->m_gameState->players()) {
        if (rival->id() == this->m_player || rival->cities().empty() ||
            rival->victoryTracker().isEliminated) {
            continue;
        }
        any = true;
        std::string intel = "Unknown";
        if (this->m_diplomacy != nullptr) {
            const uint8_t level =
                this->m_diplomacy->relation(this->m_player, rival->id()).intelLevel;
            intel = std::string(aoc::sim::intelligenceLevelName(
                static_cast<aoc::sim::IntelligenceLevel>(level)));
        }
        this->addLine(ui, civName(*rival) + "  |  Intel: " + intel, false);
        for (const std::unique_ptr<aoc::game::City>& city : rival->cities()) {
            int32_t stationed = 0;
            for (const std::unique_ptr<aoc::game::Unit>& unit : self->units()) {
                if (isSpy(*unit) && operatingTile(*unit) == city->location()) {
                    ++stationed;
                }
            }
            int32_t counter = 0;
            if (this->m_grid != nullptr) {
                counter = aoc::sim::counterSpyLevel(*this->m_gameState, *this->m_grid,
                                                    rival->id(), city->location());
            }
            std::string row = "    " + city->name() + " " + coordText(city->location()) +
                              "  |  your spies: " + std::to_string(stationed) +
                              "  |  counter-spy level " + std::to_string(counter);
            this->addLine(ui, std::move(row), true);
        }
    }
    if (!any) {
        this->addLine(ui, "No rival cities known.", true);
    }
}

void EspionageScreen::addHistoryRows(UIManager& ui) {
    const std::vector<aoc::game::GameState::SpyMissionRecord>& records =
        this->m_gameState->spyMissionRecords();
    int32_t shown = 0;
    for (std::size_t i = records.size(); i > 0 && shown < MAX_HISTORY_ROWS; --i) {
        const aoc::game::GameState::SpyMissionRecord& rec = records[i - 1];
        const bool mine = rec.spyOwner == this->m_player;
        // The victim only learns of a failed enemy spy that did not slip away.
        const bool caught = rec.targetOwner == this->m_player && !rec.success &&
                            rec.outcome != aoc::sim::SpyFailureOutcome::EscapedUndetected;
        if (!mine && !caught) {
            continue;
        }
        std::string where                = coordText(rec.location);
        const aoc::game::Player* cityOwner = this->m_gameState->player(rec.targetOwner);
        if (cityOwner != nullptr) {
            const aoc::game::City* city = cityOwner->cityAt(rec.location);
            if (city != nullptr) {
                where = city->name();
            }
        }
        const std::string outcome = std::string(aoc::sim::spyFailureOutcomeName(rec.outcome));
        std::string row = "T" + std::to_string(rec.turn) + "  ";
        if (mine) {
            row += std::string(aoc::sim::spyMissionDef(rec.mission).name) + " at " + where + ": ";
            row += rec.success ? std::string("success") : "failed, " + outcome;
        } else {
            const aoc::game::Player* enemy = this->m_gameState->player(rec.spyOwner);
            row += "Caught " + (enemy != nullptr ? civName(*enemy) : std::string("an enemy")) +
                   " spy in " + where + " (" + outcome + ")";
        }
        this->addLine(ui, std::move(row), !mine);
        ++shown;
    }
    if (shown == 0) {
        this->addLine(ui, "No missions resolved yet.", true);
    }
}

void EspionageScreen::addHeader(UIManager& ui, const std::string& text) {
    LabelData ld;
    ld.text     = text;
    ld.color    = tokens::TEXT_HEADER;
    ld.fontSize = 12.0f;
    static_cast<void>(ui.createLabel(this->m_list, {0.0f, 0.0f, ROW_W, 20.0f}, std::move(ld)));
}

void EspionageScreen::addLine(UIManager& ui, std::string text, bool dim) {
    LabelData ld;
    ld.text     = std::move(text);
    ld.color    = dim ? tokens::TEXT_DISABLED : tokens::TEXT_INK;
    ld.fontSize = 11.0f;
    static_cast<void>(ui.createLabel(this->m_list, {0.0f, 0.0f, ROW_W, ROW_H}, std::move(ld)));
}

uint64_t EspionageScreen::stateFingerprint() const {
    if (this->m_gameState == nullptr) {
        return 0;
    }
    const aoc::game::Player* self = this->m_gameState->player(this->m_player);
    if (self == nullptr) {
        return 0;
    }
    uint64_t hash = 14695981039346656037ULL;
    mixHash(hash, static_cast<uint64_t>(this->m_gameState->currentTurn()));
    mixHash(hash, static_cast<uint64_t>(this->m_gameState->spyMissionRecords().size()));
    for (const std::unique_ptr<aoc::game::Unit>& unit : self->units()) {
        if (!isSpy(*unit)) {
            continue;
        }
        const aoc::sim::SpyComponent& spy = unit->spy();
        mixHash(hash, static_cast<uint64_t>(spy.currentMission));
        mixHash(hash, static_cast<uint64_t>(spy.turnsRemaining));
        mixHash(hash, static_cast<uint64_t>(spy.level));
        mixHash(hash, spy.isRevealed ? 1u : 0u);
        mixHash(hash, static_cast<uint64_t>(spy.experience));
        mixHash(hash, static_cast<uint64_t>(spy.location.q));
        mixHash(hash, static_cast<uint64_t>(spy.location.r));
        mixHash(hash, static_cast<uint64_t>(unit->position().q));
        mixHash(hash, static_cast<uint64_t>(unit->position().r));
    }
    return hash;
}

} // namespace aoc::ui
