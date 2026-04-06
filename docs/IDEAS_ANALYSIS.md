# Ideas Analysis -- Feasibility and Implementation Plan

Based on `ideas.txt` and geological research.

---

## Idea 1: Geologically Realistic Resource Placement

> "The placement of resources should be done not pure random, but based on
> how it's done in the real world. Continental drift needs to be accounted
> for, e.g., create a base world and then transform it to today's view."

### Feasibility: YES -- Very achievable

This is a map generation enhancement. The current `MapGenerator` uses pure
noise for terrain + random resource scattering. We can replace it with a
geologically motivated system.

### How Real Geology Works (Simplified)

Resources aren't randomly distributed -- they form at specific geological settings:

| Resource | Geological Setting | Why |
|---|---|---|
| **Iron** | Old continental shields, banded iron formations | Ancient ocean chemistry deposited iron 2+ billion years ago |
| **Copper, Gold** | Convergent plate boundaries (volcanic arcs) | Magma carries dissolved metals upward from subduction |
| **Coal** | Former tropical swamp basins | Dead vegetation compressed over millions of years |
| **Oil** | Sedimentary basins with cap rock | Marine organisms buried, heated, trapped under impermeable rock |
| **Aluminum** | Tropical weathering zones (bauxite) | Intense rain leaches everything except aluminum oxide |
| **Tin** | Granitic intrusions near plate boundaries | Crystallizes from magma as it cools |
| **Diamonds** | Deep continental interiors (cratons) | Formed deep in mantle, brought up by volcanic pipes |
| **Uranium** | Granitic continental crust, sandstone basins | Concentrated by groundwater in porous rock |

### Continental Drift's Effect

When Pangaea existed (~250 million years ago), gold deposits in West Africa
and South America were ADJACENT. After drift, they're separated by the Atlantic
but the geology is continuous. Similarly, coal belts span from the US through
Europe because they were one tropical swamp belt during the Carboniferous.

### Implementation Plan

#### Phase 1: Tectonic Zone Generation

During map generation, BEFORE placing terrain:

1. **Define 3-5 tectonic plates** as polygonal regions on the hex grid
2. **Mark plate boundaries** by type:
   - Convergent (mountains form, volcanoes, copper/gold deposits)
   - Divergent (rift valleys, copper/chromium)
   - Transform (earthquake zones, minor mineral deposits)
3. **Mark plate interiors** as:
   - Old continental shields (iron, diamonds)
   - Sedimentary basins (coal, oil)
   - Volcanic hotspots (gold, geothermal)

#### Phase 2: Terrain From Tectonics

Instead of pure noise terrain:
- **Convergent boundaries** -> mountain ranges
- **Divergent boundaries** -> rift valleys, mid-ocean ridges
- **Continental interiors** -> plains/grassland
- **Sedimentary basins** -> low-lying plains, swamps
- **Volcanic zones** -> hills with occasional mountains

Add noise for variation, but the large-scale structure comes from tectonics.

#### Phase 3: Resource Placement From Geology

For each tile, compute a "geological score" per resource type:

```
ironScore = shieldBonus(0.8) * elevationFactor * noiseVariation
copperScore = convergentBoundaryBonus(0.7) * volcanicFactor * noiseVariation
oilScore = sedimentaryBasinBonus(0.9) * lowElevationFactor * noiseVariation
goldScore = volcanicBonus(0.6) * convergentBonus(0.3) * noiseVariation
coalScore = swampHistoryBonus(0.8) * temperatureFactor * noiseVariation
```

Place resource where the highest-scoring resource type exceeds a threshold.

#### Phase 4: Optional "Drift History" Layer

For extra realism:
1. Generate the map as if it were Pangaea (one landmass)
2. Define resource deposits on Pangaea
3. "Drift" the landmass apart into the current map layout
4. Resources stay at their drifted positions

This is achievable with a simple affine transformation:
- Split Pangaea into 2-4 chunks along defined "fault lines"
- Translate/rotate each chunk to its final position
- Deposit resources on the original Pangaea, map them to final positions

