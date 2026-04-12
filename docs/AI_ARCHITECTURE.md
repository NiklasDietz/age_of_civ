# AI Architecture Design for Age of Civilization

## Research Summary

This document synthesizes findings from game AI research, Civilization AI modding communities, FreeCiv's open-source AI, GDC presentations on Utility AI, and academic papers on strategy game AI. It provides a blueprint for redesigning the AI system in Age of Civilization.

---

## 1. Why Our Current AI Fails

Our current AI uses a simple priority list in `executeCityActions()` and hardcoded behavior trees. The problems:

- **Rigid priority ordering**: Settlers > Builders > Military > Buildings. No consideration of context.
- **No scoring**: The AI doesn't evaluate *how much* it wants something, just *what's next in the list*.
- **No strategic layer**: No long-term planning (expand east? rush military? tech boom?).
- **No threat assessment**: The AI doesn't know if it's in danger.
- **Disconnected subsystems**: Military, economic, diplomatic, and expansion AIs don't communicate.

Result: 12 players over 2000 turns produce only 4 cities, 16 units, and zero wars.

---

## 2. Recommended Architecture: Layered Utility AI

Replace the current priority-list system with a **three-layer architecture** combining Utility AI for decision-making and advisors for domain expertise.

### Layer 1: Strategic Advisor (Long-Term Goals)

Runs every 20-30 turns. Evaluates the global situation and sets a **strategic posture**:

| Posture | Trigger | Effect |
|---------|---------|--------|
| **Expansion** | < targetCities, no threats | Boost settler/builder production want |
| **Development** | Adequate cities, tech behind | Boost science buildings, campus districts |
| **Military Buildup** | Neighbor is aggressive, war imminent | Boost military unit production |
| **Domination** | Strong military advantage | Declare war, produce siege units |
| **Economic** | Treasury low, maintenance high | Reduce military, build markets |
| **Cultural** | Cultural victory feasible | Boost theater/wonder production |

The strategic posture multiplies the utility scores of relevant actions by 1.5-2.0x, biasing decisions without overriding them.

### Layer 2: Tactical Advisor (Per-Turn Decisions)

Runs every turn. Uses **Utility AI scoring** to evaluate every possible action and pick the best one. This is the core decision engine.

For each city, score all production options:
```
Score(Settler)  = expansion_need * pop_readiness * safety_factor
Score(Warrior)  = military_need * threat_level * can_afford
Score(Builder)  = improvement_need * tiles_unimproved * pop_factor
Score(Campus)   = science_need * tech_lag * has_pop
Score(Monument) = culture_need * early_game_bonus
...
```

Each score is a product of **considerations** (0.0 to 1.0), each mapped through a **response curve**.

### Layer 3: Operational Executor (Unit Orders)

Runs every turn after production. Handles unit movement, combat targeting, settler placement, builder improvements using simple heuristics and pathfinding.

---

## 3. Utility AI Scoring System

### Core Formula

```
ActionScore = Consideration_1 * Consideration_2 * ... * Consideration_N * StrategyMultiplier
```

If ANY consideration scores 0, the entire action is disqualified (multiplication by zero).

### Response Curves

Each consideration maps a raw game value to a 0-1 score using a mathematical curve:

| Curve Type | Formula | Use Case |
|-----------|---------|----------|
| **Linear** | `y = m*x + b` | Proportional relationships (gold income vs want) |
| **Quadratic** | `y = x^2` | Escalating urgency (low health, low military) |
| **Square Root** | `y = x^0.5` | Diminishing returns (4th city less valuable than 2nd) |
| **Logistic/Sigmoid** | `y = 1/(1+e^(-k*(x-midpoint)))` | Threshold triggers (attack when strength > X) |
| **Exponential Decay** | `y = a^x` where 0 < a < 1 | Urgency that fades (recent threat vs old threat) |
| **Inverse** | `y = 1 - x` | Inverted needs (more cities = less settler want) |

### Normalization

ALL inputs must be normalized to 0-1 before applying curves:
```
normalized = clamp((raw_value - min_expected) / (max_expected - min_expected), 0.0, 1.0)
```

### Example: Should This City Build a Settler?

