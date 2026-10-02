# Manual figure capture and review

The capture manifest is a work queue, not evidence that a figure is finished.
Use `python3 scripts/manual_figures.py audit` for current counts and
`python3 scripts/manual_coverage.py` to refresh the capability ledger under
`workplans/`. Some figures still lack a tested recipe. The procedures below
cover native controls and map interactions that need an interactive session.

## Reproducible automated batches

Use an explicit application binary and a **new** output directory for each
batch. First prepare working copies of the portable examples:

```sh
python3 scripts/prepare_manual_fixtures.py \
  --output tests/output/manual_figures/my-review/fixtures \
  --engine /absolute/path/to/openswmm
python3 scripts/capture_manual_figures.py \
  -a /absolute/path/to/SWMMVis.app/Contents/MacOS/SWMMVis -l \
  -m tests/output/manual_figures/my-review/fixtures/site/site_drainage_model.inp \
  -o 02_window_regions.png,02_ribbon_features_tab.png \
  -d tests/output/manual_figures/my-review/interface
```

`-a` selects the build; `-l` selects the native display; `-m` selects a working
model; `-o` selects exact manifest filenames; `-f` supplies an alternate manifest;
`--prepare-only` validates and records a batch without starting the app.
The shell launcher accepts the same options. On macOS an offscreen run requires
`QT_PLUGINS` from the selected build. Scene-graph maps require the native display. Standard Qt dialogs and
Qt Charts can be rendered offscreen, but inspect each result; scene-graph
content that appears blank must be recaptured natively. Record the actual
platform in the batch baseline.

The driver isolates application settings and writes `capture-baseline.json`,
`capture-manifest.json`, `capture.log` and `run.json`. The baseline records the
binary and engine hashes, source revision/diff fingerprint, platform and model.
A nonzero exit, failed capture, hidden target or blank image needs investigation.
An `ok` result still requires a human visual check against the caption.

Manifest `_model` is a preparation hint; it does **not** switch projects during
a batch. Group rows by compatible fixture and select the appropriate `-m`.
Do not run the whole manifest against one model. Some editors apply changes
immediately even when later closed, so capture only disposable working copies.
A GUI screenshot proves the displayed state, not solver correctness or persistence.

