/*!
 * \file   meshgwpreview.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * openswmm.engine/plans/INFILTRATION_TO_2D_AQUIFER_AND_REMAP_PLAN_2026-10-03.md
 * Step 2 — read-only preview, behind "Remap 1D↔2D", of where 1D losses land
 * in the 2D aquifer. Mirrors the engine's resolve rules (the engine stays
 * the source of truth; nothing here is written to the model):
 *   - nodes: point in a cell → node↔aquifer exchange, else outside;
 *   - seeping conduits: share of the polyline length inside the mesh
 *     (the rest is a system loss);
 *   - subcatchments: a lumped [GROUNDWATER] aquifer keeps the infiltration;
 *     otherwise the polygon area inside the mesh recharges the 2D aquifer
 *     (the rest is a system loss).
 * Pure functions over MeshResult — unit-testable.
 */
#ifndef OPENSWMMVIS_MESH_MESHGWPREVIEW_H
#define OPENSWMMVIS_MESH_MESHGWPREVIEW_H

#include "mesh/meshresult.h"

#include <QPair>
#include <QPointF>
#include <QStringList>
#include <QVector>

namespace mesh {

/*! A closed ring of points (no repeated closing point needed). QtCore-only
 *  so the helper links without QtGui. */
using Ring = QVector<QPointF>;

struct GwPreviewInput {
    struct Conduit {
        QString          id;
        QVector<QPointF> path;      //!< node1, [VERTICES], node2
    };
    struct Subcatch {
        QString   id;
        Ring      polygon;          //!< empty when the model has none
        bool      lumpedGw = false; //!< has a [GROUNDWATER] aquifer
    };
    QVector<QPair<QString, QPointF>> nodes;
    QVector<Conduit>                 seepingConduits;
    QVector<Subcatch>                subcatchments;
};

struct GwPreviewResult {
    int         nodesInside = 0;
    QStringList nodesOutside;

    int         conduitsInside = 0;      //!< any length inside the mesh
    double      conduitLengthInside = 0; //!< map units, summed
    double      conduitLengthTotal = 0;
    QStringList conduitsOutside;

    QStringList subcatchLumped;
    int         subcatchTo2D = 0;        //!< any area inside the mesh
    double      subcatchAreaInside = 0;  //!< map units², non-lumped only
    double      subcatchAreaTotal = 0;
    QStringList subcatchOutside;
    QStringList subcatchNoPolygon;
};

/*! Area of the intersection of \p poly (any simple polygon) with the convex
 *  cell \p cell (either winding). Sutherland–Hodgman — exact for a convex
 *  clip polygon. */
double clippedArea(const Ring &poly, const Ring &cell);

/*! Length of the segment a→b inside the convex cell \p cell (either winding). */
double clippedLength(const QPointF &a, const QPointF &b, const Ring &cell);

GwPreviewResult previewGroundwaterMapping(const MeshResult &mesh,
                                          const GwPreviewInput &in);

} // namespace mesh

#endif // OPENSWMMVIS_MESH_MESHGWPREVIEW_H
