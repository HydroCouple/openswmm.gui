@page manual_running 20 — Running a Simulation

## What you'll do

Start the SWMM engine on the active project, watch its progress in the
**Simulation Status** dock and the **Message Logs** dock, pause or stop it
mid-flight, and read the files it leaves behind — the `.rpt` report, the `.out`
1D binary results and, for a 2D model, the `.2d.h5` HDF5 results. You will also
learn what SWMMVis does *before* the engine starts (auto-save, 2D pre-flight,
output-overwrite guard) and what it does the moment the run ends (auto-load the
results, make them the active results layer, offer them to the report viewer).

\figtodo{20_run_overview.png, A simulation in flight — Simulation Status dock; Message Logs and the live map animation}

## Where to find it

| Command | Menu | Ribbon | Shortcut |
|---------|------|--------|----------|
| **Execute** | **Analysis → Execute** | **Home → Run** | `Ctrl+R` |
| **Pause Execution** | **Analysis → Pause Execution** | **Home → Run** | — |
| **Cancel Execution** | **Analysis → Cancel Execution** | **Home → Run** | `Ctrl+.` |
| **Summarize Results** | **Analysis → Summarize Results** | **Analysis → Report** | — |
| **Report** | **Analysis → Report** | **Analysis → Report** | — |
| **Show Mass Balance** | **Analysis → Show Mass Balance** | **Analysis → Network Analysis** | — |

Two docks carry the run:

- **Simulation Status** — **View → Panels → Simulation Status** (`Ctrl+Alt+6`).
  It is shown and raised automatically when a run starts.
- **Message Logs** — **View → Panels → Message Logs** (`Ctrl+Alt+7`).

The status bar carries a progress bar while any run is in flight, and a
permanent **Engine version** combo that selects which engine the next run uses.

## Step-by-step

### Preconditions for Execute

**Execute** refuses to start and writes a warning to **Message Logs** unless all
of the following hold:

| Precondition | What happens if it fails |
|--------------|--------------------------|
| A project window is active and its model layer has an open engine handle | Log warning: *Open a SWMM project first to run a simulation.* |
| The model has a `.inp` path on disk (it has been saved at least once) | Log warning: *Save the project before running — Run uses the .inp on disk.* |
| No other in-flight run is already writing the same `.out` | Blocking **Output file in use** message box; the run does not start |

If the project has unsaved edits, SWMMVis **saves it automatically** before
starting — the engine reads the `.inp` from disk, so what is in memory has to be
written out first. The log records *Auto-saved before running.* If the save
fails the run is abandoned with a **Run failed** message box.

\figtodo{20_run_preconditions_log.png, Message Logs showing the auto-save line and the resolved output paths}

### 2D pre-flight

When **2D Surface Routing** is enabled for the project but no mesh resolves —
neither inline `[2D_*]` sections in the `.inp` nor a valid `[2D_MESH_FILE]`
reference — SWMMVis puts up a **2D mesh not found** prompt. Answering **Yes**
runs the model 1D-only (and logs that it did); answering **No** cancels the run
so you can generate or select a mesh first (see \ref manual_2d_mesh).

When a mesh *does* resolve but `[2D_OPTIONS]` has no `OUTPUT_FILE`, the engine
would run the 2D solver without writing anything to disk. SWMMVis therefore sets
`OUTPUT_FILE` to `<model-stem>.2d.h5` (relative to the `.inp` directory), saves
the project, and logs the substitution. Set `OUTPUT_FILE` yourself in
**Simulation Options** if you want it somewhere else.

### Files the run writes

| File | Default location | Set by |
|------|------------------|--------|
| `.rpt` — the text status report | `<model-stem>.rpt` beside the `.inp` | **Simulation Options → Files/Output/Plugins → Output → Report file** |
| `.out` — the binary 1D results | `<model-stem>.out` beside the `.inp` | **Simulation Options → Files/Output/Plugins → Output → Results output file** |
| `.2d.h5` — the 2D HDF5 (UGRID) results | `<model-stem>.2d.h5` beside the `.inp` | `[2D_OPTIONS] OUTPUT_FILE` |
| Hot-start files | as typed, relative to the `.inp` directory | **Simulation Options → Files → Scheduled hot-start saves** |

