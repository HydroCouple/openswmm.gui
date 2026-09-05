@page manual_troubleshooting A5 — Troubleshooting

## What you'll do

Diagnose the problems people actually hit: a project that will not open, layers
that do not line up, a basemap that stays blank, a run that fails, a mesh that
will not generate, results that never appear, and a window layout you want back.

## Where to find it

The **Message Logs** dock (**Ctrl+Alt+7**) is the first place to look for
anything. The **Simulation Status** panel (**Ctrl+Alt+6**) carries per-run
detail, and the `.rpt` status report (**Analysis → Report**) carries every
engine warning and error in full.

## Step-by-step

### Reading the logs

The **Message Logs** dock is a three-column tree — **Time**, **Type**,
**Message** — with `Information`, `Warning` and `Error` rows, auto-scrolled to
the newest.

**There is no on-disk log by default, and no Save Log action.** To get the
contents out, right-click the view and use **Copy** (selected rows) or
**Copy All**, which prepends a `Time / Type / Message` header. Both produce
tab-separated text that pastes straight into a spreadsheet or a bug report.

For a persistent log — and on Windows, where the application has no console at
all — set `SWMM_LOG_FILE` before launching:

```sh
SWMM_LOG_FILE=1 ./SWMMVis            # <app data>/logs/swmmvis-<timestamp>.log
SWMM_LOG_FILE=/path/to/swmmvis.log ./SWMMVis
```

It appends, is safe across worker threads, flushes every line, and writes each
one as `hh:mm:ss.zzz <category>: <message>`. Add
`QT_LOGGING_RULES="openswmm.load.*=true"` to include the load-path detail (see
\ref manual_performance for the category list).

\figtodo{a05_message_logs.png, The Message Logs dock with an error row and its context menu open}

### A project will not open

**Check the report and the log first.** Parse failures produce numbered engine
errors:

| Code | Message |
|---|---|
| 200 | one or more errors in input file. |
| 201 | too many characters in input line. |
| 203 | too few items. |
| 205 | invalid keyword `<x>`. |
| 207 | duplicate ID name `<x>`. |
| 209 | undefined object `<x>`. |
| 211 | invalid number `<x>`. |
| 213 | invalid date/time `<x>`. |
| 233 | invalid math expression. |
| 235 | invalid infiltration parameters. |

File-level failures:

| Code | Message |
|---|---|
| 301 | files share same names. |
| 303 | cannot open input file. |
| 305 | cannot open report file. |
| 307 | cannot open binary results file. |
| 308 | amount of output produced will exceed maximum file size. |
| 309 | error writing to binary results file. |
| 311 | error reading from binary results file. |
| 317 | cannot open rainfall data file `<x>`. |
| 331 | cannot open hot start interface file `<x>`. |
| 337 | cannot open climate file `<x>`. |
| 361 | could not open external file used for Time Series `<x>`. |

Errors 317, 331, 337 and 361 almost always mean a **relative path that no
longer resolves** — see *Relative paths and portability* below.

Common validation failures once the file parses:

| Code | Message |
|---|---|
| 101 | memory allocation error. |
| 107 | cannot compute a valid time step. |
| 111 | invalid length for Conduit `<x>`. |
| 112 | elevation drop exceeds length for Conduit `<x>`. |
| 117 | no cross section defined for Link `<x>`. |
| 131 | the following links form cyclic loops in the drainage system: |
| 138 | Node `<x>` has initial depth greater than maximum depth. |
| 140 | Storage node `<x>` has negative volume at full depth. |
| 141 | Outfall `<x>` has more than 1 inlet link or an outlet link. |
| 145 | Drainage system has no acceptable outlet nodes. |
| 191 | simulation start date comes after ending date. |
| 195 | reporting time step or duration is less than routing time step. |

**If the `.inp` opens but the styling is gone**, the `.oswp` sidecar is missing
or was not found. The `.oswp` must sit beside the `.inp` with the same basename.
Nothing is lost from the model itself — the `.inp` is complete on its own.

**If map labels, a backdrop image or profile definitions vanished after a
save**, that is expected: `[LABELS]`, `[BACKDROP]` and `[PROFILE]` are parsed
and discarded, and nothing writes them back. See \ref manual_file_formats.

### Layers do not line up

Almost always a CRS problem.

1. Check the project CRS on the status bar and in **Simulation Options →
   Spatial & CRS**.
