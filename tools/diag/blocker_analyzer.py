#!/usr/bin/env python3
"""
Phase 1 IR-blocker analyzer.

Walks build/audit_matrix/ and produces:
  - tools/diag/blocker_table.csv -- one row per (sim, player, final_ir, blocker_reason, blocker_detail)
  - tools/diag/PHASE1_DIAGNOSIS.md -- ranked-evidence summary

Inputs per sim:
  - build/audit_matrix/p{P}_t{T}_{map}_s{S}.log
       -- Player-level "achieved IR" lines + IR_BLOCKED diagnostic lines
          (only emitted when binary built with AOC_DIAG_IR=ON).
  - build/audit_matrix/scratch_p{P}_t{T}_{map}_s{S}/simulation_log_events.csv
       -- per-event log: TechResearched, CityFounded.
  - build/audit_matrix/scratch_p{P}_t{T}_{map}_s{S}/simulation_log.csv
       -- per-turn per-player time series including IndustrialRev column.

Reason tags emitted by AOC_DIAG_IR:
  reason=tech       detail = TechId
  reason=cityCount  detail = current city count
  reason=good       detail = GoodId

The analyzer is read-only and never mutates the binary or audit corpus.
Run: python3 tools/diag/blocker_analyzer.py [audit_dir]
"""

from __future__ import annotations

import csv
import os
import re
import sys
from collections import Counter, defaultdict
from pathlib import Path
from typing import Iterable

# ----- Static metadata (mirrors include/aoc/simulation/economy/IndustrialRevolution.hpp) -----

IR_REQUIRED_TECHS: dict[int, list[int]] = {
    1: [11, 18, 21],
    2: [14, 22, 47],
    3: [16, 23],
    4: [27, 16],
    5: [17, 64],
}

IR_REQUIRED_GOODS: dict[int, list[int]] = {
    1: [79, 0],         # Charcoal, Iron Ore
    2: [3, 64],         # Oil, Steel
    3: [75],            # Semiconductors
    4: [108],           # Software
    5: [],
}

GOOD_NAME = {0: "IronOre", 2: "Coal", 3: "Oil", 64: "Steel",
             75: "Semiconductors", 79: "Charcoal", 108: "Software"}

TECH_NAME = {
    11: "Industrialization", 14: "Electricity", 16: "Computers",
    17: "NuclearFission", 18: "SurfacePlate", 21: "FoodPreservation",
    22: "PrecisionInstruments", 23: "Semiconductors", 27: "Internet",
    47: "Steel", 64: "Fusion",
}

# ----- Parsing -----

SIM_FILE_RE = re.compile(r"^p(\d+)_t(\d+)_([a-zA-Z]+)_s(\d+)\.log$")
ACHIEVED_RE = re.compile(
    r"Player (\d+) achieved the .+ \(Industrial Revolution #(\d+)\) on turn (\d+)"
)
BLOCKED_RE = re.compile(
    r"IR_BLOCKED player=(\d+) ir=(\d+) reason=(\w+) detail=(-?\d+) turn=(\d+)"
)


def find_audit_logs(audit_dir: Path) -> list[tuple[dict, Path, Path]]:
    """Return list of (config, log_file, scratch_dir) tuples."""
    out: list[tuple[dict, Path, Path]] = []
    for log_file in sorted(audit_dir.glob("p*_t*_*_s*.log")):
        m = SIM_FILE_RE.match(log_file.name)
        if not m:
            continue
        cfg = {
            "players": int(m.group(1)),
            "turns":   int(m.group(2)),
            "map":     m.group(3),
            "seed":    int(m.group(4)),
        }
        scratch = audit_dir / f"scratch_p{cfg['players']}_t{cfg['turns']}_{cfg['map']}_s{cfg['seed']}"
        out.append((cfg, log_file, scratch))
    return out


def parse_log(log_file: Path) -> tuple[dict[int, int], list[dict]]:
    """Return (achieved_ir_by_player, blocked_events).

    achieved_ir_by_player: player_id -> highest IR level reached.
    blocked_events: list of {player, ir, reason, detail, turn}.
    """
    achieved: dict[int, int] = {}
    blocked: list[dict] = []
    try:
        with log_file.open("r", encoding="utf-8", errors="replace") as fp:
            for line in fp:
                m = ACHIEVED_RE.search(line)
                if m:
                    pid = int(m.group(1))
                    ir = int(m.group(2))
                    if ir > achieved.get(pid, 0):
                        achieved[pid] = ir
                    continue
                m = BLOCKED_RE.search(line)
                if m:
                    blocked.append({
                        "player": int(m.group(1)),
                        "ir":     int(m.group(2)),
                        "reason": m.group(3),
                        "detail": int(m.group(4)),
                        "turn":   int(m.group(5)),
                    })
    except OSError as e:
        print(f"WARN: cannot read {log_file}: {e}", file=sys.stderr)
    return achieved, blocked


