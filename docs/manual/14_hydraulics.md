@page manual_hydraulics 14 — Hydraulics: Nodes, Links, Cross-Sections, Streets, Inlets and Controls

## What you'll do

Build the conveyance network — junctions, virtual and inlet junctions, outfalls,
dividers and storage units joined by conduits, pumps, orifices, weirs and
outlets — give the conduits cross-sections, transects or street sections, attach
inflows and treatment to the nodes, and drive the whole thing with control rules.

## Where to find it

| Task | Where |
|---|---|
| Add a node | **Model → Add Node → Add Junction / Add Virtual Junction / Add Inlet Junction / Add Outfall / Add Flow Divider / Add Storage** (ribbon **Model ▸ Nodes**) |
| Add a link | **Model → Add Link → Add Pipe / Add Pump / Add Orifice / Add Weir / Add Outlet** (ribbon **Model ▸ Links**) |
| Edit a selected object | Properties panel (right dock) or the object's attribute table |
| Cross-section / inlet usage | The **Cross Section** and **Inlets** property rows on a conduit → **Edit…** |
| Inflows / DWF / RDII / Treatment | The matching property rows on a node → **Edit…** |
| Transects | **Model → Data Objects → Transects…** |
| Streets | **Model → Data Objects → Streets…** (ribbon **Model ▸ Data Objects ▸ Street**) |
| Inlets | **Model → Data Objects → Inlets…** (ribbon **Model ▸ Data Objects ▸ Inlet**) |
| Control rules | **Model → Data Objects → Control Rules…** (ribbon **Model ▸ Data Objects ▸ Control Rule**) |
| Offset convention | The **Offset Mode: Depth ⇄ Elevation** toggle on the status bar |

Drawing and snapping the network on the map is covered in
\ref manual_map_editing; this chapter is the reference for what the objects
*are*.

\figtodo{14_model_ribbon_nodes_links.png, The Model ribbon tab with the node and link groups}

## Step-by-step

### Link offsets — Depth or Elevation

The status bar carries an **Offset Mode:** toggle flanked by the labels
**Depth** and **Elevation**; the active side is bolded. It writes the
`[OPTIONS]` key `LINK_OFFSETS` (`DEPTH` is the SWMM default and the unchecked
position). Flipping it changes the *convention*, and the property labels follow:
a conduit's rows read **Inlet Offset** / **Outlet Offset** in depth mode and
**Inlet Elevation** / **Outlet Elevation** in elevation mode; an orifice's or
outlet's single row reads **Offset** or **Elevation**.

The switch happens whether or not you convert; when the model already has links,
SWMMVis then offers to convert the existing offset values to the new convention
(matching legacy EPA SWMM behaviour, which skips the prompt on an empty
network). Both the Properties panel and the attribute tables re-label and
re-value together.

\figtodo{14_offset_mode_toggle.png, The status-bar offset-mode toggle in elevation mode}

### Node types and their properties

Every node carries **Name**, **Node Type** (read-only), **Invert Elev.**,
**Tag**, **X Coordinate**, **Y Coordinate**, a read-only computed block
(**Crown Elev.**, **Full Volume**, **Connected Links**) and, after a run,
**Max Depth, Sim.**, **Max Overflow**, **Vol. Flooded** and **Time Flooded**.
Every node also carries the five compound rows — **External Inflows**,
**Dry Weather Flow**, **RDII**, **Pollutant Treatment** and the read-only
**Groundwater Sources** — plus **Initial Quality** and **User Flags**.

#### Junction

Adds **Max Depth**, **Initial Depth**, **Surcharge Depth**, **Ponded Area**.
Writes `[JUNCTIONS]`.

#### Virtual junction

A grade break inside an otherwise continuous pipe. `[VIRTUAL_JUNCTIONS]` carries
only a name and an invert; everything else is derived from the two attached
conduits (max depth is the pipe crown, surcharge depth and ponded area are
zero), so the adapter deliberately exposes **no** depth or ponding rows. The one
editable depth is **Max Depth, Display** — the optional `[VIRTUAL_JUNCTIONS]`
MaxDepth that supplies the ground line profile views draw. It feeds no
hydraulics.

Point lateral inflows *are* allowed at a virtual junction, so the Inflows / DWF /
RDII / Treatment rows are present exactly as on a junction. Only 2D surface
coupling stays prohibited. Deleting or re-fusing a virtual junction runs the
engine's node-delete cascade — its inflow, DWF and RDII rows are erased and any
subcatchment that drained to it loses its outlet — and the prompt names the
counts before you commit.

