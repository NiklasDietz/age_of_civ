#!/usr/bin/env python3
"""MCP wrapper around the Age of Civilization debug-server API.

Lets an MCP-capable client (e.g. Claude Code) drive a running game end to
end: navigate the UI (main menu -> Game Setup -> Start Game) via the
widget-tree/click tools, then query and control the resulting session --
end turns, move/attack units, found cities, set production/research, and
read back turn/player/unit/city state.

This is a thin proxy -- all game logic and validation live in the C++
`aoc::debug::DebugServer` (default `http://127.0.0.1:9876`, opt-in via
`--enable-debug-server` on the game process). Mutation tools return
`{"queued": true}` immediately; the game applies the command on its next
frame, so poll the matching read tool afterward to observe the effect
(matches the debug server's existing async idiom -- see
`/sim/set-creator-time` in `src/app/Application.cpp`).

Typical bootstrap from a cold main menu:
    1. aoc_ui_tree()                     -> find the button whose text is
                                            "Start Game", note its id
    2. aoc_ui_click(widget_id=<id>)      -> opens Game Setup
    3. aoc_ui_tree()                     -> find Game Setup's own
                                            "Start Game" button id
    4. aoc_ui_click(widget_id=<id>)      -> starts the game
    5. poll aoc_game_state() until it stops returning "no active game"
       -- map generation is synchronous and takes SEVERAL SECONDS on a
       large map, so retry with a real timeout rather than once.

Setup (verified 2026-08-09 against mcp 2.0.0 / Python 3.13):
    pip install mcp                      # REQUIRES mcp >= 2.0
    claude mcp add age-of-civ -- python3 tools/mcp_server.py

Use the interpreter that actually has `mcp` installed -- if it lives in a
venv, point at that venv's python explicitly, e.g.
    claude mcp add age-of-civ -- ~/venv/bin/python tools/mcp_server.py
Verify with `claude mcp list` (should report "Connected"). Note MCP servers
are loaded at session start, so restart the client after adding.

API-version note: mcp 2.0 REMOVED `mcp.server.fastmcp.FastMCP`; this file
uses `mcp.server.MCPServer`, which is the 2.x replacement. On mcp 1.x the
import will fail -- upgrade rather than rewriting back to FastMCP.

The target game process must be launched with `--enable-debug-server` for
any of this to work; that flag stays opt-in by design (see
DEPENDENCIES.txt's cpp-httplib security note) -- this wrapper does not and
should not change that default.

Override the debug server's base URL with the AOC_DEBUG_SERVER_URL env var
(default http://127.0.0.1:9876).
"""

import json
import os
import urllib.error
import urllib.parse
import urllib.request

from mcp.server import MCPServer

BASE_URL = os.environ.get("AOC_DEBUG_SERVER_URL", "http://127.0.0.1:9876")
REQUEST_TIMEOUT_SECONDS = 5

mcp = MCPServer("age-of-civ")


def _request(method: str, path: str, params: dict) -> dict:
    """Issue an HTTP request against the debug server and return parsed JSON.

    Never raises: connection failures and non-2xx responses are folded into
    an {"error": ...} dict so a tool call always returns something a client
    can display, instead of crashing the wrapper process.
    """
    query = {key: value for key, value in params.items() if value is not None}
    url = f"{BASE_URL}{path}"
    if query:
        url += "?" + urllib.parse.urlencode(query)

    request = urllib.request.Request(url, method=method)
    try:
        with urllib.request.urlopen(request, timeout=REQUEST_TIMEOUT_SECONDS) as response:
            body = response.read().decode("utf-8")
    except urllib.error.HTTPError as exc:
        # The debug server puts a real JSON error body on non-2xx responses
        # too (e.g. 503 {"error":"no active game"}) -- surface that instead
        # of the raw HTTP exception.
        body = exc.read().decode("utf-8")
    except urllib.error.URLError as exc:
        return {
            "error": f"cannot reach Age of Civilization debug server at {BASE_URL}: {exc.reason}. "
                     "Is the game running with --enable-debug-server?"
        }

    try:
        return json.loads(body)
    except json.JSONDecodeError:
        return {"error": f"non-JSON response from debug server: {body[:200]}"}


