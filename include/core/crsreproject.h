/*!
 * \file   crsreproject.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * Phase 0.7 — coordinate reprojection of an open SWMM model.
 *
 * Iterates every node, link vertex chain, and subcatchment polygon and
 * rewrites the stored coordinates via OGR. Sets the model's CRS string
 * to the new authority. The caller is expected to mark the project dirty
 * so the change persists on next save.
 */
#ifndef CRSREPROJECT_H
#define CRSREPROJECT_H

#include <QString>

class SpatialReferenceSystem;
class OGRCoordinateTransformation;

#include <openswmm/engine/openswmm_engine.h>

namespace CRSReproject
{
    struct Stats {
        int nodes        = 0;
        int linkVertices = 0;
        int polygonVerts = 0;
    };

    /*!
     * \brief Transform every coordinate in the model from \p oldSRS to \p newSRS.
     *
     * Uses the bulk node API for performance; loops per-link and per-subcatch
     * for variable-length chains. Best-effort: a per-object failure logs and
     * continues so a partially-projectable model still gets the rest done.
     *
     * \param engine  Open SWMM engine handle (must outlive the call).
     * \param oldSRS  Source CRS (e.g. layer's stored CRS).
     * \param newSRS  Target CRS (also written to the engine via swmm_spatial_set_crs).
     * \param stats   Optional out-param with counts of objects touched.
     * \param errorOut Optional human-readable error if the transform itself fails.
     * \return true if the transform was built and applied to ≥ 1 object;
     *         false on hard failure (no transform, no nodes touched).
     */
    bool reprojectModel(SWMM_Engine engine,
                        const SpatialReferenceSystem &oldSRS,
                        const SpatialReferenceSystem &newSRS,
                        Stats *stats = nullptr,
                        QString *errorOut = nullptr);

    /*! \brief Number of points pushed through OGR per batched call.
     *
     *  Big enough that PROJ's per-call overhead is amortised, small enough
     *  that the scratch buffers stay cache-resident and the peak allocation is
     *  bounded regardless of model size. */
    inline constexpr int kTransformChunk = 65536;

    /*!
     * \brief Transform \p n interleaved scene points in place, in chunks.
     *
     * Replaces the `Transform(1, &x, &y)`-per-point pattern that made the
     * geometry cache do ~1.5M individual PROJ calls on a 100k-node model.
     * One call per \ref kTransformChunk points instead.
     *
     * \param ct  Transform to apply. **Null is a valid no-op** — a layer whose
     *            CRS already matches the canvas has no transform, and every
     *            caller relied on that.
     * \param xs,ys  Parallel arrays of \p n coordinates, rewritten in place.
     *
     * \note Per-point failure semantics are preserved deliberately. The
     *       per-point code left an unprojectable coordinate at its INPUT value,
     *       and real models carry such points (a projection failure left ten
     *       junctions ~4e7 units out in WW-2024). Silently changing where those
     *       land — under a performance banner — would move geometry. So this
     *       passes OGR the `pabSuccess` array and restores the original value
     *       for any point OGR rejects, rather than keeping whatever OGR wrote.
     */
    void transformPointsInPlace(OGRCoordinateTransformation *ct,
                                double *xs, double *ys, int n);
}

#endif // CRSREPROJECT_H