The engine rules a virtual junction must satisfy, surfaced verbatim when a write
is refused:

| Rule | Text |
|---|---|
| 609 | A virtual junction must connect exactly two conduits (no pumps; orifices; weirs or outlets) |
| 611 | The two conduits must have identical cross sections (shape; dimensions and barrels) |
| 613 | Both conduit offsets at the node must be zero (invert continuity) |
| 615 | The conduit inverts do not agree at the node |
| 617 | A virtual junction cannot be coupled to a 2D surface mesh (it has no opening) |
| 619 | Virtual junctions require dynamic-wave (`DYNWAVE`) flow routing |

#### Inlet junction

An inlet junction is a virtual junction that additionally owns one
`[INLET_USAGE]` row keyed on the node. It is created by **splitting a STREET
conduit** with the Inlet Junction tool, or by promoting an existing junction that
sits between two same-section STREET conduits (**Convert To ▸ Inlet Junction**).
It inherits the whole virtual-junction surface and adds:

| Property | What it does | Writes |
|---|---|---|
| **Inlet Design** | The `[INLETS]` design placed here | `[INLET_USAGE]` |
| **Capture Node** | The underdrain node that receives captured flow | `[INLET_USAGE]` |
| **Number of Inlets** | Inlets in the group | `[INLET_USAGE]` |
| **% Clogged** | Clogging fraction | `[INLET_USAGE]` |
| **Flow Restriction** | Maximum captured flow; 0 = unrestricted | `[INLET_USAGE]` |
| **Local Depression Height** / **Width** | Extra gutter depression at the inlet | `[INLET_USAGE]` |
| **Placement** | `AUTOMATIC` / `ON_GRADE` / `ON_SAG` | `[INLET_USAGE]` |
| **Approach Street** | Read-only — the street section of the host conduits | — |
| **Street Max Depth** | The optional `[INLET_JUNCTIONS]` MaxDepth — the flood threshold and the profile ground line; not read by the routing | `[INLET_JUNCTIONS]` |

Because an inlet junction is also a virtual junction, rules 609–619 apply, plus:

| Rule | Text |
|---|---|
| 623 | An inlet junction sits between two STREET conduits (RECT_OPEN or TRAPEZOIDAL for a drop inlet) |
| 625 | The inlet design named on this row does not exist |
| 627 | The capture node must be an existing node other than the inlet itself; and not a virtual or inlet junction |
| 629 | An inlet cannot be placed on both conduits of an inlet-junction pair |
| 633 | This inlet junction has no inlet design assigned |
| 635 | The inlet design is not compatible with the host's cross section |

Creation asks first rather than inserting a half-configured node: the **Inlet
Junction Setup** dialog collects the **Inlet Design** (with a "…" button that
opens the Inlet editor filtered to the gutter types), the **Capture Node** (the
list already excludes the host conduit's end nodes and every virtual or inlet
junction; its "…" button lets you click the node on the map instead — the
dialog is a floating panel, so the map stays live while it is open) and the
**Placement**. **OK** stays disabled until both identity fields are set,
because the engine rejects a partial usage row. The same map pick sits behind
the "…" of an existing inlet junction's **Capture Node** property row.
Link-drawing rejects
inlet junctions as endpoints exactly as it rejects virtual junctions.

\figtodo{14_inlet_junction_setup.png, The Inlet Junction Setup dialog}

\figtodo{14_inlet_junction_properties.png, Inlet junction property rows with the dashed capture-node connector on the map}

#### Outfall

| Property | What it does | Writes |
|---|---|---|
| **Outfall Type** | `FREE` / `NORMAL` / `FIXED` / `TIDAL` / `TIMESERIES` | `[OUTFALLS]` |
| **Stage Elev.** | Fixed stage — `FIXED` only | `[OUTFALLS]` |
| **Tidal Curve** | Tidal (hour → stage) curve picker — `TIDAL` only | `[OUTFALLS]` |
| **Stage Time Series** | Stage series picker — `TIMESERIES` only | `[OUTFALLS]` |
| **Flap Gate** | `NO` / `YES` | `[OUTFALLS]` |

The three type-specific rows follow the live **Outfall Type**: they enable and
disable as you change it, so an outfall never carries a stale stage source.
Outfalls have no depth or ponding rows.

