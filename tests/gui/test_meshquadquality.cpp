/*!
 * \file   test_meshquadquality.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * Quad redesign gate 1 (workplans/QUAD_MESHING_REDESIGN_PLAN_2026-09-06.md
 * §5, §7.1) — QtTest coverage for the header-only quad quality metrics in
 * mesh/meshquadquality.h: scaled Jacobian, rectangularity, aspect, skew,
 * convexity, the default acceptance bounds (60°/120°, SJ ≥ 0.866, aspect ≤ 2)
 * and the combined score's ordering.
 */
#include <QtTest>
#include <QPointF>
#include <QVector>

#include <cmath>

#include "mesh/meshquadquality.h"
#include "mesh/meshresult.h"

using namespace mesh;

namespace {

/*! Rhombus with unit sides, acute corner \p acuteDeg at the origin, CCW. */
QVector<QPointF> rhombus(double acuteDeg)
{
    const double a = acuteDeg * M_PI / 180.0;
    const QPointF u(1.0, 0.0), v(std::cos(a), std::sin(a));
    return {QPointF(0, 0), u, u + v, v};
}

QuadQuality qq(const QVector<QPointF> &p)
{
    return quadQuality(p[0], p[1], p[2], p[3]);
}

bool near(double a, double b, double tol) { return std::abs(a - b) <= tol; }

} // namespace

class TestMeshQuadQuality : public QObject
{
    Q_OBJECT

private slots:

    /*! Unit square: SJ 1, rectangularity 1, aspect 1, skew 0, convex, area 1. */
    void unitSquare_perfect()
    {
        const QuadQuality q = qq({QPointF(0, 0), QPointF(1, 0), QPointF(1, 1), QPointF(0, 1)});
        QVERIFY(q.convex);
        QVERIFY(near(q.scaledJacobian, 1.0, 1e-12));
        QVERIFY(near(q.rectangularity, 1.0, 1e-12));
        QVERIFY(near(q.aspect, 1.0, 1e-12));
        QVERIFY(near(q.skew, 0.0, 1e-12));
        QVERIFY(near(q.area, 1.0, 1e-12));
        QVERIFY(near(q.minAngleDeg, 90.0, 1e-9));
        QVERIFY(near(q.maxAngleDeg, 90.0, 1e-9));
        QVERIFY(quadAcceptable(q, QuadQualityBounds()));
        // Orientation does not matter: the CW square is the same quad.
        const QuadQuality cw = qq({QPointF(0, 0), QPointF(0, 1), QPointF(1, 1), QPointF(1, 0)});
        QVERIFY(cw.convex);
        QVERIFY(near(cw.scaledJacobian, 1.0, 1e-12));
    }

    /*! 60°/120° rhombus: SJ = sin 60° = 0.866, ρ = 1 − 30/90 = 0.667. */
    void rhombus60_metrics()
    {
        const QuadQuality q = qq(rhombus(60.0));
        QVERIFY(q.convex);
        QVERIFY(near(q.scaledJacobian, std::sqrt(3.0) / 2.0, 1e-9));
        QVERIFY(near(q.rectangularity, 2.0 / 3.0, 1e-9));
        QVERIFY(near(q.minAngleDeg, 60.0, 1e-9));
        QVERIFY(near(q.maxAngleDeg, 120.0, 1e-9));
        QVERIFY(near(q.aspect, 1.0, 1e-9));
        // Diagonals of a rhombus are perpendicular → skew 0.
        QVERIFY(near(q.skew, 0.0, 1e-9));
    }

    /*! Default bounds: the 59/121 rhombus is outside the hard 60°/120°
     *  window and its SJ (sin 59° = 0.857) is below 0.866 → rejected; 61/119
     *  is inside (sin 61° = 0.875) → accepted. */
    void defaultBounds_rhombusBorderline()
    {
        const QuadQualityBounds b;
        QCOMPARE(b.minAngleDeg, 60.0);
        QCOMPARE(b.maxAngleDeg, 120.0);
        QCOMPARE(b.minScaledJacobian, 0.866);
        QCOMPARE(b.maxAspect, 2.0);

        const QuadQuality reject = qq(rhombus(59.0));
        QVERIFY(reject.convex);
        QVERIFY(!quadAcceptable(reject, b));

        const QuadQuality accept = qq(rhombus(61.0));
        QVERIFY(accept.convex);
        QVERIFY(accept.scaledJacobian > 0.866);
        QVERIFY(quadAcceptable(accept, b));
    }

    /*! Aspect cap: a 2:1 rectangle is accepted (aspect exactly 2.0), 3:1 is
     *  rejected; with maxAspect <= 0 the cap is off. */
    void defaultBounds_aspect()
    {
        const QuadQualityBounds b;
        const QuadQuality r2 = qq({QPointF(0, 0), QPointF(2, 0), QPointF(2, 1), QPointF(0, 1)});
        QVERIFY(near(r2.aspect, 2.0, 1e-12));
        QVERIFY(near(r2.scaledJacobian, 1.0, 1e-12));
        QVERIFY(quadAcceptable(r2, b));

        const QuadQuality r3 = qq({QPointF(0, 0), QPointF(3, 0), QPointF(3, 1), QPointF(0, 1)});
        QVERIFY(near(r3.aspect, 3.0, 1e-12));
        QVERIFY(!quadAcceptable(r3, b));

        QuadQualityBounds open = b;
        open.maxAspect = 0.0;
        QVERIFY(quadAcceptable(r3, open));
    }

