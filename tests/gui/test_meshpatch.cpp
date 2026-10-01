/*!
 * \file   test_meshpatch.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * G3 — QtTest coverage for structured quad patches
 * (workplans/TRI_QUAD_MESHING_PLAN_2026-09-06.md §3.2): transfinite and
 * swept generators, validation of folded input, and end-to-end stitching
 * into a Triangle domain through MeshGenerator::addPatch (links Triangle,
 * so the stitching assertions are integration smoke tests like
 * test_meshgenerator.cpp).
 */
#include <QtTest>
#include <QPolygonF>
#include <QSet>
#include <QVector>

#include <algorithm>
#include <cmath>
#include <limits>

#include "mesh/meshcellgeom.h"
#include "mesh/meshgenerator.h"
#include "mesh/meshpatch.h"
#include "mesh/meshedgekey.h"
#include "mesh/meshquadquality.h"
#include "mesh/meshquadregion.h"
#include "mesh/meshresult.h"

Q_DECLARE_METATYPE(mesh::SweptPatch)
Q_DECLARE_METATYPE(mesh::BankPairPatch)

using namespace mesh;

namespace {

QVector<MeshVertex> asVertices(const QVector<QPointF> &xy)
{
    QVector<MeshVertex> v;
    for (const QPointF &p : xy) { MeshVertex mv; mv.xy = p; v.append(mv); }
    return v;
}

bool onUnitSquareSide(const QPointF &a, const QPointF &b)
{
    auto same = [](double u, double v) { return qFuzzyCompare(u + 1.0, v + 1.0); };
    return (same(a.x(), 0.0) && same(b.x(), 0.0)) || (same(a.x(), 1.0) && same(b.x(), 1.0))
        || (same(a.y(), 0.0) && same(b.y(), 0.0)) || (same(a.y(), 1.0) && same(b.y(), 1.0));
}

} // namespace

class TestMeshPatch : public QObject
{
    Q_OBJECT

private slots:

    /*! Unit square, n=2 × m=3: (n+1)(m+1) vertices, n·m convex CCW quads,
     *  2(n+m) boundary segments all lying on the square's sides. */
    void transfinite_unitSquare()
    {
        StructuredPatch p;
        p.corners << QPointF(0, 0) << QPointF(1, 0) << QPointF(1, 1) << QPointF(0, 1);
        p.n = 2; p.m = 3; p.tag = QStringLiteral("chan");
        QString err;
        const PatchMesh pm = makeTransfinitePatch(p, &err);
        QVERIFY2(err.isEmpty(), qPrintable(err));
        QCOMPARE(pm.xy.size(), 12);
        QCOMPARE(pm.quads.size(), 6);
        QCOMPARE(pm.boundarySegments.size(), 10);
        QVERIFY(validate(pm).isEmpty());

        const QVector<MeshVertex> V = asVertices(pm.xy);
        double area = 0.0;
        for (const MeshTriangle &q : pm.quads)
        {
            QVERIFY(q.isQuad());
            QCOMPARE(q.tag, QStringLiteral("chan"));
            QVERIFY(cellIsConvex(V, q));
            QVERIFY(cellSignedArea(V, q) > 0.0);
            area += cellGeom(V, q).area;
        }
        QVERIFY(qFuzzyCompare(area, 1.0));
        for (const auto &s : pm.boundarySegments)
            QVERIFY(onUnitSquareSide(pm.xy[s.first], pm.xy[s.second]));
        // Corner (1,1) is vertex idx(n, m).
        QCOMPARE(pm.xy[3 * 4 - 1], QPointF(1, 1));
    }

    /*! Clockwise corners still give CCW quads. */
    void transfinite_clockwiseCorners_ccwQuads()
    {
        StructuredPatch p;
        p.corners << QPointF(0, 0) << QPointF(0, 1) << QPointF(1, 1) << QPointF(1, 0);
        p.n = 1; p.m = 1;
        const PatchMesh pm = makeTransfinitePatch(p);
        QCOMPARE(pm.quads.size(), 1);
        QVERIFY(cellSignedArea(asVertices(pm.xy), pm.quads[0]) > 0.0);
    }

    /*! A concave (dart) outline is rejected with a message. */
    void transfinite_concave_rejected()
    {
        StructuredPatch p;
        p.corners << QPointF(0, 0) << QPointF(2, 0) << QPointF(0.5, 0.5) << QPointF(0, 2);
        p.n = 2; p.m = 2;
        QVERIFY(!validate(p).isEmpty());
        QString err;
        const PatchMesh pm = makeTransfinitePatch(p, &err);
        QVERIFY(pm.quads.isEmpty());
        QVERIFY(!err.isEmpty());
    }

    /*! validate(PatchMesh) catches a folded quad handed in directly. */
    void patchMesh_folded_rejected()
    {
        PatchMesh pm;
        pm.xy << QPointF(0, 0) << QPointF(1, 0) << QPointF(0, 1) << QPointF(1, 1);  // bowtie order
        MeshTriangle q; q.v0 = 0; q.v1 = 1; q.v2 = 2; q.v3 = 3;
        pm.quads.append(q);
        QVERIFY(validate(pm).contains(QStringLiteral("folded")));
    }

