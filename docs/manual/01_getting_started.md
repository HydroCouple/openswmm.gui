# 01 — Getting Started

## What you'll do

Install OpenSWMM GUI, launch it, and open your first SWMM model.

## Where to find it

After install: macOS Applications folder, Windows Start Menu under "OpenSWMM", or Linux
desktop launcher under "Education / Science".

## Step-by-step

### 1. Launch the application

Double-click the OpenSWMM icon. The first time you launch you will see:

- A **splash screen** while Qt initializes (this can take a moment if GDAL is loading
  large CRS database files for the first time).
- The **main window** with the **Welcome tab** active in the center.

### 2. Tour the Welcome tab

The Welcome tab is the application's home page. It has four sections:

| Section | What it does |
|---------|--------------|
| **Start Modeling** | "New Project…" creates a blank project (placeholder in the current slice — opens the Welcome page). "Open…" opens any `.inp` or `.oswp` file. |
| **Open Recent Files** | Click any recently opened file to reopen it. The list mirrors `File → Open Recent` and persists across launches. |
| **Learn SWMM** | External help links: User Manual, engine API reference, and the GitHub issues page. |
| **Example Projects** | Auto-discovered example `.inp` files bundled with the install. Each opens as a read-only copy so you cannot accidentally damage the bundled file. |

To hide this tab on next startup, uncheck **"Show welcome page on start up"** at the
bottom of the tab.

### 3. Open an example model

If your install has bundled examples, click any **Example Projects** entry. Otherwise:

1. Click **Open…** in the **Start Modeling** section.
2. Navigate to a `.inp` file (e.g., `swmm-toolkit/tests/data/test_Example1.inp` if you
   have the SWMM Python toolkit checked out).
3. Click **Open**.

You should see:

- A new MDI tab appear, named after the file (e.g., `test_Example1`).
- The model network rendered on a map canvas — junctions as circles, conduits as lines,
  subcatchments as polygons.
- The **Coordinate Reference System** button in the status bar showing the model's CRS
  (or "Local" if the model has no CRS defined).
- The **Flow Units** combo in the status bar reading the model's flow unit (e.g., "CFS"
  for US customary or "CMS" for SI).

### 4. Explore the canvas

| Action | How |
|--------|-----|
| Pan | Click the **Pan** tool in the toolbar (or default), then click-drag the canvas. |
| Zoom in / out | **Zoom In** / **Zoom Out** tools, or scroll wheel. |
| Zoom to full extent | **Full Extent** toolbar button. Auto-fits to all visible layers. |
| Identify an object | Click the **Select** tool, click a node or link. |

### 5. Switch flow units

Use the **Flow Units** dropdown in the status bar to change between CFS / GPM / MGD /
CMS / LPS / MLD. The change writes back to the engine and marks the project dirty
(see [Working with Projects](03_projects.md)).

## Tips and gotchas

- **First-launch slowness.** GDAL builds an SRS cache on first run. Subsequent launches
  are much faster.
- **macOS Gatekeeper.** If you downloaded the `.app` from the web you may need to
  right-click → Open the first time. Signed installers (planned) avoid this.
- **No CRS in your model?** A dialog will prompt you to assign one when the model loads.
  Pick "Local" if your coordinates are arbitrary engineering units; the map will display
  but no basemap (web tiles) will appear because basemaps require a real CRS.

## Related

- [02 — The Interface](02_interface.md) — what every menu, toolbar, and dock does.
- [03 — Working with Projects](03_projects.md) — saving, recent files, dirty state.
