#!/usr/bin/env python3
"""
Supervised learning pipeline for Age of Civilization AI.

Architecture: Temporal Transformer Encoder
===========================================

Why a Transformer, not an LSTM/GRU or plain MLP?

1. **Variable-length history**: A 4X game state at turn 200 depends on decisions
   made 150 turns ago (founding a city, choosing a tech). LSTMs struggle with
   dependencies beyond ~50 steps due to gradient decay, even with gating.
   Transformers use self-attention: every turn can attend directly to every
   other turn, so turn-200 can "look at" turn-50 in one hop.

2. **Parallel training**: LSTMs process sequences step-by-step (O(T) serial
   operations). Transformers process all timesteps in parallel, making training
   much faster on both CPU and GPU.

3. **Multi-system interactions**: A Civ game has ~20 interacting systems
   (economy, military, tech, diplomacy...). Self-attention learns which
   feature-time combinations matter (e.g., "treasury at turn 100 when
   military was low" → lost the game). An MLP would need hand-crafted
   cross-feature interactions.

4. **Proven on tabular time-series**: Recent work (TabTransformer, FT-Transformer)
   shows transformers match or beat gradient-boosted trees on tabular data when
   combined with proper feature embedding — which is our situation exactly.

Why NOT a larger model (GPT-style decoder, diffusion model)?
- Our feature space is only 18 floats per timestep, not language tokens.
- We have ~24K training rows, not millions. A 2-layer transformer with
  64-dim embeddings (~50K params) is right-sized to learn without overfitting.
- We want fast CPU inference for real-time AI decisions.

The model predicts:
  - **Winner prediction**: Which player wins (classification, 8 classes)
  - **Score regression**: Final EraVP score for each player

This lets the game AI evaluate "if I take this action, does my predicted
win probability go up?" by running the model on hypothetical future states.

Usage:
    # From ml/ directory with venv activated:
    python train_supervised.py --data-dir data/ --epochs 50
    python train_supervised.py --data-dir data/ --epochs 10 --quick  # fast test
"""

import argparse
import csv
import glob
import os
import sys
from pathlib import Path

import numpy as np
import torch
import torch.nn as nn
from torch.utils.data import Dataset, DataLoader


# ============================================================================
# Device auto-detection
# ============================================================================

def detect_device() -> torch.device:
    """Auto-detect best available device: CUDA > MPS > CPU."""
    if torch.cuda.is_available():
        device = torch.device("cuda")
        print(f"[Device] CUDA GPU detected: {torch.cuda.get_device_name(0)}")
        return device

    # Apple Silicon (MPS) - macOS only
    if hasattr(torch.backends, "mps") and torch.backends.mps.is_available():
        device = torch.device("mps")
        print("[Device] Apple MPS (Metal) detected")
        return device

    # AMD ROCm reports as CUDA in PyTorch ROCm builds
    # If we got here, no GPU available
    print("[Device] No GPU detected, using CPU")
    return torch.device("cpu")


# ============================================================================
# Data loading
# ============================================================================

# CSV columns (from HeadlessSimulation.cpp)
# New format includes game-level context + MetPlayersMask for fog-of-war
CSV_COLUMNS_NEW = [
    "Turn", "Player", "PlayerCount", "MapWidth", "MapHeight", "CivId",
    "MetPlayersMask",
    "GDP", "Treasury", "CoinTier", "MonetarySystem", "Inflation",
    "Population", "Cities", "Military", "TechsResearched", "CultureTotal",
    "TradePartners", "CompositeCSI", "EraVP", "AvgHappiness",
    "Corruption", "CrisisType", "IndustrialRev", "GovernmentType",
    "FoodPerTurn", "FamineCities", "ScienceDiffusion", "CultureDiffusion"
]
# Legacy format (pre-context columns) for backward compatibility
CSV_COLUMNS_LEGACY = [
    "Turn", "Player", "GDP", "Treasury", "CoinTier", "MonetarySystem",
    "Inflation", "Population", "Cities", "Military", "TechsResearched",
    "CultureTotal", "TradePartners", "CompositeCSI", "EraVP",
    "AvgHappiness", "Corruption", "CrisisType", "IndustrialRev",
    "GovernmentType"
]

