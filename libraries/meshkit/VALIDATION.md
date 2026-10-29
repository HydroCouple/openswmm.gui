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

The full requested implementation record, commands, raw evidence links and remaining work are in `plans/workpans/2026-10-29-meshing-implementation-progress.md` in the parent repository. That folder follows the repository's existing ignore rules. These observations are local measurements, not portable time/memory guarantees. The final Qt-free geometry API, streaming codecs, licensing review and numerical policy redesign remain outstanding; see [README](README.md) and [PROVENANCE](PROVENANCE.md).
