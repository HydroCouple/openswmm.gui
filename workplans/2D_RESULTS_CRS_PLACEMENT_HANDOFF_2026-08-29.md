# Issue #155 — 2D results land in the wrong place in a foot-based CRS (GUI side)

**Status:** VERIFIED and SHIPPED 2026-08-29 as GUI commit `4d7890c` on
`swmm6_gui` (13 files, pathspec-staged; this plan file is gitignored and was
not committed). `tests/gui/test_2dresults_vizfixes.cpp` IS registered (full-app
link pattern, `swmmvis_link_full_app_deps`) and its 11 cases pass, including
the four #155 placement cases. `test_mesh2dh5reader` (17) and
`test_inpmeshreader` (20) pass with the new cases; the whole gui label ran
176/177 (the one Not Run is a peer's just-registered
`test_reaction_expression_editor`, unbuilt in this tree). Full app build clean
against engine `630f531b` (the `/crs` writer). NOT done here: the manual
checks under §3–§6 (the BWSC repro file is not on this machine; canvas
reprojection, live mode, velocity glyphs are by-eye checks).

**Companion doc:** `openswmm.engine/plans/2d/2D_H5_CRS_SELF_DESCRIBING_HANDOFF_2026-08-29.md`
— the engine half of the same fix, and the source of the file contract.

---

## The bug

A model whose CRS is a foot-based projected CRS (EPSG:2249, US survey foot)
draws its 2D **results** at ~0.3048x scale, pulled toward the CRS origin, while
the 2D **mesh** and the 1D network draw correctly.

Repro: open `BWSC_Combined_Model_Aug2026NewGARR_TStorm2070_OpenSWMM`
(`.inp` + `.2dm` + `.2d.h5`, ~204 MB), load the 2D results, enable the layer.

## Two faults, stacked

The original issue report identified one and mis-described the other. Both are
real; the fix addresses both.

**1. Unit mismatch.** Every 2D source hands the layer **SI metres** — the 2D
solver runs in SI, so the engine converts the authored mesh by `0.3048` for a
US-`FLOW_UNITS` model before anything is written or queried. This is true of
`HDF5Mesh2DSource` (`/Mesh2_node_x`, tagged `units = "m"`) *and* of the live
`EngineMesh2DSource` (`swmm_2d_vertex_get_xyz_bulk`). `SWMM2DMeshLayer`, by
contrast, reads the `.2dm` and never leaves model units — hence "mesh correct,
results offset".

**2. No reprojection at all.** The report placed a
`CRSReproject::transformPointsInPlace(m_transform, ...)` call in
`SWMM2DResultsLayer::rebuildSceneGeometry_()`. There was none — that is the
*mesh* layer's code (`swmm2dmeshlayer.cpp:1165-1184`). The results layer applied
identity + Y-flip, and `onCanvasCRSChanged` discarded its argument outright
("Reprojection seam — ... comes later"). Its SRS was set at
`swmmvis.cpp:5484` / `:7876` purely so the Properties window showed a CRS. So
results and terrain also separated whenever the canvas CRS differed from the
model CRS, independent of units.

## The fix

`rebuildSceneGeometry_()` now does three things in order:

1. **metres → model-CRS linear unit** — divide by the factor from
   `resolveCoordinateScale_()`.
2. **model CRS → canvas CRS** — one batched
   `CRSReproject::transformPointsInPlace(m_transform, ...)`, exactly as
   `SWMM2DMeshLayer` does. `m_transform` is built in `onCanvasCRSChanged()`
   and is `nullptr` when the CRSes match (a documented pass-through).
3. **Y-flip** — unchanged; the scene grows downward.

### `resolveCoordinateScale_()` — the two paths

**Declared (engine 6.0+).** `Mesh2DH5Reader::readCoordinateReference()` reads
the file's `/crs` variable → `metres_per_model_unit`. The layer divides by
exactly the factor the engine multiplied by, so the round trip is **exact** —
including the engine's use of the international foot (`0.3048`) where
EPSG:2249's own unit is the US survey foot (`0.304800609601`).

