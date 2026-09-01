# Local Raster Basemap — Build / Verify / Launch Handoff (2026-08-09)

Implementation of `workplans/LOCAL_RASTER_BASEMAP_PLAN_2026-08-09.md` is **complete
and code-reviewed** (cross-phase API check, GDAL leak check, serializer call-site
audit; one validation bug found and fixed pre-handoff). It has **not been compiled**
— the implementing environment had no Qt/GDAL toolchain. This document is the
instruction set for an agent (or human) on the development Mac to build, verify,
and launch.

## What changed

Feature files (all changes confined to these; the working tree also carries
pre-existing unrelated edits to `simulationoptionsdialog.*`, `newprojectdialog.cpp`,
`preferencesdialog.cpp`, `test_options_hydration_contract.cpp` — leave those alone):

- `include/connections/basemapconnection.h` — new `LocalRasterConnection`
- `include/connections/basemapconnectionstore.h`, `src/connections/basemapconnectionstore.cpp` — `save/load/removeLocalRaster`, `localRasterConnectionNames` (QSettings group `localraster`, no auth)
- `include/io/rastergeoref.h`, `src/io/rastergeoref.cpp` (NEW) — world-file parse, center→corner GeoTransform, PAM `.aux.xml` write/merge, georef probe, `authCodeToWkt` (namespace `openswmmvis::io::rastergeoref`)
- `include/layers/gisrasterlayer.h`, `src/layers/gisrasterlayer.cpp` — `setIsBasemap(bool)` / `isBasemapLayer()` override; retags to `SWMMImageryLayer` → Basemaps tree category
- `include/ui/dialogs/addbasemapdialog.h`, `src/ui/dialogs/addbasemapdialog.cpp` — Tab 4 "Local File" (index 4 in `createLayer()`), WCS-style connection bar (no Edit button), file/world-file pickers, `CRSSelectionDialog` integration, accept-time validation via warning dialogs
- `include/project/projectserializer.h`, `src/project/projectserializer.cpp` — `"localraster"` basemap entry (`kBmPath`, relative path); `serializeBasemapLayer` gained `oswpPath` param, `deserializeBasemapLayer` gained `oswpPath` + optional `warningsOut`
- `CMakeLists.txt` — two 1-line insertions (rastergeoref header + source, next to `gdaldrivers`)
- `tests/unit/CMakeLists.txt`, `tests/unit/test_rastergeoref.cpp` (NEW) — 9 tests; fixtures written to `test_artifacts/localraster/` (reviewable per CLAUDE.md §4.1)

## Step 1 — Build

From the repo root on the Mac (existing configured Ninja build dir `build/` with
`SWMMVis.app`; user preset `Darwin-local` also available):

```sh
cd /Users/calebbuahin/Documents/Projects/cbuahin_github/openswmm.gui
cmake --build build --target SWMMVis test_rastergeoref
```

If the incremental build complains about the CMake edits, reconfigure first:
`cmake --preset Darwin-local && cmake --build --preset Darwin-local`.

Expected risk points if the first compile fails (all low-probability, all
localized): missing include in `src/io/rastergeoref.cpp`, or the
`tests/unit/CMakeLists.txt` block (modelled on `test_sublayer_fixtures`,
line ~387). Fix in place; do not restructure.

## Step 2 — Unit tests

```sh
ctest --test-dir build -R rastergeoref --output-on-failure
```

All 9 must pass. Inspect the generated fixtures afterwards:
`test_artifacts/localraster/` should contain a small PNG, an off-name world file,
and a generated `.aux.xml` whose `<SRS>` resolves to EPSG:26985 and whose
`<GeoTransform>` matches the corner-adjusted world-file values.

Also run the neighboring suites the changes touch:

```sh
ctest --test-dir build -R "projectserializer|basemap|gdaldrivers" --output-on-failure
```

## Step 3 — Launch + manual verification

```sh
open build/SWMMVis.app
```

Manual pass (the four georef cases from the plan):

1. **GeoTIFF with embedded CRS** — Map ▸ Add Basemap ▸ Local File tab: pick a
   georeferenced `.tif`. Status shows "Embedded CRS: …; georeferenced"; CRS field
   prefills; OK adds the layer under the **Basemaps** category; it renders in the
   correct location over an XYZ basemap; pan/zoom refills only edge tiles
   (tile pyramid) and a large file triggers the background `.ovr` overview build
   without blocking the UI.
2. **GeoTIFF, CRS overridden** — same file, Select CRS… to a different EPSG.
   NOTE (reviewed, by design per plan): the native-update path writes the override
   into the TIFF itself when the driver allows update; verify rendering moves
   accordingly.
3. **PNG + world file + CRS** — CRS-less PNG with a `.pgw` beside it: world-file
   field auto-fills; pick e.g. EPSG:26985; OK creates `<file>.png.aux.xml` and the
   image renders georeferenced.
4. **CRS-less TIFF + `.tfw`** — world file auto-detected, CRS required by
   validation (OK shows a warning until selected).

Then persistence: save the project (`.oswp`), close, reopen — the local basemap
returns under Basemaps with its name; move the image away and reopen — a
"Local basemap file not found — layer skipped" warning appears, no crash.
Connection reuse: the saved connection name reappears in the tab's combo in a new
session and repopulates all fields.

## Step 4 — Wrap up

- If everything passes, commit the feature files listed above (nothing else) with
  a message referencing the plan doc.
- Add a CHANGELOG.md entry at the next release (CLAUDE.md §5.2), e.g.
  "Add Basemap ▸ Local File: local image basemaps (GeoTIFF/PNG/JPEG/BMP) with
  world-file + CRS assignment via GDAL PAM, rendered through the existing raster
  tile pyramid."

## Known judgment calls (already reviewed, acceptable)

- Validation is accept-time with warning dialogs rather than live OK-button
  enable/disable (matches the dialog's existing pattern; callers handle nullptr).
- `probeGeoref` runs a cheap GDAL open on the GUI thread on file selection —
  fine for local files, brief stall possible on network mounts.
- Embedded CRS without an authority code (custom projections) counts as "CRS
  present" (fixed during review to use the CRS description, not just the code).
