/**
 * @file      resultscalargeometry.h
 * @brief     Reconstruction of scalar display geometry from valid mesh cells.
 *
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

#pragma once
#include "io/mesh2dscalarframe.h"
#include "contour/marchingtriangles.h"
#include <QFont>
#include <QRectF>
#include <QTransform>
#include <QVector>
#include <array>
namespace OpenSWMM::Render
{
struct ResultScalarLabel {
    QPointF point;
    double value = 0;
    int cell = -1;
};
struct ResultScalarGeometry {
    std::vector<OpenSWMM::Contour::IsoLineSegment> contours;
    QVector<ResultScalarLabel> labels;
    QString error;
    /** @brief Estimate payload bytes occupied by active contour segments and cell labels.
     * @return Size-based count excluding vector capacity, error text and allocator overhead. */
    size_t bytes() const
    {
        return contours.size() * sizeof(OpenSWMM::Contour::IsoLineSegment) +
               size_t(labels.size()) * sizeof(ResultScalarLabel);
    }
};
// Area-weighted nodal display reconstruction from valid cells only. Invalid
// cells are excluded entirely; this does not replace native cell values.
/** @brief Reconstruct display contours and optional native-value cell-centroid labels.
 * @param vertices Borrowed scene positions; accepted cells require valid indices
 * and finite coordinates. Geometry/values remain unchanged.
 * @param cells Native triangle/quad vertex IDs; a negative fourth slot denotes a
 * triangle. Cells with non-Valid status or nonfinite scalar values are excluded
 * before geometry checks; accepted polygons require finite nonzero signed area.
 * @param triangles Display triangle vertex IDs for contouring, with one owner each.
 * @param triangleCells Native cell index per display triangle; all owners are
 * checked when levels are nonempty, even if their cells are otherwise excluded.
 * @param frame Scalar values and statuses, each exactly cells.size(). A nonempty
 * frame.error is propagated before geometry work; descriptor identity is not checked.
 * @param levels At most 256 finite contour levels, passed in supplied order to
 * marchingTriangles(). Empty skips nodal accumulation, display-triangle validation
 * and contour work, while still checking accepted cells for optional labels.
 * @param cellLabels Include one area-centroid label with the original scalar value
 * and native cell index per accepted cell. Concave polygon centroids can lie outside it.
 * @return Owned display geometry. Contours interpolate area-weighted nodal averages
 * from accepted cells only; original cell data is never replaced. Any reported
 * dimension/geometry/level/work/segment-limit failure returns an error with both
 * output arrays cleared. Work is capped at 32 million triangle-level pairs and
 * retained output at 1.5 million segments. Allocations can still throw; no
 * cancellation hook or automatic label placement is provided. */
ResultScalarGeometry buildResultScalarGeometry(const QVector<QPointF> &vertices,
                                               const std::vector<std::array<int, 4>> &cells,
                                               const std::vector<std::array<int, 3>> &triangles,
                                               const std::vector<int> &triangleCells,
                                               const openswmmvis::io::Mesh2DScalarFrame &frame,
                                               const std::vector<double> &levels, bool cellLabels);
struct ResultScalarPlacedLabel {
    QString text;
    QRectF rect;
};
/** @brief Greedily place nonoverlapping screen-space labels without painting them.
 * @param geometry Borrowed display geometry; nonempty error yields no labels.
 * @param sceneToScreen Transform applied to each candidate center. Caller supplies
 * finite geometry and a transform producing finite screen coordinates.
 * @param viewport Screen rectangle that must contain each center and whole text
 * box; a null rectangle disables clipping. A nonnull invalid rectangle is not repaired.
 * @param font Font measured by QFontMetricsF, without changing a painter.
 * @param decimals Fixed decimal precision clamped to [0,12].
 * @param units Suffix inserted as [units]; empty uses the literal units unknown.
 * No numeric unit conversion is performed.
 * @param contourLabels True uses each contour segment's midpoint/level; false
 * uses the stored cell-centroid labels. No contour chaining or line rotation occurs.
 * @param maximum Requested label cap, normalized to at most 512; <=0 yields empty.
 * @return Owned text/rectangle pairs in accepted input order. Boxes add six
 * horizontal/four vertical pixels to font bounds and avoid prior boxes expanded
 * by another six/four pixels. This is greedy decluttering, with no global optimum,
 * cancellation or drawing. Allocation errors propagate. */
QVector<ResultScalarPlacedLabel> placeResultScalarLabels(const ResultScalarGeometry &geometry,
                                                         const QTransform &sceneToScreen,
                                                         const QRectF &viewport, const QFont &font,
                                                         int decimals, const QString &units,
                                                         bool contourLabels, int maximum = 256);
}
