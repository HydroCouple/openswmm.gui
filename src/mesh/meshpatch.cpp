/*!
 * \file   meshpatch.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * Structured quad patches (TRI_QUAD_MESHING_PLAN §3.2, phase G3): transfinite
 * four-sided patches and swept channel patches, in local indices. The
 * polyline-side transfinite overload and makeMappedPatch serve the quad-region
 * redesign (QUAD_MESHING_REDESIGN_PLAN_2026-09-06.md §4.2).
 */
#include "mesh/meshpatch.h"

#include "mesh/meshcellgeom.h"

#include <algorithm>
#include <cmath>

namespace mesh {

namespace {

bool finitePt(const QPointF &p) noexcept
{
    return std::isfinite(p.x()) && std::isfinite(p.y());
}

double cross(const QPointF &a, const QPointF &b) noexcept
{
    return a.x() * b.y() - a.y() * b.x();
}

/*! Signed area of a closed ring (positive = CCW). */
double ringSignedArea(const QVector<QPointF> &ring) noexcept
{
    double s = 0.0;
    const int n = ring.size();
    for (int i = 0; i < n; ++i)
        s += cross(ring[i], ring[(i + 1) % n]);
    return 0.5 * s;
}

/*! Every consecutive turn of the ring has the same strict sign. */
bool ringIsStrictlyConvex(const QVector<QPointF> &ring) noexcept
{
    const int n = ring.size();
    int sign = 0;
    for (int i = 0; i < n; ++i)
    {
        const QPointF &p = ring[i], &q = ring[(i + 1) % n], &r = ring[(i + 2) % n];
        const double c = cross(q - p, r - q);
        const int s = (c > 0.0) ? 1 : (c < 0.0) ? -1 : 0;
        if (s == 0 || (sign != 0 && s != sign)) return false;
        sign = s;
    }
    return true;
}

/*! A polyline parametrised by normalised arc length t in [0,1]. */
struct ArcPolyline
{
    QVector<QPointF> pts;
    QVector<double>  cum;     ///< cum[i] = arc length from pts[0] to pts[i].
    double           length = 0.0;

    explicit ArcPolyline(const QVector<QPointF> &p) : pts(p), cum(p.size(), 0.0)
    {
        for (int i = 1; i < pts.size(); ++i)
            cum[i] = cum[i - 1] + std::hypot(pts[i].x() - pts[i - 1].x(),
                                             pts[i].y() - pts[i - 1].y());
        length = pts.isEmpty() ? 0.0 : cum.last();
    }

