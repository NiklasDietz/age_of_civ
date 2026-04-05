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
#include "aoc/ecs/World.hpp"
#include "aoc/ecs/SystemScheduler.hpp"
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
#include "aoc/simulation/tech/EurekaBoost.hpp"
#include "aoc/core/ErrorCodes.hpp"
#include "aoc/core/Types.hpp"
#include "aoc/map/MapGenerator.hpp"

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
    aoc::map::HexGrid          m_hexGrid;
    aoc::ecs::World            m_world;
    aoc::ecs::SystemScheduler  m_scheduler;
    aoc::sim::TurnManager        m_turnManager;
    aoc::sim::EconomySimulation  m_economy;
    aoc::map::FogOfWar           m_fogOfWar;
    aoc::sim::DiplomacyManager   m_diplomacy;
    std::vector<aoc::sim::ai::AIController> m_aiControllers;
    aoc::sim::BarbarianController m_barbarianController;
    aoc::Random                  m_gameRng{99999};

    /// Currently selected entity (unit or city).
    EntityId m_selectedEntity = NULL_ENTITY;

    // UI
    aoc::ui::UIManager m_uiManager;
    aoc::ui::WidgetId  m_turnLabel      = aoc::ui::INVALID_WIDGET;
    aoc::ui::WidgetId  m_selectionLabel = aoc::ui::INVALID_WIDGET;
    aoc::ui::WidgetId  m_economyLabel   = aoc::ui::INVALID_WIDGET;
    aoc::ui::WidgetId  m_endTurnButton  = aoc::ui::INVALID_WIDGET;
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

    /// The entity that was selected when the action panel was last built.
    EntityId m_actionPanelEntity = NULL_ENTITY;

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

    // Turn event log
    aoc::ui::EventLog m_eventLog;

    /// Returns true if any modal screen is currently open.
    [[nodiscard]] bool anyScreenOpen() const;

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
        EntityId entity = NULL_ENTITY;
        hex::AxialCoord previousPosition;
        int32_t previousMovement = 0;
        bool hasState = false;
    };
    UndoState m_undoState;

    /// Handle Ctrl+Z undo of last unit movement.
    void handleUndoAction();

    /// True once a victory condition has been met.
    bool m_gameOver = false;

    /// The victory result once the game is over.
    aoc::sim::VictoryResult m_victoryResult{};

    /// HUD label shown when the game ends.
    aoc::ui::WidgetId m_victoryLabel = aoc::ui::INVALID_WIDGET;
};

} // namespace aoc::app
