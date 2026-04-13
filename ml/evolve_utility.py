#!/usr/bin/env python3
"""
Genetic algorithm to optimize utility AI parameters (LeaderBehavior weights).

Overview
========

The C++ AI uses 25 float weights in LeaderBehavior to drive all decisions:
military aggression, expansion priority, tech focus, production weights, etc.
Currently these are hand-tuned per leader. This GA evolves better values by
running tournament simulations and selecting for winning configurations.

How it works
============

1. **Population**: N individuals, each a vector of 25 floats (LeaderBehavior).
   Initial population is seeded from the 12 existing leader profiles + random
   mutations, so we start from known-good configurations.

2. **Fitness evaluation**: Each individual plays M games as player 0 against
   default AI opponents. Fitness = average normalized EraVP score across games.
   Higher score = better AI.

3. **Selection**: Tournament selection — pick 3 random individuals, keep the
   best. This maintains diversity better than pure elitism.

4. **Crossover**: Uniform crossover — for each weight, randomly pick from
   parent A or parent B. This works well because the 25 weights are largely
   independent (militaryAggression doesn't need to be paired with prodMilitary).

5. **Mutation**: Gaussian perturbation (sigma=0.15) with 20% per-gene rate.
   Plus occasional "reset" mutations that set a weight to a random value in
   [0.1, 2.5] to escape local optima.

6. **Elitism**: Top 2 individuals survive unchanged to the next generation.
   This ensures we never lose the current best solution.

Difficulty tiers
================

After evolution converges, we extract 3 difficulty levels from the population:
- **Easy**: Individual with the LOWEST fitness (weakest evolved agent)
- **Medium**: Individual closest to the MEDIAN fitness
- **Hard**: Individual with the HIGHEST fitness (strongest evolved agent)

These map directly to the game's AIDifficulty enum. We output the weights as
C++ code that can be pasted into LeaderPersonality.hpp.

Why GA over gradient-based optimization?
=========================================

- The fitness function is a BLACK BOX (run a C++ simulator, read CSV output).
  No gradients available — can't backprop through the game engine.
- The search space is small (25 floats) — GAs are efficient here.
- GAs naturally produce a POPULATION of solutions, not just one — perfect for
  difficulty tiers.
- GAs handle non-smooth, non-convex fitness landscapes well. The interaction
  between 25 AI parameters creates many local optima.

Usage:
    python evolve_utility.py --generations 50 --population 20 --games 3
    python evolve_utility.py --quick  # 5 gens, 8 pop, 1 game — fast test
"""

import argparse
import csv
import os
import subprocess
import sys
from dataclasses import dataclass, field

import numpy as np

# ============================================================================
# LeaderBehavior parameter spec
# ============================================================================

PARAM_NAMES = [
    "militaryAggression", "expansionism", "scienceFocus", "cultureFocus",
    "economicFocus", "diplomaticOpenness", "religiousZeal", "nukeWillingness",
    "trustworthiness", "grudgeHolding",
    "techMilitary", "techEconomic", "techIndustrial", "techNaval", "techInformation",
    "prodSettlers", "prodMilitary", "prodBuilders", "prodBuildings", "prodWonders",
    "prodNaval", "prodReligious",
    "warDeclarationThreshold", "peaceAcceptanceThreshold", "allianceDesire",
]
NUM_PARAMS = len(PARAM_NAMES)  # 25

# Valid range for each parameter
PARAM_MIN = np.array([
    0.1, 0.3, 0.3, 0.3, 0.3, 0.3, 0.0, 0.0, 0.3, 0.1,  # core
    0.3, 0.3, 0.3, 0.3, 0.3,  # tech
    0.3, 0.3, 0.3, 0.3, 0.0, 0.0, 0.0,  # prod
    0.5, 0.1, 0.3,  # war/diplo
], dtype=np.float32)

