@page manual_plugins A3 — Plugins

## What you'll do

Find out which file formats and compute backends the running build can offer,
and how a model asks for a specific engine plugin or process component.

## Where to find it

- **Tools → Plugins…** — a read-only inventory of every file-format filter
  available to the Open and Save dialogs.
- **Model → Simulation Options → Files / Output / Plugins → Plugins** — the
  editable `[PLUGINS]` table for the open model.
- **Model → Reaction System…** — the only place SWMMVis registers a
  `[PROCESS_COMPONENTS]` row for you.

## Step-by-step

### What a plugin is here

SWMMVis has **no GUI plugin system**. Nothing in the application loads
third-party Qt plugins, and there is no plugin search path setting. Everything
this appendix describes is an **engine** extension, surfaced through the GUI.

There are three distinct mechanisms, and they are easy to confuse:

| Mechanism | Declared in | What it does |
|---|---|---|
| **I/O plugins** | `[PLUGINS]` | read or write model, results, report or state files |
| **Process components** | `[PROCESS_COMPONENTS]` | add physics — reactions, ARD transport, heat, water age |
| **Compute backends** | `[OPTIONS] FV_BACKEND` and `[2D_OPTIONS] BACKEND` | choose the solver implementation (CPU, OpenMP, CUDA, HIP, SYCL) |

### The Tools → Plugins… dialog

Window title **Plugins**. Its header text says exactly what it is:

> *File-format filters available to Open / Save dialogs. Built-in filters are
> bundled with SWMMVis; engine plugins are discovered from the running engine.
> Enable / disable and user-installed plugins arrive in a follow-up slice.*

It is a tree, not a flat table. Top-level rows are format **roles**; children
are the individual filters.

| Column | Contents |
|---|---|
| **Description** | the filter's human name |
| **Patterns** | its globs, space-separated |
| **Plugin** | the providing plugin's id, or **(built-in)** for a GUI-bundled filter |
| **R/W** | `R`, `W` or `RW` |

The role groups are: *Input (read)*, *Results (read)*, *Results (write)*,
*Report (write)*, *Hot-Start (read)*, *Hot-Start (write)*, *Project (read)*,
*Project (write)*, *Vector (read)*, *Raster (read)*, *Tabular (read)*,
*Map Export (write)* and *Component Config (read)*.

Two buttons only: **Rescan**, which clears the registry, re-adds the built-in
filters and re-queries the engine, and **Close**.

**The dialog is strictly read-only.** There is no Load, Unload, Enable, Add
Path, OK or Apply, and no checkboxes. An enable/disable flag exists in the data
model and is drawn as greyed-out text, but nothing ever sets it — treat the
capability as present-but-inert.

\figtodo{a03_plugins_dialog.png, The Tools Plugins dialog with the role groups expanded}

#### What you will see listed

GUI built-in filters: `*.oswp` (project read and write), `*.out` (SWMM binary
output, read), the vector set (`*.shp`, `*.geojson *.json`, `*.gpkg`, `*.kml`,
`*.gml`), the raster set (`*.tif *.tiff`, `*.img`, `*.asc`, `*.nc`,
`*.hdf *.h5`), tabular (`*.csv`, `*.tsv`), map export (`*.png`, `*.svg`,
`*.dxf`, `*.emf`, `*.map`) and component configs (`*.rxn`, `*.ard`, `*.lard`,
`*.heat`, `*.age`, `*.i2d`).

Engine-contributed filters: `*.inp` (input read), `*.out` (output write),
`*.rpt` (report write), `*.hs` (state read and write), `*.hsf` (state read
only), and `*.gpkg` when the GeoPackage plugin is compiled in.

### Plugins that ship with the engine

All are **built in** — statically linked, registered at start-up, and loaded
without any file on disk:

| Id | Name | Formats |
|---|---|---|
| `org.hydrocouple.openswmm.builtin.input` | Default SWMM Input Plugin | `.inp` read and write |
| `org.hydrocouple.openswmm.builtin.output` | Default SWMM Output Plugin | `.out` |
| `org.hydrocouple.openswmm.builtin.report` | Default SWMM Report Plugin | `.rpt` |
| `org.hydrocouple.openswmm.builtin.state_io` | Default Hot-Start / State IO Plugin | `.hs` |
| `org.hydrocouple.openswmm.plugins.geopackage` | GeoPackage | `.gpkg` — input read, output write, report write; present only when the engine was built with GeoPackage support |

There is **no shipped example loadable I/O plugin**. The only genuine shared
objects in either repository are the GPU compute backends (below).

### `Default2DOutputPlugin`

The 2D HDF5 writer is a built-in output plugin, but it is special: it is
instantiated directly by the engine when a model has 2D enabled, it registers
no id, and it does **not** appear in the Plugins dialog or in the format
registry.

