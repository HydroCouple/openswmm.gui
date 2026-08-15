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

/*! \brief Unsigned area of triangle \a tri (|cross| / 2), in project CRS
 *         units². Returns 0 for an out-of-range triangle or one referencing
 *         out-of-range vertices.
 *
 * The single definition of "cell area" in the GUI: the Metadata tab's
 * statistics below and the Attribute Table's Cells column both read it, so a
 * sliver hunted down by sorting the table is the same cell the min/max
 * summary is reporting.
 */
[[nodiscard]] double triangleArea(const MeshResult &mesh, int tri);

/*! \brief Compute cell-area statistics over all triangles in \a mesh.
 *
 * Area is \ref triangleArea. Triangles referencing out-of-range vertex
 * indices are skipped. Median of an even count is the average of the two
 * middle values.
 */
[[nodiscard]] CellAreaStats computeCellAreaStats(const MeshResult &mesh);

} // namespace mesh

#endif // OPENSWMMVIS_MESH_MESHCELLSTATS_H
