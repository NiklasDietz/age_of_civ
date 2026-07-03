# Audit Report — Age of Civ

Repo: `/home/ndietz/Repositories/private/age_of_civ`
Audited: 2026-06-06
Reviewers spawned: 15 (parallel, 2 waves)
Standards: claude-code-tweaks (general + cpp) + CLAUDE.md (C++20, physics/debug rules)

> Surfaces only. No code was edited. Prior audit: `AUDIT_REPORT_2026-05-10.md`.

---

## Orientation

- **Stack**: C++20, CMake 3.20+/Ninja, ccache, Vulkan + GLFW (headless via `AOC_HEADLESS`), sd-bus IPC, cpp-httplib, Lua(JIT), OpenMP.
- **Size**: 234 cpp + 271 hpp, ~118k LOC. `simulation/` dominates (38k LOC, 23 sub-areas).
- **Targets**: `age_of_civ`, `aoc_simulate`, `aoc_trace_dump`, `aoc_mapgen` + 4 CTest unit tests.
- **Entry points**: `src/main.cpp`, `src/tools/{HeadlessSimulation,MapGenCli,TraceDump}.cpp`.
- **CI**: none in repo.
- **Build note**: Release builds compile `-DNDEBUG` (asserts are no-ops → unchecked `HexGrid` setters in shipping builds). High-Performance preset adds `-fno-exceptions` → any uncaught throw in load path = `std::terminate`.
- **Subsystems audited**: economy/monetary/resource/production, ai, diplomacy/victory/citystate/religion, unit/combat/city/turn, map physics, net+scripting+debug (security), save+data (security), render+app, ui.

---

## Findings

### CRITICAL — must fix

#### Memory safety / UB

- **[render+app] HTTP debug-server worker threads race on `m_hexGrid`** — `Application.cpp:488-659` read `m_hexGrid` on httplib threads while main thread `std::swap`s it in `consumeRegenResult()` with no lock (comment admits "race-tolerated"). UB; OOB read during regen. *Fix: guard with `std::shared_mutex` (shared_lock readers, unique_lock swap), or snapshot under lock.* (render-cpp + net-security both flagged)
- **[render+app] `m_selectedUnit` / `m_selectedCity` dangling raw pointers** — `Application.hpp:191-194` are raw observers into `unique_ptr` vectors; `removeUnit(&unit)` at `Application.cpp:5011/5030/5064/5099` destroys the pointee while selection still points at it. Use-after-free in normal play. *Fix: store `optional<UnitId>`; resolve live pointer on use; null selection inside removeUnit/capture.*
- **[ui] `cityPtr` use-after-free in production-screen lambdas** — `GameScreens.cpp:494` captures `c.get()` into "Add to Queue" (`:596`) / "Build Now" (`:621`) onClick lambdas; city razed/captured before click → UAF. *Fix: capture `m_cityLocation`+`m_gameState`, re-resolve via `resolveCityByLocation` in lambda.*
- **[ui] `qPtr` use-after-free in drag-drop** — `GameScreens.cpp:461` stores `&city->production().queue` in `onDrop`; `std::swap((*qPtr)[..])` after city destroyed = UAF. *Fix: re-resolve city in the handler.*
- **[ui] Iterator invalidation in `UIManager::removeWidget`** — `UIManager.cpp:407` range-for over `w.children` while recursive call `erase`s the same vector. UB. *Fix: copy children to local vector before recursing (pattern already used in `GameScreens.cpp:2388`).*
- **[unit] `resolveRangedCombat` calls `removeUnit` inline** — `Combat.cpp:646-650` destroys defender's `unique_ptr` while caller holds the `Unit&`; WP8 deferred-kill fix applied to melee was never applied here. UAF risk + ranged path skips all kill effects. *Fix: apply the `PendingKill` deferred-removal pattern from `resolveMeleeCombat`.*
- **[unit] `atWar[MAX_PLAYERS]` stack-buffer OOB** — `TurnProcessor.cpp:1201,1216-1219,1321` index a fixed `bool[20]` by `j` over `allPlayers.size()`; `HeadlessSimulation` accepts `playerCount` with no ≤20 clamp. OOB write / stack corruption one config change away. *Fix: `assert(allPlayers.size() <= MAX_PLAYERS)` + static_assert, or `std::vector<bool>` sized at runtime.*
- **[map] OOB `plates[]` read in OpenMP `accumulateClosingRate`** — `SphereFieldPhysics.cpp:1423-1424` index `plates[]` by stale `plateId` raster value with no bounds guard (other passes guard, e.g. `:338`). OOB read under parallel-for. *Fix: `if ((size_t)selfId >= plates.size()) continue;` for both ids.*
- **[map] Data race on `field.crustThicknessKm` in `accreteToCardinalNeighbours`** — `SphereFieldPhysics.cpp:1683-1688` snapshots `continentalFraction` (`nextFrac`) but reads+writes the live `crustThicknessKm[n]` for shared neighbours; physically inconsistent even single-threaded, race if parallelised. *Fix: add `nextCrust` snapshot, swap at end like `nextFrac`.*
- **[diplomacy] `Log.hpp` off-by-one stack overflow** — `Log.hpp:113`: when `total == kLogBufSize-1` (1023), writes `'\n'` at `buf[1023]`, `total`→1024, then `'\0'` at `buf[1024]` — one past the 1024 array. Affects **every** `LOG_*` call. (clang-analyzer-security.ArrayBound confirmed) *Fix: `if (total + 1 >= kLogBufSize)` / reserve 2 bytes.*