2. Check each layer's CRS in its properties.
3. If the project CRS is wrong, **Change Coordinate Reference System** offers
   two options under *How should the change be applied?* — and they are very
   different:
   - **Reproject stored coordinates** — permanently transforms every node, link
     vertex and subcatchment. *Use when the new CRS is the correct one for the
     data.*
   - **Re-render only (display in new CRS)** — keeps stored coordinates and
     reprojects for display. *Use when the source CRS is correct.*

   Picking the first by mistake and then saving is destructive. If in doubt,
   pick the second.

**If nothing reprojects at all**, PROJ or GDAL cannot find its data files.
SWMMVis locates bundled `proj/` and `gdal/` directories beside the executable
(and inside `Contents/Resources/` on macOS) and sets `PROJ_DATA` and
`GDAL_DATA` itself; if you are running an unpackaged build, set them by hand.

**A mesh with no `;; UNITS:` header** in a US-units project is silently scaled
by 0.3048. If a mesh lands a third of the size it should be, that is why. Add
`;; UNITS: SI (m)` or `;; UNITS: metre` to the `.2dm` (or above the inline 2D
sections) and reload.

### A basemap will not load

1. **Test the connection.** The **Add Basemap** dialog's XYZ tab has a
   **Test Connection** button. Use it before adding.
2. **Check the URL template.** `{s}` `{z}` `{x}` `{y}` placeholders must match
   the provider. **Axis order:** must be **ZXY (standard OSM)** for most tile
   servers and **ZYX (ArcGIS REST)** for ArcGIS ones — a wrong axis order gives
   you scrambled or blank tiles rather than an error.
3. **Zoom range.** A tile outside the provider's min/max zoom simply does not
   exist. Providers ship with sensible ranges; a custom entry may not.
4. **Authentication.** The XYZ tab has a **Basic** authentication group with
   **Username** and **Password**. Some providers additionally require an API
   key in the URL template.
5. **Proxy.** SWMMVis uses the system proxy configuration; if your browser
   needs a proxy, so does the tile fetch.
6. **WMS/WMTS quirks.** The **Advanced** group has *Ignore GetMap URI*,
   *Ignore axis orientation*, *Invert axis orientation* and a **DPI mode**
   selector (All, None, QGIS, UMN MapServer, GeoServer) for servers that need
   coaxing.

Blank-but-no-error is the normal failure mode for tiles. Check the Message Logs
and, if you need more, the network activity of the request.

