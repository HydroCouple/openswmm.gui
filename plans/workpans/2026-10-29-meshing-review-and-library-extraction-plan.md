# Meshing review and independent library plan

**Date:** 2026-10-29

**Status:** Implementation started. See the [first-delivery record](2026-10-29-meshing-implementation-progress.md) for completed work, measurements and remaining phases. The review findings below describe the pre-implementation baseline unless stated otherwise.

**Scope:** Meshing rules, constraints, terrain refinement/coarsening, progress, standalone algorithm and file I/O, and six-million-scale operation.

**Evidence baseline:** Working checkout at commit af6c41d0262345aae48f66cb8410546a64ff4fcd, inspected on the date above. Existing unrelated working changes were preserved. This is a source review and implementation plan, not a new benchmark or completed numerical validation.

## 1. Recommended direction

Keep the constrained Delaunay kernel, robust predicates, terrain error index, spatial locality, caching, and structured patch machinery. Replace the scattered policy decisions around them with one explicit, independently testable meshing contract. Extract the implementation into a standalone CMake project inside this repository first; the GUI becomes a client. Keep geometry, terrain access, mesh file I/O, and SWMM-specific adaptation in separate targets.

The main changes should be:

1. Give minimum, maximum, transitions, terrain accuracy, and cell-shape preferences precise meanings. Publish exceptions with locations and reasons.
2. Preserve boundaries, holes, junctions, and authored breaklines unless their own policy permits bounded simplification. Separate simplification from operations that delete holes or close passages.
3. Offer three understandable cell modes: Triangles, Feature-aligned mixed, and Automatic mixed. Every mode uses the same sizing and constraint rules.
4. Keep normal-based flatness as an efficient coarsening priority. Use certified terrain residuals and local target sizes to decide whether removal is allowed.
5. Put structured, filterable progress and diagnostics in the meshing dialog, backed by the same events available to a CLI and future Python interface.
6. Establish correctness and performance gates at six million delivered cells and six million vertices, including the larger temporary triangulations those cases may require.

Do the behavior-preserving extraction before changing the numerical rules. This separates regressions caused by moving code from changes intentionally caused by the new policies.

## 2. Findings from the current implementation

### 2.1 What the GUI actually runs

~~~mermaid
flowchart TD
    A[GUI captures layers, model objects and settings] --> B[Worker reads geometry and terrain]
    B --> C[Optional boundary morphology and simplification]
    C --> D[Prepare constraints, holes, regions and fixed points]
    D --> E[Build terrain reference and sizing inputs]
    E --> F[Build constrained triangulation and structured patches]
    F --> G[Refine size and angle, smooth, refine terrain]
    G --> H[Optional constrained coarsening]
    H --> I{Open-area quad mode?}
    I -- Yes --> J[Find open blocks and generate mesh again]
    I -- No --> K[Reorder and fill final elevations]
    J --> K
    K --> L[Verify final terrain, assign model attributes and report]
    L --> M[GUI adopts and saves mesh]
~~~

The main worker is embedded in a 5,479-line dialog implementation. The generator and CDT implementation add 3,484 and 2,222 lines respectively. The architecture is modular in places, but orchestration, geometry policy, terrain access, model semantics, and user reporting remain entangled.

### 2.2 Evidence and implications

| Finding | Source inspected | Implication for this work |
| --- | --- | --- |
| Minimum size clamps the target field. Refinement uses a separate cascade floor of 0.25 times that minimum. | [meshgenerator.cpp](../../libraries/meshkit/src/mesh/meshgenerator.cpp), constants near 523 and refinement near 2940 | A minimum target is not a promise about the shortest delivered edge or smallest cell. |
| The generator multiplies target length by 1.22 before its equilateral-area refinement test. | Same file, 2915–2965 | The corresponding area multiplier is 1.4884. A strict maximum-size contract cannot quietly inherit this calibration. |
| Segment resampling can produce edges below the requested target even on a straight line. The earlier review gives h=10 over length 11 producing two edges of length 5.5. | [October 14 findings](../../workplans/MESH_ALGORITHM_REVIEW_FINDINGS_2026-10-14.md), sizing corrections | Distinguish target size, resampling interval, minimum edge length, and geometric feasibility. |
| Transition is mapped to background-field slope through transition minus one. The sampled field is not a proof of adjacent-cell ratios. | [dialog](../../src/ui/dialogs/meshgenerationdialog.cpp), 1496–1516; [sizefield.h](../../libraries/meshkit/include/mesh/sizefield.h) | Explain the field rule and independently measure delivered transitions. |
| Current GUI modes are Off, Corridors and strips, and Corridors/strips/open blocks. Open blocks use a second complete generation pass. | Dialog, 2030–2098 and 3676–3688 | Automatic mixed meshing already exists, but has a different path and substantial potential extra cost. |
| Current generator accepts mapped four-sided quad regions; other shapes fall back to triangles. Headers still describe broader Submapped and Free modes. | [meshquadregion.h](../../libraries/meshkit/include/mesh/meshquadregion.h), enum near 37; generator, 2385–2440 | Audit public modes against executable behavior; remove or explicitly migrate stale options. |
| “Off” gates terrain bank-pair strips and open blocks, but region capture and conduit strip-width propagation are not visibly gated by it in the inspected paths. | Dialog, 938, 1254, 4591–4596, 5064–5111 | High-priority reproduction: prove that Triangles truly produces no quads through every source. This is a source-level inconsistency, not a completed GUI reproduction. |
| Boundary conditioning performs closing/opening and short-edge collapse, potentially deleting holes or closing passages. Its checkbox initializes checked, before saved settings are restored. | [boundaryconditioning.h](../../include/mesh/boundaryconditioning.h); dialog, 582–610 and 3332–3341 | This is a geometry generalization policy, not merely redundant-vertex removal. Split it into separate controls and preserve old recipes explicitly. |
| Constraint joining derives a shared snap tolerance from 0.25 times the minimum, with smaller channel-specific limits. | Generator, 2190–2225 | Numerical welding, geometric simplification, and physical feature resolution need independent tolerances. |
| Coarsening is implemented, defaults off, and uses normal similarity for ordering with separate acceptance gates. | [meshcdt.cpp](../../libraries/meshkit/src/mesh/meshcdt.cpp), 1930–2185; [resolution settings](../../libraries/meshkit/include/mesh/meshresolution.h) | Extend this mechanism rather than resurrecting the old terrain point thinner. |
| Terrain reference may be averaged at the minimum target scale; final accuracy is checked against the selected reference. | Dialog, 737–784, 1520–1575, 2623 onward | Report the reference and averaging footprint alongside every accuracy claim. |
| Progress is a label/bar; detailed output goes through Qt logging. A generator callback reports vertex count which the GUI labels “cells.” | Generator, 2966–2968; dialog, 1935–1936, 3916–3925, 5258–5261 | Introduce typed counters and a persistent log list; do not infer cell counts from a generic callback. |
| Core values depend on Qt and include SWMM coupling/infiltration fields; geometry preparation uses application geometry helpers and QPainterPath. | [meshresult.h](../../libraries/meshkit/include/mesh/meshresult.h); generator includes; [pslgprep.cpp](../../libraries/meshkit/src/mesh/pslgprep.cpp) | Moving files alone will not create an independent library. Data ownership and dependency direction must change. |
| Without a DEM, the fallback IDW path loops over all seeds for every output vertex. | Dialog, 2280–2355 | Benchmark this path separately; its cost is O(vertices × seeds), unlike the terrain-index path. |

