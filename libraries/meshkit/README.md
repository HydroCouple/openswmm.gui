# MeshKit extraction

MeshKit is an independently configurable, buildable and installable CMake project. OpenSWMM links the same compiled targets that a separate application can consume; there is one implementation of each extracted algorithm and codec.

| Target | Contents | Dependencies |
| --- | --- | --- |
| `meshkit::runtime` | Bounded job events, typed counts and run outcomes | Standard C++17 and threads |
| `meshkit::core` | Owned neutral arrays, triangle job API, constrained Delaunay refinement/coarsening and final checks | Standard C++17, threads, public-domain predicates |
| `meshkit::io` | Streaming neutral MKB and SMS 2DM codecs, atomic file publication | `core`; standard C++ filesystem |
| `meshkit::core_qt` | Constrained Delaunay refinement/coarsening, size field, patch/strip placement, quad regions/matching/cleanup, mesh geometry/statistics/reordering, natural-neighbor interpolation, shared geometry helpers | Qt 6 Core, Gui value geometry, Concurrent; public-domain predicates; `model_qt` |
| `meshkit::model_qt` | Existing mesh values, boundary-condition vocabulary and infiltration/cell parameters | Qt Core/Gui; currently also contains infiltration serialization helpers |
| `meshkit::io_qt` | Native INP/2DM mesh sections and SMS 2DM reader | Qt Core/Gui, `model_qt`, and native `io` |

The `_qt` names make the remaining transitional API explicit. These targets do not depend on Qt Widgets, the OpenSWMM application, the simulation engine, GDAL, or Python. **`runtime`, `core`, and `io` are Qt-free.** The native job currently supports triangles; mapped/mixed quad planning and the GUI's terrain/GIS and project orchestration remain in the Qt API. The native and Qt triangulation specializations share one algorithm implementation, with no GUI working-array mirror. See [the native API contract](NATIVE_API.md) for units, ownership, capabilities, error reporting, file schemas and examples.

## Build and use

```sh
cmake -S libraries/meshkit -B build/meshkit-standalone \
  -DCMAKE_PREFIX_PATH=/path/to/Qt/6/matching-platform \
  -DCMAKE_BUILD_TYPE=Release -DMESHKIT_BUILD_TESTS=ON
cmake --build build/meshkit-standalone
ctest --test-dir build/meshkit-standalone --output-on-failure
cmake --install build/meshkit-standalone --prefix /your/install/prefix
```

For standard C++ meshing, streaming I/O and events, configure with `-DMESHKIT_BUILD_QT=OFF`. Tests need Qt Test only when the Qt targets are enabled. There is no network fetch, sibling-checkout requirement, or application build dependency.

A downstream client:

```cmake
find_package(MeshKit 0.1 CONFIG REQUIRED COMPONENTS core io runtime)
target_link_libraries(my_mesher PRIVATE meshkit::core meshkit::io)
```

Include `<mesh/meshgenerator.h>`, `<mesh/inpmeshreader.h>`, `<mesh/inpmeshwriter.h>` or `<meshkit/events.hpp>`. The API retains `mesh::MeshGenerator`, `mesh::ConstrainedDelaunay`, and `mesh::MeshResult`. See [the standalone smoke test](tests/mesher_io_test.cpp) for generation and mixed-cell file roundtrip examples. Each generator is thread confined; inputs and results use owned Qt value containers. Callbacks execute synchronously on the generating thread and must not manipulate GUI objects. Callback lifetimes must span generation.

`GenerationOptions::allowQuads=false` disables generated quad regions, conduit strips and terrain strips, while retaining region boundaries, tags and local targets. A prebuilt quad patch conflicts with that mode and returns a diagnostic. Default `true` preserves existing callers.

The retained generator API's `MeshResult::ok` means that a candidate was produced. Consumers must also inspect `MeshGenerator::stats().refineCapped` before accepting a mesh; a capped candidate is incomplete. The GUI adapter exposes that distinction as the structured `ResourceLimit` outcome and refuses to apply it. The native triangle `runJob` API supplies the owned request/report contract and refuses partial/capped results; moving mixed quad planning behind that native contract remains later work.

## Progress and capacity

