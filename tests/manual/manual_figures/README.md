# The 26 manual figures — by hand

Companion to the automated figure capture (`docs/manual/figures.json`,
`scripts/capture_manual_figures.sh`). Every other figure in the user manual is
captured by driving the app from a manifest; these 26 cannot be, and this page
is the click-through for them.

**Why these are by hand.** The capture engine reaches widgets — dialogs, docks,
panels, tables, ribbons. It cannot reach:

- **Native menus.** A menu bar menu or a right-click context menu is an
  `NSMenu` on macOS, not a Qt widget the engine can grab. The engine also
  refuses to trigger an action that opens one (`nativePickerActions_()`), so a
  run cannot hang on a menu nobody is there to dismiss.
- **Native file pickers.** Open / Save As / Export use the system panel.
- **Tooltips and hover states.** They need a real pointer resting on a target.
- **Things outside the app** — the disk image, a terminal, a text editor.

Save everything into `tests/output/manual_figures/` (git-ignored). That is
where `scripts/manual_figures.py flip` looks by default, so a finished shot
publishes with the same command the automated ones use.

## 0. Set the app up once

These figures come from your own running app, so your preferences are in every
shot — unlike a capture run, which isolates its settings. Before starting:

1. Build and launch: `cmake --build build --target SWMMVis -j 8` then open
   `build/SWMMVis.app`.
2. **Preferences → Appearance → Light.** Every published figure is light-theme;
   a dark shot will stand out badly next to the other 94.
3. Size the window to about **1600 × 1000**. Bigger is not better — the strip
   gets downscaled and goes illegible.
4. Close panels the figure does not need, so the shot is not mostly chrome.

## 1. How to take the shot

On a Retina display, macOS screenshots are already 2× — which is what the
manual wants.

| What you want | How |
|---|---|
| One window, with its shadow trimmed | `Shift`-`Cmd`-`4`, then `Space`, then click the window. Hold `Option` while clicking to drop the drop-shadow. |
| A region (a menu, a tooltip, a toolbar) | `Shift`-`Cmd`-`4`, then drag. |
| A menu that closes when you press keys | Start the screenshot first (`Shift`-`Cmd`-`4`), *then* open the menu — the crosshair survives it. Or use `Shift`-`Cmd`-`5` with a delay. |

Then move the file into `tests/output/manual_figures/` under the exact name in
the table below.

**The two hard limits** (`flip` refuses the batch otherwise):

- **≤ 1600 px wide.** Crop rather than scale where you can.
- **≤ 400 KB.** `oxipng -o 4 --strip safe <file>` usually does it; a dense
  screenshot may need a smaller window instead. The flip runs `oxipng` for you,
  but it only recovers a few percent on an already-compressed PNG.

Include the whole menu *and* enough of what it was opened from that a reader
can tell where they are. A context menu floating on white is not a figure.

## 2. The shots

Grouped by the model you need open, so you open each one once.

### 2.1 No model (2 shots)

| File | What must be visible |
|---|---|
| `01_install_macos_dmg.png` | The macOS disk image with SWMMVis.app being dragged into Applications. Open the built `.dmg`, start the drag, screenshot mid-drag. |
| `a04_redraw_log.png` | Terminal output from `SWMMVIS_LOG_REDRAW` during a pan and a selection. Launch with `SWMMVIS_LOG_REDRAW=1 build/SWMMVis.app/Contents/MacOS/SWMMVis`, pan the map, drag a selection, then shoot the terminal. |

### 2.2 `examples/site_drainage/site_drainage_model.inp` (9 shots)

Small and fast; most menu shots belong here.

| File | What must be visible |
|---|---|
| `03_file_menu.png` | The **File** menu open with **Open Recent** expanded. Open a couple of models first so Open Recent is not empty. |
| `02_menu_model_expanded.png` | The **Model** menu expanded showing the Add Node, Add Link, Climate, Data Objects and Mesh submenus. |
| `13_model_data_objects_menu.png` | **Model → Data Objects** submenu, with the hydrology editors visible. |
| `17_data_objects_menu.png` | **Model → Data Objects** submenu. Same menu as above; shoot it once and save under both names if the framing suits both chapters. |
| `16_quality_overview.png` | The **Model** menu's quality entries — Data Objects (Pollutants, Land Uses), plus Initial Quality, Reaction System and Heat Configuration. |
| `05_canvas_context_menu.png` | Right-click a conduit on the canvas; the menu must show **Plot Time Series** and **Convert To**. |
| `05_hover_tooltip.png` | Hover a conduit until the tooltip appears — name, type and vertex count. Start the screenshot before hovering. |
| `10_browser_context_menu.png` | Right-click a junction leaf in the Object Browser. |
| `07_layer_context_menu.png` | Right-click the layer row in the Layers panel with the **Styles** submenu open. |

