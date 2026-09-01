# EA 2D Benchmark Subset — Execution Plan (Tests 1, 2, 4, 8A)

**Status:** DoD package for owner review (Task 5, 2026-07-29). Not committed.
**Scope:** the four-test subset gating the explicit marcher (plan §8.6); full suite is P5.
**Reference:** UK Environment Agency report **SC120002** — *Benchmarking the latest
generation of 2D hydraulic modelling packages* (2013). Pass criterion for every test:
**results fall within the published inter-model spread** of the report's participating
packages (TUFLOW, InfoWorks ICM, JFLOW, Flowroute, ISIS 2D, MIKE 21, SOBEK…), not
agreement with any single package.

## 1. Model sources

| Test | Name | Domain / data | What it stresses |
|---|---|---|---|
| 1 | Flooding a disconnected water body | ~700 m × 100 m sloping domain with a bund; inflow hydrograph at one end | Wetting/drying, momentum over an obstruction, final steady levels (well-balancedness) |
| 2 | Filling of floodplain depressions | 2,000 m × 2,000 m synthetic DEM, ~0.5 m depressions, 20 m grid equivalent | Mass conservation + wet/dry over irregular topography; final distribution of ponded water |
| 4 | Speed of flood propagation over an extended floodplain | ~1,000 m × 2,000 m flat floodplain, point inflow hydrograph | Front celerity + peak level/timing at gauges — the classic LI validity check |
| 8A | Rainfall and point-source surface flow in urban areas | ~0.4 km² urban DEM (~2 m data), design rainfall hyetograph + point surcharge inflow | Rain-on-grid over real urban microtopography — our production regime (thin films, curbs, roads) |

Data: the DEM rasters, inflow/rain time series, gauge locations, and per-package result
spreads are distributed with SC120002 (Defra/EA download; also mirrored with the TUFLOW
and LISFLOOD-FP validation suites). **Owner action:** confirm the download source; the
data are redistributable for validation use but we will NOT commit rasters — they live
under `tools/bench_2d/ea/data/` (gitignored), fetched by a documented script.

## 2. Mesh generation via the harness

- New script `tools/bench_2d/ea/make_ea_case.py` (committable): DEM → `.2dm` mesh +
  `.inp` per test. Triangulated meshes at the report's stated resolution equivalents
  (test 1: 10 m; test 2: 20 m; test 4: 25 m² nominal cell target; 8A: 2 m); uniform
  Manning n per the report (per-test values, e.g. 8A n=0.02 roads / 0.05 elsewhere
  via the mesh material field).
- Inflows: tests 1/4 point hydrograph → a 1D dummy node coupled at the inflow cell
  (uses the production coupling path) or a `[2D_BOUNDARY]` inflow BC — prefer the BC
  (exercises `boundaryEdgeFlux`, no 1D artifacts). Test 8A uses BOTH: rain-on-grid
  (`RAINFALL_MODE` gage → NN weights degenerate to uniform) + the point source as a
  coupled surcharge node per the report definition.
- `[2D_OPTIONS]`: `INTEGRATOR EXPLICIT`, `LTS_TIERS 4`, `MAX_TIMESTEP 60`, defaults
  otherwise; serial pinned by `run_one.py` (`OPENSWMM_2D_BACKEND=cpu`, `THREADS 1`).

## 3. Outputs and pass gates

Per test, `run_one.py` extension writes gauge time series (depth/level/velocity at the
report's gauge coordinates, sampled from cell/vertex heads) to
`tools/bench_2d/ea/out/<test>/gauges.csv` + a `results.csv` row.

- **Test 1:** final steady levels at gauges within the inter-model level spread
  (report spread ≈ ±0.05 m); pond 2 must NOT wet through the bund before overtopping.
- **Test 2:** final ponded-volume distribution — level at each of the 16 depression
  gauges within spread; global mass error ≤ 0.1 % (our own stricter gate).
- **Test 4:** arrival time (first 0.05 m depth) and peak level at each gauge within the
  published envelope; this is the LI-scheme's home test (LISFLOOD-FP lineage passes).
- **Test 8A:** peak depth + timing at the report's 9 gauges within spread; zero
  negative volumes; active-set + tier telemetry recorded (urban thin-film stress test).

Non-gating: wall time per test recorded in `results.csv` (they are small — seconds).

## 4. Effort / order

1. `make_ea_case.py` skeleton + test 4 (simplest DEM, pure 2D BC) — validates the
   gauge-sampling machinery.
2. Tests 1, 2 (synthetic DEMs, quick).
3. Test 8A last (needs the coupled point source + rain-on-grid plumbing together).

Estimated 1–2 days once the datasets are in hand. All scripts committable under
`tools/bench_2d/ea/`; data and outputs stay untracked (only `results.csv` is committed
per the artifact ground rules).