Each job owns an `EventChannel`, with thread-safe producers and a polling consumer. It coalesces consecutive progress updates and bounds pending rows and message lengths. Overflow has an explicit cumulative counter. Terminal events prevent later progress from replacing the outcome. Counts of delivered vertices, working vertex slots, triangles and quads are distinct; `-1` denotes unavailable data. The GUI adapter polls at 100 ms with a 10,000-row visible/exportable history. No process-global logging handler or QObject crosses the runtime boundary.

`RefineHook::onDetailedProgress` reports typed counts at bounded intervals and stage changes. `onDiagnostic` supplies generator diagnostics. The live triangle counter is maintained through splits, region removal and coarsening, so reporting does not scan millions of slots at every update. Working vertex slots may include unused vertices; delivered counts are reported after output assembly.

Build the opt-in `meshkit_scale_probe` with `-DMESHKIT_BUILD_BENCHMARKS=ON`:

```sh
build/meshkit-standalone/meshkit_scale_probe 2001 1501 # 6,000,000 triangular cells
build/meshkit-standalone/meshkit_scale_probe 3000 2000 # 6,000,000 vertices; 11,990,002 cells
```

This measures a uniform-grid CDT, four constrained sides, positive triangle areas, expected count/area, elapsed time and process peak RSS. It is a kernel capacity probe, not a terrain, codec, renderer or end-to-end meshing benchmark. It is deliberately excluded from normal CTest runs.

## Licensing and remaining extraction

Licensing remains GPL-3.0-or-later. Moving files does not grant a permissive license to the existing algorithm. Existing notices remain in each source. See [the provenance inventory](PROVENANCE.md). A file-level ownership/dependency review and explicit license decision are required before a permissive release. The vendored predicates retain their separate public-domain notice; the Triangle mesher is not included.

The native geometry, triangle jobs and generic streaming codec boundaries are implemented. Remaining boundaries include mixed quad planning, source/size-field construction, optional GDAL adapters, and the full project pipeline. The Qt CDT facade already uses the shared implementation with original Qt storage. Python bindings should consume the installed package with array-oriented buffers and explicit ownership; no GUI dependency should enter that interface.

## Constraint simplification

`<mesh/constraintsimplifier.h>` provides a collection-level policy pass. Features carry stable IDs, per-feature preserve/simplify policies, XY/Z limits, protected flags, corner and ring-area limits. The collection shares an immutable segment BVH; proposed shortcuts check original-span deviation, monotone projection, obstacles/clearance, pins and swept pockets. A final index checks output shortcuts against each other. Conflicts or work-budget exhaustion retain original geometry. The API returns source-vertex mappings and per-feature change statistics. It assumes a common finite planar frame and an intended original graph; it does not repair invalid input topology or generalize/delete holes.

Exact shared original segments are canonicalized into chains under the strictest owner protections. Each chain is simplified once and stitched back in its original direction. A failed ring-area limit restores the connected shared component. Different source sampling is retained conservatively. Work and estimated workspace limits return originals; cancellation returns failure. Limits bound temporary workspace, not total process memory.

`GenerationOptions::preserveConstraintCoordinates` disables proximity welding while retaining exact junction/crossing splitting. Its default is false for existing callers. The GUI enables it for the explicit preserve/bounded policies.

`<mesh/constraintaudit.h>` independently checks preparation mappings, retained vertices/heights, XY/Z/area/corners, original junctions, shared consistency, pockets and clearance. Its delivered audit checks required paths against incident cell edges, domain/hole side, markers and retained authored Z after final edits. Missing or incomplete coverage refuses adoption. This is a constraint audit, not a general arbitrary-cell-overlap certificate. Cancellation and resource exhaustion never report a successful audit.

`<mesh/constraintpreview.h>` builds bounded display data from an audited candidate. Display sampling changes neither audited geometry nor the generation request. The GUI offers per-source overrides, persistent physical units and preparation-only preview; these widgets are outside the library.

Opt-in probes `MESHKIT_SIMPLIFY_VERTICES=6000000 meshkit_constraints_test longFlatSourceHasBoundedWork` and `MESHKIT_SHARED_VERTICES=6000000 meshkit_constraints_test longSharedSourceUsesOneChain` exercise straight and shared source capacity, including independent preparation audits. These are not complex-boundary or end-to-end meshing performance guarantees.

## Delivered sizing and bounded repair

