# Mechanics Analysis: What to Add to Age of Civilization

Based on research across Civ 6, Old World, Humankind, and FreeCiv.

## Current State

**Already Implemented (16 systems):** Districts, Adjacency, Housing, Religion, Climate/Disasters, Great People, Wonders, Victory Conditions, World Congress, City-States, Barbarian Clans, Government/Policies, Air Combat, Formations (Corps/Army), Supply Lines, Naval Trade, Espionage (basic), Economy (deep: supply chains, stocks, bonds, forex, monetary evolution).

**Partially Implemented:** Golden/Dark Ages (era scores exist but limited), Tourism/Culture Victory (borders expand but no tourism mechanic).

**Missing:** Governors, Zone of Control.

---

## TIER 1: High-Impact Additions (Differentiation from Civ 6)

### 1. Expanded Espionage: Economic Intelligence Network

The existing spy system has 5 missions. Given the game's deep economy, espionage should be the primary way players interact with the hidden economic systems of other civilizations.

**New Spy Missions (economic focus, unique to this game):**
- **Monitor Treasury** — Passive. Reveals enemy's gold income/expenses per turn. Duration: ongoing while spy is placed.
- **Monitor Research** — Passive. Reveals what tech the enemy is currently researching and progress. Ongoing.
- **Siphon Funds** — Active. Steal % of enemy Commercial district income each turn (Civ 6 style but ongoing like Humankind's siphon).
- **Market Manipulation** — Active. Temporarily crash an enemy's local market prices for goods you choose. Ties into the existing Market system.
- **Currency Counterfeiting** — Active. Reduces enemy CurrencyTrust by 5-15 points. Ties into existing CurrencyTrust system.
- **Supply Chain Disruption** — Active. Target a specific supply chain node (recipe) and reduce its efficiency by 50% for 10 turns.
- **Insider Trading** — Active. Gain preview of enemy's stock market moves for 10 turns. Ties into StockMarket system.
- **Steal Trade Secrets** — Active. Copy an enemy's industrial revolution bonus if they're ahead. Ties into IndustrialRevolution system.
- **Embargo Intelligence** — Passive. Reveals who the enemy is trading with and what goods. Useful for sanctions enforcement.
- **Recruit Double Agent** — Active. Captured enemy spy can be turned, feeding false intelligence back to origin.

**Graduated Intelligence Levels (novel — inspired by real intelligence analysis):**
- Level 0: No info (fog of war)
- Level 1: Know what buildings/districts exist (embassy/basic contact)
- Level 2: See military composition and approximate strength (scout/border contact)
- Level 3: See production queues, exact treasury, income/turn (spy with Monitor mission)
- Level 4: See tech research, trade routes, diplomatic deals (advanced spy network)
- Level 5: See exact stockpiles, supply chain state, internal politics (master spy network)

This is a ML training signal too — it maps directly to what data the model should see.

**Spy Levels and Promotions (Civ 6 style):**
- Recruit → Agent → Secret Agent → Master Spy (4 levels)
- Each level improves success rate by one step on the probability scale
- Promotions chosen from random selection of 3 on level-up
- Economic promotions: Financier (+2 levels for Siphon/Market Manipulation), Analyst (+2 for Monitor missions)
- Military promotions: Saboteur (+2 for Supply Chain/Production sabotage), Guerrilla Leader (Recruit Partisans)

**Counter-Espionage:**
- Station a spy in own city as Counterspy
- Protects the district the spy is in + adjacent districts
- Polygraph promotion: enemy spies operate at -1 level in this city
- Caught spies can be: released (small diplo hit), imprisoned (big diplo hit but you lose the spy), executed (massive diplo hit), or turned (double agent)

### 2. Orders System (from Old World — highest-rated 4X innovation)

Replace per-unit fixed movement with a shared action economy. Each turn you get N Orders that can be spent on ANY unit/city action. This is the single most praised mechanic innovation in recent 4X games.

**Why it's transformative:**
- Creates meaningful resource-allocation decisions every turn
- Every action has opportunity cost (moving a scout means not attacking with a warrior)
- Solves the "late-game tedium" of moving 30 units individually
- Scales naturally: larger empires get more orders but also need more

**Implementation:**
- Base Orders = 2 + (Era * 0.5) + (Population / 20) + Government bonuses
- Moving a unit = 1 Order per tile
- Attacking = 1 Order
- Founding a city = 2 Orders
- Building an improvement = 1 Order
- Fatigue: units that spend 3+ Orders in one turn get -20% combat strength next turn

This would be the biggest differentiator from Civ 6.

### 3. Governors (Civ 6 style, enhanced)

7 unique governors with promotion trees. Each city can have one governor.

**Suggested governors tied to the game's economic focus:**
- **The Financier** — +20% gold in city, commercial district adjacency +2. Promotions: bond market access, reduced trade route costs.
- **The Industrialist** — +15% production, factory buildings +50% output. Promotions: auto-build improvements, reduced pollution.
- **The Diplomat** — +8 loyalty, +2 amenities. Promotions: spy resistance, foreign trade route bonuses.
- **The General** — +5 combat strength for garrisoned units, walls +25% HP. Promotions: militia units, reduced unit maintenance.
- **The Scholar** — +15% science, +1 great scientist point. Promotions: eureka boosts, library gold bonus.
- **The Merchant** — +1 trade route capacity, +30% trade route yield. Promotions: market price manipulation, resource monopoly bonuses.
- **The Environmentalist** — -50% pollution, +2 appeal. Promotions: clean energy unlocks earlier, national park bonuses.

---

## TIER 2: Strong Additions (Genre-Standard Features)

### 4. Zone of Control

Military units and encampment districts exert ZoC on adjacent tiles. Entering a ZoC tile costs all remaining movement. This adds huge tactical depth to combat — blocking mountain passes, river crossings, city approaches.

### 5. Tourism / Cultural Victory

Currently culture only expands borders. Add:
- Great Works (placed in Theater Square buildings, provide Tourism)
- National Parks (Naturalist unit, 4 tiles with high appeal)
- Rock Bands (Faith-purchased, perform in foreign lands for burst tourism)
- Cultural Victory: your total tourists > every other civ's domestic tourists
- Tourism sources: Great Works, Wonders, Holy Cities, open borders bonus (+25%)

### 6. Casus Belli System

Currently war declarations are binary. Add justification types:
- Formal War (full grievance penalty)
- Holy War (defending religion, half penalty)
- Liberation War (freeing allies' cities, no penalty)
- Reconquest War (retaking your own cities, no penalty)
- Colonial War (target 2+ eras behind, reduced penalty)
- Economic War (responding to trade sanctions/embargo, reduced penalty — unique to this game)

### 7. Alliance Types (Civ 6 style)

5 alliance types that level up over time:
- Research Alliance: share eureka boosts at L2, free tech every 30 turns at L3
- Military Alliance: shared vision at L2, +5 combat strength in allied territory at L3
- Economic Alliance: shared market prices at L2, +15% trade route yields at L3
- Cultural Alliance: tourism bonus at L2, shared great works at L3
- Religious Alliance: shared faith bonuses at L2

### 8. Eureka/Inspiration Boosts

Each tech has a gameplay objective that grants 40% research credit. Examples:
- Archery: kill a unit with a Slinger
- Masonry: build a Quarry
- Banking: have treasury above 1000 gold
- Economics: have 5 trade routes active
- Industrialization: build 3 Factories

This connects gameplay to research and makes the tech tree feel dynamic.

---

## TIER 3: Differentiating Additions (Novel Mechanics)

### 9. Tech Deck (from Old World — widely praised)

Instead of a fixed tech tree, draw 4 tech cards per turn, pick 1, discard 3. Discarded cards re-enter the deck after 3-4 other techs. This eliminates "solved" optimal paths and forces adaptation.

Could be offered as an alternative game mode: "Classic Tech Tree" vs "Tech Deck" in game setup.

### 10. Character/Dynasty System (from Old World)

Leaders age, develop traits, get married, have heirs, and die. Character traits emerge from game events. Succession crises can destabilize empires.

This would be a MASSIVE addition but would make the game truly unique. Could start with a simplified version: leaders have 3 traits that affect gameplay, and succession happens on leader death.

### 11. War Support / War Score (from Humankind)

Wars have a built-in clock. Losing battles/territory reduces War Support. When War Support hits 0, forced surrender. War Score accumulated from victories is "spent" as currency for peace terms.

This creates natural war pacing and prevents infinite wars that drain both sides.

### 12. Independent People / Minor Factions (Humankind style enhancement of City-States)

Non-player settlements that can be: patronized (invest money for influence), converted to client states (partial control), or assimilated (full annexation). More nuanced than Civ 6's binary envoy system.

### 13. Narrative Events (from Old World)

Random events triggered by game state: "Your merchant caravan was attacked by bandits. Do you send soldiers (cost: 2 military units for 5 turns) or negotiate (cost: 500 gold)?" Events chain into future consequences.

Creates unique stories each game and makes the world feel alive.

---

## TIER 4: Nice-to-Have

### 14. Monopolies & Corporations (Civ 6 mode)
Control 60%+ of a luxury for industry bonus; 100% for corporation with empire-wide effects. Fits perfectly with the game's economic focus.

### 15. Appeal / Aesthetic System
Per-tile beauty rating affecting housing quality, tourism, and National Parks. Adjacent forests/coast/wonders increase appeal; industrial zones/mines decrease it.

### 16. Rock Bands / Cultural Units
Faith-purchased units that perform in foreign lands for burst tourism. Random promotions and chance of disbanding after each performance.

### 17. Diplomatic Favor as Currency
Earned from alliances, city-state suzerainty, government type. Spent on World Congress votes and proposals. Creates a real economy around diplomacy.

---

## Priority Recommendation

For maximum differentiation from Civ 6 while leveraging the game's economic depth:

1. **Expanded Espionage** — Unique economic spy missions that interact with your existing deep economy (supply chains, currency, stocks). No other game does this.
2. **Orders System** — The single most praised 4X innovation. Would immediately set the game apart.
3. **Governors** — Standard feature that's missing. Easy to implement, high player satisfaction.
4. **Zone of Control** — Essential for tactical combat depth. Small implementation, big gameplay impact.
5. **Eureka Boosts** — Connects gameplay to research. Relatively easy to add.