The `.rpt` / `.out` overrides are a **per-machine preference**, not part of the
`.inp`: they are stored per project path in the application settings and are
re-applied on every run. A relative override is resolved against the `.inp`
directory; an absolute one is used verbatim. When either differs from the
sibling default the log records *Using output overrides — rpt: … / out: …* so
you can see where the run is writing.

### Overwrite guard

If the resolved `.out` or `.2d.h5` already exists on disk with content, or is
currently loaded as a results layer in *any* open project window, SWMMVis asks
**Overwrite output?** and names exactly which streams will be replaced. Answer
**No** and the run is cancelled. Answer **Yes** and SWMMVis:

- closes any open handle on the `.out` (the layer object survives; only its file
  handle is dropped, so plots bound to it keep their identity);
- removes any 2D results layer pointing at the doomed `.h5` and deletes the file
  so the engine can recreate it.

\figtodo{20_overwrite_prompt.png, The Overwrite output prompt listing the 1D and 2D results files}

### The Simulation Status dock

One top-level row per (model, engine version) pair. Re-running the same model
with the same engine reuses its row rather than accumulating duplicates; opening
a model creates an **Idle** row so you can read its simulation window before any
run. Engine warnings appear as child rows under the job.

| Column | Shows |
|--------|-------|
| **Name** | The `.inp` file name |
| **Status** | Idle / Running / Success / Failed / Cancelled |
| **Progress** | Percent of simulated time completed |
| **Start Date** | Engine-side simulation start, from `[OPTIONS]` |
| **Current Date** | The engine's current simulation clock, pushed every tick |
| **End Date** | Engine-side simulation end |
| **Runoff Err** | Cumulative runoff continuity error, in percent |
| **Routing Err** | Cumulative routing continuity error, in percent |
| **2D Err** | Cumulative 2D surface continuity error; `—` when the run has no 2D model |
| **Duration** | Wall-clock seconds once the run finishes |
| **Avg Timestep** | Running average of the engine's step size, in seconds |
| **Version** | The engine version this row was run with |

All three continuity errors are **live** — they are polled in the step loop, not
only reported at the end, so a run that is diverging shows it while it is still
running.

\figtodo{20_simulation_status_dock.png, The Simulation Status dock with a running job and its warning children}

### Message Logs

The **Message Logs** dock is a three-column table — **Time**, **Type**,
**Message** — with severities *Information*, *Warning* and *Error*. Every engine
warning is mirrored here as well as into the Simulation Status tree, prefixed
with its numeric code and the model name. Right-click for **Copy** (the selected
rows) or **Copy All** (the whole log, with a header line); rows are copied as
tab-separated text that pastes straight into a spreadsheet. `Ctrl+C` copies the
selected rows whenever the log view has focus.

At start-up SWMMVis also logs any thread-count environment variables it inherited
from the launching shell — `OMP_NUM_THREADS`, `OMP_THREAD_LIMIT`,
`OPENSWMM_2D_THREADS`, `SWMM_DW_THREADS` — because each of them silently caps or
overrides `[OPTIONS] THREADS`. They are never unset for you.

\figtodo{20_message_logs.png, The Message Logs dock with an engine warning and its context menu}

### Pause and Cancel

**Pause Execution** is a checkable toggle. It parks the engine's step loop in a
short sleep; toggling it again resumes. **Cancel Execution** pauses first, then
asks **Stop simulation** for confirmation — answering **No** resumes the run.

Cancelling is *not* the same as aborting: the runner still calls the engine's
end, report and close steps, so the partial `.out` and `.rpt` are flushed to
disk and loaded as results. The log records *Simulation cancelled. Partial
results: …*.

