# Audit Report — Age of Civ

Repo: `/home/ndietz/Repositories/private/age_of_civ`
Audited: 2026-05-10
Reviewers spawned: 8 (parallel)
Standards: claude-code-tweaks (general + cpp), CLAUDE.md project rules

---

## Orientation

- **Stack**: C++20, CMake 3.20+, ccache, Vulkan + GLFW (optional via `AOC_HEADLESS`), sd-bus, cpp-httplib, Lua, OpenMP.
- **Size**: 234 cpp + 271 hpp, ~118k LOC C++. 36+ documented in `docs/`.
- **Targets**: `age_of_civ` (interactive), `aoc_simulate` (headless), `aoc_trace_dump`, `aoc_mapgen`, plus 3 unit tests.
- **Entry points**: `src/main.cpp`, `src/tools/HeadlessSimulation.cpp`, `src/tools/MapGenCli.cpp`, `src/tools/TraceDump.cpp`.
- **AI-context**: `CLAUDE.md` (binding physics + debug API rules), `claude-code-tweaks/` (symlink to standards repo).
- **Build**: presets in `CMakePresets.json`; tests via CTest (3 tests).
- **CI**: none in repo.
- **Subsystems audited**: map/gen physics, simulation/economy, simulation/ai, simulation/{diplomacy,city,victory,unit}, debug+net+scripting+save+data (security), debug+net+core (concurrency), src/ (silent-failure sweep), render+app+ui+core+audio.

---

## Findings

### CRITICAL — must fix

#### Memory safety / UB

- **[unit/Combat.cpp:353–413]** `resolveMeleeCombat` uses raw `Unit*` after `removeUnit` invalidates the underlying `unique_ptr` vector storage. Stack-kill loop dereferences dangling pointers. *Fix: collect indices or do all secondary removals before primary.* (gameplay-reviewer)
- **[diplomacy/DiplomacyState.cpp:52–58]** `addModifier` invalidates `relation(a,b)`/`relation(b,a)` references when `m_relations.modifiers.push_back` triggers reallocation. Multiple callers (`declareWar`, `tickModifiers`) hold both refs simultaneously. *Fix: pre-`reserve()` `m_relations` in `initialize()`; document invariant.* (gameplay-reviewer)
- **[diplomacy/WorldCongress.cpp:407–409]** `castVote` writes `votes[player]` without guarding `INVALID_PLAYER` (=0xFF). If votes.size()==256, OOB write at slot 255. *Fix: explicit `INVALID_PLAYER` guard before bounds check.* (gameplay-reviewer)
- **[ai/AIDiplomacyController.cpp:42–52]** Local `MAX_PLAYER_COUNT = 16`, but project-wide `MAX_PLAYERS = 20`. Stack-buffer overrun for 17–20 player games. *Fix: use `MAX_PLAYERS` everywhere.* (ai-reviewer)
- **[ai/AIBuilderController.cpp:66–555]** Builder snapshot raw `Unit*` ptrs dangle after in-loop `removeUnit` reallocation. *Fix: index-based snapshot or null-mark slots after removal.* (ai-reviewer)
- **[map/gen/Plate.hpp:23–26]** `Plate` struct fields `cx, cy, rot, landFraction` (and possibly `latDeg/lonDeg/euler*/angularVelDeg`) lack default initializers. `Plate micro;` / `Plate fresh;` in `MapGenerator.cpp:1364,1482` produce indeterminate floats fed directly into Rodrigues rotation. UB. *Fix: add `= 0.0f` defaults to all float members.* (map-reviewer)
- **[map/gen/SphereFieldPhysics.cpp:562]** `int16_t childId = static_cast<int16_t>(plates.size() - 1)` overflows silently if plates >32767, yielding negative pid that bypasses guards and causes OOB read of `plates[]`. *Fix: assert cap or widen to int32_t.* (map-reviewer)
- **[economy/Market.cpp:91]** `marketData(goodId)` returns `m_goods[goodId]` unchecked — peer accessors guard. Bad goodId from save/mod = UB. *Fix: assert + bounds-check.* (economy-reviewer)
- **[economy/ComparativeAdvantage.cpp:49]** `playerProductionRate` recurses without depth limit / cycle detection. Stack overflow possible if recipes form cycle. *Fix: add `visited` set or convert to topological pass.* (economy-reviewer)
- **[monetary/CentralBank.cpp:39,65]** `goldAmount * goldPrice` (int64×int64) overflow in `buyGold`/`sellGold` produces negative cost = treasury credit. *Fix: saturating mul or precondition assert.* (economy-reviewer)

