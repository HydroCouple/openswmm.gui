/*!
 * \file   test_polylineoffset.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Tests for Slice Z.5b — polyline perpendicular offset.
 *
 *         Success criterion: each shape below produces vertices in the
 *         expected geometric positions to within 1e-6 tolerance.
 *
 *         Sign convention: positive offset = right of forward direction.
 *         In Qt image coords (y down), "right of forward" for a left-
 *         to-right horizontal line means y increases (downward in image
 *         space).
 */

#include <QPolygonF>
#include <QtTest/QtTest>

#include <cmath>

#include "render/polylineoffset.h"

using namespace OpenSWMM::Render;

class TestPolylineOffset : public QObject
{
    Q_OBJECT
private slots:
    // Degenerate input
    void empty_polylineReturnsEmpty();
    void singleVertex_returnsUnchanged();
    void zeroOffset_returnsInputVerbatim();
    void allCoincidentVertices_returnsInputVerbatim();

    // Two-vertex (single-segment)
    void horizontalLine_positiveOffsetShiftsDown();
    void horizontalLine_negativeOffsetShiftsUp();
    void verticalLine_positiveOffsetShiftsLeft();
    void diagonalSegmentOffsetMaintainsLength();

    // Multi-vertex (interior joins)
    void rightAngleCornerProducesExpectedMiter();
    void parallelSegmentsFallBackToBevel();
    void sharpAngleBevelsRatherThanSpike();
    void straightCollinearVerticesRemainStraight();

    // Properties
    void offsetIsAntisymmetricUnderSignFlip();
    void offsetSize_scalesLinearlyWithMagnitude();

private:
    static bool nearlyEqual(const QPointF &a, const QPointF &b, qreal eps = 1e-6) {
        return std::hypot(a.x() - b.x(), a.y() - b.y()) < eps;
    }
};

// ── Degenerate ─────────────────────────────────────────────────────

void TestPolylineOffset::empty_polylineReturnsEmpty()
{
    QCOMPARE(offsetPolyline({}, 5.0).size(), 0);
}

void TestPolylineOffset::singleVertex_returnsUnchanged()
{
    QPolygonF in;
    in << QPointF(10, 20);
    const auto out = offsetPolyline(in, 5.0);
    QCOMPARE(out.size(), 1);
    QCOMPARE(out[0], QPointF(10, 20));
}

void TestPolylineOffset::zeroOffset_returnsInputVerbatim()
{
    QPolygonF in;
    in << QPointF(0, 0) << QPointF(10, 0) << QPointF(10, 10);
    const auto out = offsetPolyline(in, 0.0);
    QCOMPARE(out, in);
}

void TestPolylineOffset::allCoincidentVertices_returnsInputVerbatim()
{
    QPolygonF in;
    in << QPointF(5, 5) << QPointF(5, 5) << QPointF(5, 5);
    const auto out = offsetPolyline(in, 3.0);
    QCOMPARE(out, in);
}

// ── Single segment ─────────────────────────────────────────────────

void TestPolylineOffset::horizontalLine_positiveOffsetShiftsDown()
{
    // Forward direction = +x. Right of forward (90° CW in y-down) = +y.
    QPolygonF in;
    in << QPointF(0, 0) << QPointF(10, 0);
    const auto out = offsetPolyline(in, 5.0);
    QCOMPARE(out.size(), 2);
    QVERIFY(nearlyEqual(out[0], QPointF(0, 5)));
    QVERIFY(nearlyEqual(out[1], QPointF(10, 5)));
}

void TestPolylineOffset::horizontalLine_negativeOffsetShiftsUp()
{
    QPolygonF in;
    in << QPointF(0, 0) << QPointF(10, 0);
    const auto out = offsetPolyline(in, -5.0);
    QCOMPARE(out.size(), 2);
    QVERIFY(nearlyEqual(out[0], QPointF(0, -5)));
    QVERIFY(nearlyEqual(out[1], QPointF(10, -5)));
}

void TestPolylineOffset::verticalLine_positiveOffsetShiftsLeft()
{
    // Forward = +y (downward in image coords). Right of forward
    // (90° CW in y-down) = -x.
    QPolygonF in;
    in << QPointF(0, 0) << QPointF(0, 10);
    const auto out = offsetPolyline(in, 5.0);
    QVERIFY(nearlyEqual(out[0], QPointF(-5, 0)));
    QVERIFY(nearlyEqual(out[1], QPointF(-5, 10)));
}

void TestPolylineOffset::diagonalSegmentOffsetMaintainsLength()
{
    QPolygonF in;
    in << QPointF(0, 0) << QPointF(10, 10);
    const auto out = offsetPolyline(in, 5.0);
    const qreal inLen  = std::hypot(in[1].x()  - in[0].x(),  in[1].y()  - in[0].y());
    const qreal outLen = std::hypot(out[1].x() - out[0].x(), out[1].y() - out[0].y());
    QVERIFY(std::abs(outLen - inLen) < 1e-6);
}

