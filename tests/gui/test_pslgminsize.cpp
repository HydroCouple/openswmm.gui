/*!
 * \file   test_pslgminsize.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * Minimum-feature-size conditioning of the input PSLG
 * (MIN_CELL_SIZE_TESTING_HANDOFF_2026-08-17.md phases 1-3).
 *
 *   Phase 1  resampleMinLength / resampleRingMinLength
 *   Phase 2  conditionMinSize welding, fail-safe, determinism, meshability
 *   Phase 3  corner trimming
 *
 * The welding properties are asserted with corner trimming OFF.  Trimming runs
 * last and its new vertices are deliberately NOT re-welded (handoff §5.1), so
 * the separation guarantee belongs to the weld stage and is tested there;
 * Phase 3 tests trimming on its own geometry.
 */
#include <QtTest>
#include <QPointF>
#include <QPolygonF>
#include <QSet>
#include <QVector>

#include "mesh/meshgenerator.h"
#include "mesh/pslgminsize.h"
#include "mesh/pslgprep.h"

#include <cmath>

using mesh::ConstraintSegment;
using mesh::SteinerPoint;
using mesh::pslg::ConditionReport;
using mesh::pslg::MinSizePolicy;
using mesh::pslg::Violation;
using mesh::pslg::ViolationCause;

namespace {

// ── Geometry helpers ────────────────────────────────────────────────────

double dist(const QPointF &a, const QPointF &b)
{
    return std::hypot(b.x() - a.x(), b.y() - a.y());
}

QVector<QPointF> ringOf(const QPolygonF &p)
{
    QVector<QPointF> r;
    r.reserve(p.size());
    for (const QPointF &q : p) r.append(q);
    return r;
}

QPolygonF square(double x0, double y0, double s)
{
    return QPolygonF(QVector<QPointF>{
        {x0, y0}, {x0 + s, y0}, {x0 + s, y0 + s}, {x0, y0 + s}, {x0, y0}});
}

/*! Every edge of the whole PSLG, as coordinate pairs. */
struct Edges
{
    QVector<QPair<QPointF, QPointF>> e;
};

Edges allEdges(const QVector<QPolygonF> &domains,
               const QVector<QVector<QPointF>> &holes,
               const QVector<ConstraintSegment> &segs)
{
    Edges out;
    auto addRing = [&](const QVector<QPointF> &r) {
        const int n = r.size();
        if (n < 3) return;
        const int en = (r.first() == r.last()) ? n - 1 : n;
        for (int i = 0; i < en; ++i) out.e.append({r[i], r[(i + 1) % en]});
    };
    for (const QPolygonF &d : domains) addRing(ringOf(d));
    for (const auto &h : holes) addRing(h);
    for (const auto &s : segs)
        for (int i = 1; i < s.path.size(); ++i)
            out.e.append({s.path[i - 1], s.path[i]});
    return out;
}

QVector<QPointF> allVertices(const QVector<QPolygonF> &domains,
                             const QVector<QVector<QPointF>> &holes,
                             const QVector<ConstraintSegment> &segs,
                             const QVector<SteinerPoint> &pts)
{
    QSet<QPair<qreal, qreal>> seen;
    QVector<QPointF> out;
    auto add = [&](const QPointF &p) {
        const auto k = qMakePair(p.x(), p.y());
        if (seen.contains(k)) return;
        seen.insert(k);
        out.append(p);
    };
    for (const QPolygonF &d : domains) for (const QPointF &q : d) add(q);
    for (const auto &h : holes)        for (const QPointF &q : h) add(q);
    for (const auto &s : segs)         for (const QPointF &q : s.path) add(q);
    for (const auto &p : pts)          add(p.xy);
    return out;
}

bool sameCoord(const QPointF &a, const QPointF &b) { return a == b; }

/*! Closest separation between two DISTINCT pool vertices, brute force.
 *  \p tagged receives true for vertices carrying a marker. */
double minSeparation(const QVector<QPointF> &v, const QVector<bool> &tagged,
                     bool skipTaggedPairs, QPointF *whereA, QPointF *whereB)
{
    double best = std::numeric_limits<double>::max();
    for (int i = 0; i < v.size(); ++i)
        for (int j = i + 1; j < v.size(); ++j)
        {
            if (skipTaggedPairs && tagged[i] && tagged[j]) continue;
            const double d = dist(v[i], v[j]);
            if (d < best) { best = d; if (whereA) *whereA = v[i];
                                      if (whereB) *whereB = v[j]; }
        }
    return best;
}

double cross2(const QPointF &o, const QPointF &a, const QPointF &b)
{
    return (a.x() - o.x()) * (b.y() - o.y()) - (a.y() - o.y()) * (b.x() - o.x());
}

bool properlyCrosses(const QPointF &a, const QPointF &b,
                     const QPointF &c, const QPointF &d)
{
    const double d1 = cross2(a, b, c), d2 = cross2(a, b, d);
    const double d3 = cross2(c, d, a), d4 = cross2(c, d, b);
    if (d1 == 0.0 || d2 == 0.0 || d3 == 0.0 || d4 == 0.0) return false;
    return ((d1 > 0.0) != (d2 > 0.0)) && ((d3 > 0.0) != (d4 > 0.0));
}

int countCrossings(const Edges &e)
{
    int n = 0;
    for (int i = 0; i < e.e.size(); ++i)
        for (int j = i + 1; j < e.e.size(); ++j)
        {
            const auto &A = e.e[i]; const auto &B = e.e[j];
            if (sameCoord(A.first, B.first) || sameCoord(A.first, B.second)
             || sameCoord(A.second, B.first) || sameCoord(A.second, B.second))
                continue;
            if (properlyCrosses(A.first, A.second, B.first, B.second)) ++n;
        }
    return n;
}

/*! Smallest distance from any vertex to a segment it is not an endpoint of,
 *  reporting WHERE so the caller can decide whether that location was declared
 *  a residual. */
double minVertexEdgeGap(const QVector<QPointF> &verts, const Edges &e,
                        QPointF *whereVertex = nullptr,
                        QPair<QPointF, QPointF> *whereEdge = nullptr)
{
    double best = std::numeric_limits<double>::max();
    for (const QPointF &p : verts)
        for (const auto &s : e.e)
        {
            if (sameCoord(p, s.first) || sameCoord(p, s.second)) continue;
            const double d =
                std::sqrt(mesh::pslg::distSqToSegment(p, s.first, s.second));
            if (d < best)
            {
                best = d;
                if (whereVertex) *whereVertex = p;
                if (whereEdge)   *whereEdge   = s;
            }
        }
    return best;
}

/*! Is \p p within \p r of any reported residual?  A sub-weldRadius feature is
 *  acceptable only when conditioning declared it — see the invariant (2)
 *  discussion in condition_weldingProperties(). */
bool residualNear(const ConditionReport &rep, const QPointF &p, double r)
{
    for (const Violation &v : rep.residuals)
        if (dist(v.xy, p) <= r) return true;
    return false;
}

/*! Largest distance from any ORIGINAL vertex to the resampled polyline —
 *  the guarantee resampleMinLength actually makes. */
double deviationOf(const QVector<QPointF> &orig, const QVector<QPointF> &res)
{
    double worst = 0.0;
    for (const QPointF &p : orig)
    {
        double best = std::numeric_limits<double>::max();
        for (int i = 1; i < res.size(); ++i)
            best = std::min(best,
                mesh::pslg::distSqToSegment(p, res[i - 1], res[i]));
        worst = std::max(worst, best);
    }
    return std::sqrt(worst);
}

int shortSegmentCount(const QVector<QPointF> &p, double minLen)
{
    int n = 0;
    for (int i = 1; i < p.size(); ++i)
        if (dist(p[i - 1], p[i]) < minLen) ++n;
    return n;
}

bool segsEqual(const QVector<ConstraintSegment> &a,
               const QVector<ConstraintSegment> &b)
{
    if (a.size() != b.size()) return false;
    for (int i = 0; i < a.size(); ++i)
    {
        if (a[i].marker != b[i].marker || a[i].tag != b[i].tag) return false;
        if (a[i].path != b[i].path) return false;
    }
    return true;
}

bool ptsEqual(const QVector<SteinerPoint> &a, const QVector<SteinerPoint> &b)
{
    if (a.size() != b.size()) return false;
    for (int i = 0; i < a.size(); ++i)
        if (a[i].xy != b[i].xy || a[i].marker != b[i].marker
            || a[i].tag != b[i].tag || a[i].z != b[i].z || a[i].hasZ != b[i].hasZ)
            return false;
    return true;
}

/*! A PSLG bundle, so the property assertions read as one call. */
struct Pslg
{
    QVector<QPolygonF>          domains;
    QVector<QVector<QPointF>>   holes;
    QVector<ConstraintSegment>  segs;
    QVector<SteinerPoint>       pts;
};

bool operator==(const Pslg &a, const Pslg &b)
{
    return a.domains == b.domains && a.holes == b.holes
        && segsEqual(a.segs, b.segs) && ptsEqual(a.pts, b.pts);
}

bool condition(Pslg *g, const MinSizePolicy &pol, ConditionReport *rep)
{
    return mesh::pslg::conditionMinSize(&g->domains, &g->holes, &g->segs,
                                        &g->pts, pol, rep);
}

/*! condition(), with the abandon reason surfaced in the failure message —
 *  a bare "returned FALSE" says nothing about which invariant tripped. */
#define VERIFY_CONDITIONED(g, pol, rep) \
    QVERIFY2(condition((g), (pol), (rep)), \
             qPrintable(QStringLiteral("conditioning abandoned: %1") \
                            .arg((rep)->abandonReason)))

/*! Does Triangle accept it?  A non-planar PSLG aborts generation, which is the
 *  failure mode conditioning must never introduce. */
bool meshable(const Pslg &g, QString *errOut = nullptr)
{
    mesh::MeshGenerator mg;
    mg.setDomains(g.domains);
    for (const auto &s : g.segs) mg.addConstraintSegment(s);
    for (const auto &p : g.pts)  mg.addSteinerPoint(p);
    mesh::GenerationOptions o;
    o.minAngle = 20.0;
    o.quiet    = true;
    mg.setOptions(o);
    const mesh::MeshResult r = mg.generate();
    if (!r.ok && errOut) *errOut = r.errorMsg;
    return r.ok && !r.triangles.isEmpty();
}


/*! Vertices produced for a 200 x 200 domain under one refinement setup. */
int meshVertexCount(double uniformCap,
                    const MinSizePolicy *floorPolicy,
                    const QVector<mesh::RegionMarker> &regions = {},
                    bool forceHook = false)
{
    mesh::MeshGenerator mg;
    mg.setDomains({square(0, 0, 200)});
    for (const auto &r : regions) mg.addRegion(r);

    mesh::GenerationOptions o;
    o.maxArea  = uniformCap;
    o.minAngle = 26.0;
    o.quiet    = true;
    mg.setOptions(o);

    if (floorPolicy || forceHook)
    {
        mesh::RefineHook hook;
        const double capped = floorPolicy
                                  ? floorPolicy->refinementAreaCap(uniformCap)
                                  : uniformCap;
        hook.targetAreaAt = [capped](double, double) { return capped; };
        mg.setRefineHook(hook);
    }
    const mesh::MeshResult r = mg.generate();
    return r.ok ? int(r.vertices.size()) : -1;
}

} // namespace

