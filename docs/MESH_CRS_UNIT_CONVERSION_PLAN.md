# Mesh CRS Unit Conversion + Local Projected CRS Picker — Plan

Status: **IMPLEMENTED (revised after engine-side discovery)**
Author: Caleb / AI assist
Date: 2026-05-30
Scope: openswmm.gui + openswmm.engine

---

## 0. Correction note (2026-05-30, revised)

The first cut of this plan assumed the engine consumes `[2D_VERTICES]` XY
as SI metres. That was wrong. `SurfaceRouter2D::initialize` already
multiplies the mesh by 0.3048 when SWMM FLOW_UNITS is US, i.e. the
engine expects values in the project's length unit (feet for US, metres
otherwise). Writing SI on disk from the GUI without an opt-in would
double-convert on US projects.

The revised design splits responsibility cleanly:

- **Producer (GUI/third-party)** writes XY in whatever unit they like and
  declares the unit via a `;; UNITS:` comment header.
- **Engine** reads the header before mesh load and gates its
  FLOW_UNITS-based mesh scaling: skipped when the header says SI,
  otherwise applied as before. Coupling-side conversions
  (`len_1d_to_2d`, `vol_1d_to_2d`, `flow_*`) stay FLOW_UNITS-driven
  because they describe the 1D side of the boundary.
- The GUI currently writes XY in project-CRS units (matching the
  legacy/default engine behaviour) and emits the descriptive header for
  traceability. Switching the GUI to SI-on-disk is a future flip that
  needs the engine update first (now done).

---

## 1. Problem

The GUI today writes mesh vertex XY **directly in the project CRS units**
(`MeshGenerationDialog::runMeshPipeline` → `InpMeshWriter::formatVertices`).
The engine multiplies by 0.3048 in `SurfaceRouter2D::initialize` when SWMM
FLOW_UNITS is US, so the *expected* contract is "project CRS units must
match the FLOW_UNITS-implied length unit". Two failure modes:

1. **Project-CRS unit doesn't match FLOW_UNITS-implied unit** (e.g.
   CFS flow units with a metric CRS, or CMS with a US-foot CRS):
   the engine scales by 0.3048 either too many or too few times.
2. **Geographic project CRS** (lat/lon): mesh values are degrees, the
   engine multiplies them by 0.3048, and the entire mesh becomes a
   sub-degree dot at the equator.

Vertical Z is already handled — `m_zFactorSpin` / `PipelineInputs::zConversionFactor`
multiply DTM-sampled Z into the mesh's vertical unit. Horizontal XY is **not**
explicitly handled today — only the implicit alignment between CRS-unit and
FLOW_UNITS keeps things consistent.

Separately, when the source data carries no recognisable CRS, the GUI falls
back to `Untitled (Local)` and forces the user through the full EPSG picker
even when they'd happily accept "local planar in whatever unit the model is
already in". `SpatialReferenceSystem::localFromMapUnits("FEET"|"METERS")`
exists but is only reachable from the `.inp [MAP] Units` autodetect path
(`swmmmodellayer.cpp:695`).

## 2. Goals & Non-goals

**Goals (as-built)**

1. The GUI refuses to mesh against a geographic / non-planar / undefined CRS, with a clear message and an escape hatch to the local-projected picker.
2. The CRS picker offers "Local projected in model units" as a first-class choice (visible in the CRS tree and as a button on the unknown-CRS prompt at project open).
3. Mesh files are self-describing: `;; UNITS:` and `;; SOURCE_CRS:` header comments declare the on-disk unit and provenance.
4. The engine honours `;; UNITS: SI (m)` (and metric synonyms) by **skipping** its FLOW_UNITS-based mesh-side scaling. Legacy files with no header behave exactly as before.

**Non-goals**

- No change to `MeshData.hpp`. The 2D solver still runs internally in SI.
- No reprojection of node / link coordinates.
- No migration tool for previously generated `.2dm` files (a non-fatal warning is emitted instead).
- No support for non-planar (geographic, degree-based) mesh generation.
- The GUI does **not** currently switch to SI-on-disk; it writes XY in the project CRS unit as today's engine expects. The producer-side flip can happen later, once the engine update has propagated.

