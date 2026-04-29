/**
 * @file Application.cpp
 * @brief Main application loop: window, Vulkan, game loop with units and turns.
 */

#include "aoc/app/Application.hpp"
#include "aoc/ui/Theme.hpp"
#include "aoc/ui/IconAtlas.hpp"
#include "aoc/data/DataLoader.hpp"
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
#include "aoc/ui/SpectatorHUD.hpp"
#include "aoc/ui/GameNotifications.hpp"
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
#include <random>
#include <utility>

#include "aoc/app/ScreenshotEncoder.hpp"

// getNextCityName is defined in TurnProcessor.cpp

#include "ApplicationHelpers.hpp"

namespace aoc::app {

// `turnToYear` now lives in ApplicationHelpers.hpp so the HUD
// translation unit (Application_HUD.cpp) shares the same implementation.
using aoc::app::detail::turnToYear;

Application::Application() = default;

Application::~Application() {
    this->shutdown();
}

ErrorCode Application::initialize(const Config& config) {
    // -- Game definitions (JSON data files with constexpr fallbacks) --
    // Must run before any subsystem reads building / unit / tech / recipe
    // tables. Falls back silently to hardcoded defaults per file if any JSON
    // is missing or unparseable.
    if (!aoc::data::DataLoader::instance().initialize("data")) {
        LOG_WARN("DataLoader fell back to constexpr defaults for one or more definition files");
    }

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

    // Register every modal screen with the central registry. Any future
    // screen just needs one `add()` call — the `anyScreenOpen`,
    // `closeAllScreens`, and `onResize` helpers all pick it up
    // automatically. SettingsMenu is included so it no longer slips past
    // the input-gate while open.
    this->m_screenRegistry.add(&this->m_productionScreen);
    this->m_screenRegistry.add(&this->m_techScreen);
    this->m_screenRegistry.add(&this->m_governmentScreen);
    this->m_screenRegistry.add(&this->m_economyScreen);
    this->m_screenRegistry.add(&this->m_cityDetailScreen);
    this->m_screenRegistry.add(&this->m_tradeScreen);
    this->m_screenRegistry.add(&this->m_tradeRouteSetupScreen);
    this->m_screenRegistry.add(&this->m_diplomacyScreen);
    this->m_screenRegistry.add(&this->m_religionScreen);
    this->m_screenRegistry.add(&this->m_scoreScreen);
    this->m_screenRegistry.add(&this->m_settingsMenu);
    this->m_screenRegistry.add(&this->m_loadingScreen);

    // Seed the icon atlas with built-in placeholders so any widget
    // that references `resources.*` / `civs.*` / etc. renders a
    // distinct colour. Optional overrides from `data/icons.txt`.
    aoc::ui::IconAtlas::instance().seedBuiltIns();
    (void)aoc::ui::IconAtlas::instance().loadPlaceholders("data/icons.txt");

    // Standard GLFW cursors created once — swapped per-frame based on
    // the hovered widget's `hoverCursor` hint. Saves repeated alloc.
    // Stored as void* in the header so GLFW stays out of public API.
    this->m_cursors.arrow     = glfwCreateStandardCursor(GLFW_ARROW_CURSOR);
    this->m_cursors.hand      = glfwCreateStandardCursor(GLFW_HAND_CURSOR);
    this->m_cursors.ibeam     = glfwCreateStandardCursor(GLFW_IBEAM_CURSOR);
    this->m_cursors.crossHair = glfwCreateStandardCursor(GLFW_CROSSHAIR_CURSOR);

    this->m_appState = AppState::MainMenu;
    const float screenW = static_cast<float>(fbWidth);
    const float screenH = static_cast<float>(fbHeight);

    // Seed the global Theme with the initial viewport + DPI. The resize
    // handler keeps these in sync for window moves across monitors.
    {
        aoc::ui::Theme& t = aoc::ui::theme();
        t.viewportW = screenW;
        t.viewportH = screenH;
        float xscale = 1.0f;
        float yscale = 1.0f;
        glfwGetWindowContentScale(this->m_window.handle(), &xscale, &yscale);
        t.dpiScale = std::max(xscale, yscale);
        if (t.dpiScale <= 0.0f) { t.dpiScale = 1.0f; }
    }

    this->buildMainMenu(screenW, screenH);

    // Publish DBus service on the session bus so external tooling (MCP shim,
    // test harnesses) can drive the running game. Non-fatal on failure --
    // the game is perfectly usable without the bus, and it may be missing on
    // headless CI runners or non-systemd hosts.
    if (!this->m_dbusService.start()) {
        LOG_INFO("DBus service not available; external automation disabled");
    }

    this->m_initialized = true;
    LOG_INFO("Initialized (%ux%u), showing main menu", fbWidth, fbHeight);
    return ErrorCode::Ok;
}

void Application::startGame(const aoc::ui::GameSetupConfig& config) {
    // Full state reset so a new game doesn't inherit prior game's data.
    // User feedback: "starting AI game after one finished, the old one
    // restarts and ends with the victory window while the new game proceeds".
    // Root cause: m_aiControllers / m_gameOver / m_turnManager / etc were
    // not cleared between games when entering startGame directly.
    this->m_aiControllers.clear();
    this->m_gameOver = false;
    this->m_selectedUnit = nullptr;
    this->m_selectedCity = nullptr;
    this->m_actionPanelUnit = nullptr;
    this->m_spectatorMode = false;
    this->m_spectatorPaused = false;
    this->m_spectatorTurnAccumulator = 0.0f;
    this->m_spectatorFollowPlayer = -1;
    this->m_victoryResult = {};

    // Apply user-selected turn limit. Used by Score victory + spectator HUD.
    this->m_spectatorMaxTurns = config.maxTurns;

    // Loading overlay. Rendered on next frame — we synchronously
    // block through map-gen / spawn below so the screen only appears
    // if something errors out, but it sets expectation for async work
    // when that arrives. Progress is coarse (phase-labelled).
    this->m_loadingScreen.open(this->m_uiManager, "Generating World");
    this->m_loadingScreen.setStatus("Generating terrain...");
    this->m_loadingScreen.setProgress(0.1f);

    // -- Map generation --
    const std::pair<int32_t, int32_t> dims = aoc::map::mapSizeDimensions(config.mapSize);
    aoc::map::MapGenerator::Config mapConfig{};
    mapConfig.width   = dims.first;
    mapConfig.height  = dims.second;
    // Use hardware entropy for a unique map seed each launch.  steady_clock
    // truncated to 32 bits loses entropy on systems where the clock advances
    // slowly between launches; random_device draws from the OS entropy pool
    // (getrandom/CryptGenRandom) giving true per-launch uniqueness.
    std::random_device rd;
    const uint32_t timeSeed = rd();
    mapConfig.seed = timeSeed;
    LOG_INFO("Map seed: %u", timeSeed);
    this->m_gameRng = aoc::Random(timeSeed + 1);
    mapConfig.mapType = config.mapType;
    mapConfig.mapSize = config.mapSize;
    mapConfig.placement = config.placement;
    aoc::map::MapGenerator::generate(mapConfig, this->m_hexGrid);
    LOG_INFO("Map generated (%dx%d)", this->m_hexGrid.width(), this->m_hexGrid.height());

    // Set camera world width for cylindrical wrapping
    if (this->m_hexGrid.topology() == aoc::map::MapTopology::Cylindrical) {
        constexpr float SQRT3 = 1.7320508075688772f;
        float hexSize = this->m_gameRenderer.mapRenderer().hexSize();
        float worldWidth = static_cast<float>(this->m_hexGrid.width()) * SQRT3 * hexSize;
        this->m_cameraController.setWorldWidth(worldWidth);
    }

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
    if (config.mapType != aoc::map::MapType::LandWithSeas) {
        this->placeMapResources();
    } else {
        LOG_INFO("Skipping random resource placement (LandWithSeas uses geology-based placement)");
    }
    this->m_economy.initialize();
    this->m_fogOfWar.initialize(this->m_hexGrid.tileCount(), MAX_PLAYERS);
    this->m_diplomacy.initialize(config.playerCount);

    // -- Initialize new GameState object model --
    this->m_gameState.initialize(static_cast<int32_t>(config.playerCount));
    for (uint8_t i = 0; i < config.playerCount; ++i) {
        aoc::game::Player* gsPlayer = this->m_gameState.player(static_cast<PlayerId>(i));
        gsPlayer->setCivId(config.players[i].civId);
        gsPlayer->setHuman(config.players[i].isHuman);
        gsPlayer->setTreasury(0);
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

    // -- War weariness, era score, religion, grievance per-player initialization --
    // These are owned by the GameState Player objects; no ECS entities needed.
    for (uint8_t p = 0; p < config.playerCount; ++p) {
        const PlayerId pid = static_cast<PlayerId>(p);
        aoc::game::Player* initPlayer = this->m_gameState.player(pid);
        if (initPlayer != nullptr) {
            initPlayer->warWeariness().owner = pid;
            initPlayer->eraScore().owner     = pid;
            initPlayer->faith().owner        = pid;
            initPlayer->grievances().owner   = pid;
        }
    }
    LOG_INFO("War weariness, era score, faith, and grievances initialized for %u players",
             static_cast<unsigned>(config.playerCount));

    // -- Religion tracker already part of GameState; reset it for new game --
    this->m_gameState.religionTracker() = aoc::sim::GlobalReligionTracker{};
    LOG_INFO("Religion system initialized for %u players",
             static_cast<unsigned>(config.playerCount));

    // -- World Congress already part of GameState; reset it for new game --
    this->m_gameState.worldCongress() = aoc::sim::WorldCongressComponent{};
    LOG_INFO("World Congress initialized (first session at turn 50)");

    // -- Climate system already part of GameState; reset it for new game --
    this->m_gameState.climate() = aoc::sim::GlobalClimateComponent{};
    LOG_INFO("Climate system initialized");

    // -- Replay recorder --
    this->m_replayRecorder.clear();

    // -- Sound event queue --
    this->m_soundQueue.clear();
    this->m_soundQueue.push(aoc::audio::SoundEffect::TurnStart);

    // -- Music: set to Ancient era --
    this->m_musicManager.setTrack(aoc::audio::MusicTrack::Ancient);

    // Place goody huts (ancient ruins): ~1 per 80 land tiles, never adjacent
    // to starting positions. Starting positions come from each player's first
    // unit (settler) since the capital isn't founded yet.
    {
        std::vector<aoc::hex::AxialCoord> startPositions;
        startPositions.reserve(config.playerCount);
        for (uint8_t i = 0; i < config.playerCount; ++i) {
            const aoc::game::Player* pptr =
                this->m_gameState.player(static_cast<PlayerId>(i));
            if (pptr == nullptr || pptr->units().empty()) { continue; }
            startPositions.push_back(pptr->units().front()->position());
        }
        this->m_goodyHuts.hutLocations.clear();
        aoc::sim::placeGoodyHuts(this->m_goodyHuts, this->m_hexGrid,
                                  startPositions, this->m_gameRng);
    }

    // Spawn city-states
    const int32_t cityStateCount = static_cast<int32_t>(config.playerCount) * 2;
    aoc::sim::spawnCityStates(this->m_gameState, this->m_hexGrid,
                               cityStateCount, this->m_gameRng);

    // Update fog of war for all players
    for (uint8_t i = 0; i < config.playerCount; ++i) {
        this->m_fogOfWar.updateVisibility(this->m_gameState, this->m_hexGrid, i);
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

    // Tear down the loading overlay now that everything's set up.
    this->m_loadingScreen.setProgress(1.0f);
    this->m_loadingScreen.setStatus("Done");
    this->m_loadingScreen.close(this->m_uiManager);

    LOG_INFO("Game started (map type=%d, size=%d, players=%u)",
             static_cast<int>(config.mapType), static_cast<int>(config.mapSize),
             static_cast<unsigned>(config.playerCount));
}

void Application::startSpectate(int32_t playerCount, int32_t maxTurns) {
    // Tear down any pre-game menus before starting. The menu-button path
    // destroys these itself; the CLI --spectate path goes through the
    // deferred startSpectate hook which did not, leaving the main menu
    // panel rendered on top of the game world.
    this->m_mainMenu.destroy(this->m_uiManager);
    this->m_settingsMenu.destroy(this->m_uiManager);

    // Clamp parameters to valid ranges.
    // GameSetupConfig::players array has 20 slots.
    if (playerCount < 2)  { playerCount = 2;  }
    if (playerCount > 20) { playerCount = 20; }
    if (maxTurns < 100)   { maxTurns = 100;   }
    if (maxTurns > 5000)  { maxTurns = 5000;  }

    // Build an all-AI GameSetupConfig and delegate to startGame().
    aoc::ui::GameSetupConfig config{};
    config.mapType    = aoc::map::MapType::Continents;
    config.mapSize    = aoc::map::MapSize::Standard;
    config.playerCount = static_cast<uint8_t>(playerCount);
    config.aiDifficulty = aoc::ui::AIDifficulty::Normal;
    config.sequentialTurnsInWar = false;

    for (int32_t i = 0; i < playerCount; ++i) {
        config.players[static_cast<std::size_t>(i)].isActive = true;
        // All slots are AI — no human player in spectator mode.
        config.players[static_cast<std::size_t>(i)].isHuman  = false;
        config.players[static_cast<std::size_t>(i)].civId    =
            static_cast<uint8_t>(i % static_cast<int32_t>(aoc::sim::CIV_COUNT));
    }

    // startGame() normally treats slot 0 as human and calls spawnStartingEntities().
    // In spectator mode every slot is AI, so startGame() must set up slot 0 as AI.
    // We temporarily mark slot 0 as human to let startGame() run through the normal
    // code path (which always spawns entities for slot 0); then immediately flip the
    // flag back to AI so no human input is expected.
    config.players[0].isHuman = true;
    this->startGame(config);
    aoc::game::Player* slot0 = this->m_gameState.player(0);
    if (slot0 != nullptr) {
        slot0->setHuman(false);
    }

    // Add an AI controller for player 0 (the slot startGame() treated as human).
    this->m_aiControllers.emplace(this->m_aiControllers.begin(),
                                   aoc::PlayerId{0},
                                   config.aiDifficulty);

    // Reveal all tiles immediately — spectator sees everything.
    this->spectatorRevealAll();

    // Initialize spectator state.
    this->m_spectatorMode          = true;
    this->m_spectatorMaxTurns      = maxTurns;
    this->m_spectatorPaused        = false;
    this->m_spectatorSpeed         = 1.0f;
    this->m_spectatorTurnAccumulator = 0.0f;
    this->m_spectatorFollowPlayer  = -1;
    this->m_spectatorFogEnabled    = false;

    // Hide the end-turn button — spectator does not need it.
    if (this->m_endTurnButton != aoc::ui::INVALID_WIDGET) {
        this->m_uiManager.setVisible(this->m_endTurnButton, false);
    }

    // Seek slider along the bottom of the screen.
    int32_t w, h;
    glfwGetFramebufferSize(this->m_window.handle(), &w, &h);
    this->buildSpectatorSeekControls(static_cast<float>(w), static_cast<float>(h));
    this->m_spectatorTargetTurn = -1;
    this->m_spectatorSnapshots.clear();

    // Center camera on the actual pixel-space center of the map.
    // The map uses offset coordinates (col, row) internally.
    // Tile (0,0) is at pixel ~(0,0). Tile (width-1, height-1) is at the bottom-right.
    // Compute pixel position of the last tile to find the map extent.
    const aoc::hex::OffsetCoord lastOffset{
        this->m_hexGrid.width() - 1,
        this->m_hexGrid.height() - 1};
    const aoc::hex::AxialCoord lastAxial = aoc::hex::offsetToAxial(lastOffset);
    float maxPx = 0.0f;
    float maxPy = 0.0f;
    aoc::hex::axialToPixel(lastAxial, this->m_gameRenderer.mapRenderer().hexSize(),
                            maxPx, maxPy);
    // Center is half of the max extent
    this->m_cameraController.setPosition(maxPx * 0.5f, maxPy * 0.5f);
    this->m_cameraController.setZoom(0.5f);

    LOG_INFO("Spectator: camera at (%.0f, %.0f) map extent (%.0f, %.0f)",
             maxPx * 0.5f, maxPy * 0.5f, maxPx, maxPy);
    LOG_INFO("Spectator mode started: %d AI players, max %d turns", playerCount, maxTurns);
}

void Application::spectatorRevealAll() {
    const int32_t tileCount = this->m_hexGrid.tileCount();
    const int32_t playerCount = this->m_gameState.playerCount();
    for (int32_t p = 0; p < playerCount; ++p) {
        for (int32_t t = 0; t < tileCount; ++t) {
            this->m_fogOfWar.setVisibility(static_cast<aoc::PlayerId>(p), t,
                                            aoc::map::TileVisibility::Visible);
        }
    }
}

void Application::spectatorAdvanceTurn() {
    // Build TurnContext for all AI players (no human player).
    aoc::sim::TurnContext turnCtx{};
    turnCtx.grid         = &this->m_hexGrid;
    turnCtx.fogOfWar     = &this->m_fogOfWar;
    turnCtx.economy      = &this->m_economy;
    turnCtx.diplomacy    = &this->m_diplomacy;
    turnCtx.barbarians   = &this->m_barbarianController;
    turnCtx.dealTracker  = &this->m_dealTracker;
    turnCtx.allianceTracker = &this->m_allianceTracker;
    turnCtx.rng          = &this->m_gameRng;
    turnCtx.gameState    = &this->m_gameState;
    turnCtx.humanPlayer  = aoc::INVALID_PLAYER;
    turnCtx.currentTurn  = static_cast<aoc::TurnNumber>(
        this->m_turnManager.currentTurn() + 1);

    for (aoc::sim::ai::AIController& ai : this->m_aiControllers) {
        turnCtx.aiControllers.push_back(&ai);
        turnCtx.allPlayers.push_back(ai.player());
    }

    // Submit all players so the TurnManager is ready.
    for (const aoc::sim::ai::AIController& ai : this->m_aiControllers) {
        this->m_turnManager.submitEndTurn(ai.player());
    }

    if (this->m_turnManager.allPlayersReady()) {
        this->m_turnManager.executeTurn(this->m_gameState);

        aoc::sim::processTurn(turnCtx);

        // Execute AI movement after processTurn (AI decisions ran inside it).
        for (const aoc::sim::ai::AIController& ai : this->m_aiControllers) {
            aoc::sim::executeMovement(this->m_gameState, ai.player(), this->m_hexGrid);
        }

        // Diplomacy decay, espionage, grievance tick, world congress all run
        // inside processTurn (TurnProcessor.cpp). Do NOT call them again here.

        // Goody hut exploration: any unit now standing on a hut tile claims it.
        // Snapshot positions first because claiming a FreeUnit hut reallocates
        // the player's units vector.
        if (!this->m_goodyHuts.hutLocations.empty()) {
            const int32_t playerCount = this->m_gameState.playerCount();
            for (int32_t p = 0; p < playerCount; ++p) {
                aoc::game::Player* gsp =
                    this->m_gameState.player(static_cast<aoc::PlayerId>(p));
                if (gsp == nullptr) { continue; }
                std::vector<aoc::hex::AxialCoord> positions;
                positions.reserve(gsp->units().size());
                for (const std::unique_ptr<aoc::game::Unit>& unitPtr : gsp->units()) {
                    positions.push_back(unitPtr->position());
                }
                for (const aoc::hex::AxialCoord& pos : positions) {
                    aoc::sim::checkAndClaimGoodyHut(this->m_goodyHuts,
                                                   this->m_gameState,
                                                   *gsp, pos, this->m_gameRng);
                }
            }
        }

        // Update fog of war: reveal all if fog is disabled, else update per-player.
        if (!this->m_spectatorFogEnabled) {
            this->spectatorRevealAll();
        } else {
            const int32_t playerCount = this->m_gameState.playerCount();
            for (int32_t p = 0; p < playerCount; ++p) {
                this->m_fogOfWar.updateVisibility(
                    this->m_gameState, this->m_hexGrid, static_cast<aoc::PlayerId>(p));
            }
        }

        // Check victory conditions: read cached result from processTurn.
        const aoc::sim::VictoryResult& vr = turnCtx.lastVictoryResult;
        if (vr.type != aoc::sim::VictoryType::None) {
            this->m_spectatorPaused = true;
            LOG_INFO("Spectator: Player %u wins by type %d at turn %u",
                     static_cast<unsigned>(vr.winner),
                     static_cast<int>(vr.type),
                     static_cast<unsigned>(this->m_turnManager.currentTurn()));
        }

        this->m_replayRecorder.recordFrame(this->m_gameState,
                                            this->m_turnManager.currentTurn());
        this->m_turnManager.beginNewTurn();
    }
}

void Application::spectatorUpdateFollowCamera() {
    if (this->m_spectatorFollowPlayer < 0) {
        return;
    }
    const aoc::game::Player* followed =
        this->m_gameState.player(static_cast<aoc::PlayerId>(this->m_spectatorFollowPlayer));
    if (followed == nullptr || followed->cityCount() == 0) {
        return;
    }
    // Pan to the first city (the capital).
    const aoc::hex::AxialCoord capitalLoc = followed->cities().front()->location();
    float cx = 0.0f;
    float cy = 0.0f;
    hex::axialToPixel(capitalLoc, this->m_gameRenderer.mapRenderer().hexSize(), cx, cy);
    this->m_cameraController.setPosition(cx, cy);
}

void Application::spectatorDrawHUD(void* cmdBufferPtr, uint32_t frameWidth, uint32_t frameHeight) {
    VkCommandBuffer cmdBuffer = static_cast<VkCommandBuffer>(cmdBufferPtr);
    const float screenW = static_cast<float>(frameWidth);
    const float screenH = static_cast<float>(frameHeight);

    this->m_renderer2d->begin();

    // Draw spectator status bar and player scoreboard.
    this->m_spectatorHUD.drawStatusBar(
        *this->m_renderer2d,
        static_cast<int32_t>(this->m_turnManager.currentTurn()),
        this->m_spectatorMaxTurns,
        this->m_spectatorSpeed,
        this->m_spectatorPaused,
        this->m_spectatorFollowPlayer,
        screenW, screenH);

    this->m_spectatorHUD.drawScoreboard(
        *this->m_renderer2d,
        this->m_gameState,
        screenW, screenH);

    this->m_renderer2d->end(cmdBuffer);
}

void Application::buildSpectatorSeekControls(float screenW, float screenH) {
    // Destroy any prior widgets from a previous spectate session.
    if (this->m_spectatorSeekPanelId != aoc::ui::INVALID_WIDGET) {
        this->m_uiManager.removeWidget(this->m_spectatorSeekPanelId);
        this->m_spectatorSeekPanelId  = aoc::ui::INVALID_WIDGET;
        this->m_spectatorSeekSliderId = aoc::ui::INVALID_WIDGET;
        this->m_spectatorSeekLabelId  = aoc::ui::INVALID_WIDGET;
    }

    constexpr float PANEL_W = 560.0f;
    constexpr float PANEL_H = 42.0f;
    const float panelX = (screenW - PANEL_W) * 0.5f;
    const float panelY = screenH - PANEL_H - 10.0f;

    this->m_spectatorSeekPanelId = this->m_uiManager.createPanel(
        {panelX, panelY, PANEL_W, PANEL_H},
        aoc::ui::PanelData{{0.05f, 0.05f, 0.10f, 0.85f}, 4.0f});

    this->m_spectatorSeekLabelId = this->m_uiManager.createLabel(
        this->m_spectatorSeekPanelId,
        {10.0f, 6.0f, 150.0f, 16.0f},
        aoc::ui::LabelData{"Seek: Turn 0", {0.9f, 0.9f, 0.9f, 1.0f}, 12.0f});

    aoc::ui::SliderData slider{};
    slider.minValue = 0.0f;
    slider.maxValue = static_cast<float>(this->m_spectatorMaxTurns);
    slider.value    = 0.0f;
    slider.step     = 1.0f;
    slider.onValueChanged = [this](float v) {
        this->m_spectatorTargetTurn = static_cast<int32_t>(v + 0.5f);
    };
    this->m_spectatorSeekSliderId = this->m_uiManager.createSlider(
        this->m_spectatorSeekPanelId,
        {170.0f, 12.0f, PANEL_W - 180.0f, 20.0f},
        std::move(slider));
}

void Application::spectatorMaybeSnapshot() {
    const int32_t turn = static_cast<int32_t>(this->m_turnManager.currentTurn());
    if (turn % SPECTATOR_SNAPSHOT_INTERVAL != 0) { return; }
    if (this->m_spectatorSnapshots.count(turn) != 0) { return; }

    // Write snapshot to /tmp so we can reuse the on-disk loadGame path
    // without refactoring Serializer for in-memory mode.  Store filepath
    // marker in the map so we can rediscover and delete later.
    const std::string path = "/tmp/aoc_spec_snap_" + std::to_string(turn) + ".sav";
    const aoc::ErrorCode result = aoc::save::saveGame(
        path, this->m_gameState, this->m_hexGrid, this->m_turnManager,
        this->m_economy, this->m_diplomacy, this->m_fogOfWar, this->m_gameRng);
    if (result != aoc::ErrorCode::Ok) {
        LOG_WARN("spectatorSnapshot turn %d: saveGame failed", turn);
        return;
    }
    // Store a sentinel (empty vector) — the file on disk is the real snapshot.
    this->m_spectatorSnapshots[turn] = std::vector<uint8_t>{};

    // Cap the ring: drop the oldest snapshots when the history grows beyond
    // SPECTATOR_SNAPSHOT_MAX so /tmp doesn't fill up.
    while (this->m_spectatorSnapshots.size() > SPECTATOR_SNAPSHOT_MAX) {
        auto oldest = this->m_spectatorSnapshots.begin();
        std::remove((std::string("/tmp/aoc_spec_snap_") +
                     std::to_string(oldest->first) + ".sav").c_str());
        this->m_spectatorSnapshots.erase(oldest);
    }
}

bool Application::spectatorRestoreSnapshot(int32_t turn) {
    // Find the newest snapshot at or before `turn`.
    auto it = this->m_spectatorSnapshots.upper_bound(turn);
    if (it == this->m_spectatorSnapshots.begin()) { return false; }
    --it;
    const int32_t snapTurn = it->first;

    // Pause the sim advance while we mutate GameState + derived containers
    // to avoid any mid-frame turn tick seeing half-loaded state.
    const bool wasPaused = this->m_spectatorPaused;
    this->m_spectatorPaused = true;

    const std::string path = "/tmp/aoc_spec_snap_" + std::to_string(snapTurn) + ".sav";
    const aoc::ErrorCode result = aoc::save::loadGame(
        path, this->m_gameState, this->m_hexGrid, this->m_turnManager,
        this->m_economy, this->m_diplomacy, this->m_fogOfWar, this->m_gameRng);
    if (result != aoc::ErrorCode::Ok) {
        LOG_WARN("spectatorRestoreSnapshot turn %d: loadGame failed", snapTurn);
        this->m_spectatorPaused = wasPaused;
        return false;
    }

    // Full post-load reinit: loadGame restores the GameState but Application
    // keeps several derived containers (AIControllers, BarbarianController,
    // GoodyHuts, TurnManager readiness) that the serializer leaves untouched.
    // Rebuild them from the freshly-loaded player roster so stale pointers
    // from the pre-restore session cannot produce the dist=999 supply loops
    // and segfault we hit on the first backward-seek attempt.
    this->m_aiControllers.clear();
    const int32_t pc = this->m_gameState.playerCount();
    for (int32_t p = 0; p < pc; ++p) {
        this->m_aiControllers.emplace_back(
            static_cast<aoc::PlayerId>(p), aoc::ui::AIDifficulty::Normal);
    }
    // In spectator mode no slot is human; controllers cover all players.
    // Reset turn-manager readiness so the next advance cycles cleanly.
    this->m_turnManager.setPlayerCount(0, static_cast<uint8_t>(pc));
    this->m_turnManager.beginNewTurn();

    // Barbarian + goody huts: clear and let the sim reseed / rediscover via
    // tile scan next turn.  Cheaper than trying to serialize their state.
    this->m_barbarianController = aoc::sim::BarbarianController{};
    this->m_goodyHuts.hutLocations.clear();

    // Fog: spectator mode reveals all.
    if (!this->m_spectatorFogEnabled) {
        this->spectatorRevealAll();
    } else {
        for (int32_t p = 0; p < pc; ++p) {
            this->m_fogOfWar.updateVisibility(
                this->m_gameState, this->m_hexGrid, static_cast<aoc::PlayerId>(p));
        }
    }

    LOG_INFO("Spectator: restored snapshot from turn %d (reinitialized %d AI controllers)",
             snapTurn, pc);
    this->m_spectatorPaused = wasPaused;
    return true;
}

void Application::run() {
    if (!this->m_initialized) {
        return;
    }

    // Deferred spectator start: trigger after the window and render pipeline
    // are fully active (first iteration of run loop).
    if (this->m_deferredSpectate) {
        this->m_deferredSpectate = false;
        this->startSpectate(this->m_deferredSpectatePlayers, this->m_deferredSpectateTurns);
    }

    std::chrono::steady_clock::time_point previousTime = std::chrono::steady_clock::now();
    float fpsAccum = 0.0f;
    int32_t fpsFrameCount = 0;

    while (!this->m_window.shouldClose()) {
        std::chrono::steady_clock::time_point currentTime = std::chrono::steady_clock::now();
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

        // Drain DBus messages and satisfy any screenshot request. Runs here
        // so all sd-bus calls stay on the render thread.
        this->m_dbusService.tick();
        {
            std::string shotPath = this->m_dbusService.takePendingScreenshotPath();
            if (!shotPath.empty()) {
                bool ok = false;
                std::string message;

                std::vector<uint8_t> pixels;
                uint32_t shotWidth = 0;
                uint32_t shotHeight = 0;
                VkFormat shotFormat = VK_FORMAT_UNDEFINED;
                if (this->m_renderPipeline && this->m_renderPipeline->readSwapchainPixels(
                        pixels, shotWidth, shotHeight, shotFormat)) {
                    const bool isBgra = (shotFormat == VK_FORMAT_B8G8R8A8_SRGB
                                        || shotFormat == VK_FORMAT_B8G8R8A8_UNORM);
                    if (writeScreenshotPng(shotPath, pixels, shotWidth, shotHeight, isBgra)) {
                        ok = true;
                        message = "screenshot written via swapchain readback";
                    } else {
                        message = "PNG encode failed (check parent directory + disk space)";
                    }
                } else {
                    message = "swapchain readback unavailable (no frame presented yet or TRANSFER_SRC not supported)";
                }

                this->m_dbusService.reportScreenshotResult(ok, std::move(message));
            }
        }

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
            this->m_uiManager.setRenderCommandBuffer(
                static_cast<void*>(frame.commandBuffer));
            this->m_uiManager.render(*this->m_renderer2d);
            this->m_uiManager.setRenderCommandBuffer(nullptr);
            this->m_widgetInspector.render(
                *this->m_renderer2d, this->m_uiManager,
                static_cast<float>(this->m_inputManager.mouseX()),
                static_cast<float>(this->m_inputManager.mouseY()));

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
        // F11 toggles the dev widget inspector overlay. Visible both
        // in-game and on menus so layout bugs surface anywhere.
        if (this->m_inputManager.isKeyPressed(GLFW_KEY_F11)) {
            this->m_widgetInspector.toggle();
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
                this->m_debugConsole.execute(this->m_gameState, this->m_hexGrid,
                                              this->m_fogOfWar, 0);
            }
        }

        // ================================================================
        // Spectator mode input and turn advancement
        // ================================================================
        if (this->m_spectatorMode && !this->m_debugConsole.isOpen()) {
            // Space: toggle pause/resume.
            if (this->m_inputManager.isKeyPressed(GLFW_KEY_SPACE)) {
                this->m_spectatorPaused = !this->m_spectatorPaused;
                LOG_INFO("Spectator: %s", this->m_spectatorPaused ? "paused" : "resumed");
            }

            // Equal/Plus: increase speed through preset steps.
            if (this->m_inputManager.isKeyPressed(GLFW_KEY_EQUAL)) {
                constexpr float SPEED_STEPS[] = {1.0f, 2.0f, 5.0f, 10.0f, 50.0f, 100.0f, 1000.0f};
                constexpr int32_t STEP_COUNT =
                    static_cast<int32_t>(sizeof(SPEED_STEPS) / sizeof(SPEED_STEPS[0]));
                for (int32_t s = 0; s < STEP_COUNT - 1; ++s) {
                    if (this->m_spectatorSpeed < SPEED_STEPS[s + 1] - 0.01f) {
                        this->m_spectatorSpeed = SPEED_STEPS[s + 1];
                        LOG_INFO("Spectator speed: %.0fx", static_cast<double>(this->m_spectatorSpeed));
                        break;
                    }
                }
            }

            // Minus: decrease speed through preset steps.
            if (this->m_inputManager.isKeyPressed(GLFW_KEY_MINUS)) {
                constexpr float SPEED_STEPS[] = {1.0f, 2.0f, 5.0f, 10.0f, 50.0f, 100.0f, 1000.0f};
                constexpr int32_t STEP_COUNT =
                    static_cast<int32_t>(sizeof(SPEED_STEPS) / sizeof(SPEED_STEPS[0]));
                for (int32_t s = STEP_COUNT - 1; s > 0; --s) {
                    if (this->m_spectatorSpeed > SPEED_STEPS[s - 1] + 0.01f) {
                        this->m_spectatorSpeed = SPEED_STEPS[s - 1];
                        LOG_INFO("Spectator speed: %.0fx", static_cast<double>(this->m_spectatorSpeed));
                        break;
                    }
                }
            }

            // Digit keys 1-9: follow player 0-8.
            for (int32_t dk = GLFW_KEY_1; dk <= GLFW_KEY_9; ++dk) {
                if (this->m_inputManager.isKeyPressed(dk)) {
                    const int32_t followIdx = dk - GLFW_KEY_1;
                    if (followIdx < this->m_gameState.playerCount()) {
                        this->m_spectatorFollowPlayer = followIdx;
                        LOG_INFO("Spectator: following player %d", followIdx);
                        this->spectatorUpdateFollowCamera();
                    }
                }
            }

            // Key 0: follow player 9.
            if (this->m_inputManager.isKeyPressed(GLFW_KEY_0)) {
                const int32_t followIdx = 9;
                if (followIdx < this->m_gameState.playerCount()) {
                    this->m_spectatorFollowPlayer = followIdx;
                    LOG_INFO("Spectator: following player 9");
                    this->spectatorUpdateFollowCamera();
                }
            }

            // F: switch to free camera.
            if (this->m_inputManager.isKeyPressed(GLFW_KEY_F)) {
                this->m_spectatorFollowPlayer = -1;
                LOG_INFO("Spectator: free camera");
            }

            // Tab: cycle to next player.
            if (this->m_inputManager.isKeyPressed(GLFW_KEY_TAB)) {
                const int32_t count = this->m_gameState.playerCount();
                if (count > 0) {
                    this->m_spectatorFollowPlayer =
                        (this->m_spectatorFollowPlayer + 2) % count;
                    LOG_INFO("Spectator: cycling to player %d", this->m_spectatorFollowPlayer);
                    this->spectatorUpdateFollowCamera();
                }
            }

            // WP-H takeover: T (or Ctrl+T) in spectator mode — assume control
            // of the currently-followed player. Sim switches to human-driven
            // for that slot; AI skips it; fog resolves from their POV.
            // Plain 'T' kept as alias for Ctrl+T per user request — clicking
            // a civ in the scoreboard sets m_spectatorFollowPlayer, then
            // pressing T overtakes that civ.
            if (this->m_inputManager.isKeyPressed(GLFW_KEY_T)) {
                if (this->m_spectatorFollowPlayer >= 0
                    && this->m_spectatorFollowPlayer
                       < this->m_gameState.playerCount()) {
                    const PlayerId tookOver =
                        static_cast<PlayerId>(this->m_spectatorFollowPlayer);
                    this->m_gameState.setHumanPlayerId(tookOver);
                    LOG_INFO("WP-H takeover: player %u is now human-controlled",
                             static_cast<unsigned>(tookOver));
                }
            }

            // G: toggle fog of war between omniscient and per-player-follow view.
            if (this->m_inputManager.isKeyPressed(GLFW_KEY_G)) {
                this->m_spectatorFogEnabled = !this->m_spectatorFogEnabled;
                if (!this->m_spectatorFogEnabled) {
                    this->spectatorRevealAll();
                }
                LOG_INFO("Spectator fog: %s",
                         this->m_spectatorFogEnabled ? "enabled (follow player)" : "disabled (reveal all)");
            }

            // Seek: if slider moved backward, restore the newest snapshot
            // at or before the target turn (spectatorRestoreSnapshot does a
            // full post-load reinit of AI controllers + barbarians + fog
            // to avoid stale-pointer crashes).  The main advance loop below
            // then fast-forwards any remaining delta.  If there's no earlier
            // snapshot (target below oldest ring entry), clamp to current.
            if (this->m_spectatorTargetTurn >= 0) {
                const int32_t curTurn = static_cast<int32_t>(this->m_turnManager.currentTurn());
                if (this->m_spectatorTargetTurn < curTurn) {
                    if (!this->spectatorRestoreSnapshot(this->m_spectatorTargetTurn)) {
                        this->m_spectatorTargetTurn = curTurn;
                    }
                }
            }

            // Snapshot ring tick — capture state every SNAPSHOT_INTERVAL
            // turns so backward seek has restore points.
            this->spectatorMaybeSnapshot();

            // Turn advancement: accumulate deltaTime and fire when threshold reached.
            // At speed 1.0 this fires once per second; at speed 1000.0 it fires
            // many turns per frame to keep the simulation from stalling.
            // When a seek target is set, run at burst speed until hit.
            if (!this->m_spectatorPaused) {
                this->m_spectatorTurnAccumulator += deltaTime * this->m_spectatorSpeed;

                // Cap the number of turns processed per frame to avoid hitching.
                constexpr int32_t MAX_TURNS_PER_FRAME = 50;
                int32_t turnsThisFrame = 0;
                const bool seeking = (this->m_spectatorTargetTurn >= 0
                    && static_cast<int32_t>(this->m_turnManager.currentTurn())
                           < this->m_spectatorTargetTurn);
                while ((seeking
                        || this->m_spectatorTurnAccumulator >= 1.0f)
                       && turnsThisFrame < MAX_TURNS_PER_FRAME
                       && !this->m_spectatorPaused) {
                    if (!seeking) { this->m_spectatorTurnAccumulator -= 1.0f; }
                    this->spectatorAdvanceTurn();
                    ++turnsThisFrame;

                    if (this->m_turnManager.currentTurn()
                            >= static_cast<aoc::TurnNumber>(this->m_spectatorMaxTurns)) {
                        this->m_spectatorPaused = true;
                        LOG_INFO("Spectator: reached maximum turn limit (%d)",
                                 this->m_spectatorMaxTurns);
                    }
                    if (seeking
                        && static_cast<int32_t>(this->m_turnManager.currentTurn())
                               >= this->m_spectatorTargetTurn) {
                        this->m_spectatorTargetTurn = -1;
                        break;
                    }
                }
                // Discard any leftover accumulation so we don't catch up with a burst
                // once unpaused after hitting the limit.
                if (this->m_spectatorPaused) {
                    this->m_spectatorTurnAccumulator = 0.0f;
                }
            }

            // Update camera follow target each frame so the camera smoothly tracks.
            this->spectatorUpdateFollowCamera();

            // Keep the seek slider's visible value + label in sync with the
            // simulation state whenever the user is NOT actively dragging
            // the slider (which would snap it back on every frame).
            if (this->m_spectatorSeekSliderId != aoc::ui::INVALID_WIDGET) {
                aoc::ui::Widget* sliderWidget = this->m_uiManager.getWidget(
                    this->m_spectatorSeekSliderId);
                if (sliderWidget != nullptr) {
                    if (auto* sd = std::get_if<aoc::ui::SliderData>(&sliderWidget->data)) {
                        if (!sd->dragging && this->m_spectatorTargetTurn < 0) {
                            sd->value = static_cast<float>(this->m_turnManager.currentTurn());
                        }
                    }
                }
                if (this->m_spectatorSeekLabelId != aoc::ui::INVALID_WIDGET) {
                    std::string txt = "Seek: Turn "
                        + std::to_string(static_cast<int32_t>(this->m_turnManager.currentTurn()));
                    this->m_uiManager.setLabelText(this->m_spectatorSeekLabelId, std::move(txt));
                }
            }
        }

        // Camera update is deferred until after UI input to prevent scroll conflicts
        // (see below after UI input handling)

        // -- Animated unit movement: advance animProgress each frame --
        {
            constexpr float ANIM_DURATION = 0.2f;
            for (const std::unique_ptr<aoc::game::Player>& animPlayer : this->m_gameState.players()) {
                for (const std::unique_ptr<aoc::game::Unit>& animUnit : animPlayer->units()) {
                    if (animUnit->isAnimating) {
                        animUnit->animProgress += deltaTime / ANIM_DURATION;
                        if (animUnit->animProgress >= 1.0f) {
                            animUnit->animProgress = 1.0f;
                            animUnit->isAnimating = false;
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

        // -- Cycle to next unit needing orders (Tab key, human mode only) --
        if (!this->m_spectatorMode
            && this->m_inputManager.isActionPressed(InputAction::CycleNextUnit) && !this->anyScreenOpen()) {
            aoc::game::Player* cyclePlayer = this->m_gameState.player(0);
            if (cyclePlayer != nullptr) {
                aoc::game::Unit* nextUnit = nullptr;
                for (const std::unique_ptr<aoc::game::Unit>& cu : cyclePlayer->units()) {
                    if (cu->movementRemaining() <= 0) {
                        continue;
                    }
                    if (cu->state() == aoc::sim::UnitState::Sleeping ||
                        cu->state() == aoc::sim::UnitState::Fortified) {
                        continue;
                    }
                    if (!cu->pendingPath().empty()) {
                        continue;
                    }
                    nextUnit = cu.get();
                    break;
                }
                if (nextUnit != nullptr) {
                    this->m_selectedUnit = nextUnit;
                    this->m_selectedCity = nullptr;
                    float ucx = 0.0f, ucy = 0.0f;
                    hex::axialToPixel(nextUnit->position(),
                                      this->m_gameRenderer.mapRenderer().hexSize(), ucx, ucy);
                    this->m_cameraController.setPosition(ucx, ucy);
                }
            }
        }

        // -- Escape: close any open screen, else open Settings as pause menu.
        // Was: `break` (quit game). User feedback: ESC should not kill game —
        // it should pause + offer save/load/main-menu options. Settings menu
        // doubles as pause menu for now (Save/Load via F5/F9).
        if (this->m_inputManager.isActionPressed(InputAction::Cancel)) {
            LOG_INFO("ESC pressed in-game (pauseBuilt=%d, anyScreen=%d)",
                     this->m_pauseMenu.isBuilt() ? 1 : 0,
                     this->anyScreenOpen() ? 1 : 0);
            if (this->m_pauseMenu.isBuilt()) {
                this->m_pauseMenu.destroy(this->m_uiManager);
            } else if (this->anyScreenOpen()) {
                this->closeAllScreens();
            } else {
                const std::pair<uint32_t, uint32_t> sz = this->m_window.framebufferSize();
                this->m_pauseMenu.build(
                    this->m_uiManager,
                    static_cast<float>(sz.first),
                    static_cast<float>(sz.second),
                    [this]() { this->m_pauseMenu.destroy(this->m_uiManager); },
                    [this](int slot) {
                        const std::string fname =
                            "save_slot_" + std::to_string(slot + 1) + ".aoc";
                        ErrorCode r = aoc::save::saveGame(
                            fname.c_str(), this->m_gameState, this->m_hexGrid,
                            this->m_turnManager, this->m_economy, this->m_diplomacy,
                            this->m_fogOfWar, this->m_gameRng);
                        if (r != ErrorCode::Ok) {
                            LOG_ERROR("PauseMenu save slot %d failed: %.*s",
                                slot + 1,
                                static_cast<int>(describeError(r).size()),
                                describeError(r).data());
                            this->m_notificationManager.push(
                                "Save failed", 3.0f, 0.9f, 0.3f, 0.3f);
                        } else {
                            LOG_INFO("PauseMenu: saved to %s", fname.c_str());
                            this->m_notificationManager.push(
                                ("Saved to " + fname).c_str(),
                                3.0f, 0.4f, 0.9f, 0.4f);
                        }
                    },
                    [this](int slot) {
                        const std::string fname =
                            "save_slot_" + std::to_string(slot + 1) + ".aoc";
                        ErrorCode r = aoc::save::loadGame(
                            fname.c_str(), this->m_gameState, this->m_hexGrid,
                            this->m_turnManager, this->m_economy, this->m_diplomacy,
                            this->m_fogOfWar, this->m_gameRng);
                        if (r != ErrorCode::Ok) {
                            LOG_ERROR("PauseMenu load slot %d failed: %.*s",
                                slot + 1,
                                static_cast<int>(describeError(r).size()),
                                describeError(r).data());
                            this->m_notificationManager.push(
                                "Load failed (no save in slot?)",
                                3.0f, 0.9f, 0.3f, 0.3f);
                        } else {
                            this->m_economy.initialize();
                            this->m_fogOfWar.initialize(
                                this->m_hexGrid.tileCount(), MAX_PLAYERS);
                            this->m_fogOfWar.updateVisibility(
                                this->m_gameState, this->m_hexGrid, 0);
                            LOG_INFO("PauseMenu: loaded from %s", fname.c_str());
                            this->m_pauseMenu.destroy(this->m_uiManager);
                        }
                    },
                    [this]() {
                        this->m_pauseMenu.destroy(this->m_uiManager);
                        this->returnToMainMenu();
                    },
                    [this]() {
                        this->m_pauseMenu.destroy(this->m_uiManager);
                        glfwSetWindowShouldClose(this->m_window.handle(), GLFW_TRUE);
                    });
            }
        }

        // -- Toggle tile yield display (Y key) --
        if (!this->m_spectatorMode
            && this->m_inputManager.isKeyPressed(GLFW_KEY_Y) && !this->m_debugConsole.isOpen()) {
            this->m_settingsMenu.settings().showTileYields =
                !this->m_settingsMenu.settings().showTileYields;
            this->m_gameRenderer.showTileYields =
                this->m_settingsMenu.settings().showTileYields;
            this->m_notificationManager.push(
                this->m_gameRenderer.showTileYields
                    ? "Tile yields: ON" : "Tile yields: OFF",
                2.0f, 0.8f, 0.8f, 0.8f);
        }

        // -- Screen toggle keys (human mode only) --
        if (!this->m_spectatorMode
            && this->m_inputManager.isActionPressed(InputAction::OpenTechTree)) {
            this->m_techScreen.setContext(&this->m_gameState, 0);
            this->m_techScreen.setGrid(&this->m_hexGrid);
            this->m_techScreen.toggle(this->m_uiManager);
        }
        if (!this->m_spectatorMode
            && this->m_inputManager.isActionPressed(InputAction::OpenEconomy)) {
            this->m_economyScreen.setContext(&this->m_gameState, &this->m_hexGrid, 0, &this->m_economy.market());
            this->m_economyScreen.toggle(this->m_uiManager);
        }
        if (!this->m_spectatorMode
            && this->m_inputManager.isActionPressed(InputAction::OpenGovernment)) {
            this->m_governmentScreen.setContext(&this->m_gameState, 0);
            this->m_governmentScreen.toggle(this->m_uiManager);
        }
        if (!this->m_spectatorMode
            && this->m_inputManager.isActionPressed(InputAction::OpenReligion)) {
            this->m_religionScreen.setContext(&this->m_gameState, &this->m_hexGrid, 0);
            this->m_religionScreen.toggle(this->m_uiManager);
        }
        if (!this->m_spectatorMode
            && this->m_inputManager.isActionPressed(InputAction::OpenProductionPicker)) {
            // Only open if an own city is selected
            if (this->m_selectedCity != nullptr && this->m_selectedCity->owner() == 0) {
                this->m_productionScreen.setContext(
                    &this->m_gameState, &this->m_hexGrid, this->m_selectedCity->location(), 0);
                this->m_productionScreen.toggle(this->m_uiManager);
            }
        }

        // -- Unit upgrade (U key, human mode only) --
        if (!this->m_spectatorMode
            && this->m_inputManager.isActionPressed(InputAction::UpgradeUnit)) {
            if (this->m_selectedUnit != nullptr) {
                const std::vector<aoc::sim::UnitUpgradeDef> upgrades =
                    aoc::sim::getAvailableUpgrades(this->m_selectedUnit->typeId());
                if (!upgrades.empty()) {
                    // Try the first available upgrade
                    const aoc::sim::UnitUpgradeDef& upg = upgrades[0];
                    bool success = aoc::sim::upgradeUnit(
                        this->m_gameState, *this->m_selectedUnit, upg.to,
                        this->m_selectedUnit->owner());
                    if (success) {
                        this->m_eventLog.addEvent("Unit upgraded!");
                    }
                }
            }
        }

        // -- Help overlay (F1, human mode only) --
        if (!this->m_spectatorMode
            && this->m_inputManager.isActionPressed(InputAction::ShowHelp)) {
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

        // -- Quick save/load (human mode only) --
        if (!this->m_spectatorMode
            && this->m_inputManager.isActionPressed(InputAction::QuickSave)) {
            ErrorCode saveResult = aoc::save::saveGame(
                "quicksave.aoc", this->m_gameState, this->m_hexGrid,
                this->m_turnManager, this->m_economy, this->m_diplomacy,
                this->m_fogOfWar, this->m_gameRng);
            if (saveResult != ErrorCode::Ok) {
                LOG_ERROR("Quick save failed: %.*s",
                          static_cast<int>(describeError(saveResult).size()),
                          describeError(saveResult).data());
            }
        }
        if (!this->m_spectatorMode
            && this->m_inputManager.isActionPressed(InputAction::QuickLoad)) {
            ErrorCode loadResult = aoc::save::loadGame(
                "quicksave.aoc", this->m_gameState, this->m_hexGrid,
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
                this->m_fogOfWar.updateVisibility(this->m_gameState, this->m_hexGrid, 0);
                for (const aoc::sim::ai::AIController& ai : this->m_aiControllers) {
                    this->m_fogOfWar.updateVisibility(this->m_gameState, this->m_hexGrid, ai.player());
                }
            }
        }

        // -- UI input (consumes clicks on widgets) --
        {
            const bool leftPressed   = this->m_inputManager.isMouseButtonPressed(GLFW_MOUSE_BUTTON_LEFT);
            const bool leftReleased  = this->m_inputManager.isMouseButtonReleased(GLFW_MOUSE_BUTTON_LEFT);
            const bool rightPressed  = this->m_inputManager.isMouseButtonPressed(GLFW_MOUSE_BUTTON_RIGHT);
            const bool rightReleased = this->m_inputManager.isMouseButtonReleased(GLFW_MOUSE_BUTTON_RIGHT);
            const float scrollDelta = static_cast<float>(this->m_inputManager.scrollDelta());
            this->m_uiConsumedInput = this->m_uiManager.handleInput(
                static_cast<float>(this->m_inputManager.mouseX()),
                static_cast<float>(this->m_inputManager.mouseY()),
                leftPressed, leftReleased, scrollDelta,
                rightPressed, rightReleased);
        }

        // -- Camera update (after UI so scroll doesn't zoom when scrolling menus) --
        if (this->m_uiConsumedInput || this->m_debugConsole.isOpen()) {
            this->m_inputManager.consumeScroll();
        }
        // Suppress edge-scroll while the mouse sits over a UI widget,
        // over the minimap, or while a modal is open. Without this the
        // map slides whenever the cursor brushes the HUD bar or hovers
        // a button — caller wants edge-scroll only over the actual map.
        const aoc::render::Minimap::Rect mmRect =
            aoc::render::Minimap::computeRect(this->m_hexGrid, fbHeight);
        const float mouseXf = static_cast<float>(this->m_inputManager.mouseX());
        const float mouseYf = static_cast<float>(this->m_inputManager.mouseY());
        const bool overMinimap = this->m_gameRenderer.minimap().containsPoint(
            mouseXf, mouseYf, mmRect.x, mmRect.y, mmRect.w, mmRect.h);
        const bool overWidget = this->m_uiManager.hoveredWidget() != aoc::ui::INVALID_WIDGET;
        const bool suppressEdgeScroll =
            overWidget || overMinimap || this->anyScreenOpen()
            || this->m_uiConsumedInput || this->m_debugConsole.isOpen();
        this->m_cameraController.update(this->m_inputManager, deltaTime,
                                         fbWidth, fbHeight, suppressEdgeScroll);

        // -- Minimap click detection --
        // Dimensions reused from the suppress-edge-scroll calculation
        // above; the click rect is the same rect sourced from the
        // shared helper.

        if (this->m_inputManager.isMouseButtonPressed(GLFW_MOUSE_BUTTON_LEFT)
            && !this->m_uiConsumedInput) {
            if (overMinimap) {
                float worldX = 0.0f;
                float worldY = 0.0f;
                this->m_gameRenderer.minimap().screenToWorld(
                    mouseXf, mouseYf, mmRect.x, mmRect.y, mmRect.w, mmRect.h,
                    this->m_hexGrid,
                    this->m_gameRenderer.mapRenderer().hexSize(),
                    worldX, worldY);
                this->m_cameraController.setPosition(worldX, worldY);
                this->m_uiConsumedInput = true;
            }
        }

        // -- Game input (only in human mode, only if UI didn't consume it and no screen is open) --
        if (!this->m_spectatorMode && !this->m_uiConsumedInput && !this->anyScreenOpen()) {
            this->handleSelect();
            this->handleContextAction();
            this->handleUndoAction();
        }
        // When only the city detail panel is open (right-side, non-blocking),
        // allow map interactions on the MAP area (left of the city panel).
        // Don't check m_uiConsumedInput — the HUD widgets shouldn't block tile clicks.
        if (!this->m_spectatorMode
            && this->onlyCityDetailScreenOpen()
            && this->m_gameState.player(0) != nullptr && this->m_gameState.player(0)->cityAt(this->m_cityDetailScreen.cityLocation()) != nullptr
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
                    aoc::game::Player* clickPlayer = this->m_gameState.player(0);
                    if (clickPlayer != nullptr) {
                        aoc::game::Unit* clickedUnit = clickPlayer->unitAt(clickedTile);
                        if (clickedUnit != nullptr) {
                            // Clicked on own unit: close city panel, select unit
                            this->m_cityDetailScreen.close(this->m_uiManager);
                            this->m_selectedUnit = clickedUnit;
                            this->m_selectedCity = nullptr;
                            clickedOtherEntity = true;
                        }
                    }
                    if (!clickedOtherEntity && clickPlayer != nullptr) {
                        aoc::game::City* clickedCity = clickPlayer->cityAt(clickedTile);
                        if (clickedCity != nullptr
                            && clickedCity->location() != this->m_cityDetailScreen.cityLocation()) {
                            // Clicked on different own city: switch to it
                            this->m_cityDetailScreen.close(this->m_uiManager);
                            this->m_selectedCity = clickedCity;
                            this->m_selectedUnit = nullptr;
                            this->m_cityDetailScreen.setContext(
                                &this->m_gameState, &this->m_hexGrid,
                                clickedCity->location(), 0,
                                &this->m_economy);
                            this->m_cityDetailScreen.open(this->m_uiManager);
                            clickedOtherEntity = true;
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
        if (!this->m_spectatorMode
            && !this->anyScreenOpen() && this->m_inputManager.isActionPressed(InputAction::EndTurn)) {
            this->handleEndTurn();
        }

        // Refresh any open screens
        this->m_productionScreen.refresh(this->m_uiManager);
        this->m_techScreen.refresh(this->m_uiManager);
        this->m_governmentScreen.refresh(this->m_uiManager);
        this->m_economyScreen.refresh(this->m_uiManager);
        this->m_cityDetailScreen.refresh(this->m_uiManager);
        this->m_tradeScreen.refresh(this->m_uiManager);
        this->m_tradeRouteSetupScreen.refresh(this->m_uiManager);

        // "Waiting for you" banner: show when human is last player still acting
        if (this->m_lastPlayerBanner != aoc::ui::INVALID_WIDGET && !this->m_spectatorMode) {
            const bool isLast = this->m_turnManager.isLastPlayer(0);
            this->m_uiManager.setVisible(this->m_lastPlayerBanner, isLast);
        }
        this->m_diplomacyScreen.refresh(this->m_uiManager);
        this->m_religionScreen.refresh(this->m_uiManager);
        this->m_scoreScreen.refresh(this->m_uiManager);

        // Tooltip dispatch. Three cases:
        //   (a) hovered widget has its own tooltip text → show that
        //       (widgets opt in via `setWidgetTooltip`);
        //   (b) mouse is over the map with no blocking screen → show
        //       the tile/unit/city tooltip via `tooltipManager.update`;
        //   (c) everything else → hide.
        {
            const aoc::ui::WidgetId hovered = this->m_uiManager.hoveredWidget();
            const bool mapClickable = (!this->anyScreenOpen()
                                       || this->onlyCityDetailScreenOpen())
                                      && !this->m_uiConsumedInput;

            if (hovered != aoc::ui::INVALID_WIDGET) {
                std::string_view widgetTip = this->m_uiManager.widgetTooltip(hovered);
                if (!widgetTip.empty()) {
                    this->m_gameRenderer.tooltipManager().showText(
                        std::string(widgetTip),
                        static_cast<float>(this->m_inputManager.mouseX()),
                        static_cast<float>(this->m_inputManager.mouseY()),
                        fbWidth, fbHeight);
                } else {
                    this->m_gameRenderer.tooltipManager().hide();
                }
            } else if (mapClickable) {
                this->m_gameRenderer.tooltipManager().update(
                    static_cast<float>(this->m_inputManager.mouseX()),
                    static_cast<float>(this->m_inputManager.mouseY()),
                    this->m_gameState, this->m_hexGrid,
                    this->m_cameraController, this->m_fogOfWar,
                    PlayerId{0}, fbWidth, fbHeight,
                    NULL_ENTITY);
            } else {
                this->m_gameRenderer.tooltipManager().hide();
            }
        }

        // Sync selection to renderer and update HUD text
        // Selection highlight deferred: renderer needs Unit* or position-based highlight
        this->updateHUD();

        // Drive UI animations (alpha tweens, hover scale, tab-underline
        // slide, button hold-to-repeat, flash decay). Passes deltaTime
        // accumulated since the last frame.
        this->m_uiManager.tickAnimations(deltaTime);

        // Cursor shape from hovered widget's `hoverCursor` hint.
        // Enum mapping: 0=default, 1=hand, 2=ibeam, 3=crosshair.
        {
            const aoc::ui::WidgetId h = this->m_uiManager.hoveredWidget();
            int32_t want = 0;
            if (h != aoc::ui::INVALID_WIDGET) {
                const aoc::ui::Widget* hw = this->m_uiManager.getWidget(h);
                if (hw != nullptr) { want = hw->hoverCursor; }
            }
            if (want != this->m_cursors.lastApplied) {
                void* picked = this->m_cursors.arrow;
                if (want == 1)      { picked = this->m_cursors.hand; }
                else if (want == 2) { picked = this->m_cursors.ibeam; }
                else if (want == 3) { picked = this->m_cursors.crossHair; }
                glfwSetCursor(this->m_window.handle(), static_cast<GLFWcursor*>(picked));
                this->m_cursors.lastApplied = want;
            }
        }

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

        // Push current selection (unit > city) into the renderer so it paints
        // a highlight ring around the controlled entity.
        if (this->m_selectedUnit != nullptr) {
            this->m_gameRenderer.selectionHighlight = this->m_selectedUnit->position();
        } else if (this->m_selectedCity != nullptr) {
            this->m_gameRenderer.selectionHighlight = this->m_selectedCity->location();
        } else {
            this->m_gameRenderer.selectionHighlight =
                aoc::render::GameRenderer::INVALID_SELECTION;
        }
        // Worker-placement overlay: only when a city is selected by the
        // human player, panel is closed, and we're not in spectator mode.
        // Mirrors Civ-6's "click city → tiles glow, click tile → toggle
        // worker" interaction.
        const bool showOverlay =
            this->m_selectedCity != nullptr
            && this->m_selectedCity->owner() == 0
            && !this->m_cityDetailScreen.isOpen()
            && !this->m_spectatorMode;
        this->m_gameRenderer.workerOverlayCity =
            showOverlay ? this->m_selectedCity : nullptr;

        this->m_gameRenderer.m_minimapSuppressed = this->anyScreenOpen();
        this->m_gameRenderer.render(
            *this->m_renderer2d,
            frame.commandBuffer,
            frame.frameIndex,
            this->m_cameraController,
            this->m_hexGrid,
            this->m_gameState,
            this->m_fogOfWar,
            PlayerId{0},
            this->m_uiManager,
            frame.extent.width, frame.extent.height,
            &this->m_eventLog,
            &this->m_notificationManager,
            &this->m_tutorialManager);

        // Dev-only widget inspector overlay — toggled via F11.
        // Drawn last so it sits on top of HUD + screens.
        if (this->m_widgetInspector.isEnabled()) {
            this->m_renderer2d->resetCamera();
            this->m_renderer2d->setZoom(1.0f);
            this->m_widgetInspector.render(
                *this->m_renderer2d, this->m_uiManager,
                static_cast<float>(this->m_inputManager.mouseX()),
                static_cast<float>(this->m_inputManager.mouseY()));
        }

        // Spectator HUD overlay (own begin/end batch, screen-space)
        if (false && this->m_spectatorMode) { // DEBUG: disabled HUD
            // Reset camera to screen-space so HUD coords are in pixels.
            this->m_renderer2d->resetCamera();
            this->m_renderer2d->setZoom(1.0f);
            this->spectatorDrawHUD(static_cast<void*>(frame.commandBuffer),
                                    frame.extent.width, frame.extent.height);
        }

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
            "quicksave.aoc", this->m_gameState, this->m_hexGrid,
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
    this->m_actionPanelUnit = nullptr;
    if (this->m_helpOverlay != aoc::ui::INVALID_WIDGET) {
        this->m_uiManager.removeWidget(this->m_helpOverlay);
        this->m_helpOverlay = aoc::ui::INVALID_WIDGET;
    }

    // Reset game state
    this->m_hexGrid = aoc::map::HexGrid{};
    this->m_selectedUnit = nullptr;
    this->m_selectedCity = nullptr;
    this->m_gameOver = false;
    this->m_aiControllers.clear();

    // Reset spectator state
    this->m_spectatorMode            = false;
    this->m_spectatorPaused          = false;
    this->m_spectatorSpeed           = 1.0f;
    this->m_spectatorTurnAccumulator = 0.0f;
    this->m_spectatorFollowPlayer    = -1;
    this->m_spectatorFogEnabled      = false;

    // Switch to main menu
    this->m_appState = AppState::MainMenu;
    const std::pair<uint32_t, uint32_t> menuFbSize = this->m_window.framebufferSize();
    float screenW = static_cast<float>(menuFbSize.first);
    float screenH = static_cast<float>(menuFbSize.second);

    this->buildMainMenu(screenW, screenH);

    LOG_INFO("Returned to main menu");
}

void Application::buildMainMenu(float screenW, float screenH) {
    // Tear down any previous instance so resize-triggered rebuilds
    // don't leave a ghost menu behind the new one. Cheap no-op if not
    // built. Same goes for the settings overlay that can sit on top.
    if (this->m_mainMenu.isBuilt()) {
        this->m_mainMenu.destroy(this->m_uiManager);
    }
    if (this->m_settingsMenu.isBuilt()) {
        this->m_settingsMenu.destroy(this->m_uiManager);
    }
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
        },
        [this, screenW, screenH]() {
            // Spectate: reuse the GameSetup screen so the user can configure
            // map type/size, placement, player count, civs, difficulty — same
            // as a regular game — then start in spectator mode (all slots AI).
            this->m_mainMenu.destroy(this->m_uiManager);
            this->m_settingsMenu.destroy(this->m_uiManager);
            this->m_gameSetupScreen.build(
                this->m_uiManager, screenW, screenH,
                [this](const aoc::ui::GameSetupConfig& config) {
                    this->m_gameSetupScreen.destroy(this->m_uiManager);
                    // Force all slots to AI — spectator has no human.
                    aoc::ui::GameSetupConfig specConfig = config;
                    for (size_t i = 0; i < specConfig.players.size(); ++i) {
                        if (specConfig.players[i].isActive) {
                            specConfig.players[i].isHuman = false;
                        }
                    }
                    // Slot 0 temporarily marked human so startGame() runs its
                    // spawn path for it; we un-mark immediately and attach an
                    // AI controller for player 0 below.
                    specConfig.players[0].isHuman = true;
                    this->startGame(specConfig);
                    aoc::game::Player* slot0 = this->m_gameState.player(0);
                    if (slot0 != nullptr) { slot0->setHuman(false); }
                    this->m_aiControllers.emplace(this->m_aiControllers.begin(),
                                                   aoc::PlayerId{0},
                                                   specConfig.aiDifficulty);
                    this->spectatorRevealAll();
                    this->m_spectatorMode            = true;
                    this->m_spectatorMaxTurns        = 500;
                    this->m_spectatorPaused          = false;
                    this->m_spectatorSpeed           = 1.0f;
                    this->m_spectatorTurnAccumulator = 0.0f;
                    this->m_spectatorFollowPlayer    = -1;
                    this->m_spectatorFogEnabled      = false;
                    if (this->m_endTurnButton != aoc::ui::INVALID_WIDGET) {
                        this->m_uiManager.setVisible(this->m_endTurnButton, false);
                    }
                    this->m_spectatorTargetTurn = -1;
                    this->m_spectatorSnapshots.clear();
                    int32_t fbw = 0, fbh = 0;
                    glfwGetFramebufferSize(this->m_window.handle(), &fbw, &fbh);
                    this->buildSpectatorSeekControls(
                        static_cast<float>(fbw), static_cast<float>(fbh));
                },
                [this, screenW, screenH]() {
                    this->m_gameSetupScreen.destroy(this->m_uiManager);
                    this->buildMainMenu(screenW, screenH);
                });
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

    // Tear down cached GLFW cursors. Safe to pass nullptr to destroy.
    glfwDestroyCursor(static_cast<GLFWcursor*>(this->m_cursors.arrow));
    glfwDestroyCursor(static_cast<GLFWcursor*>(this->m_cursors.hand));
    glfwDestroyCursor(static_cast<GLFWcursor*>(this->m_cursors.ibeam));
    glfwDestroyCursor(static_cast<GLFWcursor*>(this->m_cursors.crossHair));
    this->m_cursors.arrow = nullptr;
    this->m_cursors.hand  = nullptr;
    this->m_cursors.ibeam = nullptr;
    this->m_cursors.crossHair = nullptr;

    this->m_dbusService.stop();

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

    // Refresh the global Theme. DPI can change too (monitor swap), so
    // re-query GLFW rather than assume a one-time startup value.
    {
        aoc::ui::Theme& t = aoc::ui::theme();
        t.viewportW = static_cast<float>(width);
        t.viewportH = static_cast<float>(height);
        float xscale = 1.0f;
        float yscale = 1.0f;
        glfwGetWindowContentScale(this->m_window.handle(), &xscale, &yscale);
        // Use the larger of the two so UI stays legible on non-square
        // DPI (rare but seen on some multi-monitor setups).
        t.dpiScale = std::max(xscale, yscale);
        if (t.dpiScale <= 0.0f) { t.dpiScale = 1.0f; }
    }

    // Broadcast resize to every registered screen. Each screen stores
    // its new dimensions and, if open, tears down + rebuilds so absolute
    // pixel layouts refresh. Future screens added to the registry pick
    // this up automatically.
    const float newW = static_cast<float>(width);
    const float newH = static_cast<float>(height);
    this->m_screenRegistry.onResize(this->m_uiManager, newW, newH);

    // Main-menu-state screens aren't in the registry (they predate
    // IScreen) so rebuild them by hand. Cheap: open/close pattern.
    if (this->m_appState == AppState::MainMenu) {
        if (this->m_mainMenu.isBuilt()) {
            this->buildMainMenu(newW, newH);
        }
        if (this->m_gameSetupScreen.isBuilt()) {
            this->m_gameSetupScreen.destroy(this->m_uiManager);
            // Re-open via the setup path so the callbacks stay wired.
            this->m_gameSetupScreen.build(
                this->m_uiManager, newW, newH,
                [this](const aoc::ui::GameSetupConfig& config) {
                    this->m_gameSetupScreen.destroy(this->m_uiManager);
                    this->startGame(config);
                },
                [this, newW, newH]() {
                    this->m_gameSetupScreen.destroy(this->m_uiManager);
                    this->buildMainMenu(newW, newH);
                });
        }
    }

    // Rebuild the unit action panel so the bottom-right anchored widget
    // picks up the new window corner. Drop the menu dropdown — it uses
    // absolute positioning anchored on a one-off click.
    if (this->m_appState == AppState::InGame) {
        this->rebuildUnitActionPanel();

        // Top bar is a root widget anchored to TopLeft; the layout pass
        // doesn't auto-stretch root widths to the screen, so push the
        // new width into its requestedBounds. Children flex-spacer then
        // re-distributes leftover horizontal space, keeping the right-
        // hand button cluster against the new window edge.
        if (this->m_topBar != aoc::ui::INVALID_WIDGET) {
            aoc::ui::Widget* bar = this->m_uiManager.getWidget(this->m_topBar);
            if (bar != nullptr) {
                bar->requestedBounds.w = newW;
            }
        }

        if (this->m_menuDropdown != aoc::ui::INVALID_WIDGET) {
            this->m_uiManager.removeWidget(this->m_menuDropdown);
            this->m_menuDropdown = aoc::ui::INVALID_WIDGET;
        }

        // Help overlay + confirm dialog are full-screen panels created
        // with a snapshot of the viewport at open time. Tear them down
        // so they re-open at current dimensions on the next trigger.
        if (this->m_helpOverlay != aoc::ui::INVALID_WIDGET) {
            this->m_uiManager.removeWidget(this->m_helpOverlay);
            this->m_helpOverlay = aoc::ui::INVALID_WIDGET;
        }
        if (this->m_confirmDialog != aoc::ui::INVALID_WIDGET) {
            this->m_uiManager.removeWidget(this->m_confirmDialog);
            this->m_confirmDialog = aoc::ui::INVALID_WIDGET;
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
        this->m_selectedUnit = nullptr;
        this->m_selectedCity = nullptr;
        return;
    }

    aoc::game::Player* humanGs = this->m_gameState.humanPlayer();

    // Check if one of OUR units is on this tile
    if (humanGs != nullptr) {
        aoc::game::Unit* selectedUnit = humanGs->unitAt(clickedTile);
        if (selectedUnit != nullptr) {
            this->m_selectedUnit = selectedUnit;
            this->m_selectedCity = nullptr;
            return;
        }
    }

    // Check if one of OUR cities is on this tile
    if (humanGs != nullptr) {
        aoc::game::City* selectedCity = humanGs->cityAt(clickedTile);
        if (selectedCity != nullptr) {
            // First click: select + show worker overlay. Second click on
            // the same already-selected city: open the detail panel.
            // Mirrors Civ-6's click-twice-to-manage flow without losing
            // the on-map worker UI.
            if (this->m_selectedCity == selectedCity) {
                this->m_cityDetailScreen.setContext(
                    &this->m_gameState, &this->m_hexGrid, clickedTile, 0);
                if (!this->m_cityDetailScreen.isOpen()) {
                    this->m_cityDetailScreen.open(this->m_uiManager);
                }
                return;
            }
            this->m_selectedCity = selectedCity;
            this->m_selectedUnit = nullptr;
            return;
        }
    }

    // Click on a workable tile of the currently selected city → toggle
    // worker assignment (free if currently worked, assign if free).
    // Population caps the headcount: each citizen besides the always-
    // worked centre tile consumes one slot. Without this guard the
    // user could assign workers indefinitely.
    if (this->m_selectedCity != nullptr
        && this->m_selectedCity->owner() == 0) {
        const aoc::hex::AxialCoord ctr = this->m_selectedCity->location();
        if (aoc::hex::distance(ctr, clickedTile) <= 3) {
            const int32_t idx = this->m_hexGrid.toIndex(clickedTile);
            if (this->m_hexGrid.movementCost(idx) != 0
                && this->m_hexGrid.owner(idx) == 0) {
                aoc::game::City* selCity = this->m_selectedCity;
                if (selCity->isTileWorked(clickedTile)) {
                    // Free a worker — always allowed.
                    selCity->toggleWorker(clickedTile);
                } else {
                    // Assigning a worker: must have a free citizen slot.
                    const int32_t nonCenterWorked = static_cast<int32_t>(
                        selCity->workedTiles().size()) - 1;
                    const int32_t cap = selCity->population();
                    if (nonCenterWorked < cap) {
                        selCity->toggleWorker(clickedTile);
                    } else {
                        LOG_INFO("No free citizen slots in %s (pop %d)",
                                 selCity->name().c_str(), cap);
                    }
                }
                return;
            }
        }
    }

    this->m_selectedUnit = nullptr;
    this->m_selectedCity = nullptr;
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

    if (this->m_selectedUnit == nullptr && this->m_selectedCity == nullptr) {
        return;
    }

    // Only allow actions on own entities
    if (this->m_selectedUnit != nullptr && this->m_selectedUnit->owner() != 0) {
        return;  // Can't control other players' units
    }
    if (this->m_selectedCity != nullptr && this->m_selectedCity->owner() != 0) {
        return;  // Can't control other players' cities
    }

    const std::pair<uint32_t, uint32_t> contextFbSize = this->m_window.framebufferSize();
    const uint32_t fbWidth = contextFbSize.first;
    const uint32_t fbHeight = contextFbSize.second;
    float worldX = 0.0f, worldY = 0.0f;
    this->m_cameraController.screenToWorld(
        this->m_inputManager.mouseX(), this->m_inputManager.mouseY(),
        worldX, worldY, fbWidth, fbHeight);

    const float hexSize = this->m_gameRenderer.mapRenderer().hexSize();
    const hex::AxialCoord targetTile = hex::pixelToAxial(worldX, worldY, hexSize);

    if (!this->m_hexGrid.isValid(targetTile)) {
        return;
    }

    // Handle right-click on a selected city
    if (this->m_selectedCity != nullptr) {
        aoc::game::City& city = *this->m_selectedCity;

        // Right-click on city itself: open production picker
        if (city.location() == targetTile) {
            aoc::sim::ProductionQueueComponent& queue = city.production();
            if (queue.isEmpty()) {
                aoc::sim::ProductionQueueItem item{};
                item.type = aoc::sim::ProductionItemType::Unit;
                item.itemId = 0;  // Warrior
                item.name = "Warrior";
                item.totalCost = 40.0f;
                item.progress = 0.0f;
                queue.queue.push_back(std::move(item));
                LOG_INFO("Enqueued Warrior in %s", city.name().c_str());
            }
            return;
        }

        // Right-click on unowned tile adjacent to player's border: buy it
        const int32_t tileIdx = this->m_hexGrid.toIndex(targetTile);
        if (this->m_hexGrid.owner(tileIdx) == INVALID_PLAYER) {
            // Check if at least one neighbor is owned by this player
            bool adjacentToOwned = false;
            const std::array<aoc::hex::AxialCoord, 6> neighbors = aoc::hex::neighbors(targetTile);
            for (const aoc::hex::AxialCoord& nbr : neighbors) {
                if (this->m_hexGrid.isValid(nbr) &&
                    this->m_hexGrid.owner(this->m_hexGrid.toIndex(nbr)) == 0) {
                    adjacentToOwned = true;
                    break;
                }
            }
            if (!adjacentToOwned) { return; }

            const int32_t dist = this->m_hexGrid.distance(city.location(), targetTile);
            const int32_t cost = 25 * std::max(1, dist);

            // Two-click confirmation: first click shows cost, second click on same tile confirms
            if (this->m_pendingBuyTile == targetTile && this->m_pendingBuyConfirm) {
                // Second click: execute purchase via GameState player treasury
                aoc::game::Player* buyPlayer = this->m_gameState.player(0);
                if (buyPlayer != nullptr
                    && buyPlayer->spendGold(static_cast<CurrencyAmount>(cost))) {
                    this->m_hexGrid.setOwner(tileIdx, 0);
                    city.incrementTilesClaimed();
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

    // From here: a unit is selected.
    if (this->m_selectedUnit == nullptr) {
        return;
    }
    aoc::game::Unit& unit = *this->m_selectedUnit;

    // Activate Great Person on right-click at their own tile
    {
        aoc::sim::GreatPersonComponent& gp = unit.greatPerson();
        if (gp.position == targetTile && !gp.isActivated && gp.defId < aoc::sim::GREAT_PERSON_COUNT) {
            aoc::sim::activateGreatPerson(this->m_gameState, this->m_hexGrid, unit);
            this->m_selectedUnit = nullptr;
            return;
        }
    }

    // Religious unit actions: right-click on a city
    {
        const aoc::sim::UnitTypeDef& relDef = unit.typeDef();
        if (relDef.unitClass == aoc::sim::UnitClass::Religious && unit.spreadCharges > 0) {
            // Find city at target tile across all players
            for (const std::unique_ptr<aoc::game::Player>& relPlayer : this->m_gameState.players()) {
                aoc::game::City* relCity = relPlayer->cityAt(targetTile);
                if (relCity == nullptr) {
                    continue;
                }

                // Inquisitor: remove foreign religion from own city
                if (unit.typeId().value == 21 && relCity->owner() == unit.owner()) {
                    aoc::sim::CityReligionComponent& cityRel = relCity->religion();
                    for (uint8_t ri = 0; ri < aoc::sim::MAX_RELIGIONS; ++ri) {
                        if (ri != unit.spreadingReligion) {
                            cityRel.pressure[ri] = 0.0f;
                        }
                    }
                    LOG_INFO("Inquisitor removed foreign religion from %s",
                             relCity->name().c_str());
                    --unit.spreadCharges;
                    if (unit.spreadCharges <= 0) {
                        aoc::game::Player* relOwner = this->m_gameState.player(unit.owner());
                        if (relOwner != nullptr) { relOwner->removeUnit(&unit); }
                        this->m_selectedUnit = nullptr;
                    }
                    return;
                }

                // Missionary/Apostle: spread religion to target city
                if (unit.typeId().value == 19 || unit.typeId().value == 20) {
                    const float pressure = (unit.typeId().value == 19) ? 100.0f : 150.0f;
                    relCity->religion().addPressure(unit.spreadingReligion, pressure);
                    --unit.spreadCharges;

                    LOG_INFO("%.*s spread religion to %s (pressure +%d, charges left: %d)",
                             static_cast<int>(relDef.name.size()), relDef.name.data(),
                             relCity->name().c_str(), static_cast<int>(pressure),
                             static_cast<int>(unit.spreadCharges));

                    if (unit.spreadCharges <= 0) {
                        aoc::game::Player* relOwner = this->m_gameState.player(unit.owner());
                        if (relOwner != nullptr) { relOwner->removeUnit(&unit); }
                        this->m_selectedUnit = nullptr;
                    }
                    return;
                }
                break;
            }
        }
    }

    const aoc::sim::UnitTypeDef& def = unit.typeDef();

    // If settler and target is valid land, found a city
    if (def.unitClass == aoc::sim::UnitClass::Settler && unit.position() == targetTile) {
        const PlayerId cityOwner = unit.owner();
        const hex::AxialCoord cityPos = unit.position();

        const std::string cityName = aoc::sim::getNextCityName(this->m_gameState, cityOwner);

        aoc::game::Player* gsFounder = this->m_gameState.player(cityOwner);
        if (gsFounder != nullptr) {
            const bool isFirstCity = gsFounder->cityCount() == 0;
            aoc::game::City& gsCity = gsFounder->addCity(cityPos, cityName);
            gsCity.autoAssignWorkers(this->m_hexGrid, aoc::sim::WorkerFocus::Balanced, gsFounder);
            if (isFirstCity) {
                gsCity.setOriginalCapital(true);
                gsCity.setOriginalOwner(cityOwner);
            }
            aoc::sim::claimInitialTerritory(this->m_hexGrid, cityPos, cityOwner);

            // Remove the settler unit
            gsFounder->removeUnit(&unit);
            this->m_selectedUnit = nullptr;
            this->m_selectedCity = &gsCity;
            LOG_INFO("City founded!");

            aoc::sim::checkEurekaConditions(*gsFounder, aoc::sim::EurekaCondition::FoundCity);
        }
        return;
    }

    // Builder: build improvement at current position
    if (def.unitClass == aoc::sim::UnitClass::Civilian && unit.position() == targetTile) {
        const int32_t tileIndex = this->m_hexGrid.toIndex(unit.position());
        const aoc::map::ImprovementType bestImpr =
            aoc::sim::bestImprovementForTile(this->m_hexGrid, tileIndex);

        if (bestImpr != aoc::map::ImprovementType::None &&
            this->m_hexGrid.improvement(tileIndex) == aoc::map::ImprovementType::None) {
            this->m_hexGrid.setImprovement(tileIndex, bestImpr);
            unit.useCharge();

            // Eureka: check if the built improvement triggers a boost
            if (bestImpr == aoc::map::ImprovementType::Quarry) {
                aoc::game::Player* eurekaPlayer2 = this->m_gameState.player(unit.owner());
                if (eurekaPlayer2 != nullptr) {
                    aoc::sim::checkEurekaConditions(*eurekaPlayer2,
                                                    aoc::sim::EurekaCondition::BuildQuarry);
                }
            }

            LOG_INFO("Builder placed improvement at (%d,%d)",
                     unit.position().q, unit.position().r);

            if (!unit.hasCharges()) {
                aoc::game::Player* builderOwner = this->m_gameState.player(unit.owner());
                if (builderOwner != nullptr) { builderOwner->removeUnit(&unit); }
                this->m_selectedUnit = nullptr;
                LOG_INFO("Builder exhausted all charges");
            }
            return;
        }
    }

    // Embark: land unit right-clicking an adjacent water tile
    const int32_t targetIndex = this->m_hexGrid.toIndex(targetTile);
    const aoc::map::TerrainType targetTerrain = this->m_hexGrid.terrain(targetIndex);
    if (!aoc::sim::isNaval(def.unitClass) && unit.state() != aoc::sim::UnitState::Embarked
        && targetTerrain == aoc::map::TerrainType::Coast
        && this->m_hexGrid.distance(unit.position(), targetTile) == 1) {
        (void)aoc::sim::tryEmbark(unit, targetTile, this->m_hexGrid);
        return;
    }

    // Disembark: embarked unit right-clicking an adjacent land tile
    if (unit.state() == aoc::sim::UnitState::Embarked
        && !aoc::map::isWater(targetTerrain) && !aoc::map::isImpassable(targetTerrain)
        && this->m_hexGrid.distance(unit.position(), targetTile) == 1) {
        (void)aoc::sim::tryDisembark(unit, targetTile, this->m_hexGrid);
        return;
    }

    // Save undo state before movement
    this->m_undoState.unit = &unit;
    this->m_undoState.previousPosition = unit.position();
    this->m_undoState.previousMovement = unit.movementRemaining();
    this->m_undoState.hasState = true;

    // Order movement using the object-model overload
    const bool pathFound = aoc::sim::orderUnitMove(unit, targetTile, this->m_hexGrid);
    if (pathFound) {
        const aoc::hex::AxialCoord posBefore = unit.position();

        // Execute movement immediately for this turn's remaining movement points
        aoc::sim::moveUnitAlongPath(this->m_gameState, unit, this->m_hexGrid);

        // Only update fog if the unit actually moved (not just path set with 0 MP)
        if (unit.position() != posBefore) {
            this->m_fogOfWar.updateVisibility(this->m_gameState, this->m_hexGrid, 0);
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
    if (this->m_undoState.unit == nullptr) {
        this->m_undoState.hasState = false;
        return;
    }

    aoc::game::Unit& unit = *this->m_undoState.unit;
    unit.setPosition(this->m_undoState.previousPosition);
    unit.setMovementRemaining(this->m_undoState.previousMovement);
    unit.clearPath();
    unit.setState(aoc::sim::UnitState::Idle);

    LOG_INFO("Undo: unit moved back to (%d,%d) with %d MP",
             unit.position().q, unit.position().r, unit.movementRemaining());

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
    aoc::sim::executeMovement(this->m_gameState, 0, this->m_hexGrid);

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
        this->m_turnManager.executeTurn(this->m_gameState);

        // Capture pre-turn tech/civic state for UI notifications
        const aoc::game::Player* humanGs = this->m_gameState.humanPlayer();
        TechId prevResearch = humanGs->tech().currentResearch;
        CivicId prevCivic = humanGs->civics().currentResearch;

        // Build TurnContext and execute all game logic via TurnProcessor
        aoc::sim::TurnContext turnCtx{};

        turnCtx.grid = &this->m_hexGrid;
        turnCtx.fogOfWar = &this->m_fogOfWar;
        turnCtx.economy = &this->m_economy;
        turnCtx.diplomacy = &this->m_diplomacy;
        turnCtx.barbarians = &this->m_barbarianController;
        turnCtx.dealTracker = &this->m_dealTracker;
        turnCtx.allianceTracker = &this->m_allianceTracker;
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
            aoc::sim::executeMovement(this->m_gameState, ai.player(), this->m_hexGrid);
        }

        // Diplomacy decay, espionage, grievance tick, world congress all run
        // inside processTurn (TurnProcessor.cpp). Do NOT call them again here.

        // Goody hut exploration: any unit standing on a hut tile claims it.
        if (!this->m_goodyHuts.hutLocations.empty()) {
            const int32_t playerCount = this->m_gameState.playerCount();
            for (int32_t p = 0; p < playerCount; ++p) {
                aoc::game::Player* gsp =
                    this->m_gameState.player(static_cast<aoc::PlayerId>(p));
                if (gsp == nullptr) { continue; }
                std::vector<aoc::hex::AxialCoord> positions;
                positions.reserve(gsp->units().size());
                for (const std::unique_ptr<aoc::game::Unit>& unitPtr : gsp->units()) {
                    positions.push_back(unitPtr->position());
                }
                for (const aoc::hex::AxialCoord& pos : positions) {
                    aoc::sim::GoodyHutReward r = aoc::sim::checkAndClaimGoodyHut(
                        this->m_goodyHuts, this->m_gameState, *gsp, pos,
                        this->m_gameRng);
                    if (r != aoc::sim::GoodyHutReward::Count && p == 0) {
                        this->m_notificationManager.push("Ancient ruin explored!",
                                                          4.0f, 0.8f, 0.8f, 0.3f);
                    }
                }
            }
        }

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
                this->m_techScreen.setContext(&this->m_gameState, 0);
                this->m_techScreen.setGrid(&this->m_hexGrid);
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

        // Drain sim-layer notifications (war declarations, alliances, wonder
        // completions, resource reveals, great people, etc.) and route them
        // to both the toast system and the persistent event log.
        {
            const PlayerId humanId = (humanPost != nullptr)
                ? humanPost->id() : INVALID_PLAYER;
            std::vector<aoc::ui::GameNotification> drained =
                aoc::ui::drainNotifications(humanId);
            for (const aoc::ui::GameNotification& note : drained) {
                const std::string formatted = note.title + ": " + note.body;
                this->m_eventLog.addEvent(formatted);
                // Colour by category: diplomacy red, economy yellow, military
                // orange, city cyan, everything else white.
                float cr = 1.0f, cg = 1.0f, cb = 1.0f;
                switch (note.category) {
                    case aoc::ui::NotificationCategory::Diplomacy:
                        cr = 1.0f; cg = 0.4f; cb = 0.4f; break;
                    case aoc::ui::NotificationCategory::Economy:
                        cr = 1.0f; cg = 0.9f; cb = 0.3f; break;
                    case aoc::ui::NotificationCategory::Military:
                        cr = 1.0f; cg = 0.6f; cb = 0.2f; break;
                    case aoc::ui::NotificationCategory::City:
                        cr = 0.4f; cg = 0.9f; cb = 1.0f; break;
                    default: break;
                }
                const float duration = (note.priority >= 8) ? 6.0f : 4.0f;
                this->m_notificationManager.push(formatted, duration, cr, cg, cb);
            }
        }

        // Record replay frame
        this->m_replayRecorder.recordFrame(this->m_gameState,
                                            this->m_turnManager.currentTurn());

        // Sound events for turn transition
        this->m_soundQueue.clear();
        this->m_soundQueue.push(aoc::audio::SoundEffect::TurnEnd);
        this->m_soundQueue.push(aoc::audio::SoundEffect::TurnStart);

        // Switch music track based on human player's era
        {
            const aoc::game::Player* humanPlayer = this->m_gameState.humanPlayer();
            if (humanPlayer != nullptr) {
                const uint16_t eraVal = humanPlayer->era().currentEra.value;
                if (eraVal < static_cast<uint16_t>(aoc::audio::MusicTrack::Count) - 2) {
                    const aoc::audio::MusicTrack track =
                        static_cast<aoc::audio::MusicTrack>(eraVal + 1);
                    if (track != this->m_musicManager.track()) {
                        this->m_musicManager.setTrack(track);
                        LOG_INFO("Music track changed to era %u", eraVal);
                    }
                }
            }
        }

        // Mark intermediate path tiles AND their sight radius as Revealed for
        // the owner so an auto-moving unit uncovers fog along the whole route,
        // not just the final tile.  updateVisibility below only reveals around
        // the unit's current position; without this pass everything behind a
        // multi-step move this turn would stay Unseen even though the unit
        // physically walked past it.
        constexpr int32_t SCOUT_SIGHT = 2;
        constexpr int32_t UNIT_SIGHT  = 1;
        for (const std::unique_ptr<aoc::game::Player>& playerPtr : this->m_gameState.players()) {
            if (playerPtr == nullptr) { continue; }
            const PlayerId owner = playerPtr->id();
            for (const std::unique_ptr<aoc::game::Unit>& unitPtr : playerPtr->units()) {
                if (unitPtr == nullptr) { continue; }
                const int32_t sight = (unitPtr->typeDef().unitClass == aoc::sim::UnitClass::Scout)
                    ? SCOUT_SIGHT : UNIT_SIGHT;
                for (const aoc::hex::AxialCoord& tile : unitPtr->movementTrace()) {
                    for (int32_t q = -sight; q <= sight; ++q) {
                        const int32_t rLo = std::max(-sight, -q - sight);
                        const int32_t rHi = std::min( sight, -q + sight);
                        for (int32_t r = rLo; r <= rHi; ++r) {
                            const aoc::hex::AxialCoord t{tile.q + q, tile.r + r};
                            if (!this->m_hexGrid.isValid(t)) { continue; }
                            const int32_t idx = this->m_hexGrid.toIndex(t);
                            if (this->m_fogOfWar.visibility(owner, idx) == aoc::map::TileVisibility::Unseen) {
                                this->m_fogOfWar.setVisibility(owner, idx, aoc::map::TileVisibility::Revealed);
                            }
                        }
                    }
                }
                unitPtr->clearMovementTrace();
            }
        }

        // Update fog of war for all players
        this->m_fogOfWar.updateVisibility(this->m_gameState, this->m_hexGrid, 0);
        for (const aoc::sim::ai::AIController& ai : this->m_aiControllers) {
            this->m_fogOfWar.updateVisibility(this->m_gameState, this->m_hexGrid, ai.player());
        }

        // Check victory conditions: read cached result from processTurn.
        const aoc::sim::VictoryResult& vr = turnCtx.lastVictoryResult;
        if (vr.type != aoc::sim::VictoryType::None) {
            this->m_gameOver = true;
            this->m_victoryResult = vr;
            LOG_INFO("Game over! Player %u wins by %s",
                     static_cast<unsigned>(vr.winner),
                     vr.type == aoc::sim::VictoryType::Science       ? "Science" :
                     vr.type == aoc::sim::VictoryType::Domination    ? "Domination" :
                     vr.type == aoc::sim::VictoryType::Culture       ? "Culture" :
                     vr.type == aoc::sim::VictoryType::Score         ? "Score" :
                     vr.type == aoc::sim::VictoryType::Religion      ? "Religion" : "Unknown");

            const uint8_t totalPlayers = static_cast<uint8_t>(1 + this->m_aiControllers.size());
            this->m_scoreScreen.setContext(
                &this->m_gameState, &this->m_hexGrid, vr, totalPlayers,
                [this]() { this->returnToMainMenu(); });
            this->m_scoreScreen.open(this->m_uiManager);
        }

        // Government/policy unlocks from completed civics happen inside
        // processCivicResearch (CivicTree.cpp) during processTurn.

        this->m_turnManager.beginNewTurn();

        // Clear undo state at the start of a new turn
        this->m_undoState.hasState = false;

        // Wake sleeping units (human player only) if enemies are within 2 hexes
        {
            aoc::game::Player* humanWake = this->m_gameState.humanPlayer();
            if (humanWake != nullptr) {
                for (const std::unique_ptr<aoc::game::Unit>& sleeperPtr : humanWake->units()) {
                    aoc::game::Unit& sleeper = *sleeperPtr;
                    if (!sleeper.isSleeping()) {
                        continue;
                    }
                    bool enemyNearby = false;
                    for (const std::unique_ptr<aoc::game::Player>& otherPlayer : this->m_gameState.players()) {
                        if (otherPlayer->id() == humanWake->id() || otherPlayer->id() == BARBARIAN_PLAYER) {
                            continue;
                        }
                        for (const std::unique_ptr<aoc::game::Unit>& otherUnit : otherPlayer->units()) {
                            if (this->m_hexGrid.distance(sleeper.position(), otherUnit->position()) <= 2) {
                                enemyNearby = true;
                                break;
                            }
                        }
                        if (enemyNearby) { break; }
                    }
                    if (enemyNearby) {
                        sleeper.setState(aoc::sim::UnitState::Idle);
                        sleeper.autoExplore = false;
                        LOG_INFO("Sleeping unit at (%d,%d) woke up -- enemy nearby",
                                 sleeper.position().q, sleeper.position().r);
                    }
                }
            }
        }

        // Auto-explore: move human scout units toward unexplored territory.
        // Collect raw pointers first so iteration is stable if path-ordering
        // indirectly modifies the unit list in edge cases.
        {
            aoc::game::Player* humanAutoExplore = this->m_gameState.humanPlayer();
            if (humanAutoExplore != nullptr) {
                std::vector<aoc::game::Unit*> autoExploreUnits;
                for (const std::unique_ptr<aoc::game::Unit>& u : humanAutoExplore->units()) {
                    if (u->autoExplore) {
                        autoExploreUnits.push_back(u.get());
                    }
                }
                const int32_t tileCount = this->m_hexGrid.tileCount();
                for (aoc::game::Unit* unit : autoExploreUnits) {
                    hex::AxialCoord bestTarget = unit->position();
                    int32_t bestDist = INT32_MAX;
                    for (int32_t t = 0; t < tileCount; ++t) {
                        if (this->m_fogOfWar.visibility(0, t) != aoc::map::TileVisibility::Unseen) {
                            continue;
                        }
                        const hex::AxialCoord tileCoord = this->m_hexGrid.toAxial(t);
                        const int32_t dist = this->m_hexGrid.distance(unit->position(), tileCoord);
                        if (dist < bestDist) {
                            bestDist = dist;
                            bestTarget = tileCoord;
                        }
                    }
                    if (bestDist < INT32_MAX && !(bestTarget == unit->position())) {
                        aoc::sim::orderUnitMove(*unit, bestTarget, this->m_hexGrid);
                    }
                }
            }
        }

        // Auto-improve: civilian units build improvements or move to the nearest
        // unimproved owned tile. Builder is removed when all charges are consumed.
        {
            aoc::game::Player* humanAutoImprove = this->m_gameState.humanPlayer();
            if (humanAutoImprove != nullptr) {
                std::vector<aoc::game::Unit*> autoImproveUnits;
                for (const std::unique_ptr<aoc::game::Unit>& u : humanAutoImprove->units()) {
                    if (u->autoImprove) {
                        autoImproveUnits.push_back(u.get());
                    }
                }
                for (aoc::game::Unit* unit : autoImproveUnits) {
                    if (!unit->hasCharges()) {
                        continue;
                    }

                    const int32_t currentIdx = this->m_hexGrid.toIndex(unit->position());
                    const aoc::map::ImprovementType bestImpr =
                        aoc::sim::bestImprovementForTile(this->m_hexGrid, currentIdx);

                    if (bestImpr != aoc::map::ImprovementType::None &&
                        this->m_hexGrid.improvement(currentIdx) == aoc::map::ImprovementType::None) {
                        this->m_hexGrid.setImprovement(currentIdx, bestImpr);
                        unit->useCharge();
                        LOG_INFO("Auto-improve: built improvement at (%d,%d)",
                                 unit->position().q, unit->position().r);
                        if (!unit->hasCharges()) {
                            humanAutoImprove->removeUnit(unit);
                            LOG_INFO("Auto-improve: builder exhausted all charges");
                        }
                        continue;
                    }

                    // Find nearest unimproved owned tile and move there
                    hex::AxialCoord bestTarget = unit->position();
                    int32_t bestDist = INT32_MAX;
                    const int32_t tileCount = this->m_hexGrid.tileCount();
                    for (int32_t t = 0; t < tileCount; ++t) {
                        if (this->m_hexGrid.owner(t) != unit->owner()) { continue; }
                        if (this->m_hexGrid.improvement(t) != aoc::map::ImprovementType::None) { continue; }
                        if (this->m_hexGrid.movementCost(t) == 0) { continue; }
                        if (aoc::sim::bestImprovementForTile(this->m_hexGrid, t) == aoc::map::ImprovementType::None) {
                            continue;
                        }
                        const hex::AxialCoord tileCoord = this->m_hexGrid.toAxial(t);
                        const int32_t dist = this->m_hexGrid.distance(unit->position(), tileCoord);
                        if (dist < bestDist) {
                            bestDist = dist;
                            bestTarget = tileCoord;
                        }
                    }
                    if (bestDist < INT32_MAX && !(bestTarget == unit->position())) {
                        aoc::sim::orderUnitMove(*unit, bestTarget, this->m_hexGrid);
                    }
                }
            }
        }

        // Refresh movement for next turn
        aoc::sim::refreshMovement(this->m_gameState, 0);

        // Multi-turn movement continuation: resume pending paths after movement refresh.
        {
            aoc::game::Player* humanPending = this->m_gameState.humanPlayer();
            if (humanPending != nullptr) {
                std::vector<aoc::game::Unit*> pendingUnits;
                for (const std::unique_ptr<aoc::game::Unit>& u : humanPending->units()) {
                    if (!u->pendingPath().empty()) {
                        pendingUnits.push_back(u.get());
                    }
                }
                for (aoc::game::Unit* unit : pendingUnits) {
                    aoc::sim::moveUnitAlongPath(this->m_gameState, *unit, this->m_hexGrid);
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

    // Initialise all player state directly on the GameState object model.
    aoc::game::Player* humanPlayer = this->m_gameState.humanPlayer();
    if (humanPlayer != nullptr) {
        humanPlayer->setCivId(civId);

        aoc::sim::MonetaryStateComponent& monetary = humanPlayer->monetary();
        monetary.owner = 0;
        monetary.system = aoc::sim::MonetarySystemType::Barter;
        monetary.treasury = 0;
        monetary.moneySupply = 0;
        monetary.taxRate = 0.15f;
        monetary.governmentSpending = 0;

        humanPlayer->economy().owner = 0;
        humanPlayer->economy().treasury = 0;
        humanPlayer->setTreasury(0);

        humanPlayer->tech().owner = 0;
        humanPlayer->tech().initialize();
        humanPlayer->tech().currentResearch = TechId{0};  // Start researching Mining

        humanPlayer->civics().owner = 0;
        humanPlayer->civics().initialize();
        humanPlayer->civics().currentResearch = CivicId{0};  // Start researching Code of Laws

        humanPlayer->era().owner = 0;
        humanPlayer->victoryTracker().owner = 0;

        humanPlayer->government().owner = 0;
        humanPlayer->government().government = aoc::sim::GovernmentType::Chiefdom;

        humanPlayer->greatPeople().owner = 0;
        humanPlayer->eureka().owner = 0;
        humanPlayer->banking().owner = 0;

        // Spawn starting units. Try each adjacent neighbour in turn so
        // the warrior never overlaps the settler tile. Fall back to the
        // spiral search (which may still collide on coastal starts).
        hex::AxialCoord warriorPos = capitalPos;
        const std::array<aoc::hex::AxialCoord, 6> nbrs = aoc::hex::neighbors(capitalPos);
        for (const aoc::hex::AxialCoord& n : nbrs) {
            if (!this->m_hexGrid.isValid(n)) { continue; }
            const int32_t idx = this->m_hexGrid.toIndex(n);
            if (this->m_hexGrid.movementCost(idx) <= 0) { continue; }
            warriorPos = n;
            break;
        }
        if (warriorPos == capitalPos) {
            warriorPos = this->findNearbyLandTile({capitalPos.q + 1, capitalPos.r});
        }
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

    // Initialise all AI player state directly on the GameState object model.
    aoc::game::Player* aiPlayer = this->m_gameState.player(player);
    if (aiPlayer != nullptr) {
        aiPlayer->setCivId(civId);

        aoc::sim::MonetaryStateComponent& monetary = aiPlayer->monetary();
        monetary.owner = player;
        monetary.system = aoc::sim::MonetarySystemType::Barter;
        monetary.treasury = 0;

        aiPlayer->economy().owner = player;
        aiPlayer->economy().treasury = 0;
        aiPlayer->setTreasury(0);

        aiPlayer->tech().owner = player;
        aiPlayer->tech().initialize();

        aiPlayer->civics().owner = player;
        aiPlayer->civics().initialize();

        aiPlayer->era().owner = player;
        aiPlayer->victoryTracker().owner = player;

        aiPlayer->government().owner = player;
        aiPlayer->government().government = aoc::sim::GovernmentType::Chiefdom;

        aiPlayer->greatPeople().owner = player;
        aiPlayer->eureka().owner = player;
        aiPlayer->banking().owner = player;

        // Spawn settler (AI will auto-found city on first turn) and warrior
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
                    // Resource yield is read directly from the HexGrid by the economy
                    // simulation; no ECS entity is needed.
                    this->m_hexGrid.setResource(index, ResourceId{placement.goodId});
                    ++totalPlaced;
                    break;  // Only one resource per tile
                }
            }
        }
    }

    LOG_INFO("Placed %d resources on map", totalPlaced);
}


// ============================================================================
// Screen helpers
// ============================================================================

bool Application::anyScreenOpen() const {
    return this->m_screenRegistry.anyOpen();
}

bool Application::onlyCityDetailScreenOpen() const {
    // The city-detail screen is a right-side panel that leaves the map
    // clickable; callers special-case it so HUD input still works.
    if (!this->m_cityDetailScreen.isOpen()) { return false; }
    // Any OTHER registered screen being open disqualifies the state.
    if (this->m_productionScreen.isOpen()
        || this->m_techScreen.isOpen()
        || this->m_governmentScreen.isOpen()
        || this->m_economyScreen.isOpen()
        || this->m_tradeScreen.isOpen()
        || this->m_tradeRouteSetupScreen.isOpen()
        || this->m_diplomacyScreen.isOpen()
        || this->m_religionScreen.isOpen()
        || this->m_scoreScreen.isOpen()
        || this->m_settingsMenu.isOpen()) {
        return false;
    }
    return true;
}

void Application::closeAllScreens() {
    this->m_screenRegistry.closeAll(this->m_uiManager);
}

} // namespace aoc::app