**Undeclared (pre-6.0 file, or the live in-process source).** Neither carries
metadata, so the caller supplies the factor via
`SWMM2DResultsLayer::setFallbackCoordinateScale()`. `swmmvis.cpp`'s
`undeclared2DCoordinateScale()` derives it **the way the engine derives it**:
`swmm_get_unit_system()` says US-customary, AND the mesh file did not declare
`;; UNITS: SI (m)` (read from the new `SWMM2DMeshLayer::meshUnitsSI()`, set at
mesh load from `mesh::unitsHeaderIsSI(meshRead.unitsHeader)`) ⇒ `0.3048`, else
`1.0`. **One `qWarning` naming the layer**, guarded by `m_warnedUndeclaredCrs`.

> **An earlier draft inferred this from `srs()->planarLinearUnit()` and that
> was wrong — do not "simplify" it back.** The engine's decision is driven by
> `FLOW_UNITS` and the mesh header, never by the CRS. On a US-`FLOW_UNITS`
> model whose mesh is SI-tagged — which the engine's own `InpWriter` produces
> on round-trip, since it writes `;; UNITS: SI (m)` once `mesh_scaled_to_si` is
> set — the engine applies **no** scaling, while CRS-based inference would
> divide by 0.3048 and blow the results up 3.28x relative to the mesh layer.
> The reverse combination (US units, metric CRS) leaves the original bug in
> place, silently.

The default is `1.0` — leave the coordinates alone. That is deliberately the
pre-#155 behaviour: when we cannot establish that the engine scaled, agreeing
with `SWMM2DMeshLayer` beats inventing a new offset.

This path carries a ~2 ppm residual (the survey-vs-international foot gap:
~1.5 ft over a 740,000 ft coordinate) — enough to see at extreme zoom, nothing
like the 0.3x failure. It exists so the 204 MB repro file renders correctly
**without re-running**.

### `extent()` stays in the layer CRS

`rebuildSceneGeometry_()` now tracks two bounding boxes: `m_sceneBBox` from the
reprojected, Y-flipped scene points, and the layer extent from the
model-unit coordinates **before** reprojection. `MapCanvas::layerExtentInCanvasCRS`
transforms `layer->extent()` layer→canvas itself, so handing it scene
coordinates would transform them twice — visible as a wrong "Zoom to layer" /
"Zoom to full extent" on any reprojected canvas. `SWMM2DMeshLayer` keeps its
extent in model coordinates for the same reason.

## Contract read from the file

Scalar `/crs` variable: `model_crs` (string, verbatim `[OPTIONS] CRS`, absent
when the model declared none), `metres_per_model_unit` (double,
`stored = model x this`), `units` (`"m"`). Full rationale — including why this
is deliberately *not* `spatial_ref`/`crs_wkt` — in the engine doc.

## Changes

| File | Change |
|---|---|
| `include/io/mesh2dh5reader.h` | New `struct openswmmvis::io::CoordinateReference`; `readCoordinateReference()`. |
| `src/io/mesh2dh5reader.cpp` | `readStringAttr` / `readDoubleAttr` helpers in the anonymous namespace (alongside the existing `readStartIndexAttr`); `readCoordinateReference()` — returns `false` without error on a file with no `/crs`, and still fills `storedUnits` from `Mesh2_node_x@units`. |
| `include/layers/swmm2dresultslayer.h` | `IMesh2DSource::coordinateReference()` (virtual, defaults to undeclared); override on `HDF5Mesh2DSource`; `setFallbackCoordinateScale()`, `m_transform`, `m_warnedUndeclaredCrs`, `m_fallbackMetresPerModelUnit`, `resolveCoordinateScale_()` on the layer; includes `io/mesh2dh5reader.h` and `<ogr_spatialref.h>`. |
| `src/layers/swmm2dresultslayer.cpp` | `HDF5Mesh2DSource::coordinateReference()`; `setFallbackCoordinateScale()`; `resolveCoordinateScale_()`; scale + batched reprojection + separate layer-CRS extent in `rebuildSceneGeometry_()`; real transform construction in `onCanvasCRSChanged()`; `DestroyCT` in the destructor (was `= default`). |
| `include/mesh/inpmeshreader.h`, `src/mesh/inpmeshreader.cpp` | `mesh::unitsHeaderIsSI()` — the engine's `prescan2DUnitsHeader` keyword set, mirrored. |
| `include/layers/swmm2dmeshlayer.h` | `meshUnitsSI()` / `setMeshUnitsSI()` (+ member), mirroring the `isExternalMesh` pattern. Header-only; no `.cpp` change. |
| `src/swmmvis.cpp` | `undeclared2DCoordinateScale()` helper; `setMeshUnitsSI()` at mesh load; `setFallbackCoordinateScale()` before `setSource` at both results-layer creation sites (post-run ~L5480, live ~L7900). |
| `tests/gui/test_mesh2dh5reader.cpp` | `appendCrsVariable()` fixture helper + two cases. **Runs.** |
| `tests/gui/test_inpmeshreader.cpp` | `unitsHeaderIsSI_matchesEngineKeywords` — pins the keyword set against the engine's. **Runs.** |
| `tests/gui/test_2dresults_vizfixes.cpp` | `FakeSource::declareCoordinateReference()` + four cases. **Not yet running** — see below. |
| `CHANGELOG.md` | Entry under `[Unreleased] → Fixed`. |

