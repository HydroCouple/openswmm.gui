// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Caleb Buahin.
#ifndef OPENSWMMVIS_SUBCATCHMENTGEOMETRY_H
#define OPENSWMMVIS_SUBCATCHMENTGEOMETRY_H

#include <QPainterPath>
#include <QPointF>
#include <QVector>

#include <algorithm>

namespace OpenSWMM::Render::SubcatchmentGeometry
{
// SWMM exposes a flat vertex list. Explicit returns to a ring's first point
// delimit closed rings; the last ring may be open, as in ordinary INP files.
struct RingRange {
    int first;
    int count; // excludes the repeated closing vertex
};

inline QVector<RingRange> rings(const QVector<QPointF> &points)
{
    QVector<RingRange> result;
    int first = 0;
    for (int i = 1; i < points.size(); ++i) {
        if (i - first >= 3 && points[i] == points[first]) {
            result.append({first, i - first});
            first = i + 1;
        }
    }
    if (points.size() - first >= 3)
        result.append({first, int(points.size()) - first});
    return result;
}

inline QPainterPath path(const QVector<QPointF> &points)
{
    QPainterPath result;
    result.setFillRule(Qt::OddEvenFill);
    for (const auto &ring : rings(points)) {
        result.moveTo(points[ring.first]);
        for (int j = 1; j < ring.count; ++j)
            result.lineTo(points[ring.first + j]);
        result.closeSubpath();
    }
    return result;
}

inline bool contains(const QVector<QPointF> &points, const QPointF &point)
{
    bool inside = false;
    for (const auto &ring : rings(points)) {
        for (int i = 0, j = ring.count - 1; i < ring.count; j = i++) {
            const auto &a = points[ring.first + i], &b = points[ring.first + j];
            if ((a.y() > point.y()) != (b.y() > point.y()) &&
                point.x() < (b.x() - a.x()) * (point.y() - a.y()) / (b.y() - a.y()) + a.x())
                inside = !inside;
        }
    }
    return inside;
}

// Tessellate simple rings using horizontal slabs. Ring vertices are the slab
// boundaries, so edge order cannot change inside a slab. Pair crossings with
// the same even/odd rule used by QPainter, preserving holes and disjoint parts.
// New edge-intersection vertices are returned as triangle triples. O(n² log n) in the worst case,
// cached by the renderer's geometry revision, like the previous ear clipper.
inline QVector<QPointF> triangulate(const QVector<QPointF> &points)
{
    struct Edge {
        QPointF a, b;
        double x(double y) const { return a.x() + (y - a.y()) / (b.y() - a.y()) * (b.x() - a.x()); }
    };
    QVector<Edge> edges;
    QVector<double> levels;
    for (const auto &ring : rings(points)) {
        for (int j = 0; j < ring.count; ++j) {
            const auto &a = points[ring.first + j];
            const auto &b = points[ring.first + (j + 1) % ring.count];
            levels.append(a.y());
            if (a.y() != b.y())
                edges.append({a, b});
        }
    }
    std::sort(levels.begin(), levels.end());
    levels.erase(std::unique(levels.begin(), levels.end()), levels.end());
    QVector<QPointF> result;
    QVector<const Edge *> active;
    for (int i = 1; i < levels.size(); ++i) {
        const double lo = levels[i - 1], hi = levels[i];
        const double mid = lo + (hi - lo) * 0.5;
        active.clear();
        for (const auto &edge : edges)
            if (mid > std::min(edge.a.y(), edge.b.y()) && mid < std::max(edge.a.y(), edge.b.y()))
                active.append(&edge);
        std::sort(active.begin(), active.end(),
                  [mid](const Edge *a, const Edge *b) { return a->x(mid) < b->x(mid); });
        for (int j = 1; j < active.size(); j += 2) {
            const Edge &left = *active[j - 1], &right = *active[j];
            const QPointF a(left.x(lo), lo), b(right.x(lo), lo);
            const QPointF c(right.x(hi), hi), d(left.x(hi), hi);
            if (a != b)
                result << a << b << c;
            if (c != d)
                result << a << c << d;
        }
    }
    return result;
}
} // namespace OpenSWMM::Render::SubcatchmentGeometry
#endif