#### Concurrency / data races

- **[app/Application.cpp:648–703]** DebugServer POST handlers (`/sim/set-creator-time`, `/sim/step`, `/sim/re-roll`) mutate `m_creatorSeed`, `m_creatorEpochCache`, `m_hexGrid` from cpp-httplib worker thread without lock. Header explicitly forbids this. *Fix: queue mutation, drain on main loop.* (render-reviewer)
- **[app/Application.cpp:707–711]** `/quit` calls `glfwSetWindowShouldClose` from worker thread; main calls `glfwWindowShouldClose`. GLFW not thread-safe. *Fix: atomic flag, main checks.* (render-reviewer)
- **[debug/DebugServer.cpp:92–100]** `stop()` joins listener BEFORE `running.store(false)`. Concurrent `isRunning()` returns stale `true`. *Fix: store-false first.* (concurrency-reviewer)
- **[tools/MapGenCli.cpp:464–475]** `regenAtMy` mutates `grid`/`currentMy`/`liveConfig` without acquiring `gridMutex`; relies on caller. Latent race; one refactor away from corruption. *Fix: pass `lock_guard&` or assert held.* (concurrency-reviewer)
- **[net/GameDBus.cpp:113–114]** `m_impl` non-atomic write paired with `m_active.store(true)`. Safe under seq_cst but undocumented; relaxation in future = UB. *Fix: comment the ordering invariant.* (concurrency-reviewer)

#### Project rule violations

- **[map/MapGenerator.cpp:1381–1400]** Microplate spawn uses Euclidean nearest-centroid distance to plates (Voronoi check). Comment even names it "Voronoi territory". CLAUDE.md rule 1 forbids. *Fix: replace with `sphereField.plateId[cellIndex(midLat,midLon)]` lookup.* (map-reviewer)
- **[map/gen/PlateReference.cpp]** Loads Bird (2003) PB2002 real-Earth plate catalog. CLAUDE.md rule 2 forbids dataset imports. Currently uncalled but compiled. *Fix: delete file or gate behind `#ifdef AOC_ALLOW_REFERENCE_CATALOG`.* (map-reviewer)

#### Security

- **[scripting/LuaEngine.cpp:59]** `luaL_openlibs()` opens full stdlib including `os`, `io`, `package`, `debug`. Any mod = arbitrary code execution. *Fix: selective `luaL_requiref` for safe libs only; nil out `os/io/package/require/dofile/loadfile/debug`.* (security-reviewer)
- **[save/Serializer.cpp:122–186]** `ReadBuffer` primitive reads (`readU8/16/32/64/String/Bytes`) skip per-call bounds check; only outer `sectionSize` validated. Malformed save → OOB read / crash / controlled read. *Fix: `hasRemaining(N)` guard + error flag in every primitive.* (security-reviewer)
- **[save/Serializer.cpp:1450,1470,1484,1509,1521]** `unitCount`/`cityCount`/`workedCount`/`pathSize`/`queueSize` read from file then passed straight to `vector::reserve()`. Crafted save with 0xFFFFFFFF = 4 GB OOM per field. *Fix: cap (`MAX_UNITS=10000` etc.) before reserve.* (security-reviewer)
- **[debug/DebugServer.cpp:48–53]** `e.what()` injected into JSON response unescaped. Attacker-controlled exception messages (Lua errors echoing input) = JSON injection. *Fix: escape `\` and `"`.* (security-reviewer)

#### Silent failures / data loss