#### Security (net / save / scripting)

- **[net] Arbitrary file write via `?path=` on `/dump/*`** — `Application.cpp:545-559` (`/dump/plates`) and `:614-627` (`/dump/grid`) pass attacker `path` to `std::ofstream` with no canonicalisation/allowlist. Localhost-bound but CSRF/DNS-rebind reachable. *Fix: reuse `GameDBus::isPathInsideAllowlist` + `weakly_canonical` against a fixed dump dir.*
- **[net] Unauthenticated always-on debug control server** — `Application.cpp:455-777` / `DebugServer.cpp:114-130` start unconditionally (no `#ifndef NDEBUG`, no flag); state-changing POSTs (`/quit`, `/sim/re-roll`, `/dump/*`) have no token and no Origin/CSRF check. *Fix: gate behind `--enable-debug-server`, require per-session bearer token, reject non-loopback Origin/Host.*
- **[save] Unvalidated map dimensions → signed-overflow + OOB writes + OOM** — `Serializer.cpp:1588-1600`: `width`/`height` read from save with no range check; `HexGrid::initialize` allocs `size_t(w)*size_t(h)` (crafted → 4 GB OOM / negative → huge), fill loop uses `int32_t count = w*h` (signed overflow UB) to drive `setTerrain/setResource/setOwner` whose only guard is an assert compiled out under `-DNDEBUG` → OOB heap write in release. *Fix: reject `w/h <= 0 || > 4096` → `SaveCorrupted`; compute count as `size_t`, clamp to `tileCount()`.*
- **[save] Unbounded `reserve()` on attacker-controlled length prefixes** — `Serializer.cpp:2210,2220,1898,2271,2385,2398,2438,2443` pass disk counts (modCount, buildingCount, wonderCount, issuedCount, heldCount, posCount…) straight to `vector::reserve()` (unlike capped MAX_UNITS/MAX_CITIES); `0xFFFFFFFF` → multi-GB alloc → `bad_alloc` → `terminate` under `-fno-exceptions`. *Fix: cap every disk-read count before reserve, return `SaveCorrupted` on exceed.*

#### Correctness — silent / dead logic