This would create realistic resource clustering: two continents that were
once adjacent share similar mineral belts on their facing coastlines.

### Estimated Complexity

- Phase 1-3: **2-3 days of work** (new MapGenerator methods)
- Phase 4 (drift): **1-2 extra days** (coordinate remapping)
- Total: **Medium complexity**, no architectural changes needed

---

## Idea 2: Environment Impact on Production Buildings

> "The environment should have an impact on production buildings, similar
> to Civ 6 but with more effects."

### Feasibility: YES -- Straightforward

This is a modifier system on top of existing production.

### Current State

The production system calculates output as:
```
output = recipe.outputAmount * infrastructureBonus * (other modifiers)
```

There's no terrain-based modifier yet.

### Implementation Plan

#### Step 1: Define Environment Modifiers per Building

Each building gets a terrain/feature affinity:

| Building | Terrain Bonus | Terrain Penalty |
|---|---|---|
| **Forge** | Hills +20%, Mountain adj. +10% | Desert -15% |
| **Workshop** | Forest +15% (lumber), Plains +10% | Snow -20% |
| **Refinery** | Coast +10% (oil transport), Desert +15% (oil fields) | Mountain -20% |
| **Factory** | River adj. +15% (water power), Plains +10% | Tundra -15% |
| **Electronics Plant** | Campus adj. +10%, Coast +5% | Jungle -10% |
| **Farm** (improvement) | Floodplains +50%, River adj. +25%, Grassland +10% | Desert -50%, Tundra -30% |
| **Mine** (improvement) | Hills +30%, Mountain adj. +20% | Grassland -20% |
| **Harbor** | Coast required, Ocean adj. +20% | N/A |

#### Step 2: Environmental Effects Beyond Terrain

| Environmental Factor | Effect |
|---|---|
| **River adjacency** | +1 food, +1 gold to tile; +15% factory production (water power) |
| **Mountain adjacency** | +1 science (observatories); +10% mining output |
| **Forest nearby** | +1 production to Workshop; provides lumber for construction |
| **Jungle nearby** | +1 food; -10% production (difficult terrain) |
| **Desert** | -30% food; +20% oil extraction; solar power bonus in modern era |
| **Coast** | +1 gold (trade); +10% to Harbor buildings |
| **Volcanic soil** (near natural wonders) | +2 food (fertile volcanic ash) |
| **Flood plains** | +3 food; +10% farm production; but flood disaster risk |

#### Step 3: Implementation

Add a function:
```cpp
float computeEnvironmentModifier(const HexGrid& grid, EntityId cityEntity,
                                   BuildingId buildingId);
```

This checks the city's tile and its 6 neighbors for terrain/feature bonuses
relevant to the building type. Applied during production calculation.

### Estimated Complexity

- **1-2 days of work** (modifier function + data table)
- No architectural changes, just a new modifier in the production pipeline

---

## Combined Vision

Both ideas work together beautifully:

1. **Tectonic generation** creates realistic terrain AND resource placement
2. **Environment modifiers** mean that WHERE you build matters
3. A city on volcanic soil near mountains with a river is naturally great for
   farming + mining + factory production
4. A city in a sedimentary basin is naturally great for oil extraction
5. Players must read the terrain to decide WHAT to build WHERE

This creates another layer of emergent specialization: not just "what resources
do I have?" but "what is my land good at?" -- like real nations.

---

## References

- Plate tectonics and mineral deposits: convergent boundaries create
  copper/gold, continental shields have iron, sedimentary basins have coal/oil
- Continental drift: resources that were adjacent on Pangaea are now separated
  across continents (e.g., West Africa/Brazil gold belt)
- Red Blob Games hex map generation: procedural terrain from tectonic simulation
- Civilization VI: adjacency bonuses for districts (Campus +science near mountains,
  Harbor +gold on coast) -- our system would be deeper with per-building modifiers
