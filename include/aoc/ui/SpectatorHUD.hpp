#pragma once

/**
 * @file SpectatorHUD.hpp
 * @brief Spectator-mode HUD overlay: turn/speed status bar and player scoreboard.
 *
 * Rendered as a direct-draw overlay on top of the game view using Renderer2D.
 * Does not use the UIManager widget tree so it never interferes with the
 * normal game HUD widgets that are hidden during spectator mode.
 */

#include <cstdint>
#include <string>

namespace vulkan_app::renderer { class Renderer2D; }
namespace aoc::game { class GameState; }

namespace aoc::ui {

/**
 * @brief Lightweight spectator overlay rendered via direct Renderer2D draw calls.
 *
 * Call drawStatusBar() and drawScoreboard() each frame while spectator mode
 * is active. Both functions are stateless and take only the data they need,
 * so the caller (Application) remains the single source of truth for all
 * spectator state.
 */
class SpectatorHUD {
public:
    /**
     * @brief Draw the top status bar showing turn, speed, and pause state.
     *
     * @param renderer    Active Renderer2D batch (between begin()/end()).
     * @param currentTurn Current simulation turn number.
     * @param maxTurns    Maximum number of turns configured for this spectate session.
     * @param speed       Current speed multiplier (e.g. 1.0, 2.0, 10.0).
     * @param isPaused    Whether the simulation is currently paused.
     * @param followPlayer -1 for free camera, otherwise the player index being followed.
     * @param screenW     Viewport width in pixels.
     * @param screenH     Viewport height in pixels.
     */
    void drawStatusBar(vulkan_app::renderer::Renderer2D& renderer,
                       int32_t currentTurn,
                       int32_t maxTurns,
                       float speed,
                       bool isPaused,
                       int32_t followPlayer,
                       float screenW,
                       float screenH) const;

    /**
     * @brief Draw a compact player scoreboard on the right side of the screen.
     *
     * Lists all active players ranked by composite score index, showing
     * city count, total population, treasury, and era victory points.
     *
     * @param renderer   Active Renderer2D batch.
     * @param gameState  Current game state to read player data from.
     * @param screenW    Viewport width in pixels.
     * @param screenH    Viewport height in pixels.
     */
    void drawScoreboard(vulkan_app::renderer::Renderer2D& renderer,
                        const aoc::game::GameState& gameState,
                        float screenW,
                        float screenH) const;
};

} // namespace aoc::ui
