/*!
 * \file   quadblocks.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * Automatic open-area quad blocks (workplans/MESH_REGIONAL_TRIQUAD_PLAN_2026-10-03.md,
 * phases 2-3). Rectangles are placed where the cells a first triangle pass
 * produced are nearly uniform and no feature runs, then embedded as mapped
 * quad regions in a second pass; the generator's conforming patch seams make
 * the triangle-to-quad joins exact.
 */
#ifndef OPENSWMMVIS_MESH_QUADBLOCKS_H
#define OPENSWMMVIS_MESH_QUADBLOCKS_H

#include "mesh/meshquadregion.h"

#include <QPointF>
#include <QPolygonF>
#include <QRectF>
#include <QVector>

namespace mesh {

struct MeshResult;

/*! \brief Local cell size on a regular grid; NaN marks unusable cells. */
struct QuadBlockGrid
{
    QPointF origin;          ///< Lower-left corner of cell (0, 0).
    double  pitch = 0.0;
    int     cols = 0, rows = 0;
    QVector<float> h;        ///< Row-major, cols x rows.

    [[nodiscard]] bool isValid() const { return pitch > 0.0 && cols > 0 && rows > 0 && h.size() == qsizetype(cols) * rows; }
};

struct QuadBlockOptions
{
    double minSpacing = 0.0;    ///< Blocks finer than this are not worth a patch.
    double bandRatio = 1.3;     ///< Size bands are powers of this.
    int    minQuadsPerSide = 4;
    int    maxBlocks = 50000;
};

/*!
 * \brief The local cell size of \p mesh on a grid of \p pitch, with every cell
 *        touched by \p blockers (domain and hole rings, constraint lines,
 *        break lines) or by an existing quad marked unusable.
 */
QuadBlockGrid quadBlockGridFromMesh(const MeshResult &mesh, double pitch,
                                    const QVector<QVector<QPointF>> &blockers);

/*!
 * \brief Axis-aligned rectangles over nearly uniform usable cells, largest
 *        first, each at least minQuadsPerSide quads of its spacing per side,
 *        kept one spacing apart from each other. Each comes back as a Mapped
 *        QuadRegion whose spacing is its band's.
 */
QVector<QuadRegion> placeQuadBlocks(const QuadBlockGrid &grid, const QuadBlockOptions &options);

} // namespace mesh

#endif // OPENSWMMVIS_MESH_QUADBLOCKS_H
