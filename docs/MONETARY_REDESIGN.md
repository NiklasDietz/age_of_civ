# Monetary System Redesign: Money as a Real Good

## The Vision

Money doesn't exist at game start. Players must barter. Eventually gold emerges
as a medium of exchange because everyone values it. Gold coins are a physical good
that gets mined, minted, spent, and can leave the system. Fiat money is paper
backed only by trust — and trading between fiat currencies requires exchange.

## What shipped (2026-09-12, the money and trade programme, Phase 2)

Money is conserved. Each civ carries four pools in `MonetaryStateComponent`:
the treasury, `privateSpecie` (coin in private hands), `privateNotes` (paper in
private hands once notes are issued) and `bullion` (minted metal held before
coinage is adopted). Coins are still struck by the Mint from mined ore; at the
end of every turn the coin goods are swept out of the city stockpiles into
bullion (Barter) or private coin (coinage) at face value, with a 10% seigniorage
share to the treasury, and booked as minted. No coin good outlives the sweep.

Every treasury mutation goes through `Player::addGold`, `spendGold` or
`setTreasury` with a mandatory `MoneyFlow` tag (Domestic, Transfer, External,
Minted, Printed, Loss); `TreasuryAccount` makes a direct write a compile
error. A transient `MoneyLedger` on `EconomySimulation` books what entered or
left the world each turn, and the headless tool asserts
`worldMoney(after) - worldMoney(before) == ledger` on every turn.

- Income is a tax on private money: `privateSpecie (+ notes) x taxable share x
  taxRate x collectionEfficiency`, kept by the goldAllocation share. Buildings
  create no gold; they raise the collection efficiency (Palace 0.10, Market
  0.08, Bank 0.12, Stock Exchange 0.18, ...). See `Maintenance.hpp`.
- Every state payment lands in somebody's private money: purchases, upkeep and
  research pay the civ's own people; a garrison abroad pays the province it
  stands in. The treasury never overdraws; an unpaid bill is arrears, and five
  turns of them disband a unit.
- Trade settles in the buyer's coin: the buyer's people pay the price into the
  Trader's purse, the purse rides home and lands as customs (the tax rate) to
  the treasury and the rest to the merchants; what the buyer cannot pay it
  pays in goods. Trusted paper settles in notes between paper regimes.
- Specie regimes have a sticky price anchor: `P* = M x V / (K x population)`,
  clamped to [0.5, 4], reached at 3% a turn; purchases, upkeep and research
  funding are nominal at the level.
- The regime is a decision (`requestSetMonetaryRegime`): coinage needs a Mint
  and bullion of the chosen metal; the Gold Standard issues notes one for one
  against the people's coin; fiat needs Banking and Printing or Economics, two
  live partners and low inflation. Backing is the metal in the country over the
  notes outstanding; a specie drain is what suspends convertibility.

The "money as stockpile goods" target below was NOT taken: no mechanic reads a
coin's location, and per-city coins would have cost change-making, tax
fragmentation and cargo leaks for nothing. The per-civ ledger keeps every
behavioural requirement (conservation, a chosen metal, the Price Revolution,
specie drains, refusable fiat) at half the cost.

## Phase 1: Barter (No Money)

**What exists:** Only goods. Trade is direct good-for-good swap.

**Gameplay:** 
- No treasury number. Can't "buy" anything with gold.
- To build a unit, you pay in production (hammers) + physical resources (iron, wood)
- Trading with other players: offer wheat for their iron, etc.
- Problem: "double coincidence of wants" — you have wheat, they have iron, but 
  they don't want wheat. This makes trade inefficient (already modeled by the 
  50% trade efficiency in barter).

**What makes players WANT to leave barter:**
- Trade inefficiency (50% loss on every trade)
- Can't easily compare values (how many wheat = 1 iron?)
- Can't save wealth (goods spoil, stockpiles overflow)

## Phase 2: Commodity Money (Gold Coins)

**The transition:**
- Player builds a Mint → mints coins from gold/silver/copper ore
- Coins enter the CITY STOCKPILE (not an abstract treasury)
- Coins are a normal good that can be traded, stored, spent

**How the economy works with real coins:**

1. **Government collects coins via taxation:**
   - Each turn, tax rate% of the coins circulating in the city go to treasury
   - Treasury IS the government's coin stockpile
   - Higher population = more economic activity = more coins collected in tax

2. **Government spends coins:**
   - Building a building costs coins (from treasury to workers/void)
   - Unit maintenance costs coins each turn
   - Purchasing items costs coins
   - These coins DON'T disappear — they re-enter the private economy
     (workers get paid, spend at shops, government taxes back later)

3. **Gold mining creates new money:**
   - Each gold mine produces gold ore → Mint → gold coins
   - More coins in circulation = each coin worth less = inflation
   - This is EXACTLY how historical inflation worked (Spanish Price Revolution 
     when New World gold flooded Europe)

4. **Population growth creates money demand:**
   - More people = more transactions = more coins needed
   - If coin supply doesn't keep up with population, deflation occurs
   - Deflation is also bad (nobody spends, economy stalls)
   - Sweet spot: coin growth ≈ population growth ≈ 2-3% inflation

5. **Coins can leave the economy:**
   - Trade deficit: buying more goods from other players than selling → coins 
     flow OUT to trade partners
   - This is the historical price-specie flow mechanism
   - A player who imports too much literally runs out of money

**Key implementation change:** `treasury` should be an alias for total coin 
stockpile across all cities, not a separate number.

## Phase 3: Fiat Money

**The transition:**
- Player researches Banking/Printing → can issue paper notes
- Notes are a NEW good (specific to this player: "Roman Denarius", "Chinese Yuan")
- Government declares: "these notes are legal tender for taxes"

**How fiat works:**
- Government prints notes → adds to money supply
- Citizens accept notes because they need them to pay taxes
- Trust determines if OTHER players accept your notes
- Low trust → trade partners demand gold coins instead
- High trust → your notes are accepted globally (reserve currency)

**Inter-player currency exchange:**
- Player A has "Dollars", Player B has "Marks"
- To trade, they either:
  a) Exchange at the bilateral exchange rate (trust-based)
  b) Use a common reserve currency (if one exists)
  c) Fall back to gold settlement (always works)
- Exchange rate = (Trust_A × GDP_A) / (Trust_B × GDP_B) approximately

**Reserve currency emergence (natural, not forced):**
- The player with highest trust + largest trade network naturally becomes the 
  currency everyone prefers
- Others start holding reserves of that currency (like USD today)
- This gives the reserve holder "exorbitant privilege" (seigniorage)
- But if trust drops (inflation, debt, military loss), reserve status can shift

## Changes Required (historical; see "What shipped" above)

1. Merge treasury with coin stockpile: superseded by the per-civ ledger. The
   treasury is one account; coin goods are swept into the pools each turn.
2. Remove abstract gold income: DONE. Income is the tax on private money;
   buildings are collection efficiency.
3. Per-player fiat currency goods: NOT done as goods. Notes are a private pool;
   settlement between paper regimes applies the exchange rate, capped to
   [0.5, 2], and distrusted paper falls back to coin, then goods.
4. Fix the income loop: DONE. Mine -> ore -> Mint -> coins -> sweep -> private
   money -> tax -> treasury -> spending -> private money.
