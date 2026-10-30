# Native API, version 1

`meshkit::core` and `meshkit::io` require standard C++17, threads and the vendored
public-domain robust predicates. They contain no Qt, GUI, engine, GDAL or Python
dependency. Configure `MESHKIT_BUILD_QT=OFF` to build and install them alone.

```sh
cmake -S libraries/meshkit -B build-meshkit-native \
  -DMESHKIT_BUILD_QT=OFF -DMESHKIT_BUILD_TESTS=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build-meshkit-native
ctest --test-dir build-meshkit-native --output-on-failure
cmake --install build-meshkit-native --prefix /your/install/path
```

```cmake
find_package(MeshKit 0.1 CONFIG REQUIRED COMPONENTS core io runtime)
target_link_libraries(my_program PRIVATE meshkit::core meshkit::io)
```

## Request and result

Include `<meshkit/job.hpp>` and `<meshkit/io.hpp>`. `runJob(request, providers,
events)` is synchronous. It borrows immutable request data and callbacks for the
call and returns owned arrays and a versioned report. Independent calls can run
on independent threads. Callbacks run on the calling thread, must not reenter the
job, and must return promptly for cancellation to remain responsive.

`JobRequest::points` contains mandatory source points in a common Cartesian XY
frame. `Path` connectivity indexes these points, starting at zero. An explicit
outer domain is required. Outer domains form a union; holes are subtracted.
Region and line paths constrain edges. Non-line paths close implicitly; line
closure is explicit. Region seeds must be strictly inside a retained constrained
component. Conflicting seeds fail. Source points outside the retained domain or
inside a hole fail rather than disappearing silently. Dense raw terrain samples
should be supplied through terrain providers, not declared as mandatory pins.

Exact XY duplicates, including signed zero, share a geometric vertex across
paths. The input-to-output map and `VertexAlias` preserve their source identifiers
and markers. Consecutive duplicates and closing duplicates do not create
zero-length edges. A ring that revisits a vertex away from closure fails.
Crossing lines must be noded before using the native job API; the transitional GUI
pipeline has its existing intersection preparation. Near coordinates remain
distinct; minimum target size never becomes a welding radius in this API.

`Metadata` declares metres per XY unit and metres per Z unit. Size/edge policies
and provider footprint bounds use XY units; elevation and error tolerance use Z
units. The facet-normal scale derives from the physical unit scales. No CRS
conversion occurs here. Unknown units fail a job; imported SMS 2DM units start
unknown. Metadata strings are UTF-8. Provider identity is caller-supplied evidence
of the dataset/version; it is not a library-generated content fingerprint.

The current native job supports **triangles**. Requesting `AutomaticMixed` fails
with `unsupported_cell_plan`. The GUI's mapped patch planner, quad matching,
constraint simplification, size-field construction and project orchestration
remain in the explicit `_qt` API. The shared triangulation/refinement/coarsening
implementation is instantiated for standard storage and Qt storage; the GUI does
not copy its working triangulation through a second container mirror.

## Sizing, terrain and completion

`maximumSize(Box2)` returns a conservative upper size bound over the entire
footprint, including narrow sources. Zero means no provider limit; a global
positive maximum still applies. Negative/nonfinite bounds fail. The minimum
target is advisory because protected geometry can require smaller cells. An
independent minimum actual edge requirement is optional. Nominal triangle size
is the equilateral side of equal area. Transition ratio compares adjacent
nominal cell sizes. Repair has explicit pass/insertion limits.

Positive terrain tolerance requires elevation, residual witness and certified
bound providers. Unknown coverage rejects coarsening. The optional linear
size-weighted tolerance and resolution-aware coarsening order use the same
scalar policies as the GUI. Physical facet normals rank generated free vertices;
source points and constraints remain protected. Independent final checks report
terrain bounds, nominal sizes, edge lengths and adjacent ratios.

`Complete` and `CompleteWithExceptions` return a mesh; inspect the report to
understand advisory minimum-size or other accepted exceptions. Strict sizing
refuses unresolved maximum/edge/transition requirements. Requiring terrain
certification refuses unknown or unmet terrain bounds. `Infeasible`, `Cancelled`,
`ResourceLimit` and `Failed` return empty geometry and mappings. Examples are
bounded to 64 records, with separate total counters. Reports capture resolved
options and provider identity. Events use the existing bounded `EventChannel`.

Limits bound counts, estimated workspace and orchestration/audit work, not total
process RSS or arbitrary time inside caller providers. The kernel also has
insertion/cell/repair caps and polls cancellation within build/refinement loops.
Zero work allowance selects 20 billion orchestration/audit units. A job returns no
partially accepted candidate when it reaches a cap. `validateMesh` checks finite
coordinates, IDs, local connectivity and positive convex cell geometry; it does
not certify arbitrary nonincident cell overlap or CRS correctness.

