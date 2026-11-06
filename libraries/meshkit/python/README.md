# MeshKit for Python

MeshKit 0.2 builds independently with C++17 and Python headers. No Qt, OpenSWMM,
GDAL, NumPy headers, binding generator or network fetch is required by its CMake
build. NumPy is optional at runtime. License: Apache-2.0; robust predicates retain
their original public-domain notice. See LICENSE, NOTICE and RELICENSING.md.

```python
import meshkit
import numpy as np

request = {
    "points": np.array([[0., 0.], [20., 0.], [20., 20.], [0., 20.]]),
    "paths": [{"kind": "outer", "vertices": [0, 1, 2, 3], "closed": True}],
    "options": {"cells": "mixed", "maximum_size": 4., "minimum_target": .5,
                "adjacent_ratio": 3., "minimum_angle_degrees": 20.},
    "quad_regions": [{"ring": [[4., 4.], [16., 4.], [16., 16.], [4., 16.]],
                      "cells": "required_quads", "spacing": 3.}],
}
result = meshkit.run(request, progress=lambda event: print(event))
assert result.report["complete"], result.report
xyz = np.asarray(result.vertices)        # Owned read-only, zero-copy result view.
cells = np.asarray(result.cells)         # Strided uint32 (N,4), CCW.
counts = np.asarray(result.cell_types)   # 3 or 4. Fourth slot of triangles is unused.
meshkit.write("result.mkb", result)      # No replacement unless explicitly requested.
```

`meshkit.start(request, raster=..., provider_identity=...)` returns a native job
with `poll()`, `cancel()`, `done`, and `result(timeout_seconds=-1)`. Jobs run without
the Python GIL; progress callbacks in `run` execute on the caller thread. Dropping
a job requests cancellation. No Python callback runs for each cell. Inputs copy
into native owned storage once; strided float64 point buffers are supported.

Native terrain is supplied as `raster={"heights": float32_or_float64_2d_array,
"origin": [x0,y0], "spacing": [dx,dy]}` with positive spacing and rows increasing
in Y. NaN means missing data. The bilinear surface provider checks the complete
intersection of each triangular facet with raster cells, including extrema on
clipped edges. Reports concern the chosen raster reference, not physical source
accuracy. Set `terrain_tolerance` and `require_terrain_certification=True` to refuse
unknown/unmet terrain limits. Weighted tolerance uses `terrain_policy` with mode
`target_size`, `reference_size`, `minimum_tolerance`, `maximum_tolerance`.

Source cleanup is independent: `prepare_sources(features)` runs the native
bounded simplifier and independent audit. Features have `id`, `xy`, optional `z`
and `fixed`, `role` (`outer`, `hole`, `line`), `closed`, `marker`, and a `policy`.
A simplify policy names `maximum_deviation`, `maximum_z_deviation`,
`maximum_turn_degrees`, and area/clearance limits. Shared chains use the strictest
owner policy. `prepared_request(prepared, options=...)` uses retained buffers,
source mappings and authored elevations without constructing Python vertex rows.
The input-to-output mapping and source aliases remain available in the result.

`read(path, options=...)` supports MKB and SMS 2DM. `write(path, result,
options={"overwrite": True})` publishes atomically. 2DM export requires explicit
`allow_attribute_loss=True` when metadata cannot be retained. Inspect the report:
`complete_with_exceptions` is distinct from a certified result; failures return
empty geometry. MKB is lossless for the neutral model. External IDs are implicit
one-based sequences when the corresponding ID buffer is empty.

All output buffers retain their native owner, including after the result object
is deleted. They are read-only; use `np.array(view, copy=True)` for edits. Region
and type views have cell strides. NumPy is not required for job execution, basic
`memoryview` access, progress or file I/O. This module deliberately excludes
application-specific coupling, infiltration and project-save ownership.
