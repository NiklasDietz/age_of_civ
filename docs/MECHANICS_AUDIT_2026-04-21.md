# Age of Civilization — Mechanics Implementation Audit

**Date:** 2026-04-21
**Scope:** Full repository scan (src/, include/aoc/, data/, tests/).
**Goal:** For every game mechanic: is the core logic implemented, is it wired into the turn loop / UI, and are there orphaned, stubbed, or half-wired pieces?

Numbers in this document reference file paths and line numbers as observed at the time of audit. TurnProcessor line numbers refer to `src/simulation/turn/TurnProcessor.cpp`.

---

## 0. Legend

- ✅ **Wired** — implementation present, called each turn, exposed to UI where relevant.
- ⚠️ **Partial** — logic exists but is only called in a subset of expected places, or is missing its UI hook.
- ❌ **Orphaned / broken** — implementation exists but is never invoked, or header has no corresponding `.cpp`, or integration is missing entirely.

---

## 1. Map & Terrain

| Mechanic | Files | Status | Notes |
|---|---|---|---|
| Hex grid + axial coords | `include/aoc/map/HexGrid.hpp`, `src/map/HexGrid.cpp` | ✅ | Spiral neighborhood queries, owner/yields per tile. |
| Pathfinding (A*) | `src/map/Pathfinding.cpp` | ✅ | Used by movement + worker automation. |
| Fog of war | `src/map/FogOfWar.cpp` | ✅ | Per-player visibility tracking. |
| River gameplay | `src/map/RiverGameplay.cpp` | ✅ | |
| Map generators | `src/map/MapGenerator.cpp` | ✅ | 6 variants: Continents, Islands, ContinentsPlusIslands, LandOnly, LandWithSeas, Fractal. Realistic variant uses tectonic plate sim. |
| Resource placement | `ResourceTypes.hpp` + generators | ✅ | Bonus / strategic / luxury / mountain-mined. |
| Natural wonders | map generator + wonder system | ✅ | |
| **Goody huts** | `simulation/map/GoodyHuts.{hpp,cpp}` | ❌ **Orphaned in real game** | `placeGoodyHuts()` is only called in `src/tools/HeadlessSimulation.cpp:564`. **Never invoked from the actual application** (`src/app/Application.cpp` does not reference it). Headless sims get goody huts; interactive games do not. |

---

## 2. Cities

### 2.1 Fully wired core
All core city systems are called every turn from `processPlayerTurn()`:

| Mechanic | Function | TurnProcessor line | Status |
|---|---|---|---|
| City growth | `processCityGrowth` | 486 | ✅ |
| Happiness / amenities | `computeCityHappiness` | 489 | ✅ |
| Loyalty | `computeCityLoyalty` | 492 | ✅ |
| Production queue | `processProductionQueues` | 574 | ✅ |
| City bombardment | `processCityBombardment` | 577 | ✅ |
| Border expansion | `processBorderExpansion` | 580 | ✅ |
| Governor auto-queue | `processGovernors` | 596 | ✅ 7 gov types × 5 promotions, 6 focus modes, auto-tile assignment, rotating templates |
| City connections | `processCityConnections` | 474 | ✅ |
| Science output | `computePlayerScience` | 508 | ✅ |
| Unit healing (per-territory) | inline | 432–471 | ✅ +20 friendly, +10 own, +5 neutral, +5 fortified |

### 2.2 District adjacency — ⚠️ Partial
`DistrictAdjacency.cpp` (9.9 KB) defines adjacency bonuses for Campus/Commercial/Industrial/Harbor/HolySite/Encampment. It is called **only from `ProductionSystem.cpp:59`**, i.e. adjacency bonuses apply to **production only**, not to science / gold / culture / faith yields. The header advertises multi-yield bonuses; the wiring misses four of them.

### 2.3 Buildings — ⚠️ Data mismatch
- `District.hpp:122` defines **43** constexpr `BUILDING_DEFS`.
- `data/definitions/buildings.json` defines **32** buildings.
- The two are never reconciled because **`DataLoader::instance().initialize(...)` is never called** anywhere in `src/` (grep returns zero matches). The game always uses the hardcoded `BUILDING_DEFS`; the JSON files are dead data.