- **[turn/tech] `applyCivicEffect` is dead code — all one-time civic effects silently dropped** — `CivicTree.cpp` defines it; `TurnProcessor.cpp:738` + `TradeRouteSystem.cpp:1327` discard `advanceCivicResearch`'s `true` return and never call it. Zero call-sites repo-wide. GrantEnvoy/FreeTech/ExtraTradeRoute/LoyaltyBoost/CultureBurst/FaithBurst never apply. *Fix: on completion call `applyCivicEffect(gameState, player, lastCompletedCivic)`.*
- **[economy] `processPayments` debits the wrong treasury** — `AdvancedEconomics.cpp:437` passes `economy().treasury` (a tracking field seeded at 100, never the spending account) instead of `monetary().treasury`; every banking loan is effectively interest-free. *Fix: pass `monetary().treasury`.*
- **[economy] Bond default leaves holder's portfolio corrupt** — `Bonds.cpp:218-226`: on issuer default the bond is erased from issuer's `issuedBonds` but never from holder's `heldBonds`, accruing phantom interest forever → fictitious creditworthiness. *Fix: also erase the matching bond from holder side.*
- **[economy] Coin-settlement creates money from nothing** — `EconomySimulation.cpp:1360-1379`: three `consumeGoods` returns discarded via `[[maybe_unused]]`; if stockpile already drained the receiver still gets coins while payer loses nothing. *Fix: check each return; transfer only what was actually consumed.*
- **[unit] Production queue permanently jams at 100%** — `ProductionSystem.cpp:326-328`: when `!resourcesAvailable` it sets `progress = totalCost` and `continue`s; item never pops, city produces nothing forever, no log. *Fix: pop completed item or hold progress + `LOG_WARN` missing good.*
- **[ai] Nuclear strike retry loop** — `AIController.cpp:526-537` sets `launcher->nuclear().equipped = true` *before* `launchNuclearStrike`, discards the `ErrorCode`; on failure the armed flag stays set → the strike re-fires every turn, bypassing the `0.015f` probability gate. *Fix: log result; clear `equipped = false` on non-Ok.*
- **[ai] `POSTURE_NAMES` unguarded index** — `AIController.cpp:315` indexes a 6-element array by `static_cast<size_t>(bb.posture)` with no bounds/exhaustiveness guard. *Fix: add `Count` enumerator + static_assert, or switch.*
- **[culture] Eliminated player blocks/steals cultural victory** — `Tourism.cpp:161,168` candidate + blocker loops have no `isEliminated` guard; a dead civ with accumulated tourism can win or permanently veto culture victory for the living. *Fix: `if (p->victoryTracker().isEliminated) continue;` in both loops.*
- **[barbarian] Barbarian melee is a permanent no-op** — `BarbarianController.cpp:291-294` calls `resolveMeleeCombat(..., NULL_ENTITY, NULL_ENTITY)`; the EntityId overload returns empty `CombatResult{}` — barbarians can never damage units in melee. *Fix: call the `Unit&` overload with the live `*unit`/`*target`.*
- **[diplomacy] `StealTradeSecrets` spy mission is a documented no-op** — `EspionageSystem.cpp:249` logs "gain 25% progress" but writes no state; mission burns the timer, changes nothing. *Fix: implement IR-progress grant or `LOG_WARN` stub.*

---

### WARNING — should fix

#### Error handling / silent failure (cross-cutting theme)

