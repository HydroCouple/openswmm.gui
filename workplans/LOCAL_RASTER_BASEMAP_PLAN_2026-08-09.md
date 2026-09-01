# Local Raster Basemap Plan — 2026-08-09

Add a **Local File** tab to `AddBasemapDialog` so users can add a basemap from a local
image (GeoTIFF, PNG, JPEG, BMP, or any GDAL raster), optionally paired with a world
file, with an explicitly specified CRS. Rendering reuses the existing
`GISRasterLayer` tile pyramid — **no new tiling code is written**.

## Decisions (agreed 2026-08-09)

1. **Georeferencing wiring: GDAL PAM sidecar.** At add time the world file is parsed
   and SRS + GeoTransform are written to the standard `<image>.aux.xml` PAM sidecar.
   Every subsequent plain GDAL open (initial open, the N pooled tile-warp handles,
   overview builds, project restore) sees the georeferencing with zero changes to
   `GISRasterLayer`'s open paths. PAM has highest `GEOREF_SOURCES` priority, so it
   also cleanly overrides an embedded CRS when the user chooses to.
2. **Reusable connections: full parity.** New `LocalRasterConnection` struct +
   `BasemapConnectionStore` group, with the same New/Edit/Delete combo bar as the
   other tabs.
3. **Layer category: Basemap.** The created `GISRasterLayer` reports
   `isBasemapLayer() == true` and `SWMMImageryLayer` type → lands in `CatBasemaps`
   in the layer tree and is serialized in the project's `basemaps` array as a new
   `"localraster"` entry.

## Reuse inventory (already exists — do not reimplement)

| Need | Existing structure |
|---|---|
| Efficient tiled rendering of local rasters | `GISRasterLayer` : `TilePyramidLayer` — fixed-grid 256² canvas-CRS pyramid, N-slot pooled GDAL handles, coarser-tile fallback, seed tiles |
| Large-file performance | `maybeBuildOverviews()` background `.ovr` builds (works for PNG/JPEG too — external sidecar) |
| RGB/RGBA imagery rendering | `GISRasterLayer` bypasses the colour ramp for 3/4-band datasets |
| CRS picking UI | `CRSSelectionDialog` (GDAL CRS database browser) |
| Warp source CRS | `warpToCanvas()` reads `src->GetSpatialRef()` — PAM-supplied SRS flows through untouched |
| Dialog tab pattern | `AddBasemapDialog::setupUiXXX / populateXXX / buildXXXLayer / refreshXXXCombo` |
| Connection persistence | `BasemapConnectionStore` (QSettings, `BasemapConnections` group) |
| Project persistence | `ProjectSerializer::serializeBasemapLayer / deserializeBasemapLayer` (schema v3+ `basemaps` array) |
| Category mapping | `categoryForLayerType()` — `SWMMImageryLayer → CatBasemaps`; `setLayerType()` is already an instance-level setter (see `xyztilelayer.cpp:68`) |

## Phases

### Phase 1 — Data structures + store
**Files:** `include/connections/basemapconnection.h`,
`include/connections/basemapconnectionstore.h`, `src/connections/basemapconnectionstore.cpp`

Add to `basemapconnection.h`:

```cpp
/*! \brief Parameters for a local raster (GeoTIFF/PNG/JPEG/...) basemap. */
struct LocalRasterConnection
{
    QString name;
    QString filePath;
    QString worldFilePath;   //!< Empty = rely on embedded/sidecar georef.
    QString crsAuthCode;     //!< e.g. "EPSG:26985". Empty = use embedded CRS.
};
```

Store methods mirror the existing groups, minus auth (local files have none):
`saveLocalRaster()`, `loadLocalRaster()`, `localRasterConnectionNames()`,
`removeLocalRaster()`. Persist under the `BasemapConnections/localraster/` group.

→ verify: QSettings round-trip (save, reload, compare fields; remove, confirm gone).

### Phase 2 — Georeferencing helper (world file → PAM)
**Files:** new `include/io/rastergeoref.h`, `src/io/rastergeoref.cpp` (follows the
`io/gdaldrivers` pattern)

Free functions, no state:

- `parseWorldFile(path)` → 6 doubles {A, D, B, E, C, F}. **Pixel-center caveat:** a
  world file references the *center* of the top-left pixel; the GDAL GeoTransform
  references its *corner*: `gt[0] = C − A/2 − B/2`, `gt[3] = F − D/2 − E/2`,
  `gt[1]=A, gt[2]=B, gt[4]=D, gt[5]=E`.
- `worldFileCandidates(imagePath)` → conventional sidecar names for auto-detect:
  `.tfw`, `.pgw`, `.jgw`, `.bpw`, `.wld`, plus the "ext + w" form (`.tifw` style).
- `applyPamGeoref(imagePath, gt6 /*optional*/, crsWkt /*optional*/, QString *err)`
  → writes/merges `<imagePath>.aux.xml` (`<PAMDataset><SRS>…</SRS><GeoTransform>…`).
  Attempt the GDAL-native route first (open `GDAL_OF_UPDATE`, `SetSpatialRef` /
  `SetGeoTransform`, close); if the driver refuses update access (PNG/JPEG do),
  fall back to writing the PAM XML directly with `QXmlStreamWriter`, preserving any
  existing non-georef PAM content (statistics/histograms) by loading and editing
  the existing file.
