#!/usr/bin/env python3
"""Outcome assertions over a headless simulation run.

Why this exists
---------------
Until 2026-09-03 every game of Age of Civilization ended with all civs
eliminated by revolution and the winner logged as "Player 255", and the test
suite was entirely green throughout. `test_determinism` compares a run against
itself, so it was green on the self-destruct; the golden hash had blessed the
broken state, so the gate actively defended it. Five defects survived the whole
life of the repo because nothing asserted that a game is *playable*, only that
it is reproducible.

This script is that missing assertion. It reads the per-player-per-turn CSV that
`aoc_simulate` already emits and checks properties a working game must have.
Determinism and the golden hash stay as they are; they answer a different
question.

Usage:
    sim_health.py <sim_log.csv>

Exit 0 if every check passes, 1 otherwise. Each failure prints the measured
value, so a red run says what broke rather than just that something did.
"""

from __future__ import annotations

import csv
import sys
from collections import defaultdict


def gini(values: list[float]) -> float:
    """Gini coefficient. 0 = perfectly equal, ->1 = one player holds everything.

    Mirrors aoc::ga::gini (ml/cpp, covered by tests/test_balance_metrics.cpp),
    which is used inside the GA fitness loop but has never gated a normal run.
    """
    if not values:
        return 0.0
    ordered = sorted(values)
    total = sum(ordered)
    if total <= 0.0:
        return 0.0
    n = len(ordered)
    weighted = sum((i + 1) * v for i, v in enumerate(ordered))
    return (2.0 * weighted) / (n * total) - (n + 1.0) / n


def load(path: str) -> list[dict[str, str]]:
    with open(path, newline="") as handle:
        return list(csv.DictReader(handle))


FOUND_BY_TURN = 5


