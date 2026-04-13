#pragma once

/**
 * @file Application.hpp
 * @brief Top-level application class owning the window, renderer, and game loop.
 */

#include "aoc/app/Window.hpp"
#include "aoc/app/InputManager.hpp"
#include "aoc/render/CameraController.hpp"
#include "aoc/render/GameRenderer.hpp"
#include "aoc/map/HexGrid.hpp"
#include "aoc/map/FogOfWar.hpp"
#include "aoc/simulation/turn/TurnManager.hpp"
#include "aoc/simulation/resource/EconomySimulation.hpp"
#include "aoc/simulation/diplomacy/DiplomacyState.hpp"
#include "aoc/simulation/victory/VictoryCondition.hpp"
#include "aoc/simulation/ai/AIController.hpp"
#include "aoc/simulation/barbarian/BarbarianController.hpp"
#include "aoc/core/Random.hpp"
#include "aoc/ui/UIManager.hpp"
#include "aoc/ui/GameScreens.hpp"
#include "aoc/ui/TradeScreen.hpp"
#include "aoc/ui/DiplomacyScreen.hpp"
#include "aoc/ui/ReligionScreen.hpp"
#include "aoc/ui/EventLog.hpp"
#include "aoc/ui/MainMenu.hpp"
#include "aoc/ui/ScoreScreen.hpp"
#include "aoc/simulation/tech/EurekaBoost.hpp"
#include "aoc/audio/SoundEvent.hpp"
#include "aoc/audio/MusicManager.hpp"
#include "aoc/ui/Notifications.hpp"
#include "aoc/ui/Tutorial.hpp"
#include "aoc/ui/DebugConsole.hpp"
#include "aoc/replay/ReplayRecorder.hpp"
#include "aoc/game/GameState.hpp"
#include "aoc/core/ErrorCodes.hpp"
#include "aoc/core/Types.hpp"
#include "aoc/map/MapGenerator.hpp"
#include "aoc/ui/SpectatorHUD.hpp"

#include <cstdint>
#include <memory>

namespace vulkan_app {
class GraphicsDevice;
class RenderPipeline;
namespace renderer {
class Renderer2D;
}
}

namespace aoc::app {

/// Application-level state machine.
enum class AppState : uint8_t {
    MainMenu,
    InGame,
};

class Application {
public:
    struct Config {
        Window::Config window;
        bool enableValidation = true;
    };

    Application();
    ~Application();

    Application(const Application&) = delete;
    Application& operator=(const Application&) = delete;

    [[nodiscard]] ErrorCode initialize(const Config& config);
    void run();
    void shutdown();

    /// Apply current settings (fullscreen, vsync, FPS display, audio volumes).
    void applySettings();

    /// Transition from main menu to gameplay using full setup config.
    void startGame(const aoc::ui::GameSetupConfig& config);

    /**
     * @brief Start an all-AI spectator session.
     *
     * Configures a new game where every player slot is AI-controlled,
     * reveals all tiles, and enters spectator mode where turns auto-advance.
     *
     * @param playerCount  Number of AI civilizations to simulate (2-12).
     * @param maxTurns     Maximum turns before spectator auto-pauses (100-2000).
     */
    void startSpectate(int32_t playerCount, int32_t maxTurns);

    /// Defer spectator start until the first frame of run() so the render
    /// pipeline and window are fully initialized.
    void setDeferredSpectate(int32_t playerCount, int32_t maxTurns) {
        this->m_deferredSpectate = true;
        this->m_deferredSpectatePlayers = playerCount;
        this->m_deferredSpectateTurns = maxTurns;
    }

private:
    void onResize(uint32_t width, uint32_t height);

    /// Handle left-click: select unit/city at cursor position.
    void handleSelect();

    /// Handle right-click: order selected unit to move to cursor position.
    void handleContextAction();

    /// Handle end-turn input.
    void handleEndTurn();

    /// Spawn initial units and cities for the human player.
    void spawnStartingEntities(aoc::sim::CivId civId);

    /// Scatter resources on the generated map.
    void placeMapResources();

    /// Spawn starting entities for an AI player with specified civilization.
    void spawnAIPlayer(PlayerId player, aoc::sim::CivId civId);

    /// Find a valid land tile near a target for spawning.
    hex::AxialCoord findNearbyLandTile(hex::AxialCoord target) const;

    // Window + Input
    Window        m_window;
    InputManager  m_inputManager;

