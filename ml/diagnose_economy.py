#!/usr/bin/env python3
"""
Economic diagnostic tool: runs parallel simulations and produces a health report.

Answers the key questions:
  1. Where does money come from? (tax vs commercial vs industrial vs goods vs tiles)
  2. Where does money go? (units vs buildings vs science)
  3. Are goods being produced AND consumed, or just piling up?
  4. When do players transition monetary systems? Who gets stuck?
  5. Can small nations (1-3 cities) sustain themselves?
  6. Can large nations (8+ cities) avoid bankruptcy?
  7. What's the correlation between goods production and winning?

Usage:
    python diagnose_economy.py --sims 10 --turns 500 --workers 6
    python diagnose_economy.py --quick  # 5 sims, 200 turns, 4 workers
"""

import argparse
import csv
import multiprocessing
import os
import subprocess
import sys
import tempfile
import time
from collections import defaultdict

import numpy as np

SIMULATOR_PATH = os.path.join(os.path.dirname(__file__), "..", "build", "aoc_simulate")


# ============================================================================
# Parallel simulation runner
# ============================================================================

def _run_one_sim(args_tuple) -> str:
    """Run one simulation in a worker process. Returns path to CSV."""
    sim_id, num_players, num_turns = args_tuple
    output_path = os.path.join(tempfile.gettempdir(), f"aoc_diag_{sim_id}.csv")
    try:
        subprocess.run(
            [SIMULATOR_PATH, "--players", str(num_players),
             "--turns", str(num_turns), "--output", output_path],
            capture_output=True, timeout=180, check=False
        )
    except (subprocess.TimeoutExpired, FileNotFoundError) as e:
        print(f"  Sim {sim_id}: FAILED ({e})")
        return ""
    if not os.path.exists(output_path):
        return ""
    return output_path


def run_parallel_sims(num_sims: int, num_players: int, num_turns: int,
                      num_workers: int) -> list:
    """Run N simulations in parallel. Returns list of CSV file paths."""
    args_list = [(i, num_players, num_turns) for i in range(num_sims)]

    print(f"[Running] {num_sims} simulations ({num_players}p, {num_turns}t) "
          f"with {num_workers} workers...")
    start = time.time()

    with multiprocessing.Pool(processes=num_workers) as pool:
        results = pool.map(_run_one_sim, args_list)

    elapsed = time.time() - start
    valid = [r for r in results if r]
    print(f"  Completed: {len(valid)}/{num_sims} in {elapsed:.1f}s")
    return valid


# ============================================================================
# CSV parsing
# ============================================================================

def parse_sim_csv(filepath: str) -> list:
    """Parse a simulation CSV into list of row dicts."""
    try:
        with open(filepath, "r") as f:
            reader = csv.DictReader(f)
            rows = list(reader)
        os.unlink(filepath)  # Clean up
        return rows
    except Exception:
        return []


def safe_float(val, default=0.0):
    try:
        return float(val) if val else default
    except (ValueError, TypeError):
        return default


def safe_int(val, default=0):
    try:
        return int(val) if val else default
    except (ValueError, TypeError):
        return default


# ============================================================================
# Analysis
# ============================================================================

