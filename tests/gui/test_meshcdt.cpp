/**
 * @file      test_meshcdt.cpp
 * @brief     Tests for meshcdt.
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 *
 * Constrained Delaunay kernel (MESH_OVERHAUL_PLAN_2026-09-29.md Stage 4):
 * the Delaunay property by brute force, constraint recovery, vertices on
 * constraints, exterior/region removal, exactly cocircular and collinear
 * input, determinism — and quality refinement
 * (MESH_TRIANGLE_ENGINE_PLAN_2026-09-30.md §4): the angle bound, the size
 * bound, grading, small input angles, fixed segments, chains, speed.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#include "mesh/meshcdt.h"

#include <QElapsedTimer>
#include <QRandomGenerator>
#include <QTest>

#include <algorithm>
#include <cmath>
#include <limits>

using mesh::ConstrainedDelaunay;

namespace
{

/*! Brute-force global check: no live triangle's circumcircle strictly
 *  contains another vertex. Valid for an unconstrained triangulation. */
/**
 * @brief Count unconstrained global empty-circumcircle violations using a brute-force reference
 * calculation.
 * @param[in] cdt Stable valid triangulation, preserved; only live non-super triangles tested
 * against all non-super vertices, including inactive vertex slots if present, so use for initial
 * unconstrained fixtures.
 * @return Number of triangles with nonpositive orientation or an interior witness at squared
 * radius fraction<1-1e-9, at most one count per triangle. O(triangles*vertices), no allocation
 * or repair.
 */
int delaunayViolations(const ConstrainedDelaunay &cdt)
{
    int bad = 0;
    const auto &P = cdt.vertices();
    for (const auto &T : cdt.triangles()) {
        if (!T.alive)
            continue;
        if (cdt.isSuperVertex(T.v[0]) || cdt.isSuperVertex(T.v[1]) || cdt.isSuperVertex(T.v[2]))
            continue;
        const QPointF &A = P[T.v[0]], &B = P[T.v[1]], &C = P[T.v[2]];
        const double bx = B.x() - A.x(), by = B.y() - A.y(), cx = C.x() - A.x(), cy = C.y() - A.y();
        const double d = 2.0 * (bx * cy - by * cx);
        if (!(d > 0.0)) {
            ++bad;
            continue;
        } // clockwise or degenerate
        const double b2 = bx * bx + by * by, c2 = cx * cx + cy * cy;
        const double ux = (cy * b2 - by * c2) / d, uy = (bx * c2 - cx * b2) / d;
        const double r2 = ux * ux + uy * uy;
        for (int v = 0; v < P.size(); ++v) {
            if (cdt.isSuperVertex(v) || v == T.v[0] || v == T.v[1] || v == T.v[2])
                continue;
            const double dx = P[v].x() - (A.x() + ux), dy = P[v].y() - (A.y() + uy);
            if (dx * dx + dy * dy < r2 * (1.0 - 1e-9)) {
                ++bad;
                break;
            }
        }
    }
    return bad;
}

/*! Local check, the constrained-Delaunay criterion: across every
 *  unconstrained edge shared by two live triangles, the far vertex is not
 *  strictly inside the near triangle's circumcircle. */
/**
 * @brief Count constrained-Delaunay circumcircle violations across free live neighbor edges.
 * @param[in] cdt Stable valid connectivity/coordinates, preserved; constrained edges and super
 * vertices excluded.
 * @return Number of live real triangles with nonpositive orientation or at least one opposing
 * neighbor vertex within radius-squared*(1-1e-9). Counts per triangle, not unique edge; no
 * mutation or repair.
 */
int localDelaunayViolations(const ConstrainedDelaunay &cdt)
{
    int bad = 0;
    const auto &P = cdt.vertices();
    const auto &Ts = cdt.triangles();
    for (int t = 0; t < Ts.size(); ++t) {
        const auto &T = Ts[t];
        if (!T.alive)
            continue;
        if (cdt.isSuperVertex(T.v[0]) || cdt.isSuperVertex(T.v[1]) || cdt.isSuperVertex(T.v[2]))
            continue;
        const QPointF &A = P[T.v[0]], &B = P[T.v[1]], &C = P[T.v[2]];
        const double bx = B.x() - A.x(), by = B.y() - A.y(), cx = C.x() - A.x(), cy = C.y() - A.y();
        const double d = 2.0 * (bx * cy - by * cx);
        if (!(d > 0.0)) {
            ++bad;
            continue;
        }
        const double b2 = bx * bx + by * by, c2 = cx * cx + cy * cy;
        const double ux = (cy * b2 - by * c2) / d, uy = (bx * c2 - cx * b2) / d;
        const double r2 = ux * ux + uy * uy;
        for (int i = 0; i < 3; ++i) {
            if (T.constrained[i] || T.adj[i] < 0 || !Ts[T.adj[i]].alive)
                continue;
            const auto &N = Ts[T.adj[i]];
            int far = -1;
            for (int j = 0; j < 3; ++j)
                if (N.adj[j] == t)
                    far = N.v[j];
            if (far < 0 || cdt.isSuperVertex(far))
                continue;
            const double dx = P[far].x() - (A.x() + ux), dy = P[far].y() - (A.y() + uy);
            if (dx * dx + dy * dy < r2 * (1.0 - 1e-9)) {
                ++bad;
                break;
            }
        }
    }
    return bad;
}

/**
 * @brief Read the fixture's maintained live-triangle count.
 * @param[in] cdt Stable triangulation, preserved.
 * @return liveTriangleCount, excluding dead slots by kernel accounting; no scan/allocation.
 */
int liveTriangles(const ConstrainedDelaunay &cdt)
{
    return cdt.liveTriangleCount();
}

/*! Adjacency consistency: every adj back-pointer matches and shared edges agree. */
/**
 * @brief Validate reciprocal adjacency, shared-edge orientation and matching constraint flags
 * across all slots.
 * @param[in] cdt Stable triangulation with in-range adjacency indices; both live/dead triangle
 * slots scanned.
 * @return True if every nonnegative neighbor has a reciprocal reversed shared edge and same
 * constrained flag; false on mismatch. Does not validate index bounds, Delaunay geometry or cell
 * liveness policy.
 */
