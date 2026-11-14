/**
 * @file      qsgmarkergeometry.h
 * @brief     Scene-graph geometry for one vertex marker, drawn the way QPainter's
 *            drawMarkerShape() draws it (shape, fill, outline), so the GPU and the
 *            QPainter paths of the 2D mesh and 2D output layers look alike.
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef OPENSWMMVIS_RENDER_QSGMARKERGEOMETRY_H
#define OPENSWMMVIS_RENDER_QSGMARKERGEOMETRY_H

#include "render/qsgpremultiply.h"

#include <QColor>
#include <QPointF>
#include <QSGGeometry>
#include <QVarLengthArray>

#include <cmath>
#include <vector>

namespace OpenSWMM::Render
{

/**
 * @brief Append one marker: the fill as a triangle fan, then (when \p outlineHalfWidth > 0 and
 *   the outline is not fully transparent) the outline centered on the shape's edges, on top.
 * @param[in,out] out Per-vertex-color triangle list (premultiplied colors).
 * @param[in] cx,cy Center in the caller's scene units.
 * @param[in] r Half the marker's bounding-box side, in scene units.
 * @param[in] shape 0 Circle (an octagon), 1 Square, 2 Triangle (pointing right, as
 *   drawMarkerShape), 3 Diamond; any other value draws a square.
 * @param[in] fill,outline Straight-alpha colors (opacity already applied).
 * @param[in] outlineHalfWidth Half the outline width in scene units.
 */
inline void appendMarkerGeometry(std::vector<QSGGeometry::ColoredPoint2D> &out, float cx, float cy,
                                 float r, int shape, const QColor &fill, const QColor &outline,
                                 float outlineHalfWidth)
{
    QVarLengthArray<QPointF, 8> corner;
    if (shape == 0) {
        for (int k = 0; k < 8; ++k) {
            const double a = 2.0 * M_PI * k / 8.0;
            corner.append(QPointF(cx + r * std::cos(a), cy + r * std::sin(a)));
        }
    } else if (shape == 2) {
        corner = {QPointF(cx + r, cy), QPointF(cx - r, cy - r), QPointF(cx - r, cy + r)};
    } else if (shape == 3) {
        corner = {QPointF(cx, cy - r), QPointF(cx + r, cy), QPointF(cx, cy + r), QPointF(cx - r, cy)};
    } else {
        corner = {QPointF(cx - r, cy - r), QPointF(cx + r, cy - r), QPointF(cx + r, cy + r),
                  QPointF(cx - r, cy + r)};
    }
    const auto vertex = [](double x, double y, const QColor &c) {
        const quint8 a = quint8(c.alpha());
        QSGGeometry::ColoredPoint2D v;
        v.set(float(x), float(y), premul(quint8(c.red()), a), premul(quint8(c.green()), a),
              premul(quint8(c.blue()), a), a);
        return v;
    };
    for (int k = 1; k + 1 < corner.size(); ++k) {
        out.push_back(vertex(corner[0].x(), corner[0].y(), fill));
        out.push_back(vertex(corner[k].x(), corner[k].y(), fill));
        out.push_back(vertex(corner[k + 1].x(), corner[k + 1].y(), fill));
    }
    if (outlineHalfWidth <= 0.f || outline.alpha() == 0)
        return;
    for (int k = 0; k < corner.size(); ++k) {
        const QPointF a = corner[k], b = corner[(k + 1) % corner.size()];
        const double dx = b.x() - a.x(), dy = b.y() - a.y();
        const double len = std::sqrt(dx * dx + dy * dy);
        if (len < 1e-12)
            continue;
        // Extended by the half-width at both ends so the corners close.
        const double ux = dx / len * outlineHalfWidth, uy = dy / len * outlineHalfWidth;
        const double nx = -uy, ny = ux;
        const QPointF s(a.x() - ux, a.y() - uy), e(b.x() + ux, b.y() + uy);
        out.push_back(vertex(s.x() + nx, s.y() + ny, outline));
        out.push_back(vertex(e.x() + nx, e.y() + ny, outline));
        out.push_back(vertex(s.x() - nx, s.y() - ny, outline));
        out.push_back(vertex(e.x() + nx, e.y() + ny, outline));
        out.push_back(vertex(e.x() - nx, e.y() - ny, outline));
        out.push_back(vertex(s.x() - nx, s.y() - ny, outline));
    }
}

} // namespace OpenSWMM::Render

#endif // OPENSWMMVIS_RENDER_QSGMARKERGEOMETRY_H
