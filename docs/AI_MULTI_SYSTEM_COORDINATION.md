# Multi-System AI Coordination for Complex 4X Games

## Research Summary

This document extends `AI_ARCHITECTURE.md` with findings on how to coordinate multiple AI subsystems (economy, military, diplomacy, expansion, research, religion, trade) in a complex 4X game where all systems influence each other. Based on research from AI War 2's emergent AI design, FreeCiv's advisor system, GDC presentations on blackboard architecture, academic papers on strategy game AI, and influence map techniques.

---

## 1. The Coordination Problem

In a complex 4X game, the AI must simultaneously manage:
- **Economy**: Gold income, maintenance, trade, market
- **Military**: Unit production, positioning, combat, defense
- **Expansion**: City founding, settler movement, tile claiming
- **Research**: Tech tree priorities, eureka conditions
- **Diplomacy**: War/peace, alliances, trade agreements
- **Religion**: Faith accumulation, spreading, bonuses
- **Production**: Building queues, district placement, wonder races

These systems compete for the same resources (production time, gold, attention). A naive approach where each system maximizes independently leads to chaos - the economy advisor hoards gold while the military advisor demands units.

---

## 2. Solution: Resource Budget + Blackboard + Utility Scoring

The recommended architecture combines three patterns:

### Pattern 1: Shared Blackboard

A shared data structure where all AI subsystems read game state and write their assessments. Each subsystem posts its "wants" to the blackboard, and a coordinator resolves conflicts.

```
Blackboard contents:
  - threat_level: float (0.0 = safe, 1.0+ = emergency)     [written by MilitaryAdvisor]
  - expansion_opportunity: float (0.0 = nowhere, 1.0 = great) [written by SettlerAdvisor]  
  - tech_gap: float (0.0 = leading, 1.0 = far behind)         [written by ResearchAdvisor]
  - gold_pressure: float (0.0 = rich, 1.0 = bankrupt)         [written by EconomyAdvisor]
  - diplomatic_danger: float (0.0 = friends, 1.0 = enemies)   [written by DiplomacyAdvisor]
  - faith_opportunity: float (0.0 = no benefit, 1.0 = religion possible) [written by ReligionAdvisor]
```

Every advisor reads the blackboard to understand the full game state, not just their own domain.

### Pattern 2: Resource Budget Allocation

Instead of each system independently deciding what to build, a central **budget allocator** divides the available production capacity among competing needs:

```
Total production budget per turn: 100%

Budget allocation (adjusted by blackboard state):
  Base:     Military 30%, Expansion 25%, Infrastructure 20%, Research 15%, Other 10%
  
  If threat_level > 0.7:  Military += 20%, Expansion -= 15%, Infrastructure -= 5%
  If expansion_opportunity > 0.8:  Expansion += 15%, Military -= 10%, Other -= 5%
  If tech_gap > 0.6:  Research += 10%, Infrastructure += 5%, Military -= 10%, Other -= 5%
  If gold_pressure > 0.5:  Infrastructure += 10%, Military -= 5%, Expansion -= 5%
```

Each city then spends its production budget according to the global allocation, using utility scoring to pick the best item within its allocated category.

### Pattern 3: Utility Scoring with Cross-System Considerations

Production decisions use considerations from MULTIPLE systems:

```
Score(Build Warrior) = 
    military_need(from MilitaryAdvisor) * 
    threat_level(from Blackboard) * 
    can_afford(from EconomyAdvisor) * 
    not_expanding(from SettlerAdvisor) *  // don't build military if settler exists
    personality_weight
```

This naturally handles trade-offs: a warrior scores low when the economy advisor reports gold pressure, even though the military advisor wants units.

---

## 3. Influence Maps for Strategic Awareness

An influence map overlays the hex grid with "who controls what" information, enabling AI decisions that account for geography.

### Implementation

For each tile on the hex grid, maintain:
```
struct InfluenceData {
    float friendlyInfluence = 0.0f;    // Sum of friendly unit/city influence
    float enemyInfluence = 0.0f;       // Sum of enemy unit/city influence
    float tension = 0.0f;              // friendlyInfluence + enemyInfluence (contested areas)
    float vulnerability = 0.0f;        // enemyInfluence - friendlyInfluence (danger zones)
};
```

### Propagation

Each unit/city emits influence that decays with distance:
```
influence(distance) = strength * decay^distance

where:
  strength = combatStrength for military, population for cities
  decay = 0.8 (80% per tile)
  max_distance = 8 tiles
```

### Uses

| Query | How | Used For |
|-------|-----|----------|
| Where to attack? | Highest vulnerability tiles near enemy | Military offense |
| Where to defend? | Highest enemy influence near own cities | Military defense |
| Where to expand? | Low total influence, high yields | Settler placement |
| Where are borders? | Equal friendly/enemy influence | Diplomatic awareness |
| Where is safe? | High friendly, low enemy | Trade route paths |
| Where to patrol? | Border tiles with tension > 0 | Unit idle behavior |

