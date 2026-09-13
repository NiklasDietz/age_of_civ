# Emergent Commodity Money: a plan

**Status**: Phase 0 built and committed as `d4ac0a2` on 2026-09-13. Phase 1 was
built, measured and reverted. **Phase 2 is blocked by Phase 0's own result, see
section 5.0.** Phases 3 and 4 are unchanged and unbuilt.
**Written**: 2026-09-13, against `develop` at `c050023`.
**Investigation**: six implementation areas, each read from the code and then
independently checked for buildability, plus two design critics. All six areas
came back *not buildable as first drafted*, with sixty-plus corrections. This
plan is what survived that.

---

## 1. The goal

Money should **emerge from salability** rather than be granted by a building.
Menger's account: the good that becomes money is the one everybody can hold and
nobody can consume.

1. Gold and silver are ordinary resources. Nothing declares them money.
2. Early on they have no productive use, so civs converge on them as the medium
   of exchange. That convergence is the adoption of commodity money.
3. Later, technology gives the same metals real uses: jewelry as an amenity,
   and a premium path into electronics.
4. That creates an opportunity cost. Metal circulating as money is metal not
   available for industry.
5. Players adopt **fiat** to free their metal for production. Fiat finally has a
   motive, which today it entirely lacks.

---

## 2. What the investigation changed

### 2.1 The premise is already half-true, but not the way I first reported it

I previously said gold and silver ore each have exactly one consumer. That was
wrong, and the correction matters:

| Metal | Industrial consumers today |
|---|---|
| Silver ore | none. Its only use is mint recipe 35 |
| Gold ore | one, `Platinum Jewelry` (recipe 67), and it carries no tech gate |
| Copper ore | **four**: Draw Copper Wire, Smelt Bronze, Lithium batteries, Cobalt cells |

**Copper is therefore dropped from the money fiction.** Sweeping copper would
starve Bronze and the entire wire-to-electronics chain from turn 1, and every
mitigation for that is pure cost. Copper keeps behaving exactly as it does
today. The design is scoped to **silver and gold**, which is the only place the
"no productive use early" premise is actually true.

### 2.2 The opportunity cost already exists, in the recipe ranker

The production ranker picks recipes by profitability ratio, and the Mint is
*already* competing with industry for ore. Nothing needs inventing. Two things
are missing:

- silver and gold have no industrial consumer that can outrank the Mint, and
- the player has no way to stop the Mint eating the ore.

Both are far cheaper than the escrow machinery originally sketched.

### 2.3 Never stop the sweep. Stop the minting.

The originally proposed spine was "under fiat the sweep stops, and metal stays
in the stockpile". That ships a **duplication exploit on its first turn**:

```
Mint Copper : 2 ore   -> 30 coins
Melt Copper : 3 coins ->  2 ore     (ratio 8.0, the highest in the table)
```

Coins only stay out of that loop today because the sweep empties stockpiles
every turn. Stop the sweep and the loop prints metal and money together, and
because the sweep books face as `minted`, the H16 conservation gate would not
catch it.

**Stopping the mint recipes instead** is money-neutral by construction:
already-minted specie stays specie, no pool moves, nothing is created or
destroyed. Conservation, the ledger, `worldMoney` and H16 are all untouched.
This removes the single highest-risk element of the whole design.

### 2.4 The second half of the design is currently unobservable

Before building a motive, note what the runs measure today:

| Measurement | Value |
|---|---|
| Civs that ever reached Fiat | 2 of 38 |
| Electronics produced in a 150-turn run | zero |
| Electronics Plants ever completed | one |
| Textile Mills built by the AI | zero |
| Gold ore appearing in AI goods offers | 0 of 3783 |
| Seed 42, blessed run | 100% Barter over 500 turns |

A fiat motive is worthless if nothing can reach fiat, and a jewelry recipe is
worthless in a building the AI never builds. Hence Phase 0 and the Forge.

---

## 3. Decisions taken

- **Scope is silver and gold.** Copper is untouched.
- **Stop the minting, never the sweep.** No escrow, no new pool, no new ledger
  flow kind, no change to `worldMoney`.
- **No save bump.** `system` and `coinageStandard` already persist, ore already
  persists in stockpiles, and new goods and recipes are additive. The programme
  bumped to v34 two commits ago; a v35 would make every existing save
  unloadable to persist state this design does not need.
- **Host new recipes on the Forge**, `BuildingId{0}`. It is Industrial, the AI
  actually builds it, it is in the chain-enabler force list, and because it
  already hosts more than one recipe the per-building recipe-preference cycle
  button appears for free. The Textile Mill would make the whole first half as
  unobservable as the second.
- **Do not touch the face scale or `priceAnchorK`.** Both are live balance
  questions of their own. Changing them here would make the mechanic's
  before-and-after unattributable.
- **New health checks land as targets, not hard checks.** They are false on the
  current build, so as hard checks they would hold `test_sim_health` red from
  commit one to the end of the programme.

---

## 4. Decisions you need to make

