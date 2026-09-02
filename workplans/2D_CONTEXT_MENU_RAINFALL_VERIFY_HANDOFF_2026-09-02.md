# Verify handoff — 2D context-menu plotting + per-cell rainfall series (2026-09-02)

Two commits were authored without a compiler available (the authoring session's
sandbox had no cmake/Qt). Engine sources passed `g++ -fsyntax-only`; the GUI was
**not compiled at all**. Your job: build both, run the suites, fix anything that
does not compile or fails, and do a manual smoke check. Keep fixes minimal and
inside the scope below.

## Commits under review

| Repo | Commit | Subject |
|------|--------|---------|
| `openswmm.engine` | `f1cdaa31` | feat(2d): write per-cell cumulative rainfall volume (Mesh2_face_rain_cum) |
| `openswmm.gui`    | `75dc2f2`  | feat(2d): context-menu plotting for cells/edges/vertices + cell rainfall series |

Both trees also carry **unrelated uncommitted work** (engine: S4 transport;
GUI: Offset Mode toggle). Do not touch or commit those hunks. `git stash` is
NOT safe here (it would sweep them); work on top of them as-is.

Note: `.git/_stale_*` files in both repos are lock/tmp-object leftovers the
sandbox could not unlink. They are inert; delete them (`rm .git/_stale_*`).

## What changed (read the diffs first)

Engine (`git show f1cdaa31`):
- `SurfaceRouter2D::rain_cum_` (m³/cell), zeroed in `initialize()`, accumulated
  in `accumulateMassBalance()` as `rainfall[i]*area*dt` next to the ledger's
  `rain_vol`. Accessor `rainCumulative()`.
- `SimulationSnapshot::surface_rain_cum` ← `fillSurfaceSnapshot`.
- `Default2DOutputPlugin`: new `/Mesh2_face_rain_cum [nTime,nFace]` (units
  `m3`), written only when the snapshot vector is sized `n_faces_`.
- Test `Default2DOutputPlugin.WritesUgridHdf5WithExpectedDatasets` extended.

GUI (`git show 75dc2f2`):
- `PlotAttribute::Mesh2DRainfall` (38, mm/hr) / `Mesh2DRainVolume` (39, m³);
  `mesh2DCell/Edge/VertexPlotAttributes()` lists; `attributesForKind()` now
  returns them for the three mesh kinds; `isMesh2DAttribute` includes rainfall.
- `Mesh2DH5Reader::readFaceFieldAt(name,t,out)` / `hasFaceField(name)`
  (probe-once cache); `readDepthsAt` delegates. `IMesh2DSource` gets both as
  virtuals (default false); `HDF5Mesh2DSource` forwards.
- `Mesh2DRunLayer::supportsAttribute/getSeriesAt` serve rainfall (×3.6e6 for
  intensity; volume verbatim).
- `AttributePickerMenu::createForObjectKind` handles mesh kinds; new
  `execForMeshKind(kind, globalPos, availability, tip)` pops the menu, greys
  unsupported entries, expands "All attributes".
- `MapToolPick2DCells::requestPlotAt_` pops the menu on right-RELEASE and emits
  `cellsPicked(layer, cells, attrs)`; the old `QDialog` popover in
  `SWMMVis::openComparisonPlotForCells` is gone (signature now takes `attrs`).
  `qRegisterMetaType<QVector<openswmmvis::plot::PlotAttribute>>` added before
  the queued connect in `SWMMVis::onActiveSubWindowChanged`.
- `MapToolMeshSelectEdge` right-click uses `execForMeshKind(Mesh2DEdge)` and
  emits `plotEdgeFluxRequested` once per chosen attr (signal unchanged).
- `MapToolMeshSelectVertex` right-click: "Plot Time Series…" submenu;
  `plotVertexSeriesRequested(mesh, verts, attrs)`; `SWMMVis::openMeshVertexSeriesFor`
  takes `attrs`. `SWMMVisProjectWindow` forwarding signals updated to match.