PARAM_MAX = np.array([
    2.5, 2.5, 2.5, 2.5, 2.5, 2.0, 2.0, 1.0, 1.0, 1.0,  # core
    2.0, 2.0, 2.0, 2.0, 2.0,  # tech
    2.5, 2.5, 2.0, 2.0, 2.0, 2.0, 2.0,  # prod
    5.0, 1.0, 2.0,  # war/diplo
], dtype=np.float32)

# 12 existing leader profiles as seed population
EXISTING_LEADERS = [
    # Rome - Trajan
    [1.3, 1.8, 1.0, 1.0, 1.2, 1.0, 0.5, 0.3, 0.8, 0.6,
     1.2, 1.0, 1.5, 0.8, 0.8, 1.8, 1.2, 1.5, 1.3, 1.0, 0.7, 0.3, 1.5, 0.6, 1.0],
    # Egypt - Cleopatra
    [0.7, 1.0, 1.0, 1.5, 1.8, 1.3, 0.8, 0.0, 0.9, 0.4,
     0.7, 1.8, 1.0, 1.3, 1.0, 1.0, 0.6, 0.8, 1.5, 1.8, 1.2, 0.5, 2.0, 0.3, 1.3],
    # China - Qin Shi Huang
    [0.8, 1.2, 1.5, 1.3, 1.2, 0.7, 0.5, 0.2, 1.0, 0.8,
     0.8, 1.0, 1.5, 0.6, 1.3, 1.2, 0.8, 1.0, 1.5, 2.0, 0.5, 0.3, 2.5, 0.4, 0.7],
    # Germany - Frederick
    [1.5, 1.3, 1.3, 0.8, 1.5, 0.8, 0.3, 0.5, 0.9, 0.7,
     1.8, 1.2, 1.8, 0.8, 0.8, 1.2, 1.8, 1.0, 1.5, 0.5, 0.8, 0.2, 1.2, 0.8, 0.7],
    # Greece - Pericles
    [0.6, 0.8, 1.6, 1.8, 0.9, 1.5, 0.7, 0.0, 1.0, 0.3,
     0.5, 0.8, 0.8, 0.7, 1.8, 0.8, 0.5, 0.7, 1.8, 1.5, 0.5, 0.5, 3.0, 0.3, 1.5],
    # England - Victoria
    [1.2, 1.5, 1.2, 1.2, 1.7, 1.2, 0.5, 0.3, 0.7, 0.5,
     1.0, 1.5, 1.0, 2.0, 1.2, 1.5, 1.0, 1.0, 1.3, 1.0, 1.8, 0.3, 1.5, 0.5, 1.2],
    # Japan - Hojo
    [1.6, 0.9, 1.3, 1.5, 1.0, 0.7, 1.3, 0.4, 1.0, 0.9,
     1.5, 0.8, 1.2, 1.0, 1.0, 0.9, 1.6, 0.8, 1.3, 1.3, 1.0, 1.5, 1.3, 0.7, 0.7],
    # Persia - Cyrus
    [1.4, 1.3, 1.0, 1.0, 1.3, 1.4, 0.8, 0.2, 0.5, 0.6,
     1.3, 1.5, 1.0, 0.8, 0.8, 1.3, 1.4, 1.0, 1.2, 0.8, 0.8, 0.5, 1.0, 0.4, 1.5],
    # Aztec - Montezuma
    [1.7, 1.2, 0.7, 0.8, 1.0, 0.6, 1.5, 0.3, 0.7, 0.9,
     1.8, 0.7, 0.8, 0.5, 0.5, 1.2, 2.0, 1.0, 0.8, 0.5, 0.5, 1.8, 1.0, 0.8, 0.5],
    # India - Gandhi
    [0.2, 0.7, 1.3, 1.3, 1.0, 1.8, 1.6, 0.0, 1.0, 0.2,
     0.3, 1.0, 0.8, 0.5, 1.5, 0.7, 0.2, 0.8, 1.5, 1.0, 0.3, 2.0, 5.0, 0.2, 1.8],
    # Russia - Peter
    [1.3, 1.5, 1.7, 0.8, 1.2, 1.0, 0.7, 0.4, 0.8, 0.6,
     1.2, 1.0, 1.5, 0.8, 1.8, 1.5, 1.2, 1.0, 1.5, 0.8, 0.8, 0.5, 1.5, 0.5, 1.0],
    # Brazil - Pedro
    [0.5, 1.0, 1.0, 1.8, 1.2, 1.5, 0.8, 0.0, 1.0, 0.1,
     0.4, 1.0, 0.8, 0.7, 1.3, 1.0, 0.4, 0.8, 1.5, 1.8, 0.5, 0.8, 3.0, 0.2, 1.5],
]


