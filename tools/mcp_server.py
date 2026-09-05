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
    """Attack with the unit owned by `player` at (q, r) against the unit of another seat
    occupying (target_q, target_r). Same validated request as the in-game right-click:
    melee needs an adjacent target and movement left, ranged a target within its range
    and movement left (either spends the unit's movement), aircraft fly a bombing run
    within their operational range while they have a sortie (patrolling enemy fighters
    in range may intercept). Queues the request; a rejection (no enemy there, out of
    reach, no movement or sortie) is logged in the game log. Poll aoc_list_units
    afterward to see HP/death.
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
def aoc_activate_great_person(player: int, q: int, r: int) -> dict:
    """Activate the Great Person unit owned by `player` standing at hex (q, r), where it
    stands. Queues the request; a unit that is not an unused Great Person is rejected in the
    game log. The unit is consumed on success.
    """
    return _post("/game/greatperson/activate", player=player, q=q, r=r)


@mcp.tool()
def aoc_congress_vote(player: int, weight: int) -> dict:
    """Replace `player`'s vote on the open World Congress proposal with a signed weight
    from -4 to 4 (0 abstains). Every seat's vote is cast automatically when a resolution
    is proposed and tallied at the next end of turn, so this is the window to change it.
    The first point of weight is free; each extra costs 10 favor, and extras the automatic
    vote bought are refunded first. Queues the request; no open proposal or too little
    favor is rejected in the game log.
    """
    return _post("/game/congress/vote", player=player, weight=weight)


@mcp.tool()
def aoc_congress_propose(player: int, resolution: int, target: int = 255) -> dict:
    """Register what `player` proposes the next time it is chosen as proposer (the seat
    with the most favor, at least 30). Resolution ids: 0 BanNuclearWeapons,
    1 GlobalSanctions (needs a living rival as `target`), 2 WorldsFair,
    3 InternationalGames, 4 ArmsReduction, 5 ClimateAccord; 6 clears the registration.
    Queues the request; an invalid target is rejected in the game log.
    """
    return _post("/game/congress/propose", player=player, resolution=resolution, target=target)


@mcp.tool()
def aoc_merge_units(player: int, q: int, r: int, source_q: int, source_r: int) -> dict:
    """Merge the unit owned by `player` at (source_q, source_r) into its same-type unit on the
    adjacent tile (q, r): a single unit becomes a Corps (Fleet for ships) once the Nationalism
    civic is complete, a Corps becomes an Army (Armada) once Mobilization is; +15% / +25%
    strength. The source unit is consumed. Queues the request; a rejection (different type,
    not adjacent, missing civic, already an Army) is logged in the game log.
    """
    return _post("/game/unit/merge", player=player, q=q, r=r, sourceQ=source_q, sourceR=source_r)


@mcp.tool()
def aoc_assign_governor(player: int, q: int, r: int, governor_type: int) -> dict:
    """Seat a named governor in the city owned by `player` at (q, r). Types: 1 Financier
    (+20% gold), 2 Industrialist (+15% production), 3 Diplomat (+8 loyalty), 4 General,
    5 Scholar (+15% science), 6 Merchant (+10% gold), 7 Environmentalist. Titles come one per
    five completed civics; recruiting a new governor costs one, moving a seated one is free.
    Queues the request; a rejection (no title, unknown city) is logged in the game log.
    """
    return _post("/game/governor/assign", player=player, q=q, r=r, type=governor_type)


@mcp.tool()
def aoc_promote_governor(player: int, q: int, r: int, promotion: int) -> dict:
    """Buy a title (1 to 35, five per governor in type order: 1-5 Financier, 6-10
    Industrialist, 11-15 Diplomat, 16-20 General, 21-25 Scholar, 26-30 Merchant, 31-35
    Environmentalist) for the governor seated in the city at (q, r). Titles with an effect
    today: 1 Tax Haven +10% gold, 6 Automated Factory +10% production, 13 Peace Keeper +10
    favor/turn, 16 Citadel +4 loyalty, 21 Research Grant +10% science, 35 Carbon Credit +5
    favor/turn. Costs one title; three per governor at most. Queues the request.
    """
    return _post("/game/governor/promote", player=player, q=q, r=r, promotion=promotion)


@mcp.tool()
def aoc_slot_policy(player: int, slot: int, policy: int) -> dict:
    """Put policy card `policy` (0-35; -1 clears) into policy slot `slot` (0-5) for `player`.
    Slots run Military, Economic, Diplomatic, then Wildcard (a wildcard takes any card).
    The card must be unlocked by a civic and not slotted elsewhere; anarchy blocks it.
    Slotting is free on a turn after a civic completed, else 50 gold; clearing is free.
    Queues the request; a rejection is logged in the game log.
    """
    return _post("/game/policy/slot", player=player, slot=slot, policy=policy)


@mcp.tool()
def aoc_change_government(player: int, government: int) -> dict:
    """Adopt an unlocked government: 0 Chiefdom, 1 Autocracy, 2 Oligarchy, 3 Monarchy,
    4 Democracy, 5 Communism, 6 Fascism, 7 Theocracy, 8 Merchant Republic. Leaving
    Chiefdom is free; every later change costs 5 turns of anarchy (no bonuses, slots
    cleared) and there are 10 turns between changes. Queues the request.
    """
    return _post("/game/government/change", player=player, government=government)


@mcp.tool()
def aoc_purchase(player: int, q: int, r: int, item_type: int, item_id: int, faith: bool = False) -> dict:
    """Buy in the city at (q, r): item_type 0 = unit, 1 = building (gold, 4x production cost,
    same tech/civic/district gates as building it). With faith=True buy a religious unit
    (19 Missionary, 20 Apostle, 21 Inquisitor) for production x 2 faith; needs a founded
    religion. Queues the request; a rejection is logged in the game log.
    """
    return _post("/game/city/purchase", player=player, q=q, r=r, type=item_type, item=item_id,
                 faith=1 if faith else 0)


@mcp.tool()
def aoc_set_city_focus(player: int, q: int, r: int, focus: int) -> dict:
    """Set the citizen focus of the city at (q, r): 0 Balanced, 1 Growth, 2 Production,
    3 Science, 4 Gold, 5 Military. Worked tiles are re-assigned for it. Queues the request.
    """
    return _post("/game/city/focus", player=player, q=q, r=r, focus=focus)


@mcp.tool()
def aoc_lock_tile(player: int, q: int, r: int, tile_q: int, tile_r: int) -> dict:
    """Pin or unpin tile (tile_q, tile_r) for the city at (q, r); a pinned tile keeps its
    citizen through re-assignment (pinning works it when a citizen is free). Queues the request.
    """
    return _post("/game/city/lock-tile", player=player, q=q, r=r, tq=tile_q, tr=tile_r)


@mcp.tool()
def aoc_remove_queue_item(player: int, q: int, r: int, index: int) -> dict:
    """Drop entry `index` (0 = head) from the production queue of the city at (q, r); its
    progress is lost. Queues the request.
    """
    return _post("/game/city/queue/remove", player=player, q=q, r=r, index=index)


@mcp.tool()
def aoc_queue_project(player: int, q: int, r: int, project: int) -> dict:
    """Queue a repeatable city project in the city at (q, r): 0 Bread and Circuses (+20
    loyalty, City Center), 1 Campus Research Grant (+50 science, Campus), 2 Industrial Surge
    (+50 production, Industrial), 3 Commercial Investment (+100 gold, Commercial), 4 Shipyard
    Rush (Harbor), 5 Military Training (Encampment). The city needs the district. Queues the
    request.
    """
    return _post("/game/city/project", player=player, q=q, r=r, project=project)


@mcp.tool()
def aoc_builder_improve(player: int, q: int, r: int, improvement_type: int) -> dict:
    """Place an improvement (ImprovementType value: 1 Farm, 2 Mine, ... see the Civilopedia)
    with the Builder or Military Engineer standing on (q, r). Builders place terrain
    improvements their techs allow; Military Engineers place Road, Railway (Industrialization)
    and Fort. Spends a charge. Queues the request; a rejection is logged.
    """
    return _post("/game/builder/improve", player=player, q=q, r=r, type=improvement_type)


@mcp.tool()
def aoc_builder_chop(player: int, q: int, r: int) -> dict:
    """Chop the Forest (needs Mining), Jungle (Bronze Working) or Marsh under the Builder on
    (q, r): the nearest own city within 3 tiles that is building something gets 20 + 10 per era
    production. Spends a charge. Queues the request.
    """
    return _post("/game/builder/chop", player=player, q=q, r=r)


@mcp.tool()
def aoc_builder_harvest(player: int, q: int, r: int) -> dict:
    """Harvest the bonus resource under the Builder on (q, r): the nearest own city within 3
    tiles gets 20 + 10 per era food; the resource is gone. Spends a charge. Queues the request.
    """
    return _post("/game/builder/harvest", player=player, q=q, r=r)


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
