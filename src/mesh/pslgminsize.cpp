/*!
 * \file   pslgminsize.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * See pslgminsize.h for why this file exists and what it guarantees.
 *
 * Everything here works on one unified Pool of vertices plus a list of Paths
 * that index into it.  That indirection is the whole trick: welding a vertex
 * is a union-find link, and every path referencing it — a domain ring, a hole
 * ring, three conduits — follows automatically.  Conditioning each geometry
 * kind separately cannot work, because the violations that matter are
 * BETWEEN kinds (a conduit endpoint a hair from the domain edge).
 *
 * The segment index is chunked with a global chunk budget, copied from the
 * worker's existing near-constraint filter (meshgenerationdialog.cpp:1429) so
 * a pathological bufferDist degrades the same way in both places.
 */
#include "mesh/pslgminsize.h"

#include "mesh/pslgprep.h"

#include <QHash>
#include <QSet>
#include <QtGlobal>
#include <QtMath>   // M_PI on MSVC

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

namespace mesh {
namespace pslg {

// ---------------------------------------------------------------------------
// Policy / report
// ---------------------------------------------------------------------------

void MinSizePolicy::resolveDefaults()
{
    const double h = minCellSize;
    if (h <= 0.0) return;
    // <= 0, not < 0: a zero radius would divide by zero in the spatial grids.
    if (weldRadius    <= 0.0) weldRadius    = h;
    if (minSegmentLen <= 0.0) minSegmentLen = h;
    if (maxDeviation  <  0.0) maxDeviation  = 0.5 * h;
    if (identityMergeRadius <= 0.0) identityMergeRadius = weldRadius;
    if (maxIterations < 1)   maxIterations = 1;
    if (maxResiduals  < 0)   maxResiduals  = 0;
}

double MinSizePolicy::minTriangleArea() const
{
    if (minCellSize <= 0.0) return 0.0;
    // Equilateral triangle of side h.
    return 0.25 * std::sqrt(3.0) * minCellSize * minCellSize;
}

double MinSizePolicy::refinementAreaCap(double uniformCap) const
{
    const double floor = minTriangleArea();
    if (floor <= 0.0) return uniformCap;      // feature off: cap unchanged
    return uniformCap > 0.0 ? std::max(floor, uniformCap) : 0.0;
}

QString violationCauseName(ViolationCause c)
{
    switch (c)
    {
    case ViolationCause::ShortSegment:  return QStringLiteral("short segment");
    case ViolationCause::CloseFeatures: return QStringLiteral("close features");
    case ViolationCause::SmallAngle:    return QStringLiteral("small angle");
    case ViolationCause::SubScaleRing:  return QStringLiteral("sub-scale ring");
    case ViolationCause::IdentityMerged:return QStringLiteral("identity merged");
    }
    return QStringLiteral("unknown");
}

QString ConditionReport::summary() const
{
    if (conditioningAbandoned)
        return QStringLiteral("conditioning ABANDONED (%1) — "
                              "PSLG restored, generating unconditioned")
            .arg(abandonReason.isEmpty()
                     ? QStringLiteral("crossings %1 -> %2")
                           .arg(crossingsBefore).arg(crossingsAfter)
                     : abandonReason);
    return QStringLiteral("welded %1, split %2, resampled %3 path(s) "
                          "(%4 short edge(s) kept), collapsed %5, holes dropped %6, "
                          "corners trimmed %7 (skipped %8), crossings repaired %9, "
                          "iterations %10, max shift %11, area %12 -> %13, "
                          "min lfs %14, residuals %15, merged alignments %16")
        .arg(verticesWelded).arg(segmentsSplit).arg(pathsShortened)
        .arg(shortEdgesKept).arg(pathsCollapsed).arg(holesDropped)
        .arg(cornersTrimmed).arg(cornersSkipped).arg(crossingsRepaired)
        .arg(iterationsUsed)
        .arg(maxDisplacement, 0, 'g', 4)
        .arg(domainAreaBefore, 0, 'g', 8).arg(domainAreaAfter, 0, 'g', 8)
        .arg(predictedMinLfs, 0, 'g', 4)
        .arg(residuals.size())
        .arg(duplicateSegments)
        // Appended, not interleaved, so the default (opt-in off) line is
        // byte-identical to what it has always been.
        + (identitiesMerged > 0
               ? QStringLiteral(", identities merged %1").arg(identitiesMerged)
               : QString());
}

// ---------------------------------------------------------------------------
// Internals
// ---------------------------------------------------------------------------

namespace {

using CellKey = QPair<qint32, qint32>;

// Chunk budget for the segment index. Same intent as the worker's
// kMaxSegChunks: refuse to grind on a pathological radius.
constexpr qint64 kMaxSegChunks   = 40'000'000;
constexpr qint64 kMaxPairTests   = 60'000'000;

// Vertex priorities. Higher survives a weld.
constexpr int kPrioLocked = 4;  ///< read-only ring vertex — cannot move at all
constexpr int kPrioNode   = 3;  ///< tagged SWMM node Steiner point
constexpr int kPrioEnd    = 2;  ///< endpoint of a tagged constraint (conduit)
constexpr int kPrioTagged = 1;  ///< tagged constraint interior / domain ring
constexpr int kPrioPlain  = 0;  ///< everything else

// Two vertices at or above this priority are distinct identities and must
// never be merged into one another.
constexpr int kPrioIdentity = kPrioEnd;

/*! Bit-exact coordinate key, so vertices arriving from different sources at
 *  literally the same coordinate become one pool vertex (a conduit endpoint
 *  and the junction Steiner point on top of it, typically). */
QPair<quint64, quint64> exactKey(const QPointF &p)
{
    quint64 kx = 0, ky = 0;
    const double x = p.x(), y = p.y();
    std::memcpy(&kx, &x, sizeof(kx));
    std::memcpy(&ky, &y, sizeof(ky));
    return qMakePair(kx, ky);
}

enum class PathKind { Domain, Hole, Constraint };

struct Path
{
    PathKind     kind    = PathKind::Constraint;
    int          src     = -1;      ///< index into the caller's vector; -1 = new
    int          marker  = 0;
    QString      tag;
    bool         closed  = false;
    bool         closedDup = false; ///< input repeated its first vertex at the end
    bool         readOnly = false;
    bool         dropped  = false;
    QVector<int> v;                 ///< pool vertex indices

    /*! Edge count: closed rings wrap, open paths do not. */
    [[nodiscard]] int edgeCount() const
    {
        const int n = v.size();
        if (n < 2) return 0;
        return closed ? n : n - 1;
    }
    [[nodiscard]] int edgeA(int e) const { return v[e]; }
    [[nodiscard]] int edgeB(int e) const { return v[(e + 1) % v.size()]; }
};

struct Pool
{
    QVector<QPointF> pos;
    QVector<int>     prio;
    QVector<int>     parent;   ///< union-find
    QVector<QPointF> origPos;  ///< for the displacement bound

    QHash<QPair<quint64, quint64>, int> byCoord;

    int add(const QPointF &p, int priority)
    {
        const auto key = exactKey(p);
        auto it = byCoord.constFind(key);
        if (it != byCoord.constEnd())
        {
            // Resolve through the union-find: the cached index may have been
            // welded away since it was recorded, and callers splice whatever
            // comes back straight into a path.  Returning a stale index would
            // silently place the vertex at its representative's coordinate.
            const int i = find(*it);
            prio[i] = std::max(prio[i], priority);
            return i;
        }
        const int i = pos.size();
        pos.append(p);
        origPos.append(p);
        prio.append(priority);
        parent.append(i);
        byCoord.insert(key, i);
        return i;
    }

    /*! Non-mutating find, for read-only passes over a finished pool. */
    [[nodiscard]] int findConst(int i) const
    {
        while (parent[i] != i) i = parent[i];
        return i;
    }

    int find(int i)
    {
        while (parent[i] != i)
        {
            parent[i] = parent[parent[i]];   // path halving
            i = parent[i];
        }
        return i;
    }

    /*! Weld \p child onto \p rep: child adopts rep's position. */
    void link(int child, int rep)
    {
        parent[find(child)] = find(rep);
    }

    [[nodiscard]] bool isIdentity(int i) const { return prio[i] >= kPrioIdentity; }
    [[nodiscard]] bool isLocked(int i)   const { return prio[i] >= kPrioLocked; }
};

/*! A directed reference to one edge of one path. */
struct EdgeRef
{
    int path = -1;
    int e    = -1;
    bool operator==(const EdgeRef &o) const { return path == o.path && e == o.e; }
};
/*! 64-bit identity for one edge.  Must be qint64: packing path and edge index
 *  into an int overflows past ~2100 paths, and a PSLG with tens of thousands
 *  of hole rings is routine here. */
inline qint64 edgeKey(const EdgeRef &r)
{
    return (static_cast<qint64>(r.path) << 32) | static_cast<quint32>(r.e);
}

/*! Order-independent identity for an edge pair. */
inline QPair<qint64, qint64> pairKey(const EdgeRef &a, const EdgeRef &b)
{
    const qint64 ka = edgeKey(a), kb = edgeKey(b);
    return ka < kb ? qMakePair(ka, kb) : qMakePair(kb, ka);
}

inline size_t qHash(const EdgeRef &r, size_t seed = 0) noexcept
{
    return ::qHash(static_cast<quint64>(edgeKey(r)), seed);
}

/*!
 * \brief Chunked uniform grid over path edges.
 *
 * Long edges are split into chunks of ~\c radius so a single edge cannot
 * occupy an unbounded number of cells; a global chunk budget trips
 * \c overflow, and the caller then degrades rather than grinding.
 */
class EdgeGrid
{
public:
    bool overflow = false;