    /*! L-shaped centreline, width 2, 2 quads across, 5 m along: 5 stations,
     *  8 convex CCW quads, a mitred outer corner at (11,-1). */
    void swept_lShape()
    {
        SweptPatch p;
        p.centreline << QPointF(0, 0) << QPointF(10, 0) << QPointF(10, 10);
        p.width = 2.0; p.across = 2; p.along = 5.0; p.tag = QStringLiteral("street");
        QString err;
        const PatchMesh pm = makeSweptPatch(p, &err);
        QVERIFY2(err.isEmpty(), qPrintable(err));
        QCOMPARE(pm.xy.size(), 5 * 3);
        QCOMPARE(pm.quads.size(), 4 * 2);
        QCOMPARE(pm.boundarySegments.size(), 2 * 4 + 2 * 2);
        QVERIFY(validate(pm).isEmpty());

        const QVector<MeshVertex> V = asVertices(pm.xy);
        for (const MeshTriangle &q : pm.quads)
        {
            QVERIFY(cellIsConvex(V, q));
            QVERIFY(cellSignedArea(V, q) > 0.0);
            QCOMPARE(q.tag, QStringLiteral("street"));
        }
        // Station 2 is the corner: right-hand (j=0) vertex is the outer mitre,
        // left-hand (j=2) the inner one.
        QVERIFY(qFuzzyCompare(pm.xy[2 * 3 + 0].x(), 11.0) && qFuzzyIsNull(pm.xy[2 * 3 + 0].y() + 1.0));
        QVERIFY(qFuzzyCompare(pm.xy[2 * 3 + 2].x(), 9.0)  && qFuzzyCompare(pm.xy[2 * 3 + 2].y(), 1.0));
        // First station straddles the start point across the width.
        QCOMPARE(pm.xy[0], QPointF(0, -1));
        QCOMPARE(pm.xy[2], QPointF(0, 1));
    }

    /*! along = 0 keeps one station per centreline vertex; bad inputs fail. */
    void swept_validation()
    {
        SweptPatch p;
        p.centreline << QPointF(0, 0) << QPointF(10, 0) << QPointF(10, 10);
        p.width = 2.0; p.across = 1; p.along = 0.0;
        const PatchMesh pm = makeSweptPatch(p);
        QCOMPARE(pm.xy.size(), 3 * 2);
        QCOMPARE(pm.quads.size(), 2);

        SweptPatch bad = p; bad.width = 0.0;
        QVERIFY(!validate(bad).isEmpty());
        bad = p; bad.across = 0;
        QVERIFY(!validate(bad).isEmpty());
        bad = p; bad.centreline.clear(); bad.centreline << QPointF(0, 0);
        QVERIFY(!validate(bad).isEmpty());
        // A hairpin folds the inner offset: rejected rather than emitted.
        bad = p; bad.centreline.clear();
        bad.centreline << QPointF(0, 0) << QPointF(10, 0) << QPointF(0, 0.001);
        bad.width = 4.0;
        QString err;
        QVERIFY(makeSweptPatch(bad, &err).quads.isEmpty());
        QVERIFY(!err.isEmpty());
    }

    void swept_globalOverlap_data()
    {
        QTest::addColumn<QVector<QPointF>>("line");
        QTest::newRow("crossing") << QVector<QPointF>{{-10,-10}, {10,10}, {-10,10}, {10,-10}};
        QTest::newRow("overlapping-return") << QVector<QPointF>{{0,0}, {20,0}, {20,20}, {0,20}, {0,1}, {15,1}};
        QTest::newRow("touching-return") << QVector<QPointF>{{0,0}, {20,0}, {20,20}, {0,20}, {0,2}, {15,2}};
    }

    void swept_globalOverlap()
    {
        QFETCH(QVector<QPointF>, line);
        SweptPatch p;
        p.centreline = line;
        p.width = 2.0; p.across = 2;
        QString error;
        const auto patch = makeSweptPatch(p, &error);
        QVERIFY2(patch.quads.isEmpty(), "Individually convex corridor cells must not overlap or touch distant stations.");
        QVERIFY2(error.contains("station", Qt::CaseInsensitive), qPrintable(error));
    }

    void swept_rectangles_data()
    {
        QTest::addColumn<int>("ratio");
        QTest::addColumn<bool>("translated");
        QTest::addColumn<bool>("reversed");
        for (int ratio : {1, 2, 4, 8})
            for (bool translated : {false, true})
                for (bool reversed : {false, true})
                    QTest::newRow(qPrintable(QStringLiteral("ratio%1-%2-%3").arg(ratio)
                        .arg(translated ? "UTM" : "origin").arg(reversed ? "reverse" : "forward")))
                        << ratio << translated << reversed;
    }