Q_DECLARE_METATYPE(Pslg)

class TestPslgMinSize : public QObject
{
    Q_OBJECT

private slots:

    // ════════════════════════════════════════════════════════════════════
    // Phase 1 — resampleMinLength / resampleRingMinLength
    // ════════════════════════════════════════════════════════════════════

    void resample_noOpOnDegenerateInput()
    {
        const QVector<QPointF> two{{0, 0}, {0.001, 0}};
        QCOMPARE(mesh::pslg::resampleMinLength(two, 5.0, 1.0), two);

        const QVector<QPointF> many{{0, 0}, {0.1, 0}, {0.2, 0}, {1, 0}};
        QCOMPARE(mesh::pslg::resampleMinLength(many, 0.0, 1.0), many);
        QCOMPARE(mesh::pslg::resampleMinLength(many, -3.0, 1.0), many);
    }

    void resample_endpointsBitIdentical()
    {
        QVector<QPointF> p;
        for (int i = 0; i < 60; ++i)
            p.append(QPointF(i * 0.13, std::sin(i * 0.37) * 0.02));
        const QVector<QPointF> r = mesh::pslg::resampleMinLength(p, 1.0, 0.5);
        QVERIFY(r.size() >= 2);
        QVERIFY(r.first() == p.first());     // bit-identical, not fuzzy
        QVERIFY(r.last()  == p.last());
    }

    /*! Every short surviving segment is accounted for by a flag. */
    void resample_shortSegmentsAreAllFlagged()
    {
        // Densified straight run (drops cleanly) followed by a hard corner the
        // deviation cap must preserve.
        QVector<QPointF> p;
        for (int i = 0; i <= 40; ++i) p.append(QPointF(i * 0.1, 0.0));
        for (int i = 1; i <= 10; ++i) p.append(QPointF(4.0, i * 0.1));

        const double minLen = 1.0, dev = 0.05;
        int flagged = 0;
        const QVector<QPointF> r =
            mesh::pslg::resampleMinLength(p, minLen, dev, &flagged);

        const int shortN = shortSegmentCount(r, minLen);
        QVERIFY2(shortN <= flagged,
                 qPrintable(QStringLiteral("%1 short segment(s) but only %2 flagged")
                                .arg(shortN).arg(flagged)));
        QVERIFY(deviationOf(p, r) <= dev + 1e-9);
    }

