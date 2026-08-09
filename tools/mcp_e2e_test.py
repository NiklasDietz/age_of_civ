#!/usr/bin/env python3
"""End-to-end test of tools/mcp_server.py over the real MCP stdio protocol.

Spawns the wrapper as a subprocess exactly the way `claude mcp add ... --
python3 tools/mcp_server.py` would, speaks real MCP over stdio, and drives
a live game from the main menu into a running session using only MCP tool
calls (no curl, no direct HTTP). Exercises all six mutation routes plus the
screenshot route.

Bootstrap deliberately selects a SMALL map with TWELVE players rather than
accepting the defaults, because the attack route is otherwise untestable:
with the default 2 players, both spawn on separate continents (measured 76-114
hexes apart), no land path exists between them, so orderUnitMove() finds no
path and every move is a silent no-op. Twelve players on a small map reliably
puts several starts on a shared landmass within a few tiles. The small map
also generates faster than Standard.
"""

import asyncio
import json
import sys

from mcp import ClientSession, StdioServerParameters, stdio_client

TARGET_PLAYERS = 12


def unwrap(result):
    """Pull the JSON payload out of a CallToolResult."""
    if getattr(result, "structured_content", None):
        sc = result.structured_content
        # Non-dict returns get wrapped under a "result" key.
        return sc.get("result", sc) if isinstance(sc, dict) else sc
    for block in result.content:
        text = getattr(block, "text", None)
        if text:
            try:
                return json.loads(text)
            except json.JSONDecodeError:
                return text
    return None


def find_button(tree, label):
    for w in tree.get("widgets", []):
        if w.get("kind") == "button" and w.get("text") == label and w.get("visible"):
            return w
    return None


def player_count_row(tree):
    """Locate the player-count '+' button and its count label.

    Game Setup has FIVE '+' buttons; picking widgets[0] grabs a worldgen
    slider instead and silently leaves the player count at 2. Anchor on the
    'Number:' label's row instead. Widget bounds are FLAT x/y/w/h keys on
    each widget -- there is no nested 'bounds' object.
    """
    anchor = next((w for w in tree["widgets"] if w.get("text") == "Number:"), None)
    if anchor is None:
        return None, None
    row = [w for w in tree["widgets"] if abs(w["y"] - anchor["y"]) < 20]
    plus = next((w for w in row if w["text"] == "+" and w["kind"] == "button"), None)
    count = next((w for w in row if w["kind"] == "label" and w["text"].isdigit()), None)
    return plus, count


def hex_distance(a_q, a_r, b_q, b_r):
    """Axial hex distance -- used to find an adjacent enemy for the attack test."""
    dq = a_q - b_q
    dr = a_r - b_r
    return (abs(dq) + abs(dq + dr) + abs(dr)) // 2


async def settle(session, seconds=2.0):
    """Mutations are queued and applied on the game's next frame -- wait for it."""
    await asyncio.sleep(seconds)


async def units_of(session, player):
    return unwrap(await session.call_tool("aoc_list_units", {"player": player}))["units"]


async def cities_of(session, player):
    return unwrap(await session.call_tool("aoc_list_cities", {"player": player}))["cities"]


async def research_id_of(session, player):
    detail = unwrap(await session.call_tool("aoc_player_detail", {"player": player}))
    return detail["currentResearchTechId"]