#### Flow divider

| Property | What it does | Writes |
|---|---|---|
| **Divider Type** | `CUTOFF` / `OVERFLOW_` / `TABULAR` / `WEIR` — the trailing underscore is a C++ name-collision guard; it writes `OVERFLOW` | `[DIVIDERS]` |
| **Max Depth**; **Initial Depth**; **Surcharge Depth**; **Ponded Area** | As for a junction | `[DIVIDERS]` |

**Not exposed in this release:** the diverted link and the type-specific
parameters (cutoff flow, weir coefficients, the tabular diversion curve) do not
have Property-panel or attribute-table rows. They round-trip through the `.inp`
but must be edited there.

#### Storage unit

| Property | What it does | Writes |
|---|---|---|
| **Max Depth**; **Initial Depth**; **Surcharge Depth**; **Ponded Area** | As for a junction | `[STORAGE]` |
| **Seepage Rate** | Bottom seepage | `[STORAGE]` |
| **Storage Shape** | `Tabular` / `Functional` / `Cylindrical` / `Conical` / `Paraboloid` / `Pyramidal` | `[STORAGE]` |
| **Storage Curve** | Depth–area curve picker — `Tabular` only | `[STORAGE]` / `[CURVES]` |
| **Functional Coeff. (A)**; **Functional Exponent (B)**; **Functional Constant (C)** | `Area = A·Depth^B + C` — `Functional` only | `[STORAGE]` |
| **Shape Param 1 / 2 / 3** | The three raw dimensions of a geometric shape | `[STORAGE]` |

The three shape-parameter rows carry generic labels because their meaning
depends on the shape; the row's tooltip and the Section View supply the specific
one:

| Shape | Param 1 | Param 2 | Param 3 |
|---|---|---|---|
| `CYLINDRICAL` | Major Axis Length | Minor Axis Width | — |
| `CONICAL` | Major Axis Length | Minor Axis Width | Side Slope (run/rise) |
| `PARABOLIC` (paraboloid) | Top Major Axis | Top Minor Axis | Height at Top Axes |
| `PYRAMIDAL` | Base Length | Base Width | Side Slope (run/rise) |

`TABULAR` and `FUNCTIONAL` use no raw dimensions. For the elliptical shapes the
first two parameters are the **full** axes, not semi-axes.

\figtodo{14_storage_shapes.png, Storage shape rows for a conical unit}

### Node compound editors — Inflows; DWF; RDII; Treatment

The four compound rows open one modal dialog on the matching page. Every page is
a table of existing entries with **Remove Selected**, plus an add/update form
below; edits go straight to the engine and the cell summary refreshes.

**External Inflows** (`[INFLOWS]`)

| Control | What it does |
|---|---|
| **Constituent** | `FLOW` or a defined pollutant |
| **Type** | `FLOW`, `CONCEN` or `MASS` — `MASS` is pollutant-only and is disabled (with the selection stepped down) when the constituent is `FLOW` |
| **Time Series** | Inflow series picker; the "…" button opens the Time Series editor |
| **Baseline** | Constant baseline added to the series |
| **Multiplier (M)** | Baseline multiplier |
| **Scale Factor (S)** | Series scale factor |
| **Pattern** | Optional baseline pattern; the "…" button opens the Pattern editor |
| **Add / Update** | Commits the row; adding a second entry for a constituent that already has one prompts to replace |

Table columns: *Constituent*; *Type*; *Time Series*; *Baseline*; *M-Factor*;
*S-Factor*; *Pattern*.

**Dry Weather Flow** (`[DWF]`)

Form: **Constituent**, **Average Value**, and four pattern pickers —
**Monthly Pattern**, **Daily Pattern**, **Hourly Pattern**, **Weekend
Pattern** — each with a "…" button into the Pattern editor. Table columns:
*Constituent*; *Average*; *Monthly*; *Daily*; *Hourly*; *Weekend*.

**RDII** (`[RDII]`)

Form: **UH Group Name** (picker whose "…" button opens the Unit Hydrograph
editor — see \ref manual_hydrology) and **Sewer Area**. Table columns:
*UH Group*; *Sewer Area*.

**Pollutant Treatment** (`[TREATMENT]`)

