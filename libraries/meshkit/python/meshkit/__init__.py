# SPDX-License-Identifier: Apache-2.0
"""Qt-free mixed meshing with native terrain, read-only buffers and bounded jobs."""
from ._meshkit import MeshResult, capabilities, prepare_sources, read, start, write
__version__ = "0.2.0"

def run(request, *, raster=None, provider_identity="", progress=None, poll_interval=0.1):
    """Run in a native worker; deliver bounded progress batches on the caller thread.

    Inputs accept native float64 (N,2) buffers or point rows. Result arrays are
    owned, read-only strided buffers. NumPy is optional: np.asarray(view) shares
    storage. Polling/cancellation never call Python from a native inner loop.
    """
    job = start(request, raster=raster, provider_identity=provider_identity)
    try:
        while True:
            try:
                result = job.result(max(0.001, poll_interval))
                if progress:
                    for event in job.poll():
                        progress(event)
                return result
            except TimeoutError:
                if progress:
                    for event in job.poll():
                        progress(event)
    except BaseException:
        job.cancel()
        raise


def prepared_request(prepared, *, options=None, metadata=None, **extra):
    """Build a request from an audited source graph without per-vertex Python rows."""
    if not prepared.get("ok"):
        raise ValueError(prepared.get("error", "Source preparation failed"))
    paths = [{key: row[key] for key in ("vertices", "kind", "id", "marker", "closed")}
             for row in prepared["features"]]
    return dict(points=prepared["points"], point_elevations=prepared["point_elevations"], paths=paths, options=options or {},
                metadata=metadata or {}, **extra)