# Features used for training — game-level context is included so the model
# learns that a 4-player game plays differently from a 12-player game, and
# that playing as Montezuma (CivId=8, aggressive) differs from Gandhi (CivId=9).
FEATURE_COLUMNS = [
    "PlayerCount", "MapWidth", "MapHeight", "CivId",
    "GDP", "Treasury", "CoinTier", "MonetarySystem", "Inflation",
    "Population", "Cities", "Military", "TechsResearched", "CultureTotal",
    "TradePartners", "CompositeCSI", "EraVP", "AvgHappiness",
    "Corruption", "CrisisType", "IndustrialRev", "GovernmentType",
    "FoodPerTurn", "FamineCities", "ScienceDiffusion", "CultureDiffusion"
]
NUM_FEATURES = len(FEATURE_COLUMNS)  # 26
NUM_PLAYERS = 8


def load_simulation_csv(filepath: str) -> dict:
    """Load a single simulation CSV into a structured dict.

    Handles both old format (20 columns, no game context) and new format
    (24 columns, with PlayerCount/MapWidth/MapHeight/CivId).

    Returns:
        {
            "sequences": np.ndarray (num_players, num_turns, num_features),
            "winner": int (player id of winner — highest final EraVP),
            "final_scores": np.ndarray (num_players,) — final EraVP per player
        }
    """
    with open(filepath, "r") as f:
        reader = csv.DictReader(f)
        rows = list(reader)

    if len(rows) == 0:
        return None

    # Detect format: new CSVs have "PlayerCount" column
    has_context = "PlayerCount" in rows[0]

    # Determine number of turns and players
    max_turn = max(int(r["Turn"]) for r in rows)
    players_seen = sorted(set(int(r["Player"]) for r in rows))
    num_players = len(players_seen)
    player_map = {p: i for i, p in enumerate(players_seen)}

    # Build sequences array
    sequences = np.zeros((num_players, max_turn, NUM_FEATURES), dtype=np.float32)

    for row in rows:
        turn_idx = int(row["Turn"]) - 1  # 0-indexed
        player_idx = player_map[int(row["Player"])]
        if turn_idx >= max_turn:
            continue
        for fi, col in enumerate(FEATURE_COLUMNS):
            if col in row:
                sequences[player_idx, turn_idx, fi] = float(row[col])
            else:
                # Legacy CSV missing context columns — fill with defaults
                if col == "PlayerCount":
                    sequences[player_idx, turn_idx, fi] = float(num_players)
                elif col == "MapWidth":
                    sequences[player_idx, turn_idx, fi] = 80.0
                elif col == "MapHeight":
                    sequences[player_idx, turn_idx, fi] = 52.0
                elif col == "CivId":
                    sequences[player_idx, turn_idx, fi] = float(player_idx)

    # Winner = player with highest final EraVP
    final_scores = sequences[:, -1, FEATURE_COLUMNS.index("EraVP")]
    winner = int(np.argmax(final_scores))

    return {
        "sequences": sequences,
        "winner": winner,
        "final_scores": final_scores,
    }


def load_all_simulations(data_dir: str) -> list:
    """Load all simulation CSVs from a directory."""
    csv_files = sorted(glob.glob(os.path.join(data_dir, "*.csv")))
    if not csv_files:
        print(f"[Error] No CSV files found in {data_dir}")
        sys.exit(1)

    simulations = []
    for filepath in csv_files:
        sim = load_simulation_csv(filepath)
        if sim is not None:
            simulations.append(sim)
            print(f"  Loaded {filepath}: {sim['sequences'].shape[1]} turns, "
                  f"winner=P{sim['winner']}, scores={sim['final_scores']}")

    print(f"[Data] Loaded {len(simulations)} simulations")
    return simulations


class CivDataset(Dataset):
    """Dataset of per-player game trajectories with win/loss labels.

    Each sample is one player's full game trajectory (T timesteps x F features).
    Labels: (did_win: 0/1, final_score: float)
    """

    def __init__(self, simulations: list, max_turns: int = 300):
        self.samples = []
        self.labels_win = []
        self.labels_score = []

        for sim in simulations:
            seq = sim["sequences"]  # (P, T, F)
            num_players = seq.shape[0]
            num_turns = min(seq.shape[1], max_turns)

            for p in range(num_players):
                # Pad or truncate to max_turns
                player_seq = np.zeros((max_turns, NUM_FEATURES), dtype=np.float32)
                player_seq[:num_turns] = seq[p, :num_turns]
                self.samples.append(player_seq)
                self.labels_win.append(1 if p == sim["winner"] else 0)
                self.labels_score.append(sim["final_scores"][p])

        self.samples = np.stack(self.samples)  # (N, T, F)
        self.labels_win = np.array(self.labels_win, dtype=np.int64)
        self.labels_score = np.array(self.labels_score, dtype=np.float32)

        print(f"[Dataset] {len(self.samples)} samples, "
              f"{np.sum(self.labels_win)} winners, "
              f"score range [{self.labels_score.min():.0f}, {self.labels_score.max():.0f}]")

        # Compute normalization stats
        flat = self.samples.reshape(-1, NUM_FEATURES)
        self.feature_mean = flat.mean(axis=0)
        self.feature_std = flat.std(axis=0) + 1e-8  # avoid div by 0

    def normalize(self, x: np.ndarray) -> np.ndarray:
        return (x - self.feature_mean) / self.feature_std

    def __len__(self):
        return len(self.samples)

    def __getitem__(self, idx):
        x = self.normalize(self.samples[idx])
        return (
            torch.from_numpy(x),
            torch.tensor(self.labels_win[idx], dtype=torch.long),
            torch.tensor(self.labels_score[idx], dtype=torch.float32),
        )


