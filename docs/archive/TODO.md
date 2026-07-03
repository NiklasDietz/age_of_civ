# Age of Civilization -- Feature TODO

## Status Legend
- [ ] Not started
- [x] Done

---

## 7. Game Experience Polish (Current)

### High Impact
- [x] **City capture mechanics** -- Military units capture enemy cities on move. Owner changes, pop -1, queue cleared, territory transferred
- [x] **Unit maintenance costs** -- 1g per unit above free count (= city count). Bankruptcy disbands random unit. Building maintenance deducted too
- [x] **Citizen tile assignment** -- City detail screen: toggle worked/unworked tiles, constrained by population
- [x] **Tile purchase with gold** -- City detail screen: buy adjacent unowned tiles for 50 + 25*N gold

### Medium Impact
- [x] **Turn year display** -- "Turn 47 (1600 BC)" with 5 era brackets from 4000 BC to 2050 AD
- [x] **Animated unit movement** -- 0.2s interpolation between tiles. animFrom/animTo/animProgress fields
- [x] **Next unit cycling** -- Tab key finds next unit with movement remaining, selects and centers camera
- [x] **Map city labels** -- City names drawn above hexes with player color, zoom-independent
- [x] **Territory color overlay** -- Subtle player-colored polygon overlay (alpha 0.10) on owned tiles
- [x] **Worker automation** -- "Auto-Improve" toggle for builders. Auto-builds improvements or pathfinds to improvable tiles
- [x] **City connection gold bonus** -- BFS through road tiles to capital. +3 gold per connected city per turn

---

## Previously Completed (Sections 1-6)

### 1. Critical Gameplay Features
- [x] Trade Deal UI
- [x] Diplomacy Screen
- [x] Production Picker (full) with tech gating
- [x] Tech gating for units and buildings
- [x] Tech completion notification + auto-pick prompt
- [x] Turn event log

### 2. Important Content
- [x] City names per civilization (12 historical names each)
- [x] Unit upgrade path (8 upgrade paths, U key)
- [x] City ranged bombardment (Walls auto-bombard)
- [x] Natural wonders (6 types, golden diamond rendering)
- [x] City-states (8 city-states, 6 types, envoy/suzerain)

### 3. UI/UX Features
- [x] Unit action panel (context-sensitive buttons)
- [x] City yield breakdown (per-tile yields, building bonuses, totals)
- [x] Research progress bar in HUD
- [x] Production progress bar in HUD
- [x] Keyboard shortcuts help (F1)
- [x] Map resource icons (color-coded dots)

### 4. Economic Features
- [x] Trade route creation UI
- [x] Market price history viewer
- [x] Embargo/sanctions

### 5. Gameplay Polish
- [x] Auto-explore for scouts
- [x] Unit sleep/skip turn
- [x] Fortify action
- [x] Multi-turn movement persistence
- [x] Ranged attack visualization
- [x] Combat preview tooltip
- [x] Undo last action (Z key)

### 6. Religion System
- [x] Faith accumulation
- [x] Found religion with beliefs (16 beliefs, 4 types)
- [x] Religious units (Missionary, Apostle, Inquisitor)
- [x] Religious victory (>50% cities converted)

### Core Systems (Phases 0-9)
- [x] All 10 phases implemented (ECS, map, rendering, UI, economy, monetary, tech, combat, diplomacy, AI, save/load)
- [x] 148 files (88 headers + 60 source files)
