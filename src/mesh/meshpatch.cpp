/*!
 * \file   meshpatch.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * Structured quad patches (TRI_QUAD_MESHING_PLAN §3.2, phase G3): transfinite
 * four-sided patches and swept channel patches, in local indices.
 */
#include "mesh/meshpatch.h"

#include "mesh/meshcellgeom.h"

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