    // Vulkan rendering (forward-declared, need complete type in .cpp)
    std::unique_ptr<vulkan_app::GraphicsDevice>       m_graphicsDevice;
    std::unique_ptr<vulkan_app::RenderPipeline>       m_renderPipeline;
    std::unique_ptr<vulkan_app::renderer::Renderer2D> m_renderer2d;

    // Game rendering
    aoc::render::CameraController m_cameraController;
    aoc::render::GameRenderer     m_gameRenderer;

    // Game state
    aoc::game::GameState         m_gameState;
    aoc::map::HexGrid          m_hexGrid;
    aoc::sim::TurnManager        m_turnManager;
    aoc::sim::EconomySimulation  m_economy;
    aoc::map::FogOfWar           m_fogOfWar;
    aoc::sim::DiplomacyManager   m_diplomacy;
    std::vector<aoc::sim::ai::AIController> m_aiControllers;
    aoc::sim::BarbarianController m_barbarianController;
    aoc::Random                  m_gameRng{0};  ///< Reseeded in startGame()

    /// Currently selected unit (nullptr if none or city selected).
    aoc::game::Unit* m_selectedUnit = nullptr;

    /// Currently selected city (nullptr if none or unit selected).
    aoc::game::City* m_selectedCity = nullptr;

    // UI
    aoc::ui::UIManager m_uiManager;
    aoc::ui::WidgetId  m_turnLabel      = aoc::ui::INVALID_WIDGET;
    aoc::ui::WidgetId  m_selectionLabel = aoc::ui::INVALID_WIDGET;
    aoc::ui::WidgetId  m_economyLabel   = aoc::ui::INVALID_WIDGET;
    aoc::ui::WidgetId  m_endTurnButton  = aoc::ui::INVALID_WIDGET;
    aoc::ui::WidgetId  m_lastPlayerBanner = aoc::ui::INVALID_WIDGET; ///< "Waiting for you" glow
    aoc::ui::WidgetId  m_topBar         = aoc::ui::INVALID_WIDGET;
    aoc::ui::WidgetId  m_resourceLabel  = aoc::ui::INVALID_WIDGET;
    aoc::ui::WidgetId  m_menuDropdown   = aoc::ui::INVALID_WIDGET;
    aoc::ui::WidgetId  m_confirmDialog  = aoc::ui::INVALID_WIDGET;

    // Unit action panel
    aoc::ui::WidgetId  m_unitActionPanel = aoc::ui::INVALID_WIDGET;
    void rebuildUnitActionPanel();

    // Research progress bar
    aoc::ui::WidgetId  m_researchLabel   = aoc::ui::INVALID_WIDGET;
    aoc::ui::WidgetId  m_researchBar     = aoc::ui::INVALID_WIDGET;
    aoc::ui::WidgetId  m_researchBarFill = aoc::ui::INVALID_WIDGET;

    // Production progress bar
    aoc::ui::WidgetId  m_productionLabel   = aoc::ui::INVALID_WIDGET;
    aoc::ui::WidgetId  m_productionBar     = aoc::ui::INVALID_WIDGET;
    aoc::ui::WidgetId  m_productionBarFill = aoc::ui::INVALID_WIDGET;

    // Help overlay
    aoc::ui::WidgetId  m_helpOverlay = aoc::ui::INVALID_WIDGET;

    /// The unit selected when the action panel was last built (nullptr = no unit / city selected).
    aoc::game::Unit* m_actionPanelUnit = nullptr;

    /// Show "Save before returning to main menu?" dialog.
    void showReturnToMenuConfirm();

    /// Tear down all game state and return to the main menu.
    void returnToMainMenu();
    bool m_uiConsumedInput = false;

    // Game screens
    aoc::ui::ProductionScreen   m_productionScreen;
    aoc::ui::TechScreen         m_techScreen;
    aoc::ui::GovernmentScreen   m_governmentScreen;
    aoc::ui::EconomyScreen      m_economyScreen;
    aoc::ui::CityDetailScreen   m_cityDetailScreen;
    aoc::ui::TradeScreen        m_tradeScreen;
    aoc::ui::DiplomacyScreen    m_diplomacyScreen;
    aoc::ui::ReligionScreen     m_religionScreen;
    aoc::ui::ScoreScreen        m_scoreScreen;

    // Turn event log
    aoc::ui::EventLog m_eventLog;

    // Audio system (event queue + music manager)
    aoc::audio::SoundEventQueue m_soundQueue;
    aoc::audio::MusicManager    m_musicManager;

    // Notification toast system
    aoc::ui::NotificationManager m_notificationManager;