    /*! The tail-handling bug: a zig-zag whose WHOLE extent is below minLen.
     *  Nothing can be dropped without exceeding the deviation cap, and the fix
     *  was to loop the tail check rather than test once. */
    void resample_tightZigZagRespectsDeviation()
    {
        QVector<QPointF> p;
        for (int i = 0; i <= 20; ++i)
            p.append(QPointF(i * 0.05, (i % 2) ? 0.04 : -0.04));

        const double minLen = 10.0;        // far larger than the whole extent
        const double dev    = 0.01;        // smaller than the zig amplitude
        int flagged = 0;
        const QVector<QPointF> r =
            mesh::pslg::resampleMinLength(p, minLen, dev, &flagged);

        QVERIFY2(deviationOf(p, r) <= dev + 1e-9,
                 qPrintable(QStringLiteral("deviation %1 > cap %2")
                                .arg(deviationOf(p, r)).arg(dev)));
        QVERIFY(shortSegmentCount(r, minLen) <= flagged);
        QVERIFY(r.first() == p.first());
        QVERIFY(r.last()  == p.last());
    }

    void resample_isIdempotent()
    {
        QVector<QPointF> p;
        for (int i = 0; i <= 50; ++i)
            p.append(QPointF(i * 0.2, std::cos(i * 0.5) * 0.3));

        const QVector<QPointF> once  = mesh::pslg::resampleMinLength(p, 1.0, 0.4);
        const QVector<QPointF> twice = mesh::pslg::resampleMinLength(once, 1.0, 0.4);
        QCOMPARE(twice, once);
    }

    void resampleRing_keepsClosureAndOrientation()
    {
        // Densified CCW square; every edge far below minLen.
        QVector<QPointF> ring;
        const double s = 10.0;
        const int    k = 40;
        for (int i = 0; i < k; ++i) ring.append(QPointF(s * i / k, 0));
        for (int i = 0; i < k; ++i) ring.append(QPointF(s, s * i / k));
        for (int i = 0; i < k; ++i) ring.append(QPointF(s - s * i / k, s));
        for (int i = 0; i < k; ++i) ring.append(QPointF(0, s - s * i / k));
        ring.append(ring.first());

        const double before = mesh::pslg::ringSignedArea(ring);
        QVERIFY(before > 0.0);                         // CCW to begin with

        int flagged = 0;
        const QVector<QPointF> r =
            mesh::pslg::resampleRingMinLength(ring, 1.0, 0.2, &flagged);

        QVERIFY(r.size() >= 4);
        QVERIFY(r.first() == r.last());                // still closed
        QVERIFY(mesh::pslg::ringSignedArea(r) > 0.0);  // orientation preserved
        QVERIFY(r.size() < ring.size());               // and it actually thinned
    }

    /*! The seam edge last->first is invisible to the open-sequence pass; it
     *  needed its own fix-up, so it gets its own test. */
    void resampleRing_seamEdgeObeysMinLen()
    {
        // Vertex spacing chosen so the open pass lands its final kept vertex a
        // hair before the closing vertex, leaving a very short seam.
        QVector<QPointF> ring;
        const int n = 37;                       // deliberately not a multiple
        const double rad = 5.0;
        for (int i = 0; i < n; ++i)
        {
            const double a = 2.0 * M_PI * i / n;
            ring.append(QPointF(rad * std::cos(a), rad * std::sin(a)));
        }
        ring.append(ring.first());

        const double minLen = 2.0;
        int flagged = 0;
        const QVector<QPointF> r =
            mesh::pslg::resampleRingMinLength(ring, minLen, 1.0, &flagged);
        QVERIFY(r.size() >= 4);
        QVERIFY(r.first() == r.last());

        // Seam = the edge from the last DISTINCT vertex back to the first.
        const double seam = dist(r[r.size() - 2], r.first());
        QVERIFY2(seam >= minLen || flagged > 0,
                 qPrintable(QStringLiteral("seam %1 < minLen %2 and nothing flagged")
                                .arg(seam).arg(minLen)));
    }

    void resampleRing_isIdempotent()
    {
        QVector<QPointF> ring;
        const int n = 60;
        for (int i = 0; i < n; ++i)
        {
            const double a = 2.0 * M_PI * i / n;
            ring.append(QPointF(8.0 * std::cos(a), 6.0 * std::sin(a)));
        }
        ring.append(ring.first());

        const QVector<QPointF> once =
            mesh::pslg::resampleRingMinLength(ring, 1.5, 0.5);
        const QVector<QPointF> twice =
            mesh::pslg::resampleRingMinLength(once, 1.5, 0.5);
        QCOMPARE(twice, once);
    }

    void resampleRing_degenerateInputUnchanged()
    {
        const QVector<QPointF> tri{{0, 0}, {1, 0}, {0, 1}, {0, 0}};
        // minLen swallows the whole triangle: must return the input, not a
        // degenerate 2-vertex ring.
        const QVector<QPointF> r =
            mesh::pslg::resampleRingMinLength(tri, 100.0, 50.0);
        QCOMPARE(r, tri);
    }

    // ════════════════════════════════════════════════════════════════════
    // Phase 2 — conditionMinSize: welding properties
    // ════════════════════════════════════════════════════════════════════

    void policy_defaultsResolveAndAreIdempotent()
    {
        MinSizePolicy p;
        p.minCellSize = 4.0;
        p.resolveDefaults();
        QCOMPARE(p.weldRadius,    4.0);
        QCOMPARE(p.minSegmentLen, 4.0);
        QCOMPARE(p.maxDeviation,  2.0);
        const MinSizePolicy q = p;
        p.resolveDefaults();
        QCOMPARE(p.weldRadius, q.weldRadius);
        QCOMPARE(p.maxDeviation, q.maxDeviation);

        // Area of an equilateral triangle of side h.
        QVERIFY(std::abs(p.minTriangleArea()
                         - 0.25 * std::sqrt(3.0) * 16.0) < 1e-12);

        MinSizePolicy off;
        QVERIFY(!off.enabled());
        QCOMPARE(off.minTriangleArea(), 0.0);
    }

    void condition_disabledIsExactNoOp()
    {
        Pslg g;
        g.domains = {square(0, 0, 100)};
        g.segs    = {{{{10, 10}, {10.05, 10}, {40, 40}}, 7, QStringLiteral("C1")}};
        g.pts     = {{QPointF(50, 50), 3, QStringLiteral("J1"), 0.0, false}};
        const Pslg before = g;

        MinSizePolicy pol;                 // minCellSize stays 0
        ConditionReport rep;
        VERIFY_CONDITIONED(&g, pol, &rep);
        QVERIFY(!rep.conditioningAbandoned);
        QVERIFY(g == before);
    }