1. **Does a metal stop being a luxury while it is money?** Menger's own argument
   is that the monetary good *keeps* its non-monetary value, which is why it is
   chosen. But gold ore is already `RawLuxury` today, so monetising it costs a
   measured **0.939 amenities per city** at a point where 63% of live
   player-turns are already pinned at the 2.0 shortfall cap. Options: gold is
   both (simplest, slight double-count), or a regime-aware predicate skips it
   while monetised (more honest, more code).
2. **Jewelry as `CONSUMER_GOODS` or a new good?** Consumer goods already carry
   an amenity, a goods tax and population demand, so it needs zero wiring and
   matches the existing Platinum Jewelry precedent. A dedicated good reads
   better but needs amenity and tax wiring, and a manufactured good carrying
   `RawLuxury` would leak into the worldgen luxury placement list.
3. **Is the mint-recipe stop enough of a "choice"?** Under this plan the ranker
   decides, and the player can override per building. If you want an explicit
   lever, that is a fourth phase.

---

## 5. The plan

Each step is one commit, records shadow 200-turn seed 42/43 hashes and the ctest
count in its message, and runs the full non-golden suite plus ASan. Goldens move
only where noted, and **only with explicit approval**.

### Phase 0: measure first, change nothing

The repo's own history records a change built on a confident causal story that
had to be reverted. This phase exists so that does not happen again.

- Add two CSV columns: `MetalOreHeld` (silver and gold ore in stockpiles) and
  `MintOreConsumed` (ore consumed by mint recipes).
- Instrument `canTransition` to record **which clause refused**, per civ per
  turn. Today every table gate returns the same error code, so nobody can tell
  whether fiat is blocked by partners, inflation, GDP rank or tech.
- Run one baseline and answer three questions: is there ever more than zero
  silver or gold ore sitting in a stockpile after upkeep; which clause actually
  blocks fiat; does any metal-consuming recipe ever fire.

**Gate**: if the refusal data shows fiat is blocked by something other than
motive, fix that first and revisit this plan. Giving fiat a motive does nothing
if the gate is shut.

### 5.0 What Phase 0 measured, and why Phase 2 is now blocked

Phase 0 shipped and answered its question. Over 200 turns at six players on
seed 42:

| Target | Asked | Passed | Refused, by clause |
|---|---|---|---|
| Commodity money | 136 | 10 | no Mint 378, strength 126, no bullion 79 |
| Gold standard | 5 | 5 | never refused |
| Fiat | 34 | 0 | no Printing or Economics 153, strength 34 |
| Digital | 55 | 0 | tech 55 |

**Fiat is refused for tech and currency strength alone.** Partners, inflation,
GDP rank, city count and turns-in-system never refuse it once, and no civ has
ever passed that gate. The two civs observed on fiat were forced there by the
crisis suspension, which bypasses the gate entirely.

This trips the gate written into this plan: the motive is not what is missing,
so **Phase 2 must not be built yet**. Three things block the ladder, in order:

1. **No Mint gets built.** 378 of 583 Barter refusals, against 126 for the
   hundred-face threshold. Two thirds of Barter time is spent without a Mint.
2. **Printing and Economics are never researched.** 153 of 187 fiat refusals.
   This is a tech-priority problem, not a monetary one.
3. **Currency strength is blind to copper.** Every observed civ adopted the
   copper standard, and the gold-standard branch of `currencyStrength` counts
   only silver and gold, so a copper civ sits near zero against a required 75
   for ever. Read from the code, not instrumented; worth confirming before it
   is treated as fact.

A fourth finding changes the design's premise in its favour. The metal ore
column first read identically zero on both four-player golden runs, which was a
false negative caused by reading only the stockpile and not the export buffer.
Corrected, metal is held on 166 of 744 rows on seed 42 and 216 of 744 on seed
43, peaking at 12 and 20 units, while the silver and gold mints consume 0 and
14 units respectively. **The metal exists and is idle.** It simply never
reaches a Mint, which is exactly the unclaimed resource an industrial use would
take.

### Phase 1: give the metals somewhere to go (data only)

**BUILT, MEASURED AND REVERTED on 2026-09-13.** The two recipes below were
implemented exactly as specified, with the ranker ratios confirmed against real
base prices: Work Gold 3.60 against Smelt Gold Bars 2.00, Work Silver 2.05
against Mint Silver 2.27. They then measured **zero firings** in every run
tested, at 300 turns with six players and 500 turns with four, on both seeds,
while still moving seed 43 from turn 350 to turn 190 by perturbing the ranker's
topological tie-break. Section 8's first risk forbids landing a condition with a
measured firing count of zero, so they were reverted.

They never fired because the ore is in the export buffer rather than the
stockpile, and because a Forge recipe competes for worker slots it rarely wins.
Restoring them is nine lines once the ore actually reaches a recipe.

Two recipes appended at the end of `buildRecipes` so push order does not perturb
topological tie-breaks. Use recipe 35's positional form; putting the tech
straight after the building **will not compile**.

```
id 48  Work Gold    1 GOLD_ORE   -> 2 CONSUMER_GOODS  Forge  tech Metallurgy (8)
id 49  Work Silver  1 SILVER_ORE -> 1 CONSUMER_GOODS  Forge  tech Currency  (5)
```