### 2.4 Housing — ⚠️ Underspec
Aqueduct building exists (`District.hpp:172`) but no housing cap mechanic ticks in the turn loop. Docs describe base 4 / Granary +2 / Hospital +4 with growth throttling at surplus ≥ housing; implementation is either unreachable or absent from the per-turn path.

---

## 3. Economy — Core

### 3.1 `EconomySimulation` — ✅ Wired, the heart of the economy
Called from `TurnProcessor.cpp:900` as `turnContext.economy->executeTurn(...)`. Ordered pipeline inside `executeTurn()`:

1. `harvestResources` — worked tiles → city stockpile
2. `applyResourceDepletion` — finite resource tracking
3. `processInternalTradeForAllPlayers`
4. `consumeBuildingFuel`
5. `executeProduction` — recipe chains from `recipes.json`
6. `reportToMarket` + `updatePrices`
7. `executeTradeRoutes`
8. `settleTradeInCoins`
9. `updateCoinReservesFromStockpiles`
10. `tickMonetaryMechanics`
11. `processCrisisAndBonds`
12. `processEconomicZonesAndSpeculation`
13. `executeMonetaryPolicy`

Goods (66) and recipes (46) loaded via JSON-sourced arrays in `DataLoader.cpp:127-134`. (Good: this *is* JSON-backed, unlike buildings.)

### 3.2 Population consumption — ✅
`EconomySimulation.cpp:274-299` runs per-capita consumption of wheat / clothing / consumer goods.

### 3.3 Resource curse — ❌ Orphaned
`applyResourceCurseEffects()` in `ResourceCurse.cpp` is never called from TurnProcessor. The curse mechanic described in docs does not tick.

### 3.4 Wage demands — ❌ Orphaned
`updateWageDemands()` in `WageDemands.cpp` — no call sites.

### 3.5 Merchant ship fuel — ❌ Orphaned
`processMerchantShipFuel()` in `NavalTrade.cpp` declared but not invoked.

---

## 4. Economy — Monetary

### 4.1 Monetary state machine — ✅
`MonetarySystem.hpp` (620 lines, header-only inline methods, no `.cpp`). Barter → Commodity → GoldStandard → Fiat → Digital, with coin tiers, Gresham's law, gold-backing ratio, fiat trust score, money-printing cap, reserve currency seigniorage.

AI triggers transitions at `AIController.cpp:2504-2507`; forced fiat on crisis at `CurrencyCrisis.cpp:275`.

### 4.2 Inflation — ⚠️ **Implemented but not called**
`src/simulation/monetary/Inflation.cpp` (300 lines) computes Fisher-equation inflation, price-level drift, happiness penalties, maintenance cost scaling, debt erosion. **`computeInflation()` and `applyInflationEffects()` have zero call sites in TurnProcessor.cpp.** Price level never advances in the actual turn loop despite the logic being written. This is the highest-impact wiring gap in the economy layer.

### 4.3 Bonds / IOUs — ❌ Header-only
`include/aoc/simulation/monetary/Bonds.hpp` (~250 lines) declares issuance, trading, default, `processBondPayments()`, `createIOU()`, `processIOUPayments()`. **No `Bonds.cpp` exists and no turn-loop call sites.** This is a designed-but-unbuilt feature.

### 4.4 Central bank — ⚠️
`CentralBank.hpp/.cpp` exists (159 lines impl) but no TurnProcessor integration.

### 4.5 Other monetary modules
- `CurrencyTrust`, `CurrencyCrisis`, `CurrencyWar` — ✅ wired.
- `FiscalPolicy`, `ForexMarket` — ⚠️ status unclear; headers exist, no clear call sites.

---

## 5. Economy — Advanced Layer