Whole-window recipes use `page` to activate the actual project and
`prepareActions: ["map.zoomExtent"]` to fit the network. Scope ribbon tabs with
`tabIn: "compactToolbarTabBar"` to avoid matching similarly named dock tabs.
`hostSelectMore` adds selected objects, for example two profile endpoints.
`dialogPage` selects a tab in a child dialog opened by `click`. `hide` and `show`
configure named docks in this isolated session. `floatDock: true` detaches a
dock before applying `size`, so neighboring panels cannot squeeze a table
back to a narrow sidebar. Use `page` for the category and `tab` for a control
that becomes visible only after the host object is selected (for example the
Section View's `10:1` scale).

**Why these are by hand.** The capture engine reaches widgets — dialogs, docks,
panels, tables, ribbons. It cannot reach:

- **Native menus.** A menu bar menu or a right-click context menu is an
  `NSMenu` on macOS, not a Qt widget the engine can grab. The engine also
  refuses to trigger an action that opens one (`nativePickerActions_()`), so a
  run cannot hang on a menu nobody is there to dismiss.
- **Native file pickers.** Open / Save As / Export use the system panel.
- **Tooltips and hover states.** They need a real pointer resting on a target.
- **Things outside the app** — the disk image, a terminal, a text editor.
- **The pointer on the map canvas** — a rubber band, a lasso, a dragged
  vertex, the two clicks that route a profile. Driving those would mean
  synthesising mouse events at canvas pixels computed from model coordinates,
  which is the map internals the engine is deliberately kept out of.

Save everything into `tests/output/manual_figures/` (git-ignored). That is
where `scripts/manual_figures.py flip` looks by default, so a finished shot
publishes with the same command the automated ones use.

## Reproducible selection and report captures

The manifest supports `activate` for a report section: unlike `select`, it
also activates the selected view row so the report body scrolls. For example,
`"activate": "Flow Routing Continuity"` captures that section, not merely its
highlight in the navigator.

Use `sortColumn` with the exact visible table header for an ascending sort.
For an explicit query, set `type` and `typeInto`, then use
`afterTypeClick: "Apply"` to press the inline button after entering the text.
Unlike `click`, this step does not expect a new dialog. Check the matched-row
count and selected rows; typing a clause alone does not apply it.
Use `column` separately to scroll a wide table to the fields in the caption.
For a Plot Variables picker, `check` is a list of exact row paths, for example
`["Node J11 > Depth (node) (ft)", "Link C11 > Flow (ft³/s)"]`.
The ` > ` separator permits `/` in unit labels. Checks are applied before
`"click": "OK"` opens the plot. Do not use Select All when the caption promises
only particular variables.

These operations use visible Qt controls and their public signals. A capture
that succeeds still needs visual review: a populated plot can have illegible
axes or an incorrect legend. Keep failed candidates in the review output and
leave their published placeholders intact.

## 0. Set up an interactive session

Use a dedicated documentation session and working copies. Do not change or
close a user's active modeling session. Before starting:

1. Build and launch: `cmake --build build --target SWMMVis -j 8` then open
   `build/SWMMVis.app`.
2. **Preferences → Appearance → Light.** Every published figure is light-theme;
   use the same theme throughout a chapter batch.
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

### 2.5 A completed Bellinge run (6 shots)

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

### 2.7 Map interaction — pointer on the canvas (22 shots)

The capture engine drives widgets through their public API. These figures are
of the *pointer doing something on the map* — dragging a rubber band, lassoing,
hauling a vertex, clicking two nodes to route a profile. Driving those would
mean synthesising mouse events at canvas pixels computed from model
coordinates, which is exactly the map internals the engine is kept out of.
A person with a mouse does them in a couple of sittings.

**Profiles (chapter 23 and the tutorials) all start the same way:** Analysis →
**Plot Profile** (`Ctrl+Shift+T`) arms the profile tool; click the upstream
node, then the downstream one; the path picker lists the candidate routes.
Shoot `23_profile_path_picker` at that moment, pick a route, and the rest of
the chapter's figures come from the profile window that opens.

| File | Model | What must be visible |
|---|---|---|
| `05_zoom_rubber_band.png` | site_drainage | Zoom In tool mid-drag, rubber band over part of the network. |
| `09_selection_overview.png` | site_drainage | One map selection mirrored in the Object Browser, the Properties panel and the Attribute Table — all four in frame. |
| `09_select_by_polygon.png` | site_drainage | A lasso mid-draw across the network with the enclosed objects highlighted. |
| `09_select_upstream.png` | site_drainage | The upstream subnetwork of a selected outfall highlighted on the map. |
| `09_mesh_edge_selection.png` | 2d_complete_example | Boundary edges selected along an outfall face. |
| `09_confirm_bulk_delete.png` | site_drainage | The Confirm Delete prompt for a multi-object selection. **Cancel it** — never delete from a tracked example. |
| `12_inline_vertex_edit.png` | site_drainage | A conduit in edit mode, interior vertex handles showing, two of them selected. |
| `12_add_node_terrain.png` | 2d_complete_example | Placing a junction with a DTM active on the Terrain toolbar. |
| `12_add_conduit_snap.png` | site_drainage | Drawing a conduit with the snap indicator ringing the target node. |
| `23_profile_overview.png` | Bellinge (run) | A 1D profile with the HGL animation and two attribute tracks below it. |
| `23_profile_path_picker.png` | Bellinge (run) | The path picker listing candidate routes with length, conduit counts and invert drop. |
| `23_profile_anatomy.png` | Bellinge (run) | Ground, inverts, crowns, HGL, max HGL and node glyphs. **Annotated** — shoot the base, then draw the callouts (see §5). |
| `23_profile_toolbar_layers.png` | Bellinge (run) | The profile toolbar and the Layers panel with the HGL and label toggles. |
| `23_profile_sources_tab.png` | Bellinge (run) | The Sources tab of the profile Display Options dialog. |
| `23_profile_attribute_tracks.png` | Bellinge (run) | Two attribute tracks below a profile with the envelope overlay visible. |
| `23_2d_mesh_profile.png` | 2d_complete_example | A 2D mesh profile: bed, animated water surface, maximum-depth envelope. |
| `23_terrain_profile.png` | Bellinge (run) | A terrain profile traced over the DEM with the position marker on the map. |
| `t01_profile_plot.png` | site_drainage | Profile from J3 to the outfall with the maximum HGL envelope. |
| `t03_2d_profile.png` | 2d_complete_example | A 2D mesh profile across the bowl with the ground line and water surface. |
| `t04_profile_2d_overlay.png` | 2d_complete_example | Profile from J1 to OUT1 with the 2D water surface overlaid. |
| `t05_profile_across_embankment.png` | demo_road_culvert | A 2D profile across the road embankment with the ponded upstream surface. |
| `t08_profile_pumped_branch.png` | Bellinge (run) | A profile through a pumped branch with the HGL at an animation time. |

"Bellinge (run)" means a working copy of
`examples/bellinge_2d/BellingeSWMM_v021_nopervious.inp` with its external mesh,
DEM and rainfall dependencies and a newly completed run. Do not depend on a
private Downloads folder or assume generated results are checked in. It takes a couple of minutes to open — do all of
its shots in one session.

### 2.8 Outside the app (2 shots)

| File | What must be visible |
|---|---|
| `a02_oswp_structure.png` | An `.oswp` open in a text editor, scrolled so both the `sessions` and `meshLayers` blocks are visible. Use `examples/site_drainage/site_drainage_model.oswp`; a plain light editor theme matches the manual best. |
| `a01_timeseries_editor_keys.png` | The time-series editor toolbar with **Insert**, **Delete**, **Copy** and **Paste** identifiable. The buttons are icon-only, so hover one and frame the strip with its tooltip — the automated grab was rejected precisely because unlabelled icons do not serve this caption. |

## 3. Annotated figures

The window guide in chapter 02 now uses numbered HTML overlays above an
unaltered PNG. Keep its overlay aspect ratio synchronized with the raw image
size, and inspect the rendered page at desktop and narrow widths. The overview
and introduction reuse the same application state without claiming to label
every control in the image itself.

`23_profile_anatomy` and `t05_bc_types_reference` still need their own real
captures and matching explanations. A profile figure that labels maximum HGL
must actually show that envelope; the ordinary first-timestep overview is not
sufficient. Preserve the unannotated original and add labels in the document
layer where possible.

## 4. Publish them

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

## 5. Check each one against its caption before flipping

This is the discipline the automated lane learned the hard way: a capture can
succeed and still be the wrong picture. The caption is the contract — if it
names a tooltip, the tooltip has to be in the frame; if it says "twelve
selected conduits", twelve rows have to be highlighted.

If a shot cannot match its caption, **do not publish it**. Say so instead, and
either the caption or the feature needs to change. Several captions have
already been corrected this way — a placeholder is honest about being missing,
a wrong figure is not.

## 6. Record acceptance and check the rendered manual

After inspecting the full-size PNG, publish it with `manual_figures.py flip`.
Record its **published** SHA-256 (after lossless optimization), review date,
batch baseline and any intended reuse in `docs/manual/figure_reviews.json`.
Never mark every image in a successful batch accepted automatically. Keep
rejected attempts in the batch directory with their reason in the handoff.

`manual_figures.py audit --strict` is the completion gate: it fails for missing
recipes, unfinished figures/videos or absent/stale visual reviews. The ordinary
audit remains suitable for incremental chapter batches. Build with `doxygen
Doxyfile` from `docs/`, then inspect the HTML images, captions and numbered
orientation guide at desktop and narrow reading widths.

## Populated hydrology editors

Prepare the tracked synthetic example with `prepare_manual_fixtures.py --case
hydrology`. Select its copied `hydrology_editor_example.inp` for the `13_` LID,
snowpack, aquifer, groundwater, initial-loadings and unit-hydrograph recipes.
Use a native batch and inspect every value against the source input. The LID
Control editor is an explicit exception: imported layer values show defaults,
so capture its warning without editing it. Use the wider recipe for its diagram
and the Aquifer editor so callouts remain readable. The Initial Abstraction
capture shows the first decay columns; its remaining recovery/snow columns are
reached by horizontal scrolling.

The example is uncalibrated. Snowmelt is disabled and exponential RDII decay
runs without temperature data, producing the expected T_ref warning. Review
runoff, groundwater and routing continuity separately. A successful run is not
validation of snowmelt, design parameters, or edit/save persistence.
