# 02 — The Interface

## What you'll do

Learn what every menu item, toolbar button, dock, and status-bar widget does. Use this
chapter as a reference; you don't need to read it top-to-bottom.

## Where to find it

The whole window. The screenshot below is annotated with the regions discussed in this
chapter (image to be added in a later slice).

## Step-by-step

### Window layout

```
┌──────────────────────────────────────────────────────────────────────────────┐
│ Menu bar:  File   Edit   View   Project   Report   Tools   Window   Help     │
├──────────────────────────────────────────────────────────────────────────────┤
│ Toolbar:  New | Open | Save | … | Run | Summary | Profile | Graph | Table   │
│           Map Edit:   Select | Pan | Zoom | Add Junction | Add Conduit | … │
│           Animation:  ⏮ Skip Back | ◀ Backward | ▶ Forward | … | Speed     │
├──────────────┬──────────────────────────────────────┬─────────────────────┤
│  Layers      │                                       │   Object Browser   │
│  Dock        │     MDI tab area (Welcome + models)   │   Dock             │
│              │                                       │                     │
├──────────────┴──────────────────────────────────────┴─────────────────────┤
│                          Message Logs Dock                                 │
├──────────────────────────────────────────────────────────────────────────────┤
│ Status bar: Flow Units | Offset Mode | Coordinates | Map Scale | CRS       │
└──────────────────────────────────────────────────────────────────────────────┘
```

### Menus

| Menu | Items |
|------|-------|
| **File** | New | Open | Open Recent ▶ | Save | Save As | Export ▶ | Combine Projects | Page Setup | Print Preview | Print | Exit |
| **Edit** | Copy | Select Object | Select Vertex | Select Region | Select All | Find Object | Edit Object Properties | Delete Object | Group Edit | Group Delete |
| **View** | Pan | Zoom In | Zoom Out | Full Extent | Select by Attribute | Select by Location | Overview Map | Objects ▶ | Legends ▶ | Toolbars ▶ |
| **Project** | Project Summary | Project Details | Project Defaults | Calibration Data | Add Object ▶ | Run Simulation |
| **Report** | Status Report | Summary Report | Graph ▶ | Table ▶ | Statistics Report | Report Options |
| **Tools** | Program Preferences | Map Display Options | Configure External Tools |
| **Window** | Tile | Cascade | Close All |
| **Help** | Help Topics | Tutorials ▶ | About |

In the current slice (Slice A), only **File → New / Open / Open Recent / Save / Save As**
and **Help → About** are functional. Other items are wired in later slices.

### Toolbars

Three toolbars dock at the top:

- **Main toolbar** — common file and analysis actions.
- **Map Edit toolbar** — tool selectors for editing the network.
- **Animation toolbar** — playback of result time-steps (active when a results file is
  loaded).

### Docks

| Dock | Purpose |
|------|---------|
| **Layers** (left) | Layer tree showing all visible layers; toggle visibility, reorder, set CRS. |
| **Object Browser** (right) | Tree of every SWMM object grouped by type; double-click to edit. |
| **Message Logs** (bottom) | Information / warning / error log produced by the GUI and the engine. |
| **Simulation Status** (bottom) | Runtime simulation messages while a run is active. |

All docks are floatable, closable, and tabbable. Use **View → Toolbars** (later slice) to
restore a closed dock.

### MDI tab area

The center of the window holds **MDI sub-windows** — one per opened project plus the
Welcome tab. The tab bar at the top lets you switch between them. Each tab has its own
map canvas, model layer, and undo stack. Switching tabs rebinds the status-bar Flow
Units, offset mode, and CRS button to the active project.

### Status bar (left to right)

| Widget | Purpose |
|--------|---------|
| Progress bar | Indeterminate spinner while loading or saving a model. |
| **Flow Units** combo | CFS / GPM / MGD / CMS / LPS / MLD. Changing it writes the engine's `FLOW_UNITS` option for the **active project** and marks that project dirty. Disabled when no project is open. |
| **Offset Mode** toggle | Elevation vs Depth interpretation for link offsets — writes the engine's `LINK_OFFSETS` option for the active project. The label shows "Elevation" when on, "Depth" when off. Disabled when no project is open. |
| **Coordinates** | Live cursor coordinates in the canvas CRS. |
| **Map Scale** | Approximate display scale (1:N). |
| **CRS button** | Click to open the CRS picker. |

## Tips and gotchas

- The Welcome tab is just an MDI sub-window like any other. Closing it does not exit the
  app; you can reopen it via `Help → Welcome` (when wired in a future slice).
- Per-project state (units, offset, CRS, dirty marker, undo stack) follows the active
  tab. Switching tabs is the only way to "switch projects." The status bar widgets above
  (and any open property dialogs that show unit labels) re-bind automatically.
- Open two projects with different units (e.g. one CFS, one CMS): switch between them
  and watch the **Flow Units** combo and **Offset Mode** toggle flip to match each
  project's stored options. Edits in one project don't affect the other.

## Related

- [01 — Getting Started](01_getting_started.md) — first steps after install.
- [03 — Working with Projects](03_projects.md) — saving, dirty tracking, recents.
