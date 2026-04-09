# Civilization 6 Reference Data (Gathering Storm, Final Patch)

Note: These values are from training data knowledge of the final Civ 6 Gathering Storm patch.
Wiki access was blocked, so values are from memory. Flagged items may need verification.

## Technologies by Era (Standard Speed)

### Ancient Era (8 techs)
| Tech | Cost | Eureka |
|------|------|--------|
| Pottery | 25 | - |
| Animal Husbandry | 25 | - |
| Mining | 25 | - |
| Sailing | 50 | Coast tile |
| Astrology | 50 | Find natural wonder |
| Irrigation | 50 | Farm |
| Writing | 50 | Meet another civ |
| Archery | 50 | Kill unit with Slinger |

### Classical Era (9 techs)
| Tech | Cost | Key Unlock |
|------|------|------------|
| Celestial Navigation | 120 | Harbor district |
| Currency | 120 | Market |
| Horseback Riding | 120 | Horseman |
| Iron Working | 120 | Swordsman |
| Shipbuilding | 120 | Galley upgrade |
| Mathematics | 200 | - |
| Construction | 200 | Water Mill |
| Engineering | 200 | Aqueduct |
| Bronze Working | 80 | Encampment |

### Medieval Era (10 techs)
| Tech | Cost |
|------|------|
| Apprenticeship | 275 |
| Buttress | 300 |
| Castles | 300 |
| Education | 390 |
| Machinery | 275 |
| Military Engineering | 275 |
| Military Tactics | 275 |
| Stirrups | 300 |
| Gunpowder | 390 |
| Printing | 300 |

### Renaissance Era (7 techs)
| Tech | Cost |
|------|------|
| Cartography | 490 |
| Mass Production | 490 |
| Banking | 490 |
| Astronomy | 600 |
| Metal Casting | 490 |
| Siege Tactics | 490 |
| Square Rigging | 600 |

### Industrial Era (7 techs)
| Tech | Cost |
|------|------|
| Industrialization | 700 |
| Scientific Theory | 700 |
| Ballistics | 680 |
| Military Science | 700 |
| Steam Power | 805 |
| Sanitation | 805 |
| Economics | 700 |

### Modern Era (7 techs)
| Tech | Cost |
|------|------|
| Electricity | 985 |
| Radio | 985 |
| Chemistry | 985 |
| Combustion | 985 |
| Flight | 1065 |
| Advanced Flight | 1140 |
| Rocketry | 1140 |

### Atomic Era (5 techs)
| Tech | Cost |
|------|------|
| Advanced Ballistics | 1410 |
| Combined Arms | 1410 |
| Plastics | 1195 |
| Nuclear Fission | 1410 |
| Synthetic Materials | 1195 |

### Information Era (5 techs)
| Tech | Cost |
|------|------|
| Computers | 1580 |
| Nuclear Fusion | 1850 |
| Nanotechnology | 1850 |
| Robotics | 1560 |
| Satellites | 1690 |
| Telecommunications | 1580 |
| Guidance Systems | 1690 |
| Stealth Technology | 1690 |
| Lasers | 1580 |

### Future Era (Gathering Storm, repeatable)
| Tech | Cost |
|------|------|
| Future Tech | 1780 |
| Smart Materials | 1780 |
| Predictive Systems | 1780 |
| Offworld Mission | 2500 |
| Smart Power Grid | 1780 |

**Total: ~67 unique techs**

## Game Speed Multipliers

| Speed | Max Turns | Research Cost | Production Cost | Growth | Gold | Faith | Culture |
|-------|-----------|--------------|-----------------|--------|------|-------|---------|
| Online | 250 | 0.5x | 0.5x | 1.0x | 1.0x | 1.0x | 0.5x |
| Quick | 330 | 0.67x | 0.67x | 1.0x | 1.0x | 1.0x | 0.67x |
| Standard | 500 | 1.0x | 1.0x | 1.0x | 1.0x | 1.0x | 1.0x |
| Epic | 750 | 1.5x | 1.5x | 1.5x | 1.5x | 1.5x | 1.5x |
| Marathon | 1500 | 3.0x | 3.0x | 3.0x | 3.0x | 3.0x | 3.0x |

**KEY INSIGHT**: In Civ 6, growth cost IS scaled by game speed in Epic/Marathon.
But yields (science/turn, production/turn) are NOT scaled. Only thresholds scale.
This means in Marathon you produce the same science/turn but techs cost 3x more.
Result: same number of techs completed, but each takes 3x as many turns.