    void swept_rectangles()
    {
        QFETCH(int, ratio);
        QFETCH(bool, translated);
        QFETCH(bool, reversed);
        const QPointF origin = translated ? QPointF(500000, 4600000) : QPointF();
        const double acrossSpacing = 0.01;
        const double alongSpacing = ratio * acrossSpacing;
        SweptPatch p;
        p.centreline = {origin, origin + QPointF(8 * alongSpacing, 0)};
        if (reversed) std::reverse(p.centreline.begin(), p.centreline.end());
        p.width = 2 * acrossSpacing; p.across = 2; p.along = alongSpacing;
        QString error;
        const auto patch = makeSweptPatch(p, &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(patch.quads.size(), 16);
        QVERIFY2(validate(patch).isEmpty(), qPrintable(validate(patch)));
        for (const auto &quad : patch.quads) {
            const QPointF along = patch.xy[quad.v1] - patch.xy[quad.v0];
            const QPointF across = patch.xy[quad.v3] - patch.xy[quad.v0];
            QVERIFY(std::abs(std::hypot(along.x(), along.y()) / std::hypot(across.x(), across.y()) - ratio) < 1e-5);
            QVERIFY(along.x() * across.y() - along.y() * across.x() > 0);
        }
    }

    void swept_gentleTranslatedReverse()
    {
        SweptPatch p;
        p.centreline = {{500000,4600000}, {500020,4600000}, {500040,4600002}, {500060,4600006}};
        p.width = 2; p.across = 2; p.along = 4;
        for (int direction = 0; direction < 2; ++direction) {
            QString error;
            const auto patch = makeSweptPatch(p, &error);
            QVERIFY2(!patch.quads.isEmpty(), qPrintable(error));
            QVERIFY2(validate(patch).isEmpty(), qPrintable(validate(patch)));
            std::reverse(p.centreline.begin(), p.centreline.end());
        }
    }

    void swept_representableHighOriginStations()
    {
        // QPointF equality uses a coordinate-relative fuzzy comparison.
        // These distinct, representable coordinates must not become repeated
        // stations merely because the origin is large.
        for (double spacing : {0.0, 0.0001}) {
            SweptPatch p;
            const QPointF origin(1e9, 1e9);
            p.centreline = {origin, origin + QPointF(spacing == 0 ? 0.0008 : 0.004, 0)};
            p.width = 0.0002; p.across = 2; p.along = spacing;
            QString error;
            QVERIFY2(validate(p).isEmpty(), qPrintable(validate(p)));
            const auto patch = makeSweptPatch(p, &error);
            QVERIFY2(!patch.quads.isEmpty(), qPrintable(error));
            QVERIFY2(validate(patch).isEmpty(), qPrintable(validate(patch)));
        }
    }

    void swept_unsafeCounts_data()
    {
        QTest::addColumn<SweptPatch>("patch");
        SweptPatch p;
        p.centreline = {{0,0}, {10,0}}; p.width = 2; p.across = 1;
        p.along = std::numeric_limits<double>::denorm_min();
        QTest::newRow("nonfinite-station-count") << p;
        p.along = 10.0 / double(std::numeric_limits<int>::max());
        QTest::newRow("vertex-index-overflow") << p;
        p.along = 0; p.across = std::numeric_limits<int>::max();
        QTest::newRow("across-index-overflow") << p;
        p.across = 1;
        p.centreline = {{-std::numeric_limits<double>::max(),0}, {std::numeric_limits<double>::max(),0}};
        QTest::newRow("nonfinite-segment-length") << p;
    }

    void swept_unsafeCounts()
    {
        QFETCH(SweptPatch, patch);
        // Validate first: the old unchecked cast/reserve path must never be
        // exercised with an enormous request just to establish the baseline.
        const QString error = validate(patch);
        QVERIFY2(!error.isEmpty(), "Unsafe station/index counts must fail before allocation.");
    }

    void transfinite_unsafeCounts_data()
    {
        QTest::addColumn<int>("n"); QTest::addColumn<int>("m");
        QTest::newRow("n-addition-overflow") << std::numeric_limits<int>::max() << 1;
        QTest::newRow("m-addition-overflow") << 1 << std::numeric_limits<int>::max();
        QTest::newRow("grid-product-overflow") << 50000 << 50000;
    }

    void transfinite_unsafeCounts()
    {
        QFETCH(int, n); QFETCH(int, m);
        StructuredPatch p;
        p.corners = {{0,0}, {10,0}, {10,10}, {0,10}}; p.n = n; p.m = m;
        // Establish the old validation failure without entering its huge or
        // overflowing allocation path. Once the guard passes, check both APIs.
        QVERIFY2(!validate(p).isEmpty(), "Unsafe transfinite index products must fail validation.");
        QString error;
        QVERIFY(makeTransfinitePatch(p, &error).xy.isEmpty());
        QVERIFY(!error.isEmpty());
        QVector<QVector<QPointF>> sides;
        for (int i = 0; i < 4; ++i) sides.append({p.corners[i], p.corners[(i + 1) % 4]});
        error.clear();
        QVERIFY(makeTransfinitePatch(sides, n, m, {}, &error).xy.isEmpty());
        QVERIFY(!error.isEmpty());
    }

    void patchMesh_boundaryIntegrity()
    {
        StructuredPatch p;
        p.corners = {{0,0}, {2,0}, {2,2}, {0,2}}; p.n = 2; p.m = 2;
        const auto good = makeTransfinitePatch(p);
        QVERIFY(validate(good).isEmpty());
        auto bad = good;
        bad.boundarySegments.removeLast();
        QVERIFY(!validate(bad).isEmpty());
        bad = good; bad.boundarySegments.append(bad.boundarySegments.first());
        QVERIFY(!validate(bad).isEmpty());
        bad = good; bad.boundarySegments[0].first = bad.xy.size();
        QVERIFY(!validate(bad).isEmpty());
        bad = good; bad.boundarySegments.append({1,4}); // an interior edge
        QVERIFY(!validate(bad).isEmpty());
        bad = good; bad.xy[0].setX(std::numeric_limits<double>::infinity());
        QVERIFY(!validate(bad).isEmpty());
        QVERIFY(!validate(PatchMesh{}).isEmpty());
    }

    /*! QUAD_MESHING_REDESIGN_PLAN §4.2 — polyline-sided transfinite patch on
     *  a curved four-sided region: bottom side an arc of 9 points, the other
     *  three straight. (n+1)(m+1) vertices, n·m convex CCW quads, every
     *  boundary vertex lies on an input side, corners exact. */
    void transfinite_polylineSides_curved()
    {
        // Corners: c0 (0,0), c1 (10,0), c2 (10,6), c3 (0,6). Bottom bows down
        // to y = -1.5 at mid-span.
        QVector<QPointF> bottom;
        for (int i = 0; i <= 8; ++i)
        {
            const double u = i / 8.0;
            bottom << QPointF(10.0 * u, -1.5 * std::sin(M_PI * u));
        }
        const QVector<QPointF> right{QPointF(10, 0), QPointF(10, 6)};
        const QVector<QPointF> top{QPointF(10, 6), QPointF(0, 6)};
        const QVector<QPointF> left{QPointF(0, 6), QPointF(0, 0)};
        const QVector<QVector<QPointF>> sides{bottom, right, top, left};
        const int n = 8, m = 4;
        QString err;
        const PatchMesh pm = makeTransfinitePatch(sides, n, m, QStringLiteral("arc"), &err);
        QVERIFY2(err.isEmpty(), qPrintable(err));
        QCOMPARE(pm.xy.size(), (n + 1) * (m + 1));
        QCOMPARE(pm.quads.size(), n * m);
        QCOMPARE(pm.boundarySegments.size(), 2 * (n + m));
        QVERIFY(validate(pm).isEmpty());

        const QVector<MeshVertex> V = asVertices(pm.xy);
        for (const MeshTriangle &q : pm.quads)
        {
            QVERIFY(q.isQuad());
            QCOMPARE(q.tag, QStringLiteral("arc"));
            QVERIFY(cellIsConvex(V, q));
            QVERIFY(cellSignedArea(V, q) > 0.0);
        }
        // Boundary vertices lie on the input polylines.
        auto onPolyline = [](const QVector<QPointF> &pl, const QPointF &p) {
            for (int i = 0; i + 1 < pl.size(); ++i)
            {
                const QPointF a = pl[i], b = pl[i + 1], ab = b - a, ap = p - a;
                const double l2 = ab.x() * ab.x() + ab.y() * ab.y();
                if (l2 <= 0.0) continue;
                const double t = std::clamp((ap.x() * ab.x() + ap.y() * ab.y()) / l2, 0.0, 1.0);
                const QPointF q = a + ab * t;
                if (std::hypot(p.x() - q.x(), p.y() - q.y()) < 1e-9) return true;
            }
            return false;
        };
        int onBoundary = 0;
        for (const auto &s : pm.boundarySegments)
            for (int idx : {s.first, s.second})
            {
                bool on = false;
                for (const auto &side : sides) on = on || onPolyline(side, pm.xy[idx]);
                QVERIFY2(on, qPrintable(QStringLiteral("(%1,%2)").arg(pm.xy[idx].x()).arg(pm.xy[idx].y())));
                ++onBoundary;
            }
        QCOMPARE(onBoundary, 2 * pm.boundarySegments.size());
        for (const QPointF &c : {QPointF(0, 0), QPointF(10, 0), QPointF(10, 6), QPointF(0, 6)})
            QVERIFY(pm.xy.contains(c));
        // The bottom row follows the arc: its interior vertices dip below y = 0.
        int dipped = 0;
        for (const QPointF &p : pm.xy) if (p.y() < -1e-9) ++dipped;
        QCOMPARE(dipped, n - 1);

        // Invalid input: mismatched side endpoints, short side, n < 1.
        QVector<QVector<QPointF>> bad = sides;
        bad[1] = QVector<QPointF>{QPointF(10, 0.5), QPointF(10, 6)};
        QVERIFY(makeTransfinitePatch(bad, n, m, QString(), &err).quads.isEmpty());
        QVERIFY(!err.isEmpty());
        bad = sides; bad[2] = QVector<QPointF>{QPointF(10, 6)};
        QVERIFY(makeTransfinitePatch(bad, n, m, QString(), &err).quads.isEmpty());
        QVERIFY(makeTransfinitePatch(sides, 0, m, QString(), &err).quads.isEmpty());
    }

    /*! QUAD_MESHING_REDESIGN_PLAN §4.2 — makeMappedPatch on a 20×10 rectangle
     *  ring resampled at h = 2 with the four corners: n = 10, m = 5 → 50 unit
     *  squares of 2×2, every SJ 1, boundary vertices = the ring vertices. */
    void mappedPatch_rectangleRing()
    {
        const QPolygonF rect{QPointF(0, 0), QPointF(20, 0), QPointF(20, 10), QPointF(0, 10)};
        const QPolygonF ring = resampleRing(rect, 2.0);
        QCOMPARE(ring.size(), 30);
        QVector<int> corners;
        for (const QPointF &c : rect) corners << ring.indexOf(c);
        std::sort(corners.begin(), corners.end());
        QCOMPARE(corners, QVector<int>({0, 10, 15, 25}));

        QString err;
        const PatchMesh pm = makeMappedPatch(ring, corners, 2.0, QStringLiteral("map"), &err);
        QVERIFY2(err.isEmpty(), qPrintable(err));
        QCOMPARE(pm.quads.size(), 50);
        QCOMPARE(pm.xy.size(), 11 * 6);
        QCOMPARE(pm.boundarySegments.size(), 30);
        QVERIFY(validate(pm).isEmpty());
        const QVector<MeshVertex> V = asVertices(pm.xy);
        for (const MeshTriangle &q : pm.quads)
        {
            QCOMPARE(q.tag, QStringLiteral("map"));
            QVERIFY(cellSignedArea(V, q) > 0.0);
            QVERIFY(quadQuality(V, q).scaledJacobian > 1.0 - 1e-9);
            QVERIFY(std::abs(cellGeom(V, q).area - 4.0) < 1e-9);
        }
        // Every ring vertex is a patch boundary vertex (exact coordinates).
        QSet<int> bverts;
        for (const auto &s : pm.boundarySegments) { bverts.insert(s.first); bverts.insert(s.second); }
        QCOMPARE(bverts.size(), 30);
        for (const QPointF &r : ring)
        {
            bool found = false;
            for (int idx : bverts)
                if (std::hypot(pm.xy[idx].x() - r.x(), pm.xy[idx].y() - r.y()) < 1e-9) { found = true; break; }
            QVERIFY(found);
        }
        // Bad corners: wrong count, out of range, not strictly increasing.
        QVERIFY(makeMappedPatch(ring, {0, 10, 15}, 2.0, QString(), &err).quads.isEmpty());
        QVERIFY(!err.isEmpty());
        QVERIFY(makeMappedPatch(ring, {0, 10, 15, 40}, 2.0, QString(), &err).quads.isEmpty());
        QVERIFY(makeMappedPatch(ring, {0, 15, 10, 25}, 2.0, QString(), &err).quads.isEmpty());
        QVERIFY(makeMappedPatch(ring, corners, 0.0, QString(), &err).quads.isEmpty());
    }

    void mappedPatch_directionalRectangle_data()
    {
        QTest::addColumn<double>("angle");
        QTest::addColumn<int>("start");
        QTest::addColumn<bool>("reverse");
        QTest::addColumn<bool>("translated");
        for (double angle : {0.0,31.0,45.0,90.0})
            for (int start=0;start<4;++start)
                for (bool reverse : {false,true})
                    for (bool translated : {false,true}) {
                        const QByteArray name=QString("angle%1-start%2-reverse%3-translated%4")
                            .arg(angle).arg(start).arg(reverse).arg(translated).toLatin1();
                        QTest::newRow(name.constData())<<angle<<start<<reverse<<translated;
                    }
    }

    void mappedPatch_directionalRectangle()
    {
        QFETCH(double,angle);QFETCH(int,start);QFETCH(bool,reverse);QFETCH(bool,translated);
        const double radians=angle*std::acos(-1.0)/180;
        const QPointF along(std::cos(radians),std::sin(radians));
        const QPointF across(-along.y(),along.x());
        const QPointF origin=translated?QPointF(500000,4500000):QPointF();
        const QPolygonF base{origin,origin+80*along,origin+80*along+8*across,origin+8*across};
        QPolygonF ring;
        for(int k=0;k<4;++k)ring.append(base[(start+(reverse?-k:k)+4)%4]);
        QString error="stale";
        const auto patch=makeMappedPatch(ring,{0,1,2,3},8.0,2.0,angle,"corridor",&error);
        QVERIFY2(error.isEmpty(),qPrintable(error));
        QCOMPARE(patch.quads.size(),40);QCOMPARE(patch.xy.size(),55);
        QVERIFY2(validate(patch).isEmpty(),qPrintable(validate(patch)));
        for(const auto &quad:patch.quads) {
            const int ids[]{quad.v0,quad.v1,quad.v2,quad.v3};
            int alongEdges=0,acrossEdges=0;
            for(int k=0;k<4;++k) {
                const QPointF delta=patch.xy[ids[(k+1)%4]]-patch.xy[ids[k]];
                const double a=std::abs(delta.x()*along.x()+delta.y()*along.y());
                const double b=std::abs(delta.x()*across.x()+delta.y()*across.y());
                if(a>b) {QVERIFY(std::abs(a-8)<1e-7);QVERIFY(b<1e-7);++alongEdges;}
                else {QVERIFY(std::abs(b-2)<1e-7);QVERIFY(a<1e-7);++acrossEdges;}
            }
            QCOMPARE(alongEdges,2);QCOMPARE(acrossEdges,2);
        }
    }

    void mappedPatch_directionalDenseRingAndUniformParity()
    {
        // Start in the middle of the long side rather than at a logical corner.
        const QPolygonF ring{{40,0},{80,0},{80,4},{80,8},{40,8},{0,8},{0,4},{0,0}};
        const QVector<int> corners{1,3,5,7};
        QString error;
        const auto directional=makeMappedPatch(ring,corners,8,2,180,"road",&error);
        QVERIFY2(error.isEmpty(),qPrintable(error));QCOMPARE(directional.quads.size(),40);
        const auto scalar=makeMappedPatch(ring,corners,2,"road",&error);
        QVERIFY2(error.isEmpty(),qPrintable(error));
        const auto uniform=makeMappedPatch(ring,corners,2,2,0,"road",&error);
        QVERIFY2(error.isEmpty(),qPrintable(error));QCOMPARE(uniform.xy,scalar.xy);
        QCOMPARE(uniform.quads.size(),scalar.quads.size());
        QCOMPARE(uniform.boundarySegments,scalar.boundarySegments);
    }

    void mappedPatch_ambiguousAxis()
    {
        const QPolygonF ring{{0,0},{10,0},{10,10},{0,10}};
        QString error;
        QVERIFY(makeMappedPatch(ring,{0,1,2,3},4,1,45,"",&error).quads.isEmpty());
        QVERIFY2(error.contains("ambiguous",Qt::CaseInsensitive),qPrintable(error));
    }

    void mappedPatch_invalidDirectionalInputs_data()
    {
        QTest::addColumn<double>("along");QTest::addColumn<double>("across");QTest::addColumn<double>("angle");
        QTest::addColumn<QString>("diagnostic");
        const double nan=std::numeric_limits<double>::quiet_NaN();
        const double inf=std::numeric_limits<double>::infinity();
        QTest::newRow("zero-along")<<0.0<<2.0<<0.0<<QString("spacing");
        QTest::newRow("negative-across")<<8.0<<-1.0<<0.0<<QString("spacing");
        QTest::newRow("nan-along")<<nan<<2.0<<0.0<<QString("spacing");
        QTest::newRow("infinite-across")<<8.0<<inf<<0.0<<QString("spacing");
        QTest::newRow("nan-angle")<<8.0<<2.0<<nan<<QString("angle");
        QTest::newRow("infinite-angle")<<8.0<<2.0<<inf<<QString("angle");
        QTest::newRow("ratio-overflow")<<std::numeric_limits<double>::denorm_min()<<2.0<<0.0<<QString("capacity");
        QTest::newRow("index-product-overflow")<<0.0001<<0.0001<<0.0<<QString("capacity");
    }

    void mappedPatch_invalidDirectionalInputs()
    {
        QFETCH(double,along);QFETCH(double,across);QFETCH(double,angle);QFETCH(QString,diagnostic);
        const QPolygonF ring{{0,0},{80,0},{80,8},{0,8}};
        QString error;
        QVERIFY(makeMappedPatch(ring,{0,1,2,3},along,across,angle,"",&error).quads.isEmpty());
        QVERIFY2(error.contains(diagnostic,Qt::CaseInsensitive),qPrintable(error));
        // Do not exercise the old unsafe scalar cast until the shared directional
        // preflight above is present; the baseline stub fails the assertion first.
        if(diagnostic=="capacity") {
            QVERIFY(makeMappedPatch(ring,{0,1,2,3},along,"",&error).quads.isEmpty());
            QVERIFY2(error.contains("capacity",Qt::CaseInsensitive),qPrintable(error));
        }
    }

    void bankPair_rectangles_data()
    {
        QTest::addColumn<double>("angle"); QTest::addColumn<double>("origin");
        QTest::addColumn<int>("directions"); QTest::addColumn<bool>("swapBanks");
        for (double angle : {0.0,31.0})
            for (double origin : {0.0,1e6,1e9})
                for(int directions=0;directions<4;++directions)
                    for(bool swapBanks:{false,true}) {
                        const QByteArray label=QString("angle%1-origin%2-directions%3-swap%4")
                            .arg(angle).arg(origin).arg(directions).arg(swapBanks).toLatin1();
                        QTest::newRow(label.constData())<<angle<<origin<<directions<<swapBanks;
                    }
    }

    void bankPair_rectangles()
    {
        QFETCH(double,angle);QFETCH(double,origin);QFETCH(int,directions);QFETCH(bool,swapBanks);
        const double radians=angle*std::acos(-1.0)/180;
        const QPointF along(std::cos(radians),std::sin(radians)), across(-along.y(),along.x()), offset(origin,origin);
        BankPairPatch p;
        p.bankA={offset,offset+80*along};p.bankB={offset+8*across,offset+80*along+8*across};
        p.across=4;p.along=8;p.tag="banks";
        if(directions&1)std::reverse(p.bankA.begin(),p.bankA.end());
        if(directions&2)std::reverse(p.bankB.begin(),p.bankB.end());
        if(swapBanks)std::swap(p.bankA,p.bankB);
        QString error="stale";const auto patch=makeBankPairPatch(p,&error);
        QVERIFY2(error.isEmpty(),qPrintable(error));QCOMPARE(patch.quads.size(),40);QCOMPARE(patch.xy.size(),55);
        QVERIFY2(validate(patch).isEmpty(),qPrintable(validate(patch)));
        QCOMPARE(patch.tag,p.tag);
        for(const auto &q:patch.quads) {
            QCOMPARE(q.tag,p.tag);const int ids[]{q.v0,q.v1,q.v2,q.v3};
            int longitudinal=0,transverse=0;
            for(int k=0;k<4;++k) {
                const auto d=patch.xy[ids[(k+1)%4]]-patch.xy[ids[k]];
                const double a=std::abs(QPointF::dotProduct(d,along)),b=std::abs(QPointF::dotProduct(d,across));
                if(a>b){QVERIFY(std::abs(a-8)<2e-6);QVERIFY(b<2e-6);++longitudinal;}
                else{QVERIFY(std::abs(b-2)<2e-6);QVERIFY(a<2e-6);++transverse;}
            }
            QCOMPARE(longitudinal,2);QCOMPARE(transverse,2);
        }
    }

    void bankPair_preservesUnequalBankVertices()
    {
        BankPairPatch p;
        p.bankA={{0,0},{25,0},{100,0}};
        p.bankB={{0,10},{50,10},{75,10},{100,10}};p.across=2;
        QString error;const auto patch=makeBankPairPatch(p,&error);
        QVERIFY2(error.isEmpty(),qPrintable(error));QCOMPARE(patch.quads.size(),8);QCOMPARE(patch.xy.size(),15);
        QSet<int> boundary;for(const auto &edge:patch.boundarySegments){boundary.insert(edge.first);boundary.insert(edge.second);}
        for(const auto &bank:{p.bankA,p.bankB})for(const auto &point:bank){
            bool exact=false;for(int index:boundary)exact|=patch.xy[index].x()==point.x()&&patch.xy[index].y()==point.y();
            QVERIFY2(exact,"Every original bank vertex must occur exactly on the patch boundary.");
        }
        p.along=20;const auto refined=makeBankPairPatch(p,&error);QVERIFY2(error.isEmpty(),qPrintable(error));
        QCOMPARE(refined.quads.size(),16);
    }

    void bankPair_asymmetricGentleCurves()
    {
        BankPairPatch p;
        p.bankA={{0,0},{20,1},{45,4},{80,8}};
        p.bankB={{0,7},{12,8},{40,14},{60,17},{80,20}};
        p.across=3;p.along=9;p.tag="asymmetric";
        for(double origin:{0.0,1e6,1e9}){
            auto translated=p;for(auto *bank:{&translated.bankA,&translated.bankB})for(auto &point:*bank)point+=QPointF(origin,origin);
            QString error;const auto patch=makeBankPairPatch(translated,&error);
            QVERIFY2(error.isEmpty(),qPrintable(error));QVERIFY(!patch.quads.isEmpty());
            QVERIFY2(validate(patch).isEmpty(),qPrintable(validate(patch)));
            QSet<int> boundary;for(const auto &edge:patch.boundarySegments){boundary.insert(edge.first);boundary.insert(edge.second);}
            for(const auto &bank:{translated.bankA,translated.bankB})for(const auto &point:bank){
                bool exact=false;for(int index:boundary)exact|=patch.xy[index].x()==point.x()&&patch.xy[index].y()==point.y();
                QVERIFY2(exact,"A bank bend was omitted or rounded away.");
            }
        }
    }

    void bankPair_nearCoincidentStationsAndCanonicalOrientation()
    {
        BankPairPatch p;p.across=2;
        p.bankA={{1e9,1e9},{1e9+50,1e9},{1e9+100,1e9}};
        p.bankB={{1e9,1e9+10},{1e9+50.000001,1e9+10},{1e9+100,1e9+10}};
        QString error;const auto expected=makeBankPairPatch(p,&error);
        QVERIFY2(error.isEmpty(),qPrintable(error));QCOMPARE(expected.quads.size(),6);
        for(const auto &bank:{p.bankA,p.bankB})for(const auto &point:bank){
            bool exact=false;for(const auto &generated:expected.xy)exact|=generated.x()==point.x()&&generated.y()==point.y();
            QVERIFY2(exact,"Near-coincident normalized stations must preserve both original bank vertices.");
        }
        for(int reversals=0;reversals<4;++reversals)for(bool swapBanks:{false,true}){
            auto changed=p;
            if(reversals&1)std::reverse(changed.bankA.begin(),changed.bankA.end());
            if(reversals&2)std::reverse(changed.bankB.begin(),changed.bankB.end());
            if(swapBanks)std::swap(changed.bankA,changed.bankB);
            const auto actual=makeBankPairPatch(changed,&error);QVERIFY2(error.isEmpty(),qPrintable(error));
            QCOMPARE(actual.xy.size(),expected.xy.size());
            for(int i=0;i<actual.xy.size();++i){QCOMPARE(actual.xy[i].x(),expected.xy[i].x());QCOMPARE(actual.xy[i].y(),expected.xy[i].y());}
            QCOMPARE(actual.boundarySegments,expected.boundarySegments);
        }
    }

    void bankPair_invalid_data()
    {
        QTest::addColumn<BankPairPatch>("patch");QTest::addColumn<QString>("diagnostic");
        BankPairPatch base;base.bankA={{0,0},{20,0}};base.bankB={{0,4},{20,4}};base.across=2;
        auto add=[&](const char *name,const BankPairPatch &p,const char *diagnostic){QTest::newRow(name)<<p<<QString::fromLatin1(diagnostic);};
        auto p=base;p.bankB={{10,-10},{10,10}};add("ambiguous",p,"ambiguous");
        p=base;p.bankB={{0,4},{10,-4},{20,4}};add("crossing",p,"bank");
        p=base;p.bankB={{0,4},{10,0},{20,4}};add("touching",p,"bank");
        p=base;p.bankB=p.bankA;add("overlapping",p,"bank");
        p=base;p.bankA={{0,0},{10,0},{10,0},{20,0}};add("duplicate",p,"duplicate");
        p=base;p.bankA={{0,0},{10,0},{0,0}};add("closed",p,"closed");
        p=base;p.bankA={{0,0},{12,3},{2,3},{14,0},{20,0}};add("self-crossing",p,"bank");
        p=base;p.bankA[1].setX(std::numeric_limits<double>::quiet_NaN());add("nonfinite",p,"finite");
        p=base;p.bankA.resize(1);add("one-vertex",p,"two");
        p=base;p.along=-1;add("negative-spacing",p,"spacing");
        p=base;p.along=std::numeric_limits<double>::infinity();add("nonfinite-spacing",p,"spacing");
        p=base;p.along=std::numeric_limits<double>::denorm_min();add("tiny-spacing",p,"capacity");
        p=base;p.across=std::numeric_limits<int>::max();add("huge-across",p,"capacity");
        p=base;p.across=0;add("zero-across",p,"across");
    }

    void bankPair_invalid()
    {
        QFETCH(BankPairPatch,patch);QFETCH(QString,diagnostic);QString error;
        const auto result=makeBankPairPatch(patch,&error);
        QVERIFY(result.xy.isEmpty());QVERIFY(result.quads.isEmpty());
        QVERIFY2(error.contains(diagnostic,Qt::CaseInsensitive),qPrintable(error));
    }

    /*! End-to-end: a 100×100 domain with a 20×20 transfinite patch hole.
     *  Triangles first, then the 4 patch quads; the patch boundary vertices
     *  are shared with the triangulation (no duplicates); no triangle lies
     *  inside the patch. */
    void generator_patchStitched()
    {
        MeshGenerator g;
        // Give the outline a vertex every 10 units, roughly the cell size.
        QPolygonF dom;
        for (int i = 0; i < 10; ++i) dom << QPointF(10 * i, 0);
        for (int i = 0; i < 10; ++i) dom << QPointF(100, 10 * i);
        for (int i = 0; i < 10; ++i) dom << QPointF(100 - 10 * i, 100);
        for (int i = 0; i < 10; ++i) dom << QPointF(0, 100 - 10 * i);
        g.setDomain(dom);

        StructuredPatch p;
        p.corners << QPointF(40, 40) << QPointF(60, 40) << QPointF(60, 60) << QPointF(40, 60);
        p.n = 2; p.m = 2; p.tag = QStringLiteral("patch");
        QString err;
        const PatchMesh pm = makeTransfinitePatch(p, &err);
        QVERIFY2(err.isEmpty(), qPrintable(err));
        g.addPatch(pm);

        GenerationOptions o;
        o.maxArea = 200.0;
        o.trianglesOnly = true;   // so the patch supplies the only quads
        g.setOptions(o);
        const MeshResult r = g.generate();
        QVERIFY2(r.ok, qPrintable(r.errorMsg));
        QCOMPARE(r.quadCount(), 4);
        QVERIFY(r.triangles.size() > 4);

        // Engine order: once a quad appears, no triangle follows.
        bool seenQuad = false;
        for (const MeshTriangle &c : r.triangles)
        {
            if (c.isQuad()) seenQuad = true;
            else QVERIFY(!seenQuad);
        }

        // Every patch boundary vertex exists exactly once and is referenced
        // by both a triangle and a quad; the centre vertex (50,50) is
        // referenced by quads only.
        auto countAt = [&](const QPointF &xy) {
            int n = 0;
            for (const MeshVertex &v : r.vertices) if (v.xy == xy) ++n;
            return n;
        };
        auto indexAt = [&](const QPointF &xy) {
            for (int i = 0; i < r.vertices.size(); ++i) if (r.vertices[i].xy == xy) return i;
            return -1;
        };
        const QVector<QPointF> ring = {
            {40, 40}, {50, 40}, {60, 40}, {60, 50}, {60, 60}, {50, 60}, {40, 60}, {40, 50}};
        for (const QPointF &b : ring)
        {
            QCOMPARE(countAt(b), 1);
            const int vi = indexAt(b);
            bool inTri = false, inQuad = false;
            for (const MeshTriangle &c : r.triangles)
                if (c.hasVertex(vi)) (c.isQuad() ? inQuad : inTri) = true;
            QVERIFY2(inTri && inQuad, qPrintable(QStringLiteral("(%1,%2)").arg(b.x()).arg(b.y())));
        }
        QCOMPARE(countAt(QPointF(50, 50)), 1);
        {
            const int vi = indexAt(QPointF(50, 50));
            for (const MeshTriangle &c : r.triangles)
                if (!c.isQuad()) QVERIFY(!c.hasVertex(vi));
        }

        // The hole was carved: no triangle centroid inside the patch, and
        // every quad is convex with tag "patch" and valid indices.
        for (const MeshTriangle &c : r.triangles)
        {
            const CellGeom cg = cellGeom(r.vertices, c);
            if (c.isQuad())
            {
                QVERIFY(c.v0 >= 0 && c.v1 >= 0 && c.v2 >= 0 && c.v3 >= 0);
                QVERIFY(c.v3 < r.vertices.size());
                QVERIFY(cellIsConvex(r.vertices, c));
                QCOMPARE(c.tag, QStringLiteral("patch"));
                QVERIFY(qFuzzyCompare(cg.area, 100.0));
            }
            else
            {
                QVERIFY(!(cg.centroid.x() > 40 && cg.centroid.x() < 60
                          && cg.centroid.y() > 40 && cg.centroid.y() < 60));
            }
        }

        // The patch boundary segments survived as constrained boundary edges.
        QSet<QPair<int, int>> bset;
        for (const MeshEdge &e : r.boundaryEdges) bset.insert(edgeKey(e.v0, e.v1));
        for (int k = 0; k < ring.size(); ++k)
            QVERIFY(bset.contains(edgeKey(indexAt(ring[k]), indexAt(ring[(k + 1) % ring.size()]))));
    }
};

QTEST_MAIN(TestMeshPatch)
#include "test_meshpatch.moc"
