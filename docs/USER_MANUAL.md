# OpenSWMM GUI — User Manual

The OpenSWMM GUI is a modern Qt6/C++ application for building, running, and analyzing
SWMM models. It links directly against the refactored OpenSWMMCore engine and adopts
modern GIS idioms (layer trees, on-the-fly CRS reprojection, dock-based UI) so users
familiar with QGIS or ArcGIS Pro feel at home immediately.

This manual is **task-oriented** — every chapter answers "how do I…" with a numbered
walkthrough rather than dumping reference material. For API documentation see the
Doxygen reference at `docs/html/`.

## Table of Contents

| # | Chapter | Slice |
|---|---------|-------|
| 01 | [Getting Started](manual/01_getting_started.md) | A |
| 02 | [The Interface](manual/02_interface.md) | A |
| 03 | [Working with Projects](manual/03_projects.md) | A |
| 04 | [Coordinate Reference Systems](manual/04_crs.md) | C |
| 05 | [Layer Management](manual/05_layers.md) | D |
| 06 | [Object Browser and Property View](manual/06_object_browser.md) | L |
| 07 | Property Editors | E |
| 08 | [Selection](manual/08_selection.md) | E |
| 09 | Themes and Symbology | F |
| 10 | [Running Simulations: Simulation Options](manual/10_simulation_options.md) | G-1 |
| 11 | [Running and Viewing Results](manual/11_results.md) | M |
| 12 | Animation and Playback | F |
| 13 | Comparing Scenarios | G |
| 14 | 2D Mesh Generation | H |
| 15 | Calibration | G |
| 16 | IO Plugins | H |
| 17 | Print and Export | G |
| 18 | Preferences | A |
| 19 | [About and Licenses](manual/19_about.md) | K |
| 20 | Keyboard Shortcuts | — |
| 21 | [Redraw Policy and Performance](manual/21_redraw_policy.md) | H |
| 22 | Troubleshooting | A |
| 25 | [Project Portability](manual/25_portability.md) | IO |

### Tutorials

- [01 — First Simulation](manual/tutorials/01_first_simulation.md) (planned, Slice E)
- [02 — Modify and Compare](manual/tutorials/02_modify_and_compare.md) (planned, Slice G)
- [03 — Build From GeoPackage](manual/tutorials/03_build_from_geopackage.md) (planned, Slice H)
- [04 — 2D Mesh Workflow](manual/tutorials/04_2d_mesh_workflow.md) (planned, Slice H)
- [05 — Calibrate to Observed](manual/tutorials/05_calibrate_to_observed.md) (planned, Slice G)

### Document conventions

- Menu paths use an em-dash: `File → Open`.
- Dock and panel names are **bold**: **Layer Tree**, **Object Browser**.
- Keyboard shortcuts: <kbd>Ctrl</kbd>+<kbd>S</kbd>, <kbd>Ctrl</kbd>+<kbd>Shift</kbd>+click.
- Error codes are listed verbatim in [Troubleshooting](manual/20_troubleshooting.md) so
  they are searchable.

### Where this manual is being co-authored

Each implementation slice ships its corresponding chapters in the same PR as the code.
Chapters labeled **(planned)** above will land with the slice listed; the slices column
above tracks delivery.
