/**
 * @file WorldCongressScreen.cpp
 * @brief Read-only World Congress screen implementation.
 */

#include "aoc/ui/WorldCongressScreen.hpp"
#include "aoc/ui/UIManager.hpp"
#include "aoc/ui/StyleTokens.hpp"
#include "aoc/game/GameState.hpp"
#include "aoc/game/Player.hpp"
#include "aoc/simulation/civilization/Civilization.hpp"
#include "aoc/simulation/diplomacy/DiplomaticFavor.hpp"
#include "aoc/simulation/diplomacy/WorldCongress.hpp"

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

/// Favor costs mirrored from WorldCongress.cpp so the screen can explain them.
constexpr int32_t PROPOSAL_COST   = 30;
constexpr int32_t EXTRA_VOTE_COST = 10;
constexpr int32_t MAX_VOTE_WEIGHT = 4;

void mixHash(uint64_t& hash, uint64_t value) {
    hash ^= value;
    hash *= 1099511628211ULL;
}

} // namespace

void WorldCongressScreen::setContext(aoc::game::GameState* gameState,
                                     const aoc::map::HexGrid* grid, PlayerId humanPlayer) {
    this->m_gameState = gameState;
    this->m_grid      = grid;
    this->m_player    = humanPlayer;
    this->setBaseContext(gameState, grid, humanPlayer);
}

std::string WorldCongressScreen::civLabel(PlayerId id) const {
    if (this->m_gameState == nullptr || id == INVALID_PLAYER) {
        return "nobody";
    }
    const aoc::game::Player* p = this->m_gameState->player(id);
    if (p == nullptr) {
        return "P" + std::to_string(static_cast<unsigned>(id));
    }
    std::string name = std::string(aoc::sim::civDef(p->civId()).name);
    if (name.empty()) {
        name = "P" + std::to_string(static_cast<unsigned>(id));
    }
    if (id == this->m_player) {
        name += " (you)";
    }
    return name;
}

