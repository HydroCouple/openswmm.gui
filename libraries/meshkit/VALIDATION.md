# Extraction validation — 2026-10-29

- Standalone Release configure/build/install succeeds with Qt 6.9.3 on Apple arm64/macOS 27.0.1, without GUI/engine/GDAL dependencies.
- Five standalone suites pass: runtime events, constraint simplification/audits, CDT, generator, and generation/native/SMS codec roundtrip.
- The runtime-only build passes with Qt disabled.
- A separate installed-package client links `meshkit::runtime`, `meshkit::core_qt`, and `meshkit::io_qt`, generates a mesh and serializes its sections.
- GUI application build succeeds. Eleven geometry/codec/log suites plus infiltration integration pass. The full terrain suite has 75 passing cases and three normally skipped opt-in workloads; the synthetic scale workload was then run separately.
- A 600-hole/24-breakline fixture retains 19,249 cells and the baseline hash `665bcd25f66fd6bd`.
- Kernel grid probes complete at 6,000,000 cells and, separately, 6,000,000 vertices / 11,990,002 cells. Their measured peak RSS is 413.5 MB and 766.1 MB respectively.
- The synthetic-plane terrain worker completes 6,499,882 triangular cells in 16.924 s at 2.450 GB peak RSS, with no terrain unresolved/unknown cells or cap. This excludes rendering, old GUI meshes and final file publication; it is not a worst-case terrain or hydraulic benchmark.

The source-controls/shared-chain increment additionally passes the full integrated pipeline suite (80 cases, zero failures, three optional workloads skipped), native source/preview widget checks (five cases), and native full-dialog checks (six cases). It fixes the coarsening-checkbox defect where triangle-only regions were incorrectly protected as mapped quad interiors. Both terrain-off and terrain-on checkbox-to-worker regressions remove two free vertices, changing the fixture from 287 to 283 cells while retaining constraints.

New capacity observations, including independent audits:

- Six million vertices on one straight source: 2.567 s simplification, 4.422 s including audit, 719.2 MB peak RSS.
- Two reversed owners sharing six million source vertices total: 4.188 s simplification, 7.819 s including audit, 809.4 MB peak RSS.
- A six-million-cell constrained grid: 0.970 s delivered constraint audit after a 0.286 s conversion to the current GUI mesh DTO; 16.860 s total including triangulation, 1.047 GB peak RSS with both representations resident. The audit checks every cell's incidence and four required sides. It does not represent a complex terrain or GUI-render workload. Enable this probe with `MESHKIT_AUDIT_DELIVERED=1 meshkit_scale_probe 2001 1501`.

Raw evidence and native layout captures are under the parent repository's `tests/output/constraint-review-2026-10-29/`.

## Verified sizing increment

Six standalone suites pass, including independent nominal-size/face-ratio checks,
minimum-edge requirements, invalid connectivity/callbacks, cancellation/resource
limits, legacy migration, narrow-region footprint bounds, float-floor consistency,
and bounded local transition repair. Native settings checks pass five cases,
including SI/foot unit persistence and dock resizing; the full native dialog
suite passes six cases. The full integrated pipeline passes 82 cases with three
optional workloads skipped. Existing legacy behavior and coarsening regressions
remain covered.

The curved-terrain/fine-region fixture completes with 391 cells, no terrain
unresolved/unknown cells, no global/footprint size exceedances and no transition
failures (largest adjacent nominal ratio 1.49977 against 1.5). Repair inserts 25
vertices in two passes. A fixed oversized quad remains intact: verified mode
reports its conflicts and strict mode refuses the candidate. A short protected
source conflicting with a strict edge floor is refused before meshing.

The sizing audit covers 6,000,000 cells / 8,996,500 shared faces in 2.153 s. With
CDT, current mesh DTO and both delivered audits, total time is 7.731 s at 1.423 GB
peak RSS. At 6,000,000 vertices / 11,990,002 cells / 17,980,005 shared faces, sizing
takes 4.843 s; total 17.855 s at 2.639 GB peak RSS. These uniform grids exercise
capacity, not a realistic terrain/GIS/render workload. Six million conservative
footprint queries against 1,000 narrow region bboxes take 1.379 s using the region
BVH and field minimum pyramid. Source bboxes can overrefine nearby ground.

Evidence: `tests/output/sizing-review-2026-10-29/`. The GUI uses two repair passes
and at most 200,000 additional repair vertices. Fixed quads/mixed interfaces may
retain reported conflicts; strict sizing refuses unresolved requested limits.
Sizing audits do not certify arbitrary cell overlap or replace the terrain audit.

The full requested implementation record, commands, raw evidence links and remaining work are in `plans/workpans/2026-10-29-meshing-implementation-progress.md` in the parent repository. That folder follows the repository's existing ignore rules. These observations are local measurements, not portable time/memory guarantees. The final Qt-free geometry API, streaming codecs, licensing review and numerical policy redesign remain outstanding; see [README](README.md) and [PROVENANCE](PROVENANCE.md).


## Unified planning increment

Seven standalone suites pass. Planner regressions cover canonical full shared
sides with junction retention, winding/projected origins, explicit priorities,
holes/lines/pins/masks, terrain repair/unknown coverage, cancellation/budgets,
partial-side refusal, exact domain stitching, required interface refinement,
preferred fallback with its source outline, and fixed-patch pin refusal. The full
GUI worker suite passes 90 cases with three optional workloads skipped. The new
open-block case uses one triangulation pass and has zero delivered upper-size,
transition or terrain failures. Native settings and dialog suites pass six and
seven cases respectively.