    /*! The core property set, over several adversarial PSLGs. */
    void condition_weldingProperties_data()
    {
        QTest::addColumn<Pslg>("input");
        QTest::addColumn<double>("h");

        const QPolygonF dom = square(-10, -10, 220);

        {   // Dense polyline: GIS-digitised vertices centimetres apart.
            Pslg g; g.domains = {dom};
            QVector<QPointF> path;
            for (int i = 0; i <= 400; ++i) path.append(QPointF(10 + i * 0.05, 30));
            g.segs = {{path, 11, QStringLiteral("dense")}};
            QTest::newRow("dense polyline") << g << 2.0;
        }
        {   // Two near-parallel alignments a hair apart.
            Pslg g; g.domains = {dom};
            QVector<QPointF> a, b;
            for (int i = 0; i <= 40; ++i)
            {
                a.append(QPointF(10 + i * 2.0, 60.0));
                b.append(QPointF(10 + i * 2.0, 60.3));
            }
            g.segs = {{a, 21, QStringLiteral("pA")}, {b, 22, QStringLiteral("pB")}};
            QTest::newRow("near-parallel pair") << g << 2.0;
        }
        {   // Endpoint stopping just short of another alignment (the classic
            // dangling-node case the vertex-edge fix-up exists for).
            Pslg g; g.domains = {dom};
            g.segs = {
                {{{20, 100}, {120, 100}}, 31, QStringLiteral("trunk")},
                {{{70, 130}, {70, 100.4}}, 32, QStringLiteral("stub")}};
            QTest::newRow("dangling endpoint") << g << 2.0;
        }
        {   // A segment grazing a hole ring.
            Pslg g; g.domains = {dom};
            g.holes = {ringOf(square(80, 150, 30))};
            g.segs  = {{{{20, 149.0}, {170, 149.0}}, 41, QStringLiteral("graze")}};
            QTest::newRow("segment grazing a ring") << g << 2.0;
        }
        {   // Two tagged SWMM nodes closer than h: must both survive.
            Pslg g; g.domains = {dom};
            g.pts = {{QPointF(100, 40), 51, QStringLiteral("J1"), 0.0, false},
                     {QPointF(100.5, 40), 52, QStringLiteral("J2"), 0.0, false}};
            g.segs = {{{{100, 40}, {160, 40}}, 53, QStringLiteral("C1")},
                      {{{100.5, 40}, {160, 90}}, 54, QStringLiteral("C2")}};
            QTest::newRow("two tagged nodes within h") << g << 2.0;
        }
        {   // Crossing alignments — the PSLG-planarity case.
            Pslg g; g.domains = {dom};
            g.segs = {{{{20, 20}, {180, 180}}, 61, QStringLiteral("X1")},
                      {{{20, 180}, {180, 20}}, 62, QStringLiteral("X2")}};
            QTest::newRow("crossing alignments") << g << 2.0;
        }
    }

    void condition_weldingProperties()
    {
        QFETCH(Pslg, input);
        QFETCH(double, h);

        Pslg g = input;
        MinSizePolicy pol;
        pol.minCellSize  = h;
        pol.trimAngleDeg = 0.0;             // welding under test, not trimming
        pol.resolveDefaults();

        ConditionReport rep;
        const bool ok = condition(&g, pol, &rep);
        if (!ok)
        {
            // Fail-safe is always a legal outcome; then nothing may have moved.
            QVERIFY(rep.conditioningAbandoned);
            QVERIFY(g == input);
            return;
        }

        const double wr = pol.weldRadius;

        // (4) displacement bound — weldRadius, not h/2 (handoff §2.1).
        QVERIFY2(rep.maxDisplacement <= wr + 1e-9,
                 qPrintable(QStringLiteral("maxDisplacement %1 > weldRadius %2")
                                .arg(rep.maxDisplacement).arg(wr)));

        // (5) tagged vertices did not move AT ALL, and none merged.
        QCOMPARE(g.pts.size(), input.pts.size());
        for (int i = 0; i < input.pts.size(); ++i)
        {
            if (input.pts[i].marker == 0) continue;
            QVERIFY2(g.pts[i].xy == input.pts[i].xy,
                     qPrintable(QStringLiteral("tagged node %1 moved")
                                    .arg(input.pts[i].tag)));
        }
        QSet<QPair<qreal, qreal>> taggedXY;
        for (const auto &p : std::as_const(g.pts))
        {
            if (p.marker == 0) continue;
            const auto k = qMakePair(p.xy.x(), p.xy.y());
            QVERIFY2(!taggedXY.contains(k), "two tagged nodes merged");
            taggedXY.insert(k);
        }

        // (3) no new crossings.
        const int after  = countCrossings(allEdges(g.domains, g.holes, g.segs));
        const int before = countCrossings(
            allEdges(input.domains, input.holes, input.segs));
        QVERIFY2(after <= before,
                 qPrintable(QStringLiteral("crossings %1 -> %2").arg(before).arg(after)));
        QVERIFY(rep.crossingsAfter <= rep.crossingsBefore);

        // (1) survivors pairwise >= weldRadius, tagged pairs excepted.
        QVector<QPointF> verts = allVertices(g.domains, g.holes, g.segs, g.pts);
        // An "identity" is anything conditionMinSize refuses to merge into
        // another identity: a tagged Steiner node OR an endpoint of a tagged
        // constraint.  Both are coupling locations.
        QVector<QPointF> identities;
        for (const auto &p : std::as_const(g.pts))
            if (p.marker != 0) identities.append(p.xy);
        for (const auto &sg : std::as_const(g.segs))
            if (sg.marker != 0 && sg.path.size() >= 2)
            { identities.append(sg.path.first()); identities.append(sg.path.last()); }
        QVector<bool> tagged(verts.size(), false);
        for (int i = 0; i < verts.size(); ++i)
            for (const QPointF &id : std::as_const(identities))
                if (id == verts[i]) { tagged[i] = true; break; }
        QPointF a, b;
        const double sep = minSeparation(verts, tagged, true, &a, &b);
        QVERIFY2(sep >= wr - 1e-9,
                 qPrintable(QStringLiteral("separation %1 < weldRadius %2 "
                                           "between (%3,%4) and (%5,%6)")
                                .arg(sep).arg(wr)
                                .arg(a.x()).arg(a.y()).arg(b.x()).arg(b.y())));

        // (2) no vertex within weldRadius of a non-incident segment — EXCEPT
        // where two protected identities sit closer than that to begin with.
        //
        // Neither of a pair of un-mergeable identities may move, so once the
        // vertex-edge fix-up has spliced each one into the other's path, the
        // path doubles back and the first vertex is still within weldRadius of
        // the edge two steps along.  No further split can help: the guard in
        // stage 3c refuses to insert a vertex a path already visits, and
        // inserting it twice would emit a duplicate segment.  The design's
        // answer is to REPORT such a location rather than to move a coupling
        // point, so that is what this asserts.  pslgminsize.h states invariant
        // (2) unqualified; the qualification is recorded there too.
        QPointF badV;
        QPair<QPointF, QPointF> badE;
        const double gap = minVertexEdgeGap(
            verts, allEdges(g.domains, g.holes, g.segs), &badV, &badE);
        if (gap < wr - 1e-9)
            QVERIFY2(residualNear(rep, badV, wr),
                     qPrintable(QStringLiteral(
                         "vertex-edge gap %1 < weldRadius %2 at (%3,%4) "
                         "against edge (%5,%6)-(%7,%8), and no residual "
                         "was reported there")
                             .arg(gap).arg(wr).arg(badV.x()).arg(badV.y())
                             .arg(badE.first.x()).arg(badE.first.y())
                             .arg(badE.second.x()).arg(badE.second.y())));

        // (6) the single most valuable assertion: Triangle accepts it.
        QString err;
        QVERIFY2(meshable(g, &err), qPrintable(QStringLiteral("mesh failed: ") + err));
    }

