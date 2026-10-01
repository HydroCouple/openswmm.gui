/*!
 * \file   naturalnbinterpolator.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * Natural-neighbour (Sibson / Laplace) interpolation built on a Delaunay
 * triangulation of the seed points from the in-house kernel
 * (mesh/meshcdt.h).  See naturalnbinterpolator.h for the algorithm overview.
 */
#include "mesh/naturalnbinterpolator.h"

#include "mesh/meshcdt.h"

#include <QHash>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <limits>

namespace mesh {

namespace {

constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();

// All internal geometry runs in a normalised [0,~1] space (see build()), so
// these absolute tolerances behave like relative ones in the source CRS.
constexpr double kOrientEps = 1e-12;   // collinearity / on-edge tolerance
constexpr double kInCircEps = 1e-12;   // in-circle tolerance
constexpr double kAreaEps   = 1e-15;   // degenerate stolen-area tolerance
constexpr double kSeedEps   = 1e-12;   // query-coincident-with-seed tolerance

// 2x signed area of triangle (a,b,c); > 0 when CCW.
inline double orient2d(double ax, double ay, double bx, double by,
                       double cx, double cy)
{
    return (bx - ax) * (cy - ay) - (by - ay) * (cx - ax);
}

// Circumcenter of (a,b,c).  Returns false (cc untouched) when degenerate.
inline bool circumcenter(double ax, double ay, double bx, double by,
                         double cx, double cy, double *ccx, double *ccy)
{
    const double d = 2.0 * (ax * (by - cy) + bx * (cy - ay) + cx * (ay - by));
    if (std::fabs(d) < kOrientEps) return false;
    const double a2 = ax * ax + ay * ay;
    const double b2 = bx * bx + by * by;
    const double c2 = cx * cx + cy * cy;
    *ccx = (a2 * (by - cy) + b2 * (cy - ay) + c2 * (ay - by)) / d;
    *ccy = (a2 * (cx - bx) + b2 * (ax - cx) + c2 * (bx - ax)) / d;
    return true;
}

} // namespace

bool NaturalNeighbourInterpolator::build(const QVector<QPointF> &pts,
                                         const QVector<double> &z,
                                         QString *err)
{
    m_valid = false;
    m_px.clear(); m_py.clear(); m_pz.clear(); m_seedOf.clear();
    m_tris.clear(); m_nbrs.clear(); m_ccx.clear(); m_ccy.clear();
    m_numTri = 0; m_lastTri = 0;

    auto setErr = [&](const QString &m) { if (err) *err = m; return false; };

    if (pts.size() != z.size())
        return setErr(QStringLiteral("seed point / value count mismatch"));

    // ── Snap-dedupe coincident points (Triangle aborts on duplicates) ────
    // Same 1e-7 quantisation as the pipeline's keyOf().  Keep first z per key.
    QHash<QPair<qint64, qint64>, int> seen;
    seen.reserve(pts.size());
    std::vector<double> ux, uy, uz;
    ux.reserve(pts.size()); uy.reserve(pts.size()); uz.reserve(pts.size());
    m_seedOf.reserve(pts.size());
    for (int i = 0; i < pts.size(); ++i)
    {
        const double x = pts[i].x(), y = pts[i].y();
        if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z[i])) continue;
        const auto k = qMakePair(qRound64(x * 1e7), qRound64(y * 1e7));
        if (seen.contains(k)) continue;
        seen.insert(k, 1);
        ux.push_back(x); uy.push_back(y); uz.push_back(z[i]);
        // Remember where this Delaunay vertex came from, so weightsAt() can
        // report weights against the caller's own indexing. Dropped duplicates
        // are simply absent, and therefore never carry weight.
        m_seedOf.push_back(i);
    }

    const int n = int(ux.size());
    if (n < 3)
        return setErr(QStringLiteral("need >= 3 unique seed points for natural neighbour"));

    // ── Normalise to a local [0,~1] space for numeric conditioning ───────
    double minx = ux[0], miny = uy[0], maxx = ux[0], maxy = uy[0];
    for (int i = 1; i < n; ++i)
    {
        minx = std::min(minx, ux[i]); maxx = std::max(maxx, ux[i]);
        miny = std::min(miny, uy[i]); maxy = std::max(maxy, uy[i]);
    }
    m_ox = minx; m_oy = miny;
    const double scale = std::max(maxx - minx, maxy - miny);
    if (!(scale > 0.0))
        return setErr(QStringLiteral("degenerate seed extent"));
    m_scale = scale;

    // ── Triangulate the bare point set (mesh/meshcdt.h) ──────────────────
    QVector<QPointF> norm(n);
    for (int i = 0; i < n; ++i)
        norm[i] = QPointF((ux[i] - m_ox) / m_scale, (uy[i] - m_oy) / m_scale);
    ConstrainedDelaunay cdt;
    QVector<int> vertexOf;
    if (!cdt.build(norm, &vertexOf))
        return setErr(QStringLiteral("Delaunay failed (collinear or too few seeds): %1").arg(cdt.errorMsg()));
    for (int i = 0; i < n; ++i)
        if (vertexOf[i] != i)
            return setErr(QStringLiteral("coincident seeds after normalisation"));
    cdt.removeSuperTriangles();

    // ── Copy into owned arrays (order preserved → z aligns by index) ─────
    m_px.resize(n); m_py.resize(n); m_pz.assign(uz.begin(), uz.end());
    for (int i = 0; i < n; ++i) { m_px[i] = norm[i].x(); m_py[i] = norm[i].y(); }

    // Compact the live triangles; neighbour = across the edge opposite
    // corner i (the convention the walk and the cavity search rely on).
    const auto &T = cdt.triangles();
    std::vector<int> newIndex(T.size(), -1);
    m_numTri = 0;
    for (int t = 0; t < T.size(); ++t) if (T[t].alive) newIndex[t] = m_numTri++;
    if (m_numTri <= 0)
        return setErr(QStringLiteral("Delaunay produced no triangles (collinear seeds)"));
    m_tris.resize(3 * m_numTri);
    m_nbrs.resize(3 * m_numTri);
    for (int t = 0; t < T.size(); ++t)
    {
        if (!T[t].alive) continue;
        const int k = newIndex[t];
        for (int i = 0; i < 3; ++i)
        {
            m_tris[3 * k + i] = T[t].v[i];
            const int a = T[t].adj[i];
            m_nbrs[3 * k + i] = (a >= 0 && T[a].alive) ? newIndex[a] : -1;
        }
    }

    // Precompute circumcenters (NaN when degenerate).
    m_ccx.resize(m_numTri); m_ccy.resize(m_numTri);
    for (int t = 0; t < m_numTri; ++t)
    {
        const int a = m_tris[3 * t], b = m_tris[3 * t + 1], c = m_tris[3 * t + 2];
        double cx, cy;
        if (circumcenter(m_px[a], m_py[a], m_px[b], m_py[b], m_px[c], m_py[c], &cx, &cy))
        { m_ccx[t] = cx; m_ccy[t] = cy; }
        else
        { m_ccx[t] = kNaN; m_ccy[t] = kNaN; }
    }

    m_valid = true;
    return true;
}

