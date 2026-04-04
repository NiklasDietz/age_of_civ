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
#include "aoc/simulation/resource/ResourceComponent.hpp"
#include "aoc/simulation/resource/ResourceTypes.hpp"
#include "aoc/simulation/monetary/MonetarySystem.hpp"
#include "aoc/simulation/tech/TechTree.hpp"
#include "aoc/simulation/tech/CivicTree.hpp"
#include "aoc/simulation/tech/EraProgression.hpp"
#include "aoc/simulation/unit/Combat.hpp"
#include "aoc/simulation/city/CityGrowth.hpp"
#include "aoc/simulation/city/Happiness.hpp"
#include "aoc/save/Serializer.hpp"

#include <renderer/GraphicsDevice.hpp>
#include <renderer/RenderPipeline.hpp>
#include <renderer/Renderer2D.hpp>

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include <chrono>
#include <cstdio>

namespace aoc::app {

Application::Application() = default;

Application::~Application() {
    this->shutdown();
}

ErrorCode Application::initialize(const Config& config) {
    // -- Window --
    ErrorCode result = this->m_window.create(config.window);
    if (result != ErrorCode::Ok) {
        std::fprintf(stderr, "[Application] %s:%d Window creation failed: %.*s\n",
                     __FILE__, __LINE__,
                     static_cast<int>(describeError(result).size()),
                     describeError(result).data());
        return result;
    }

    this->m_inputManager.bindToWindow(this->m_window);

    // -- Vulkan --
    vulkan_app::GraphicsDevice::Config deviceConfig{};
    deviceConfig.appName = "AgeOfCiv";
    deviceConfig.enableValidation = config.enableValidation;

    this->m_graphicsDevice = vulkan_app::GraphicsDevice::createFromNativeWindow(
        static_cast<void*>(this->m_window.handle()), "glfw", deviceConfig);
    if (this->m_graphicsDevice == nullptr) {
        std::fprintf(stderr, "[Application] %s:%d Vulkan device creation failed\n",
                     __FILE__, __LINE__);
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

    // -- Map generation --
    aoc::map::MapGenerator::Config mapConfig{};
    mapConfig.width  = 80;
    mapConfig.height = 50;
    mapConfig.seed   = 12345;
    aoc::map::MapGenerator::generate(mapConfig, this->m_hexGrid);
    std::fprintf(stdout, "[Application] Map generated (%dx%d)\n",
                 this->m_hexGrid.width(), this->m_hexGrid.height());

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

    // -- Game renderer --
    this->m_gameRenderer.initialize(*this->m_renderPipeline, *this->m_renderer2d);

    // -- HUD --
    this->buildHUD();

    // -- Resize --
    this->m_window.setResizeCallback([this](uint32_t width, uint32_t height) {
        this->onResize(width, height);
    });

    // Center camera roughly on the map
    float centerX = 0.0f, centerY = 0.0f;
    hex::AxialCoord mapCenter = hex::offsetToAxial({mapConfig.width / 2, mapConfig.height / 2});
    hex::axialToPixel(mapCenter, this->m_gameRenderer.mapRenderer().hexSize(), centerX, centerY);
    this->m_cameraController.setPosition(centerX, centerY);

    this->m_initialized = true;
    std::fprintf(stdout, "[Application] Initialized (%ux%u), Turn 0\n", fbWidth, fbHeight);
    return ErrorCode::Ok;
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
        this->m_cameraController.update(this->m_inputManager, deltaTime, fbWidth, fbHeight);

        // -- Escape to close --
        if (this->m_inputManager.isActionPressed(InputAction::Cancel)) {
            break;
        }

        // -- Quick save/load --
        if (this->m_inputManager.isActionPressed(InputAction::QuickSave)) {
            ErrorCode saveResult = aoc::save::saveGame(
                "quicksave.aoc", this->m_world, this->m_hexGrid,
                this->m_turnManager, this->m_economy, this->m_diplomacy,
                this->m_fogOfWar, this->m_gameRng);
            if (saveResult != ErrorCode::Ok) {
                std::fprintf(stderr, "[Game] Quick save failed: %.*s\n",
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
                std::fprintf(stderr, "[Game] Quick load failed: %.*s\n",
                             static_cast<int>(describeError(loadResult).size()),
                             describeError(loadResult).data());
            } else {
                this->m_fogOfWar.updateVisibility(this->m_world, this->m_hexGrid, 0);
            }
        }

        // -- UI input (consumes clicks on widgets) --
        bool leftPressed  = this->m_inputManager.isMouseButtonPressed(GLFW_MOUSE_BUTTON_LEFT);
        bool leftReleased = this->m_inputManager.isMouseButtonReleased(GLFW_MOUSE_BUTTON_LEFT);
        this->m_uiConsumedInput = this->m_uiManager.handleInput(
            static_cast<float>(this->m_inputManager.mouseX()),
            static_cast<float>(this->m_inputManager.mouseY()),
            leftPressed, leftReleased);

        // -- Game input (only if UI didn't consume it) --
        if (!this->m_uiConsumedInput) {
            this->handleSelect();
            this->handleContextAction();
        }
        this->handleEndTurn();

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
    std::fprintf(stdout, "[Application] Shutdown complete\n");
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

    // Only move units, not cities
    if (!this->m_world.hasComponent<aoc::sim::UnitComponent>(this->m_selectedEntity)) {
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

    aoc::sim::UnitComponent& unit =
        this->m_world.getComponent<aoc::sim::UnitComponent>(this->m_selectedEntity);

    // If settler and target is valid land, found a city
    const aoc::sim::UnitTypeDef& def = aoc::sim::unitTypeDef(unit.typeId);
    if (def.unitClass == aoc::sim::UnitClass::Settler && unit.position == targetTile) {
        // Found city at current position
        EntityId cityEntity = this->m_world.createEntity();
        this->m_world.addComponent<aoc::sim::CityComponent>(
            cityEntity,
            aoc::sim::CityComponent::create(unit.owner, unit.position, "New City"));
        this->m_world.destroyEntity(this->m_selectedEntity);
        this->m_selectedEntity = cityEntity;
        std::fprintf(stdout, "[Game] City founded!\n");
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

        // Research: sum science from cities, advance tech/civic
        float totalScience = 0.0f;
        float totalCulture = 0.0f;
        aoc::ecs::ComponentPool<aoc::sim::CityComponent>* cityPool =
            this->m_world.getPool<aoc::sim::CityComponent>();
        if (cityPool != nullptr) {
            for (uint32_t ci = 0; ci < cityPool->size(); ++ci) {
                const aoc::sim::CityComponent& city = cityPool->data()[ci];
                if (city.owner != 0) {
                    continue;
                }
                for (const hex::AxialCoord& tile : city.workedTiles) {
                    if (this->m_hexGrid.isValid(tile)) {
                        aoc::map::TileYield yield = this->m_hexGrid.tileYield(
                            this->m_hexGrid.toIndex(tile));
                        totalScience += static_cast<float>(yield.science);
                        totalCulture += static_cast<float>(yield.culture);
                    }
                }
                // Base science/culture from population
                totalScience += static_cast<float>(city.population) * 0.5f;
                totalCulture += static_cast<float>(city.population) * 0.3f;
            }
        }

        // Advance research
        this->m_world.forEach<aoc::sim::PlayerTechComponent>(
            [totalScience](EntityId, aoc::sim::PlayerTechComponent& tech) {
                aoc::sim::advanceResearch(tech, totalScience);
            });
        this->m_world.forEach<aoc::sim::PlayerCivicComponent>(
            [totalCulture](EntityId, aoc::sim::PlayerCivicComponent& civic) {
                aoc::sim::advanceCivicResearch(civic, totalCulture);
            });

        // AI turns
        for (aoc::sim::ai::AIController& ai : this->m_aiControllers) {
            ai.executeTurn(this->m_world, this->m_hexGrid,
                           this->m_diplomacy, this->m_economy.market(),
                           this->m_gameRng);
            aoc::sim::executeMovement(this->m_world, ai.player(), this->m_hexGrid);
        }

        // Diplomacy modifier decay
        this->m_diplomacy.tickModifiers();

        // Update fog of war for all players
        this->m_fogOfWar.updateVisibility(this->m_world, this->m_hexGrid, 0);
        for (const aoc::sim::ai::AIController& ai : this->m_aiControllers) {
            this->m_fogOfWar.updateVisibility(this->m_world, this->m_hexGrid, ai.player());
        }

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

    std::fprintf(stdout, "[Game] Spawned starting units at (%d,%d)\n",
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

    // Spawn settler + warrior
    EntityId settler = this->m_world.createEntity();
    this->m_world.addComponent<aoc::sim::UnitComponent>(
        settler,
        aoc::sim::UnitComponent::create(player, UnitTypeId{3}, settlerPos));

    EntityId warrior = this->m_world.createEntity();
    this->m_world.addComponent<aoc::sim::UnitComponent>(
        warrior,
        aoc::sim::UnitComponent::create(player, UnitTypeId{0}, warriorPos));

    std::fprintf(stdout, "[Game] AI Player %u spawned at (%d,%d)\n",
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

    constexpr std::array<ResourcePlacement, 8> PLACEMENTS = {{
        {aoc::sim::goods::IRON_ORE,   0.04f, true,  false, true,  true,  true},
        {aoc::sim::goods::COPPER_ORE, 0.03f, true,  false, true,  true,  false},
        {aoc::sim::goods::COAL,       0.03f, false, false, true,  true,  true},
        {aoc::sim::goods::OIL,        0.02f, false, true,  true,  false, true},
        {aoc::sim::goods::HORSES,     0.03f, false, false, true,  true,  false},
        {aoc::sim::goods::WOOD,       0.06f, false, false, false, true,  true},
        {aoc::sim::goods::STONE,      0.04f, true,  true,  true,  true,  true},
        {aoc::sim::goods::WHEAT,      0.05f, false, false, true,  true,  false},
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

    std::fprintf(stdout, "[Game] Placed %d resources on map\n", totalPlaced);
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
}

} // namespace aoc::app