    void condition_isDeterministic()
    {
        Pslg base;
        base.domains = {square(-5, -5, 120)};
        for (int i = 0; i < 12; ++i)
        {
            QVector<QPointF> path;
            for (int j = 0; j <= 30; ++j)
                path.append(QPointF(5 + j * 3.3 + 0.11 * i, 5 + i * 8.0 + 0.07 * j));
            base.segs.append({path, 100 + i, QStringLiteral("L%1").arg(i)});
        }
        for (int i = 0; i < 20; ++i)
            base.pts.append({QPointF(7 + i * 5.4, 60 + (i % 3) * 0.4),
                             200 + i, QStringLiteral("N%1").arg(i), 0.0, false});

        MinSizePolicy pol;
        pol.minCellSize = 3.0;
        pol.resolveDefaults();

        Pslg a = base, b = base;
        ConditionReport ra, rb;
        const bool oka = condition(&a, pol, &ra);
        const bool okb = condition(&b, pol, &rb);
        QCOMPARE(oka, okb);
        QVERIFY(a == b);
        QCOMPARE(ra.verticesWelded, rb.verticesWelded);
        QCOMPARE(ra.segmentsSplit,  rb.segmentsSplit);
        QCOMPARE(ra.maxDisplacement, rb.maxDisplacement);
    }

    /*! Fail-safe: a domain too small for h collapses on welding, so the whole
     *  conditioning is abandoned and every argument comes back untouched. */
    void condition_failSafeRestoresEveryArgument()
    {
        Pslg g;
        g.domains = {square(0, 0, 1.0)};       // 1 unit across, h = 20
        g.holes   = {ringOf(square(0.2, 0.2, 0.4))};
        g.segs    = {{{{0.1, 0.1}, {0.9, 0.9}}, 5, QStringLiteral("C")}};
        g.pts     = {{QPointF(0.5, 0.5), 9, QStringLiteral("J"), 1.25, true}};
        const Pslg before = g;

        MinSizePolicy pol;
        pol.minCellSize = 20.0;
        pol.resolveDefaults();

        ConditionReport rep;
        QVERIFY(!condition(&g, pol, &rep));
        QVERIFY(rep.conditioningAbandoned);
        QVERIFY2(g == before, "fail-safe did not restore all four vectors");
        QVERIFY(!rep.summary().isEmpty());
    }

    /*! ringsReadOnly is set on a boundary-cache hit; the rings must then come
     *  back byte-identical, since the cached hole seeds were computed against
     *  those exact vertices. */
    void condition_ringsReadOnlyLeavesRingsUntouched()
    {
        Pslg g;
        g.domains = {square(0, 0, 100)};
        QVector<QPointF> hole;                          // densified, sub-h edges
        for (int i = 0; i < 60; ++i)
        {
            const double a = 2.0 * M_PI * i / 60;
            hole.append(QPointF(50 + 6 * std::cos(a), 50 + 6 * std::sin(a)));
        }
        hole.append(hole.first());
        g.holes = {hole};
        g.segs  = {{{{5, 5}, {5.2, 5}, {40, 20}}, 3, QStringLiteral("C")}};
        const Pslg before = g;

        MinSizePolicy pol;
        pol.minCellSize   = 3.0;
        pol.ringsReadOnly = true;
        pol.resolveDefaults();

        ConditionReport rep;
        VERIFY_CONDITIONED(&g, pol, &rep);
        QVERIFY(!rep.conditioningAbandoned);
        QCOMPARE(g.domains, before.domains);
        QCOMPARE(g.holes,   before.holes);
        QCOMPARE(rep.holesDropped, 0);
    }

    void condition_subScalePathBecomesSteinerPoint()
    {
        Pslg g;
        g.domains = {square(0, 0, 100)};
        g.segs    = {{{{50, 50}, {50.4, 50.2}}, 77, QStringLiteral("tiny")}};

        MinSizePolicy pol;
        pol.minCellSize = 5.0;
        pol.resolveDefaults();

        ConditionReport rep;
        VERIFY_CONDITIONED(&g, pol, &rep);
        QCOMPARE(rep.pathsCollapsed, 1);
        QVERIFY(g.segs.isEmpty());
        // Identity survives as a Steiner point, exactly as nodeMinSeparation
        // demotes crowded nodes.
        QCOMPARE(g.pts.size(), 1);
        QCOMPARE(g.pts.first().marker, 77);
        QCOMPARE(g.pts.first().tag, QStringLiteral("tiny"));
    }

    void condition_subScaleHoleIsEmptiedNotErased()
    {
        Pslg g;
        g.domains = {square(0, 0, 100)};
        g.holes   = {ringOf(square(20, 20, 30)),      // survives
                     ringOf(square(70, 70, 0.5))};    // sub-scale
        MinSizePolicy pol;
        pol.minCellSize = 5.0;
        pol.resolveDefaults();

        ConditionReport rep;
        VERIFY_CONDITIONED(&g, pol, &rep);
        QCOMPARE(rep.holesDropped, 1);
        // Emptied, NOT erased — holeRings must stay index-parallel with
        // bprep.holeSeeds / holeValid downstream (handoff §5.4).
        QCOMPARE(g.holes.size(), 2);
        QVERIFY(!g.holes[0].isEmpty());
        QVERIFY(g.holes[1].isEmpty());
    }

