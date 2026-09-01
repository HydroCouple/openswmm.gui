# Bellinge 2D stall — diagnosis, 2026-07-28

**Symptom (reported):** `BellingeSWMM_v021_nopervious.inp` "does not appear to be
moving forward — average timestep remains at zero".

**Model as run:** coupled 1D/2D, external mesh `BellingeSWMM_v021_nopervious.2dm`
regenerated from the GUI at 23:31 (228,410 cells / 114,722 vertices),
1,020 node→cell coupling rows over 955 distinct cells (65 shared-cell rows).
`[2D_OPTIONS]`: `CELL_CLOSURE FLAT`, `MIN_TIMESTEP 0.25`, `PRECONDITIONER AMG`,
`COUPLING_INTERVAL 10`, `COUPLING_WINDOW -1`. `[OPTIONS] THREADS 2`.

---

## 1. Root cause of the zero timestep — `MIN_TIMESTEP 0.25` in the .inp

The 2D solver never took a single step. From the run's report:

```
2D Solver Statistics
  Internal BDF Steps .......  0     Newton Iterations ....  0
  Nonlinear RHS Evals ......  0     Krylov Iterations ....  0
  Frozen (Failed) Windows ..  43
  Avg Internal Step (s) ....  0.0000
```

2D continuity confirms an inert surface: initial volume == final volume, every
flux exactly `0.000`. The 7 m 44 s of wall time was the 1D routing alone,
pinned at `MINIMUM_STEP` with **98.72 % of steps not converging** and
**100 % of inflow leaving as flooding loss** (continuity −12.7 %).

Headless run, line 12 of the log:

```
[cvHandleFailure] At t = 0 and h = 0.25, the corrector convergence test
failed repeatedly or with |h| = hmin.
```

`h = 0.25` is the model's own `MIN_TIMESTEP`. This is the failure storm
root-caused on 2026-07-27: the corrector cannot converge at wetting-front kinks
at that step, and an explicit floor forbids CVODE from cutting further, so it
fails at the first step of every window. Engine commit `e8c0505b` promoted
`MIN_TIMESTEP 0` (plus tangent-exact PC / partial windows) to **defaults** — but
**an explicit `MIN_TIMESTEP` line in the .inp overrides the default**. This is
the trap called out verbatim in the campaign notes.

**Fix:** delete the `MIN_TIMESTEP` line from `[2D_OPTIONS]`.
**Verified:** with the line removed, the identical model ran 4+ minutes with
**zero** CVODE failures where the original failed at t = 0 and never recovered.

---

## 2. ENGINE BUG — the Kokkos/GPU backend ignores `CELL_CLOSURE`

`grep -rn "cell_closure\|CellClosure" src/engine/2d/gpu/` → **no matches.**
The GPU solver never reads the option and always applies the VFR closure.

`CvodeKokkosSurfaceSolver::seedVolumeFromHead()` (gpu/CvodeKokkosSurfaceSolver.cpp:165):

```cpp
// SurfaceRouter2D sets head[i] = tri_cz[i] ⇒ V = 0     ← true only for FLAT
V[i] = m.tri_area[i]
     * vfrMeanDepthFromEta(z1, z2, z3, state_host_->head[i], eps);  ← always VFR
```

`downloadState()` (same file, ~line 183) reconstructs η with `vfrEtaFromMeanDepth`
unconditionally too. The serial solver does branch correctly —
`solver/CvodeSurfaceSolver.cpp:82-97` returns `A·(η − tri_cz)` for FLAT, which is
exactly 0 at the dry anchor.

**Consequence with `CELL_CLOSURE FLAT` + the (default-on) GPU plugin:**
`SurfaceRouter2D` seeds `head[i] = tri_cz[i]` (the FLAT dry anchor); the Kokkos
solver evaluates the VFR planar-bed integral at that head; every cell starts
holding the water below its own centroid.

Measured on this model: reported `2D Initial Stored Volume = 2,649,680.68 m³`
with zero rainfall and zero inflow. An independent exact integral of
`∫ max(0, z_centroid − z_bed) dA` over the mesh gives **2,649,680.68 m³** —
ratio 1.0000. (For contrast `∫A·(z_mean − z_min)` = 14.8 M m³ and
`∫A·(z_max − z_min)` = 29.7 M m³, so the match is not coincidental.)