---

## 4. The Advisor Architecture

Each domain has an advisor that runs every N turns and posts assessments to the blackboard:

### MilitaryAdvisor (every turn)
- Counts friendly/enemy military strength
- Computes threat_level per border region
- Identifies weak enemies for opportunistic attacks
- Posts: `threat_level`, `attack_targets[]`, `defend_priorities[]`

### EconomyAdvisor (every 5 turns)
- Evaluates income vs expenses
- Identifies gold sinks and sources
- Recommends tax rate, trade priorities
- Posts: `gold_pressure`, `recommended_tax_rate`, `trade_targets[]`

### ExpansionAdvisor (every 10 turns)
- Scans map for valuable unclaimed regions
- Scores potential city sites
- Tracks distance to nearest enemy
- Posts: `expansion_opportunity`, `best_city_sites[]`, `expansion_urgency`

### ResearchAdvisor (every 10 turns)
- Compares own tech count to average
- Identifies techs that unlock important units/buildings
- Checks for eureka opportunities
- Posts: `tech_gap`, `priority_techs[]`, `science_per_turn`

### DiplomacyAdvisor (every 20 turns)
- Evaluates relations with each player
- Identifies potential allies and targets
- Tracks war weariness, grievances
- Posts: `diplomatic_danger`, `potential_allies[]`, `war_targets[]`

### ReligionAdvisor (every 10 turns)
- Checks faith accumulation
- Identifies cities to spread to
- Evaluates religion founding opportunity
- Posts: `faith_opportunity`, `spread_targets[]`

---

## 5. Strategic Posture System

Every 20-30 turns, the AI evaluates all blackboard data and selects a **strategic posture** that biases all decisions:

```
enum class StrategicPosture {
    Expansion,       // Focus on settlers and infrastructure
    Development,     // Focus on science and culture
    MilitaryBuildup, // Focus on army production
    Aggression,      // Active war, produce siege and attack
    Defense,         // Under threat, fortify and garrison
    Economic,        // Focus on gold and trade
    Cultural,        // Focus on wonders and culture
    Religious,       // Focus on faith and spreading religion
};
```

### Selection Logic

```
if threat_level > 0.7 and ownMilitary < enemyMilitary:
    posture = Defense
elif ownMilitary > 2 * nearestEnemy and diplomatic_danger > 0.5:
    posture = Aggression
elif expansion_opportunity > 0.6 and ownCities < targetCities:
    posture = Expansion
elif tech_gap > 0.5:
    posture = Development
elif gold_pressure > 0.6:
    posture = Economic
elif faith_opportunity > 0.7:
    posture = Religious
else:
    posture = Development  // default
```

The posture multiplies utility scores:
- `Expansion`: settler_score * 2.0, builder_score * 1.5
- `MilitaryBuildup`: military_score * 2.0, settler_score * 0.5
- `Aggression`: military_score * 2.5, settler_score * 0.3
- `Defense`: military_score * 2.0, wall_score * 2.0
- `Development`: campus_score * 2.0, library_score * 1.5
- `Economic`: market_score * 2.0, trader_score * 1.5

---

## 6. Turn Execution Order

Each AI turn follows a strict pipeline:

```
1. SENSE   - Update influence maps, count units/cities/resources
2. ASSESS  - All advisors run and post to blackboard
3. DECIDE  - Strategic posture evaluation
4. BUDGET  - Allocate production budget across categories
5. PRODUCE - Each city uses utility scoring within its budget
6. MOVE    - Military units execute orders (attack/defend/patrol)
7. SETTLE  - Settlers move toward targets and found cities
8. TRADE   - Manage trade routes and agreements
9. DIPLO   - Evaluate diplomatic actions (war/peace/deals)
```

---

## 7. Cross-System Interaction Examples

### Example 1: Enemy army approaching

```
Turn N:   MilitaryAdvisor detects enemy stack, posts threat_level = 0.9
Turn N:   StrategicPosture switches to Defense
Turn N:   Budget shifts: Military 60%, Defense buildings 20%, Other 20%
Turn N:   All cities prioritize military units and walls
Turn N+1: Units move to defend threatened city
Turn N+5: Threat passes, MilitaryAdvisor lowers threat to 0.2
Turn N+5: Posture shifts to Expansion (was delayed by threat)
```

### Example 2: Economic crisis

```
Turn N:   EconomyAdvisor detects gold_pressure = 0.8
Turn N:   Budget shifts toward Infrastructure (markets, harbors)
Turn N:   MilitaryAdvisor is overruled - no new units despite wanting them
Turn N:   DiplomacyAdvisor seeks trade agreements for gold income
Turn N+5: New markets built, gold_pressure drops to 0.3
Turn N+5: Military spending resumes
```

### Example 3: Opportunistic war

