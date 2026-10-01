# Phase 35: combined sections and groundwater assignment

Status: Phase 35 supported-scope implementation and automated checkpoint complete; compiled, packaged, signature verified and launched. Phase 36 release/native gates remain explicit below.

## Delivered scope

Saved 2D sections combine the existing terrain/surface profile with named groundwater elevation lines and independently configured scalar tracks. Each result source retains its own cell mapping, report cadence, validity and units. The table and CSV identify requested versus effective time, dry/missing values and source identity. Definitions, display options and source references are project owned; Save Section is explicit, and paths are rebased for Save As. Missing primary sources and incompatible coordinate systems do not silently substitute another run.

A common groundwater assignment dialog captures selected cells, all cells, tags, typed cell IDs or a polygon in mesh coordinates. Manual values, vector attributes and rasters feed the same immutable preview. CRS failures, ambiguous overlaps, stale geometry/selection/model/source files and invalid values are rejected. Aquifer overrides preserve precision, inheritance and unrelated rows. SAT/UNSAT quality and signed well/source assignments use typed transactions, one undo operation and durable apply/undo/redo provenance. Region-total manual or series flow is distributed by cell area; sampled per-cell values retain their declared meaning.

The existing aquifer dialog is also included: no-op Apply, complete preflight, stale edit rejection, checked writes and rollback, explicit soil-law presence, and engine-close invalidation.

## Engine correction authorized by the user

The installed engine probe reproduced ignored positive/negative groundwater sources and species loading, plus LAYER initial quality incorrectly entering the bulk UNSAT store. Phase 35 therefore includes engine fixes, not just authoring controls. Constant and series sources require runtime water/species ledger evidence, chronological handling of flow reversal, save/reopen and hotstart continuity. The public contract distinguishes TOTAL flow over a matched region, CELL source scaling, native CONC and native pollutant mass units per second. New source ledger fields are appended to preserve previous positions.

Unsupported groundwater boundary chemistry without a hydraulic binding, sigma-layer quality and coupled RK2 routing must report explicit errors rather than accept inert or misdirected input. These restrictions are not claims of completed boundary or per-layer transport support.

## Verification

Evidence directory: `workplans/artifacts/phase_35_profiles_forcing/`.

- Retained baseline public-API probe: ten cases reproduced ignored source/sink water and species inputs and incorrect LAYER seeding. The old binary identity and results are retained separately.
- Corrected Release engine: **102 checks across five suites passed** (source arithmetic, groundwater transport, aquifer, authoring and hotstart). Fixed public-API probes passed against both the build and installed libraries.
- Installed engine: arm64 UUID **865C7A9B-4DF3-3248-AB22-6283F57348F6**. Packaging must be checked against this identity.
- Focused GUI-side tests: **96 checks passed** — profile series/codec 12, saved profile store 7, assignment history 5, combined widget/editor 9, aquifer assignment 11, spatial sampling 7, typed transport assignment 25, and existing groundwater editor 20.
- Aquifer preview benchmark: one million cells resolved in about **15.1 seconds**. This measures preview/resolution, not authoring throughput; exact per-row engine mutations remain a separate performance gate.
- Isolated GUI compilation and linking succeeded. **14 CTest suites passed (176 QtTest checks)**, covering the focused tests plus existing interpolation, shoreline, extrapolation, axis, scalar-layer and icon regressions. **Four application-object integration suites passed (19 checks)**: common assignment dialog, real project persistence, real combined sections and complete forcing journeys. Total: **195 GUI-side checks**, with zero failures or skips. Packaging and signing completed; strict deep signature verification passed, and the GUI launched with its own verified bundled engine.

## Supported quantities and explicit limits

A positive source injects groundwater; a negative source extracts available groundwater and its in-situ dissolved quality. GLOBAL/TAG named flow is a total distributed by matched active-cell area, while CELL rows retain per-cell flow and explicit SCALE. Concentration uses the species' native concentration unit; pollutant MASS uses its native mass unit per second. Unresolved multispecies MASS dimensions are rejected. Piecewise-linear flow and concentration are integrated together, splitting at reversals and knots. Hotstart and HDF/report ledgers retain separate source contributions.