`EngineMesh2DSource` deliberately does **not** override
`coordinateReference()`: it has no metadata channel, so it takes the fallback
path, which `swmmvis.cpp` supplies. See follow-up 2.

---

## Verify

### 1. Build

```bash
cd /Users/calebbuahin/Documents/Projects/cbuahin_github/openswmm.gui
cmake --preset Darwin -DSWMMVIS_BUILD_TESTS=ON && cmake --build --preset Darwin
```

A subagent traced the transitive include closure and confirmed every TU that
now sees `<ogr_spatialref.h>` (25 app `.cpp` files plus
`tests/gui/test_2dresults_vizfixes.cpp`) is compiled only by `SWMMVis` or a
`swmmvis_link_full_app_deps` test target, both of which add
`${GDAL_INCLUDE_DIRS}`. `test_mesh2dh5reader` is unaffected —
`io/mesh2dh5reader.h` is HDF5- and OGR-free. Still, that was static analysis,
not a compile. Other things to watch:

- `swmm2dresultslayer.h` keeps its `namespace openswmmvis::io { class Mesh2DH5Reader; }`
  forward declaration *and* now includes the full header. Legal (re-declaration),
  but tidy it if you like.
- `resolveCoordinateScale_()` is `const` and touches the `mutable`
  `m_warnedUndeclaredCrs`.
- `undeclared2DCoordinateScale()` lives in a **new anonymous namespace** opened
  immediately before `SWMMVis::maybeLoad2DResults` in `src/swmmvis.cpp`. It
  uses `swmm_get_unit_system` (`openswmm/engine/openswmm_engine.h`, already
  included) and `qobject_cast<SWMM2DMeshLayer*>` (already included).

### 2. Unit tests

```bash
ctest --preset Darwin -R "test_mesh2dh5reader|test_inpmeshreader" --output-on-failure
```

`test_inpmeshreader`:

- `unitsHeaderIsSI_matchesEngineKeywords` — pins `mesh::unitsHeaderIsSI`'s
  keyword set against the engine's `prescan2DUnitsHeader`. These two lists are
  duplicated across repos; if they drift, results land in the wrong place with
  no error anywhere, so this test is the only thing holding them together.

`test_mesh2dh5reader`:

- `coordinateReferenceReadsDeclaredCrs` — fixture with `/crs` → `EPSG:2249`,
  `0.3048`, `"m"`, `declared == true`; and `readMeshGeometry` still returns the
  **raw stored metres** (the factor is metadata, never applied behind the
  caller's back).
- `coordinateReferenceUndeclaredOnLegacyFile` — no `/crs` → returns `false`,
  `declared == false`, factor `1.0`. Distinguishing "undeclared" from "declared
  as metric" is what makes the fallback safe; don't collapse them.

**`tests/gui/test_2dresults_vizfixes.cpp` is still unregistered** in
`tests/gui/CMakeLists.txt` — a pre-existing condition (its header block explains
the link closure is large and unresolved; see `tests/gui/CMakeLists.txt:725`).
Four new cases were added there ready to run:

- `declaredMetreFactorScalesToModelUnits` — a declared `0.3048` turns the unit
  square (0..1 m) into 0..3.2808 ft of extent. **This is the case that would
  have caught the bug.**
- `metricModelIsUnscaled` — a declared `1.0` is bit-exact.
- `undeclaredSourceUsesCallerFallback` — `setFallbackCoordinateScale(0.3048)`
  on a source that declares nothing.
- `undeclaredSourceDefaultsToNoScaling` — nothing declared, no fallback set ⇒
  coordinates untouched. This is the guard against re-introducing CRS-based
  guessing.

**Registering that file is the highest-value remaining task.** If the link
closure is tractable, do it in this commit; if it turns into a yak-shave, leave
it and say so — don't half-wire it and leave the build red.

### 3. The actual repro (the test that matters)

1. Open `BWSC_Combined_Model_Aug2026NewGARR_TStorm2070_OpenSWMM`.
2. Load the existing `.2d.h5` — the 204 MB one, **not** re-run. This exercises
   the fallback path.
3. Enable the 2D results layer with the mesh layer visible underneath.

Expect: results overlay the mesh and the 1D network. Expect one
`[2D-render] <layer name>: 2D result source declares no /crs variable ...`
warning on the console. Check the layer extent in Properties reads ~739,809 –
794,916 in X, not ~225,494.

Then re-run the model with an engine carrying the companion change and reload —
same placement, no warning, and the residual against the mesh layer should drop
from ~1.5 ft to zero.

### 4. Reprojected canvas (fault 2, independent of units)

Set the canvas CRS to something other than the model CRS (EPSG:3857 will do)
and confirm the results layer **moves with** the mesh layer. Before this
change it stayed put. Worth doing on a metric model too, so the reprojection is
tested without the unit conversion in play.

### 5. Live mode

Run a 2D simulation and watch the live results layer while it streams. It takes
the fallback path (`EngineMesh2DSource` declares nothing), so a US-units model
should place correctly with the one-line warning. Verify the warning does not
repeat per tick.

### 6. Things the change could plausibly have broken

- **Velocity arrows / contours / cell picking.** Scene coordinates are now in
  canvas units, not metres. Arrow scaling reads `m_sceneBBox` dimensions
  (relative, fine) and the wireframe `slope` still comes from `vx_`/`vy_`/`vz_`
  in metres (dimensionless ratio, fine, and deliberately left alone). Confirm
  by eye: hover a cell, check the arrows, scrub the time slider.
- **Deferred/async geometry.** The mesh layer has three reprojection sites
  (`rebuildSceneGeometry`, `...Light`, `finishSceneGeometryAsync`); the results
  layer has one. If a deferred path is added later it must carry the same two
  steps.

---

## Ship

`swmm6_gui` carries substantial unrelated uncommitted work — `forms/swmmvis.ui`,
`tests/gui/CMakeLists.txt`, the simulation-options dialog, the GIS vector layer
and several other tests. **Do not `git add -A` or `git commit -a`.**

**`src/swmmvis.cpp` is the one file that needs care** — it had pending edits
before this work, so `git add -p` it and stage only the three #155 hunks
(`setMeshUnitsSI` at mesh load, the `undeclared2DCoordinateScale` helper, and
the two `setFallbackCoordinateScale` calls). Every other file below was clean;
staging them by name is safe as-is.

```
git add include/io/mesh2dh5reader.h src/io/mesh2dh5reader.cpp \
        include/mesh/inpmeshreader.h src/mesh/inpmeshreader.cpp \
        include/layers/swmm2dmeshlayer.h \
        include/layers/swmm2dresultslayer.h src/layers/swmm2dresultslayer.cpp \
        tests/gui/test_mesh2dh5reader.cpp tests/gui/test_inpmeshreader.cpp \
        tests/gui/test_2dresults_vizfixes.cpp \
        CHANGELOG.md \
        workplans/2D_RESULTS_CRS_PLACEMENT_HANDOFF_2026-08-29.md
git add -p src/swmmvis.cpp
```

If you register the layer test, `tests/gui/CMakeLists.txt` joins the list —
`git add -p` that one too, since it has other pending edits.

Commit message:

```
2d: place 2D results correctly in a foot-based CRS (#155)

Two stacked faults. Every 2D source hands the layer SI metres (the
solver's internal unit), and the layer treated them as model units, so
a model in EPSG:2249 drew its inundation at ~0.3048x scale toward the
CRS origin while the .2dm-backed mesh layer sat correctly. And the
layer never reprojected at all -- onCanvasCRSChanged discarded its
argument -- so terrain and inundation also separated whenever the
canvas CRS differed from the model's.

Divide by the metres_per_model_unit factor the engine 6.0+ /crs
variable declares, then reproject model -> canvas through the same
batched OGR path SWMM2DMeshLayer uses. Sources that declare nothing
(pre-6.0 files, the live in-process source) get the factor from the
caller, derived the way the engine derives it -- FLOW_UNITS,
suppressed by a ";; UNITS: SI (m)" mesh header -- so existing .2d.h5
files render correctly without re-running. The layer extent stays in
the layer CRS; MapCanvas reprojects it itself.

Refs #155
```

---

## Follow-ups (do NOT fold into this commit)

1. **Register `tests/gui/test_2dresults_vizfixes.cpp`** if it wasn't done above.
   Until then nothing pins the layer's placement behaviour.
2. **Give the live source an exact factor.** `EngineMesh2DSource` uses the
   fallback because no engine API exposes `SolverOptions2D::mesh_to_si_factor`.
   A `swmm_2d_get_mesh_coordinate_scale()` (or a field on
   `twod_get_mesh_summary`) would let `swmmvis.cpp` declare it directly at
   `twoDInitialized`, closing the 2 ppm gap for live runs and removing the
   duplicated `unitsHeaderIsSI` keyword list.
3. **Velocity glyph direction is not rotated by the reprojection.**
   `st.vx`/`st.vy` (`swmm2dresultslayer.cpp:~2611`) come from the RT0 solve in
   the model frame; the glyph draws that direction on a canvas that may be in
   another CRS, so meridian convergence tilts every arrow by that angle.
   Magnitudes are unaffected (they derive from `edge_length_`, not scene
   coordinates), and the error is zero when canvas CRS == model CRS, which is
   the default. Same applies to `velocityAtScene` and `vvx_`/`vvy_`.
4. **`CoordinateReference::crs` is read but never checked.**
   `resolveCoordinateScale_()` trusts the file's factor while `m_transform`'s
   source CRS comes from the layer's SRS. If a user re-assigns the layer CRS,
   or the `.h5` came from a run with a different `[OPTIONS] CRS`, the two
   silently disagree. A one-line `IsSame` sanity warning would close it.
5. **Vertical datum is still mixed** (pre-existing, same class of bug on the
   z axis). `invScale` applies to x/y only; `vz_`, `cellZc_`, `dry_depth_`,
   `max_depth_` stay in engine metres. Harmless inside
   `CellSurfaceInterp::depthAt` (barycentric, affine-invariant), but
   `src/plot/meshprofilesampler.cpp:97-104` combines `mesh->sampleZAt()`
   (model units) with `results->depthAtSceneInterp()` (metres) into one WSE
   column. Not caused or fixed here; worth its own ticket.
6. **`InpMeshReader`'s doc comment contradicts its callers.**
   `include/mesh/inpmeshreader.h:29` says mesh XY are "in SI metres (the engine
   contract); caller divides back to the project CRS for display", while
   `swmmvis.cpp:~5327` says the opposite and is what the code actually does.
   One of them is wrong; the comment, on the evidence. Unrelated to #155 but
   directly adjacent, and it will mislead the next person here.
7. **`;; SOURCE_CRS:` is parsed into `InpMeshReadResult::sourceCrsTag` and never
   consumed.** The mesh layer's CRS is copied from the model layer instead. If
   a `.2dm` is ever imported standalone, that tag is the only CRS available.
8. **`;; UNITS: SI (m)` on a foot-CRS model** makes the engine skip scaling and
   report `metres_per_model_unit = 1.0`. Mesh and results then agree — both
   placed as if the metre values were feet. An authoring-time validation
   warning belongs in the mesh writer or the engine; see the engine doc.
