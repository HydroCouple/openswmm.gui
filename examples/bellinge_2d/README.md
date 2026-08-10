# Bellinge 2D Urban Flood Model

Coupled 1D/2D example for the Bellinge catchment (Odense, Denmark), derived
from the open Bellinge dataset published by the University of Southern
Denmark / VCS Denmark.

| File | Purpose |
|---|---|
| `BellingeSWMM_v021_nopervious.inp` | SWMM model: 1D drainage network + `[2D_*]` sections |
| `BellingeSWMM_v021_nopervious.oswp` | GUI project (styling, legend, mesh layer state) |
| `BellingeSWMM_v021_nopervious.2dm` | 2D overland-flow mesh referenced by the `.inp` |
| `rg_bellinge_Jun2010_Aug2021.dat` | Rain-gage record (two gages, Jun 2010 – Aug 2021) |
| `output_SRTMGL1.tif` (+`.aux.xml`) | SRTM DEM of the catchment — not referenced by the project; add it as a terrain/raster layer if desired |
| `example.json` | Display metadata for the Welcome screen |

All paths inside the `.inp`/`.oswp` are relative, so the folder is fully
self-contained. Opening this example from the Welcome screen always copies
the folder to a location you choose first — simulation results (`.out`,
`.rpt`, `.2d.h5`) are written next to the copied `.inp`, never into the
bundled baseline.
