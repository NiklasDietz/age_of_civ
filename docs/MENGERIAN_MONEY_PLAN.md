# Mengerian Money: money as an adopted good

**Status:** proposed 2026-09-15, not approved.
**Supersedes:** the monetary half of `COMMODITY_MONEY_PLAN.md`. That document's
findings stay valid and are cited here; its Phase 1 and Phase 2 are withdrawn.

---

## 1. The decision this encodes

No good is money by declaration. A good becomes money for a civ when that civ
adopts it, and it is worth adopting because other civs accept it. Metal is
metal: ore in a stockpile, tradeable, no different from stone until somebody
treats it as money.

This is Carl Menger's account of the origin of money (*On the Origin of Money*,
1892): the most **saleable** good wins, not the most valuable one, and the
process is self-reinforcing because acceptance breeds acceptance. The opposing
position, that money is a creature of the state, is what the code implements
today: build a Mint, strike coin, the coin is money because the table says
`GoodCategory::Monetary`.

The user's instruction: **do not force the money stamp onto gold and silver.**
The metals are resources. Whether they become money is a decision, and it is a
decision that only pays off if others make it too.

---

## 2. What this deletes

The change is net negative in lines. That is the main argument for it.

| Deleted | Where | Why it goes |
|---|---|---|
| `GoodCategory::Monetary` and the three coin goods | `ResourceTypes.cpp:112-114` | no good is money by category |
| `isCoinGood` and its ~12 call sites | `ResourceTypes.cpp:783`, `TradeRouteSystem.cpp:238,242,585`, `EconomySimulation.cpp:1199`, `AITradeRoutesController.cpp:98`, `CommodityExchange.cpp:71`, `Speculation.cpp:26`, `DealProposals.cpp:577,588,663` | nothing to exempt from cargo |
| Mint recipes 34, 35, 36 and melt recipes 46, 47 | recipe table | no conversion step exists |
| **The mint/melt duplication exploit** | ratio 8.0, `COMMODITY_MONEY_PLAN.md` 2.3 | no coins to melt |
| The Mint building as a monetary gate | `District.hpp:357` | adoption needs no building |
| `CoinTier`, 84 references across 16 files | throughout | replaced by a good id |
| `preferredCoinTier` | `MonetaryActions.cpp:113` | replaced by the saleability score |
| Seigniorage, `GOLD_BARS_FOR_A_GOLD_STANDARD` | the sweep, `MonetarySystem.hpp` | no minting to tax |

The Mint building stays in the game as an ordinary gold building if we want it;
it simply stops being the thing that makes money exist.

---

## 3. The representation

### 3.1 One new field replaces `CoinTier`

```cpp
uint16_t moneyGood = INVALID_GOOD;   // the good this civ treats as money
```

`INVALID_GOOD` means barter (nothing is money) or fiat (notes carry it; which
of the two is `system` as today). No new save version: this replaces
`coinageStandard`, which is already persisted, and is the same width.

### 3.2 Money stock stays a scalar; only its origin changes

`treasury`, `privateSpecie` and `privateNotes` stay exactly as they are, and so
does the `MoneyFlow` seam on `Player::addGold/spendGold/setTreasury`. That is
deliberate: roughly ninety call sites move money through that seam, and
rewriting them to shuffle commodities would be a far larger change than this
one, for no behavioural gain.

What changes is how `privateSpecie` comes into existence. Today it is created
by minting. Under this design it is created by **monetisation**: a civ that has
adopted good G may move G out of its stockpiles and into circulation as money
at the current market price. Demonetisation runs the other way and hands the
metal back to the goods economy. Neither needs a building, a recipe, or a tech.

The metal is genuinely removed from the stockpile when monetised, so it is not
double counted as both cargo and cash. That is the honest reading of coin in
hand: it is no longer ore you can smelt.

### 3.3 Conservation

`worldMoney` (`MoneyFlow.cpp:283`) is unchanged: treasury plus private specie
plus notes plus bullion plus trader purses. Two flow kinds join the seven in
`MoneyFlowKind` (`MoneyFlow.hpp:23`):

```cpp
Monetised,    ///< a good left the stockpile and became money at market price
Demonetised,  ///< money became a good again and returned to the stockpile
```

`moneyConserved` keeps its exact form. Monetisation is the only creator of
specie, replacing `Minted`, and it is booked the same way, so H16 continues to
hold turn by turn with no loosening. This matters: the reason
`COMMODITY_MONEY_PLAN.md` kept the sweep was that stopping it opened a
duplication loop the conservation gate could not see. Deleting the coin goods
closes that loop at the source instead of fencing it.

---

## 4. The saleability rule

This is the piece that does not exist today in any form, and it is the whole
mechanism. `preferredCoinTier` currently reads only the civ's own reserves,
which is why every civ on every seed measured lands on copper: copper is simply
what gets mined most (156 units against 11 gold and 0 silver over 40 turns on
seed 42). It is an accounting result, not a choice.

Score each candidate good G for civ C:

```
saleability(G, C) = held(G, C)            // can't monetise what you don't have
                  * acceptance(G, C)      // who else takes it  <- the network term
                  * (1 - industrial(G))   // what industry eats, it cannot store
                  * stability(G)          // price variance, inverted
```