def evaluate(rows: list[dict[str, str]], quiet: bool = False) -> int:
    """Run every health assertion over `rows`. Returns 0 if all pass."""
    if not rows:
        if not quiet:
            print("FAIL: sim log is empty")
        return 1

    # (turn, player) -> row, plus per-turn and per-player views.
    by_turn: dict[int, list[dict[str, str]]] = defaultdict(list)
    by_player: dict[int, list[dict[str, str]]] = defaultdict(list)
    for row in rows:
        turn = int(row["Turn"])
        player = int(row["Player"])
        by_turn[turn].append(row)
        by_player[player].append(row)
    for series in by_player.values():
        series.sort(key=lambda r: int(r["Turn"]))

    last_turn = max(by_turn)
    failures: list[str] = []
    notes: list[str] = []

    def check(name: str, ok: bool, detail: str) -> None:
        (notes if ok else failures).append(f"{'PASS' if ok else 'FAIL'}: {name} -- {detail}")

    # H1  Somebody is still standing. The original bug eliminated every civ.
    survivors = [r for r in by_turn[last_turn] if int(r["Cities"]) > 0]
    check(
        "a player survives to the final turn",
        len(survivors) >= 1,
        f"{len(survivors)} of {len(by_turn[last_turn])} players hold a city at turn {last_turn}",
    )

    # H2  A civ with no cities must not keep developing. processPlayerTurn used
    #     to ignore isEliminated, so a 0-city player kept researching and
    #     founding nothing while its tech count climbed.
    #
    #     Deliberately asserts techs and cities, not GDP. GDP is a derived
    #     figure recomputed from market state every turn, so for a dead player
    #     it oscillates without trending (measured: 1916, 1491, 1884, 1479 on
    #     consecutive turns). Asserting it does not rise would flag that noise
    #     as a regression. Tech count is the honest development signal: it only
    #     moves when the turn loop actually ran research for that player.
    ghosts = []
    for player, series in by_player.items():
        for prev, cur in zip(series, series[1:]):
            if int(cur["Cities"]) != 0 or int(prev["Cities"]) != 0:
                continue
            if int(cur["TechsResearched"]) > int(prev["TechsResearched"]):
                ghosts.append(f"P{player}@T{cur['Turn']}")
    check(
        "no city-less player researches a tech",
        not ghosts,
        "clean" if not ghosts else f"{len(ghosts)} violations, first {ghosts[:3]}",
    )

    # H3  The era clock advances. updateEra() had zero call sites, so this was
    #     pinned at 0 for the entire history of the repo.
    peak_era = max(int(r["Era"]) for r in rows)
    check(
        "era advances past the Ancient era",
        peak_era >= 2,
        f"peak era reached = {peak_era}",
    )

    # H4  Civs expand. Three of four used to sit on one city for the whole game.
    mid = min((t for t in by_turn if t >= min(100, last_turn)), default=last_turn)
    counts = sorted(int(r["Cities"]) for r in by_turn[mid])
    median = counts[len(counts) // 2] if counts else 0
    check(
        "median cities per player is at least 3 by mid-game",
        median >= 3,
        f"turn {mid} city counts {counts}, median {median}",
    )

    # H5  No runaway. gini over city counts catches the 5/0/1/1 spread that the
    #     old build produced without any single assertion firing.
    spread = gini([float(c) for c in counts])
    check(
        "city-count Gini below 0.5 at mid-game",
        spread < 0.5,
        f"turn {mid} Gini = {spread:.3f}",
    )

    # H6  Civs find each other. Before the shared start placement (2026-09-04)
    #     a default 2-player start could put both civs on separate continents
    #     with no land path, so diplomacy had no counterparty.
    #     MetPlayersMask always carries the player's own bit (the ML pipeline
    #     wants own data visible), so mask it out or the check can never fail;
    #     it never had until 2026-09-05.
    unmet = [
        p for p, series in by_player.items()
        if all((int(r["MetPlayersMask"]) & ~(1 << p)) == 0 for r in series)
    ]
    check(
        "every player meets at least one rival",
        not unmet,
        "all players met someone" if not unmet else f"never met anyone: {unmet}",
    )

    # H7  Trade routes form at all. TradeRouteSystem is the second-largest sim
    #     file in the repo and produced zero routes for 37 turns.
    traders = {int(r["Player"]) for r in rows if int(r["TradePartners"]) > 0}
    check(
        "at least two players establish a trade partner",
        len(traders) >= 2,
        f"players with a trade partner: {sorted(traders)}",
    )

    # H9  Every civ founds a city early. A settler that walked to a far site
    #     bled to death from supply attrition before founding (Tutorial,
    #     2026-09-04) and the civ played on as a ghost with 0 cities and 0 units.
    def founded_early(series: list[dict[str, str]]) -> bool:
        early = [r for r in series if int(r["Turn"]) <= FOUND_BY_TURN] or series[:1]
        return any(int(r["Cities"]) > 0 for r in early)

    late = [p for p, series in sorted(by_player.items()) if not founded_early(series)]
    check(
        f"every player founds a city by turn {FOUND_BY_TURN}",
        not late,
        f"players without a city by turn {FOUND_BY_TURN}: {late}" if late else "all founded",
    )

    # H10 Barbarians exist. Until 2026-09-05 BARBARIAN_PLAYER (255) routed into
    #     the city-state branch of GameState::player() and returned null, so no
    #     encampment or barbarian unit ever appeared in any game. The column is
    #     the live barbarian unit count, identical on every player row of a turn.
    barb_turns = [t for t in by_turn if t <= 30 and any(int(r["BarbarianUnits"]) > 0 for r in by_turn[t])]
    check(
        "barbarian units appear by turn 30",
        bool(barb_turns),
        f"first turn with barbarian units: {min(barb_turns)}" if barb_turns
        else "no barbarian unit in the first 30 turns",
    )

    # H8  The income breakdown reconciles with its own total. The CSV used to
    #     omit IncomeCapital, so the channels never summed to TotalIncome.
    channels = (
        "IncomeCapital", "IncomeTax", "IncomeCommercial",
        "IncomeIndustrial", "IncomeTileGold", "IncomeGoodsEcon",
    )
    mismatches = []
    for row in rows:
        parts = sum(float(row[c]) for c in channels)
        total = float(row["TotalIncome"])
        if abs(parts - total) > 0.5:
            mismatches.append(
                f"T{row['Turn']}P{row['Player']} {parts:g}!={total:g}")
    check(
        "income channels sum to TotalIncome",
        not mismatches,
        "reconciles" if not mismatches else
        f"{len(mismatches)} rows differ, first {mismatches[:3]}",
    )

    if not quiet:
        for line in notes:
            print(line)
        for line in failures:
            print(line)
        print(f"\n{len(notes)} passed, {len(failures)} failed "
              f"({last_turn} turns, {len(by_player)} players)")
    return 1 if failures else 0


COLUMNS = [
    "Turn", "Player", "GDP", "Cities", "TechsResearched", "TradePartners",
    "EraVP", "Era", "Eliminated", "MetPlayersMask", "IncomeCapital",
    "IncomeTax", "IncomeCommercial", "IncomeIndustrial", "IncomeTileGold",
    "IncomeGoodsEcon", "TotalIncome", "BarbarianUnits",
]


def _row(turn: int, player: int, **over: object) -> dict[str, str]:
    base = {c: "0" for c in COLUMNS}
    base.update({"Turn": str(turn), "Player": str(player), "Cities": "5",
                 "Era": "4", "MetPlayersMask": "3", "TradePartners": "1",
                 "BarbarianUnits": "2" if turn <= 30 else "0"})
    base.update({k: str(v) for k, v in over.items()})
    return base


def _healthy() -> list[dict[str, str]]:
    """A run that must pass every check, as the baseline the cases perturb."""
    rows: list[dict[str, str]] = []
    for turn in (1, 100, 200):
        for player in range(4):
            rows.append(_row(turn, player, TechsResearched=turn // 4))
    return rows


def selftest() -> int:
    """Prove every check can actually fail.

    A health gate that cannot go red is worse than no gate: it reads as
    evidence while asserting nothing. This repo has been burned by exactly that
    (see the mapgen_metrics selftest and the CMake note about a broken
    instrument invalidating eight worldgen baselines), so each assertion here
    gets a case that must trip it.
    """
    cases: list[tuple[str, list[dict[str, str]]]] = []

    # H1: nobody holds a city at the end.
    rows = _healthy()
    for r in rows:
        if int(r["Turn"]) == 200:
            r["Cities"] = "0"
    cases.append(("no survivor", rows))

    # H2: a city-less player researches anyway.
    rows = _healthy()
    rows += [_row(201, 0, Cities=0, TechsResearched=50),
             _row(202, 0, Cities=0, TechsResearched=51)]
    cases.append(("ghost research", rows))

    # H3: era clock stuck in the Ancient era.
    rows = _healthy()
    for r in rows:
        r["Era"] = "0"
    cases.append(("era pinned at 0", rows))

    # H4/H5: one runaway civ, everyone else on a single city.
    rows = _healthy()
    for r in rows:
        r["Cities"] = "20" if int(r["Player"]) == 0 else "1"
    cases.append(("runaway leader", rows))

    # H6: a player never meets anyone.
    rows = _healthy()
    for r in rows:
        if int(r["Player"]) == 3:
            r["MetPlayersMask"] = "0"
    cases.append(("isolated player", rows))

    # H7: no trade routes anywhere.
    rows = _healthy()
    for r in rows:
        r["TradePartners"] = "0"
    cases.append(("no trade", rows))

    # H9: a player never founds a city (its settler died on the walk).
    rows = _healthy()
    for r in rows:
        if r["Player"] == "3" and int(r["Turn"]) <= FOUND_BY_TURN:
            r["Cities"] = "0"
    cases.append(("H9 never founded", rows))

    # H10: no barbarian ever spawns.
    rows = _healthy()
    for r in rows:
        r["BarbarianUnits"] = "0"
    cases.append(("no barbarians", rows))

    # H8: the income breakdown does not reconcile.
    rows = _healthy()
    for r in rows:
        r["TotalIncome"] = "99"
    cases.append(("income mismatch", rows))

    ok = True
    baseline = evaluate(_healthy(), quiet=True)
    if baseline != 0:
        print("SELFTEST FAIL: the healthy baseline does not pass")
        ok = False
    else:
        print("SELFTEST PASS: healthy baseline passes")

    for name, rows in cases:
        if evaluate(rows, quiet=True) == 0:
            print(f"SELFTEST FAIL: '{name}' was not detected")
            ok = False
        else:
            print(f"SELFTEST PASS: '{name}' detected")

    return 0 if ok else 1


def main() -> int:
    if len(sys.argv) == 2 and sys.argv[1] == "--selftest":
        return selftest()
    if len(sys.argv) != 2:
        print(f"usage: {sys.argv[0]} <sim_log.csv> | --selftest", file=sys.stderr)
        return 2

    rows = load(sys.argv[1])
    return evaluate(rows)


if __name__ == "__main__":
    sys.exit(main())
