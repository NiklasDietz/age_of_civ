/**
 * @file Application.cpp
 * @brief Main application loop: window, Vulkan, game loop with units and turns.
 */

#include "aoc/app/Application.hpp"
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

    auto [fbWidth, fbHeight] = this->m_window.framebufferSize();
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
    // Initialize TrueType font rendering
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
    mapConfig.seed    = 12345;
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

        auto [fbWidth, fbHeight] = this->m_window.framebufferSize();

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
                    auto [sw, sh] = this->m_window.framebufferSize();
                    this->buildMainMenu(static_cast<float>(sw), static_cast<float>(sh));
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
        this->m_cameraController.update(this->m_inputManager, deltaTime, fbWidth, fbHeight);

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
                this->m_world.hasComponent<aoc::sim::CityComponent>(this->m_selectedEntity)) {
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

                constexpr std::array<std::pair<const char*, const char*>, 14> SHORTCUTS = {{
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
        if (!this->anyScreenOpen()) {
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

        // Update tooltip when no screen is open
        if (!this->anyScreenOpen()) {
            this->m_gameRenderer.tooltipManager().update(
                static_cast<float>(this->m_inputManager.mouseX()),
                static_cast<float>(this->m_inputManager.mouseY()),
                this->m_world, this->m_hexGrid,
                this->m_cameraController, this->m_fogOfWar,
                PlayerId{0}, fbWidth, fbHeight,
                this->m_selectedEntity);
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

        this->m_renderPipeline->endRenderPass(frame);
        this->m_renderPipeline->endFrame(frame);
    }

    this->m_graphicsDevice->waitIdle();
}

void Application::showReturnToMenuConfirm() {
    if (this->m_confirmDialog != aoc::ui::INVALID_WIDGET) {
        return;  // Already showing
    }

    auto [fw, fh] = this->m_window.framebufferSize();
    float screenW = static_cast<float>(fw);
    float screenH = static_cast<float>(fh);

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
    auto [fw, fh] = this->m_window.framebufferSize();
    float screenW = static_cast<float>(fw);
    float screenH = static_cast<float>(fh);

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

    LOG_INFO("Settings applied: fullscreen=%d vsync=%d showFPS=%d vol=%d/%d/%d",
             settings.fullscreen ? 1 : 0, settings.vsync ? 1 : 0, settings.showFPS ? 1 : 0,
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
}

// ============================================================================
// Game input handlers
// ============================================================================

void Application::handleSelect() {
    if (!this->m_inputManager.isMouseButtonPressed(GLFW_MOUSE_BUTTON_LEFT)) {
        return;
    }

    auto [fbWidth, fbHeight] = this->m_window.framebufferSize();
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

    // Check if a unit is on this tile
    aoc::ecs::ComponentPool<aoc::sim::UnitComponent>* unitPool =
        this->m_world.getPool<aoc::sim::UnitComponent>();
    if (unitPool != nullptr) {
        for (uint32_t i = 0; i < unitPool->size(); ++i) {
            if (unitPool->data()[i].position == clickedTile) {
                this->m_selectedEntity = unitPool->entities()[i];
                return;
            }
        }
    }

    // Check if a city is on this tile
    aoc::ecs::ComponentPool<aoc::sim::CityComponent>* cityPool =
        this->m_world.getPool<aoc::sim::CityComponent>();
    if (cityPool != nullptr) {
        for (uint32_t i = 0; i < cityPool->size(); ++i) {
            if (cityPool->data()[i].location == clickedTile) {
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

    auto [fbWidth, fbHeight] = this->m_window.framebufferSize();
    float worldX = 0.0f, worldY = 0.0f;
    this->m_cameraController.screenToWorld(
        this->m_inputManager.mouseX(), this->m_inputManager.mouseY(),
        worldX, worldY, fbWidth, fbHeight);

    float hexSize = this->m_gameRenderer.mapRenderer().hexSize();
    hex::AxialCoord targetTile = hex::pixelToAxial(worldX, worldY, hexSize);

    if (!this->m_hexGrid.isValid(targetTile)) {
        return;
    }

    // Handle right-click on a selected city: enqueue production
    if (this->m_world.hasComponent<aoc::sim::CityComponent>(this->m_selectedEntity)) {
        aoc::sim::CityComponent& city =
            this->m_world.getComponent<aoc::sim::CityComponent>(this->m_selectedEntity);
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
        this->m_world.destroyEntity(this->m_selectedEntity);
        this->m_selectedEntity = cityEntity;
        LOG_INFO("City founded!");

        // Eureka: FoundCity condition
        aoc::sim::checkEurekaConditions(this->m_world, cityOwner,
                                        aoc::sim::EurekaCondition::FoundCity);
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
                aoc::sim::checkEurekaConditions(this->m_world, unit.owner,
                                                aoc::sim::EurekaCondition::BuildQuarry);
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
        // Execute movement immediately for this turn's remaining movement points
        aoc::sim::moveUnitAlongPath(this->m_world, this->m_selectedEntity, this->m_hexGrid);
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
    if (!this->m_inputManager.isActionPressed(InputAction::EndTurn)) {
        return;
    }

    // If the game is over, skip all turn processing
    if (this->m_gameOver) {
        return;
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

        // Run economic simulation
        this->m_economy.executeTurn(this->m_world, this->m_hexGrid);

        // Unit and building maintenance costs
        aoc::sim::processUnitMaintenance(this->m_world, 0);
        aoc::sim::processBuildingMaintenance(this->m_world, 0);
        for (const aoc::sim::ai::AIController& ai : this->m_aiControllers) {
            aoc::sim::processUnitMaintenance(this->m_world, ai.player());
            aoc::sim::processBuildingMaintenance(this->m_world, ai.player());
        }

        // City connection gold bonuses
        aoc::sim::processCityConnections(this->m_world, this->m_hexGrid, 0);
        for (const aoc::sim::ai::AIController& ai : this->m_aiControllers) {
            aoc::sim::processCityConnections(this->m_world, this->m_hexGrid, ai.player());
        }

        // Advanced economics (tariffs, banking, debt crisis, infrastructure)
        aoc::sim::processAdvancedEconomics(this->m_world, this->m_hexGrid, 0,
                                           this->m_economy.market());
        for (const aoc::sim::ai::AIController& ai : this->m_aiControllers) {
            aoc::sim::processAdvancedEconomics(this->m_world, this->m_hexGrid,
                                               ai.player(), this->m_economy.market());
        }

        // War weariness
        aoc::sim::processWarWeariness(this->m_world, 0, this->m_diplomacy);
        for (const aoc::sim::ai::AIController& ai : this->m_aiControllers) {
            aoc::sim::processWarWeariness(this->m_world, ai.player(), this->m_diplomacy);
        }

        // Golden/Dark age effects
        aoc::sim::processAgeEffects(this->m_world, 0);
        for (const aoc::sim::ai::AIController& ai : this->m_aiControllers) {
            aoc::sim::processAgeEffects(this->m_world, ai.player());
        }

        // City growth and happiness
        aoc::sim::processCityGrowth(this->m_world, this->m_hexGrid, 0);
        aoc::sim::computeCityHappiness(this->m_world, 0);

        // City loyalty (after happiness so loyalty can read happiness values)
        aoc::sim::computeCityLoyalty(this->m_world, this->m_hexGrid, 0);
        for (const aoc::sim::ai::AIController& ai : this->m_aiControllers) {
            aoc::sim::computeCityLoyalty(this->m_world, this->m_hexGrid, ai.player());
        }

        // Religion: accumulate faith, spread, and apply bonuses
        aoc::sim::accumulateFaith(this->m_world, this->m_hexGrid, 0);
        for (const aoc::sim::ai::AIController& ai : this->m_aiControllers) {
            aoc::sim::accumulateFaith(this->m_world, this->m_hexGrid, ai.player());
        }
        aoc::sim::processReligiousSpread(this->m_world, this->m_hexGrid);
        aoc::sim::applyReligionBonuses(this->m_world, 0);
        for (const aoc::sim::ai::AIController& ai : this->m_aiControllers) {
            aoc::sim::applyReligionBonuses(this->m_world, ai.player());
        }

        // Production queues
        aoc::sim::processProductionQueues(this->m_world, this->m_hexGrid, 0);

        // City bombardment: cities with Walls shoot at adjacent enemies
        aoc::sim::processCityBombardment(this->m_world, this->m_hexGrid, 0, this->m_gameRng);
        for (const aoc::sim::ai::AIController& ai : this->m_aiControllers) {
            aoc::sim::processCityBombardment(this->m_world, this->m_hexGrid,
                                              ai.player(), this->m_gameRng);
        }

        // City-state bonuses
        aoc::sim::processCityStateBonuses(this->m_world, 0);
        for (const aoc::sim::ai::AIController& ai : this->m_aiControllers) {
            aoc::sim::processCityStateBonuses(this->m_world, ai.player());
        }

        // Clear event log for new turn
        this->m_eventLog.clear();

        // Research: compute science/culture from cities (includes building multipliers)
        float totalScience = aoc::sim::computePlayerScience(this->m_world, this->m_hexGrid, 0);
        float totalCulture = aoc::sim::computePlayerCulture(this->m_world, this->m_hexGrid, 0);

        bool techCompleted = false;
        bool civicCompleted = false;
        this->m_world.forEach<aoc::sim::PlayerTechComponent>(
            [totalScience, &techCompleted](EntityId, aoc::sim::PlayerTechComponent& tech) {
                if (tech.owner == 0) {
                    techCompleted = aoc::sim::advanceResearch(tech, totalScience);
                }
            });
        this->m_world.forEach<aoc::sim::PlayerCivicComponent>(
            [totalCulture, &civicCompleted](EntityId, aoc::sim::PlayerCivicComponent& civic) {
                if (civic.owner == 0) {
                    civicCompleted = aoc::sim::advanceCivicResearch(civic, totalCulture);
                }
            });

        // Tech completion notification
        if (techCompleted) {
            // Find which tech was just completed (it was cleared by completeResearch,
            // so scan for the most recently completed one)
            std::string techName = "Unknown";
            this->m_world.forEach<aoc::sim::PlayerTechComponent>(
                [&techName](EntityId, const aoc::sim::PlayerTechComponent& tech) {
                    if (tech.owner != 0) {
                        return;
                    }
                    // Find the highest-ID completed tech as a heuristic
                    const uint16_t count = aoc::sim::techCount();
                    for (uint16_t t = count; t > 0; --t) {
                        if (tech.hasResearched(TechId{static_cast<uint16_t>(t - 1)})) {
                            techName = std::string(aoc::sim::techDef(TechId{static_cast<uint16_t>(t - 1)}).name);
                            break;
                        }
                    }
                });
            LOG_INFO("Research completed: %s", techName.c_str());
            this->m_eventLog.addEvent("Researched " + techName);
            this->m_notificationManager.push("Research complete: " + techName, 4.0f,
                                              0.3f, 0.7f, 1.0f);
            this->m_soundQueue.push(aoc::audio::SoundEffect::TechResearched);

            // Era score for completing a tech
            aoc::sim::addEraScore(this->m_world, 0, 2, "Researched " + techName);

            // Auto-open tech screen if no next research is queued
            bool hasNextResearch = false;
            this->m_world.forEach<aoc::sim::PlayerTechComponent>(
                [&hasNextResearch](EntityId, const aoc::sim::PlayerTechComponent& tech) {
                    if (tech.owner == 0 && tech.currentResearch.isValid()) {
                        hasNextResearch = true;
                    }
                });
            if (!hasNextResearch) {
                this->m_techScreen.setContext(&this->m_world, 0);
                this->m_techScreen.open(this->m_uiManager);
            }
        }

        // Civic completion notification
        if (civicCompleted) {
            std::string civicName = "Unknown";
            this->m_world.forEach<aoc::sim::PlayerCivicComponent>(
                [&civicName](EntityId, const aoc::sim::PlayerCivicComponent& civic) {
                    if (civic.owner != 0) {
                        return;
                    }
                    const uint16_t count = aoc::sim::civicCount();
                    for (uint16_t c = count; c > 0; --c) {
                        if (civic.hasCompleted(CivicId{static_cast<uint16_t>(c - 1)})) {
                            civicName = std::string(aoc::sim::civicDef(CivicId{static_cast<uint16_t>(c - 1)}).name);
                            break;
                        }
                    }
                });
            LOG_INFO("Civic completed: %s", civicName.c_str());
            this->m_eventLog.addEvent("Completed " + civicName);
            this->m_notificationManager.push("Civic complete: " + civicName, 4.0f,
                                              0.8f, 0.5f, 1.0f);
            this->m_soundQueue.push(aoc::audio::SoundEffect::CivicCompleted);
        }

        // Great People: accumulate points and check recruitment
        aoc::sim::accumulateGreatPeoplePoints(this->m_world, 0);
        aoc::sim::checkGreatPeopleRecruitment(this->m_world, 0);

        // Border expansion
        aoc::sim::processBorderExpansion(this->m_world, this->m_hexGrid, 0);

        // AI turns
        for (aoc::sim::ai::AIController& ai : this->m_aiControllers) {
            ai.executeTurn(this->m_world, this->m_hexGrid,
                           this->m_diplomacy, this->m_economy.market(),
                           this->m_gameRng);
            aoc::sim::executeMovement(this->m_world, ai.player(), this->m_hexGrid);
        }

        // Barbarian turn (after AI, before diplomacy)
        this->m_barbarianController.executeTurn(this->m_world, this->m_hexGrid, this->m_gameRng);

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

        // Climate system
        {
            aoc::ecs::ComponentPool<aoc::sim::GlobalClimateComponent>* climatePool =
                this->m_world.getPool<aoc::sim::GlobalClimateComponent>();
            if (climatePool != nullptr) {
                for (uint32_t ci = 0; ci < climatePool->size(); ++ci) {
                    // Industrial era onward: add CO2 per city
                    aoc::ecs::ComponentPool<aoc::sim::CityComponent>* cityPool =
                        this->m_world.getPool<aoc::sim::CityComponent>();
                    if (cityPool != nullptr) {
                        for (uint32_t cj = 0; cj < cityPool->size(); ++cj) {
                            // Each city adds a small amount of CO2 (scales with population)
                            const float co2PerCity = static_cast<float>(
                                cityPool->data()[cj].population) * 0.1f;
                            climatePool->data()[ci].addCO2(co2PerCity);
                        }
                    }
                    climatePool->data()[ci].processTurn(this->m_hexGrid, this->m_gameRng);
                }
            }
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
                        // Map era index to music track
                        // Ancient=0, Classical=1, Medieval=2, Renaissance=3,
                        // Industrial=4, Modern=5, Information=6
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

        // Update victory trackers and check win conditions
        aoc::sim::updateVictoryTrackers(this->m_world, this->m_hexGrid);
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

            // Open end-game score screen
            const uint8_t totalPlayers = static_cast<uint8_t>(1 + this->m_aiControllers.size());
            this->m_scoreScreen.setContext(
                &this->m_world, &this->m_hexGrid, vr, totalPlayers,
                [this]() { this->returnToMainMenu(); });
            this->m_scoreScreen.open(this->m_uiManager);
        }

        // Process government/policy unlocks from completed civics
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
    // Find a land tile near the center of the map for the capital
    hex::AxialCoord mapCenter = hex::offsetToAxial(
        {this->m_hexGrid.width() / 4, this->m_hexGrid.height() / 4});

    hex::AxialCoord capitalPos = this->findNearbyLandTile(mapCenter);

    // Create player economy entity with monetary state
    EntityId playerEntity = this->m_world.createEntity();
    aoc::sim::MonetaryStateComponent monetary{};
    monetary.owner = 0;
    monetary.system = aoc::sim::MonetarySystemType::Barter;
    monetary.goldReserves = 100;
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

    // Center camera on the settler
    float cx = 0.0f, cy = 0.0f;
    hex::axialToPixel(capitalPos, this->m_gameRenderer.mapRenderer().hexSize(), cx, cy);
    this->m_cameraController.setPosition(cx, cy);

    LOG_INFO("Spawned starting units at (%d,%d)", capitalPos.q, capitalPos.r);
}

hex::AxialCoord Application::findNearbyLandTile(hex::AxialCoord target) const {
    // Spiral outward from target to find a walkable land tile
    for (int32_t radius = 0; radius < 10; ++radius) {
        std::vector<hex::AxialCoord> ringTiles;
        hex::ring(target, radius, std::back_inserter(ringTiles));
        for (const hex::AxialCoord& tile : ringTiles) {
            if (this->m_hexGrid.isValid(tile)) {
                int32_t index = this->m_hexGrid.toIndex(tile);
                if (this->m_hexGrid.movementCost(index) > 0) {
                    return tile;
                }
            }
        }
    }
    return target;  // Fallback (shouldn't happen on a reasonable map)
}

// ============================================================================
// AI player spawning
// ============================================================================

void Application::spawnAIPlayer(PlayerId player, aoc::sim::CivId civId) {
    // Spawn AI units on the opposite side of the map from the human player
    hex::AxialCoord aiSpawn = hex::offsetToAxial(
        {this->m_hexGrid.width() * 3 / 4, this->m_hexGrid.height() * 3 / 4});

    hex::AxialCoord settlerPos = this->findNearbyLandTile(aiSpawn);
    hex::AxialCoord warriorPos = this->findNearbyLandTile(
        {settlerPos.q + 1, settlerPos.r});

    // Player entity with economy + tech
    EntityId playerEntity = this->m_world.createEntity();

    aoc::sim::MonetaryStateComponent monetary{};
    monetary.owner = player;
    monetary.system = aoc::sim::MonetarySystemType::Barter;
    monetary.goldReserves = 100;
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
    auto [fbW, fbH] = this->m_window.framebufferSize();
    float screenW = static_cast<float>(fbW);

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
    }

    // Helper for top bar buttons
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
            auto [fw, fh] = this->m_window.framebufferSize();
            float dropX = static_cast<float>(fw) - 120.0f;
            float dropY = 34.0f;

            this->m_menuDropdown = this->m_uiManager.createPanel(
                {dropX, dropY, 110.0f, 150.0f},
                aoc::ui::PanelData{{0.10f, 0.10f, 0.14f, 0.95f}, 4.0f});
            {
                aoc::ui::Widget* dp = this->m_uiManager.getWidget(this->m_menuDropdown);
                dp->padding = {6.0f, 6.0f, 6.0f, 6.0f};
                dp->childSpacing = 4.0f;
            }

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
                auto [sw, sh] = this->m_window.framebufferSize();
                if (!this->m_settingsMenu.isBuilt()) {
                    this->m_settingsMenu.build(
                        this->m_uiManager,
                        static_cast<float>(sw), static_cast<float>(sh),
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

    // Bottom-right end turn button
    this->m_endTurnButton = this->m_uiManager.createPanel(
        {0.0f, 0.0f, 130.0f, 40.0f});  // Position set in updateHUD

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
        {390.0f, 300.0f, 500.0f, 50.0f},
        aoc::ui::PanelData{{0.1f, 0.1f, 0.15f, 0.9f}, 6.0f});
    this->m_victoryLabel = this->m_uiManager.createLabel(
        victoryPanel,
        {10.0f, 10.0f, 480.0f, 30.0f},
        aoc::ui::LabelData{"", {1.0f, 0.85f, 0.2f, 1.0f}, 24.0f});
    {
        aoc::ui::Widget* vPanel = this->m_uiManager.getWidget(victoryPanel);
        if (vPanel != nullptr) {
            vPanel->isVisible = false;
        }
    }
}

void Application::updateHUD() {
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
            econText += "  Gold:" + std::to_string(ms.goldReserves);
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

    // Position end turn button at bottom-right of screen
    auto [fbWidth, fbHeight] = this->m_window.framebufferSize();
    aoc::ui::Widget* endTurnPanel = this->m_uiManager.getWidget(this->m_endTurnButton);
    if (endTurnPanel != nullptr) {
        endTurnPanel->requestedBounds.x = static_cast<float>(fbWidth) - 150.0f;
        endTurnPanel->requestedBounds.y = static_cast<float>(fbHeight) - 60.0f;
    }

    // Update top bar width to match screen
    aoc::ui::Widget* topBar = this->m_uiManager.getWidget(this->m_topBar);
    if (topBar != nullptr) {
        topBar->requestedBounds.w = static_cast<float>(fbWidth);
    }

    // Update resource display in top bar
    if (this->m_resourceLabel != aoc::ui::INVALID_WIDGET) {
        std::string resText;
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
                for (const auto& [goodId, amount] : stockPool->data()[i].goods) {
                    totals[goodId] += amount;
                }
            }
            // Display top resources with amounts > 0
            for (const auto& [goodId, amount] : totals) {
                if (amount > 0 && goodId < aoc::sim::goodCount()) {
                    const aoc::sim::GoodDef& def = aoc::sim::goodDef(goodId);
                    if (!resText.empty()) {
                        resText += "  ";
                    }
                    resText += std::string(def.name) + ":" + std::to_string(amount);
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

    // Only build for valid unit selection
    if (!this->m_selectedEntity.isValid() || !this->m_world.isAlive(this->m_selectedEntity)) {
        return;
    }
    if (!this->m_world.hasComponent<aoc::sim::UnitComponent>(this->m_selectedEntity)) {
        return;
    }

    const aoc::sim::UnitComponent& unit =
        this->m_world.getComponent<aoc::sim::UnitComponent>(this->m_selectedEntity);
    const aoc::sim::UnitTypeDef& def = aoc::sim::unitTypeDef(unit.typeId);

    auto [fbW, fbH] = this->m_window.framebufferSize();
    const float screenW = static_cast<float>(fbW);
    const float screenH = static_cast<float>(fbH);

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
    constexpr float BTN_H = 28.0f;
    constexpr float BTN_SPACING = 4.0f;
    constexpr float PAD = 8.0f;
    const float panelW = static_cast<float>(buttonCount) * (BTN_W + BTN_SPACING) - BTN_SPACING + PAD * 2.0f;
    constexpr float PANEL_H = BTN_H + PAD * 2.0f;
    const float panelX = (screenW - panelW) * 0.5f;
    const float panelY = screenH - 110.0f;

    this->m_unitActionPanel = this->m_uiManager.createPanel(
        {panelX, panelY, panelW, PANEL_H},
        aoc::ui::PanelData{{0.08f, 0.08f, 0.12f, 0.85f}, 6.0f});
    {
        aoc::ui::Widget* panel = this->m_uiManager.getWidget(this->m_unitActionPanel);
        if (panel != nullptr) {
            panel->layoutDirection = aoc::ui::LayoutDirection::Horizontal;
            panel->padding = {PAD, PAD, PAD, PAD};
            panel->childSpacing = BTN_SPACING;
        }
    }

    // Helper to create action buttons
    const EntityId selectedEnt = this->m_selectedEntity;
    aoc::ecs::World* worldPtr = &this->m_world;

    auto makeActionBtn = [this](const std::string& label,
                                 aoc::ui::Color normalColor,
                                 std::function<void()> onClick) {
        constexpr float ACTION_BTN_W = 90.0f;
        constexpr float ACTION_BTN_H = 28.0f;
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
            {0.0f, 0.0f, ACTION_BTN_W, ACTION_BTN_H}, std::move(btn));
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
                this->m_world.destroyEntity(selectedEnt);
                this->m_selectedEntity = cityEntity;
                LOG_INFO("City founded via action panel!");

                aoc::sim::checkEurekaConditions(this->m_world, cityOwner,
                                                aoc::sim::EurekaCondition::FoundCity);
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
