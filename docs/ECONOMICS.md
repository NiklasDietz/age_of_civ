# Age of Civilization -- Economic Mechanics Documentation

## Overview

The economy is designed so that trade patterns, specialization, and comparative
advantage emerge naturally from game rules rather than from explicit player
choices. Players don't click "enable comparative advantage" -- it happens
because the rules make it the optimal strategy.

---

## Core Economic Loop (Per Turn)

```
1. Harvest raw resources from worked tiles -> city stockpiles
2. Resource depletion (1% chance per worked tile per turn)
3. Internal trade between player's own cities (surplus -> deficit)
4. Execute production recipes (limited by worker capacity)
5. Report supply/demand to global market
6. Update market prices (per-good elasticity)
7. Execute trade routes (deliver cargo between cities)
8. Monetary policy (inflation, fiscal, central bank)
9. Advanced economics (tariffs, banking, infrastructure)
```

---

## Mechanic 1: Resource Scarcity & Geographic Distribution

**What:** Resources are placed on specific tiles during map generation based on
terrain rules (iron on hills, wheat on grassland, oil in desert/tundra, etc.).
Not every player starts with every resource.

**Why it matters:** A player without iron MUST trade for it or conquer it.
This is the foundation of all trade -- uneven resource distribution creates
natural demand.

**Emergent effect:** Players with rare resources become trade hubs. Players
without them must specialize in what they CAN produce and trade for what
they can't.

---

## Mechanic 2: Production Chains (4-Tier Processing)

**What:** Raw resources must be processed through multi-tier chains:
```
Iron Ore -> Iron Ingots -> Tools -> Machinery -> Industrial Equipment
Coal + Iron Ore -> Steel -> Advanced Machinery
Copper Ore -> Copper Wire -> Electronics -> Microchips -> Computers
```

34 recipes across 4 tiers. Each recipe requires a specific building in the city.

**Why it matters:** You can't just have iron and build tanks. You need iron
AND coal AND a factory AND an electronics plant AND multiple processing steps.
This creates deep economic dependency chains.

**Emergent effect:** Manufacturing nations that lack raw resources NEED trade
partners who have them. Resource-rich nations that lack factories NEED to
trade raw materials for finished goods.

---

## Mechanic 3: Worker Capacity Limits

**What:** Each city can execute at most `population / 2` recipes per turn
(minimum 1). A pop-3 city runs 1 recipe. A pop-10 city runs 5.

**Why it matters:** Small cities MUST specialize. A mining town can smelt
iron OR forge tools, but not both. Only large industrial cities can run
complex multi-step production.

**Emergent effect:** Natural city specialization. Mining towns, farm towns,
industrial centers, and trade hubs emerge from population constraints.

---

## Mechanic 4: Internal Trade (Between Your Own Cities)

**What:** Each turn, cities with surplus goods (>2) automatically share with
cities that have deficit (0 but need the good for a recipe). Transfer is
limited by distance: 10% loss per 5 hexes, reduced by roads.

**Why it matters:** Your iron mining town needs to get iron to your factory
city. If they're far apart, you lose material to transport. Roads and coastal
cities reduce this loss.

**Emergent effect:** Players build cities in clusters. Road infrastructure
becomes economically important (not just for unit movement). Coastal cities
with harbors become natural trade hubs.

---

## Mechanic 5: Supply/Demand Market with Per-Good Elasticity

**What:** A global market tracks supply and demand for every good. Prices
adjust each turn based on the ratio:
```
price = basePrice * (demand / supply) ^ elasticity
```

Different goods have different elasticities:
- Food (wheat, cattle): 0.2 -- prices barely move (always needed)
- Strategic (iron, oil): 0.4 -- moderate swings
- Processed (tools, steel): 0.5 -- medium
- Luxury (gems, silk): 0.7 -- volatile
- Advanced (electronics): 0.8 -- very volatile

**Why it matters:** Producing lots of a good crashes its price (diminishing
returns). Scarce goods command premium prices. This incentivizes
diversification over monoculture.

**Emergent effect:** "Flooding the market" is a real strategy risk. A player
who mass-produces one good sees prices drop. Better to produce a variety or
find buyers who need it.

---

## Mechanic 6: Comparative Advantage (Natural Emergence)

**What:** No explicit mechanic. Comparative advantage emerges from:
- Uneven resource distribution (Mechanic 1)
- Processing chain requirements (Mechanic 2)
- Worker capacity limits (Mechanic 3)
- Market pricing with elasticity (Mechanic 5)