def _get(path: str, **params) -> dict:
    return _request("GET", path, params)


def _post(path: str, **params) -> dict:
    return _request("POST", path, params)


def _wrap_list(payload, key: str) -> dict:
    """Normalize a JSON-array response into an object under `key`.

    MCP structured output requires the tool's return value to match its
    annotation. Returning a bare list from a `-> dict` tool makes the SDK
    drop structured_content entirely and emit ONE CONTENT BLOCK PER
    ELEMENT, so a client that reads content[0] silently sees only the
    first item. Error responses are already objects and pass through.
    """
    if isinstance(payload, list):
        return {key: payload, "count": len(payload)}
    return payload


# ---------------------------------------------------------------------------
# UI control -- works at the main menu AND in-game, unlike the /game/* tools
# below which require an already-running session. Use these to get FROM the
# main menu INTO a game in the first place.
# ---------------------------------------------------------------------------


@mcp.tool()
def aoc_ui_tree() -> dict:
    """Get the current UI widget tree plus which screens are open.

    Returns appState ("MainMenu"/"InGame"), mainMenuOpen/gameSetupOpen/
    settingsMenuOpen flags, and a widgets array where each entry has
    id, kind ("button"/"label"/"listrow"/...), text, bounds (x/y/w/h),
    visible, and disabled. Find a control by its `text`, then act on it
    with aoc_ui_click using its `id`.
    """
    return _get("/ui/tree")


@mcp.tool()
def aoc_ui_click(widget_id: int) -> dict:
    """Click the widget with the given id (from aoc_ui_tree).

    Works for buttons, icons, and list rows. Disabled widgets are ignored.
    For tab bars and sliders -- which are position-dependent -- use
    aoc_ui_click_at instead. Queues the click; call aoc_ui_tree afterward
    to see the resulting screen.
    """
    return _post("/ui/click", widgetId=widget_id)


@mcp.tool()
def aoc_ui_click_at(x: int, y: int) -> dict:
    """Click at a screen coordinate, simulating a real mouse press+release.

    Use when the target is position-dependent (picking a specific tab in a
    tab bar, dragging a slider) rather than a whole widget. Coordinates
    come from a widget's x/y/w/h in aoc_ui_tree.
    """
    return _post("/ui/click-at", x=x, y=y)


@mcp.tool()
def aoc_ui_scroll(x: int, y: int, delta: int, shift: bool = False) -> dict:
    """Scroll at a screen coordinate (e.g. a scroll list or the tech-tree canvas).

    Negative delta scrolls down/right, positive up/left. Set shift=True for
    horizontal panning on widgets that support it.
    """
    return _post("/ui/scroll", x=x, y=y, delta=delta, shift="1" if shift else "0")


# ---------------------------------------------------------------------------
# Game control -- all of these require an already-running session (they
# return {"error": "no active game"} at the main menu).
# ---------------------------------------------------------------------------


@mcp.tool()
def aoc_game_state() -> dict:
    """Get the current turn number, phase, active player, and a summary of every player (treasury, research, units, cities)."""
    return _get("/game/state")


@mcp.tool()
def aoc_player_detail(player: int) -> dict:
    """Get full detail for one player: treasury, income, current research, units, and cities."""
    return _get("/game/player", id=player)


@mcp.tool()
def aoc_list_units(player: int) -> dict:
    """List every unit owned by `player`, with position, HP, movement, and combat stats.

    Returns {"units": [...], "count": N}.
    """
    return _wrap_list(_get("/game/units", player=player), "units")


