# Phase 27 — directional mapped-region spacing

Status: implemented, tested, compiled, signature-verified and launched. Native acceptance remains pending.

This bounded W6a checkpoint implements the approved plan's independent directional spacing and explicit axis association for four-sided Mapped regions. Geometry, dialog and generator regression work were parallelized with separate ownership and coordinated builds.

## Behavior

Mapped regions accept separate positive Along and Across target spacings, in mesh CRS units. A physical axis angle (counter-clockwise from the mesh CRS +x direction) selects which opposite logical side pair is Along. This is an undirected guide direction: adding 180 degrees gives the same association. It is independent of polygon starting vertex and winding. The pair with the higher mean squared projection of normalized endpoint-chord directions onto the axis is selected; an ambiguous tie is refused. This does not rotate a polygon or force its sides to match an arbitrary axis.

Subdivision counts use each opposite pair's mean polyline length divided by its selected target spacing, rounded to the nearest positive integer. Actual edge lengths therefore depend on the polygon dimensions. Counts, finite values and index products are checked before casting or allocation. The existing single-spacing overload and default-off GUI behavior remain available.

Generate 2D Mesh → Quality → Quads → Quad regions provides the opt-in controls with labels, accessible descriptions, mesh-CRS unit guidance and validation that reveals/focuses the offending field. Auto must resolve to Mapped; explicit Free, Submapped and Triangles-only modes cannot use directional spacing. Directional requests that cannot be honored stop generation instead of silently reverting to another mode or skipping a region.

GIS region features can override the dialog using numeric `quad_h_along`, `quad_h_across` and `quad_axis` attributes. Set all three together; partial, null or invalid values within an override fail. All three unset/null inherit the dialog defaults. The angle is in the target mesh coordinate frame, even when the source layer uses another CRS. Existing Region-layer columns can be extended through Add column; this phase does not migrate source schemas.

Requested region files/sublayers that cannot be opened now fail generation. Directional GIS regions require known source/target CRSs, a planar target with linear units, successful transformation of every vertex, nonempty polygon geometry and no interior holes. Directional boundaries bypass RDP simplification. Failed reads cannot be treated as a complete source.

## Safety and compatibility

Directional validation uses a four-cell minimum based on Along × Across, without requiring a scalar spacing or maximum area. Invalid supplied corners, unsupported outlines, holes/background use, protected crossings, overlaps and failed mapped construction return an error without cell output. Positive-area overlap checks cover coincident and collinear boundaries for pairs involving a directional request; legacy overlap handling remains for unrelated isotropic regions.

Area and centroid calculations use a local origin to avoid cancellation at projected coordinates. A bounded coordinate-roundoff allowance applies to the directional minimum-area threshold. Structured patch placement, topology, cavity and stitching checks remain in force.

Dialog settings reset when the dialog is reopened, consistent with existing quad-region settings. Feature attributes persist in their GIS datasource through its existing editing workflow. This phase does not extend the Phase 26 centreline recipe or add general mesh-dialog persistence, external GIS snapshots, bank-pair selection or a map-picked guide. Existing generation/adoption/Save ownership rules continue to apply.

## Verification

The pre-implementation baselines reproduced **113 failures**: geometry 70, generator 30, worker 5 and dialog 8. Existing controls passed. The first production run passed the geometry, generator and dialog checks but exposed an overlap-gate interaction with a whole-domain background, recorded in `production_tests.log`. Additional review added translated minimum-area, empty-geometry and overlap-order controls.

A focused Qt probe reproduced that interaction: cancelling OddEven contours from boolean path operations were counted as positive shoelace area. The new overlap check intersects outer rings, subtracts holes and simplifies the common path before measuring filled area. Both the focused probe and the full worker regression are retained in the artifact directory.

The final run passes **362 QtTest checks across eight suites**, including setup/cleanup: patch geometry 115, region validation 33, submapping 8, generator integration 77, actual worker 44, placement/stitching 28, dialog 33 and GIS corridor reader 24. There are no unexpected failures or skips; the existing pinned-junction expected failure remains. Tests cover physical cell direction/dimensions, ring start/winding, rotation, large coordinates, scalar compatibility, source validation, failure propagation and dialog validation/focus. The targeted code whitespace check passes.

The Release GUI build/deployment exited 0 using the pinned macOS 26.5 SDK. Strict deep signature verification passed. Installed and bundled engine UUIDs match `E31718A1-72CE-3B04-A7DA-AE5AA2C0A4A1`. New GUI PID **12354** launched **2026-09-27 17:22:10 EDT**, preserving prior PID 81686. Launch is process-verified; this is not native manual acceptance. Known optional database-driver deployment diagnostics remain W9 work. Build/launch evidence and native steps are recorded in the [testing guide](../../workplans/artifacts/phase_27_directional_mapped/README.md).

Remaining W6a/W6/W7/W8 work includes bank-pair authoring, varying/asymmetric corridors, curved-feature fidelity, junctions/transitions and hydraulic/performance acceptance. This checkpoint does not make free-quad placement anisotropic. Native keyboard, screen-reader/theme and model inspection remain separate acceptance tasks; optional database-driver packaging remains W9 work.