bool topologyConsistent(const ConstrainedDelaunay &cdt)
{
    const auto &T = cdt.triangles();
    for (int t = 0; t < T.size(); ++t)
        for (int i = 0; i < 3; ++i) {
            const int n = T[t].adj[i];
            if (n < 0)
                continue;
            bool back = false;
            for (int j = 0; j < 3; ++j)
                if (T[n].adj[j] == t) {
                    back = true;
                    // Shared edge vertices must match (reversed).
                    const int a = T[t].v[(i + 1) % 3], b = T[t].v[(i + 2) % 3];
                    const int c = T[n].v[(j + 1) % 3], d = T[n].v[(j + 2) % 3];
                    if (!(a == d && b == c))
                        return false;
                    if (T[t].constrained[i] != T[n].constrained[j])
                        return false;
                }
            if (!back)
                return false;
        }
    return true;
}

/*! Smallest corner angle of a triangle, degrees. */
/**
 * @brief Calculate minimum triangle corner angle by clamped vector dot products.
 * @param[in] A First finite planar vertex.
 * @param[in] B Second finite planar vertex, distinct from other vertices.
 * @param[in] C Third finite planar vertex, distinct from other vertices.
 * @return Smallest angle in degrees; callers supply nondegenerate geometry, no zero-length
 * checks. Read-only constant work, no external units conversion.
 */
double minAngleDeg(const QPointF &A, const QPointF &B, const QPointF &C)
{
    auto ang = [](const QPointF &p, const QPointF &q, const QPointF &r) {
        const QPointF u = q - p, v = r - p;
        const double c =
            QPointF::dotProduct(u, v) / (std::hypot(u.x(), u.y()) * std::hypot(v.x(), v.y()));
        return std::acos(std::clamp(c, -1.0, 1.0)) * 180.0 / M_PI;
    };
    return std::min({ang(A, B, C), ang(B, C, A), ang(C, A, B)});
}

/**
 * @brief Calculate one stored triangle's longest Euclidean edge.
 * @param[in] cdt Stable triangulation with valid vertex references.
 * @param[in] t In-range stored triangle index, liveness not checked.
 * @return Maximum of three lengths in planar coordinate units; no allocation/mutation.
 */
double longestEdge(const ConstrainedDelaunay &cdt, int t)
{
    const auto &T = cdt.triangles()[t];
    const auto &P = cdt.vertices();
    double best = 0.0;
    for (int k = 0; k < 3; ++k) {
        const QPointF d = P[T.v[(k + 1) % 3]] - P[T.v[k]];
        best = std::max(best, std::hypot(d.x(), d.y()));
    }
    return best;
}

/*! Triangles below the angle bound that are neither exempt (small input
 *  angle) nor touching a fixed segment. */
/**
 * @brief Count live triangles below a quality angle bound after recognized geometric exemptions.
 * @param[in] cdt Stable valid triangulation, preserved.
 * @param[in] theta Requested minimum corner angle in degrees; comparison uses theta-1e-6.
 * @param[out] worst Optional borrowed double overwritten with minimum violating angle in
 * degrees, or sentinel 180 when no unexplained violations.
 * @return Count excluding small-input-angle exemptions and cells touching fixed segments. No
 * refinement, allocation or ownership retention.
 */
int unexplainedBadAngles(const ConstrainedDelaunay &cdt, double theta, double *worst = nullptr)
{
    int bad = 0;
    double w = 180.0;
    const auto &Ts = cdt.triangles();
    for (int t = 0; t < Ts.size(); ++t) {
        const auto &T = Ts[t];
        if (!T.alive)
            continue;
        const double a =
            minAngleDeg(cdt.vertices()[T.v[0]], cdt.vertices()[T.v[1]], cdt.vertices()[T.v[2]]);
        if (a >= theta - 1e-6 || cdt.smallAngleExempt(t) || cdt.touchesFixed(t))
            continue;
        ++bad;
        w = std::min(w, a);
    }
    if (worst)
        *worst = w;
    return bad;
}

/*! Build a CDT of closed rings, each a constraint loop;
 *  exterior removed, and the ring containing each hole seed removed. */
/**
 * @brief Build a fixture CDT with closed constraint loops and remove exterior/seeded hole
 * components.
 * @param[in,out] cdt Triangulation rebuilt from concatenated ring points, then
 * constraints/regions modified.
 * @param[in] rings Stable open vertex sequences, each implicitly closed last-to-first; caller
 * supplies valid noncrossing geometry and at least enough distinct points for build. No separate
 * loose-point argument.
 * @param[in] holes Stable seeds in components to remove after exterior removal, default empty.
 * @param[in,out] ringIds Optional borrowed vector appended with each completed ring's mapped
 * vertex IDs; not cleared first, may contain partial progress on failure.
 * @return True if build/all constraints succeed, false otherwise; cdt/output may be partly
 * modified, no rollback or error propagation beyond cdt.errorMsg. Local arrays allocate, no
 * files/ownership transfer.
 */
bool buildRings(ConstrainedDelaunay &cdt, const QVector<QVector<QPointF>> &rings,
                const QVector<QPointF> &holes = {}, QVector<QVector<int>> *ringIds = nullptr)
{
    QVector<QPointF> pts;
    for (const auto &r : rings)
        pts += r;
    QVector<int> ids;
    if (!cdt.build(pts, &ids))
        return false;
    int base = 0;
    for (const auto &r : rings) {
        QVector<int> rid;
        for (int k = 0; k < r.size(); ++k)
            rid.append(ids[base + k]);
        for (int k = 0; k < r.size(); ++k)
            if (!cdt.insertConstraint(rid[k], rid[(k + 1) % r.size()]))
                return false;
        if (ringIds)
            ringIds->append(rid);
        base += r.size();
    }
    cdt.removeExterior();
    for (const QPointF &h : holes)
        cdt.removeRegionAt(h);
    return true;
}

/**
 * @brief Construct an open axis-aligned square ring.
 * @param[in] x0 Initial xcoordinate in shared planar units.
 * @param[in] y0 Initial ycoordinate in those units.
 * @param[in] s Positive side length for ordinary fixtures, no validation.
 * @return Owned four-pointCCWring for s>0,without repeated closing point or external state.
 */
QVector<QPointF> square(double x0, double y0, double s)
{
    return {QPointF(x0, y0), QPointF(x0 + s, y0), QPointF(x0 + s, y0 + s), QPointF(x0, y0 + s)};
}

/**
 * @brief Sum signed triangle areas over every live stored cell.
 * @param[in] cdt Stable mesh with valid referenced coordinates, preserved; super triangles not
 * separately filtered.
 * @return Sum in squared planar units, zero for no live cells; callers remove exterior/super
 * geometry and expect CCWpositive cells. No overlap checking or repair.
 */