- **[app/DebugCommandFile.cpp:84,87]** `filesystem::rename` and `filesystem::remove` errors discarded. Failed rename → stale response; failed remove → command re-dispatched every poll → repeated state mutation. Primary debug instrument silently double-executes. *Fix: check `ec`, log, halt.* (silent-failure-hunter)
- **[map/MapGenerator.cpp:857–880]** Craton-seed loop attempts placement 64×; on failure leaves seed at zero-init `(0,0)`. World silently has cratons clustered at top-left grid corner. *Fix: log warn or relax separation on retry.* (silent-failure-hunter)
- **[scripting/LuaEngine.cpp:174–176]** `callFunctionWithTurn` swallows `lua_pcall` error (peer overloads log it). Per-turn victory/event hook errors invisible. *Fix: log error before pop.* (silent-failure-hunter)
- **[save/Serializer.cpp:1341–1344]** `file.write()` failure returns `SaveFailed` with no log; writes to final path (no temp+rename) = corrupt save. *Fix: log + atomic write via tmp-then-rename.* (silent-failure-hunter)

---

### WARNING — should fix

#### Determinism (GA / seed reproducibility)

- **[ai/AIController.cpp:153, 1673–1708]** `unordered_map` iteration order drives military unit choice + sell/buy desires. Seeds non-reproducible across runs/hosts. *Fix: `vector<int32_t>` indexed by goodId.* (ai-reviewer)
- **[ai/AIDiplomacyController.cpp:178–217]** War declaration uses `(military * weights) % 100` hash whose inputs depend on per-turn AI execution order, not RNG. *Fix: feed the existing `rng` parameter.* (ai-reviewer)

#### Memory / lifetime

- **[diplomacy/EspionageSystem.cpp:360–557]** Spy `toRemove` raw ptrs may dangle after first `removeUnit` if same unit appears twice. (gameplay)
- **[economy/TradeRouteSystem.cpp:829–841]** `traderUnits` raw ptrs into `vector<unique_ptr<Unit>>` during loop that calls cleanup paths. (economy)
- **[ai/AIDiplomacyController.cpp:493–501]** `victim->name()` after `acceptDeal` may have transferred city. Dangling ptr in LOG. (ai)
- **[net/Transport.cpp + .hpp:65–98]** `LocalTransport` queues unsynchronized; class comment says "in-process zero overhead" but doesn't enforce single-thread. (concurrency)
- **[net/GameDBus.cpp:122–125]** `stop()` accesses `m_impl->pendingCall` without `pendingMutex`. (concurrency)
- **[net/GameDBus.cpp:73,99,107,133]** Raw `new`/`delete Impl` instead of `unique_ptr`. (concurrency)
- **[tools/MapGenCli.cpp:759–777]** `/quit` → `server.stop()` may not drain in-flight handlers; HTTP threads dereference dangling stack locals (`grid`, `liveConfig`, `currentMy`). (concurrency)
- **[app/Application.hpp:459, app/Application.cpp:1484]** `m_creatorEpochCache` unbounded — 60+ HexGrid copies with no LRU. (render)

#### Logic / correctness bugs