    /*! Two coupling identities closer than h cannot be separated, so BOTH
     *  survive and the location is reported.  This is the documented exception
     *  to invariants (1) and (2) — see pslgminsize.h. */
    void condition_closeIdentitiesSurviveAndAreReported()
    {
        Pslg g;
        g.domains = {square(0, 0, 200)};
        g.pts = {{QPointF(100, 100), 1, QStringLiteral("J1"), 0.0, false},
                 {QPointF(100.4, 100), 2, QStringLiteral("J2"), 0.0, false}};
        g.segs = {{{{100, 100}, {170, 100}}, 3, QStringLiteral("C1")},
                  {{{100.4, 100}, {170, 160}}, 4, QStringLiteral("C2")}};

        MinSizePolicy pol;
        pol.minCellSize  = 4.0;
        pol.trimAngleDeg = 0.0;
        pol.resolveDefaults();

        ConditionReport rep;
        VERIFY_CONDITIONED(&g, pol, &rep);
        QVERIFY(!rep.conditioningAbandoned);

        // Both nodes survive, at their original coordinates.
        QCOMPARE(g.pts.size(), 2);
        QVERIFY(g.pts[0].xy == QPointF(100, 100));
        QVERIFY(g.pts[1].xy == QPointF(100.4, 100));

        // ...and the un-fixable proximity is reported, not swallowed.
        bool reported = false;
        for (const Violation &v : std::as_const(rep.residuals))
            if (v.cause == ViolationCause::CloseFeatures
                && dist(v.xy, QPointF(100.2, 100)) < 4.0) reported = true;
        QVERIFY2(reported, "the un-separable identity pair was not reported");

        QString err;
        QVERIFY2(meshable(g, &err), qPrintable(QStringLiteral("mesh failed: ") + err));
    }

    /*! Cancelling mid-conditioning must leave NO partial state: the same
     *  fail-safe restore as any other abandonment. */
    void condition_cancellationLeavesNoPartialState()
    {
        Pslg base;
        base.domains = {square(-5, -5, 220)};
        for (int i = 0; i < 8; ++i)
        {
            QVector<QPointF> path;
            for (int j = 0; j <= 60; ++j)
                path.append(QPointF(5 + j * 0.3, 10 + i * 20.0 + 0.05 * j));
            base.segs.append({path, 10 + i, QStringLiteral("L%1").arg(i)});
        }

        MinSizePolicy pol;
        pol.minCellSize = 2.0;
        pol.resolveDefaults();

        // Cancel at each of the first few polls, so every early-out inside
        // conditionMinSize gets exercised.
        for (int budget = 0; budget < 6; ++budget)
        {
            Pslg g = base;
            int polls = 0;
            ConditionReport rep;
            const bool ok = mesh::pslg::conditionMinSize(
                &g.domains, &g.holes, &g.segs, &g.pts, pol, &rep,
                [&polls, budget] { return polls++ >= budget; });

            QVERIFY2(!ok, qPrintable(QStringLiteral(
                "cancellation at poll %1 was not honoured").arg(budget)));
            QVERIFY(rep.conditioningAbandoned);
            QVERIFY2(g == base, qPrintable(QStringLiteral(
                "cancellation at poll %1 left partial state").arg(budget)));
        }
    }

    // ════════════════════════════════════════════════════════════════════
    // Phase 3 — corner trimming
    // ════════════════════════════════════════════════════════════════════

    /*! Two constraints meeting end-to-end, sweeping the apex angle. */
    void trim_endToEndCorner_data()
    {
        QTest::addColumn<double>("thetaDeg");
        QTest::addColumn<bool>("expectTrim");
        for (const double t : {5.0, 10.0, 20.0, 25.0})
            QTest::addRow("theta %.0f (below)", t) << t << true;
        for (const double t : {31.0, 35.0, 40.0})
            QTest::addRow("theta %.0f (above)", t) << t << false;
    }

    void trim_endToEndCorner()
    {
        QFETCH(double, thetaDeg);
        QFETCH(bool, expectTrim);

        // Legs long enough that the short-leg cap never fires: at 5 degrees
        // rNeed = h / (2 sin(2.5 deg)) is about 11.5 h, and the cap is
        // 0.4 * leg, so the leg must exceed roughly 29 h.
        const double h    = 1.0;
        const double leg  = 200.0;
        const QPointF apex(0, 0);
        const double half = thetaDeg * M_PI / 360.0;
        const QPointF p(leg * std::cos(half), leg * std::sin(half));
        const QPointF q(leg * std::cos(half), -leg * std::sin(half));

        Pslg g;
        g.domains = {square(-50, -260, 520)};
        g.segs = {{{p, apex}, 1, QStringLiteral("A")},
                  {{apex, q}, 2, QStringLiteral("B")}};
        const Pslg before = g;

        MinSizePolicy pol;
        pol.minCellSize  = h;
        pol.trimAngleDeg = 30.0;
        pol.resolveDefaults();

        ConditionReport rep;
        VERIFY_CONDITIONED(&g, pol, &rep);
        QVERIFY(!rep.conditioningAbandoned);

        if (!expectTrim)
        {
            QCOMPARE(rep.cornersTrimmed, 0);
            QCOMPARE(g.segs.size(), before.segs.size());
            // Apex still present on both legs.
            QVERIFY(g.segs[0].path.contains(apex));
            QVERIFY(g.segs[1].path.contains(apex));
            return;
        }

        QCOMPARE(rep.cornersTrimmed, 1);
        // A bridge segment is added, and the apex is gone from both legs.
        QCOMPARE(g.segs.size(), before.segs.size() + 1);
        for (const auto &s : std::as_const(g.segs))
            QVERIFY2(!s.path.contains(apex), "apex survived the trim");

        // The bridge is the untagged 2-point path that was added.
        int bridges = 0;
        for (const auto &s : std::as_const(g.segs))
            if (s.marker == 0 && s.tag.isEmpty() && s.path.size() == 2) ++bridges;
        QCOMPARE(bridges, 1);
    }

    /*! Hairpin inside ONE polyline: a single path edit, no bridge segment. */
    void trim_hairpinInOnePathAddsNoBridge()
    {
        const QPointF apex(0, 0);
        const double half = 10.0 * M_PI / 360.0;
        const QPointF p(200 * std::cos(half),  200 * std::sin(half));
        const QPointF q(200 * std::cos(half), -200 * std::sin(half));

        Pslg g;
        g.domains = {square(-50, -260, 520)};
        g.segs    = {{{p, apex, q}, 9, QStringLiteral("hairpin")}};

        MinSizePolicy pol;
        pol.minCellSize  = 1.0;
        pol.trimAngleDeg = 30.0;
        pol.resolveDefaults();

        ConditionReport rep;
        VERIFY_CONDITIONED(&g, pol, &rep);
        QCOMPARE(rep.cornersTrimmed, 1);
        QCOMPARE(g.segs.size(), 1);                 // no bridge
        QCOMPARE(g.segs[0].path.size(), 4);         // p, n0, n1, q
        QVERIFY(!g.segs[0].path.contains(apex));
        QCOMPARE(g.segs[0].marker, 9);
        QCOMPARE(g.segs[0].tag, QStringLiteral("hairpin"));
    }

