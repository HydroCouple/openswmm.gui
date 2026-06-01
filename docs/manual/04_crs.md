@page manual_crs 04 — Coordinate Reference Systems

## What you'll do

Pick the CRS for an open model, switch between **reprojecting stored coordinates**
and **re-rendering only**, and recover when a model loads with no CRS at all.

## Where to find it

- The **CRS button** in the bottom-right of the status bar shows the canvas CRS
  (e.g. `EPSG:6595`). Click it to open the picker.
- `Project → Set Layer CRS` does the same thing scoped to the active layer.

## Step-by-step

### Setting the CRS for the first time

When a model has no `[PROJECTION]` section, the GUI loads it with a placeholder
**Local** CRS. You'll be prompted at load time to pick one. If you cancel:

- The network still renders at its native coordinates.
- Basemap reprojection is unavailable (basemaps are in EPSG:3857; with no source
  CRS we cannot transform).
- You can return to it later via the CRS button or `Project → Set Layer CRS`.

### Changing the CRS on a loaded model

Clicking the CRS button after the model is loaded opens the **CRS picker**.
Picking a different CRS opens the **CRS Change** dialog with two choices:

| Choice | What it does | When to pick it |
|--------|--------------|-----------------|
| **Reproject stored coordinates** | Iterates every node, link vertex, and subcatchment polygon and rewrites the coordinates via the OGR transform from the source CRS to the target CRS. The new CRS is written to the engine via `swmm_spatial_set_crs`. The project is marked dirty; the change persists on save. | The new CRS is the *correct* one for the data — the old CRS was wrong, or you genuinely want to convert your model into a different projection. |
| **Re-render only (display in new CRS)** | Stored coordinates are unchanged. The canvas reprojects on the fly for display. The model file is untouched. | The source CRS is correct; you just want a different visualization (e.g. WebMercator for online sharing). |

**Cancel** dismisses the dialog without changing anything.

### Local source CRS

If the model's source CRS is **Local** (no real spatial reference), the
**Re-render only** choice is disabled — there is no transform to build.
Use **Reproject** to first commit to a real source CRS.

### Recovering from a misaligned basemap

The most common cause of features that "don't sit on top of" a basemap is a
*wrong* CRS — usually a UTM or State Plane zone that's geographically near but
not the right one. Symptoms:

- Network is the right shape but offset from the basemap by hundreds of metres.
- Network is mirrored, scaled, or rotated relative to the basemap.

Fix:

1. Open the CRS picker.
2. Pick the *correct* CRS for the data.
3. In the CRS Change dialog, choose **Re-render only** — your stored coordinates
   are correct; only the interpretation was wrong.

If you're not sure which CRS is correct, try common ones for your region (state
plane / UTM / national grid). If the basemap aligns, you've found it.

## Tips and gotchas

- **Reproject is permanent for the in-memory model**, but only persists on disk
  when you Save. If you reproject and then Quit without saving, the file on disk
  is unchanged.
- **Reproject is best-effort** — if a single link or polygon fails to transform
  (e.g. invalid geometry), the rest still get done. Watch the message log for
  per-object failures.
- **Re-render only is free**: it costs zero engine writes. Toggle CRSes freely
  to compare visualizations.
- The **CRS button text** always reflects the *canvas* CRS, not the layer CRS.
  After a Re-render they differ; after a Reproject they match.

## Related

- [02 — The Interface](02_interface.md) — where the CRS button lives.
- [03 — Working with Projects](03_projects.md) — dirty marker after Reproject.
