# Phase 1 IR-Blocker Diagnosis

- Total sims: **40**
- Total (sim, civ, next_ir) blocker rows: **1099**
- AOC_DIAG_IR lines parsed: **6596** (diag-classified rows: **395**, proxy-classified: **704**)

## Civ count by final IR level

| Final IR | Civs |
|---:|---:|
| 0 | 176 |
| 1 | 28 |
| 2 | 35 |
| 3 | 1 |
| 4 | 0 |
| 5 | 0 |

## Blockers preventing IR#1 (176 civ-cases)

- tech: 3 (1.7%)
- cityCount: 0 (0.0%)
- good: 173 (98.3%)

  Top tech blockers:
  - 18 (SurfacePlate): 3

  Top good blockers:
  - 79 (Charcoal): 173

## Blockers preventing IR#2 (204 civ-cases)

- tech: 95 (46.6%)
- cityCount: 0 (0.0%)
- good: 109 (53.4%)

  Top tech blockers:
  - 47 (Steel): 76
  - 22 (PrecisionInstruments): 19

  Top good blockers:
  - 64 (Steel): 60
  - 3 (Oil): 49

## Blockers preventing IR#3 (239 civ-cases)

- tech: 4 (1.7%)
- cityCount: 1 (0.4%)
- good: 234 (97.9%)

  Top tech blockers:
  - 23 (Semiconductors): 4

  Top good blockers:
  - 75 (Semiconductors): 234

## Blockers preventing IR#4 (240 civ-cases)

- tech: 21 (8.8%)
- cityCount: 1 (0.4%)
- good: 218 (90.8%)

  Top tech blockers:
  - 27 (Internet): 21

  Top good blockers:
  - 108 (Software): 218

## Blockers preventing IR#5 (240 civ-cases)

- tech: 240 (100.0%)
- cityCount: 0 (0.0%)
- good: 0 (0.0%)

  Top tech blockers:
  - 64 (Fusion): 240

## Civ % reaching each IR by map type

| Map | Civs | %IR1 | %IR2 | %IR3 | %IR4 | %IR5 |
|---|---:|---:|---:|---:|---:|---:|
| archipelago | 60 | 15% | 8% | 0% | 0% | 0% |
| continents | 60 | 20% | 15% | 0% | 0% | 0% |
| islands | 60 | 43% | 27% | 2% | 0% | 0% |
| pangaea | 60 | 28% | 10% | 0% | 0% | 0% |

## Civ % reaching each IR by player count

| Players | Civs | %IR1 | %IR2 | %IR3 | %IR4 | %IR5 |
|---:|---:|---:|---:|---:|---:|---:|
| 4 | 80 | 20% | 15% | 1% | 0% | 0% |
| 8 | 160 | 30% | 15% | 0% | 0% | 0% |

## Hypothesis ranking from this corpus

Each hypothesis is scored by the fraction of civ-cases it explains across IR levels 2-5 (IR#1 is near-saturation and rarely the bottleneck).

- **H2-tech-cost-pacing**: 360 (39.0%)
- **H2-Semiconductor-good**: 234 (25.4%)
- **H4-Software-good**: 218 (23.6%)
- **H3-Steel-chain**: 60 (6.5%)
- **H1-OIL-gate**: 49 (5.3%)
- **H7-undersized-civ**: 2 (0.2%)

## Confidence note

- **Diag-classified rows** are direct evidence from the C++ instrumentation and are authoritative.
- **Proxy-classified rows** for `reason=good` infer the gate by elimination (tech ok + city count ok => goods must be the gate). Without diag, the proxy picks the FIRST listed required good (OIL for IR#2, Steel left ambiguous).
- If proxy_rows >> diag_rows, re-run with `cmake -DAOC_DIAG_IR=ON` then rebuild before invoking the analyzer.