def analyze_simulations(csv_paths: list, num_turns: int) -> dict:
    """Analyze all simulation CSVs and produce aggregate statistics."""

    # Per-player-per-turn aggregated data across all sims
    all_final_states = []  # Final turn data for each player in each sim
    income_sources = defaultdict(list)  # {source_name: [values across all sims]}
    expense_sinks = defaultdict(list)
    treasury_trajectories = defaultdict(list)  # {(sim, player): [treasury per turn]}
    monetary_transitions = []  # [(sim, player, system_at_end)]
    small_nation_treasury = []  # Treasury of players with ≤3 cities at end
    large_nation_treasury = []  # Treasury of players with ≥8 cities at end
    winner_traits = []  # Final stats of winners
    loser_traits = []  # Final stats of non-winners
    goods_stockpiles = []

    for sim_idx, csv_path in enumerate(csv_paths):
        rows = parse_sim_csv(csv_path)
        if not rows:
            continue

        max_turn = max(safe_int(r.get("Turn")) for r in rows)

        # Get final-turn data
        final_rows = [r for r in rows if safe_int(r.get("Turn")) == max_turn]

        # Find winner (highest EraVP)
        winner_id = -1
        best_vp = -1
        for row in final_rows:
            vp = safe_int(row.get("EraVP"))
            if vp > best_vp:
                best_vp = vp
                winner_id = safe_int(row.get("Player"))

        for row in final_rows:
            player_id = safe_int(row.get("Player"))
            cities = safe_int(row.get("Cities"))
            pop = safe_int(row.get("Population"))
            treasury = safe_float(row.get("Treasury"))
            mon_sys = safe_int(row.get("MonetarySystem"))
            era_vp = safe_int(row.get("EraVP"))

            # Income breakdown
            inc_tax = safe_float(row.get("IncomeTax"))
            inc_comm = safe_float(row.get("IncomeCommercial"))
            inc_ind = safe_float(row.get("IncomeIndustrial"))
            inc_tile = safe_float(row.get("IncomeTileGold"))
            inc_goods = safe_float(row.get("IncomeGoodsEcon"))
            total_inc = safe_float(row.get("TotalIncome"))
            eff_inc = safe_float(row.get("EffectiveIncome"))

            # Expense breakdown
            exp_units = safe_float(row.get("ExpenseUnits"))
            exp_build = safe_float(row.get("ExpenseBuildings"))
            total_exp = safe_float(row.get("TotalExpense"))
            net_flow = safe_float(row.get("NetFlow"))
            goods_stock = safe_int(row.get("GoodsStockpiled"))

            all_final_states.append({
                "sim": sim_idx, "player": player_id, "cities": cities,
                "pop": pop, "treasury": treasury, "mon_sys": mon_sys,
                "era_vp": era_vp, "is_winner": player_id == winner_id,
                "total_income": total_inc, "effective_income": eff_inc,
                "total_expense": total_exp, "net_flow": net_flow,
                "goods_stockpiled": goods_stock,
            })

            if total_inc > 0:
                income_sources["Tax"].append(inc_tax)
                income_sources["Commercial"].append(inc_comm)
                income_sources["Industrial Rev"].append(inc_ind)
                income_sources["Tile Gold"].append(inc_tile)
                income_sources["Goods Econ"].append(inc_goods)

            if total_exp > 0:
                expense_sinks["Units"].append(exp_units)
                expense_sinks["Buildings"].append(exp_build)

            monetary_transitions.append((sim_idx, player_id, mon_sys))

            if cities <= 3:
                small_nation_treasury.append(treasury)
            if cities >= 8:
                large_nation_treasury.append(treasury)

            if player_id == winner_id:
                winner_traits.append({
                    "cities": cities, "pop": pop, "treasury": treasury,
                    "income": total_inc, "expense": total_exp, "net": net_flow,
                    "goods": goods_stock, "vp": era_vp,
                })
            else:
                loser_traits.append({
                    "cities": cities, "pop": pop, "treasury": treasury,
                    "income": total_inc, "expense": total_exp, "net": net_flow,
                    "goods": goods_stock, "vp": era_vp,
                })

            goods_stockpiles.append(goods_stock)

    return {
        "all_final": all_final_states,
        "income_sources": income_sources,
        "expense_sinks": expense_sinks,
        "monetary_transitions": monetary_transitions,
        "small_nation_treasury": small_nation_treasury,
        "large_nation_treasury": large_nation_treasury,
        "winner_traits": winner_traits,
        "loser_traits": loser_traits,
        "goods_stockpiles": goods_stockpiles,
    }