void WorldCongressScreen::open(UIManager& ui) {
    if (this->m_isOpen) {
        return;
    }
    this->m_isOpen = true;

    const WidgetId innerPanel = this->createScreenFrame(ui, "World Congress", PANEL_W, PANEL_H,
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

void WorldCongressScreen::close(UIManager& ui) {
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

void WorldCongressScreen::refresh(UIManager& ui) {
    if (!this->m_isOpen || this->m_gameState == nullptr) {
        return;
    }
    if (this->stateFingerprint() == this->m_shownFingerprint) {
        return;
    }
    this->close(ui);
    this->open(ui);
}

void WorldCongressScreen::buildRows(UIManager& ui) {
    this->m_shownFingerprint = this->stateFingerprint();
    const aoc::game::Player* self =
        this->m_gameState != nullptr ? this->m_gameState->player(this->m_player) : nullptr;
    if (self == nullptr) {
        ui.setLabelText(this->m_summaryLabel, "No player context");
        return;
    }
    const aoc::sim::WorldCongressComponent& wc = this->m_gameState->worldCongress();
    const aoc::sim::PlayerDiplomaticFavorComponent& favor = self->diplomaticFavor();

    std::string summary = wc.isActive ? "Congress in session" : "Congress not yet convened";
    summary += "   Next session in " + std::to_string(wc.turnsUntilNextSession) + " turns";
    summary += "   Your favor: " + std::to_string(favor.favor) + " (+" +
               std::to_string(favor.favorPerTurn) + " per turn)";
    ui.setLabelText(this->m_summaryLabel, std::move(summary));

    this->addHeader(ui, "CURRENT PROPOSAL");
    this->addProposalRows(ui);
    this->addHeader(ui, "ACTIVE EFFECTS");
    this->addEffectRows(ui);
    this->addHeader(ui, "PASSED RESOLUTIONS");
    if (wc.passedResolutions.empty()) {
        this->addLine(ui, "None yet.", true);
    }
    for (const aoc::sim::Resolution r : wc.passedResolutions) {
        this->addLine(ui, std::string(aoc::sim::resolutionName(r)), false);
    }
    this->addHeader(ui, "HOW IT WORKS");
    this->addLine(ui, "The civilization with the most favor proposes (costs " +
                          std::to_string(PROPOSAL_COST) + "). The first vote is free; each extra vote costs " +
                          std::to_string(EXTRA_VOTE_COST) + " favor, up to a weight of " +
                          std::to_string(MAX_VOTE_WEIGHT) + ".",
                  true);
    this->addLine(ui, "Votes are cast automatically for every civilization, including yours.", true);
}

void WorldCongressScreen::addProposalRows(UIManager& ui) {
    const aoc::sim::WorldCongressComponent& wc = this->m_gameState->worldCongress();
    if (wc.currentProposal == aoc::sim::Resolution::Count) {
        this->addLine(ui, "None on the table.", true);
        return;
    }
    std::string head = std::string(aoc::sim::resolutionName(wc.currentProposal)) +
                       "  proposed by " + this->civLabel(wc.proposer);
    if (wc.proposalTarget != INVALID_PLAYER) {
        head += "  targeting " + this->civLabel(wc.proposalTarget);
    }
    this->addLine(ui, std::move(head), false);

    int32_t yes = 0;
    int32_t no  = 0;
    int32_t mine = 0;
    for (std::size_t i = 0; i < wc.votes.size(); ++i) {
        const int32_t v = wc.votes[i];
        if (v > 0) { yes += v; }
        if (v < 0) { no -= v; }
        if (i == static_cast<std::size_t>(this->m_player)) { mine = v; }
    }
    std::string tally = "    Votes: yes " + std::to_string(yes) + "  no " + std::to_string(no);
    if (mine > 0) {
        tally += "  |  your vote: +" + std::to_string(mine);
    } else if (mine < 0) {
        tally += "  |  your vote: " + std::to_string(mine);
    } else {
        tally += "  |  your vote: none";
    }
    this->addLine(ui, std::move(tally), true);
}

void WorldCongressScreen::addEffectRows(UIManager& ui) {
    const aoc::sim::WorldCongressComponent& wc = this->m_gameState->worldCongress();
    if (wc.activeEffects.empty()) {
        this->addLine(ui, "None.", true);
        return;
    }
    for (const aoc::sim::ActiveResolution& e : wc.activeEffects) {
        std::string row = std::string(aoc::sim::resolutionName(e.type));
        if (e.target != INVALID_PLAYER) {
            row += "  on " + this->civLabel(e.target);
        }
        row += "  " + std::to_string(e.turnsRemaining) + " turns left";
        this->addLine(ui, std::move(row), false);
    }
}

void WorldCongressScreen::addHeader(UIManager& ui, const std::string& text) {
    LabelData ld;
    ld.text     = text;
    ld.color    = tokens::TEXT_HEADER;
    ld.fontSize = 12.0f;
    static_cast<void>(ui.createLabel(this->m_list, {0.0f, 0.0f, ROW_W, 20.0f}, std::move(ld)));
}

void WorldCongressScreen::addLine(UIManager& ui, std::string text, bool dim) {
    LabelData ld;
    ld.text     = std::move(text);
    ld.color    = dim ? tokens::TEXT_DISABLED : tokens::TEXT_INK;
    ld.fontSize = 11.0f;
    static_cast<void>(ui.createLabel(this->m_list, {0.0f, 0.0f, ROW_W, ROW_H}, std::move(ld)));
}

uint64_t WorldCongressScreen::stateFingerprint() const {
    uint64_t hash = 14695981039346656037ULL;
    if (this->m_gameState == nullptr) {
        return hash;
    }
    const aoc::sim::WorldCongressComponent& wc = this->m_gameState->worldCongress();
    mixHash(hash, static_cast<uint64_t>(this->m_gameState->currentTurn()));
    mixHash(hash, wc.isActive ? 1u : 0u);
    mixHash(hash, static_cast<uint64_t>(wc.turnsUntilNextSession));
    mixHash(hash, static_cast<uint64_t>(wc.currentProposal));
    mixHash(hash, static_cast<uint64_t>(wc.proposer));
    mixHash(hash, static_cast<uint64_t>(wc.proposalTarget));
    for (const int16_t v : wc.votes) {
        mixHash(hash, static_cast<uint64_t>(static_cast<int64_t>(v)));
    }
    mixHash(hash, static_cast<uint64_t>(wc.passedResolutions.size()));
    for (const aoc::sim::ActiveResolution& e : wc.activeEffects) {
        mixHash(hash, static_cast<uint64_t>(e.type));
        mixHash(hash, static_cast<uint64_t>(e.turnsRemaining));
    }
    const aoc::game::Player* self = this->m_gameState->player(this->m_player);
    if (self != nullptr) {
        mixHash(hash, static_cast<uint64_t>(self->diplomaticFavor().favor));
        mixHash(hash, static_cast<uint64_t>(self->diplomaticFavor().favorPerTurn));
    }
    return hash;
}

} // namespace aoc::ui