Earlier plans contain both implemented work and superseded proposals. In particular, the [October 14 findings](../../workplans/MESH_ALGORITHM_REVIEW_FINDINGS_2026-10-14.md) correct unsafe assumptions in the [coarsening proposal](../../workplans/MESH_COARSENING_PLAN_2026-10-14.md). Treat current code and those corrections as the baseline. Historical timing numbers are useful leads, not present six-million-scale evidence.

## 3. A single meshing contract

### 3.1 Priority and infeasibility

Use this order when resolving competing requests:

1. Valid topology and numerical validity: no inverted/zero-area cells, overlaps, illegal intersections, or broken adjacency.
2. Required domain components, holes, constrained lines, semantic junctions, and fixed points, after only the explicitly permitted geometry edits.
3. Explicitly hard user bounds: strict minimum/maximum requirements, required mapped regions, and required terrain coverage/accuracy.
4. Preferred resolution, grading, ordinary angle quality, and terrain detail under the selected reference.
5. Preferred cell shape and optional reduction of cell count.

Conflicting hard requirements produce an infeasible result with the responsible features identified. They do not authorize silently changing geometry. In practical mode, geometry may force small cells; those become localized, counted exceptions. In strict mode, the same conflict prevents acceptance.

Example: a preserved 0.5 m passage and a strict 2 m minimum width cannot both be represented by ordinary conforming cells. Offer a concrete choice among finer cells, a permitted geometry edit, or failure. Never silently close the passage. Resource limits stop work and report incompleteness; they never convert an unfinished run into a certified result.

### 3.2 Define size before enforcing it

Use physical lengths internally with explicit horizontal and vertical units. Reject angular/geographic coordinates as an unqualified Euclidean meshing frame; the I/O layer must supply an appropriate planar frame and transformations.

For scalar target size h, define the nominal cell metric:

- Triangle: h_eq = sqrt(4 A / sqrt(3)), the equilateral side with the same area.
- Quad: h_eq = sqrt(A), the square side with the same area.
- Directional quad region: retain separate along/across targets and explicit aspect limits.

Equal h means comparable nominal edge scale, not equal area between shapes. A square and equilateral triangle of equal side have different area. Also report actual shortest/longest edges, minimum altitude, minimum angle, aspect, and finite-volume length metrics; h_eq alone can hide slivers and stretched cells.

| Setting | Proposed meaning | Enforcement/reporting |
| --- | --- | --- |
| Minimum target size | Finest ordinary target allowed in the size field | Clamp field; report cells below target with cause. Does not itself prohibit short constrained segments. |
| Maximum target size | Coarsest nominal cell size allowed | Bound final h_eq using the local field, including quads; also apply shape/edge-span limits. No hidden 1.22 factor in the new strict contract. |
| Local target | Preferred upper resolution scale in a region or near a feature | Fine regions remain fine even when terrain is planar. Most restrictive applicable cap wins; incompatible directional requirements fail preflight. |
| Strict minimum cell/edge requirement | Optional delivered geometric floor, with the exact metric named | Validate feasibility; reject unresolved violations instead of claiming the target clamp enforced it. |
| Transition | Requested maximum growth between neighboring targets/cells under a named metric | Construction rule plus independent final audit; unavoidable geometric exceptions identified separately. |
| Angle quality | Requested minimum angle, subject to declared geometric exemptions | Count exempt/nonexempt failures; give locations and insertion cost. |