- Tests: `test_resultdescriptor.cpp`, `test_plotvariablepickerdialog.cpp`
  (old "mesh kinds have empty list" assertions flipped), new case in
  `test_mesh2dh5reader.cpp` (`readsNamedFaceFieldAndProbesAbsence`).

## Step 1 — Engine build + tests

```
cd ~/Documents/Projects/cbuahin_github/openswmm.engine
cmake --build build-arm64-osx -j 8 2>&1 | tee build/verify_rain_cum_build.log | grep -E "error|warning: unused" 
(cd build-arm64-osx && ctest -j 4 --output-on-failure -R "2d|surface|output" 2>&1 | tee ../build/verify_rain_cum_ctest.log | tail -20)
```
(If `build-arm64-osx` is stale, `build/build-and-test.sh` uses `build/darwin`;
use whichever configure dir exists and is current.)

Pass criteria: zero errors; `test_engine_2d_surface_routing` (contains
`Default2DOutputPlugin.WritesUgridHdf5WithExpectedDatasets`) passes; the full
2D subset passes. Then install so the GUI picks up the new engine:
`cmake --install build-arm64-osx` (GUI expects `../openswmm.engine/install/...`,
check `openswmm.gui/CMakeUserPresets.json` / `build/CMakeCache.txt` for the
exact path it links against).

## Step 2 — Engine ledger identity (rain_cum sums to rainfall_in)

Run any rain-on-grid 2D example (e.g. the one used by
`tests/unit/engine/test_2d_infil_integration.cpp`, or an example under
`examples/` with `[2D_*]` sections and a rain gage) and check, e.g. with h5py:

```python
import h5py, numpy as np
f = h5py.File("<run>.2d.h5")
rc = f["Mesh2_face_rain_cum"][-1, :]          # last step, m3 per cell
print(rc.sum(), f["mass_balance_2d/rainfall_in"][()])   # must match to ~1e-9 rel
print((np.diff(f["Mesh2_face_rain_cum"][:, :], axis=0) < -1e-12).any())  # False: monotone
```
Also confirm `Mesh2_face_rainfall` is non-zero during the storm.

## Step 3 — GUI build + tests

```
cd ~/Documents/Projects/cbuahin_github/openswmm.gui
./build-gui.sh 2>&1 | tee build/verify_2d_menu_build.log | grep -E "error:" 
(cd build && ctest --output-on-failure -R "mesh2dh5reader|resultdescriptor|plotvariablepicker|attributepicker|comparisonplot|mesh2d|2dresults" 2>&1 | tee ../build/verify_2d_menu_ctest.log | tail -30)
```
Likely trouble spots if something fails to compile — fix in place:
- `attributepickermenu.cpp`: `IRunLayer` / `attributesForKind` resolve via
  `using namespace openswmmvis::plot;` at the top of the file.
- `maptoolmeshselectvertex.cpp`: ternary mixing `const QVector<PA>&` and
  `QVector<PA>{a}` — if the compiler objects, build the vector explicitly.
- Any remaining caller of the old 2-arg `cellsPicked` / `pick2DCellsPicked` /
  `plotVertexSeriesRequested` / `meshVertexSeriesRequested` /
  `openComparisonPlotForCells` / `openMeshVertexSeriesFor` signatures
  (`grep -rn` across `src include tests`).
- Queued connection of `QVector<openswmmvis::plot::PlotAttribute>`: if Qt warns
  "QObject::connect: Cannot queue arguments of type …" at runtime, the
  `qRegisterMetaType` call in `swmmvis.cpp` is not reached before the connect —
  move it to `SWMMVis` ctor.
- `Mesh2DH5Reader` test fixture: `readFaceFieldAt("Mesh2_face_depth", 2, v)` must
  equal `readDepthsAt(2, d)`.

## Step 4 — Manual smoke check (needs a fresh 2D run with the new engine)

1. Open a 2D model, run it, load the `.2d.h5` results layer.
2. Analysis ▸ Pick 2D Cells tool. Right-click a cell (nothing selected) →
   context menu appears with: Depth, HGL, |V|, Vx, Vy, Rainfall (mm/hr),
   Rainfall volume (m³), separator, All attributes. No dialog.
   - Pick "Rainfall volume" → Comparison Plot opens with a monotone
     non-decreasing series; "Rainfall" → matches the gage hyetograph shape.
   - Box-select several cells, right-click, "All attributes" → N×7 series;
     >500 total still triggers the "Many series" prompt.