double liveArea(const ConstrainedDelaunay &cdt)
{
    double area = 0.0;
    for (const auto &T : cdt.triangles()) {
        if (!T.alive)
            continue;
        const QPointF &A = cdt.vertices()[T.v[0]], &B = cdt.vertices()[T.v[1]],
                      &C = cdt.vertices()[T.v[2]];
        area += 0.5 * ((B.x() - A.x()) * (C.y() - A.y()) - (B.y() - A.y()) * (C.x() - A.x()));
    }
    return area;
}

} // namespace

class TestMeshCdt : public QObject
{
    Q_OBJECT
private slots:
    /**
     * @brief Verify interior vertex removal preserves live area, constrained chains and local
     * Delaunay topology.
     * @note 20 square with 90 seeded random points,with/withoutdiagonal;input corner cannot
     * remove,>60 interiors removed,each removes 2 cells/marks inactive,area 400 within 1e-8.
     * Subsequent insertion works after dead-slot reuse.
     */
    void removalPreservesTopologyAndConstraints()
    {
        for (bool diagonal : {false, true}) {
            ConstrainedDelaunay cdt;
            QVERIFY(buildRings(cdt, {square(0, 0, 20)}));
            if (diagonal)
                QVERIFY(cdt.insertConstraint(0, 2));
            QRandomGenerator rng(912);
            for (int i = 0; i < 90; ++i)
                QVERIFY(cdt.insertPoint({.1 + 19.8 * rng.generateDouble(),
                                         .1 + 19.8 * rng.generateDouble()}) >= 0);
            QVERIFY(!cdt.removeVertex(0));
            int removed = 0;
            for (int v = cdt.inputVertexCount() + 3; v < cdt.vertices().size(); ++v) {
                const int before = cdt.liveTriangleCount();
                if (!cdt.removeVertex(v))
                    continue;
                ++removed;
                QCOMPARE(cdt.liveTriangleCount(), before - 2);
                QVERIFY(!cdt.isVertexActive(v));
                QVERIFY(topologyConsistent(cdt));
                QCOMPARE(localDelaunayViolations(cdt), 0);
                QVERIFY(std::abs(liveArea(cdt) - 400) < 1e-8);
                for (int i = 0; i < 4; ++i)
                    QVERIFY(!cdt.constrainedChain(i, (i + 1) % 4).isEmpty());
                if (diagonal)
                    QVERIFY(!cdt.constrainedChain(0, 2).isEmpty());
            }
            QVERIFY(removed > 60);
            // Point location and insertion must work after dead slot reuse.
            QVERIFY(cdt.insertPoint({7, 8}) >= 0);
            QVERIFY(topologyConsistent(cdt));
            QCOMPARE(localDelaunayViolations(cdt), 0);
        }
    }

    /**
     * @brief Verify three coarsening passes remove eligible vertices while honoring quality/size
     * and deterministic output.
     * @note 20 square interior 2 unitgrid,h 6,angle 20;requires
     * removals,consistent/free-edgeDelaunaytopology, angle>=20-1e-8 and
     * area<=sqrt(3)/4*36*(1+1e-10). Repeat with always-falsecancel callback must retain exact
     * vertices,triangle slot count,liveness and connectivity.
     */
    void coarseningGatesAndDeterminism()
    {
        QVector<QPointF> firstVertices;
        QVector<ConstrainedDelaunay::Triangle> firstTriangles;
        for (int run = 0; run < 2; ++run) {
            ConstrainedDelaunay cdt;
            QVERIFY(buildRings(cdt, {square(0, 0, 20)}));
            for (int y = 2; y < 20; y += 2)
                for (int x = 2; x < 20; x += 2)
                    QVERIFY(cdt.insertPoint({double(x), double(y)}) >= 0);
            ConstrainedDelaunay::QualityOptions q;
            q.minAngleDeg = 20;
            q.coarsenPasses = 3;
            q.maxInsertions = 10000;
            q.hAt = [](double, double) { return 6.0; };
            if (run)
                q.cancelled = [] { return false; };
            const auto rep = cdt.refineQuality(q);
            QVERIFY(rep.coarsenRemoved > 0);
            QVERIFY(topologyConsistent(cdt));
            QCOMPARE(localDelaunayViolations(cdt), 0);
            for (const auto &T : cdt.triangles())
                if (T.alive) {
                    const auto &a = cdt.vertices()[T.v[0]], &b = cdt.vertices()[T.v[1]],
                               &c = cdt.vertices()[T.v[2]];
                    QVERIFY(minAngleDeg(a, b, c) >= 20 - 1e-8);
                    QVERIFY(ConstrainedDelaunay::orientExact(a, b, c) * .5 <=
                            .4330127018922193 * 36 * (1 + 1e-10));
                }
            if (!run) {
                firstVertices = cdt.vertices();
                firstTriangles = cdt.triangles();
            } else {
                QCOMPARE(cdt.vertices(), firstVertices);
                QCOMPARE(cdt.triangles().size(), firstTriangles.size());
                for (int i = 0; i < firstTriangles.size(); ++i) {
                    QCOMPARE(cdt.triangles()[i].alive, firstTriangles[i].alive);
                    for (int k = 0; k < 3; ++k)
                        QCOMPARE(cdt.triangles()[i].v[k], firstTriangles[i].v[k]);
                }
            }
        }
    }

    /**
     * @brief Verify terrain height/error certificates and explicit vertex protection gate
     * removal conservatively.
     * @note Five 10 square center modes:flatcertificatepermitsone removal,raisedcenter/NaN
     * bound/missingbound/ protectedcenterrefuse. Checks
     * skip/rejectcounters,activecenter/topology/localDelaunay, no raster IO.
     */
    void coarseningTerrainCoverageAndProtectedVertices()
    {
        for (int mode = 0; mode < 5; ++mode) {
            ConstrainedDelaunay cdt;
            QVERIFY(buildRings(cdt, {square(0, 0, 10)}));
            const int centre = cdt.insertPoint({5, 5});
            QVERIFY(centre >= 0);
            ConstrainedDelaunay::QualityOptions q;
            q.minAngleDeg = 0;
            q.coarsenPasses = 3;
            q.terrainTolerance = .1;
            q.terrainWorstFirst = true;
            q.terrainElevationAt = [mode](double x, double y) {
                return mode == 1 && x == 5 && y == 5 ? 1.0 : 0.0;
            };
            q.terrainError = [](const QPointF *, const double *, QPointF *) { return 0.0; };
            if (mode != 3)
                q.terrainErrorBound = [mode](const QPointF *, const double *) {
                    return mode == 2 ? std::numeric_limits<double>::quiet_NaN() : 0.0;
                };
            if (mode == 4)
                q.protectedVertices.insert(centre);
            const auto rep = cdt.refineQuality(q);
            QCOMPARE(rep.coarsenRemoved, mode == 0 ? 1 : 0);
            QCOMPARE(cdt.isVertexActive(centre), mode != 0);
            QCOMPARE(rep.coarsenSkippedTerrain, mode == 3);
            if (mode == 1)
                QVERIFY(rep.coarsenHeightRejects > 0);
            if (mode == 2)
                QVERIFY(rep.coarsenRejectedTerrain > 0);
            QVERIFY(topologyConsistent(cdt));
            QCOMPARE(localDelaunayViolations(cdt), 0);
        }
    }