This is not only a seeding slip: the router honours FLAT for head↔depth while
the solver runs VFR for the whole simulation, so the two disagree on the closure
used for coupling, the CFL hint and output. **The same model gives different
answers on the CPU and GPU paths.**

**Options:** set `CELL_CLOSURE VFR` (router and solver then agree; the VFR dry
anchor round-trips to exactly V = 0) · disable the GPU plugin (serial path
honours FLAT) · fix the engine by branching `seedVolumeFromHead()` and
`downloadState()` on `opts_host_->cell_closure`, and reject/warn on an
unsupported combination rather than silently substituting a closure.

---

## 3. BUILD BUG — duplicate OpenMP runtime aborts every 2D run

`install/Darwin/lib/libopenswmm_gpu_omp.dylib` links
`/opt/homebrew/opt/libomp/lib/libomp.dylib` by **absolute path**, while
`libopenswmm.engine.6.dylib` uses `@rpath/libomp.dylib` → `install/Darwin/bin/libomp.dylib`.
Two OpenMP runtimes load as soon as Kokkos initialises:

```
OMP: Error #15: Initializing libomp.dylib, but found libomp.dylib already initialized.
```

Aborts (`SIGABRT`) the CLI and the GUI alike, at `SimulationRunner::start()` →
`SurfaceRouter2D::initialize` → `Kokkos::initialize`. `KMP_DUPLICATE_LIB_OK=TRUE`
only converts it into a `SIGSEGV` in `__kmp_suspend_initialize_thread`.

The GUI bundle had the identical problem
(`SWMMVis.app/Contents/Frameworks/libopenswmm_gpu_omp.dylib`).

**Applied as a temporary artefact patch** (both trees, **lost on next build**):

```
install_name_tool -add_rpath @loader_path/../bin <plugin>          # install tree only
install_name_tool -change /opt/homebrew/opt/libomp/lib/libomp.dylib \
                          @rpath/libomp.dylib <plugin>
codesign --force --sign - <plugin>
```

**Permanent fix belongs in the engine's deploy step**, next to whatever already
rewrites the engine dylib's libomp reference — the GUI inherits the install tree,
so fixing it there fixes both.

---

## 4. Coupling exchange area — 781 of 1,020 nodes flagged

The engine warns for **781** coupled nodes:

> `2D-coupled node 'F74F370' has exchange AREA 2.000 m² — 10x the largest
> connected conduit area (0.1963 m²). The orifice can inject far more than the
> pipe can convey; expect the node to fill and spill back each window.`

2.0 m² is `mesh::kCellCouplingDefaultArea`, the mapper default set by plan
decision 2 (2026-07-28). Bellinge's pipes are 0.126–0.196 m². Prior benchmarking
put the useful coupling area near 0.3. Worth revisiting the default, or deriving
it from the largest connected conduit area instead of a constant.

---

## 5. Runtime expectation

The validated campaign numbers (48 h solo 358 s; 48 h coupled 5,516 s) were
measured on a **13,114-cell** mesh (`bench_3min/mesh_A1.0.2dm`). Tonight's mesh
is **228,410 cells — 17.4×** — roughly 12 m resolution over 34.89 km² instead of
~52 m. The mesh itself is healthy (min cell 20.0 m², no slivers, no NaN Z; the
junctions-OFF default removed the old 0.04 m² sliver problem), it is simply much
finer. A 5-minute target has to be set against a mesh of comparable size, or the
resolution reduced.

Other levers from the campaign, not yet re-tested here: `THREADS 4` (8 threads
barrier-thrash), `COUPLING_WINDOW 30` (−28 % on the 8 h probe with identical
exchange).

---

## Reproduction artefacts (all under `~/Downloads/7_SWMM/`)

| File | What |
|---|---|
| `BellingeSWMM_diag_30min.inp` / `.log` | 30-min cut of the model as-shipped — fails at t = 0, h = 0.25 |
| `BellingeSWMM_diag_hmin0_30min.inp` / `.log` | same, `MIN_TIMESTEP` deleted + `THREADS 4` — zero failures |
| `BellingeSWMM_v021_nopervious.rpt` | the original 7 m 44 s run showing all-zero 2D solver stats |