def parse_per_sim_csv(scratch: Path) -> tuple[dict[int, set[int]], dict[int, int], dict[int, int]]:
    """Return (techs_by_player, cities_by_player, max_ir_by_player) from scratch dir CSVs."""
    techs: dict[int, set[int]] = defaultdict(set)
    cities: dict[int, int] = defaultdict(int)
    max_ir: dict[int, int] = defaultdict(int)

    events_csv = scratch / "simulation_log_events.csv"
    if events_csv.is_file():
        try:
            with events_csv.open("r", encoding="utf-8", errors="replace") as fp:
                reader = csv.DictReader(fp)
                for row in reader:
                    et = row.get("EventType", "")
                    pid_raw = row.get("Player", "")
                    if not pid_raw or pid_raw == "255":
                        continue
                    try:
                        pid = int(pid_raw)
                    except ValueError:
                        continue
                    if et == "TechResearched":
                        try:
                            techs[pid].add(int(row.get("Value1", "-1")))
                        except ValueError:
                            pass
                    elif et == "CityFounded":
                        cities[pid] += 1
        except OSError as e:
            print(f"WARN: cannot read {events_csv}: {e}", file=sys.stderr)

    sim_csv = scratch / "simulation_log.csv"
    if sim_csv.is_file():
        try:
            with sim_csv.open("r", encoding="utf-8", errors="replace") as fp:
                reader = csv.DictReader(fp)
                for row in reader:
                    pid_raw = row.get("Player", "")
                    ir_raw = row.get("IndustrialRev", "")
                    if not pid_raw or not ir_raw:
                        continue
                    try:
                        pid = int(pid_raw)
                        ir = int(ir_raw)
                    except ValueError:
                        continue
                    if ir > max_ir[pid]:
                        max_ir[pid] = ir
        except OSError as e:
            print(f"WARN: cannot read {sim_csv}: {e}", file=sys.stderr)

    return techs, cities, max_ir


# ----- Classification -----

def classify_blocker(player_techs: set[int], player_cities: int,
                     blocked_events: list[dict], target_ir: int) -> dict[str, object]:
    """Decide why a player did not reach target_ir.

    Strategy:
      1. If diagnostic blocked events exist for ir=target_ir, take the most
         recently emitted reason (= the persistent gate at end-of-game).
      2. Else fall back to a tech-only proxy: missing tech > city count.
      3. If neither tech nor city count is missing, mark "good" (proxy).
    """
    diag = [e for e in blocked_events if e["ir"] == target_ir]
    if diag:
        most_recent: dict[tuple[str, int], int] = {}
        for e in diag:
            key = (e["reason"], e["detail"])
            if e["turn"] > most_recent.get(key, -1):
                most_recent[key] = e["turn"]
        latest_turn = max(most_recent.values())
        latest_reasons = [k for k, t in most_recent.items() if t == latest_turn]
        reason, detail = latest_reasons[0]
        return {"source": "diag", "reason": reason, "detail": detail,
                "turn": latest_turn}

    missing_techs = [t for t in IR_REQUIRED_TECHS.get(target_ir, []) if t not in player_techs]
    if missing_techs:
        return {"source": "proxy", "reason": "tech",
                "detail": missing_techs[0], "turn": -1}
    min_city = 1 if target_ir == 1 else 2
    if player_cities < min_city:
        return {"source": "proxy", "reason": "cityCount",
                "detail": player_cities, "turn": -1}
    goods = IR_REQUIRED_GOODS.get(target_ir, [])
    return {"source": "proxy", "reason": "good",
            "detail": goods[0] if goods else -1, "turn": -1}


# ----- Aggregation + report -----

def label_tech(detail: int) -> str:
    return f"{detail} ({TECH_NAME.get(detail, '?')})"


def label_good(detail: int) -> str:
    return f"{detail} ({GOOD_NAME.get(detail, '?')})"


