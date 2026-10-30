# SWMMVis 6.0.0-alpha.4

**Pre-release.** APIs, file formats and defaults may still change before 6.0.0.

**Requires** `openswmm.engine` 6.0.0-alpha.4.

Covers everything merged since the `6.0.0-alpha.3` version bump. See [`CHANGELOG.md`](../../CHANGELOG.md) for the itemized record.

---

## Highlights

### Mixed triangle/quadrilateral 2D meshes

The mesh generator now produces **mixed tri-quad meshes**, with PSLG quad regions (Mapped, Submapped and Free) built on cross-field aligned lattices. Per-region mesh area bounds are honoured — they were previously accepted and silently ignored — and a minimum cell size can be set for constraining lines and polylines.

### Street inlets and the Inlet Junction node

The **Inlets editor is rebuilt** and joined by an **Inlet Junction** node type, matching the engine's HEC-22 capture work in the same cycle.

### Groundwater, heat and water quality reach the editors

An **Aquifer editor** and a **Groundwater Exchange editor** (for `[GWF]` expressions), a **Heat Configuration** editor, and a water-quality and transport surface in Simulation Options.

### Live results while a run is going

**Live 1D results** stream from both the 6.x engine and the legacy workers while a run is in progress, and **live 1D/2D profile plotting no longer hangs the UI on long runs**. Simulation Status gains **2D Solver** and **LTS Tiers** columns read from the engine's run statistics, and the runner's worker is wrapped so a failing run is captured and reported rather than lost.

### Plotting and section views

Separate **Profile** and **2D Profile** entries on the Analysis tab, a 2D inundation overlay on the 1D profile plot, 2D cell rainfall series, attribute tracks beneath the profile plot, a profile ground-line source option, more axis number-format styles, and a **Section View dock** with engine-accurate cross-section drawings — including five newly surfaced shapes (`BASKETHANDLE`, `SEMICIRCULAR` and others) and illustrated LID layer diagrams. Profiles no longer exaggerate slope to fill the pane.

### Running against SWMM 5.x

Runs on a **SWMM 5.x engine use a SWMM 5 profile of the model** rather than the canonical `.inp`, so a model using 6.x-only sections still runs on a legacy engine.

## Fixed

Round-trip and path handling received sustained attention this cycle:

- **External file references are now relative for every kind of file**, and a dangling 2D mesh reference survives Save As.
- The **climate file was opened against the working directory** rather than the project.
- `WRITE_ABSOLUTE_PATHS` set through the API had no effect on the save; portability warnings only appeared on Save As, never on a plain Save.
- The **Offset Mode toggle and ELEVATION-mode round-trip** are fixed, as is the Time Series editor showing absolute paths for file-backed series.
- **2D results land on top of the 2D mesh in a foot-based CRS** — two separate faults.
- Window handling: moving one plot dialog no longer moves another, windows can no longer be restored off-screen, and time-series plots opened from the map no longer hijack the profile plot.
- The **Welcome screen painted in the wrong theme** and model tabs floated over it.
- The Object Browser refreshes when data objects are added, removed or renamed, and application settings identity is set before the main window is built.

## Also in this release

A **Window menu** listing open dialogs with "Reset Window Positions", opt-in pure-Qt dialog stacking (`OPENSWMM_DIALOG_STACKING=qt`), local image basemaps via Add Basemap ▸ Local File (GeoTIFF/PNG/JPEG/BMP), and a bundled Bellinge 2D example with copy-on-open from the Welcome page.

## Upgrading

- Pair this build with `openswmm.engine` **6.0.0-alpha.4**. The GUI resolves the engine by sibling path and does not verify its version, so check what you have installed.
- The engine is now licensed under **Apache-2.0** (it was MIT); the About dialog's component list has been corrected to say so.