`<mesh/sizingaudit.h>` measures nominal triangle/square sizes, actual edge floors,
global and footprint maxima, and ratios across shared faces, including mixed
interfaces. Compact sorted face records avoid a per-face hash table. It validates
finite convex cell geometry and consistent face incidence; it does not certify
arbitrary nonincident overlap. Cancellation and work/estimated workspace budgets
return an incomplete audit. Advisory minimum targets are separate from the
optional hard edge floor. Independent checks use delivered arrays, not the CDT's
acceptance helpers. `footprintMaximum` supplies an immutable source-field bound.

`ResolutionSettings::Sizing` persists version-1 legacy, verified/reporting and
strict/refusal modes. Missing settings and legacy migrations select legacy.
`GenerationOptions::verifiedSizing` adds an uncalibrated nominal cap, and
`RefineHook::footprintMaximumSize` is required when a spatial point callback is
installed. `SizeFieldOptions::footprintBounds` builds a compact minimum pyramid;
`minimumSizeInBox` conservatively includes bilinear support and explicit region
bboxes, including narrow regions missed by grid samples. This may overrefine
around nonrectangular regions. Without the pyramid the public query scans the
requested grid range.

The CDT repairs after smoothing, terrain insertion and coarsening, with at most
three allowed passes (the GUI uses two) and a separate insertion budget (200,000
by default). Job cell limits, fixed stations and terrain/quality gates remain.
Ordinary constraints may be subdivided, never moved or removed by this repair.
Triangle transitions are repaired locally; fixed quad and mixed-interface limits
are independently audited and may remain exceptions or cause strict refusal.
Python/Qt-free extraction remains a separate phase. Unified planning below adds repair/refusal at fixed interfaces when explicitly enabled.

`MESHKIT_AUDIT_SIZING=1 meshkit_scale_probe 2001 1501` measures six million cells;
`... meshkit_scale_probe 3000 2000` measures six million vertices / 11,990,002
cells. Both include the delivered constraint and sizing audits. These uniform
grids are capacity probes, not complex terrain or GUI-render guarantees.

## Unified cell planning (opt in)

`ResolutionSettings::CellPlanning::Unified` is versioned independently and requires
verified/strict sizing. `GenerationOptions::unifiedCellPlanning` enables the core
path; a spatial size callback also requires a conservative footprint callback.
`QuadRegion::requireMapped` and directional spacing are hard requirements; ordinary
regions are preferences and `TrianglesOnly` is an explicit local priority. Global
triangles conflict with required mapped patches. Existing callers remain legacy.

`<mesh/cellplanner.h>` plans four-sided regions together using a source-segment BVH,
canonical exact side paths and joined opposite/shared station counts. Every authored
station is retained. The already-stationed Coons helper never resamples its boundary.
Both ring windings and projected coordinate origins are supported. Pins, protected
lines, holes and explicit triangle masks take priority over optional patches. Full
shared logical sides are coordinated; unmatched partial sides are refused or fall
back, requiring source decomposition. This is a conservative mapped planner, not a
general polygon quadrangulator.

Patches pass size-footprint, edge-floor, angle/aspect, directional-spacing and
certified terrain tests before CDT construction. Up to three repairs double joined
station counts. Defaults cap a patch at 250,000 cells and estimated planning workspace
at 2 GiB; these are separate from the complete job cell budget. A required failure,
cancellation or exhausted required budget returns no partial plan. Optional sources
can fall back to triangles. Geometry/certification callbacks are immutable and execute
synchronously on the job thread.

Fixed-boundary neighbour sizes let CDT repair oversized adjacent triangles using
storage proportional to patch perimeter. An independent delivered-interface audit
checks actual nominal ratios. Up to three exceptional regenerations can omit preferred
patches or refine required generated stations. Immutable fixed patches fail with an
actionable diagnostic. The normal valid path triangulates once. The GUI derives open
blocks from conservative field bounds before that pass, without a triangle pilot.
Count/area shares and bounded decision logs explain the outcome. Source geometry and
final terrain/sizing audits remain required in the GUI adapter.

`MESHKIT_PLAN_QUADS=1 meshkit_scale_probe 3000 2000` exercises six million mapped
cells across 100 conforming patches. This measures planning and patch acceptance,
not triangulation, DEM sampling, publication, or rendering.


## Resolution-weighted terrain and coarsening (opt in)

