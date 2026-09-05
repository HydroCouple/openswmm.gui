@page manual_profile_plots 23 — Profile Plots

## What you'll do

Draw a longitudinal section through the model and see what the water does along
it. There are three profile tools, each with its own dialog:

| Tool | Section through | Shows |
|------|-----------------|-------|
| **Plot Profile** | A routed path of nodes and links | Inverts, crowns, node structures, ground, animated HGL / EGL and their maximum envelopes, plus optional attribute tracks |
| **Plot 2D Profile** | A polyline you draw across the 2D mesh | Bed elevation, animated water depth and surface, and the maximum-depth envelope |
| **Terrain Profile** | A polyline you draw over a DEM raster | Ground elevation only |

\figtodo{23_profile_overview.png, A 1D profile plot with the HGL animation and two attribute tracks below it}

## Where to find it

| Command | Menu | Ribbon | Shortcut |
|---------|------|--------|----------|
| **Plot Profile** | **Analysis → Plot Profile** | **Analysis → Plots** | `Ctrl+Shift+T` |
| **Plot 2D Profile** | **Analysis → Plot 2D Profile** | **Analysis → Plots** | — |
| **Terrain Profile** | — | The **Terrain** toolbar's **Profile** group | — |
| 2D **bed-only** profile | — | The **Mesh 2D** toolbar's profile tool | — |

All three are checkable map tools: clicking the action arms the tool, clicking it
again returns to the select tool.

## Step-by-step

### 1D profile — choosing the path

**Plot Profile** arms the profile-trace tool on the map. What happens next depends
on what is already selected:

| Starting state | Behaviour |
|----------------|-----------|
| Two nodes already selected | Routing starts immediately between them |
| One node selected | It becomes the start; click the end node or link |
| Nothing selected | Click the start node or link, then `Ctrl`/`⌘`-click the end |

Clicking additional nodes between the endpoints adds **waypoints** the path must
pass through. The status bar narrates each step, and `Esc` cancels. The
previously accepted path stays drawn on the map so you can trace a new one from a
visible reference.

The router enumerates simple paths through the routing graph between the two
endpoints. When there is more than one candidate, a **path picker** lists them:

| Column | Meaning |
|--------|---------|
| Length | Total path length |
| Conduits | Number of conduits on the path |
| Non-conduits | Pumps, orifices, weirs and outlets on the path |
| Invert drop | Total fall from the first invert to the last |

Selecting a row highlights that candidate on the map; double-clicking zooms the
map to it; **OK** accepts it. If the enumeration hit its cap the dialog says the
list is truncated.

Because routing is a path search, a start and end with no connected route between
them reports exactly that rather than drawing a broken profile.

\figtodo{23_profile_path_picker.png, The path picker listing candidate routes with length; conduit counts and invert drop}

### What the 1D profile draws

Along the horizontal chainage axis, from the bottom up:

- **Soil** and **bedding** fill, with the **ground line** on top;
- each **node** as a structure sized by its invert and rim — junctions, outfalls,
  storage units and flow dividers each get their own fill and outline colours; a
  **virtual junction** draws as a dashed outline over the conduit that passes
  through it, because it is a break point, not a structure;
- each **link** as a barrel between its invert and crown, coloured per link type
  (conduit, pump, orifice, weir, outlet);
- the animated **HGL** — as a stroked line, an under-line fill, or both
  (independent toggles) — following the animation cursor;
- the animated **EGL** (line only; the velocity-head band above the HGL is not a
  meaningful filled region);
- the **maximum HGL** as a line and/or a band, and the **maximum EGL** as a line;
- a **flooding indicator**: an animated wedge above the rim of any node that is
  surcharging;
- **branch stubs** — truncated stubs of the links that meet a path node but are
  not followed — and a small **node rose** giving each connected link's map
  bearing and flow direction;
- optionally, the **2D inundation** water surface of the active 2D results layer,
  sampled along the path and drawn as a band above the ground with its own
  surface line — nothing draws when the project has no active 2D results layer;
- **node** and **link** labels on a secondary axis, vertical, diagonal or
  horizontal, or inline over the node glyphs;
- a **legend** and a **time label** showing the animation cursor's date and time.

The **ground line** comes from one of four sources, chosen in the layer panel or
the options dialog: **Auto** (the 2D mesh vertex elevations when the project has a
mesh, otherwise the node rims), **Node rims** (invert + maximum depth),
**Mesh 2D**, or **Terrain DEM** (sampled from the active DEM raster at each path
station).

\figtodo{23_profile_anatomy.png, An annotated 1D profile identifying ground; inverts; crowns; HGL; max HGL and node glyphs}

### The profile window