    /**
     * @brief Verify disabled coarsening and immediate cancellation leave an interior fixture
     * vertex active.
     * @note Defaults remove 0;enabled 3 passes/canceltrue reports cancelled with consistent
     * topology. No rollback claim for general partially completed refinement.
     */
    void coarseningCancellationAndOff()
    {
        ConstrainedDelaunay cdt;
        QVERIFY(buildRings(cdt, {square(0, 0, 10)}));
        const int centre = cdt.insertPoint({5, 5});
        ConstrainedDelaunay::QualityOptions q;
        q.minAngleDeg = 0;
        QCOMPARE(cdt.refineQuality(q).coarsenRemoved, 0);
        QVERIFY(cdt.isVertexActive(centre));
        q.coarsenPasses = 3;
        q.cancelled = [] { return true; };
        const auto report = cdt.refineQuality(q);
        QVERIFY(report.cancelled);
        QVERIFY(cdt.isVertexActive(centre));
        QVERIFY(topologyConsistent(cdt));
    }

    /**
     * @brief Verify 2000 seeded random points build globally Delaunay with reciprocal topology
     * and Euler cell count.
     * @note Seed 1234,100 by 60 domain;after super removal live triangles=2*n-2-hullEdges. Local
     * random engine, no external state or timing threshold.
     */
    void randomPointsAreDelaunayAndConsistent()
    {
        QRandomGenerator rng(1234);
        QVector<QPointF> pts;
        for (int i = 0; i < 2000; ++i)
            pts.append(QPointF(rng.generateDouble() * 100.0, rng.generateDouble() * 60.0));
        ConstrainedDelaunay cdt;
        QVector<int> ids;
        QVERIFY2(cdt.build(pts, &ids), qPrintable(cdt.errorMsg()));
        QCOMPARE(ids.size(), 2000);
        QVERIFY(topologyConsistent(cdt));
        QCOMPARE(delaunayViolations(cdt), 0);
        cdt.removeSuperTriangles();
        // Euler for a triangulated convex hull: t = 2n - 2 - h.
        int hull = 0;
        for (const auto &T : cdt.triangles())
            if (T.alive)
                for (int i = 0; i < 3; ++i)
                    if (T.adj[i] < 0 || !cdt.triangles()[T.adj[i]].alive)
                        ++hull;
        QCOMPARE(liveTriangles(cdt), 2 * 2000 - 2 - hull);
    }

    /**
     * @brief Verify exact cocircular/collinear lattice geometry is stable and duplicate inputs
     * map to existing vertices.
     * @note 30 square point grid yields 2*29^2 real triangles,no Delaunayviolations;two
     * duplicate inputs reuse IDs and retain 900+3 super vertices. No tolerance snapping beyond
     * kernel behavior asserted.
     */
    void gridAndCocircularPointsDoNotBreakIt()
    {
        // A 30x30 lattice: every 2x2 cell is exactly cocircular, every row
        // exactly collinear.
        QVector<QPointF> pts;
        for (int r = 0; r < 30; ++r)
            for (int c = 0; c < 30; ++c)
                pts.append(QPointF(c, r));
        ConstrainedDelaunay cdt;
        QVERIFY2(cdt.build(pts), qPrintable(cdt.errorMsg()));
        QVERIFY(topologyConsistent(cdt));
        QCOMPARE(delaunayViolations(cdt), 0);
        cdt.removeSuperTriangles();
        QCOMPARE(liveTriangles(cdt), 2 * 29 * 29);
        // Duplicates map to one vertex.
        QVector<QPointF> dup = pts;
        dup.append(pts[5]);
        dup.append(pts[7]);
        QVector<int> ids;
        ConstrainedDelaunay cdt2;
        QVERIFY(cdt2.build(dup, &ids));
        QCOMPARE(ids[900], ids[5]);
        QCOMPARE(ids[901], ids[7]);
        QCOMPARE(cdt2.vertices().size(), 900 + 3);
    }

    /**
     * @brief Verify long constraints recover through a dense point cloud and crossing
     * constraints refuse.
     * @note 1500 seeded points plus square/endpoints,longsegment(5,50)-(95,52),valid
     * topology/localDelaunay; verticalcrossing refuses,exteriorremoval leaves>2000 cells. Local
     * mutable CDT only.
     */
    void constraintsAreRecoveredAcrossManyEdges()
    {
        QRandomGenerator rng(99);
        QVector<QPointF> pts;
        pts.append(QPointF(0, 0));
        pts.append(QPointF(100, 0));
        pts.append(QPointF(100, 100));
        pts.append(QPointF(0, 100));
        pts.append(QPointF(5, 50));
        pts.append(QPointF(95, 52)); // a long constraint through the cloud
        for (int i = 0; i < 1500; ++i)
            pts.append(QPointF(rng.generateDouble() * 100.0, rng.generateDouble() * 100.0));
        ConstrainedDelaunay cdt;
        QVector<int> ids;
        QVERIFY(cdt.build(pts, &ids));
        QVERIFY(cdt.insertConstraint(ids[4], ids[5]));
        QVERIFY(cdt.isConstrained(ids[4], ids[5]));
        for (int i = 0; i < 4; ++i)
            QVERIFY(cdt.insertConstraint(ids[i], ids[(i + 1) % 4]));
        QVERIFY(topologyConsistent(cdt));
        QCOMPARE(localDelaunayViolations(cdt), 0);
        // A constraint that would cross the first one is refused.
        const int extraA = cdt.insertPoint(QPointF(50, 10)),
                  extraB = cdt.insertPoint(QPointF(50, 90));
        QVERIFY(extraA >= 0 && extraB >= 0);
        QVERIFY(!cdt.insertConstraint(extraA, extraB));
        cdt.removeExterior();
        QVERIFY(liveTriangles(cdt) > 2000);
    }