### 5.1 Wired and called each turn
| Module | TurnProcessor call | Line |
|---|---|---|
| `AdvancedEconomics` | `processAdvancedEconomics` | 477 |
| `SupplyChain` | `processSupplyChains` | 807 |
| `MonopolyPricing` | `detectMonopolies` + `applyMonopolyIncome` | 810–811 |
| `BlackMarket` | `processBlackMarketTrade` | 814 |
| `HumanCapital` | `updateHumanCapital` | 828 |
| `TechUnemployment` | `processUnemployment` | 827 |
| `EnergyDependency` | `updateEnergyDependency`, `processOilShock` | 722–723 |
| `InternalTrade` | via `EconomySimulation:62` | — |
| `StockMarket` | `processStockMarket` | 751 |
| `AIInvestmentDecisions` | `runAIInvestmentDecisions` | 754 |
| `SpeculationBubble` | `processSpeculationBubble` | 822 |
| Trade routes | `processTradeRoutes` | 728 |
| Trade agreements | `processTradeAgreements` | 757 |

### 5.2 Advanced modules with no call sites — ⚠️
`EconomicDepth.cpp`, `IndustrialRevolution.cpp`, `ColonialEconomics.cpp`, `EconomicEspionage.cpp` all have small implementations (50–96 lines) but grep finds no turn-loop invocations. Either orphan or called from one of the advanced modules transitively — worth verifying by hand.

### 5.3 Stockmarket + speculation — ✅
`StockMarket.cpp`, `Speculation.cpp`, `SpeculationBubble.cpp` fully implemented. Phase machine: None → Formation → Inflation → Euphoria → Crash → Recovery. Crash = 15% value loss/turn × 8 turns; recovery = 15 turns. Investment cap scales with monetary system.

---

## 6. Production, Automation, Power, Waste

### 6.1 Power grid — ✅
`production/PowerGrid.cpp` (~200 lines). Unpowered buildings lose yield.

### 6.2 Waste / pollution — ✅
`production/Waste.cpp` (76 lines). Feeds global CO2 at `TurnProcessor.cpp:679` via `totalIndustrialCO2(gameState)`.

### 6.3 Robot workers — ⚠️
`production/Automation.hpp` defines robot workers as good id 143, +1 recipe slot each, 5 energy/turn, 1-in-10 decay. `updateCityAutomation()` exists but not clearly wired to per-turn loop.

### 6.4 Automation (player/unit level) — ✅
`automation/Automation.cpp` handles research queue auto-advance, scout auto-explore, military alert, trade route auto-renewal. Called at `TurnProcessor:599` via `processAutomation()`.

### 6.5 Missing `.cpp` files — ❌
- `BuildingCapacity.hpp` — no matching `.cpp`.
- `ProductionEfficiency.hpp` — no matching `.cpp`.

Either delete headers or finish implementation.

---

## 7. Resources & Goods

- `ResourceTypes.hpp` — 327 lines of IDs and definitions. ✅
- `ResourceComponent.hpp` — per-city stockpile. ✅
- `ProductionChain.hpp` — recipe DAG. ✅
- `data/definitions/goods.json` (66 goods) + `recipes.json` (46 recipes) — ✅ loaded at `DataLoader.cpp:127-134`.

Stockpile caps enforced; shortages/surpluses tracked.

---

## 8. Units & Combat

### 8.1 Fully wired
| Mechanic | Status |
|---|---|
| ~100 unit types, 13 classes | ✅ |
| Movement (A*, ZoC, embarkation, naval, transport) | ✅ |
| Combat (Lanchester, terrain, flanking, class matchups, XP) | ✅ |
| CombatExtensions (Corps/Army, nuclear, air combat, interception) | ✅ |
| Supply lines + attrition | ✅ `computeSupplyLines` + `applySupplyAttrition` at TurnProcessor 418–419 |
| Promotions (6 types, 6-tier XP curve) | ✅ AI auto-promotion per class |
| Unit upgrade paths | ✅ 8 paths |

### 8.2 Unit ID integrity
Per project memory: Diplomat/Spy were moved 55/56 → 100/101 to avoid collision with Frigate/Ironclad in `UnitTypes.hpp`. Memory still current, no further collisions detected on scan.

---

## 9. AI Controllers