The size field should combine background, feature, region, and terrain requests by the most restrictive active upper bound, then apply the minimum target clamp. If terrain asks for a finer size than permitted, record an accuracy conflict; clamping must not erase the evidence of that request. Coarse region requests cannot override intersecting protected fine features.

Support an explicit “No maximum” option with a resource estimate and a declared terrain policy. For normal workflows, a finite maximum is easier to explain and test. Migrate existing projects into a named legacy policy so they retain their prior calibration and behavior until deliberately changed.

### 3.3 Transitions and final enforcement

Keep a gradation-limited field as the initial construction mechanism. For a selected adjacent-size ratio R, solve the discrete target constraints so h_i <= R h_j across the field graph in both directions, and state the graph spacing/interpolation used. This field property alone does not prove a delivered mesh ratio.

After generation, audit adjacent delivered cell sizes and shape-aware face lengths. Refine transition bands or adjust patch boundary subdivisions locally, then revalidate. Bound repair iterations and report unresolved cases. Area ratios and length ratios are separate: area grows approximately with the square of length for similar shapes.

Avoid centroid-only certification of a spatially varying maximum. A cell crossing a fine strip must use a conservative bound over its footprint or a proven subdivision test, not a coarse centroid sample. Apply the same rule to coarsening and quad acceptance. Develop the stronger contract behind versioned settings; do not change legacy results during extraction.

## 4. Configurable boundary and line simplification

### 4.1 Separate three operations

1. **Numerical cleanup:** exact duplicate removal, consistent closure/orientation, robust intersection splitting, and explicitly bounded coordinate welding.
2. **Shape-preserving simplification:** remove redundant vertices within geometric and semantic tolerances while preserving topology.
3. **Geometry generalization:** delete a small hole, close a passage, merge polygons, or change connectivity. A separate, opt-in operation with a before/after preview and quantified changes.

Expose policies by source layer, feature, ring, and selected segment. Feature-level overrides inherit from their source; shared edges receive one canonical policy. The stricter applicable protection wins unless the configuration is contradictory, in which case preflight reports a conflict.

| Policy field | Purpose |
| --- | --- |
| Geometry mode: preserve / simplify / generalize | Makes allowed changes explicit. |
| Maximum XY deviation, in map-display units | Bound displacement relative to the original geometry. |
| Maximum Z deviation | Preserve surveyed vertical shape on 3D lines. |
| Protected corners and turn threshold | Keep significant bends without treating small-angle similarity as sufficient proof. |
| Absolute/relative area-change limits for rings | Bound loss of footprint area as well as lateral displacement. |
| Protected endpoints, junctions, markers and attribute changes | Preserve topology and hydraulic meanings. |
| Minimum clearance to other constraints | Prevent shortcuts across holes, neighboring boundaries, banks, or channel toes. |
| Optional size-relative tolerance alpha × h_target | Permit less simplification in fine areas; cap by the absolute XY/Z tolerances. |
| Resampling policy | Distinguish removal of source vertices from insertion of needed mesh vertices. |

Use the prescribed target field, not the evolving output cell size, to set size-relative tolerances. Otherwise simplification can coarsen the mesh, which permits more simplification, creating feedback. Prefer preserving authored boundaries by default in the new policy. Suggested derived terrain breaklines can use a separately documented simplification preset.

### 4.2 Algorithm and invariants

Build a planar constraint graph with stable source identities. Normalize intersections and preserve the vertices where endpoints, junctions, tags, boundary conditions, region boundaries, or survey attributes change. Store shared boundaries once to prevent independently simplified polygons from leaving gaps.

Generate removable candidates using bounded polyline simplification, then validate each shortcut against an indexed neighborhood. Require a symmetric source/output distance bound, ring validity, hole containment, component count, orientation, clearance, and area/Z limits. An implementation must actually certify the chosen distance metric; a one-sided RDP tolerance alone is insufficient. Measure cumulative change against the original geometry, not only the latest simplified version.

Do not equate “nearly straight” with “safe to delete.” A collinear vertex may carry a coupling point, a boundary-condition change, or an important Z break. Conversely, extra collinear survey vertices without such meaning should be removable even on a required boundary.

Use robust predicates for intersection/orientation decisions and a spatial index for candidate tests. Avoid every-segment-against-every-segment scans. Tiles require shared seam ownership and validation against neighbors; independent per-tile simplification is not a topology guarantee.

Return a change report mapping each original feature to retained/removed vertices, maximum deviations, area change, and rejection reasons. Preview original and proposed geometry together. A failed local edit rolls back locally; the final whole-domain validity check remains mandatory.

## 5. Triangles, quads, and automatic balancing

### 5.1 User-facing modes and precedence

| Mode | Intended behavior |
| --- | --- |
| Triangles | Triangular cells throughout. Preserve line and region constraints. Explicit quad-only requests are reported as conflicts or deliberately converted by policy; they cannot silently survive the mode. |
| Feature-aligned mixed | Prefer mapped quads in valid explicit regions and channels/corridors. Use triangles around junctions, irregular boundaries, and transitions. |
| Automatic mixed | Feature-aligned mixed plus opportunistic quads in suitable open areas, evaluated against sizing, terrain, geometry quality, and cost. |

Local policies are “inherit,” “triangles,” “prefer quads,” and “require mapped quads.” A local triangle exclusion blocks automatic quad proposals. Conflicting required local policies fail preflight. Global Triangles has a clear conflict report for required quads. Preferred quads may fall back with a reason; required quads may not.

Do not define “automatic balance” as a 50/50 cell ratio. Mesh composition should follow the geometry and error contract. Report both quad fraction by count and by covered area; these can differ substantially.