// Jump-and-walk point location.  Returns triangle index, or -1 if (qx,qy) is
// outside the convex hull.  Coordinates are in normalised space.
int NaturalNeighbourInterpolator::locate(double qx, double qy) const
{
    int t = m_lastTri;
    if (t < 0 || t >= m_numTri) t = 0;

    const int cap = 2 * m_numTri + 8;
    for (int iter = 0; iter < cap; ++iter)
    {
        const int v0 = m_tris[3 * t], v1 = m_tris[3 * t + 1], v2 = m_tris[3 * t + 2];
        // CCW edges (v0->v1),(v1->v2),(v2->v0); neighbour opposite the 3rd vertex.
        if (orient2d(m_px[v0], m_py[v0], m_px[v1], m_py[v1], qx, qy) < -kOrientEps)
        {
            const int nb = m_nbrs[3 * t + 2];   // opposite v2, across (v0,v1)
            if (nb < 0) return -1;
            t = nb; continue;
        }
        if (orient2d(m_px[v1], m_py[v1], m_px[v2], m_py[v2], qx, qy) < -kOrientEps)
        {
            const int nb = m_nbrs[3 * t + 0];   // opposite v0, across (v1,v2)
            if (nb < 0) return -1;
            t = nb; continue;
        }
        if (orient2d(m_px[v2], m_py[v2], m_px[v0], m_py[v0], qx, qy) < -kOrientEps)
        {
            const int nb = m_nbrs[3 * t + 1];   // opposite v1, across (v2,v0)
            if (nb < 0) return -1;
            t = nb; continue;
        }
        m_lastTri = t;
        return t;   // left of all three edges → inside (or on) t
    }
    return -1;   // no convergence (degenerate) → caller falls back to IDW
}