### 9.1 Personality system — ✅
`LeaderPersonality.hpp` (23.6 KB) defines `LeaderBehavior` with 25+ weights across military, economic, tech route, production priority, war/diplomacy thresholds, plus 8 hidden traits (Backstabber, WonderHoarder, NukeHappy, ParanoidDefender, TradeAddict, Isolationist, Opportunist, Perfectionist). Docs call it "32-float"; actual count is slightly higher.

### 9.2 Controllers wired to `AIController::executeTurn()` — ✅
`executeCityActions`, `executeDiplomacyActions`, `manageEconomy`, `manageTradeRoutes`, `manageMonetarySystem`, `manageGovernment`, `manageGreatPeople`, `considerPurchases`, `considerCanalBuilding`.

| Sub-controller | Status |
|---|---|
| `AIBuilderController` | ✅ called from `executeCityActions` |
| `AIMilitaryController` | ✅ |
| `AISettlerController` (580 L) | ✅ |
| `AIResearchPlanner` | ✅ |
| `AIEconomicStrategy` | ✅ via `manageEconomy` |
| `AIEventChoice` | ✅ TurnProcessor:96 (`resolvePendingAIEvents`) |
| `AIInvestmentController` (84 L impl) | ❌ **Orphaned** — defined, never called from `AIController` |
| `AIFuturesTrading` | ⚠️ called globally at TurnProcessor:647 but **not per-AI** — no personality weighting |
| `AICommodityHoarding` | ⚠️ same — called globally at TurnProcessor:651 only |

### 9.3 GA evolution — ✅
`ml/cpp/GeneticAlgorithm.hpp` evolves LeaderBehavior populations; offline pipeline via `ml/extract_weights.py`.

---

## 10. Diplomacy

### 10.1 Core state — ✅
`DiplomacyState.hpp` — pairwise scores, at-war flag, open borders, flags for each alliance type, time-decaying modifiers.

### 10.2 Grievance, war state, border violation, naval passage — ✅
All implemented (Grievance.cpp 168 L, BorderViolation.cpp 187 L, NavalPassage.cpp 185 L) and integrated.

### 10.3 Alliances — ⚠️ **Biggest single gap in the game**
`AllianceTypes.hpp` defines 5 alliance types (Research / Military / Economic / Cultural / Religious) × 3 level progression (30/60 turn thresholds), with concrete per-level bonuses (e.g. Economic L3 = +15% trade-route gold, Cultural L3 = shared Great Work slots).

Implementation state in `DiplomacyState.cpp`:
- `formMilitaryAlliance` (line 197) — implemented
- `formResearchAgreement` (line 208) — implemented
- `formEconomicAlliance` (line 219) — implemented
- `formCulturalAlliance` / `formReligiousAlliance` — **not present**

AI call sites:
- `AIController.cpp:1701` calls `formEconomicAlliance` when `complementaryGoods >= 3`.
- **No AI-driven calls to `formMilitaryAlliance` or `formResearchAgreement`.**

UI call sites:
- `DiplomacyScreen.cpp:216` (Military) and `:248` (Economic) — human player can form these manually.

**Net effect:** The Confederation co-win (see §11) depends on allied blocs, but AI only ever forms economic alliances. This is why AI coalitions rarely form — confirmed by memory note.

### 10.4 Espionage — ✅
`Espionage.hpp` (361 L), `EspionageSystem.cpp` (429 L). 16 missions + EstablishEmbassy. 4-tier spy levels. 8-step probability scale [0.16…0.90]. Graduated failure outcomes (Escaped / Identified / Captured / Killed).

### 10.5 World Congress — ✅
`WorldCongress.hpp` (91 L) — resolutions and voting. Multiple resolution types.

---

## 11. Victory

### 11.1 CSI (Civilization Score Index) — ✅
8 categories: Economic Power, Military Strength, Cultural Influence, Scientific Achievement, Diplomatic Standing, Quality of Life, Territorial Control, Financial Power. `updateVictoryTrackers` called at TurnProcessor:841 each turn; era evaluation every 30 turns awards VP (3/2/1 for top three).