The opt-in planning-only probe completes 6,000,000 mapped cells in 100 patches,
180 shared sides, in 2.372 s at 774,455,296 bytes peak RSS. It excludes CDT, DEM,
publication and GUI rendering. Default individual patches are capped at 250,000
cells; full shared logical sides are supported, unmatched partial sides require
source decomposition. Mapped planning is limited to four logical sides. Required
fixed patches cannot be restationed. These limits are explicit failures/fallbacks,
not silent geometry relaxation. Evidence: `tests/output/cell-planning-2026-10-29/`.


## Resolution-weighted thinning/coarsening increment

All eight standalone suites pass, including scalar/version/queue checks,
metre/foot normal/removal invariance, narrow full-footprint size protection,
pins, unknown/error-bound refusal, directional terrain targets without nominal
isotropic refinement, and fixed/preferred weighted quad gates. The complete GUI
worker suite passes 91 cases with three optional workloads skipped. Native
settings and full-dialog suites each pass seven cases, including widened
controls, navigation sizing, transition rates above 10, policy persistence and
inactive terrain deactivation. The original coarsening-checkbox cases now verify
that delivered vertex count decreases by the reported removals, with terrain off
and on; inactive generated CDT slots are omitted from delivery.

The curved-reference/fine-region fixture changes from 286 cells at constant
tolerance to 493 with weighting, while removing seven redundant vertices. Applied
allowance is 0.01–0.04 m; maximum conservative bound/allowance is 0.999987 with no
terrain unresolved/unknown cells and a passed delivered constraint audit.

The indexed queue handles six million insert/update/pop entries in 2.406 s,
120,000,000 bytes queue/index allocation and 133,955,584 bytes process peak RSS.
This is a queue-only capacity measurement.

The complete weighted synthetic-plane worker delivers 6,433,024 cells /
3,220,202 vertices from a 24,039,409-pixel source in 43.574 s,
peak RSS 2,970,304,512 bytes. One attempt per free vertex removes
38,822 vertices in 12.846 s; peak queue
3,251,646, priority queries 3,376,768.
Every delivered vertex is checked for cell incidence. The independent sizing
audit covers 9,645,847 shared faces and satisfies all requested size/edge/ratio
limits; largest nominal ratio 1.77121 against 3. Terrain allowance is about
0.0025 m with largest conservative bound/allowance 0.999857, no unresolved or
unknown cells and no refinement cap. This includes terrain preparation,
meshing/coarsening and final audits, excluding GUI rendering/final publication.
It is a synthetic-plane capacity observation, not complex real-terrain,
hole/line-density, rendering, physical-accuracy or portable timing certification.
The complete worker case has 3.22 million vertices; older six-million-vertex CDT
probes remain separately scoped.

Evidence: `tests/output/thinning-review-2026-10-29/` in the parent repository.


## Native jobs, streaming codecs and shared vertices — 2026-10-29

The native-only configuration passes runtime, native job/kernel and native I/O
suites. The full standalone configuration passes all 11 suites. A separate
installed-package client consumes only `core`, `io`, `runtime` with Qt discovery
disabled, including when the installed package also contains optional Qt targets.
The CLI generates/inspects a native rectangular mesh. Exact native/Qt kernel
parity covers ordinary refinement, resolution-aware coarsening and projected XY.

Native I/O checks include sparse IDs, mixed cells, labels/markers/aliases/units,
forward references, multi-record node strings, clockwise normalization, unknown
cards, legacy material tags, corruption/truncation/trailing data, record/count
limits, cancellation and failed-write preservation of the original destination.
The GUI SMS file adapter streams and surfaces compatibility warnings.

Shared constraint regressions exercise exact corners, T-junctions, reversed and
partly overlapping lines, domain seams, union membership and identical generated
stations in opposite directions. The delivery audit still rejects exterior cells
and filled holes. The real GUI worker runs six shared cases across preserve and
bounded policies. Full terrain/pipeline suite: **97 passed / 0 failed / 3 optional
probes skipped**; Quality, settings dock and SMS reader: seven passes each.

Synthetic native triangle job: 6,006,578 cells / 3,006,756 vertices, 7.19411 s,
1,244,659,712 bytes peak RSS, no maximum-size conflicts. Input is a uniform lattice
of mandatory source vertices, with no DEM/coarsening/mixed planning/rendering.

Native I/O fixture: 6,002,500 vertices / 5,997,601 quadrilaterals. MKB: 390,020,533
bytes, write 1.24745 s, read/validate 1.43851 s, peak RSS 577,994,752 bytes. SMS:
489,509,490 bytes, write 2.21841 s, read/validate 9.26698 s, process peak RSS
822,935,552 bytes (original mesh retained during read). Temporary capacity files
are deleted. These are local synthetic observations, not a realistic end-to-end
terrain/GIS or portable performance certificate.

Evidence: `tests/output/native-api-review-2026-10-29/` in the parent checkout.
The native job's automatic mixed capability remains false; quad planning and
source/size-field construction still use the transitional Qt API. See
[NATIVE_API.md](NATIVE_API.md). Python bindings and relicensing are not delivered.