| Region | Contents |
|--------|----------|
| Toolbar | **Select**, **Zoom In**, **Zoom Out**, **Pan**, **Fit to Path**; **Sources**; **2D Inundation**; **Tracks**; **Export PNG…**; **Display Options…** |
| Header | *Path: N nodes, M links · Length: …* |
| Chart | The profile itself |
| **Layers** panel (right) | Quick toggles for the ground source, the current HGL line and fill, EGL, max HGL (as a min↔max band or an invert→max fill), and the label groups |
| Tracks pane (below) | Attribute tracks, when enabled |

Interaction mirrors the comparison plots: **Select** identifies and selects the
element under the cursor, **Zoom In** / **Zoom Out** accept a click or a
drag-rectangle, **Pan** drags the view, and **Fit to Path** returns to the full
extent.

Right-clicking a node or a link in the profile gives **Zoom to on map**, a
**Plot Time Series…** submenu of that kind's attributes, and **Properties…**. The
time-series pick opens a comparison plot that floats over the profile window —
see \ref manual_time_series_plots.

The profile follows the global animation cursor, so pressing **Play** on the
Results tab animates the HGL along the section while the map animates in step.

\figtodo{23_profile_toolbar_layers.png, The profile toolbar and the Layers panel with the HGL and label toggles}

### Comparing runs in one profile — the Sources panel

The **Sources** toolbar button lists every results layer the animation controller
knows about, with a checkbox each; the button caption reads *Sources (on/total)*.
Each enabled source draws its own HGL in its own tint, so several runs share one
section.

The **Sources** tab of the Display Options dialog does more:

- toggle a source on or off;
- recolour it;
- **rename** it — the name you type becomes the run's **scenario name**, which is
  also what the comparison plots' legends and the report viewer's run combo show;
- **add an external `.out` file** as an extra comparison source, without loading
  it into the project as a map layer.

\figtodo{23_profile_sources_tab.png, The Sources tab of the profile Display Options dialog}

### Attribute tracks

The **Tracks** toolbar button adds mini-charts below the profile, sharing its
chainage axis exactly — the tracks pane adopts the profile's left and right
gutters and stays locked to it through zoom and pan.

Eleven attributes can be tracked, all of them read from the same runs the profile
already has open:

| Node tracks | Link tracks |
|-------------|-------------|
| Depth, Head, Volume, Lateral inflow, Total inflow, Overflow | Flow, Depth, Velocity, Volume, Capacity |

Node attributes draw as a polyline through the value at each node. Link
attributes draw as one horizontal segment per link with no vertical connectors —
a link's flow or velocity is a single value over its length, and interpolating it
across nodes would invent data.

Pane-level options are **Track height**, **Show track titles**, and an
**envelope** overlay (the per-element minimum-to-maximum range over the whole run)
with its own opacity. Each attribute has its own pen — colour, width and dash —
editable from the **Attribute Tracks** tab of the Display Options dialog.

Elements the results file does not know leave a gap rather than a zero, so
"no data" and "zero" stay distinguishable.

\figtodo{23_profile_attribute_tracks.png, Two attribute tracks below a profile with the envelope overlay visible}

### Display Options

**Display Options…** opens a tabbed property editor over the profile's option
object. Everything is live — the plot repaints as you edit.

| Tab | What it edits |
|-----|---------------|
| **Display** | Every profile knob: layer visibility, labels, ground source, 2D inundation, connectivity, flooding indicator, per-type colours, line pens and fill brushes, axis number formats, legend and time label |
| **Sources** | The per-run controls described above |
| **Attribute Tracks** | The track options above (present once tracks are enabled) |

Selected controls worth knowing:

| Group | Controls |
|-------|----------|
| Layer visibility | **HGL line**, **HGL fill**, **EGL**, **Max HGL band**, **Max HGL line**, **Max EGL line** |
| Labels | **Node labels**, **Link labels**, **Inline node labels**, **Orientation** (Vertical / Horizontal / Diagonal) and an explicit **angle** |
| Ground | **Ground source** — Auto / Node rims / Mesh 2D / Terrain DEM |
| 2D inundation | **Show 2D inundation** plus the band's line pen and fill brush |
| Connectivity | **Show branch stubs**, **Show node roses** |
| Flooding | Wedge **radius**, **sweep angle** and **colour** |
| Theming | Fill and outline colours per node type and per link type; the virtual-junction pen; soil and bedding fills; a pen per result line and a brush per result fill |
| Axes | **X** and **Y number format** — decimals, significant figures, scientific, engineering or thousands-separated, with a digit count, or an explicit format string |
| Legend | **Visible**, **Position** (four corners), **Font**, **Opacity**, **Offset** |
| Time label | **Visible**, **Position**, **Colour**, **Font**, **Format** (default `dd-MMM-yyyy HH:mm:ss`), **Offset** |

**Export PNG…** saves the chart as an image. There is no numeric export from the
profile window — plot the same attributes in the comparison plot and export from
there.

\videotodo{Tracing a profile between two manholes — picking a route; animating the HGL and adding attribute tracks}

### 2D mesh profile

