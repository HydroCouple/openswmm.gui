# HANDOFF — GIS raster/vector layer persistence in the project file

Date: 2026-07-24
Files changed: `src/project/projectserializer.cpp`, `include/project/projectserializer.h`
Status: implemented from source, **not compiled here** — build + verify.

## Problem
Saving a project (`.oswp`) persisted SWMM model layers, basemaps (XYZ/WMTS/WMS), and mesh display state, but **not loaded GIS data layers** (GDAL rasters like GeoTIFF, OGR vectors like shapefiles). On reopen, those layers were gone.

## What was implemented
The `.oswp` root now carries a `gisLayers` array (schema v4+; the schema version was already 4). Each non-basemap `GISRasterLayer` / `GISVectorLayer` on the canvas is written with:
- `type`: `"raster"` or `"vector"`
- `path`: source path **relative to the .oswp** (via `toRelativePath`; resolved on load via `resolveStoredPath`)
- `name`, `visible`, `opacity`
- vector only: `layerName` (the OGR sublayer, from `ogrLayerName()`)

On load, `deserializeGisLayer` reconstructs each layer with the exact async open the *File ▸ Add Layer* flow uses: `new GISRasterLayer(QString())` / `new GISVectorLayer(QString())`, connect `openFinished` (single-shot) to apply name/visibility/opacity and `canvas->addLayer(..., pushUndo=false)`, then `openAsync(path[, layerName])`.

Integration points: write is in `writeRootJson` right after the basemap loop; load is in `applyFromFile` right after the basemap load block. New keys and both helper functions mirror the basemap ones.

## Verify (definition of done)
- [ ] Builds (2 files). New forward-decl `class MapCanvas;` added to the header for `deserializeGisLayer`.
- [ ] Load a raster + a shapefile, save, close, reopen → both reappear at the same path, name, visibility, opacity.
- [ ] Move the `.oswp` + data together to another folder → relative paths still resolve.
- [ ] A missing/renamed source file fails gracefully (open fails → layer dropped, no crash — `openFinished(false)` deletes it).

## Known limitations / follow-ups
1. **Styling not restored.** Only path + name + visibility + opacity are saved. Raster color ramps / vector symbology (renderers) are not yet persisted for GIS layers. Model/results layers already persist renderers via the `kRenderer`/sublayer hooks; extend the same to GIS layers as a follow-up.
2. **Z-order.** Layers are added on async-open completion, so stacking order among GIS layers may not exactly match the saved order. If order matters, add an index field and sort/insert at position on completion.
3. **Absolute vs relative paths.** Uses the same relative-path scheme as the rest of the serializer. Data outside the `.oswp` subtree is stored as an absolute path (per `toRelativePath` behavior) and won't be portable across machines.
4. **Vector sublayers.** Only the single active OGR layer (`ogrLayerName()`) is saved per `GISVectorLayer`. If one datasource was opened as multiple layers, each is its own layer entry (correct), but confirm multi-select opens round-trip.
5. **WCS / tile-pyramid layers** are not covered here (WCS is a basemap-ish service layer; `TilePyramidLayer` is a raster-overview helper). Add cases to `serializeGisLayer`/`deserializeGisLayer` if those should persist too.