# ============================================================================
# Model: Temporal Transformer Encoder
# ============================================================================

class PositionalEncoding(nn.Module):
    """Sinusoidal positional encoding for turn number."""

    def __init__(self, d_model: int, max_len: int = 500):
        super().__init__()
        pe = torch.zeros(max_len, d_model)
        position = torch.arange(0, max_len, dtype=torch.float32).unsqueeze(1)
        div_term = torch.exp(
            torch.arange(0, d_model, 2, dtype=torch.float32)
            * (-np.log(10000.0) / d_model)
        )
        pe[:, 0::2] = torch.sin(position * div_term)
        pe[:, 1::2] = torch.cos(position * div_term)
        self.register_buffer("pe", pe.unsqueeze(0))  # (1, max_len, d_model)

    def forward(self, x: torch.Tensor) -> torch.Tensor:
        return x + self.pe[:, :x.size(1)]


class CivTransformer(nn.Module):
    """Temporal Transformer for predicting game outcomes from state trajectories.

    Architecture:
        Input (T, 18) → Linear projection (T, d_model) → Positional encoding
        → 2x Transformer Encoder layers (self-attention + FFN)
        → Mean pooling over time → 2 output heads (win classification + score regression)

    Parameters (~50K for d_model=64, 2 layers, 4 heads):
        - Input projection: 18 * 64 = 1,152
        - Each transformer layer: ~4 * 64^2 = 16,384 (attention + FFN)
        - Output heads: 64 * 2 + 64 * 1 = 192
        - Total: ~34K parameters — fast on CPU, resistant to overfitting on 80 samples
    """

    def __init__(self, num_features: int = NUM_FEATURES, d_model: int = 64,
                 nhead: int = 4, num_layers: int = 2, dim_feedforward: int = 128,
                 dropout: float = 0.1):
        super().__init__()

        self.input_projection = nn.Linear(num_features, d_model)
        self.pos_encoding = PositionalEncoding(d_model)

        encoder_layer = nn.TransformerEncoderLayer(
            d_model=d_model,
            nhead=nhead,
            dim_feedforward=dim_feedforward,
            dropout=dropout,
            batch_first=True,
        )
        self.transformer = nn.TransformerEncoder(encoder_layer, num_layers=num_layers)

        # Two heads: classification (win/lose) and regression (final score)
        self.win_head = nn.Sequential(
            nn.Linear(d_model, 32),
            nn.ReLU(),
            nn.Dropout(dropout),
            nn.Linear(32, 2),  # binary: win / lose
        )
        self.score_head = nn.Sequential(
            nn.Linear(d_model, 32),
            nn.ReLU(),
            nn.Dropout(dropout),
            nn.Linear(32, 1),  # regression: predicted final score
        )

    def forward(self, x: torch.Tensor) -> tuple:
        """
        Args:
            x: (batch, seq_len, num_features)

        Returns:
            win_logits: (batch, 2)
            score_pred: (batch, 1)
        """
        # Project input features to d_model dimensions
        h = self.input_projection(x)  # (B, T, d_model)
        h = self.pos_encoding(h)

        # Transformer encoder: self-attention across all turns
        h = self.transformer(h)  # (B, T, d_model)

        # Mean pooling over time dimension
        h_pooled = h.mean(dim=1)  # (B, d_model)

        win_logits = self.win_head(h_pooled)   # (B, 2)
        score_pred = self.score_head(h_pooled)  # (B, 1)

        return win_logits, score_pred