**Example:**
Player A has iron tiles, Player B has wheat tiles.
- A produces iron cheaply (local supply), food is expensive (must import)
- B produces food cheaply (local supply), iron is expensive (must import)
- Market price for iron is high for B, low for A
- Market price for food is high for A, low for B
- Trading is mutually beneficial: A sells iron at B's high price, B sells food at A's high price
- Both are better off than trying to produce everything themselves

**Even with absolute advantage:** If Player A can produce BOTH iron and food
more efficiently, they should still specialize in iron (where their
advantage is GREATEST) and trade for food, because their workers are more
productive doing what they're best at.

---

## Mechanic 7: Resource Curse (Dutch Disease)

**What:** Players whose income is >60% from raw resource exports get:
- Manufacturing penalty: up to -30% processed goods output
- Currency appreciation: up to +20% export prices (makes exports expensive for buyers)
- Happiness penalty: up to -3 amenities

**Why it matters:** Being resource-rich is a double-edged sword. Easy money
from raw exports crowds out manufacturing development.

**Emergent effect:** Resource-rich nations are incentivized to SELL raw
materials rather than process them (manufacturing penalty makes processing
inefficient). Manufacturing nations are incentivized to BUY cheap raw
materials. This creates natural trade flows.

---

## Mechanic 8: Monetary System Evolution

**What:** Four stages: Barter -> Commodity Money -> Gold Standard -> Fiat

Each stage unlocks more trade capacity:
- Barter: 50% trade efficiency, 1 trade route max
- Commodity Money: 80% efficiency, 3 routes
- Gold Standard: 95% efficiency, 6 routes
- Fiat Money: 100% efficiency, 10 routes

**Why it matters:** Early-game trade is inefficient and limited. Advancing
your monetary system is essential for a trade-based economy.

**Emergent effect:** Players who advance their monetary system first get a
trade advantage. Fiat money is powerful but risks inflation.

---

## Mechanic 9: Inflation (Fisher Equation)

**What:** `inflationRate = (dM/M) + (dV/V) - (dY/Y)`
- Money supply growth faster than GDP = inflation
- Velocity (spending speed) affected by interest rates
- Central bank controls: interest rate, reserve requirement, money printing

**Why it matters:** Printing money to fund wars causes inflation, which
reduces everyone's purchasing power and causes unhappiness.

**Emergent effect:** Fiscal discipline is rewarded. Players who over-spend
face economic consequences (inflation spiral, debt crisis).

---

## Mechanic 10: Tariffs & Trade Blocs

**What:** Per-player import tariffs (0-50%). Trade blocs with shared internal
tariff (usually 0%) and common external tariff.

**Why it matters:** Tariffs protect domestic industry but raise prices for
consumers. Trade blocs create preferential trading zones.

**Emergent effect:** Political alliances have economic consequences. Embargoing
a rival hurts their economy but also yours (lost trade).

---

## Mechanic 11: Resource Depletion

**What:** Each turn, worked resource tiles have a ~1% chance of depleting
(resource removed). Over a long game, resources shift.

**Why it matters:** Forces long-term economic planning. A player relying on
a single iron mine will eventually need alternatives.

**Emergent effect:** Trade patterns shift over time. New resources become
valuable as old ones deplete. Exploration and expansion remain important
in late-game.

---

## Mechanic 12: Technology Spillover

**What:** Trading with a more advanced nation gives a small science bonus
proportional to the tech gap.

**Why it matters:** Trade isn't just about goods -- it's about knowledge.
Less advanced nations benefit from trading with leaders.

**Emergent effect:** Isolationist strategies miss out on tech spillover.
Trade-heavy strategies accelerate research.

---

## Mechanic 13: Banking & Debt Crisis

**What:** Players can take loans (instant gold, accumulates interest).
Debt > 2x GDP triggers a banking crisis: -20% production, -3 amenity.

**Why it matters:** Debt-fueled expansion is risky. Banking crises can
cripple an economy at the worst time.

**Emergent effect:** Conservative fiscal policy is safer but slower.
Aggressive borrowing can accelerate growth but risks collapse.

---

## Summary: How It All Fits Together

The economic system creates a web of interconnected incentives:

1. **Geography determines initial advantages** (resource distribution)
2. **Production chains create dependencies** (you need inputs you may not have)
3. **Worker limits force specialization** (small cities do one thing well)
4. **Markets reward scarcity** (produce what others can't)
5. **Trade enables growth** (access to goods you don't produce)
6. **Monetary policy enables/constrains trade** (fiat = more trade, but inflation risk)
7. **Resource depletion creates dynamism** (trade patterns shift over time)
8. **Tariffs/blocs create political economy** (alliances affect trade)

No single mechanic creates comparative advantage. The combination of ALL of
them makes it the naturally optimal strategy: specialize in what you're good
at and trade for the rest.
