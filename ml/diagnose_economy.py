#!/usr/bin/env python3
"""
Whole-game diagnostic tool: runs parallel simulations and produces a health
report covering every mechanic the sim emits signals for.

Data sources (auto-detected from a base CSV path):
  - foo.csv         : per-turn per-player metrics (economy, population, CSI, ...)
  - foo_events.csv  : structured events (UnitProduced, WarDeclared, ...)
  - foo.log         : unstructured log lines from every subsystem

Mechanic categories surfaced:
  Economy / Monetary / Production / Trade / Supply chain / Industrial Revolution
  Military / Combat / Promotion / Supply attrition
  Diplomacy / Grievances / Trade deals / Sanctions
  Cities / Growth / Loyalty / Secession / Border expansion / Goody huts
  Tech / Civics / Eureka / Great People / Space race
  Religion / Culture / World Congress / Victory conditions
  Currency crisis / Bonds / Speculation / Stock market / Monopoly pricing
  Espionage / World events / Natural disasters / Climate / Pollution
  AI controllers (economy, military, settler, builder, research)

Usage:
    python diagnose_economy.py --sims 10 --turns 500 --workers 6
    python diagnose_economy.py --quick                       # 5 sims, 200 turns
    python diagnose_economy.py --analyze run.csv             # reuse existing run
"""

import argparse
import csv
import multiprocessing
import os
import re
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

def parse_sim_csv(filepath: str, delete_after: bool = True) -> list:
    """Parse a simulation CSV into list of row dicts."""
    try:
        with open(filepath, "r") as f:
            reader = csv.DictReader(f)
            rows = list(reader)
        if delete_after:
            os.unlink(filepath)
        return rows
    except Exception:
        return []


def parse_events_csv(filepath: str, delete_after: bool = True) -> list:
    """Parse an events CSV into list of row dicts. Returns [] if missing."""
    if not filepath or not os.path.exists(filepath):
        return []
    try:
        with open(filepath, "r") as f:
            reader = csv.DictReader(f)
            rows = list(reader)
        if delete_after:
            os.unlink(filepath)
        return rows
    except Exception:
        return []


def _events_path_for(csv_path: str) -> str:
    """Return the conventional _events.csv companion path."""
    if csv_path.endswith(".csv"):
        return csv_path[:-4] + "_events.csv"
    return csv_path + "_events.csv"


def _log_path_for(csv_path: str) -> str:
    """Return the conventional .log companion path."""
    if csv_path.endswith(".csv"):
        return csv_path[:-4] + ".log"
    return csv_path + ".log"


# ============================================================================
# Log scanning — extracts per-mechanic signals from unstructured log lines.
# Every subsystem logs via LOG_INFO/LOG_WARN which the headless runner dumps
# to <csv_base>.log. Line format:
#
#   [HH:MM:SS][LEVEL] /abs/path/to/Source.cpp:LINE message text
#
# We don't have per-turn markers in the log, so we aggregate totals only.
# The events CSV already gives per-turn timelines for structured events.
# ============================================================================

_LOG_PATTERNS = {
    "religion_pantheon":   re.compile(r"founded pantheon"),
    "religion_founded":    re.compile(r"founded religion '([^']+)'"),
    "religion_spread":     re.compile(r"religion spread|converted to '"),
    "espionage_assigned":  re.compile(r"Spy \(P\d+,? ?\w*\) assigned to ([^ ]+(?: [A-Z][a-z]+)*) at"),
    "espionage_success":   re.compile(r"Spy \(P\d+\) (counterfeited|stole|sabotaged|recruited|infiltrated)"),
    "espionage_failed":    re.compile(r"Spy .* failed .* — (Identified|Captured|Killed|Escaped)"),
    "world_congress_prop": re.compile(r"World Congress: Player \d+ proposes '([^']+)'"),
    "world_congress_pass": re.compile(r"World Congress: '([^']+)' PASSED"),
    "world_congress_fail": re.compile(r"World Congress: '([^']+)' FAILED"),
    "currency_suspension": re.compile(r"SUSPENSION OF CONVERTIBILITY"),
    "currency_hyperinfl":  re.compile(r"HYPERINFLATION CRISIS"),
    "currency_reform":     re.compile(r"currency reform executed"),
    "bonds_issued":        re.compile(r"IOU created: player (\d+) lent (\d+) to player (\d+)"),
    "bonds_defaulted":     re.compile(r"IOU defaulted|bond default"),
    "secession_warning":   re.compile(r"SECESSION:"),
    "secession_flip":      re.compile(r"REVOLT:.*flips to player (\d+)"),
    "secession_free":      re.compile(r"REVOLT:.*becomes Free City"),
    "grievance_added":     re.compile(r"added grievance \(type=(\d+)\)"),
    "greatperson_recr":    re.compile(r"recruited Great Person: (.+)$"),
    "greatperson_used":    re.compile(r"(Scientist|Engineer|Merchant|Artist|Writer|Musician|Prophet|General|Admiral) added"),
    "promotion":           re.compile(r"promoted \w+: chose (\w+) \(level (\d+)\)"),
    "eureka_banked":       re.compile(r"Eureka banked! Player \d+:"),
    "inspiration_banked":  re.compile(r"Inspiration banked! Player \d+:"),
    "world_event":         re.compile(r"World event triggered for player \d+: (.+)$"),
    "supply_attrition":    re.compile(r"Supply attrition: player \d+"),
    "disaster_volcano":    re.compile(r"VOLCANIC ERUPTION"),
    "disaster_flood":      re.compile(r"^\S+ FLOOD|river FLOOD|FLOOD at"),
    "disaster_quake":      re.compile(r"EARTHQUAKE"),
    "disaster_hurricane":  re.compile(r"HURRICANE"),
    "disaster_wildfire":   re.compile(r"WILDFIRE"),
    "climate_flood":       re.compile(r"Climate: coastal tile \d+ flooded"),
    "civic_completed":     re.compile(r"completed civic: (.+)$"),
    "spec_bubble_form":    re.compile(r"speculation bubble FORMING"),
    "spec_bubble_inflate": re.compile(r"bubble INFLATING"),
    "spec_bubble_euphor":  re.compile(r"bubble reaching EUPHORIA"),
    "spec_bubble_burst":   re.compile(r"BUBBLE CRASH|bubble POPPED|bubble deflated"),
    "speculation_hoard":   re.compile(r"Player \d+ hoarded (\d+) units of good \d+"),
    "speculation_release": re.compile(r"Player \d+ released (\d+) units of good \d+"),
    "trade_deal":          re.compile(r"Trade deal: bilateral agreement between player"),
    "trade_standing":      re.compile(r"Standing route spawned:"),
    "era_csi":             re.compile(r"Era evaluation complete\. Top CSI: player (\d+) \(([-0-9.]+)\)"),
    "monopoly_form":       re.compile(r"MONOPOLY: player \d+ controls (\d+)% of ([^ ]+(?: [A-Z][a-z]+)?) supply"),
    "monopoly_broken":     re.compile(r"Monopoly broken: (.+?) supply now distributed"),
    "commodity_trade":     re.compile(r"Commodity trade: P\d+ -> P\d+"),
    "stock_invest":        re.compile(r"Stock market: player \d+ invested (\d+) in player \d+"),
    "ir_achieved":         re.compile(r"achieved the Steam Age \(Industrial Revolution #(\d+)\) on turn (\d+)"),
    "space_project":       re.compile(r"completed '([^']+)' \((\d+)/\d+ projects\)"),
    "city_founded":        re.compile(r"City founded: (\S+) by player (\d+)"),
    "city_grew":           re.compile(r"(\S+) grew to pop (\d+)"),
    "border_expand":       re.compile(r"Border expanded for (\S+): claimed tile"),
    "citystate_spawn":     re.compile(r"Spawned city-state (\S+) at"),
    "goody_gold":          re.compile(r"Goody hut: P\d+ found (\d+) gold"),
    "goody_tech":          re.compile(r"Goody hut: P\d+ found ancient scroll"),
    "goody_map":           re.compile(r"Goody hut: P\d+ found ancient map"),
    "canal_built":         re.compile(r"Canal built at tile"),
    "combat_pillage_unit": re.compile(r"pillaged (\d+) gold from destroying (\w+)"),
    "combat_pillage_tile": re.compile(r"pillaged tile improvements at .+ for (\d+) gold"),
    "combat_defkilled":    re.compile(r"defender killed"),
    "combat_atkkilled":    re.compile(r"attacker killed"),
    "nuclear_strike":      re.compile(r"NUCLEAR STRIKE"),
    "wonder_built":        re.compile(r"Completed wonder (.+?) in ", re.IGNORECASE),
    "famine_city":         re.compile(r"famine in (.+?)\s|starvation in "),
}


