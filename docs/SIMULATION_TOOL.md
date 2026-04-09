# Age of Civilization -- Headless Simulation Tool

## Overview

The headless simulation tool (`aoc_simulate`) runs AI-vs-AI games without any graphics or window. It logs per-turn per-player game state to a CSV file for analysis, balance testing, and bug detection.

## Building

```bash
cmake --build build -j$(nproc)
```

This produces two executables:
- `build/age_of_civ` -- the full game with graphics
- `build/aoc_simulate` -- the headless simulation tool

## Running

### With YAML config file (recommended)

```bash
cd build
./aoc_simulate ../data/simulation.yaml
```

The default config file is at `data/simulation.yaml`. Copy and modify it for different test scenarios.

### With command-line arguments

```bash
./aoc_simulate [max_turns] [player_count] [output_file]
```

Examples:
```bash
./aoc_simulate 300 4 results.csv       # 300 turns, 4 players
./aoc_simulate 500 8 big_game.csv      # 500 turns, 8 players
./aoc_simulate 100 2 duel.csv          # 100 turns, 2 players (duel)
```

## YAML Configuration

Edit `data/simulation.yaml`:

```yaml
# Game settings
max_turns: 300
seed: 42

# Map
map_width: 60
map_height: 40
map_type: Realistic    # Continents, Pangaea, Archipelago, Fractal, Realistic

# Players
player_count: 4
civilizations: [0, 3, 9, 5]   # Rome, Germany, India, England
difficulty: 1                   # 0=Easy, 1=Normal, 2=Hard

# Output
output_file: simulation_log.csv
show_progress: true
log_interval: 25

# Victory
score_victory_turn: 300

# Starting conditions
starting_gold: 100
starting_population: 3
```

### Civilization IDs

| ID | Civilization | Leader |
|----|-------------|--------|
| 0  | Rome        | Trajan |
| 1  | Egypt       | Cleopatra |
| 2  | China       | Qin Shi Huang |
| 3  | Germany     | Frederick |
| 4  | Greece      | Pericles |
| 5  | England     | Victoria |
| 6  | Japan       | Hojo Tokimune |
| 7  | Persia      | Cyrus |
| 8  | Aztec       | Montezuma |
| 9  | India       | Gandhi |
| 10 | Russia      | Peter |
| 11 | Brazil      | Pedro II |

## Output Format

The tool outputs a CSV file with one row per player per turn:

```
Turn,Player,GDP,Treasury,CoinTier,MonetarySystem,Inflation,Population,Cities,
Military,TechsResearched,CultureTotal,TradePartners,CompositeCSI,EraVP,
AvgHappiness,Corruption,CrisisType,IndustrialRev,GovernmentType
```

### Column Descriptions

| Column | Type | Description |
|--------|------|-------------|
| Turn | int | Current turn number (1-based) |
| Player | int | Player index (0-based) |
| GDP | int | Gross domestic product (total production value) |
| Treasury | int | Gold on hand |
| CoinTier | int | 0=None, 1=Copper, 2=Silver, 3=Gold |
| MonetarySystem | int | 0=Barter, 1=Commodity, 2=Gold Standard, 3=Fiat |
| Inflation | float | Per-turn inflation rate (-0.2 to 0.5) |
| Population | int | Total population across all cities |
| Cities | int | Number of cities owned |
| Military | int | Number of military units |
| TechsResearched | int | Total technologies completed |
| CultureTotal | float | Lifetime culture accumulated |
| TradePartners | int | Number of trade route partners |
| CompositeCSI | float | Civilization Score Index (relative to average) |
| EraVP | int | Cumulative Era Victory Points |
| AvgHappiness | float | Average net amenities across cities |
| Corruption | float | Corruption rate (0.0-0.3) |
| CrisisType | int | 0=None, 1=BankRun, 2=Hyperinflation, 3=Default |
| IndustrialRev | int | 0=None, 1=Steam, 2=Electric, 3=Digital, 4=Info, 5=Post |
| GovernmentType | int | 0=Chiefdom, 1=Autocracy, ..., 8=Merchant Republic |

## Progress Display

While running, the tool shows a progress bar on stderr:

```
  === Age of Civilization: Headless Simulation ===

  Config: data/simulation.yaml
  Turns:  300
  Players: 4
  Map:    Realistic (60x40)
  Seed:   42
  Output: simulation_log.csv

  [========================>                         ]  50% (150/300 turns)
  Turn 150: P0 pop=8 cities=3 techs=12 GDP=580
```

## Analyzing Results

### Quick analysis with Python

```python
import csv
import sys

data = {}
with open('simulation_log.csv') as f:
    for row in csv.DictReader(f):
        p = int(row['Player'])
        if p not in data: data[p] = []
        data[p].append(row)

for p in sorted(data.keys()):
    final = data[p][-1]
    print(f"Player {p}: pop={final['Population']} cities={final['Cities']} "
          f"techs={final['TechsResearched']} GDP={final['GDP']} "
          f"CSI={float(final['CompositeCSI']):.3f} VP={final['EraVP']}")
```

### What to look for (health checks)

| Metric | Healthy Range | Problem If |
|--------|--------------|------------|
| GDP at turn 100 | 100-500 | Always 0 = no resources |
| Population growth | 3 -> 8+ by turn 100 | Declining = food deficit |
| Cities | 2-4 by turn 100 | Stuck at 1 = AI not settling |
| Techs at turn 100 | 5-10 | < 3 = science too low |
| Military | 3+ by turn 50 | 0 = AI not building army |
| MonetarySystem | 1+ by turn 80 | 0 = no coins minted |
| GovernmentType | 1+ by turn 60 | 0 = civics not advancing |
| CrisisType | Mostly 0 | Always > 0 = economy broken |
| Inflation | -0.05 to 0.10 | > 0.25 = runaway inflation |

### Plotting with matplotlib

```python
import csv
import matplotlib.pyplot as plt

turns = {p: [] for p in range(4)}
gdps = {p: [] for p in range(4)}
pops = {p: [] for p in range(4)}

with open('simulation_log.csv') as f:
    for row in csv.DictReader(f):
        p = int(row['Player'])
        turns[p].append(int(row['Turn']))
        gdps[p].append(int(row['GDP']))
        pops[p].append(int(row['Population']))

fig, (ax1, ax2) = plt.subplots(2, 1, figsize=(12, 8))
for p in range(4):
    ax1.plot(turns[p], gdps[p], label=f'Player {p}')
    ax2.plot(turns[p], pops[p], label=f'Player {p}')

ax1.set_ylabel('GDP'); ax1.legend(); ax1.set_title('GDP over Time')
ax2.set_ylabel('Population'); ax2.legend(); ax2.set_xlabel('Turn')
plt.tight_layout()
plt.savefig('simulation_analysis.png')
```

## Test Scenarios

### Balance test: all same civ
```yaml
player_count: 4
civilizations: [0, 0, 0, 0]    # All Rome -- should be roughly equal
```

### Stress test: max players
```yaml
player_count: 12
map_width: 100
map_height: 66
max_turns: 500
```

### Economic test: long game
```yaml
player_count: 4
max_turns: 500
map_type: Realistic
difficulty: 2
```

### Duel test
```yaml
player_count: 2
civilizations: [3, 9]    # Germany vs India (aggressive vs peaceful)
max_turns: 300
```