class CivTransformerBottleneck(nn.Module):
    """Transformer with attention bottleneck for human-like limited perception.

    A human player doesn't process all 300 turns of history equally — they
    focus on recent turns, active threats, and key milestones. This model
    simulates that by:

    1. Processing the full sequence through a lightweight encoder
    2. Using K learned "query" tokens to ATTEND to the sequence
       (like a human choosing what to look at)
    3. Only these K summary tokens (not the full sequence) feed into
       the decision heads

    K=8 means the model can only "look at" 8 aspects of the game at once,
    forcing it to prioritize. This:
    - Prevents overfitting to noise (can't memorize every turn)
    - Makes decisions more interpretable (what did it choose to look at?)
    - Better matches human-level play (humans can track ~5-9 things at once)
    """

    def __init__(self, num_features: int = NUM_FEATURES, d_model: int = 64,
                 nhead: int = 4, num_layers: int = 2, dim_feedforward: int = 128,
                 dropout: float = 0.1, num_queries: int = 8):
        super().__init__()

        self.input_projection = nn.Linear(num_features, d_model)
        self.pos_encoding = PositionalEncoding(d_model)

        encoder_layer = nn.TransformerEncoderLayer(
            d_model=d_model, nhead=nhead, dim_feedforward=dim_feedforward,
            dropout=dropout, batch_first=True)
        self.encoder = nn.TransformerEncoder(encoder_layer, num_layers=1)

        # Learned query tokens: K "attention slots" that select what to focus on
        self.query_tokens = nn.Parameter(torch.randn(1, num_queries, d_model) * 0.02)

        # Cross-attention: queries attend to the encoded sequence
        self.cross_attention = nn.MultiheadAttention(
            embed_dim=d_model, num_heads=nhead, dropout=dropout, batch_first=True)

        # Decision layers operate on the K bottleneck tokens, not the full sequence
        bottleneck_dim = num_queries * d_model

        self.win_head = nn.Sequential(
            nn.Linear(bottleneck_dim, 64),
            nn.ReLU(),
            nn.Dropout(dropout),
            nn.Linear(64, 2),
        )
        self.score_head = nn.Sequential(
            nn.Linear(bottleneck_dim, 64),
            nn.ReLU(),
            nn.Dropout(dropout),
            nn.Linear(64, 1),
        )

    def forward(self, x: torch.Tensor) -> tuple:
        batch_size = x.size(0)

        # Encode full sequence (lightweight: 1 layer)
        h = self.input_projection(x)
        h = self.pos_encoding(h)
        h = self.encoder(h)  # (B, T, d_model)

        # Cross-attention bottleneck: K queries attend to T encoded turns
        queries = self.query_tokens.expand(batch_size, -1, -1)  # (B, K, d_model)
        bottleneck, _ = self.cross_attention(queries, h, h)  # (B, K, d_model)

        # Flatten K tokens into decision vector
        h_flat = bottleneck.reshape(batch_size, -1)  # (B, K*d_model)

        win_logits = self.win_head(h_flat)
        score_pred = self.score_head(h_flat)

        return win_logits, score_pred


# ============================================================================
# Training loop
# ============================================================================

def train(model: nn.Module, train_loader: DataLoader, val_loader: DataLoader,
          device: torch.device, epochs: int = 50, lr: float = 1e-3):
    """Train the model with combined classification + regression loss."""

    model.to(device)
    optimizer = torch.optim.AdamW(model.parameters(), lr=lr, weight_decay=1e-4)
    scheduler = torch.optim.lr_scheduler.CosineAnnealingLR(optimizer, T_max=epochs)

    ce_loss_fn = nn.CrossEntropyLoss()
    mse_loss_fn = nn.MSELoss()

    best_val_acc = 0.0

    for epoch in range(epochs):
        # --- Train ---
        model.train()
        train_loss = 0.0
        train_correct = 0
        train_total = 0

        for x, y_win, y_score in train_loader:
            x = x.to(device)
            y_win = y_win.to(device)
            y_score = y_score.to(device)

            win_logits, score_pred = model(x)

            loss_ce = ce_loss_fn(win_logits, y_win)
            loss_mse = mse_loss_fn(score_pred.squeeze(-1), y_score)
            # Weight MSE loss lower since score magnitudes are large
            loss = loss_ce + 0.001 * loss_mse

            optimizer.zero_grad()
            loss.backward()
            torch.nn.utils.clip_grad_norm_(model.parameters(), 1.0)
            optimizer.step()

            train_loss += loss.item() * x.size(0)
            preds = win_logits.argmax(dim=1)
            train_correct += (preds == y_win).sum().item()
            train_total += x.size(0)

        scheduler.step()

        train_loss /= train_total
        train_acc = train_correct / train_total

        # --- Validate ---
        model.eval()
        val_loss = 0.0
        val_correct = 0
        val_total = 0

        with torch.no_grad():
            for x, y_win, y_score in val_loader:
                x = x.to(device)
                y_win = y_win.to(device)
                y_score = y_score.to(device)

                win_logits, score_pred = model(x)
                loss_ce = ce_loss_fn(win_logits, y_win)
                loss_mse = mse_loss_fn(score_pred.squeeze(-1), y_score)
                loss = loss_ce + 0.001 * loss_mse

                val_loss += loss.item() * x.size(0)
                preds = win_logits.argmax(dim=1)
                val_correct += (preds == y_win).sum().item()
                val_total += x.size(0)

        val_loss /= max(val_total, 1)
        val_acc = val_correct / max(val_total, 1)

        if val_acc > best_val_acc:
            best_val_acc = val_acc
            torch.save(model.state_dict(), "best_model.pt")

        if (epoch + 1) % 5 == 0 or epoch == 0:
            print(f"  Epoch {epoch+1:3d}/{epochs} | "
                  f"Train loss={train_loss:.4f} acc={train_acc:.3f} | "
                  f"Val loss={val_loss:.4f} acc={val_acc:.3f} | "
                  f"LR={scheduler.get_last_lr()[0]:.6f}")

    print(f"\n[Result] Best validation accuracy: {best_val_acc:.3f}")
    return best_val_acc