- **Pervasive `[[maybe_unused]] bool ok = stockpile.consumeGoods(...)`** discards failure across **20+ sites** (economy, production, fuel pooling `EconomySimulation.cpp:384-426`, population food `:547-566`, ProductionSystem `:336,:343`). Each masks underflow/race; failures invisible (no log). *Fix: assert in debug, or branch + `LOG_WARN` where consumption is load-bearing.*
- **[net] `ReplayRecorder` save/load both unchecked** — `ReplayRecorder.cpp:69-86` writes unchecked then always logs success (disk-full → silent truncation); `:99-126` reads unchecked + unbounded `playerCount` resize → garbage/OOM on truncation, then logs "loaded". *Fix: check `file.good()` after each I/O; cap `playerCount`; return false on partial.*
- **[net] `sd_bus_process` error swallowed** — `GameDBus.cpp:252-253` `if (r <= 0) break;` treats bus error (r<0) same as "no messages" (r==0); dead bus never flagged. *Fix: split; `LOG_ERROR` + deactivate on r<0.*
- **[net] `TransitionMonetaryCommand` arm is an empty stub** — `GameServer.cpp:458-460` accepts/dispatches then silently drops; no state change, no error to client. *Fix: implement or `LOG_WARN` + protocol error.*
- **[turn] Tech-completion side-effects not fired** — `TurnProcessor.cpp:737`/`TradeRouteSystem.cpp:1304` discard `advanceResearch` `true`; eureka for just-completed tech consumed before completion → never applied. *Fix: capture return, fire completion handlers.*
- **[event] World-event gold wiped by treasury sync** — `WorldEvents.cpp:212-221` writes `monetary().treasury` but `TurnProcessor.cpp:454` overwrites it from the authoritative account next turn; event gold has no effect. *Fix: use canonical `addGold/setTreasury` mutators or apply after sync.*
- **[turn] `foundCity` places city at invalid location on relocation failure** — `TurnProcessor.cpp:211-223` sets `expansionExhausted` then still `addCity(location,...)` at the too-close tile (violates 3-tile rule). *Fix: early-return on relocation failure.*
- **[ai] Discarded action results** — `issueBond` (`AIEconomicStrategy.cpp:99`), `bullyCityState`/`levyCityStateMilitary` (`AIDiplomacyController.cpp:969,975`), `imposeSanction` cascade (`AIEconomicStrategy.cpp:126-131`), `considerPurchases` (`AIController.cpp:2000,2027,2055`), spy mission (`:456`) — all `(void)`/discard with no log; failures retried each tick. *Fix: check + log; gate dependent actions on success.*
- **[ai] Stale `snap.position` re-lookup after movement** — `AIMilitaryController.cpp:791` (aggressive pass) and `:373` re-`unitAt(snap.position)` for units that already moved this turn → `nullptr` → silently skipped. *Fix: reindex snapshots by unit ID or update recorded position.*
- **[map] Degenerate-map failures unlogged** — craton double-failure defaults seed to (0,0) `MapGenerator.cpp:928-931`; no land-fraction validation `Thresholds.cpp:46-63`; land/ocean plate shortfall after 800 attempts `MapGenerator.cpp:545-603`; orphan→plate-0 fallback `SphereFieldPhysics.cpp:1319-1331`. **Likely root cause of the "seeds 100/200 → 3-5% land" calibration failure.** *Fix: unconditional `LOG_WARN` on each degenerate path + emergent land-fraction at INFO.*

#### Memory / lifetime / UB

- **[render+app] `m_creatorTimeCurrentMy` data race** — plain `int32_t` read on httplib thread (`Application.cpp:711`), written on main (`:1660`). *Fix: `std::atomic<int32_t>` relaxed.*
- **[render] Vulkan handle leak on partial construction** — `SpriteRenderer.cpp:47-49`: if `createBuffers()`/`createPipeline()` throw, already-created layout+pool leak (dtor not called). *Fix: RAII (`vk::Unique*`) or two-phase init returning bool.*
- **[render] `vkDeviceWaitIdle` every dirty frame** — `GlobeRenderer.cpp:297-348` (and `SpriteRenderer::cleanup`) full GPU stall on map regen. *Fix: per-frame deletion queue, destroy `framesInFlight` later.*
- **[render] `getWidget()` result dereferenced without null check** — `Application.cpp:4237-4239,4252`. *Fix: null-guard.*
- **[diplomacy] `const`-correctness lies** — `Religion.cpp:267` `const_cast`s a `const DiplomacyManager*` to call mutating `addModifier` (call site already passes non-const); `DiplomacyState.cpp:56-64` const `relation()` returns a mutable `thread_local`. *Fix: drop the const on the param; document/assert the sentinel.*
- **[map] BFS frontier not deduplicated** — `SphereFieldPhysics.cpp:126-203` pushes cells multiple times → frontier grows to O(cells). *Fix: `vector<bool> inFrontier` guard.*
- **[map] Polar `cellWidthM ≈ 24 m` amplifies erosion ~2600×** — `SphereFieldPhysics.cpp:1905,1935`: `cos(lat)` near poles drives unphysical polar crust stripping. *Fix: cap slope or zero `dzLon` when `cellWidthM` tiny.*
- **[map] `getenv` per epoch in physics loop** — `SphereFieldPhysics.cpp:2064,:1333`. *Fix: cache once before loop.*