- **[city/ProductionSystem.cpp:429–501]** Wonder race-loss path erases queue front, then falls through to `popCompleted` → double-pop next item. (gameplay)
- **[victory/VictoryCondition.cpp:556–567]** `CollapseType::DebtSpiral` requires `inDefault && inHyper` but `activeCrisis` is single-enum. Unreachable. *Fix: bitmask or two booleans.* (gameplay)
- **[victory/VictoryCondition.cpp:103–120]** Eliminated players with stale cities still drag global CSI averages. *Fix: skip `isEliminated`.* (gameplay)
- **[city/Secession.cpp:115–133]** Seceded city stays in ex-owner's `cities()` vector; inconsistent with other code that filters by `owner()`. *Fix: pick one canonical convention.* (gameplay)
- **[city/CityGrowth.cpp:435–442]** O(P×T) worked-tile membership scan inside growth loop. *Fix: `unordered_set<int32_t>` lookup.* (gameplay)
- **[diplomacy/WarWeariness.cpp:33]** Barbarian war weariness accumulates without bound (no `BARBARIAN_PLAYER` skip). (gameplay)
- **[diplomacy/DiplomacyState.cpp + .hpp:25–31, 210–211]** `relation(a,b)` only `assert`s — `INVALID_PLAYER`/`BARBARIAN_PLAYER` from external callers may pass through release builds. (gameplay)
- **[victory/VictoryCondition.cpp:787–788]** Float compare `bestCulture >= CULTURE_VICTORY_THRESHOLD` with no epsilon; accumulator may stall sub-threshold. (gameplay)
- **[ai/AIEconomicStrategy.cpp:107–124]** `aiSanctionStrategy` logs intent, never applies sanctions. Misleading log spam. (ai)
- **[ai/AIEconomicStrategy.cpp:173–190]** `aiManagePowerGrid` is empty stub but called per-turn per civ. (ai)
- **[ai/AIController.cpp:366]** `bubble = 1.0f` placeholder dead var; comment claims dynamic. (ai)
- **[ai/AIController.cpp:1699]** `totalStockpile[g]` insert-on-read inflates map size and erases supply-history meaning. (ai)
- **[ai/AIController.cpp:98–112, AIMilitaryController.cpp:78–95]** Threat/composition functions don't skip eliminated civs. Wasted work + inflated threat. (ai)
- **[ai/AIResearchPlanner.cpp:58–64]** `ownedBuildings` rebuilt every turn over all cities/districts. (ai)
- **[debug/DebugServer.cpp:10]** `#define CPPHTTPLIB_THREAD_POOL_COUNT 4` in `.cpp` — ODR risk if `httplib.h` included elsewhere with default 8. *Fix: move to CMake `target_compile_definitions`.* (concurrency)
- **[core/Log.hpp:66–86]** `logMessage` emits 3 separate `fprintf` calls — interleaving across threads garbles lines. *Fix: single `snprintf` then one `fprintf`.* (concurrency)

#### Hot-path performance

- **[economy/TradeRouteSystem.cpp:436, 247, 263, 1060, 1326]** `longestRangeGap` + `findCityByLocation`: O(players × cities) per tile per trader per turn. *Fix: pre-build `unordered_map<AxialCoord, bool> cityRelay` once per turn.* (economy)
- **[economy/CommodityExchange.cpp:125–181]** O(n_civs² × goods) every turn = ~216k iter for 36×36×167. *Fix: invert into per-good surplus/shortage lists.* (economy)
- **[economy/StockMarket.cpp:171–182]** Investment mirror uses `principalInvested` as part of identity key + linear scan. *Fix: stable monotonic `investmentId`.* (economy)
- **[economy/ComparativeAdvantage.cpp:43, 63]** Pairwise computation is O(n_civs² × goods × recipe_depth) per turn. *Fix: cache per-player rate vectors.* (economy)
- **[economy/TradeRouteSystem.cpp:994]** `std::string` concatenation for toll-refused notification per trader per crossing. (economy)
- **[ai/AIController.cpp:1007, 1673]** `candidates` and `totalStockpile` heap-allocated per city per turn (~360 + 36 allocations). *Fix: hoist as caches.* (ai)
- **[ui/UIManager.cpp:202–205]** `m_eventLog.erase(begin())` on full vector (50 entries) on hot input path. *Fix: ring buffer.* (render)
- **[ui/UIManager.cpp:548–589]** `collectFocusable` allocates per Tab keystroke. *Fix: cache + invalidate.* (render)
- **[map/gen/SphereFieldPhysics.cpp:472]** `std::exp(-dtMy / RIFT_THRESHOLD_MY)` per cell instead of once per epoch. (map)
- **[map/gen/SphereFieldPhysics.cpp:496–618]** Wilson rifting walks 259200 cells THREE TIMES per rift. *Fix: pre-collect plate cell indices.* (map)
- **[map/gen/SphereFieldPhysics.cpp:626–768 + MapGenerator.cpp:1067–1140]** O(P² × CELL_COUNT) merge sweeps; defer remap until end of epoch. (map)
- **[map/gen/EarthSystem.cpp:108–183]** ~600k `sqrt` calls per single-threaded volcanism pass. *Fix: squared-magnitude compare.* (map)
- **[map/gen/EarthSystem.cpp:416–448]** Tsunami `hazard` write inside OpenMP parallel loop reads neighbor cells = data race. *Fix: split read pass / merge pass, or drop pragma.* (map)

#### Renderer correctness