# ============================================================================
# Main
# ============================================================================

def main():
    parser = argparse.ArgumentParser(description="Train Civ AI from simulation data")
    parser.add_argument("--data-dir", default="data/", help="Directory with simulation CSVs")
    parser.add_argument("--epochs", type=int, default=50, help="Training epochs")
    parser.add_argument("--batch-size", type=int, default=16, help="Batch size")
    parser.add_argument("--lr", type=float, default=1e-3, help="Learning rate")
    parser.add_argument("--d-model", type=int, default=64, help="Transformer hidden dim")
    parser.add_argument("--num-layers", type=int, default=2, help="Transformer layers")
    parser.add_argument("--bottleneck", action="store_true",
                        help="Use attention bottleneck model (human-like limited view)")
    parser.add_argument("--num-queries", type=int, default=8,
                        help="Number of attention queries for bottleneck model")
    parser.add_argument("--quick", action="store_true", help="Quick test mode")
    args = parser.parse_args()

    print("=" * 60)
    print("Age of Civilization — Supervised AI Training Pipeline")
    print("=" * 60)

    # Device
    device = detect_device()

    # Load data
    print(f"\n[Loading] Simulation data from {args.data_dir}")
    simulations = load_all_simulations(args.data_dir)

    # Split: 80% train, 20% val
    split_idx = max(1, int(len(simulations) * 0.8))
    train_sims = simulations[:split_idx]
    val_sims = simulations[split_idx:]
    print(f"[Split] {len(train_sims)} train, {len(val_sims)} val simulations")

    train_dataset = CivDataset(train_sims)
    val_dataset = CivDataset(val_sims)
    # Use same normalization stats for val
    val_dataset.feature_mean = train_dataset.feature_mean
    val_dataset.feature_std = train_dataset.feature_std

    train_loader = DataLoader(train_dataset, batch_size=args.batch_size, shuffle=True)
    val_loader = DataLoader(val_dataset, batch_size=args.batch_size)

    # Model
    if args.bottleneck:
        model = CivTransformerBottleneck(
            num_features=NUM_FEATURES,
            d_model=args.d_model,
            num_queries=args.num_queries,
        )
        param_count = sum(p.numel() for p in model.parameters())
        print(f"\n[Model] CivTransformerBottleneck: {param_count:,} parameters")
        print(f"  d_model={args.d_model}, queries={args.num_queries} "
              f"(human-like: can only attend to {args.num_queries} game aspects)")
    else:
        model = CivTransformer(
            num_features=NUM_FEATURES,
            d_model=args.d_model,
            num_layers=args.num_layers,
        )
        param_count = sum(p.numel() for p in model.parameters())
        print(f"\n[Model] CivTransformer: {param_count:,} parameters")
        print(f"  d_model={args.d_model}, layers={args.num_layers}, heads=4")
    print(f"  Input: ({train_dataset.samples.shape[1]} turns, {NUM_FEATURES} features)")

    # Train
    epochs = 10 if args.quick else args.epochs
    print(f"\n[Training] {epochs} epochs, batch_size={args.batch_size}, lr={args.lr}")
    print("-" * 60)
    train(model, train_loader, val_loader, device, epochs=epochs, lr=args.lr)

    # Save normalization stats alongside model
    np.savez("normalization_stats.npz",
             mean=train_dataset.feature_mean,
             std=train_dataset.feature_std)
    print("\n[Saved] best_model.pt + normalization_stats.npz")


if __name__ == "__main__":
    main()