A two-column table — *Pollutant* | *Expression* — edited inline, one removal
expression per pollutant. The expression cell hosts a syntax-highlighting,
completing editor with debounced engine validation; a banner above the table
shows the verdict (**● Valid expression**, or **⚠ Column N: …**). Clearing a cell
clears that pollutant's treatment.

Expression vocabulary, mirroring the engine's treatment grammar: variables
`C`, `R`, `DT`, `HRT`, `Q`, `V`, `D`, `AREA`; functions `exp`, `log`, `ln`,
`sqrt`, `min`, `max`, `abs`, `sgn`, `step`. The authoritative accept/reject
verdict always comes from the engine, so the highlighter can lag the grammar
without ever mis-validating.

\figtodo{14_node_inflows_page.png, The External Inflows page of the node compound editor}

\figtodo{14_treatment_page.png, The Pollutant Treatment page with a validated expression}

\videotodo{Adding a time-series inflow and a dry-weather-flow pattern to a junction}

### Link types and their properties

Every link carries **Name**, **Link Type** (read-only), **From Node**,
**To Node**, **Tag**, **Initial Quality**, **User Flags** and a read-only
post-run block (**Max Flow, Sim.**, **Max Velocity**, **Max/Full Depth**,
**Total Flow Volume**, **Time Surcharged**).

#### Conduit — `[CONDUITS]`, `[XSECTIONS]`, `[LOSSES]`

| Property | What it does | Writes |
|---|---|---|
| **Length** | Conduit length | `[CONDUITS]` |
| **Manning's n** | Roughness | `[CONDUITS]` |
| **Inlet Offset** / **Outlet Offset** (or *Elevation*) | End offsets in the current `LINK_OFFSETS` convention | `[CONDUITS]` |
| **Initial Flow**; **Maximum Flow** | Initial and limiting flow | `[CONDUITS]` |
| **Entry Loss Coeff.**; **Exit Loss Coeff.**; **Avg. Loss Coeff.** | Minor losses | `[LOSSES]` |
| **Flap Gate** | `NO` / `YES` | `[LOSSES]` |
| **Seepage Rate** | Conduit seepage | `[LOSSES]` |
| **Cross Section** | Summary + **Edit…** → the cross-section editor | `[XSECTIONS]` |
| **Geom 1** … **Geom 4** | The four raw geometry slots, inline | `[XSECTIONS]` |
| **Barrels** | Number of barrels | `[XSECTIONS]` |
| **Culvert Code** | FHWA HDS-5 inlet-geometry code, chosen inline from a grouped combo | `[XSECTIONS]` |
| **Inlets** | Summary + **Edit…** → the inlet-usage page | `[INLET_USAGE]` |

The culvert combo carries all 57 HDS-5 codes plus a "(none)" entry for code 0,
labelled `N) <shape/material group>: <inlet edge description>` (for example
`3) Circular Concrete: Groove end projecting`). Code 0 means no culvert / no
inlet control.

#### Pump — `[PUMPS]`

**Pump Curve** (curve picker), **Initial Status** (`OFF` / `ON`), **Startup
Depth**, **Shutoff Depth**. Post-run adds **Pump Cycles**, **Pump On Time** and
**Volume Pumped**.

The pump's behaviour follows the curve *type* you pick, which is set in the Curve
editor (\ref manual_data_objects):

| Curve type | Relation |
|---|---|
| Pump 1 | Volume → Flow |
| Pump 2 | Depth → Flow (on/off) |
| Pump 3 | Head → Flow (continuous) |
| Pump 4 | Depth → Flow (continuous) |
| Pump 5 | Depth → Flow (variable speed) |

#### Orifice — `[ORIFICES]`

