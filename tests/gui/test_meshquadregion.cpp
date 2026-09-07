/*!
 * \file   test_meshquadregion.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * Quad redesign gate 2 (workplans/QUAD_MESHING_REDESIGN_PLAN_2026-09-06.md
 * §3, §4.1, §7.2) — QtTest coverage for the PSLG quad-region geometry
 * helpers in mesh/meshquadregion.h: ring normalisation, turning angles,
 * auto-classification (Mapped / Submapped / Free), single-region validation
 * against domains and holes, pairwise overlap checks, ring resampling and
 * point-in-ring.
 */
#include <QtTest>
#include <QPolygonF>
#include <QVector>

#include <cmath>

#include "mesh/meshquadregion.h"

using namespace mesh;

namespace {

QPolygonF rect(double x0, double y0, double w, double h)
{
    return QPolygonF{QPointF(x0, y0), QPointF(x0 + w, y0), QPointF(x0 + w, y0 + h), QPointF(x0, y0 + h)};
}

/*! 10×10 L with a 5×5 notch removed at the top-right, CCW. */
QPolygonF lShape()
{
    return QPolygonF{QPointF(0, 0), QPointF(10, 0), QPointF(10, 5), QPointF(5, 5), QPointF(5, 10), QPointF(0, 10)};
}

QPolygonF regularPolygon(int n, double r, const QPointF &c = QPointF(0, 0))
{
    QPolygonF p;
    for (int i = 0; i < n; ++i)
    {
        const double a = 2.0 * M_PI * i / n;
        p << QPointF(c.x() + r * std::cos(a), c.y() + r * std::sin(a));
    }
    return p;
}

bool near(double a, double b, double tol) { return std::abs(a - b) <= tol; }

/*! True when \p p lies on some edge of the closed ring within \p tol. */
bool onRingBoundary(const QPolygonF &ring, const QPointF &p, double tol)
{
    const int n = ring.size();
    for (int i = 0; i < n; ++i)
    {
        const QPointF a = ring[i], b = ring[(i + 1) % n];
        const QPointF ab = b - a, ap = p - a;
        const double l2 = ab.x() * ab.x() + ab.y() * ab.y();
        if (l2 <= 0.0) continue;
        const double t = std::clamp((ap.x() * ab.x() + ap.y() * ab.y()) / l2, 0.0, 1.0);
        const QPointF q = a + ab * t;
        if (std::hypot(p.x() - q.x(), p.y() - q.y()) <= tol) return true;
    }
    return false;
}

} // namespace

class TestMeshQuadRegion : public QObject
{
    Q_OBJECT

private slots:

    /*! CW input becomes CCW; a closing duplicate and consecutive duplicates
     *  are dropped; a CCW ring is returned as-is. */
    void normalizeRingCCW_orientationAndDuplicates()
    {
        QPolygonF cw{QPointF(0, 0), QPointF(0, 5), QPointF(10, 5), QPointF(10, 0), QPointF(0, 0)};
        const QPolygonF n = normalizeRingCCW(cw);
        QCOMPARE(n.size(), 4);
        QVERIFY(ringSignedArea(n) > 0.0);
        QVERIFY(near(ringSignedArea(n), 50.0, 1e-9));
        QVERIFY(ringSignedArea(cw) < 0.0);

        QPolygonF dup{QPointF(0, 0), QPointF(10, 0), QPointF(10, 0), QPointF(10, 5), QPointF(0, 5), QPointF(0, 5)};
        const QPolygonF d = normalizeRingCCW(dup);
        QCOMPARE(d.size(), 4);
        QVERIFY(ringSignedArea(d) > 0.0);

        const QPolygonF ccw = rect(0, 0, 10, 5);
        QCOMPARE(normalizeRingCCW(ccw), ccw);
        QVERIFY(normalizeRingCCW(QPolygonF()).isEmpty());
    }

    /*! Exterior turning angle: 90° at every corner of a CCW square, −90° at
     *  the reflex corner of the L, 0 at a collinear vertex. */
    void ringTurnDeg_square()
    {
        const QPolygonF sq = rect(0, 0, 10, 10);
        for (int i = 0; i < 4; ++i) QVERIFY(near(ringTurnDeg(sq, i), 90.0, 1e-9));
        const QPolygonF L = lShape();
        int reflex = 0;
        for (int i = 0; i < L.size(); ++i)
        {
            const double t = ringTurnDeg(L, i);
            QVERIFY(near(std::abs(t), 90.0, 1e-9));
            if (t < 0.0) { ++reflex; QCOMPARE(i, 3); }
        }
        QCOMPARE(reflex, 1);
        QPolygonF withMid{QPointF(0, 0), QPointF(5, 0), QPointF(10, 0), QPointF(10, 5), QPointF(0, 5)};
        QVERIFY(near(ringTurnDeg(withMid, 1), 0.0, 1e-9));
    }