- **held** is a hard gate, then a mild preference: a civ needs a working stock.
- **acceptance** is `Σ over met civs i, weighted by trade volume with i, of
  [moneyGood(i) == G]`. Early it is zero for everything, so the first adopters
  choose on the other three terms, which is how silver won historically. After
  that it compounds, and civs converge. **This term is what makes money a
  coordination problem between players rather than a building.**
- **industrial** is the fraction of G consumed by available recipes. Early,
  gold and silver score near 1 because nothing consumes them. When industrial
  uses arrive, the term collapses, the metal stops being good money, **and that
  is what pushes a civ to fiat to free it** — the arc the user asked for, as a
  consequence rather than a scripted event.
- **stability** is inverse variance of market price over a window.

A civ adopts the argmax when it beats its current money by a margin, with a
dwell time so it does not oscillate. The human sees the same table and chooses;
the AI takes the score. Nothing forces anyone: staying in barter is legal and
costs only what barter costs.

---

## 5. Prerequisite: metal must be able to sit still

`f12ead0` measured that a good with no local consumer is reserved for export
the turn it is mined, because `selectTradeGoods` scores anything above one unit
as surplus and `commitPickupReservation` (`TradeRouteSystem.cpp:396`) moves it
to the export buffer, while recipes read the stockpile alone
(`EconomySimulation.cpp:926`). Copper is the control: it accumulates because it
has consumers, gold never holds a single unit because it has none.

Under this design that bug is fatal rather than merely blocking, because the
whole point is that metal with no industrial use is the good you monetise. If
the trade system ships it first, nothing can ever be adopted.

**Fix first, as its own step:** `selectTradeGoods` subtracts what the origin
city's own available recipes and its civ's monetisation demand would consume
before scoring the remainder as surplus. This is the general rule the economy
is missing, and it is a prerequisite for every phase below.

---

## 6. Phases

Each phase is one or more commits, records 200-turn seed 42/43 shadow hashes,
runs `ctest -LE golden` plus ASan, and ends at a measurement gate. Goldens move
and are re-blessed only on explicit approval. New health checks land as targets
first, never as hard checks, because they are false on the current build.

### Phase A: let a city keep what it needs (prerequisite)
Reserve local recipe inputs before export scoring. **Gate:** gold ore stock
rises above zero on a seed where it is mined; copper unchanged. This is
measurable in one run with `AOC_DUMP_ECONOMY=1` and needs no new instrument.

### Phase B: delete the coin layer
Remove the three coin goods, `isCoinGood`, the mint and melt recipes, and the
seigniorage branch of the sweep. Replace the sweep with monetisation booked as
`Monetised`. `CoinTier` becomes `moneyGood`. **This phase is money-neutral by
construction and H16 must stay green every turn;** that is its gate. Expect the
largest diff of the programme and expect it to be mostly deletions.

### Phase C: adoption as a decision
`requestSetMoneyGood(player, goodId)` alongside the existing
`requestSetMonetaryRegime`. Validation: the civ holds the good, is not on fiat,
and a dwell time since its last change. Human UI row, REST, MCP. **Gate:** a
civ can be driven through barter to silver to fiat by request alone, and each
denial path leaves state untouched.

### Phase D: the saleability score
The four-term score, the AI adopting through the Phase C request, and a
"who uses what" readout for the human. **Gate, and this is the real one:** on
seeds 42 and 43, at least two civs adopt a money good without being told to,
and at least two civs converge on the *same* good by turn 150. Convergence is
the whole thesis; if it does not happen, the acceptance weight is wrong and the
score gets tuned before anything else proceeds.

### Phase E: the exit to fiat
Industrial recipes that consume gold and silver, so the `industrial` term
falls. **Gate:** at least one civ demonetises a metal and moves to fiat for
measured opportunity-cost reasons, with the metal then appearing in industrial
recipes. Withheld until D's gate passes: an exit is meaningless until the
entrance works.

---

## 7. Risks

1. **Phase B is a large diff in the money layer.** Mitigated by H16 being a
   per-turn invariant with an existing test file, and by the phase being
   money-neutral by construction. The pre-existing negative-circulation class of
   bug (`0eeb976`) shows this area does hide errors that conservation alone does
   not catch, so the circulation-sign check belongs in the gate too.
2. **Convergence may not happen.** Phase D's gate exists to catch exactly this
   rather than to confirm it. If civs each sit on their own metal, money is
   private and the design has failed; the acceptance term is then the knob.
3. **Deleting `CoinTier` touches 16 files.** Mostly mechanical, but it is the
   kind of sweep that hides a behavioural change in a rename. The shadow hashes
   per step exist for this.
4. **A civ with no money good and no notes cannot pay.** Barter must stay
   genuinely playable rather than a penalty box, or the "choice" is not one.
   This is already half-true today and needs measuring, not assuming.
5. **The 500-turn and golden questions are unchanged** and still gated on
   explicit approval.

---

## 8. Open questions for the user

1. **Does the Mint building survive** as an ordinary gold building, or go?
2. **May two civs use different money goods at once?** The design allows it and
   the historical record is full of it. Settlement between mismatched civs then
   needs a rule: bullion at market rate, or goods for goods.
3. **Does the human get a prompt** when adoption first becomes worthwhile, or
   only the readout? Without a prompt a human may never notice the mechanic,
   which is the failure mode `COMMODITY_MONEY_PLAN.md` Phase 3 already flagged.
4. **Is copper in scope?** The old plan excluded it deliberately. Under this
   design there is no reason to special-case it, and excluding it would be
   exactly the kind of forced stamp this change is meant to remove.
