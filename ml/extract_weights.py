#!/usr/bin/env python3
"""
Extract learned knowledge from trained models back into the utility AI.

This script analyzes a trained CivTransformer model and produces:

1. **Feature importance by game phase** — which features matter most in early,
   mid, and late game for predicting a winner. Maps directly to adjusting
   AI advisor update frequencies and utility curve weights.

2. **Attention patterns** — which turns the model considers most predictive.
   Reveals whether early expansion or late-game economy matters more.

3. **Gradient-based sensitivity** — "if I increase military by 1 unit at
   turn X, how much does my predicted win probability change?" Directly
   calibrates scoreMilitary(), scoreSettler(), etc. base weights.

4. **Recommended LeaderBehavior adjustments** — concrete weight suggestions
   that can be copy-pasted into the C++ AI code.

Usage:
    python extract_weights.py --model best_model.pt --data-dir data/
"""

import argparse
import csv
import glob
import os
import sys

import numpy as np
import torch

# Import from our training pipeline
from train_supervised import (
    CivTransformer, CivDataset, load_all_simulations,
    FEATURE_COLUMNS, NUM_FEATURES
)


def detect_device() -> torch.device:
    if torch.cuda.is_available():
        return torch.device("cuda")
    if hasattr(torch.backends, "mps") and torch.backends.mps.is_available():
        return torch.device("mps")
    return torch.device("cpu")


# ============================================================================
# Feature importance via gradient attribution
# ============================================================================

def compute_feature_importance(model: CivTransformer, dataset: CivDataset,
                               device: torch.device) -> dict:
    """Compute feature importance by averaging gradients of win probability w.r.t. inputs.

    For each game phase (early: turns 1-100, mid: 100-200, late: 200-300),
    compute which input features have the largest gradient magnitude when
    predicting the win class. Larger gradient = more influence on prediction.

    Returns:
        {
            "early": np.ndarray (NUM_FEATURES,) — importance per feature,
            "mid":   np.ndarray (NUM_FEATURES,),
            "late":  np.ndarray (NUM_FEATURES,),
            "overall": np.ndarray (NUM_FEATURES,),
        }
    """
    model.eval()
    model.to(device)

    # Phase boundaries (turn indices)
    phases = {
        "early": (0, 100),
        "mid": (100, 200),
        "late": (200, 300),
    }

    importance = {name: np.zeros(NUM_FEATURES) for name in phases}
    importance["overall"] = np.zeros(NUM_FEATURES)
    count = 0

    for i in range(len(dataset)):
        x_np = dataset.normalize(dataset.samples[i])
        x = torch.from_numpy(x_np).unsqueeze(0).to(device).requires_grad_(True)

        win_logits, _ = model(x)
        # Gradient of win class (index 1) w.r.t. input
        win_prob = torch.softmax(win_logits, dim=1)[0, 1]
        win_prob.backward()

        grad = x.grad[0].detach().cpu().numpy()  # (T, F)

        for phase_name, (t_start, t_end) in phases.items():
            t_end_actual = min(t_end, grad.shape[0])
            if t_start < t_end_actual:
                phase_grad = np.abs(grad[t_start:t_end_actual]).mean(axis=0)
                importance[phase_name] += phase_grad

        importance["overall"] += np.abs(grad).mean(axis=0)
        count += 1

    # Normalize
    for key in importance:
        if count > 0:
            importance[key] /= count
        # Normalize to [0, 1]
        max_val = importance[key].max()
        if max_val > 0:
            importance[key] /= max_val

    return importance


# ============================================================================
# Attention pattern analysis
# ============================================================================