def main(argv: list[str]) -> int:
    audit_dir = Path(argv[1]) if len(argv) > 1 else Path("build/audit_matrix")
    out_dir = Path("tools/diag")
    out_dir.mkdir(parents=True, exist_ok=True)

    if not audit_dir.is_dir():
        print(f"ERROR: audit dir {audit_dir} does not exist. "
              f"Run scripts/audit_matrix.sh first.", file=sys.stderr)
        return 1

    sims = find_audit_logs(audit_dir)
    if not sims:
        print(f"ERROR: no per-sim logs found in {audit_dir}.", file=sys.stderr)
        return 1

    rows: list[dict] = []
    diag_lines_seen = 0

    for cfg, log_file, scratch in sims:
        achieved, blocked = parse_log(log_file)
        diag_lines_seen += len(blocked)
        techs, cities, max_ir_csv = parse_per_sim_csv(scratch)

        all_pids = set(achieved.keys()) | set(techs.keys()) | set(cities.keys()) | set(max_ir_csv.keys())
        for pid in sorted(all_pids):
            final_ir = max(achieved.get(pid, 0), max_ir_csv.get(pid, 0))
            for next_ir in range(final_ir + 1, 6):
                blk = classify_blocker(techs.get(pid, set()),
                                       cities.get(pid, 0),
                                       blocked, next_ir)
                rows.append({
                    "players": cfg["players"], "turns": cfg["turns"],
                    "map": cfg["map"], "seed": cfg["seed"],
                    "player": pid, "final_ir": final_ir,
                    "next_ir": next_ir,
                    "blocker_source": blk["source"],
                    "blocker_reason": blk["reason"],
                    "blocker_detail": blk["detail"],
                    "blocker_turn": blk["turn"],
                    "techs_count": len(techs.get(pid, set())),
                    "cities": cities.get(pid, 0),
                })

    csv_path = out_dir / "blocker_table.csv"
    with csv_path.open("w", encoding="utf-8", newline="") as fp:
        writer = csv.DictWriter(fp, fieldnames=[
            "players", "turns", "map", "seed", "player",
            "final_ir", "next_ir",
            "blocker_source", "blocker_reason", "blocker_detail", "blocker_turn",
            "techs_count", "cities",
        ])
        writer.writeheader()
        for row in rows:
            writer.writerow(row)
    print(f"Wrote {csv_path} ({len(rows)} rows)")

    summary = build_summary(rows, sims, diag_lines_seen)
    md_path = out_dir / "PHASE1_DIAGNOSIS.md"
    md_path.write_text(summary, encoding="utf-8")
    print(f"Wrote {md_path}")
    return 0