**Type** (`SIDE` / `BOTTOM`), **Offset** (or **Elevation**), **Discharge
Coeff.**, **Flap Gate**, **Open/Close Rate (1/s)**, plus a **Cross Section**
row and the four **Geom** rows (an orifice's opening is a cross-section like a
conduit's).

#### Weir — `[WEIRS]`

**Type** (`TRANSVERSE` / `SIDEFLOW` / `VNOTCH` / `TRAPEZOIDAL` / `ROADWAY`),
**Inlet Offset** and **Outlet Offset**, **Crest Height**, **Discharge Coeff.**,
**End Contractions**, **Flap Gate**, plus **Cross Section** and the four **Geom**
rows. The weir shape is derived from the type — there is no separate shape
control.

#### Outlet — `[OUTLETS]`

**Rating Curve** type (`FUNCTIONAL_HEAD`, `FUNCTIONAL_DEPTH`, `TABULAR_HEAD`,
`TABULAR_DEPTH`), **Offset** (or **Elevation**), **Coefficient** and **Exponent**
for the functional forms, a **Rating Curve** picker for the tabular forms, and
**Flap Gate**. The exponent and curve rows show and hide with the chosen type.

\figtodo{14_link_properties_conduit.png, Conduit property rows including cross-section and losses}

### Cross-section editor and the Section View

The conduit's **Cross Section** row opens a three-pane editor: a shape gallery
(one icon per allowed shape), a parameters form whose four rows re-label
themselves per shape, and a live engine-accurate section drawing with labelled
dimensions. The drawing tracks the widget values as you type, not the last
committed value, and highlights the dimension line of the spin box you are
editing. Changes commit as you make them.

| Shape | Geom 1 | Geom 2 | Geom 3 | Geom 4 |
|---|---|---|---|---|
| `CIRCULAR` | Diameter | | | |
| `FILLED_CIRCULAR` | Diameter | Filled Depth | | |
| `RECT_CLOSED` | Max Depth | Width | | |
| `RECT_OPEN` | Max Depth | Width | | |
| `TRAPEZOIDAL` | Max Depth | Bottom Width | Left Slope | Right Slope |
| `TRIANGULAR` | Max Depth | Top Width | | |
| `PARABOLIC` | Max Depth | Top Width | | |
| `POWER` | Max Depth | Top Width | Exponent | |
| `RECT_TRIANGULAR` | Max Depth | Top Width | Triangle Height | |
| `RECT_ROUND` | Max Depth | Top Width | Bottom Radius | |
| `MOD_BASKETHANDLE` | Max Depth | Bottom Width | Top Radius | |
| `HORIZ_ELLIPSE` | Max Height | Max Width | | |
| `VERT_ELLIPSE` | Max Height | Max Width | | |
| `ARCH` | Max Height | Max Width | | |
| `EGGSHAPED` | Max Depth | | | |
| `HORSESHOE` | Max Depth | | | |
| `GOTHIC` | Max Depth | | | |
| `CATENARY` | Max Depth | | | |
| `SEMIELLIPTICAL` | Max Depth | | | |
| `BASKETHANDLE` | Max Depth | | | |
| `SEMICIRCULAR` | Max Depth | | | |
| `FORCE_MAIN` | Diameter | Roughness (C or e) | | |
| `IRREGULAR` | *Transect picker* | | | |
| `CUSTOM` | Max Depth | *Shape-curve picker* | | |
| `STREET` | *Street picker* | | | |
| `DUMMY` | — | — | — | — |

For `IRREGULAR` and `STREET` the first geometry slot is an *index* into the
transect or street list, so the raw spin box is replaced by a name picker whose
"…" button opens the Transect or Street editor; the picked name is translated
back to an index on commit. `CUSTOM` keeps Geom 1 as a real dimension and
replaces only Geom 2 with the shape-curve picker. `FORCE_MAIN`'s second slot is
the friction-law coefficient (Hazen-Williams C or Darcy-Weisbach roughness
height), not a dimension. `DUMMY` has no geometry at all — it lets a link be
routed without conveyance.

The **Section View** dock (**View → Panels → Section View**, `Ctrl+Alt+9`) shows
the same drawing for whatever link is currently selected, without opening a
dialog.

\figtodo{14_xsection_editor.png, The cross-section editor with the shape gallery and the live section preview}

### Transect editor

**Model → Data Objects → Transects…** opens a non-modal three-pane editor over
`[TRANSECTS]`:

- **Left** — the transect list with **New** and **Delete**.
- **Middle** — **Name**, a rich-text **Comments** field, property groups for
  *Roughness* (left / right / channel Manning's n), *Bank Stations*,
  *Encroachment* and the station/elevation *Modifiers*, and a station table with
  **Add** and **Delete** row buttons.
- **Right** — the cross-section chart: ground fill, the overbank and channel
  lines, bank markers and draggable point handles, with a toolbar for
  zoom-to-extent, zoom in/out, pan, edit-points, copy and chart properties.
  Right-clicking the chart offers **Chart properties…**, which edits the view's
  colours, handle size and handle visibility.

Dragging a handle on the chart and typing in the station table are the same
edit — every mutation routes through the transect provider, so the list, the
property tree, the table and the chart stay in lock-step.

Assign a transect to a conduit by setting the conduit's cross-section shape to
`IRREGULAR` and picking the transect by name.

\figtodo{14_transect_editor.png, The Transect editor with the station table and the section chart}

### Street editor

**Model → Data Objects → Streets…** opens a three-pane editor over `[STREETS]`.
A street is parametric, so the middle pane is a field form rather than a station
table, and the right pane is a schematic section preview (drawn with vertical
exaggeration) of the crown, curb, depressed gutter and backing.

| Field | What it does |
|---|---|
| **Name** | Street id |
| **Road Width** | Crown-to-curb road width |
| **Curb Height** | Curb height |
| **Cross Slope (%)** | Road cross slope |
| **Road Roughness (n)** | Manning's n for the road surface |
| **Gutter Depression** | Depression depth at the curb |
| **Gutter Width** | Depressed-gutter width |
| **Sides** | **One Sided** or **Two Sided** |
| **Backing Width**; **Backing Slope (%)**; **Backing Roughness (n)** | The strip behind the curb |

Assign a street to a conduit by setting the conduit's cross-section shape to
`STREET` and picking the street by name.

\figtodo{14_street_editor.png, The Street editor with the schematic section preview}

### Inlet editor

**Model → Data Objects → Inlets…** opens a three-pane editor over `[INLETS]`:
the design list with **Add** / **Delete**, a middle pane with **Name**, a
comments field, an **Inlet Type** combo and a property tree whose rows are
filtered by the chosen type, and a drawing view showing the design in plan and
section with dimension callouts (toolbar: fit, zoom, copy, export image).

Inlet types (HEC-22): `GRATE`, `CURB OPENING`, `COMBINATION`, `SLOTTED DRAIN`,
`DROP GRATE`, `DROP CURB`, `CUSTOM`.

| Group | Rows | Applies to |
|---|---|---|
| Grate | **Grate — Type**, **Grate — Length**, **Grate — Width**, **Grate — Open Area Fraction**, **Grate — Splash-over Velocity** | `GRATE`, `DROP GRATE`, `COMBINATION` |
| Curb opening | **Curb Opening — Length**, **Curb Opening — Height**, **Curb Opening — Throat Angle** | `CURB OPENING`, `DROP CURB`, `COMBINATION` |
| Slotted drain | **Slotted Drain — Length**, **Slotted Drain — Width** | `SLOTTED DRAIN` |
| Custom | **Custom — Curve Type**, **Custom — Curve** | `CUSTOM` |

Grate bar patterns: `P_BAR-50`, `P_BAR-50x100`, `P_BAR-30`, `CURVED_VANE`,
`TILT_BAR-45`, `TILT_BAR-30`, `RETICULINE`, `GENERIC`. The open-area fraction
and splash-over velocity rows are only meaningful for `GENERIC` and are hidden
otherwise. Throat angles are `HORIZONTAL`, `INCLINED`, `VERTICAL`. A custom
inlet's curve is either a **Diversion** curve (captured flow versus approach
flow) or a **Rating** curve (captured flow versus water depth); the **…** button
next to the curve row picks it.

Deleting a design warns which conduits and inlet junctions reference it. Edits
are undoable — they push onto the project's undo stack like map edits.

When the editor is opened from an inlet-usage picker it filters the list to the
designs legal on the host cross-section: gutter types for `STREET`, drop types
for `RECT_OPEN` and `TRAPEZOIDAL`, `CUSTOM` everywhere.

\figtodo{14_inlet_editor.png, The Inlet editor with the plan-and-section drawing}

### Inlet usage on a conduit

A conduit's **Inlets** row opens the inlet-usage page — the same `[INLET_USAGE]`
row an inlet junction owns, but hosted on the link. The eight fields are
**Inlet Design**, **Capture Node**, **Number of Inlets**, **% Clogged**,
**Flow Restriction** (0 shows as *(none)* and means unrestricted), **Local
Depression Height**, **Local Depression Width** and **Placement**
(*Automatic* / *On Grade* / *On Sag*), with a **Remove Inlet** button.

Nothing is committed while either the design or the capture node is unset,
because the engine rejects a partial row. The capture-node list excludes the
conduit's own end nodes and every virtual or inlet junction (rule 627). The whole
row commits as one undoable command, so this edit and the same edit made on an
inlet junction's property rows are literally the same operation.

\figtodo{14_inlet_usage_page.png, The Inlets page of the link compound editor}

\videotodo{Splitting a street conduit into an inlet junction and wiring its capture node}

### Control rules editor

**Model → Data Objects → Control Rules…** opens a non-modal two-pane editor over
`[CONTROLS]`.

- **Left** — a searchable rule list, each row badged with its validation state
  (valid / invalid / pending), with **Add**, **Delete** and **Rename…**.
- **Right** — the rule name, a validation banner (**● Valid**, or
  **⚠ Line N: …**), and a code editor holding the whole `RULE` block.

The editor highlights the SWMM rule grammar and completes it:

| Token class | Vocabulary |
|---|---|
| Block keywords | `RULE`, `IF`, `THEN`, `ELSE`, `AND`, `OR`, `NOT`, `PRIORITY` |
| Object types | `NODE`, `LINK`, `CONDUIT`, `PUMP`, `ORIFICE`, `WEIR`, `OUTLET`, `SUBCATCH`, `SIMULATION`, `GAGE` |
| Variables | `DEPTH`, `HEAD`, `FLOW`, `VOLUME`, `INFLOW`, `SETTING`, `STATUS`, `CLOCKTIME`, `DATE`, `MONTH`, `DAY`, `TIME`, `OVERFLOW` |
| Operators | `=`, `<`, `>`, `<=`, `>=`, `<>`, `IS` |
| Numbers | Decimals and clock times `HH:MM[:SS]` |
| Comments | `;` to end of line |

Completion draws on that vocabulary **plus the live node, link and subcatchment
names from the model**, so premises and actions can be completed against real
object ids. The popup triggers after two characters or on **Ctrl+Space**.

Validation runs against the engine's own rule parser, debounced 250 ms after you
stop typing, resolving `NODE` / `LINK` / `CURVE` / `TIMESERIES` references
against the live name tables without mutating engine state. There is also an
explicit validate-now button. Edits flush on focus-out and on changing the
selected rule.

The rule body is free text, so anything the engine's parser accepts can be
written here — including the engine's curve-, time-series- and PID-modulated
actions. Note that `PID` is *not* in the highlighter/completer vocabulary above,
so a PID action will not be coloured or completed; the engine validator is still
the authority on whether it is accepted.

\figtodo{14_rules_editor.png, The control rules editor with syntax highlighting and a valid badge}

\figtodo{14_rules_completion.png, Rule completion offering live node names}

## Tips and gotchas

- **Virtual and inlet junctions need `DYNWAVE`.** Rule 619 refuses them under
  steady-flow or kinematic-wave routing; set the routing model in
  \ref manual_simulation_options first.
- **Changing a conduit away from `STREET`** while it hosts an inlet or touches an
  inlet junction is refused with rule 623 — remove the inlet usage first.
- **Flipping the offset convention is not a no-op on an existing model.** Answer
  the conversion prompt deliberately; the option flips either way.
- **`Geom 1` on `IRREGULAR` and `STREET` is an index, not a dimension.** Always
  set it through the name picker; typing a raw number is not offered because the
  indices are not stable across rename or reorder.
- **Divider parameters are `.inp`-only in this release.** The type is editable;
  the diverted link and its coefficients are not.
- **`MASS` inflows are pollutant-only.** The Type combo disables it (and steps
  the selection down) when the constituent is `FLOW`.
- **Inlet usage commits as a whole row.** A partial row is never written, so the
  page stays quiet until both the design and the capture node are chosen.
- **Deleting a data object cascades.** Deleting an inlet design, a transect or a
  street reports which links reference it before it commits.

## Related

- \ref manual_hydrology — subcatchments; rain gages; RDII unit hydrographs
- \ref manual_water_quality — pollutants and the treatment grammar in context
- \ref manual_data_objects — time series; curves; patterns used by the pickers
- \ref manual_map_editing — drawing; splitting and converting objects on the map
- \ref manual_object_browser — the Properties panel; compound editors and the Section View
- \ref manual_attribute_tables — editing the same rows in bulk
- \ref manual_simulation_options — flow routing; `LINK_OFFSETS` and the other model-wide options
- \ref manual_2d_mesh — coupling inlets and nodes to a 2D surface
- \ref manual_profile_plots — how virtual and inlet junctions are drawn in profiles
- \ref tutorial_street_inlets — a worked street-and-inlet model
- \ref tutorial_1d2d_coupling — capped street inlets coupled to a 2D surface
