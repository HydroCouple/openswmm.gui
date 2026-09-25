# Workflow, dialog and visualization surface contracts

This is a source-reviewed allocation of the critical journeys. Detailed discovered declarations and consumers are in `dialogs.json`. All rows still owe native keyboard/assistive-technology, scaling and both-theme evidence. “Existing” below means a source path exists, not that every option has passed the full rendering/export chain.

## Critical dialog families and state ownership

| Family / implementation | Entry paths and owner | Persistence/edit model | Priority acceptance / package |
|---|---|---|---|
| Project/save prompts: `swmmvis.cpp`, `swmmvisprojectwindow.cpp` | File actions, close/quit and save-before-run; project window + model/mesh layers | INP + mesh + OSWP; current multi-file limitations in save contract | Failure/dirty/retry/Don't Save and project identity; W1. |
| Mesh generation: `meshgenerationdialog.cpp` and region defaults | Model Generate Mesh action; project owns attached mesh, worker owns value inputs/results | Currently worker writes model files; migrate to staging; generation options belong to project | Cancel/stale job handling, source/raster units, accessible tabs/tables/errors, rectangular corridor authoring; W1/W2/W6a. |
| Simulation options: `simulationoptionsdialog.cpp`, `simoptions/` | Model actions; `SimOptionsContext`, pages, engine model | Apply/OK to engine/options; controls and engine capability gates | All pages, especially 2D process/output/mesh; incompatible runtime explained, no silent partial Apply; W3/W4c. |
| Mesh groundwater: `mesh2dgroundwaterdialog.cpp`, `mesh2daquifermodel.cpp`, and the 2D simulation-options pages | Mesh/2D actions; aquifer rows and runtime engine state | Buffered row/property authoring vs read-only runtime inspection must remain distinct | G1 field/unit/closure parity; forcing bindings; no fabricated edit controls on inspectors; W3/W4c. |
| Mesh attribute assignment: `meshattributeassigndialog.cpp`, `meshcellparams.cpp` | Mesh tool/action, project selection bus, target mesh layer | Existing assignment/undo paths; groundwater rows require a common batch model | Rectangle/lasso/vector/raster targets, signed values, NoData, overlap, conservative totals and undo; W4c. |
| Compound node/link/subcatchment editors | Object browser, map/context actions, properties/table; model layer + project undo stack | Multiple UI entry points edit the same underlying model | Cancel/Apply, external changes/removal, unit labels, focus and geometry; W3. |
| Data editors: comprehensive editor registry | Data menu, browser category actions, property pickers, attribute table | Engine/model registries, undo stack; several modeless editors flush on close | Audit close semantics, dirty tracking and single-instance/model ownership; 13 runtime callbacks observed, invocation pending; W3. |
| GIS/import/basemaps: import dialog/pages, CRS and connection dialogs | Add/import/layer context actions; source layer and import models | Dataset/config/reference ownership; async I/O for import/remote sources | Error recovery, source removal, no partial imports, source credentials not in diagnostics; W1/W3. |
| Layer/style panels: `layerstyledialog.cpp`, style subjects/adapters | Layer properties/styling dock; layer style model and renderer | Preview/Apply/Cancel; project styles and explicit style exports | Roll back all sublayers/caches; shared selection, legends and exports; W3/W4. |
| Profiles/sections: mesh/profile plot/options and source styles | Profile tools and sections/layer panels; section definition + run/source adapters | Section geometry and per-source series styles; plots/export | Unified chainage/time/datum, groundwater/surface overlays and separate unit tracks, missing runs; W4b. |
| Preferences/shortcuts: `preferencesdialog.cpp`, shortcut editor | Application settings/help actions; preference/theme/action managers | Application QSettings, not project styles | Live theme changes, reset scope, focus restoration, display scale; W3/W4. |
| Supporting dialogs: About/plugins/status/statistics/reports | Help, run/results and plugin actions; application/run-local owners | Read-only and configuration dialogs need different close/refresh semantics | Reachability, errors, details, focus, resizable readable content; W3. |