bool NaturalNeighbourInterpolator::build(const QVector<QPointF> &pts, QString *err)
{
    // Weight queries never touch z; a zero vector keeps one triangulation path.
    return build(pts, QVector<double>(pts.size(), 0.0), err);
}

bool NaturalNeighbourInterpolator::computeWeights(
    double qx, double qy, std::vector<std::pair<int, double>> &out) const
{
    out.clear();
    if (!m_valid || m_numTri <= 0) return false;

    const int t0 = locate(qx, qy);
    if (t0 < 0) return false;   // outside convex hull

    auto inCircle = [&](int t) -> bool {
        const int a = m_tris[3 * t], b = m_tris[3 * t + 1], c = m_tris[3 * t + 2];
        const double adx = m_px[a] - qx, ady = m_py[a] - qy;
        const double bdx = m_px[b] - qx, bdy = m_py[b] - qy;
        const double cdx = m_px[c] - qx, cdy = m_py[c] - qy;
        const double det =
            (adx * adx + ady * ady) * (bdx * cdy - cdx * bdy)
          - (bdx * bdx + bdy * bdy) * (adx * cdy - cdx * ady)
          + (cdx * cdx + cdy * cdy) * (adx * bdy - bdx * ady);
        return det > kInCircEps;   // CCW triangles → >0 means strictly inside
    };

    // ── (1) Delaunay cavity: triangles whose circumcircle contains Q ─────
    std::vector<int> cavity;
    std::vector<int> stack(1, t0);
    QHash<int, char> inCav;     // triangle index → 1
    inCav.insert(t0, 1);
    cavity.push_back(t0);
    while (!stack.empty())
    {
        const int t = stack.back(); stack.pop_back();
        for (int k = 0; k < 3; ++k)
        {
            const int nb = m_nbrs[3 * t + k];
            if (nb < 0 || inCav.contains(nb)) continue;
            if (inCircle(nb)) { inCav.insert(nb, 1); cavity.push_back(nb); stack.push_back(nb); }
        }
    }

    // ── (2) Boundary edges → natural neighbours; new circumcenters ───────
    // For each natural-neighbour vertex collect old circumcenters (incident
    // cavity triangles) and new circumcenters (incident boundary edges).
    QHash<int, std::vector<QPointF>> oldCC;  // vertex → cavity-triangle circumcenters
    QHash<int, std::vector<QPointF>> newCC;  // vertex → new circumcenters (2 expected)

    for (const int t : cavity)
    {
        // old circumcenter contributes to all three vertices of t
        if (!std::isfinite(m_ccx[t])) return false;   // degenerate triangle in cavity
        const QPointF cc(m_ccx[t], m_ccy[t]);
        for (int k = 0; k < 3; ++k)
            oldCC[m_tris[3 * t + k]].push_back(cc);

        for (int k = 0; k < 3; ++k)
        {
            const int nb = m_nbrs[3 * t + k];
            if (nb >= 0 && inCav.contains(nb)) continue;   // interior cavity edge
            if (nb < 0) return false;                      // cavity touches the hull

            // Boundary edge = the two vertices other than the one opposite nb.
            const int pa = m_tris[3 * t + (k + 1) % 3];
            const int pb = m_tris[3 * t + (k + 2) % 3];
            double cx, cy;
            if (!circumcenter(m_px[pa], m_py[pa], m_px[pb], m_py[pb], qx, qy, &cx, &cy))
                return false;
            const QPointF nc(cx, cy);
            newCC[pa].push_back(nc);
            newCC[pb].push_back(nc);
        }
    }

    if (newCC.isEmpty()) return false;

    // ── (3) Per-neighbour weights ────────────────────────────────────────
    // Emitted in QHash iteration order. interpolate() accumulates in exactly
    // this order, so its rounding is unchanged by the extraction; weightsAt()
    // sorts a copy afterwards for reproducibility.
    out.reserve(static_cast<size_t>(newCC.size()));
    for (auto it = newCC.constBegin(); it != newCC.constEnd(); ++it)
    {
        const int pi = it.key();
        const std::vector<QPointF> &ncs = it.value();

        // Exact coincidence with a seed → that seed takes the whole weight,
        // which makes the normalised interpolant reduce to its z exactly.
        const double dx = m_px[pi] - qx, dy = m_py[pi] - qy;
        const double dist2 = dx * dx + dy * dy;
        if (dist2 < kSeedEps * kSeedEps)
        {
            out.clear();
            out.emplace_back(pi, 1.0);
            return true;
        }

        double w;
        if (m_variant == Variant::Laplace)
        {
            if (ncs.size() != 2) return false;   // malformed boundary fan
            const double ex = ncs[0].x() - ncs[1].x();
            const double ey = ncs[0].y() - ncs[1].y();
            const double facet = std::sqrt(ex * ex + ey * ey);
            w = facet / std::sqrt(dist2);
        }
        else // Sibson — area Q steals from neighbour pi
        {
            // Ring = new circumcenters (incident boundary edges) +
            //        old circumcenters (incident cavity triangles).
            std::vector<QPointF> ring = ncs;
            const auto oit = oldCC.constFind(pi);
            if (oit != oldCC.constEnd())
                ring.insert(ring.end(), oit->begin(), oit->end());
            if (ring.size() < 3) return false;

            // Angular sort around pi, then shoelace.
            const double cxp = m_px[pi], cyp = m_py[pi];
            std::sort(ring.begin(), ring.end(),
                      [cxp, cyp](const QPointF &a, const QPointF &b) {
                          return std::atan2(a.y() - cyp, a.x() - cxp)
                               < std::atan2(b.y() - cyp, b.x() - cxp);
                      });
            double area2 = 0.0;
            for (size_t r = 0; r < ring.size(); ++r)
            {
                const QPointF &p = ring[r];
                const QPointF &q = ring[(r + 1) % ring.size()];
                area2 += p.x() * q.y() - q.x() * p.y();
            }
            w = std::fabs(area2) * 0.5;
        }

        if (!std::isfinite(w) || w < 0.0) return false;
        out.emplace_back(pi, w);
    }
    return true;
}