```
Considerations:
  1. expansion_need    = inverseCurve(ownedCities / targetCities)    // 1.0 at 0 cities, 0.0 at target
  2. population_ready  = stepCurve(cityPop >= 2 ? 1.0 : 0.3)        // Strong preference for pop 2+
  3. safety_factor     = logisticCurve(militaryStrength / threatLevel) // Don't expand under threat
  4. treasury_ok       = linearCurve(treasury / 200.0)                // Need some gold buffer
  5. no_settler_exists = stepCurve(existingSettlers == 0 ? 1.0 : 0.2) // Don't build 2 at once
  6. strategy_mult     = (posture == Expansion) ? 1.8 : 1.0          // Strategic bias

Score = c1 * c2 * c3 * c4 * c5 * strategy_mult
```

---

## 4. Production Decision Architecture

Every turn, for each city with an empty production queue:

1. **Generate candidate list**: All units, buildings, districts, wonders the player can build
2. **Score each candidate** using utility considerations
3. **Pick the highest scoring** candidate (with optional randomization within 10% of top)

### Key Production Considerations

| Consideration | Input | Curve | Purpose |
|--------------|-------|-------|---------|
| military_need | militaryUnits / desiredMilitary | inverse quadratic | More units when below target |
| threat_level | enemyMilitaryNearby / ownMilitary | logistic | Urgent when outgunned |
| expansion_need | ownCities / targetCities | inverse linear | Settler want decreases as cities grow |
| science_lag | ownTechs / avgTechs | inverse linear | Build campus when behind in tech |
| gold_pressure | maintenance / income | quadratic | Build market when costs outpace income |
| improvement_need | unimprovedTiles / workedTiles | linear | Builder want based on unimproved tiles |
| happiness_pressure | amenityDeficit | quadratic | Entertainment when unhappy |
| faith_need | faith < religionCost | step | Holy site when close to founding |
| defense_need | cityCount > militaryCount | logistic | Minimum garrison per city |

### Personality Modifiers

Each leader personality multiplies specific considerations:
- **Aggressive**: military_need * 1.5, expansion_need * 1.2
- **Scientific**: science_lag * 1.8, culture_need * 1.3
- **Economic**: gold_pressure * 0.5 (less worried), trade_want * 1.5
- **Expansionist**: expansion_need * 2.0, settler production always viable

---

## 5. City Placement (Settler AI)

### Tile Scoring Algorithm (from Civ 5/6 analysis)

Each candidate tile receives a score from:

```
score = sum_of_neighbor_yields * 2.0
      + freshWaterBonus          (river: +8, lake: +4)
      + coastalBonus             (1-3 water tiles: +5, harbor potential)
      + resourceBonus            (luxury: +6, strategic: +4, bonus: +2 per resource in range)
      + distanceFromOwnCity      (optimal 4-6 tiles: +5, too close <3: -30, too far >10: -3/tile)
      + distanceFromEnemy        (too close <4: -15, buffer zone)
      + hillDefenseBonus         (+3 if on hill)
      - tooManyDesertPenalty     (>3 desert tiles in range: -10)
```

### Settler Behavior Rules