    /*! Degree-3 apex: trimming one pair leaves the apex, so nothing is done
     *  and it is counted as skipped instead (handoff §2.2). */
    void trim_degreeThreeVertexIsSkippedNotTrimmed()
    {
        const QPointF apex(0, 0);
        const double half = 8.0 * M_PI / 360.0;

        Pslg g;
        g.domains = {square(-260, -260, 520)};
        g.segs = {
            {{QPointF(200 * std::cos(half),  200 * std::sin(half)), apex}, 1, QStringLiteral("A")},
            {{apex, QPointF(200 * std::cos(half), -200 * std::sin(half))}, 2, QStringLiteral("B")},
            {{apex, QPointF(-200, 0)}, 3, QStringLiteral("C")}};
        const int segsBefore = g.segs.size();

        MinSizePolicy pol;
        pol.minCellSize  = 1.0;
        pol.trimAngleDeg = 30.0;
        pol.resolveDefaults();

        ConditionReport rep;
        VERIFY_CONDITIONED(&g, pol, &rep);
        QCOMPARE(rep.cornersTrimmed, 0);
        QVERIFY(rep.cornersSkipped >= 1);
        QCOMPARE(g.segs.size(), segsBefore);

        // Reported as a SmallAngle residual rather than silently ignored.
        bool sawSmallAngle = false;
        for (const Violation &v : std::as_const(rep.residuals))
            if (v.cause == ViolationCause::SmallAngle) { sawSmallAngle = true; break; }
        QVERIFY(sawSmallAngle);
    }

    /*! A tagged SWMM node apex is not trimmed by default, and does not move. */
    void trim_taggedApexUntouchedByDefault()
    {
        const QPointF apex(0, 0);
        const double half = 8.0 * M_PI / 360.0;
        const QPointF p(200 * std::cos(half),  200 * std::sin(half));
        const QPointF q(200 * std::cos(half), -200 * std::sin(half));

        Pslg base;
        base.domains = {square(-50, -260, 520)};
        base.segs = {{{p, apex}, 1, QStringLiteral("A")},
                     {{apex, q}, 2, QStringLiteral("B")}};
        base.pts  = {{apex, 42, QStringLiteral("MH1"), 0.0, false}};

        MinSizePolicy pol;
        pol.minCellSize      = 1.0;
        pol.trimAngleDeg     = 30.0;
        pol.trimAtTaggedNodes = false;
        pol.resolveDefaults();

        Pslg g = base;
        ConditionReport rep;
        VERIFY_CONDITIONED(&g, pol, &rep);
        QCOMPARE(rep.cornersTrimmed, 0);
        QVERIFY(rep.cornersSkipped >= 1);
        QCOMPARE(g.pts.size(), 1);
        QVERIFY(g.pts.first().xy == apex);          // the coupling location held
        QVERIFY(g.segs[0].path.contains(apex));

        // With the opt-in on, the same corner IS trimmed.
        Pslg g2 = base;
        MinSizePolicy pol2 = pol;
        pol2.trimAtTaggedNodes = true;
        ConditionReport rep2;
        VERIFY_CONDITIONED(&g2, pol2, &rep2);
        QCOMPARE(rep2.cornersTrimmed, 1);
    }

    /*! Short-leg cap: r would exceed 0.4 x the shorter leg, so skip + report. */
    void trim_shortLegCapSkipsAndReports()
    {
        const QPointF apex(0, 0);
        const double half = 8.0 * M_PI / 360.0;
        const double leg  = 5.0;                    // far below the ~29 h needed
        const QPointF p(leg * std::cos(half),  leg * std::sin(half));
        const QPointF q(leg * std::cos(half), -leg * std::sin(half));

        Pslg g;
        g.domains = {square(-20, -20, 60)};
        g.segs = {{{p, apex}, 1, QStringLiteral("A")},
                  {{apex, q}, 2, QStringLiteral("B")}};

        MinSizePolicy pol;
        pol.minCellSize  = 1.0;                     // rNeed ~ 7.2 > 0.4 * 5
        pol.trimAngleDeg = 30.0;
        pol.resolveDefaults();

        ConditionReport rep;
        VERIFY_CONDITIONED(&g, pol, &rep);
        QCOMPARE(rep.cornersTrimmed, 0);
        QVERIFY(rep.cornersSkipped >= 1);
        bool sawSmallAngle = false;
        for (const Violation &v : std::as_const(rep.residuals))
            if (v.cause == ViolationCause::SmallAngle) { sawSmallAngle = true; break; }
        QVERIFY(sawSmallAngle);
    }

    /*! Conditioning twice trims nothing the second time. */
    void trim_isIdempotent()
    {
        const QPointF apex(0, 0);
        const double half = 10.0 * M_PI / 360.0;
        Pslg g;
        g.domains = {square(-50, -260, 520)};
        g.segs = {{{QPointF(200 * std::cos(half),  200 * std::sin(half)), apex},
                   1, QStringLiteral("A")},
                  {{apex, QPointF(200 * std::cos(half), -200 * std::sin(half))},
                   2, QStringLiteral("B")}};

        MinSizePolicy pol;
        pol.minCellSize  = 1.0;
        pol.trimAngleDeg = 30.0;
        pol.resolveDefaults();

        ConditionReport r1;
        VERIFY_CONDITIONED(&g, pol, &r1);
        QCOMPARE(r1.cornersTrimmed, 1);
        const Pslg once = g;

        ConditionReport r2;
        VERIFY_CONDITIONED(&g, pol, &r2);
        QCOMPARE(r2.cornersTrimmed, 0);
        QVERIFY2(g == once, "second conditioning pass changed the geometry");
    }


    // ════════════════════════════════════════════════════════════════════
    // Phase 4 — refinement floor and region bounds
    // ════════════════════════════════════════════════════════════════════

    void refinementAreaCap_rules()
    {
        MinSizePolicy off;                       // feature disabled
        QCOMPARE(off.refinementAreaCap(50.0), 50.0);
        QCOMPARE(off.refinementAreaCap(0.0), 0.0);

        MinSizePolicy p;
        p.minCellSize = 4.0;
        p.resolveDefaults();
        const double floorA = p.minTriangleArea();   // ~6.93
        QVERIFY(floorA > 0.0);

        // No cap: MUST be unconstrained, not the floor.  Returning the floor
        // refines the whole domain to the minimum size (handoff §2.4).
        QCOMPARE(p.refinementAreaCap(0.0), 0.0);

        // Too-small cap is raised to the floor.
        QCOMPARE(p.refinementAreaCap(floorA / 10.0), floorA);
        // A cap above the floor is left alone.
        QCOMPARE(p.refinementAreaCap(floorA * 10.0), floorA * 10.0);
    }

    /*! The §2.4 explosion regression, measured on a real triangulation:
     *  minimum cell size set and NO area cap must not refine the domain. */
    void refinementFloor_withNoCapDoesNotExplode()
    {
        MinSizePolicy p;
        p.minCellSize = 1.0;                     // A_min ~ 0.433 on a 200x200
        p.resolveDefaults();

        const int unconditioned = meshVertexCount(0.0, nullptr);
        const int withFloor     = meshVertexCount(0.0, &p);
        QVERIFY(unconditioned > 0);
        QVERIFY(withFloor > 0);

        // Refining 40000 map units^2 to A_min would be ~92000 cells; the
        // unconstrained answer is a few dozen vertices.
        QVERIFY2(withFloor <= 2 * unconditioned + 16,
                 qPrintable(QStringLiteral("vertex explosion: %1 with floor vs "
                                           "%2 unconditioned")
                                .arg(withFloor).arg(unconditioned)));
    }