    QPointF at(double t) const
    {
        if (t <= 0.0) return pts.first();
        if (t >= 1.0) return pts.last();
        const double s = t * length;
        // First vertex whose cumulative length exceeds s: segment (k-1, k).
        const int k = std::max(1, int(std::upper_bound(cum.begin(), cum.end(), s) - cum.begin()));
        const int kk = std::min(k, int(pts.size()) - 1);
        const double seg = cum[kk] - cum[kk - 1];
        const double f = seg > 0.0 ? (s - cum[kk - 1]) / seg : 0.0;
        return pts[kk - 1] + (pts[kk] - pts[kk - 1]) * f;
    }
};

} // namespace

// ---------------------------------------------------------------------------
// Validation
// ---------------------------------------------------------------------------

QString validate(const StructuredPatch &p)
{
    if (p.corners.size() != 4)
        return QStringLiteral("Structured patch needs exactly 4 corners (got %1).")
            .arg(p.corners.size());
    for (const QPointF &c : p.corners)
        if (!finitePt(c))
            return QStringLiteral("Structured patch has a non-finite corner coordinate.");
    if (p.n < 1 || p.m < 1)
        return QStringLiteral("Structured patch subdivisions must be >= 1 (n=%1, m=%2).")
            .arg(p.n).arg(p.m);
    if (!ringIsStrictlyConvex(p.corners))
        return QStringLiteral("Structured patch corners must form a convex quadrilateral "
                              "(a concave or self-intersecting outline folds the "
                              "transfinite map).");
    return QString();
}

QString validate(const SweptPatch &p)
{
    if (p.centreline.size() < 2)
        return QStringLiteral("Swept patch centreline needs at least 2 points.");
    for (const QPointF &c : p.centreline)
        if (!finitePt(c))
            return QStringLiteral("Swept patch centreline has a non-finite coordinate.");
    if (!(p.width > 0.0) || !std::isfinite(p.width))
        return QStringLiteral("Swept patch width must be > 0.");
    if (p.across < 1)
        return QStringLiteral("Swept patch needs at least 1 quad across.");
    if (p.along < 0.0 || !std::isfinite(p.along))
        return QStringLiteral("Swept patch along-spacing must be >= 0.");
    for (int i = 1; i < p.centreline.size(); ++i)
        if (p.centreline[i] == p.centreline[i - 1])
            return QStringLiteral("Swept patch centreline has a repeated vertex at %1.").arg(i);
    return QString();
}

QString validate(const PatchMesh &pm)
{
    QVector<MeshVertex> verts;
    verts.reserve(pm.xy.size());
    for (const QPointF &p : pm.xy) { MeshVertex v; v.xy = p; verts.append(v); }
    for (int k = 0; k < pm.quads.size(); ++k)
    {
        const MeshTriangle &q = pm.quads[k];
        if (!q.isQuad())
            return QStringLiteral("Patch cell %1 is not a quadrilateral.").arg(k);
        for (int i = 0; i < 4; ++i)
            if (q.vertex(i) < 0 || q.vertex(i) >= verts.size())
                return QStringLiteral("Patch cell %1 references a vertex outside the patch.").arg(k);
        if (!cellIsConvex(verts, q))
            return QStringLiteral("Patch%1 quad %2 is folded or concave.")
                .arg(pm.tag.isEmpty() ? QString() : QStringLiteral(" '%1'").arg(pm.tag))
                .arg(k);
        if (cellSignedArea(verts, q) <= 0.0)
            return QStringLiteral("Patch%1 quad %2 is clockwise or degenerate.")
                .arg(pm.tag.isEmpty() ? QString() : QStringLiteral(" '%1'").arg(pm.tag))
                .arg(k);
    }
    return QString();
}

// ---------------------------------------------------------------------------
// Transfinite (Coons) patch on four straight sides
// ---------------------------------------------------------------------------

PatchMesh makeTransfinitePatch(const StructuredPatch &p, QString *err)
{
    PatchMesh pm;
    const QString msg = validate(p);
    if (!msg.isEmpty()) { if (err) *err = msg; return pm; }

    pm.tag = p.tag;
    const int n = p.n, m = p.m;
    const QPointF &P0 = p.corners[0], &P1 = p.corners[1], &P2 = p.corners[2], &P3 = p.corners[3];

    // Coons interpolation with linear side curves: the two ruled surfaces
    // and the bilinear correction coincide, so the map is bilinear in (u,v).
    pm.xy.reserve((n + 1) * (m + 1));
    for (int j = 0; j <= m; ++j)
    {
        const double v = double(j) / m;
        for (int i = 0; i <= n; ++i)
        {
            const double u = double(i) / n;
            pm.xy.append((1 - u) * (1 - v) * P0 + u * (1 - v) * P1
                         + u * v * P2 + (1 - u) * v * P3);
        }
    }
    auto idx = [n](int i, int j) { return j * (n + 1) + i; };

    const bool ccw = ringSignedArea(p.corners) > 0.0;
    pm.quads.reserve(n * m);
    for (int j = 0; j < m; ++j)
        for (int i = 0; i < n; ++i)
        {
            MeshTriangle q;
            q.tag = p.tag;
            if (ccw) { q.v0 = idx(i, j); q.v1 = idx(i + 1, j); q.v2 = idx(i + 1, j + 1); q.v3 = idx(i, j + 1); }
            else     { q.v0 = idx(i, j); q.v1 = idx(i, j + 1); q.v2 = idx(i + 1, j + 1); q.v3 = idx(i + 1, j); }
            pm.quads.append(q);
        }

    pm.boundarySegments.reserve(2 * (n + m));
    for (int i = 0; i < n; ++i)
    {
        pm.boundarySegments.append(qMakePair(idx(i, 0), idx(i + 1, 0)));
        pm.boundarySegments.append(qMakePair(idx(i, m), idx(i + 1, m)));
    }
    for (int j = 0; j < m; ++j)
    {
        pm.boundarySegments.append(qMakePair(idx(0, j), idx(0, j + 1)));
        pm.boundarySegments.append(qMakePair(idx(n, j), idx(n, j + 1)));
    }

    const QString bad = validate(pm);
    if (!bad.isEmpty()) { if (err) *err = bad; return PatchMesh(); }
    return pm;
}

// ---------------------------------------------------------------------------
// Transfinite (Coons) patch on four polyline sides (redesign plan §4.2)
// ---------------------------------------------------------------------------

PatchMesh makeTransfinitePatch(const QVector<QVector<QPointF>> &sides, int n, int m,
                               const QString &tag, QString *err)
{
    PatchMesh pm;
    auto fail = [&](const QString &msg) { if (err) *err = msg; return PatchMesh(); };

    if (sides.size() != 4)
        return fail(QStringLiteral("Transfinite patch needs exactly 4 sides (got %1).").arg(sides.size()));
    if (n < 1 || m < 1)
        return fail(QStringLiteral("Transfinite patch subdivisions must be >= 1 (n=%1, m=%2).").arg(n).arg(m));
    double scale = 1.0;
    for (int k = 0; k < 4; ++k)
    {
        if (sides[k].size() < 2)
            return fail(QStringLiteral("Transfinite patch side %1 needs at least 2 points.").arg(k));
        for (const QPointF &p : sides[k])
        {
            if (!finitePt(p))
                return fail(QStringLiteral("Transfinite patch side %1 has a non-finite coordinate.").arg(k));
            scale = std::max({scale, std::abs(p.x()), std::abs(p.y())});
        }
    }
    for (int k = 0; k < 4; ++k)
    {
        const QPointF &a = sides[k].last(), &b = sides[(k + 1) % 4].first();
        if (std::abs(a.x() - b.x()) > 1e-9 * scale || std::abs(a.y() - b.y()) > 1e-9 * scale)
            return fail(QStringLiteral("Transfinite patch side %1 does not end where side %2 starts.")
                            .arg(k).arg((k + 1) % 4));
    }

    // Side curves, all arc-length parametrised on [0,1]:
    //   B(u) = sides[0], c0 -> c1        (bottom, v = 0)
    //   R(v) = sides[1], c1 -> c2        (right,  u = 1)
    //   T(u) = sides[2] reversed, c3 -> c2   (top,  v = 1)
    //   L(v) = sides[3] reversed, c0 -> c3   (left, u = 0)
    // so that B(0)=L(0)=c0, B(1)=R(0)=c1, R(1)=T(1)=c2, T(0)=L(1)=c3.
    const ArcPolyline B(sides[0]), R(sides[1]), Tr(sides[2]), Lr(sides[3]);
    const ArcPolyline *arcs[4] = {&B, &R, &Tr, &Lr};
    for (int k = 0; k < 4; ++k)
        if (!(arcs[k]->length > 0.0))
            return fail(QStringLiteral("Transfinite patch side %1 has zero length.").arg(k));
    const QPointF P00 = sides[0].first(), P10 = sides[1].first();
    const QPointF P11 = sides[2].first(), P01 = sides[3].first();
    auto T = [&](double u) { return Tr.at(1.0 - u); };
    auto L = [&](double v) { return Lr.at(1.0 - v); };

    pm.tag = tag;
    pm.xy.reserve((n + 1) * (m + 1));
    for (int j = 0; j <= m; ++j)
    {
        const double v = double(j) / m;
        for (int i = 0; i <= n; ++i)
        {
            const double u = double(i) / n;
            // Boundary vertices sit exactly ON the polylines (the Coons formula
            // reproduces them only up to rounding).
            if (j == 0)      { pm.xy.append(B.at(u)); continue; }
            if (j == m)      { pm.xy.append(T(u));    continue; }
            if (i == 0)      { pm.xy.append(L(v));    continue; }
            if (i == n)      { pm.xy.append(R.at(v)); continue; }
            pm.xy.append((1 - v) * B.at(u) + v * T(u) + (1 - u) * L(v) + u * R.at(v)
                         - ((1 - u) * (1 - v) * P00 + u * (1 - v) * P10
                            + (1 - u) * v * P01 + u * v * P11));
        }
    }
    auto idx = [n](int i, int j) { return j * (n + 1) + i; };

    // Orientation from the full boundary loop (c0 -> c1 -> c2 -> c3).
    QVector<QPointF> loop;
    for (int k = 0; k < 4; ++k)
        for (int i = 0; i + 1 < sides[k].size(); ++i) loop.append(sides[k][i]);
    const bool ccw = ringSignedArea(loop) > 0.0;
    pm.quads.reserve(n * m);
    for (int j = 0; j < m; ++j)
        for (int i = 0; i < n; ++i)
        {
            MeshTriangle q;
            q.tag = tag;
            if (ccw) { q.v0 = idx(i, j); q.v1 = idx(i + 1, j); q.v2 = idx(i + 1, j + 1); q.v3 = idx(i, j + 1); }
            else     { q.v0 = idx(i, j); q.v1 = idx(i, j + 1); q.v2 = idx(i + 1, j + 1); q.v3 = idx(i + 1, j); }
            pm.quads.append(q);
        }

    pm.boundarySegments.reserve(2 * (n + m));
    for (int i = 0; i < n; ++i)
    {
        pm.boundarySegments.append(qMakePair(idx(i, 0), idx(i + 1, 0)));
        pm.boundarySegments.append(qMakePair(idx(i, m), idx(i + 1, m)));
    }
    for (int j = 0; j < m; ++j)
    {
        pm.boundarySegments.append(qMakePair(idx(0, j), idx(0, j + 1)));
        pm.boundarySegments.append(qMakePair(idx(n, j), idx(n, j + 1)));
    }

    const QString bad = validate(pm);
    if (!bad.isEmpty()) return fail(bad);
    return pm;
}

PatchMesh makeMappedPatch(const QPolygonF &ring, const QVector<int> &corners, double h,
                          const QString &tag, QString *err)
{
    auto fail = [&](const QString &msg) { if (err) *err = msg; return PatchMesh(); };
    if (!(h > 0.0) || !std::isfinite(h))
        return fail(QStringLiteral("Mapped patch spacing must be > 0."));
    if (corners.size() != 4)
        return fail(QStringLiteral("Mapped patch needs exactly 4 corners (got %1).").arg(corners.size()));

    // The ring is expected CCW-normalised by the caller (indices refer to it);
    // only a closing duplicate is tolerated here.
    int nr = ring.size();
    if (nr >= 2 && ring.first() == ring.last()) --nr;
    if (nr < 4)
        return fail(QStringLiteral("Mapped patch ring needs at least 4 vertices (got %1).").arg(nr));
    for (int k = 0; k < 4; ++k)
    {
        if (corners[k] < 0 || corners[k] >= nr)
            return fail(QStringLiteral("Mapped patch corner %1 (ring index %2) is out of range.")
                            .arg(k).arg(corners[k]));
        if (k > 0 && corners[k] <= corners[k - 1])
            return fail(QStringLiteral("Mapped patch corners must be strictly increasing ring indices."));
    }

    // Side k walks the ring forward from corners[k] to corners[(k+1)%4].
    QVector<QVector<QPointF>> sides(4);
    double len[4] = {0.0, 0.0, 0.0, 0.0};
    for (int k = 0; k < 4; ++k)
    {
        int i = corners[k];
        const int end = corners[(k + 1) % 4];
        sides[k].append(ring[i]);
        do
        {
            const int nx = (i + 1) % nr;
            len[k] += std::hypot(ring[nx].x() - ring[i].x(), ring[nx].y() - ring[i].y());
            sides[k].append(ring[nx]);
            i = nx;
        } while (i != end);
    }
    const int n = std::max(1, qRound(0.5 * (len[0] + len[2]) / h));
    const int m = std::max(1, qRound(0.5 * (len[1] + len[3]) / h));
    return makeTransfinitePatch(sides, n, m, tag, err);
}

// ---------------------------------------------------------------------------
// Swept patch along a centreline
// ---------------------------------------------------------------------------

PatchMesh makeSweptPatch(const SweptPatch &p, QString *err)
{
    PatchMesh pm;
    const QString msg = validate(p);
    if (!msg.isEmpty()) { if (err) *err = msg; return pm; }
    pm.tag = p.tag;

    // Stations: the centreline vertices, each segment resampled to the
    // target along-spacing (original vertices always kept).
    QVector<QPointF> st;
    st.append(p.centreline.first());
    for (int i = 1; i < p.centreline.size(); ++i)
    {
        const QPointF &a = p.centreline[i - 1], &b = p.centreline[i];
        const double len = std::hypot(b.x() - a.x(), b.y() - a.y());
        const int parts = (p.along > 0.0) ? std::max(1, int(std::ceil(len / p.along - 1e-9))) : 1;
        for (int k = 1; k <= parts; ++k)
            st.append(a + (b - a) * (double(k) / parts));
    }
    const int ns = st.size();

    // Left-hand unit normal of each segment, mitred at interior stations.
    QVector<QPointF> segN(ns - 1);
    for (int i = 0; i < ns - 1; ++i)
    {
        const QPointF d = st[i + 1] - st[i];
        const double len = std::hypot(d.x(), d.y());
        segN[i] = QPointF(-d.y() / len, d.x() / len);
    }
    QVector<QPointF> offs(ns);   // per-station offset vector for unit distance
    for (int i = 0; i < ns; ++i)
    {
        if (i == 0)           { offs[i] = segN[0]; continue; }
        if (i == ns - 1)      { offs[i] = segN[ns - 2]; continue; }
        const QPointF &n0 = segN[i - 1], &n1 = segN[i];
        QPointF nb = n0 + n1;
        const double lb = std::hypot(nb.x(), nb.y());
        if (lb < 1e-12)
        {
            if (err) *err = QStringLiteral("Swept patch centreline reverses on itself at station %1.").arg(i);
            return PatchMesh();
        }
        nb /= lb;
        const double c = nb.x() * n0.x() + nb.y() * n0.y();   // cos(half turn)
        if (c < 1e-6)
        {
            if (err) *err = QStringLiteral("Swept patch centreline turns too sharply at station %1.").arg(i);
            return PatchMesh();
        }
        offs[i] = nb / c;                                       // mitre length
    }

    const int na = p.across;
    auto idx = [na](int i, int j) { return i * (na + 1) + j; };
    pm.xy.reserve(ns * (na + 1));
    for (int i = 0; i < ns; ++i)
        for (int j = 0; j <= na; ++j)
        {
            const double s = -0.5 * p.width + p.width * double(j) / na;
            pm.xy.append(st[i] + offs[i] * s);
        }

    // Along the tangent then across to the left: CCW.
    pm.quads.reserve((ns - 1) * na);
    for (int i = 0; i < ns - 1; ++i)
        for (int j = 0; j < na; ++j)
        {
            MeshTriangle q;
            q.tag = p.tag;
            q.v0 = idx(i, j); q.v1 = idx(i + 1, j); q.v2 = idx(i + 1, j + 1); q.v3 = idx(i, j + 1);
            pm.quads.append(q);
        }

    pm.boundarySegments.reserve(2 * (ns - 1) + 2 * na);
    for (int i = 0; i < ns - 1; ++i)
    {
        pm.boundarySegments.append(qMakePair(idx(i, 0),  idx(i + 1, 0)));
        pm.boundarySegments.append(qMakePair(idx(i, na), idx(i + 1, na)));
    }
    for (int j = 0; j < na; ++j)
    {
        pm.boundarySegments.append(qMakePair(idx(0, j),      idx(0, j + 1)));
        pm.boundarySegments.append(qMakePair(idx(ns - 1, j), idx(ns - 1, j + 1)));
    }

    // A tight bend whose inner offset crosses itself shows up as a folded
    // (non-convex or clockwise) quad.
    const QString bad = validate(pm);
    if (!bad.isEmpty()) { if (err) *err = bad; return PatchMesh(); }
    return pm;
}

} // namespace mesh
