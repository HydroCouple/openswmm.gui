# Phase 26 — selected GIS corridor authoring

Status: implemented, tested, compiled, signature-verified and launched. Native acceptance remains pending.

This bounded W6a checkpoint adds selected GIS centreline authoring to the approved corridor workflow. Source extraction, controls and persistence were implemented with separate ownership and one shared build coordinator.

## Behavior

- Generate 2D Mesh → Quality → Quads → GIS corridor sources captures selected road, river or other centreline feature IDs. Later map selection changes do not alter a captured row. Empty selection never means all features.
- Each source uses a constant total width or a numeric width attribute, an independent Across cell count, and Along spacing in mesh CRS units. Along 0 retains supplied vertices. Width is symmetric and constant along each feature; separate features may have different attribute widths. Each LineString or MultiLineString part produces a checked swept patch.
- Source extraction opens its own GDAL dataset on the worker. Missing files, sublayers, selected features, invalid widths, failed coordinate transforms or unsafe patches reject the whole request. No selected vertices or parts are silently omitted. The assigned source CRS is captured explicitly; the target must be planar with positive linear units. Recipes bind width and spacing to that target CRS.
- Requested patches use the existing placement and conforming stitching checks. Their footprints are excluded from the whole-domain quad background before stitching. Burn-in, conduits and a DTM are optional; normal elevation fallback remains available.
- Controls provide label buddies, accessible names/descriptions, checked numeric entry and multi-row removal. Validation reveals ancestor tabs, scrolls and focuses the invalid row. Missing map layers remain visible as stored references rather than disappearing from the recipe.

## State and persistence

The dialog edits a private draft. Only successful generation and guarded mesh adoption replace the project's recipe. Cancellation, worker failure, project/source changes and rejected adoption leave the previous recipe intact.

A versioned `meshCorridors` session block in `.oswp` stores datasource path, sublayer, explicit centreline role and selection mode, decimal-string 64-bit feature IDs, width/spacing, source and mesh CRS, selected-geometry digest and discovered dependency paths. Paths rebase during Save As. Publication uses the existing project Save transaction; generation does not publish final INP/2DM/settings files.

Unsupported or malformed recipes fail before session state is applied. A retained diagnostic blocks subsequent Save from erasing an unreadable recipe; successful, explicitly confirmed regeneration can replace it. Missing GIS files do not prevent loading the model/settings, but regeneration reports the unresolved source.

Every built-in Save protects corridor source and recorded companion paths from output conflicts, including after reopening and through symlink aliases. The reader compares source path identity, size and modification time during extraction, at worker completion and before adoption. The persisted selected-geometry/width digest detects changed selected input on later regeneration; users reselect and re-add changed features.

## Verification

The new reader's stub baseline reproduced 14 expected failures before implementation. The first production build exposed a Qt JSON iterator compatibility issue, which was corrected. Final verification passes **331 checks across eight suites**, with no failures or skips: reader 24, recipe codec 25, source widget 21, actual worker/adoption 34, production Save 133, existing patch dialog 25, patch geometry 41 and placement/stitching 28. Counts include QtTest setup/cleanup checks.

Worker regressions verify eight 6-by-2 corridor rectangles, total domain area preservation, source references and absence of final mesh publication for both triangular and whole-domain quad backgrounds. Adoption cases cover success, stale project, source change after worker completion, worker failure and Cancel. Additional cases check that draft collection binds the current target CRS without changing the project and that feature/schema edits invalidate an active job. Save tests cover source/companion conflicts and aliases, Save/Save As/reload, malformed recipe retention and invalid in-memory state. Widget tests cover selection snapshots, numeric fields, layer deletion, accessible controls, multiple-row removal and validation inside nested tabs/scroll areas.

Evidence and native testing steps: [Phase 26 guide](../../workplans/artifacts/phase_26_gis_corridors/README.md).

The final Release GUI build/deployment exited 0 with the pinned macOS 26.5 SDK. Strict deep signature verification passed. Installed/bundled engine UUIDs match `E31718A1-72CE-3B04-A7DA-AE5AA2C0A4A1`. New GUI PID **81686** launched **2026-09-27 16:47:42 EDT**, preserving existing PID 14087. Launch is process-verified; native inspection resolved the older running instance, so it is not counted as acceptance of the new controls. Existing optional Mimer/ODBC/PostgreSQL dependency diagnostics remain W9 packaging work.

## Remaining limits

This does not complete W6a/W6. Bank-pair selection, varying/asymmetric width, surveyed sections, directional mapped-region controls, junction construction and hydraulic/performance acceptance remain open. Existing typed patches and other mesh options are not part of this corridor-only recipe.

Source metadata checks are not a locked filesystem snapshot: a same-size outside rewrite with preserved modification time can evade the during-job check. Only GDAL-reported existing dependencies are captured; future companion files and concurrent database journal changes need broader snapshot work. Selected-geometry digests are checked on regeneration, not before every Save of an already-generated mesh. Plugin exporters retain their separate existing contracts; source-output protection here covers built-in Save.

Native keyboard, screen-reader, theme and mesh inspection remain acceptance tasks. Geometry validation is not hydraulic certification. Complete topology/quality/groundwater remapping and live editing-engine refresh remain W1 work; optional database-driver packaging remains W9 work.