Its only control is **`[2D_OPTIONS] OUTPUT_FILE`**. An empty value means no 2D
output is written; a relative path is resolved against the `.inp`'s directory.
`REPORT_2D` toggles reporting independently. See \ref manual_file_formats for
the datasets it writes.

### The `[PLUGINS]` table

**Simulation Options → Files / Output / Plugins → Plugins** edits the open
model's `[PLUGINS]` section. Its intro reads:

> *Plugins listed in the model's `[PLUGINS]` section. Each row names a writer /
> output / report plugin (by id, id:version, or shared-library path) and any
> free-form arguments to pass to its `initialize()` call. The first
> input-capable row is also used by File → Save As when picking a non-`.inp`
> extension.*

Two columns:

| Column | Contents |
|---|---|
| **Plugin (path / id / id:version)** | placeholder *plugin id, id:version, or library path*; the **…** browse button opens **Choose Plugin Library** with a per-platform filter (`*.dll`; `*.dylib *.so *.bundle`; `*.so`) |
| **Arguments** | free-form tokens passed verbatim to the plugin |

**Add** and **Remove** manage rows. **Remove** is greyed for a **built-in**
plugin id, with the tooltip:

> *"…" is a built-in plugin statically linked into the engine — removing the row
> would have no effect (the plugin stays loaded in-process).*

An empty first column is a validation error: *[PLUGINS] row N: plugin id / path
is required.*

Above the table are combo boxes selecting which plugin id drives the input
writer, the results output and the report; applying makes sure each non-empty
id has a matching table row.

On **OK** or **Apply**, the table is diffed against the engine: new or changed
rows are set, and engine rows no longer in the table are removed. The result is
written into **the model's `[PLUGINS]` section**, so the setting is **per
project**, not per user, and nothing is stored in preferences.

The whole group is disabled — *Not available in SWMM 5 (legacy engine).* —
when the project is bound to a legacy engine.

Row syntax in the file is one plugin per line: the first token is the library
path, id, or `id:version`, and everything after it is passed through as
initialisation arguments.

```
[PLUGINS]
;;LibraryPath                Arguments
./plugins/hdf5_output.so     file="results.h5"  compress=9
```

No path rebasing or quoting is applied on write.

\figtodo{a03_plugins_table.png, The Plugins sub-tab of the Files Output Plugins page in Simulation Options}

### Where plugins are discovered

The engine's plugin factory runs a fixed search at start-up:

1. Built-in plugins are registered first, with no disk access.
2. Every `.so` / `.dylib` / `.dll` in the **directory of the engine shared
   library**.
3. Everything in **`<engine library dir>/plugins`**.
4. Everything in **`<engine library dir>/components`**.

Each candidate is opened and must export the plugin-info entry point;
libraries that do not are skipped silently.

An explicit `[PLUGINS]` row is then resolved: a token containing a path
separator or a shared-library extension is loaded directly; anything else is
looked up in the registry by `id:version` first and then by `id`, and the
first-loaded wins.

There is **no `OPENSWMM_PLUGIN_PATH` environment variable** and no user-managed
plugin path for I/O plugins. To add one, put the library next to the engine
library or in its `plugins` subdirectory.

Compute backends use a different search (see below).

### `[PROCESS_COMPONENTS]`

A process component adds physics rather than file formats. Row syntax is the
component id followed by `key="value"` pairs; a token without `=` is a hard
error. Only `config=` is special-cased — it names the component's sidecar
configuration file, resolved relative to the `.inp`.

```
[PROCESS_COMPONENTS]
org.hydrocouple.openswmm.reactions       config="model.rxn"
org.hydrocouple.openswmm.transport.ard   config="model.ard"
org.hydrocouple.openswmm.waterage        config="model.age"
org.hydrocouple.openswmm.heat            config="model.heat"
```

Registering a component **enables** it. Duplicate ids are refused, and a
component config file may not contain a nested `[PROCESS_COMPONENTS]` section.

| Id | State |
|---|---|
| `org.hydrocouple.openswmm.reactions` | implemented |
| `org.hydrocouple.openswmm.transport.ard` | implemented |
| `org.hydrocouple.openswmm.heat` | implemented |
| `org.hydrocouple.openswmm.waterage` | implemented |
| `org.hydrocouple.openswmm.transport.lard` | **placeholder only** — the Lagrangian solver is selected with `[OPTIONS] QUALITY_SOLVER LAGRANGIAN` and takes no config file |
| `org.hydrocouple.openswmm.integrated2d` | not implemented |

The shared-library-path form of a component row is documented but reserved; only
registered ids resolve today.

**In the GUI**, there is no general component editor. The only automatic
registration happens in the **Reaction System** editor: pressing **Save to
File** with no reactions component bound asks

> **Create reactions component** — *No reactions component is bound to this
> model. Register one and create its config file (model.rxn beside the project
> input)?*

