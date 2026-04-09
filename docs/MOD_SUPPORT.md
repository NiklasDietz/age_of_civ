# Age of Civilization -- Mod Support Guide

## Overview

Age of Civilization supports data-driven modding through JSON definition files. Mods can override or extend units, buildings, technologies, civilizations, production recipes, and more without recompiling the game.

## How It Works

### Mod Structure

A mod is a directory under `mods/` with the following structure:

```
mods/
  my_mod/
    mod.json           -- Mod metadata (name, author, version, dependencies)
    units.json         -- Custom or overridden unit definitions
    buildings.json     -- Custom or overridden building definitions
    techs.json         -- Custom or overridden technology definitions
    civs.json          -- Custom or overridden civilization definitions
    recipes.json       -- Custom or overridden production recipes
    goods.json         -- Custom or overridden good definitions
    governments.json   -- Custom government types and policies
    wonders.json       -- Custom world wonders
    city_states.json   -- Custom city-states
    great_people.json  -- Custom great people
```

All files are optional -- a mod only needs to include files for the data it modifies.

### mod.json (Required)

```json
{
    "name": "My Awesome Mod",
    "author": "Your Name",
    "version": "1.0.0",
    "description": "Adds new civilizations and rebalances economics",
    "gameVersion": "0.1.0",
    "dependencies": [],
    "loadOrder": 100
}
```

- `loadOrder`: Lower values load first. Mods with higher values override earlier mods.
- `dependencies`: Array of mod names that must be loaded before this one.
- `gameVersion`: Minimum game version required.

### Data Format: Units (units.json)

```json
{
    "units": [
        {
            "id": 50,
            "name": "Samurai",
            "class": "Melee",
            "maxHP": 120,
            "combatStrength": 40,
            "rangedStrength": 0,
            "range": 0,
            "movement": 2,
            "productionCost": 160,
            "replacesUnit": 10,
            "requiredCiv": "Japan"
        }
    ]
}
```

- `id`: Must be unique across all mods. Use IDs >= 50 for custom units.
- `replacesUnit`: If set, this unit replaces the standard unit with that ID for the specified civilization.
- `requiredCiv`: If set, only this civilization can build this unit.

### Data Format: Buildings (buildings.json)

```json
{
    "buildings": [
        {
            "id": 50,
            "name": "Onsen",
            "district": "CityCenter",
            "productionCost": 80,
            "maintenance": 1,
            "productionBonus": 1,
            "scienceBonus": 0,
            "goldBonus": 2,
            "amenityBonus": 1,
            "replaces": "Hospital",
            "requiredCiv": "Japan"
        }
    ]
}
```

### Data Format: Technologies (techs.json)

```json
{
    "techs": [
        {
            "id": 50,
            "name": "Bushido",
            "era": 2,
            "researchCost": 120,
            "prerequisites": ["Bronze Working"],
            "unlocksBuildings": ["Onsen"],
            "unlocksUnits": ["Samurai"]
        }
    ]
}
```

### Data Format: Civilizations (civs.json)

```json
{
    "civilizations": [
        {
            "id": 12,
            "name": "Aztec Empire",
            "leader": "Montezuma II",
            "abilityName": "Flower Wars",
            "abilityDescription": "+1 amenity for each unique luxury resource improved.",
            "modifiers": {
                "productionMultiplier": 1.0,
                "goldMultiplier": 1.0,
                "scienceMultiplier": 1.0,
                "cultureMultiplier": 1.1,
                "combatBonus": 2
            },
            "uniqueUnit": "Eagle Warrior",
            "uniqueBuilding": "Tlachtli",
            "cityNames": ["Tenochtitlan", "Texcoco", "Tlacopan", "Cholula"]
        }
    ]
}
```

### Data Format: Production Recipes (recipes.json)

```json
{
    "recipes": [
        {
            "id": 50,
            "name": "Forge Obsidian Tools",
            "inputs": [
                {"good": "Stone", "amount": 2, "consumed": true}
            ],
            "outputGood": "Tools",
            "outputAmount": 2,
            "requiredBuilding": "Workshop",
            "turnsToProcess": 1
        }
    ]
}
```