~~~mermaid
flowchart TD
    A[Candidate area] --> B{Triangles required?}
    B -- Yes --> T[Use triangles]
    B -- No --> C{Required mapped region?}
    C -- Yes --> D{Conforming patch satisfies all hard bounds?}
    D -- No --> X[Report infeasible region]
    D -- Yes --> Q[Use mapped quads]
    C -- No --> E{Preferred patch or automatic candidate?}
    E -- No --> T
    E -- Yes --> F{Geometry, terrain, size and transition gates pass?}
    F -- No --> T
    F -- Yes --> G{Benefit exceeds transition and construction cost?}
    G -- Yes --> Q
    G -- No --> T
~~~

### 5.2 Construction strategy

First preserve the present behavior behind the adapter and reproduce every mode, including the suspected Off inconsistency. Then consolidate patch planning before final triangulation:

- Plan explicit mapped regions, corridors, and suggested open blocks from the constraint graph and target field.
- Validate opposite-side subdivision counts, corner quality, positive Jacobian, convexity, aspect, terrain twist, boundary conformity, and required transition widths.
- Reserve accepted patches and triangulate the remainder once. Shared patch interfaces must have identical vertices and edges, with no hanging nodes or overlaps.
- Revalidate terrain after actual elevations are assigned. An optional patch that fails can be split or locally retriangulated; a required patch reports infeasibility.

Compare two optional open-area approaches under the same audit: field-driven block planning and local triangle-pair recombination. Existing quad matching/cleanup code is a review candidate, not automatically a production dependency. Pairing must never remove a constrained edge, cross an exclusion, or form a concave/twisted quad. Use bounded local improvement; avoid a global matching algorithm whose memory or complexity is unsuitable at six million cells.

Select an approach through measured total construction cost, transition-cell overhead, terrain error, and downstream solver behavior. Retain the current two-pass route only as a compatibility/reference path until the replacement meets parity gates. Do not promise that more quads always mean faster hydraulics: geometry quality, flow, depth, and solver controls also matter.

## 6. Current thinning, and how to make it resolution-aware

### 6.1 What existed previously

The retired implementation inspected at revision bf1d5a2a9^, src/mesh/dtmthinner.cpp, computes triangular fans over active raster-grid neighbors. It forms an area-weighted vertex normal, compares surrounding unit face normals using a minimum or average dot product, and removes sufficiently flat candidates over bounded passes. Survivors become terrain point inputs to the mesher.

Thus the remembered normal-comparison idea is present in that history, but this retrieved version uses a raster-neighbor fan rather than a full independently maintained Delaunay terrain triangulation. Its angular criterion does not certify vertical error against all original terrain samples, and retained points can impose density independently of the intended size field.

The newer terrain workflow uses a selected DEM reference, a spatial error index, terrain-error-driven insertion, and final delivered-cell checks. The legacy terrain-size estimator is another path and should not be confused with the retired normal-based point thinner. Preserve these distinctions in names and migration documentation.

### 6.2 What current coarsening does

For an eligible free vertex v, the implementation forms each incident face cross-product c_f, with unit normal n_f = c_f / |c_f|. It computes:

    n_bar = sum(c_f) / |sum(c_f)|
    flatness(v) = min over incident faces of dot(n_f, n_bar)

Because |c_f| is twice the 3D triangle area, the average normal is already area weighted. Larger incident faces influence its direction more. This is not a resolution-dependent tolerance, and the current score contains no explicit target-cell-size factor. A score near one ranks a locally planar vertex favorably.

The queue prioritizes coarse Morton tiles for locality, then flatter vertices within the tile, with deterministic ties. It is not a global flatness sort. When terrain mode is inactive, the score is constant. Candidates exclude original fixed inputs, constrained vertices, and protected regions/locations. The pass is serial; neighboring candidates can be requeued after a successful edit, up to the configured attempt limit.

Removal is transactional: prepare and legalize a replacement cavity, then check topology, shortest-edge nondegradation, the active angle/exemption rule, local area target, and terrain fidelity. Terrain acceptance uses a separate coverage-aware bound or a conservative bound carried from prior certified replacements. Unknown coverage rejects removal. The refinement witness error is not a safe substitute for this certificate.

Coarsening defaults off. The previous review found that locally acceptable removals could still worsen global angle percentiles, and cell removal after hitting a cap did not complete terrain refinement that had already been skipped. These remain explicit acceptance questions.

### 6.3 Proposed resolution weighting

Keep normals as a cheap ordering signal. Make terrain acceptance depend on an immutable prescribed target field, and use the same spatial tolerance for insertion, removal, quad checks, and final verification:

    epsilon_z(x) = clamp(epsilon_ref * (h_target(x) / h_ref)^p,
                         epsilon_floor, epsilon_cap)

Initial research settings: p=0 reproduces constant vertical tolerance; p=1 provides a simple scale-dependent option; p=2 is an experiment motivated by smooth-surface interpolation error scaling. These are proposals to measure, not established best defaults. epsilon_floor reflects data precision and an explicit user choice; epsilon_cap prevents coarse cells from tolerating arbitrarily large errors. For directional cells, use the finer prescribed scale for a conservative scalar policy until a directional error model is validated.

For example, with h_ref=10 m, epsilon_ref=0.10 m, and p=1 before clamping, a 2 m target permits 0.02 m error, a 10 m target 0.10 m, and a 20 m target 0.20 m. This directly expresses “finer areas preserve more terrain detail.” It cannot manufacture accuracy finer than the reference DEM supports.

