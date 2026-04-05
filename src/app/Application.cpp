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
#include "aoc/save/Serializer.hpp"
#include "aoc/ui/BitmapFont.hpp"
#include "aoc/core/Log.hpp"

#include <renderer/GraphicsDevice.hpp>
#include <renderer/RenderPipeline.hpp>
#include <renderer/Renderer2D.hpp>

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include <chrono>

namespace aoc::app {

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

    this->m_appState = AppState::MainMenu;
    const float screenW = static_cast<float>(fbWidth);
    const float screenH = static_cast<float>(fbHeight);
    this->m_mainMenu.build(
        this->m_uiManager, screenW, screenH,
        [this](aoc::map::MapType type, aoc::map::MapSize size) {
            this->m_mainMenu.destroy(this->m_uiManager);
            this->m_settingsMenu.destroy(this->m_uiManager);
            this->startGame(type, size);
        },
        [this]() {
            glfwSetWindowShouldClose(this->m_window.handle(), GLFW_TRUE);
        });

    this->m_initialized = true;
    LOG_INFO("Initialized (%ux%u), showing main menu", fbWidth, fbHeight);
    return ErrorCode::Ok;
}

void Application::startGame(aoc::map::MapType mapType, aoc::map::MapSize mapSize) {
    // -- Map generation --
    const std::pair<int32_t, int32_t> dims = aoc::map::mapSizeDimensions(mapSize);
    aoc::map::MapGenerator::Config mapConfig{};
    mapConfig.width   = dims.first;
    mapConfig.height  = dims.second;
    mapConfig.seed    = 12345;
    mapConfig.mapType = mapType;
    mapConfig.mapSize = mapSize;
    aoc::map::MapGenerator::generate(mapConfig, this->m_hexGrid);
    LOG_INFO("Map generated (%dx%d)", this->m_hexGrid.width(), this->m_hexGrid.height());

    // -- Game setup --
    this->m_turnManager.setPlayerCount(1, 1);  // 1 human + 1 AI
    this->m_turnManager.beginNewTurn();
    this->placeMapResources();
    this->m_economy.initialize();
    this->m_fogOfWar.initialize(this->m_hexGrid.tileCount(), MAX_PLAYERS);
    this->m_diplomacy.initialize(2);

    this->spawnStartingEntities();

    // Spawn AI player
    this->m_aiControllers.emplace_back(static_cast<PlayerId>(1));
    this->spawnAIPlayer(1);

    this->m_fogOfWar.updateVisibility(this->m_world, this->m_hexGrid, 0);
    this->m_fogOfWar.updateVisibility(this->m_world, this->m_hexGrid, 1);

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
    LOG_INFO("Game started (map type=%d, size=%d)", static_cast<int>(mapType), static_cast<int>(mapSize));
}