Ranker arithmetic, which is the whole mechanism:

| Recipe | Ratio | Against the Mint |
|---|---|---|
| Work Gold | 3.60 | Smelt Gold Bars 2.00, so gold leaves the Mint when Metallurgy lands |
| Work Silver | 2.05 | Mint Silver 2.27, so silver is a genuine contest the market can tip |

Reversible, no save impact. Moves both goldens.

### Phase 2: the fiat motive (one branch)

In `executeProduction`, beside the existing tech gate where the player is
already in scope: **a mint recipe does not run while the civ is on FiatMoney or
Digital**. Apply the same predicate to the two melt recipes as cheap insurance
against the fountain.

Add a `SkipReason::Regime` enumerator **and** its `kSkipNames[]` string in the
same edit, or the diagnostic dump reads past the array.

Money-neutral: no pool moves. Conservation untouched.

### Phase 3: make it visible and let the AI play it

- One line on the Economy screen's regime row: under metal, `Mint: N ore/turn`;
  under paper, `Mint idle, metal stays in your cities`.
- Rewrite the AI's Gold-to-Fiat branch as an opportunity-cost test rather than
  an inflation test: industrial metal demand against metal held as goods.
- **A human-facing signal in the same commit as the AI rule.** Every AI would
  otherwise demonetise the turn its conditions are met while a human who never
  found the screen keeps minting for the rest of the game.

### Phase 4: deferred, only if Phase 0 says it is reachable

Gold into electronics as an additive premium variant rather than by rewriting
recipe 10. Do **not** revive `GOLD_CONTACTS` (id 82): it was deliberately cut
and the loader actively remaps it to `SEMICONDUCTORS` on every load.

---

## 6. Two pre-existing bugs, to fix separately

Neither belongs to this design, and both are worth fixing whether or not it
ships. They must not ride along, or they will be judged on the design's merits.

1. **The unclamped cross-civ take.** `takeFromPrivate`'s four-argument overload
   measures capacity as coin plus notes but deducts the whole amount from coin,
   driving the pool negative. This is the mechanism behind the 62 negative-money
   rows measured in the eight-run audit. Fix: drain notes first, gated on
   `notesInUse`.
2. **The money-supply floor never applies on the normal path.** Two direct
   assignments bypass `adjustMoneySupply`.

---

## 7. Explicitly dropped, with reasons

- **The escrow sweep**, a per-metal escrow stock, `FACE_PER_ORE`, the export
  buffer drain and the trader-cargo policy. The Mint already removes ore from
  stockpiles. Escrow also reverses a decision recorded in
  `MONETARY_REDESIGN.md` without arguing against its reasons.
- **Deleting the mint recipes.** Minting is a technology for making metal
  fungible, not a legal-tender declaration. Keeping it costs the fiction almost
  nothing.
- **Redefining `currencyStrength`, floating `coinageStandard` with hysteresis,
  the Mint face multiplier, and the debasement rewrite.** No player-facing
  payoff; six call sites and an oscillation risk.
- **Converting the Gold Standard's note issue 1:1.** Backing is measured as
  specie plus treasury over notes plus treasury, so a conversion would force
  every gold-standard civ into suspension on its sixth turn.
- **Extending the price anchor to fiat.** `priceAnchored` is a bool that also
  decides who writes `inflationRate`; this is a three-mode restructure touching
  twelve readers and belongs to its own package.
- **A `RegimeAdopted` event type.** No event log is reachable from the regime
  request. Derive it by diffing, as `TechResearched` already is.
- **Automatic Barter to CommodityMoney inside the sweep.** It would make
  `EconomySimulation` a third `transitionTo` writer and break the guard test
  that exists precisely to prevent that.

---

## 8. Risks

- **The condition that never fires.** No new gate or predicate lands without a
  measured firing count. This repo has already reverted one wide fallback that
  measured zero firings across four seeds.
- **The balance change that migrates.** A previous fix multiplied Mint output
  tenfold and raised the coinage gate; the constraint moved rather than
  resolved. This plan deliberately touches neither.
- **Amenity loss.** Monetising gold costs amenities before it gives any back.
  Decision 1 in section 4 settles this.
- **Both goldens move** from Phase 1 onward. Re-bless with the portable preset,
  and delete the stale precompiled header first or the old binary silently
  reproduces the old hashes.

---

## 9. Verification

Per step: release and ASan builds, the full non-golden suite, the step's own new
test, and shadow 200-turn hashes for seeds 42 and 43 in the commit message.

New targets for `sim_health`, each with a selftest perturbation that can trip it:

- first turn any civ leaves Barter, on a majority of seeds;
- among civs adopting fiat, the share showing metal flowing to industry within
  20 turns, which is the causal claim the whole design rests on;
- metal held as goods after the last demonetisation, to catch the deflation
  case.

Judge on the six-player health seeds as well as the two golden seeds, since
seed 42 is 100% Barter today and cannot exercise any of this until the mechanic
lands.