Evaluate replacement residuals relative to local epsilon_z across the entire affected footprint. A conservative implementation uses the smallest applicable tolerance over that footprint; a tighter implementation bounds the normalized residual pointwise using the terrain index. Checking tolerance at the removed vertex or centroid alone is insufficient.

Preserve separate size gates so planar terrain inside a fine region does not coarsen beyond its prescribed resolution. If an optional priority score includes estimated normalized residual or cell savings, benchmark it against the current tile-local flatness queue. Ordering may change speed and the resulting mesh; it must never bypass the acceptance gates.

Compute normals with XY and Z converted to compatible physical units. Test metre/foot combinations: unit-dependent normals would otherwise change ordering. Prefer maximum angular deviation over an average-only score near sharp features, while protecting actual breaklines explicitly.

### 6.4 Averaging and missing terrain

Keep “terrain reference” separate from “allowed mesh error.” A mesh verified to an averaged DEM is not thereby verified to the raw DEM. In fine local regions, a globally averaged reference may have already removed the detail the user wants. Compare full-resolution verification with a conservative multiresolution reference that never averages more coarsely than the local prescribed scale. Include reference construction error if claiming an end-to-end bound against raw terrain.

NoData filling remains an explicit estimate with provenance, even when interpolation produces finite elevations. Unknown coverage must not become certified coverage. Reject strict terrain certification where coverage is absent; in practical mode, show interpolated areas as exceptions. After coarsening, rerun unfinished refinement when useful under a bounded repair budget, or retain the original incomplete status.

## 7. Standalone project and licensing boundary

### 7.1 Proposed project structure

Use a provisional in-repository location libraries/meshkit with its own project declaration, tests, install/export rules, notices, and dependency manifest. The name is provisional. The GUI links exported targets through add_subdirectory initially; later it can find a separately installed package or a sibling checkout.

~~~mermaid
flowchart TB
    GUI[OpenSWMM GUI and project model] --> AD[SWMM and Qt adapter]
    CLI[Standalone CLI] --> IO[Mesh file I/O]
    PY[Future Python bindings] --> CORE[Meshing core]
    PY --> IO
    AD --> CORE
    AD --> IO
    IO --> CORE
    GIS[Optional GIS and terrain adapter] --> CORE
    AD --> GIS
    CLI --> GIS
    CORE --> P[Robust predicates and standard C++]
~~~

| Target | Responsibility | Dependencies allowed |
| --- | --- | --- |
| meshkit::core | Geometry values, constraint graph, sizing, CDT, patch planning, refinement/coarsening, validation, structured events | Standard C++, audited predicates; no Qt, GDAL, engine, project, or Python headers in public API |
| meshkit::terrain | Error indexing and terrain queries against an abstract tiled provider | Core and small audited utilities; provider does actual file access |
| meshkit::io | Generic mesh readers/writers, attributes, deterministic metadata, streaming and transactional output | Core; optional format targets explicitly enabled |
| meshkit::gdal | Raster/vector import, CRS/unit conversion, geometry operations that require GIS libraries | Core/terrain and audited GDAL/PROJ/GEOS distribution dependencies |
| OpenSWMM adapter | GUI values, model coupling, hydraulic attributes, project paths, undo/adoption, existing recipe migration | Meshkit plus Qt and OpenSWMM; remains with GUI |
| Optional SWMM mesh-format codec | Parsing/writing documented mesh sections without engine calls | Generic I/O plus independent attribute schema; build independently if rights allow |
| CLI / future Python module | Headless execution and public API clients | Exported library targets only |

Extract the terrain/refinement code that currently lives in the dialog into an application-independent pipeline. The GUI only snapshots inputs, launches a job, consumes events, and adopts a completed result. Route optional channel/corridor generation through geometry and terrain interfaces; engine-based selection and coupling remain in the adapter.

### 7.2 API and ownership

Provide immutable input views and owned output buffers: coordinates, triangle/quad connectivity, constrained edges, region IDs, and sparse attribute tables. Intern marker/tag strings rather than copying strings per vertex or cell. Do not expose today's MeshResult wholesale: its SWMM coupling, infiltration, and application-specific data belong in extensions/adapters.

Use explicit status values such as Complete, CompleteWithExceptions, Infeasible, ResourceLimit, Cancelled, and Failed. A report records resolved policies, counts, exception causes, source IDs, timings, peak memory, selected terrain reference, algorithm/schema version, and reproducibility information. Preserve stable internal IDs during edits; compact and return remapping only at controlled boundaries.

Use callbacks or interfaces for cancellation, bounded event delivery, size/terrain providers, and I/O. No process-global logger interception, GUI translation, or application singleton in the core. Specify callback lifetime, threading, exception behavior, and buffer ownership. A C++ API is sufficient initially; add a narrow versioned C ABI only if an actual consumer needs it.

### 7.3 File I/O extraction

Inventory the actual supported formats and field semantics before moving code. The current candidates include the [INP reader](../../libraries/meshkit/src/mesh/inpmeshreader.cpp), [INP writer](../../libraries/meshkit/src/mesh/inpmeshwriter.cpp), [SMS 2DM reader](../../libraries/meshkit/src/mesh/sms2dmreader.cpp), [mesh export](../../src/io/meshexport.cpp), and [stage cache](../../src/mesh/meshstagecache.cpp). The INP paths include model-specific fields; split generic geometry serialization from project-section updates and model ownership rules.

