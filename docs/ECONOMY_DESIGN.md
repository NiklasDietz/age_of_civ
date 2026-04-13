# Economy Design: Analysis and Fixes

Based on the user's questions about economic realism.

## The Core Problem

The economy has incredibly deep systems (46 recipes, 5 monetary stages, supply chains,
inflation, bonds, forex) but the connections between them are weak. Goods pile up in
stockpiles with no real purpose. Money is abstracted as "+gold per building" which
contradicts the commodity money system. The result: a sophisticated economic engine
where the gameplay feels like "+3 gold" buildings.

## What Should Happen (the correct economic loop)

```
Mine ore → Mint coins → coins ARE money (commodity)
         → OR smelt into tools/steel → sell goods on market → earn coins from buyers

Goods are needed by:
  - Population: food, consumer goods, luxuries → meeting demand = happy = more tax
  - Buildings: tools to operate, fuel to power → without inputs, buildings work at 50%
  - Military: steel for swords, fuel for tanks → without resources, can't build units
  - Science: funding research costs gold → but new techs unlock better goods → ROI
  - Other players: they buy what they can't produce → comparative advantage drives trade

Money enters the system through:
  - Mining + minting coins (commodity money stage)
  - Government spending (fiat money stage — printing, with inflation risk)
  - Trade surpluses (selling more than buying)
  - Taxation (% of economic activity)
```

## Fixes Required

### Fix 1: Remove artificial gold generation from industrial buildings

The "+gold per production bonus" I added contradicts commodity money. A factory doesn't
create gold coins — it creates goods that are sold for coins. The gold should come from:
- Selling goods on the market (already implemented but underused)
- Population taxation (already implemented)
- Minting coins from ore (already implemented)

### Fix 2: Make goods consumption matter more

Currently goods pile up because demand is too low. Increase demand:
- **Building effectiveness**: Factories without tools/fuel operate at 50% efficiency.
  Already partially implemented via ongoingFuel, but tool dependency is missing.
- **Population happiness from goods**: Consumer goods should directly boost happiness.
  Having Clothing, Processed Food, Electronics = happier citizens = more growth + tax.
- **Science from goods**: Certain goods (Paper, Glass, Computers) boost research speed.
  A civ with computers researches Information-era techs 25% faster.

### Fix 3: Science should cost money

Research is currently free — you just accumulate science points. In reality, research
costs money (labs, equipment, scientists' salaries). Adding a gold-per-science-point
cost means rich nations research faster, but it's an investment because new techs
unlock better recipes and buildings.

### Fix 4: Fiat money should be trust-based and impactful

Currently fiat is just "better commodity money." It should be:
- Theoretically worthless paper/digital currency
- Accepted because of trust (GDP, military, trade partners)
- Can be printed (increases money supply → inflation risk)
- Named per-player ("Dollar", "Yuan", etc.)
- Exchange rates between currencies based on trust/GDP
- Reserve currency status = huge power (seigniorage already exists)

### Fix 5: Military should have economic benefits long-term

Currently war is purely destructive. Should also allow:
- Pillaging improvements → gain resources
- Capturing cities → gain their production/trade
- War reparations in peace treaties → forced resource/gold payments
- Controlling strategic resources → monopoly pricing power

### Fix 6: Small countries can get rich after industrialization

Already partially implemented (industrial revolution per-citizen bonus). But the key
mechanic is: small country with high-tech can produce high-value goods (electronics,
software) that large countries with many people can't (they're still making steel).
The comparative advantage system handles this — just needs the goods to actually matter.
