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

Checks (H1-H10) are what a working game must have; one failing makes the exit
code 1. Targets (T1-T5) are the money and trade programme's acceptance metrics
(plan of 2026-09-10, Part B7). They are reported as MET or MISSED and never
touch the exit code: the phase that must meet one flips it to a check.

Usage:
    sim_health.py <sim_log.csv> [<sim_log_events.csv>] [--war-baseline N]

The events file defaults to the sim log's name with `_events` before the
extension, which is where aoc_simulate writes it. Exit 0 if every check passes,
1 otherwise. Each failure prints the measured value, so a red run says what
broke rather than just that something did.
"""

from __future__ import annotations

import argparse
import csv
import os
import sys
from collections import defaultdict
from dataclasses import dataclass, field


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


def events_path_for(sim_path: str) -> str:
    """Where aoc_simulate puts the events CSV for a given sim log."""
    stem, ext = os.path.splitext(sim_path)
    return f"{stem}_events{ext}" if ext else f"{sim_path}_events"


FOUND_BY_TURN = 5


@dataclass
class Report:
    notes: list[str] = field(default_factory=list)
    failures: list[str] = field(default_factory=list)
    missed: list[str] = field(default_factory=list)

    @property
    def ok(self) -> bool:
        return not self.failures


def evaluate(rows: list[dict[str, str]], events: list[dict[str, str]] | None = None,
             war_baseline: int | None = None, quiet: bool = False) -> Report:
    """Run every check and target over `rows` (and `events`, if given)."""
    report = Report()
    if not rows:
        report.failures.append("FAIL: sim log is empty")
        if not quiet:
            print(report.failures[0])
        return report

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

    def check(name: str, ok: bool, detail: str) -> None:
        (report.notes if ok else report.failures).append(
            f"{'PASS' if ok else 'FAIL'}: {name} -- {detail}")

    def target(name: str, ok: bool, detail: str) -> None:
        (report.notes if ok else report.missed).append(
            f"{'MET' if ok else 'MISSED'}: {name} -- {detail}")

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
    #     omit IncomeCapital, so the channels never summed to TotalIncome; the
    #     money-supply tax was folded into IncomeCommercial until 2026-09-10.
    channels = (
        "IncomeCapital", "IncomeTax", "IncomeCommercial",
        "IncomeIndustrial", "IncomeTileGold", "IncomeGoodsEcon",
        "IncomeMoneyTax",
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

    # ---- Programme targets (money and trade plan, Part B7) -------------------
    survivors_mid = [r for r in by_turn[mid] if int(r["Cities"]) > 0]

    def mean_at_mid(column: str) -> float:
        if not survivors_mid:
            return 0.0
        return sum(float(r[column]) for r in survivors_mid) / len(survivors_mid)

    # T1 (M1) Routes are the norm, not the exception.
    mean_routes = mean_at_mid("ActiveRoutes")
    target(
        "T1 mean active routes per surviving civ at mid-game >= 2",
        mean_routes >= 2.0,
        f"turn {mid}: {mean_routes:.2f}",
    )

    # T2 (M2) Most civs keep an international partner most of the time.
    late_from = min(50, last_turn // 2)
    shares = []
    for series in by_player.values():
        late_rows = [r for r in series if int(r["Turn"]) > late_from]
        if late_rows:
            shares.append(
                sum(1 for r in late_rows if int(r["TradePartners"]) >= 1) / len(late_rows))
    connected = (sum(1 for s in shares if s >= 0.5) / len(shares)) if shares else 0.0
    target(
        f"T2 civs with a trade partner on half the turns after turn {late_from} >= 75%",
        connected >= 0.75,
        f"{connected:.0%} of {len(shares)} civs",
    )

    # T3 (M3) Trade brings home real money. Until Phase 2 IncomeTradeRoutes is
    #     the coin Traders delivered this turn, reported beside TotalIncome.
    window = [r for r in rows if int(r["Turn"]) >= max(1, (3 * last_turn) // 4)]
    route_gold = sum(float(r["IncomeTradeRoutes"]) for r in window)
    all_income = sum(float(r["TotalIncome"]) for r in window) + route_gold
    share = route_gold / all_income if all_income > 0 else 0.0
    target(
        "T3 route gold share of income over the last quarter >= 25%",
        share >= 0.25,
        f"{share:.1%} (route {route_gold:g} of {all_income:g})",
    )

    # T4 (M4) Deals happen and stay in force.
    if events is None:
        target("T4a deals accepted per 10 turns after turn 50 >= 1", False, "no events file")
    else:
        accepted = [e for e in events
                    if e["EventType"] == "DealAccepted" and int(e["Turn"]) > 50]
        span = last_turn - 50
        rate = (len(accepted) / (span / 10.0)) if span > 0 else 0.0
        target(
            "T4a deals accepted per 10 turns after turn 50 >= 1",
            rate >= 1.0,
            f"{len(accepted)} deals in {max(span, 0)} turns = {rate:.2f} per 10 turns",
        )
    mean_deals = mean_at_mid("DealsActive")
    target(
        "T4b mean active deals per surviving civ at mid-game >= 1",
        mean_deals >= 1.0,
        f"turn {mid}: {mean_deals:.2f}",
    )

    # T5 (M5) Trade must not silence war entirely nor inflame it: the count of
    #     declarations stays within a band of the Phase 0 baseline per seed.
    if events is None:
        target("T5 wars declared within 25% of the baseline", False, "no events file")
    else:
        wars = sum(1 for e in events if e["EventType"] == "WarDeclared")
        if war_baseline is None:
            report.notes.append(
                f"NOTE: T5 wars declared = {wars} (pass --war-baseline N to check the band)")
        else:
            lo, hi = 0.75 * war_baseline, 1.25 * war_baseline
            target(
                "T5 wars declared within 25% of the baseline",
                lo <= wars <= hi,
                f"{wars} declared, baseline {war_baseline}, band {lo:.1f}..{hi:.1f}",
            )

    if not quiet:
        for line in report.notes:
            print(line)
        for line in report.missed:
            print(line)
        for line in report.failures:
            print(line)
        print(f"\n{len(report.notes)} passed, {len(report.failures)} failed, "
              f"{len(report.missed)} targets missed "
              f"({last_turn} turns, {len(by_player)} players)")
    return report


COLUMNS = [
    "Turn", "Player", "GDP", "Cities", "TechsResearched", "TradePartners",
    "EraVP", "Era", "Eliminated", "MetPlayersMask", "IncomeCapital",
    "IncomeTax", "IncomeCommercial", "IncomeIndustrial", "IncomeTileGold",
    "IncomeGoodsEcon", "IncomeMoneyTax", "TotalIncome", "BarbarianUnits",
    "IncomeTradeRoutes", "ActiveRoutes", "DealsActive", "LuxuryTypesHeld",
]

EVENT_COLUMNS = ["Turn", "SubStep", "EventType", "Player", "OtherPlayer",
                 "Value1", "Value2", "Detail"]


def _row(turn: int, player: int, **over: object) -> dict[str, str]:
    base = {c: "0" for c in COLUMNS}
    base.update({"Turn": str(turn), "Player": str(player), "Cities": "5",
                 "Era": "4", "MetPlayersMask": "3", "TradePartners": "1",
                 "BarbarianUnits": "2" if turn <= 30 else "0",
                 "ActiveRoutes": "2", "DealsActive": "1",
                 "TotalIncome": "100", "IncomeTax": "100",
                 "IncomeTradeRoutes": "40"})
    base.update({k: str(v) for k, v in over.items()})
    return base


def _event(turn: int, kind: str, player: int = 0, other: int = 1) -> dict[str, str]:
    base = {c: "0" for c in EVENT_COLUMNS}
    base.update({"Turn": str(turn), "EventType": kind, "Player": str(player),
                 "OtherPlayer": str(other), "Detail": kind})
    return base


def _healthy() -> list[dict[str, str]]:
    """A run that must pass every check and meet every target."""
    rows: list[dict[str, str]] = []
    for turn in (1, 100, 200):
        for player in range(4):
            rows.append(_row(turn, player, TechsResearched=turn // 4))
    return rows


def _healthy_events() -> list[dict[str, str]]:
    """One deal every ten turns after turn 50, four wars."""
    events = [_event(turn, "DealAccepted") for turn in range(60, 201, 10)]
    events += [_event(turn, "WarDeclared") for turn in (30, 80, 130, 180)]
    return events


WAR_BASELINE = 4


def selftest() -> int:
    """Prove every check and target can actually go red.

    A health gate that cannot go red is worse than no gate: it reads as
    evidence while asserting nothing. This repo has been burned by exactly that
    (see the mapgen_metrics selftest and the CMake note about a broken
    instrument invalidating eight worldgen baselines), so each assertion here
    gets a case that must trip it.
    """
    # (name, rows, events, expected failing check or missed target prefix)
    cases: list[tuple[str, list[dict[str, str]], list[dict[str, str]], str]] = []

    def add(name: str, rows: list[dict[str, str]], expect: str,
            events: list[dict[str, str]] | None = None) -> None:
        cases.append((name, rows, _healthy_events() if events is None else events, expect))

    # H1: nobody holds a city at the end.
    rows = _healthy()
    for r in rows:
        if int(r["Turn"]) == 200:
            r["Cities"] = "0"
    add("no survivor", rows, "FAIL: a player survives")

    # H2: a city-less player researches anyway.
    rows = _healthy()
    rows += [_row(201, 0, Cities=0, TechsResearched=50),
             _row(202, 0, Cities=0, TechsResearched=51)]
    add("ghost research", rows, "FAIL: no city-less player")

    # H3: era clock stuck in the Ancient era.
    rows = _healthy()
    for r in rows:
        r["Era"] = "0"
    add("era pinned at 0", rows, "FAIL: era advances")

    # H4/H5: one runaway civ, everyone else on a single city.
    rows = _healthy()
    for r in rows:
        r["Cities"] = "20" if int(r["Player"]) == 0 else "1"
    add("runaway leader", rows, "FAIL: city-count Gini")

    # H6: a player never meets anyone.
    rows = _healthy()
    for r in rows:
        if int(r["Player"]) == 3:
            r["MetPlayersMask"] = "0"
    add("isolated player", rows, "FAIL: every player meets")

    # H7: no trade routes anywhere.
    rows = _healthy()
    for r in rows:
        r["TradePartners"] = "0"
    add("no trade", rows, "FAIL: at least two players establish")

    # H9: a player never founds a city (its settler died on the walk).
    rows = _healthy()
    for r in rows:
        if r["Player"] == "3" and int(r["Turn"]) <= FOUND_BY_TURN:
            r["Cities"] = "0"
    add("H9 never founded", rows, "FAIL: every player founds")

    # H10: no barbarian ever spawns.
    rows = _healthy()
    for r in rows:
        r["BarbarianUnits"] = "0"
    add("no barbarians", rows, "FAIL: barbarian units")

    # H8: the income breakdown does not reconcile.
    rows = _healthy()
    for r in rows:
        r["TotalIncome"] = "99"
    add("income mismatch", rows, "FAIL: income channels")

    # H8: the money-supply tax is a channel of its own; a total that leaves
    # it out no longer reconciles.
    rows = _healthy()
    for r in rows:
        r["IncomeMoneyTax"] = "7"
    add("money tax left out of the total", rows, "FAIL: income channels")

    # T1: Traders sit idle.
    rows = _healthy()
    for r in rows:
        r["ActiveRoutes"] = "0"
    add("T1 no routes", rows, "MISSED: T1")

    # T2: half the civs trade only with themselves after turn 50.
    rows = _healthy()
    for r in rows:
        if int(r["Player"]) >= 2 and int(r["Turn"]) > 50:
            r["TradePartners"] = "0"
    add("T2 half the civs isolated", rows, "MISSED: T2")

    # T3: routes bring no money home.
    rows = _healthy()
    for r in rows:
        r["IncomeTradeRoutes"] = "0"
    add("T3 no route gold", rows, "MISSED: T3")

    # T4a: no deal is ever accepted.
    add("T4a no deals", _healthy(), "MISSED: T4a",
        events=[e for e in _healthy_events() if e["EventType"] != "DealAccepted"])

    # T4b: deals lapse before mid-game.
    rows = _healthy()
    for r in rows:
        r["DealsActive"] = "0"
    add("T4b no deals in force", rows, "MISSED: T4b")

    # T5: wars spike past the band.
    add("T5 war spike", _healthy(), "MISSED: T5",
        events=_healthy_events() + [_event(t, "WarDeclared") for t in range(90, 150, 10)])

    ok = True
    baseline = evaluate(_healthy(), _healthy_events(), WAR_BASELINE, quiet=True)
    if baseline.failures or baseline.missed:
        print("SELFTEST FAIL: the healthy baseline does not pass:",
              baseline.failures + baseline.missed)
        ok = False
    else:
        print("SELFTEST PASS: healthy baseline passes and meets every target")

    for name, rows, events, expect in cases:
        report = evaluate(rows, events, WAR_BASELINE, quiet=True)
        hits = [line for line in report.failures + report.missed if line.startswith(expect)]
        if not hits:
            print(f"SELFTEST FAIL: '{name}' was not detected (expected '{expect}')")
            ok = False
        else:
            print(f"SELFTEST PASS: '{name}' detected")

    return 0 if ok else 1


def main() -> int:
    if len(sys.argv) == 2 and sys.argv[1] == "--selftest":
        return selftest()
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("sim_log")
    parser.add_argument("events_log", nargs="?", default=None)
    parser.add_argument("--war-baseline", type=int, default=None,
                        help="WarDeclared count of the Phase 0 baseline run for this seed")
    args = parser.parse_args()

    rows = load(args.sim_log)
    events_path = args.events_log or events_path_for(args.sim_log)
    events = load(events_path) if os.path.exists(events_path) else None
    if events is None:
        print(f"NOTE: no events file at {events_path}; T4a and T5 cannot be measured")
    return 0 if evaluate(rows, events, args.war_baseline).ok else 1


if __name__ == "__main__":
    sys.exit(main())