    /*! Square → Mapped with its four corners in ring order. */
    void classify_square_mapped()
    {
        QVector<int> corners;
        QCOMPARE(classifyQuadRegion(rect(0, 0, 10, 10), &corners), QuadRegionMode::Mapped);
        QCOMPARE(corners, QVector<int>({0, 1, 2, 3}));
        // CW input classifies the same (normalised inside).
        QPolygonF cw{QPointF(0, 0), QPointF(0, 5), QPointF(10, 5), QPointF(10, 0)};
        QCOMPARE(classifyQuadRegion(cw, &corners), QuadRegionMode::Mapped);
        QCOMPARE(corners.size(), 4);
    }

    /*! A straight-through midpoint on one side (turn 0) is not a corner:
     *  still Mapped, corners skip it. */
    void classify_rectangleWithMidpoint_mapped()
    {
        QPolygonF r{QPointF(0, 0), QPointF(10, 0), QPointF(20, 0), QPointF(20, 5), QPointF(0, 5)};
        QVector<int> corners;
        QCOMPARE(classifyQuadRegion(r, &corners), QuadRegionMode::Mapped);
        QCOMPARE(corners, QVector<int>({0, 2, 3, 4}));
    }

    /*! L-shape: six ±90° turns, one of them reflex → Submapped, no corners. */
    void classify_lShape_submapped()
    {
        QVector<int> corners{1, 2, 3, 4};
        QCOMPARE(classifyQuadRegion(lShape(), &corners), QuadRegionMode::Submapped);
        QVERIFY(corners.isEmpty());
    }

    /*! 12-gon "circle": 30° turns everywhere → neither four corners nor
     *  rectilinear → Free. A skewed blob is Free too. Null corners OK. */
    void classify_circle_free()
    {
        QVector<int> corners;
        QCOMPARE(classifyQuadRegion(regularPolygon(12, 10.0), &corners), QuadRegionMode::Free);
        QVERIFY(corners.isEmpty());
        QPolygonF blob{QPointF(0, 0), QPointF(10, 1), QPointF(12, 6), QPointF(6, 11), QPointF(-1, 5)};
        QCOMPARE(classifyQuadRegion(blob, nullptr), QuadRegionMode::Free);
    }

    /*! Inside the domain → valid; partly outside → error; inside a hole →
     *  error; too small for its spacing (area < 4h²) → error; degenerate →
     *  error. */
    void validateQuadRegion_cases()
    {
        const QVector<QPolygonF> domains{rect(0, 0, 100, 100)};
        const QVector<QPolygonF> noHoles;

        QuadRegion ok;
        ok.ring = rect(10, 10, 20, 10);
        ok.spacing = 1.0;
        QVERIFY2(validateQuadRegion(ok, domains, noHoles).isEmpty(),
                 qPrintable(validateQuadRegion(ok, domains, noHoles)));

        QuadRegion partlyOutside = ok;
        partlyOutside.ring = rect(90, 90, 20, 20);
        QVERIFY(!validateQuadRegion(partlyOutside, domains, noHoles).isEmpty());

        QuadRegion fullyOutside = ok;
        fullyOutside.ring = rect(200, 200, 10, 10);
        QVERIFY(!validateQuadRegion(fullyOutside, domains, noHoles).isEmpty());

        const QVector<QPolygonF> holes{rect(5, 5, 40, 40)};
        QVERIFY(!validateQuadRegion(ok, domains, holes).isEmpty());
        QuadRegion besideHole = ok;
        besideHole.ring = rect(60, 60, 20, 10);
        QVERIFY(validateQuadRegion(besideHole, domains, holes).isEmpty());

        QuadRegion tooSmall = ok;            // area 200 < 4·8² = 256
        tooSmall.spacing = 8.0;
        QVERIFY(!validateQuadRegion(tooSmall, domains, noHoles).isEmpty());
        QuadRegion justBig = ok;             // area 200 >= 4·7² = 196
        justBig.spacing = 7.0;
        QVERIFY(validateQuadRegion(justBig, domains, noHoles).isEmpty());
        QuadRegion noSpacing = tooSmall;     // spacing 0 → area test skipped
        noSpacing.spacing = 0.0;
        QVERIFY(validateQuadRegion(noSpacing, domains, noHoles).isEmpty());

        QuadRegion degenerate = ok;
        degenerate.ring = QPolygonF{QPointF(0, 0), QPointF(10, 0)};
        QVERIFY(!validateQuadRegion(degenerate, domains, noHoles).isEmpty());
        QuadRegion bowtie = ok;
        bowtie.ring = QPolygonF{QPointF(10, 10), QPointF(20, 20), QPointF(20, 10), QPointF(10, 20)};
        QVERIFY(!ringIsSimple(bowtie.ring));
        QVERIFY(!validateQuadRegion(bowtie, domains, noHoles).isEmpty());
    }