## Neutral data and file I/O

`Mesh` owns contiguous standard arrays: XYZ doubles, zero-based uint32 cell and
edge connectivity, paths, optional signed 64-bit external IDs, numeric markers,
region labels, source aliases and generic metadata. Empty ID arrays mean
sequential IDs starting at one. Empty vertex-marker arrays mean zero. The neutral
model excludes SWMM boundary, coupling, infiltration and project ownership data.
Those remain in application codecs/adapters and must not be discarded by
pretending a generic export preserves them. Python bindings are not implemented
in this increment; these owned arrays establish their native boundary.

Reads return no partial mesh on failure. Writes validate first, stream to an
exclusive temporary in the destination directory and publish atomically.
Existing files are protected by default. `overwrite=true` allows atomic
replacement. Cancellation/failure before publication preserves old contents.
`durable=true` synchronizes temporary file contents before publication; it does
not promise portable directory-entry durability across power loss. Filesystems
must support same-directory atomic publication (and POSIX hard links for the
no-replace case); unsupported operations return an error without a direct-write
fallback.

### MeshKit binary (`.mkb`)

Version 1 is lossless for the neutral model. Fields are little-endian; no host
struct layout or padding is serialized. Strings are counted UTF-8 bytes.

| Order | Fields |
| --- | --- |
| Header | 8 bytes `MESHKIT` followed by NUL; uint32 version=1; uint32 flags (bit 0: units known); six uint64 counts: vertices, cells, edges, paths, labels, aliases |
| Metadata | two float64 unit scales; CRS and description (each uint64 byte length followed by bytes) |
| Labels | int32 ID; counted name |
| Vertices | int64 external ID; float64 X/Y/Z; int32 marker |
| Cells | int64 external ID; uint8 corner count (3 or 4); int32 region; uint32 indexes for each corner |
| Edges | int64 external ID; uint32 endpoints; int32 marker |
| Paths | int64 ID; int32 marker; uint8 kind; uint8 closed flag; uint64 index count; uint32 indexes |
| Aliases | uint32 vertex; int64 source ID; int32 marker |
| Footer | uint64 FNV-1a checksum of all preceding bytes |

Reserved features, invalid counts/UTF-8/connectivity, truncation, checksum failure
and trailing bytes are rejected. The checksum detects accidental corruption; it
is not an authenticity signature. Count and workspace checks precede bulk
allocation. Nonseekable `std::istream` binary reads are supported.

### SMS 2DM (`.2dm`)

The two-pass seekable reader resolves elements that precede nodes without
loading the whole file as text. It supports `MESH2D`, `ND`, `E3T`, `E4Q` and
standard `NS` node strings. Negative final node ID terminates a node string;
consecutive NS records may continue it. See the
[Aquaveo SMS manual](https://smsdocs.aquaveo.com/SMS_User_Manual_v11.1.pdf).
Clockwise cells are reoriented and counted; degenerate/nonconvex cells fail.
Sparse positive node/element IDs are preserved. Duplicate IDs, missing references,
extra geometry fields, nonfinite coordinates, unterminated node strings and
oversized records fail. The two passes compare file content checksums to detect
changes during reading.

Unknown cards fail by default. `IgnoreWithReport` is an explicit compatibility
choice, returning `CompleteWithExceptions`, a total ignored count and bounded
codes. `legacy2dm` also accepts historical nonpositive IDs and optional/text
material labels for the GUI import adapter. The GUI file reader now streams
through this path; its existing whole-text `parse` helper retains legacy behavior.
Project import displays compatibility warnings. Node strings become tagged GUI
constraint edges. GUI indexing remaps external IDs as before.

2DM cannot preserve all neutral attributes. Export refuses by default if units,
CRS, description, labels, aliases, vertex/edge markers or path roles/IDs would be
lost. Explicit `allowAttributeLoss` returns the loss categories. MKB is the
lossless alternative. SWMM's `[2D_*]` sidecar grammar is separate and remains with
the application-aware `_qt` codec.

## Command-line smoke path

The optional `meshkit` executable requires no Qt:

```sh
meshkit box 0 0 100 100 5 rectangle.mkb
meshkit inspect rectangle.mkb
meshkit convert rectangle.mkb rectangle.2dm --allow-loss
```

`box` explicitly declares a flat rectangular domain, zero elevation and metre
units. `--overwrite` permits replacement. SIGINT/SIGTERM request cancellation.
No mesh bounds, units, terrain source or missing domain are silently inferred by
an import-to-job shortcut.
