/*!
 * \file   meshsubmap.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * Grid-based submapping of a rectilinear ring (QUAD_MESHING_REDESIGN_PLAN
 * §4.3, reduced scope): local frame from the length-weighted mean edge
 * direction (angle doubling trick on 4φ), axis snapping of every edge run,
 * grid bands between the distinct local coordinates split at spacing h, and
 * one rectangle per grid cell whose centre lies inside the ring.
 */
#include "mesh/meshsubmap.h"

#include "mesh/meshquadregion.h"

#include <QHash>

#include <algorithm>
#include <cmath>

namespace mesh {

namespace {

constexpr double kDeg = 180.0 / M_PI;

/*! Fold an angular deviation (degrees) into (-45, 45]. */
double foldDeviation(double deg) noexcept
{
    double d = std::fmod(deg, 90.0);
    if (d <= -45.0) d += 90.0;
    if (d > 45.0)   d -= 90.0;
    return d;
}

/*! Frame angle (degrees, in [0, 90)) from the length-weighted mean of 4φ.
 *  Returns false when the ring has no dominant direction. */
bool frameAngle(const QPolygonF &ring, double *deg)
{
    const int n = ring.size();
    double sx = 0.0, sy = 0.0;
    for (int i = 0; i < n; ++i)
    {
        const QPointF d = ring[(i + 1) % n] - ring[i];
        const double len = std::hypot(d.x(), d.y());
        if (!(len > 0.0)) continue;
        const double phi = std::atan2(d.y(), d.x());
        sx += len * std::cos(4.0 * phi);
        sy += len * std::sin(4.0 * phi);
    }
    if (std::hypot(sx, sy) < 1e-12) return false;
    double a = std::atan2(sy, sx) / 4.0 * kDeg;   // (-45, 45]
    if (a < 0.0) a += 90.0;
    if (a >= 90.0) a -= 90.0;
    if (deg) *deg = a;
    return true;
}

/*! Merge sorted values closer than \p eps. */
QVector<double> distinctSorted(QVector<double> v, double eps)
{
    std::sort(v.begin(), v.end());
    QVector<double> out;
    for (double x : v)
        if (out.isEmpty() || x - out.last() > eps) out.append(x);
    return out;
}

} // namespace

bool ringIsRectilinear(const QPolygonF &ring, double tolDeg, double *frameAngleDeg)
{
    if (frameAngleDeg) *frameAngleDeg = 0.0;
    const QPolygonF rn = normalizeRingCCW(ring);
    const int n = rn.size();
    if (n < 3) return false;

    double frame = 0.0;
    if (!frameAngle(rn, &frame)) return false;
    if (frameAngleDeg) *frameAngleDeg = frame;

    for (int i = 0; i < n; ++i)
    {
        const QPointF d = rn[(i + 1) % n] - rn[i];
        const double len = std::hypot(d.x(), d.y());
        if (!(len > 0.0)) continue;
        const double phi = std::atan2(d.y(), d.x()) * kDeg;
        if (std::abs(foldDeviation(phi - frame)) > tolDeg) return false;
    }
    return true;
}

PatchMesh makeSubmappedPatch(const QPolygonF &ring, double h, const QString &tag,
                             QString *err, double tolDeg)
{
    PatchMesh pm;
    if (err) err->clear();
    auto fail = [&](const QString &msg) { if (err) *err = msg; return PatchMesh(); };

    if (!(h > 0.0) || !std::isfinite(h)) return fail(QStringLiteral("spacing must be > 0"));
    const QPolygonF rn = normalizeRingCCW(ring);
    const int n = rn.size();
    if (n < 4 || !(ringSignedArea(rn) > 0.0)) return fail(QStringLiteral("degenerate ring"));
    double frame = 0.0;
    if (!ringIsRectilinear(rn, tolDeg, &frame)) return fail(QStringLiteral("not rectilinear"));

    // ── Rotate into the frame (about the first vertex for precision) ─────
    const QPointF origin = rn[0];
    const double ca = std::cos(-frame / kDeg), sa = std::sin(-frame / kDeg);
    QPolygonF loc(n);
    for (int i = 0; i < n; ++i)
    {
        const QPointF d = rn[i] - origin;
        loc[i] = QPointF(ca * d.x() - sa * d.y(), sa * d.x() + ca * d.y());
    }

    // ── Snap: every run of same-orientation edges becomes exactly axis-parallel
    QVector<bool> horizontal(n);
    for (int i = 0; i < n; ++i)
    {
        const QPointF d = loc[(i + 1) % n] - loc[i];
        horizontal[i] = std::abs(d.y()) <= std::abs(d.x());
    }
    // A positive-area ring has both orientations, so every run has a start
    // (an edge whose predecessor has the other orientation).
    for (int orient = 0; orient < 2; ++orient)
    {
        const bool wantHorizontal = orient == 0;
        for (int s = 0; s < n; ++s)
        {
            if (horizontal[s] != wantHorizontal || horizontal[(s + n - 1) % n] == wantHorizontal)
                continue;
            QVector<int> verts;   // vertices of the run: s, s+1, ..., end
            verts.append(s);
            int e = s;
            while (horizontal[e] == wantHorizontal)
            {
                verts.append((e + 1) % n);
                e = (e + 1) % n;
                if (e == s) break;
            }
            double mean = 0.0;
            for (int v : verts) mean += wantHorizontal ? loc[v].y() : loc[v].x();
            mean /= verts.size();
            for (int v : verts)
            {
                if (wantHorizontal) loc[v].setY(mean);
                else                loc[v].setX(mean);
            }
        }
    }
    if (!(ringSignedArea(loc) > 0.0)) return fail(QStringLiteral("degenerate ring"));

    // ── Grid lines: distinct local coordinates, bands split at h ─────────
    double minX = loc[0].x(), maxX = minX, minY = loc[0].y(), maxY = minY;
    QVector<double> xs, ys;
    for (const QPointF &p : loc)
    {
        xs.append(p.x()); ys.append(p.y());
        minX = std::min(minX, p.x()); maxX = std::max(maxX, p.x());
        minY = std::min(minY, p.y()); maxY = std::max(maxY, p.y());
    }
    const double span = std::max(maxX - minX, maxY - minY);
    if (!(span > 0.0)) return fail(QStringLiteral("degenerate ring"));
    xs = distinctSorted(xs, 1e-9 * span);
    ys = distinctSorted(ys, 1e-9 * span);
    if (xs.size() < 2 || ys.size() < 2) return fail(QStringLiteral("degenerate ring"));

    auto subdivide = [h](const QVector<double> &bands) {
        QVector<double> lines;
        lines.append(bands[0]);
        for (int i = 1; i < bands.size(); ++i)
        {
            const double a = bands[i - 1], b = bands[i];
            const int parts = std::max(1, int(std::lround((b - a) / h)));
            for (int k = 1; k <= parts; ++k) lines.append(a + (b - a) * (double(k) / parts));
        }
        return lines;
    };
    const QVector<double> gx = subdivide(xs), gy = subdivide(ys);
    const int nx = gx.size(), ny = gy.size();
    if (double(nx - 1) * double(ny - 1) > 5e6)
        return fail(QStringLiteral("spacing %1 would produce more than 5e6 cells").arg(h));

    // ── Cells whose centre lies inside the ring ──────────────────────────
    QHash<qint64, int> nodeIndex;
    auto node = [&](int i, int j) {
        const qint64 key = (qint64(i) << 32) | qint64(quint32(j));
        auto it = nodeIndex.find(key);
        if (it != nodeIndex.end()) return it.value();
        const int idx = pm.xy.size();
        nodeIndex.insert(key, idx);
        // rotate back: R(+frame) · local + origin
        const double x = gx[i], y = gy[j];
        pm.xy.append(QPointF(ca * x + sa * y, -sa * x + ca * y) + origin);
        return idx;
    };

    QVector<QPair<int, int>> edgeOrder;
    QHash<QPair<int, int>, int> edgeCount;
    auto countEdge = [&](int a, int b) {
        const QPair<int, int> k = a < b ? qMakePair(a, b) : qMakePair(b, a);
        auto it = edgeCount.find(k);
        if (it == edgeCount.end()) { edgeCount.insert(k, 1); edgeOrder.append(k); }
        else ++it.value();
    };

    for (int j = 0; j < ny - 1; ++j)
        for (int i = 0; i < nx - 1; ++i)
        {
            const QPointF centre(0.5 * (gx[i] + gx[i + 1]), 0.5 * (gy[j] + gy[j + 1]));
            if (!pointInRing(loc, centre)) continue;
            MeshTriangle q;
            q.tag = tag;
            q.v0 = node(i, j); q.v1 = node(i + 1, j); q.v2 = node(i + 1, j + 1); q.v3 = node(i, j + 1);
            pm.quads.append(q);
            countEdge(q.v0, q.v1); countEdge(q.v1, q.v2); countEdge(q.v2, q.v3); countEdge(q.v3, q.v0);
        }
    if (pm.quads.isEmpty()) return fail(QStringLiteral("degenerate ring"));

    for (const QPair<int, int> &k : edgeOrder)
        if (edgeCount.value(k) == 1) pm.boundarySegments.append(k);
    pm.tag = tag;

    const QString bad = validate(pm);
    if (!bad.isEmpty()) return fail(bad);
    return pm;
}

} // namespace mesh