    /*! Bowtie (self-intersecting order): not convex, score 0, rejected. */
    void folded_notConvex()
    {
        const QuadQuality q = qq({QPointF(0, 0), QPointF(1, 1), QPointF(1, 0), QPointF(0, 1)});
        QVERIFY(!q.convex);
        QVERIFY(q.scaledJacobian <= 0.0);
        QCOMPARE(quadScore(q, QuadQualityBounds()), 0.0);
        QVERIFY(!quadAcceptable(q, QuadQualityBounds()));

        // A concave (dart) quad is also non-convex.
        const QuadQuality dart = qq({QPointF(0, 0), QPointF(2, 0), QPointF(0.5, 0.5), QPointF(0, 2)});
        QVERIFY(!dart.convex);
        QCOMPARE(quadScore(dart, QuadQualityBounds()), 0.0);
    }

    /*! quadScore = SJ · min(1, aspectMax/aspect) · sqrt(1 − skew) (plan §5).
     *  Square: 1. 61/119 rhombus: SJ sin 61° = 0.875, perpendicular
     *  diagonals → skew 0 → 0.875. 2:1 rectangle: SJ 1 but its diagonals
     *  (2,1)/(−2,1) give skew 0.6 → sqrt(0.4) = 0.632. So the ordering the
     *  formula yields is square > rhombus(61) > rectangle(2:1); the
     *  rectangle is still ACCEPTED (aspect 2.0) — the score only ranks
     *  candidates. Pinned here so a change of the formula is deliberate. */
    void score_ordering()
    {
        const QuadQualityBounds b;
        const double sq = quadScore(qq({QPointF(0, 0), QPointF(1, 0), QPointF(1, 1), QPointF(0, 1)}), b);
        const double r2 = quadScore(qq({QPointF(0, 0), QPointF(2, 0), QPointF(2, 1), QPointF(0, 1)}), b);
        const double rh = quadScore(qq(rhombus(61.0)), b);
        QVERIFY(near(sq, 1.0, 1e-12));
        QVERIFY(near(rh, std::sin(61.0 * M_PI / 180.0), 1e-9));
        QVERIFY(near(r2, std::sqrt(0.4), 1e-9));
        QVERIFY(sq > rh);
        QVERIFY(rh > r2);
        QVERIFY(r2 > 0.0);
        // Above the aspect cap the score is scaled down by maxAspect/aspect.
        const double r4 = quadScore(qq({QPointF(0, 0), QPointF(4, 0), QPointF(4, 1), QPointF(0, 1)}), b);
        QVERIFY(r4 < r2);
    }

    /*! Equilateral triangle: min sine = sin 60° = 0.866; right isosceles 0.707;
     *  orientation-independent; degenerate → 0. */
    void triangleScaledJacobian_values()
    {
        const QPointF a(0, 0), bpt(1, 0), c(0.5, std::sqrt(3.0) / 2.0);
        QVERIFY(near(triangleScaledJacobian(a, bpt, c), std::sqrt(3.0) / 2.0, 1e-9));
        QVERIFY(near(triangleScaledJacobian(a, c, bpt), std::sqrt(3.0) / 2.0, 1e-9));
        QVERIFY(near(triangleScaledJacobian(QPointF(0, 0), QPointF(1, 0), QPointF(0, 1)),
                     std::sqrt(0.5), 1e-9));
        QCOMPARE(triangleScaledJacobian(a, a, c), 0.0);
    }

    /*! The MeshResult overload and cellCornerAngleDeg agree with the point form. */
    void meshOverloads()
    {
        QVector<MeshVertex> v;
        for (const QPointF &p : rhombus(60.0)) { MeshVertex mv; mv.xy = p; v.append(mv); }
        MeshTriangle q; q.v0 = 0; q.v1 = 1; q.v2 = 2; q.v3 = 3;
        const QuadQuality a = quadQuality(v, q), b = qq(rhombus(60.0));
        QVERIFY(near(a.scaledJacobian, b.scaledJacobian, 1e-12));
        QVERIFY(near(a.rectangularity, b.rectangularity, 1e-12));
        QVERIFY(near(cellCornerAngleDeg(v, q, 0), 60.0, 1e-9));
        QVERIFY(near(cellCornerAngleDeg(v, q, 1), 120.0, 1e-9));
        MeshTriangle t; t.v0 = 0; t.v1 = 1; t.v2 = 3;   // 60° corner at vertex 0
        QVERIFY(near(cellCornerAngleDeg(v, t, 0), 60.0, 1e-9));
    }
};

QTEST_MAIN(TestMeshQuadQuality)
#include "test_meshquadquality.moc"
