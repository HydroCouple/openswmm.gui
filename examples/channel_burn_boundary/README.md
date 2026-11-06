# Channel burn at the study boundary

A small, synthetic **mesh-generation and network-editing test**. Coordinates,
elevations and lengths are in metres. EPSG:3857 provides a shared projected
coordinate frame; these coordinates do not describe a real site. The source
terrain is a flat 10 m surface with 1 m pixels. No rain or inflow is supplied:
this example verifies geometry and connectivity, not hydraulic calibration.

## Generate, then burn

1. From the Welcome screen, open **Channel Burn at the Study Boundary** and
   choose a working folder. Alternatively, copy this whole directory and open
   `channel_burn_boundary.inp` in the copy. Its `.oswp` sidecar loads the terrain,
   study boundary, projected CRS and mesh options.
2. Open **Generate 2D Mesh** after the terrain and boundary layers have loaded.
   Select **Study domain** as the boundary polygon layer and
   **Flat terrain (10 m)** as the DTM. The domain is the square
   `0 <= x <= 32`, `0 <= y <= 32`; using the model's extent would include XY
   and would test a different case.
3. Generate the background mesh using the supplied 4 m cell size and terrain
   refinement settings. Generation leaves the 1D network unchanged.
4. Open **Burn Channels…**. Choose **All open channels**, channel size **2 m**,
   spacing divisor **4**, and enable replacement of burned intervals in 1D.
   Quads are preferred where they fit. Burn, then review the quality warnings.
5. Optional: enable the DEM-copy option, select `terrain.asc`, and use **1** metre
   per DEM elevation unit. This stages a VRT, changed tiles and a CSV report.
6. Undo and redo the burn, then **Save As** `adopted.inp` and reopen it. You can
   also Undo after Save and save the restored network and mesh again.

The general-purpose node/rim and conduit mesh constraints are disabled in this
fixture to isolate channel replacement. Required channel-interface couplings
are still created. Layer selection is intentionally a manual first step:
saved GIS selector identities refer to the loaded layer in the working copy.

## Expected result

```text
Before: A --AB(pipe)-- B --BC(channel)-- | -- C --CD(channel)-- D --DE(pipe)-- E
                                       x=0 study boundary
After:  A --AB(pipe)-- B --BC(2 m)-- outfall === 2D channel === D(outfall) --DE-- E

Outside the square: X --XY(channel)-- Y   (unchanged)
```

| Item | Expected behavior |
| --- | --- |
| BC | Split at `(0,16)`; its 2 m outside prefix remains named BC. Its 16 m inside portion is replaced by the mesh. |
| Boundary interface | A generated `BN_…` outfall at `(0,16)` connects retained BC to the mesh. Its invert is approximately 8.633333 m. |
| CD and C | CD is fully replaced; unreferenced interior junction C is removed. |
| D | Becomes a coupled outfall at `(24,16)`. The surviving closed pipe DE retains its physical upstream elevation of 8.10 m. |
| AB, B, DE, E | Retained; AB's downstream physical elevation remains 8.90 m. |
| XY, X, Y | Wholly outside; remain unchanged. XY is 10 m long. |
| Network totals | Four links and seven nodes after replacement. Originally five links and seven nodes. |
| Terrain | When requested, a separate VRT with burned GeoTIFF tiles is published on Save. Source `terrain.asc` and every raster pixel outside the study domain remain unchanged. |
| Mesh | Confined to the square. Channel banks and bed are checked against the independent channel tolerance; deviations are warnings. Clipped reaches can use triangles despite the quad preference. |
| Undo / Redo | One Undo restores the original network and previous mesh; Redo reapplies both. |

Three open channels qualify by section (BC, CD, XY); domain clipping excludes XY. Do not expect an exact cell count: triangulation and
quality refinement may vary while satisfying the same geometric constraints.

## Automated regression

Use an engine build containing commit `1b03238b` (cross-section preservation
during link deletion). Older engines can change a surviving channel's section
when another link is removed; the regression below detects this.

From the repository root, using a configured build with GUI tests enabled:

```sh
cmake --build build --target test_meshterrainpipeline
ctest --test-dir build -R '^test_meshterrainpipeline$' --output-on-failure
```

The `bundledChannelBoundaryExample` case copies these exact input files into
the test output directory, loads the saved project through the GUI serializer,
selects its boundary, and runs the real mesh pipeline and adoption path. It
checks retained link length, interface conversion, node retirement, outside
channel retention (including its complete cross-section after each edit),
Undo/Redo, Save As/reopen and engine topology validation,
finite in-domain mesh coordinates, unchanged outside raster pixels, and
unchanged bundled inputs. The generated `adopted.inp`, mesh, project sidecar and
burned raster can be inspected under the configured
`SWMMVIS_TERRAIN_PIPELINE_OUTPUT/bundled_channel_example/` directory.

For all focused channel/terrain tests:

```sh
ctest --test-dir build -R '^test_(channelburn.*|burnedrasterwriter|terrainerrorfield|meshdialog_seeds|meshterrainpipeline)$' --output-on-failure
```

Separate regression cases cover repeated crossings, holes, irregular sections,
rectangular banks, conflicting overlaps and rollback. This compact example does
not establish hydraulic equivalence between a 1D channel and a 2D replacement.
