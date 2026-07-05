#pragma once

/**
 * @file Application.hpp
 * @brief Top-level application class owning the window, renderer, and game loop.
 */

#include "aoc/app/Window.hpp"
#include "aoc/app/InputManager.hpp"
#include "aoc/app/DebugCommandFile.hpp"
#include "aoc/debug/DebugServer.hpp"
#include "aoc/render/CameraController.hpp"
#include "aoc/render/GameRenderer.hpp"
#include "aoc/render/GlobeRenderer.hpp"
#include "aoc/map/HexGrid.hpp"
#include "aoc/map/FogOfWar.hpp"
#include "aoc/simulation/turn/TurnManager.hpp"
#include "aoc/simulation/resource/EconomySimulation.hpp"
#include "aoc/simulation/diplomacy/DiplomacyState.hpp"
#include "aoc/simulation/diplomacy/DealTerms.hpp"
#include "aoc/simulation/diplomacy/AllianceObligations.hpp"
#include "aoc/simulation/victory/VictoryCondition.hpp"
#include "aoc/simulation/ai/AIController.hpp"
#include "aoc/simulation/barbarian/BarbarianController.hpp"
#include "aoc/simulation/map/GoodyHuts.hpp"
#include "aoc/core/Random.hpp"
#include "aoc/ui/UIManager.hpp"
#include "aoc/ui/ScreenRegistry.hpp"
#include "aoc/ui/WidgetInspector.hpp"
#include "aoc/ui/LoadingScreen.hpp"
#include "aoc/ui/PauseMenu.hpp"
#include "aoc/ui/GameScreens.hpp"
#include "aoc/ui/TradeScreen.hpp"
#include "aoc/ui/TradeRouteSetupScreen.hpp"
#include "aoc/ui/DiplomacyScreen.hpp"
#include "aoc/ui/ReligionScreen.hpp"
#include "aoc/ui/EventLog.hpp"
#include "aoc/ui/MainMenu.hpp"
#include "aoc/ui/SettingsMenu.hpp"
#include "aoc/ui/ScoreScreen.hpp"
#include "aoc/simulation/tech/EurekaBoost.hpp"
#include "aoc/audio/SoundEvent.hpp"
#include "aoc/audio/MusicManager.hpp"
#include "aoc/ui/Notifications.hpp"
#include "aoc/ui/Tutorial.hpp"
#include "aoc/ui/DebugConsole.hpp"
#include "aoc/replay/ReplayRecorder.hpp"
#include "aoc/net/GameDBus.hpp"
#include "aoc/game/GameState.hpp"
#include "aoc/core/ErrorCodes.hpp"
#include "aoc/core/Types.hpp"
#include "aoc/map/MapGenerator.hpp"
#include "aoc/ui/SpectatorHUD.hpp"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_map>

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
        /// Opt-in localhost HTTP debug API (`--enable-debug-server`).
        /// OFF by default: the server exposes mutation routes and file
        /// dumps, so it must never listen unless explicitly requested.
        bool enableDebugServer = false;
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
    /// 3D globe renderer for the Continent Creator. Held as
    /// pointer-to-impl so we don't drag Renderer3D into the public
    /// Application header (Vulkan types stay confined to .cpp).
    std::unique_ptr<aoc::render::GlobeRenderer> m_globeRenderer;
    /// Live-debug hook. Polls /tmp/aoc_debug.cmd each frame; each
    /// command runs synchronously on the main thread so handlers
    /// can read/mutate any state without locking. Initialised + verbs
    /// registered in `Application::initialize`.
    aoc::app::DebugCommandFile m_debugCmdFile;
    /// Localhost HTTP debug API. Listens on 127.0.0.1:9876 (default).
    /// Routes registered in `Application::initialize`. Handlers run
    /// on cpp-httplib worker threads; v1 routes are read-only against
    /// game state (race tolerated, mutations come via mutation queue
    /// in a later phase).
    std::unique_ptr<aoc::debug::DebugServer> m_debugServer;

    // Game state
    aoc::game::GameState         m_gameState;
    aoc::map::HexGrid          m_hexGrid;
    aoc::sim::TurnManager        m_turnManager;
    aoc::sim::EconomySimulation  m_economy;
    aoc::map::FogOfWar           m_fogOfWar;
    aoc::sim::DiplomacyManager   m_diplomacy;
    std::vector<aoc::sim::ai::AIController> m_aiControllers;
    aoc::sim::BarbarianController m_barbarianController;
    aoc::sim::GlobalDealTracker   m_dealTracker;
    aoc::sim::AllianceObligationTracker m_allianceTracker;
    aoc::sim::GoodyHutState      m_goodyHuts;   ///< Ancient ruins placed on map.
    aoc::Random                  m_gameRng{0};  ///< Reseeded in startGame()

    /// Currently selected unit (nullptr if none or city selected).
    aoc::game::Unit* m_selectedUnit = nullptr;

    /// Currently selected city (nullptr if none or unit selected).
    aoc::game::City* m_selectedCity = nullptr;

    /// Previous-frame selection pointers. `updateHUD` diffs against these
    /// and calls `rebuildUnitActionPanel` whenever the selection changes,
    /// so the action panel stays in sync with click/cycle/end-turn paths
    /// without each site needing to remember to rebuild.
    aoc::game::Unit* m_prevSelectedUnit = nullptr;
    aoc::game::City* m_prevSelectedCity = nullptr;

    // UI
    aoc::ui::UIManager m_uiManager;
    aoc::ui::WidgetId  m_turnLabel      = aoc::ui::INVALID_WIDGET;
    aoc::ui::WidgetId  m_selectionLabel = aoc::ui::INVALID_WIDGET;
    aoc::ui::WidgetId  m_economyLabel   = aoc::ui::INVALID_WIDGET;
    aoc::ui::WidgetId  m_endTurnButton  = aoc::ui::INVALID_WIDGET;
    aoc::ui::WidgetId  m_endTurnInnerBtn = aoc::ui::INVALID_WIDGET; ///< Inner button child for label/onClick mutation
    aoc::ui::WidgetId  m_lastPlayerBanner = aoc::ui::INVALID_WIDGET; ///< "Waiting for you" glow
    aoc::ui::WidgetId  m_topBar         = aoc::ui::INVALID_WIDGET;
    aoc::ui::WidgetId  m_resourceLabel  = aoc::ui::INVALID_WIDGET;
    /// Civ-6-style yield strip in the HUD top bar. Each yield gets its
    /// own (icon + value) pair so updateHUD can refresh just the
    /// numeric text without rebuilding labels. Labels live as siblings
    /// inside `m_yieldStrip`.
    aoc::ui::WidgetId  m_yieldStrip     = aoc::ui::INVALID_WIDGET;
    aoc::ui::WidgetId  m_goldLabel      = aoc::ui::INVALID_WIDGET;
    aoc::ui::WidgetId  m_scienceLabel   = aoc::ui::INVALID_WIDGET;
    aoc::ui::WidgetId  m_cultureLabel   = aoc::ui::INVALID_WIDGET;
    aoc::ui::WidgetId  m_faithLabel     = aoc::ui::INVALID_WIDGET;
    /// Civ-6 style strip of player icons in the top bar. Children
    /// rebuilt each frame from `updateDiploStrip` to reflect met /
    /// at-war / allied state. One icon per known civ.
    aoc::ui::WidgetId  m_diploStrip     = aoc::ui::INVALID_WIDGET;

    /// Dev-time widget inspector. F11 toggles. Renders hover-highlight
    /// + hovered/focused ids over the UI.
    aoc::ui::WidgetInspector m_widgetInspector;

    /// Full-screen loading overlay shown during `startGame` while map
    /// generation + placement + initial player spawn run. Registered
    /// in the ScreenRegistry so resize re-layouts it.
    aoc::ui::LoadingScreen   m_loadingScreen;
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
    /// City-detail-open state at last rebuild. Used by
    /// `rebuildUnitActionPanel` to detect when the bottom-right margin
    /// needs to shift (past the city-detail panel).
    bool m_actionPanelCityOpen = false;

    /// Cached GLFW standard cursors. Created once at init; switched
    /// per frame based on the hovered widget's `hoverCursor` field.
    /// Enum: 0=default, 1=hand, 2=ibeam, 3=crosshair. Void* keeps
    /// GLFW out of the header.
    struct CursorHandles {
        void* arrow     = nullptr;
        void* hand      = nullptr;
        void* ibeam     = nullptr;
        void* crossHair = nullptr;
        int32_t lastApplied = 0;
    };
    CursorHandles m_cursors;

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
    aoc::ui::TradeRouteSetupScreen m_tradeRouteSetupScreen;
    aoc::ui::DiplomacyScreen    m_diplomacyScreen;
    aoc::ui::ReligionScreen     m_religionScreen;
    aoc::ui::ScoreScreen        m_scoreScreen;

    /// Central registry for all modal screens + menus. Populated once in
    /// `initialize()`; replaces the hand-maintained `anyScreenOpen` /
    /// `closeAllScreens` enumerations. SettingsMenu registers here too,
    /// closing the input-gate leak that let it swallow clicks in-game
    /// without the input dispatcher knowing a screen was open.
    aoc::ui::ScreenRegistry m_screenRegistry;

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

    // Session-bus DBus service for external automation (screenshots etc.).
    aoc::net::GameDBus m_dbusService;

    /// Returns true if any modal screen is currently open.
    [[nodiscard]] bool anyScreenOpen() const;

    /// Returns true if only the city detail screen is open (a right-side panel
    /// that should not block map interaction).
    [[nodiscard]] bool onlyCityDetailScreenOpen() const;

    /// Close all open screens.
    void closeAllScreens();

    void buildHUD();
    void updateHUD();
    /// Rebuild the diplo strip children from the current met/at-war
    /// state. Cheap — drops existing children + adds one icon per
    /// major player. Called each frame.
    void updateDiploStrip();

    bool m_initialized = false;

    // App state machine
    AppState m_appState = AppState::MainMenu;
    aoc::ui::MainMenu       m_mainMenu;
    aoc::ui::GameSetupScreen m_gameSetupScreen;
    aoc::ui::SettingsMenu   m_settingsMenu;
    aoc::ui::PauseMenu      m_pauseMenu;

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

    /// Target turn set by the seek slider.  When != current, Application
    /// fast-forwards (target > current) or loads a snapshot + replays
    /// (target < current) until reached.  -1 = no active seek.
    int32_t m_spectatorTargetTurn = -1;

    /// Widget IDs for the seek slider built during startSpectate.
    aoc::ui::WidgetId m_spectatorSeekSliderId = aoc::ui::INVALID_WIDGET;
    aoc::ui::WidgetId m_spectatorSeekPanelId  = aoc::ui::INVALID_WIDGET;
    aoc::ui::WidgetId m_spectatorSeekLabelId  = aoc::ui::INVALID_WIDGET;

    /// Snapshot ring: serialized game states captured every N turns so the
    /// seek slider can step backwards.  Key = turn number, value = binary
    /// blob produced by saveGame.  Kept in memory (tmpfs), capped to the
    /// last ~12 snapshots.
    std::map<int32_t, std::vector<uint8_t>> m_spectatorSnapshots;
    static constexpr int32_t SPECTATOR_SNAPSHOT_INTERVAL = 20;
    static constexpr size_t  SPECTATOR_SNAPSHOT_MAX      = 12;

    /// Build the seek slider + label at the bottom of the screen.  Called
    /// once from the spectate-start path.
    void buildSpectatorSeekControls(float screenW, float screenH);

    // ---- Continent Creator state ----
    /// True while the player is in continent-creator preview mode.
    /// Atomic: HTTP debug handlers read it from worker threads while
    /// the main thread flips it. Same for the seed / time scalars
    /// below -- they are independent flags/counters, so plain atomic
    /// loads/stores (no ordering relationship) are sufficient.
    std::atomic<bool> m_continentCreatorMode{false};
    /// Frozen seed + parameters for the current preview. The epoch
    /// scrubber regenerates the map by re-running MapGenerator with
    /// these params and a varying runEpochsLimit.
    std::atomic<uint32_t> m_creatorSeed{0};
    /// Total simulated geological time in millions of years. Default
    /// 3000 My (3 Gy) covers ~5 Wilson supercontinent cycles. The
    /// generator translates internally to its physics-epoch count via
    /// `MapGenerator::MY_PER_EPOCH_TARGET`. The continent-creator
    /// scrubber tracks `m_creatorTimeCurrentMy` for the on-screen
    /// "Now: 1.5 / 3.0 Gy" readout.
    std::atomic<int32_t> m_creatorTotalMy{3000};
    /// Plain staging value for the numeric text-input widget, which
    /// edits through an `int32_t*`. Synced from/to the atomic
    /// `m_creatorTotalMy` around each focus/commit (main thread only).
    int32_t  m_creatorTotalMyInput = 3000;
    int32_t  m_creatorLandPlates  = 4;
    std::atomic<int32_t> m_creatorTimeCurrentMy{3000};
    int32_t  m_creatorWidth  = 400;
    int32_t  m_creatorHeight = 200;
    /// Total plate-drift budget for the sim, in 10ths of a map width.
    /// 1 = 0.1x map width total drift, 60 = 6x. Default 12 = 1.2 of
    /// map width — plates traverse the map ≥ once during the sim,
    /// enabling multi-cycle Pangea assembly/dispersal.
    // 2026-05-04: dropped 12 -> 8 (0.6 -> 0.4 map widths total drift).
    // Earth's continents drifted ~12 % of equator-circumference over
    // 400 My; sim was 3x faster. With slower drift, plate boundaries
    // remain coherent longer and Wilson-cycle dynamics play out at a
    // pace closer to real Earth. User-adjustable via creator slider
    // (range 1-20 = 0.1-2.0 map widths).
    int32_t  m_creatorDriftPct = 8;
    /// Lazy snapshot cache: maps epoch → HexGrid copy. Populated on
    /// first visit to each epoch; subsequent scrubs to that epoch
    /// blit from cache instead of re-running MapGenerator. Cleared
    /// on parameter changes (Generate / Re-roll / W/H/Plates/Drift edits).
    /// Bounded LRU: the `m_creatorEpochCacheLru` deque tracks insertion
    /// order; oldest entries are evicted when size > CREATOR_CACHE_MAX.
    std::unordered_map<int32_t, aoc::map::HexGrid> m_creatorEpochCache;
    std::deque<int32_t> m_creatorEpochCacheLru;
    static constexpr std::size_t CREATOR_CACHE_MAX = 20;
    /// Serialises access to `m_creatorEpochCache`/`m_creatorEpochCacheLru`.
    /// Currently the cache is touched only from the main thread (via
    /// `regenerateContinentPreview`) but the mutex keeps the invariant
    /// explicit so a future helper that touches it from a worker thread
    /// is forced to lock. Hold for the shortest possible window.
    std::mutex m_creatorEpochCacheMutex;
    /// Drop both the snapshot map and the LRU order under the
    /// `m_creatorEpochCacheMutex`. Use this everywhere the cache
    /// becomes stale (re-roll, parameter change, exit creator mode);
    /// never call `m_creatorEpochCache.clear()` directly because that
    /// would leave the LRU desynchronised.
    void clearCreatorEpochCache();
    /// HTTP-debug worker → main-thread mailbox. Worker handlers set
    /// these flags and return immediately; the main loop drains them
    /// at the top of each frame (before any sim/render state mutates)
    /// so the actual work happens on the main thread.
    ///
    /// Sentinel choice for `m_pendingCreatorTime`: `std::atomic<int32_t>`
    /// with `std::numeric_limits<int32_t>::min()` meaning "no pending
    /// request". `std::atomic<std::optional<int32_t>>` is not lock-free
    /// on most platforms because `std::optional` is not trivially
    /// copyable in general, so we use a sentinel int instead. All real
    /// creator times are positive (clamped to >=0 by the regenerator),
    /// so the sentinel is unambiguous.
    static constexpr int32_t PENDING_TIME_NONE =
        std::numeric_limits<int32_t>::min();
    std::atomic<int32_t> m_pendingCreatorTime{PENDING_TIME_NONE};
    std::atomic<bool>    m_pendingReroll{false};
    /// Seed associated with the pending re-roll. Read on the main
    /// thread only after `m_pendingReroll` is observed true; the worker
    /// writes this BEFORE setting the flag (release on the flag), so a
    /// matching acquire on the flag synchronises this value.
    std::atomic<uint32_t> m_pendingRerollSeed{0};
    std::atomic<bool>    m_quitRequested{false};

    /// Off-main-thread map regeneration. The main thread enqueues a target
    /// epoch via `enqueueRegen(my)`; the worker thread runs `MapGenerator::generate`
    /// into the side buffer `m_pendingGrid`; the main thread picks up the result
    /// in `consumeRegenResult()` at the top of each frame and swaps it into
    /// `m_hexGrid` plus runs fix-ups (FogOfWar resize, renderer dirty flag).
    ///
    /// Why a side buffer instead of `std::atomic<std::shared_ptr<HexGrid>>`:
    /// every reader of `m_hexGrid` today is on the main thread, and the swap
    /// is exclusive (main thread is paused inside `consumeRegenResult` when it
    /// runs). Adding a shared-pointer indirection would force ~200 reader
    /// sites to load + dereference per frame; the simpler model owns the
    /// invariant via mutex on the cache and a single owning swap on the main
    /// thread. HexGrid is ~750 KB so the swap is a pointer-level operation
    /// (std::swap on vectors).
    aoc::map::HexGrid              m_pendingGrid;
    /// CV wakes the worker when a new request lands. Lock guards
    /// `m_regenRequestCfg` (non-trivially-copyable; cannot live in an atomic).
    /// `m_regenRequestMy` + `m_regenRequestGeneration` are atomic so the
    /// worker can observe cancellation without holding the lock.
    std::condition_variable        m_regenCv;
    std::mutex                     m_regenWakeMutex;
    /// Configuration snapshot captured by the main thread at enqueue time.
    /// The worker reads this once under `m_regenWakeMutex` then releases the
    /// lock for the slow `MapGenerator::generate` call.
    aoc::map::MapGenerator::Config m_regenRequestCfg{};
    /// Latest requested target epoch, in My. PENDING_TIME_NONE = nothing
    /// queued. Writers (HTTP handlers, main thread) increment
    /// `m_regenRequestGeneration` AFTER writing this so the worker observes
    /// the latest request via a single acquire on the generation counter.
    std::atomic<int32_t>           m_regenRequestMy{PENDING_TIME_NONE};
    /// Monotonic counter bumped on every `enqueueRegen` call. Worker snapshots
    /// it at the start of a run; on completion, if the value has moved, the
    /// result is stale and the worker discards it (single-flight semantics).
    std::atomic<uint64_t>          m_regenRequestGeneration{0};
    /// Worker → main signal. Set when `m_pendingGrid` holds a fresh result
    /// AND the target epoch the worker computed is still current. Cleared by
    /// the main thread inside `consumeRegenResult()` after the swap.
    std::atomic<bool>              m_regenResultReady{false};
    /// Snapshot of the epoch the worker computed `m_pendingGrid` for. Used by
    /// `consumeRegenResult()` for cache-insert + epoch-current-state update.
    std::atomic<int32_t>           m_regenResultEpochMy{PENDING_TIME_NONE};
    /// Worker stop signal. Set by `~Application` (or explicit shutdown) before
    /// destruction. The worker checks it on every CV wake and exits when set.
    std::jthread                   m_regenWorker;

    /// Immutable grid snapshot for the HTTP debug handlers. The main
    /// thread whole-object-assigns `m_hexGrid` (regen swap, map load,
    /// reset), so worker threads must never read it directly -- that
    /// is a heap use-after-free. Instead the main thread PUBLISHES a
    /// copy after every grid rebuild via `publishDebugGridSnapshot()`
    /// and handlers read only `debugGridSnapshot()` (null until the
    /// first publish). shared_ptr keeps a superseded snapshot alive
    /// until the last in-flight handler drops it; the mutex only spans
    /// the pointer load/store, never a handler body.
    mutable std::mutex m_debugGridSnapshotMutex;
    std::shared_ptr<const aoc::map::HexGrid> m_debugGridSnapshot;
    /// Main thread only: copy `m_hexGrid` into a fresh snapshot.
    void publishDebugGridSnapshot();
    /// Any thread: fetch the latest published snapshot (may be null).
    [[nodiscard]] std::shared_ptr<const aoc::map::HexGrid>
    debugGridSnapshot() const;

    /// Drop every raw `Unit*` / `City*` the UI caches (selection,
    /// previous-frame selection, action panel, movement undo). Call
    /// whenever entities are destroyed wholesale (new game, load,
    /// return to menu); per-unit death is handled by the
    /// `Player::setUnitRemovalObserver` hook.
    void clearEntitySelection();

    /// Enqueue a regen request. Sets the target and bumps the generation
    /// counter so any in-flight worker pass is implicitly cancelled (its
    /// result will be discarded). Returns immediately; the worker picks it
    /// up asynchronously.
    void enqueueRegen(int32_t timeMy);
    /// Worker loop body. Sleeps on `m_regenCv` until either a new request
    /// lands or the stop_token fires. Snapshots the request, runs the
    /// generator into `m_pendingGrid`, and (if no superseding request
    /// arrived) flips `m_regenResultReady` for the main thread.
    void regenWorkerLoop(std::stop_token stopToken);
    /// Main-thread drain. Checks `m_regenResultReady`; on hit, swaps
    /// `m_pendingGrid` into `m_hexGrid`, runs FogOfWar resize + renderer
    /// dirty flag + cache insert. Called at the top of every frame so the
    /// UI thread never blocks on the generator.
    void consumeRegenResult();

    /// Play state — when true, m_creatorTimeCurrentMy advances by one
    /// physics epoch (MY_PER_EPOCH_TARGET My) every PLAY_INTERVAL
    /// seconds, looping at m_creatorTotalMy.
    bool   m_creatorPlaying    = false;
    float  m_creatorPlayAccum  = 0.0f;
    aoc::ui::WidgetId m_creatorPlayBtnId = aoc::ui::INVALID_WIDGET;
    aoc::ui::WidgetId m_creatorPanelId = aoc::ui::INVALID_WIDGET;
    aoc::ui::WidgetId m_creatorEpochLabelId = aoc::ui::INVALID_WIDGET;
    aoc::ui::WidgetId m_creatorWidthLabelId  = aoc::ui::INVALID_WIDGET;
    aoc::ui::WidgetId m_creatorHeightLabelId = aoc::ui::INVALID_WIDGET;
    aoc::ui::WidgetId m_creatorPlatesLabelId = aoc::ui::INVALID_WIDGET;
    aoc::ui::WidgetId m_creatorDriftLabelId  = aoc::ui::INVALID_WIDGET;
    /// Top-of-screen ADVANCED config panel for creator-mode knobs
    /// that don't fit in the bottom panel anymore. Hosts climate
    /// phase + sea level + axial tilt + ENSO + Milankovitch.
    aoc::ui::WidgetId m_creatorAdvPanelId       = aoc::ui::INVALID_WIDGET;
    aoc::ui::WidgetId m_creatorClimatePhaseLabel= aoc::ui::INVALID_WIDGET;
    aoc::ui::WidgetId m_creatorSeaLevelLabel    = aoc::ui::INVALID_WIDGET;
    aoc::ui::WidgetId m_creatorAxialTiltLabel   = aoc::ui::INVALID_WIDGET;
    aoc::ui::WidgetId m_creatorEnsoLabel        = aoc::ui::INVALID_WIDGET;
    aoc::ui::WidgetId m_creatorMilanLabel       = aoc::ui::INVALID_WIDGET;
    aoc::ui::WidgetId m_creatorProjectionLabel  = aoc::ui::INVALID_WIDGET;
    /// Backing values for advanced config (mirror MapGenerator::Config
    /// flags). Updated by UI buttons / text inputs and pushed to the
    /// generator on regenerate.
    int32_t m_creatorClimatePhase = 0;     ///< 0 neutral / 1 greenhouse / 2 icehouse
    int32_t m_creatorSeaLevelTenths = 0;   ///< -10..+10 (= -1.0..+1.0 dec)
    int32_t m_creatorAxialTiltTenths = 235; ///< 0..400 (= 0.0..40.0°), Earth=235
    int32_t m_creatorEnsoState    = 0;     ///< 0/1/2
    int32_t m_creatorMilanTenths  = 0;     ///< 0..10 (= 0.0..1.0)
    /// Sphere → rectangle projection used to render hex tiles.
    /// 0 Mollweide, 1 Equirectangular, 2 Mercator, 3 Robinson.
    /// Cycler in continent creator advanced row.
    int32_t m_creatorProjection   = 0;
    /// 3D globe view toggle. When true, the continent creator preview
    /// renders a textured sphere instead of the flat hex map; left-
    /// drag rotates the sphere (Google-Maps style), scroll zooms.
    /// Existing time-scrubber, projection cycler, play/pause keep
    /// working -- the projection cycler is greyed out while globe is
    /// active because the sphere itself is the projection.
    bool    m_creatorGlobe        = false;
    aoc::ui::WidgetId m_creatorGlobeBtnId = aoc::ui::INVALID_WIDGET;
    /// Orbit camera state for the 3D globe view. Yaw/pitch in degrees,
    /// zoom = camera radius in unit-sphere multiples (1.0 = touching
    /// the surface, larger = further away).
    float   m_globeYawDeg         = 0.0f;
    float   m_globePitchDeg       = 0.0f;
    float   m_globeZoom           = 4.0f;
    /// Drag-rotate state. Set on left-button press over a non-widget
    /// region while the globe is active; cleared on release.
    bool    m_globeDragActive     = false;
    double  m_globeLastMouseX     = 0.0;
    double  m_globeLastMouseY     = 0.0;
    /// Deferred-regen flag. Setup-knob changes (W/H/Plates/EpochsTotal/
    /// text-input typing) only set this to true; the explicit "Generate"
    /// button consumes it and runs MapGenerator. Without this each
    /// keystroke would hang the UI mid-typing.
    bool m_creatorDirty = false;

    /// Focused numeric text-input. When `m_numInputTarget != nullptr`,
    /// digit / backspace / Enter / Esc keys go to this field; the
    /// `m_numInputBuffer` accumulates user keystrokes and the value
    /// commits on each edit so the live preview regenerates immediately.
    int32_t* m_numInputTarget = nullptr;
    int32_t  m_numInputMin    = 1;
    aoc::ui::WidgetId m_numInputLabelId = aoc::ui::INVALID_WIDGET;
    std::string m_numInputBuffer;
    std::function<void()> m_numInputOnChange;
    std::function<std::string()> m_numInputDisplay; ///< Render label text from int

    /// Begin editing the int pointed to by `target`. Snapshot value to
    /// buffer, raise focus indicator on `labelId`, store callbacks.
    void numInputFocus(int32_t* target,
                        int32_t minVal,
                        aoc::ui::WidgetId labelId,
                        std::function<void()> onChange,
                        std::function<std::string()> display);
    /// Commit + clear focus. Optional applyDelta=true triggers onChange.
    void numInputDefocus();
    /// Per-frame keystroke routing while focused.
    void numInputTick();

    /// Re-run MapGenerator with the stored creator config, halting
    /// the tectonic sim at `timeMy` millions of years simulated. The
    /// caller passes simulated geological age, not an epoch index;
    /// the regenerator snaps to the nearest physics-epoch boundary.
    /// SYNCHRONOUS — blocks until the new grid is in `m_hexGrid`. Use
    /// at startup or anywhere the caller must have the new grid before
    /// the next instruction. For UI scrub paths, prefer `enqueueRegen`
    /// which runs the generation on a worker thread so the UI stays
    /// responsive.
    void regenerateContinentPreview(int32_t timeMy);
    /// Post-swap fix-ups shared between the synchronous and async paths:
    /// FogOfWar resize, renderer dirty flag, camera world bounds + minZoom +
    /// re-fit, spectator reveal. Called after `m_hexGrid` is up-to-date.
    void applyNewHexGridFixups(int32_t timeMy);

    /// Format a creator age label as "Age <cur> / <total> Gy" when
    /// either value crosses 1000 My, otherwise "Age <cur> / <total> My".
    /// Used by the scrubber readout, the focus-input live formatter,
    /// and the play / step / total-time delta callbacks.
    static std::string formatCreatorAgeLabel(int32_t curMy, int32_t totalMy);
    /// Build the creator overlay (slider + Use This Map button) along
    /// the bottom of the screen.
    void buildContinentCreatorControls(float screenW, float screenH);

    // ---- Map Editor state ----
    enum class BrushMode : uint8_t {
        Terrain,
        Feature,
    };
    /// True while the player is in the map-editor preview mode.
    bool m_mapEditorMode = false;
    BrushMode m_editorBrushMode = BrushMode::Terrain;
    aoc::map::TerrainType m_editorBrush = aoc::map::TerrainType::Grassland;
    aoc::map::FeatureType m_editorFeatureBrush = aoc::map::FeatureType::Forest;
    int32_t m_editorBrushRadius = 1;  ///< 1..4 hex radius
    aoc::ui::WidgetId m_editorPanelId = aoc::ui::INVALID_WIDGET;
    aoc::ui::WidgetId m_editorBrushLabelId = aoc::ui::INVALID_WIDGET;
    aoc::ui::WidgetId m_editorRadiusLabelId = aoc::ui::INVALID_WIDGET;

    /// Per-action change list captured between mouse-down and the
    /// next mouse-down (so a single drag groups into one undo step).
    /// Each entry: tileIndex + previous TerrainType (or FeatureType
    /// encoded into the same byte slot since we never mix kinds in
    /// one action — see m_undoLastWasFeature).
    struct EditorChange {
        int32_t  tileIndex;
        uint8_t  oldValue;
    };
    struct EditorAction {
        std::vector<EditorChange> changes;
        bool isFeature = false;
    };
    std::vector<EditorAction> m_editorUndoStack;
    bool m_editorMouseDownLast = false;
    EditorAction m_editorCurrentAction;

    /// Set true when "Use This Map" is pressed in the editor; the next
    /// startGame() reuses the in-memory m_hexGrid instead of running
    /// MapGenerator. Cleared after consumption.
    bool m_useExistingGridOnNextStart = false;

    /// Build the editor overlay (palette + Save/Use/Back buttons).
    void buildMapEditorControls(float screenW, float screenH);

    /// Persist current game state to a /tmp file keyed by turn so the seek
    /// slider can reload older turns on backwards scrub.
    void spectatorMaybeSnapshot();

    /// Restore the game state from the newest snapshot at or before the
    /// requested turn.  Returns true on success.  Caller is responsible for
    /// replaying turns from there to the exact requested turn.
    [[nodiscard]] bool spectatorRestoreSnapshot(int32_t turn);

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