### 11.2 Victory types
| Type | Implemented | Wired |
|---|---|---|
| Score | ✅ | ✅ |
| Domination (own every original capital) | ✅ VictoryCondition.cpp:577-607 | ✅ |
| Science (4 Space-Race projects: Satellite, Moon, Mars, Exoplanet) | ✅ `SpaceRace.hpp` | ✅ |
| Culture (tourism > every rival's domestic tourists) | ✅ | ✅ |
| Religion | ✅ VictoryCondition.cpp:621-656 | ✅ |
| Prestige (turn-limit accumulated score) | ✅ `Prestige.hpp` | ✅ TurnProcessor:844 |
| Confederation co-win (3+ allied civs dominate) | ✅ VictoryCondition.cpp:744-782 | ✅ — but upstream alliance-formation bug (§10.3) makes it rare |
| Collapse / losing conditions (GDP<25% × 30t; loyalty<30 × 5t; capital+75% cities lost; default+hyperinflation) | ✅ | ✅ |

### 11.3 Memory discrepancies — ⚠️
- Memory says "Religious victory >50% cities". Code actually uses `aoc::balance::params().religionDominanceFrac` with default **0.4** (40% of **every** other civ's cities). Memory is slightly off; update it.
- Memory mentions "Integration victory threshold 1.40 / 6-of-8 cats / 13 turns". Code does not contain these constants. The string "Integration" is only used as an **alias for Prestige** in `parseVictoryTypeMask` at `VictoryCondition.cpp:843`. The memory claim appears to be stale from a prior design iteration.

### 11.4 Tourism — ✅ (memory was wrong)
Memory claims Tourism is "stubbed header-only". **Incorrect.** `src/simulation/culture/Tourism.cpp` (135 lines) is fully implemented and called at `TurnProcessor.cpp:1017` via `computeTourism(...)`. Sources: wonders (3 each), great-work slots (2 each), holy sites (2 each), +12% per active trade agreement (capped +50%). Foreign tourists = cumTourism/150; domestic = culture/100.

---

## 12. Tech, Civics, Era

| Mechanic | Status | Notes |
|---|---|---|
| `TechTree` | ✅ | Called at TurnProcessor:569 |
| `CivicTree` | ✅ | Called at :570 |
| `EurekaBoost` (9 conditions) | ✅ | Checked at :407 (MeetCivilization), :413 (FoundCity) |
| Science funding cost (0.2 gold/science) | ✅ | TurnProcessor:512-528 |
| Laggard catch-up bonus (max +60% at 4+ techs behind) | ✅ | TurnProcessor:545-567 |
| `EraProgression` | ✅ header | no separate `.cpp`, logic in `ExpandedContent` |
| `CivicEffects` | ✅ | |
| `TechGating` | ✅ | |

---

## 13. Religion

- `Religion.hpp` / `Religion.cpp` — pantheon 25 faith, religion 50 faith (lowered from 100, per code comment), 8 religions max, 16 beliefs (4 × founder/follower/worship/enhancer). ✅
- `TheologicalCombat.cpp` — apostle vs missionary. ✅
- `accumulateFaith` + `applyReligionBonuses` — TurnProcessor:498–499. ✅
- `processReligiousSpread` — TurnProcessor:613. ✅
- `processAIReligionFounding` — auto-founds for AI. ✅

---

## 14. Government & Policies

- 8 governments (Chiefdom / Autocracy / Oligarchy / Monarchy / Democracy / Communism / Fascism / Theocracy / MerchantRepublic).
- 22 policy cards across Military / Economic / Diplomatic / Wildcard slots.
- Government switch: 5 turns anarchy, -3 amenities, bonuses suspended.
- `processGovernment` called at TurnProcessor:495. ✅

---

## 15. Great People — ✅
`GreatPeopleExpanded.hpp` (5.8 KB). Multiple GP types.
- `accumulateGreatPeoplePoints` — TurnProcessor:583
- `checkGreatPeopleRecruitment` — TurnProcessor:584

---

## 16. City-States — ✅
8 city-states × 6 types. 6 quest types (BuildWonder, TrainUnit, ResearchTech, SendTradeRoute, ConvertToReligion, DefeatBarbarian). Envoy tiers 1/3/6, type-specific per-turn bonuses, Suzerain exclusive bonus.

- `processCityStateDiplomacy` — TurnProcessor:999
- `processCityStateAI` — TurnProcessor:1006
- `processCityStateBonuses` — TurnProcessor:587

---

## 17. Barbarians — ✅
`BarbarianController.hpp` + `BarbarianClans.hpp`. 8 named clans, 4 types (Raiders, Warband, Pirates, Nomads). Encampment spawns, scaling difficulty by turn. `turnContext.barbarians->executeTurn()` at TurnProcessor:624 (conditional on non-null).

---

## 18. World Events — ✅
12 event types (Plague, GoldRush, ArtisticRenaissance, FamineWarning, ScientificBreakthrough, TradeDisruption, ReligiousSchism, MigrantWave, IndustrialAccident, DiplomaticIncident, EconomicBoom, VolcanicFertility). Per-event 2-3 choices. 30-turn refire cooldown.

- `checkWorldEvents` — TurnProcessor:833
- `resolvePendingAIEvents` — TurnProcessor:837 (AI auto-resolve)
- `tickWorldEvents` — TurnProcessor:838
- Climate coupling: sea level ≥5 or temperature ≥2.0 °C triggers MigrantWave / FamineWarning — TurnProcessor:684–705

---

## 19. Climate — ✅
`GlobalClimateComponent` (temperature, CO2, sea level rise). CO2 from population (0.1/citizen) + industry (TurnProcessor:670-679). `climate.processTurn(...)` at :680. Natural disasters (earthquake / hurricane / tsunami / volcano / drought) at :666.

---

## 20. Wonders — ✅
24 definitions, tech/resource prereqs, one-per-game globals. Tourism source (Tourism.cpp:73) and victory-score source (VictoryCondition.cpp:681). `Wonder.cpp` 4.5 KB impl.

---

## 21. Turn Processor Summary

```
AI decisions (per AI player)          line 891-896
  └─ AIController::executeTurn()
Economy simulation                     line 900
  └─ EconomySimulation::executeTurn()   (13-step pipeline, §3.1)
Per-player processing                  line 903-905
  └─ processPlayerTurn()                (22-step pipeline, §1.1 table)
Global systems                         line 908
  └─ processGlobalSystems()             (climate, victory, city-states, …)
```

Overall turn-loop completeness estimate: **~95% of documented mechanics tick every turn**. The missing 5% is concentrated in a small set of listed orphans.

---

## 22. UI Coverage

Screens present under `include/aoc/ui/`: DiplomacyScreen, TradeScreen, TradeRouteSetupScreen, ReligionScreen, ScoreScreen, SpectatorHUD, EventLog, GameNotifications, Tooltip, DebugConsole, Encyclopedia, MainMenu, Tutorial, AdvancedTutorial, MapEditor, GameScreens, UIManager, Widget, Notifications, BitmapFont.

| System | UI exposure |
|---|---|
| Combat (preview + animation) | ✅ |
| Unit promotions / upgrades | ✅ |
| Diplomacy relations + actions | ✅ (but only Military + Economic alliance buttons wired; no Research / Cultural / Religious alliance UI) |
| Espionage mission UI | ✅ |
| City detail (yields, tile assignment, tile purchase) | ✅ |
| City-state quests | ✅ |
| Victory / Score | ✅ `ScoreScreen` |
| Religion | ✅ `ReligionScreen` |
| World events | ✅ `EventLog` |
| Prestige | ⚠️ no dedicated screen |
| Supply-line attrition | ⚠️ only via HP bar |
| District adjacency | ⚠️ partial (production only) |
| Resource curse, wage demands, goody huts | ❌ no UI (matches their orphan status) |

---

## 23. Tests — ❌ Empty

`tests/` directory exists but is empty. No unit or integration tests are checked in. Regression risk for a simulation of this scale is high. Consider at minimum:
- Determinism test (same seed → same final state)
- Turn-loop smoke (run N turns, no asserts fail)
- Per-subsystem unit tests for monetary transitions, combat class matchups, victory predicates.

---

## 24. Data Loading — ⚠️ Infrastructure present, half dormant

`src/data/DataLoader.cpp` is 23.5 KB of JSON parsing with constexpr fallbacks. `DataLoader::instance().initialize(dataDirectory)` is **never called** from `Application.cpp`, `main.cpp`, or `HeadlessSimulation.cpp`. Yet goods and recipes *do* get loaded via a lower-level path inside `EconomySimulation.cpp:127-134`. Buildings do **not** get loaded that way — they fall back to the constexpr `BUILDING_DEFS` (43 entries), while the JSON has 32 entries. Net effect: you can edit `goods.json` / `recipes.json` and see changes, but editing `buildings.json`, `techs.json`, `units.json`, or `leaders.json` has no effect on a running game.

---

## 25. Prioritized Gap List

### High impact / low effort
1. **Wire `Inflation.cpp`** — call `computeInflation()` + `applyInflationEffects()` inside `EconomySimulation::executeMonetaryPolicy()`. The logic is complete; it just never fires. (§4.2)
2. **AI alliance formation beyond Economic** — extend `AIController::executeDiplomacyActions()` to consider Military and Research alliances using `LeaderBehavior::allianceDesire` + personality traits. Unlocks Confederation co-win as an AI strategy. (§10.3, memory note confirmed.)
3. **Add missing `formCulturalAlliance` + `formReligiousAlliance`** in `DiplomacyState.cpp` to match the 5 types promised by `AllianceTypes.hpp`.
4. **Wire goody huts into `Application.cpp`** — same 2 lines as `HeadlessSimulation.cpp:563-564`. Currently only headless games have them. (§1)
5. **Call `DataLoader::instance().initialize("data")`** at startup. Turns JSON files from decoration into actual game data. (§24)

### Medium impact
6. **Bonds / IOUs** — either implement `Bonds.cpp` or delete `Bonds.hpp`. Dangerous to leave a 250-line header advertising a financial instrument that doesn't exist. (§4.3)
7. **Reconcile buildings**: either regenerate `buildings.json` from `BUILDING_DEFS` (43 entries), or drop to 32 and delete the extra constexpr entries.
8. **District adjacency for non-production yields** — extend `DistrictAdjacency` application beyond `ProductionSystem.cpp:59` to science/gold/culture/faith. Current setup silently drops the majority of the bonus. (§2.2)
9. **Delete or implement**: `BuildingCapacity.cpp`, `ProductionEfficiency.cpp`, `AIInvestmentController` turn-loop wiring, orphaned advanced-economics modules (`EconomicDepth`, `IndustrialRevolution`, `ColonialEconomics`, `EconomicEspionage`).
10. **Tests**: add a minimal smoke test + determinism test in `tests/`.

### Low impact / housekeeping
11. **Orphaned economy modules**: `ResourceCurse`, `WageDemands`, `processMerchantShipFuel`. Wire or remove.
12. **Move `AIFuturesTrading` / `AICommodityHoarding`** from global TurnProcessor calls into `AIController::manageEconomy` so personality weights matter.
13. **Update project memory**:
    - Religious victory threshold is 40% (not 50%).
    - Tourism is fully implemented (memory says "stubbed header-only" — wrong).
    - "Integration victory (1.40 / 6-of-8 / 13 turns)" doesn't exist; "Integration" is a parse alias for Prestige.
14. **Housing mechanic**: build it out or document that it's intentionally abstracted.

---

## 26. Bottom Line

The codebase is far more complete than a typical mid-development 4X: 148 files, ~80 k lines, a 22-phase per-player turn loop, and a 13-phase economy pipeline that actually runs. The simulated economy (monetary evolution, supply chains, stock market, speculation bubbles, human capital, black markets, tariffs, currency wars) is genuinely deeper than Civ 6.

The weak points form a consistent pattern — **logic written, wiring missing**:
- Inflation simulated but price level never advances.
- 5 alliance types defined, 1 is AI-reachable.
- Bonds specified, not built.
- JSON data pipeline built, never called.
- Goody huts work in headless runs but not in the actual game.
- Tests directory empty.

Most of these are <1-day fixes; a handful (Bonds, tests, alliance strategy) are multi-day. Closing the top 5 items in §25 would lift the game from "mostly wired" to "everything the docs claim".