3. Open an OLD `.2d.h5` (pre-`rain_cum`): Rainfall volume greyed out with the
   tooltip; Rainfall (intensity) still enabled if the file has
   `Mesh2_face_rainfall`.
4. Mesh ▸ Select Edge tool: right-click an edge → menu with Edge flow, Edge
   flux, All attributes. "All" adds both series.
5. Mesh ▸ Select Vertex tool: right-click a vertex → header + elevation lines,
   then "Plot Time Series…" submenu with Depth, HGL, All attributes.
6. macOS input sanity: after cancelling each menu with Esc / click-away, the
   canvas still responds to left-clicks (no latched right-button grab — the cell
   menu is deliberately shown on release, see `m_pendingRightPlot`).
7. 1D regression: right-click a node/link in the Select tool still shows its
   Plot Time Series menu unchanged.

## Addendum — 2D inundation overlay on the 1D profile (GUI, third commit)

Also uncompiled. Files: `include/plot/profileplotoptions.h`,
`src/plot/profileplotoptions.cpp` (`show2DInundation`, `inundation2DLinePen`,
`inundation2DFillBrush`), `include/plot/profileplotwidget.h`,
`src/plot/profileplotwidget.cpp` (`Surface2DSample`, `setSurface2DSamples`,
`paintSurface2D`, `realChainageToVirtualX` — the terrain lambda now delegates
to it; y-extent widens to the wettest WSE seen; legend row),
`include/ui/dialogs/profileplotdialog.h`, `src/ui/dialogs/profileplotdialog.cpp`
(`forEachPathStation` factored out of `rebuildTerrainSamples`;
`rebuildSurface2DStations` / `refreshSurface2DDepths`; toolbar action
`show2DInundation`; hooks on `ProfilePlotOptions::changed`,
`SWMMVisProjectWindow::active2DResultsLayerChanged`,
`SWMM2DResultsLayer::currentTimeChanged`, and `onAnimationTimeChanged` →
`setCurrentSimTimeAsOf`).

Compile trouble spots: `MapCanvas::layers()` returns
`const QList<OpenSWMMVisLayer*>&` (iterated with a range-for);
`SpatialReferenceSystem::equals/createTransformationTo` used exactly as in
`rebuildTerrainSamples`; `QPolygonF::append(QVector<QPointF>)` in
`paintSurface2D` (QPolygonF is a QList<QPointF> in Qt 6 — if it objects,
loop-push instead).

Smoke check (coupled 1D/2D model with a 2D results layer active):
1. Open a 1D profile through nodes that sit inside the mesh → teal band
   + line above the ground where the surface is wet; nothing where dry;
   1D HGL/pipes unchanged and drawn on top; legend has "2D water surface".
2. Play the animation → band rises/falls with the map's 2D layer; hide the
   2D layer in the layer tree → profile still animates.
3. Toolbar "2D Inundation" off → overlay gone, legend row gone; on → back.
   Display Options ▸ "Show 2D inundation" mirrors the button both ways;
   editing the pen/brush restyles immediately.
4. Switch the Analysis toolbar's 2D results selector to another run /
   none → overlay re-samples / clears.
5. Terrain ground on + overlay on: band's lower edge is the MESH bed
   (may differ slightly from the DEM line) — expected, documented.
6. Model authored in feet with a metre mesh: WSE = bed + depth adds mesh z
   and depth without unit conversion, matching the existing 2D mesh
   profile's behaviour. If it visibly mismatches, that is a pre-existing
   unit gap shared with `MeshProfileSampler` — note it, don't patch it here.

## Step 5 — Report

Append results (build/ctest status, the rain_cum-vs-ledger numbers, smoke
outcomes, any fixes made) to this file under `## Results`, and commit fixes as
`fix(2d): …` in the respective repo — again without sweeping the unrelated
uncommitted hunks (`git add -p`, or apply a filtered patch).