def build_summary(rows: list[dict], sims: list[tuple[dict, Path, Path]],
                  diag_lines: int) -> str:
    total_civ_slots = len(rows)
    diag_rows = sum(1 for r in rows if r["blocker_source"] == "diag")
    proxy_rows = total_civ_slots - diag_rows

    out: list[str] = []
    out.append("# Phase 1 IR-Blocker Diagnosis")
    out.append("")
    out.append(f"- Total sims: **{len(sims)}**")
    out.append(f"- Total (sim, civ, next_ir) blocker rows: **{total_civ_slots}**")
    out.append(f"- AOC_DIAG_IR lines parsed: **{diag_lines}** "
               f"(diag-classified rows: **{diag_rows}**, proxy-classified: **{proxy_rows}**)")
    if diag_lines == 0:
        out.append("")
        out.append("> **Note:** no `IR_BLOCKED` lines found. The binary was likely built "
                   "without `-DAOC_DIAG_IR=ON`. All classifications below are proxy-based.")
    out.append("")

    ir_finals = Counter(_player_final_ir(rows))
    out.append("## Civ count by final IR level")
    out.append("")
    out.append("| Final IR | Civs |")
    out.append("|---:|---:|")
    for ir in range(0, 6):
        out.append(f"| {ir} | {ir_finals.get(ir, 0)} |")
    out.append("")

    for target_ir in (1, 2, 3, 4, 5):
        rs = [r for r in rows if r["next_ir"] == target_ir]
        if not rs:
            continue
        out.append(f"## Blockers preventing IR#{target_ir} ({len(rs)} civ-cases)")
        out.append("")
        reasons = Counter(r["blocker_reason"] for r in rs)
        for reason in ("tech", "cityCount", "good"):
            n = reasons.get(reason, 0)
            pct = (n * 100.0 / len(rs)) if rs else 0.0
            out.append(f"- {reason}: {n} ({pct:.1f}%)")
        tech_details = Counter(r["blocker_detail"] for r in rs if r["blocker_reason"] == "tech")
        if tech_details:
            out.append("")
            out.append("  Top tech blockers:")
            for det, n in tech_details.most_common(5):
                out.append(f"  - {label_tech(det)}: {n}")
        good_details = Counter(r["blocker_detail"] for r in rs if r["blocker_reason"] == "good")
        if good_details:
            out.append("")
            out.append("  Top good blockers:")
            for det, n in good_details.most_common(5):
                out.append(f"  - {label_good(det)}: {n}")
        out.append("")

    out.append("## Civ % reaching each IR by map type")
    out.append("")
    by_map: dict[str, list[int]] = defaultdict(list)
    for ir, _pid, mapname in _final_ir_with_map(rows):
        by_map[mapname].append(ir)
    out.append("| Map | Civs | %IR1 | %IR2 | %IR3 | %IR4 | %IR5 |")
    out.append("|---|---:|---:|---:|---:|---:|---:|")
    for mapname in sorted(by_map):
        irs = by_map[mapname]
        n = len(irs)
        pcts = [(sum(1 for x in irs if x >= k) * 100.0 / n) if n else 0.0
                for k in (1, 2, 3, 4, 5)]
        out.append(f"| {mapname} | {n} | {pcts[0]:.0f}% | {pcts[1]:.0f}% | "
                   f"{pcts[2]:.0f}% | {pcts[3]:.0f}% | {pcts[4]:.0f}% |")
    out.append("")

    out.append("## Civ % reaching each IR by player count")
    out.append("")
    by_pc: dict[int, list[int]] = defaultdict(list)
    for ir, _pid, _, pc in _final_ir_with_pc(rows):
        by_pc[pc].append(ir)
    out.append("| Players | Civs | %IR1 | %IR2 | %IR3 | %IR4 | %IR5 |")
    out.append("|---:|---:|---:|---:|---:|---:|---:|")
    for pc in sorted(by_pc):
        irs = by_pc[pc]
        n = len(irs)
        pcts = [(sum(1 for x in irs if x >= k) * 100.0 / n) if n else 0.0
                for k in (1, 2, 3, 4, 5)]
        out.append(f"| {pc} | {n} | {pcts[0]:.0f}% | {pcts[1]:.0f}% | "
                   f"{pcts[2]:.0f}% | {pcts[3]:.0f}% | {pcts[4]:.0f}% |")
    out.append("")

    out.append("## Hypothesis ranking from this corpus")
    out.append("")
    out.append("Each hypothesis is scored by the fraction of civ-cases it explains "
               "across IR levels 2-5 (IR#1 is near-saturation and rarely the bottleneck).")
    out.append("")
    h_counts: Counter[str] = Counter()
    h_total = 0
    for r in rows:
        if r["next_ir"] < 2:
            continue
        h_total += 1
        if r["blocker_reason"] == "tech":
            h_counts["H2-tech-cost-pacing"] += 1
        elif r["blocker_reason"] == "cityCount":
            h_counts["H7-undersized-civ"] += 1
        elif r["blocker_reason"] == "good":
            det = r["blocker_detail"]
            if det == 3:
                h_counts["H1-OIL-gate"] += 1
            elif det == 75:
                h_counts["H2-Semiconductor-good"] += 1
            elif det == 108:
                h_counts["H4-Software-good"] += 1
            elif det == 64:
                h_counts["H3-Steel-chain"] += 1
            else:
                h_counts["H-other-good"] += 1
    if h_total:
        for h, n in h_counts.most_common():
            pct = n * 100.0 / h_total
            out.append(f"- **{h}**: {n} ({pct:.1f}%)")
    else:
        out.append("- (no IR>=2 blocker rows -- nothing to score)")
    out.append("")

    out.append("## Confidence note")
    out.append("")
    out.append("- **Diag-classified rows** are direct evidence from the C++ instrumentation "
               "and are authoritative.")
    out.append("- **Proxy-classified rows** for `reason=good` infer the gate by elimination "
               "(tech ok + city count ok => goods must be the gate). Without diag, the proxy "
               "picks the FIRST listed required good (OIL for IR#2, Steel left ambiguous).")
    out.append("- If proxy_rows >> diag_rows, re-run with `cmake -DAOC_DIAG_IR=ON` then "
               "rebuild before invoking the analyzer.")
    out.append("")
    return "\n".join(out)


def _player_final_ir(rows: list[dict]) -> Iterable[int]:
    seen: set[tuple[int, int, str, int, int]] = set()
    for r in rows:
        key = (r["players"], r["turns"], r["map"], r["seed"], r["player"])
        if key in seen:
            continue
        seen.add(key)
        yield r["final_ir"]


def _final_ir_with_map(rows: list[dict]) -> Iterable[tuple[int, int, str]]:
    seen: set[tuple[int, int, str, int, int]] = set()
    for r in rows:
        key = (r["players"], r["turns"], r["map"], r["seed"], r["player"])
        if key in seen:
            continue
        seen.add(key)
        yield (r["final_ir"], r["player"], r["map"])


def _final_ir_with_pc(rows: list[dict]) -> Iterable[tuple[int, int, str, int]]:
    seen: set[tuple[int, int, str, int, int]] = set()
    for r in rows:
        key = (r["players"], r["turns"], r["map"], r["seed"], r["player"])
        if key in seen:
            continue
        seen.add(key)
        yield (r["final_ir"], r["player"], r["map"], r["players"])


if __name__ == "__main__":
    sys.exit(main(sys.argv))