def analyze_attention_patterns(model: CivTransformer, dataset: CivDataset,
                                device: torch.device, num_samples: int = 20) -> dict:
    """Extract attention weights to see which turns the model attends to most.

    Returns:
        {
            "winners_attention": np.ndarray (num_turns,) — avg attention for winners,
            "losers_attention":  np.ndarray (num_turns,) — avg attention for losers,
            "critical_turns":    list of int — turns with highest attention delta,
        }
    """
    model.eval()
    model.to(device)

    # Hook to capture attention weights from the transformer
    attention_maps = []

    def attention_hook(module, input_args, output):
        # TransformerEncoderLayer returns output tensor; we need the attention
        # weights which aren't directly exposed. Instead, we'll use the
        # input projection's gradient as a proxy for attention.
        pass

    # Alternative: use gradient-based attention proxy (Grad-SAM style)
    max_turns = dataset.samples.shape[1]
    winner_turn_importance = np.zeros(max_turns)
    loser_turn_importance = np.zeros(max_turns)
    winner_count = 0
    loser_count = 0

    num_samples = min(num_samples, len(dataset))

    for i in range(num_samples):
        x_np = dataset.normalize(dataset.samples[i])
        x = torch.from_numpy(x_np).unsqueeze(0).to(device).requires_grad_(True)
        is_winner = dataset.labels_win[i] == 1

        win_logits, _ = model(x)
        win_prob = torch.softmax(win_logits, dim=1)[0, 1]
        win_prob.backward()

        # Per-turn importance = L2 norm of gradient across features
        grad = x.grad[0].detach().cpu().numpy()  # (T, F)
        turn_importance = np.linalg.norm(grad, axis=1)  # (T,)

        if is_winner:
            winner_turn_importance += turn_importance
            winner_count += 1
        else:
            loser_turn_importance += turn_importance
            loser_count += 1

    if winner_count > 0:
        winner_turn_importance /= winner_count
    if loser_count > 0:
        loser_turn_importance /= loser_count

    # Find turns where winners diverge most from losers
    delta = winner_turn_importance - loser_turn_importance
    critical_turns = list(np.argsort(-delta)[:10])

    return {
        "winners_attention": winner_turn_importance,
        "losers_attention": loser_turn_importance,
        "critical_turns": critical_turns,
    }


# ============================================================================
# Sensitivity analysis: marginal value of each game action
# ============================================================================

def compute_action_sensitivity(model: CivTransformer, dataset: CivDataset,
                                device: torch.device) -> dict:
    """Compute how much each feature change affects win probability.

    For each feature, compute: d(win_prob) / d(feature) averaged across
    all samples and turns. This directly tells us the marginal value of
    adding 1 unit of military, 1 city, 1 tech, etc.

    Returns dict mapping feature name to average gradient.
    """
    model.eval()
    model.to(device)

    avg_grad = np.zeros(NUM_FEATURES)
    count = 0

    for i in range(len(dataset)):
        x_np = dataset.normalize(dataset.samples[i])
        x = torch.from_numpy(x_np).unsqueeze(0).to(device).requires_grad_(True)

        win_logits, _ = model(x)
        win_prob = torch.softmax(win_logits, dim=1)[0, 1]
        win_prob.backward()

        grad = x.grad[0].detach().cpu().numpy()  # (T, F)
        # Average across time: mean sensitivity per feature
        avg_grad += grad.mean(axis=0)
        count += 1

    if count > 0:
        avg_grad /= count

    # Un-normalize: multiply by std to get real-unit sensitivity
    sensitivity = {}
    for fi, name in enumerate(FEATURE_COLUMNS):
        sensitivity[name] = avg_grad[fi] * dataset.feature_std[fi]

    return sensitivity


# ============================================================================
# Generate C++ weight recommendations
# ============================================================================

def generate_recommendations(importance: dict, sensitivity: dict) -> str:
    """Generate human-readable recommendations for tuning the C++ utility AI.

    Maps learned feature importance to specific code changes.
    """
    lines = []
    lines.append("=" * 60)
    lines.append("RECOMMENDED UTILITY AI WEIGHT ADJUSTMENTS")
    lines.append("Based on trained model analysis")
    lines.append("=" * 60)

    # Map features to AI scoring functions
    feature_to_scorer = {
        "Military": ("scoreMilitary", "BASE_WEIGHT"),
        "Cities": ("scoreSettler", "BASE_WEIGHT"),
        "TechsResearched": ("scoreBuildingCandidate", "science building boost"),
        "Treasury": ("considerPurchases", "purchase thresholds"),
        "Population": ("scoreBuilder", "tile improvement priority"),
        "TradePartners": ("scoreTrader", "BASE_WEIGHT"),
    }

    lines.append("\n--- Feature Importance (late game is most predictive of winner) ---")
    late = importance.get("late", importance.get("overall", {}))
    ranked = sorted(enumerate(late), key=lambda x: -x[1])
    for fi, imp in ranked[:10]:
        name = FEATURE_COLUMNS[fi]
        scorer_info = feature_to_scorer.get(name, ("(no direct mapping)", ""))
        lines.append(f"  {name:20s}: importance={imp:.3f}  → {scorer_info[0]}")

    lines.append("\n--- Sensitivity: Marginal Win-Probability per Unit ---")
    lines.append("  (Positive = increasing this helps win)")
    ranked_sens = sorted(sensitivity.items(), key=lambda x: -abs(x[1]))
    for name, sens in ranked_sens[:10]:
        direction = "+" if sens > 0 else "-"
        lines.append(f"  {name:20s}: {direction}{abs(sens):.6f} win-prob per unit")

    lines.append("\n--- Concrete C++ Recommendations ---")

    # Find the most impactful features
    if len(ranked) >= 3:
        top_feature = FEATURE_COLUMNS[ranked[0][0]]
        lines.append(f"\n  1. HIGHEST PRIORITY: '{top_feature}' dominates late-game prediction.")
        if top_feature == "Military":
            lines.append("     → Increase scoreMilitary() BASE_WEIGHT from 0.8 to ~1.0")
            lines.append("     → AI advisor should evaluate threat more frequently")
        elif top_feature == "TechsResearched":
            lines.append("     → Science buildings are undervalued. Boost science building")
            lines.append("       score multiplier in scoreBuildingCandidate()")
        elif top_feature == "Cities":
            lines.append("     → Expansion is the strongest predictor. Increase")
            lines.append("       scoreSettler() BASE_WEIGHT from 0.95 to ~1.1")
        elif top_feature == "GDP":
            lines.append("     → Economy dominates. Increase economicFocus weight in")
            lines.append("       building scoring and trade route priority")

    lines.append("\n  2. PHASE-SPECIFIC TUNING:")
    early = importance.get("early", np.zeros(NUM_FEATURES))
    early_top = FEATURE_COLUMNS[np.argmax(early)]
    late_top = FEATURE_COLUMNS[np.argmax(late)]
    lines.append(f"     Early game (turns 1-100): '{early_top}' matters most")
    lines.append(f"     Late game (turns 200-300): '{late_top}' matters most")
    if early_top != late_top:
        lines.append(f"     → AI should shift strategy from {early_top}-focus to "
                     f"{late_top}-focus around turn 150")

    # Military sensitivity
    mil_sens = sensitivity.get("Military", 0)
    if mil_sens > 0:
        lines.append(f"\n  3. MILITARY: Each additional unit adds +{mil_sens:.6f} win probability")
        lines.append(f"     → At 10 units, that's +{mil_sens*10:.4f} total advantage")
    else:
        lines.append(f"\n  3. MILITARY: Negative sensitivity ({mil_sens:.6f}) — armies may be over-built")
        lines.append("     → Consider reducing scoreMilitary() weight or desiredMilitaryUnits")

    return "\n".join(lines)