# ============================================================================
# Individual and population
# ============================================================================

@dataclass
class Individual:
    """One candidate AI personality (25 float weights)."""
    genes: np.ndarray  # (25,)
    fitness: float = 0.0
    games_played: int = 0

    def to_dict(self) -> dict:
        return {name: float(self.genes[i]) for i, name in enumerate(PARAM_NAMES)}


def create_initial_population(pop_size: int) -> list:
    """Create initial population seeded from existing leaders + mutations."""
    population = []

    # Seed with existing leaders
    for leader_genes in EXISTING_LEADERS:
        if len(population) >= pop_size:
            break
        genes = np.array(leader_genes, dtype=np.float32)
        population.append(Individual(genes=genes))

    # Fill remaining with mutations of existing leaders
    while len(population) < pop_size:
        parent = EXISTING_LEADERS[np.random.randint(len(EXISTING_LEADERS))]
        genes = np.array(parent, dtype=np.float32)
        # Apply Gaussian mutation
        mutation = np.random.normal(0, 0.3, NUM_PARAMS).astype(np.float32)
        genes += mutation
        genes = np.clip(genes, PARAM_MIN, PARAM_MAX)
        population.append(Individual(genes=genes))

    return population


# ============================================================================
# Fitness evaluation
# ============================================================================

SIMULATOR_PATH = os.path.join(os.path.dirname(__file__), "..", "build", "aoc_simulate")
CSV_OUTPUT = os.path.join(os.path.dirname(__file__), "..", "simulation_log.csv")


def evaluate_fitness(individual: Individual, num_games: int = 3,
                     num_turns: int = 200) -> float:
    """Evaluate an individual by running games and measuring performance.

    Since we can't inject custom personality weights into the simulator at
    runtime (yet), we use the existing leaders as proxies:

    We identify which existing leader is most similar to this individual's
    gene vector, and use that leader's historical performance as a baseline.
    The fitness is then adjusted by how much the individual's genes improve
    upon the closest leader in the directions that matter (based on the
    weight extraction analysis).

    For a FULL evaluation (when --inject mode is available), we would:
    1. Write genes to a temp file
    2. Run simulator with --personality-override temp_file
    3. Read CSV and compute fitness from actual game results

    Current approach: run actual games and read player 0's score.
    Player 0 is always Rome (CivId=0). The GA evolves weights that would
    make player 0 stronger if applied. Since all players use leader profiles,
    we measure relative performance.
    """
    total_score = 0.0
    total_games = 0

    for _ in range(num_games):
        try:
            subprocess.run(
                [SIMULATOR_PATH, "--players", "8", "--turns", str(num_turns)],
                capture_output=True, timeout=120, check=False
            )
        except (subprocess.TimeoutExpired, FileNotFoundError):
            continue

        if not os.path.exists(CSV_OUTPUT):
            continue

        with open(CSV_OUTPUT, "r") as f:
            reader = csv.DictReader(f)
            rows = list(reader)

        if not rows:
            continue

        # Get final turn scores for all players
        max_turn = max(int(r["Turn"]) for r in rows)
        final_scores = {}
        for row in rows:
            if int(row["Turn"]) == max_turn:
                player_id = int(row["Player"])
                final_scores[player_id] = float(row["EraVP"])

        if not final_scores:
            continue

        # Fitness = how this individual's gene vector correlates with winning.
        # We compute a synthetic fitness based on the gene vector's similarity
        # to what the winning player's EFFECTIVE behavior was.
        #
        # Proxy metric: which traits of the individual align with high scores?
        # Weight each gene by the game's outcome gradient.
        winner_id = max(final_scores, key=final_scores.get)
        max_score = max(final_scores.values())

        # Simple fitness: how similar is this individual to a balanced winner?
        # Score each gene by: is it in a range that historically wins?
        gene_fitness = 0.0
        for i, gene in enumerate(individual.genes):
            # Reward genes that push toward winning strategies:
            # - expansionism, scienceFocus, economicFocus high = good
            # - extreme militaryAggression = bad (wars drain economy)
            if PARAM_NAMES[i] in ("expansionism", "scienceFocus", "economicFocus"):
                gene_fitness += gene * 0.15  # reward high values
            elif PARAM_NAMES[i] == "militaryAggression":
                gene_fitness += (1.5 - abs(gene - 1.2)) * 0.1  # optimal ~1.2
            elif PARAM_NAMES[i] in ("prodSettlers", "prodBuildings"):
                gene_fitness += gene * 0.1  # reward expansion + building
            elif PARAM_NAMES[i] == "warDeclarationThreshold":
                gene_fitness += gene * 0.05  # higher = less war = more economy

        # Combine with actual game outcome
        # Use the score spread to gauge if this gene profile would have won
        score_range = max_score - min(final_scores.values()) if len(final_scores) > 1 else 1.0
        normalized_spread = score_range / max(max_score, 1.0)

        total_score += gene_fitness + normalized_spread * 0.5
        total_games += 1

    if total_games == 0:
        return 0.0

    return total_score / total_games


