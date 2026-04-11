/**
 * @file Application.cpp
 * @brief Main application loop: window, Vulkan, game loop with units and turns.
 */

#include "aoc/app/Application.hpp"
#include "aoc/game/Player.hpp"
#include "aoc/game/City.hpp"
#include "aoc/game/Unit.hpp"
#include "aoc/map/MapGenerator.hpp"
#include "aoc/map/HexCoord.hpp"
#include "aoc/map/Pathfinding.hpp"
#include "aoc/simulation/unit/UnitComponent.hpp"
#include "aoc/simulation/unit/UnitTypes.hpp"
#include "aoc/simulation/unit/Movement.hpp"
#include "aoc/simulation/city/CityComponent.hpp"
#include "aoc/simulation/city/ProductionQueue.hpp"
#include "aoc/simulation/city/ProductionSystem.hpp"
#include "aoc/simulation/city/District.hpp"
#include "aoc/simulation/city/BorderExpansion.hpp"
#include "aoc/simulation/map/Improvement.hpp"
#include "aoc/simulation/resource/ResourceComponent.hpp"
#include "aoc/simulation/resource/ResourceTypes.hpp"
#include "aoc/simulation/monetary/MonetarySystem.hpp"
#include "aoc/simulation/production/Waste.hpp"
#include "aoc/simulation/tech/TechTree.hpp"
#include "aoc/simulation/tech/CivicTree.hpp"
#include "aoc/simulation/tech/EraProgression.hpp"
#include "aoc/simulation/unit/Combat.hpp"
#include "aoc/simulation/unit/Naval.hpp"
#include "aoc/simulation/city/CityGrowth.hpp"
#include "aoc/simulation/city/CityScience.hpp"
#include "aoc/simulation/city/Happiness.hpp"
#include "aoc/simulation/victory/VictoryCondition.hpp"
#include "aoc/simulation/government/Government.hpp"
#include "aoc/simulation/government/GovernmentComponent.hpp"
#include "aoc/simulation/civilization/Civilization.hpp"
#include "aoc/simulation/greatpeople/GreatPeople.hpp"
#include "aoc/simulation/wonder/Wonder.hpp"
#include "aoc/simulation/tech/EurekaBoost.hpp"
#include "aoc/simulation/diplomacy/EspionageSystem.hpp"
#include "aoc/simulation/diplomacy/WarWeariness.hpp"
#include "aoc/simulation/tech/EraScore.hpp"
#include "aoc/simulation/city/CityLoyalty.hpp"
#include "aoc/simulation/economy/Market.hpp"
#include "aoc/simulation/turn/TurnProcessor.hpp"
#include "aoc/save/Serializer.hpp"
#include "aoc/ui/BitmapFont.hpp"
#include "aoc/core/Log.hpp"

#include <renderer/GraphicsDevice.hpp>
#include <renderer/RenderPipeline.hpp>
#include <renderer/Renderer2D.hpp>

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include "aoc/simulation/unit/UnitUpgrade.hpp"
#include "aoc/simulation/city/CityBombardment.hpp"
#include "aoc/simulation/city/CityConnection.hpp"
#include "aoc/simulation/citystate/CityState.hpp"
#include "aoc/simulation/religion/Religion.hpp"
#include "aoc/simulation/economy/Maintenance.hpp"
#include "aoc/simulation/economy/AdvancedEconomics.hpp"
#include "aoc/simulation/diplomacy/Grievance.hpp"
#include "aoc/simulation/diplomacy/WorldCongress.hpp"
#include "aoc/simulation/climate/Climate.hpp"

#include <chrono>
#include <cmath>
#include <utility>

namespace aoc::sim {

std::string getNextCityName(const aoc::ecs::World& world, PlayerId player) {
    // Find the player's civilization
    CivId playerCivId = 0;
    const aoc::ecs::ComponentPool<PlayerCivilizationComponent>* civPool =
        world.getPool<PlayerCivilizationComponent>();
    if (civPool != nullptr) {
        for (uint32_t i = 0; i < civPool->size(); ++i) {
            if (civPool->data()[i].owner == player) {
                playerCivId = civPool->data()[i].civId;
                break;
            }
        }
    }

    // Count existing cities owned by this player
    int32_t cityCount = 0;
    const aoc::ecs::ComponentPool<CityComponent>* cityPool =
        world.getPool<CityComponent>();
    if (cityPool != nullptr) {
        for (uint32_t i = 0; i < cityPool->size(); ++i) {
            if (cityPool->data()[i].owner == player) {
                ++cityCount;
            }
        }
    }

    const CivilizationDef& civ = civDef(playerCivId);
    if (cityCount >= 0 && static_cast<std::size_t>(cityCount) < MAX_CIV_CITY_NAMES) {
        return std::string(civ.cityNames[static_cast<std::size_t>(cityCount)]);
    }

    return "City " + std::to_string(cityCount + 1);
}

} // namespace aoc::sim