    // Tutorial
    aoc::ui::TutorialManager m_tutorialManager;

    // Debug console (opened with ` key)
    aoc::ui::DebugConsole m_debugConsole;

    // Replay recorder
    aoc::replay::ReplayRecorder m_replayRecorder;

    /// Returns true if any modal screen is currently open.
    [[nodiscard]] bool anyScreenOpen() const;

    /// Returns true if only the city detail screen is open (a right-side panel
    /// that should not block map interaction).
    [[nodiscard]] bool onlyCityDetailScreenOpen() const;

    /// Close all open screens.
    void closeAllScreens();

    void buildHUD();
    void updateHUD();

    bool m_initialized = false;

    // App state machine
    AppState m_appState = AppState::MainMenu;
    aoc::ui::MainMenu       m_mainMenu;
    aoc::ui::GameSetupScreen m_gameSetupScreen;
    aoc::ui::SettingsMenu   m_settingsMenu;

    /// Build the main menu with all its callbacks. Used by initialize() and returnToMainMenu().
    void buildMainMenu(float screenW, float screenH);

    /// Single-level undo state for the last unit movement.
    struct UndoState {
        aoc::game::Unit* unit = nullptr;
        hex::AxialCoord previousPosition;
        int32_t previousMovement = 0;
        bool hasState = false;
    };
    UndoState m_undoState;

    /// Tile buying: two-click confirmation state.
    aoc::hex::AxialCoord m_pendingBuyTile{-9999, -9999};
    bool m_pendingBuyConfirm = false;

    /// Handle Ctrl+Z undo of last unit movement.
    void handleUndoAction();

    /// True once a victory condition has been met.
    bool m_gameOver = false;

    /// The victory result once the game is over.
    aoc::sim::VictoryResult m_victoryResult{};

    /// HUD label shown when the game ends.
    aoc::ui::WidgetId m_victoryLabel = aoc::ui::INVALID_WIDGET;

    // ========================================================================
    // Spectator mode state
    // ========================================================================

    /// Deferred spectator start (set before run(), executed on first frame).
    bool m_deferredSpectate = false;
    int32_t m_deferredSpectatePlayers = 8;
    int32_t m_deferredSpectateTurns = 500;

    /// True when the game is running in all-AI spectator mode.
    bool m_spectatorMode = false;

    /// Turn advance speed: 1.0 = one turn per second, 10.0 = ten turns per second.
    float m_spectatorSpeed = 1.0f;

    /// Fractional turn accumulator — incremented by deltaTime * speed each frame.
    float m_spectatorTurnAccumulator = 0.0f;

    /// Whether the simulation advance is paused (user can still pan/zoom camera).
    bool m_spectatorPaused = false;

    /// Maximum turns before spectator auto-pauses; set by startSpectate().
    int32_t m_spectatorMaxTurns = 500;

    /// Camera follow target: -1 = free camera, 0-11 = follow that player's capital.
    int32_t m_spectatorFollowPlayer = -1;

    /// Whether fog of war is shown per followed player (false = reveal all tiles).
    bool m_spectatorFogEnabled = false;

    /// Renderer for the spectator HUD overlay (status bar + scoreboard).
    aoc::ui::SpectatorHUD m_spectatorHUD;

    /**
     * @brief Advance one spectator turn: run processTurn, update fog, check victory.
     *
     * Extracted from the frame loop to keep run() readable.
     */
    void spectatorAdvanceTurn();

    /**
     * @brief Update the camera position to track the followed player's capital.
     *
     * No-op when m_spectatorFollowPlayer is -1 (free camera).
     */
    void spectatorUpdateFollowCamera();

    /**
     * @brief Reveal all tiles for all players (spectator "omniscient" view).
     */
    void spectatorRevealAll();

    /**
     * @brief Draw the spectator HUD overlay on top of the game view.
     *
     * Opens its own Renderer2D begin/end batch and submits to the active
     * Vulkan command buffer stored internally in the current frame context.
     * Must be called within an active render pass.
     *
     * @param cmdBufferPtr Opaque pointer to the VkCommandBuffer for the frame.
     *                     Typed as void* to avoid pulling <vulkan/vulkan.h> into
     *                     the header; Application.cpp casts it back internally.
     * @param frameWidth   Current framebuffer width.
     * @param frameHeight  Current framebuffer height.
     */
    void spectatorDrawHUD(void* cmdBufferPtr, uint32_t frameWidth, uint32_t frameHeight);
};

} // namespace aoc::app