# ============================================================================
# Main
# ============================================================================

def main():
    parser = argparse.ArgumentParser(description="Extract AI weights from trained model")
    parser.add_argument("--model", default="best_model.pt", help="Path to trained model")
    parser.add_argument("--data-dir", default="data/", help="Directory with simulation CSVs")
    parser.add_argument("--output", default="ai_recommendations.txt",
                        help="Output file for recommendations")
    args = parser.parse_args()

    print("=" * 60)
    print("Age of Civilization — Weight Extraction Tool")
    print("=" * 60)

    device = detect_device()

    # Load data
    print(f"\n[Loading] Data from {args.data_dir}")
    simulations = load_all_simulations(args.data_dir)
    dataset = CivDataset(simulations)

    # Load model
    print(f"\n[Loading] Model from {args.model}")
    if not os.path.exists(args.model):
        print(f"[Error] Model file not found: {args.model}")
        print("  Run train_supervised.py first to train a model.")
        sys.exit(1)

    model = CivTransformer(num_features=NUM_FEATURES)
    model.load_state_dict(torch.load(args.model, map_location=device, weights_only=True))
    print(f"  Loaded ({sum(p.numel() for p in model.parameters()):,} parameters)")

    # Feature importance
    print("\n[Analyzing] Feature importance by game phase...")
    importance = compute_feature_importance(model, dataset, device)

    for phase in ["early", "mid", "late"]:
        top3_idx = np.argsort(-importance[phase])[:3]
        top3 = [(FEATURE_COLUMNS[i], importance[phase][i]) for i in top3_idx]
        print(f"  {phase:5s}: {top3[0][0]}={top3[0][1]:.3f}, "
              f"{top3[1][0]}={top3[1][1]:.3f}, {top3[2][0]}={top3[2][1]:.3f}")

    # Attention patterns
    print("\n[Analyzing] Attention patterns (which turns matter most)...")
    attention = analyze_attention_patterns(model, dataset, device)
    print(f"  Critical turns for winners: {attention['critical_turns'][:5]}")

    # Sensitivity
    print("\n[Analyzing] Action sensitivity (marginal value per unit)...")
    sensitivity = compute_action_sensitivity(model, dataset, device)
    top_sens = sorted(sensitivity.items(), key=lambda x: -abs(x[1]))[:5]
    for name, sens in top_sens:
        print(f"  {name}: {sens:+.6f} win-prob/unit")

    # Generate recommendations
    recommendations = generate_recommendations(importance, sensitivity)
    print(f"\n{recommendations}")

    # Save to file
    with open(args.output, "w") as f:
        f.write(recommendations)
    print(f"\n[Saved] {args.output}")


if __name__ == "__main__":
    main()