#### Correctness

- **[victory] CSI averages polluted by eliminated players** — `VictoryCondition.cpp:197` `stats[route.sourcePlayer]` `operator[]` inserts zero-filled entry for filtered-out players. *Fix: guarded `find()`.*
- **[diplomacy] `WorldCongress` votes array sized 16 < MAX_PLAYERS(20)** — `WorldCongress.cpp:414` `std::array<int16_t,16>` silently drops votes for players 16-19. *Fix: size to MAX_PLAYERS or `LOG_WARN` on OOR.*
- **[diplomacy] `StealTechnology` reads owner's research, not target's** — `EspionageSystem.cpp:124` credits bonus from `ownerPlayer.tech()` regardless of victim. *Fix: use target's current research.*
- **[economy] `StockMarket` MirrorKey shift + id-mint mismatch** — `StockMarket.cpp:177` `(playerId << 32)` cast applied to result not operand (UB if PlayerId>32-bit); `:196` investor/mirror zero-ids minted separately → lookup miss skips mirror updates. *Fix: `(uint64_t(id) << 32)`; single pre-pass assigning paired ids.*
- **[economy] `findTraderByEntityId` ignores EntityId, walks by offset** — `TradeRouteSystem.cpp:330` returns wrong unit if any player has 0 units or after removals. *Fix: `unordered_map<EntityId,Unit*>` lookup.*
- **[victory] Float `==` tiebreaker** — `VictoryCondition.cpp:884` `csi == bestCSI`. *Fix: drop branch / epsilon.*
- **[empire] `processCommunication` break-vs-continue null mismatch** — `CommunicationSpeed.cpp:229-244` second loop `break`s on null city (first loop `continue`s) → cuts off all later cities. *Fix: `continue`.*

#### Performance (hot path — per-turn / per-frame)

- **[ai] Repeated full-grid + duplicate scans per civ per turn** — O(builders×tiles) trading-post/encampment scans `AIBuilderController.cpp:319-369,452-456`; duplicate enemy-proximity in `analyzeEnemyComposition` + `computeThreatRatio` (`AIController.cpp:88-118`, `AIMilitaryController.cpp:62-118`); per-turn full-tile `tradeProximity` vector `AIController.cpp:2088`; ~750 allocs/settler in `scoreCityLocation` `AISettlerController.cpp:115`; O(traders×players×cities) `AITradeRoutesController.cpp:63-133`. *Fix: hoist/cache out of per-unit loops; reuse scratch buffers; write proximity to blackboard once.*
- **[economy] Per-tick allocations + repeated O(cities×recipes) scans** — `computePlayerNeeds`/`reportToMarket`/`executeProduction` rebuild `unordered_map` per player + rescan all recipes (`EconomySimulation.cpp:440,654,1094`); per-trader `TollEntry`/`combined`/`candidates` vectors (`TradeRouteSystem.cpp:80,924`); double city/unit passes in `Maintenance.cpp:372,482,724`. *Fix: per-city building bitset, reuse scratch with clear()+reserve.*
- **[turn] Aqueduct BFS allocates `tileCount`-byte visited per city per turn** — `TurnProcessor.cpp:999` (~1.26 MB/turn → 2.5 GB over 2000 turns). *Fix: one reusable buffer, `std::fill` per city.*
- **[economy] Pointer address used as quality/meltdown hash seed** — `EconomySimulation.cpp:627` `reinterpret_cast<uintptr_t>(cityPtr.get())` breaks determinism/reproducibility. *Fix: stable per-city integer id.*
- **[ui] Per-frame string allocs + software glyph rasteriser** — `UIManager.cpp:632,667` (~6000 allocs/s), `BitmapFont.cpp:180` ~19k draw calls/frame for text. *Fix: prev-value cache for bindings; batch glyphs to textured quads (separate task).*

