/*!
 * \file   polylineoffset.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Polyline offset algorithm (Slice Z.5b).
 */

#include "render/polylineoffset.h"

#include <QPointF>

#include <cmath>
#include <optional>

namespace OpenSWMM::Render
{

namespace {

/*! Unit-length perpendicular to segment (a → b), pointing to the
 *  **visually-right** side of the forward direction in Qt image
 *  coordinates (where y increases downward).
 *
 *  Derivation: forward (1, 0) → right of forward = visually-below =
 *  positive y = (0, 1). That requires the transform
 *  `(dx, dy) ↦ (-dy, dx)`. (Equivalent to a 90° counter-clockwise
 *  rotation in math terms — but because the y axis is flipped, the
 *  visual result is a clockwise rotation, which matches the spec
 *  contract that positive offsetPx goes right of forward.)
 *
 *  Returns nullopt when the segment has zero length. */
std::optional<QPointF> rightNormal(const QPointF &a, const QPointF &b)
{
    const qreal dx = b.x() - a.x();
    const qreal dy = b.y() - a.y();
    const qreal len = std::hypot(dx, dy);
    if (len <= 1e-9) return std::nullopt;
    return QPointF(-dy / len, dx / len);
}

/*! Intersect two infinite lines, each defined by a point + direction.
 *  Returns nullopt when the directions are parallel (cross ≈ 0). */
std::optional<QPointF> lineIntersection(const QPointF &p1, const QPointF &d1,
                                         const QPointF &p2, const QPointF &d2)
{
    const qreal cross = d1.x() * d2.y() - d1.y() * d2.x();
    if (std::abs(cross) <= 1e-12) return std::nullopt;
    const qreal dx = p2.x() - p1.x();
    const qreal dy = p2.y() - p1.y();
    const qreal t  = (dx * d2.y() - dy * d2.x()) / cross;
    return QPointF(p1.x() + t * d1.x(), p1.y() + t * d1.y());
}

} // namespace

QPolygonF offsetPolyline(const QPolygonF &input,
                          qreal offsetPx,
                          qreal miterLimit)
{
    const int n = input.size();
    if (n < 2 || qFuzzyIsNull(offsetPx))
        return input;

    QPolygonF out;
    out.reserve(n + 4);   // bevel joins can add a few extra vertices

    // Pre-compute right-normal for every segment that is non-degenerate.
    // We'll re-use them at join time.
    QVector<std::optional<QPointF>> normals;
    normals.reserve(n - 1);
    for (int i = 0; i < n - 1; ++i)
        normals.append(rightNormal(input[i], input[i + 1]));

    // Start endpoint: shift by the first valid segment normal.
    // Walk forward to find one — handles leading-coincident-vertices.
    {
        int firstValid = 0;
        while (firstValid < normals.size() && !normals[firstValid].has_value())
            ++firstValid;
        if (firstValid >= normals.size())
            return input;  // all-coincident — nothing to offset
        const QPointF nrm = normals[firstValid].value();
        out.append(QPointF(input[0].x() + offsetPx * nrm.x(),
                           input[0].y() + offsetPx * nrm.y()));
    }

    // Interior vertices: miter / bevel join.
    for (int i = 1; i < n - 1; ++i) {
        const auto nrmA = normals[i - 1];   // segment ending at vertex i
        const auto nrmB = normals[i];       // segment starting at vertex i

        if (!nrmA.has_value() && !nrmB.has_value())
            continue;  // both degenerate; skip this vertex
        if (!nrmA.has_value()) {
            // Only the outgoing segment is real — emit its shifted start.
            out.append(QPointF(input[i].x() + offsetPx * nrmB->x(),
                               input[i].y() + offsetPx * nrmB->y()));
            continue;
        }
        if (!nrmB.has_value()) {
            // Only the incoming segment is real — emit its shifted end.
            out.append(QPointF(input[i].x() + offsetPx * nrmA->x(),
                               input[i].y() + offsetPx * nrmA->y()));
            continue;
        }

        // Both segments are real. Try miter.
        const QPointF shiftedA(input[i].x() + offsetPx * nrmA->x(),
                               input[i].y() + offsetPx * nrmA->y());
        const QPointF shiftedB(input[i].x() + offsetPx * nrmB->x(),
                               input[i].y() + offsetPx * nrmB->y());

        // Direction along segment A: (input[i] - input[i-1]) normalised.
        // We can recover it from the right-normal: (nrmA.y, -nrmA.x).
        const QPointF dirA(nrmA->y(), -nrmA->x());
        const QPointF dirB(nrmB->y(), -nrmB->x());

        const auto miter = lineIntersection(shiftedA, dirA, shiftedB, dirB);
        bool useBevel = !miter.has_value();
        if (miter.has_value()) {
            const qreal dx = miter->x() - input[i].x();
            const qreal dy = miter->y() - input[i].y();
            const qreal miterLen = std::hypot(dx, dy);
            // Reject when miter is too long. Use the absolute value of
            // offsetPx so the limit is direction-agnostic.
            if (miterLen > miterLimit * std::abs(offsetPx))
                useBevel = true;
        }

        if (useBevel) {
            // Bevel: emit the two shifted endpoints; the resulting
            // polyline gains one extra vertex at this corner. When the
            // two segments are collinear (parallel + coincident after
            // offset) shiftedA and shiftedB land on the same point —
            // emit only one to avoid a duplicate vertex.
            const qreal ddx = shiftedB.x() - shiftedA.x();
            const qreal ddy = shiftedB.y() - shiftedA.y();
            if (std::hypot(ddx, ddy) <= 1e-9) {
                out.append(shiftedA);
            } else {
                out.append(shiftedA);
                out.append(shiftedB);
            }
        } else {
            out.append(*miter);
        }
    }

    // End endpoint: shift by the last valid segment normal (walk
    // backward through normals for leading-coincident trailing).
    {
        int lastValid = normals.size() - 1;
        while (lastValid >= 0 && !normals[lastValid].has_value())
            --lastValid;
        if (lastValid >= 0) {
            const QPointF nrm = normals[lastValid].value();
            out.append(QPointF(input[n - 1].x() + offsetPx * nrm.x(),
                               input[n - 1].y() + offsetPx * nrm.y()));
        }
    }

    return out;
}

} // namespace OpenSWMM::Render