```
Turn N:   MilitaryAdvisor: ownMilitary = 15, neighbor military = 4
Turn N:   DiplomacyAdvisor: neighbor has no allies, negative relations
Turn N:   Posture switches to Aggression
Turn N:   DiplomacyAdvisor declares war
Turn N:   All military units assigned to attack nearest enemy city
Turn N:   Economy advisor ensures gold reserves for extended war
Turn N+10: Enemy city captured, war weariness rising
Turn N+15: DiplomacyAdvisor negotiates peace (war weariness high)
```

---

## 8. FreeCiv's Want System (Reference Implementation)

FreeCiv uses a numerical "want" system (0-200) where each advisor posts wants:

```
Want(Building) = building_benefit * amortization_factor
Want(Unit)     = (battle_profit - maintenance) * amortization_factor

amortize(benefit, delay) = benefit * ((MORT-1)/MORT)^delay   // MORT=24, ~4.3% decay
```

This naturally penalizes distant future benefits, making the AI prefer immediate payoffs - similar to human time preference. A building that gives +2 gold/turn is worth more when treasury is low (immediate need) than when treasury is high (distant benefit).

---

## 9. AI War 2's Emergent Design (Reference)

AI War 2 manages 60,000 units across multiple planets using:

1. **Three-tier thinking**: Strategic (galaxy level), Sub-Commander (planet level), Individual Unit
2. **Dynamic agent subdivision**: Groups of units form ad-hoc commanders that merge/split as situations change
3. **Performance scheduling**: Only 1,000 target acquisitions per second, spread across frames
4. **Near-optimal randomization**: AI picks randomly from the top 10% of scored options, preventing exploitable predictability

Key insight: "Simulate intelligence, don't implement it." The AI doesn't actually plan ahead - it uses local information and good heuristics to produce behavior that looks intelligent.

---

## 10. Implementation Priority for Age of Civilization

### Phase 1 (Done): Utility AI Scoring
- UtilityCurve, UtilityConsideration infrastructure
- Production scoring replaces priority list

### Phase 2 (Next): Blackboard + Advisors
- Create AIBlackboard struct on Player
- Implement MilitaryAdvisor, EconomyAdvisor, ExpansionAdvisor
- Advisors post assessments every N turns

### Phase 3: Influence Maps
- Overlay influence data on HexGrid
- Propagate unit/city influence with decay
- Use for military positioning, settler placement, border detection

### Phase 4: Strategic Posture
- Posture evaluation using blackboard data
- Posture multipliers on utility scores
- Personality-driven posture preferences

### Phase 5: Budget Allocation
- Global production budget per player
- Dynamic reallocation based on blackboard state
- Per-city budget compliance via utility score weighting

---

## Sources

- [AI Blackboard Architecture for Game AI - Tono Game Consultants](https://tonogameconsultants.com/ai-blackboard/)
- [AI War: AI Design - Arcen Wiki](https://wiki.arcengames.com/index.php?title=AI_War:AI_Design)
- [Designing Emergent AI Part 1 - Arcen Games](https://arcengames.com/designing-emergent-ai-part-1-an-introduction/)
- [Designing AI Algorithms for Turn-Based Strategy Games - Game Developer](https://www.gamedeveloper.com/design/designing-ai-algorithms-for-turn-based-strategy-games)
- [Strategies for Strategy Game AI - AAAI 1999](https://cdn.aaai.org/Symposia/Spring/1999/SS-99-02/SS99-02-005.pdf)
- [Core Mechanics of Influence Mapping - GameDev.net](https://www.gamedev.net/tutorials/programming/artificial-intelligence/the-core-mechanics-of-influence-mapping-r2799/)
- [Modular Tactical Influence Maps - Game AI Pro 2](https://www.gameaipro.com/GameAIPro2/GameAIPro2_Chapter30_Modular_Tactical_Influence_Maps.pdf)
- [FreeCiv AI Documentation - GitHub](https://github.com/freeciv/freeciv/blob/master/doc/README.AI)
- [Game AI Pro - HTN Planners](https://www.gameaipro.com/GameAIPro/GameAIPro_Chapter12_Exploring_HTN_Planners_through_Example.pdf)
- [GOAP in F.E.A.R. - Game Developer](https://www.gamedeveloper.com/design/building-the-ai-of-f-e-a-r-with-goal-oriented-action-planning)
- [Influence Maps - Andrew Hunt](https://www.andrewshunt.com/influence-maps)
- [Game AI Planning: GOAP, Utility, and Behavior Trees](https://tonogameconsultants.com/game-ai-planning/)
- [GDC Vault - Improving AI Decision Modeling Through Utility Theory](https://www.gdcvault.com/play/1012410/Improving-AI-Decision-Modeling-Through)
- [Design Unpredictable AI in Games Part 1 - Medium](https://medium.com/@stannotes/design-unpredictable-ai-in-games-part-1-architecture-3752a618db6)
