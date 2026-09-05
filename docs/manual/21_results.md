@page manual_results 21 — Results Layers, Result Styling and Animation

## What you'll do

Get simulation results onto the map: load a run's `.out` (1D) and `.2d.h5` (2D)
files as layers, choose which run is *active* for analysis, colour the network
and the mesh by a result variable, read the legend, and play the whole run back
as an animation on a shared time cursor.

\figtodo{21_results_map_animated.png, The map canvas showing a 1D network coloured by flow over an animated 2D depth surface}

## Where to find it

| What | Where |
|------|-------|
| Load a `.out` | **File → Import → Open SWMM Output**; ribbon **Home → Import** |
| Load a `.2d.h5` | **File → Import → Add 2D Results…**; ribbon **Home → Import** |
| The loaded results layers | The **Layers** dock, under the **SWMM Results** category |
| Which run is active | The **Results Layers** group at the head of the **Analysis** ribbon tab — **1D results:** and **2D results:** combos |
| Playback | The **Results** menu and the **Results** ribbon tab |
| Styling | **Results → Set Style**, or right-click a layer → **Properties…** |
| Legend | **Results → Show Legend** (canvas overlay); **View → Panels → Legend** (`Ctrl+Alt+5`) for the dock |

## Step-by-step

### Getting results onto the map

There are three routes:

1. **After a run.** A finished (or cancelled) run auto-loads its `.out` as a
   results layer, auto-stretches its colour ramp and makes it the active 1D
   results layer. A 2D run's layer is created when the solver initialises and
   swaps to the on-disk `.h5` when the run ends. See \ref manual_running.
2. **Open SWMM Output.** Browse for an existing `.out`. The layer attaches to the
   active project's model layer — the `.out` carries names, not geometry, so a
   project has to be open first. Loading the same file twice focuses and reloads
   the existing layer instead of stacking a duplicate.
3. **Add 2D Results…** Browse for an OpenSWMM 2D results file (`*.h5`). The file
   picker starts in the model's own folder, because that is where a relative
   `[2D_OPTIONS] OUTPUT_FILE` lands. The layer is built exactly as an auto-load
   or a live run would build it: HDF5 UGRID mesh, simulation-start anchor, CRS
   inherited from the model, a peak-frame scan for the colour ramp, and the
   engine's `DRY_DEPTH` as the dry-cell threshold.

\figtodo{21_add_results_dialogs.png, The Open SWMM Output and Add 2D Results file pickers}

### Results layers in the Layers panel

Results layers appear under **SWMM Results** in the Layers dock and behave like
any other layer — visibility checkbox, opacity, drag to reorder, right-click for
**Properties…** and **Zoom to Layer** (see \ref manual_layers). A 2D results
layer is a *sublayer host*: expanding its row shows the individual visualization
passes (cell depth fill, smooth depth fill, depth contours, isolines, velocity
vectors, mesh edges, mesh vertices), each with its own visibility toggle, and
right-clicking a sublayer row opens the styling dialog on that sublayer's tab.

### Choosing the active run

The two combos in the **Results Layers** ribbon group name which loaded run every
analysis tool acts on:

| Combo | Feeds |
|-------|-------|
| **1D results:** | Comparison and profile plots, tabular view, statistics dashboard, colour-by-result, the animation controller |
| **2D results:** | Mesh-cell time series, mesh profiles, 2D cell picking, 2D animation |

Choosing **— none —** returns the project to model editing. The 1D list is drawn
from the project's output registry, so each entry is the `.out` basename (with
`(2)`, `(3)` … appended when two runs share a basename) and its tooltip is the
full path. A combo is disabled while the project has nothing of that kind loaded.
Setting the active layer from the Layers dock context menu keeps the combos in
step, and vice versa.

\figtodo{21_active_results_combos.png, The 1D results and 2D results selectors in the Analysis ribbon}

### Styling 1D results — colour and size by variable

**Results → Set Style** opens the layer properties dialog for the layer selected
in the Layers panel (falling back to the topmost visible layer). Its
**Symbology** tab is the same kind-tree editor used for model layers — one row
per element kind (Junctions, Outfalls, Storage, Dividers; Conduits, Pumps,
Orifices, Weirs, Outlets; Subcatchments; Rain gages) with a renderer per kind.

What makes a *results* layer different is the attribute list a graduated or
rule-based renderer can classify on. Alongside the static model fields, a results
layer publishes the run's **dynamic** result fields, which are re-read at every
animation step:

| Element kinds | Result attributes |
|---------------|-------------------|
| Junctions, Outfalls, Storage, Dividers | `depth`, `head`, `volume`, `inflow`, `overflow`, `lateralInflow` |
| Conduits, Pumps, Orifices, Weirs, Outlets | `flow`, `depth`, `velocity`, `capacity` |
| Subcatchments | `runoff`, `infiltration`, `evaporation`, `snowDepth` |
| Rain gages | none — gages carry no per-feature engine output |

