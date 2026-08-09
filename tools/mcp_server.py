#!/usr/bin/env python3
"""MCP wrapper around the Age of Civilization debug-server game-control API.

Lets an MCP-capable client (e.g. Claude Code) query and drive an
already-running game session: end turns, move/attack units, found cities,
set production/research, and read back turn/player/unit/city state.

This is a thin proxy -- all game logic and validation live in the C++
`aoc::debug::DebugServer` (default `http://127.0.0.1:9876`, opt-in via
`--enable-debug-server` on the game process). Mutation tools return
`{"queued": true}` immediately; the game applies the command on its next
frame, so poll `aoc_game_state`/`aoc_list_units`/`aoc_list_cities` afterward
to observe the effect (matches the debug server's existing async idiom --
see `/sim/set-creator-time` in `src/app/Application.cpp`).

Setup:
    pip install mcp
    claude mcp add age-of-civ -- python3 tools/mcp_server.py

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

from mcp.server.fastmcp import FastMCP

BASE_URL = os.environ.get("AOC_DEBUG_SERVER_URL", "http://127.0.0.1:9876")
REQUEST_TIMEOUT_SECONDS = 5

mcp = FastMCP("age-of-civ")


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
    """List every unit owned by `player`, with position, HP, movement, and combat stats."""
    return _get("/game/units", player=player)


@mcp.tool()
def aoc_list_cities(player: int) -> dict:
    """List every city owned by `player`, with population, food surplus, and production queue."""
    return _get("/game/cities", player=player)


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


if __name__ == "__main__":
    mcp.run()