1. **Produce first settler by turn 10** at pop 1 (don't wait for pop 2)
2. **Pre-compute top 3 city locations** when settler is queued, not when it's built
3. **Move directly to target** using A* pathfinding
4. **Found immediately on arrival** - don't re-evaluate every turn
5. **Fallback: found after 5 turns of movement** if stuck or target unreachable
6. **Never wander** - if no good location within radius 15, found at current position

---

## 6. Military AI

### Threat Assessment (Every Turn)

```
threat_level = sum(enemy_units_within_10_tiles * unit_strength) / own_military_strength
```

| Threat Level | Response |
|-------------|----------|
| < 0.3 | Safe. Focus on economy/expansion |
| 0.3 - 0.7 | Moderate. Build 1 military unit per city |
| 0.7 - 1.5 | Dangerous. Prioritize military, fortify borders |
| > 1.5 | Critical. Emergency military production, defensive posture |

### Unit Composition Targets

| Era | Target Military | Composition |
|-----|----------------|-------------|
| Ancient (0-50) | 2 per city | 2 melee |
| Classical (50-100) | 3 per city | 2 melee + 1 ranged |
| Medieval (100-200) | 4 per city | 2 melee + 1 ranged + 1 cavalry |
| Industrial+ (200+) | 5 per city | Mixed, include siege |

### Combat Decision

Use utility scoring:
```
attack_score = (expected_damage_to_enemy / enemy_hp)
             * (own_hp_after / own_max_hp)
             * terrain_modifier
             * flanking_bonus
             * strategic_value_of_target
```

---

## 7. Economic AI

### Treasury Management

| Treasury State | Action |
|---------------|--------|
| > 3x income | Lower tax rate, increase spending |
| 1-3x income | Normal operation |
| < income | Raise tax, cut non-essential buildings |
| Negative | Emergency: disband weakest non-essential unit |

### Trade Route Priority

Score = `resource_complementarity * partner_trust * route_safety * gold_per_turn_estimate`

---

## 8. FreeCiv's Want System (Reference)

FreeCiv uses a "want" value from 0-200 where:
- 0 = no desire
- 1-100 = normal priority
- 100-200 = urgent/critical

**Key formula**: `Want = Operation_Profit * Amortization_Factor`

Where `amortize(benefit, delay) = benefit * ((MORT-1)/MORT)^delay` with MORT=24 (~4.3% discount rate). This penalizes actions whose payoff is far in the future.

---

## 9. Implementation Plan

### Phase 1: Utility Scoring Infrastructure
- Create `UtilityCurve` struct with curve type, parameters (slope, exponent, offset)
- Create `UtilityConsideration` struct binding an input source to a curve
- Create `UtilityAction` struct containing a list of considerations and a multiplier
- Create `UtilityDecisionMaker` that scores all actions and returns the best

### Phase 2: Production AI
- Replace `executeCityActions()` priority list with utility scoring
- Define 15-20 considerations for production decisions
- Tune curves through simulation testing

### Phase 3: Strategic Layer
- Add `StrategicPosture` enum and evaluation function
- Run every 20 turns, evaluate global state, set posture
- Posture multiplies relevant production scores

### Phase 4: Military AI
- Add threat assessment per-turn
- Add unit composition targets by era
- Add attack/defend utility scoring for unit orders

### Phase 5: Settler AI
- Rewrite city placement scoring with the full formula above
- Pre-compute locations, direct movement, forced founding fallback

---

## Sources

- [Utility AI Introduction - The Shaggy Dev](https://shaggydev.com/2023/04/19/utility-ai/)
- [Are Behavior Trees a Thing of the Past? - Game Developer](https://www.gamedeveloper.com/programming/are-behavior-trees-a-thing-of-the-past-)
- [Response Curves for Game AI - Alastair Aitchison](https://alastaira.wordpress.com/2013/01/25/at-a-glance-functions-for-modelling-utility-based-game-ai/)
- [FreeCiv AI Documentation - GitHub](https://github.com/freeciv/freeciv/blob/master/doc/README.AI)
- [Game AI Pro - Utility Theory Chapter](http://www.gameaipro.com/GameAIPro/GameAIPro_Chapter09_An_Introduction_to_Utility_Theory.pdf)
- [Game AI Pro 3 - Choosing Effective Considerations](http://www.gameaipro.com/GameAIPro3/GameAIPro3_Chapter13_Choosing_Effective_Utility-Based_Considerations.pdf)
- [Civilization AI Mods - CivFanatics](https://forums.civfanatics.com/threads/real-strategy-ai.640452/)
- [Civ City Placement Algorithm - CivFanatics](https://forums.civfanatics.com/threads/question-how-to-modify-ai-city-placement-algorithm.177465/)
- [AI in Strategy Games - PPQTY](https://ppqty.com/ai-in-strategy-games/)
- [GDC AI Summit - Utility Theory Presentations](https://slideplayer.com/slide/7103179/)
- [Sigmoid Curves for Game Design - Medium](https://medium.com/@pedro.camara/sigmoid-curves-are-game-designers-friends-8b1f5b53d2fc)
- [Design Patterns for Utility AI Configuration](https://course.ccs.neu.edu/cs5150f13/readings/dill_designpatterns.pdf)