When more than one run is in flight, Pause and Cancel act on the job **selected
in the Simulation Status dock**. With nothing selected they act on the single
in-flight run; with several running and no selection, a message box asks you to
select one first. Both actions are disabled unless at least one run is active.

\videotodo{Running a model — Execute; watching progress; pausing and stopping with partial results}

### Live results while the run is going

Two check boxes on the **Results** ribbon tab (group **Display**) govern what
updates while the engine is still writing:

| Control | What it does | Scope |
|---------|--------------|-------|
| **Live 1D** | Opens the `.out` as it is being written and re-reads its period count on every progress tick | A saved preference; applies to the **next** run started, any engine version |
| **Live 2D** | Renders the active 2D results layer live and keeps streaming frames into it | Per layer; only enabled for a live (streaming) source |

With **Live 1D** on, the results layer that the finished run will end up using is
created up front and grows in place. That means the map animation range, the
\ref manual_profile_plots "profile plot" and the
\ref manual_time_series_plots "comparison plots" all extend as the run
progresses — there is no swap from a "live" layer to a "final" one at the end,
and no backwards jump. Unchecking **Live 1D** mid-run stops further growth; the
finish handler still finalises the layer.

2D results stream over a separate path: the runner ships the mesh geometry once
when the 2D solver initialises, then pushes per-tick cell depths, edge fluxes,
vertex depths and rainfall into the layer at roughly one packet per second. Turn
**Live 2D** off to stop both the rendering and the streaming when the frame rate
is costing you engine throughput; turn it back on to resume at the newest frame.
When the run ends the layer's source is swapped from the live engine feed to the
`.h5` file so you can scrub backwards through the whole run.

\figtodo{20_live_toggles.png, The Live 1D and Live 2D check boxes in the Results ribbon Display group}

### What happens when the run finishes

- The job row flips to **Success**, **Failed** or **Cancelled** and its
  continuity errors, duration and average timestep are filled in.
- On success or cancellation with output on disk, the `.out` is opened (or the
  live layer finalised in place), its colour ramp auto-stretched, and it becomes
  the project's **active 1D results layer** — the one the analysis toolbar
  combos, the animation controller and every plotting tool act on.
- A 2D run's layer swaps to the on-disk `.h5` and becomes the **active 2D
  results layer**.
- The `.rpt` path is remembered on the results layer so the report viewer can
  list this run alongside others.

A run that reports success but wrote **no** `.out` is not treated as a success:
SWMMVis logs an error pointing you at the `.rpt`, because that almost always
means the engine failed while parsing input or setting up and the report file
carries the real `ERROR` line.

### Report — the status report viewer

**Analysis → Report** and **Analysis → Show Mass Balance** open the same
two-panel report viewer over the `.rpt` file. (They are wired to the same
handler; **Show Mass Balance** is a labelled second entry point to the same
report — where the continuity sections live — not a separate dialog, and it does
not pre-select those sections for you.)

| Part | What it does |
|------|--------------|
| **Run combo** (top) | One entry per loaded run — the scenario or layer name plus the `.rpt` that run wrote. Only shown when more than one report is available. Preselects the active results layer's report. |
| **Continuity banner** | Appears above the viewer only when a *Continuity Error (%)* value in the report exceeds the alert threshold |
| **Section list** (left) | Every `*****`-delimited section of the `.rpt` in file order, with a filter box above it. Clicking a section scrolls the viewer to it |
| **Report text** (right) | The full report in a monospace, syntax-highlighted view |
| **Search bar** | Find text in the report, with a **regular expression** toggle, next/previous buttons and a match counter |

Typical sections include the analysis options echo, element counts, the
raingage/subcatchment/node/link summaries, *Runoff Quantity Continuity*, *Flow
Routing Continuity*, *Quality Routing Continuity*, the highest-continuity-error
and time-step-critical element lists, node depth/inflow/flooding summaries, outfall
loadings, link flow and conduit-surcharge summaries, and the 2D and coupling
ledgers when those processes ran. The viewer shows whatever the engine wrote —
which sections exist depends on the `[REPORT]` flags and the processes the model
turned on.

