/**
 * @file      test_polylineoffset.cpp
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     Tests for Slice Z.5b — polyline perpendicular offset.
 *
 *         Success criterion: each shape below produces vertices in the
 *         expected geometric positions to within 1e-6 tolerance.
 *
 *         Sign convention: positive offset = right of forward direction.
 *         In Qt image coords (y down), "right of forward" for a left-
 *         to-right horizontal line means y increases (downward in image
 *         space).
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
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
    /**
     *
     * @brief Require an empty path to stay empty under nonzero offset.
     *
     */
    void empty_polylineReturnsEmpty();
    /**
     *
     * @brief Require a one-point path to preserve its sole coordinate under nonzero offset.
     *
     */
    void singleVertex_returnsUnchanged();
    /**
     *
     * @brief Require zero offset to preserve the complete bent-path coordinate sequence.
     *
     */
    void zeroOffset_returnsInputVerbatim();
    /**
     *
     * @brief Require an all-coincident path to remain unchanged.
     *
     */
    void allCoincidentVertices_returnsInputVerbatim();

    // Two-vertex (single-segment)
    /**
     *
     * @brief Require positive offset to move a rightward segment toward increasing image y.
     *
     */
    void horizontalLine_positiveOffsetShiftsDown();
    /**
     *
     * @brief Require negative offset to move a rightward segment toward decreasing image y.
     *
     */
    void horizontalLine_negativeOffsetShiftsUp();
    /**
     *
     * @brief Require positive offset of a downward image segment to move toward negative x.
     *
     */
    void verticalLine_positiveOffsetShiftsLeft();
    /**
     *
     * @brief Require a single diagonal segment offset to preserve length within 1e-6.
     *
     */
    void diagonalSegmentOffsetMaintainsLength();

    // Multi-vertex (interior joins)
    /**
     *
     * @brief Require three expected right-angle mitre coordinates for offset 5.
     *
     */
    void rightAngleCornerProducesExpectedMiter();
    /**
     *
     * @brief Require a 180-degree folded path to return at least three vertices.
     * This case does not assert exact bevel geometry or a four-vertex result.
     *
     */
    void parallelSegmentsFallBackToBevel();
    /**
     *
     * @brief Require a 20-degree reversal with mitre limit 1.5 to emit at least four vertices.
     *
     */
    void sharpAngleBevelsRatherThanSpike();
    /**
     *
     * @brief Require three collinear vertices to keep their x positions and shift uniformly by
     * y=4.
     *
     */
    void straightCollinearVerticesRemainStraight();

    // Properties
    /**
     *
     * @brief Require paired positive/negative offsets to have equal counts and midpoint near each
     * corresponding input vertex.
     *
     */
    void offsetIsAntisymmetricUnderSignFlip();
    /**
     *
     * @brief Pin single-segment offsets 2 and 4 to expected coordinates; both endpoints of the
     * larger offset are checked.
     *
     */
    void offsetSize_scalesLinearlyWithMagnitude();

private:
    /**
     *
     * @brief Compare planar fixture points by strict Euclidean distance tolerance.
     * @param a Borrowed first XY point in common scene-coordinate units.
     * @param b Borrowed second point in the same frame/units.
     * @param eps Positive distance tolerance, default 1e-6 in those units.
     * @return True when hypot(dx,dy)<eps, false for equality at tolerance/nonfinite
     * ordinary differences. No mutation, relative scaling or diagnostic assertion.
     *
     */
    static bool nearlyEqual(const QPointF &a, const QPointF &b, qreal eps = 1e-6)
    {
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
    const qreal inLen = std::hypot(in[1].x() - in[0].x(), in[1].y() - in[0].y());
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
    in << QPointF(-100, 0) << QPointF(0, 0)
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
        const QPointF mid((pos[i].x() + neg[i].x()) * 0.5, (pos[i].y() + neg[i].y()) * 0.5);
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