## 3. Assumptions

- A1. The 1D⇄2D coupling factors (`len_1d_to_2d`, `vol_1d_to_2d`, `flow_*`) describe the 1D side of the boundary, not the mesh — they stay FLOW_UNITS-driven even when the mesh is declared SI. Verified in `NodeCoupling.cpp`.
- A2. Mesh CRS axis order is always traditional GIS (`x=east, y=north`). The codebase enforces this via `OAMS_TRADITIONAL_GIS_ORDER`.
- A3. `OGRSpatialReference::GetLinearUnits` returns 1.0 for a freshly-defaulted SRS — acceptable fallback.
- A4. The producer's `;; UNITS:` declaration is authoritative. If a producer writes SI metres but doesn't declare it, the engine will silently apply its FLOW_UNITS-based factor and the mesh will be wrong. This is the trade-off of staying compatible with legacy producers.

## 4. Alternatives considered

- **Alt-A: GUI writes SI to disk unconditionally**. *Rejected* — would double-convert on US-FLOW_UNITS projects against today's engine. Could be revisited once the engine update has shipped and old engine binaries are out of circulation.
- **Alt-B: Add a `[2D_OPTIONS] UNITS m` key instead of a header comment**. *Rejected* — heavier (engine parser change), conflates the mesh unit with solver options. A `;;` comment is invisible to any tokenizer-only consumer.
- **Alt-C: Force the user to pick a CRS that matches FLOW_UNITS**. *Rejected* — too coercive; some users legitimately work in foot-based CRSes for metric SWMM models or vice versa.

Recommended (and implemented): **descriptive header in the file; engine acts on it.**

## 5. Design (as-built)

### 5.1 GUI helper

`SpatialReferenceSystem::planarLinearUnit()` returns:

```cpp
struct LinearUnitInfo {
    double  metresPerUnit = 1.0;
    QString name;                  // "metre", "US survey foot", …
    bool    usable = false;        // false if geographic / undefined
};
```

`usable` ⇔ projected or local AND `metresPerUnit` finite and > 0.

### 5.2 GUI write path

`MeshGenerationDialog::collectInputs`:
1. Read `lui = layer->srs()->planarLinearUnit()`. Fail (with the local-projected escape hatch) if `!lui.usable`.
2. Forward `lui.name` to `PipelineInputs::meshLinearUnitName` (e.g. "US survey foot") and `srs->toAuthority()` to `meshCRSTag` ("EPSG:2249").

`runMeshPipeline` → `InpMeshWriter::write` with a `UnitInfo` whose fields are descriptive only. The writer emits:

```
;; UNITS: <linearUnitName>
;; SOURCE_CRS: <meshCRSTag>
```

… and writes XY verbatim. No multiplication.

### 5.3 GUI read path

`InpMeshReader::read` scans the inline `.inp` and the resolved `.2dm` for the two header lines. Values are stored on `InpMeshReadResult` (`unitsHeader`, `sourceCrsTag`); a missing header surfaces a non-fatal warning. XY are passed through unchanged — display coordinates remain in project-CRS units.

### 5.4 Engine units-aware mesh load

- `SolverOptions2D::mesh_units_si` (default false) — true when the mesh file declared SI.
- `prescan2DUnitsHeader(path, opts)` in `SectionHandlers2D.cpp` opens the file, scans for the first `;; UNITS:` line, and sets the flag when the value is `SI (m)` / `m` / `metre` / `metres` / `meter` / `meters` (case-insensitive). Anything else (including `ft`) leaves the flag unchanged.
- Called from `SWMMEngine::open` for the inline `.inp` and from `load2DMeshExternalFile` for the resolved `.2dm`. External overrides inline (later call wins).
- `SurfaceRouter2D::initialize` gates its mesh-side scaling: `if (!options_.mesh_units_si && options_.len_1d_to_2d != 1.0) { /* multiply */ }`. Coupling-side factors are unchanged.

