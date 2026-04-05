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

    /// Transition from main menu to gameplay.
    void startGame(aoc::map::MapType mapType, aoc::map::MapSize mapSize);

private:
    void onResize(uint32_t width, uint32_t height);

    /// Handle left-click: select unit/city at cursor position.
    void handleSelect();

    /// Handle right-click: order selected unit to move to cursor position.
    void handleContextAction();

    /// Handle end-turn input.
    void handleEndTurn();

    /// Spawn initial units and cities for testing.
    void spawnStartingEntities();

    /// Scatter resources on the generated map.
    void placeMapResources();

    /// Spawn starting entities for an AI player.
    void spawnAIPlayer(PlayerId player);

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
    bool m_uiConsumedInput = false;

    // Game screens
    aoc::ui::ProductionScreen  m_productionScreen;
    aoc::ui::TechScreen        m_techScreen;
    aoc::ui::GovernmentScreen  m_governmentScreen;
    aoc::ui::EconomyScreen     m_economyScreen;
    aoc::ui::CityDetailScreen  m_cityDetailScreen;

    /// Returns true if any modal screen is currently open.
    [[nodiscard]] bool anyScreenOpen() const;

    /// Close all open screens.
    void closeAllScreens();

    void buildHUD();
    void updateHUD();

    bool m_initialized = false;

    // App state machine
    AppState m_appState = AppState::MainMenu;
    aoc::ui::MainMenu    m_mainMenu;
    aoc::ui::SettingsMenu m_settingsMenu;

    /// True once a victory condition has been met.
    bool m_gameOver = false;

    /// The victory result once the game is over.
    aoc::sim::VictoryResult m_victoryResult{};

    /// HUD label shown when the game ends.
    aoc::ui::WidgetId m_victoryLabel = aoc::ui::INVALID_WIDGET;
};

} // namespace aoc::app