    /*! Two disjoint rings → OK; overlapping → "region i overlaps region j";
     *  rings sharing an edge → OK. */
    void validateQuadRegionsDisjoint_cases()
    {
        QuadRegion a; a.ring = rect(0, 0, 10, 5);
        QuadRegion b; b.ring = rect(20, 0, 10, 5);
        QVERIFY(validateQuadRegionsDisjoint({a, b}).isEmpty());

        QuadRegion c; c.ring = rect(5, 2, 15, 6);
        const QString err = validateQuadRegionsDisjoint({a, c});
        QVERIFY(!err.isEmpty());
        QVERIFY(err.contains(QStringLiteral("overlaps")));

        QuadRegion d; d.ring = rect(10, 0, 10, 5);      // shares edge x = 10
        QVERIFY(validateQuadRegionsDisjoint({a, d}).isEmpty());
        QuadRegion e; e.ring = rect(10, 5, 10, 5);      // shares only the vertex (10,5)
        QVERIFY(validateQuadRegionsDisjoint({a, e}).isEmpty());

        // Fully nested is an overlap as well.
        QuadRegion inner; inner.ring = rect(2, 1, 4, 2);
        QVERIFY(!validateQuadRegionsDisjoint({a, inner}).isEmpty());
        QVERIFY(validateQuadRegionsDisjoint({a}).isEmpty());
        QVERIFY(validateQuadRegionsDisjoint({}).isEmpty());
    }

    /*! 10×10 square at h = 2: every side split into 5 → 20 vertices, all on
     *  the boundary, CCW, open (no closing duplicate), original corners kept. */
    void resampleRing_square()
    {
        const QPolygonF sq = rect(0, 0, 10, 10);
        const QPolygonF rs = resampleRing(sq, 2.0);
        QCOMPARE(rs.size(), 20);
        QVERIFY(ringSignedArea(rs) > 0.0);
        QVERIFY(near(ringSignedArea(rs), 100.0, 1e-9));
        QVERIFY(rs.first() != rs.last());
        for (const QPointF &p : rs) QVERIFY(onRingBoundary(sq, p, 1e-9));
        for (const QPointF &c : sq) QVERIFY(rs.contains(c));
        // Consecutive spacing is h on every edge.
        for (int i = 0; i < rs.size(); ++i)
        {
            const QPointF a = rs[i], b = rs[(i + 1) % rs.size()];
            QVERIFY(near(std::hypot(b.x() - a.x(), b.y() - a.y()), 2.0, 1e-9));
        }
        // CW input comes out CCW too; h larger than every edge keeps the ring.
        QPolygonF cw{QPointF(0, 0), QPointF(0, 10), QPointF(10, 10), QPointF(10, 0)};
        const QPolygonF rcw = resampleRing(cw, 2.0);
        QCOMPARE(rcw.size(), 20);
        QVERIFY(ringSignedArea(rcw) > 0.0);
        QCOMPARE(resampleRing(sq, 50.0).size(), 4);
        // Non-multiple: 10 / 3 → round(3.33) = 3 parts per side.
        QCOMPARE(resampleRing(sq, 3.0).size(), 12);
    }

    /*! Odd-even point-in-polygon on a square, an L and a closed (last ==
     *  first) ring. */
    void pointInRing_cases()
    {
        const QPolygonF sq = rect(0, 0, 10, 10);
        QVERIFY(pointInRing(sq, QPointF(5, 5)));
        QVERIFY(pointInRing(sq, QPointF(0.01, 9.99)));
        QVERIFY(!pointInRing(sq, QPointF(-1, 5)));
        QVERIFY(!pointInRing(sq, QPointF(5, 11)));
        QVERIFY(!pointInRing(sq, QPointF(10.5, 10.5)));

        const QPolygonF L = lShape();
        QVERIFY(pointInRing(L, QPointF(2, 8)));
        QVERIFY(pointInRing(L, QPointF(8, 2)));
        QVERIFY(!pointInRing(L, QPointF(8, 8)));   // the notch

        QPolygonF closed = sq; closed << sq.first();
        QVERIFY(pointInRing(closed, QPointF(5, 5)));
        QVERIFY(!pointInRing(closed, QPointF(15, 5)));
        QVERIFY(!pointInRing(QPolygonF{QPointF(0, 0), QPointF(1, 0)}, QPointF(0.5, 0)));
    }
};

QTEST_MAIN(TestMeshQuadRegion)
#include "test_meshquadregion.moc"