Spatial inputs currently provide centroid-sampled intensive values or explicitly declared per-cell rates. Raster flux-density integration over overlap areas is not implemented. Hydraulic groundwater boundary binding and sigma-layer chemistry are unsupported and rejected. Coupled groundwater with RK2 is also rejected rather than silently running without groundwater. Cross-source elevations require declared matching vertical datums. Groundwater SAT/UNSAT HDF output now carries identity-aligned native species units, including groundwater-only runs; unknown units remain explicitly unavailable. Groundwater age authoring/output uses seconds without reinterpreting existing numbers. Immutable live groundwater chemical frames remain a separate gate.

## End-to-end evidence

The real selection/forcing journey passed: rectangle and polygon map tools, immutable preview, exact Apply/Undo/Redo, engine Save/reopen, and five-minute runs through manual, projected feature and raster input. Each route produced 0.6000000000000003 m³ injected water and 59.999999999999986 MG/L·m³ injected TSS inventory (60,000 mg). Water/species residuals were approximately 8.9e-16 m³ and 2.1e-14 MG/L·m³. Equivalent routes matched final storage and every recorded source ledger. These results are from actual engine runs, not authoring-only checks.

Project persistence passed four checks, including exact source-path restoration, legacy files without the new lists, no edit events from either new store during restore, and refusing to overwrite malformed sidecar data. The older global engine-version restoration path still marks a project dirty; the store-specific test does not misrepresent that behavior as fixed.

The first combined-profile run reproduced missing species units in real engine output. The new writer regression verifies native MG/L, UG/L and #/L, plus groundwater age seconds, before the installed library is replaced. The final real combined-profile journey passed: groundwater table/base and saturated TSS samples match the real HDF reader, styles/paths survive Save/reopen, CSV retains units/effective times, the combined figure renders, keyboard cursor movement works, and source removal invalidates the section. `review/section-journey/section-journey.oswp` is the ready-to-open review fixture. Both new main-window actions use the themed catalog; the icon suite covers them.

## Build isolation

Other ongoing tasks also rebuilt the shared `build` directory. Concurrent Ninja dependency-cache recovery and object replacement caused repeated work and unstable intermediate links. The final checkpoint uses **build-phase35** with its own generated files, application objects and dependency builds, while reusing the installed third-party package tree read-only. Earlier logs remain diagnostic history; the isolated final build/test/signature records establish acceptance.

## Packaged GUI and native launch

`build-phase35/SWMMVis.app` is the isolated checkpoint application. Build/deployment exited 0; strict deep signature verification passed. The installed and bundled arm64 engine UUIDs match **865C7A9B-4DF3-3248-AB22-6283F57348F6**. GUI PID **89192** launched at **2026-10-01 05:32:16 EDT**, and loaded-library inspection confirms it uses the bundle's engine. Native accessibility inspection confirmed the welcome window and file picker. The user changed the active window to Bellinge during the walkthrough, so further UI interaction stopped. The generated combined-profile fixture was validated through real application-object tests; it is not claimed as a completed native walkthrough.

The prior optional Mimer/iODBC/PostgreSQL plugin dependency diagnostics remain packaging gates. Signature success and native startup do not certify those optional drivers or release readiness. Details are in `final/native_acceptance.md` in the evidence directory.

For review, open `review/section-journey/section-journey.inp` with its matching `.oswp` sidecar, then **Analysis → Saved 2D Sections…**. For forcing, use **Model → Assign Groundwater…** after selecting the active mesh/cells. [Full testing steps](../../workplans/artifacts/phase_35_profiles_forcing/TESTING.md) describe both fixtures.

## Final integration phase

Phase 36 still owns native accessibility/visual acceptance, larger end-to-end projects, large-model apply/undo/parse/write throughput, and release acceptance including the separately modified mesh generator. Preview/resolution indexing is addressed here; the existing engine's per-row named-source and initial-quality setters still have linear lookup costs, so very large exact-row mutation throughput remains an explicit performance risk. No approximate grouping is introduced.
