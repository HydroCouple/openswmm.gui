# Engine, species and forcing capability baseline

Evidence lives in `workplans/artifacts/phase_07_w0_baseline/runtime/` and the retained `probe_runtime.py`. The probe uses owned copies of Phase 06's two-cell compatibility decks, explicitly loads the installed library, records API return codes before/after initialization and at the end, completes the run, and inventories HDF5 output. It does not modify the engine installation, user models or plugin configuration.

## What the installed build actually establishes

Both decay-on/off runs complete successfully in 242 step calls. Two aquifer rows survive parsing. Before start, the aquifer active flag is zero and dimensions are unavailable; after start the flag is one, with two cells and eight reported layers. The report resolves both cells as CLOSED_FORM, so the layer-count field must not be interpreted as proof that SIGMA is active.

The groundwater state API returns initialized saturated thicknesses of 0.95 m and 0.50 m, water-table elevations of −0.05 m and −0.50 m, and finite unsaturated storage. **Those values are unchanged at the final sample**, although the surface report records approximately 0.321 m³ of infiltration. The reported groundwater continuity residual is zero. This is evidence of initialization/read access, **not** proof of hydraulic advancement or correct surface-to-groundwater transfer. Dynamic consumption and ledgers remain a gate.

Both reports explicitly warn that `[GW_*]` transport is authored but inert. Surface species concentration is written; groundwater hydraulic/species datasets are absent from these outputs. Decay-on/off is therefore not a passing groundwater transport comparison. The installed runtime has no authored `[PLUGINS]` rows in these fixtures; this does **not** enumerate built-in or discovered components. It reports CPU explicit local-inertial surface routing.

This refines Phase 06: do not describe all groundwater APIs or hydraulic initialization as absent. Equally, do not promote an active flag to solver-consumption acceptance. The source checkout has newer subsurface/transport and output code, including a different inert-warning condition; the installed binary cannot be attributed to that checkout's HEAD by its version string.

## API → output → GUI map

| Quantity/workflow | Current source/API evidence | Installed/runtime evidence | GUI gap and gate |
|---|---|---|---|
| Surface depth/WSE/velocity, scalar face results | `openswmm_2d.h`, engine output plugin, GUI `Mesh2DH5Reader`, `SWMM2DResultsLayer` | Existing file/live paths; not re-certified numerically by this phase | W4 configuration/legend/time/serialization/export parity. |
| Surface pollutant/MSX concentrations | Checkout `openswmm_sq2d.h`; file `Mesh2_face_species_conc`, rank `[time,species,face]`, `species_names` and `species_units` in current output source | `openswmm_sq2d.h` is absent from installed headers. Surface concentration dataset exists in the probe; header absence alone is not a binary symbol audit | W4a rank-3 reader and semantic catalog. Validate per-species units from actual files, stable identity and live API availability separately (ENG-3). Existing 1D species helpers are reusable, not proof of 2D support. |
| Groundwater hydraulic properties | `openswmm_gw2d.h` row/option/node authoring APIs | Two rows parsed; authoring uses project units, state uses SI | `Mesh2DAquiferModel`/groundwater dialog provide a row editor; reconcile every field/property/closure before spatial assignment. |
| Groundwater table elevation / saturated thickness / unsaturated storage | `swmm_gw2d_get_cell[_bulk]`; checkout datasets `Mesh2_face_gw_table_elev`, `_hg`, `_hu` | Initialization/state access confirmed; advancement and required file output unproven | W4a readers/catalog and W4b profiles must distinguish table elevation, thickness and storage. Never substitute HG for elevation. |
| Groundwater recharge, lateral, deep, ET, node exchange, Dunne return | State selectors and current engine output source describe signs/units and held last-firing values | Not accepted by these probes; groundwater outputs absent | Preserve per-area vs volume-rate units, sign and temporal held-value semantics; zero is not missing, and missing is not zero. |
| SAT/UNSAT groundwater species | `openswmm_gw_transport.h` setters; newer checkout transport implementation; two rank-3 zone datasets plus species ledger | Row authoring header present; transport warning remains inert, no datasets | W4a depends on executable consumption and ENG-2 units. Current source groundwater datasets say `units=1` and list names; that is insufficient for concentration conversion. |
| Groundwater initial quality / boundaries / transport sources | `swmm_gw_init_quality_*`, `swmm_gw_boundary_*`, `swmm_gw_source_*` and source-species setters | Authoring interfaces present; active transport not established | W4c new bindings, validation, undo and INP round trip. Node/link `InitialQualityDialog` remains inappropriate and disabled as a subsurface entry point. |
| Manual groundwater spatial targets | `MapToolPick2DCells`, `SelectionManager`, `MeshObjectRef`; vertex/edge tools for their own target types | New tests exercise box/lasso with no results; mixed cells retain cell indices | Reuse these gestures. Keep target mesh identity/revision, prevent cross-mesh references, and specify add/toggle/cancel consistently. Native journey still owed. |
| Vector/raster forcing | Existing `MeshAttributeAssignDialog`, mesh parameter metadata, aquifer row model | Source workflow exists for other attributes, not a complete groundwater forcing implementation | W4c one preview/commit path, source CRS/NoData/overlap policy, signed rates where allowed, conservative totals. Do not quantize continuous raster values into tags implicitly. |

## Unit and identity contract

Persist domain (`surface`/`groundwater`), zone where applicable, quantity, species identity/name, declared units, mesh identity/revision, time axis and missing-value status. Numeric array position is a run-specific lookup only. Missing metadata must produce an unavailable/explained state or explicit reconciliation, never guessed concentrations.

Profiles use one chainage, horizontal CRS, elevation datum and synchronized time basis. Surface WSE and groundwater table elevation can share the elevation axis; concentrations, rates and storage need separate tracks/units. Preserve disconnected/gapped sections and dry or absent results. Scalar rank-2 support does not automatically confer a rank-3 species or sigma-column reader.

For authoring, keep global → tag → cell precedence, explicit vs inherited values and GUI-to-engine index conversion visible in the model. Preserve unrelated tags and imported unsupported fields. Validate entire batches before commit; undo restores rows and selection-associated intent. Register overlap and no-data policies before vector/raster sampling. Measured exact-row performance determines whether ENG-4 bulk authoring is needed.

## Required next executable gate

Use a known hydraulic exchange case and a nonzero transport source/decay case against the engine build intended for release. Record library identity, process flags, resolved CPU/backend/components, input rows, initial/time-varying/final state, water and species ledgers, exact output shapes/units, and independent expected changes. A clean exit, an active flag, an exported symbol or an accepted input section cannot satisfy this gate alone. Refreshing/rebuilding the engine and proving these paths is a separate bounded implementation slice; this baseline does not alter the installation.