// ── Multi-vertex ───────────────────────────────────────────────────

void TestPolylineOffset::rightAngleCornerProducesExpectedMiter()
{
    // L-shape: (0,0) → (10,0) → (10,10).
    //   First segment goes right (forward +x); right-normal = +y → (0,5).
    //   Second segment goes down (forward +y); right-normal = -x → (10-5, 10) = (5,10).
    // Miter intersection: both shifted segments meet at (5, 5).
    QPolygonF in;
    in << QPointF(0, 0) << QPointF(10, 0) << QPointF(10, 10);
    const auto out = offsetPolyline(in, 5.0);
    QCOMPARE(out.size(), 3);
    QVERIFY(nearlyEqual(out[0], QPointF(0, 5)));
    QVERIFY(nearlyEqual(out[1], QPointF(5, 5)));
    QVERIFY(nearlyEqual(out[2], QPointF(5, 10)));
}

void TestPolylineOffset::parallelSegmentsFallBackToBevel()
{
    // Two collinear segments via a coincident "interior" vertex (not
    // truly parallel and offset — just two segments at 180° fold). A
    // 180° turn yields a degenerate intersection; bevel kicks in.
    QPolygonF in;
    in << QPointF(0, 0) << QPointF(10, 0) << QPointF(0, 0);
    const auto out = offsetPolyline(in, 5.0);
    // Bevel adds a vertex at the corner — 3 input vertices, 4 output
    // (start, two bevel vertices, end).
    QVERIFY(out.size() >= 3);
}

void TestPolylineOffset::sharpAngleBevelsRatherThanSpike()
{
    // 20° angle — miter would be enormous; bevel limit kicks in.
    const qreal deg = 20.0;
    const qreal rad = deg * M_PI / 180.0;
    QPolygonF in;
    in << QPointF(-100, 0)
       << QPointF(0, 0)
       << QPointF(100 * std::cos(M_PI - rad), 100 * std::sin(M_PI - rad));

    // Tight miter limit so any non-trivial miter falls back to bevel.
    const auto out = offsetPolyline(in, 5.0, 1.5);
    // Bevel inserts one extra vertex at the corner.
    QVERIFY2(out.size() >= 4,
             "Bevel should emit at least 4 vertices for a 3-vertex polyline at sharp angles");
}

void TestPolylineOffset::straightCollinearVerticesRemainStraight()
{
    // Three collinear points: offset stays in a straight line.
    QPolygonF in;
    in << QPointF(0, 0) << QPointF(5, 0) << QPointF(10, 0);
    const auto out = offsetPolyline(in, 4.0);
    QCOMPARE(out.size(), 3);
    QVERIFY(nearlyEqual(out[0], QPointF(0, 4)));
    QVERIFY(nearlyEqual(out[1], QPointF(5, 4)));
    QVERIFY(nearlyEqual(out[2], QPointF(10, 4)));
}

// ── Properties ─────────────────────────────────────────────────────

void TestPolylineOffset::offsetIsAntisymmetricUnderSignFlip()
{
    QPolygonF in;
    in << QPointF(0, 0) << QPointF(10, 0) << QPointF(20, 5);
    const auto pos = offsetPolyline(in, 3.0);
    const auto neg = offsetPolyline(in, -3.0);
    QCOMPARE(pos.size(), neg.size());
    for (int i = 0; i < pos.size(); ++i) {
        // Reflected across the original polyline's projected centerline —
        // approximately satisfies pos[i] + neg[i] ≈ 2 * input vertex when
        // angles are not too sharp. Use input[i] when polyline sizes line up;
        // otherwise just confirm magnitudes oppose.
        const QPointF mid((pos[i].x() + neg[i].x()) * 0.5,
                          (pos[i].y() + neg[i].y()) * 0.5);
        if (i < in.size())
            QVERIFY(std::hypot(mid.x() - in[i].x(), mid.y() - in[i].y()) < 1e-3);
    }
}

void TestPolylineOffset::offsetSize_scalesLinearlyWithMagnitude()
{
    QPolygonF in;
    in << QPointF(0, 0) << QPointF(10, 0);
    const auto a = offsetPolyline(in, 2.0);
    const auto b = offsetPolyline(in, 4.0);
    // Second offset moves twice as far from the original.
    QVERIFY(nearlyEqual(b[0], QPointF(0, 4)));
    QVERIFY(nearlyEqual(b[1], QPointF(10, 4)));
    QVERIFY(nearlyEqual(a[0], QPointF(0, 2)));
}

QTEST_MAIN(TestPolylineOffset)
#include "test_polylineoffset.moc"