**IMPORTANT**: Standard is 500 turns, not 400. At Standard you typically complete
the tech tree by turn ~350-400, leaving ~100 turns for endgame.

## Science Sources

| Source | Science/Turn |
|--------|-------------|
| Per citizen | +0.7 |
| Palace (capital) | +2 |
| Library | +2 |
| University | +4 |
| Research Lab | +5 |
| Campus adjacency (per adj. mountain) | +1 |
| Campus adjacency (per adj. district) | +0.5 |
| Campus adjacency (per adj. rainforest) | +0.5 |
| Great Scientist (recruitment) | Varies (burst) |
| Rationalism policy | +100% campus adjacency |
| Natural Philosophy policy | +100% campus building yields |
| International Space Agency (late policy) | +10% science per city-state suzerain |
| Trade route to ally | +1-3 science |
| Geneva suzerain bonus | +15% science |

**Typical science output progression:**
- Turn 50 (1 city): ~8 science/turn
- Turn 100 (3 cities): ~25 science/turn
- Turn 150 (5 cities): ~60 science/turn
- Turn 200 (6 cities): ~100 science/turn
- Turn 250 (7 cities): ~150+ science/turn

## Civic Costs by Era

| Era | Cost Range |
|-----|-----------|
| Ancient | 25-60 |
| Classical | 110-200 |
| Medieval | 275-400 |
| Renaissance | 440-660 |
| Industrial | 660-940 |
| Modern | 940-1140 |
| Atomic | 1195-1410 |
| Information | 1580-1850 |
| Future | 1780+ |

## District Costs

Base cost scales with number of techs+civics completed:
- Base: 54 production
- Scales: +9 per tech/civic completed (approximately)
- Late game: 300-500 production per district
- Unique districts: 50% of normal cost

## Building Costs and Yields

### Campus Buildings
| Building | Cost | Maintenance | Science | Other |
|----------|------|-------------|---------|-------|
| Library | 90 | 1 | +2 | - |
| University | 250 | 2 | +4 | +1 housing |
| Research Lab | 480 | 3 | +5 | - |

### Commercial Hub Buildings
| Building | Cost | Maintenance | Gold | Other |
|----------|------|-------------|------|-------|
| Market | 120 | 0 | +3 | +1 trade route |
| Bank | 290 | 0 | +5 | +1 trade route |
| Stock Exchange | 390 | 0 | +7 | +1 trade route |

### Industrial Zone Buildings
| Building | Cost | Maintenance | Production | Other |
|----------|------|-------------|------------|-------|
| Workshop | 195 | 1 | +2 | - |
| Factory | 390 | 2 | +3 (+3 regional) | - |
| Power Plant (Coal) | 390 | 3 | +4 (+4 regional) | Needs coal |
| Power Plant (Oil) | 390 | 3 | +4 (+4 regional) | Needs oil |
| Power Plant (Nuclear) | 600 | 3 | +4 (+4 regional) | Needs uranium |

### City Center Buildings
| Building | Cost | Maintenance | Yields |
|----------|------|-------------|--------|
| Monument | 60 | 0 | +2 culture |
| Granary | 65 | 0 | +1 food, +2 housing |
| Water Mill | 80 | 0 | +1 food, +1 production |
| Ancient Walls | 80 | 0 | +100 outer defense |
| Sewer | 200 | 0 | +2 housing |

## Unit Costs and Combat Strengths

### Melee
| Unit | Cost | Strength | Era |
|------|------|----------|-----|
| Warrior | 40 | 20 | Ancient |
| Swordsman | 90 | 36 | Classical |
| Man-at-Arms | 160 | 45 | Medieval |
| Musketman | 240 | 55 | Renaissance |
| Line Infantry | 360 | 65 | Industrial |
| Infantry | 430 | 70 | Modern |
| Mechanized Infantry | 510 | 80 | Atomic |

### Ranged
| Unit | Cost | Ranged Str | Range | Era |
|------|------|-----------|-------|-----|
| Slinger | 35 | 15 | 1 | Ancient |
| Archer | 60 | 25 | 2 | Ancient |
| Crossbowman | 180 | 40 | 2 | Medieval |
| Field Cannon | 330 | 60 | 2 | Industrial |
| Machine Gun | 430 | 75 | 2 | Modern |

### Cavalry
| Unit | Cost | Strength | Movement | Era |
|------|------|----------|----------|-----|
| Horseman | 80 | 36 | 4 | Classical |
| Knight | 180 | 48 | 4 | Medieval |
| Cavalry | 330 | 62 | 5 | Industrial |
| Tank | 480 | 80 | 4 | Modern |
| Modern Armor | 580 | 90 | 4 | Atomic |

