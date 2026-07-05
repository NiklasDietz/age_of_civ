# Component: ui

## Responsibility

Owns the entire UI widget tree, all in-game screens (menu, diplomacy, religion, trade,
encyclopedia, map editor, …), font rendering, theme tokens, localization, and the
screen lifecycle registry. Interactive only; not compiled in headless builds.

## Key files

- [include/aoc/ui/UIManager.hpp](../../../include/aoc/ui/UIManager.hpp) — `UIManager`:
  manages the widget tree stored as a flat vector with parent/children index links.
  Creates panels, buttons, labels, scroll lists, tab bars, progress bars, sliders, icons,
  rich text, portraits, and markdown widgets. Layout computed top-down; rendering
  back-to-front. Routes mouse/keyboard input to focused widgets.
- [include/aoc/ui/Widget.hpp](../../../include/aoc/ui/Widget.hpp) — `Widget` struct:
  type tag, bounds `Rect`, per-type data union, children indices, enabled/visible flags.
  `WidgetId` is an index into `UIManager`'s flat vector.
- [include/aoc/ui/IScreen.hpp](../../../include/aoc/ui/IScreen.hpp) — `IScreen`:
  abstract interface for a full-screen UI mode (`onEnter`, `onExit`, `onUpdate`,
  `onRender`).
- [include/aoc/ui/ScreenRegistry.hpp](../../../include/aoc/ui/ScreenRegistry.hpp) —
  `ScreenRegistry`: maps `ScreenId` enums to `IScreen` instances and drives push/pop
  navigation (e.g. Main Menu → New Game → Loading → In-Game → Pause Menu).
- [include/aoc/ui/LayoutBuilder.hpp](../../../include/aoc/ui/LayoutBuilder.hpp) —
  Fluent builder for programmatic widget layout.
- [include/aoc/ui/BitmapFont.hpp](../../../include/aoc/ui/BitmapFont.hpp) — Rasterizes
  TrueType fonts to a bitmap atlas via `stb_truetype`. **Security note:** uses
  stb_truetype v1.26 with unpatched CVE-2026-5314 OOB-read on hostile fonts; only
  bundled system fonts are ever fed to it — never mod/user-supplied fonts.
- [include/aoc/ui/Theme.hpp](../../../include/aoc/ui/Theme.hpp) /
  [StyleTokens.hpp](../../../include/aoc/ui/StyleTokens.hpp) — color palette and
  spacing tokens consumed by all widget draw paths.
- [include/aoc/ui/Localization.hpp](../../../include/aoc/ui/Localization.hpp) — Runtime
  string lookup by key; locale loaded from `data/` at startup.
- [include/aoc/ui/Tooltip.hpp](../../../include/aoc/ui/Tooltip.hpp) — Hover-delay popup
  showing contextual info for map tiles, units, and buildings.
- [include/aoc/ui/EventLog.hpp](../../../include/aoc/ui/EventLog.hpp) /
  [Notifications.hpp](../../../include/aoc/ui/Notifications.hpp) — In-game event log
  feed and transient notification banners (city founded, tech researched, etc.).
- [include/aoc/ui/UITestHarness.hpp](../../../include/aoc/ui/UITestHarness.hpp) /
  [WidgetInspector.hpp](../../../include/aoc/ui/WidgetInspector.hpp) — Development-only
  widget inspection and layout testing tools.
- [include/aoc/ui/UIPersistence.hpp](../../../include/aoc/ui/UIPersistence.hpp) —
  Saves and restores UI panel positions/sizes across sessions.

### Screen classes

All located in `src/ui/` and `include/aoc/ui/`:

`MainMenu`, `LoadingScreen`, `GameScreens` (in-game HUD), `PauseMenu`, `DiplomacyScreen`,
`ReligionScreen`, `TradeScreen`, `TradeRouteSetupScreen`, `ScoreScreen`, `Encyclopedia`,
`MapEditor`, `SettingsMenu`, `SpectatorHUD`, `Tutorial`, `AdvancedTutorial`,
`CityDetailTabs`.

## Public surface

- `UIManager` — created by `Application`; widgets added by each `IScreen` on enter.
- `ScreenRegistry` — driven by `Application` for screen transitions.
- `Localization::get(key)` — called from all screen/widget code that displays text.
- `GameDBus` (in `src/ui/GameDBus.cpp`) — D-Bus IPC for Linux desktop integration
  (taskbar progress, rich presence); compiled only when sdbus-cpp is found.

## Internal structure

Flat directory. Screens are registered in `ScreenRegistry`; each screen builds its
widget subtree via `UIManager` calls on `onEnter` and tears it down on `onExit`. Widgets
are value types stored contiguously; `UIManager` is the allocator and lifetime owner.