**Plot 2D Profile** arms a polyline tool over the mesh: click to add vertices,
double-click or `Enter` to finish, right-click to undo the last vertex, `Esc` to
cancel. The action tells you so in its tooltip, and refuses to arm when the
project has no mesh loaded.

The **2D Mesh Profile** window draws, along the traced chainage:

- the **bed elevation** sampled from the mesh, with soil fill below it;
- the **water depth** as a fill and a **water-surface (WSE) line**, animated with
  the global time cursor;
- the **maximum-depth envelope** as a fill and/or a line;
- optional **cell boundary** dots marking where the line crosses mesh-cell edges.

Its toolbar carries **Select**, **Zoom In**, **Zoom Out**, **Fit**, **Pan**,
**Cell boundaries**, **Move marker on map**, and **Display Options…**. **Move
marker on map** turns the traced line on the map into a draggable position arrow —
drag it and the profile's cursor follows, so you can relate a feature in the
section to a place on the map.

The same tool on the **Mesh 2D** toolbar draws a **2D Mesh Bed Profile** — the
identical chart with no water, useful while you are building the mesh and before
any run exists.

Display Options for the mesh profile edits: **Show depth fill**, **Show WSE
line**, **Show max envelope fill**, **Show max envelope line**, **Show cell
boundaries**; the soil brush, ground pen, depth brush, WSE pen and envelope pen
and brush; the cell-boundary colour; X and Y axis number formats; and the same
legend and time-label groups as the 1D profile.

\figtodo{23_2d_mesh_profile.png, A 2D mesh profile with the bed; the animated water surface and the maximum-depth envelope}

#### How the water surface is drawn at the edges

Two behaviours explain what you see where the water meets dry ground. Neither is a
control you set, but both matter when reading the plot:

- **Free-surface extrapolation.** The profile interpolates the free surface η, not
  a clamped depth, and treats a corner with no valid η as no-data rather than as
  η = bed. A pool against a rising bank therefore stays level instead of climbing
  the bank, and the depth tapers to the exact point where the surface meets the
  bed.
- **Shoreline intercept.** The painted band is closed at the computed
  surface-and-ground intersection rather than at the last sample that happened to
  be wet, so the water tapers to a point instead of ending in a vertical cliff up
  to half a cell short of the shoreline.

Dry gaps between two wet runs are bridged **only** when the gap contains a cell
with no valid surface at all. Two pools separated by a genuinely dry crest stay
split, each flat at its own level; a bridged surface is monotone between its two
wet ends, so water is never painted flowing uphill; and an off-mesh sample inside
a gap blocks bridging across it, preserving a real hole in the data.

Because the map fill and the profile now share the same reconstruction, the
inundation edge you see on the map and the waterline in the profile agree.

### Terrain profile

The **Terrain** toolbar's profile tool traces a polyline over the **active terrain
raster** — the DEM chosen in that toolbar's **Active Terrain** combo. Click to add
vertices, double-click or `Enter` to finish, right-click to undo, `Esc` to cancel.

The **Terrain Profile** window is the mesh-profile chart with no water: ground
elevation only, with **Select**, **Zoom In**, **Zoom Out**, **Fit**, **Pan**,
**Move marker on map** and **Display Options…**.

Elevations are converted from the DEM's raw values into the project's vertical
units using the terrain toolbar's **DEM vertical unit** and **× factor** controls.
Change either and the profile re-samples, so the ground line always reads in model
units.

\figtodo{23_terrain_profile.png, A terrain profile traced over a DEM with the position marker on the map}

## Tips and gotchas

- **Select two nodes first.** It is the fastest route to a profile — the tool
  routes immediately without any further clicking.
- **Many candidate paths means a meshed network.** Add waypoints to narrow the
  search rather than scrolling a long picker.
- **A flat HGL that ignores your run** usually means the wrong sources are
  enabled — check the **Sources** button's on/total count.
- **The ground line can come from four places.** If it looks wrong, set
  **Ground source** explicitly instead of leaving it on Auto.
- **The 2D inundation overlay needs an active 2D results layer**, chosen in the
  Analysis toolbar's **2D results:** selector. Without one the toggle draws
  nothing.
- **Link tracks are step functions on purpose.** The horizontal segments are the
  data; there is no interpolation between links.
- **Rename a source, and you have renamed the run.** The Sources tab writes the
  scenario name that the comparison plots and the report viewer also show.
- **Profiles export as pictures only.** For numbers, plot the same variables in
  the comparison plot and use its CSV export.

## Related

- \ref manual_results — the results layers and the animation cursor these plots follow
- \ref manual_time_series_plots — the comparison plot that opens from a profile right-click
- \ref manual_running — producing the results; live 1D results extend an open profile
- \ref manual_2d_mesh — the mesh the 2D profile samples; and the mesh-toolbar bed profile
- \ref manual_layers — loading the DEM the terrain profile samples
- \ref manual_selection — selecting the endpoints the router uses
- \ref manual_hydraulics — inverts; rim elevations and the geometry the section draws