    /**
     * @brief Verify an existing midpoint splits a newly inserted collinear constraint into two
     * constrained edges.
     * @note Segment 0..10 on x axis passes vertex 5,both halves constrained and reciprocal
     * topology intact.
     */
    void aVertexOnTheSegmentSplitsTheConstraint()
    {
        QVector<QPointF> pts = {QPointF(0, 0),  QPointF(10, 0), QPointF(5, 0), QPointF(5, 5),
                                QPointF(5, -5), QPointF(2, 3),  QPointF(8, -2)};
        ConstrainedDelaunay cdt;
        QVector<int> ids;
        QVERIFY(cdt.build(pts, &ids));
        QVERIFY(cdt.insertConstraint(ids[0], ids[1]));
        QVERIFY(cdt.isConstrained(ids[0], ids[2]));
        QVERIFY(cdt.isConstrained(ids[2], ids[1]));
        QVERIFY(topologyConsistent(cdt));
    }

    /**
     * @brief Verify hole flood removal respects constrained loops and leaves correct
     * area/location results.
     * @note 100 square outer/20 square
     * hole,exactlytwoholetrianglesremoved,insidehole/exteriorlocate-1, interiorvalid,area 9600
     * within 1e-6. All geometry local, no export artifacts.
     */
    void regionsAreRemovedWithoutCrossingConstraints()
    {
        // Outer square with a square hole; a diagonal constraint inside.
        QVector<QPointF> pts;
        const double o[4][2] = {{0, 0}, {100, 0}, {100, 100}, {0, 100}};
        const double h[4][2] = {{40, 40}, {60, 40}, {60, 60}, {40, 60}};
        for (auto &p : o)
            pts.append(QPointF(p[0], p[1]));
        for (auto &p : h)
            pts.append(QPointF(p[0], p[1]));
        QRandomGenerator rng(7);
        for (int i = 0; i < 300; ++i) {
            const QPointF p(rng.generateDouble() * 100.0, rng.generateDouble() * 100.0);
            if (p.x() > 38 && p.x() < 62 && p.y() > 38 && p.y() < 62)
                continue;
            pts.append(p);
        }
        ConstrainedDelaunay cdt;
        QVector<int> ids;
        QVERIFY(cdt.build(pts, &ids));
        for (int i = 0; i < 4; ++i)
            QVERIFY(cdt.insertConstraint(ids[i], ids[(i + 1) % 4]));
        for (int i = 0; i < 4; ++i)
            QVERIFY(cdt.insertConstraint(ids[4 + i], ids[4 + (i + 1) % 4]));
        cdt.removeExterior();
        const int before = liveTriangles(cdt);
        cdt.removeRegionAt(QPointF(50, 50));
        const int after = liveTriangles(cdt);
        QVERIFY(after < before);
        QCOMPARE(after, before - 2); // the hole was exactly two triangles
        QCOMPARE(cdt.locate(QPointF(50, 50)), -1);
        QVERIFY(cdt.locate(QPointF(20, 20)) >= 0);
        QCOMPARE(cdt.locate(QPointF(-5, 50)), -1);
        // Total area of live triangles = outer minus hole.
        double area = 0.0;
        for (const auto &T : cdt.triangles()) {
            if (!T.alive)
                continue;
            const QPointF &A = cdt.vertices()[T.v[0]], &B = cdt.vertices()[T.v[1]],
                          &C = cdt.vertices()[T.v[2]];
            area += 0.5 * ((B.x() - A.x()) * (C.y() - A.y()) - (B.y() - A.y()) * (C.x() - A.x()));
        }
        QVERIFY(std::abs(area - (10000.0 - 400.0)) < 1e-6);
    }

    /**
     * @brief Verify refinement meets 30 degree quality and uniformh 5 area bounds while
     * preserving a slanted constraint chain.
     * @note 100 square minus 15 square hole,uncapped,zero unexplained angles/local Delaunay
     * violations, all areas positive<=sqrt(3)/4*25*(1+1e-9),total 9775 within 1e-6. Chain
     * lengthhypot(20,65)within 1e-9; repeat gives exact vertices/insertion count. No
     * model/files.
     */
    void qualityRefinementMeetsTheAngleAndSizeBounds()
    {
        // Square with a square hole and a slanted interior line.
        QVector<QVector<QPointF>> rings = {square(0, 0, 100), square(40, 40, 15)};
        ConstrainedDelaunay cdt;
        QVERIFY(buildRings(cdt, rings, {QPointF(47, 47)}));
        const int a = cdt.insertPoint(QPointF(10, 20)), b = cdt.insertPoint(QPointF(30, 85));
        QVERIFY(a >= 0 && b >= 0);
        QVERIFY(cdt.insertConstraint(a, b));
        ConstrainedDelaunay::QualityOptions o;
        o.hAt = [](double, double) { return 5.0; };
        o.minAngleDeg = 30.0;
        const auto rep = cdt.refineQuality(o);
        QVERIFY(!rep.capped);
        QVERIFY(topologyConsistent(cdt));
        QCOMPARE(localDelaunayViolations(cdt), 0);
        double worst = 180.0;
        QCOMPARE(unexplainedBadAngles(cdt, 30.0, &worst), 0);
        for (const auto &T : cdt.triangles()) {
            if (!T.alive)
                continue;
            const QPointF &A = cdt.vertices()[T.v[0]], &B = cdt.vertices()[T.v[1]],
                          &C = cdt.vertices()[T.v[2]];
            const double area =
                0.5 * ((B.x() - A.x()) * (C.y() - A.y()) - (B.y() - A.y()) * (C.x() - A.x()));
            QVERIFY(area > 0.0);
            QVERIFY(area <= 0.4330127018922193 * 25.0 * (1.0 + 1e-9));
        }
        QVERIFY(std::abs(liveArea(cdt) - (10000.0 - 225.0)) < 1e-6);
        // The interior line is still there, as a chain of edges.
        const QVector<int> chain = cdt.constrainedChain(a, b);
        QVERIFY(chain.size() > 2);
        double len = 0.0;
        for (int i = 0; i + 1 < chain.size(); ++i) {
            QVERIFY(cdt.isConstrained(chain[i], chain[i + 1]));
            const QPointF d = cdt.vertices()[chain[i + 1]] - cdt.vertices()[chain[i]];
            len += std::hypot(d.x(), d.y());
        }
        QVERIFY(std::abs(len - std::hypot(20.0, 65.0)) < 1e-9);
        // Deterministic.
        ConstrainedDelaunay again;
        QVERIFY(buildRings(again, rings, {QPointF(47, 47)}));
        const int a2 = again.insertPoint(QPointF(10, 20)), b2 = again.insertPoint(QPointF(30, 85));
        QVERIFY(again.insertConstraint(a2, b2));
        QCOMPARE(again.refineQuality(o).inserted, rep.inserted);
        QCOMPARE(again.vertices(), cdt.vertices());
    }