double NaturalNeighbourInterpolator::interpolate(double x, double y) const
{
    if (!m_valid || m_numTri <= 0) return kNaN;

    const double qx = (x - m_ox) / m_scale;
    const double qy = (y - m_oy) / m_scale;

    std::vector<std::pair<int, double>> w;
    if (!computeWeights(qx, qy, w)) return kNaN;

    double wsum = 0.0, zsum = 0.0;
    for (const auto &[pi, wi] : w)
    {
        wsum += wi;
        zsum += wi * m_pz[pi];
    }

    if (wsum < kAreaEps) return kNaN;
    return zsum / wsum;
}

bool NaturalNeighbourInterpolator::weightsAt(double x, double y,
                                             QVector<QPair<int, double>> &out) const
{
    out.clear();
    if (!m_valid || m_numTri <= 0) return false;

    const double qx = (x - m_ox) / m_scale;
    const double qy = (y - m_oy) / m_scale;

    std::vector<std::pair<int, double>> raw;
    if (!computeWeights(qx, qy, raw)) return false;

    double wsum = 0.0;
    for (const auto &[pi, wi] : raw)
        wsum += wi;
    if (wsum < kAreaEps) return false;

    out.reserve(static_cast<int>(raw.size()));
    for (const auto &[pi, wi] : raw)
        out.append({m_seedOf[static_cast<size_t>(pi)], wi / wsum});

    // Ascending original-seed order — contractual, since computeWeights walks a
    // QHash whose iteration order is randomised per process.
    std::sort(out.begin(), out.end(),
              [](const QPair<int, double> &a, const QPair<int, double> &b) {
                  return a.first < b.first;
              });
    return true;
}

} // namespace mesh