Every one of those kinds *also* publishes one attribute per **water-quality
species** the run carries — each pollutant plus the reserved water-age and
temperature columns. Species attributes are named `qual:<SpeciesName>` and are
persisted by **name**, not by column index, so re-ordering pollutants in the
model can never silently re-point a saved theme. A run with no quality
constituents publishes no species attributes at all.

Because they are ordinary renderer attributes, everything the symbology system
offers applies: graduated colour, graduated size, rule-based classes,
classification methods, colour ramps and per-class overrides. Those controls are
documented once in \ref manual_styling. **Auto-stretch** sets the ramp range from
the data minimum and maximum across *all* time steps for the current variable, so
the colours stay comparable frame to frame.

\figtodo{21_results_symbology_tab.png, The Symbology tab of a results layer with a graduated flow renderer}

### Legends

- **Results → Show Legend** toggles a legend **overlay** drawn on the map canvas
  itself. Its right-click menu includes *Hide legend*, which unchecks the action.
- **View → Panels → Legend** (`Ctrl+Alt+5`) opens the dockable **Legend** panel,
  which lists each visible layer's classes and lets you edit a class's style in
  place (**Edit Style…** on the class row).

Whether a layer contributes to the legend at all is a per-layer setting on the
layer's properties.

\figtodo{21_legend_overlay_and_dock.png, The canvas legend overlay beside the dockable Legend panel}

### Playback — the Results ribbon tab

The **Results** tab (and the **Results** menu) carries the animation. One time
cursor drives everything: the 1D primary layer's report-step grid is
authoritative, and every other output — additional `.out` layers, the 2D mesh —
is snapped to it.

**Playback group**

| Control | Menu text | What it does |
|---------|-----------|--------------|
| **Backward** | **Results → Backward** | Step one period back |
| **Play** | **Results → Play** | Start playback; checked while playing |
| **Pause** | **Results → Pause** | Pause; checked while paused |
| **Stop** | **Results → Stop** | Stop and rewind |
| **Forward** | **Results → Forward** | Step one period forward |

**Timeline group**

| Control | What it does |
|---------|--------------|
| Time slider | A single thumb — the current time cursor. Drag it or click the track to scrub. The shaded band behind it is the look-back window, drawn ending at the cursor |
| **Window:** | Width of the look-back window, in minutes. Each non-driving output shows its latest frame at or before the cursor; the window is what classifies an output as fresh or stale. `0` = latest-at-or-before with no band |
| Date/time display | The cursor's calendar date and time (`MM/dd/yyyy hh:mm`), driven by the controller — read-only during playback |
| **Speed:** | Playback multiplier: 0.25×, 0.5×, 1×, 2×, 4×, 8×. The tick interval is the report step divided by the multiplier, floored at ~20 Hz — beyond that, higher speeds advance several frames per tick. Persisted across sessions |
| **Cycle** | On (the default) wraps to the start of the range at the end; off leaves playback paused at the end |

**Display group**

| Control | What it does |
|---------|--------------|
| **Show Legend** | Toggles the canvas legend overlay |
| **Set Style** | Opens layer properties for the selected layer |
| **Live 2D** | Renders and streams the active 2D layer during a run; only enabled for a live source |
| **Live 1D** | Opens the `.out` while it is still being written; a saved preference, applied to the next run |

Playback is *causal*: a secondary output never displays a frame ahead of the
cursor, and a hidden 2D layer is skipped entirely so scrubbing does not pay for
what you cannot see. A 2D-only run (no `.out` loaded) still animates — the 2D
layer becomes the controller's driver.

\figtodo{21_results_ribbon_tab.png, The Results ribbon tab — Playback; Timeline and Display groups}

\videotodo{Loading a run and animating it — choosing the active results layer; colouring by flow and scrubbing the timeline}

### Styling 2D results

A 2D results layer's properties dialog carries a dedicated panel with one tab per
visualization pass. Each tab starts with a **Show …** checkbox that toggles that
sublayer, and every control writes straight through to the renderer, so edits
repaint immediately.