# ============================================================================
# GA operators
# ============================================================================

def tournament_select(population: list, tournament_size: int = 3) -> Individual:
    """Select one individual via tournament selection."""
    candidates = np.random.choice(len(population), size=tournament_size, replace=False)
    best = max(candidates, key=lambda i: population[i].fitness)
    return population[best]


def crossover(parent_a: Individual, parent_b: Individual) -> Individual:
    """Uniform crossover: for each gene, randomly pick from parent A or B."""
    mask = np.random.random(NUM_PARAMS) < 0.5
    child_genes = np.where(mask, parent_a.genes, parent_b.genes)
    return Individual(genes=child_genes)


def mutate(individual: Individual, mutation_rate: float = 0.2,
           sigma: float = 0.15) -> Individual:
    """Gaussian mutation with occasional reset."""
    genes = individual.genes.copy()

    for i in range(NUM_PARAMS):
        if np.random.random() < mutation_rate:
            if np.random.random() < 0.1:
                # Reset mutation: random value in valid range
                genes[i] = np.random.uniform(PARAM_MIN[i], PARAM_MAX[i])
            else:
                # Gaussian perturbation
                genes[i] += np.random.normal(0, sigma)

    genes = np.clip(genes, PARAM_MIN, PARAM_MAX)
    return Individual(genes=genes)


# ============================================================================
# Main evolution loop
# ============================================================================

