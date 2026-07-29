/*!
 * \file   meshcellstats.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * MESH_DECOUPLED_1D2D_REMAP_PLAN Part A — cell-area statistics for the
 * mesh layer's Metadata tab. Free function, no layer dependency, so it
 * is unit-testable against a bare MeshResult.
 */
#ifndef OPENSWMMVIS_MESH_MESHCELLSTATS_H
#define OPENSWMMVIS_MESH_MESHCELLSTATS_H

#include "meshresult.h"

namespace mesh {

/*! \brief Min / max / mean / median of triangle areas (project CRS units²). */
struct CellAreaStats
{
    double min    = 0.0;
    double max    = 0.0;
    double mean   = 0.0;
    double median = 0.0;
    int    count  = 0;    ///< Triangles measured; 0 = stats invalid.
};

/*! \brief Compute cell-area statistics over all triangles in \a mesh.
 *
 * Area is the unsigned triangle area (|cross| / 2). Triangles referencing
 * out-of-range vertex indices are skipped. Median of an even count is the
 * average of the two middle values.
 */
[[nodiscard]] CellAreaStats computeCellAreaStats(const MeshResult &mesh);

} // namespace mesh

#endif // OPENSWMMVIS_MESH_MESHCELLSTATS_H