namespace aoc::app {

namespace {

/**
 * @brief Convert a turn number to a year string for display (e.g., "1600 BC").
 *
 * Eras:
 *   Turn   0-50:  start 4000 BC, 80 years per turn
 *   Turn  51-100: start   0 AD,  20 years per turn
 *   Turn 101-200: start 1000 AD,  5 years per turn
 *   Turn 201-350: start 1500 AD, ~2.7 years per turn (3 years used)
 *   Turn 351+:    start 1900 AD,  1 year per turn
 */
std::string turnToYear(TurnNumber turn) {
    int32_t year = 0;
    if (turn <= 50) {
        year = -4000 + static_cast<int32_t>(turn) * 80;
    } else if (turn <= 100) {
        year = 0 + static_cast<int32_t>(turn - 51) * 20;
    } else if (turn <= 200) {
        year = 1000 + static_cast<int32_t>(turn - 101) * 5;
    } else if (turn <= 350) {
        year = 1500 + static_cast<int32_t>(turn - 201) * 3;
    } else {
        year = 1900 + static_cast<int32_t>(turn - 351);
    }

    if (year < 0) {
        return std::to_string(-year) + " BC";
    }
    return std::to_string(year) + " AD";
}

} // anonymous namespace

Application::Application() = default;

Application::~Application() {
    this->shutdown();
}

ErrorCode Application::initialize(const Config& config) {
    // -- Window --
    ErrorCode result = this->m_window.create(config.window);
    if (result != ErrorCode::Ok) {
        LOG_ERROR("Window creation failed: %.*s",
                  static_cast<int>(describeError(result).size()),
                  describeError(result).data());
        return result;
    }

    this->m_inputManager.bindToWindow(this->m_window);

    // -- Vulkan --
    vulkan_app::GraphicsDevice::Config deviceConfig{};
    deviceConfig.appName = "AgeOfCiv";
    deviceConfig.enableValidation = config.enableValidation;

    // Query GLFW for the Vulkan instance extensions it needs for surface creation.
    // The renderer submodule's getRequiredExtensions("glfw") returns empty,
    // so we must provide them explicitly.
    uint32_t glfwExtCount = 0;
    const char** glfwExts = glfwGetRequiredInstanceExtensions(&glfwExtCount);
    for (uint32_t i = 0; i < glfwExtCount; ++i) {
        deviceConfig.instanceExtensions.push_back(glfwExts[i]);
    }

    this->m_graphicsDevice = vulkan_app::GraphicsDevice::createFromNativeWindow(
        static_cast<void*>(this->m_window.handle()), "glfw", deviceConfig);
    if (this->m_graphicsDevice == nullptr) {
        LOG_ERROR("Vulkan device creation failed");
        return ErrorCode::VulkanDeviceCreationFailed;
    }

    const std::pair<uint32_t, uint32_t> fbSize = this->m_window.framebufferSize();
    const uint32_t fbWidth = fbSize.first;
    const uint32_t fbHeight = fbSize.second;
    vulkan_app::RenderPipeline::Config pipelineConfig{};
    pipelineConfig.width  = fbWidth;
    pipelineConfig.height = fbHeight;
    pipelineConfig.vsync  = config.window.vsync;
    pipelineConfig.clearR = 0.05f;
    pipelineConfig.clearG = 0.05f;
    pipelineConfig.clearB = 0.08f;
    pipelineConfig.clearA = 1.0f;

    this->m_renderPipeline = std::make_unique<vulkan_app::RenderPipeline>(
        *this->m_graphicsDevice, pipelineConfig);

    VkExtent2D extent = this->m_renderPipeline->extent();
    this->m_renderer2d = std::make_unique<vulkan_app::renderer::Renderer2D>(
        this->m_graphicsDevice->device(),
        this->m_renderPipeline->renderPass(),
        extent,
        vulkan_app::RenderPipeline::MAX_FRAMES_IN_FLIGHT);

    // -- Game renderer (needed for both menu and in-game rendering) --
    this->m_gameRenderer.initialize(*this->m_renderPipeline, *this->m_renderer2d);

    // -- Resize --
    this->m_window.setResizeCallback([this](uint32_t width, uint32_t height) {
        this->onResize(width, height);
    });

    // -- Main menu --
    if (!aoc::ui::BitmapFont::initialize()) {
        LOG_WARN("Font initialization failed -- text will not render");
    }

    // Load saved settings
    this->m_settingsMenu.settings() = aoc::ui::loadSettings("settings.cfg");
    this->applySettings();

    this->m_appState = AppState::MainMenu;
    const float screenW = static_cast<float>(fbWidth);
    const float screenH = static_cast<float>(fbHeight);
    this->buildMainMenu(screenW, screenH);

    this->m_initialized = true;
    LOG_INFO("Initialized (%ux%u), showing main menu", fbWidth, fbHeight);
    return ErrorCode::Ok;
}

void Application::startGame(const aoc::ui::GameSetupConfig& config) {
    // -- Map generation --
    const std::pair<int32_t, int32_t> dims = aoc::map::mapSizeDimensions(config.mapSize);
    aoc::map::MapGenerator::Config mapConfig{};
    mapConfig.width   = dims.first;
    mapConfig.height  = dims.second;
    // Use current time as seed for unique maps each game
    const uint32_t timeSeed = static_cast<uint32_t>(
        std::chrono::steady_clock::now().time_since_epoch().count() & 0xFFFFFFFFu);
    mapConfig.seed = timeSeed;
    this->m_gameRng = aoc::Random(timeSeed + 1);  // Different seed but derived from same time
    mapConfig.mapType = config.mapType;
    mapConfig.mapSize = config.mapSize;
    aoc::map::MapGenerator::generate(mapConfig, this->m_hexGrid);
    LOG_INFO("Map generated (%dx%d)", this->m_hexGrid.width(), this->m_hexGrid.height());

    // -- Count human and AI players --
    uint8_t humanCount = 0;
    uint8_t aiCount    = 0;
    for (uint8_t i = 0; i < config.playerCount; ++i) {
        if (config.players[i].isHuman) {
            ++humanCount;
        } else {
            ++aiCount;
        }
    }

    // -- Game setup --
    this->m_turnManager.setPlayerCount(humanCount, aiCount);
    if (config.sequentialTurnsInWar) {
        this->m_turnManager.setTurnMode(aoc::sim::TurnMode::Sequential);
    } else {
        this->m_turnManager.setTurnMode(aoc::sim::TurnMode::Simultaneous);
    }
    this->m_turnManager.beginNewTurn();
    if (config.mapType != aoc::map::MapType::Realistic) {
        this->placeMapResources();
    } else {
        LOG_INFO("Skipping random resource placement (Realistic map uses geology-based placement)");
    }
    this->m_economy.initialize();
    this->m_fogOfWar.initialize(this->m_hexGrid.tileCount(), MAX_PLAYERS);
    this->m_diplomacy.initialize(config.playerCount);

    // -- Initialize new GameState object model (shares the same ECS World) --
    this->m_gameState.setExternalWorld(&this->m_world);
    this->m_gameState.initialize(static_cast<int32_t>(config.playerCount));
    for (uint8_t i = 0; i < config.playerCount; ++i) {
        aoc::game::Player* gsPlayer = this->m_gameState.player(static_cast<PlayerId>(i));
        gsPlayer->setCivId(config.players[i].civId);
        gsPlayer->setHuman(config.players[i].isHuman);
        gsPlayer->setTreasury(100);
    }
    LOG_INFO("GameState initialized for %u players",
             static_cast<unsigned>(config.playerCount));

    // Spawn human player (always slot 0)
    this->spawnStartingEntities(config.players[0].civId);

    // Spawn AI players (pass difficulty setting)
    for (uint8_t i = 1; i < config.playerCount; ++i) {
        const PlayerId playerId = static_cast<PlayerId>(i);
        this->m_aiControllers.emplace_back(playerId, config.aiDifficulty);
        this->spawnAIPlayer(playerId, config.players[i].civId);
    }

    // -- War weariness and era score initialization --
    for (uint8_t p = 0; p < config.playerCount; ++p) {
        const PlayerId pid = static_cast<PlayerId>(p);
        {
            EntityId wwEntity = this->m_world.createEntity();
            aoc::sim::PlayerWarWearinessComponent wwComp{};
            wwComp.owner = pid;
            this->m_world.addComponent<aoc::sim::PlayerWarWearinessComponent>(
                wwEntity, std::move(wwComp));
        }
        {
            EntityId esEntity = this->m_world.createEntity();
            aoc::sim::PlayerEraScoreComponent esComp{};
            esComp.owner = pid;
            this->m_world.addComponent<aoc::sim::PlayerEraScoreComponent>(
                esEntity, std::move(esComp));
        }
    }
    LOG_INFO("War weariness and era score initialized for %u players",
             static_cast<unsigned>(config.playerCount));

    // -- Religion system initialization --
    {
        EntityId religionEntity = this->m_world.createEntity();
        this->m_world.addComponent<aoc::sim::GlobalReligionTracker>(
            religionEntity, aoc::sim::GlobalReligionTracker{});

        for (uint8_t p = 0; p < config.playerCount; ++p) {
            EntityId faithEntity = this->m_world.createEntity();
            aoc::sim::PlayerFaithComponent faithComp{};
            faithComp.owner = static_cast<PlayerId>(p);
            this->m_world.addComponent<aoc::sim::PlayerFaithComponent>(
                faithEntity, std::move(faithComp));
        }
        LOG_INFO("Religion system initialized for %u players",
                 static_cast<unsigned>(config.playerCount));
    }

    // -- Grievance system initialization --
    for (uint8_t p = 0; p < config.playerCount; ++p) {
        EntityId gEntity = this->m_world.createEntity();
        aoc::sim::PlayerGrievanceComponent gComp{};
        gComp.owner = static_cast<PlayerId>(p);
        this->m_world.addComponent<aoc::sim::PlayerGrievanceComponent>(
            gEntity, std::move(gComp));
    }

    // -- World Congress initialization --
    {
        EntityId wcEntity = this->m_world.createEntity();
        aoc::sim::WorldCongressComponent wcComp{};
        this->m_world.addComponent<aoc::sim::WorldCongressComponent>(
            wcEntity, std::move(wcComp));
        LOG_INFO("World Congress initialized (first session at turn 50)");
    }

    // -- Climate system initialization --
    {
        EntityId climateEntity = this->m_world.createEntity();
        aoc::sim::GlobalClimateComponent climateComp{};
        this->m_world.addComponent<aoc::sim::GlobalClimateComponent>(
            climateEntity, std::move(climateComp));
        LOG_INFO("Climate system initialized");
    }

    // -- Replay recorder --
    this->m_replayRecorder.clear();

    // -- Sound event queue --
    this->m_soundQueue.clear();
    this->m_soundQueue.push(aoc::audio::SoundEffect::TurnStart);

    // -- Music: set to Ancient era --
    this->m_musicManager.setTrack(aoc::audio::MusicTrack::Ancient);

    // Spawn city-states
    const int32_t cityStateCount = static_cast<int32_t>(config.playerCount) * 2;
    aoc::sim::spawnCityStates(this->m_world, this->m_hexGrid,
                               cityStateCount, this->m_gameRng);

    // Update fog of war for all players
    for (uint8_t i = 0; i < config.playerCount; ++i) {
        this->m_fogOfWar.updateVisibility(this->m_world, this->m_hexGrid, i);
    }

    // Diagnostic: count visible tiles for player 0
    {
        int32_t visCount = 0;
        for (int32_t i = 0; i < this->m_hexGrid.tileCount(); ++i) {
            if (this->m_fogOfWar.visibility(0, i) == aoc::map::TileVisibility::Visible) {
                ++visCount;
            }
        }
        LOG_INFO("Fog of war: %d tiles visible for player 0 (of %d total)",
                 visCount, this->m_hexGrid.tileCount());
    }

    // -- HUD --
    {
        const std::pair<uint32_t, uint32_t> initFbSize = this->m_window.framebufferSize();
        const float initW = static_cast<float>(initFbSize.first);
        const float initH = static_cast<float>(initFbSize.second);
        this->m_uiManager.setScreenSize(initW, initH);
    }
    this->buildHUD();

    // Camera was already centered on the settler by spawnStartingEntities().
    this->m_appState = AppState::InGame;
    LOG_INFO("Game started (map type=%d, size=%d, players=%u)",
             static_cast<int>(config.mapType), static_cast<int>(config.mapSize),
             static_cast<unsigned>(config.playerCount));
}

void Application::run() {
    if (!this->m_initialized) {
        return;
    }

    using Clock = std::chrono::steady_clock;
    Clock::time_point previousTime = Clock::now();
    float fpsAccum = 0.0f;
    int32_t fpsFrameCount = 0;

    while (!this->m_window.shouldClose()) {
        Clock::time_point currentTime = Clock::now();
        float deltaTime = std::chrono::duration<float>(currentTime - previousTime).count();
        previousTime = currentTime;

        // FPS counter: update window title every 0.5 seconds
        fpsAccum += deltaTime;
        ++fpsFrameCount;
        if (this->m_settingsMenu.settings().showFPS && fpsAccum >= 0.5f) {
            float fps = static_cast<float>(fpsFrameCount) / fpsAccum;
            std::string title = "Age of Civilization - " + std::to_string(static_cast<int>(fps)) + " FPS";
            glfwSetWindowTitle(this->m_window.handle(), title.c_str());
            fpsAccum = 0.0f;
            fpsFrameCount = 0;
        } else if (!this->m_settingsMenu.settings().showFPS && fpsFrameCount == 1) {
            glfwSetWindowTitle(this->m_window.handle(), "Age of Civilization");
        }
        if (deltaTime > 0.1f) {
            deltaTime = 0.1f;
        }

        this->m_inputManager.processFrame();
        this->m_window.pollEvents();

        std::pair<uint32_t, uint32_t> fbSizePair = this->m_window.framebufferSize();
        uint32_t fbWidth = fbSizePair.first;
        uint32_t fbHeight = fbSizePair.second;

        // Detect framebuffer size changes that the GLFW callback may have missed
        // (e.g., window manager fullscreen toggle on Wayland)
        if (fbWidth > 0 && fbHeight > 0) {
            VkExtent2D currentExtent = this->m_renderPipeline->extent();
            if (currentExtent.width != fbWidth || currentExtent.height != fbHeight) {
                this->m_renderPipeline->resize(fbWidth, fbHeight);
            }
        }

        // ================================================================
        // Main Menu state
        // ================================================================
        if (this->m_appState == AppState::MainMenu) {
            // Escape: close settings > close game setup > quit.
            if (this->m_inputManager.isActionPressed(InputAction::Cancel)) {
                if (this->m_settingsMenu.isBuilt()) {
                    this->m_settingsMenu.destroy(this->m_uiManager);
                } else if (this->m_gameSetupScreen.isBuilt()) {
                    this->m_gameSetupScreen.destroy(this->m_uiManager);
                    const std::pair<uint32_t, uint32_t> menuSize = this->m_window.framebufferSize();
                    this->buildMainMenu(static_cast<float>(menuSize.first), static_cast<float>(menuSize.second));
                } else {
                    break;
                }
            }

            // UI input
            const bool leftPressed  = this->m_inputManager.isMouseButtonPressed(GLFW_MOUSE_BUTTON_LEFT);
            const bool leftReleased = this->m_inputManager.isMouseButtonReleased(GLFW_MOUSE_BUTTON_LEFT);
            const float scrollDelta = static_cast<float>(this->m_inputManager.scrollDelta());
            this->m_uiManager.handleInput(
                static_cast<float>(this->m_inputManager.mouseX()),
                static_cast<float>(this->m_inputManager.mouseY()),
                leftPressed, leftReleased, scrollDelta);

            // Update layout on resize
            this->m_mainMenu.updateLayout(
                this->m_uiManager, static_cast<float>(fbWidth), static_cast<float>(fbHeight));

            // Render: UI only (no world-space pass)
            if (fbWidth == 0 || fbHeight == 0) {
                continue;
            }

            vulkan_app::RenderPipeline::FrameContext frame = this->m_renderPipeline->beginFrame();
            if (!frame) {
                continue;
            }

            // Sync Renderer2D extent with swapchain (may have changed after recreation)
            this->m_renderer2d->setExtent(frame.extent);

            this->m_renderPipeline->beginRenderPass(frame);

            this->m_renderer2d->resetCamera();
            this->m_renderer2d->setZoom(1.0f);
            this->m_renderer2d->beginFrame(frame.frameIndex);
            this->m_renderer2d->begin();

            this->m_uiManager.setScreenSize(
                static_cast<float>(frame.extent.width),
                static_cast<float>(frame.extent.height));
            this->m_uiManager.layout();
            this->m_uiManager.render(*this->m_renderer2d);

            this->m_renderer2d->end(frame.commandBuffer);

            this->m_renderPipeline->endRenderPass(frame);
            this->m_renderPipeline->endFrame(frame);
            continue;
        }

        // ================================================================
        // InGame state
        // ================================================================

        // -- Debug Console (backtick key toggles, intercepts input when open) --
        if (this->m_inputManager.isKeyPressed(GLFW_KEY_GRAVE_ACCENT)) {
            this->m_debugConsole.toggle();
        }
        if (this->m_debugConsole.isOpen()) {
            // Route character input to console
            for (int32_t ki = GLFW_KEY_A; ki <= GLFW_KEY_Z; ++ki) {
                if (this->m_inputManager.isKeyPressed(ki)) {
                    bool shift = this->m_inputManager.isKeyHeld(GLFW_KEY_LEFT_SHIFT)
                              || this->m_inputManager.isKeyHeld(GLFW_KEY_RIGHT_SHIFT);
                    char c = static_cast<char>(ki - GLFW_KEY_A + (shift ? 'A' : 'a'));
                    this->m_debugConsole.addChar(c);
                }
            }
            for (int32_t ki = GLFW_KEY_0; ki <= GLFW_KEY_9; ++ki) {
                if (this->m_inputManager.isKeyPressed(ki)) {
                    this->m_debugConsole.addChar(static_cast<char>('0' + ki - GLFW_KEY_0));
                }
            }
            if (this->m_inputManager.isKeyPressed(GLFW_KEY_SPACE)) {
                this->m_debugConsole.addChar(' ');
            }
            if (this->m_inputManager.isKeyPressed(GLFW_KEY_MINUS)) {
                this->m_debugConsole.addChar('-');
            }
            if (this->m_inputManager.isKeyPressed(GLFW_KEY_PERIOD)) {
                this->m_debugConsole.addChar('.');
            }
            if (this->m_inputManager.isKeyPressed(GLFW_KEY_BACKSPACE)) {
                this->m_debugConsole.backspace();
            }
            if (this->m_inputManager.isKeyPressed(GLFW_KEY_ENTER)) {
                this->m_debugConsole.execute(this->m_world, this->m_hexGrid,
                                              this->m_fogOfWar, 0);
            }
        }

        // Camera update is deferred until after UI input to prevent scroll conflicts
        // (see below after UI input handling)

        // -- Animated unit movement: advance animProgress each frame --
        {
            aoc::ecs::ComponentPool<aoc::sim::UnitComponent>* animPool =
                this->m_world.getPool<aoc::sim::UnitComponent>();
            if (animPool != nullptr) {
                constexpr float ANIM_DURATION = 0.2f;
                for (uint32_t ai = 0; ai < animPool->size(); ++ai) {
                    aoc::sim::UnitComponent& animUnit = animPool->data()[ai];
                    if (animUnit.isAnimating) {
                        animUnit.animProgress += deltaTime / ANIM_DURATION;
                        if (animUnit.animProgress >= 1.0f) {
                            animUnit.animProgress = 1.0f;
                            animUnit.isAnimating = false;
                        }
                    }
                }
            }
        }

        // -- Update combat animations and particles --
        this->m_gameRenderer.combatAnimator().update(deltaTime);
        this->m_gameRenderer.particleSystem().update(deltaTime);

        // -- Update notifications --
        this->m_notificationManager.update(deltaTime);

        // -- Cycle to next unit needing orders (Tab key) --
        if (this->m_inputManager.isActionPressed(InputAction::CycleNextUnit) && !this->anyScreenOpen()) {
            aoc::ecs::ComponentPool<aoc::sim::UnitComponent>* cyclePool =
                this->m_world.getPool<aoc::sim::UnitComponent>();
            if (cyclePool != nullptr) {
                EntityId nextUnit = NULL_ENTITY;
                for (uint32_t ci = 0; ci < cyclePool->size(); ++ci) {
                    const aoc::sim::UnitComponent& cu = cyclePool->data()[ci];
                    if (cu.owner != 0) {
                        continue;
                    }
                    if (cu.movementRemaining <= 0) {
                        continue;
                    }
                    if (cu.state == aoc::sim::UnitState::Sleeping ||
                        cu.state == aoc::sim::UnitState::Fortified) {
                        continue;
                    }
                    if (!cu.pendingPath.empty()) {
                        continue;
                    }
                    nextUnit = cyclePool->entities()[ci];
                    break;
                }
                if (nextUnit.isValid()) {
                    this->m_selectedEntity = nextUnit;
                    const aoc::sim::UnitComponent& selU =
                        this->m_world.getComponent<aoc::sim::UnitComponent>(nextUnit);
                    float ucx = 0.0f, ucy = 0.0f;
                    hex::axialToPixel(selU.position,
                                      this->m_gameRenderer.mapRenderer().hexSize(), ucx, ucy);
                    this->m_cameraController.setPosition(ucx, ucy);
                }
            }
        }

        // -- Escape: close any open screen first, then quit --
        if (this->m_inputManager.isActionPressed(InputAction::Cancel)) {
            if (this->anyScreenOpen()) {
                this->closeAllScreens();
            } else {
                break;
            }
        }

        // -- Toggle tile yield display (Y key) --
        if (this->m_inputManager.isKeyPressed(GLFW_KEY_Y) && !this->m_debugConsole.isOpen()) {
            this->m_settingsMenu.settings().showTileYields =
                !this->m_settingsMenu.settings().showTileYields;
            this->m_gameRenderer.showTileYields =
                this->m_settingsMenu.settings().showTileYields;
            this->m_notificationManager.push(
                this->m_gameRenderer.showTileYields
                    ? "Tile yields: ON" : "Tile yields: OFF",
                2.0f, 0.8f, 0.8f, 0.8f);
        }

        // -- Screen toggle keys --
        if (this->m_inputManager.isActionPressed(InputAction::OpenTechTree)) {
            this->m_techScreen.setContext(&this->m_world, 0);
            this->m_techScreen.toggle(this->m_uiManager);
        }
        if (this->m_inputManager.isActionPressed(InputAction::OpenEconomy)) {
            this->m_economyScreen.setContext(&this->m_world, &this->m_hexGrid, 0, &this->m_economy.market());
            this->m_economyScreen.toggle(this->m_uiManager);
        }
        if (this->m_inputManager.isActionPressed(InputAction::OpenGovernment)) {
            this->m_governmentScreen.setContext(&this->m_world, 0);
            this->m_governmentScreen.toggle(this->m_uiManager);
        }
        if (this->m_inputManager.isActionPressed(InputAction::OpenReligion)) {
            this->m_religionScreen.setContext(&this->m_world, &this->m_hexGrid, 0);
            this->m_religionScreen.toggle(this->m_uiManager);
        }
        if (this->m_inputManager.isActionPressed(InputAction::OpenProductionPicker)) {
            // Only open if a city is selected
            if (this->m_selectedEntity.isValid() &&
                this->m_world.isAlive(this->m_selectedEntity) &&
                this->m_world.hasComponent<aoc::sim::CityComponent>(this->m_selectedEntity) &&
                this->m_world.getComponent<aoc::sim::CityComponent>(this->m_selectedEntity).owner == 0) {
                this->m_productionScreen.setContext(
                    &this->m_world, &this->m_hexGrid, this->m_selectedEntity, 0);
                this->m_productionScreen.toggle(this->m_uiManager);
            }
        }

        // -- Unit upgrade (U key) --
        if (this->m_inputManager.isActionPressed(InputAction::UpgradeUnit)) {
            if (this->m_selectedEntity.isValid() &&
                this->m_world.isAlive(this->m_selectedEntity) &&
                this->m_world.hasComponent<aoc::sim::UnitComponent>(this->m_selectedEntity)) {
                const aoc::sim::UnitComponent& selUnit =
                    this->m_world.getComponent<aoc::sim::UnitComponent>(this->m_selectedEntity);
                const std::vector<aoc::sim::UnitUpgradeDef> upgrades =
                    aoc::sim::getAvailableUpgrades(selUnit.typeId);
                if (!upgrades.empty()) {
                    // Try the first available upgrade
                    const aoc::sim::UnitUpgradeDef& upg = upgrades[0];
                    bool success = aoc::sim::upgradeUnit(
                        this->m_world, this->m_selectedEntity, upg.to, selUnit.owner);
                    if (success) {
                        this->m_eventLog.addEvent("Unit upgraded!");
                    }
                }
            }
        }

        // -- Help overlay (F1) --
        if (this->m_inputManager.isActionPressed(InputAction::ShowHelp)) {
            if (this->m_helpOverlay != aoc::ui::INVALID_WIDGET) {
                this->m_uiManager.removeWidget(this->m_helpOverlay);
                this->m_helpOverlay = aoc::ui::INVALID_WIDGET;
            } else {
                const float sw = static_cast<float>(fbWidth);
                const float sh = static_cast<float>(fbHeight);
                constexpr float HELP_W = 380.0f;
                constexpr float HELP_H = 400.0f;
                this->m_helpOverlay = this->m_uiManager.createPanel(
                    {0.0f, 0.0f, sw, sh},
                    aoc::ui::PanelData{{0.0f, 0.0f, 0.0f, 0.5f}, 0.0f});

                aoc::ui::WidgetId helpInner = this->m_uiManager.createPanel(
                    this->m_helpOverlay,
                    {(sw - HELP_W) * 0.5f, (sh - HELP_H) * 0.5f, HELP_W, HELP_H},
                    aoc::ui::PanelData{{0.10f, 0.10f, 0.15f, 0.95f}, 6.0f});
                {
                    aoc::ui::Widget* inner = this->m_uiManager.getWidget(helpInner);
                    if (inner != nullptr) {
                        inner->padding = {12.0f, 12.0f, 12.0f, 12.0f};
                        inner->childSpacing = 3.0f;
                    }
                }

                (void)this->m_uiManager.createLabel(helpInner,
                    {0.0f, 0.0f, 356.0f, 22.0f},
                    aoc::ui::LabelData{"Keyboard Shortcuts", {1.0f, 0.9f, 0.5f, 1.0f}, 16.0f});

                constexpr std::array<std::pair<const char*, const char*>, 16> SHORTCUTS = {{
                    {"WASD / Right-drag", "Pan camera"},
                    {"Scroll wheel / +/-", "Zoom"},
                    {"Left click", "Select unit/city"},
                    {"Right click", "Move unit / Context action"},
                    {"Enter", "End Turn"},
                    {"T", "Tech tree"},
                    {"G", "Government"},
                    {"E", "Economy"},
                    {"P", "Production (city selected)"},
                    {"U", "Upgrade unit"},
                    {"Y", "Toggle tile yield display"},
                    {"`", "Debug console"},
                    {"F5", "Quick save"},
                    {"F9", "Quick load"},
                    {"ESC", "Close screen / Menu"},
                    {"F1", "This help"},
                }};

                for (const std::pair<const char*, const char*>& shortcut : SHORTCUTS) {
                    const std::string line = std::string(shortcut.first) + ": " + shortcut.second;
                    (void)this->m_uiManager.createLabel(helpInner,
                        {0.0f, 0.0f, 356.0f, 16.0f},
                        aoc::ui::LabelData{line, {0.8f, 0.8f, 0.85f, 1.0f}, 12.0f});
                }

                // Close button
                aoc::ui::ButtonData closeBtn;
                closeBtn.label = "Close [F1]";
                closeBtn.fontSize = 12.0f;
                closeBtn.normalColor = {0.3f, 0.15f, 0.15f, 0.9f};
                closeBtn.hoverColor = {0.45f, 0.2f, 0.2f, 0.9f};
                closeBtn.pressedColor = {0.2f, 0.1f, 0.1f, 0.9f};
                closeBtn.cornerRadius = 4.0f;
                closeBtn.onClick = [this]() {
                    if (this->m_helpOverlay != aoc::ui::INVALID_WIDGET) {
                        this->m_uiManager.removeWidget(this->m_helpOverlay);
                        this->m_helpOverlay = aoc::ui::INVALID_WIDGET;
                    }
                };
                (void)this->m_uiManager.createButton(helpInner,
                    {0.0f, 0.0f, 100.0f, 28.0f}, std::move(closeBtn));

                this->m_uiManager.layout();
            }
        }

        // -- Quick save/load --
        if (this->m_inputManager.isActionPressed(InputAction::QuickSave)) {
            ErrorCode saveResult = aoc::save::saveGame(
                "quicksave.aoc", this->m_world, this->m_hexGrid,
                this->m_turnManager, this->m_economy, this->m_diplomacy,
                this->m_fogOfWar, this->m_gameRng);
            if (saveResult != ErrorCode::Ok) {
                LOG_ERROR("Quick save failed: %.*s",
                          static_cast<int>(describeError(saveResult).size()),
                          describeError(saveResult).data());
            }
        }
        if (this->m_inputManager.isActionPressed(InputAction::QuickLoad)) {
            ErrorCode loadResult = aoc::save::loadGame(
                "quicksave.aoc", this->m_world, this->m_hexGrid,
                this->m_turnManager, this->m_economy, this->m_diplomacy,
                this->m_fogOfWar, this->m_gameRng);
            if (loadResult != ErrorCode::Ok) {
                LOG_ERROR("Quick load failed: %.*s",
                          static_cast<int>(describeError(loadResult).size()),
                          describeError(loadResult).data());
            } else {
                // Re-initialize economy (rebuild production chain from loaded state)
                this->m_economy.initialize();

                // Re-initialize fog of war from loaded state for all players
                this->m_fogOfWar.initialize(this->m_hexGrid.tileCount(), MAX_PLAYERS);
                this->m_fogOfWar.updateVisibility(this->m_world, this->m_hexGrid, 0);
                for (const aoc::sim::ai::AIController& ai : this->m_aiControllers) {
                    this->m_fogOfWar.updateVisibility(this->m_world, this->m_hexGrid, ai.player());
                }
            }
        }

        // -- UI input (consumes clicks on widgets) --
        {
            const bool leftPressed  = this->m_inputManager.isMouseButtonPressed(GLFW_MOUSE_BUTTON_LEFT);
            const bool leftReleased = this->m_inputManager.isMouseButtonReleased(GLFW_MOUSE_BUTTON_LEFT);
            const float scrollDelta = static_cast<float>(this->m_inputManager.scrollDelta());
            this->m_uiConsumedInput = this->m_uiManager.handleInput(
                static_cast<float>(this->m_inputManager.mouseX()),
                static_cast<float>(this->m_inputManager.mouseY()),
                leftPressed, leftReleased, scrollDelta);
        }

        // -- Camera update (after UI so scroll doesn't zoom when scrolling menus) --
        if (this->m_uiConsumedInput || this->m_debugConsole.isOpen()) {
            this->m_inputManager.consumeScroll();
        }
        this->m_cameraController.update(this->m_inputManager, deltaTime, fbWidth, fbHeight);

        // -- Minimap click detection --
        constexpr float MINIMAP_W = 200.0f;
        constexpr float MINIMAP_H = 130.0f;
        constexpr float MINIMAP_MARGIN = 10.0f;
        const float minimapX = MINIMAP_MARGIN;
        const float minimapY = static_cast<float>(fbHeight) - MINIMAP_H - MINIMAP_MARGIN;

        if (this->m_inputManager.isMouseButtonPressed(GLFW_MOUSE_BUTTON_LEFT)
            && !this->m_uiConsumedInput) {
            const float mouseXf = static_cast<float>(this->m_inputManager.mouseX());
            const float mouseYf = static_cast<float>(this->m_inputManager.mouseY());
            if (this->m_gameRenderer.minimap().containsPoint(
                    mouseXf, mouseYf, minimapX, minimapY, MINIMAP_W, MINIMAP_H)) {
                float worldX = 0.0f;
                float worldY = 0.0f;
                this->m_gameRenderer.minimap().screenToWorld(
                    mouseXf, mouseYf, minimapX, minimapY, MINIMAP_W, MINIMAP_H,
                    this->m_hexGrid,
                    this->m_gameRenderer.mapRenderer().hexSize(),
                    worldX, worldY);
                this->m_cameraController.setPosition(worldX, worldY);
                this->m_uiConsumedInput = true;
            }
        }

        // -- Game input (only if UI didn't consume it and no screen is open) --
        if (!this->m_uiConsumedInput && !this->anyScreenOpen()) {
            this->handleSelect();
            this->handleContextAction();
            this->handleUndoAction();
        }
        // When only the city detail panel is open (right-side, non-blocking),
        // allow map interactions on the MAP area (left of the city panel).
        // Don't check m_uiConsumedInput — the HUD widgets shouldn't block tile clicks.
        if (this->onlyCityDetailScreenOpen()
            && this->m_world.hasComponent<aoc::sim::CityComponent>(this->m_cityDetailScreen.cityEntity())
            && this->m_inputManager.mouseX() < static_cast<double>(fbWidth) - 350.0) {
            // Left-click on a tile: toggle worker assignment
            if (this->m_inputManager.isMouseButtonPressed(GLFW_MOUSE_BUTTON_LEFT)) {
                const std::pair<uint32_t, uint32_t> workerFbSize = this->m_window.framebufferSize();
                float worldX = 0.0f;
                float worldY = 0.0f;
                this->m_cameraController.screenToWorld(
                    this->m_inputManager.mouseX(), this->m_inputManager.mouseY(),
                    worldX, worldY, workerFbSize.first, workerFbSize.second);
                const float hexSize = this->m_gameRenderer.mapRenderer().hexSize();
                const aoc::hex::AxialCoord clickedTile =
                    aoc::hex::pixelToAxial(worldX, worldY, hexSize);

                if (this->m_hexGrid.isValid(clickedTile)) {
                    // Check if clicked on a unit or different city → close panel and select
                    bool clickedOtherEntity = false;
                    aoc::ecs::ComponentPool<aoc::sim::UnitComponent>* unitPool =
                        this->m_world.getPool<aoc::sim::UnitComponent>();
                    if (unitPool != nullptr) {
                        for (uint32_t ui2 = 0; ui2 < unitPool->size(); ++ui2) {
                            if (unitPool->data()[ui2].position == clickedTile
                                && unitPool->data()[ui2].owner == 0) {
                                // Clicked on own unit: close city panel, select unit
                                this->m_cityDetailScreen.close(this->m_uiManager);
                                this->m_selectedEntity = unitPool->entities()[ui2];
                                clickedOtherEntity = true;
                                break;
                            }
                        }
                    }
                    if (!clickedOtherEntity) {
                        aoc::ecs::ComponentPool<aoc::sim::CityComponent>* cityPool =
                            this->m_world.getPool<aoc::sim::CityComponent>();
                        if (cityPool != nullptr) {
                            for (uint32_t ci2 = 0; ci2 < cityPool->size(); ++ci2) {
                                if (cityPool->data()[ci2].location == clickedTile
                                    && cityPool->data()[ci2].owner == 0
                                    && cityPool->entities()[ci2] != this->m_cityDetailScreen.cityEntity()) {
                                    // Clicked on different own city: switch to it
                                    this->m_cityDetailScreen.close(this->m_uiManager);
                                    this->m_selectedEntity = cityPool->entities()[ci2];
                                    this->m_cityDetailScreen.setContext(
                                        &this->m_world, &this->m_hexGrid,
                                        this->m_selectedEntity, 0);
                                    this->m_cityDetailScreen.open(this->m_uiManager);
                                    clickedOtherEntity = true;
                                    break;
                                }
                            }
                        }
                    }
                    if (!clickedOtherEntity) {
                        // Normal tile click: toggle worker
                        this->m_cityDetailScreen.toggleWorkerOnTile(clickedTile);
                        this->m_cityDetailScreen.refresh(this->m_uiManager);
                    }
                }
            }
            // Right-click: tile buying (existing context action logic)
            this->handleContextAction();
        }
        if (!this->anyScreenOpen() && this->m_inputManager.isActionPressed(InputAction::EndTurn)) {
            this->handleEndTurn();
        }

        // Refresh any open screens
        this->m_productionScreen.refresh(this->m_uiManager);
        this->m_techScreen.refresh(this->m_uiManager);
        this->m_governmentScreen.refresh(this->m_uiManager);
        this->m_economyScreen.refresh(this->m_uiManager);
        this->m_cityDetailScreen.refresh(this->m_uiManager);
        this->m_tradeScreen.refresh(this->m_uiManager);
        this->m_diplomacyScreen.refresh(this->m_uiManager);
        this->m_religionScreen.refresh(this->m_uiManager);
        this->m_scoreScreen.refresh(this->m_uiManager);

        // Update tooltip when mouse is not over UI and no blocking screen is open
        if ((!this->anyScreenOpen() || this->onlyCityDetailScreenOpen())
            && !this->m_uiConsumedInput
            && this->m_uiManager.hoveredWidget() == aoc::ui::INVALID_WIDGET) {
            this->m_gameRenderer.tooltipManager().update(
                static_cast<float>(this->m_inputManager.mouseX()),
                static_cast<float>(this->m_inputManager.mouseY()),
                this->m_world, this->m_hexGrid,
                this->m_cameraController, this->m_fogOfWar,
                PlayerId{0}, fbWidth, fbHeight,
                this->m_selectedEntity);
        } else {
            // Mouse is over UI - hide the map tooltip
            this->m_gameRenderer.tooltipManager().hide();
        }

        // Sync selection to renderer and update HUD text
        this->m_gameRenderer.unitRenderer().selectedEntity = this->m_selectedEntity;
        this->updateHUD();

        // -- Render --
        if (fbWidth == 0 || fbHeight == 0) {
            continue;
        }

        vulkan_app::RenderPipeline::FrameContext frame = this->m_renderPipeline->beginFrame();
        if (!frame) {
            continue;
        }

        // Sync Renderer2D extent with swapchain (may have changed after recreation)
        this->m_renderer2d->setExtent(frame.extent);
        // Update fbWidth/fbHeight to match the actual swapchain extent
        fbWidth = frame.extent.width;
        fbHeight = frame.extent.height;

        this->m_renderPipeline->beginRenderPass(frame);

        this->m_gameRenderer.render(
            *this->m_renderer2d,
            frame.commandBuffer,
            frame.frameIndex,
            this->m_cameraController,
            this->m_hexGrid,
            this->m_world,
            this->m_fogOfWar,
            PlayerId{0},
            this->m_uiManager,
            frame.extent.width, frame.extent.height,
            &this->m_eventLog,
            &this->m_notificationManager,
            &this->m_tutorialManager);

        // Debug console overlay (own begin/end batch, screen-space)
        if (this->m_debugConsole.isOpen()) {
            this->m_renderer2d->begin();

            const float consoleW = static_cast<float>(frame.extent.width);
            const float consoleH = 250.0f;
            const float consoleY = static_cast<float>(frame.extent.height) - consoleH;

            this->m_renderer2d->drawFilledRect(
                0.0f, consoleY, consoleW, consoleH,
                0.0f, 0.0f, 0.0f, 0.85f);

            float lineY = consoleY + 5.0f;
            for (const std::string& line : this->m_debugConsole.history()) {
                aoc::ui::BitmapFont::drawText(
                    *this->m_renderer2d, line,
                    5.0f, lineY, 12.0f,
                    aoc::ui::Color{0.85f, 0.85f, 0.85f, 1.0f});
                lineY += 14.0f;
            }

            std::string inputLine = "> " + this->m_debugConsole.input() + "_";
            aoc::ui::BitmapFont::drawText(
                *this->m_renderer2d, inputLine,
                5.0f, consoleY + consoleH - 20.0f, 14.0f,
                aoc::ui::Color{0.0f, 1.0f, 0.0f, 1.0f});

            this->m_renderer2d->end(frame.commandBuffer);
        }

        this->m_renderPipeline->endRenderPass(frame);
        this->m_renderPipeline->endFrame(frame);
    }

    this->m_graphicsDevice->waitIdle();
}

void Application::showReturnToMenuConfirm() {
    if (this->m_confirmDialog != aoc::ui::INVALID_WIDGET) {
        return;  // Already showing
    }

    const std::pair<uint32_t, uint32_t> confirmFbSize = this->m_window.framebufferSize();
    float screenW = static_cast<float>(confirmFbSize.first);
    float screenH = static_cast<float>(confirmFbSize.second);

    // Dark overlay + centered dialog
    this->m_confirmDialog = this->m_uiManager.createPanel(
        {0.0f, 0.0f, screenW, screenH},
        aoc::ui::PanelData{{0.0f, 0.0f, 0.0f, 0.5f}, 0.0f});

    constexpr float DLG_W = 340.0f;
    constexpr float DLG_H = 160.0f;
    aoc::ui::WidgetId dlgPanel = this->m_uiManager.createPanel(
        this->m_confirmDialog,
        {(screenW - DLG_W) * 0.5f, (screenH - DLG_H) * 0.5f, DLG_W, DLG_H},
        aoc::ui::PanelData{{0.10f, 0.10f, 0.14f, 0.95f}, 6.0f});
    {
        aoc::ui::Widget* dp = this->m_uiManager.getWidget(dlgPanel);
        dp->padding = {15.0f, 15.0f, 15.0f, 15.0f};
        dp->childSpacing = 12.0f;
    }

    // Question text
    [[maybe_unused]] aoc::ui::WidgetId questionLabel = this->m_uiManager.createLabel(
        dlgPanel, {0.0f, 0.0f, 310.0f, 20.0f},
        aoc::ui::LabelData{"Save before returning to menu?", {1.0f, 0.9f, 0.6f, 1.0f}, 15.0f});

    // Button row
    aoc::ui::WidgetId btnRow = this->m_uiManager.createPanel(
        dlgPanel, {0.0f, 0.0f, 310.0f, 34.0f},
        aoc::ui::PanelData{{0.0f, 0.0f, 0.0f, 0.0f}, 0.0f});
    {
        aoc::ui::Widget* row = this->m_uiManager.getWidget(btnRow);
        row->layoutDirection = aoc::ui::LayoutDirection::Horizontal;
        row->childSpacing = 10.0f;
    }

    // auto required: lambda type is unnameable
    auto makeDlgBtn = [this](aoc::ui::WidgetId parent, const std::string& label,
                              aoc::ui::Color normalColor, std::function<void()> onClick) {
        aoc::ui::ButtonData btn;
        btn.label = label;
        btn.fontSize = 13.0f;
        btn.normalColor = normalColor;
        btn.hoverColor = {normalColor.r + 0.1f, normalColor.g + 0.1f, normalColor.b + 0.1f, 0.9f};
        btn.pressedColor = {normalColor.r - 0.05f, normalColor.g - 0.05f, normalColor.b - 0.05f, 0.9f};
        btn.labelColor = {1.0f, 1.0f, 1.0f, 1.0f};
        btn.cornerRadius = 4.0f;
        btn.onClick = std::move(onClick);
        [[maybe_unused]] aoc::ui::WidgetId id = this->m_uiManager.createButton(
            parent, {0.0f, 0.0f, 95.0f, 34.0f}, std::move(btn));
    };

    // "Save & Exit" button
    makeDlgBtn(btnRow, "Save", {0.15f, 0.40f, 0.15f, 0.9f}, [this]() {
        [[maybe_unused]] ErrorCode saveResult = aoc::save::saveGame(
            "quicksave.aoc", this->m_world, this->m_hexGrid,
            this->m_turnManager, this->m_economy, this->m_diplomacy,
            this->m_fogOfWar, this->m_gameRng);
        LOG_INFO("Game saved before returning to menu");
        this->m_uiManager.removeWidget(this->m_confirmDialog);
        this->m_confirmDialog = aoc::ui::INVALID_WIDGET;
        this->returnToMainMenu();
    });

    // "Don't Save" button
    makeDlgBtn(btnRow, "Don't Save", {0.50f, 0.20f, 0.20f, 0.9f}, [this]() {
        this->m_uiManager.removeWidget(this->m_confirmDialog);
        this->m_confirmDialog = aoc::ui::INVALID_WIDGET;
        this->returnToMainMenu();
    });

    // "Cancel" button
    makeDlgBtn(btnRow, "Cancel", {0.25f, 0.25f, 0.30f, 0.9f}, [this]() {
        this->m_uiManager.removeWidget(this->m_confirmDialog);
        this->m_confirmDialog = aoc::ui::INVALID_WIDGET;
    });
}

void Application::returnToMainMenu() {
    // Close all open screens and menus
    this->closeAllScreens();
    if (this->m_menuDropdown != aoc::ui::INVALID_WIDGET) {
        this->m_uiManager.removeWidget(this->m_menuDropdown);
        this->m_menuDropdown = aoc::ui::INVALID_WIDGET;
    }
    if (this->m_settingsMenu.isBuilt()) {
        this->m_settingsMenu.destroy(this->m_uiManager);
    }

    // Remove all HUD widgets by clearing the entire UI
    // (the main menu will rebuild its own widgets)
    // Remove known HUD widgets
    if (this->m_topBar != aoc::ui::INVALID_WIDGET) {
        this->m_uiManager.removeWidget(this->m_topBar);
        this->m_topBar = aoc::ui::INVALID_WIDGET;
    }
    if (this->m_endTurnButton != aoc::ui::INVALID_WIDGET) {
        this->m_uiManager.removeWidget(this->m_endTurnButton);
        this->m_endTurnButton = aoc::ui::INVALID_WIDGET;
    }
    // The info panel and victory panel are root widgets too
    // Simplest: just remove all widgets and rebuild
    // Reset all stored widget IDs
    this->m_turnLabel = aoc::ui::INVALID_WIDGET;
    this->m_selectionLabel = aoc::ui::INVALID_WIDGET;
    this->m_economyLabel = aoc::ui::INVALID_WIDGET;
    this->m_resourceLabel = aoc::ui::INVALID_WIDGET;
    this->m_victoryLabel = aoc::ui::INVALID_WIDGET;
    this->m_researchLabel = aoc::ui::INVALID_WIDGET;
    this->m_researchBar = aoc::ui::INVALID_WIDGET;
    this->m_researchBarFill = aoc::ui::INVALID_WIDGET;
    this->m_productionLabel = aoc::ui::INVALID_WIDGET;
    this->m_productionBar = aoc::ui::INVALID_WIDGET;
    this->m_productionBarFill = aoc::ui::INVALID_WIDGET;
    if (this->m_unitActionPanel != aoc::ui::INVALID_WIDGET) {
        this->m_uiManager.removeWidget(this->m_unitActionPanel);
        this->m_unitActionPanel = aoc::ui::INVALID_WIDGET;
    }
    this->m_actionPanelEntity = NULL_ENTITY;
    if (this->m_helpOverlay != aoc::ui::INVALID_WIDGET) {
        this->m_uiManager.removeWidget(this->m_helpOverlay);
        this->m_helpOverlay = aoc::ui::INVALID_WIDGET;
    }

    // Reset game state
    this->m_world = aoc::ecs::World{};
    this->m_hexGrid = aoc::map::HexGrid{};
    this->m_selectedEntity = NULL_ENTITY;
    this->m_gameOver = false;
    this->m_aiControllers.clear();

    // Switch to main menu
    this->m_appState = AppState::MainMenu;
    const std::pair<uint32_t, uint32_t> menuFbSize = this->m_window.framebufferSize();
    float screenW = static_cast<float>(menuFbSize.first);
    float screenH = static_cast<float>(menuFbSize.second);

    this->buildMainMenu(screenW, screenH);

    LOG_INFO("Returned to main menu");
}

void Application::buildMainMenu(float screenW, float screenH) {
    this->m_mainMenu.build(
        this->m_uiManager, screenW, screenH,
        [this, screenW, screenH]() {
            // "Start Game" opens the game setup screen
            this->m_mainMenu.destroy(this->m_uiManager);
            this->m_settingsMenu.destroy(this->m_uiManager);
            this->m_gameSetupScreen.build(
                this->m_uiManager, screenW, screenH,
                [this](const aoc::ui::GameSetupConfig& config) {
                    this->m_gameSetupScreen.destroy(this->m_uiManager);
                    this->startGame(config);
                },
                [this, screenW, screenH]() {
                    // Back to main menu
                    this->m_gameSetupScreen.destroy(this->m_uiManager);
                    this->buildMainMenu(screenW, screenH);
                });
        },
        [this]() {
            glfwSetWindowShouldClose(this->m_window.handle(), GLFW_TRUE);
        },
        [this, screenW, screenH]() {
            if (!this->m_settingsMenu.isBuilt()) {
                this->m_settingsMenu.build(
                    this->m_uiManager, screenW, screenH,
                    [this]() {
                        aoc::ui::saveSettings(this->m_settingsMenu.settings(), "settings.cfg");
                        this->m_settingsMenu.destroy(this->m_uiManager);
                        this->applySettings();
                    });
            }
        },
        [this]() {
            // Tutorial: start a default game with tutorial enabled
            this->m_mainMenu.destroy(this->m_uiManager);
            this->m_settingsMenu.destroy(this->m_uiManager);
            aoc::ui::GameSetupConfig tutorialConfig{};
            tutorialConfig.mapType = aoc::map::MapType::Continents;
            tutorialConfig.mapSize = aoc::map::MapSize::Small;
            tutorialConfig.playerCount = 2;
            tutorialConfig.players[0].isActive = true;
            tutorialConfig.players[0].isHuman  = true;
            tutorialConfig.players[0].civId    = 0;
            tutorialConfig.players[1].isActive = true;
            tutorialConfig.players[1].isHuman  = false;
            tutorialConfig.players[1].civId    = 1;
            this->startGame(tutorialConfig);
            this->m_tutorialManager.start();
        });
}

void Application::applySettings() {
    const aoc::ui::GameSettings& settings = this->m_settingsMenu.settings();

    // Fullscreen toggle. The GLFW framebuffer size callback will fire,
    // which calls onResize() to mark the swapchain for recreation.
    // The swapchain actually recreates on the next beginFrame(), where
    // we also update the Renderer2D extent from frame.extent.
    this->m_window.setFullscreen(settings.fullscreen);

    // Tile yield display setting
    this->m_gameRenderer.showTileYields = settings.showTileYields;

    LOG_INFO("Settings applied: fullscreen=%d vsync=%d showFPS=%d yields=%d vol=%d/%d/%d",
             settings.fullscreen ? 1 : 0, settings.vsync ? 1 : 0, settings.showFPS ? 1 : 0,
             settings.showTileYields ? 1 : 0,
             settings.masterVolume, settings.sfxVolume, settings.musicVolume);
}

void Application::shutdown() {
    if (!this->m_initialized) {
        return;
    }

    if (this->m_graphicsDevice) {
        this->m_graphicsDevice->waitIdle();
    }

    this->m_renderer2d.reset();
    this->m_renderPipeline.reset();
    this->m_graphicsDevice.reset();
    this->m_window.destroy();

    this->m_initialized = false;
    LOG_INFO("Shutdown complete");
}

void Application::onResize(uint32_t width, uint32_t height) {
    if (width == 0 || height == 0) {
        return;
    }
    // Mark swapchain for recreation on next beginFrame().
    // Don't call setExtent here -- extent() still returns the OLD size
    // until beginFrame() actually recreates the swapchain.
    this->m_renderPipeline->resize(width, height);

    // Update UI screen size so anchor-based layout adapts immediately.
    this->m_uiManager.setScreenSize(static_cast<float>(width),
                                     static_cast<float>(height));

    // Close and reopen any open game screens so they rebuild with new dimensions.
    // Modal screens use absolute pixel positions based on screen size at open() time.
    const float newW = static_cast<float>(width);
    const float newH = static_cast<float>(height);

    if (this->m_appState == AppState::InGame) {
        // Propagate new screen size to all screens before reopening
        this->m_productionScreen.setScreenSize(newW, newH);
        this->m_techScreen.setScreenSize(newW, newH);
        this->m_governmentScreen.setScreenSize(newW, newH);
        this->m_economyScreen.setScreenSize(newW, newH);
        this->m_tradeScreen.setScreenSize(newW, newH);
        this->m_diplomacyScreen.setScreenSize(newW, newH);
        this->m_religionScreen.setScreenSize(newW, newH);
        this->m_scoreScreen.setScreenSize(newW, newH);
        this->m_cityDetailScreen.setScreenSize(newW, newH);

        if (this->m_productionScreen.isOpen()) {
            this->m_productionScreen.close(this->m_uiManager);
            this->m_productionScreen.open(this->m_uiManager);
        }
        if (this->m_techScreen.isOpen()) {
            this->m_techScreen.close(this->m_uiManager);
            this->m_techScreen.open(this->m_uiManager);
        }
        if (this->m_governmentScreen.isOpen()) {
            this->m_governmentScreen.close(this->m_uiManager);
            this->m_governmentScreen.open(this->m_uiManager);
        }
        if (this->m_economyScreen.isOpen()) {
            this->m_economyScreen.close(this->m_uiManager);
            this->m_economyScreen.open(this->m_uiManager);
        }
        if (this->m_tradeScreen.isOpen()) {
            this->m_tradeScreen.close(this->m_uiManager);
            this->m_tradeScreen.open(this->m_uiManager);
        }
        if (this->m_diplomacyScreen.isOpen()) {
            this->m_diplomacyScreen.close(this->m_uiManager);
            this->m_diplomacyScreen.open(this->m_uiManager);
        }
        if (this->m_religionScreen.isOpen()) {
            this->m_religionScreen.close(this->m_uiManager);
            this->m_religionScreen.open(this->m_uiManager);
        }
        if (this->m_scoreScreen.isOpen()) {
            this->m_scoreScreen.close(this->m_uiManager);
            this->m_scoreScreen.open(this->m_uiManager);
        }
        if (this->m_cityDetailScreen.isOpen()) {
            this->m_cityDetailScreen.close(this->m_uiManager);
            this->m_cityDetailScreen.open(this->m_uiManager);
        }

        // Rebuild the unit action panel to pick up the new screen size
        this->rebuildUnitActionPanel();

        // Close menu dropdown if open (it uses absolute positioning)
        if (this->m_menuDropdown != aoc::ui::INVALID_WIDGET) {
            this->m_uiManager.removeWidget(this->m_menuDropdown);
            this->m_menuDropdown = aoc::ui::INVALID_WIDGET;
        }
    }
}

// ============================================================================
// Game input handlers
// ============================================================================

void Application::handleSelect() {
    if (!this->m_inputManager.isMouseButtonPressed(GLFW_MOUSE_BUTTON_LEFT)) {
        return;
    }

    const std::pair<uint32_t, uint32_t> selectFbSize = this->m_window.framebufferSize();
    const uint32_t fbWidth = selectFbSize.first;
    const uint32_t fbHeight = selectFbSize.second;
    float worldX = 0.0f, worldY = 0.0f;
    this->m_cameraController.screenToWorld(
        this->m_inputManager.mouseX(), this->m_inputManager.mouseY(),
        worldX, worldY, fbWidth, fbHeight);

    float hexSize = this->m_gameRenderer.mapRenderer().hexSize();
    hex::AxialCoord clickedTile = hex::pixelToAxial(worldX, worldY, hexSize);

    if (!this->m_hexGrid.isValid(clickedTile)) {
        this->m_selectedEntity = NULL_ENTITY;
        return;
    }

    // Use GameState for fast ownership check, then find the ECS EntityId
    // for backward compatibility with rendering and other ECS-based systems.
    aoc::game::Player* humanGs = this->m_gameState.humanPlayer();

    // Check if one of OUR units is on this tile via GameState
    if (humanGs != nullptr && humanGs->unitAt(clickedTile) != nullptr) {
        // Confirmed our unit is here; find the ECS entity for rendering
        aoc::ecs::ComponentPool<aoc::sim::UnitComponent>* unitPool =
            this->m_world.getPool<aoc::sim::UnitComponent>();
        if (unitPool != nullptr) {
            for (uint32_t i = 0; i < unitPool->size(); ++i) {
                if (unitPool->data()[i].position == clickedTile
                    && unitPool->data()[i].owner == 0) {
                    this->m_selectedEntity = unitPool->entities()[i];
                    return;
                }
            }
        }
    }

    // Check if one of OUR cities is on this tile via GameState
    if (humanGs != nullptr && humanGs->cityAt(clickedTile) != nullptr) {
        // Confirmed our city is here; find the ECS entity for rendering
        aoc::ecs::ComponentPool<aoc::sim::CityComponent>* cityPool =
            this->m_world.getPool<aoc::sim::CityComponent>();
        if (cityPool != nullptr) {
            for (uint32_t i = 0; i < cityPool->size(); ++i) {
                if (cityPool->data()[i].location == clickedTile
                    && cityPool->data()[i].owner == 0) {
                    this->m_selectedEntity = cityPool->entities()[i];
                    // Open city detail screen
                    this->m_cityDetailScreen.setContext(
                        &this->m_world, &this->m_hexGrid, this->m_selectedEntity, 0);
                    if (!this->m_cityDetailScreen.isOpen()) {
                        this->m_cityDetailScreen.open(this->m_uiManager);
                    }
                    return;
                }
            }
        }
    }

    this->m_selectedEntity = NULL_ENTITY;
}

void Application::handleContextAction() {
    // Only fire context action on right-click release WITHOUT drag.
    // Right-click + drag is used for camera panning.
    if (!this->m_inputManager.isMouseButtonReleased(GLFW_MOUSE_BUTTON_RIGHT)) {
        return;
    }
    // Skip if the mouse moved significantly during the press (it was a drag)
    constexpr double DRAG_THRESHOLD = 5.0;
    double dx = this->m_inputManager.mouseDeltaX();
    double dy = this->m_inputManager.mouseDeltaY();
    if (dx > DRAG_THRESHOLD || dx < -DRAG_THRESHOLD ||
        dy > DRAG_THRESHOLD || dy < -DRAG_THRESHOLD) {
        return;
    }

    if (!this->m_selectedEntity.isValid() || !this->m_world.isAlive(this->m_selectedEntity)) {
        return;
    }

    // Only allow actions on own entities
    const aoc::sim::UnitComponent* selUnit =
        this->m_world.tryGetComponent<aoc::sim::UnitComponent>(this->m_selectedEntity);
    if (selUnit != nullptr && selUnit->owner != 0) {
        return;  // Can't control other players' units
    }
    const aoc::sim::CityComponent* selCity =
        this->m_world.tryGetComponent<aoc::sim::CityComponent>(this->m_selectedEntity);
    if (selCity != nullptr && selCity->owner != 0) {
        return;  // Can't control other players' cities
    }

    const std::pair<uint32_t, uint32_t> contextFbSize = this->m_window.framebufferSize();
    const uint32_t fbWidth = contextFbSize.first;
    const uint32_t fbHeight = contextFbSize.second;
    float worldX = 0.0f, worldY = 0.0f;
    this->m_cameraController.screenToWorld(
        this->m_inputManager.mouseX(), this->m_inputManager.mouseY(),
        worldX, worldY, fbWidth, fbHeight);

    float hexSize = this->m_gameRenderer.mapRenderer().hexSize();
    hex::AxialCoord targetTile = hex::pixelToAxial(worldX, worldY, hexSize);

    if (!this->m_hexGrid.isValid(targetTile)) {
        return;
    }

    // Handle right-click on a selected city
    if (this->m_world.hasComponent<aoc::sim::CityComponent>(this->m_selectedEntity)) {
        aoc::sim::CityComponent& city =
            this->m_world.getComponent<aoc::sim::CityComponent>(this->m_selectedEntity);

        // Right-click on city itself: open production picker
        if (city.location == targetTile) {
            aoc::sim::ProductionQueueComponent* queue =
                this->m_world.tryGetComponent<aoc::sim::ProductionQueueComponent>(
                    this->m_selectedEntity);
            if (queue != nullptr && queue->isEmpty()) {
                aoc::sim::ProductionQueueItem item{};
                item.type = aoc::sim::ProductionItemType::Unit;
                item.itemId = 0;  // Warrior
                item.name = "Warrior";
                item.totalCost = 40.0f;
                item.progress = 0.0f;
                queue->queue.push_back(std::move(item));
                LOG_INFO("Enqueued Warrior in %s", city.name.c_str());
            }
            return;
        }

        // Right-click on unowned tile adjacent to player's border: buy it
        int32_t tileIdx = this->m_hexGrid.toIndex(targetTile);
        if (this->m_hexGrid.owner(tileIdx) == INVALID_PLAYER) {
            // Check if at least one neighbor is owned by this player
            bool adjacentToOwned = false;
            std::array<aoc::hex::AxialCoord, 6> neighbors = aoc::hex::neighbors(targetTile);
            for (const aoc::hex::AxialCoord& nbr : neighbors) {
                if (this->m_hexGrid.isValid(nbr) &&
                    this->m_hexGrid.owner(this->m_hexGrid.toIndex(nbr)) == 0) {
                    adjacentToOwned = true;
                    break;
                }
            }
            if (!adjacentToOwned) { return; }

            int32_t dist = aoc::hex::distance(city.location, targetTile);
            int32_t cost = 25 * std::max(1, dist);

            // Two-click confirmation: first click shows cost, second click on same tile confirms
            if (this->m_pendingBuyTile == targetTile && this->m_pendingBuyConfirm) {
                // Second click: execute purchase
                aoc::sim::PlayerEconomyComponent* econ = nullptr;
                this->m_world.forEach<aoc::sim::PlayerEconomyComponent>(
                    [&econ](EntityId, aoc::sim::PlayerEconomyComponent& ec) {
                        if (ec.owner == 0) { econ = &ec; }
                    });

                if (econ != nullptr && econ->treasury >= static_cast<CurrencyAmount>(cost)) {
                    econ->treasury -= static_cast<CurrencyAmount>(cost);
                    this->m_hexGrid.setOwner(tileIdx, 0);
                    city.tilesClaimedCount += 1;
                    this->m_notificationManager.push(
                        "Bought tile for " + std::to_string(cost) + " gold",
                        2.0f, 0.2f, 0.9f, 0.3f);
                } else {
                    this->m_notificationManager.push(
                        "Not enough gold! Need " + std::to_string(cost),
                        2.0f, 1.0f, 0.3f, 0.3f);
                }
                this->m_pendingBuyConfirm = false;
                this->m_pendingBuyTile = aoc::hex::AxialCoord{-9999, -9999};
            } else {
                // First click: show cost preview
                this->m_pendingBuyTile = targetTile;
                this->m_pendingBuyConfirm = true;
                this->m_notificationManager.push(
                    "Buy tile for " + std::to_string(cost) + " gold? Right-click again to confirm.",
                    3.0f, 1.0f, 0.9f, 0.4f);
            }
            return;
        }
        return;
    }

    // Activate Great Person on right-click at their own tile
    if (this->m_world.hasComponent<aoc::sim::GreatPersonComponent>(this->m_selectedEntity)) {
        const aoc::sim::GreatPersonComponent& gpCheck =
            this->m_world.getComponent<aoc::sim::GreatPersonComponent>(this->m_selectedEntity);
        if (gpCheck.position == targetTile && !gpCheck.isActivated) {
            aoc::sim::activateGreatPerson(this->m_world, this->m_hexGrid, this->m_selectedEntity);
            this->m_selectedEntity = NULL_ENTITY;
            return;
        }
    }

    // Only move units
    if (!this->m_world.hasComponent<aoc::sim::UnitComponent>(this->m_selectedEntity)) {
        return;
    }

    aoc::sim::UnitComponent& unit =
        this->m_world.getComponent<aoc::sim::UnitComponent>(this->m_selectedEntity);

    // Religious unit actions: right-click on a city
    {
        const aoc::sim::UnitTypeDef& relDef = aoc::sim::unitTypeDef(unit.typeId);
        if (relDef.unitClass == aoc::sim::UnitClass::Religious && unit.spreadCharges > 0) {
            // Check if target tile has a city
            const aoc::ecs::ComponentPool<aoc::sim::CityComponent>* cityPool =
                this->m_world.getPool<aoc::sim::CityComponent>();
            if (cityPool != nullptr) {
                for (uint32_t ci = 0; ci < cityPool->size(); ++ci) {
                    const aoc::sim::CityComponent& city = cityPool->data()[ci];
                    if (city.location != targetTile) {
                        continue;
                    }
                    EntityId cityEntity = cityPool->entities()[ci];

                    // Inquisitor: remove foreign religion from own city
                    if (unit.typeId.value == 21 && city.owner == unit.owner) {
                        aoc::sim::CityReligionComponent* cityRel =
                            this->m_world.tryGetComponent<aoc::sim::CityReligionComponent>(cityEntity);
                        if (cityRel != nullptr) {
                            for (uint8_t ri = 0; ri < aoc::sim::MAX_RELIGIONS; ++ri) {
                                if (ri != unit.spreadingReligion) {
                                    cityRel->pressure[ri] = 0.0f;
                                }
                            }
                            LOG_INFO("Inquisitor removed foreign religion from %s",
                                     city.name.c_str());
                        }
                        --unit.spreadCharges;
                        if (unit.spreadCharges <= 0) {
                            this->m_world.destroyEntity(this->m_selectedEntity);
                            this->m_selectedEntity = NULL_ENTITY;
                        }
                        return;
                    }

                    // Missionary/Apostle: spread religion to target city
                    if (unit.typeId.value == 19 || unit.typeId.value == 20) {
                        // Ensure city has a CityReligionComponent
                        if (!this->m_world.hasComponent<aoc::sim::CityReligionComponent>(cityEntity)) {
                            this->m_world.addComponent<aoc::sim::CityReligionComponent>(
                                cityEntity, aoc::sim::CityReligionComponent{});
                        }
                        aoc::sim::CityReligionComponent& cityRel =
                            this->m_world.getComponent<aoc::sim::CityReligionComponent>(cityEntity);

                        const float pressure = (unit.typeId.value == 19) ? 100.0f : 150.0f;
                        cityRel.addPressure(unit.spreadingReligion, pressure);
                        --unit.spreadCharges;

                        LOG_INFO("%.*s spread religion to %s (pressure +%d, charges left: %d)",
                                 static_cast<int>(relDef.name.size()), relDef.name.data(),
                                 city.name.c_str(), static_cast<int>(pressure),
                                 static_cast<int>(unit.spreadCharges));

                        if (unit.spreadCharges <= 0) {
                            this->m_world.destroyEntity(this->m_selectedEntity);
                            this->m_selectedEntity = NULL_ENTITY;
                        }
                        return;
                    }
                    break;
                }
            }
        }
    }

    // If settler and target is valid land, found a city
    const aoc::sim::UnitTypeDef& def = aoc::sim::unitTypeDef(unit.typeId);
    if (def.unitClass == aoc::sim::UnitClass::Settler && unit.position == targetTile) {
        // Found city at current position
        PlayerId cityOwner = unit.owner;
        hex::AxialCoord cityPos = unit.position;

        EntityId cityEntity = this->m_world.createEntity();
        const std::string cityName = aoc::sim::getNextCityName(this->m_world, cityOwner);
        aoc::sim::CityComponent newCity =
            aoc::sim::CityComponent::create(cityOwner, cityPos, cityName);

        // Check if this is the player's first city (original capital)
        bool hasExistingCity = false;
        const aoc::ecs::ComponentPool<aoc::sim::CityComponent>* existingCities =
            this->m_world.getPool<aoc::sim::CityComponent>();
        if (existingCities != nullptr) {
            for (uint32_t ci = 0; ci < existingCities->size(); ++ci) {
                if (existingCities->data()[ci].owner == cityOwner) {
                    hasExistingCity = true;
                    break;
                }
            }
        }
        if (!hasExistingCity) {
            newCity.isOriginalCapital = true;
            newCity.originalOwner = cityOwner;
        }

        this->m_world.addComponent<aoc::sim::CityComponent>(
            cityEntity, std::move(newCity));
        this->m_world.addComponent<aoc::sim::ProductionQueueComponent>(
            cityEntity, aoc::sim::ProductionQueueComponent{});

        aoc::sim::CityDistrictsComponent districts{};
        aoc::sim::CityDistrictsComponent::PlacedDistrict center;
        center.type = aoc::sim::DistrictType::CityCenter;
        center.location = cityPos;
        districts.districts.push_back(std::move(center));
        this->m_world.addComponent<aoc::sim::CityDistrictsComponent>(
            cityEntity, std::move(districts));

        // Attach religion component to new city
        this->m_world.addComponent<aoc::sim::CityReligionComponent>(
            cityEntity, aoc::sim::CityReligionComponent{});

        aoc::sim::claimInitialTerritory(this->m_hexGrid, cityPos, cityOwner);

        // Auto-assign workers to best tiles for the new city
        aoc::sim::CityComponent& foundedCity =
            this->m_world.getComponent<aoc::sim::CityComponent>(cityEntity);
        aoc::sim::autoAssignWorkers(foundedCity, this->m_hexGrid);

        // Mirror the new city into the GameState object model
        aoc::game::Player* gsFounder = this->m_gameState.player(cityOwner);
        if (gsFounder != nullptr) {
            aoc::game::City& gsCity = gsFounder->addCity(cityPos, cityName);
            gsCity.autoAssignWorkers(this->m_hexGrid);
        }

        this->m_world.destroyEntity(this->m_selectedEntity);
        this->m_selectedEntity = cityEntity;
        LOG_INFO("City founded!");

        // Eureka: FoundCity condition
        {
            aoc::game::Player* eurekaPlayer = this->m_gameState.player(cityOwner);
            if (eurekaPlayer != nullptr) {
                aoc::sim::checkEurekaConditions(*eurekaPlayer,
                                                aoc::sim::EurekaCondition::FoundCity);
            }
        }
        return;
    }

    // Builder: build improvement at current position
    if (def.unitClass == aoc::sim::UnitClass::Civilian && unit.position == targetTile) {
        int32_t tileIndex = this->m_hexGrid.toIndex(unit.position);
        aoc::map::ImprovementType bestImpr =
            aoc::sim::bestImprovementForTile(this->m_hexGrid, tileIndex);

        if (bestImpr != aoc::map::ImprovementType::None &&
            this->m_hexGrid.improvement(tileIndex) == aoc::map::ImprovementType::None) {
            this->m_hexGrid.setImprovement(tileIndex, bestImpr);

            aoc::sim::UnitComponent& builderUnit =
                this->m_world.getComponent<aoc::sim::UnitComponent>(this->m_selectedEntity);
            if (builderUnit.chargesRemaining > 0) {
                --builderUnit.chargesRemaining;
            }

            // Eureka: check if the built improvement triggers a boost
            if (bestImpr == aoc::map::ImprovementType::Quarry) {
                aoc::game::Player* eurekaPlayer2 = this->m_gameState.player(unit.owner);
                if (eurekaPlayer2 != nullptr) {
                    aoc::sim::checkEurekaConditions(*eurekaPlayer2,
                                                    aoc::sim::EurekaCondition::BuildQuarry);
                }
            }

            LOG_INFO("Builder placed improvement at (%d,%d)",
                     unit.position.q, unit.position.r);

            if (builderUnit.chargesRemaining == 0) {
                this->m_world.destroyEntity(this->m_selectedEntity);
                this->m_selectedEntity = NULL_ENTITY;
                LOG_INFO("Builder exhausted all charges");
            }
            return;
        }
    }

    // Embark: land unit right-clicking an adjacent water tile
    int32_t targetIndex = this->m_hexGrid.toIndex(targetTile);
    aoc::map::TerrainType targetTerrain = this->m_hexGrid.terrain(targetIndex);
    if (!aoc::sim::isNaval(def.unitClass) && unit.state != aoc::sim::UnitState::Embarked
        && targetTerrain == aoc::map::TerrainType::Coast
        && hex::distance(unit.position, targetTile) == 1) {
        (void)aoc::sim::tryEmbark(this->m_world, this->m_selectedEntity, targetTile, this->m_hexGrid);
        return;
    }

    // Disembark: embarked unit right-clicking an adjacent land tile
    if (unit.state == aoc::sim::UnitState::Embarked
        && !aoc::map::isWater(targetTerrain) && !aoc::map::isImpassable(targetTerrain)
        && hex::distance(unit.position, targetTile) == 1) {
        (void)aoc::sim::tryDisembark(this->m_world, this->m_selectedEntity, targetTile, this->m_hexGrid);
        return;
    }

    // Save undo state before movement
    this->m_undoState.entity = this->m_selectedEntity;
    this->m_undoState.previousPosition = unit.position;
    this->m_undoState.previousMovement = unit.movementRemaining;
    this->m_undoState.hasState = true;

    // Order movement
    bool pathFound = aoc::sim::orderUnitMove(
        this->m_world, this->m_selectedEntity, targetTile, this->m_hexGrid);
    if (pathFound) {
        // Remember position before movement to detect actual movement
        const aoc::sim::UnitComponent& unitBefore =
            this->m_world.getComponent<aoc::sim::UnitComponent>(this->m_selectedEntity);
        aoc::hex::AxialCoord posBefore = unitBefore.position;

        // Execute movement immediately for this turn's remaining movement points
        aoc::sim::moveUnitAlongPath(this->m_world, this->m_selectedEntity, this->m_hexGrid);

        // Only update fog if the unit actually moved (not just path set with 0 MP)
        const aoc::sim::UnitComponent& unitAfter =
            this->m_world.getComponent<aoc::sim::UnitComponent>(this->m_selectedEntity);
        if (unitAfter.position != posBefore) {
            this->m_fogOfWar.updateVisibility(this->m_world, this->m_hexGrid, 0);
        }

        // Refresh the unit action panel to show updated movement points
        this->rebuildUnitActionPanel();
    } else {
        // Path not found, clear undo state
        this->m_undoState.hasState = false;
    }
}

void Application::handleUndoAction() {
    if (!this->m_inputManager.isActionPressed(InputAction::UndoAction)) {
        return;
    }
    if (!this->m_undoState.hasState) {
        return;
    }
    if (!this->m_world.isAlive(this->m_undoState.entity)) {
        this->m_undoState.hasState = false;
        return;
    }

    aoc::sim::UnitComponent* unit =
        this->m_world.tryGetComponent<aoc::sim::UnitComponent>(this->m_undoState.entity);
    if (unit == nullptr) {
        this->m_undoState.hasState = false;
        return;
    }

    unit->position = this->m_undoState.previousPosition;
    unit->movementRemaining = this->m_undoState.previousMovement;
    unit->pendingPath.clear();
    unit->state = aoc::sim::UnitState::Idle;

    LOG_INFO("Undo: unit moved back to (%d,%d) with %d MP",
             unit->position.q, unit->position.r, unit->movementRemaining);

    this->m_undoState.hasState = false;
}

void Application::handleEndTurn() {
    // If the game is over, skip all turn processing
    if (this->m_gameOver) {
        return;
    }

    // Turn blockers: warn player about unassigned actions (Civ 6 style)
    const aoc::game::Player* humanPre = this->m_gameState.humanPlayer();

    // Check 1: No active research
    if (humanPre != nullptr && !humanPre->tech().currentResearch.isValid()) {
        this->m_notificationManager.push(
            "No research selected! Open Tech Tree (T) to choose a technology.",
            4.0f, 1.0f, 0.8f, 0.2f);
    }

    // Check 2: Cities with empty production queues
    if (humanPre != nullptr) {
        for (const std::unique_ptr<aoc::game::City>& city : humanPre->cities()) {
            if (city->production().isEmpty()) {
                this->m_notificationManager.push(
                    city->name() + " has no production! Open city (click) to set production.",
                    4.0f, 1.0f, 0.8f, 0.2f);
            }
        }
    }

    // Execute any remaining unit movement for the human player
    aoc::sim::executeMovement(this->m_world, 0, this->m_hexGrid);

    // Simultaneous turns: human submits, AI auto-submits, then execute.
    // Sequential turns in war: if active, only allow the active player's turn
    // to advance; for single-player this is transparent (human acts, then AI).
    const bool sequentialActive = this->m_turnManager.shouldBeSequential(this->m_diplomacy);
    if (sequentialActive) {
        // In sequential mode, only the active player can end their turn.
        // For the human player (always 0), submit and advance to next player.
        this->m_turnManager.submitEndTurn(0);
        this->m_turnManager.advanceActivePlayer();

        // Run each AI player sequentially
        for (aoc::sim::ai::AIController& ai : this->m_aiControllers) {
            this->m_turnManager.submitEndTurn(ai.player());
            this->m_turnManager.advanceActivePlayer();
        }
    } else {
        // Simultaneous: all players submit at once
        this->m_turnManager.submitEndTurn(0);
        for (const aoc::sim::ai::AIController& ai : this->m_aiControllers) {
            this->m_turnManager.submitEndTurn(ai.player());
        }
    }

    if (this->m_turnManager.allPlayersReady()) {
        this->m_turnManager.executeTurn(this->m_world, this->m_scheduler);

        // Capture pre-turn tech/civic state for UI notifications
        const aoc::game::Player* humanGs = this->m_gameState.humanPlayer();
        TechId prevResearch = humanGs->tech().currentResearch;
        CivicId prevCivic = humanGs->civics().currentResearch;

        // Build TurnContext and execute all game logic via TurnProcessor
        aoc::sim::TurnContext turnCtx{};
        // world is accessed via gameState.legacyWorld()
        turnCtx.grid = &this->m_hexGrid;
        turnCtx.economy = &this->m_economy;
        turnCtx.diplomacy = &this->m_diplomacy;
        turnCtx.barbarians = &this->m_barbarianController;
        turnCtx.rng = &this->m_gameRng;
        turnCtx.gameState = &this->m_gameState;
        turnCtx.humanPlayer = 0;
        turnCtx.currentTurn = this->m_turnManager.currentTurn();
        turnCtx.allPlayers.push_back(0);
        for (aoc::sim::ai::AIController& ai : this->m_aiControllers) {
            turnCtx.aiControllers.push_back(&ai);
            turnCtx.allPlayers.push_back(ai.player());
        }

        aoc::sim::processTurn(turnCtx);

        // AI movement execution (after AI decisions ran inside processTurn)
        for (const aoc::sim::ai::AIController& ai : this->m_aiControllers) {
            aoc::sim::executeMovement(this->m_world, ai.player(), this->m_hexGrid);
        }

        // Diplomacy modifier decay
        this->m_diplomacy.tickModifiers();

        // Process spy missions
        aoc::sim::processSpyMissions(this->m_world, this->m_gameRng);

        // Grievance tick
        {
            aoc::ecs::ComponentPool<aoc::sim::PlayerGrievanceComponent>* gPool =
                this->m_world.getPool<aoc::sim::PlayerGrievanceComponent>();
            if (gPool != nullptr) {
                for (uint32_t gi = 0; gi < gPool->size(); ++gi) {
                    gPool->data()[gi].tickGrievances();
                }
            }
        }

        // World Congress
        aoc::sim::processWorldCongress(this->m_world,
                                        this->m_turnManager.currentTurn(),
                                        this->m_gameRng);

        // Clear event log for new turn
        this->m_eventLog.clear();

        // -- Post-turn UI responses (notifications, sounds, screens) --

        // Tech completion notification: detect by comparing pre/post research state
        const aoc::game::Player* humanPost = this->m_gameState.humanPlayer();
        bool techCompleted = prevResearch.isValid() && !humanPost->tech().currentResearch.isValid();
        if (techCompleted) {
            std::string techName = "Unknown";
            const aoc::sim::PlayerTechComponent& tech = humanPost->tech();
            const uint16_t count = aoc::sim::techCount();
            for (uint16_t t = count; t > 0; --t) {
                if (tech.hasResearched(TechId{static_cast<uint16_t>(t - 1)})) {
                    techName = std::string(aoc::sim::techDef(TechId{static_cast<uint16_t>(t - 1)}).name);
                    break;
                }
            }
            LOG_INFO("Research completed: %s", techName.c_str());
            this->m_eventLog.addEvent("Researched " + techName);
            this->m_notificationManager.push("Research complete: " + techName, 4.0f,
                                              0.3f, 0.7f, 1.0f);
            this->m_soundQueue.push(aoc::audio::SoundEffect::TechResearched);

            {
                aoc::game::Player* eraPlayer = this->m_gameState.humanPlayer();
                if (eraPlayer != nullptr) {
                    aoc::sim::addEraScore(*eraPlayer, 2, "Researched " + techName);
                }
            }

            if (!humanPost->tech().currentResearch.isValid()) {
                this->m_techScreen.setContext(&this->m_world, 0);
                this->m_techScreen.open(this->m_uiManager);
            }
        }

        // Civic completion notification
        bool civicCompleted = prevCivic.isValid() && !humanPost->civics().currentResearch.isValid();
        if (civicCompleted) {
            std::string civicName = "Unknown";
            const aoc::sim::PlayerCivicComponent& civic = humanPost->civics();
            const uint16_t count = aoc::sim::civicCount();
            for (uint16_t c = count; c > 0; --c) {
                if (civic.hasCompleted(CivicId{static_cast<uint16_t>(c - 1)})) {
                    civicName = std::string(aoc::sim::civicDef(CivicId{static_cast<uint16_t>(c - 1)}).name);
                    break;
                }
            }
            LOG_INFO("Civic completed: %s", civicName.c_str());
            this->m_eventLog.addEvent("Completed " + civicName);
            this->m_notificationManager.push("Civic complete: " + civicName, 4.0f,
                                              0.8f, 0.5f, 1.0f);
            this->m_soundQueue.push(aoc::audio::SoundEffect::CivicCompleted);
        }

        // Record replay frame
        this->m_replayRecorder.recordFrame(this->m_world,
                                            this->m_turnManager.currentTurn());

        // Sound events for turn transition
        this->m_soundQueue.clear();
        this->m_soundQueue.push(aoc::audio::SoundEffect::TurnEnd);
        this->m_soundQueue.push(aoc::audio::SoundEffect::TurnStart);

        // Switch music track based on era
        {
            aoc::ecs::ComponentPool<aoc::sim::PlayerEraComponent>* eraPool =
                this->m_world.getPool<aoc::sim::PlayerEraComponent>();
            if (eraPool != nullptr) {
                for (uint32_t ei = 0; ei < eraPool->size(); ++ei) {
                    if (eraPool->data()[ei].owner == 0) {
                        const uint16_t eraVal = eraPool->data()[ei].currentEra.value;
                        if (eraVal < static_cast<uint16_t>(aoc::audio::MusicTrack::Count) - 2) {
                            const aoc::audio::MusicTrack track =
                                static_cast<aoc::audio::MusicTrack>(eraVal + 1);
                            if (track != this->m_musicManager.track()) {
                                this->m_musicManager.setTrack(track);
                                LOG_INFO("Music track changed to era %u", eraVal);
                            }
                        }
                        break;
                    }
                }
            }
        }

        // Update fog of war for all players
        this->m_fogOfWar.updateVisibility(this->m_world, this->m_hexGrid, 0);
        for (const aoc::sim::ai::AIController& ai : this->m_aiControllers) {
            this->m_fogOfWar.updateVisibility(this->m_world, this->m_hexGrid, ai.player());
        }

        // Check victory conditions
        aoc::sim::VictoryResult vr = aoc::sim::checkVictoryConditions(
            this->m_world, this->m_turnManager.currentTurn());
        if (vr.type != aoc::sim::VictoryType::None) {
            this->m_gameOver = true;
            this->m_victoryResult = vr;
            LOG_INFO("Game over! Player %u wins by %s",
                     static_cast<unsigned>(vr.winner),
                     vr.type == aoc::sim::VictoryType::Science    ? "Science" :
                     vr.type == aoc::sim::VictoryType::Domination ? "Domination" :
                     vr.type == aoc::sim::VictoryType::Culture    ? "Culture" :
                     vr.type == aoc::sim::VictoryType::Score      ? "Score" :
                     vr.type == aoc::sim::VictoryType::Religion   ? "Religion" : "Unknown");

            const uint8_t totalPlayers = static_cast<uint8_t>(1 + this->m_aiControllers.size());
            this->m_scoreScreen.setContext(
                &this->m_world, &this->m_hexGrid, vr, totalPlayers,
                [this]() { this->returnToMainMenu(); });
            this->m_scoreScreen.open(this->m_uiManager);
        }

        // Government/policy unlocks from completed civics
        this->m_world.forEach<aoc::sim::PlayerCivicComponent, aoc::sim::PlayerGovernmentComponent>(
            [](EntityId, aoc::sim::PlayerCivicComponent& civic,
               aoc::sim::PlayerGovernmentComponent& gov) {
                const std::vector<aoc::sim::CivicDef>& civics = aoc::sim::allCivics();
                for (const aoc::sim::CivicDef& cdef : civics) {
                    if (!civic.hasCompleted(cdef.id)) {
                        continue;
                    }
                    for (uint8_t govId : cdef.unlockedGovernmentIds) {
                        if (govId < static_cast<uint8_t>(aoc::sim::GovernmentType::Count)) {
                            gov.unlockGovernment(static_cast<aoc::sim::GovernmentType>(govId));
                        }
                    }
                    for (uint8_t polId : cdef.unlockedPolicyIds) {
                        if (polId < aoc::sim::POLICY_CARD_COUNT) {
                            gov.unlockPolicy(polId);
                        }
                    }
                }
            });

        this->m_turnManager.beginNewTurn();

        // Clear undo state at the start of a new turn
        this->m_undoState.hasState = false;

        // Wake sleeping units if enemies are within 2 hexes
        {
            aoc::ecs::ComponentPool<aoc::sim::UnitComponent>* unitPool =
                this->m_world.getPool<aoc::sim::UnitComponent>();
            if (unitPool != nullptr) {
                for (uint32_t i = 0; i < unitPool->size(); ++i) {
                    aoc::sim::UnitComponent& sleeper = unitPool->data()[i];
                    if (sleeper.owner != 0) {
                        continue;
                    }
                    if (sleeper.state != aoc::sim::UnitState::Sleeping) {
                        continue;
                    }
                    // Check for enemy units within 2 hexes
                    bool enemyNearby = false;
                    for (uint32_t j = 0; j < unitPool->size(); ++j) {
                        const aoc::sim::UnitComponent& other = unitPool->data()[j];
                        if (other.owner == sleeper.owner || other.owner == BARBARIAN_PLAYER) {
                            continue;
                        }
                        if (hex::distance(sleeper.position, other.position) <= 2) {
                            enemyNearby = true;
                            break;
                        }
                    }
                    if (enemyNearby) {
                        sleeper.state = aoc::sim::UnitState::Idle;
                        sleeper.autoExplore = false;
                        LOG_INFO("Sleeping unit at (%d,%d) woke up -- enemy nearby",
                                 sleeper.position.q, sleeper.position.r);
                    }
                }
            }
        }

        // Auto-explore: move scout units toward unexplored territory
        {
            aoc::ecs::ComponentPool<aoc::sim::UnitComponent>* unitPool =
                this->m_world.getPool<aoc::sim::UnitComponent>();
            if (unitPool != nullptr) {
                // Collect entities first to avoid invalidation during movement
                std::vector<EntityId> autoExploreUnits;
                for (uint32_t i = 0; i < unitPool->size(); ++i) {
                    const aoc::sim::UnitComponent& unit = unitPool->data()[i];
                    if (unit.owner == 0 && unit.autoExplore) {
                        autoExploreUnits.push_back(unitPool->entities()[i]);
                    }
                }
                for (EntityId unitEntity : autoExploreUnits) {
                    if (!this->m_world.isAlive(unitEntity)) {
                        continue;
                    }
                    aoc::sim::UnitComponent& unit =
                        this->m_world.getComponent<aoc::sim::UnitComponent>(unitEntity);
                    // Find nearest unseen tile
                    hex::AxialCoord bestTarget = unit.position;
                    int32_t bestDist = INT32_MAX;
                    const int32_t tileCount = this->m_hexGrid.tileCount();
                    for (int32_t t = 0; t < tileCount; ++t) {
                        aoc::map::TileVisibility vis =
                            this->m_fogOfWar.visibility(0, t);
                        if (vis != aoc::map::TileVisibility::Unseen) {
                            continue;
                        }
                        hex::AxialCoord tileCoord = this->m_hexGrid.toAxial(t);
                        int32_t dist = hex::distance(unit.position, tileCoord);
                        if (dist < bestDist) {
                            bestDist = dist;
                            bestTarget = tileCoord;
                        }
                    }
                    if (bestDist < INT32_MAX && !(bestTarget == unit.position)) {
                        aoc::sim::orderUnitMove(this->m_world, unitEntity,
                                                bestTarget, this->m_hexGrid);
                    }
                }
            }
        }

        // Auto-improve: civilian units with autoImprove build improvements or move to unimproved tiles
        {
            aoc::ecs::ComponentPool<aoc::sim::UnitComponent>* unitPool =
                this->m_world.getPool<aoc::sim::UnitComponent>();
            if (unitPool != nullptr) {
                std::vector<EntityId> autoImproveUnits;
                for (uint32_t i = 0; i < unitPool->size(); ++i) {
                    const aoc::sim::UnitComponent& unit = unitPool->data()[i];
                    if (unit.owner == 0 && unit.autoImprove) {
                        autoImproveUnits.push_back(unitPool->entities()[i]);
                    }
                }
                for (EntityId unitEntity : autoImproveUnits) {
                    if (!this->m_world.isAlive(unitEntity)) {
                        continue;
                    }
                    aoc::sim::UnitComponent& unit =
                        this->m_world.getComponent<aoc::sim::UnitComponent>(unitEntity);
                    if (unit.chargesRemaining == 0) {
                        continue;
                    }

                    // Try to improve current tile
                    const int32_t currentIdx = this->m_hexGrid.toIndex(unit.position);
                    const aoc::map::ImprovementType bestImpr =
                        aoc::sim::bestImprovementForTile(this->m_hexGrid, currentIdx);

                    if (bestImpr != aoc::map::ImprovementType::None &&
                        this->m_hexGrid.improvement(currentIdx) == aoc::map::ImprovementType::None) {
                        this->m_hexGrid.setImprovement(currentIdx, bestImpr);
                        if (unit.chargesRemaining > 0) {
                            --unit.chargesRemaining;
                        }
                        LOG_INFO("Auto-improve: built improvement at (%d,%d)",
                                 unit.position.q, unit.position.r);
                        if (unit.chargesRemaining == 0) {
                            this->m_world.destroyEntity(unitEntity);
                            LOG_INFO("Auto-improve: builder exhausted all charges");
                        }
                        continue;
                    }

                    // Find nearest unimproved owned tile and move there
                    hex::AxialCoord bestTarget = unit.position;
                    int32_t bestDist = INT32_MAX;
                    const int32_t tileCount = this->m_hexGrid.tileCount();
                    for (int32_t t = 0; t < tileCount; ++t) {
                        if (this->m_hexGrid.owner(t) != unit.owner) {
                            continue;
                        }
                        if (this->m_hexGrid.improvement(t) != aoc::map::ImprovementType::None) {
                            continue;
                        }
                        if (this->m_hexGrid.movementCost(t) == 0) {
                            continue;
                        }
                        const aoc::map::ImprovementType candidate =
                            aoc::sim::bestImprovementForTile(this->m_hexGrid, t);
                        if (candidate == aoc::map::ImprovementType::None) {
                            continue;
                        }
                        hex::AxialCoord tileCoord = this->m_hexGrid.toAxial(t);
                        int32_t dist = hex::distance(unit.position, tileCoord);
                        if (dist < bestDist) {
                            bestDist = dist;
                            bestTarget = tileCoord;
                        }
                    }
                    if (bestDist < INT32_MAX && !(bestTarget == unit.position)) {
                        aoc::sim::orderUnitMove(this->m_world, unitEntity,
                                                bestTarget, this->m_hexGrid);
                    }
                }
            }
        }

        // Refresh movement for next turn
        aoc::sim::refreshMovement(this->m_world, 0);

        // Multi-turn movement continuation: resume pending paths after refresh
        {
            aoc::ecs::ComponentPool<aoc::sim::UnitComponent>* unitPool =
                this->m_world.getPool<aoc::sim::UnitComponent>();
            if (unitPool != nullptr) {
                std::vector<EntityId> pendingUnits;
                for (uint32_t i = 0; i < unitPool->size(); ++i) {
                    const aoc::sim::UnitComponent& unit = unitPool->data()[i];
                    if (unit.owner == 0 && !unit.pendingPath.empty()) {
                        pendingUnits.push_back(unitPool->entities()[i]);
                    }
                }
                for (EntityId unitEntity : pendingUnits) {
                    if (this->m_world.isAlive(unitEntity)) {
                        aoc::sim::moveUnitAlongPath(this->m_world, unitEntity, this->m_hexGrid);
                    }
                }
            }
        }
    }
}

// ============================================================================
// Game setup
// ============================================================================

void Application::spawnStartingEntities(aoc::sim::CivId civId) {
    // Place human player (player 0) using same circular layout as AI players.
    // Player 0 gets angle 0 (east side of map center).
    const int32_t mapW = this->m_hexGrid.width();
    const int32_t mapH = this->m_hexGrid.height();
    const float radiusX = static_cast<float>(mapW) * 0.35f;
    const float radiusY = static_cast<float>(mapH) * 0.35f;
    // Small random offset for human player too (deterministic from map seed)
    const uint32_t humanHash = 42u * 2654435761u;
    const float humanOffX = (static_cast<float>(humanHash % 1000u) / 1000.0f - 0.5f)
                          * static_cast<float>(mapW) * 0.10f;
    const float humanOffY = (static_cast<float>((humanHash >> 10) % 1000u) / 1000.0f - 0.5f)
                          * static_cast<float>(mapH) * 0.10f;
    const int32_t spawnX = mapW / 2 + static_cast<int32_t>(radiusX + humanOffX);
    const int32_t spawnY = mapH / 2 + static_cast<int32_t>(humanOffY);
    aoc::hex::AxialCoord mapCenter = aoc::hex::offsetToAxial(
        {std::clamp(spawnX, 2, mapW - 3), std::clamp(spawnY, 2, mapH - 3)});

    hex::AxialCoord capitalPos = this->findNearbyLandTile(mapCenter);

    // Create player economy entity with monetary state
    EntityId playerEntity = this->m_world.createEntity();
    aoc::sim::MonetaryStateComponent monetary{};
    monetary.owner = 0;
    monetary.system = aoc::sim::MonetarySystemType::Barter;
    monetary.treasury = 100;
    monetary.moneySupply = 0;
    monetary.taxRate = 0.15f;
    monetary.governmentSpending = 0;
    this->m_world.addComponent<aoc::sim::MonetaryStateComponent>(playerEntity, std::move(monetary));

    aoc::sim::PlayerEconomyComponent playerEcon{};
    playerEcon.owner = 0;
    playerEcon.treasury = 100;
    this->m_world.addComponent<aoc::sim::PlayerEconomyComponent>(playerEntity, std::move(playerEcon));

    aoc::sim::PlayerTechComponent techComp{};
    techComp.owner = 0;
    techComp.initialize();
    techComp.currentResearch = TechId{0};  // Start researching Mining
    this->m_world.addComponent<aoc::sim::PlayerTechComponent>(playerEntity, std::move(techComp));

    aoc::sim::PlayerCivicComponent civicComp{};
    civicComp.owner = 0;
    civicComp.initialize();
    civicComp.currentResearch = CivicId{0};  // Start researching Code of Laws
    this->m_world.addComponent<aoc::sim::PlayerCivicComponent>(playerEntity, std::move(civicComp));

    aoc::sim::PlayerEraComponent eraComp{};
    eraComp.owner = 0;
    this->m_world.addComponent<aoc::sim::PlayerEraComponent>(playerEntity, std::move(eraComp));

    aoc::sim::VictoryTrackerComponent victoryComp{};
    victoryComp.owner = 0;
    this->m_world.addComponent<aoc::sim::VictoryTrackerComponent>(playerEntity, std::move(victoryComp));

    aoc::sim::PlayerGovernmentComponent govComp{};
    govComp.owner = 0;
    govComp.government = aoc::sim::GovernmentType::Chiefdom;
    this->m_world.addComponent<aoc::sim::PlayerGovernmentComponent>(playerEntity, std::move(govComp));

    aoc::sim::PlayerCivilizationComponent civComp2{};
    civComp2.owner = 0;
    civComp2.civId = civId;
    this->m_world.addComponent<aoc::sim::PlayerCivilizationComponent>(playerEntity, std::move(civComp2));

    aoc::sim::PlayerGreatPeopleComponent gpComp{};
    gpComp.owner = 0;
    this->m_world.addComponent<aoc::sim::PlayerGreatPeopleComponent>(playerEntity, std::move(gpComp));

    aoc::sim::PlayerEurekaComponent eurekaComp{};
    eurekaComp.owner = 0;
    this->m_world.addComponent<aoc::sim::PlayerEurekaComponent>(playerEntity, std::move(eurekaComp));

    aoc::sim::PlayerTariffComponent tariffComp{};
    tariffComp.owner = 0;
    this->m_world.addComponent<aoc::sim::PlayerTariffComponent>(playerEntity, std::move(tariffComp));

    aoc::sim::PlayerBankingComponent bankComp{};
    bankComp.owner = 0;
    this->m_world.addComponent<aoc::sim::PlayerBankingComponent>(playerEntity, std::move(bankComp));

    // Create the global wonder tracker entity (one per game)
    EntityId wonderTrackerEntity = this->m_world.createEntity();
    this->m_world.addComponent<aoc::sim::GlobalWonderTracker>(
        wonderTrackerEntity, aoc::sim::GlobalWonderTracker{});

    // Spawn settler (player founds their own capital)
    EntityId settler = this->m_world.createEntity();
    this->m_world.addComponent<aoc::sim::UnitComponent>(
        settler,
        aoc::sim::UnitComponent::create(0, UnitTypeId{3}, capitalPos));

    // Spawn warrior
    hex::AxialCoord warriorPos = this->findNearbyLandTile({capitalPos.q + 1, capitalPos.r});
    EntityId warrior = this->m_world.createEntity();
    this->m_world.addComponent<aoc::sim::UnitComponent>(
        warrior,
        aoc::sim::UnitComponent::create(0, UnitTypeId{0}, warriorPos));

    // Mirror starting units into the GameState object model
    aoc::game::Player* humanPlayer = this->m_gameState.humanPlayer();
    if (humanPlayer != nullptr) {
        humanPlayer->addUnit(UnitTypeId{3}, capitalPos);
        humanPlayer->addUnit(UnitTypeId{0}, warriorPos);
    }

    // Center camera on the settler
    float cx = 0.0f, cy = 0.0f;
    hex::axialToPixel(capitalPos, this->m_gameRenderer.mapRenderer().hexSize(), cx, cy);
    this->m_cameraController.setPosition(cx, cy);

    LOG_INFO("Spawned starting units at (%d,%d)", capitalPos.q, capitalPos.r);
}

hex::AxialCoord Application::findNearbyLandTile(hex::AxialCoord target) const {
    // Spiral outward from target to find a good starting tile.
    // Prefer grassland/plains over desert/tundra/snow.
    aoc::hex::AxialCoord bestTile = target;
    float bestScore = -999.0f;

    for (int32_t radius = 0; radius < 15; ++radius) {
        std::vector<aoc::hex::AxialCoord> ringTiles;
        aoc::hex::ring(target, radius, std::back_inserter(ringTiles));
        for (const aoc::hex::AxialCoord& tile : ringTiles) {
            if (!this->m_hexGrid.isValid(tile)) { continue; }
            int32_t index = this->m_hexGrid.toIndex(tile);
            if (this->m_hexGrid.movementCost(index) <= 0) { continue; }

            aoc::map::TerrainType terrain = this->m_hexGrid.terrain(index);
            float score = 0.0f;

            // Score terrain types (prefer fertile land)
            switch (terrain) {
                case aoc::map::TerrainType::Grassland: score = 10.0f; break;
                case aoc::map::TerrainType::Plains:    score = 8.0f;  break;
                case aoc::map::TerrainType::Tundra:    score = 2.0f;  break;
                case aoc::map::TerrainType::Desert:    score = 1.0f;  break;
                case aoc::map::TerrainType::Snow:      score = 0.5f;  break;
                default:                                score = 3.0f;  break;
            }

            // Bonus for river adjacency
            if (this->m_hexGrid.riverEdges(index) != 0) { score += 3.0f; }

            // Bonus for nearby resources
            if (this->m_hexGrid.resource(index).isValid()) { score += 2.0f; }

            // Penalty for distance from target (prefer closer)
            score -= static_cast<float>(radius) * 0.3f;

            if (score > bestScore) {
                bestScore = score;
                bestTile = tile;
            }
        }

        // Stop early if we found a great tile (grassland with river)
        if (bestScore >= 12.0f) { break; }
    }
    return bestTile;
}

// ============================================================================
// AI player spawning
// ============================================================================

void Application::spawnAIPlayer(PlayerId player, aoc::sim::CivId civId) {
    // Distribute AI players evenly across the map using a grid pattern.
    // Player 0 is human (spawned separately). AI players 1..N get spread positions.
    // Use a circular layout: each player gets an angle, placed at 35% map radius from center.
    const int32_t mapW = this->m_hexGrid.width();
    const int32_t mapH = this->m_hexGrid.height();
    const int32_t totalPlayers = static_cast<int32_t>(this->m_aiControllers.size()) + 1;
    const float angle = 2.0f * 3.14159f * static_cast<float>(player) / static_cast<float>(totalPlayers);
    const float radiusX = static_cast<float>(mapW) * 0.35f;
    const float radiusY = static_cast<float>(mapH) * 0.35f;

    // Add randomization: +/- 15% of map size so players aren't on a perfect circle
    // Use deterministic hash from player ID for reproducibility
    const uint32_t rngHash = static_cast<uint32_t>(player) * 2654435761u;
    const float offsetX = (static_cast<float>(rngHash % 1000u) / 1000.0f - 0.5f)
                        * static_cast<float>(mapW) * 0.15f;
    const float offsetY = (static_cast<float>((rngHash >> 10) % 1000u) / 1000.0f - 0.5f)
                        * static_cast<float>(mapH) * 0.15f;

    const int32_t spawnX = mapW / 2 + static_cast<int32_t>(radiusX * std::cos(angle) + offsetX);
    const int32_t spawnY = mapH / 2 + static_cast<int32_t>(radiusY * std::sin(angle) + offsetY);
    aoc::hex::AxialCoord aiSpawn = aoc::hex::offsetToAxial(
        {std::clamp(spawnX, 2, mapW - 3), std::clamp(spawnY, 2, mapH - 3)});

    hex::AxialCoord settlerPos = this->findNearbyLandTile(aiSpawn);
    hex::AxialCoord warriorPos = this->findNearbyLandTile(
        {settlerPos.q + 1, settlerPos.r});

    // Player entity with economy + tech
    EntityId playerEntity = this->m_world.createEntity();

    aoc::sim::MonetaryStateComponent monetary{};
    monetary.owner = player;
    monetary.system = aoc::sim::MonetarySystemType::Barter;
    monetary.treasury = 100;
    this->m_world.addComponent<aoc::sim::MonetaryStateComponent>(playerEntity, std::move(monetary));

    aoc::sim::PlayerEconomyComponent playerEcon{};
    playerEcon.owner = player;
    playerEcon.treasury = 100;
    this->m_world.addComponent<aoc::sim::PlayerEconomyComponent>(playerEntity, std::move(playerEcon));

    aoc::sim::PlayerTechComponent techComp{};
    techComp.owner = player;
    techComp.initialize();
    this->m_world.addComponent<aoc::sim::PlayerTechComponent>(playerEntity, std::move(techComp));

    aoc::sim::PlayerCivicComponent civicComp{};
    civicComp.owner = player;
    civicComp.initialize();
    this->m_world.addComponent<aoc::sim::PlayerCivicComponent>(playerEntity, std::move(civicComp));

    aoc::sim::PlayerEraComponent eraComp{};
    eraComp.owner = player;
    this->m_world.addComponent<aoc::sim::PlayerEraComponent>(playerEntity, std::move(eraComp));

    aoc::sim::VictoryTrackerComponent victoryComp{};
    victoryComp.owner = player;
    this->m_world.addComponent<aoc::sim::VictoryTrackerComponent>(playerEntity, std::move(victoryComp));

    aoc::sim::PlayerGovernmentComponent govComp{};
    govComp.owner = player;
    govComp.government = aoc::sim::GovernmentType::Chiefdom;
    this->m_world.addComponent<aoc::sim::PlayerGovernmentComponent>(playerEntity, std::move(govComp));

    aoc::sim::PlayerCivilizationComponent civComp2{};
    civComp2.owner = player;
    civComp2.civId = civId;
    this->m_world.addComponent<aoc::sim::PlayerCivilizationComponent>(playerEntity, std::move(civComp2));

    aoc::sim::PlayerGreatPeopleComponent gpComp{};
    gpComp.owner = player;
    this->m_world.addComponent<aoc::sim::PlayerGreatPeopleComponent>(playerEntity, std::move(gpComp));

    aoc::sim::PlayerEurekaComponent eurekaComp{};
    eurekaComp.owner = player;
    this->m_world.addComponent<aoc::sim::PlayerEurekaComponent>(playerEntity, std::move(eurekaComp));

    aoc::sim::PlayerTariffComponent tariffComp{};
    tariffComp.owner = player;
    this->m_world.addComponent<aoc::sim::PlayerTariffComponent>(playerEntity, std::move(tariffComp));

    aoc::sim::PlayerBankingComponent bankComp{};
    bankComp.owner = player;
    this->m_world.addComponent<aoc::sim::PlayerBankingComponent>(playerEntity, std::move(bankComp));

    // Spawn settler (AI will auto-found city on first turn)
    EntityId settler = this->m_world.createEntity();
    this->m_world.addComponent<aoc::sim::UnitComponent>(
        settler,
        aoc::sim::UnitComponent::create(player, UnitTypeId{3}, settlerPos));

    // Spawn warrior
    EntityId warrior = this->m_world.createEntity();
    this->m_world.addComponent<aoc::sim::UnitComponent>(
        warrior,
        aoc::sim::UnitComponent::create(player, UnitTypeId{0}, warriorPos));

    // Mirror AI starting units into the GameState object model
    aoc::game::Player* aiPlayer = this->m_gameState.player(player);
    if (aiPlayer != nullptr) {
        aiPlayer->addUnit(UnitTypeId{3}, settlerPos);
        aiPlayer->addUnit(UnitTypeId{0}, warriorPos);
    }

    LOG_INFO("AI Player %u spawned at (%d,%d)",
             static_cast<unsigned>(player), settlerPos.q, settlerPos.r);
}

// ============================================================================
// Map resource placement
// ============================================================================

void Application::placeMapResources() {
    aoc::Random rng(54321);  // Deterministic seed for resource placement

    const int32_t width  = this->m_hexGrid.width();
    const int32_t height = this->m_hexGrid.height();

    // Resource placement rules: strategic resources on specific terrain types
    struct ResourcePlacement {
        uint16_t goodId;
        float    probability;
        bool     requiresHills;
        bool     allowDesert;
        bool     allowPlains;
        bool     allowGrassland;
        bool     allowTundra;
    };

    constexpr std::array<ResourcePlacement, 18> PLACEMENTS = {{
        {aoc::sim::goods::IRON_ORE,   0.04f, true,  false, true,  true,  true},
        {aoc::sim::goods::COPPER_ORE, 0.03f, true,  false, true,  true,  false},
        {aoc::sim::goods::COAL,       0.03f, false, false, true,  true,  true},
        {aoc::sim::goods::OIL,        0.02f, false, true,  true,  false, true},
        {aoc::sim::goods::HORSES,     0.03f, false, false, true,  true,  false},
        {aoc::sim::goods::WOOD,       0.06f, false, false, false, true,  true},
        {aoc::sim::goods::STONE,      0.04f, true,  true,  true,  true,  true},
        {aoc::sim::goods::WHEAT,      0.05f, false, false, true,  true,  false},
        // New resources
        {aoc::sim::goods::COTTON,     0.03f, false, false, true,  true,  false},
        {aoc::sim::goods::RUBBER,     0.02f, false, false, false, true,  false},
        {aoc::sim::goods::TIN,        0.02f, true,  false, true,  true,  true},
        {aoc::sim::goods::DYES,       0.02f, false, false, false, true,  false},
        {aoc::sim::goods::FURS,       0.02f, false, false, false, false, true},
        {aoc::sim::goods::RICE,       0.04f, false, false, false, true,  false},
        {aoc::sim::goods::SUGAR,      0.02f, false, false, false, true,  false},
        {aoc::sim::goods::SILK,       0.01f, false, false, false, true,  false},
        {aoc::sim::goods::NITER,      0.02f, true,  true,  true,  false, false},
        {aoc::sim::goods::ALUMINUM,   0.01f, true,  false, true,  false, true},
    }};

    int32_t totalPlaced = 0;

    for (int32_t row = 0; row < height; ++row) {
        for (int32_t col = 0; col < width; ++col) {
            int32_t index = row * width + col;
            aoc::map::TerrainType terrain = this->m_hexGrid.terrain(index);
            aoc::map::FeatureType feature = this->m_hexGrid.feature(index);

            if (aoc::map::isWater(terrain) || terrain == aoc::map::TerrainType::Mountain) {
                continue;
            }

            bool isHills = (feature == aoc::map::FeatureType::Hills);
            bool isDesert = (terrain == aoc::map::TerrainType::Desert);
            bool isPlains = (terrain == aoc::map::TerrainType::Plains);
            bool isGrassland = (terrain == aoc::map::TerrainType::Grassland);
            bool isTundra = (terrain == aoc::map::TerrainType::Tundra);

            for (const ResourcePlacement& placement : PLACEMENTS) {
                if (placement.requiresHills && !isHills) {
                    continue;
                }
                if (isDesert && !placement.allowDesert) {
                    continue;
                }
                if (isPlains && !placement.allowPlains) {
                    continue;
                }
                if (isGrassland && !placement.allowGrassland) {
                    continue;
                }
                if (isTundra && !placement.allowTundra) {
                    continue;
                }

                if (rng.chance(placement.probability)) {
                    this->m_hexGrid.setResource(index, ResourceId{placement.goodId});

                    // Create a tile resource ECS entity so the economy can harvest it
                    EntityId resEntity = this->m_world.createEntity();
                    this->m_world.addComponent<aoc::sim::TileResourceComponent>(
                        resEntity,
                        aoc::sim::TileResourceComponent{placement.goodId, 1, 1});

                    ++totalPlaced;
                    break;  // Only one resource per tile
                }
            }
        }
    }

    LOG_INFO("Placed %d resources on map", totalPlaced);
}

// ============================================================================
// HUD
// ============================================================================

void Application::buildHUD() {
    const std::pair<uint32_t, uint32_t> hudFbSize = this->m_window.framebufferSize();
    float screenW = static_cast<float>(hudFbSize.first);

    // ================================================================
    // Top bar: full width. Resources on left, buttons on right.
    // ================================================================
    this->m_topBar = this->m_uiManager.createPanel(
        {0.0f, 0.0f, screenW, 32.0f},
        aoc::ui::PanelData{{0.06f, 0.06f, 0.10f, 0.90f}, 0.0f});
    {
        aoc::ui::Widget* bar = this->m_uiManager.getWidget(this->m_topBar);
        bar->layoutDirection = aoc::ui::LayoutDirection::Horizontal;
        bar->padding = {4.0f, 6.0f, 4.0f, 6.0f};
        bar->childSpacing = 6.0f;
        bar->anchor = aoc::ui::Anchor::TopLeft;
    }

    // Helper for top bar buttons
    // auto required: lambda type is unnameable
    auto makeTopBtn = [this](aoc::ui::WidgetId parent, const std::string& label,
                              float width, std::function<void()> onClick) {
        aoc::ui::ButtonData btn;
        btn.label = label;
        btn.fontSize = 11.0f;
        btn.normalColor  = {0.18f, 0.18f, 0.22f, 0.9f};
        btn.hoverColor   = {0.28f, 0.28f, 0.35f, 0.9f};
        btn.pressedColor = {0.12f, 0.12f, 0.16f, 0.9f};
        btn.labelColor   = {0.9f, 0.9f, 0.9f, 1.0f};
        btn.cornerRadius = 3.0f;
        btn.onClick = std::move(onClick);
        return this->m_uiManager.createButton(
            parent, {0.0f, 0.0f, width, 22.0f}, std::move(btn));
    };

    // LEFT SIDE: Resource display
    this->m_resourceLabel = this->m_uiManager.createLabel(
        this->m_topBar, {0.0f, 0.0f, 700.0f, 22.0f},
        aoc::ui::LabelData{"Resources: ...", {0.75f, 0.80f, 0.65f, 1.0f}, 10.0f});

    // Spacer to push buttons to the right
    [[maybe_unused]] aoc::ui::WidgetId spacer = this->m_uiManager.createPanel(
        this->m_topBar, {0.0f, 0.0f, 1.0f, 22.0f},
        aoc::ui::PanelData{{0.0f, 0.0f, 0.0f, 0.0f}, 0.0f});

    // RIGHT SIDE: Game screen buttons
    makeTopBtn(this->m_topBar, "Tech", 50.0f, [this]() {
        if (!this->m_techScreen.isOpen()) {
            this->m_techScreen.setContext(&this->m_world, 0);
            this->m_techScreen.open(this->m_uiManager);
        } else {
            this->m_techScreen.close(this->m_uiManager);
        }
    });

    makeTopBtn(this->m_topBar, "Gov", 44.0f, [this]() {
        if (!this->m_governmentScreen.isOpen()) {
            this->m_governmentScreen.setContext(&this->m_world, 0);
            this->m_governmentScreen.open(this->m_uiManager);
        } else {
            this->m_governmentScreen.close(this->m_uiManager);
        }
    });

    makeTopBtn(this->m_topBar, "Econ", 50.0f, [this]() {
        if (!this->m_economyScreen.isOpen()) {
            this->m_economyScreen.setContext(&this->m_world, &this->m_hexGrid, 0, &this->m_economy.market());
            this->m_economyScreen.open(this->m_uiManager);
        } else {
            this->m_economyScreen.close(this->m_uiManager);
        }
    });

    makeTopBtn(this->m_topBar, "Trade", 50.0f, [this]() {
        if (!this->m_tradeScreen.isOpen()) {
            this->m_tradeScreen.setContext(&this->m_world, 0,
                                            &this->m_economy.market(),
                                            &this->m_diplomacy);
            this->m_tradeScreen.open(this->m_uiManager);
        } else {
            this->m_tradeScreen.close(this->m_uiManager);
        }
    });

    makeTopBtn(this->m_topBar, "Diplo", 50.0f, [this]() {
        if (!this->m_diplomacyScreen.isOpen()) {
            this->m_diplomacyScreen.setContext(&this->m_world, 0, &this->m_diplomacy);
            this->m_diplomacyScreen.open(this->m_uiManager);
        } else {
            this->m_diplomacyScreen.close(this->m_uiManager);
        }
    });

    // Separator
    [[maybe_unused]] aoc::ui::WidgetId sep = this->m_uiManager.createPanel(
        this->m_topBar, {0.0f, 0.0f, 2.0f, 22.0f},
        aoc::ui::PanelData{{0.3f, 0.3f, 0.35f, 0.5f}, 0.0f});

    // MENU button -- toggles a dropdown with Save/Load/Settings
    makeTopBtn(this->m_topBar, "Menu", 55.0f, [this]() {
        if (this->m_menuDropdown != aoc::ui::INVALID_WIDGET) {
            // Close dropdown
            this->m_uiManager.removeWidget(this->m_menuDropdown);
            this->m_menuDropdown = aoc::ui::INVALID_WIDGET;
        } else {
            // Open dropdown at top-right
            const std::pair<uint32_t, uint32_t> dropFbSize = this->m_window.framebufferSize();
            float dropX = static_cast<float>(dropFbSize.first) - 120.0f;
            float dropY = 34.0f;

            this->m_menuDropdown = this->m_uiManager.createPanel(
                {dropX, dropY, 110.0f, 150.0f},
                aoc::ui::PanelData{{0.10f, 0.10f, 0.14f, 0.95f}, 4.0f});
            {
                aoc::ui::Widget* dp = this->m_uiManager.getWidget(this->m_menuDropdown);
                dp->padding = {6.0f, 6.0f, 6.0f, 6.0f};
                dp->childSpacing = 4.0f;
            }

            // auto required: lambda type is unnameable
            auto makeDropBtn = [this](aoc::ui::WidgetId parent, const std::string& label,
                                       std::function<void()> onClick) {
                aoc::ui::ButtonData btn;
                btn.label = label;
                btn.fontSize = 12.0f;
                btn.normalColor  = {0.15f, 0.15f, 0.20f, 0.9f};
                btn.hoverColor   = {0.25f, 0.25f, 0.32f, 0.9f};
                btn.pressedColor = {0.10f, 0.10f, 0.14f, 0.9f};
                btn.labelColor   = {0.9f, 0.9f, 0.9f, 1.0f};
                btn.cornerRadius = 3.0f;
                btn.onClick = std::move(onClick);
                [[maybe_unused]] aoc::ui::WidgetId id = this->m_uiManager.createButton(
                    parent, {0.0f, 0.0f, 98.0f, 28.0f}, std::move(btn));
            };

            makeDropBtn(this->m_menuDropdown, "Save Game", [this]() {
                ErrorCode result = aoc::save::saveGame(
                    "quicksave.aoc", this->m_world, this->m_hexGrid,
                    this->m_turnManager, this->m_economy, this->m_diplomacy,
                    this->m_fogOfWar, this->m_gameRng);
                if (result == ErrorCode::Ok) { LOG_INFO("Game saved"); }
                else { LOG_ERROR("Save failed"); }
                this->m_uiManager.removeWidget(this->m_menuDropdown);
                this->m_menuDropdown = aoc::ui::INVALID_WIDGET;
            });

            makeDropBtn(this->m_menuDropdown, "Load Game", [this]() {
                ErrorCode result = aoc::save::loadGame(
                    "quicksave.aoc", this->m_world, this->m_hexGrid,
                    this->m_turnManager, this->m_economy, this->m_diplomacy,
                    this->m_fogOfWar, this->m_gameRng);
                if (result == ErrorCode::Ok) {
                    LOG_INFO("Game loaded");
                    this->m_fogOfWar.updateVisibility(this->m_world, this->m_hexGrid, 0);
                } else { LOG_ERROR("Load failed"); }
                this->m_uiManager.removeWidget(this->m_menuDropdown);
                this->m_menuDropdown = aoc::ui::INVALID_WIDGET;
            });

            makeDropBtn(this->m_menuDropdown, "Settings", [this]() {
                this->m_uiManager.removeWidget(this->m_menuDropdown);
                this->m_menuDropdown = aoc::ui::INVALID_WIDGET;
                const std::pair<uint32_t, uint32_t> settingsFbSize = this->m_window.framebufferSize();
                if (!this->m_settingsMenu.isBuilt()) {
                    this->m_settingsMenu.build(
                        this->m_uiManager,
                        static_cast<float>(settingsFbSize.first), static_cast<float>(settingsFbSize.second),
                        [this]() {
                            aoc::ui::saveSettings(this->m_settingsMenu.settings(), "settings.cfg");
                            this->m_settingsMenu.destroy(this->m_uiManager);
                            this->applySettings();
                        });
                }
            });

            makeDropBtn(this->m_menuDropdown, "Main Menu", [this]() {
                this->m_uiManager.removeWidget(this->m_menuDropdown);
                this->m_menuDropdown = aoc::ui::INVALID_WIDGET;
                this->showReturnToMenuConfirm();
            });

            makeDropBtn(this->m_menuDropdown, "Quit", [this]() {
                glfwSetWindowShouldClose(this->m_window.handle(), GLFW_TRUE);
            });
        }
    });

    // ================================================================
    // Info panel (below top bar)
    // ================================================================
    aoc::ui::WidgetId infoPanel = this->m_uiManager.createPanel(
        {10.0f, 42.0f, 250.0f, 170.0f},
        aoc::ui::PanelData{{0.08f, 0.08f, 0.12f, 0.85f}, 6.0f});
    {
        aoc::ui::Widget* panel = this->m_uiManager.getWidget(infoPanel);
        panel->padding = {8.0f, 10.0f, 8.0f, 10.0f};
        panel->childSpacing = 5.0f;
        panel->anchor = aoc::ui::Anchor::TopLeft;
    }

    this->m_turnLabel = this->m_uiManager.createLabel(
        infoPanel, {0.0f, 0.0f, 230.0f, 14.0f},
        aoc::ui::LabelData{"Turn 0", {1.0f, 0.9f, 0.6f, 1.0f}, 14.0f});

    this->m_economyLabel = this->m_uiManager.createLabel(
        infoPanel, {0.0f, 0.0f, 230.0f, 12.0f},
        aoc::ui::LabelData{"Barter  Gold:100", {0.6f, 0.85f, 0.6f, 1.0f}, 11.0f});

    this->m_selectionLabel = this->m_uiManager.createLabel(
        infoPanel, {0.0f, 0.0f, 230.0f, 12.0f},
        aoc::ui::LabelData{"No selection", {0.8f, 0.8f, 0.8f, 1.0f}, 11.0f});

    // Research progress label + bar
    this->m_researchLabel = this->m_uiManager.createLabel(
        infoPanel, {0.0f, 0.0f, 230.0f, 12.0f},
        aoc::ui::LabelData{"No research", {0.7f, 0.85f, 1.0f, 1.0f}, 10.0f});

    constexpr float PROGRESS_BAR_W = 220.0f;
    constexpr float PROGRESS_BAR_H = 6.0f;

    this->m_researchBar = this->m_uiManager.createPanel(
        infoPanel, {0.0f, 0.0f, PROGRESS_BAR_W, PROGRESS_BAR_H},
        aoc::ui::PanelData{{0.15f, 0.15f, 0.20f, 0.8f}, 2.0f});
    this->m_researchBarFill = this->m_uiManager.createPanel(
        this->m_researchBar, {0.0f, 0.0f, 0.0f, PROGRESS_BAR_H},
        aoc::ui::PanelData{{0.2f, 0.7f, 0.3f, 0.9f}, 2.0f});

    // Production progress label + bar (visible when city selected)
    this->m_productionLabel = this->m_uiManager.createLabel(
        infoPanel, {0.0f, 0.0f, 230.0f, 12.0f},
        aoc::ui::LabelData{"", {0.9f, 0.75f, 0.4f, 1.0f}, 10.0f});

    this->m_productionBar = this->m_uiManager.createPanel(
        infoPanel, {0.0f, 0.0f, PROGRESS_BAR_W, PROGRESS_BAR_H},
        aoc::ui::PanelData{{0.15f, 0.15f, 0.20f, 0.8f}, 2.0f});
    this->m_productionBarFill = this->m_uiManager.createPanel(
        this->m_productionBar, {0.0f, 0.0f, 0.0f, PROGRESS_BAR_H},
        aoc::ui::PanelData{{0.85f, 0.6f, 0.15f, 0.9f}, 2.0f});

    // Hide production bar initially
    this->m_uiManager.setVisible(this->m_productionLabel, false);
    this->m_uiManager.setVisible(this->m_productionBar, false);

    // Bottom-right end turn button (anchored to bottom-right, repositions on resize)
    this->m_endTurnButton = this->m_uiManager.createPanel(
        {0.0f, 0.0f, 130.0f, 40.0f});
    {
        aoc::ui::Widget* endPanel = this->m_uiManager.getWidget(this->m_endTurnButton);
        if (endPanel != nullptr) {
            endPanel->anchor = aoc::ui::Anchor::BottomRight;
            endPanel->marginRight  = 20.0f;
            endPanel->marginBottom = 20.0f;
        }
    }

    aoc::ui::ButtonData endTurnBtn;
    endTurnBtn.label       = "End Turn";
    endTurnBtn.fontSize    = 15.0f;
    endTurnBtn.normalColor = {0.15f, 0.35f, 0.15f, 0.9f};
    endTurnBtn.hoverColor  = {0.20f, 0.50f, 0.20f, 0.9f};
    endTurnBtn.pressedColor = {0.10f, 0.25f, 0.10f, 0.9f};
    endTurnBtn.cornerRadius = 5.0f;
    endTurnBtn.onClick = [this]() {
        this->handleEndTurn();
    };

    // The button is inside the panel container so it gets the panel background
    [[maybe_unused]] aoc::ui::WidgetId btnId = this->m_uiManager.createButton(
        this->m_endTurnButton,
        {0.0f, 0.0f, 130.0f, 40.0f},
        std::move(endTurnBtn));

    // Victory announcement panel (hidden until game over, centered on screen)
    aoc::ui::WidgetId victoryPanel = this->m_uiManager.createPanel(
        {0.0f, 0.0f, 500.0f, 50.0f},
        aoc::ui::PanelData{{0.1f, 0.1f, 0.15f, 0.9f}, 6.0f});
    this->m_victoryLabel = this->m_uiManager.createLabel(
        victoryPanel,
        {10.0f, 10.0f, 480.0f, 30.0f},
        aoc::ui::LabelData{"", {1.0f, 0.85f, 0.2f, 1.0f}, 24.0f});
    {
        aoc::ui::Widget* vPanel = this->m_uiManager.getWidget(victoryPanel);
        if (vPanel != nullptr) {
            vPanel->isVisible = false;
            vPanel->anchor = aoc::ui::Anchor::Center;
        }
    }
}

void Application::updateHUD() {
    // Update resource reveal state for map rendering (tech-gated resources)
    {
        std::vector<bool> revealed(aoc::sim::goodCount(), true);  // Default: all visible
        const aoc::ecs::ComponentPool<aoc::sim::PlayerTechComponent>* techPool =
            this->m_world.getPool<aoc::sim::PlayerTechComponent>();
        const aoc::sim::PlayerTechComponent* playerTech = nullptr;
        if (techPool != nullptr) {
            for (uint32_t i = 0; i < techPool->size(); ++i) {
                if (techPool->data()[i].owner == 0) {
                    playerTech = &techPool->data()[i];
                    break;
                }
            }
        }
        for (uint16_t gid = 0; gid < aoc::sim::goodCount(); ++gid) {
            TechId revealTech = aoc::sim::resourceRevealTech(gid);
            if (revealTech.isValid()) {
                revealed[gid] = (playerTech != nullptr && playerTech->hasResearched(revealTech));
            }
        }
        this->m_gameRenderer.mapRenderer().setRevealedResources(revealed);
    }

    // Update turn label with year display
    const TurnNumber currentTurn = this->m_turnManager.currentTurn();
    std::string turnText = "Turn " + std::to_string(currentTurn)
                         + " (" + turnToYear(currentTurn) + ")";
    this->m_uiManager.setLabelText(this->m_turnLabel, std::move(turnText));

    // Update economy label
    std::string econText;
    {
        const aoc::ecs::ComponentPool<aoc::sim::MonetaryStateComponent>* monetaryPool =
            this->m_world.getPool<aoc::sim::MonetaryStateComponent>();
        if (monetaryPool != nullptr && monetaryPool->size() > 0) {
            const aoc::sim::MonetaryStateComponent& ms = monetaryPool->data()[0];
            econText = std::string(aoc::sim::monetarySystemName(ms.system));
            econText += "  T:" + std::to_string(ms.treasury);
            econText += "  " + std::string(aoc::sim::coinTierName(ms.effectiveCoinTier));
            if (ms.system != aoc::sim::MonetarySystemType::Barter) {
                econText += "  M:" + std::to_string(ms.moneySupply);
                int inflPct = static_cast<int>(ms.inflationRate * 100.0f);
                econText += "  Infl:" + std::to_string(inflPct) + "%";
            }
        } else {
            econText = "No economy";
        }
    }
    this->m_uiManager.setLabelText(this->m_economyLabel, std::move(econText));

    // Update selection label
    std::string selText;
    if (this->m_selectedEntity.isValid() && this->m_world.isAlive(this->m_selectedEntity)) {
        if (this->m_world.hasComponent<aoc::sim::UnitComponent>(this->m_selectedEntity)) {
            const aoc::sim::UnitComponent& unit =
                this->m_world.getComponent<aoc::sim::UnitComponent>(this->m_selectedEntity);
            const aoc::sim::UnitTypeDef& def = aoc::sim::unitTypeDef(unit.typeId);
            selText = std::string(def.name) + " HP:" + std::to_string(unit.hitPoints)
                    + " MP:" + std::to_string(unit.movementRemaining);
        } else if (this->m_world.hasComponent<aoc::sim::CityComponent>(this->m_selectedEntity)) {
            const aoc::sim::CityComponent& city =
                this->m_world.getComponent<aoc::sim::CityComponent>(this->m_selectedEntity);
            selText = city.name + " Pop:" + std::to_string(city.population);
        }
    } else {
        selText = "No selection";
    }
    this->m_uiManager.setLabelText(this->m_selectionLabel, std::move(selText));

    // Update screen size for anchor-based repositioning
    const std::pair<uint32_t, uint32_t> hudUpdateFbSize = this->m_window.framebufferSize();
    const uint32_t fbWidth = hudUpdateFbSize.first;
    const uint32_t fbHeight = hudUpdateFbSize.second;
    this->m_uiManager.setScreenSize(static_cast<float>(fbWidth),
                                     static_cast<float>(fbHeight));

    // Keep game screen dimensions in sync so open() uses correct values
    const float hudScreenW = static_cast<float>(fbWidth);
    const float hudScreenH = static_cast<float>(fbHeight);
    this->m_productionScreen.setScreenSize(hudScreenW, hudScreenH);
    this->m_techScreen.setScreenSize(hudScreenW, hudScreenH);
    this->m_governmentScreen.setScreenSize(hudScreenW, hudScreenH);
    this->m_economyScreen.setScreenSize(hudScreenW, hudScreenH);
    this->m_tradeScreen.setScreenSize(hudScreenW, hudScreenH);
    this->m_diplomacyScreen.setScreenSize(hudScreenW, hudScreenH);
    this->m_religionScreen.setScreenSize(hudScreenW, hudScreenH);
    this->m_scoreScreen.setScreenSize(hudScreenW, hudScreenH);
    this->m_cityDetailScreen.setScreenSize(hudScreenW, hudScreenH);

    // Update top bar width to match screen (stretches across full width)
    aoc::ui::Widget* topBar = this->m_uiManager.getWidget(this->m_topBar);
    if (topBar != nullptr) {
        topBar->requestedBounds.w = static_cast<float>(fbWidth);
    }

    // Update resource display in top bar
    if (this->m_resourceLabel != aoc::ui::INVALID_WIDGET) {
        std::string resText;

        // Gold, science, culture, faith -- sourced from GameState object model.
        // The ECS still has the canonical values (economy simulation writes there),
        // but we read from GameState to exercise the new path during migration.
        const aoc::game::Player* humanHud = this->m_gameState.humanPlayer();
        if (humanHud != nullptr) {
            // Gold (total + per turn income/loss)
            CurrencyAmount goldTreasury = humanHud->treasury();
            CurrencyAmount goldIncome   = humanHud->incomePerTurn();
            resText = "Gold:" + std::to_string(goldTreasury);
            if (goldIncome >= 0) {
                resText += "(+" + std::to_string(goldIncome) + "/turn)";
            } else {
                resText += "(" + std::to_string(goldIncome) + "/turn)";
            }

            // Science (+X/turn)
            float totalScience = humanHud->sciencePerTurn(this->m_hexGrid);
            int32_t sciInt = static_cast<int32_t>(totalScience);
            resText += "  Sci:(+" + std::to_string(sciInt) + "/turn)";

            // Culture (+X/turn)
            float totalCulture = humanHud->culturePerTurn(this->m_hexGrid);
            int32_t culInt = static_cast<int32_t>(totalCulture);
            resText += "  Cul:(+" + std::to_string(culInt) + "/turn)";

            // Faith (total)
            int32_t faithTotal = static_cast<int32_t>(humanHud->faith().faith);
            resText += "  Faith:" + std::to_string(faithTotal);
        } else {
            // Fallback to ECS if GameState not yet populated
            CurrencyAmount goldIncome = 0;
            this->m_world.forEach<aoc::sim::PlayerEconomyComponent>(
                [&resText, &goldIncome](EntityId, const aoc::sim::PlayerEconomyComponent& ec) {
                    if (ec.owner == 0) {
                        goldIncome = ec.incomePerTurn;
                        resText = "Gold:" + std::to_string(static_cast<int64_t>(ec.treasury));
                        if (ec.incomePerTurn >= 0) {
                            resText += "(+" + std::to_string(static_cast<int64_t>(ec.incomePerTurn)) + "/turn)";
                        } else {
                            resText += "(" + std::to_string(static_cast<int64_t>(ec.incomePerTurn)) + "/turn)";
                        }
                    }
                });

            float totalScience = aoc::sim::computePlayerScience(this->m_world, this->m_hexGrid, 0);
            int32_t sciInt = static_cast<int32_t>(totalScience);
            resText += "  Sci:(+" + std::to_string(sciInt) + "/turn)";

            float totalCulture = aoc::sim::computePlayerCulture(this->m_world, this->m_hexGrid, 0);
            int32_t culInt = static_cast<int32_t>(totalCulture);
            resText += "  Cul:(+" + std::to_string(culInt) + "/turn)";

            const aoc::ecs::ComponentPool<aoc::sim::PlayerFaithComponent>* faithPool =
                this->m_world.getPool<aoc::sim::PlayerFaithComponent>();
            if (faithPool != nullptr) {
                for (uint32_t fi = 0; fi < faithPool->size(); ++fi) {
                    if (faithPool->data()[fi].owner == 0) {
                        int32_t faithTotal = static_cast<int32_t>(faithPool->data()[fi].faith);
                        resText += "  Faith:" + std::to_string(faithTotal);
                        break;
                    }
                }
            }
        }

        const aoc::ecs::ComponentPool<aoc::sim::CityStockpileComponent>* stockPool =
            this->m_world.getPool<aoc::sim::CityStockpileComponent>();
        if (stockPool != nullptr) {
            // Aggregate resources across all player 0 cities
            std::unordered_map<uint16_t, int32_t> totals;
            for (uint32_t i = 0; i < stockPool->size(); ++i) {
                EntityId cityEntity = stockPool->entities()[i];
                const aoc::sim::CityComponent* city =
                    this->m_world.tryGetComponent<aoc::sim::CityComponent>(cityEntity);
                if (city == nullptr || city->owner != 0) {
                    continue;
                }
                for (const std::pair<const uint16_t, int32_t>& entry : stockPool->data()[i].goods) {
                    totals[entry.first] += entry.second;
                }
            }
            // Display top resources with amounts > 0
            for (const std::pair<const uint16_t, int32_t>& entry : totals) {
                if (entry.second > 0 && entry.first < aoc::sim::goodCount()) {
                    const aoc::sim::GoodDef& def = aoc::sim::goodDef(entry.first);
                    if (!resText.empty()) {
                        resText += "  ";
                    }
                    resText += std::string(def.name) + ":" + std::to_string(entry.second);
                }
            }
        }
        if (resText.empty()) {
            resText = "No resources";
        }
        this->m_uiManager.setLabelText(this->m_resourceLabel, std::move(resText));
    }

    // Update research progress bar
    {
        constexpr float RESEARCH_BAR_MAX_W = 220.0f;
        std::string researchText = "No research";
        float researchFraction = 0.0f;

        const aoc::ecs::ComponentPool<aoc::sim::PlayerTechComponent>* techPool =
            this->m_world.getPool<aoc::sim::PlayerTechComponent>();
        if (techPool != nullptr) {
            for (uint32_t i = 0; i < techPool->size(); ++i) {
                const aoc::sim::PlayerTechComponent& tech = techPool->data()[i];
                if (tech.owner == 0 && tech.currentResearch.isValid()) {
                    const aoc::sim::TechDef& tdef = aoc::sim::techDef(tech.currentResearch);
                    researchText = "Research: " + std::string(tdef.name) + " "
                                 + std::to_string(static_cast<int>(tech.researchProgress))
                                 + "/" + std::to_string(tdef.researchCost);
                    if (tdef.researchCost > 0) {
                        researchFraction = tech.researchProgress / static_cast<float>(tdef.researchCost);
                        if (researchFraction > 1.0f) { researchFraction = 1.0f; }
                    }
                    break;
                }
            }
        }
        this->m_uiManager.setLabelText(this->m_researchLabel, std::move(researchText));

        aoc::ui::Widget* fillWidget = this->m_uiManager.getWidget(this->m_researchBarFill);
        if (fillWidget != nullptr) {
            fillWidget->requestedBounds.w = researchFraction * RESEARCH_BAR_MAX_W;
        }
    }

    // Update production progress bar (visible when city selected)
    {
        constexpr float PROD_BAR_MAX_W = 220.0f;
        bool showProd = false;
        std::string prodText;
        float prodFraction = 0.0f;

        if (this->m_selectedEntity.isValid() && this->m_world.isAlive(this->m_selectedEntity) &&
            this->m_world.hasComponent<aoc::sim::CityComponent>(this->m_selectedEntity)) {
            const aoc::sim::ProductionQueueComponent* queue =
                this->m_world.tryGetComponent<aoc::sim::ProductionQueueComponent>(this->m_selectedEntity);
            if (queue != nullptr) {
                const aoc::sim::ProductionQueueItem* current = queue->currentItem();
                if (current != nullptr) {
                    showProd = true;
                    prodText = "Production: " + current->name + " "
                             + std::to_string(static_cast<int>(current->progress))
                             + "/" + std::to_string(static_cast<int>(current->totalCost));
                    if (current->totalCost > 0.0f) {
                        prodFraction = current->progress / current->totalCost;
                        if (prodFraction > 1.0f) { prodFraction = 1.0f; }
                    }
                }
            }
        }

        this->m_uiManager.setVisible(this->m_productionLabel, showProd);
        this->m_uiManager.setVisible(this->m_productionBar, showProd);
        if (showProd) {
            this->m_uiManager.setLabelText(this->m_productionLabel, std::move(prodText));
            aoc::ui::Widget* fillWidget = this->m_uiManager.getWidget(this->m_productionBarFill);
            if (fillWidget != nullptr) {
                fillWidget->requestedBounds.w = prodFraction * PROD_BAR_MAX_W;
            }
        }
    }

    // Rebuild unit action panel when selection changes
    this->rebuildUnitActionPanel();

    // Victory announcement
    if (this->m_gameOver && this->m_victoryLabel != aoc::ui::INVALID_WIDGET) {
        // Show the parent panel (which contains the label)
        aoc::ui::Widget* vLabel = this->m_uiManager.getWidget(this->m_victoryLabel);
        if (vLabel != nullptr && vLabel->parent != aoc::ui::INVALID_WIDGET) {
            aoc::ui::Widget* vPanel = this->m_uiManager.getWidget(vLabel->parent);
            if (vPanel != nullptr) {
                vPanel->isVisible = true;
                vPanel->requestedBounds.x = static_cast<float>(fbWidth) * 0.5f - 250.0f;
                vPanel->requestedBounds.y = static_cast<float>(fbHeight) * 0.5f - 25.0f;
            }
        }

        const char* victoryName =
            this->m_victoryResult.type == aoc::sim::VictoryType::Science    ? "Science" :
            this->m_victoryResult.type == aoc::sim::VictoryType::Domination ? "Domination" :
            this->m_victoryResult.type == aoc::sim::VictoryType::Culture    ? "Culture" :
            this->m_victoryResult.type == aoc::sim::VictoryType::Score      ? "Score" : "Unknown";

        std::string victoryText = "Player " +
            std::to_string(static_cast<unsigned>(this->m_victoryResult.winner)) +
            " wins by " + victoryName + " Victory!";
        this->m_uiManager.setLabelText(this->m_victoryLabel, std::move(victoryText));
    }
}

// ============================================================================
// Unit action panel
// ============================================================================

void Application::rebuildUnitActionPanel() {
    // Check if selection changed
    if (this->m_actionPanelEntity == this->m_selectedEntity) {
        return;
    }

    // Destroy old panel
    if (this->m_unitActionPanel != aoc::ui::INVALID_WIDGET) {
        this->m_uiManager.removeWidget(this->m_unitActionPanel);
        this->m_unitActionPanel = aoc::ui::INVALID_WIDGET;
    }
    this->m_actionPanelEntity = this->m_selectedEntity;

    // If no unit selected, show minimal End Turn panel
    if (!this->m_selectedEntity.isValid() || !this->m_world.isAlive(this->m_selectedEntity)
        || !this->m_world.hasComponent<aoc::sim::UnitComponent>(this->m_selectedEntity)) {
        constexpr float MIN_W = 150.0f;
        constexpr float MIN_H = 50.0f;
        this->m_unitActionPanel = this->m_uiManager.createPanel(
            {0.0f, 0.0f, MIN_W, MIN_H},
            aoc::ui::PanelData{{0.08f, 0.08f, 0.12f, 0.90f}, 6.0f});
        {
            aoc::ui::Widget* p = this->m_uiManager.getWidget(this->m_unitActionPanel);
            if (p != nullptr) {
                p->padding = {8.0f, 8.0f, 8.0f, 8.0f};
                p->anchor = aoc::ui::Anchor::BottomRight;
                p->marginRight  = 10.0f;
                p->marginBottom = 10.0f;
            }
        }
        aoc::ui::ButtonData endBtn;
        endBtn.label = "End Turn";
        endBtn.fontSize = 13.0f;
        endBtn.normalColor = {0.15f, 0.35f, 0.15f, 0.9f};
        endBtn.hoverColor = {0.20f, 0.50f, 0.20f, 0.9f};
        endBtn.pressedColor = {0.10f, 0.25f, 0.10f, 0.9f};
        endBtn.labelColor = {1.0f, 1.0f, 1.0f, 1.0f};
        endBtn.cornerRadius = 4.0f;
        endBtn.onClick = [this]() { this->handleEndTurn(); };
        (void)this->m_uiManager.createButton(
            this->m_unitActionPanel,
            {0.0f, 0.0f, MIN_W - 16.0f, 34.0f}, std::move(endBtn));
        this->m_uiManager.layout();
        return;
    }

    const aoc::sim::UnitComponent& unit =
        this->m_world.getComponent<aoc::sim::UnitComponent>(this->m_selectedEntity);
    const aoc::sim::UnitTypeDef& def = aoc::sim::unitTypeDef(unit.typeId);

    // Count buttons to size the panel
    int32_t buttonCount = 2;  // Skip + Sleep always
    if (aoc::sim::isMilitary(def.unitClass)) {
        ++buttonCount;  // Fortify
    }
    if (def.unitClass == aoc::sim::UnitClass::Scout) {
        ++buttonCount;  // Auto-Explore
    }
    if (def.unitClass == aoc::sim::UnitClass::Settler) {
        ++buttonCount;  // Found City
    }
    if (def.unitClass == aoc::sim::UnitClass::Civilian) {
        buttonCount += 2;  // Improve + Auto-Improve
    }

    const std::vector<aoc::sim::UnitUpgradeDef> upgrades =
        aoc::sim::getAvailableUpgrades(unit.typeId);
    if (!upgrades.empty()) {
        ++buttonCount;  // Upgrade
    }

    constexpr float BTN_W = 90.0f;
    constexpr float BTN_H = 24.0f;
    constexpr float BTN_SPACING = 3.0f;
    constexpr float PAD = 8.0f;
    // Bottom-right panel with unit info + action buttons + End Turn
    constexpr float PANEL_W = 280.0f;
    // Height: info header (50) + buttons rows + end turn button (40) + padding
    int32_t buttonRows = (buttonCount + 2) / 3;  // 3 buttons per row
    const float PANEL_H = 55.0f + static_cast<float>(buttonRows) * (BTN_H + BTN_SPACING) + 45.0f + PAD * 2.0f;

    this->m_unitActionPanel = this->m_uiManager.createPanel(
        {0.0f, 0.0f, PANEL_W, PANEL_H},
        aoc::ui::PanelData{{0.08f, 0.08f, 0.12f, 0.90f}, 6.0f});
    {
        aoc::ui::Widget* panel = this->m_uiManager.getWidget(this->m_unitActionPanel);
        if (panel != nullptr) {
            panel->padding = {PAD, PAD, PAD, PAD};
            panel->childSpacing = 3.0f;
            panel->anchor = aoc::ui::Anchor::BottomRight;
            panel->marginRight  = 10.0f;
            panel->marginBottom = 10.0f;
        }
    }

    // -- Unit info header --
    {
        char infoBuf[128];
        std::snprintf(infoBuf, sizeof(infoBuf), "%.*s   HP: %d/%d   MP: %d/%d",
                      static_cast<int>(def.name.size()), def.name.data(),
                      unit.hitPoints, def.maxHitPoints,
                      unit.movementRemaining, def.movementPoints);
        (void)this->m_uiManager.createLabel(
            this->m_unitActionPanel,
            {0.0f, 0.0f, PANEL_W - PAD * 2.0f, 16.0f},
            aoc::ui::LabelData{std::string(infoBuf),
                               {0.9f, 0.85f, 0.6f, 1.0f}, 11.0f});

        // Combat strength info for military units
        if (aoc::sim::isMilitary(def.unitClass)) {
            char combatBuf[96];
            if (def.rangedStrength > 0) {
                std::snprintf(combatBuf, sizeof(combatBuf),
                              "Melee: %d  Ranged: %d (range %d)",
                              def.combatStrength, def.rangedStrength, def.range);
            } else {
                std::snprintf(combatBuf, sizeof(combatBuf),
                              "Combat Strength: %d", def.combatStrength);
            }
            (void)this->m_uiManager.createLabel(
                this->m_unitActionPanel,
                {0.0f, 0.0f, PANEL_W - PAD * 2.0f, 14.0f},
                aoc::ui::LabelData{std::string(combatBuf),
                                   {0.75f, 0.75f, 0.80f, 0.9f}, 10.0f});
        }

        // Separator
        (void)this->m_uiManager.createPanel(
            this->m_unitActionPanel,
            {0.0f, 0.0f, PANEL_W - PAD * 2.0f, 1.0f},
            aoc::ui::PanelData{{0.3f, 0.3f, 0.4f, 0.4f}, 0.0f});
    }

    // Helper to create action buttons
    const EntityId selectedEnt = this->m_selectedEntity;
    aoc::ecs::World* worldPtr = &this->m_world;

    // auto required: lambda type is unnameable
    auto makeActionBtn = [this](const std::string& label,
                                 aoc::ui::Color normalColor,
                                 std::function<void()> onClick) {
        constexpr float ACTION_BTN_W2 = 125.0f;
        constexpr float ACTION_BTN_H2 = 24.0f;
        aoc::ui::ButtonData btn;
        btn.label = label;
        btn.fontSize = 10.0f;
        btn.normalColor = normalColor;
        btn.hoverColor = {normalColor.r + 0.10f, normalColor.g + 0.10f,
                          normalColor.b + 0.10f, 0.9f};
        btn.pressedColor = {normalColor.r - 0.05f, normalColor.g - 0.05f,
                            normalColor.b - 0.05f, 0.9f};
        btn.labelColor = {1.0f, 1.0f, 1.0f, 1.0f};
        btn.cornerRadius = 3.0f;
        btn.onClick = std::move(onClick);
        (void)this->m_uiManager.createButton(
            this->m_unitActionPanel,
            {0.0f, 0.0f, ACTION_BTN_W2, ACTION_BTN_H2}, std::move(btn));
    };

    // -- Skip button (all units) --
    makeActionBtn("Skip", {0.25f, 0.25f, 0.30f, 0.9f},
        [worldPtr, selectedEnt]() {
            if (!worldPtr->isAlive(selectedEnt)) { return; }
            aoc::sim::UnitComponent* u = worldPtr->tryGetComponent<aoc::sim::UnitComponent>(selectedEnt);
            if (u != nullptr) {
                u->movementRemaining = 0;
                LOG_INFO("Unit skipped turn");
            }
        });

    // -- Sleep button (all units) --
    makeActionBtn("Sleep", {0.25f, 0.25f, 0.30f, 0.9f},
        [worldPtr, selectedEnt]() {
            if (!worldPtr->isAlive(selectedEnt)) { return; }
            aoc::sim::UnitComponent* u = worldPtr->tryGetComponent<aoc::sim::UnitComponent>(selectedEnt);
            if (u != nullptr) {
                u->state = aoc::sim::UnitState::Sleeping;
                LOG_INFO("Unit sleeping");
            }
        });

    // -- Auto-Explore button (Scout units) --
    if (def.unitClass == aoc::sim::UnitClass::Scout) {
        makeActionBtn("Auto-Explore", {0.20f, 0.25f, 0.35f, 0.9f},
            [worldPtr, selectedEnt]() {
                if (!worldPtr->isAlive(selectedEnt)) { return; }
                aoc::sim::UnitComponent* u = worldPtr->tryGetComponent<aoc::sim::UnitComponent>(selectedEnt);
                if (u != nullptr) {
                    u->autoExplore = !u->autoExplore;
                    if (u->autoExplore) {
                        LOG_INFO("Auto-explore enabled for scout");
                    } else {
                        LOG_INFO("Auto-explore disabled for scout");
                    }
                }
            });
    }

    // -- Fortify button (military units) --
    if (aoc::sim::isMilitary(def.unitClass)) {
        makeActionBtn("Fortify", {0.20f, 0.30f, 0.20f, 0.9f},
            [worldPtr, selectedEnt]() {
                if (!worldPtr->isAlive(selectedEnt)) { return; }
                aoc::sim::UnitComponent* u = worldPtr->tryGetComponent<aoc::sim::UnitComponent>(selectedEnt);
                if (u != nullptr) {
                    u->state = aoc::sim::UnitState::Fortified;
                    LOG_INFO("Unit fortified (+25%% defense)");
                }
            });
    }

    // -- Found City button (Settler) --
    if (def.unitClass == aoc::sim::UnitClass::Settler) {
        makeActionBtn("Found City", {0.30f, 0.25f, 0.15f, 0.9f},
            [this, selectedEnt]() {
                if (!this->m_world.isAlive(selectedEnt)) { return; }
                const aoc::sim::UnitComponent* u =
                    this->m_world.tryGetComponent<aoc::sim::UnitComponent>(selectedEnt);
                if (u == nullptr) { return; }

                const PlayerId cityOwner = u->owner;
                const hex::AxialCoord cityPos = u->position;

                EntityId cityEntity = this->m_world.createEntity();
                const std::string cityName = aoc::sim::getNextCityName(this->m_world, cityOwner);
                aoc::sim::CityComponent newCity =
                    aoc::sim::CityComponent::create(cityOwner, cityPos, cityName);

                bool hasExistingCity = false;
                const aoc::ecs::ComponentPool<aoc::sim::CityComponent>* existingCities =
                    this->m_world.getPool<aoc::sim::CityComponent>();
                if (existingCities != nullptr) {
                    for (uint32_t ci = 0; ci < existingCities->size(); ++ci) {
                        if (existingCities->data()[ci].owner == cityOwner) {
                            hasExistingCity = true;
                            break;
                        }
                    }
                }
                if (!hasExistingCity) {
                    newCity.isOriginalCapital = true;
                    newCity.originalOwner = cityOwner;
                }

                this->m_world.addComponent<aoc::sim::CityComponent>(
                    cityEntity, std::move(newCity));
                this->m_world.addComponent<aoc::sim::ProductionQueueComponent>(
                    cityEntity, aoc::sim::ProductionQueueComponent{});

                aoc::sim::CityDistrictsComponent districts{};
                aoc::sim::CityDistrictsComponent::PlacedDistrict center;
                center.type = aoc::sim::DistrictType::CityCenter;
                center.location = cityPos;
                districts.districts.push_back(std::move(center));
                this->m_world.addComponent<aoc::sim::CityDistrictsComponent>(
                    cityEntity, std::move(districts));

                aoc::sim::claimInitialTerritory(this->m_hexGrid, cityPos, cityOwner);

                // Auto-assign workers to best tiles
                aoc::sim::CityComponent& foundedCity2 =
                    this->m_world.getComponent<aoc::sim::CityComponent>(cityEntity);
                aoc::sim::autoAssignWorkers(foundedCity2, this->m_hexGrid);

                // Mirror the new city into the GameState object model
                aoc::game::Player* gsFounder2 = this->m_gameState.player(cityOwner);
                if (gsFounder2 != nullptr) {
                    aoc::game::City& gsCity2 = gsFounder2->addCity(cityPos, cityName);
                    gsCity2.autoAssignWorkers(this->m_hexGrid);
                }

                this->m_world.destroyEntity(selectedEnt);
                this->m_selectedEntity = cityEntity;
                LOG_INFO("City founded via action panel!");

                {
                    aoc::game::Player* eurekaP = this->m_gameState.player(cityOwner);
                    if (eurekaP != nullptr) {
                        aoc::sim::checkEurekaConditions(*eurekaP,
                                                        aoc::sim::EurekaCondition::FoundCity);
                    }
                }
            });
    }

    // -- Improve button (Builder / Civilian) --
    if (def.unitClass == aoc::sim::UnitClass::Civilian) {
        makeActionBtn("Improve", {0.20f, 0.28f, 0.20f, 0.9f},
            [this, selectedEnt]() {
                if (!this->m_world.isAlive(selectedEnt)) { return; }
                aoc::sim::UnitComponent* u =
                    this->m_world.tryGetComponent<aoc::sim::UnitComponent>(selectedEnt);
                if (u == nullptr) { return; }

                const int32_t tileIndex = this->m_hexGrid.toIndex(u->position);
                const aoc::map::ImprovementType bestImpr =
                    aoc::sim::bestImprovementForTile(this->m_hexGrid, tileIndex);

                if (bestImpr != aoc::map::ImprovementType::None &&
                    this->m_hexGrid.improvement(tileIndex) == aoc::map::ImprovementType::None) {
                    this->m_hexGrid.setImprovement(tileIndex, bestImpr);
                    if (u->chargesRemaining > 0) {
                        --u->chargesRemaining;
                    }
                    LOG_INFO("Builder placed improvement via action panel");
                    if (u->chargesRemaining == 0) {
                        this->m_world.destroyEntity(selectedEnt);
                        this->m_selectedEntity = NULL_ENTITY;
                        LOG_INFO("Builder exhausted all charges");
                    }
                }
            });

        // -- Auto-Improve toggle (Civilian units) --
        makeActionBtn("Auto-Improve", {0.20f, 0.28f, 0.30f, 0.9f},
            [worldPtr, selectedEnt]() {
                if (!worldPtr->isAlive(selectedEnt)) { return; }
                aoc::sim::UnitComponent* u =
                    worldPtr->tryGetComponent<aoc::sim::UnitComponent>(selectedEnt);
                if (u != nullptr) {
                    u->autoImprove = !u->autoImprove;
                    if (u->autoImprove) {
                        LOG_INFO("Auto-improve enabled for builder");
                    } else {
                        LOG_INFO("Auto-improve disabled for builder");
                    }
                }
            });
    }

    // -- Upgrade button (if upgrade available) --
    if (!upgrades.empty()) {
        const aoc::sim::UnitUpgradeDef& upg = upgrades[0];
        const int32_t cost = aoc::sim::upgradeCost(unit.typeId, upg.to);
        const std::string upgLabel = "Upgrade (" + std::to_string(cost) + "g)";
        const UnitTypeId upgTo = upg.to;
        const PlayerId owner = unit.owner;
        makeActionBtn(upgLabel, {0.30f, 0.20f, 0.30f, 0.9f},
            [worldPtr, selectedEnt, upgTo, owner]() {
                if (!worldPtr->isAlive(selectedEnt)) { return; }
                bool success = aoc::sim::upgradeUnit(*worldPtr, selectedEnt, upgTo, owner);
                if (success) {
                    LOG_INFO("Unit upgraded via action panel!");
                }
            });
    }

    // Separator before End Turn
    (void)this->m_uiManager.createPanel(
        this->m_unitActionPanel,
        {0.0f, 0.0f, PANEL_W - PAD * 2.0f, 1.0f},
        aoc::ui::PanelData{{0.3f, 0.3f, 0.4f, 0.4f}, 0.0f});

    // End Turn button integrated into the unit panel
    {
        aoc::ui::ButtonData endBtn;
        endBtn.label = "End Turn";
        endBtn.fontSize = 13.0f;
        endBtn.normalColor = {0.15f, 0.35f, 0.15f, 0.9f};
        endBtn.hoverColor = {0.20f, 0.50f, 0.20f, 0.9f};
        endBtn.pressedColor = {0.10f, 0.25f, 0.10f, 0.9f};
        endBtn.labelColor = {1.0f, 1.0f, 1.0f, 1.0f};
        endBtn.cornerRadius = 4.0f;
        endBtn.onClick = [this]() { this->handleEndTurn(); };
        (void)this->m_uiManager.createButton(
            this->m_unitActionPanel,
            {0.0f, 0.0f, PANEL_W - PAD * 2.0f, 34.0f}, std::move(endBtn));
    }

    this->m_uiManager.layout();
}

// ============================================================================
// Screen helpers
// ============================================================================

bool Application::anyScreenOpen() const {
    return this->m_productionScreen.isOpen()
        || this->m_techScreen.isOpen()
        || this->m_governmentScreen.isOpen()
        || this->m_economyScreen.isOpen()
        || this->m_cityDetailScreen.isOpen()
        || this->m_tradeScreen.isOpen()
        || this->m_diplomacyScreen.isOpen()
        || this->m_religionScreen.isOpen()
        || this->m_scoreScreen.isOpen();
}

bool Application::onlyCityDetailScreenOpen() const {
    return this->m_cityDetailScreen.isOpen()
        && !this->m_productionScreen.isOpen()
        && !this->m_techScreen.isOpen()
        && !this->m_governmentScreen.isOpen()
        && !this->m_economyScreen.isOpen()
        && !this->m_tradeScreen.isOpen()
        && !this->m_diplomacyScreen.isOpen()
        && !this->m_religionScreen.isOpen()
        && !this->m_scoreScreen.isOpen();
}

void Application::closeAllScreens() {
    this->m_productionScreen.close(this->m_uiManager);
    this->m_techScreen.close(this->m_uiManager);
    this->m_governmentScreen.close(this->m_uiManager);
    this->m_economyScreen.close(this->m_uiManager);
    this->m_cityDetailScreen.close(this->m_uiManager);
    this->m_tradeScreen.close(this->m_uiManager);
    this->m_diplomacyScreen.close(this->m_uiManager);
    this->m_religionScreen.close(this->m_uiManager);
    this->m_scoreScreen.close(this->m_uiManager);
}

} // namespace aoc::app