    void build(const QVector<Path> &paths, const Pool &pool, double radius)
    {
        m_radius = radius;
        m_inv    = 1.0 / radius;
        m_chunks.clear();
        m_cells.clear();
        overflow = false;
        qint64 total = 0;

        for (int pi = 0; pi < paths.size(); ++pi)
        {
            const Path &p = paths[pi];
            if (p.dropped) continue;
            const int ec = p.edgeCount();
            for (int e = 0; e < ec; ++e)
            {
                const QPointF &a = pool.pos[p.edgeA(e)];
                const QPointF &b = pool.pos[p.edgeB(e)];
                if (a == b) continue;
                const double len = std::hypot(b.x() - a.x(), b.y() - a.y());
                if (!std::isfinite(len)) continue;

                const double nReal = std::ceil(len * m_inv);
                if (!std::isfinite(nReal)
                    || total + qint64(std::min(nReal, 1e18)) > kMaxSegChunks)
                {
                    overflow = true;
                    m_chunks.clear();
                    m_cells.clear();
                    return;
                }
                const int nChunks = std::max(1, static_cast<int>(nReal));
                total += nChunks;
                for (int c = 0; c < nChunks; ++c)
                {
                    const double t0 = double(c)     / nChunks;
                    const double t1 = double(c + 1) / nChunks;
                    const QPointF ca(a.x() + t0 * (b.x() - a.x()),
                                     a.y() + t0 * (b.y() - a.y()));
                    const QPointF cb(a.x() + t1 * (b.x() - a.x()),
                                     a.y() + t1 * (b.y() - a.y()));
                    const int ci = m_chunks.size();
                    m_chunks.append(Chunk{EdgeRef{pi, e}, ca, cb});
                    const qint32 gx0 = qint32(std::floor((std::min(ca.x(), cb.x()) - radius) * m_inv));
                    const qint32 gx1 = qint32(std::floor((std::max(ca.x(), cb.x()) + radius) * m_inv));
                    const qint32 gy0 = qint32(std::floor((std::min(ca.y(), cb.y()) - radius) * m_inv));
                    const qint32 gy1 = qint32(std::floor((std::max(ca.y(), cb.y()) + radius) * m_inv));
                    for (qint32 gy = gy0; gy <= gy1; ++gy)
                        for (qint32 gx = gx0; gx <= gx1; ++gx)
                            m_cells[qMakePair(gx, gy)].append(ci);
                }
            }
        }
    }

    /*! Distinct edges with a chunk in one of the 3x3 cells around \p p. */
    void nearEdges(const QPointF &p, QVector<EdgeRef> *out) const
    {
        out->clear();
        if (m_cells.isEmpty()) return;
        const qint32 cx = qint32(std::floor(p.x() * m_inv));
        const qint32 cy = qint32(std::floor(p.y() * m_inv));
        QSet<EdgeRef> seen;
        for (qint32 dy = -1; dy <= 1; ++dy)
            for (qint32 dx = -1; dx <= 1; ++dx)
            {
                auto it = m_cells.constFind(qMakePair(cx + dx, cy + dy));
                if (it == m_cells.constEnd()) continue;
                for (const int ci : *it)
                {
                    const EdgeRef &r = m_chunks[ci].ref;
                    if (!seen.contains(r)) { seen.insert(r); out->append(r); }
                }
            }
    }

    /*! Every co-located edge pair, for crossing broad-phase.  Returns false if
     *  the pair budget was exceeded.
     *
     *  A pair straddling several cells is reported more than once, deliberately:
     *  deduplicating every TESTED pair costs a set entry per test — tens of
     *  millions of them under the budget below — whereas the callers only need
     *  to deduplicate the pairs that actually cross, of which there are
     *  approximately none.  \ref edgeKey builds the identity they use. */
    bool forEachCoLocatedPair(const std::function<void(EdgeRef, EdgeRef)> &fn) const
    {
        qint64 tests = 0;
        for (auto it = m_cells.constBegin(); it != m_cells.constEnd(); ++it)
        {
            const QVector<int> &v = it.value();
            const int n = v.size();
            for (int i = 0; i < n; ++i)
                for (int j = i + 1; j < n; ++j)
                {
                    if (++tests > kMaxPairTests) return false;
                    const EdgeRef &ra = m_chunks[v[i]].ref;
                    const EdgeRef &rb = m_chunks[v[j]].ref;
                    if (ra == rb) continue;
                    fn(ra, rb);
                }
        }
        return true;
    }

    [[nodiscard]] bool isEmpty() const { return m_cells.isEmpty(); }

private:
    struct Chunk { EdgeRef ref; QPointF a, b; };
    QVector<Chunk>                m_chunks;
    QHash<CellKey, QVector<int>>  m_cells;
    double m_radius = 1.0;
    double m_inv    = 1.0;
};

/*! Uniform point grid, cell size = radius, for the greedy weld pass. */
class PointGrid
{
public:
    explicit PointGrid(double radius) : m_inv(1.0 / radius) {}

    void insert(int idx, const QPointF &p)
    {
        m_cells[cell(p)].append(idx);
    }

