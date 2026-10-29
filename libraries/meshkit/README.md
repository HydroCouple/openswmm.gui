# MeshKit extraction

MeshKit is an independently configurable, buildable and installable CMake project. OpenSWMM links the same compiled targets that a separate application can consume; there is one implementation of each extracted algorithm and codec.

| Target | Contents | Dependencies |
| --- | --- | --- |
| `meshkit::runtime` | Bounded job events, typed counts and run outcomes | Standard C++17 and threads |
| `meshkit::core_qt` | Constrained Delaunay refinement/coarsening, size field, patch/strip placement, quad regions/matching/cleanup, mesh geometry/statistics/reordering, natural-neighbor interpolation, shared geometry helpers | Qt 6 Core, Gui value geometry, Concurrent; public-domain predicates; `model_qt` |
| `meshkit::model_qt` | Existing mesh values, boundary-condition vocabulary and infiltration/cell parameters | Qt Core/Gui; currently also contains infiltration serialization helpers |
| `meshkit::io_qt` | Native INP/2DM mesh sections and SMS 2DM reader | Qt Core/Gui and `model_qt` |

The `_qt` names make the transitional API explicit. These targets do not depend on Qt Widgets, the OpenSWMM application, the simulation engine, GDAL, or Python. **Only `runtime` is currently Qt-free.** The terrain/GIS providers, GUI pipeline orchestration, project adoption, and feature export adapters remain in OpenSWMM. Existing format behavior and limitations are retained; extraction alone does not make the codecs streaming or fully validating.

## Build and use

```sh
cmake -S libraries/meshkit -B build/meshkit-standalone \
  -DCMAKE_PREFIX_PATH=/path/to/Qt/6/matching-platform \
  -DCMAKE_BUILD_TYPE=Release -DMESHKIT_BUILD_TESTS=ON
cmake --build build/meshkit-standalone
ctest --test-dir build/meshkit-standalone --output-on-failure
cmake --install build/meshkit-standalone --prefix /your/install/prefix
```

For the standard C++ event API alone, configure with `-DMESHKIT_BUILD_QT=OFF`. Tests need Qt Test only when the Qt targets are enabled. There is no network fetch, sibling-checkout requirement, or application build dependency.

A downstream client:

```cmake
find_package(MeshKit 0.1 CONFIG REQUIRED)
target_link_libraries(my_mesher PRIVATE meshkit::core_qt meshkit::io_qt meshkit::runtime)
```

Include `<mesh/meshgenerator.h>`, `<mesh/inpmeshreader.h>`, `<mesh/inpmeshwriter.h>` or `<meshkit/events.hpp>`. The API retains `mesh::MeshGenerator`, `mesh::ConstrainedDelaunay`, and `mesh::MeshResult`. See [the standalone smoke test](tests/mesher_io_test.cpp) for generation and mixed-cell file roundtrip examples. Each generator is thread confined; inputs and results use owned Qt value containers. Callbacks execute synchronously on the generating thread and must not manipulate GUI objects. Callback lifetimes must span generation.

`GenerationOptions::allowQuads=false` disables generated quad regions, conduit strips and terrain strips, while retaining region boundaries, tags and local targets. A prebuilt quad patch conflicts with that mode and returns a diagnostic. Default `true` preserves existing callers.

The retained generator API's `MeshResult::ok` means that a candidate was produced. Consumers must also inspect `MeshGenerator::stats().refineCapped` before accepting a mesh; a capped candidate is incomplete. The GUI adapter exposes that distinction as the structured `ResourceLimit` outcome and refuses to apply it. The fully unified standalone request/report contract remains a later extraction step.

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

Next boundaries are standard C++ geometry/arrays in a new `meshkit::core`, generic streaming codecs in `meshkit::io`, optional GDAL terrain adapters, and the headless pipeline/request/report contract. The `_qt` facade can then be implemented as an adapter. Python bindings should consume the installed package with array-oriented buffers and explicit ownership; no GUI dependency should enter that interface.

## Constraint simplification

`<mesh/constraintsimplifier.h>` provides a collection-level policy pass. Features carry stable IDs, per-feature preserve/simplify policies, XY/Z limits, protected flags, corner and ring-area limits. The collection shares an immutable segment BVH; proposed shortcuts check original-span deviation, monotone projection, obstacles/clearance, pins and swept pockets. A final index checks output shortcuts against each other. Conflicts or work-budget exhaustion retain original geometry. The API returns source-vertex mappings and per-feature change statistics. It assumes a common finite planar frame and an intended original graph; it does not repair invalid input topology or generalize/delete holes.

Exact shared original segments are canonicalized into chains under the strictest owner protections. Each chain is simplified once and stitched back in its original direction. A failed ring-area limit restores the connected shared component. Different source sampling is retained conservatively. Work and estimated workspace limits return originals; cancellation returns failure. Limits bound temporary workspace, not total process memory.

`GenerationOptions::preserveConstraintCoordinates` disables proximity welding while retaining exact junction/crossing splitting. Its default is false for existing callers. The GUI enables it for the explicit preserve/bounded policies.

`<mesh/constraintaudit.h>` independently checks preparation mappings, retained vertices/heights, XY/Z/area/corners, original junctions, shared consistency, pockets and clearance. Its delivered audit checks required paths against incident cell edges, domain/hole side, markers and retained authored Z after final edits. Missing or incomplete coverage refuses adoption. This is a constraint audit, not a general arbitrary-cell-overlap certificate. Cancellation and resource exhaustion never report a successful audit.

`<mesh/constraintpreview.h>` builds bounded display data from an audited candidate. Display sampling changes neither audited geometry nor the generation request. The GUI offers per-source overrides, persistent physical units and preparation-only preview; these widgets are outside the library.

Opt-in probes `MESHKIT_SIMPLIFY_VERTICES=6000000 meshkit_constraints_test longFlatSourceHasBoundedWork` and `MESHKIT_SHARED_VERTICES=6000000 meshkit_constraints_test longSharedSourceUsesOneChain` exercise straight and shared source capacity, including independent preparation audits. These are not complex-boundary or end-to-end meshing performance guarantees.
