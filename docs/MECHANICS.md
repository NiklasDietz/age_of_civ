# Age of Civilization — Mechanics Reference

Comprehensive list of simulated mechanics. Organized by subsystem. Each entry
names the authoritative source file(s). Numbers are current balance values —
subject to change as GA-evolved personalities drift balance.

## Map & Terrain

- **Hex grid** (`map/HexGrid.hpp`): axial coords, per-tile terrain, resource,
  owner, yields (food/prod/gold/science/culture). Spiral neighborhood queries
  up to radius N.
- **Map generators** (`map/MapGenerator.cpp`): Continents, Pangaea,
  Archipelago, Fractal, Realistic. Noise-driven elevation + moisture +
  latitude climate bands.
- **Resources** (`simulation/resource/ResourceTypes.hpp`): bonus (food/prod
  bumps), strategic (iron, coal, oil, uranium, horses, niter, aluminum),
  luxury (wine/spices/silk/ivory/gems/dyes/furs/incense/sugar/pearls/tea/
  coffee/tobacco). Mountain-mined metals also carried.
- **Goody huts** (`simulation/map/GoodyHuts.hpp`): scattered reward tiles —
  gold / science / map reveal / unit.

## Cities

- **Founding + workable area**: 3-hex ring around center, auto-worker
  assignment on growth.
- **Districts** (`simulation/city/District.hpp`): CityCenter, Industrial,
  Commercial, Campus, HolySite, Harbor, Encampment, **Theatre**. Placed on
  adjacent tiles; gate building construction.
- **District adjacency** (`simulation/city/DistrictAdjacency.hpp`): terrain
  and neighbor-district adjacency bonuses.
