# UI/file I/O fixes — implementation and verification

22 October 2026. Corrections have been implemented for all 38 gap groups in the original audit. Changes span the GUI and its sibling engine repository. The original audit remains unchanged as baseline evidence.

## Verification

- Eight focused engine suites pass: options parser, native writer, new UI/file round trips, filled-circular offset API, time-series modes, plugin lifecycle, HDF5 model and GeoPackage. The GeoPackage suite contains 61 passing checks.
- The selected GUI regression run contains 43 passing checks. Final rebuild and final run evidence are recorded in `gui_regressions.log`.
- Nine real simulations (snow, groundwater and 2D mesh, each reopened from INP/GeoPackage/HDF5) produce byte-identical native OUT files within each case.
- Four additional models retain the tested extended 2D, snow/radiation, heat and LID sections across all three formats.
- Report-only and shared-container GeoPackage runs have 12 summary records each; shared-container results have 80 time-series records. A forced summary-insert failure is returned to the caller with its original explanation.
- Reaction models reopen with two species from both containers after the source reaction configuration is removed.

## Gap closure ledger

| Audit ID | Area | Implemented correction | Evidence |
| --- | --- | --- | --- |
| 1 | Pollutant molecular weight crash | GeoPackage initializes every pollutant column; molecular-weight access safely defaults missing legacy data. | New engine regression; isolated getter probe; 61 GeoPackage checks. |
| 2 | Rain-gage scale | Store and restore the independent rainfall scale, including 10. | All-format regression; snow simulations. |
| 3 | 1D groundwater | Preserve AQUIFERS, GROUNDWATER and GWF through the versioned native-section supplement. | All 12 aquifer parameters compared; groundwater simulation parity. |
| 4 | Snowpack assignment | Persist each subcatchment snowpack name and resolve it after reading definitions. | All-format regression and snow simulation parity. |
| 5 | Co-pollutant references | Register all pollutant identifiers first; initialize absent references to -1. | Forward-reference and absent-reference assertions. |
| 6 | Hydraulic options | Supplement relational GeoPackage options with every native hydraulic setting missing from its existing schema. | 40 common-option probes plus four 2D option probes; repeated saves. |
| 7 | Quality and heat options | Retain transport selection, stepping, switches and physical constants. | Common-option probes and all-format regression. |
| 8 | 2D solver and heat options | Persist missing native 2D options independently of existing relational keys. | 2D option probes and extended heat section comparisons. |
| 9 | Per-cell 2D physics | Carry native initial velocity, infiltration, initial/boundary quality, surface-quality and snow sections. | Surface-quality section comparisons; mesh simulation parity; serializer/handler review. |
| 10 | Snow and meteorology | Retain UEB, snow cadence, humidity and radiation configuration. | Snow cadence assertion; radiation/snow section comparisons. |
| 11 | Components and reactions | Preserve component bindings, arguments and configuration contents in containers; retain general reaction sections. | Both containers reopen with two species after deleting the original configuration file. |
| 12 | Extension records | Carry INITIAL_QUALITY, USER_FLAGS, USER_FLAG_VALUES, EVENTS and PLUGINS in the supplemental native sections. | Writer/filter/handler review; component reopening. |
| 13 | LID curves and removals | Retain the drain-curve name in the native model and replay DRAIN/REMOVALS rows with other conventional LID fields. | LID section comparisons; parser/store/writer review. |
| 14 | Adjustment patterns | Use the pattern registry for names and lookup. | Existing GeoPackage checks and authored pattern fixture. |
| 15 | Additional weir inputs | Preserve secondary coefficient, fractional contractions, surcharge, roadway and coefficient-curve metadata. | Property writer/reader review; regulator round trips. |
| 16 | Titles and descriptions | Store ordered title lines and comments for nodes, links, subcatchments, gages, pollutants and tables. | Metadata writer/reader review; transect comment regression. |
| 17 | Native molecular weight | Add a native pollutant column; warn when SWMM 5 export cannot carry the value. | 58.44 round trip through INP, GeoPackage and HDF5. |
| 18 | Transect encroachment | Read/write native X1 encroachment stations and associate comments; warn for SWMM 5 omission. | 1.25/8.75 and description assertions. |
| 19 | Small routing steps | Only exact whole seconds use whole-second clock formatting. | 0.0005-second step round trip in all formats. |
| 20 | Numeric precision | Use round-trip double precision across native numeric fields and fractional timeseries timestamps. | Fractional timestamps, tiny concentration and aquifer assertions; native writer/time-series suites. |
| 21 | Inactive solver configuration | Write TPA, DPS and FV choices in native files even when another method is selected. | Inactive TPA/DPS probes; writer review. |
| 22 | Regulator crest offsets | Bind upstream offsets to the authoritative subtype crest and reject independent downstream editing for weirs/outlets. | Weir/outlet offset probes, crest setter and round-trip assertions; GUI property review. |
| 23 | Draft 2D configuration | Write 2D options even without a mesh. | Dedicated all-format draft regression. |
| 24 | Embedded reactions | Write current reaction state for embedded reaction sections; preserve other captured sections. | Native serializer review and component configuration scenarios. |
| 25 | Unedited Apply | Retain original engine text while a tagged control is unchanged; widen small-step ranges and display precision. | Imported-value regression, thread-count edit/no-op assertions and existing no-edit Apply tests. |
| 26 | Small numeric edits | Compare authored numeric values exactly. | 2D limiter edit from zero to 1e-9. |
| 27 | Text truncation | Use growing buffers for common/2D options, title lines, plugin paths/arguments, secondary files and hotstart paths. | Long selector, 5,000-character Unicode notes and 6,000-character plugin arguments. |
| 28 | Writer role selection | Persist explicit independent role choices, including BUILTIN. | Select built-in after legacy GeoPackage selection and reopen dialog. |
| 29 | Writer destinations | Pass role-specific paths to plugins, implement shared container destinations and retain native GUI companions. | Real report-only and shared-container runs; path restoration checks. |
| 30 | Plugin arguments | Retain each output/report/state instance’s own arguments and originating metadata; initialize and validate loaded runtime plugins. | Plugin lifecycle suite and real GeoPackage writer runs. |
| 31 | HDF5 model reopening | Embed the full editable native model alongside existing analysis columns and implement its reader. | HDF5 suite; all-format regression; simulation parity. |
| 32 | Write diagnostics | Forward model-save warnings and finalization errors; check HDF5 operations; propagate runtime finalization/report failures. | Forced SQLite report failure returns error 10 and the original diagnostic. |
| 33 | Vector styles and filters | Save full vector style snapshots and filter expressions; restore after asynchronous open. | Actual OSWP save/restore with symbol and filter assertions. |
| 34 | Result parent display | Save parent result-layer name, visibility and opacity. | Actual simulation plus OSWP round trip. |
| 35 | Custom CRS | Save full canvas WKT and restore it before applying the view. | Custom azimuthal-equidistant CRS OSWP assertion. |
| 36 | Calibration bindings | Store project-owned bindings with stable kind/attribute IDs and portable paths; accepted changes mark the project dirty. | Project isolation and saved binding/path assertions; LinkDepth mapping review. |
| 37 | Portable destinations | Store destination overrides in model IO options and OSWP; restore them into run-path settings on model/project open. | OSWP round trip and direct model-open path assertion. |
| 38 | CSV/TSV exports | Use a shared quoted delimited writer, atomic file replacement and visible error reporting. | Comma, quote and newline assertion plus failed destination check. |

