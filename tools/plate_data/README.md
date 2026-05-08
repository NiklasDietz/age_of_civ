# Plate-tectonics datasets for sim calibration

Real-Earth GPlates rotation files used to validate `MapGenerator`
parameters (plate count, motion speed, rift cadence). Datasets are
~496 MB total; excluded from git via `.gitignore`. Re-download with the
URLs below.

## Downloads (free, CC-BY)

```
# Matthews et al. 2016 — Late Paleozoic to Present (410-0 Ma) — 11 MB
curl -L -o matthews2016.zip "https://zenodo.org/api/records/10526157/files/Matthews_etal_2016_GPC.zip/content"

# Muller et al. 2022 — mantle reference frame (1000-0 Ma) — 12 MB
curl -L -o muller2022.zip "https://zenodo.org/api/records/13636799/files/Muller_etal_2022_SE_v1.2.4.zip/content"

# Muller 2022 animations (optional) — 43 MB
curl -L -o muller2022_anim.zip "https://zenodo.org/api/records/13636799/files/Animations.zip/content"

# Scotese PALEOMAP 2016 (1100-0 Ma) — 56 MB
curl -L -o scotese2016.zip "https://zenodo.org/api/records/10596610/files/Scotese_PaleoAtlas_2016_v3.zip/content"

for z in *.zip; do unzip -q "$z" -d "${z%.zip}/"; done
```

## Usage

`python3 analyze.py` parses GPlates rotation files (`.rot`) and prints:

- Active plate-ID count per geological time
- Plate motion speed distribution (deg/Ma)
- Plate birth/death events per 50 Ma (rift / merge cadence)
- Recommendation block comparing real-Earth stats to current sim parameters

Polygon (`.gpml`) files require `pygplates` for full analysis — not used
here. Rotation files alone capture plate count + motion which is the
primary calibration target.

## Findings (informs `MapGenerator` calibration)

- Real Earth has **~7-15 major plates** active throughout Phanerozoic.
- GPlates models track **~700-900 plate IDs** but most are sub-plate
  terranes / microcontinents / deformed regions, not whole plates.
- Plate motion **median 0.1 deg/Ma** (~1 cm/yr), **95th percentile
  8-18 deg/Ma** (~10-20 cm/yr).
- Plate birth/death events **cluster** at major reorganizations
  (Pangaea breakup ~250 Ma, Atlantic opening ~150 Ma, India-Asia
  collision ~50 Ma) -- not uniform.
- Sim's current 9-14 plates with cap=13 falls **within Earth's
  major-plate range**.
- Sim drift speed is ~3x Earth (faster pace acceptable for game
  timescale).