    /**
     * @brief Verify angle-only refinement does not introduce unnecessary uniform cell-size
     * limits.
     * @note 1 kmsquarekeeps 2 triangles/0 insertions,regularpentagon<=10 insertions,zero
     * unexplained 30 degree angles.
     */
    void withoutASizeBoundOpenGroundStaysCoarse()
    {
        // Only the quality bound: a 1 km square needs no new vertex at all,
        // a pentagon only a handful.
        ConstrainedDelaunay cdt;
        QVERIFY(buildRings(cdt, {square(0, 0, 1000)}));
        ConstrainedDelaunay::QualityOptions o;
        o.minAngleDeg = 30.0;
        QCOMPARE(cdt.refineQuality(o).inserted, 0);
        QCOMPARE(cdt.liveTriangleCount(), 2); // two 45-45-90 triangles
        ConstrainedDelaunay pent;
        QVector<QPointF> ring;
        for (int k = 0; k < 5; ++k)
            ring.append(
                QPointF(500.0 * std::cos(2 * M_PI * k / 5), 500.0 * std::sin(2 * M_PI * k / 5)));
        QVERIFY(buildRings(pent, {ring}));
        QVERIFY(pent.refineQuality(o).inserted <= 10);
        QCOMPARE(unexplainedBadAngles(pent, 30.0), 0);
    }

    /**
     * @brief Verify h=1+.3*xgrades a 400 square mesh smoothly while permitting distant
     * coarsening.
     * @note No unexplained 30 degree angles,neighbor longest-edge ratio max<=2+1e-9,median<=1.3,
     * right-quarterlongest edge>20 times left-edge maximum. Logs ratios, no hard time gate.
     */
    void gradedSizesGiveSmoothNeighbours()
    {
        // h grows away from the left edge at slope 0.3: neighbouring
        // triangles' longest edges differ by at most 1/sin 30° = 2 and are
        // usually within 1.3.
        ConstrainedDelaunay cdt;
        QVERIFY(buildRings(cdt, {square(0, 0, 400)}));
        ConstrainedDelaunay::QualityOptions o;
        o.hAt = [](double x, double) { return 1.0 + 0.3 * x; };
        o.minAngleDeg = 30.0;
        QVERIFY(!cdt.refineQuality(o).capped);
        QCOMPARE(unexplainedBadAngles(cdt, 30.0), 0);
        QVector<double> ratios;
        const auto &Ts = cdt.triangles();
        for (int t = 0; t < Ts.size(); ++t) {
            if (!Ts[t].alive)
                continue;
            for (int k = 0; k < 3; ++k) {
                const int n = Ts[t].adj[k];
                if (n <= t || !Ts[n].alive)
                    continue;
                const double a = longestEdge(cdt, t), b = longestEdge(cdt, n);
                ratios.append(std::max(a, b) / std::min(a, b));
            }
        }
        std::sort(ratios.begin(), ratios.end());
        QVERIFY(!ratios.isEmpty());
        const double median = ratios[ratios.size() / 2], worst = ratios.last();
        qInfo("graded: %lld faces, median %.3f, max %.3f", (long long)ratios.size(), median, worst);
        QVERIFY2(worst <= 2.0 + 1e-9, qPrintable(QString::number(worst)));
        QVERIFY2(median <= 1.3, qPrintable(QString::number(median)));
        // Coarse where h is large: the right-hand quarter has cells far
        // bigger than the left-hand edge's.
        double leftMax = 0.0, rightMax = 0.0;
        for (int t = 0; t < Ts.size(); ++t) {
            if (!Ts[t].alive)
                continue;
            const QPointF c = (cdt.vertices()[Ts[t].v[0]] + cdt.vertices()[Ts[t].v[1]] +
                               cdt.vertices()[Ts[t].v[2]]) /
                              3.0;
            if (c.x() < 10)
                leftMax = std::max(leftMax, longestEdge(cdt, t));
            if (c.x() > 300)
                rightMax = std::max(rightMax, longestEdge(cdt, t));
        }
        QVERIFY2(rightMax > 20.0 * leftMax,
                 qPrintable(QStringLiteral("%1 vs %2").arg(rightMax).arg(leftMax)));
    }

    /**
     * @brief Verify acute constrained geometry terminates refinement and leaves only recognized
     * angle exemptions.
     * @note 3/8/20 degree fan and~9.5 degree boundary spike,h 8/30 degree bound;uncapped,<20000
     * insertions, consistent topology and 0 unexplained angles. Logs elapsed but imposes
     * insertion rather than time cap.
     */
    void smallInputAnglesTerminate()
    {
        // Segments leaving one vertex at 3°, 8° and 20° apart, and a sharp
        // 10° spike in the boundary: refinement must stop, and every thin
        // triangle left must be the small-angle exemption.
        QVector<QPointF> ring = {QPointF(0, 0),    QPointF(100, 0), QPointF(100, 100),
                                 QPointF(55, 100), QPointF(50, 40), QPointF(45, 100),
                                 QPointF(0, 100)};
        // The spike at (50, 40) has an interior angle of ~9.5°.
        ConstrainedDelaunay cdt;
        QVector<QPointF> pts = ring;
        const QPointF c(20, 20);
        pts.append(c);
        for (double deg : {0.0, 3.0, 11.0, 31.0})
            pts.append(c + QPointF(30.0 * std::cos(deg * M_PI / 180.0),
                                   30.0 * std::sin(deg * M_PI / 180.0)));
        QVector<int> ids;
        QVERIFY(cdt.build(pts, &ids));
        for (int k = 0; k < ring.size(); ++k)
            QVERIFY(cdt.insertConstraint(ids[k], ids[(k + 1) % ring.size()]));
        for (int k = 0; k < 4; ++k)
            QVERIFY(cdt.insertConstraint(ids[ring.size()], ids[ring.size() + 1 + k]));
        cdt.removeExterior();
        ConstrainedDelaunay::QualityOptions o;
        o.hAt = [](double, double) { return 8.0; };
        o.minAngleDeg = 30.0;
        o.maxInsertions = 200000;
        QElapsedTimer timer;
        timer.start();
        const auto rep = cdt.refineQuality(o);
        qInfo("small angles: %d inserted, %d splits, %lld ms", rep.inserted, rep.segmentSplits,
              (long long)timer.elapsed());
        QVERIFY(!rep.capped);
        QVERIFY2(rep.inserted < 20000, qPrintable(QString::number(rep.inserted)));
        QVERIFY(topologyConsistent(cdt));
        double worst = 180.0;
        const int bad = unexplainedBadAngles(cdt, 30.0, &worst);
        QVERIFY2(bad == 0,
                 qPrintable(QStringLiteral("%1 triangles, worst %2°").arg(bad).arg(worst)));
    }