## Format and compatibility behavior

GeoPackage retains its normalized tables as the authority for existing fields. New fields are stored in versioned `model_supplement` sections and per-object `model_properties`. Repeated saves replace authored rows transactionally while retaining separate run/results/observation data. Sparse legacy imports receive safe defaults.

New HDF5 model exports include a `native_input` dataset and embedded process-component configurations. Older column-only HDF5 exports cannot recover settings that were never saved and are refused with an explicit diagnostic. External resources referenced by ordinary model sections continue to use their existing file/path conventions.

Native INP files retain molecular weight and transect encroachment extensions. The SWMM 5 compatibility profile omits those unsupported values and reports warnings.

Output/report writer choices and their destinations persist independently of plugin discovery. A selected container receives its authored destination; native `.out` and `.rpt` companions support the current GUI readers. Opening a model restores authored destinations into the run-path cache.

Calibration bindings are project-owned. Historic application-wide bindings have no reliable project identity, so no automatic assignment to an arbitrary project is performed.

## Coverage limits

The tested settings and scenarios provide regression coverage for these fixes. Exhaustive GUI interaction with all 271 writable property declarations and every compound editor has not been performed. Every external forcing format, nested external data inside component configurations, cross-platform path encoding, and numerical parity of every hotstart and plugin result format remain outside this run. The general save validator still checks structural integrity; targeted regression tests provide setting comparisons.

The working checkout also contains the separate `nativeImplicitPendingApplyCancelUndoAndScientificTolerance` GUI test requires a matching implicit-FV preview engine. It fails its capability prerequisite with the current hotstart-format engine, which lacks `FV_IMPLICIT_SCHEMA`. It is excluded from the 43 passing GUI checks; its assertion and solver implementation remain outside these commits. The selected GUI source changes were additionally compiled against headers without those unrelated preview additions.

Engine implementation commit: `c5244651`. The GUI fixes, regression tests and review records are committed with this report. Existing unrelated work in both checkouts remains outside these commits.

The retained manual probes use local fixture/build paths and generated physics fixtures. The committed C++ and Qt regressions cover the regular repeatable test targets.

## Evidence files

- `probe_results.json`, `additional_probe_results.json`: original audit probes rerun against the corrected engine.
- `extended_results.json`: normalized extended-section comparisons.
- `scenario_results.json`: simulation hashes, component survival, output row counts and forced-error diagnostic.
- `engine_regressions.log`, `gui_regressions.log`: retained test output.
- `implementation_sources.json`: hashes of the main implementation files.
