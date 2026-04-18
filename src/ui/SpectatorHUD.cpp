/**
 * @file SpectatorHUD.cpp
 * @brief Spectator mode HUD overlay implementation.
 */

#include "aoc/ui/SpectatorHUD.hpp"
#include "aoc/ui/BitmapFont.hpp"
#include "aoc/ui/Widget.hpp"
#include "aoc/game/GameState.hpp"
#include "aoc/game/Player.hpp"
#include "aoc/simulation/civilization/Civilization.hpp"
// aoc::game::City is accessed only via Player::cities() — no direct include needed.

#include <renderer/Renderer2D.hpp>

#include <algorithm>
#include <string>
#include <vector>

namespace aoc::ui {

namespace {

/// Returns a human-readable multiplier string ("1x", "2x", etc.).
std::string formatSpeed(float speed) {
    int32_t rounded = static_cast<int32_t>(speed + 0.5f);
    if (rounded >= 100) {
        return "MAX";
    }
    return std::to_string(rounded) + "x";
}

} // anonymous namespace

void SpectatorHUD::drawStatusBar(vulkan_app::renderer::Renderer2D& renderer,
                                  int32_t currentTurn,
                                  int32_t maxTurns,
                                  float speed,
                                  bool isPaused,
                                  int32_t followPlayer,
                                  float screenW,
                                  [[maybe_unused]] float screenH) const {
    constexpr float BAR_H      = 28.0f;
    constexpr float PADDING_X  = 10.0f;
    constexpr float PADDING_Y  = 5.0f;
    constexpr float FONT_SIZE  = 13.0f;

    // Translucent background strip across the top.
    renderer.drawFilledRect(0.0f, 0.0f, screenW, BAR_H,
                             0.04f, 0.04f, 0.08f, 0.88f);

    // Separator line beneath bar.
    renderer.drawFilledRect(0.0f, BAR_H - 1.0f, screenW, 1.0f,
                             0.25f, 0.45f, 0.70f, 0.7f);

    // Left section: SPECTATOR label.
    aoc::ui::BitmapFont::drawText(renderer,
                                   "SPECTATOR",
                                   PADDING_X, PADDING_Y,
                                   FONT_SIZE,
                                   aoc::ui::Color{0.40f, 0.75f, 1.0f, 1.0f});

    // Center section: turn counter and speed.
    std::string centerText = "Turn " + std::to_string(currentTurn)
                           + " / " + std::to_string(maxTurns)
                           + "   Speed: " + formatSpeed(speed);
    if (isPaused) {
        centerText += "   [PAUSED]";
    }
    const aoc::ui::Rect centerMeasure = aoc::ui::BitmapFont::measureText(centerText, FONT_SIZE);
    const float centerX = (screenW - centerMeasure.w) * 0.5f;
    const aoc::ui::Color centerColor = isPaused
        ? aoc::ui::Color{1.0f, 0.75f, 0.20f, 1.0f}
        : aoc::ui::Color{0.90f, 0.90f, 0.90f, 1.0f};
    aoc::ui::BitmapFont::drawText(renderer, centerText, centerX, PADDING_Y,
                                   FONT_SIZE, centerColor);

    // Right section: camera follow status.
    std::string followText = (followPlayer < 0)
        ? "Camera: Free  [1-9: follow player | F: free]"
        : "Camera: P" + std::to_string(followPlayer) + "  [F: free]";
    const aoc::ui::Rect followMeasure = aoc::ui::BitmapFont::measureText(followText, FONT_SIZE);
    const float followX = screenW - followMeasure.w - PADDING_X;
    aoc::ui::BitmapFont::drawText(renderer, followText, followX, PADDING_Y,
                                   FONT_SIZE,
                                   aoc::ui::Color{0.65f, 0.80f, 0.65f, 1.0f});
}

// ---------------------------------------------------------------------------

void SpectatorHUD::drawScoreboard(vulkan_app::renderer::Renderer2D& renderer,
                                   const aoc::game::GameState& gameState,
                                   float screenW,
                                   float screenH) const {
    constexpr float PANEL_W    = 310.0f;
    constexpr float ROW_H      = 18.0f;
    constexpr float HEADER_H   = 20.0f;
    constexpr float PADDING_X  = 8.0f;
    constexpr float PADDING_Y  = 6.0f;
    constexpr float FONT_HEADER = 12.0f;
    constexpr float FONT_ROW    = 11.0f;
    constexpr float TOP_OFFSET  = 38.0f; // below spectator status bar

    // Collect per-player snapshot data sorted by composite score index (descending).
    struct PlayerRow {
        int32_t index = 0;
        std::string_view civName;
        int32_t cities = 0;
        int32_t population = 0;
        int64_t treasury = 0;
        int32_t eraVP = 0;
        float compositeCSI = 0.0f;
    };

    std::vector<PlayerRow> rows;
    rows.reserve(static_cast<std::size_t>(gameState.playerCount()));

    for (int32_t p = 0; p < gameState.playerCount(); ++p) {
        const aoc::game::Player* player = gameState.player(static_cast<aoc::PlayerId>(p));
        if (player == nullptr) {
            continue;
        }
        PlayerRow row{};
        row.index      = p;
        row.civName    = aoc::sim::civDef(player->civId()).name;
        row.cities     = player->cityCount();
        row.population = player->totalPopulation();
        row.treasury   = static_cast<int64_t>(player->treasury());
        row.eraVP      = player->victoryTracker().eraVictoryPoints;
        row.compositeCSI = player->victoryTracker().compositeCSI;
        rows.push_back(row);
    }

    // Sort by composite CSI descending so leader is at top.
    std::sort(rows.begin(), rows.end(),
              [](const PlayerRow& a, const PlayerRow& b) {
                  return a.compositeCSI > b.compositeCSI;
              });

    const float totalRows = static_cast<float>(rows.size());
    const float panelH = HEADER_H + PADDING_Y * 2.0f + totalRows * ROW_H + 4.0f;
    const float panelX = screenW - PANEL_W - 10.0f;
    const float panelY = TOP_OFFSET;

    // Make sure we don't render off-screen vertically.
    if (panelY + panelH > screenH - 10.0f) {
        return;
    }

    // Background panel.
    renderer.drawFilledRect(panelX, panelY, PANEL_W, panelH,
                             0.04f, 0.04f, 0.08f, 0.85f);
    renderer.drawFilledRect(panelX, panelY, PANEL_W, 1.0f,
                             0.25f, 0.45f, 0.70f, 0.8f);
    renderer.drawFilledRect(panelX, panelY + panelH - 1.0f, PANEL_W, 1.0f,
                             0.25f, 0.45f, 0.70f, 0.8f);

    // Header row.
    const float headerY = panelY + PADDING_Y;
    aoc::ui::BitmapFont::drawText(renderer,
                                   "#  Civilization       Cities Pop  Gold   VP   CSI",
                                   panelX + PADDING_X, headerY,
                                   FONT_HEADER,
                                   aoc::ui::Color{0.75f, 0.85f, 1.0f, 1.0f});

    // Separator under header.
    renderer.drawFilledRect(panelX, headerY + HEADER_H, PANEL_W, 1.0f,
                             0.25f, 0.35f, 0.50f, 0.6f);

    // Player rows.
    float rowY = headerY + HEADER_H + 2.0f;
    int32_t rank = 1;
    for (const PlayerRow& row : rows) {
        // Highlight the leading player with a subtle tint.
        if (rank == 1) {
            renderer.drawFilledRect(panelX, rowY - 1.0f, PANEL_W, ROW_H,
                                     0.15f, 0.25f, 0.15f, 0.35f);
        }

        // Build the row string with fixed-width columns for alignment.
        // #N  CivName(12)  CC  PP  GGGG  VVV  0.NN
        const std::string rankStr   = std::to_string(rank);
        const std::string civStr    = std::string(row.civName).substr(0, 12);
        const std::string citStr    = std::to_string(row.cities);
        const std::string popStr    = std::to_string(row.population);
        const std::string goldStr   = std::to_string(row.treasury);
        const std::string vpStr     = std::to_string(row.eraVP);

        // CSI formatted as 0.XX
        int32_t csiInt  = static_cast<int32_t>(row.compositeCSI * 100.0f);
        std::string csiStr = std::to_string(csiInt / 100) + "."
                           + (csiInt % 100 < 10 ? "0" : "")
                           + std::to_string(csiInt % 100);

        // Pad each column manually using spaces (BitmapFont is monospaced).
        // auto required: lambda types are unnameable.
        auto padRight = [](const std::string& s, int32_t width) -> std::string {
            if (static_cast<int32_t>(s.size()) >= width) {
                return s.substr(0, static_cast<std::size_t>(width));
            }
            return s + std::string(static_cast<std::size_t>(width - static_cast<int32_t>(s.size())), ' ');
        };
        auto padLeft = [](const std::string& s, int32_t width) -> std::string {
            if (static_cast<int32_t>(s.size()) >= width) {
                return s.substr(0, static_cast<std::size_t>(width));
            }
            return std::string(static_cast<std::size_t>(width - static_cast<int32_t>(s.size())), ' ') + s;
        };

        std::string rowText = padLeft(rankStr, 2)
                            + "  " + padRight(civStr, 12)
                            + "  " + padLeft(citStr, 2)
                            + "  " + padLeft(popStr, 4)
                            + "  " + padLeft(goldStr, 5)
                            + "  " + padLeft(vpStr, 4)
                            + "  " + csiStr;

        // Color gold for rank 1, silver for rank 2, bronze for rank 3, white otherwise.
        aoc::ui::Color rowColor;
        switch (rank) {
            case 1:  rowColor = {1.00f, 0.85f, 0.20f, 1.0f}; break;
            case 2:  rowColor = {0.80f, 0.82f, 0.84f, 1.0f}; break;
            case 3:  rowColor = {0.80f, 0.55f, 0.30f, 1.0f}; break;
            default: rowColor = {0.80f, 0.80f, 0.80f, 1.0f}; break;
        }

        aoc::ui::BitmapFont::drawText(renderer, rowText,
                                       panelX + PADDING_X, rowY,
                                       FONT_ROW, rowColor);

        rowY += ROW_H;
        ++rank;
    }
}

} // namespace aoc::ui