def generate_report(data: dict, num_sims: int) -> str:
    """Generate human-readable economic health report."""
    lines = []
    lines.append("=" * 70)
    lines.append("ECONOMIC HEALTH DIAGNOSTIC REPORT")
    lines.append(f"Based on {num_sims} simulations")
    lines.append("=" * 70)

    # 1. Income sources
    lines.append("\n--- 1. WHERE DOES MONEY COME FROM? ---")
    total_all = []
    for source, values in sorted(data["income_sources"].items()):
        avg = np.mean(values) if values else 0
        total_all.append((source, avg))
    grand_total = sum(v for _, v in total_all)
    for source, avg in sorted(total_all, key=lambda x: -x[1]):
        pct = (avg / grand_total * 100) if grand_total > 0 else 0
        lines.append(f"  {source:20s}: {avg:6.1f} gold/turn avg ({pct:4.1f}%)")
    lines.append(f"  {'TOTAL':20s}: {grand_total:6.1f} gold/turn")

    # 2. Expense sinks
    lines.append("\n--- 2. WHERE DOES MONEY GO? ---")
    total_exp = []
    for sink, values in sorted(data["expense_sinks"].items()):
        avg = np.mean(values) if values else 0
        total_exp.append((sink, avg))
    grand_exp = sum(v for _, v in total_exp)
    for sink, avg in sorted(total_exp, key=lambda x: -x[1]):
        pct = (avg / grand_exp * 100) if grand_exp > 0 else 0
        lines.append(f"  {sink:20s}: {avg:6.1f} gold/turn avg ({pct:4.1f}%)")
    lines.append(f"  {'TOTAL':20s}: {grand_exp:6.1f} gold/turn")
    lines.append(f"  NET (income-expense): {grand_total - grand_exp:+.1f} gold/turn")

    # 3. Goods economy
    lines.append("\n--- 3. GOODS ECONOMY ---")
    gs = data["goods_stockpiles"]
    if gs:
        lines.append(f"  Avg goods stockpiled per player: {np.mean(gs):.0f}")
        lines.append(f"  Median: {np.median(gs):.0f}, Max: {max(gs)}, Min: {min(gs)}")
        lines.append(f"  Players with 0 goods: {sum(1 for g in gs if g == 0)}/{len(gs)}")
    else:
        lines.append("  No goods data available")

    # 4. Monetary transitions
    lines.append("\n--- 4. MONETARY SYSTEM DISTRIBUTION ---")
    sys_names = {0: "Barter", 1: "CommodityMoney", 2: "GoldStandard", 3: "FiatMoney"}
    sys_counts = defaultdict(int)
    for _, _, sys_id in data["monetary_transitions"]:
        sys_counts[sys_id] += 1
    total_players = sum(sys_counts.values())
    for sys_id in sorted(sys_counts.keys()):
        pct = sys_counts[sys_id] / total_players * 100
        lines.append(f"  {sys_names.get(sys_id, f'Unknown({sys_id})'):20s}: "
                     f"{sys_counts[sys_id]:3d} players ({pct:.0f}%)")

    # 5. Small nation viability
    lines.append("\n--- 5. SMALL NATION VIABILITY (≤3 cities) ---")
    snt = data["small_nation_treasury"]
    if snt:
        profitable = sum(1 for t in snt if t >= 0)
        lines.append(f"  Count: {len(snt)} players")
        lines.append(f"  Profitable (treasury ≥ 0): {profitable}/{len(snt)} "
                     f"({profitable/len(snt)*100:.0f}%)")
        lines.append(f"  Avg treasury: {np.mean(snt):+.0f}")
        lines.append(f"  Range: [{min(snt):+.0f}, {max(snt):+.0f}]")
    else:
        lines.append("  No small nations observed")

    # 6. Large nation sustainability
    lines.append("\n--- 6. LARGE NATION SUSTAINABILITY (≥8 cities) ---")
    lnt = data["large_nation_treasury"]
    if lnt:
        profitable = sum(1 for t in lnt if t >= 0)
        lines.append(f"  Count: {len(lnt)} players")
        lines.append(f"  Profitable (treasury ≥ 0): {profitable}/{len(lnt)} "
                     f"({profitable/len(lnt)*100:.0f}%)")
        lines.append(f"  Avg treasury: {np.mean(lnt):+.0f}")
        lines.append(f"  Range: [{min(lnt):+.0f}, {max(lnt):+.0f}]")
    else:
        lines.append("  No large nations observed")

    # 7. Winner vs Loser profile
    lines.append("\n--- 7. WINNER vs LOSER PROFILE ---")
    def avg_trait(traits, key):
        vals = [t[key] for t in traits if key in t]
        return np.mean(vals) if vals else 0

    lines.append(f"  {'Metric':20s} {'Winner':>10s} {'Loser':>10s} {'Delta':>10s}")
    lines.append(f"  {'-'*20} {'-'*10} {'-'*10} {'-'*10}")
    for key, label in [("cities", "Cities"), ("pop", "Population"),
                       ("treasury", "Treasury"), ("income", "Income/turn"),
                       ("expense", "Expense/turn"), ("net", "Net flow"),
                       ("goods", "Goods stock"), ("vp", "Era VP")]:
        w = avg_trait(data["winner_traits"], key)
        l = avg_trait(data["loser_traits"], key)
        d = w - l
        lines.append(f"  {label:20s} {w:10.1f} {l:10.1f} {d:+10.1f}")

    # 8. Issues detected
    lines.append("\n--- 8. ISSUES DETECTED ---")
    issues = []

    # Income too low?
    if grand_total < grand_exp * 0.8:
        issues.append(f"CRITICAL: Average income ({grand_total:.0f}) is less than 80% of "
                      f"expenses ({grand_exp:.0f}). Players will chronically go bankrupt.")

    # Too many in barter?
    barter_pct = sys_counts.get(0, 0) / max(total_players, 1)
    if barter_pct > 0.5:
        issues.append(f"WARNING: {barter_pct*100:.0f}% of players still in Barter at game end. "
                      "AI may not be building Mints or minting coins.")

    # Goods piling up?
    if gs and np.mean(gs) > 500 and np.mean([t.get("goods", 0) for t in data["winner_traits"]] or [0]) > 500:
        issues.append("WARNING: Goods stockpiles averaging >500. Demand may be too low — "
                      "goods are piling up without being consumed.")

    # Small nations can't survive?
    if snt and sum(1 for t in snt if t >= 0) / len(snt) < 0.3:
        issues.append("WARNING: Less than 30% of small nations are profitable. "
                      "Population tax may be too low for 1-3 city empires.")

    # Large nations always bankrupt?
    if lnt and sum(1 for t in lnt if t >= 0) / len(lnt) < 0.2:
        issues.append("WARNING: Less than 20% of large nations are profitable. "
                      "Maintenance scaling or income growth may be broken.")

    # No goods being produced?
    if gs and np.median(gs) == 0:
        issues.append("CRITICAL: Median goods stockpile is 0. Production chain may be broken.")

    if not issues:
        issues.append("No critical issues detected. Economy appears healthy.")

    for issue in issues:
        lines.append(f"  {issue}")

    return "\n".join(lines)