def scan_log(filepath: str) -> dict:
    """Scan a .log file and return per-mechanic tallies.

    Returns a dict with fields:
      - source_counts: {source_file: line_count}
      - counters:      {pattern_name: int}
      - religions:     [religion_name, ...]
      - world_events:  [event_name, ...]   (counter too)
      - civics:        [civic_name, ...]
      - great_people:  [name, ...]
      - monopolies:    [(pct, good_name), ...]
      - ir_turns:      [turn_int, ...]
      - era_csi:       [(player_id, csi_float), ...]
      - promotions:    {promo_name: count}
      - disasters:     {kind: count}
      - spy_missions:  {mission_name: count}
      - wc_resolutions:{name: 'passed'|'failed'}
      - bonds_sum:     total gold lent via IOUs
    """
    result = {
        "source_counts": defaultdict(int),
        "counters": defaultdict(int),
        "religions": [],
        "world_events_list": [],
        "world_events_counts": defaultdict(int),
        "civics": [],
        "great_people": [],
        "monopolies": [],
        "ir_turns": [],
        "era_csi": [],
        "promotions": defaultdict(int),
        "disasters": defaultdict(int),
        "spy_missions": defaultdict(int),
        "wc_resolutions": [],
        "bonds_sum": 0,
        "hoarded_total": 0,
        "released_total": 0,
        "stock_invest_total": 0,
        "pillage_gold_total": 0,
    }

    if not filepath or not os.path.exists(filepath):
        return result

    src_re = re.compile(r"/src/[^/]*(?:/[^/]*)*/([A-Z][a-zA-Z]+\.cpp):\d+")
    counters = result["counters"]

    try:
        with open(filepath, "r", errors="replace") as f:
            for line in f:
                m = src_re.search(line)
                if m:
                    result["source_counts"][m.group(1)] += 1

                for name, pat in _LOG_PATTERNS.items():
                    m = pat.search(line)
                    if not m:
                        continue
                    counters[name] += 1
                    if name == "religion_founded":
                        result["religions"].append(m.group(1))
                    elif name == "world_event":
                        evname = m.group(1).strip()
                        result["world_events_list"].append(evname)
                        result["world_events_counts"][evname] += 1
                    elif name == "civic_completed":
                        result["civics"].append(m.group(1).strip())
                    elif name == "greatperson_recr":
                        result["great_people"].append(m.group(1).strip())
                    elif name == "monopoly_form":
                        try:
                            result["monopolies"].append(
                                (int(m.group(1)), m.group(2).strip()))
                        except (ValueError, IndexError):
                            pass
                    elif name == "ir_achieved":
                        try:
                            result["ir_turns"].append(int(m.group(2)))
                        except (ValueError, IndexError):
                            pass
                    elif name == "era_csi":
                        try:
                            result["era_csi"].append(
                                (int(m.group(1)), float(m.group(2))))
                        except (ValueError, IndexError):
                            pass
                    elif name == "promotion":
                        result["promotions"][m.group(1)] += 1
                    elif name in ("disaster_volcano", "disaster_flood",
                                  "disaster_quake", "disaster_hurricane",
                                  "disaster_wildfire"):
                        result["disasters"][name[9:]] += 1
                    elif name == "espionage_assigned":
                        result["spy_missions"][m.group(1).strip()] += 1
                    elif name == "world_congress_pass":
                        result["wc_resolutions"].append((m.group(1), "passed"))
                    elif name == "world_congress_fail":
                        result["wc_resolutions"].append((m.group(1), "failed"))
                    elif name == "bonds_issued":
                        try:
                            result["bonds_sum"] += int(m.group(2))
                        except (ValueError, IndexError):
                            pass
                    elif name == "speculation_hoard":
                        try:
                            result["hoarded_total"] += int(m.group(1))
                        except (ValueError, IndexError):
                            pass
                    elif name == "speculation_release":
                        try:
                            result["released_total"] += int(m.group(1))
                        except (ValueError, IndexError):
                            pass
                    elif name == "stock_invest":
                        try:
                            result["stock_invest_total"] += int(m.group(1))
                        except (ValueError, IndexError):
                            pass
                    elif name in ("combat_pillage_unit", "combat_pillage_tile"):
                        try:
                            result["pillage_gold_total"] += int(m.group(1))
                        except (ValueError, IndexError):
                            pass
    except OSError:
        pass

    return result