### 2.3 `examples/site_drainage/site_drainage_model.inp` — native pickers (4 shots)

| File | What must be visible |
|---|---|
| `03_save_as_dialog.png` | **File → Save As**, with the format dropdown open showing the project, input and GeoPackage filters. Cancel — do not save over the example. |
| `05_export_map_dialog.png` | **Export Map**, with the PNG filter selected. Cancel. |
| `a02_export_filters.png` | The same Export Map dialog with the filter list open, showing every available filter. Cancel. |
| `21_add_results_dialogs.png` | The **Open SWMM Output** and **Add 2D Results** pickers. Two dialogs; either shoot them side by side or compose the two shots into one image. Cancel both. |

> Cancel every one of these. A figure must never write to a tracked example.

### 2.4 `docs/manual/tutorials/models/street_inlet_junction.inp` (2 shots)

| File | What must be visible |
|---|---|
| `10_convert_to_menu.png` | **Convert To** submenu with **Virtual Junction** greyed out, and its rule tooltip showing. Hover the greyed entry until the tooltip appears. |
| `t02_rule_violation.png` | The same greyed Convert To entry with the virtual-junction rule text as its tooltip. Same interaction as above, framed for the tutorial. |

### 2.5 The executed Bellinge — `~/Downloads/bellinge_2d/BellingeSWMM_v021_nopervious.oswp` (6 shots)

These need a completed run. Bellinge takes a couple of minutes to open; do all
six in one session.

| File | What must be visible |
|---|---|
| `11_apply_value_to_rows.png` | In the Attribute Table on Conduits, select twelve rows, right-click a roughness cell; the menu must offer to apply one value to the selection. |
| `a05_message_logs.png` | The Message Logs dock with an **error** row and its context menu open. |
| `22_series_tree_context_menu.png` | Comparison Plot (select a node → `Ctrl+T` → Select All → OK): the series tree with a run group, a baseline marker, and the series context menu open. |
| `22_load_observed.png` | In that plot, **Load Observed…**, choose a CSV, and shoot the observed-attribute prompt that follows. |
| `22_chart_modes_toolbar.png` | The Comparison Plot toolbar. The icons carry no labels, so hover one long enough for its tooltip and frame the strip with that tooltip showing — otherwise a reader cannot tell the modes apart. |
| `25_pick_2d_cells_menu.png` | A lasso selection of mesh cells with the attribute context menu open. |

### 2.6 `docs/manual/tutorials/models/2d_complete_example.inp` (1 shot)

| File | What must be visible |
|---|---|
| `t03_add_2d_results.png` | The **Add 2D Results** file dialog. Cancel. |

### 2.7 Outside the app (2 shots)

| File | What must be visible |
|---|---|
| `a02_oswp_structure.png` | An `.oswp` open in a text editor, scrolled so both the `sessions` and `meshLayers` blocks are visible. Use `examples/site_drainage/site_drainage_model.oswp`; a plain light editor theme matches the manual best. |
| `a01_timeseries_editor_keys.png` | The time-series editor toolbar with **Insert**, **Delete**, **Copy** and **Paste** identifiable. The buttons are icon-only, so hover one and frame the strip with its tooltip — the automated grab was rejected precisely because unlabelled icons do not serve this caption. |

## 3. Publish them

Stage everything, then flip in one go:

```bash
ls tests/output/manual_figures/            # the names must match exactly
python3 scripts/manual_figures.py flip 01_install_macos_dmg.png 03_file_menu.png …
python3 scripts/manual_figures.py todo     # refresh docs/manual/images/TODO.md
cd docs && doxygen Doxyfile 2>&1 | grep -i "image file .* is not found"   # must be empty
```

`flip` verifies the whole batch before it touches anything: a missing file, an
over-wide image or one over 400 KB refuses the lot, and nothing is half-applied.
It copies the PNGs into `docs/manual/images/`, turns each `\figtodo` into
`\fig`, runs `oxipng`, and re-runs the audit.

## 4. Check each one against its caption before flipping

This is the discipline the automated lane learned the hard way: a capture can
succeed and still be the wrong picture. The caption is the contract — if it
names a tooltip, the tooltip has to be in the frame; if it says "twelve
selected conduits", twelve rows have to be highlighted.

If a shot cannot match its caption, **do not publish it**. Say so instead, and
either the caption or the feature needs to change. Several captions have
already been corrected this way — a placeholder is honest about being missing,
a wrong figure is not.