and registers `org.hydrocouple.openswmm.reactions` with the relative path
`model.rxn`. Every other component row is written by hand.

(The Quality & Transport page's note claiming the ARD component file is *bound
on the Files / Output / Plugins page* is stale — no such control exists.)

### Compute backends

The finite-volume 1D solver and the 2D marcher can each run on a Kokkos-backed
compute plugin instead of the built-in CPU code.

| Where | Key | Values | Default |
|---|---|---|---|
| `[OPTIONS]` | `FV_BACKEND` | `CPU`, `AUTO`, `OMP`, `CUDA`, `HIP`, `SYCL` | *(engine default)* |
| `[2D_OPTIONS]` | `BACKEND` | `AUTO`, `CPU`, `OMP`, `CUDA`, `HIP`, `SYCL` | `AUTO` |

An unknown `FV_BACKEND` token is ignored silently; an unknown `[2D_OPTIONS]
BACKEND` is rejected with *Unknown BACKEND: \<v\> (expected
AUTO|CPU|OMP|CUDA|HIP|SYCL)*.

`AUTO` tries the available plugins in the order **cuda → hip → sycl → omp**,
then falls back to the built-in CPU solver. Naming a backend explicitly loads
that plugin outright and bypasses the model-size gate that `AUTO` respects. A
missing plugin or an absent device falls back to CPU with a notice on stderr —
never a hard failure. `FV_PRESSURIZED_IMPLICIT` forces CPU regardless of the
request.

Environment variables take precedence over the deck: **`OPENSWMM_FV_BACKEND`**
overrides `FV_BACKEND`, and **`OPENSWMM_2D_BACKEND`** overrides `[2D_OPTIONS]
BACKEND`.

In the GUI, the 2D backend is the **Backend:** combo in the **Performance**
group of **Simulation Options → 2D Surface Routing → Rainfall & Output**, with
the entries
**Auto**, **CPU (built-in marcher)**, **OpenMP (Kokkos)**, **CUDA**, **HIP**
and **SYCL**.

#### Backend discovery

Compute plugins have their **own** search path, unlike I/O plugins:

1. every directory in **`OPENSWMM_GPU_PLUGIN_PATH`** (`:`-separated, `;` on
   Windows);
2. the engine library's own directory;
3. `<engine library dir>/gpu`.

The filenames probed are `libopenswmm_gpu_<backend>` with the platform's
shared-library extension. A candidate must export the probe and factory symbols
and match the ABI version.

#### What is actually built

Backends are a **build-time** choice: the CMake option selects exactly one
target per configure, defaulting to OpenMP. A stock build therefore produces the
**Kokkos-OpenMP** plugin only. CUDA, HIP and SYCL are fully wired in code and
CMake but require the corresponding vendor toolchain and a Kokkos built with
that backend enabled — the configure hard-errors otherwise. The portable base
build ships with the GPU plugin off entirely and stays Kokkos-free; OpenMP is
distributed as a separate package.

So if the **Backend:** combo lists CUDA but selecting it silently falls back to
CPU, the plugin simply is not present in your build.

\figtodo{a03_backend_combo.png, The Backend combo in the Performance group of 2D Surface Routing - Rainfall & Output}

## Tips and gotchas

- The Plugins dialog reports what the engine *found*, so it is the quickest way
  to answer "does this build have GeoPackage support?" — look for `*.gpkg` under
  *Input (read)*.
- Removing a built-in plugin's `[PLUGINS]` row does nothing; built-ins are
  statically linked and always loaded. The dialog greys the button and says so.
- A `[PLUGINS]` row is per model. Copying a model to another machine copies the
  requirement with it — if that machine lacks the library, the run fails at
  open.
- `[PROCESS_COMPONENTS]` config paths are relative to the `.inp`. Move the
  `.inp` and take the sidecars with it, or the components will not load.
- A process component whose master `[OPTIONS]` switch is off (`WATER_AGE`,
  `HEAT_TRANSPORT`) loads and then warns that it has nothing to do.
- `Default2DOutputPlugin` never needs a `[PLUGINS]` row. If you do not get a
  `.2d.h5`, check `[2D_OPTIONS] OUTPUT_FILE` and `REPORT_2D`, not the plugin
  list.
- No `.inp` in either repository actually uses `[PLUGINS]`. The section is
  exercised through the API; the example above is from the parser's own
  documentation.

## Related

- \ref manual_file_formats — every format these plugins provide
- \ref manual_simulation_options — the Files / Output / Plugins page and the 2D page
- \ref manual_water_quality — the process components that add transport physics
- \ref manual_performance — threads, backends and what actually speeds a run up
- \ref manual_troubleshooting — missing plugin and missing 2D build symptoms
- \ref tutorial_transport — a model that binds four process components