---

### SUGGESTION — nice to have (condensed)

- **Magic numeric IDs everywhere** — building/unit/wonder IDs (`6`/`20`/`24`/`13`/`0`/`17`…) hardcoded across AIController, Maintenance, StockMarket, ProductionSystem. *Fix: named `constexpr BuildingId`/`WonderId` constants in one header.*
- **Style violations** — `auto`/`using` in `.cpp`s (`WorldCongress.cpp:36,329`, `CityState.cpp:537`, `MapGenerator.cpp:76-77`, `PauseMenu.cpp:26`, `Tooltip.cpp:348`); duplicate `#include` (`VictoryCondition.cpp:31`, `ProductionSystem.cpp:38,41`); `AIBuilderController::manageBuildersAndImprovements` ~570 LOC > 100 cap.
- **Stubs that silently succeed** — `EspionageSystem.cpp:325-330` (NeutralizeGovernor/RecruitDoubleAgent/EstablishEmbassy), `LuaEngine.cpp:226-231` (`registerFunction`). *Fix: implement or `LOG_WARN`.*
- **[scripting] LuaJIT bytecode-loader gap** — `LuaEngine.cpp:92-98` nils os/io/package but not `load`/`loadstring`/`collectgarbage`; LuaJIT runs unverified bytecode from mods. *Fix: block them, reject non-`0x1B` bytecode.*
- **[data] JSON accessors throw on missing/mistyped keys** — `DataLoader.cpp` `asString()/asInt()` unguarded for scalars, no try/catch around `load*` → terminate on bad mod JSON. *Fix: typed `hasKey()+isString()` helpers or wrap in try/catch.*
- **[ui/save] Unchecked `std::stoi`/`stoul`/`stof`** — `SettingsMenu.cpp:430-434`, `UIPersistence.cpp:35`. *Fix: `from_chars`/strtol or try/catch + default.* (flagged Critical by save agent due to startup crash on corrupt cfg)
- **[ui] UI mutates sim state directly** — `GameScreens.cpp:1169,1283,1576` write `tech().currentResearch`/`government` bypassing command path. *Fix: route through GameCommand (architectural; blocks future authoritative MP).*
- **[net] DebugServer hardening** — no payload-size/timeout limits (`DebugServer.cpp:114-130`), handler exceptions logged only into response not server log (`:88-94`), listener jthread has no catch-all (`:123-128` → terminate on throw). *Fix: set limits; `LOG_WARN` in catch + `catch(...)`; wrap jthread body.*

---

## Verdict

**BLOCK** — 25 Critical, ~30 Warning, ~12 Suggestion across 9 subsystems.

Top priorities (by blast radius):
1. **`Log.hpp:113` off-by-one** — corrupts the stack on any 1023-byte log line; touches the entire codebase. Smallest fix, widest reach.
2. **Save-loader OOB/OOM** (`Serializer.cpp` dims + unbounded reserve) — untrusted input crossing machines, live in release builds.
3. **`applyCivicEffect` dead code** — every civic bonus silently missing; invalidates all prior balance tuning that relied on civics.
4. **Use-after-free cluster** — `m_selectedUnit/City`, `cityPtr`/`qPtr`, `resolveRangedCombat`, `removeWidget` — crashes in normal play.
5. **HTTP debug server** — arbitrary file write + unauthenticated state-changing POSTs + data race on `m_hexGrid`.

Recommend triaging in passes: Criticals 1-5 first (highest reach/lowest cost), then memory-safety Warnings, then hot-path perf, then style/suggestions. Several Criticals are one-to-three-line fixes (`Log.hpp`, civic call, atWar clamp, barbarian overload, nuclear flag).