- **Buildings**: 42 building defs with fields for productionCost,
  maintenance, yields (prod/sci/gold/faith/**culture**),
  **greatWorksSlots**, resource costs, ongoing fuel. Theatre buildings:
  Amphitheater / Art Museum / Archaeological Museum.
- **Pollution emission** per building feeds global climate CO2.
- **Growth** (`simulation/city/CityGrowth.cpp`): food surplus vs. per-pop
  cost → population ticks. Granary preserves 50% food on growth. Starvation
  drops pop at -30 surplus.
- **Housing cap**: base 4, Granary +2, Hospital +4. Surplus >= housing
  throttles growth (-25% per pop over); +4 over housing stops it.
- **Happiness / amenities** (`simulation/city/Happiness.cpp`): base +1
  capital + luxury allocation + unique luxury count (capped by city count).
  **Luxury monopoly**: 3+ stock of one luxury counts as an extra unique.
  Penalties: war weariness, inflation, tax rate, empire size, military
  units away.
- **Loyalty** (`simulation/city/CityLoyalty.cpp`): pressure from
  neighboring cultures + distance from capital. Prolonged low loyalty
  leads to collapse.
- **Production queue** (`simulation/city/ProductionQueue.hpp`): slotted
  queue + repeating templates.
- **Governor** (`simulation/city/Governor.hpp`): auto-queue by focus mode
  (food / prod / gold / science / balanced).
- **Walls & bombardment** (`simulation/city/CityBombardment.cpp`): tiered
  walls (Ancient / Medieval / Renaissance), wall HP + auto-repair, ranged
  attack vs weakest unit in range. **Encampment district** also bombards
  independently — strength 22 (+10 with Barracks), range 2 (+1 with
  Barracks).
- **Border expansion** (`simulation/city/BorderExpansion.hpp`): culture
  accumulates → next tile claimed by score (yields + resources).
- **City connections** (`simulation/city/CityConnection.hpp`): roads +
  sea link cities for trade and loyalty.

## Economy

- **Goods & stockpiles**: per-city stockpile of goods (IDs for raw
  resources, intermediate goods, finished goods). Production recipes
  consume/produce goods.
- **Monetary systems** (`simulation/monetary/`): Barter → Commodity →
  Coinage → Fiat. Evolution gated by Mint building, trade volume, tax
  infrastructure. Inflation rate, tax rate, science/luxury/gold
  allocation sliders per player.
- **Maintenance**: building upkeep drains treasury per turn.
- **Trade routes** (`simulation/economy/TradeRouteSystem.cpp`): land /
  sea / air traders carry goods between cities. Science/culture spread
  accumulated on traders. Route utility score drives AI route creation.
- **Naval trade** (`simulation/economy/NavalTrade.cpp`): coastal routes
  unlock with Harbor.
- **Speculation / stock market** (`simulation/economy/StockMarket.hpp`,
  `Speculation.cpp`): investment, dividends, bubble risk, crashes.
- **Advanced economics** (`AdvancedEconomics.cpp`, `EconomicDepth.cpp`,
  `HumanCapital.cpp`, `BlackMarket.cpp`, `IndustrialRevolution.cpp`,
  `TechUnemployment.cpp`, `ColonialEconomics.cpp`,
  `MonopolyPricing.cpp`, `EnergyDependency.cpp`, `EconomicEspionage.cpp`,
  `InternalTrade.cpp`, `SupplyChain.cpp`): layered multipliers on
  treasury + inflation + dependencies.
- **Waste / pollution** (`simulation/production/Waste.cpp`): byproduct
  of industry feeds climate.
- **Power grid** (`simulation/production/PowerGrid.cpp`): plants feed
  cities; without fuel the plant is unpowered.
- **Goods automation** (`simulation/production/Automation.cpp`,
  `automation/Automation.cpp`): recipe chains fire automatically per
  city.

## Science & Culture

- **Tech tree** (`simulation/tech/TechTree.hpp`): per-player research
  component, science points accumulate toward currentResearch. Cost
  scales over eras. Completed bitfield drives unlocks.
- **Civic tree** (`simulation/tech/CivicTree.hpp`): parallel culture-
  gated research tree. Unlocks policy cards / civics.
- **Era progression** (`simulation/tech/EraProgression.hpp`): effective
  era derived from completed tech count.
- **Expanded content** (`simulation/tech/ExpandedContent.hpp`): late-era
  tech definitions.
- **Culture output** (`simulation/city/CityScience.cpp`): tile yield +
  pop + capital bonus + **Theatre buildings' cultureBonus** + civ
  multiplier + economic stability.
- **Tourism** (`simulation/culture/Tourism.hpp`): tourism-per-turn from
  Great Works + wonders + national parks (currently stubbed — header
  only).

## Units

- **Unit types** (`simulation/unit/UnitTypes.hpp`): ~100+ ids covering
  eras Ancient→Modern. Classes: Melee, Ranged, Cavalry, Armor,
  Artillery, Air, Naval, Civilian, Builder, Spy, Diplomat.
- **Movement** (`simulation/unit/Movement.cpp`): terrain-costed move, ZoC,
  embarkation, transport.
- **Combat** (`simulation/unit/Combat.cpp`,
  `simulation/unit/CombatExtensions.cpp`): strength + era-diff modifier
  + terrain + fortification + healing.
- **Supply lines** (`simulation/unit/SupplyLines.hpp`): units N hexes
  from friendly city suffer attrition (−10 HP/turn, −25% combat).
- **Builder charges**: builders have N charges; each use spends one.
- **Great People** (`simulation/greatpeople/`,
  `GreatPeopleExpanded.hpp`): points accumulate per district activity,
  recruit spawns GP unit, activation grants one-shot boon.

## Diplomacy

- **Relations** (`simulation/diplomacy/`): pairwise baseScore, atWar flag,
  tradeVolume, open borders.
- **Grievances** (`simulation/diplomacy/Grievance.cpp`): border violation,
  city capture, lost city, denouncements. Decays over time.
- **Alliances & treaties** (`DiplomacyExtensions.hpp`,
  `AllianceTypes.hpp`): defensive pact, research pact, alliance.
- **Espionage** (`simulation/diplomacy/Espionage.hpp`): 16 missions
  (StealTech, Sabotage, SiphonFunds, Counterintel, Assassinate,
  MarketManipulation, …), spy levels, cover/action/escape rolls.
- **Border violation** (`BorderViolation.cpp`): passing army without
  open borders accumulates grievance.

## City-States

- **Spawning** (`simulation/citystate/CityState.cpp`): min-spacing
  placement, typed (Militaristic / Scientific / Cultural / Trade /
  Religious / Industrial).
- **Envoys**: accumulate per civ; tiers at 1/3/6. **Type-specific
  per-turn bonuses** (scaled by tier):
  - Militaristic → +prod to every city's current build (×2)
  - Scientific → +science to current research (×4)
  - Cultural → +civic progress (×4)
  - Trade → +gold (×3)
  - Religious → +faith (×3)
  - Industrial → +prod (×3)
- **Suzerain**: highest envoy civ gains exclusive bonus (gated by
  threshold).

## Government & Ideology

- **Government** (`simulation/government/Government.hpp`): 8 types incl.
  Democracy, Communism, Fascism. 22 policy cards. Mid-game switching
  with adoption cost.
- **Government modifiers**: per-type multipliers on science/gold/
  happiness/military; empire size threshold; military-unhappy factor.

## Religion

- **Religion system** (`simulation/religion/Religion.hpp`): faith
  accumulation → found religion → spread to neighboring cities.
- **Theological combat** (`religion/TheologicalCombat.cpp`): competing
  religions contest cities; apostles / missionaries.
- **Era-gated science-vs-devotion curve**: monastic bonus early,
  secularization penalty late.

## Victory

- **Conditions** (`simulation/victory/VictoryCondition.cpp`): Score,
  Domination, Science, Culture, Religion, Integration, Last Standing.
  Game end check each turn.
- **Configurable** via `--victory-types` (comma list or `all`).

## Climate

- **Global CO2 + temperature + sea level** (`simulation/climate/`):
  pollution emissions accumulate → global indicators drift. Sea rise
  threatens coastal tiles. Extreme weather events (future).

## Events

- **World events** (`simulation/event/WorldEvents.hpp`): 12 event types
  (plague, volcano, reformation, drought …), per-event choice struct
  scored by AI.

## AI

- **Leader personality** (`simulation/ai/LeaderPersonality.hpp`):
  32-float LeaderBehavior vector — economicFocus, prodBuildings,
  scienceFocus, cultureFocus, militaryAggression, expansionism,
  religiousZeal, trustworthiness, greatPersonFocus, environmentalism,
  speculationAppetite, riskTolerance, … Full list in `LeaderBehavior`.
- **Utility scoring** (`simulation/ai/UtilityScoring.cpp`): per-building,
  per-tech, per-unit score functions parameterized by personality.
- **Sub-controllers**: `AIController.cpp` (master),
  `AIBuilderController.cpp`, `AIEconomicStrategy.cpp`,
  `AIInvestmentController.cpp`, `AIMilitaryController.cpp`,
  `AISettlerController.cpp`, `AIEventChoice.cpp`.
- **GA evolution** (`ml/cpp/GeneticAlgorithm.hpp`): population of
  LeaderBehavior vectors, fitness from win rate + score delta, mutation
  + crossover, checkpoint/resume.

## Turn Processing

- **TurnProcessor** (`simulation/turn/TurnProcessor.cpp`): ordered phase
  list — move → combat → production → growth → yields → diplomacy →
  religion → climate → events → victory. Per-player TurnSummary written
  to DecisionLog each turn.
- **Game length** (`simulation/turn/GameLength.hpp`): growth/research
  multiplier scales cost curves for Short/Standard/Long/Marathon games.

## Logging & Tooling

- **DecisionLog** (`core/DecisionLog.hpp`): binary AOCL trace — production
  choices, research choices, per-turn player summaries. Thread-local
  `currentDecisionLog()` keeps the hot path free of logger parameters.
- **TraceDump** (`tools/TraceDump.cpp`): AOCL → 3 CSVs
  (production/research/summary) for Pandas analysis.
- **Headless simulation** (`tools/HeadlessSimulation.cpp`):
  `aoc_simulate --turns N --players P --map-type T --victory-types V
  --output csv --trace-file aocl`.
- **Sweep matrix** (`scripts/sweep_matrix.sh`): env-configurable cross
  of maps × lengths × players with parallel workers; dumps all traces.

## Barbarians

- **Barbarian spawns** (`simulation/barbarian/`): neutral-hostile units
  from unowned terrain; raid cities; killed for gold.

## Wonders

- **Wonders** (`simulation/wonder/Wonder.hpp`): global + national
  wonders, prerequisites (tech/resource), one-per-game global wonders,
  bonus yields/GreatWorks/tourism on completion.

## Genetic Algorithm (offline)

- `ml/cpp/GeneticAlgorithm.hpp`: population of LeaderBehaviors, parallel
  headless games drive fitness, checkpointed every N generations.
- `ml/extract_weights.py`: export evolved behavior table.
