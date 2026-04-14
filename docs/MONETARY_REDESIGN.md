# Monetary System Redesign: Money as a Real Good

## The Vision

Money doesn't exist at game start. Players must barter. Eventually gold emerges
as a medium of exchange because everyone values it. Gold coins are a physical good
that gets mined, minted, spent, and can leave the system. Fiat money is paper
backed only by trust — and trading between fiat currencies requires exchange.

## Current State vs Target

### Current (broken abstraction)
- Treasury is an abstract number starting at 100
- "Gold income" appears from buildings (+3 gold from Market)
- Gold coins exist as stockpile goods but are SEPARATE from treasury
- No connection between physical coins and spending power

### Target (money as real good)
- Treasury IS the coin stockpile (copper/silver/gold coins)
- Income = government taxes on economic activity (coins flow from citizens → treasury)
- Spending = coins flow OUT of treasury (paying for buildings, units, maintenance)
- More mining → more coins in economy → inflation if output doesn't keep up
- Population growth → more demand for coins → deflationary pressure
- Fiat: government creates tokens, trust determines if anyone accepts them

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

## Changes Required

### 1. Merge treasury with coin stockpile
- Remove `MonetaryStateComponent::treasury` as a separate field
- Treasury = sum of all coin goods across all player cities
- All income/expense operations become stockpile add/remove operations

### 2. Remove abstract gold income
- No more "+X gold from buildings"
- Instead: buildings increase PRODUCTIVITY → more goods produced → more 
  economic activity → more taxable commerce → more coins collected in tax
- The coins already exist (were minted earlier) — taxation just moves them 
  from private to government

### 3. Add per-player fiat currency good
- When a player transitions to fiat, create a new good type specific to them
- "Roman Note", "Egyptian Papyrus", etc.
- These are goods that can be traded, stockpiled, and have market prices
- Exchange rates emerge from market supply/demand

### 4. Fix the income loop
```
Current:  Building → "+5 gold" (from nothing)
Correct:  Gold mine → ore → Mint → coins → circulate → tax → treasury
          Building → more production → more goods → more trade → more tax
```

## Implementation Priority

This is a MASSIVE redesign. Suggested phases:

**Phase A (can do now):** Make treasury = coin stockpile. Remove starting treasury.
Income comes from taxation of existing circulating coins.

**Phase B (medium):** Remove all abstract "+gold" bonuses from buildings.
Buildings increase productivity, not gold directly. Tax system collects coins.

**Phase C (large):** Add per-player fiat currency goods. Exchange rates.
Reserve currency emergence.

Each phase should be tested with the diagnostic tool and GA before proceeding.