Remaining source rows are assigned to their matching family. `dialog-files.json` retains every implementation including models, adapters, delegates and registry helpers; those are not extra dialogs. The Designer file is `forms/swmmvis.ui`. Standard prompts and dynamic QWidget pages must be checked through their hosts, not omitted because they lack QDialog subclasses.

## Visualization configuration coverage

For each row, W4 must record separately: **model → editor → renderer → legend/probe → serializer → reopen → export**. No row is globally “complete” based on source presence.

| Surface | Model/editor/renderer anchors | Persistence / export anchors | Current evidence and missing gate |
|---|---|---|---|
| 1D network/results | Model/results layers, species attributes, symbol/label editors and network renderers | Project/style serializers, result export/plots | Existing paths; verify classifications, rules, units, selection, widths/arrows and missing species across the entire chain. |
| Mesh geometry | `SWMM2DMeshLayer`, mesh style panel, mesh QSG renderer | Project serializer, mesh writer | Existing fill/edge/vertex/BC/coupling styles; verify all sublayers, labels, live theme and render parity. |
| 2D surface hydraulics | `SWMM2DResultsLayer`, HDF5 reader, results style panel | Project settings, mesh result export | Existing scalar/depth/vector paths; variable availability, dry/no-data, range/time policy, legend/export parity still require audit. |
| Surface species | Existing 1D species identity utilities; engine rank-3 file output | New catalog/style/reader persistence needed | Missing 2D species configuration/reader/renderer/probe/export chain; W4a. |
| Groundwater properties/results/species | Aquifer authoring model vs dynamic output APIs | New output catalog/readers/style persistence | Keep static properties separate; dynamic advancement/file output/runtime units gates in capability map; W4a. |
| Combined groundwater/surface sections | `ProfileSection`, profile samplers/widgets, profile source style adapter | Saved sections/styles, profile exports | Current mesh/profile foundations; combined groundwater time/datum/quantity tracks missing; W4b. |
| Groundwater assignment preview | Selection bus, mesh highlight, attribute assignment | Batch rows/undo plus model Save | No complete common manual/vector/raster forcing preview today; W4c. |
| Raster/terrain | Raster layer, raster symbology, classification, hillshade/sun controls | Style/project serializer, raster export | Existing paths; audit band/scale/offset/NoData, stretch, contours and live painting. Phase 05 fixed sun-disc background; compact cardinal/readout layout remains W3/W4. |
| GIS/basemaps | Feature layer renderer, symbol editors, labels and connection config | Project/source references and style exports | Audit rule filters, scale visibility, opacity, CRS and failure recovery. |
| Profiles/charts | Series/axis styles, profile/comparison dialogs and plot widgets | Plot/section style settings and exported images/data | Audit axes/time formatting, missing values, legends/grid/cursor and saved-vs-exported appearance. |
| Overlays/annotations | Legend/label/style controls, map overlays | Project/style config and map export | Audit text alternatives, fonts/halo, format/units, selection and theme; Phase 05 colour descriptions are a foundation only. |

## Icons and live theming

`action-icons.json` maps the compiled catalog's source entries to resource files. The main-window initialization reassigns registered icons through `IconFactory`; do not count Designer icons as confirmed bypasses. Other icon sites include previews, decorations and chart symbols; classify them before replacement. Prioritize mesh cell/edge/vertex tools, boundary/forcing/source types, groundwater vs surface, profile quantities, import/export and destructive operations.

W4d must provide light/dark contact sheets at native toolbar sizes and enlarged scale, inspect silhouettes and paired labels, and record replacement/retain decisions. No icon is declared clear merely because its resource exists. Painter/style references similarly identify audit sites, not proven contrast failures. Shared semantic colors must refresh visible states without overwriting explicit user symbology.

Known selection guidance gap: the cell tool tooltip in `src/swmmvis.cpp` still describes a results layer, although the tool selects a mesh without results. W3/W4c should correct the wording and verify toolbar availability in that journey; the new baseline tests exercise the tool, not its menu/toolbar gating.