@mcp.tool()
def aoc_list_cities(player: int) -> dict:
    """List every city owned by `player`, with population, food surplus, and production queue.

    Returns {"cities": [...], "count": N}.
    """
    return _wrap_list(_get("/game/cities", player=player), "cities")


@mcp.tool()
def aoc_end_turn() -> dict:
    """End the current turn. Queues the request; applied on the game's next frame."""
    return _post("/game/turn/end")


@mcp.tool()
def aoc_move_unit(player: int, q: int, r: int, target_q: int, target_r: int) -> dict:
    """Move the unit owned by `player` at hex (q, r) toward hex (target_q, target_r). Queues the request."""
    return _post("/game/unit/move", player=player, q=q, r=r, targetQ=target_q, targetR=target_r)


@mcp.tool()
def aoc_attack_unit(player: int, q: int, r: int, target_q: int, target_r: int) -> dict:
    """Attack with the unit owned by `player` at (q, r) against whatever unit occupies (target_q, target_r).

    Melee vs ranged is chosen automatically from the attacker's stats.
    Queues the request; poll aoc_list_units afterward to see HP/death.
    """
    return _post("/game/unit/attack", player=player, q=q, r=r, targetQ=target_q, targetR=target_r)


@mcp.tool()
def aoc_found_city(player: int, q: int, r: int, name: str) -> dict:
    """Found a city named `name` with the Settler owned by `player` at hex (q, r). Queues the request."""
    return _post("/game/unit/found-city", player=player, q=q, r=r, name=name)


@mcp.tool()
def aoc_assign_spy_mission(player: int, q: int, r: int, mission: int) -> dict:
    """Assign mission id `mission` to the Spy unit owned by `player` standing at hex (q, r).

    Mission ids: 0 GatherIntelligence, 1 CounterIntelligence, 2 MonitorTreasury,
    3 MonitorResearch, 4 StealTechnology, 5 SabotageProduction, 6 SiphonFunds,
    7 MarketManipulation, 8 CurrencyCounterfeit, 9 SupplyChainDisrupt, 10 InsiderTrading,
    11 StealTradeSecrets, 12 RecruitPartisans, 13 FomentUnrest, 14 NeutralizeGovernor,
    15 SiphonTourism, 16 RecruitDoubleAgent, 17 EstablishEmbassy. Every mission except
    CounterIntelligence needs a rival city under the spy. Queues the request; a spy
    already on a timed mission is rejected in the game log.
    """
    return _post("/game/spy/mission", player=player, q=q, r=r, mission=mission)


@mcp.tool()
def aoc_set_production(player: int, q: int, r: int, item_type: str, item_id: int) -> dict:
    """Queue a production item onto the city owned by `player` at hex (q, r).

    item_type is one of "Unit", "Building", "District", "Wonder"; item_id
    is the numeric id within that type's definition table. Name and cost
    are looked up server-side -- this call only selects what to build.
    """
    return _post("/game/city/production", player=player, q=q, r=r, type=item_type, itemId=item_id)


@mcp.tool()
def aoc_set_research(player: int, tech_id: int) -> dict:
    """Start researching the tech with the given numeric id for `player`.

    Silently ignored by the game if the tech is already researched or a
    prerequisite is missing -- check aoc_player_detail's currentResearchTechId
    afterward to confirm it took effect.
    """
    return _post("/game/research", player=player, techId=tech_id)


@mcp.tool()
def aoc_screenshot() -> dict:
    """Capture a screenshot of the current game window and return its file path.

    Works from the main menu and in-game. The server performs a Vulkan
    swapchain readback on the render thread and writes a timestamped PNG to
    /tmp/. Returns {"path": "/tmp/aoc_screenshot_<timestamp>.png"} on success.

    Claude Code can then Read the returned path to view the PNG as an image --
    useful for verifying rendering, UI state, and hex-map appearance.
    """
    return _post("/debug/screenshot")


if __name__ == "__main__":
    mcp.run()