The standalone codec must support triangles/quads, stable external IDs, markers/regions, optional Z, units/CRS metadata where supported, and declared loss of unsupported attributes. For formats lacking fields, offer a versioned sidecar or report the loss. Do not silently roundtrip away boundary identities or coupling metadata.

Stream large text/binary inputs in bounded buffers. Validate counts, indices, overflow, NaN/Inf, duplicate IDs, truncated records, and format capabilities. Writes use a temporary destination followed by atomic replacement where supported; cancellation or failure leaves the prior file intact. Preserve relative-path and Save As behavior in the GUI adapter. Test boundary-edge orientation and cell-index remapping after reorder. Cache keys include source identity, CRS/units, simplification policies, terrain reference/tolerance, and algorithm/schema version; stale caches cannot silently survive behavior changes.

### 7.4 Licensing work required

Moving GPL code into another directory or library does not change its license. Audit authorship, copied code, contribution rights, dependency licenses, and existing notices for every extracted file before offering a permissive release. A copyright holder can release its code under multiple licenses; rights to third-party contributions require separate resolution. The GPL GUI can consume a compatible permissively licensed library while the standalone library remains available under its own license. [GNU GPL FAQ](https://www.gnu.org/licenses/gpl-faq.en.html#ReleaseUnderGPLAndNF), [compatibility guidance](https://www.gnu.org/licenses/gpl-faq.en.html#WhatDoesCompatMean).

Propose a permissive license, with BSD-3-Clause or Apache-2.0 as review candidates, after the ownership inventory. Keep existing notices unchanged during extraction until that decision is documented. If rights are unclear, preserve the current license for those files and resolve consent, replacement, or exclusion before independent release. Prior GPL releases are not retroactively withdrawn.

The current build vendors Shewchuk's public-domain predicates; that status does not extend to the separate Triangle mesher. Preserve provenance and avoid importing Triangle code under an assumption that it has the predicates' terms. [Author's predicates page](https://www.cs.cmu.edu/~quake/robust.html), [Triangle terms](https://www.cs.cmu.edu/~quake/triangle.html). GDAL describes its core as MIT-style licensed, but audit the actual shipped driver/dependency set as well. [GDAL overview](https://gdal.org/en/stable/about.html).

Independent-release gate: the core builds, tests, and installs without the GUI, Qt, engine, or Python; the optional I/O/GIS targets have complete dependency notices; a clean downstream sample links installed targets. Merely linking a new target inside the GUI is not sufficient evidence of independence.

### 7.5 Python readiness without premature packaging

Design the API now for array-oriented bindings: float64 coordinates, checked connectivity arrays, integer marker IDs, explicit units, owned result lifetime, and a structured report. Avoid one Python object or callback per cell. Release the GIL during native work; reacquire only for bounded Python interactions, or queue events for polling. Cancellation must work while native work runs. pybind11 requires explicit GIL release; it does not do it automatically. [pybind11 documentation](https://pybind11.readthedocs.io/en/stable/advanced/misc.html#global-interpreter-lock-gil).

Later add a small binding prototype, NumPy views with explicit ownership/read-only rules, CLI/Python parity tests, and wheel builds. Bindings must depend on the installed standalone package and must not import GUI headers. Package the optional GIS stack separately if that materially reduces install size and dependency burden.

## 8. Progress and log list

Add an always-available Run log panel within the meshing dialog, next to a concise current-stage indicator and progress bar. Use a virtualized list model with time, severity, stage, message, and optional source/region link. Provide search, severity/stage filters, copy/export, auto-scroll that pauses when the user scrolls back, and a final summary that remains after completion or cancellation.

Define typed events: StageStarted, Progress, Diagnostic, GeometryChange, ConstraintConflict, StageCompleted, and RunCompleted. Every event carries a job ID and monotonic sequence number. Progress uses separate live vertex, triangle, quad, and total-cell counters. Include attempted/accepted/rejected refinements and removals, cap reasons, and terrain exceptions without creating a row per entity.

Throttle normal updates to approximately 5–10 per second. Aggregate repeated warnings by code and source; retain total counts and bounded example IDs. Keep a bounded visible history, for example 10,000 rows, with a streamed JSONL/text report when full persistence is requested. The event queue must also be bounded and coalesce progress under load. Never block hot meshing loops on the GUI thread.

Use stage fractions where a denominator is known, and indeterminate progress plus counts otherwise. The current fixed 45% generation update and a possible 70%→45% second pass should become explicit stage/substage progress. Do not manufacture precise ETA values before enough work is measured. Cancellation checks must extend to geometry preparation, patch placement, long reads, and writing, not only refinement.

Example list content:

| Time | Level | Stage | Message |
| --- | --- | --- | --- |
| 00:02 | Info | Constraints | 12,400 redundant vertices removed; largest deviation 0.018 m; all 308 holes retained |
| 00:08 | Info | Patches | 42 mapped patches accepted; 6 preferred patches use triangles because of terrain error |
| 00:21 | Warning | Refinement | 24 cells below minimum target beside protected passage P-18 |
| 00:38 | Info | Coarsening | 83,240 vertices removed; 1,028 candidates rejected by terrain coverage/error |
| 00:46 | Info | Validation | 2,140,000 cells; completed with 24 declared size exceptions |

These numbers illustrate formatting only; they are not measurements from this review. Selecting a diagnostic should highlight affected features or a bounded sample of cells without constructing millions of GUI objects.

## 9. Performance at six million vertices and cells

### 9.1 Capacity model

Test cell and vertex limits independently. A large triangle mesh with six million cells often has roughly three million vertices; six million interior vertices can require about twelve million triangles. A six-million-cell quad mesh has a different relationship. Holes and boundaries modify those counts. Output limits must be distinct from temporary topology capacity, input point counts, edge counts, and DEM pixel counts.

Use 64-bit counts, byte sizes, offsets, checked arithmetic, and file positions throughout. Compact 32-bit connectivity is reasonable when validated against capacity, but all allocations and products need overflow checks and room for transient entities. No silent truncation of Python array shapes or file counts.

### 9.2 Memory and complexity

Keep coordinates/connectivity contiguous, intern attributes, reuse scratch storage, reserve capacity deliberately, and avoid full mesh copies at worker-to-GUI handoff. Measure simultaneous residency of CDT, patches, output, elevation caches, spatial indices, renderer, old GUI mesh, and undo state. Do not hide memory growth by measuring only the kernel.

Illustrative raw sizes at six million entities: XYZ doubles are 144 MB; four uint32 corner IDs per cell are 96 MB; a 32-byte triangle record is 192 MB; one 64-bit value per entity is 48 MB. These are decimal MB arithmetic, not measured structure sizes or a complete budget. Strings, hash-table overhead, dead slots, queues, staging copies, and terrain tiles can dominate.

Initial engineering targets, to be confirmed in the baseline phase: on a designated 32 GiB machine, at most 8 GiB process peak for the agreed six-million-cell reference cases and 12 GiB for the six-million-vertex/transient-topology cases; leave usable headroom for the GUI and operating system. Revise targets transparently if baseline evidence demands it. Enforce a configurable byte budget before large allocations and during refinement; reducing cache residency must not reduce requested terrain fidelity.

Preserve the existing Morton/Hilbert locality, terrain summaries and bounded raster tiles. Profile and address:

- The second full generation for open blocks and simultaneous old/new result buffers.
- Hash-based coordinate/elevation lookup and per-entity string/application payloads.
- All-seed IDW in the no-DEM path; compare a documented spatially bounded interpolator against current global IDW rather than silently claiming numerical parity.
- Repeated scans over holes/regions and large-polygon quadratic validation.
- The NoData propagation/relaxation path on large contiguous gaps.
- Per-candidate allocations, stale priority-queue entries, and retained dead topology.
- Reprojection, cache misses, final terrain verification, file output, and GUI adoption/render preparation.

Target near O(N log N + K) for the principal indexed stages, where K represents actual constraint intersections/local work, and bounded linear passes where possible. Report pathological inputs rather than asserting a universal complexity guarantee. Keep topology mutation deterministic and serial initially; parallelize independent reads, terrain tiles, and validation first. A colored tile schedule alone does not establish safe parallel cavity edits.

### 9.3 Benchmark matrix and gates

Run cold-cache and warm-cache cases at about 0.1 M, 1 M, 3 M, and 6 M delivered cells, plus six-million-vertex input/output capacity cases with adequate internal capacity. Include triangles, explicit mapped patches, automatic mixed mode, highly constrained urban holes, long narrow domains, steep/noisy DEMs, sparse local detail, no DEM with many seeds, large NoData gaps, and projected CRS reprojection. Scale hole count and DEM pixels independently of cell count.

Use deterministic synthetic fixtures plus Bellinge and Boston reference cases from earlier reviews, read-only copies of user data, and existing large mesh files where useful. Loading or rendering a prebuilt five-/six-million-cell file is not a generation benchmark.

Record hardware/OS/compiler, build type, thread count, source/recipe hashes, peak process RSS, allocation peaks, stage times, file I/O, output counts, error distributions, exceptions, and cancellation latency. Use repeated trials and medians/tails; keep raw reports.

Proposed release gates:

- Behavior-preserving extraction: equivalent output and reports, no material timing/RSS regression; investigate median regressions above 5% on controlled runs rather than accepting noise as proof.
- Comparable 3 M→6 M cases: time growth target no more than 2.5× and peak-memory growth no more than 2.3×, with geometric workload held comparable.
- Meshing log/UI updates remain responsive, ordinary cancellation observed within 1 s, and bounded external-I/O cancellation within 5 s or explicitly reported as pending.
- All correctness gates pass at target scale with the log panel active; bounded memory and valid output on cancel, disk-full, cache corruption, and resource exhaustion.
- Absolute completion-time commitments are set from measured reference hardware and workloads in phase 0, not invented here.

## 10. Validation and independent acceptance

| Area | Required cases and evidence |
| --- | --- |
| Topology | Robust orientation, adjacency reciprocity, no overlaps/hanging nodes, correct domain-minus-holes coverage, preserved connected components, constraint-chain coverage, stable markers and junctions |
| Size | Uniform and graded fields; local fine patches; h_min=h_max; absent maximum; small-angle/narrow-gap conflicts; resampling interval effects; final h_eq/edge/altitude distributions and every exception |
| Transitions | Fine/coarse interfaces, explicit quad seams, anisotropic regions, sharp target changes; independently measured adjacent ratios and localized repair outcomes |
| Simplification | Collinear duplicates, curves, small holes, touching/shared rings, bank crest/toe, Z breaks, marker/BC changes, near crossings, large coordinate offsets, cumulative deviation and rollback |
| Cell mode | Global and local combinations; Off/triangles through every source; mandatory versus preferred quads; failed patches; no constrained-edge removal; positive quad Jacobians and terrain twist |
| Terrain | Plane, tilted plane, ridge, step, saddle, curved hill, quantized DEM, mixed XY/Z units, NoData, averaged/full references; p=0/1/2 tolerance experiments and independent exhaustive checks on small fixtures |
| Coarsening | Random local removals with topology checks after each edit, protected inputs, cap reached before terrain, deterministic ties, cancellation, rejection accounting, global quality percentiles and total error |
| I/O | Existing INP/2DM fixtures, generic attributes, CRS/units, large IDs/counts, malformed files, precision, relative paths, Save As, reorder mappings, cancellation and atomic writes |
| UX | Log filters/export, long messages, no per-cell row explosion, multiple job isolation, cancellation, retained final report, preview of geometry edits and exception selection |
| Library | Clean standalone configure/build/install, downstream CMake consumer, headless CLI, no forbidden dependencies; later Python ownership/GIL/cancellation and parity |
| Hydraulics | Same forcing and solver configuration across baseline/new meshes; budgets, water levels, velocities, wet/dry front and runtime/substeps; geometry proxies are not substitutes |

Extend existing tests such as test_meshcdt, test_meshgenerator_v2, test_pslgprep, test_boundaryconditioning, test_sizefield_v2, test_terrainerrorfield, and test_meshterrainpipeline. Move genuine core tests out of GUI-dependent targets during extraction. Add an independent delivered-mesh checker that does not simply call the same acceptance routines being tested. On small synthetic terrains, exhaustive reference sampling verifies the certificate shortcuts.

Distinguish strict invariants from empirical objectives. A valid local minimum-angle gate does not guarantee unchanged global P1/P5. An error bound can remain within tolerance while increasing relative to the baseline. Report both threshold compliance and distribution changes; explicitly review acceptable tradeoffs before enabling a new default.

## 11. Delivery sequence and review gates

| Phase | Work and concrete deliverable | Exit gate |
| --- | --- | --- |
| 0 — Baseline and decisions | Reproduce current policies, Off inconsistency, terrain modes, caps, and simplification effects; inventory dependencies/rights; record representative performance and resource budgets | Versioned baseline recipes/results, issue list, dependency/license map, agreed size metrics and reference workloads |
| 1 — Observable contract | Introduce result statuses, typed events/counters, exception reasons, effective-setting report, and dialog log model; preserve existing geometry behavior | Accurate vertex/cell counters, bounded log overhead, no hidden incomplete success |
| 2 — Independent core and I/O | Add standalone CMake project, neutral types, pipeline orchestration, terrain provider, format layers, GUI adapter, and behavior-preserving tests | Builds/installs independently; GUI parity and no material regression; rights issues recorded before permissive release |
| 3 — Constraint policies | Implement per-source/feature simplification, canonical shared edges, previews, protected identities, and separate generalization | Topology/deviation/area/Z gates; migration preserves old recipes; no implicit hole loss in new preserve mode |
| 4 — Sizing and transitions | Introduce precise versioned min/max/local/transition rules, feasibility checks, footprint-aware caps, and bounded local repair | Independent checker verifies delivered metrics or explicit exceptions/infeasibility |
| 5 — Unified cell policy | Enforce three modes and local precedence; consolidate patch planning; benchmark single-pass planning and bounded recombination | All-mode tests, conforming seams, terrain/shape gates, reduced total cost where claimed |
| 6 — Resolution-aware terrain/coarsening | Add spatial tolerance option and reference policy; reuse certification; validate coarsen/refine repair and angle tradeoffs | Same tolerance contract across stages; fine-region preservation; numerical/hydraulic evidence supports any default change |
| 7 — Scale and hardening | Full scale matrix, peak residency reduction, I/O/fault/cancel work, documentation and migration | Six-million targets met with audit/logging on; reproducible reports and no correctness tradeoff hidden by resource caps |
| 8 — Independent distribution | Final rights/license decision, standalone release sample, later Python prototype and packaging | Installed-library consumer and binding parity; no GUI/engine dependency leaks |

Dependencies: 0 precedes all behavior changes; 1–2 establish the seams; 3–4 define the constraints consumed by 5–6; 7 validates each increment and closes the release gate. Phase 8 can prepare packaging earlier, but release waits on rights and independent-build gates. Each phase should be a small series of reviewable changes with its own fixture/report, rather than one meshing rewrite.

Rollout uses a persisted policy version. Existing recipes remain on legacy behavior, new recipes get the reviewed policy, and the GUI can compare both on a copied result. A failed run or comparison leaves the currently adopted mesh and user files intact. Document the selected reference, size semantics, quad fallback, simplification allowances, logs, and independent API together with the implementation.

## 12. Decisions to review

Recommended starting decisions are:

1. **Preserve topology by default**, with per-feature shape simplification; morphology is separately opt-in.
2. **Practical target sizes with explicit exceptions by default**, plus a strict acceptance mode for users who require hard floors/caps.
3. **Automatic mixed as an explainable preference**, with local triangle exclusions and optional mandatory mapped regions; no arbitrary quad percentage target.
4. **Constant terrain tolerance retained for compatibility**; size-scaled tolerance offered experimentally with p=1 first, and coarsening kept opt-in until validation supports changing it.
5. **A Qt-free core and separate I/O/GIS targets**, linked into this repository first and designed for an eventual standalone package.
6. **Permissive licensing only after the file-by-file rights/dependency audit**, without changing the GPL GUI license.
7. **Six million cells and six million vertices tested separately**, with explicit temporary capacity, byte budgets, and realistic terrain/constraint workloads.

The first implementation increment should produce the baseline report and accurate in-dialog log list. The next should establish the standalone library boundary while preserving output. Numerical and policy changes follow on that measurable foundation.