def _merge_log(dst: dict, src: dict) -> None:
    """Accumulate one scan_log() result into a running aggregate dict."""
    for k, v in src["source_counts"].items():
        dst["source_counts"][k] += v
    for k, v in src["counters"].items():
        dst["counters"][k] += v
    dst["religions"].extend(src["religions"])
    dst["world_events_list"].extend(src["world_events_list"])
    for k, v in src["world_events_counts"].items():
        dst["world_events_counts"][k] += v
    dst["civics"].extend(src["civics"])
    dst["great_people"].extend(src["great_people"])
    dst["monopolies"].extend(src["monopolies"])
    dst["ir_turns"].extend(src["ir_turns"])
    dst["era_csi"].extend(src["era_csi"])
    for k, v in src["promotions"].items():
        dst["promotions"][k] += v
    for k, v in src["disasters"].items():
        dst["disasters"][k] += v
    for k, v in src["spy_missions"].items():
        dst["spy_missions"][k] += v
    dst["wc_resolutions"].extend(src["wc_resolutions"])
    dst["bonds_sum"] += src["bonds_sum"]
    dst["hoarded_total"] += src["hoarded_total"]
    dst["released_total"] += src["released_total"]
    dst["stock_invest_total"] += src["stock_invest_total"]
    dst["pillage_gold_total"] += src["pillage_gold_total"]


def _empty_log_agg() -> dict:
    return {
        "source_counts": defaultdict(int),
        "counters": defaultdict(int),
        "religions": [],
        "world_events_list": [],
        "world_events_counts": defaultdict(int),
        "civics": [],
        "great_people": [],
        "monopolies": [],
        "ir_turns": [],
        "era_csi": [],
        "promotions": defaultdict(int),
        "disasters": defaultdict(int),
        "spy_missions": defaultdict(int),
        "wc_resolutions": [],
        "bonds_sum": 0,
        "hoarded_total": 0,
        "released_total": 0,
        "stock_invest_total": 0,
        "pillage_gold_total": 0,
    }


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