    /**
     * @brief Verify fixed constraint edges survive refinement intact and unavoidable thin cells
     * remain local.
     * @note 2 unit fixed patch ring and 12 unit fixed bar underh 2/30 degree bound;each
     * fixedchain exactly 2 vertices, uncapped,no unexplained bad angles for
     * ring,barthincentroids within 12 units ofbar. Logs blocked counts.
     */
    void fixedSegmentsAreNeverSplit()
    {
        // A patch-like square ring with fixed edges every 2 units inside a
        // larger square, in a size field of 2: the ring edges fit the field,
        // so the angle bound holds everywhere, fixed edges untouched.
        QVector<QPointF> outer = square(0, 0, 60);
        QVector<QPointF> patch;
        for (int k = 0; k < 5; ++k)
            patch.append(QPointF(20 + 2 * k, 20));
        for (int k = 0; k < 5; ++k)
            patch.append(QPointF(30, 20 + 2 * k));
        for (int k = 0; k < 5; ++k)
            patch.append(QPointF(30 - 2 * k, 30));
        for (int k = 0; k < 5; ++k)
            patch.append(QPointF(20, 30 - 2 * k));
        ConstrainedDelaunay::QualityOptions o;
        o.hAt = [](double, double) { return 2.0; };
        o.minAngleDeg = 30.0;
        {
            ConstrainedDelaunay cdt;
            QVector<QVector<int>> ids;
            QVERIFY(buildRings(cdt, {outer, patch}, {QPointF(25, 25)}, &ids));
            const QVector<int> &r = ids[1];
            for (int k = 0; k < r.size(); ++k)
                cdt.setFixedConstraint(r[k], r[(k + 1) % r.size()]);
            const auto rep = cdt.refineQuality(o);
            QVERIFY(!rep.capped);
            for (int k = 0; k < r.size(); ++k)
                QCOMPARE(cdt.constrainedChain(r[k], r[(k + 1) % r.size()]).size(), 2);
            QCOMPARE(unexplainedBadAngles(cdt, 30.0), 0);
            qInfo("fixed ring: %d blocked", rep.blockedByFixed);
        }
        // A fixed bar six sizes long with only its ends: never split; the
        // triangles it leaves thin all sit next to it.
        ConstrainedDelaunay cdt;
        QVERIFY(buildRings(cdt, {outer}));
        const int a = cdt.insertPoint(QPointF(24, 30)), b = cdt.insertPoint(QPointF(36, 30));
        QVERIFY(cdt.insertConstraint(a, b));
        cdt.setFixedConstraint(a, b);
        const auto rep = cdt.refineQuality(o);
        QVERIFY(!rep.capped);
        QVERIFY(cdt.isConstrained(a, b));
        QCOMPARE(cdt.constrainedChain(a, b).size(), 2);
        int thin = 0;
        for (int t = 0; t < cdt.triangles().size(); ++t) {
            const auto &T = cdt.triangles()[t];
            if (!T.alive)
                continue;
            const QPointF &A = cdt.vertices()[T.v[0]], &B = cdt.vertices()[T.v[1]],
                          &C = cdt.vertices()[T.v[2]];
            if (minAngleDeg(A, B, C) >= 30.0 - 1e-6)
                continue;
            ++thin;
            const QPointF c = (A + B + C) / 3.0;
            const double dx = c.x() < 24 ? 24 - c.x() : (c.x() > 36 ? c.x() - 36 : 0.0);
            QVERIFY2(std::hypot(dx, c.y() - 30.0) < 12.0,
                     qPrintable(QStringLiteral("%1,%2").arg(c.x()).arg(c.y())));
        }
        qInfo("fixed bar: %d blocked, %d thin triangles next to it", rep.blockedByFixed, thin);
    }

    /**
     * @brief Verify small-angle exemptions recognize an apex introduced by splitting a parent
     * constraint.
     * @note 5 degree wedge represented by separate/through-apexlegs,h 8,uncapped,<2000
     * insertions, no unexplained 30 degree angles;throughparentchain retains apex ID. Local
     * geometry only.
     */
    void smallAngleAtAVertexLyingOnASegment()
    {
        // A 5° wedge whose straight leg is ONE input segment passing exactly
        // through the wedge's apex: the exemption must see the two pieces
        // meeting there (it did not when pieces kept their parent's origin).
        for (bool throughVertex : {false, true}) {
            const double deg = 5.0;
            QVector<QPointF> pts = {QPointF(0, 0),
                                    QPointF(100, 0),
                                    QPointF(100, 100),
                                    QPointF(0, 100),
                                    QPointF(20, 50),
                                    QPointF(50, 50),
                                    QPointF(80, 50),
                                    QPointF(50 + 30 * std::cos(deg * M_PI / 180),
                                            50 + 30 * std::sin(deg * M_PI / 180))};
            ConstrainedDelaunay cdt;
            QVector<int> id;
            QVERIFY(cdt.build(pts, &id));
            for (int k = 0; k < 4; ++k)
                QVERIFY(cdt.insertConstraint(id[k], id[(k + 1) % 4]));
            if (throughVertex)
                QVERIFY(cdt.insertConstraint(id[4], id[6]));
            else {
                QVERIFY(cdt.insertConstraint(id[4], id[5]));
                QVERIFY(cdt.insertConstraint(id[5], id[6]));
            }
            QVERIFY(cdt.insertConstraint(id[5], id[7]));
            cdt.removeExterior();
            ConstrainedDelaunay::QualityOptions o;
            o.hAt = [](double, double) { return 8.0; };
            o.minAngleDeg = 30.0;
            o.maxInsertions = 200000;
            const auto rep = cdt.refineQuality(o);
            qInfo("wedge (%s): %d inserted", throughVertex ? "through vertex" : "own segments",
                  rep.inserted);
            QVERIFY(!rep.capped);
            QVERIFY2(rep.inserted < 2000, qPrintable(QString::number(rep.inserted)));
            QCOMPARE(unexplainedBadAngles(cdt, 30.0), 0);
            if (throughVertex) {
                // The long segment is still one chain, through the apex.
                const QVector<int> chain = cdt.constrainedChain(id[4], id[6]);
                QVERIFY(chain.contains(id[5]));
            }
        }
    }