# ============================================================================
# Main
# ============================================================================

def main():
    parser = argparse.ArgumentParser(description="Economic diagnostic tool")
    parser.add_argument("--sims", type=int, default=10, help="Number of simulations")
    parser.add_argument("--turns", type=int, default=500, help="Turns per simulation")
    parser.add_argument("--players", type=int, default=8, help="Players per simulation")
    parser.add_argument("--workers", type=int, default=0,
                        help="Parallel workers (0 = auto-detect)")
    parser.add_argument("--quick", action="store_true",
                        help="Quick: 5 sims, 200 turns, 4 workers")
    parser.add_argument("--output", default="economic_report.txt",
                        help="Output report file")
    args = parser.parse_args()

    if args.quick:
        args.sims = 5
        args.turns = 200
        args.workers = 4

    if args.workers <= 0:
        args.workers = max(1, multiprocessing.cpu_count() - 1)

    if not os.path.exists(SIMULATOR_PATH):
        print(f"[Error] Simulator not found: {SIMULATOR_PATH}")
        sys.exit(1)

    print("=" * 60)
    print("Age of Civilization — Economic Diagnostic Tool")
    print("=" * 60)

    # Run simulations in parallel
    csv_paths = run_parallel_sims(args.sims, args.players, args.turns, args.workers)

    if not csv_paths:
        print("[Error] No simulations completed successfully")
        sys.exit(1)

    # Analyze
    print(f"\n[Analyzing] {len(csv_paths)} simulation results...")
    data = analyze_simulations(csv_paths, args.turns)

    # Generate report
    report = generate_report(data, len(csv_paths))
    print(f"\n{report}")

    # Save
    with open(args.output, "w") as f:
        f.write(report)
    print(f"\n[Saved] {args.output}")


if __name__ == "__main__":
    main()
