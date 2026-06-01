@page manual_results 11 — Running and Viewing Results

## What you'll do

Run a SWMM simulation from the toolbar and inspect time series of any
node, link, or subcatchment in a chart.

## Where to find it

- **Run** → toolbar's Execute button (lightning-bolt icon).
- **Plot Time Series** → right-click any object in the **Object Browser**
  → *Plot Time Series…*

## Step-by-step

### 1. Run a simulation

1. Open a `.inp` project (File → Open or the Welcome tab).
2. (Optional) Edit options via Tools → Simulation Options.
3. Click the **Execute** toolbar button.
4. The GUI auto-saves the project (the engine reads from disk), then
   runs the simulation on a background thread. The status-bar progress
   bar enters busy mode for the duration; the GUI stays responsive.
5. On completion, the Message Log reports the path to the produced
   `.out` file. The same `.out` is automatically added to the canvas
   as a **SWMM Results** layer — you don't need to manually load it.
6. Engine errors (non-zero return) are logged with their numeric code.

### 2. Plot a time series

1. Open the **Object Browser** dock (tabbed with Layers).
2. Find the node / link / subcatchment you care about (use the filter
   box if needed).
3. Right-click the row → **Plot Time Series…**
4. The dialog opens with a default variable selected. Use the
   **Variable** dropdown to switch:
   - **Node**: depth, head, volume, lateral inflow, total inflow, overflow
   - **Link**: flow, depth, velocity, volume, capacity
   - **Subcatchment**: rainfall, snow depth, evaporation, infiltration, runoff
5. The X axis is **hours since simulation start** (calendar-time axis is
   a planned follow-up).
6. The dialog is non-modal — open as many plots side-by-side as you like.

## Tips and gotchas

- **Run requires a saved project.** Unsaved edits are auto-saved
  before the run; if save fails the run is aborted and the failure
  reason logged.
- **No cancel button yet.** The engine doesn't expose a clean cancel
  API, so once a run starts it goes to completion. Long runs block
  any subsequent run attempt; the Run toolbar button stays clickable
  but the second click queues a no-op until the first completes.
- **"No results loaded" message?** The Plot Time Series action needs a
  `.out` layer on the canvas. Either run a simulation first (which
  auto-loads it) or use the toolbar's **Add SWMM Output** button to
  add an existing `.out` file.
- **Pollutant variables not yet shown.** The variable dropdown lists
  the standard SWMM variables; per-pollutant time series ship with a
  follow-up slice that introspects the `.out`'s pollutant table.
- **Multi-series and difference plots are deferred.** Today's dialog
  is single-object / single-variable. The full Phase 5.2 plan adds a
  per-session column for comparing two open projects' results, plus a
  derived **Difference** series.

## Related

- [03 — Working with Projects](03_projects.md) — saving the project (the
  Run action requires this).
- [05 — Layer Management](05_layers.md) — the SWMM Results layer that's
  auto-added after a run lives in the Layers dock under "SWMM Results".
- [06 — Object Browser and Property View](06_object_browser.md) — the
  dock you right-click from to open the plot.
- [10 — Simulation Options](10_simulation_options.md) — what the Run
  action sees on disk.