### 5.5 Local projected CRS picker in model units

Same as the original §5.4 — implemented:
- `CRSSelectionDialog` adds a synthetic `Local (no transform)` group with two leaves; `selectedSRS` short-circuits to `SpatialReferenceSystem::localFromMapUnits` for `LOCAL:METERS` / `LOCAL:FEET`.
- `SWMMVisProjectWindow` unknown-CRS prompt loop adds a "Use local projected (ft|m)" button that defaults to the model's flow-unit system.

## 6. Validation plan

| Step | Verification |
|------|--------------|
| 1. `planarLinearUnit()` | EPSG:32634 → `{1.0, "metre", true}`; EPSG:2249 → `{0.3048006…, "US survey foot", true}`; EPSG:4326 → `{_, _, false}`. |
| 2. Write-path header | Generate against a US-foot project; `.2dm` contains `;; UNITS: US survey foot` and `;; SOURCE_CRS: EPSG:2249`; XY values agree numerically with the GUI extent (unchanged from today). |
| 3. Geographic-CRS guard | Selecting a project in EPSG:4326 and clicking *Generate* shows the explanatory error before `runMeshPipeline`. |
| 4. Local picker | New project with no CRS → "Use local projected (ft)" available; clicking sets the model layer SRS to `Local (ft)`. |
| 5. Legacy `.2dm` warning | Read pre-existing `.2dm` (no header) → one warning, mesh still loads. |
| 6. Engine SI honour | Hand-edit a `.2dm` to add `;; UNITS: SI (m)` and rewrite XY in metres; run the engine with US FLOW_UNITS; physics matches the equivalent metric-FLOW_UNITS run within solver tolerance. |
| 7. Coupling math regression | Reuse existing 1D-2D coupling tests (US FLOW_UNITS, legacy header-less mesh) — must produce identical results. |

## 7. Affected files (as-built)

GUI:
- `include/map/spatialreferencesystem.h` + `src/map/spatialreferencesystem.cpp` — `planarLinearUnit()`.
- `include/mesh/inpmeshwriter.h` + `src/mesh/inpmeshwriter.cpp` — `UnitInfo` (descriptive), header emission.
- `include/mesh/inpmeshreader.h` + `src/mesh/inpmeshreader.cpp` — `;; UNITS:` / `;; SOURCE_CRS:` capture, legacy warning.
- `include/ui/dialogs/meshgenerationdialog.h` + `src/ui/dialogs/meshgenerationdialog.cpp` — frame validity, descriptive metadata plumbing.
- `src/ui/dialogs/crsselectiondialog.cpp` — synthetic `Local` group.
- `src/swmmvisprojectwindow.cpp` — "Use local projected" button.
- `src/swmmvis.cpp` — surface the reader's warning; no divide-back (XY already in project units).
- `docs/MESH_CRS_UNIT_CONVERSION_PLAN.md` — this document.

Engine:
- `src/engine/2d/data/SolverOptions2D.hpp` — `mesh_units_si` flag.
- `src/engine/2d/input/SectionHandlers2D.hpp` + `.cpp` — `prescan2DUnitsHeader` helper; called from `load2DMeshExternalFile`.
- `src/engine/core/SWMMEngine.cpp` — call `prescan2DUnitsHeader` on the inline `.inp` after `register2DSections`.
- `src/engine/2d/SurfaceRouter2D.cpp` — gate the mesh-side scaling on `!options_.mesh_units_si`.
- `docs/2d_external_mesh_file.md` §5.6 — units contract (both formats).

## 8. Follow-ups (not in this change)

- F1. Switch the GUI to SI-on-disk by default once the engine update has shipped widely. Would simplify the producer side and also fix the latent CRS-vs-FLOW_UNITS mismatch problem mentioned in §1.
- F2. Add a project-open warning when the project CRS linear unit doesn't match the FLOW_UNITS-implied unit (today this silently produces wrong physics for the 2D side).
- F3. Engine fuzz test that explicitly mixes FLOW_UNITS and `;; UNITS:` declarations to verify the gate.