`ResolutionSettings::coarseningOrder=ResolutionAware` and
`terrainTolerancePolicy.mode=TargetSize` persist separate version-1 policies.
Absent settings and legacy migration retain flatness ordering, constant tolerance
and disabled coarsening. The GUI requires verified sizing for either new choice.
The tolerance law is linear (p=1), with physical reference size, optional floor
and an explicit cap; zero cap inherits the resolved reference tolerance. It is
experimental and does not change the selected DEM reference or claim raw-terrain
accuracy when that reference was averaged.

`<mesh/terrainpolicy.h>` supplies the scalar law and ordering heuristic.
`RefineHook::terrainToleranceForFootprint` and the corresponding CDT/patch callback
return the conservative positive tolerance for a whole affected footprint in Z
units. The callback is immutable for the job. CDT insertion, normalized worst
error ordering, coarsening's height/certificate checks, patch acceptance and final
GUI verification share the rule. Invalid values cannot disable a gate. Unknown
coverage rejects removal; missing certificate callbacks still skip terrain
coarsening. General terrain exceptions remain explicit; this is not a universal
strict terrain acceptance mode.

`SizeFieldOptions::terrainRegions` and `minimumTerrainSizeInBox` include finer
prescribed directional scales through the same compact region BVH. They do not
change `minimumSizeInBox`, the nominal size grid, or directional quad proportions.
The global nominal floor does not erase a finer explicit directional terrain
scale. Conservative region bounding boxes may overrefine nearby ground. Narrow
regions remain represented even without a minimum pyramid; the fallback scans
the covered grid range while retaining the indexed explicit regions.

`QualityOptions::coarseningTargetSize` supplies an immutable full-footprint target
(zero means unbounded). Resolution ordering also enforces this target on each
replacement triangle. `normalVerticalToXY` converts output Z units to the XY unit
for physical normals; the GUI supplies vertical-to-SI / XY-to-SI. The ordering is
tile-local: maximum normal departure times current nominal size / prescribed
target, then sampling density, then vertex ID. Unbounded prescribed size
uses `coarseningReferenceSize` for ordering; it does not invent a size cap.
Normals remain an ordering signal, never a terrain error certificate.

`<mesh/coarseningqueue.h>` maintains one live entry per vertex and preallocates the
known free-slot ceiling. Reconsidering neighbours updates an existing entry rather
than accumulating stale heap entries. Morton tiles retain locality and the pass
remains serial. Topology, original input/constraint/pin protection, shortest-edge,
angle, nominal/full-footprint size and terrain certificate gates remain separate.
`QualityReport::invalidPolicy` and generator failure expose invalid policy inputs.
Counters include peak queued vertices and prescribed-size priority queries.

Opt-in queue probe:

```sh
MESHKIT_COARSENING_QUEUE_VERTICES=6000000 meshkit_thinning_test queueCapacityProbe
```

This measures insert/update/pop operations and storage for six million queue
entries. It is not an end-to-end meshing or realistic terrain benchmark. The
parent GUI's optional terrain scale fixture can additionally enable
`SWMMVIS_WEIGHTED_TERRAIN_SCALE=1` with `SWMMVIS_TERRAIN_SCALE_CELLS` to exercise
weighted adaptive terrain, verified sizing and one coarsening attempt per vertex.


## Native extraction and shared constraint verification

The native API and codec checks run with `MESHKIT_BUILD_QT=OFF`. With Qt enabled,
a parity test compares exact coordinates, live connectivity, constraint flags and
insertion/removal counts across standard and Qt kernel storage, including weighted
coarsening and projected coordinates. The GUI's SMS file import now uses the
streaming reader; the legacy `parse(text)` helper remains explicitly separate.

The delivered constraint audit accepts adjacent domain seams with cells on both
sides only after checking those cells against the authored domain union minus
holes. Marker-specific source edge records remain independent of geometric cell
incidence. Shared corners, T-junctions, reversed/partly overlapping lines and
shared domain sides have regressions. Exterior cells and filled holes still fail.
Proximity welding remains disabled under preserve/bounded GUI policies.

Opt-in `meshkit_native_scale_probe io 2450` checks MKB and 2DM streaming roundtrips
with 6,002,500 vertices and 5,997,601 quadrilaterals. `... job 1734` checks native
triangle job orchestration with 3,006,756 source vertices and 6,006,578 cells.
These synthetic probes exclude terrain/GIS, mixed patch planning and rendering.