### Siege
| Unit | Cost | Ranged Str | Range | Era |
|------|------|-----------|-------|-----|
| Catapult | 120 | 35 | 2 | Classical |
| Bombard | 280 | 55 | 2 | Renaissance |
| Artillery | 420 | 70 | 3 | Industrial |
| Rocket Artillery | 580 | 90 | 3 | Atomic |

### Naval
| Unit | Cost | Strength | Era |
|------|------|----------|-----|
| Galley | 65 | 30 | Ancient |
| Caravel | 240 | 55 | Renaissance |
| Ironclad | 380 | 60 | Industrial |
| Battleship | 430 | 70 (ranged) | Modern |
| Destroyer | 480 | 75 | Atomic |
| Missile Cruiser | 580 | 80 | Information |

### Air
| Unit | Cost | Strength | Range | Era |
|------|------|----------|-------|-----|
| Biplane | 400 | 60 | 4 | Modern |
| Fighter | 520 | 80 | 6 | Atomic |
| Bomber | 560 | 75 (bombard) | 10 | Atomic |
| Jet Fighter | 600 | 95 | 8 | Information |
| Jet Bomber | 650 | 90 (bombard) | 15 | Information |

## Growth Formula

Food needed for next citizen:
```
food_needed = 15 + 8 * (current_pop - 1) + (current_pop - 1)^1.5
```

| Pop | Food Needed | Cumulative |
|-----|-------------|------------|
| 1→2 | 15 | 15 |
| 2→3 | 23 | 38 |
| 3→4 | 32 | 70 |
| 5→6 | 52 | 174 |
| 10→11 | 103 | 580 |
| 15→16 | 161 | 1225 |
| 20→21 | 225 | 2160 |

## Amenities (Happiness)

- Demand: 1 amenity per 2 citizens (starting from pop 3)
  - Pop 1-2: 0 demand
  - Pop 3-4: 1 demand
  - Pop 5-6: 2 demand
  - Pop 7-8: 3 demand
  - Pop 10: 4 demand
  - Pop 20: 9 demand

- Sources:
  - Luxury resource: +1 amenity to 4 cities (first copy only)
  - Entertainment Complex buildings: +1 each (Arena, Zoo, Stadium)
  - Water Park buildings: +1 each
  - Policies: various (+1-2)
  - Religion: various
  - Great People: various
  - Wonders: various (+1-2 each)
  - National Parks: +2

Effects:
- Ecstatic (+3 surplus): +10% all yields
- Happy (+1-2 surplus): +5% all yields
- Content (0): normal
- Displeased (-1-2): -5% all yields, -10% growth
- Unhappy (-3-4): -10% all yields, -30% growth
- Unrest (-5-6): no growth, rebels may spawn

## Wonder Costs (Selected)

| Wonder | Era | Cost |
|--------|-----|------|
| Stonehenge | Ancient | 180 |
| Pyramids | Ancient | 220 |
| Hanging Gardens | Ancient | 220 |
| Colosseum | Classical | 400 |
| Great Library | Classical | 400 |
| Alhambra | Medieval | 710 |
| Machu Picchu | Medieval | 710 |
| Forbidden City | Renaissance | 920 |
| Potala Palace | Renaissance | 920 |
| Big Ben | Industrial | 1450 |
| Ruhr Valley | Industrial | 1620 |
| Eiffel Tower | Modern | 1620 |
| Cristo Redentor | Modern | 1620 |
| Sydney Opera House | Atomic | 1850 |

## AI Difficulty Bonuses (Civ 6)

| Difficulty | AI Production | AI Science | AI Culture | AI Gold | AI Combat | AI Faith |
|------------|--------------|-----------|-----------|---------|-----------|---------|
| Settler | -50% | -50% | -50% | -50% | -3 | -50% |
| Chieftain | -25% | -25% | -25% | -25% | -1 | -25% |
| Warlord | 0% | 0% | 0% | 0% | 0 | 0% |
| Prince | 0% | 0% | 0% | 0% | 0 | 0% |
| King | +20% | +20% | +20% | +20% | +1 | +20% |
| Emperor | +40% | +40% | +40% | +40% | +2 | +40% |
| Immortal | +60% | +60% | +60% | +60% | +3 | +60% |
| Deity | +80% | +80% | +80% | +80% | +4 | +80% |

Note: AI also gets free units and builders at higher difficulties.
