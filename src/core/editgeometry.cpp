/*!
 * \file   editgeometry.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date 2026
 */

#include "core/editgeometry.h"

#include <cmath>
#include <limits>

namespace EditGeometry
{

double polylineLength(const QVector<QPointF> &vertices)
{
    if (vertices.size() < 2)
        return 0.0;

    double total = 0.0;
    for (int i = 1; i < vertices.size(); ++i)
    {
        const double dx = vertices[i].x() - vertices[i - 1].x();
        const double dy = vertices[i].y() - vertices[i - 1].y();
        total += std::hypot(dx, dy);
    }
    return total;
}

QVector<QPointF> replacedAt(const QVector<QPointF> &vertices,
                            int index,
                            const QPointF &newPt)
{
    QVector<QPointF> out = vertices;
    if (index >= 0 && index < out.size())
        out[index] = newPt;
    return out;
}

QVector<QPointF> insertedAt(const QVector<QPointF> &vertices,
                            int index,
                            const QPointF &newPt)
{
    QVector<QPointF> out = vertices;
    // QVector::size() returns qsizetype on Qt 6.10+, which can't mix with
    // int in std::clamp without explicit conversion.
    const qsizetype clamped = std::clamp<qsizetype>(static_cast<qsizetype>(index),
                                                    qsizetype(0), out.size());
    out.insert(clamped, newPt);
    return out;
}

QVector<QPointF> removedAt(const QVector<QPointF> &vertices, int index)
{
    if (index < 0 || index >= vertices.size())
        return vertices;
    if (vertices.size() <= 2)
        return vertices;

    QVector<QPointF> out = vertices;
    out.removeAt(index);
    return out;
}

double distanceToPolyline(const QVector<QPointF> &vertices,
                          const QPointF &point,
                          int *segmentIndex,
                          QPointF *closestPoint)
{
    if (vertices.size() < 2)
    {
        if (segmentIndex) *segmentIndex = -1;
        if (closestPoint) *closestPoint = {};
        return std::numeric_limits<double>::infinity();
    }

    double best = std::numeric_limits<double>::infinity();
    int bestSeg = 0;
    QPointF bestPt = vertices.first();

    for (int i = 1; i < vertices.size(); ++i)
    {
        const QPointF &a = vertices[i - 1];
        const QPointF &b = vertices[i];
        const double dx = b.x() - a.x();
        const double dy = b.y() - a.y();
        const double len2 = dx * dx + dy * dy;

        QPointF proj;
        if (len2 <= 0.0)
        {
            proj = a;
        }
        else
        {
            const double t = std::clamp(
                ((point.x() - a.x()) * dx + (point.y() - a.y()) * dy) / len2,
                0.0, 1.0);
            proj = QPointF(a.x() + t * dx, a.y() + t * dy);
        }

        const double pdx = point.x() - proj.x();
        const double pdy = point.y() - proj.y();
        const double d = std::hypot(pdx, pdy);
        if (d < best)
        {
            best = d;
            bestSeg = i - 1;
            bestPt = proj;
        }
    }

    if (segmentIndex) *segmentIndex = bestSeg;
    if (closestPoint) *closestPoint = bestPt;
    return best;
}

double polygonArea(const QVector<QPointF> &polygon)
{
    const int n = polygon.size();
    if (n < 3)
        return 0.0;

    // Shoelace formula. Handles open and closed polygons:
    // if first == last the extra degenerate edge contributes zero.
    double area = 0.0;
    for (int i = 0; i < n; ++i)
    {
        const QPointF &a = polygon[i];
        const QPointF &b = polygon[(i + 1) % n];
        area += a.x() * b.y() - b.x() * a.y();
    }
    return std::abs(area) * 0.5;
}

} // namespace EditGeometry