- **[render/SpriteRenderer.cpp:387–452]** Multi-batch upload writes same instance buffer offset 0 — risks overwriting in-flight GPU read at high sprite counts. *Fix: ring buffer of sub-ranges.* (render)
- **[render/Particles.cpp:65–66]** `vx *= (1 - 2*dt)` is frame-rate-dependent and inverts at dt > 0.5. *Fix: `pow(damping, dt)`.* (render)

#### Security (lower severity)

- **[net/GameDBus.cpp:44–48]** `TakeScreenshot` accepts any absolute path — local DBus path traversal. *Fix: canonicalize + allowlist `$HOME/Pictures`.* (security)
- **[net/GameServer.cpp:213–215]** `validateCommand` returns true unconditionally. Owner / range check missing. Latent risk if remote transport added. (security)
- **[data/JsonParser.hpp:136–299]** No nesting depth limit → stack exhaustion on malicious mod JSON. *Fix: `depth` param, cap 64.* (security)
- **[save/Serializer.cpp:106–109]** `writeString` truncates >65535-byte strings silently → save desync corrupting whole section. (security)
- **[save/Serializer.cpp:175–181]** `readString` adds `m_offset + len` without overflow protection. (security)
- **[data/DataLoader.cpp:18–26]** Read-error indistinguishable from missing file → silent fallback to hardcoded data. (silent-failure)

#### Misc warnings

- **[map/MapGenerator.cpp:1069–1077]** Plate collision uses Euclidean Mollweide distance, not haversine. Polar plates fuse early; equatorial plates miss. *Fix: haversine + threshold in radians.* (map)
- **[map/gen/SphereFieldPhysics.cpp:204,314,438,810,1182,1414]** Six physics functions silently `return` on empty plate vector. (silent-failure)
- **[ui/UIPersistence.cpp:35,40]** `catch(...) continue` discards parse errors on layout hot-reload. (silent-failure)
- **[economy/Market.cpp:44,79]** `uint16_t goodIndex` — wraps at 65535. No `static_assert`. (economy)
- **[economy/IndustrialRevolution.cpp:171]** `turnAchieved[Fifth]` index-5 OK today; no `static_assert` if Sixth is added. (economy)
- **[economy/TradeRouteSystem.cpp:975,981]** `const_cast<DiplomacyManager*>` — broken const-correctness in call chain. (economy)
- **[monetary/CurrencyCrisis.cpp:168]** Log says "25%%" but threshold is 30%. (economy)
- **[ai/AIResearchPlanner.cpp:279]** `auto` lambda param violates project rule "never use auto". (ai)
- **[resource/EconomySimulation.cpp + TradeRouteSystem.cpp]** Multiple `auto` usages violating project style. (economy)
- **[map/MapGenerator.cpp:744–1535]** `DT` (drift fraction) vs `MY_PER_EPOCH_P1` (physical My) used interchangeably for plate motion vs physics — incommensurable scales. (map)
- **[map/MapGenerator.cpp:1695]** `pid >= 0 && pid < 255` silently drops valid plate 255. Add `static_assert MAX_PLATE_CAP < 255`. (map)
- **[render/SpriteRenderer.cpp:55–86]** Constructor exception path relies on implicit `vkDestroyDescriptorPool` set-cleanup semantics. Document. (render)
- **[app/Application.cpp:1165–1167]** `glfwGetFramebufferSize` raw call with `int` shadows `Window::framebufferSize()` `uint32_t` abstraction. (render)

---

### SUGGESTION

Selected, not exhaustive:

- **GameRenderer.cpp:83–84** uses `goto skip_to_ui_layer`. Refactor into helper. (render)
- **app/Application.cpp** — plate-stats accumulation duplicated 3× (debug-cmd, `/info`, `/plates`). Extract free function. (render)
- **`HotkeyBinding::keyCode`** uninitialized; relies on aggregate zero-init. Add `= 0`. (render)
- **`ProductionScores`** uninitialized members; computed at every call site, but adds-a-field bug magnet. Add `= 0.0f`. (ai)
- **Magic building/tech/unit IDs** scattered as bare integer literals across 50+ scoring sites. Replace with named `constexpr`. (ai)
- **`ProductionQueueComponent::popCompleted`** uses `vector::erase(begin())`. `std::deque` for O(1) pop-front. (gameplay)
- **`addProgress`/`addProgressMultiSlot`** missing `[[nodiscard]]`. (gameplay)
- **`std::jthread`** instead of `std::thread` for DebugServer. (concurrency)
- **`DebugServer.hpp` doc** mentions `MutationQueue`/`StateSnapshot` types that don't exist. Misleading. (concurrency)
- **CLAUDE.md** documents `AOC_DEBUG_HOST` env override that is not implemented. Update doc or add code with loopback validation. (security)
- **`SimpleYaml`** uses `std::atoi` (silent-fail on overflow). Use `strtol` + errno. (security)
- **Constants**: `SUPERCONTINENT_FRACTION = 0.20`, `RIFT_RAMP_MY = 100` lack literature citation per CLAUDE.md rule 3. (map)
- **`PlateReference` deletion**: see Critical above. (map)
- **`Plate::cx/cy/rot`**: mark `// LEGACY` per project memory snapshot. (map)
- **`SphereFieldPhysics.cpp:1671`** stale comment about Voronoi — code no longer Voronoi-based. (map)

---

## Summary

| Severity | Count |
|---|---|
| Critical | 26 |
| Warning | ~55 |
| Suggestion | ~20 |

### CLAUDE.md rule adherence

| Rule | Status |
|---|---|
| 1. No Voronoi | **PARTIALLY VIOLATED** — `MapGenerator.cpp:1381` microplate spawn uses Euclidean nearest-centroid |
| 2. Algorithms over data | **VIOLATED** — `PlateReference.cpp` compiles Bird (2003) catalog (currently uncalled, but compiled) |
| 3. Cited constants | **MOSTLY COMPLIANT** — K_THICKEN, K_EROSION, mantleDatumM cited; `SUPERCONTINENT_FRACTION`, `RIFT_RAMP_MY` missing |
| 4. Mechanism-driven shapes | COMPLIANT |
| 5. 3 Gy / 50 My epoch | COMPLIANT |
| 6. No fudges | COMPLIANT (audit did not surface quota shapers) |

### Top-impact fix order

1. **Lua sandbox** (LuaEngine.cpp:59) — arbitrary code execution from any mod.
2. **Save deserialization** (Serializer.cpp ReadBuffer + reserve caps) — crash + DoS.
3. **DebugServer worker-thread mutation** (Application.cpp:648–711) — race + GLFW thread safety.
4. **Voronoi reintroduction** (MapGenerator.cpp:1381) — direct CLAUDE.md rule 1 violation.
5. **`Plate` uninitialized fields** (Plate.hpp:23–26) — UB feeds Rodrigues rotation.
6. **`unordered_map` non-determinism** (AIController.cpp:153, 1673) — breaks GA seed reproducibility.
7. **Combat raw-pointer dangling** (Combat.cpp + AIBuilderController.cpp + EspionageSystem.cpp) — crashes under removeUnit pattern.
8. **Save corruption** (Serializer.cpp:1341 — no atomic write, no log) + **DebugCommandFile rename** (silent failure of primary diagnostic instrument).

## Verdict

**BLOCK**

26 Critical findings span memory safety, security, project-rule violations, and concurrency. Several are exploitable (Lua sandbox absent, save reserve OOM), several are silent corruptors (Voronoi, uninitialized struct, AI determinism), and several are crashes-under-load (raw-pointer dangling in combat / builder / spy paths).

Recommend fixing in passes:
- **Pass 1**: security + UB (Lua, save reader, Plate fields, int16 overflow). Smallest diffs, highest blast radius.
- **Pass 2**: project rules (Voronoi, PlateReference). Required by CLAUDE.md.
- **Pass 3**: dangling raw pointers (combat, builder, spy, trade-route). Coordinated removeUnit pattern fix.
- **Pass 4**: concurrency (DebugServer, GameDBus, Log, MapGenCli).
- **Pass 5**: determinism (AI unordered_map → vector).
- **Pass 6**: warnings + perf hot paths.
- **Pass 7**: suggestions (cleanup).

Audit produces inventory only. No code edited. Triage and fix per pass above; or run targeted `/refactor` on individual findings.