    /*! Nearest inserted index to \p p within \p radius for which \p accept
     *  returns true, or -1. */
    int nearest(const QPointF &p, double radius,
                const QVector<QPointF> &pos,
                const std::function<bool(int)> &accept) const
    {
        const double r2 = radius * radius;
        const CellKey c = cell(p);
        int    best  = -1;
        double bestD = r2;
        for (qint32 dy = -1; dy <= 1; ++dy)
            for (qint32 dx = -1; dx <= 1; ++dx)
            {
                auto it = m_cells.constFind(qMakePair(c.first + dx, c.second + dy));
                if (it == m_cells.constEnd()) continue;
                for (const int j : *it)
                {
                    const double ddx = pos[j].x() - p.x();
                    const double ddy = pos[j].y() - p.y();
                    const double d2  = ddx * ddx + ddy * ddy;
                    if (d2 < bestD && accept(j)) { bestD = d2; best = j; }
                }
            }
        return best;
    }

private:
    [[nodiscard]] CellKey cell(const QPointF &p) const
    {
        return qMakePair(qint32(std::floor(p.x() * m_inv)),
                         qint32(std::floor(p.y() * m_inv)));
    }
    QHash<CellKey, QVector<int>> m_cells;
    double m_inv;
};

double cross2(const QPointF &o, const QPointF &a, const QPointF &b)
{
    return (a.x() - o.x()) * (b.y() - o.y()) - (a.y() - o.y()) * (b.x() - o.x());
}

/*! Strict crossing: the two open segments meet at a point interior to both.
 *  Shared endpoints and mere touching are legal in a PSLG (and touching is
 *  what the vertex-edge fix-up exists to create), so only the strict case
 *  counts — it is also exactly what makes Triangle abort. */
bool properlyCrosses(const QPointF &a, const QPointF &b,
                     const QPointF &c, const QPointF &d)
{
    const double d1 = cross2(a, b, c);
    const double d2 = cross2(a, b, d);
    const double d3 = cross2(c, d, a);
    const double d4 = cross2(c, d, b);
    if (d1 == 0.0 || d2 == 0.0 || d3 == 0.0 || d4 == 0.0) return false;
    return ((d1 > 0.0) != (d2 > 0.0)) && ((d3 > 0.0) != (d4 > 0.0));
}

/*! Two COLLINEAR segments sharing more than a single point.
 *
 *  properlyCrosses() reports an X and nothing else — by construction it
 *  returns false the moment any orientation determinant is zero, which is
 *  exactly the degenerate family.  Triangle aborts on an overlap
 *  ("Topological inconsistency after splitting a segment") just as readily as
 *  on a crossing, so the fail-safe has to see these too. */
bool collinearOverlap(const QPointF &a, const QPointF &b,
                      const QPointF &c, const QPointF &d)
{
    if (cross2(a, b, c) != 0.0 || cross2(a, b, d) != 0.0) return false;
    const double dx = b.x() - a.x(), dy = b.y() - a.y();
    const bool useX = std::abs(dx) >= std::abs(dy);
    auto axis = [useX](const QPointF &p) { return useX ? p.x() : p.y(); };
    double a0 = axis(a), a1 = axis(b);
    if (a1 < a0) std::swap(a0, a1);
    double b0 = axis(c), b1 = axis(d);
    if (b1 < b0) std::swap(b0, b1);
    // Strictly positive overlap: a shared endpoint alone is legal.
    return std::min(a1, b1) > std::max(a0, b0);
}

bool segmentIntersection(const QPointF &a, const QPointF &b,
                         const QPointF &c, const QPointF &d, QPointF *out)
{
    const double rx = b.x() - a.x(), ry = b.y() - a.y();
    const double sx = d.x() - c.x(), sy = d.y() - c.y();
    const double den = rx * sy - ry * sx;
    if (den == 0.0 || !std::isfinite(den)) return false;
    const double t = ((c.x() - a.x()) * sy - (c.y() - a.y()) * sx) / den;
    if (!std::isfinite(t)) return false;
    *out = QPointF(a.x() + t * rx, a.y() + t * ry);
    return true;
}

/*! Angle at \p v between rays v->p and v->q, in degrees [0, 180]. */
double angleAt(const QPointF &v, const QPointF &p, const QPointF &q)
{
    const double ax = p.x() - v.x(), ay = p.y() - v.y();
    const double bx = q.x() - v.x(), by = q.y() - v.y();
    const double la = std::hypot(ax, ay), lb = std::hypot(bx, by);
    if (la <= 0.0 || lb <= 0.0) return 180.0;
    double cosT = (ax * bx + ay * by) / (la * lb);
    cosT = std::clamp(cosT, -1.0, 1.0);
    return std::acos(cosT) * 180.0 / M_PI;
}

/*! Perimeter of a ring, wrapping the closing edge. */
double ringPerimeter(const QVector<QPointF> &ring)
{
    const int n = ring.size();
    if (n < 3) return 0.0;
    const int en = (ring.first() == ring.last()) ? n - 1 : n;
    double per = 0.0;
    for (int i = 0; i < en; ++i)
    {
        const QPointF &a = ring[i];
        const QPointF &b = ring[(i + 1) % en];
        per += std::hypot(b.x() - a.x(), b.y() - a.y());
    }
    return per;
}

/*! Cheap width proxy: 2*area/perimeter is the radius of the inscribed circle
 *  for convex shapes and a reasonable lower-bound proxy otherwise.  Used only
 *  to decide "this ring is narrower than one cell", where being approximate
 *  is fine because the alternative is an exact medial axis. */
/*!
 * \brief Does this closed ring enclose (essentially) no area?
 *
 * Welding under a large radius can fold a narrow ring back onto itself.  The
 * result is invisible to every other check in this file: a fold has a ZERO
 * orientation determinant, so the proper-crossing test cannot see it, and its
 * repeated edges look exactly like the benign duplicate alignments the census
 * deliberately tolerates.  Triangle, however, cannot mesh a boundary that
 * bounds nothing.
 *
 * The tolerance is scaled by the ring's own extent so the test is
 * dimensionless.  A legitimately thin 100 x 0.001 ring has area 0.1 — eight
 * orders above the floor — while a fold is exactly zero, because the shoelace
 * sum of any out-and-back sequence cancels term by term.
 */
bool ringEnclosesNoArea(const QVector<QPointF> &ring, double radius)
{
    if (ring.size() < 3) return true;
    double xmin = ring.first().x(), xmax = xmin;
    double ymin = ring.first().y(), ymax = ymin;
    for (const QPointF &q : ring)
    {
        xmin = std::min(xmin, q.x()); xmax = std::max(xmax, q.x());
        ymin = std::min(ymin, q.y()); ymax = std::max(ymax, q.y());
    }
    const double w = xmax - xmin, hgt = ymax - ymin;
    const double scale2 = std::max(w * w + hgt * hgt, radius * radius);
    return std::abs(ringSignedArea(ring)) <= 1.0e-12 * scale2;
}

double ringWidthProxy(const QVector<QPointF> &ring)
{
    const double per = ringPerimeter(ring);
    if (per <= 0.0) return 0.0;
    return 4.0 * std::abs(ringSignedArea(ring)) / per;
}

// ── Pool / path construction ──────────────────────────────────────────────

struct Build
{
    Pool          pool;
    QVector<Path> paths;
};

void buildPool(Build *b,
               const QVector<QPolygonF>         &domains,
               const QVector<QVector<QPointF>>  &holeRings,
               const QVector<ConstraintSegment> &segs,
               const QVector<SteinerPoint>      &pts,
               QVector<int>                     *steinerVertex,
               bool ringsReadOnly)
{
    auto addRing = [&](const QVector<QPointF> &ring, PathKind kind, int src,
                       int vertexPrio) {
        Path p;
        p.kind      = kind;
        p.src       = src;
        p.closed    = true;
        p.readOnly  = ringsReadOnly;
        p.closedDup = (ring.size() >= 2 && ring.first() == ring.last());
        const int en = p.closedDup ? ring.size() - 1 : ring.size();
        const int prio = ringsReadOnly ? kPrioLocked : vertexPrio;
        p.v.reserve(en);
        for (int i = 0; i < en; ++i)
        {
            const int vi = b->pool.add(ring[i], prio);
            if (!p.v.isEmpty() && p.v.last() == vi) continue;  // exact dup
            p.v.append(vi);
        }
        b->paths.append(std::move(p));
    };

    for (int i = 0; i < domains.size(); ++i)
    {
        QVector<QPointF> ring;
        ring.reserve(domains[i].size());
        for (const QPointF &q : domains[i]) ring.append(q);
        addRing(ring, PathKind::Domain, i, kPrioTagged);
    }
    for (int i = 0; i < holeRings.size(); ++i)
        addRing(holeRings[i], PathKind::Hole, i, kPrioPlain);

    for (int i = 0; i < segs.size(); ++i)
    {
        const ConstraintSegment &cs = segs[i];
        Path p;
        p.kind   = PathKind::Constraint;
        p.src    = i;
        p.marker = cs.marker;
        p.tag    = cs.tag;
        p.closed = false;
        const int endPrio = cs.marker != 0 ? kPrioEnd    : kPrioPlain;
        const int midPrio = cs.marker != 0 ? kPrioTagged : kPrioPlain;
        p.v.reserve(cs.path.size());
        for (int j = 0; j < cs.path.size(); ++j)
        {
            const bool end = (j == 0 || j == cs.path.size() - 1);
            const int vi = b->pool.add(cs.path[j], end ? endPrio : midPrio);
            if (!p.v.isEmpty() && p.v.last() == vi) continue;
            p.v.append(vi);
        }
        b->paths.append(std::move(p));
    }

    steinerVertex->resize(pts.size());
    for (int i = 0; i < pts.size(); ++i)
        (*steinerVertex)[i] =
            b->pool.add(pts[i].xy, pts[i].marker != 0 ? kPrioNode : kPrioPlain);
}

void addResidual(ConditionReport *rep, int cap, const Violation &v)
{
    if (rep->residuals.size() < static_cast<qsizetype>(cap))
        rep->residuals.append(v);
}

/*!
 * \brief Everything about a PSLG that can stop Triangle, counted.
 *
 * Invariant (5) was originally phrased in terms of proper crossings alone, and
 * that is not enough: properlyCrosses() returns false the moment any
 * orientation determinant is zero, so the whole DEGENERATE family — a segment
 * emitted twice, a segment of zero length, two segments lying along one line
 * and overlapping — passes the test while Triangle aborts on it.  Real models
 * hit exactly that (tests/manual/mesh_minsize/README.md records the numbers:
 * a 2363-node model conditioned at h = 8 produced 393 duplicate segments and 5
 * collinear overlaps, no new proper crossings, and killed Triangle).
 *
 * So the fail-safe compares the whole census, not one number of it.
 */
struct PslgCensus
{
    int  crossings  = 0;   ///< proper X crossings
    int  duplicates = 0;   ///< the same undirected vertex pair emitted twice
    int  zeroLength = 0;
    int  overlaps   = 0;   ///< collinear and sharing more than a point
    int  degenerateRings = 0;  ///< closed paths that enclose no area
    bool verified   = false;   ///< false when the broad phase gave up

    /*!
     * \brief Did conditioning introduce something Triangle will ABORT on?
     *
     * Duplicates are deliberately not in this test.  Measured on a real
     * 2363-node model (tests/manual/mesh_minsize/README.md), 114 duplicate
     * segments meshed without complaint — Triangle detects a repeated segment
     * and drops it — whereas the run that died carried collinear overlaps and
     * a zero-length segment as well.  Abandoning on duplicates alone would
     * make the whole feature inert on any model with two near-parallel
     * alignments, which is most of them.  They are still counted and reported,
     * because a duplicate means two constraint alignments were welded onto the
     * same geometry and one of them will lose its edge marker.
     */
    [[nodiscard]] bool worseThan(const PslgCensus &base) const
    {
        return crossings  > base.crossings
            || zeroLength > base.zeroLength
            || overlaps   > base.overlaps
            // A ring folded onto itself IS gated, unlike a plain duplicate:
            // the duplicated edges are the same tolerated shape, but the
            // boundary has stopped enclosing anything and Triangle fails.
            || degenerateRings > base.degenerateRings;
    }