    /**
     * @brief Verify overlapping collinear constraint identities survive repeated shared-edge
     * refinement splits.
     * @note Two partially overlapping oblique lines,h.37/30 degree bound,uncapped;both
     * returnedchains>2 vertices. No explicit length comparison or global Delaunay check in this
     * slot.
     */
    void overlappingConstraintsKeepTheirChains()
    {
        // Two collinear constraints sharing a piece: after refinement splits
        // the shared piece, both still come back as chains.
        const QPointF a(0, 0), b(31.1, 17.3), c = a + (b - a) * 0.5, d = a + (b - a) * 2.0;
        QVector<QPointF> pts = {
            QPointF(-50, -50), QPointF(100, -50), QPointF(100, 100), QPointF(-50, 100), a, b, c, d};
        ConstrainedDelaunay cdt;
        QVector<int> id;
        QVERIFY(cdt.build(pts, &id));
        for (int k = 0; k < 4; ++k)
            QVERIFY(cdt.insertConstraint(id[k], id[(k + 1) % 4]));
        QVERIFY(cdt.insertConstraint(id[4], id[5]));
        QVERIFY(cdt.insertConstraint(id[6], id[7]));
        cdt.removeExterior();
        ConstrainedDelaunay::QualityOptions o;
        o.hAt = [](double, double) { return 0.37; };
        o.minAngleDeg = 30.0;
        QVERIFY(!cdt.refineQuality(o).capped);
        const QVector<int> first = cdt.constrainedChain(id[4], id[5]),
                           second = cdt.constrainedChain(id[6], id[7]);
        QVERIFY(first.size() > 2);
        QVERIFY(second.size() > 2);
    }

    /**
     * @brief Verify a minimum-edge floor prevents 33 degree quality cascades from reaching
     * numerical sliver scales.
     * @note 60 seeded star polygons,failed initial ring builds
     * skipped;hvaries,minEdge=h/16,uncapped output, shortest live edge>h/64. No claim every
     * input polygon builds or every requested angle is achieved.
     */
    void theFloorStopsCascadesAtHighBounds()
    {
        // Random star polygons at 31–33°: without a floor a few cascade to
        // rounding error; with the generator's floor none goes below it.
        quint32 seed = 12345;
        auto rnd = [&]() {
            seed = seed * 1664525u + 1013904223u;
            return (seed >> 8) / double(1 << 24);
        };
        for (int trial = 0; trial < 60; ++trial) {
            const int n = 6 + int(rnd() * 30);
            QVector<QPointF> ring;
            for (int k = 0; k < n; ++k) {
                const double a = 2 * M_PI * (k + 0.8 * rnd()) / n, r = 60 + 50 * rnd();
                ring.append(QPointF(r * std::cos(a), r * std::sin(a)));
            }
            const double h = 2.0 + 30.0 * rnd();
            ConstrainedDelaunay cdt;
            QVector<QVector<int>> ids;
            if (!buildRings(cdt, {ring}, {}, &ids))
                continue;
            ConstrainedDelaunay::QualityOptions o;
            o.minAngleDeg = 33.0;
            o.hAt = [h](double, double) { return h; };
            o.minEdge = h / 16.0;
            o.maxInsertions = 400000;
            QVERIFY(!cdt.refineQuality(o).capped);
            double shortest = std::numeric_limits<double>::infinity();
            for (const auto &T : cdt.triangles())
                if (T.alive)
                    for (int k = 0; k < 3; ++k) {
                        const QPointF dd =
                            cdt.vertices()[T.v[(k + 1) % 3]] - cdt.vertices()[T.v[k]];
                        shortest = std::min(shortest, std::hypot(dd.x(), dd.y()));
                    }
            QVERIFY2(
                shortest > h / 64.0,
                qPrintable(QStringLiteral("trial %1: %2 vs h %3").arg(trial).arg(shortest).arg(h)));
        }
    }

    /**
     * @brief Verify uniform refinement produces>800000 triangles within 20 seconds without
     * hitting insertion limits.
     * @note 1000 square,h 1.5,30 degree bound;logs triangle/insertion/time,resource-sensitive
     * explicit performance gate.
     */
    void aMillionTrianglesInSeconds()
    {
        ConstrainedDelaunay cdt;
        QVERIFY(buildRings(cdt, {square(0, 0, 1000)}));
        ConstrainedDelaunay::QualityOptions o;
        o.hAt = [](double, double) { return 1.5; };
        o.minAngleDeg = 30.0;
        QElapsedTimer t;
        t.start();
        const auto rep = cdt.refineQuality(o);
        const qint64 ms = t.elapsed();
        const int n = cdt.liveTriangleCount();
        qInfo("refine: %d triangles, %d inserted in %lld ms", n, rep.inserted, (long long)ms);
        QVERIFY(n > 800000);
        QVERIFY(!rep.capped);
        QVERIFY2(ms < 20000, qPrintable(QString::number(ms)));
    }

    /**
     * @brief Verify repeated builds from 500 seeded random points produce exact same triangle
     * connectivity/order.
     * @note Seed 4321 unit domain,comparesstoredslotcount/threeindices,does not compare timings
     * or all metadata.
     */
    void identicalInputsGiveIdenticalTriangulations()
    {
        QRandomGenerator rng(4321);
        QVector<QPointF> pts;
        for (int i = 0; i < 500; ++i)
            pts.append(QPointF(rng.generateDouble(), rng.generateDouble()));
        ConstrainedDelaunay a, b;
        QVERIFY(a.build(pts));
        QVERIFY(b.build(pts));
        QCOMPARE(a.triangles().size(), b.triangles().size());
        for (int i = 0; i < a.triangles().size(); ++i)
            for (int k = 0; k < 3; ++k)
                QCOMPARE(a.triangles()[i].v[k], b.triangles()[i].v[k]);
    }

    /**
     * @brief Verify 100000 seeded points triangulate successfully within 20 seconds.
     * @note Seed 1,1000 square point cloud;logs elapsed,substantial local allocations,explicit
     * timing gate, no filesystem or global random mutation.
     */
    void aHundredThousandPointsBuildQuickly()
    {
        QRandomGenerator rng(1);
        QVector<QPointF> pts;
        for (int i = 0; i < 100000; ++i)
            pts.append(QPointF(rng.generateDouble() * 1000.0, rng.generateDouble() * 1000.0));
        ConstrainedDelaunay cdt;
        QElapsedTimer t;
        t.start();
        QVERIFY(cdt.build(pts));
        qInfo("cdt: 100k points in %lld ms", (long long)t.elapsed());
        QVERIFY(t.elapsed() < 20000);
    }
};

QTEST_MAIN(TestMeshCdt)
#include "test_meshcdt.moc"