void Application::run() {
    if (!this->m_initialized) {
        return;
    }

    using Clock = std::chrono::steady_clock;
    Clock::time_point previousTime = Clock::now();

    while (!this->m_window.shouldClose()) {
        Clock::time_point currentTime = Clock::now();
        float deltaTime = std::chrono::duration<float>(currentTime - previousTime).count();
        previousTime = currentTime;
        if (deltaTime > 0.1f) {
            deltaTime = 0.1f;
        }

        this->m_window.pollEvents();
        this->m_inputManager.processFrame();

        auto [fbWidth, fbHeight] = this->m_window.framebufferSize();

        // ================================================================
        // Main Menu state
        // ================================================================
        if (this->m_appState == AppState::MainMenu) {
            // Escape in settings: close settings. Escape in menu: quit.
            if (this->m_inputManager.isActionPressed(InputAction::Cancel)) {
                if (this->m_settingsMenu.isBuilt()) {
                    this->m_settingsMenu.destroy(this->m_uiManager);
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
            this->m_economyScreen.setContext(&this->m_world, &this->m_hexGrid, 0);
            this->m_economyScreen.toggle(this->m_uiManager);
        }
        if (this->m_inputManager.isActionPressed(InputAction::OpenGovernment)) {
            this->m_governmentScreen.setContext(&this->m_world, 0);
            this->m_governmentScreen.toggle(this->m_uiManager);
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

        // Update tooltip when no screen is open
        if (!this->anyScreenOpen()) {
            this->m_gameRenderer.tooltipManager().update(
                static_cast<float>(this->m_inputManager.mouseX()),
                static_cast<float>(this->m_inputManager.mouseY()),
                this->m_world, this->m_hexGrid,
                this->m_cameraController, this->m_fogOfWar,
                PlayerId{0}, fbWidth, fbHeight);
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
            frame.extent.width, frame.extent.height);

        this->m_renderPipeline->endRenderPass(frame);
        this->m_renderPipeline->endFrame(frame);
    }

    this->m_graphicsDevice->waitIdle();
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
    this->m_renderPipeline->resize(width, height);
    this->m_renderer2d->setExtent(this->m_renderPipeline->extent());
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
    if (!this->m_inputManager.isMouseButtonPressed(GLFW_MOUSE_BUTTON_RIGHT)) {
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

    // If settler and target is valid land, found a city
    const aoc::sim::UnitTypeDef& def = aoc::sim::unitTypeDef(unit.typeId);
    if (def.unitClass == aoc::sim::UnitClass::Settler && unit.position == targetTile) {
        // Found city at current position
        PlayerId cityOwner = unit.owner;
        hex::AxialCoord cityPos = unit.position;

        EntityId cityEntity = this->m_world.createEntity();
        aoc::sim::CityComponent newCity =
            aoc::sim::CityComponent::create(cityOwner, cityPos, "New City");

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

    // Order movement
    bool pathFound = aoc::sim::orderUnitMove(
        this->m_world, this->m_selectedEntity, targetTile, this->m_hexGrid);
    if (pathFound) {
        // Execute movement immediately for this turn's remaining movement points
        aoc::sim::moveUnitAlongPath(this->m_world, this->m_selectedEntity, this->m_hexGrid);
    }
}

void Application::handleEndTurn() {
    if (!this->m_inputManager.isActionPressed(InputAction::EndTurn)) {
        return;
    }

    // If the game is over, skip all turn processing
    if (this->m_gameOver) {
        return;
    }

    // Execute any remaining unit movement
    aoc::sim::executeMovement(this->m_world, 0, this->m_hexGrid);

    // Advance turn
    this->m_turnManager.submitEndTurn(0);
    if (this->m_turnManager.allPlayersReady()) {
        this->m_turnManager.executeTurn(this->m_world, this->m_scheduler);

        // Run economic simulation
        this->m_economy.executeTurn(this->m_world, this->m_hexGrid);

        // City growth and happiness
        aoc::sim::processCityGrowth(this->m_world, this->m_hexGrid, 0);
        aoc::sim::computeCityHappiness(this->m_world, 0);

        // Production queues
        aoc::sim::processProductionQueues(this->m_world, this->m_hexGrid, 0);

        // Research: compute science/culture from cities (includes building multipliers)
        float totalScience = aoc::sim::computePlayerScience(this->m_world, this->m_hexGrid, 0);
        float totalCulture = aoc::sim::computePlayerCulture(this->m_world, this->m_hexGrid, 0);

        this->m_world.forEach<aoc::sim::PlayerTechComponent>(
            [totalScience](EntityId, aoc::sim::PlayerTechComponent& tech) {
                aoc::sim::advanceResearch(tech, totalScience);
            });
        this->m_world.forEach<aoc::sim::PlayerCivicComponent>(
            [totalCulture](EntityId, aoc::sim::PlayerCivicComponent& civic) {
                aoc::sim::advanceCivicResearch(civic, totalCulture);
            });

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
                     vr.type == aoc::sim::VictoryType::Score      ? "Score" : "Unknown");
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

        // Refresh movement for next turn
        aoc::sim::refreshMovement(this->m_world, 0);
    }
}

// ============================================================================
// Game setup
// ============================================================================

void Application::spawnStartingEntities() {
    // Find land tiles near the center of the map for spawning
    hex::AxialCoord mapCenter = hex::offsetToAxial(
        {this->m_hexGrid.width() / 2, this->m_hexGrid.height() / 2});

    hex::AxialCoord settlerPos = this->findNearbyLandTile(mapCenter);
    hex::AxialCoord warriorPos = this->findNearbyLandTile(
        {settlerPos.q + 1, settlerPos.r});
    hex::AxialCoord scoutPos = this->findNearbyLandTile(
        {settlerPos.q - 1, settlerPos.r + 1});

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
    civComp2.civId = 0;  // Rome
    this->m_world.addComponent<aoc::sim::PlayerCivilizationComponent>(playerEntity, std::move(civComp2));

    aoc::sim::PlayerGreatPeopleComponent gpComp{};
    gpComp.owner = 0;
    this->m_world.addComponent<aoc::sim::PlayerGreatPeopleComponent>(playerEntity, std::move(gpComp));

    aoc::sim::PlayerEurekaComponent eurekaComp{};
    eurekaComp.owner = 0;
    this->m_world.addComponent<aoc::sim::PlayerEurekaComponent>(playerEntity, std::move(eurekaComp));

    // Create the global wonder tracker entity (one per game)
    EntityId wonderTrackerEntity = this->m_world.createEntity();
    this->m_world.addComponent<aoc::sim::GlobalWonderTracker>(
        wonderTrackerEntity, aoc::sim::GlobalWonderTracker{});

    // Spawn settler
    EntityId settler = this->m_world.createEntity();
    this->m_world.addComponent<aoc::sim::UnitComponent>(
        settler,
        aoc::sim::UnitComponent::create(0, UnitTypeId{3}, settlerPos));

    // Spawn warrior
    EntityId warrior = this->m_world.createEntity();
    this->m_world.addComponent<aoc::sim::UnitComponent>(
        warrior,
        aoc::sim::UnitComponent::create(0, UnitTypeId{0}, warriorPos));

    // Spawn scout
    EntityId scout = this->m_world.createEntity();
    this->m_world.addComponent<aoc::sim::UnitComponent>(
        scout,
        aoc::sim::UnitComponent::create(0, UnitTypeId{2}, scoutPos));

    // Center camera on the settler
    float cx = 0.0f, cy = 0.0f;
    hex::axialToPixel(settlerPos, this->m_gameRenderer.mapRenderer().hexSize(), cx, cy);
    this->m_cameraController.setPosition(cx, cy);

    LOG_INFO("Spawned starting units at (%d,%d)",
             settlerPos.q, settlerPos.r);
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

void Application::spawnAIPlayer(PlayerId player) {
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
    civComp2.civId = 1;  // Egypt for AI
    this->m_world.addComponent<aoc::sim::PlayerCivilizationComponent>(playerEntity, std::move(civComp2));

    aoc::sim::PlayerGreatPeopleComponent gpComp{};
    gpComp.owner = player;
    this->m_world.addComponent<aoc::sim::PlayerGreatPeopleComponent>(playerEntity, std::move(gpComp));

    aoc::sim::PlayerEurekaComponent eurekaComp{};
    eurekaComp.owner = player;
    this->m_world.addComponent<aoc::sim::PlayerEurekaComponent>(playerEntity, std::move(eurekaComp));

    // Spawn settler + warrior
    EntityId settler = this->m_world.createEntity();
    this->m_world.addComponent<aoc::sim::UnitComponent>(
        settler,
        aoc::sim::UnitComponent::create(player, UnitTypeId{3}, settlerPos));

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
    // Top-left info panel
    aoc::ui::WidgetId infoPanel = this->m_uiManager.createPanel(
        {10.0f, 10.0f, 250.0f, 110.0f},
        aoc::ui::PanelData{{0.08f, 0.08f, 0.12f, 0.85f}, 6.0f});
    {
        aoc::ui::Widget* panel = this->m_uiManager.getWidget(infoPanel);
        panel->padding = {8.0f, 10.0f, 8.0f, 10.0f};
        panel->childSpacing = 5.0f;
    }

    this->m_turnLabel = this->m_uiManager.createLabel(
        infoPanel, {0.0f, 0.0f, 230.0f, 16.0f},
        aoc::ui::LabelData{"Turn 0", {1.0f, 0.9f, 0.6f, 1.0f}, 16.0f});

    this->m_economyLabel = this->m_uiManager.createLabel(
        infoPanel, {0.0f, 0.0f, 230.0f, 13.0f},
        aoc::ui::LabelData{"Barter  Gold:100", {0.6f, 0.85f, 0.6f, 1.0f}, 12.0f});

    this->m_selectionLabel = this->m_uiManager.createLabel(
        infoPanel, {0.0f, 0.0f, 230.0f, 14.0f},
        aoc::ui::LabelData{"No selection", {0.8f, 0.8f, 0.8f, 1.0f}, 13.0f});

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
    // Update turn label
    std::string turnText = "Turn " + std::to_string(this->m_turnManager.currentTurn());
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
// Screen helpers
// ============================================================================

bool Application::anyScreenOpen() const {
    return this->m_productionScreen.isOpen()
        || this->m_techScreen.isOpen()
        || this->m_governmentScreen.isOpen()
        || this->m_economyScreen.isOpen()
        || this->m_cityDetailScreen.isOpen();
}

void Application::closeAllScreens() {
    this->m_productionScreen.close(this->m_uiManager);
    this->m_techScreen.close(this->m_uiManager);
    this->m_governmentScreen.close(this->m_uiManager);
    this->m_economyScreen.close(this->m_uiManager);
    this->m_cityDetailScreen.close(this->m_uiManager);
}

} // namespace aoc::app