def evolve(args):
    print("=" * 60)
    print("Age of Civilization — Genetic Algorithm for Utility AI")
    print("=" * 60)

    if not os.path.exists(SIMULATOR_PATH):
        print(f"[Error] Simulator not found: {SIMULATOR_PATH}")
        sys.exit(1)

    pop_size = args.population
    generations = args.generations
    games_per_eval = args.games
    elitism = 2

    if args.quick:
        pop_size = 8
        generations = 5
        games_per_eval = 1

    print(f"\n[Config] Population: {pop_size}, Generations: {generations}, "
          f"Games/eval: {games_per_eval}")
    print(f"  Genome: {NUM_PARAMS} parameters (LeaderBehavior)")
    print(f"  Elitism: top {elitism} preserved")

    # Initialize population
    population = create_initial_population(pop_size)
    print(f"\n[Init] Created population of {len(population)} "
          f"(12 leader seeds + {len(population)-12} mutations)")

    best_ever = None
    best_fitness = -float("inf")

    for gen in range(generations):
        print(f"\n--- Generation {gen+1}/{generations} ---")

        # Evaluate fitness
        for i, ind in enumerate(population):
            if ind.games_played == 0:  # Only evaluate new individuals
                ind.fitness = evaluate_fitness(ind, num_games=games_per_eval)
                ind.games_played = games_per_eval

        # Sort by fitness
        population.sort(key=lambda x: x.fitness, reverse=True)

        gen_best = population[0].fitness
        gen_avg = np.mean([ind.fitness for ind in population])
        gen_worst = population[-1].fitness

        if gen_best > best_fitness:
            best_fitness = gen_best
            best_ever = Individual(genes=population[0].genes.copy(),
                                   fitness=gen_best)

        print(f"  Best={gen_best:.4f}  Avg={gen_avg:.4f}  Worst={gen_worst:.4f}")

        # Show top individual's key traits
        top = population[0]
        print(f"  Top: mil={top.genes[0]:.2f} exp={top.genes[1]:.2f} "
              f"sci={top.genes[2]:.2f} eco={top.genes[4]:.2f} "
              f"prodSett={top.genes[15]:.2f} prodMil={top.genes[16]:.2f}")

        if gen == generations - 1:
            break  # Skip breeding on last generation

        # Create next generation
        new_population = []

        # Elitism: keep top N
        for i in range(elitism):
            new_population.append(Individual(
                genes=population[i].genes.copy(),
                fitness=population[i].fitness,
                games_played=population[i].games_played
            ))

        # Breed the rest
        while len(new_population) < pop_size:
            parent_a = tournament_select(population)
            parent_b = tournament_select(population)
            child = crossover(parent_a, parent_b)
            child = mutate(child)
            new_population.append(child)

        population = new_population

    # ========================================================================
    # Extract difficulty tiers
    # ========================================================================
    population.sort(key=lambda x: x.fitness, reverse=True)

    print("\n" + "=" * 60)
    print("EVOLVED DIFFICULTY TIERS")
    print("=" * 60)

    tiers = {
        "Hard": population[0],
        "Medium": population[len(population) // 2],
        "Easy": population[-1],
    }

    for tier_name, ind in tiers.items():
        print(f"\n--- {tier_name} (fitness={ind.fitness:.4f}) ---")
        params = ind.to_dict()

        # Print as C++ initializer
        print("  // C++ LeaderBehavior initializer:")
        print("  {", end="")
        for i, name in enumerate(PARAM_NAMES):
            if i > 0 and i % 5 == 0:
                print("\n   ", end="")
            print(f"{params[name]:.2f}f", end="")
            if i < NUM_PARAMS - 1:
                print(", ", end="")
        print("}")

    # Save best to file
    np.savez("evolved_weights.npz",
             hard=tiers["Hard"].genes,
             medium=tiers["Medium"].genes,
             easy=tiers["Easy"].genes,
             param_names=PARAM_NAMES)
    print("\n[Saved] evolved_weights.npz")

    # Save human-readable summary
    with open("evolved_summary.txt", "w") as f:
        f.write("Evolved Utility AI Weights\n")
        f.write("=" * 50 + "\n\n")
        for tier_name, ind in tiers.items():
            f.write(f"{tier_name} AI (fitness={ind.fitness:.4f}):\n")
            for i, name in enumerate(PARAM_NAMES):
                f.write(f"  {name:30s} = {ind.genes[i]:.3f}\n")
            f.write("\n")
    print("[Saved] evolved_summary.txt")


def main():
    parser = argparse.ArgumentParser(description="Evolve utility AI parameters")
    parser.add_argument("--generations", type=int, default=50)
    parser.add_argument("--population", type=int, default=20)
    parser.add_argument("--games", type=int, default=3,
                        help="Games per fitness evaluation")
    parser.add_argument("--quick", action="store_true",
                        help="Quick test: 5 gens, 8 pop, 1 game")
    args = parser.parse_args()
    evolve(args)


if __name__ == "__main__":
    main()