### Data Format: Goods (goods.json)

```json
{
    "goods": [
        {
            "id": 150,
            "name": "Obsidian",
            "category": "RawStrategic",
            "basePrice": 15,
            "isStrategic": true,
            "priceElasticity": 0.4
        }
    ]
}
```

### Data Format: Governments (governments.json)

```json
{
    "governments": [
        {
            "id": 9,
            "name": "Technocracy",
            "requiredCivic": 14,
            "slots": {"military": 1, "economic": 3, "diplomatic": 1, "wildcard": 2},
            "bonuses": {
                "scienceMultiplier": 1.2,
                "productionMultiplier": 1.1,
                "cultureMultiplier": 0.9
            },
            "corruptionRate": 0.03,
            "uniqueAction": "Research Grant"
        }
    ],
    "policies": [
        {
            "id": 22,
            "name": "Digital Infrastructure",
            "slotType": "Economic",
            "requiredCivic": 14,
            "effects": {
                "scienceMultiplier": 1.15,
                "productionPerCity": 1
            }
        }
    ]
}
```

## Loading Process

1. **Discovery**: On startup, the game scans `mods/` for directories containing `mod.json`.
2. **Validation**: Each mod's `mod.json` is parsed. Dependencies are checked.
3. **Ordering**: Mods are sorted by `loadOrder` (lower first). Dependencies are loaded before dependents.
4. **Loading**: For each mod (in order):
   a. Parse each data file (units.json, buildings.json, etc.)
   b. For each entry: if `id` matches an existing definition, **override** it. Otherwise, **append** as new.
5. **Validation**: After all mods are loaded, cross-reference checks run (e.g., ensure referenced techs exist, no duplicate IDs).

## Override vs Append

- **Override**: If a mod defines a unit with `"id": 0` (same as Warrior), it replaces the Warrior definition entirely.
- **Append**: If a mod defines a unit with `"id": 50` (no existing unit), it's added as a new unit type.
- **Partial Override**: Use `"extends": "Warrior"` to copy the Warrior definition and only change specific fields.

## Best Practices

1. **Use high IDs** (50+) for new content to avoid conflicts with future base game additions.
2. **Declare dependencies** if your mod requires another mod's content.
3. **Test incrementally**: Add one definition at a time and verify it loads correctly.
4. **Document your changes**: Include a README.md in your mod directory.
5. **Don't modify core game files**: Always create a mod instead.

## Limitations

- Mods cannot add new game mechanics or change game logic (code mods).
- Mods cannot add custom graphics/textures (future feature).
- Mods cannot modify the UI layout (future feature).
- Maximum 256 units, 256 buildings, 256 techs, 32 civilizations, 256 goods.

## Example: Total Conversion Mod

```
mods/
  ancient_world/
    mod.json        -- name: "Ancient World", loadOrder: 1
    units.json      -- Override ALL 30 units with ancient-only types
    techs.json      -- Replace tech tree with ancient-only techs
    civs.json       -- Replace 12 civs with ancient civilizations
    buildings.json  -- Replace buildings with era-appropriate ones
    recipes.json    -- Remove modern production chains
```

This effectively creates a new game within the engine.

## API Reference (for C++ modders)

The `ModLoader` class in `include/aoc/mod/ModLoader.hpp` provides:

```cpp
// Load all mods from the mods/ directory
static bool loadAllMods(const std::string& modsDirectory);

// Load a specific mod
static bool loadMod(const std::string& modDirectory);

// Check if a mod is loaded
static bool isModLoaded(const std::string& modName);

// Get load order
static std::vector<std::string> getLoadOrder();
```

All game definition tables (UNIT_TYPE_DEFS, BUILDING_DEFS, etc.) are populated
from the static constexpr arrays at compile time, then overridden by mod data
at runtime during the loading phase. The game always uses the runtime tables
for gameplay, so mods transparently affect all systems.
