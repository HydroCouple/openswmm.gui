/*!
 * \file   boundaryconditioning.h
 * \license GPL-3.0-or-later
 *
 * Condition a meshing region (domain minus building holes) to the minimum
 * cell size before it reaches the triangulation, so footprint detail that
 * adds no shape cannot force tiny cells (MESH_REGIONAL_TRIQUAD_PLAN D-R3):
 *
 *  1. Closing by g/2 (dilate, then erode): holes narrower than g vanish and
 *     building protrusions thinner than g are cut back.
 *  2. Opening by g/2 (erode, then dilate): passages narrower than g between
 *     buildings, or between a building and the boundary, close up.
 *  3. Topology-preserving simplification: vertices within the simplify
 *     tolerance of the simplified outline are dropped.
 *  4. Short-edge collapse: vertices closer than g to the previous kept
 *     vertex go (buffering leaves short bevel and junction segments), and
 *     the result is made valid if that introduced a defect.
 *
 * Buffers use mitred joins, so right-angled building corners stay square.
 * Large regions are conditioned as parallel tiles with a 3-gap margin.
 */
#ifndef OPENSWMMVIS_MESH_BOUNDARYCONDITIONING_H
#define OPENSWMMVIS_MESH_BOUNDARYCONDITIONING_H

#include <QtGlobal>

class OGRGeometry;

namespace mesh {

struct BoundaryConditionReport
{
    int    polygonsIn = 0, polygonsOut = 0;
    int    holesIn = 0, holesOut = 0;
    qint64 verticesIn = 0, verticesOut = 0;
    double areaIn = 0.0, areaOut = 0.0;
    qint64 shortEdgesIn = 0, shortEdgesOut = 0;   ///< Ring edges shorter than the gap.
    qint64 milliseconds = 0;
    int    tiles = 1;   ///< Parallel tiles used (1 = conditioned whole).
};

/*! \brief Close, open and simplify \p region at gap \p gap.
 *  \returns a new (caller-owned) polygon or multipolygon, or nullptr when
 *  \p gap <= 0, the input is not polygonal, or GEOS fails; the caller then
 *  keeps the original geometry. */
[[nodiscard]] OGRGeometry *conditionMeshRegion(const OGRGeometry *region, double gap,
                                               double simplifyTolerance,
                                               BoundaryConditionReport *report = nullptr);

/*! Polygon, hole and vertex counts plus area of a (multi)polygon. */
void measurePolygonal(const OGRGeometry *g, int *polygons, int *holes,
                      qint64 *vertices, double *area,
                      double shortEdge = 0.0, qint64 *shortEdges = nullptr);

} // namespace mesh

#endif // OPENSWMMVIS_MESH_BOUNDARYCONDITIONING_H