    [[nodiscard]] QString describe() const
    {
        return QStringLiteral("crossings %1, duplicate segs %2, zero-length %3, "
                              "collinear overlaps %4, degenerate rings %5")
            .arg(crossings).arg(duplicates).arg(zeroLength).arg(overlaps)
            .arg(degenerateRings);
    }
};

PslgCensus censusPslg(const QVector<Path> &paths, const Pool &pool, double radius)
{
    PslgCensus c;

    // Duplicate and zero-length edges are a linear pass — no proximity search
    // needed, since the pool has already unified bit-identical coordinates, so
    // identical geometry means an identical index pair.
    QSet<QPair<int, int>> seenEdges;
    for (const Path &p : paths)
    {
        if (p.dropped) continue;
        const int ec = p.edgeCount();
        for (int e = 0; e < ec; ++e)
        {
            const int a = pool.findConst(p.edgeA(e));
            const int b = pool.findConst(p.edgeB(e));
            if (a == b || pool.pos[a] == pool.pos[b]) { ++c.zeroLength; continue; }
            const auto k = a < b ? qMakePair(a, b) : qMakePair(b, a);
            if (seenEdges.contains(k)) ++c.duplicates;
            else                       seenEdges.insert(k);
        }
    }

    // The only member of the census that looks at a whole path rather than a
    // pair of edges.  It has to run before the grid, because it is the one
    // check that stays valid even when the broad phase gives up below.
    for (const Path &p : paths)
    {
        if (p.dropped || !p.closed) continue;
        QVector<QPointF> ring;
        ring.reserve(p.v.size());
        for (const int vi : p.v) ring.append(pool.pos[pool.findConst(vi)]);
        if (ringEnclosesNoArea(ring, radius)) ++c.degenerateRings;
    }

    EdgeGrid grid;
    grid.build(paths, pool, radius);
    if (grid.overflow) return c;                 // verified stays false

    // Deduplicate the FINDINGS, not the tests: a pair straddling several grid
    // cells is offered more than once, and these are rare enough that the sets
    // stay tiny.
    QSet<QPair<qint64, qint64>> crossed, overlapped;
    const bool ok = grid.forEachCoLocatedPair([&](EdgeRef ra, EdgeRef rb) {
        const Path &pa = paths[ra.path];
        const Path &pb = paths[rb.path];
        const int a0 = pa.edgeA(ra.e), a1 = pa.edgeB(ra.e);
        const int b0 = pb.edgeA(rb.e), b1 = pb.edgeB(rb.e);
        const QPointF &A = pool.pos[a0], &B = pool.pos[a1];
        const QPointF &C = pool.pos[b0], &D = pool.pos[b1];

        // An overlap is checked even when the two share a vertex: two legs
        // leaving one vertex along the same ray overlap for their whole
        // shorter length, and that is a Triangle abort, not a legal T.
        const bool sameEdge = (a0 == b0 && a1 == b1) || (a0 == b1 && a1 == b0);
        if (!sameEdge && collinearOverlap(A, B, C, D))
            overlapped.insert(pairKey(ra, rb));

        if (a0 == b0 || a0 == b1 || a1 == b0 || a1 == b1) return;  // shares a vertex
        if (properlyCrosses(A, B, C, D)) crossed.insert(pairKey(ra, rb));
    });
    if (!ok) return c;                           // pair budget blown

    c.crossings = static_cast<int>(crossed.size());
    c.overlaps  = static_cast<int>(overlapped.size());
    c.verified  = true;
    return c;
}

} // namespace

// ---------------------------------------------------------------------------
// analyseLocalFeatureSize
// ---------------------------------------------------------------------------

QVector<Violation> analyseLocalFeatureSize(
    const QVector<QPolygonF>         &domains,
    const QVector<QVector<QPointF>>  &holeRings,
    const QVector<ConstraintSegment> &segs,
    const QVector<SteinerPoint>      &pts,
    double h,
    int maxReported)
{
    QVector<Violation> out;
    if (h <= 0.0) return out;

    Build b;
    QVector<int> steinerVertex;
    buildPool(&b, domains, holeRings, segs, pts, &steinerVertex,
              /*ringsReadOnly=*/false);

    const double h2 = h * h;

    // Bounded worst-N.  Every stage below can fire on a large fraction of the
    // geometry — a densified boundary against a coarse h flags nearly every
    // edge — so collecting first and truncating last would allocate hundreds
    // of megabytes on exactly the models this feature exists for.  Compact
    // whenever the buffer reaches a small multiple of the cap.
    const int keep = (maxReported >= 0) ? std::max(maxReported, 1) : -1;
    const qsizetype watermark =
        (keep > 0) ? static_cast<qsizetype>(keep) * 4 : std::numeric_limits<qsizetype>::max();
    auto compact = [&] {
        std::nth_element(out.begin(), out.begin() + keep, out.end(),
                         [](const Violation &a, const Violation &c) {
                             return a.lfs < c.lfs;
                         });
        out.resize(keep);
    };
    auto push = [&](const Violation &v) {
        out.append(v);
        if (keep > 0 && out.size() >= watermark) compact();
    };

    // Tag lookup per pool vertex, for readable reports.
    QVector<QString> vtag(b.pool.pos.size());
    for (const Path &p : std::as_const(b.paths))
        if (!p.tag.isEmpty())
            for (const int vi : p.v)
                if (vtag[vi].isEmpty()) vtag[vi] = p.tag;

    // ── 1. Short constrained edges ───────────────────────────────────────
    for (const Path &p : std::as_const(b.paths))
    {
        const int ec = p.edgeCount();
        for (int e = 0; e < ec; ++e)
        {
            const QPointF &a = b.pool.pos[p.edgeA(e)];
            const QPointF &c = b.pool.pos[p.edgeB(e)];
            const double len = std::hypot(c.x() - a.x(), c.y() - a.y());
            if (len > 0.0 && len < h)
                push({QPointF((a.x() + c.x()) / 2, (a.y() + c.y()) / 2),
                      len, ViolationCause::ShortSegment, p.tag, {}});
        }
    }

    // ── 2. Vertex within h of a non-incident edge ────────────────────────
    EdgeGrid grid;
    grid.build(b.paths, b.pool, h);
    if (!grid.overflow)
    {
        QVector<EdgeRef> near;
        for (int vi = 0; vi < b.pool.pos.size(); ++vi)
        {
            const QPointF &p = b.pool.pos[vi];
            grid.nearEdges(p, &near);
            double worst = std::numeric_limits<double>::max();
            QString worstTag;
            for (const EdgeRef &r : std::as_const(near))
            {
                const Path &pa = b.paths[r.path];
                const int a0 = pa.edgeA(r.e), a1 = pa.edgeB(r.e);
                if (a0 == vi || a1 == vi) continue;          // incident
                const double d2 = distSqToSegment(p, b.pool.pos[a0],
                                                     b.pool.pos[a1]);
                if (d2 < h2 && d2 < worst) { worst = d2; worstTag = pa.tag; }
            }
            if (worst < h2)
                push({p, std::sqrt(worst), ViolationCause::CloseFeatures,
                      vtag[vi], worstTag});
        }
    }

    // ── 3. Small angles ─────────────────────────────────────────────────
    // At radius h from the apex, two legs meeting at theta are only
    // 2*h*sin(theta/2) apart; below h — i.e. theta < 60 degrees, Ruppert's
    // classic threshold — the apex cannot hold a cell of size h.
    {
        QHash<int, QVector<int>> nbr;
        for (const Path &p : std::as_const(b.paths))
        {
            const int ec = p.edgeCount();
            for (int e = 0; e < ec; ++e)
            {
                nbr[p.edgeA(e)].append(p.edgeB(e));
                nbr[p.edgeB(e)].append(p.edgeA(e));
            }
        }
        for (auto it = nbr.constBegin(); it != nbr.constEnd(); ++it)
        {
            const QVector<int> &ns = it.value();
            if (ns.size() < 2) continue;
            const QPointF &v = b.pool.pos[it.key()];
            double worstTheta = 180.0;
            for (int i = 0; i < ns.size(); ++i)
                for (int j = i + 1; j < ns.size(); ++j)
                    worstTheta = std::min(worstTheta,
                        angleAt(v, b.pool.pos[ns[i]], b.pool.pos[ns[j]]));
            const double chord = 2.0 * h * std::sin(worstTheta * M_PI / 360.0);
            if (chord < h)
                push({v, chord, ViolationCause::SmallAngle,
                      vtag[it.key()], {}});
        }
    }

    // ── 4. Sub-scale rings ──────────────────────────────────────────────
    for (const Path &p : std::as_const(b.paths))
    {
        if (p.kind == PathKind::Constraint) continue;
        QVector<QPointF> ring;
        ring.reserve(p.v.size());
        for (const int vi : p.v) ring.append(b.pool.pos[vi]);
        const double w = ringWidthProxy(ring);
        if (w > 0.0 && w < h && !ring.isEmpty())
            push({ring.first(), w, ViolationCause::SubScaleRing, p.tag, {}});
    }

    std::stable_sort(out.begin(), out.end(),
                     [](const Violation &a, const Violation &c) {
                         return a.lfs < c.lfs;
                     });
    if (maxReported >= 0 && out.size() > static_cast<qsizetype>(maxReported))
        out.resize(maxReported);
    return out;
}

// ---------------------------------------------------------------------------
// conditionMinSize
// ---------------------------------------------------------------------------

bool conditionMinSize(QVector<QPolygonF>         *domains,
                      QVector<QVector<QPointF>>  *holeRings,
                      QVector<ConstraintSegment> *segs,
                      QVector<SteinerPoint>      *pts,
                      const MinSizePolicy        &policyIn,
                      ConditionReport            *report,
                      const std::function<bool()> &isCancelled)
{
    Q_ASSERT(domains && holeRings && segs && pts && report);

    MinSizePolicy policy = policyIn;
    policy.resolveDefaults();
    *report = ConditionReport{};
    if (!policy.enabled()) return true;

    const double h   = policy.minCellSize;
    const double wr  = policy.weldRadius;
    const int    cap = policy.maxResiduals;

    auto cancelled = [&] { return isCancelled && isCancelled(); };

    // Snapshot for the fail-safe.
    const QVector<QPolygonF>         domains0   = *domains;
    const QVector<QVector<QPointF>>  holeRings0 = *holeRings;
    const QVector<ConstraintSegment> segs0      = *segs;
    const QVector<SteinerPoint>      pts0       = *pts;

    auto restore = [&] {
        *domains = domains0; *holeRings = holeRings0;
        *segs = segs0;       *pts = pts0;
        report->conditioningAbandoned = true;
    };

    for (const QPolygonF &d : std::as_const(*domains))
    {
        QVector<QPointF> ring;
        ring.reserve(d.size());
        for (const QPointF &q : d) ring.append(q);
        report->domainAreaBefore += std::abs(ringSignedArea(ring));

        // Diagnostic only -- this deliberately does NOT clamp the weld radius.
        // A user who asks for a large h is asking for it; the point is to say
        // in the log, BEFORE conditioning runs, why the abandon they are about
        // to see was likely. ringWidthProxy is 4A/P, the width of the
        // equivalent rectangle, which is what a fold actually depends on.
        const double w = ringWidthProxy(ring);
        if (w > 0.0 && (report->domainNarrowestExtent <= 0.0
                        || w < report->domainNarrowestExtent))
            report->domainNarrowestExtent = w;
    }
    report->weldRadiusDominatesDomain =
        report->domainNarrowestExtent > 0.0
        && wr >= 0.5 * report->domainNarrowestExtent;

    // Baseline census on the UNTOUCHED input.  It has to be measured here, not
    // after resampling: dropping vertices can itself introduce a degeneracy,
    // and invariant (5) is "no worse than what we were handed".
    PslgCensus censusBefore;
    {
        Build b0;
        QVector<int> sv0;
        buildPool(&b0, domains0, holeRings0, segs0, pts0, &sv0,
                  policy.ringsReadOnly);
        censusBefore = censusPslg(b0.paths, b0.pool, wr);
        report->crossingsBefore = censusBefore.crossings;
        if (!censusBefore.verified)
        {
            // Broad phase gave up (chunk or pair budget). We cannot verify
            // invariant (5), so do not risk handing Triangle a PSLG we have
            // not checked.
            report->abandonReason =
                QStringLiteral("input census could not be verified "
                               "(segment-chunk or pair budget exceeded)");
            restore();
            return false;
        }
    }

    // Baseline WORST FEATURE SCALE on the untouched input (V2 plan Phase 0
    // guard).  Conditioning exists to RAISE the local feature size; measured
    // on real models it can instead create features smaller than anything in
    // the input (weld/split interactions — the h = 8 "6× vertices with a
    // SMALLER minimum cell" result in MIN_CELL_SIZE_TESTING_RESULTS_2026-08-18
    // §4), and Triangle then refines to the new, worse scale.  So the same
    // no-worse contract the census enforces for degeneracies is enforced for
    // the feature scale: commit only when the conditioned worst lfs is at
    // least the input's worst lfs.
    double beforeWorstLfs = h;
    {
        const QVector<Violation> worst = analyseLocalFeatureSize(
            domains0, holeRings0, segs0, pts0, h, 1);
        if (!worst.isEmpty()) beforeWorstLfs = worst.first().lfs;
    }

    // ── Stage 1/2 on the caller's vectors: length resampling, collapse ───
    // Done before the pool is built so the pool never contains vertices that
    // resampling is about to delete.
    {
        int flagged = 0;
        if (!policy.ringsReadOnly)
        {
            for (QPolygonF &d : *domains)
            {
                QVector<QPointF> ring;
                ring.reserve(d.size());
                for (const QPointF &q : d) ring.append(q);
                const QVector<QPointF> res = resampleRingMinLength(
                    ring, policy.minSegmentLen, policy.maxDeviation, &flagged);
                if (res.size() != ring.size())
                {
                    ++report->pathsShortened;
                    d = QPolygonF(res);
                }
            }
            for (QVector<QPointF> &hr : *holeRings)
            {
                if (hr.size() < 3) continue;
                const int before = hr.size();
                const QVector<QPointF> res = resampleRingMinLength(
                    hr, policy.minSegmentLen, policy.maxDeviation, &flagged);
                if (res.size() != before) { ++report->pathsShortened; hr = res; }
            }
        }

        QVector<ConstraintSegment> kept;
        kept.reserve(segs->size());
        for (const ConstraintSegment &cs : std::as_const(*segs))
        {
            if (cs.path.size() < 2) continue;
            const double total = polylineLength(cs.path);
            if (policy.collapseSubScalePaths && total < policy.minSegmentLen)
            {
                // Cannot be a breakline at this resolution: demote to a
                // Steiner point, exactly as nodeMinSeparation demotes
                // crowded nodes.  Coupling survives via the post-generation
                // node mapper.
                SteinerPoint sp;
                const QPointF a = cs.path.first(), z = cs.path.last();
                sp.xy     = QPointF((a.x() + z.x()) / 2.0, (a.y() + z.y()) / 2.0);
                sp.marker = cs.marker;
                sp.tag    = cs.tag;
                pts->append(sp);
                ++report->pathsCollapsed;
                addResidual(report, cap,
                            {sp.xy, total, ViolationCause::ShortSegment, cs.tag, {}});
                continue;
            }
            ConstraintSegment out = cs;
            const int before = out.path.size();
            out.path = resampleMinLength(out.path, policy.minSegmentLen,
                                         policy.maxDeviation, &flagged);
            if (out.path.size() != before) ++report->pathsShortened;
            if (out.path.size() >= 2) kept.append(std::move(out));
        }
        *segs = kept;
        report->shortEdgesKept = flagged;
    }

    if (cancelled()) { restore(); return false; }

    // Sub-scale hole rejection.  An emptied ring is exactly what the existing
    // prepareHoleRing() treats as invalid, and the worker already skips those
    // by index — so emptying rather than erasing keeps holeRings index-parallel
    // with bprep.holeSeeds / bprep.holeValid downstream.
    if (policy.dropSubScaleHoles && !policy.ringsReadOnly)
    {
        for (QVector<QPointF> &hr : *holeRings)
        {
            if (hr.size() < 3) continue;
            const double area = std::abs(ringSignedArea(hr));
            const double wid  = ringWidthProxy(hr);
            if (area < h * h || wid < h)
            {
                addResidual(report, cap, {hr.first(), std::min(wid, std::sqrt(area)),
                                          ViolationCause::SubScaleRing, {}, {}});
                hr.clear();
                ++report->holesDropped;
            }
        }
    }

    if (cancelled()) { restore(); return false; }

    // ── Build the unified pool ──────────────────────────────────────────
    Build b;
    QVector<int> steinerVertex;
    buildPool(&b, *domains, *holeRings, *segs, *pts, &steinerVertex,
              policy.ringsReadOnly);

    // Pool vertex -> the identity it carries, for the IdentityMerged residual.
    // Built once; only consulted when allowIdentityMerge actually fires.
    QHash<int, QString> identityTag;
    if (policy.allowIdentityMerge)
    {
        for (int i = 0; i < pts->size() && i < steinerVertex.size(); ++i)
            if (!(*pts)[i].tag.isEmpty() && steinerVertex[i] >= 0)
                identityTag.insert(steinerVertex[i], (*pts)[i].tag);
        for (const Path &p : std::as_const(b.paths))
        {
            if (p.tag.isEmpty() || p.v.isEmpty() || p.closed) continue;
            identityTag.insert(p.v.first(), p.tag);
            identityTag.insert(p.v.last(),  p.tag);
        }
    }
    auto tagOf = [&](int v) {
        return identityTag.value(b.pool.find(v), QStringLiteral("(untagged)"));
    };

    // ── Stage 3: weld / fix-up / crossing repair to a fixed point ───────
    for (int iter = 0; iter < policy.maxIterations; ++iter)
    {
        if (cancelled()) { restore(); return false; }
        report->iterationsUsed = iter + 1;
        bool changed = false;

        // 3a. Greedy priority-ordered welding.  Input order inside a priority
        // band is the tie-break, so the pass is deterministic.
        {
            QVector<int> order;
            order.reserve(b.pool.pos.size());
            for (int i = 0; i < b.pool.pos.size(); ++i)
                if (b.pool.find(i) == i) order.append(i);
            std::stable_sort(order.begin(), order.end(), [&](int x, int y) {
                return b.pool.prio[x] > b.pool.prio[y];
            });

            PointGrid accepted(wr);
            for (const int v : std::as_const(order))
            {
                const QPointF &p = b.pool.pos[v];
                const int rep = b.pool.isLocked(v)
                    ? -1     // locked vertices cannot move; always their own rep
                    : accepted.nearest(p, wr, b.pool.pos, [&](int j) {
                          // Two distinct identities (nodes / conduit ends) must
                          // never merge into one another -- unless the caller
                          // has explicitly opted in, and then only within
                          // identityMergeRadius, which may be tighter than the
                          // outer weld radius this search used.
                          if (!(b.pool.isIdentity(v) && b.pool.isIdentity(j)))
                              return true;
                          if (!policy.allowIdentityMerge) return false;
                          const double dx = b.pool.pos[j].x() - b.pool.pos[v].x();
                          const double dy = b.pool.pos[j].y() - b.pool.pos[v].y();
                          return std::hypot(dx, dy) <= policy.identityMergeRadius;
                      });
                if (rep >= 0)
                {
                    const double d = std::hypot(b.pool.pos[rep].x() - p.x(),
                                                b.pool.pos[rep].y() - p.y());
                    report->maxDisplacement = std::max(report->maxDisplacement, d);

                    // Instrumented HERE, at the one place that sees all three
                    // merge shapes (node-node, node-conduit-end, end-end).
                    // The priority order already guarantees the survivor is the
                    // higher-ranked identity, so `rep` is never subsumed.
                    if (policy.allowIdentityMerge
                        && b.pool.isIdentity(v) && b.pool.isIdentity(rep))
                    {
                        ++report->identitiesMerged;
                        addResidual(report, cap,
                            {b.pool.pos[rep], d, ViolationCause::IdentityMerged,
                             tagOf(v), tagOf(rep)});
                    }

                    b.pool.link(v, rep);
                    ++report->verticesWelded;
                    changed = true;
                }
                else
                {
                    if (!b.pool.isLocked(v) && b.pool.isIdentity(v))
                    {
                        // Report an identity that could not be separated.
                        const int other = accepted.nearest(
                            p, wr, b.pool.pos, [&](int) { return true; });
                        if (other >= 0)
                            addResidual(report, cap,
                                {p, std::hypot(b.pool.pos[other].x() - p.x(),
                                               b.pool.pos[other].y() - p.y()),
                                 ViolationCause::CloseFeatures, {}, {}});
                    }
                    accepted.insert(v, p);
                }
            }
        }

        // 3b. Rewrite paths through the union-find and collapse duplicates.
        for (Path &p : b.paths)
        {
            if (p.dropped) continue;
            QVector<int> nv;
            nv.reserve(p.v.size());
            for (const int vi : std::as_const(p.v))
            {
                const int r = b.pool.find(vi);
                if (!nv.isEmpty() && nv.last() == r) continue;
                nv.append(r);
            }
            if (p.closed && nv.size() >= 2 && nv.first() == nv.last())
                nv.removeLast();

            // Two ways a ring dies here.  Too few vertices is the obvious
            // one.  The other is a FOLD: welding maps the ring's two sides
            // onto each other, so it runs out and back through the same
            // representatives and encloses nothing.  A fold keeps its vertex
            // count -- only CONSECUTIVE duplicates were collapsed above, and
            // an out-and-back path has none -- so a count test cannot see it.
            bool ringDead = p.closed && nv.size() < 3;
            if (p.closed && !ringDead)
            {
                QVector<QPointF> rr;
                rr.reserve(nv.size());
                for (const int r : std::as_const(nv)) rr.append(b.pool.pos[r]);
                ringDead = ringEnclosesNoArea(rr, wr);
            }
            if (ringDead)
            {
                if (p.kind == PathKind::Domain)
                {
                    // The minimum size is too coarse for this domain: welding
                    // collapsed its outline.  Nothing safe to hand Triangle.
                    report->abandonReason =
                        QStringLiteral("welding collapsed a domain ring at "
                                       "weld radius %1 (it encloses no area); "
                                       "the minimum cell size is too coarse "
                                       "for this domain")
                            .arg(wr, 0, 'g', 4);
                    restore();
                    return false;
                }
                // A ring the sub-scale drop above already emptied arrives here
                // with no vertices.  It was counted then, so counting it again
                // would double-report the modelling change to the user.
                const bool wasAlreadyEmpty = p.v.isEmpty();
                p.dropped = true;
                if (!wasAlreadyEmpty)
                {
                    ++report->holesDropped;
                    changed = true;
                }
                continue;
            }
            if (!p.closed && nv.size() < 2)
            {
                // Collapsed to a point: demote, as in stage 1.
                if (!nv.isEmpty())
                {
                    SteinerPoint sp;
                    sp.xy     = b.pool.pos[nv.first()];
                    sp.marker = p.marker;
                    sp.tag    = p.tag;
                    pts->append(sp);
                    steinerVertex.append(nv.first());
                }
                p.dropped = true;
                ++report->pathsCollapsed;
                changed = true;
                continue;
            }
            if (nv.size() != p.v.size()) changed = true;
            p.v = std::move(nv);
        }

        if (cancelled()) { restore(); return false; }

        // 3c. Vertex-edge fix-up: weld a vertex onto a segment passing too
        // close to it, by splitting that segment at the vertex.  Both are
        // representatives, hence >= wr apart from the split edge's endpoints,
        // so the two new sub-edges are >= wr long.
        {
            EdgeGrid grid;
            grid.build(b.paths, b.pool, wr);
            if (grid.overflow) { restore(); return false; }

            // path -> edge -> list of (t along edge, vertex)
            QHash<int, QHash<int, QVector<QPair<double, int>>>> inserts;
            QVector<EdgeRef> near;
            const double wr2 = wr * wr;

            // Splitting edge (a0,a1) at v emits the sub-edges (a0,v) and
            // (v,a1).  If one of those already exists — most often because the
            // MIRROR-IMAGE fix-up has just been scheduled, v into an edge at w
            // while w goes into an edge at v — the PSLG ends up carrying the
            // same segment twice.  That is not a crossing, so nothing
            // downstream sees it, and Triangle aborts with "Topological
            // inconsistency after splitting a segment".  It is not exotic
            // either: two protected identities closer than wr produce it every
            // time, which is the normal case around a pair of nearby manholes.
            //
            // So claim the adjacency before creating it, and decline the
            // second half of a mirror pair.  The proximity is then left
            // unresolved and reported, which is the design's answer for
            // anything it may not move.
            QSet<QPair<int, int>> adjacency;
            auto adjKey = [](int x, int y) {
                return x < y ? qMakePair(x, y) : qMakePair(y, x);
            };
            for (const Path &pp : std::as_const(b.paths))
            {
                if (pp.dropped) continue;
                const int ec = pp.edgeCount();
                for (int e = 0; e < ec; ++e)
                    adjacency.insert(adjKey(pp.edgeA(e), pp.edgeB(e)));
            }

            for (int vi = 0; vi < b.pool.pos.size(); ++vi)
            {
                if (b.pool.find(vi) != vi) continue;
                const QPointF &p = b.pool.pos[vi];
                grid.nearEdges(p, &near);
                for (const EdgeRef &r : std::as_const(near))
                {
                    const Path &pa = b.paths[r.path];
                    if (pa.dropped) continue;
                    const int a0 = pa.edgeA(r.e), a1 = pa.edgeB(r.e);
                    if (a0 == vi || a1 == vi) continue;
                    // Not just the split edge's own endpoints: if vi appears
                    // anywhere else in this path, splicing it in again makes
                    // the path visit it twice and can emit a duplicate segment.
                    if (pa.v.contains(vi)) continue;
                    const QPointF &A = b.pool.pos[a0];
                    const QPointF &B = b.pool.pos[a1];
                    if (distSqToSegment(p, A, B) >= wr2) continue;

                    if (pa.readOnly)
                    {
                        addResidual(report, cap,
                            {p, std::sqrt(distSqToSegment(p, A, B)),
                             ViolationCause::CloseFeatures, pa.tag, {}});
                        continue;
                    }
                    const double abx = B.x() - A.x(), aby = B.y() - A.y();
                    const double len2 = abx * abx + aby * aby;
                    if (len2 <= 0.0) continue;
                    const double t =
                        ((p.x() - A.x()) * abx + (p.y() - A.y()) * aby) / len2;

                    // Only a projection STRICTLY INSIDE the edge is a
                    // vertex-on-segment to be repaired by splitting.  When the
                    // nearest point is an endpoint, the vertex is simply too
                    // close to that endpoint — a separation problem, which
                    // welding either already solved or is forbidden to solve
                    // because both are coupling identities.  Splicing it in
                    // anyway makes the path double back over itself: the new
                    // leg lies ON the edge it was spliced into, which is a
                    // collinear overlap and a Triangle abort.
                    if (!(t > 0.0 && t < 1.0))
                    {
                        addResidual(report, cap,
                            {p, std::sqrt(distSqToSegment(p, A, B)),
                             ViolationCause::CloseFeatures, pa.tag, {}});
                        continue;
                    }
                    if (adjacency.contains(adjKey(vi, a0))
                        || adjacency.contains(adjKey(vi, a1)))
                    {
                        // Would duplicate a segment that already exists (see
                        // the note above).  Report rather than corrupt.
                        addResidual(report, cap,
                            {p, std::sqrt(distSqToSegment(p, A, B)),
                             ViolationCause::CloseFeatures, pa.tag, {}});
                        continue;
                    }
                    adjacency.insert(adjKey(vi, a0));
                    adjacency.insert(adjKey(vi, a1));
                    inserts[r.path][r.e].append(qMakePair(t, vi));
                }
            }

            for (auto pit = inserts.constBegin(); pit != inserts.constEnd(); ++pit)
            {
                Path &p = b.paths[pit.key()];
                if (p.dropped) continue;
                // Apply from the last edge backwards so earlier edge indices
                // stay valid while the vertex list grows.
                QVector<int> edgesDesc = pit.value().keys();
                std::sort(edgesDesc.begin(), edgesDesc.end(), std::greater<int>());
                for (const int e : std::as_const(edgesDesc))
                {
                    QVector<QPair<double, int>> ins = pit.value().value(e);
                    std::sort(ins.begin(), ins.end(),
                              [](const QPair<double, int> &x,
                                 const QPair<double, int> &y) {
                                  return x.first < y.first;
                              });
                    QVector<int> add;
                    add.reserve(ins.size());
                    for (const auto &pr : std::as_const(ins))
                        if (add.isEmpty() || add.last() != pr.second)
                            add.append(pr.second);
                    if (add.isEmpty()) continue;
                    // Edge e runs v[e] -> v[e+1] (wrapping when closed); the
                    // insertion point is therefore e+1.
                    for (int k = add.size() - 1; k >= 0; --k)
                        p.v.insert(e + 1, add[k]);
                    report->segmentsSplit += add.size();
                    changed = true;
                }
            }
        }

        if (cancelled()) { restore(); return false; }

        // 3d. Crossing repair: resolve a strict crossing into a legal shared
        // vertex.  Never attempted on read-only paths.
        {
            EdgeGrid grid;
            grid.build(b.paths, b.pool, wr);
            if (grid.overflow) { restore(); return false; }

            struct Fix { EdgeRef a, c; QPointF x; };
            QVector<Fix> fixes;
            QSet<QPair<qint64, qint64>> seenPairs;
            const bool swept = grid.forEachCoLocatedPair([&](EdgeRef ra, EdgeRef rb) {
                const Path &pa = b.paths[ra.path];
                const Path &pb = b.paths[rb.path];
                if (pa.dropped || pb.dropped) return;
                if (pa.readOnly || pb.readOnly) return;
                const int a0 = pa.edgeA(ra.e), a1 = pa.edgeB(ra.e);
                const int b0 = pb.edgeA(rb.e), b1 = pb.edgeB(rb.e);
                if (a0 == b0 || a0 == b1 || a1 == b0 || a1 == b1) return;
                const QPointF &A = b.pool.pos[a0], &B = b.pool.pos[a1];
                const QPointF &C = b.pool.pos[b0], &D = b.pool.pos[b1];
                if (!properlyCrosses(A, B, C, D)) return;
                const auto key = pairKey(ra, rb);
                if (seenPairs.contains(key)) return;   // reported from >1 cell
                seenPairs.insert(key);
                QPointF x;
                if (segmentIntersection(A, B, C, D, &x))
                    fixes.append({ra, rb, x});
            });
            if (!swept) { restore(); return false; }

            // Splicing a vertex into edge e shifts every edge index above e in
            // that path, so fixes cannot be applied in discovery order: group
            // them per path and insert from the highest edge index downwards,
            // the same discipline stage 3c uses.  At most one fix per edge per
            // iteration; anything else waits for the next one.
            // `fixes` came out of a QHash walk (forEachCoLocatedPair), and Qt 6
            // randomises the hash seed per process.  The loop below is
            // order-SENSITIVE twice over: `touchedEdges` lets only the first
            // fix on an edge through, and `pool.add` hands out vertex indices
            // in call order.  Left unsorted the same project meshes differently
            // on two runs.  pairKey is the canonical edge-pair identity.
            std::sort(fixes.begin(), fixes.end(),
                      [](const Fix &x, const Fix &y) {
                          return pairKey(x.a, x.c) < pairKey(y.a, y.c);
                      });

            QHash<int, QVector<QPair<int, int>>> perPath;   // path -> (edge, vertex)
            QSet<qint64> touchedEdges;
            for (const Fix &f : std::as_const(fixes))
            {
                if (touchedEdges.contains(edgeKey(f.a))
                    || touchedEdges.contains(edgeKey(f.c))) continue;
                touchedEdges.insert(edgeKey(f.a));
                touchedEdges.insert(edgeKey(f.c));

                const int xv = b.pool.add(f.x, kPrioPlain);
                perPath[f.a.path].append(qMakePair(f.a.e, xv));
                perPath[f.c.path].append(qMakePair(f.c.e, xv));
                ++report->crossingsRepaired;
                changed = true;
            }
            for (auto pit = perPath.begin(); pit != perPath.end(); ++pit)
            {
                QVector<QPair<int, int>> &ins = pit.value();
                std::sort(ins.begin(), ins.end(),
                          [](const QPair<int, int> &x, const QPair<int, int> &y) {
                              return x.first > y.first;    // descending edge index
                          });
                Path &p = b.paths[pit.key()];
                for (const auto &pr : std::as_const(ins))
                    p.v.insert(pr.first + 1, pr.second);
            }
        }

        if (!changed) break;
    }

    if (cancelled()) { restore(); return false; }

    // ── Stage 4: corner trim ────────────────────────────────────────────
    // Only simple corners (exactly two incident edges) are trimmed. With three
    // or more legs the apex survives whatever pair we blunt, so trimming would
    // not achieve the goal — those are reported instead.
    if (policy.trimAngleDeg > 0.0)
    {
        struct Inc { int path; int posInPath; int nbr; };
        QHash<int, QVector<Inc>> inc;
        for (int pi = 0; pi < b.paths.size(); ++pi)
        {
            const Path &p = b.paths[pi];
            if (p.dropped) continue;
            const int ec = p.edgeCount();
            for (int e = 0; e < ec; ++e)
            {
                const int ia = e, ib = (e + 1) % p.v.size();
                inc[p.v[ia]].append({pi, ia, p.v[ib]});
                inc[p.v[ib]].append({pi, ib, p.v[ia]});
            }
        }

        struct Bridge { int a; int b; };
        QVector<Bridge> bridges;
        // Collected first, applied after, so position indices stay valid.
        struct Trim { int v; Inc l0; Inc l1; QPointF p0; QPointF p1; };
        QVector<Trim> trims;

        // Walk `inc` in vertex order, not QHash order.  Two things downstream
        // depend on it: `trims` feeds pool.add, which numbers vertices in call
        // order, and addResidual fills a list capped at maxResiduals, so hash
        // order decides WHICH residuals the user is shown.  Pool indices are
        // assigned during path build and are themselves deterministic.
        QVector<int> incKeys = inc.keys();
        std::sort(incKeys.begin(), incKeys.end());

        for (const int incVertex : std::as_const(incKeys))
        {
            const auto it = inc.constFind(incVertex);
            const QVector<Inc> &legs = it.value();
            if (legs.size() != 2)
            {
                if (legs.size() > 2)
                {
                    const QPointF &v = b.pool.pos[it.key()];
                    double worst = 180.0;
                    for (int i = 0; i < legs.size(); ++i)
                        for (int j = i + 1; j < legs.size(); ++j)
                            worst = std::min(worst, angleAt(v, b.pool.pos[legs[i].nbr],
                                                               b.pool.pos[legs[j].nbr]));
                    if (worst < policy.trimAngleDeg)
                    {
                        ++report->cornersSkipped;
                        addResidual(report, cap,
                            {v, worst, ViolationCause::SmallAngle, {}, {}});
                    }
                }
                continue;
            }
            const int vi = it.key();
            if (b.paths[legs[0].path].readOnly || b.paths[legs[1].path].readOnly)
                continue;
            if (b.pool.prio[vi] >= kPrioNode && !policy.trimAtTaggedNodes)
            {
                const QPointF &v = b.pool.pos[vi];
                if (angleAt(v, b.pool.pos[legs[0].nbr], b.pool.pos[legs[1].nbr])
                        < policy.trimAngleDeg)
                {
                    ++report->cornersSkipped;
                    addResidual(report, cap,
                        {v, 0.0, ViolationCause::SmallAngle, {}, {}});
                }
                continue;
            }

            const QPointF &v = b.pool.pos[vi];
            const QPointF &p = b.pool.pos[legs[0].nbr];
            const QPointF &q = b.pool.pos[legs[1].nbr];
            const double theta = angleAt(v, p, q);
            if (theta >= policy.trimAngleDeg) continue;

            const double s = std::sin(theta * M_PI / 360.0);   // sin(theta/2)
            if (s <= 0.0) { ++report->cornersSkipped; continue; }
            const double rNeed = h / (2.0 * s);
            const double lp = std::hypot(p.x() - v.x(), p.y() - v.y());
            const double lq = std::hypot(q.x() - v.x(), q.y() - v.y());
            const double rMax = 0.4 * std::min(lp, lq);
            if (rNeed > rMax)
            {
                // Trimming enough would eat the legs; leave it and report.
                ++report->cornersSkipped;
                addResidual(report, cap,
                    {v, theta, ViolationCause::SmallAngle, {}, {}});
                continue;
            }
            const QPointF p0(v.x() + (p.x() - v.x()) * rNeed / lp,
                             v.y() + (p.y() - v.y()) * rNeed / lp);
            const QPointF p1(v.x() + (q.x() - v.x()) * rNeed / lq,
                             v.y() + (q.y() - v.y()) * rNeed / lq);
            trims.append({vi, legs[0], legs[1], p0, p1});
        }

        for (const Trim &t : std::as_const(trims))
        {
            const int n0 = b.pool.add(t.p0, kPrioPlain);
            const int n1 = b.pool.add(t.p1, kPrioPlain);
            if (n0 == n1) { ++report->cornersSkipped; continue; }

            if (t.l0.path == t.l1.path)
            {
                // Hairpin inside one path: [.. p, v, q ..] -> [.. p, n0, n1, q ..]
                Path &p = b.paths[t.l0.path];
                const int at = t.l0.posInPath;
                if (at < 0 || at >= p.v.size() || p.v[at] != t.v)
                { ++report->cornersSkipped; continue; }
                // Orient the pair so n0 sits on the side of leg l0's neighbour.
                const int prevIdx = (at - 1 + p.v.size()) % p.v.size();
                const bool prevIsL0 = (p.v[prevIdx] == t.l0.nbr);
                p.v[at] = prevIsL0 ? n0 : n1;
                p.v.insert(at + 1, prevIsL0 ? n1 : n0);
                ++report->cornersTrimmed;
            }
            else
            {
                Path &pa = b.paths[t.l0.path];
                Path &pb = b.paths[t.l1.path];
                if (t.l0.posInPath >= pa.v.size() || pa.v[t.l0.posInPath] != t.v
                 || t.l1.posInPath >= pb.v.size() || pb.v[t.l1.posInPath] != t.v)
                { ++report->cornersSkipped; continue; }
                pa.v[t.l0.posInPath] = n0;
                pb.v[t.l1.posInPath] = n1;
                bridges.append({n0, n1});
                ++report->cornersTrimmed;
            }
        }

        for (const Bridge &br : std::as_const(bridges))
        {
            Path p;
            p.kind   = PathKind::Constraint;
            p.src    = -1;
            p.marker = 0;
            p.closed = false;
            p.v      = {br.a, br.b};
            b.paths.append(std::move(p));
        }
    }

    if (cancelled()) { restore(); return false; }

    // ── Verify invariant (5) before committing ──────────────────────────
    // The whole census, not just the crossings: see PslgCensus.
    {
        const PslgCensus after = censusPslg(b.paths, b.pool, wr);
        report->crossingsAfter    = after.crossings;
        report->duplicateSegments =
            std::max(0, after.duplicates - censusBefore.duplicates);
        if (!after.verified)
        {
            report->abandonReason =
                QStringLiteral("conditioned census could not be verified "
                               "(segment-chunk or pair budget exceeded)");
            restore();
            return false;
        }
        if (after.worseThan(censusBefore))
        {
            report->abandonReason = QStringLiteral("%1  ->  %2")
                .arg(censusBefore.describe(), after.describe());
            restore();
            return false;
        }
    }

    // ── Write back ──────────────────────────────────────────────────────
    auto ringOf = [&](const Path &p) {
        QVector<QPointF> ring;
        ring.reserve(p.v.size() + 1);
        for (const int vi : p.v) ring.append(b.pool.pos[b.pool.find(vi)]);
        if (p.closedDup && !ring.isEmpty()) ring.append(ring.first());
        return ring;
    };

    QVector<ConstraintSegment> outSegs;
    outSegs.reserve(b.paths.size());
    for (const Path &p : std::as_const(b.paths))
    {
        switch (p.kind)
        {
        case PathKind::Domain:
            if (!p.dropped && !p.readOnly && p.src >= 0 && p.src < domains->size())
                (*domains)[p.src] = QPolygonF(ringOf(p));
            break;
        case PathKind::Hole:
            if (p.readOnly || p.src < 0 || p.src >= holeRings->size()) break;
            (*holeRings)[p.src] = p.dropped ? QVector<QPointF>{} : ringOf(p);
            break;
        case PathKind::Constraint:
        {
            if (p.dropped || p.v.size() < 2) break;
            ConstraintSegment cs;
            if (p.src >= 0 && p.src < segs->size())
            {
                cs        = (*segs)[p.src];
                cs.path   = ringOf(p);
            }
            else
            {
                cs.marker = p.marker;
                cs.tag    = p.tag;
                cs.path   = ringOf(p);
            }
            if (cs.path.size() >= 2) outSegs.append(std::move(cs));
            break;
        }
        }
    }
    *segs = outSegs;

    // Steiner points follow their pool vertex; entries that welded onto the
    // same representative collapse to the highest-priority survivor.
    {
        QVector<SteinerPoint> outPts;
        outPts.reserve(pts->size());
        QHash<int, int> seen;                 // rep -> index into outPts
        for (int i = 0; i < pts->size(); ++i)
        {
            const int vi = (i < steinerVertex.size())
                               ? b.pool.find(steinerVertex[i])
                               : -1;
            SteinerPoint sp = (*pts)[i];
            if (vi < 0) { outPts.append(sp); continue; }
            sp.xy = b.pool.pos[vi];
            auto it = seen.constFind(vi);
            if (it == seen.constEnd())
            {
                seen.insert(vi, outPts.size());
                outPts.append(sp);
                continue;
            }
            // Already have a point here: keep the tagged one, and never lose a
            // known elevation.
            SteinerPoint &cur = outPts[*it];
            if (cur.marker == 0 && sp.marker != 0)
            {
                const bool hadZ = cur.hasZ;
                const double z  = cur.z;
                cur = sp;
                if (!cur.hasZ && hadZ) { cur.z = z; cur.hasZ = true; }
            }
            else if (!cur.hasZ && sp.hasZ)
            {
                cur.z = sp.z; cur.hasZ = true;
            }
        }
        *pts = outPts;
    }

    for (const QPolygonF &d : std::as_const(*domains))
    {
        QVector<QPointF> ring;
        ring.reserve(d.size());
        for (const QPointF &q : d) ring.append(q);
        report->domainAreaAfter += std::abs(ringSignedArea(ring));
    }

    // Predicted worst surviving feature scale, for the log line.
    {
        const QVector<Violation> resid =
            analyseLocalFeatureSize(*domains, *holeRings, *segs, *pts, h, cap);
        report->predictedMinLfs = resid.isEmpty() ? h : resid.first().lfs;
        for (const Violation &v : resid) addResidual(report, cap, v);
    }

    // No-worse contract on the feature scale (see beforeWorstLfs above).  A
    // tiny relative slack absorbs pure floating-point jitter in a violation
    // conditioning left untouched.
    if (report->predictedMinLfs < beforeWorstLfs * (1.0 - 1e-9))
    {
        report->abandonReason =
            QStringLiteral("conditioning made the worst feature scale WORSE "
                           "(%1 -> %2) — it would create smaller cells than "
                           "the input demanded")
                .arg(beforeWorstLfs, 0, 'g', 6)
                .arg(report->predictedMinLfs, 0, 'g', 6);
        restore();
        return false;
    }

    return true;
}

} // namespace pslg
} // namespace mesh
