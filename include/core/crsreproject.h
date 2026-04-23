/*!
 * \file   crsreproject.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license MIT
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

#ifdef HAVE_OPENSWMMCORE
#include <openswmm/engine/openswmm_engine.h>
#else
typedef void* SWMM_Engine;
#endif

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
}

#endif // CRSREPROJECT_H
