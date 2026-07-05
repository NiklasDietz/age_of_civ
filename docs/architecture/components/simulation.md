# Component: simulation

## Responsibility

The game's domain layer. Contains all rules that advance game state from one turn to the
next: AI decisions, economic production, diplomacy, city and unit mechanics, technology,
religion, culture, government, monetary policy, victory evaluation, and the `TurnProcessor`
that sequences them all.

## Key files

- [include/aoc/simulation/turn/TurnProcessor.hpp](../../../include/aoc/simulation/turn/TurnProcessor.hpp)
  — `processTurn(TurnContext&)`: the single function called by both `Application` and
  `HeadlessSimulation` to advance one turn. Documents the full execution order (AI → economy
  → per-player → global). `TurnContext` carries pointers to grid, fog, economy, diplomacy,
  market, RNG, barbarian controller, AI controllers, and optional `DecisionLog`.
- [include/aoc/simulation/resource/EconomySimulation.hpp](../../../include/aoc/simulation/resource/EconomySimulation.hpp)
  — `EconomySimulation::executeTurn()`: harvest raw resources → run production recipes in
  DAG topological order → report to market → update prices → execute trade routes → monetary
  policy.
- [include/aoc/simulation/diplomacy/DiplomacyState.hpp](../../../include/aoc/simulation/diplomacy/DiplomacyState.hpp)
  — `DiplomacyManager`: NxN relation-score matrix (int32, -100..+100) with decaying
  `RelationModifier` records. `DiplomaticStance` derived from score bands.
- [include/aoc/simulation/BalanceConfig.hpp](../../../include/aoc/simulation/BalanceConfig.hpp)
  — `aoc::sim::balance` namespace: all compile-time balance constants grouped by system
  (growth, combat, economy, monetary, production). Overridden at runtime by `BalanceParams`
  from the balance/ml subsystem.

### Sub-module index

| Sub-module | Key files | What it does |
|---|---|---|
| `ai/` | `AIController.hpp`, `BehaviorTree.cpp`, `UtilityAI.cpp`, `LeaderPersonality.cpp` | Orchestrates 15 focused controllers (research, settler, builder, military, diplomacy, economy, government, investment, trade routes, …) via utility scoring |
| `economy/` | `Market.cpp`, `AdvancedEconomics.cpp`, `CommodityExchange.cpp`, `SpeculationBubble.cpp`, `Sanctions.cpp`, `BlackMarket.cpp`, … | Global commodity market, supply/demand pricing, futures trading, speculation, sanctions, colonial/naval trade |
| `diplomacy/` | `DiplomacyState.cpp`, `EspionageSystem.cpp`, `WorldCongress.cpp`, `Grievance.cpp`, `WarWeariness.cpp`, `AllianceObligations.cpp`, … | Pairwise relations, espionage, World Congress votes, grievances, war weariness, alliances, border violations |
| `city/` | `CityGrowth.cpp`, `CityScience.cpp`, `CityLoyalty.cpp`, `CityBombardment.cpp`, `Governor.cpp`, `ProductionSystem.cpp`, `DistrictAdjacency.cpp`, `Happiness.cpp`, `Secession.cpp`, … | City lifecycle: growth, science, loyalty, bombardment, district bonuses, governor assignments, production queue processing |
| `unit/` | `Combat.cpp`, `Movement.cpp`, `Naval.cpp`, `Promotion.cpp`, `UnitUpgrade.cpp`, `ZoneOfControl.cpp`, `SupplyLines.cpp`, `CombatExtensions.cpp` | Unit combat resolution, movement costs, naval rules, promotion trees, upgrade paths, ZoC |
| `tech/` | `TechTree.cpp`, `CivicTree.cpp`, `EurekaBoost.cpp`, `TechGating.cpp`, `EraScore.cpp` | Research tree, civic tree, Eureka triggers, era transitions, tech-gated unit/building unlocks |
| `religion/` | `Religion.cpp`, `TheologicalCombat.cpp` | Faith generation, religion founding/spread, theological combat between missionaries |
| `culture/` | `Tourism.cpp` | Tourism accumulation and the culture victory counter |
| `government/` | `Government.cpp`, `GovernmentComponent.cpp` | Policy slots, anarchy transitions, government-type bonuses |
| `production/` | `Automation.cpp`, `PowerGrid.cpp`, `QualityTier.cpp`, `Waste.cpp` | Robot worker automation, electricity grid, good quality tiers, production waste/pollution |
| `resource/` | `EconomySimulation.cpp`, `ProductionChain.cpp`, `ResourceTypes.cpp` | DAG production chain (raw → processed → luxury), resource type definitions |
| `monetary/` | (CurrencyWar, MonetarySystem via GameState) | Fiat currency trust, debasement, hyperinflation, bonds, devaluation, bank runs |
| `barbarian/` | `BarbarianClans.cpp`, `BarbarianController.cpp` | Barbarian clan spawning and behavior |
| `citystate/` | `CityState.cpp` | City-state bonuses, suzerain/tribute mechanics |
| `climate/` | `Climate.cpp` | Global CO₂, temperature drift, natural disaster triggers |
| `event/` | `VisibilityEvents.cpp`, `GameNotifications.cpp` | Visibility event bus, notification dispatch to UI |
| `wonder/` | `Wonder.cpp` | Wonder construction tracking and global effects |
| `victory/` | `VictoryCondition.cpp`, `Prestige.cpp`, `SpaceRace.cpp` | Domination, science, culture, prestige, and space-race victory evaluation |
| `greatpeople/` | `GreatPeople.cpp`, `GreatPeopleExpanded.cpp` | Great person accumulation, recruitment, and abilities |
| `map/` | `Improvement.cpp`, `Infrastructure.cpp`, `TerrainModification.cpp`, `Chokepoint.cpp`, `GoodyHuts.cpp` | Builder improvements, road/railway networks, terrain conversion, chokepoint marking |
| `empire/` | — | Empire-level effects (confederation, colonial relationships) |
| `turn/` | `TurnProcessor.cpp`, `TurnManager.cpp` | Turn sequencing and the turn-counter state machine |
| `automation/` | `Automation.cpp` | City automation directives |

## Public surface

All simulation sub-modules are called from `TurnProcessor::processTurn()`. External
callers:
- `GameServer` — creates and drives `EconomySimulation`, `DiplomacyManager`, `TurnProcessor`
- `Application` — calls `processTurn()` at end-of-turn, reads `Market` prices for UI
- `GameClient` / UI — reads `DiplomaticStance`, research progress, city production state
- `save/Serializer` — serializes every simulation state struct to binary sections

## Internal structure

Each sub-module is a directory under `src/simulation/` mirrored in
`include/aoc/simulation/`. Sub-modules are self-contained; dependencies flow inward
(sub-modules read `game::GameState` and `map::HexGrid`, never UI or render). The
`turn/TurnProcessor` is the single integration point that orchestrates all other
sub-modules in the documented order.