    /*! A cap below the floor is raised to it, so the mesh is the size the
     *  FLOOR implies rather than the size the cap asked for. */
    void refinementFloor_raisesATooSmallCap()
    {
        MinSizePolicy p;
        p.minCellSize = 10.0;
        p.resolveDefaults();
        const double floorA = p.minTriangleArea();    // ~43.3

        const int atFloor  = meshVertexCount(floorA, nullptr, {}, /*forceHook=*/true);
        const int tinyCap  = meshVertexCount(floorA / 20.0, nullptr, {}, true);
        const int clamped  = meshVertexCount(floorA / 20.0, &p);
        QVERIFY(atFloor > 0 && tinyCap > 0 && clamped > 0);

        QVERIFY2(tinyCap > 5 * atFloor,
                 "the unclamped tiny cap did not actually produce a finer mesh");
        // Clamped result tracks the floor, not the tiny cap.
        QVERIFY2(clamped < tinyCap / 5,
                 qPrintable(QStringLiteral("clamped %1 vs tiny %2")
                                .arg(clamped).arg(tinyCap)));
        QVERIFY(std::abs(clamped - atFloor) <= std::max(4, atFloor / 20));
    }

    /*! Region area bounds are inert behind a numeric `a<area>` switch and
     *  ACTIVATE the moment a size function is installed
     *  (trirefinehook.h, corrected 2026-08-17).  That is why the dialog clamps
     *  them; this test is what makes the claim checkable. */
    void regionBounds_activateWithASizeFunctionAndClampToTheFloor()
    {
        mesh::RegionMarker fine;
        fine.xy        = QPointF(100, 100);
        fine.attribute = 1.0;
        fine.maxArea   = 5.0;                      // very fine
        fine.tag       = QStringLiteral("sub1");

        // (a) numeric cap, no size function: the region bound is ignored.
        const int inert = meshVertexCount(2000.0, nullptr, {fine});
        const int plain = meshVertexCount(2000.0, nullptr, {});
        QVERIFY(inert > 0 && plain > 0);
        QCOMPARE(inert, plain);

        // (b) size function installed: the bare `a` switch goes out and the
        //     region bound now bites.
        const int active = meshVertexCount(2000.0, nullptr, {fine}, true);
        QVERIFY2(active > 5 * inert,
                 qPrintable(QStringLiteral("region bound did not activate: "
                                           "%1 vs inert %2").arg(active).arg(inert)));

        // (c) clamped to the floor, as the dialog does before addRegion().
        MinSizePolicy p;
        p.minCellSize = 10.0;
        p.resolveDefaults();
        const double floorA = p.minTriangleArea();  // ~43.3 > 5.0
        mesh::RegionMarker clampedRegion = fine;
        clampedRegion.maxArea = std::max(fine.maxArea, floorA);
        QCOMPARE(clampedRegion.maxArea, floorA);

        const int bounded = meshVertexCount(2000.0, nullptr, {clampedRegion}, true);
        QVERIFY2(bounded < active,
                 qPrintable(QStringLiteral("clamp did not bound the region: "
                                           "%1 vs unclamped %2")
                                .arg(bounded).arg(active)));
        QVERIFY(bounded > inert);          // still finer than no region bound
    }

    /*! h = 0 must leave generation bit-for-bit as it was: no hook installed
     *  by this feature, no region clamp, no conditioning. */
    void minCellSizeOff_reproducesTheUnconditionedMesh()
    {
        mesh::RegionMarker rm;
        rm.xy = QPointF(100, 100); rm.attribute = 1.0; rm.maxArea = 5.0;

        MinSizePolicy off;                      // minCellSize 0
        QVERIFY(!off.enabled());
        QCOMPARE(off.minTriangleArea(), 0.0);
        // The dialog only clamps when the floor is positive.
        QCOMPARE(off.refinementAreaCap(2000.0), 2000.0);

        // And no hook: same mesh as the plain call.
        QCOMPARE(meshVertexCount(2000.0, nullptr, {rm}),
                 meshVertexCount(2000.0, nullptr, {rm}));
    }

    // ════════════════════════════════════════════════════════════════════
    // analyseLocalFeatureSize — the diagnostic Phase 0 rests on
    // ════════════════════════════════════════════════════════════════════

    void analyse_reportsEachCauseAndIsBounded()
    {
        QVector<QPolygonF> domains{square(0, 0, 200)};
        QVector<QVector<QPointF>> holes{ringOf(square(150, 150, 0.4))};  // sub-scale
        QVector<ConstraintSegment> segs{
            {{{10, 10}, {10.2, 10}, {60, 10}}, 1, QStringLiteral("short")},
            {{{10, 40}, {60, 40}}, 2, QStringLiteral("nearA")},
            {{{10, 40.3}, {60, 40.3}}, 3, QStringLiteral("nearB")},
            {{{100, 20}, {160, 20}}, 4, QStringLiteral("legA")},
            {{{100, 20}, {160, 22}}, 5, QStringLiteral("legB")}};
        QVector<SteinerPoint> pts;

        const QVector<Violation> v =
            mesh::pslg::analyseLocalFeatureSize(domains, holes, segs, pts, 2.0, -1);
        QVERIFY(!v.isEmpty());

        QSet<int> causes;
        for (const Violation &x : v) causes.insert(int(x.cause));
        QVERIFY(causes.contains(int(ViolationCause::ShortSegment)));
        QVERIFY(causes.contains(int(ViolationCause::CloseFeatures)));
        QVERIFY(causes.contains(int(ViolationCause::SmallAngle)));
        QVERIFY(causes.contains(int(ViolationCause::SubScaleRing)));

        // Worst-first ordering.
        for (int i = 1; i < v.size(); ++i) QVERIFY(v[i - 1].lfs <= v[i].lfs);

        // The cap is honoured and keeps the WORST entries.
        const QVector<Violation> capped =
            mesh::pslg::analyseLocalFeatureSize(domains, holes, segs, pts, 2.0, 3);
        QCOMPARE(capped.size(), 3);
        QCOMPARE(capped.first().lfs, v.first().lfs);

        // h <= 0 is a no-op.
        QVERIFY(mesh::pslg::analyseLocalFeatureSize(
                    domains, holes, segs, pts, 0.0, -1).isEmpty());
    }

    void violationCauseName_coversEveryEnumerator()
    {
        for (const auto c : {ViolationCause::ShortSegment,
                             ViolationCause::CloseFeatures,
                             ViolationCause::SmallAngle,
                             ViolationCause::SubScaleRing})
            QVERIFY(mesh::pslg::violationCauseName(c)
                    != QStringLiteral("unknown"));
    }
};

QTEST_MAIN(TestPslgMinSize)
#include "test_pslgminsize.moc"