def analyze_simulations(csv_paths: list, num_turns: int,
                         delete_after: bool = True) -> dict:
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

    # Time-series: income composition and monetary stage across turns, aggregated
    # over all (sim, player) samples at each turn bucket. Each turn bucket holds
    # the raw values so downstream code can compute averages or distributions.
    income_over_time = defaultdict(lambda: defaultdict(list))  # turn -> source -> [values]
    monsys_over_time = defaultdict(lambda: defaultdict(int))   # turn -> mon_sys -> count
    inflation_over_time = defaultdict(list)                    # turn -> [values]
    pop_over_time = defaultdict(list)                          # turn -> [pop per player]

    # Extra time-series covering every other CSV signal we know about.
    ts_scalar = defaultdict(lambda: defaultdict(list))   # bucket -> metric -> [values]
    ts_discrete = defaultdict(lambda: defaultdict(lambda: defaultdict(int)))  # bucket -> field -> value -> count

    # Events aggregation (from _events.csv when available).
    event_totals = defaultdict(int)                      # event_type -> count
    event_by_player = defaultdict(lambda: defaultdict(int))   # event_type -> player -> count
    events_over_time = defaultdict(lambda: defaultdict(int))  # bucket -> event_type -> count
    unit_type_produced = defaultdict(int)                # unit_type_id -> count
    unit_type_killed = defaultdict(int)                  # unit_type_id -> count
    tech_research_turns = []                             # list of turn ints (how early research happens)

    # Log-derived aggregate across all sims.
    log_agg = _empty_log_agg()

    # Derived per-final-row aggregates for richer end-state views.
    final_culture = []
    final_happiness = []
    final_corruption = []
    final_military = []
    final_tech = []
    final_era_vp = []
    final_csi = []
    final_industrial = []
    final_trade_partners = []
    final_government = []    # governmentType ids
    final_crisis = []        # crisisType ids
    famine_events = []       # per-row FamineCities counts (cumulative or current)

    for sim_idx, csv_path in enumerate(csv_paths):
        rows = parse_sim_csv(csv_path, delete_after=delete_after)
        if not rows:
            continue

        max_turn = max(safe_int(r.get("Turn")) for r in rows)

        # Time-series capture: every row (not just final turn)
        for row in rows:
            t = safe_int(row.get("Turn"))
            # Bucket at 25-turn granularity for plotting
            bucket = (t // 25) * 25
            income_over_time[bucket]["Tax"].append(safe_float(row.get("IncomeTax")))
            income_over_time[bucket]["Commercial"].append(safe_float(row.get("IncomeCommercial")))
            income_over_time[bucket]["Industrial"].append(safe_float(row.get("IncomeIndustrial")))
            income_over_time[bucket]["TileGold"].append(safe_float(row.get("IncomeTileGold")))
            income_over_time[bucket]["GoodsEcon"].append(safe_float(row.get("IncomeGoodsEcon")))
            monsys_over_time[bucket][safe_int(row.get("MonetarySystem"))] += 1
            inflation_over_time[bucket].append(safe_float(row.get("Inflation")))
            pop_over_time[bucket].append(safe_int(row.get("Population")))

            # Broad metric bucket: every quantitative column worth tracking.
            for metric in ("GDP", "Treasury", "Cities", "Military",
                           "TechsResearched", "CultureTotal", "TradePartners",
                           "CompositeCSI", "EraVP", "AvgHappiness",
                           "Corruption", "IndustrialRev",
                           "FoodPerTurn", "FamineCities",
                           "ScienceDiffusion", "CultureDiffusion",
                           "GoodsStockpiled", "NetFlow"):
                ts_scalar[bucket][metric].append(safe_float(row.get(metric)))

            # Discrete fields tracked as histograms.
            ts_discrete[bucket]["CrisisType"][safe_int(row.get("CrisisType"))] += 1
            ts_discrete[bucket]["GovernmentType"][safe_int(row.get("GovernmentType"))] += 1
            ts_discrete[bucket]["CoinTier"][safe_int(row.get("CoinTier"))] += 1

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

            final_culture.append(safe_float(row.get("CultureTotal")))
            final_happiness.append(safe_float(row.get("AvgHappiness")))
            final_corruption.append(safe_float(row.get("Corruption")))
            final_military.append(safe_int(row.get("Military")))
            final_tech.append(safe_int(row.get("TechsResearched")))
            final_era_vp.append(safe_int(row.get("EraVP")))
            final_csi.append(safe_float(row.get("CompositeCSI")))
            final_industrial.append(safe_float(row.get("IndustrialRev")))
            final_trade_partners.append(safe_int(row.get("TradePartners")))
            final_government.append(safe_int(row.get("GovernmentType")))
            final_crisis.append(safe_int(row.get("CrisisType")))
            famine_events.append(safe_int(row.get("FamineCities")))

        # Events CSV: companion file by convention (foo.csv -> foo_events.csv).
        events_path = _events_path_for(csv_path)
        event_rows = parse_events_csv(events_path, delete_after=delete_after)
        for erow in event_rows:
            etype = erow.get("EventType", "")
            if not etype:
                continue
            etime = safe_int(erow.get("Turn"))
            eplayer = safe_int(erow.get("Player"), default=-1)
            bucket = (etime // 25) * 25
            event_totals[etype] += 1
            if eplayer >= 0:
                event_by_player[etype][eplayer] += 1
            events_over_time[bucket][etype] += 1
            if etype == "UnitProduced":
                unit_type_produced[safe_int(erow.get("Value1"))] += 1
            elif etype == "UnitKilled":
                unit_type_killed[safe_int(erow.get("Value1"))] += 1
            elif etype == "TechResearched":
                tech_research_turns.append(etime)

        # Log companion: scan unstructured log for every other mechanic signal.
        log_path = _log_path_for(csv_path)
        per_sim_log = scan_log(log_path)
        _merge_log(log_agg, per_sim_log)

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
        "income_over_time": income_over_time,
        "monsys_over_time": monsys_over_time,
        "inflation_over_time": inflation_over_time,
        "pop_over_time": pop_over_time,
        "ts_scalar": ts_scalar,
        "ts_discrete": ts_discrete,
        "event_totals": event_totals,
        "event_by_player": event_by_player,
        "events_over_time": events_over_time,
        "unit_type_produced": unit_type_produced,
        "unit_type_killed": unit_type_killed,
        "tech_research_turns": tech_research_turns,
        "final_culture": final_culture,
        "final_happiness": final_happiness,
        "final_corruption": final_corruption,
        "final_military": final_military,
        "final_tech": final_tech,
        "final_era_vp": final_era_vp,
        "final_csi": final_csi,
        "final_industrial": final_industrial,
        "final_trade_partners": final_trade_partners,
        "final_government": final_government,
        "final_crisis": final_crisis,
        "famine_events": famine_events,
        "log_agg": log_agg,
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

    # 8. Timeline: income composition across turn buckets
    iot = data.get("income_over_time", {})
    if iot:
        lines.append("\n--- 8. INCOME COMPOSITION OVER TIME ---")
        lines.append(f"  {'Turn':>6s} {'Tax':>8s} {'Comm':>8s} {'Indust':>8s} {'Tile':>8s} {'Goods':>8s}")
        for bucket in sorted(iot.keys()):
            row = iot[bucket]
            lines.append(
                f"  {bucket:>6d} "
                f"{np.mean(row.get('Tax', [0])):>8.1f} "
                f"{np.mean(row.get('Commercial', [0])):>8.1f} "
                f"{np.mean(row.get('Industrial', [0])):>8.1f} "
                f"{np.mean(row.get('TileGold', [0])):>8.1f} "
                f"{np.mean(row.get('GoodsEcon', [0])):>8.1f}"
            )

    # 9. Timeline: monetary stage distribution across turn buckets
    mst = data.get("monsys_over_time", {})
    if mst:
        lines.append("\n--- 9. MONETARY STAGE DISTRIBUTION OVER TIME ---")
        lines.append(f"  {'Turn':>6s} {'Barter':>8s} {'Commod':>8s} {'Gold':>8s} {'Fiat':>8s} {'Digital':>8s}")
        for bucket in sorted(mst.keys()):
            row = mst[bucket]
            total = sum(row.values()) or 1
            lines.append(
                f"  {bucket:>6d} "
                f"{row.get(0,0)/total*100:>7.1f}% "
                f"{row.get(1,0)/total*100:>7.1f}% "
                f"{row.get(2,0)/total*100:>7.1f}% "
                f"{row.get(3,0)/total*100:>7.1f}% "
                f"{row.get(4,0)/total*100:>7.1f}%"
            )

    # 10. Inflation + population trajectory
    inft = data.get("inflation_over_time", {})
    popt = data.get("pop_over_time", {})
    if inft and popt:
        lines.append("\n--- 10. INFLATION + POPULATION OVER TIME ---")
        lines.append(f"  {'Turn':>6s} {'InflAvg':>8s} {'InflMax':>8s} {'PopAvg':>8s} {'PopMax':>8s}")
        for bucket in sorted(inft.keys()):
            inf = inft[bucket]
            pop = popt.get(bucket, [0])
            lines.append(
                f"  {bucket:>6d} "
                f"{np.mean(inf):>8.3f} "
                f"{max(inf):>8.3f} "
                f"{np.mean(pop):>8.1f} "
                f"{max(pop):>8d}"
            )

    # 11. Military overview (from events + final Military column)
    lines.append("\n--- 11. MILITARY ---")
    prod = data.get("unit_type_produced", {})
    killed = data.get("unit_type_killed", {})
    mil_final = data.get("final_military", [])
    total_prod = sum(prod.values())
    total_killed = sum(killed.values())
    lines.append(f"  Units produced (events)   : {total_prod}")
    lines.append(f"  Units killed   (events)   : {total_killed}")
    if total_prod > 0:
        lines.append(f"  Kill / produce ratio      : {total_killed/total_prod:.2f}")
    if mil_final:
        lines.append(f"  Final military size avg   : {np.mean(mil_final):.1f} "
                     f"(max={max(mil_final)}, min={min(mil_final)})")
    if prod:
        lines.append("  Top produced unit types (by unitTypeId):")
        for tid, n in sorted(prod.items(), key=lambda x: -x[1])[:5]:
            lines.append(f"    type {tid:>3d}: produced={n:>4d} killed={killed.get(tid, 0):>3d}")

    # 12. Diplomacy overview (from events)
    lines.append("\n--- 12. DIPLOMACY ---")
    ev = data.get("event_totals", {})
    lines.append(f"  First contacts (PlayersMet): {ev.get('PlayersMet', 0)}")
    lines.append(f"  Wars declared              : {ev.get('WarDeclared', 0)}")
    lines.append(f"  Peace treaties signed      : {ev.get('PeaceMade', 0)}")
    lines.append(f"  Cities founded             : {ev.get('CityFounded', 0)}")
    fp = data.get("final_trade_partners", [])
    if fp:
        lines.append(f"  Avg trade partners (final) : {np.mean(fp):.1f} (max={max(fp)})")

    # 13. Tech + culture
    lines.append("\n--- 13. TECH & CULTURE ---")
    tt = data.get("tech_research_turns", [])
    ft = data.get("final_tech", [])
    if ft:
        lines.append(f"  Techs researched (final avg): {np.mean(ft):.1f} (max={max(ft)})")
    if tt:
        lines.append(f"  Total tech events           : {len(tt)}")
        lines.append(f"  First tech researched turn  : {min(tt)}")
        lines.append(f"  Median tech research turn   : {int(np.median(tt))}")
    fc = data.get("final_culture", [])
    if fc:
        lines.append(f"  Final culture avg           : {np.mean(fc):.1f} "
                     f"(max={max(fc):.0f}, min={min(fc):.0f})")
    fcsi = data.get("final_csi", [])
    if fcsi:
        lines.append(f"  Final CompositeCSI avg      : {np.mean(fcsi):.2f} "
                     f"(max={max(fcsi):.2f}, min={min(fcsi):.2f})")

    # 14. Population + food
    lines.append("\n--- 14. POPULATION & FOOD ---")
    ts_s = data.get("ts_scalar", {})
    popt = data.get("pop_over_time", {})
    if ts_s:
        lines.append(f"  {'Turn':>6s} {'PopAvg':>8s} {'FoodPT':>8s} {'Famine':>8s}")
        for bucket in sorted(ts_s.keys()):
            row = ts_s[bucket]
            pop_vals = popt.get(bucket, [0])
            lines.append(
                f"  {bucket:>6d} "
                f"{np.mean(pop_vals):>8.1f} "
                f"{np.mean(row.get('FoodPerTurn', [0])):>8.1f} "
                f"{np.mean(row.get('FamineCities', [0])):>8.1f}"
            )
    fam = data.get("famine_events", [])
    if fam:
        lines.append(f"  Players with famine at game end: "
                     f"{sum(1 for f in fam if f > 0)}/{len(fam)}")

    # 15. Happiness + corruption
    lines.append("\n--- 15. HAPPINESS & CORRUPTION ---")
    fh = data.get("final_happiness", [])
    fcor = data.get("final_corruption", [])
    if fh:
        lines.append(f"  Final happiness avg  : {np.mean(fh):.2f} "
                     f"(max={max(fh):.1f}, min={min(fh):.1f})")
        lines.append(f"  Unhappy players (<5) : "
                     f"{sum(1 for v in fh if v < 5)}/{len(fh)}")
    if fcor:
        lines.append(f"  Final corruption avg : {np.mean(fcor):.3f} "
                     f"(max={max(fcor):.2f}, min={min(fcor):.2f})")

    # 16. Government + crisis distribution
    lines.append("\n--- 16. GOVERNMENT & CRISIS (final turn) ---")
    fg = data.get("final_government", [])
    fcr = data.get("final_crisis", [])
    if fg:
        gov_counts = defaultdict(int)
        for g in fg:
            gov_counts[g] += 1
        lines.append("  Government type distribution:")
        for gid, cnt in sorted(gov_counts.items(), key=lambda x: -x[1]):
            lines.append(f"    govId {gid}: {cnt} players")
    if fcr:
        cr_counts = defaultdict(int)
        for c in fcr:
            cr_counts[c] += 1
        nonzero = sum(cnt for cid, cnt in cr_counts.items() if cid != 0)
        lines.append(f"  Players in crisis (final)  : {nonzero}/{len(fcr)}")
        if nonzero > 0:
            for cid, cnt in sorted(cr_counts.items(), key=lambda x: -x[1]):
                if cid == 0:
                    continue
                lines.append(f"    crisisId {cid}: {cnt} players")

    # 17. Industrial revolution + science/culture diffusion
    lines.append("\n--- 17. INDUSTRIAL REV + DIFFUSION ---")
    fi = data.get("final_industrial", [])
    if fi:
        lines.append(f"  Final IndustrialRev avg: {np.mean(fi):.2f} "
                     f"(max={max(fi):.2f}, min={min(fi):.2f})")
        started = sum(1 for v in fi if v > 0)
        lines.append(f"  Players w/ IR > 0      : {started}/{len(fi)}")
    if ts_s:
        lines.append(f"  {'Turn':>6s} {'IR':>8s} {'SciDiff':>8s} {'CultDiff':>8s}")
        for bucket in sorted(ts_s.keys()):
            row = ts_s[bucket]
            lines.append(
                f"  {bucket:>6d} "
                f"{np.mean(row.get('IndustrialRev', [0])):>8.2f} "
                f"{np.mean(row.get('ScienceDiffusion', [0])):>8.2f} "
                f"{np.mean(row.get('CultureDiffusion', [0])):>8.2f}"
            )

    # 18. Event trajectory per 25-turn bucket
    eot = data.get("events_over_time", {})
    if eot:
        lines.append("\n--- 18. EVENT TRAJECTORY ---")
        lines.append(f"  {'Turn':>6s} {'Prod':>5s} {'Kill':>5s} {'Tech':>5s} "
                     f"{'City':>5s} {'War':>5s} {'Peace':>5s} {'Met':>5s}")
        for bucket in sorted(eot.keys()):
            row = eot[bucket]
            lines.append(
                f"  {bucket:>6d} "
                f"{row.get('UnitProduced', 0):>5d} "
                f"{row.get('UnitKilled', 0):>5d} "
                f"{row.get('TechResearched', 0):>5d} "
                f"{row.get('CityFounded', 0):>5d} "
                f"{row.get('WarDeclared', 0):>5d} "
                f"{row.get('PeaceMade', 0):>5d} "
                f"{row.get('PlayersMet', 0):>5d}"
            )

    log = data.get("log_agg", _empty_log_agg())
    c = log["counters"]

    # 19. Religion
    lines.append("\n--- 19. RELIGION ---")
    lines.append(f"  Pantheons founded       : {c.get('religion_pantheon', 0)}")
    lines.append(f"  Religions founded       : {c.get('religion_founded', 0)}")
    if log["religions"]:
        rel_counts = defaultdict(int)
        for r in log["religions"]:
            rel_counts[r] += 1
        lines.append("  Religions seen:")
        for r, n in sorted(rel_counts.items(), key=lambda x: -x[1]):
            lines.append(f"    {r:20s}: {n}")
    lines.append(f"  Spread events           : {c.get('religion_spread', 0)}")

    # 20. Espionage
    lines.append("\n--- 20. ESPIONAGE ---")
    lines.append(f"  Spy missions assigned   : {c.get('espionage_assigned', 0)}")
    lines.append(f"  Successful missions     : {c.get('espionage_success', 0)}")
    lines.append(f"  Failed missions         : {c.get('espionage_failed', 0)}")
    if log["spy_missions"]:
        lines.append("  Top mission types:")
        for name, n in sorted(log["spy_missions"].items(),
                               key=lambda x: -x[1])[:5]:
            lines.append(f"    {name:30s}: {n}")

    # 21. Natural disasters + climate
    lines.append("\n--- 21. DISASTERS & CLIMATE ---")
    total_dis = sum(log["disasters"].values())
    lines.append(f"  Total natural disasters : {total_dis}")
    for kind, n in sorted(log["disasters"].items(), key=lambda x: -x[1]):
        lines.append(f"    {kind:15s}: {n}")
    lines.append(f"  Climate flood events    : {c.get('climate_flood', 0)}")

    # 22. Civics + Great People
    lines.append("\n--- 22. CIVICS & GREAT PEOPLE ---")
    lines.append(f"  Civics completed        : {c.get('civic_completed', 0)}")
    if log["civics"]:
        civ_counts = defaultdict(int)
        for v in log["civics"]:
            civ_counts[v] += 1
        lines.append("  Top civics:")
        for v, n in sorted(civ_counts.items(), key=lambda x: -x[1])[:5]:
            lines.append(f"    {v:25s}: {n}")
    lines.append(f"  Great People recruited  : {c.get('greatperson_recr', 0)}")
    lines.append(f"  Great People used       : {c.get('greatperson_used', 0)}")
    lines.append(f"  Eurekas banked          : {c.get('eureka_banked', 0)}")
    lines.append(f"  Inspirations banked     : {c.get('inspiration_banked', 0)}")

    # 23. Currency crisis / bonds / stock / speculation
    lines.append("\n--- 23. CURRENCY CRISIS / BONDS / STOCK / SPECULATION ---")
    lines.append(f"  Convertibility suspended: {c.get('currency_suspension', 0)}")
    lines.append(f"  Hyperinflation crises   : {c.get('currency_hyperinfl', 0)}")
    lines.append(f"  Currency reforms        : {c.get('currency_reform', 0)}")
    lines.append(f"  IOUs issued             : {c.get('bonds_issued', 0)} "
                 f"(total {log['bonds_sum']}g lent)")
    lines.append(f"  Bond defaults           : {c.get('bonds_defaulted', 0)}")
    lines.append(f"  Speculation bubbles form: {c.get('spec_bubble_form', 0)}")
    lines.append(f"  Speculation bubbles pop : {c.get('spec_bubble_burst', 0)}")
    lines.append(f"  Hoarded units (sum)     : {log['hoarded_total']}")
    lines.append(f"  Released units (sum)    : {log['released_total']}")
    lines.append(f"  Stock investments (sum) : {log['stock_invest_total']}g")

    # 24. Secession / grievances / world congress
    lines.append("\n--- 24. SECESSION / GRIEVANCES / WORLD CONGRESS ---")
    lines.append(f"  Secession warnings      : {c.get('secession_warning', 0)}")
    lines.append(f"  Cities flipped          : {c.get('secession_flip', 0)}")
    lines.append(f"  Cities freed            : {c.get('secession_free', 0)}")
    lines.append(f"  Grievances added        : {c.get('grievance_added', 0)}")
    lines.append(f"  WC proposals            : {c.get('world_congress_prop', 0)}")
    lines.append(f"  WC passed               : {c.get('world_congress_pass', 0)}")
    lines.append(f"  WC failed               : {c.get('world_congress_fail', 0)}")
    if log["wc_resolutions"]:
        wc_counts = defaultdict(lambda: [0, 0])
        for name, status in log["wc_resolutions"]:
            wc_counts[name][0 if status == "passed" else 1] += 1
        lines.append("  Resolution outcomes:")
        for name, (p, f_) in sorted(wc_counts.items(),
                                     key=lambda x: -(x[1][0] + x[1][1]))[:5]:
            lines.append(f"    {name:25s}: pass={p} fail={f_}")

    # 25. World events / goody huts / city-states
    lines.append("\n--- 25. WORLD EVENTS / GOODY HUTS / CITY-STATES ---")
    lines.append(f"  World events triggered  : {c.get('world_event', 0)}")
    if log["world_events_counts"]:
        lines.append("  Top world event types:")
        for name, n in sorted(log["world_events_counts"].items(),
                               key=lambda x: -x[1])[:5]:
            lines.append(f"    {name:30s}: {n}")
    lines.append(f"  Goody huts (gold)       : {c.get('goody_gold', 0)}")
    lines.append(f"  Goody huts (tech)       : {c.get('goody_tech', 0)}")
    lines.append(f"  Goody huts (map)        : {c.get('goody_map', 0)}")
    lines.append(f"  City-states spawned     : {c.get('citystate_spawn', 0)}")
    lines.append(f"  Canals built            : {c.get('canal_built', 0)}")

    # 26. Promotions / supply / combat extras
    lines.append("\n--- 26. PROMOTIONS / SUPPLY / COMBAT EXTRAS ---")
    lines.append(f"  Promotions chosen       : {c.get('promotion', 0)}")
    if log["promotions"]:
        lines.append("  Top promotions:")
        for p, n in sorted(log["promotions"].items(), key=lambda x: -x[1])[:5]:
            lines.append(f"    {p:20s}: {n}")
    lines.append(f"  Supply attrition ticks  : {c.get('supply_attrition', 0)}")
    lines.append(f"  Pillage events (unit)   : {c.get('combat_pillage_unit', 0)}")
    lines.append(f"  Pillage events (tile)   : {c.get('combat_pillage_tile', 0)}")
    lines.append(f"  Pillage gold (total)    : {log['pillage_gold_total']}")
    lines.append(f"  Defender kills          : {c.get('combat_defkilled', 0)}")
    lines.append(f"  Attacker kills          : {c.get('combat_atkkilled', 0)}")
    lines.append(f"  Nuclear strikes         : {c.get('nuclear_strike', 0)}")

    # 27. Trade / monopoly / commodity / stock
    lines.append("\n--- 27. TRADE / MONOPOLY / COMMODITY / STOCK ---")
    lines.append(f"  Bilateral trade deals   : {c.get('trade_deal', 0)}")
    lines.append(f"  Standing routes spawned : {c.get('trade_standing', 0)}")
    lines.append(f"  Monopolies formed       : {c.get('monopoly_form', 0)}")
    lines.append(f"  Monopolies broken       : {c.get('monopoly_broken', 0)}")
    if log["monopolies"]:
        good_counts = defaultdict(int)
        for pct, good in log["monopolies"]:
            good_counts[good] += 1
        lines.append("  Top monopolized goods:")
        for good, n in sorted(good_counts.items(), key=lambda x: -x[1])[:5]:
            lines.append(f"    {good:20s}: {n}")
    lines.append(f"  Commodity swaps         : {c.get('commodity_trade', 0)}")
    lines.append(f"  Stock investments (evt) : {c.get('stock_invest', 0)}")

    # 28. Cities: founding / growth / borders
    lines.append("\n--- 28. CITY LIFECYCLE ---")
    lines.append(f"  City foundings (log)    : {c.get('city_founded', 0)}")
    lines.append(f"  City growth events      : {c.get('city_grew', 0)}")
    lines.append(f"  Border expansions       : {c.get('border_expand', 0)}")
    lines.append(f"  Wonders built           : {c.get('wonder_built', 0)}")

    # 29. Victory / space race / IR milestones
    lines.append("\n--- 29. VICTORY PROGRESS ---")
    lines.append(f"  Era CSI evaluations     : {c.get('era_csi', 0)}")
    if log["era_csi"]:
        last_csi = log["era_csi"][-10:]
        lines.append("  Last 10 era winners:")
        for pid, csi in last_csi:
            lines.append(f"    player {pid}: CSI={csi:.2f}")
    lines.append(f"  Industrial Rev achieved : {c.get('ir_achieved', 0)}")
    if log["ir_turns"]:
        lines.append(f"    Earliest IR turn      : {min(log['ir_turns'])}")
        lines.append(f"    Latest IR turn        : {max(log['ir_turns'])}")
    lines.append(f"  Space race projects     : {c.get('space_project', 0)}")

    # 30. Log coverage summary — which source files logged at all?
    sc = log["source_counts"]
    if sc:
        lines.append("\n--- 30. LOG COVERAGE (source files emitting log lines) ---")
        all_sources = sorted(sc.items(), key=lambda x: -x[1])
        lines.append(f"  Total distinct sources  : {len(all_sources)}")
        lines.append(f"  Total log lines         : {sum(sc.values())}")
        lines.append("  Top 10:")
        for src, n in all_sources[:10]:
            lines.append(f"    {src:35s}: {n}")

    # 31. Issues detected
    lines.append("\n--- 31. ISSUES DETECTED ---")
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

    # IncomeGoodsEcon effectively dead across the run?
    goods_econ_values = data["income_sources"].get("Goods Econ", [])
    if goods_econ_values:
        nonzero = sum(1 for v in goods_econ_values if v > 0)
        if nonzero / len(goods_econ_values) < 0.05:
            issues.append(
                f"CRITICAL: IncomeGoodsEcon > 0 in only {nonzero}/{len(goods_econ_values)} "
                "final-turn samples. Consumer goods / clothing / electronics / processed "
                "food chains may never fire — AI may not build the required buildings."
            )

    # Inflation hitting a hard ceiling?
    inft = data.get("inflation_over_time", {})
    if inft:
        clamped_buckets = 0
        for bucket, vals in inft.items():
            if vals and max(vals) >= 0.499:
                clamped_buckets += 1
        if clamped_buckets >= 3:
            issues.append(
                f"WARNING: Inflation hits 0.500 in {clamped_buckets} turn buckets — "
                "looks like a hard cap. Verify this is intentional or inflation math is saturating."
            )

    # Military never engages (no kills despite production)?
    total_prod = sum(data.get("unit_type_produced", {}).values())
    total_killed = sum(data.get("unit_type_killed", {}).values())
    if total_prod > 50 and total_killed == 0:
        issues.append(
            "WARNING: Zero UnitKilled events despite "
            f"{total_prod} units produced. Combat may never fire — "
            "check AI war targets / movement."
        )

    # Diplomacy never happens?
    ev = data.get("event_totals", {})
    if ev.get("PlayersMet", 0) == 0:
        issues.append(
            "WARNING: Zero PlayersMet events. Either civs never meet "
            "(map too big) or first-contact notification is broken."
        )
    if ev.get("WarDeclared", 0) == 0:
        issues.append(
            "INFO: Zero WarDeclared events — fully peaceful run. "
            "Expected for small maps but unusual on multi-civ games."
        )

    # Tech research stalled?
    tt = data.get("tech_research_turns", [])
    ft = data.get("final_tech", [])
    if ft and np.mean(ft) < 5 and tt and max(tt) > 100:
        issues.append(
            f"WARNING: Avg final tech count {np.mean(ft):.1f} despite "
            f"{max(tt)} turns elapsed. Research pipeline may be blocked."
        )

    # Culture diffusion dead?
    ts_s = data.get("ts_scalar", {})
    if ts_s:
        all_cult_diff = []
        all_sci_diff = []
        for bucket in ts_s:
            all_cult_diff.extend(ts_s[bucket].get("CultureDiffusion", []))
            all_sci_diff.extend(ts_s[bucket].get("ScienceDiffusion", []))
        if all_cult_diff and sum(all_cult_diff) == 0:
            issues.append(
                "INFO: CultureDiffusion stays 0 across the run. "
                "Cultural contact may not be registering."
            )
        if all_sci_diff and sum(all_sci_diff) == 0:
            issues.append(
                "INFO: ScienceDiffusion stays 0 across the run. "
                "Tech-leakage loop may be dead."
            )

    # Industrial Revolution never fires?
    fi = data.get("final_industrial", [])
    if fi and all(v == 0 for v in fi):
        issues.append(
            "WARNING: IndustrialRev stays 0 for every player at game end. "
            "Industrial Revolution triggers may never fire — check thresholds."
        )

    # Any player dies to famine?
    fam = data.get("famine_events", [])
    if fam and sum(1 for f in fam if f > 0) > len(fam) * 0.3:
        issues.append(
            "WARNING: >30% of players famine at game end. Food balance or "
            "granary building may be broken."
        )

    # Happiness collapse?
    fh = data.get("final_happiness", [])
    if fh and np.mean(fh) < 3:
        issues.append(
            f"WARNING: Avg final happiness {np.mean(fh):.1f} (< 3). "
            "Amenity supply or demand curve may be out of balance."
        )

    # Population runaway?
    popt = data.get("pop_over_time", {})
    if popt:
        latest_bucket = max(popt.keys())
        latest = popt[latest_bucket]
        if latest and max(latest) > 400:
            issues.append(
                f"WARNING: Max population {max(latest)} at end — housing/amenity "
                "cap may not bind. Check CityGrowth housing math."
            )

    # Crisis never triggers?
    fcr = data.get("final_crisis", [])
    if fcr and all(v == 0 for v in fcr):
        pass  # not an issue by itself; crises are rare

    # Food went deeply negative mid-game (famine cascade)?
    if ts_s:
        for bucket in sorted(ts_s.keys()):
            food_vals = ts_s[bucket].get("FoodPerTurn", [])
            if food_vals and np.mean(food_vals) < -20:
                issues.append(
                    f"WARNING: FoodPerTurn avg {np.mean(food_vals):.1f} at turn "
                    f"bucket {bucket} — cities starving. Check food yield vs "
                    "population growth pacing."
                )
                break

    # CoinTier distribution: any player stuck at None long after MonSys left Barter?
    tsd = data.get("ts_discrete", {})
    if tsd:
        late_buckets = [b for b in tsd if b >= 300]
        stuck_none = 0
        for b in late_buckets:
            stuck_none += tsd[b].get("CoinTier", {}).get(0, 0)
        if late_buckets and stuck_none > 0:
            issues.append(
                f"INFO: {stuck_none} (player,turn) samples with CoinTier=None "
                "after turn 300. Expected 0 once coinage transitions fire."
            )

    # Log-based detectors (non-economic mechanics).
    if c.get("religion_pantheon", 0) == 0:
        issues.append(
            "WARNING: Zero pantheons founded. Religion loop may be dead — "
            "check FaithSystem / Religion.cpp AI founding path.")
    if c.get("religion_founded", 0) == 0 and c.get("religion_pantheon", 0) > 0:
        issues.append(
            "WARNING: Pantheons founded but zero religions founded. "
            "Religion gate (faith threshold?) may be too high.")
    if c.get("espionage_assigned", 0) == 0:
        issues.append(
            "WARNING: Zero spy missions assigned. Espionage AI never fires — "
            "check EspionageSystem mission selection.")
    if c.get("civic_completed", 0) == 0:
        issues.append(
            "WARNING: Zero civics completed. Civic research never advances — "
            "check CivicTree progression / culture accumulation.")
    if c.get("greatperson_recr", 0) == 0:
        issues.append(
            "INFO: Zero Great People recruited. Great Person points may "
            "not be generated or threshold too high.")
    if c.get("world_event", 0) == 0:
        issues.append(
            "INFO: Zero world events triggered. Random event roll may be "
            "disabled or frequency=0.")
    if c.get("world_congress_prop", 0) == 0:
        issues.append(
            "INFO: Zero World Congress proposals. Congress never convenes — "
            "check activation threshold (requires diplomatic contact).")
    if sum(log["disasters"].values()) == 0:
        issues.append(
            "INFO: Zero natural disasters. NaturalDisasters loop may be "
            "disabled or probability=0.")
    if c.get("secession_flip", 0) + c.get("secession_free", 0) == 0 \
            and c.get("secession_warning", 0) > 0:
        issues.append(
            "INFO: Secession warnings fired but no cities ever flipped — "
            "loyalty floor may prevent actual revolts.")
    if c.get("bonds_issued", 0) == 0:
        issues.append(
            "INFO: Zero IOUs issued. Bond/lending market inactive — "
            "AI may never enter BondIssuer role.")
    if c.get("spec_bubble_form", 0) > 0 and c.get("spec_bubble_burst", 0) == 0:
        issues.append(
            "WARNING: Speculation bubbles form but never burst. "
            "Check SpeculationBubble burst condition.")
    if c.get("promotion", 0) == 0 and sum(
            data.get("unit_type_produced", {}).values()) > 10:
        issues.append(
            "WARNING: Zero unit promotions despite units produced. "
            "XP / promotion-eligibility path may be broken.")
    if c.get("supply_attrition", 0) == 0:
        issues.append(
            "INFO: Zero supply attrition. Either all units stay near home "
            "OR SupplyLines attrition check is disabled.")
    if c.get("monopoly_form", 0) == 0:
        issues.append(
            "INFO: Zero monopolies formed. MonopolyPricing detection may "
            "never trigger — check market share threshold.")
    if c.get("commodity_trade", 0) == 0:
        issues.append(
            "INFO: Zero commodity swaps. CommodityExchange never matches — "
            "bids/asks may not overlap.")
    if c.get("stock_invest", 0) == 0:
        issues.append(
            "INFO: Zero stock-market investments. StockMarket AI never "
            "invests — check AIInvestmentController gating.")
    if c.get("trade_deal", 0) == 0:
        issues.append(
            "WARNING: Zero bilateral trade deals. TradeAgreement AI "
            "negotiates nothing — check acceptance logic.")
    if c.get("city_founded", 0) == 0:
        issues.append(
            "CRITICAL: Zero cities founded in log. Settler AI broken.")
    if c.get("border_expand", 0) == 0:
        issues.append(
            "WARNING: Zero border expansions. Culture bomb or territory "
            "growth may be broken.")
    if c.get("ir_achieved", 0) == 0:
        issues.append(
            "INFO: No player achieved Industrial Revolution via log — "
            "cross-check with IndustrialRev scalar in section 17.")
    if c.get("currency_hyperinfl", 0) == 0 and c.get("currency_reform", 0) > 0:
        issues.append(
            "NOTE: Currency reform fired without hyperinflation crisis — "
            "verify trigger ordering.")
    if c.get("canal_built", 0) == 0:
        issues.append(
            "INFO: Zero canals built. TerrainModification AI may never "
            "consider canals — low priority but signals dead feature.")
    if c.get("nuclear_strike", 0) > 0:
        issues.append(
            f"NOTE: {c.get('nuclear_strike', 0)} nuclear strikes fired. "
            "Verify AI restraint is tuned.")

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
    parser.add_argument("--analyze", nargs="+", metavar="CSV",
                        help="Analyze existing CSV file(s) instead of running sims. "
                             "Paths are not deleted after analysis. The companion "
                             "_events.csv (if present alongside) is auto-loaded.")
    args = parser.parse_args()

    if args.quick:
        args.sims = 5
        args.turns = 200
        args.workers = 4

    if args.workers <= 0:
        args.workers = max(1, multiprocessing.cpu_count() - 1)

    print("=" * 60)
    print("Age of Civilization — Economic Diagnostic Tool")
    print("=" * 60)

    if args.analyze:
        csv_paths = [p for p in args.analyze if os.path.exists(p)]
        missing = [p for p in args.analyze if not os.path.exists(p)]
        for p in missing:
            print(f"[Warn] Not found: {p}")
        if not csv_paths:
            print("[Error] No readable CSV files provided")
            sys.exit(1)
        print(f"[Analyzing] {len(csv_paths)} pre-run CSV file(s) (not deleting)...")
        data = analyze_simulations(csv_paths, args.turns, delete_after=False)
        report = generate_report(data, len(csv_paths))
        print(f"\n{report}")
        with open(args.output, "w") as f:
            f.write(report)
        print(f"\n[Saved] {args.output}")
        return

    if not os.path.exists(SIMULATOR_PATH):
        print(f"[Error] Simulator not found: {SIMULATOR_PATH}")
        sys.exit(1)

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