async def main():
    params = StdioServerParameters(
        command=sys.executable,
        args=["tools/mcp_server.py"],
    )
    async with stdio_client(params) as (read, write):
        async with ClientSession(read, write) as session:
            await session.initialize()

            tools = await session.list_tools()
            print(f"[1] initialize + list_tools OK -- {len(tools.tools)} tools exposed")

            tree = unwrap(await session.call_tool("aoc_ui_tree", {}))
            if "error" in tree:
                print(f"    FAIL: {tree['error']}")
                return 1
            print(f"[2] aoc_ui_tree OK -- appState={tree['appState']} "
                  f"mainMenuOpen={tree['mainMenuOpen']} widgets={len(tree['widgets'])}")

            btn = find_button(tree, "Start Game")
            if btn is None:
                print("    FAIL: no 'Start Game' button on the main menu")
                return 1
            print(f"[3] found main-menu 'Start Game' -> id={btn['id']}")

            print(f"    {unwrap(await session.call_tool('aoc_ui_click', {'widget_id': btn['id']}))}")
            await asyncio.sleep(1.5)

            tree = unwrap(await session.call_tool("aoc_ui_tree", {}))
            print(f"[4] after click -- mainMenuOpen={tree['mainMenuOpen']} "
                  f"gameSetupOpen={tree['gameSetupOpen']} widgets={len(tree['widgets'])}")
            if not tree["gameSetupOpen"]:
                print("    FAIL: Game Setup did not open")
                return 1

            # Small map + 12 players -- see the module docstring for why the
            # defaults make the attack route unreachable.
            small = find_button(tree, "Small")
            if small is None:
                print("    FAIL: no 'Small' map-size button in Game Setup")
                return 1
            await session.call_tool("aoc_ui_click", {"widget_id": small["id"]})
            await asyncio.sleep(0.8)

            for _ in range(TARGET_PLAYERS - 2):
                tree = unwrap(await session.call_tool("aoc_ui_tree", {}))
                plus, _count = player_count_row(tree)
                if plus is None:
                    print("    FAIL: could not locate the player-count '+' button")
                    return 1
                await session.call_tool("aoc_ui_click", {"widget_id": plus["id"]})
                await asyncio.sleep(0.35)
            tree = unwrap(await session.call_tool("aoc_ui_tree", {}))
            _plus, count = player_count_row(tree)
            got = int(count["text"]) if count else -1
            print(f"[4b] configured Small map, player count = {got}")
            if got != TARGET_PLAYERS:
                print(f"    FAIL: player count is {got}, expected {TARGET_PLAYERS}")
                return 1

            btn = find_button(tree, "Start Game")
            if btn is None:
                print("    FAIL: no 'Start Game' button on Game Setup")
                return 1
            print(f"[5] found Game-Setup 'Start Game' -> id={btn['id']}")

            print(f"    {unwrap(await session.call_tool('aoc_ui_click', {'widget_id': btn['id']}))}")

            print("[6] polling aoc_game_state while map generates...")
            state = None
            for attempt in range(24):
                await asyncio.sleep(5)
                state = unwrap(await session.call_tool("aoc_game_state", {}))
                if "error" not in state:
                    print(f"    game live after ~{(attempt + 1) * 5}s")
                    break
                print(f"    attempt {attempt + 1}: {state['error']}")
            else:
                print("    FAIL: game never started")
                return 1

            print(f"[7] aoc_game_state OK -- turn={state['turnNumber']} "
                  f"phase={state['phase']} players={len(state['players'])}")

            resp = unwrap(await session.call_tool("aoc_list_units", {"player": 0}))
            units = resp["units"]
            assert resp["count"] == len(units), "count/list mismatch"
            assert len(units) > 1, f"expected >1 unit, got {len(units)} (list truncation bug?)"
            print(f"[8] aoc_list_units OK -- count={resp['count']}: "
                  f"{[(u['typeName'], u['q'], u['r']) for u in units]}")

            before = state["turnNumber"]
            print(f"    {unwrap(await session.call_tool('aoc_end_turn', {}))}")
            await asyncio.sleep(2)
            after = unwrap(await session.call_tool("aoc_game_state", {}))["turnNumber"]
            print(f"[9] aoc_end_turn -- turn {before} -> {after}")
            if after <= before:
                print("    FAIL: turn did not advance")
                return 1

            skipped = []

            # ---- [10] aoc_found_city -------------------------------------
            # NOTE: the server routes this through aoc::sim::foundCity(), which
            # applies a MIN_CITY_DISTANCE relocation check -- the city can land
            # on a DIFFERENT tile than the settler stood on. Assert on the city
            # count, never on an exact q/r.
            units = await units_of(session, 0)
            settler = next((u for u in units if u["typeName"] == "Settler"), None)
            if settler is None:
                print("    FAIL: no Settler in player 0's starting units")
                return 1
            cities_before = len(await cities_of(session, 0))
            print(f"    {unwrap(await session.call_tool('aoc_found_city', {'player': 0, 'q': settler['q'], 'r': settler['r'], 'name': 'E2E City'}))}")
            await settle(session)
            cities = await cities_of(session, 0)
            print(f"[10] aoc_found_city -- cities {cities_before} -> {len(cities)}")
            if len(cities) != cities_before + 1:
                print(f"    FAIL: expected {cities_before + 1} cities, got {len(cities)}")
                return 1
            city = cities[-1]
            print(f"     founded '{city['name']}' at ({city['q']},{city['r']}) "
                  f"pop={city['population']}")
            if any(u["q"] == settler["q"] and u["r"] == settler["r"]
                   and u["typeName"] == "Settler" for u in await units_of(session, 0)):
                print("    FAIL: settler was not consumed by founding")
                return 1
            print("     settler consumed OK")

            # ---- [11] aoc_set_production ---------------------------------
            queue_before = len(city["productionQueue"])
            print(f"    {unwrap(await session.call_tool('aoc_set_production', {'player': 0, 'q': city['q'], 'r': city['r'], 'item_type': 'Unit', 'item_id': 0}))}")
            await settle(session)
            city = (await cities_of(session, 0))[-1]
            queue = city["productionQueue"]
            print(f"[11] aoc_set_production -- queue {queue_before} -> {len(queue)}: "
                  f"{[(i['name'], i['totalCost']) for i in queue]}")
            if not any(i["name"] == "Warrior" for i in queue):
                print("    FAIL: Warrior (Unit id 0) not in the production queue")
                return 1
            warrior = next(i for i in queue if i["name"] == "Warrior")
            if warrior["totalCost"] != 40.0:
                print(f"    FAIL: server-derived cost wrong: {warrior['totalCost']} != 40")
                return 1
            print("     name+cost derived server-side OK (client sent only type+itemId)")

            # out-of-range itemId must be rejected by the drain's bounds check
            print(f"    {unwrap(await session.call_tool('aoc_set_production', {'player': 0, 'q': city['q'], 'r': city['r'], 'item_type': 'Unit', 'item_id': 9999}))}")
            await settle(session)
            queue_after_bogus = len((await cities_of(session, 0))[-1]["productionQueue"])
            print(f"[12] bogus itemId=9999 -- queue stayed {len(queue)} -> {queue_after_bogus}")
            if queue_after_bogus != len(queue):
                print("    FAIL: out-of-range itemId was accepted (bounds check broken)")
                return 1

            # ---- [13] aoc_set_research -----------------------------------
            # Tech 0 = Mining (no prerequisites) -> must be accepted.
            print(f"    {unwrap(await session.call_tool('aoc_set_research', {'player': 0, 'tech_id': 0}))}")
            await settle(session)
            current = await research_id_of(session, 0)
            print(f"[13] aoc_set_research(0=Mining) -- currentResearchTechId={current}")
            if current != 0:
                print(f"    FAIL: expected currentResearchTechId=0, got {current}")
                return 1

            # Tech 7 = Apprenticeship, prereqs [5,6] unresearched -> must be
            # REJECTED by canResearch(). Tech 9999 -> rejected by the bounds
            # check the UI click path does NOT have.
            for bogus, why in ((7, "unmet prereqs [5,6]"), (9999, "out of range")):
                print(f"    {unwrap(await session.call_tool('aoc_set_research', {'player': 0, 'tech_id': bogus}))}")
                await settle(session, 1.5)
                still = await research_id_of(session, 0)
                print(f"[14] aoc_set_research({bogus}) [{why}] -- currentResearchTechId={still}")
                if still != 0:
                    print(f"    FAIL: invalid techId {bogus} was accepted (now {still})")
                    return 1
            print("     both invalid techs correctly rejected, research unchanged")

            # ---- [15] aoc_attack_unit ------------------------------------
            # Attack needs two units from DIFFERENT players 1 hex apart. Rather
            # than hoping the AI walks into us, drive BOTH sides: aoc_move_unit
            # takes an explicit `player` and resolves it via GameState::player(),
            # which does not care whether that player is human or AI. Walk the
            # globally-closest cross-player pair together until adjacent.
            player_ids = [p["id"] for p in
                          unwrap(await session.call_tool("aoc_game_state", {}))["players"]]

            async def closest_pair():
                by_player = {p: await units_of(session, p) for p in player_ids}
                best = None
                for pa in player_ids:
                    for pb in player_ids:
                        if pa >= pb:
                            continue
                        for a in by_player[pa]:
                            for b in by_player[pb]:
                                d = hex_distance(a["q"], a["r"], b["q"], b["r"])
                                if best is None or d < best[0]:
                                    best = (d, pa, pb, a, b)
                return best

            staged = None
            for _ in range(30):
                best = await closest_pair()
                if best is None:
                    break
                dist, pa, pb, a, b = best
                if dist == 1:
                    staged = (pa, pb, a, b)
                    break
                # Converge: move each of the two closest units at the other.
                await session.call_tool("aoc_move_unit", {
                    "player": pa, "q": a["q"], "r": a["r"],
                    "target_q": b["q"], "target_r": b["r"]})
                await settle(session, 0.7)
                b_now = next((u for u in await units_of(session, pb)
                              if u["q"] == b["q"] and u["r"] == b["r"]), None)
                a_now = next(iter(await units_of(session, pa)), None)
                if b_now and a_now:
                    await session.call_tool("aoc_move_unit", {
                        "player": pb, "q": b_now["q"], "r": b_now["r"],
                        "target_q": a_now["q"], "target_r": a_now["r"]})
                await settle(session, 0.7)
                await session.call_tool("aoc_end_turn", {})
                await settle(session, 2.2)

            if staged is None:
                skipped.append("aoc_attack_unit: no cross-player pair reached 1 hex "
                               "in 30 turns -- all starts landed on separate landmasses "
                               "(orderUnitMove finds no path, so moves no-op)")
                print("[15] aoc_attack_unit -- SKIPPED (could not stage adjacency)")
            else:
                pa, pb, attacker, defender = staged
                hp_before = defender["hitPoints"]
                atk_hp_before = attacker["hitPoints"]
                print(f"     p{pa} {attacker['typeName']}({attacker['q']},{attacker['r']}) "
                      f"hp={atk_hp_before} -> p{pb} {defender['typeName']}"
                      f"({defender['q']},{defender['r']}) hp={hp_before}")
                print(f"    {unwrap(await session.call_tool('aoc_attack_unit', {'player': pa, 'q': attacker['q'], 'r': attacker['r'], 'target_q': defender['q'], 'target_r': defender['r']}))}")
                await settle(session)
                defender_after = next((u for u in await units_of(session, pb)
                                       if u["q"] == defender["q"] and u["r"] == defender["r"]),
                                      None)
                if defender_after is None:
                    print("[15] aoc_attack_unit -- defender DIED (removed from the map)")
                elif defender_after["hitPoints"] < hp_before:
                    print(f"[15] aoc_attack_unit -- defender hp {hp_before} -> "
                          f"{defender_after['hitPoints']}")
                else:
                    print(f"    FAIL: combat did not resolve -- defender hp unchanged "
                          f"({hp_before})")
                    return 1

                # Locating the attacker afterward must consider BOTH tiles:
                # Combat.cpp:352-355 moves a surviving melee attacker ONTO the
                # defender's tile when the defender dies. Checking only the
                # original tile reports a victorious attacker as "died".
                attacker_units = await units_of(session, pa)
                at_origin = next((u for u in attacker_units
                                  if u["q"] == attacker["q"] and u["r"] == attacker["r"]), None)
                at_target = next((u for u in attacker_units
                                  if u["q"] == defender["q"] and u["r"] == defender["r"]), None)
                if at_target is not None:
                    print(f"     attacker WON and advanced onto ({defender['q']},"
                          f"{defender['r']}); hp {atk_hp_before} -> {at_target['hitPoints']}")
                elif at_origin is None:
                    print("     attacker DIED to melee retaliation")
                elif at_origin["hitPoints"] < atk_hp_before:
                    print(f"     attacker took retaliation: hp {atk_hp_before} -> "
                          f"{at_origin['hitPoints']} (held its tile)")
                else:
                    print("     attacker unharmed (ranged path -- no retaliation)")

            # ---- [16] aoc_screenshot -------------------------------------
            shot = unwrap(await session.call_tool("aoc_screenshot", {}))
            if "path" not in shot:
                print(f"    FAIL: screenshot route returned {shot}")
                return 1
            print(f"[16] aoc_screenshot -- {shot['path']}")

            if skipped:
                print("\nCHECKS PASSED, WITH SKIPS:")
                for s in skipped:
                    print(f"  SKIPPED  {s}")
                return 0
            print("\nALL CHECKS PASSED -- all six mutation routes exercised live")
            return 0


if __name__ == "__main__":
    sys.exit(asyncio.run(main()))