| Tab | What it draws | Key controls |
|-----|---------------|--------------|
| **Cell Depth Fill** | Flat per-cell scalar fill | **Attribute** (Depth / Elevation); classification editor — continuous ramp or classified bins, method, class count, range |
| **Smooth Depth Fill** | Gouraud per-vertex fill | Same controls as Cell Depth Fill |
| **Depth Contours** | Filled contour bands | Class scheme; **Interpolate band boundaries (marching triangles)** — on interpolates boundaries through the triangle, off gives flat per-cell bands |
| **Depth Isolines** | Stroked iso-depth lines | **Mode** (Fixed count / Fixed interval + base); **Count**; **Method** (Equal interval, Quantile, Natural breaks (Jenks), Standard deviation, Logarithmic, Exponential); **Interval** and **Base level** (contours fall at base + k × interval); colour, width, stroke style; **Index contour every** N with its own width; contour **Labels** with decimals, font size and white halo |
| **Flow Velocity** | Centroid arrow glyphs | **Length scaling** (Linear / Square root / Logarithmic); **Scale** in px per m/s; **Min**/**Max length**; **Head size**; **Shaft width**; **Colour by magnitude** (with its own class scheme) or a single colour; **Spacing** — minimum on-screen px between arrows, strongest cell per slot wins; **Dry depth cutoff** — suppress arrows in shallower water |
| **Mesh Edges** | The triangulation's edges | Edge colour and width |
| **Mesh Vertices** | Vertex markers | Marker style |

Colour for the band and isoline classes comes from either a named **colour ramp**
(with an **Invert ramp** option) or a **two-colour gradient** between a **Low
colour** and a **High colour**.

\figtodo{21_2d_style_panel_tabs.png, The 2D results styling panel showing the Depth Isolines tab}

### How the 2D surface is reconstructed

Three behaviours are worth knowing because they explain what you see at the
wet/dry boundary — none of them is a control you set:

- **Dry threshold.** Cells shallower than the dry depth draw fully transparent.
  SWMMVis reads the engine's `[2D_OPTIONS] DRY_DEPTH` for the run and uses that
  value, so the render boundary matches what the solver calls wet. Without it a
  GUI default would clip a fringe of shallow-but-wet cells to black.
- **Vertex reconstruction.** The animated fill and the contours interpolate a
  per-vertex *signed* depth η−z rather than a clamped depth. When the engine
  supplies that field it is used directly; otherwise SWMMVis reconstructs it from
  the per-cell mean depths with the same depth-weighted, wet-masked rule the
  engine uses — deep, fully-wet cells dominate a shoreline vertex, and a wet cell
  only votes at a corner its water actually reaches. That gate is what stops a
  thin film at the base of a wall from notching the surface at the wall top.
- **Pooling extrapolation.** A no-data corner is filled by extending the free
  surface from the valid corners at constant elevation, then depth is
  `max(0, η − z)`. The result is that a pool against a rising bank tapers to the
  exact sub-cell bed intercept instead of stopping square at the cell edge, and
  the map agrees with the profile plot analytically rather than approximately.

The **maximum-depth envelope** available to the profile tools is the per-vertex
temporal maximum of exactly this field, so the envelope can never disagree with
the animation that produced it.

### Comparing several runs on one map

Load more than one `.out` into the same project and all of them animate together:
the active one drives the clock, the others follow causally. Practical notes:

- Give each run a **scenario name** so plots and the report viewer label it
  usefully. Scenario names are edited from the profile plot's options dialog
  (see \ref manual_profile_plots); the name is what the report viewer's run combo
  and the plot legends show.
- Runs with different report steps stay aligned because secondary layers snap to
  the cursor rather than to a period index. Use the **Window** box to decide how
  stale a secondary frame may be before you treat it as no longer current.
- Layer order and opacity in the Layers dock decide which run you see on top;
  turning a layer off removes it from the scrub cost entirely.

For quantitative comparison — differences, statistics, fit metrics — use the
comparison plots in \ref manual_time_series_plots rather than the map.

### Exporting what you see

There is **no dedicated animation-frame exporter**. To get an image out, park the
time cursor where you want it and use **File → Map Image…** or **File → Print**,
or `Ctrl+C` over the map to copy the current view to the clipboard (see
\ref manual_map_navigation). Repeat per frame if you need a sequence.

## Tips and gotchas

- **A `.out` needs its model.** Results carry element *names*; the geometry comes
  from the project's model layer. Opening a `.out` against a different network
  will only theme the elements whose names match.
- **Auto-stretch is global, not per frame.** The ramp range spans the whole run,
  which is what makes colours comparable across time. Re-stretch after you switch
  variables.
- **Species attributes are name-keyed.** A theme on `qual:TSS` survives a model
  edit that re-orders pollutants; a run that does not carry `TSS` simply renders
  no classes rather than mis-colouring.
- **The 2D dry threshold comes from the run, not from you.** If the inundation
  fringe looks clipped, check `[2D_OPTIONS] DRY_DEPTH` in the model rather than
  the styling panel.
- **8× is genuinely faster than 4×.** Above the tick floor the controller skips
  intermediate frames — expect fast playback to be visually coarser.
- **Hidden layers do not animate.** That is deliberate; if a 2D layer seems stuck,
  check its visibility checkbox before its time range.
- The 1D results combo lists only `.out`-backed layers; the 2D combo enumerates
  the canvas. A 2D layer whose file was deleted underneath it will still be
  listed until you remove the layer.

## Related

- \ref manual_running — producing the results this chapter displays; live 1D and 2D results
- \ref manual_styling — renderers; classification methods; colour ramps and the Style Manager
- \ref manual_layers — the Layers dock; layer ordering and opacity
- \ref manual_time_series_plots — plotting and comparing runs quantitatively
- \ref manual_profile_plots — HGL and 2D surface profiles built from the same fields
- \ref manual_tabular_results — the numbers behind the colours
- \ref manual_analysis_tools — 2D cell time series and the statistics dashboard
- \ref manual_2d_mesh — the mesh the 2D results are drawn on
- \ref manual_map_navigation — exporting and printing the map view