If no `.rpt` exists yet the viewer reports the path it looked at and tells you to
run a simulation.

\figtodo{20_report_viewer.png, The report viewer with the section list; the continuity banner and the search bar}

### Summarize Results

**Analysis → Summarize Results** opens the statistics dashboard over the active
1D results layer. It reads the engine's cumulative statistics rather than
re-scanning the `.out`, and shows three sortable tables — **Node**, **Link**,
**Subcatchment** — plus a frequency histogram for the selected column. It needs
results: with no active results layer it puts up a **No Results** message box
telling you to run a simulation or load a `.out`. The dashboard is documented in
full in \ref manual_analysis_tools.

### Hot-start files

Hot-start handling is configured in **Simulation Options**, not at run time:

- **Files → Secondary file references → Hot-start file (USE)** names a state file
  the run reads at t = 0 (`[FILES] HOTSTART USE`).
- **Files → Scheduled hot-start saves** is an uncapped table of *(path, datetime)*
  rows. Leave the **Datetime** cell as *(end of run)* to write the file when the
  simulation ends; otherwise the engine writes when the simulation clock crosses
  the chosen date and time. Paths are stored relative to the `.inp` directory.

See \ref manual_simulation_options for the full page-by-page reference.

### Engine version, plugins and threads

- The **engine version** used by the next run is chosen in the status-bar combo:
  the built-in OpenSWMM 6 engine, or the legacy SWMM 5 engine. The legacy engine
  runs out of process in the `openswmm-legacy-worker` executable, so its progress
  and warnings arrive over the worker's standard output — everything else in this
  chapter (status columns, pause/cancel, live 1D results) behaves the same.
  Because the status model keys rows on *(model, engine version)*, running the
  same model on both engines gives you two rows to compare.
- **Writer plugins** replace the built-in `.inp` / `.out` / `.rpt` writers and are
  selected in **Simulation Options → Files/Output/Plugins**; see
  \ref manual_plugins.
- `[OPTIONS] THREADS` sets the OpenMP worker-thread count for the run. It is
  edited in **Simulation Options** (and its default for new projects in
  **Preferences**). Remember the environment variables listed above override it —
  which is exactly why they are logged at start-up.

## Tips and gotchas

- **Run always uses the `.inp` on disk.** Unsaved edits are written out first;
  if you want to keep the previous state, save a copy before you press `Ctrl+R`.
- **Cancel keeps results.** Stopping a long run is a legitimate way to inspect a
  partial answer — the engine's end/report/close sequence still runs.
- **A "successful" run with a 0-byte `.out` is a failure.** Open the report; the
  cause is almost always an input-parsing error.
- The overwrite guard inspects **every** open project, not just the active one —
  a second project that loaded the same `.out` as a comparison run will be
  flagged.
- **Live 1D** is read afresh on every progress tick, so you can turn it off in
  the middle of a run that is producing more data than you want to plot.
- A 2D run with no `[2D_OPTIONS] OUTPUT_FILE` gets one defaulted for it. If you
  never want the `.h5`, clear the key again after the run — the live view works
  without it, but nothing can be reopened afterwards.
- Continuity errors are visible **while the run is going**. A routing error that
  is already several percent an hour in rarely recovers; stop and look at the
  time-step settings rather than waiting for the report.

## Related

- \ref manual_simulation_options — every option the run reads, including `THREADS`, the `[FILES]` block and the output overrides
- \ref manual_results — results layers, styling by result variable and map animation
- \ref manual_time_series_plots — plotting the results this chapter produced
- \ref manual_profile_plots — profile plots along 1D paths and across the 2D mesh
- \ref manual_tabular_results — tabular results and report export
- \ref manual_analysis_tools — the statistics dashboard, flow balance and travel time
- \ref manual_2d_mesh — generating the mesh the 2D pre-flight looks for
- \ref manual_plugins — writer and engine plugins
- \ref manual_performance — thread counts, redraw policy and large models
- \ref manual_troubleshooting — what to do when a run fails