- `probeGeoref(imagePath)` → {hasGeoTransform, crsDescription} for dialog prefill,
  via a cheap read-only `GDALOpenEx`.

→ verify: unit test — copy a CRS-less PNG + off-name world file into
`test_artifacts/localraster/`, call `applyPamGeoref`, reopen with GDAL, assert
`GetSpatialRef()` matches the requested EPSG and `GetGeoTransform()` matches the
corner-adjusted values. (Test files in `test_artifacts/`, not temp dirs — CLAUDE.md §4.1.)

### Phase 3 — GISRasterLayer basemap flag
**Files:** `include/layers/gisrasterlayer.h`, `src/layers/gisrasterlayer.cpp`

Minimal addition — no rendering changes:

```cpp
void setIsBasemap(bool on);                       // also setLayerType(SWMMImageryLayer)
[[nodiscard]] bool isBasemapLayer() const override { return m_isBasemap; }
```

→ verify: flagged layer appears under **Basemaps** in the layer tree; unflagged
rasters still land under Raster Layers (no regression).

### Phase 4 — Dialog tab
**Files:** `include/ui/dialogs/addbasemapdialog.h`, `src/ui/dialogs/addbasemapdialog.cpp`

Tab 4 "Local File", built with the existing patterns:

- Connection bar via `buildConnectionBar()` (combo + New/Edit/Delete), backed by the
  Phase 1 store; `refreshLocalCombo()`, `populateLocal()`, `onLocalConnectionSelected/New/Edit/Delete` slots.
- **File row:** path line edit + Browse. Filter:
  `GeoTIFF (*.tif *.tiff);;PNG (*.png);;JPEG (*.jpg *.jpeg);;BMP (*.bmp);;All GDAL rasters (*)`.
- **World file row:** path line edit + Browse (`*.tfw *.pgw *.jgw *.bpw *.wld *.*w`).
  On image selection, auto-fill from `worldFileCandidates()` when a sidecar exists.
- **CRS row:** read-only line edit + "Select CRS…" → `CRSSelectionDialog`;
  store `selectedAuthCode()`.
- **Status label:** after file selection, `probeGeoref()` reports what's embedded
  ("Embedded CRS: EPSG:32617, georeferenced" / "No georeferencing — world file and
  CRS required") and prefills the CRS field from the embedded CRS when present.
- **Validation:** OK enabled iff file exists AND (embedded geotransform present OR
  world file given) AND (embedded CRS present OR CRS selected).
- `buildLocalRasterLayer(QObject*)`:
  1. If a world file and/or CRS override is set → `applyPamGeoref()` (corner-adjusted
     GT; CRS auth code → WKT via `OGRSpatialReference::importFromUserInput`). Surface
     failure (e.g. read-only directory) in a message box; abort.
  2. `auto *layer = new GISRasterLayer(QString()); layer->setIsBasemap(true);
     layer->setName(<connection name or file base name>); layer->openAsync(path);`
     — async open matches the File ▸ Add Layer flow; `SWMMVis::onAddBasemapLayer()`
     adds it immediately and tiles appear when the open completes. No change needed
     in `swmmvis.cpp`.
  3. Wire into `createLayer()`'s tab switch.

→ verify: manual — PNG + `.pgw` + EPSG:26985 renders in the right place over an XYZ
basemap; pan/zoom shows tile-pyramid reuse (edge tiles only); a large GeoTIFF
triggers the background `.ovr` build and stays responsive.

### Phase 5 — Project persistence
**Files:** `src/project/projectserializer.cpp` (+ header for the changed signature)

- `serializeBasemapLayer()`: new branch
  `if (auto *r = qobject_cast<GISRasterLayer*>(layer); r && r->isBasemapLayer())`
  → `{type:"localraster", path, name}`. Pass `oswpPath` into
  `serializeBasemapLayer()` (update its one call site) so the path is stored
  relative via `toRelativePath()`, like the GIS-layer path. Georeferencing itself
  needs no serialization — it lives in the `.aux.xml` next to the image.
- `deserializeBasemapLayer()`: `"localraster"` → resolve path
  (`resolveStoredPath`), missing-file warning like `deserializeGisLayer`, then
  construct empty `GISRasterLayer`, `setIsBasemap(true)`, `setName`, `openAsync`,
  return immediately (caller adds to canvas; existing visibility/order restore
  applies as for other basemaps).

→ verify: save project with a local basemap, reload — layer returns under
Basemaps with correct name/visibility/position; move the image away, reload —
warning listed, no crash.

### Phase 6 — Final verification + release note

- Run Phase 2 unit tests + full manual pass: all four georef cases —
  (a) GeoTIFF with embedded CRS (no world file needed), (b) GeoTIFF, CRS
  overridden, (c) PNG + world file + CRS, (d) CRS-less TIFF + `.tfw`.
- Confirm no diff outside the files listed above (surgical-change check).
- Add CHANGELOG.md entry at release per CLAUDE.md §5.2.

## Explicitly out of scope

- Any new tiling/warping/caching code (`GISRasterLayer` + `TilePyramidLayer` already
  provide it).
- Reprojection-on-import, raster styling changes, multi-file mosaics.
- Auth/HTTP-header UI on the Local File tab (not applicable).
