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
#include <limits>

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

    /*! Permitted rectangles tie squares. Rectangularity supplies the actual
     *  corner-angle penalty for rhombi, whose perpendicular diagonals give no
     *  diagonal-skew penalty. The aspect soft penalty remains independent. */
    void score_ordering()
    {
        const QuadQualityBounds b;
        const double sq = quadScore(qq({QPointF(0, 0), QPointF(1, 0), QPointF(1, 1), QPointF(0, 1)}), b);
        const double r2 = quadScore(qq({QPointF(0, 0), QPointF(2, 0), QPointF(2, 1), QPointF(0, 1)}), b);
        const double rh = quadScore(qq(rhombus(61.0)), b);
        QVERIFY(near(sq, 1.0, 1e-12));
        QVERIFY(near(rh, std::sin(61.0 * M_PI / 180.0)*(1.0-29.0/90.0), 1e-9));
        QVERIFY(near(r2, 1.0, 1e-9));
        QVERIFY(sq > rh);
        QVERIFY(r2 > rh);
        QVERIFY(r2 > 0.0);
        // Above the aspect cap the score is scaled down by maxAspect/aspect.
        const double r4 = quadScore(qq({QPointF(0, 0), QPointF(4, 0), QPointF(4, 1), QPointF(0, 1)}), b);
        QVERIFY(near(r4,0.5,1e-12));
    }

    void permittedRectangles_data()
    {
        QTest::addColumn<double>("aspect");QTest::addColumn<double>("angle");QTest::addColumn<double>("origin");
        QTest::addColumn<int>("start");QTest::addColumn<bool>("reverse");
        for(double aspect:{1.0,2.0,4.0,8.0})for(double angle:{0.0,31.0})for(double origin:{0.0,1e6,1e9})
            for(int start=0;start<4;++start)for(bool reverse:{false,true}){
                const auto name=QString("aspect%1-angle%2-origin%3-start%4-reverse%5").arg(aspect).arg(angle).arg(origin).arg(start).arg(reverse).toLatin1();
                QTest::newRow(name.constData())<<aspect<<angle<<origin<<start<<reverse;
            }
    }
    void permittedRectangles()
    {
        QFETCH(double,aspect);QFETCH(double,angle);QFETCH(double,origin);QFETCH(int,start);QFETCH(bool,reverse);
        const double radians=angle*M_PI/180;
        const QPointF u(.25*aspect*std::cos(radians),.25*aspect*std::sin(radians));
        const QPointF v(-.25*std::sin(radians),.25*std::cos(radians)),o(origin,origin);
        const QVector<QPointF> base{o,o+u,o+u+v,o+v};QVector<QPointF> points;
        for(int k=0;k<4;++k)points.append(base[(start+(reverse?-k:k)+4)%4]);
        const auto q=qq(points);QuadQualityBounds bounds;
        // Input rounding at1e9 may perturb the actual aspect very slightly;
        // the cap includes that representational variation, not an angle relaxation.
        bounds.maxAspect=aspect+1e-5;
        QVERIFY(q.convex);QVERIFY(std::isfinite(q.area));QVERIFY(q.area>0);
        QVERIFY(near(q.area,.0625*aspect,1e-6*aspect));
        QVERIFY(near(q.scaledJacobian,1,2e-6));QVERIFY(near(q.rectangularity,1,2e-6));
        QVERIFY(near(q.aspect,aspect,1e-5));QVERIFY(quadAcceptable(q,bounds));
        QVERIFY2(near(quadScore(q,bounds),1,2e-6),qPrintable(QString::number(quadScore(q,bounds),'g',17)));
        QVERIFY(q.skew>=0 && q.skew<=1);
        QVERIFY(near(q.skew,(aspect*aspect-1)/(aspect*aspect+1),2e-6));
    }

    void rhombiAndKitesPenalizedByAngles()
    {
        QuadQualityBounds bounds;bounds.maxAspect=8;
        const auto rectangle=qq({{0,0},{8,0},{8,1},{0,1}});
        const auto rh=qq(rhombus(65));
        const auto kite=qq({{0,0},{1,-.8},{2.2,0},{1,.8}});
        QVERIFY(quadAcceptable(rh,bounds));QVERIFY(quadAcceptable(kite,bounds));
        QVERIFY(near(rh.skew,0,1e-12));QVERIFY(near(kite.skew,0,1e-12));
        QVERIFY(quadScore(rectangle,bounds)>quadScore(rh,bounds));
        QVERIFY(quadScore(rectangle,bounds)>quadScore(kite,bounds));
        QVERIFY(near(quadScore(kite,bounds),kite.scaledJacobian*kite.rectangularity,1e-12));
    }

    void representableExtremeScales_data()
    {
        QTest::addColumn<double>("x");QTest::addColumn<double>("y");
        QTest::newRow("small")<<1e-100<<1e-100;
        QTest::newRow("large")<<1e100<<1e100;
        QTest::newRow("finite-area-near-overflow")<<1e154<<1e154;
        QTest::newRow("finite-area-anisotropic")<<1e150<<1e-150;
    }
    void representableExtremeScales()
    {
        QFETCH(double,x);QFETCH(double,y);
        const auto q=qq({{0,0},{x,0},{x,y},{0,y}});QuadQualityBounds bounds;bounds.maxAspect=0;
        QVERIFY(q.convex);QVERIFY(std::isfinite(q.area));QVERIFY(q.area>0);
        QVERIFY(std::abs(q.area/(x*y)-1)<1e-12);
        QVERIFY(std::isfinite(q.aspect));QVERIFY(std::isfinite(q.skew));
        QVERIFY(near(q.minAngleDeg,90,1e-12));QVERIFY(near(q.maxAngleDeg,90,1e-12));
        QVERIFY(near(q.scaledJacobian,1,1e-12));QVERIFY(near(q.rectangularity,1,1e-12));
        QVERIFY(quadAcceptable(q,bounds));QVERIFY(near(quadScore(q,bounds),1,1e-12));
    }

    void invalidGeometry_data()
    {
        QTest::addColumn<QVector<QPointF>>("points");
        const double nan=std::numeric_limits<double>::quiet_NaN(),inf=std::numeric_limits<double>::infinity(),huge=std::numeric_limits<double>::max();
        QTest::newRow("nan")<<QVector<QPointF>{{0,0},{1,0},{nan,1},{0,1}};
        QTest::newRow("inf")<<QVector<QPointF>{{0,0},{1,0},{1,inf},{0,1}};
        QTest::newRow("duplicate")<<QVector<QPointF>{{0,0},{1,0},{1,0},{0,1}};
        QTest::newRow("zero-area")<<QVector<QPointF>{{0,0},{1,0},{2,0},{3,0}};
        QTest::newRow("bowtie")<<QVector<QPointF>{{0,0},{1,1},{1,0},{0,1}};
        QTest::newRow("concave")<<QVector<QPointF>{{0,0},{2,0},{.5,.5},{0,2}};
        QTest::newRow("difference-overflow")<<QVector<QPointF>{{-huge,0},{huge,0},{huge,1},{-huge,1}};
        QTest::newRow("area-overflow")<<QVector<QPointF>{{0,0},{1e200,0},{1e200,1e200},{0,1e200}};
        QTest::newRow("area-underflow")<<QVector<QPointF>{{0,0},{1e-200,0},{1e-200,1e-200},{0,1e-200}};
    }
    void invalidGeometry()
    {
        QFETCH(QVector<QPointF>,points);const auto q=qq(points);
        QVERIFY(!quadAcceptable(q,QuadQualityBounds()));QCOMPARE(quadScore(q,QuadQualityBounds()),0.0);
    }

    void malformedMetricAndBounds_data()
    {
        QTest::addColumn<int>("field");QTest::addColumn<double>("value");QTest::addColumn<bool>("bounds");
        const double nan=std::numeric_limits<double>::quiet_NaN(),inf=std::numeric_limits<double>::infinity();
        for(int field=0;field<7;++field)for(double value:{nan,inf,-inf}){
            const auto label=QString("metric-%1-%2").arg(field).arg(value).toLatin1();
            QTest::newRow(label.constData())<<field<<value<<false;
        }
        for(int field=0;field<4;++field)for(double value:{nan,inf,-inf}){
            const auto label=QString("bounds-%1-%2").arg(field).arg(value).toLatin1();
            QTest::newRow(label.constData())<<field<<value<<true;
        }
        QTest::newRow("zero-area")<<6<<0.0<<false;QTest::newRow("negative-area")<<6<<-1.0<<false;
        QTest::newRow("sj-above-one")<<2<<2.0<<false;QTest::newRow("rectangularity-above-one")<<3<<2.0<<false;
        QTest::newRow("negative-rectangularity")<<3<<-1.0<<false;QTest::newRow("aspect-below-one")<<4<<.5<<false;
        QTest::newRow("skew-above-one")<<5<<2.0<<false;
    }
    void malformedMetricAndBounds()
    {
        QFETCH(int,field);QFETCH(double,value);QFETCH(bool,bounds);
        auto q=qq({{0,0},{1,0},{1,1},{0,1}});QuadQualityBounds b;
        if(bounds){double *fields[]{&b.minAngleDeg,&b.maxAngleDeg,&b.minScaledJacobian,&b.maxAspect};*fields[field]=value;}
        else{double *fields[]{&q.minAngleDeg,&q.maxAngleDeg,&q.scaledJacobian,&q.rectangularity,&q.aspect,&q.skew,&q.area};*fields[field]=value;}
        QVERIFY(!quadAcceptable(q,b));QCOMPARE(quadScore(q,b),0.0);
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