\figtodo{a05_basemap_test_connection.png, The Add Basemap dialog's Test Connection result}

### A simulation fails

**Read the report.** A failed run writes a `.rpt` even when it writes no
`.out`, and that is where the reason is. The GUI's completion messages are:

- *Simulation finished. Results: `<path>`* — success.
- *Simulation cancelled. Partial results: `<path>`* — the partial `.out` is
  still loaded.
- *Simulation failed (code N): `<message>` — `<detail>`*.
- *Simulation reported success but produced no output: `<path>` / The run
  likely failed during input parsing or setup — see the report file for the
  cause: `<rpt path>`* — this one almost always means a parse or setup error.

**Pre-run refusals** you may hit:

- *Open a SWMM project first to run a simulation.*
- *Save the project before running — Run uses the .inp on disk.* (Or the run
  auto-saves and logs *Auto-saved before running.*)
- *Run cancelled — output overwrite declined.*
- **2D mesh not found** — a modal warning:
  > *2D Surface Routing is enabled for this model, but no 2D mesh was found —
  > there is no inline mesh in the .inp and no valid [2D_MESH_FILE] reference.
  > The simulation will run as 1D-only. Generate a mesh (or set one active in
  > Simulation Options → Mesh) to run the 2D solver. Continue with a 1D-only
  > run?*

**Engine warnings** worth understanding when a run finishes but looks wrong:

| Code | Meaning |
|---|---|
| 2 | maximum depth increased for a node |
| 5 | minimum slope used for a conduit |
| 8 | elevation drop exceeds length for a conduit |
| 101 | duplicate x values in a curve |
| 102 | boundary regions overlap |
| 103 | a `[FILES]` entry is not supported by this engine and was ignored |
| 104 | a retired `[2D_OPTIONS]` key was ignored — see below |
| 105 | an option is accepted but has no effect yet under `FLOW_ROUTING FV` |
| 106 | a dynamic-wave option does not apply under `FLOW_ROUTING FV` |
| 107 | unreadable rows were skipped in a rainfall CSV |
| 108 | the Preissmann-slot top-width cap overrode `FV_SLOT_CELERITY` |

### Retired 2D options

Loading a pre-2026 2D model produces one warning per retired key:

> `WARNING 104: [2D_OPTIONS] <key> was retired with the CVODE/ARKODE 2D
> solvers and was ignored; the explicit local-inertial marcher is the only 2D
> integrator.`

The retired keys are `MIN_TIMESTEP`, `REL_TOLERANCE`, `ABS_TOLERANCE`,
`MAX_CVODE_STEPS`, `MAX_KRYLOV_DIM`, `LINEAR_SOLVER`, `PRECONDITIONER`,
`JACOBIAN`, `ATOL_AREA_REF`, `COUPLING_INTERVAL`, `COUPLING_WINDOW`,
`ACTIVE_SET`, `ACTIVE_SET_HALO` and `MOMENTUM`, plus any `INTEGRATOR` value
other than `EXPLICIT`. The replacements are `THETA`, `CFL_NUMBER`,
`LTS_TIERS`, `H_MOVE`, `FROUDE_MAX`, `ADVECTION`, `MAX_TIMESTEP` and
`COUPLING_AREA`.

**They are harmless** — warn and ignore on file load. To silence them, just
save the model from SWMMVis: the Simulation Options dialog writes through the
API, which rejects retired keys outright, so they are dropped.

An unknown (as opposed to retired) key is a hard parse error:
*Unknown 2D_OPTIONS parameter: `<key>`*.

Three `[OPTIONS]` keys are retired the same way, with a
*"… is retired and will be treated as …"* warning: `FV_NODE_COUPLING EXPLICIT`,
`FV_NODE_DT NONE` and `FV_NODE_PICARD`. `VIRTUAL_JUNCTION_MOMENTUM FULL` is
retired and is not re-emitted on save.

### The 2D pages are missing

If **Simulation Options** has no **2D Surface Routing** category at all, the
engine you are running was built without the 2D module. The **2D Surface
Routing** checkbox on the **Models / Processes** page stays enabled either way,
but its tooltip changes to say:

> *Project-level 2D module flag. Mesh selection tab becomes interactive when
> checked. The engine 2D solver itself is not compiled in this binary — rebuild
> with `-DOPENSWMM_BUILD_2D=ON` for end-to-end coupled runs; mesh generation
> works regardless.*

The **Mesh** category is always present, because mesh management is a pure GUI
concern. And the GUI links HDF5 unconditionally for its own results reader, so
**reading a `.2d.h5` and generating a mesh both work even when the engine has
no 2D solver**.

### Mesh generation fails

The **Generate 2D Mesh** dialog reports the reason directly.

**By far the most common: the CRS.**

> *The project CRS (…) has no usable planar linear unit. 2D mesh generation
> requires a projected or local CRS in metres or feet. Geographic (lat/lon)
> CRSes are not supported.*
>
> *Fix: open Project → Change CRS… and pick a projected CRS, or use the 'Local
> projected' option when the source units are unknown.*

Or, more bluntly, *No CRS is set for the model.*

**Geometry failures:**

| Message | Cause |
|---|---|
| *MeshGenerator: domain is empty.* | no boundary polygon and no model extent |
| *…no usable boundary polygons (every supplied polygon had < 3 vertices after vertex deduplication).* | degenerate boundary layer |
| *…all domain boundary segments were degenerate (zero-length after vertex deduplication).* | coincident boundary vertices |
| *Triangle fatal error — check PSLG for degenerate geometry (duplicate/coincident vertices, crossing or zero-length constraint segments, boundary not forming a closed ring).* | the usual self-intersection case |
| *Triangle produced 0 triangles — domain may be self-intersecting or constraint segments may cross.* | same, detected later |

Non-finite coordinates are rejected explicitly — a NaN coordinate is invisible
to duplicate and degeneracy screening, so it is caught up front.

**DTM failures:** *DTM open failed*, *DTM pixel size is invalid*,
*Terrain thinning failed*, *DTM sampling failed*. With no DTM chosen the dialog
logs *Using junction rim elevations (no DTM selected)…* and carries on.

**Minimum cell size conditioning** can abandon rather than fail:

- *conditioning ABANDONED (…) — PSLG restored, generating unconditioned*
- *welding collapsed a domain ring at weld radius … (it encloses no area); the
  minimum cell size is too coarse for this domain*
- *conditioning made the worst feature scale WORSE (… -> …) — it would create
  smaller cells than the input demanded*

The remedy for all three is either a larger minimum cell size or simpler input
geometry. The generation log also reports a census —
*crossings N, duplicate segs N, zero-length N, collinear overlaps N,
degenerate rings N* — which tells you which of those to go and fix.

**Protected slivers:** *N sub-scale cell(s) are bounded by constrained or
coupled geometry and cannot be collapsed — these need a larger minimum cell
size or simpler input geometry.*

**Holes.** The **Drop holes smaller than one cell** checkbox warns that
*Hole rings narrower than the minimum cell size cannot be meshed around. When
checked they are removed, which means THE MESH COVERS THEM — a modelling
change, reported in the generation log.* Read the log after checking it.

A quality hint you will see often: *Min angle is N° — consider 26–28° with a
minimum cell size…*

\figtodo{a05_mesh_generation_error.png, The Generate 2D Mesh dialog reporting a CRS failure}

### Inlet junction rule violations

Inlet junctions are constrained, and the GUI translates the engine's codes into
plain language:

| Code | Message |
|---|---|
| 623 | An inlet junction sits between two STREET conduits (RECT_OPEN or TRAPEZOIDAL for a drop inlet). |
| 625 | The inlet design named on this row does not exist. |
| 627 | The capture node must be an existing node other than the inlet itself, and not a virtual or inlet junction. |
| 629 | An inlet cannot be placed on both conduits of an inlet-junction pair. |
| 631 | Too many items on the `[INLET_USAGE]` line. |
| 633 | This inlet junction has no inlet design assigned. |
| 635 | The inlet design is not compatible with the host's cross section (gutter inlets need STREET; drop inlets need RECT_OPEN or TRAPEZOIDAL). |

Virtual junctions use the same mechanism with codes 609–621. There is also a
2D-specific error, **617** — *Virtual Junction `<x>` cannot be coupled to a 2D
surface mesh.*

Other messages you will meet while editing: *"…" is not an inlet junction.*,
*An inlet junction needs both an inlet …*, *Click a street conduit to insert an
inlet junction at that point.* and *Could not insert an inlet junction on
"…".* Deleting one of the paired conduits raises a guard dialog
(**Delete Inlet Junction** or **Conduit Belongs to an Inlet Junction**).

### Results do not appear

1. **Did the engine write anything?** Check for a `.out` beside the `.inp`. If
   the run reported success but produced no output, the failure was during
   parsing or setup — read the `.rpt`.
2. **Is the layer active?** Right-click the results layer in **Layers** and
   check **Set as Active Results Layer**. Only the active layer drives the
   Analysis toolbar and the animation.
3. **A cancelled run still loads.** The engine flushes partial output, so a
   cancelled run gives you a usable partial `.out`. Only an engine error with no
   output at all skips the load.
4. **No `.2d.h5`?** Check `[2D_OPTIONS] OUTPUT_FILE` — an empty value means no
   2D output is written — and `REPORT_2D`. The 2D writer needs no `[PLUGINS]`
   entry.
5. **Nothing coloured on the map?** A results layer styled by an attribute the
   run does not carry skips silently with a one-shot warning. That happens most
   often with a species (`qual:<name>`) after pollutants changed.
6. **A legacy quality-free `.out`** shows no species entries at all in the
   variable picker. That is not a bug.

### Slow or stale rendering

\ref manual_performance covers this in full. The short list:

- Raise **Preferences → Rendering → Label Rendering → Label zoom-out
  threshold (m11)** before animating a large network.
- Keep both **GPU Rendering** checkboxes on.
- Launch with `SWMMVIS_LOG_REDRAW=1` and see which channel a gesture dirties.
  A selection change should dirty **Overlay**, never **Raster**.
- `OPENSWMM_2D_RENDER_DEBUG` prints the per-paint frame re-grab decision.
- If the terrain mesh renders wrongly, `OPENSWMM_QSG_MESH=0` forces the painter
  fallback — useful as a diagnosis, not as a fix.

One known stale case is on record and not resolved: adding a node with the Add
Node tool has been reported not to repaint immediately in some situations. If
you see it, a pan or a layer visibility toggle forces the redraw.

### Relative paths and portability

Everything SWMMVis writes uses **relative** paths where it can — the `.oswp`'s
layer sources, `[2D_MESH_FILE]`, `[PROCESS_COMPONENTS] config=`, `[FILES]`
entries and `[RAINGAGES] FILE` sources are all resolved against the file that
names them. That makes a project folder portable, provided you move the **whole
folder**.

What breaks:

- Moving the `.inp` without its sidecar configs (`.rxn`, `.ard`, `.age`,
  `.heat`) — the components fail to load.
- Moving the `.inp` without its `.2dm` — the mesh is missing and a 2D run
  degrades to 1D with the warning above.
- Moving the `.inp` without its rain or climate file — errors 317 and 337.
- A rain file source line whose station id no longer matches column 1 of the
  file — error 203 on the legacy grammar, which requires a station.
- Layers added from outside the project folder are stored with a relative path
  that walks out of it; move the project and they break.
- An absolute path is only written when a relative one is impossible (a
  different drive on Windows).

**Examples are always copied, never opened in place.** Choosing one from the
Welcome page asks *Choose a folder to copy "…" into* first, so that simulation
results never land in the bundled baseline. If a copy already exists you get
**Example copy already exists** with **Open Existing**, **Replace** and
**Cancel**.

### Resetting preferences and window layout

Two separate resets:

| What | Where | Effect |
|---|---|---|
| Preferences | **Reset to defaults** at the bottom-left of the Preferences dialog | restores compiled-in defaults into the widgets; still needs Apply or OK |
| Window positions | **Window → Reset Window Positions** | discards saved dialog geometry and gathers open windows back onto the main window's screen |

**Reset Window Positions** deliberately removes only the `geometry` key under
each saved dialog group — splitter, header, tab, page and toggle state are kept
— and clears the main window's saved geometry while leaving the dock and
toolbar **layout** alone.

The legend overlay has its own **Reset layout** on its context menu.

For a full reset, delete the application's settings store:

| Platform | Location |
|---|---|
| macOS | `~/Library/Preferences/<reversed-domain>.OpenSWMM Stormwater Management Model.plist` |
| Windows | `HKEY_CURRENT_USER\Software\hydrocouple\OpenSWMM Stormwater Management Model` |
| Linux | `~/.config/hydrocouple/OpenSWMM Stormwater Management Model.conf` |

The key groups you will find there are `SWMMVis/Preferences/…`,
`Dialogs/<dialog>/…`, `SWMMVis::MainWindow/…`, `SWMMVis::Shortcuts/…` and
`Window/DialogStacking`. Deleting a single group is a targeted reset — for
example, dropping `SWMMVis::Shortcuts` restores every default shortcut.

On macOS, if a dialog stacks behind the main window (or refuses to), override
the stacking mode with `OPENSWMM_DIALOG_STACKING=qt` or `=native`.

### Reporting a bug

Issues go to **https://github.com/HydroCouple/openswmm.engine/issues**, which
is also the **Report an Issue** link on the Welcome page.

Include:

1. **Environment.** **Help → About** has a **Copy environment** button that
   puts the app version, build date, Qt version, OS and architecture on the
   clipboard as a diff-friendly block.
2. **The log.** Right-click the Message Logs dock → **Copy All**, or re-run with
   `SWMM_LOG_FILE=1` and attach the file.
3. **The report.** For an engine problem, the `.rpt` is usually decisive.
4. **A minimal model.** The `.inp` plus any sidecars it references, zipped.

\figtodo{a05_about_copy_environment.png, The About dialog's Copy environment button}

## Tips and gotchas

- The Message Logs dock is not a log file. Copy it out before you close the
  application, or set `SWMM_LOG_FILE` before you start.
- A continuity error that grows steadily from early in a run is a time-step
  problem (`MINIMUM_STEP`, `MAX_TRIALS`, `HEAD_TOLERANCE`), not an end-of-run
  surprise. The Simulation Status panel polls it live, so watch it.
- A `NORMAL_FLOW` 2D boundary with slope 0 behaves as a wall. If water will not
  leave a boundary you configured, check the slope first.
- Saving an external `.2dm` twice produces different bytes each time, because
  the vertex-node map is written in hash order. That is expected version-control
  noise, not corruption.
- Several entries on the Layers panel context menu are visible but disabled
  placeholders. They are not broken; they are not implemented.
- If a shortcut stopped working, check the **Keyboard** page of Preferences for
  a bold override.

## Related

- \ref manual_running — running a simulation, the status panel and the report
- \ref manual_projects — opening, saving, recents and portability
- \ref manual_crs — coordinate reference systems and reprojection
- \ref manual_layers — adding and configuring data sources
- \ref manual_2d_mesh — mesh generation options in detail
- \ref manual_file_formats — what round-trips and what does not
- \ref manual_performance — logging categories and environment variables
- \ref manual_preferences — every preference and where it is stored
- \ref manual_about — version and environment information for a bug report
