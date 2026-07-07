/*!
 * \file   test_editgeometry.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date 2026
 *
 * \brief Leaf QtTest for the pure geometry helpers used by the map-edit
 *        tools (MapToolMoveNode, MapToolEditVertex) and their undo
 *        commands (MoveNodeCommand, EditVertexCommand).
 */

#include <QtTest/QtTest>

#include "core/editgeometry.h"

class TestEditGeometry : public QObject
{
    Q_OBJECT

private slots:
    // polylineLength -----------------------------------------------------
    void polylineLength_empty()
    {
        QCOMPARE(EditGeometry::polylineLength({}), 0.0);
    }

    void polylineLength_singleVertex()
    {
        QCOMPARE(EditGeometry::polylineLength({{1.0, 1.0}}), 0.0);
    }

    void polylineLength_twoPoints()
    {
        // 3-4-5 triangle
        QCOMPARE(EditGeometry::polylineLength({{0.0, 0.0}, {3.0, 4.0}}), 5.0);
    }

    void polylineLength_multiSegment()
    {
        // 3 segments of length 1 each = 3
        const QVector<QPointF> v{{0, 0}, {1, 0}, {1, 1}, {0, 1}};
        QCOMPARE(EditGeometry::polylineLength(v), 3.0);
    }

    // orientInteriorToEndpoints ------------------------------------------
    void orient_fewerThanTwoIsNoOp()
    {
        const QPointF from(0, 0), to(10, 0);
        QCOMPARE(EditGeometry::orientInteriorToEndpoints({}, from, to),
                 QVector<QPointF>{});
        const QVector<QPointF> one{{5, 5}};
        QCOMPARE(EditGeometry::orientInteriorToEndpoints(one, from, to), one);
    }

    void orient_alreadyAlignedUnchanged()
    {
        // Interior runs from→to (first near from, last near to): keep as-is.
        const QPointF from(0, 0), to(10, 0);
        const QVector<QPointF> interior{{2, 1}, {8, 1}};
        QCOMPARE(EditGeometry::orientInteriorToEndpoints(interior, from, to),
                 interior);
    }

    void orient_reversedGetsFlipped()
    {
        // Interior stored to→from (first near to, last near from): flip so the
        // assembled [from, interior, to] polyline no longer doubles back.
        const QPointF from(0, 0), to(10, 0);
        const QVector<QPointF> stored{{8, 1}, {2, 1}};   // to→from order
        const QVector<QPointF> want {{2, 1}, {8, 1}};    // from→to order
        QCOMPARE(EditGeometry::orientInteriorToEndpoints(stored, from, to),
                 want);
    }

    void orient_fixesSelfCrossingLoop()
    {
        // Reproduce the reported artifact: a reversed interior makes the full
        // polyline self-cross; orienting removes the crossing.
        const QPointF from(0, 0), to(10, 0);
        const QVector<QPointF> reversedInterior{{9, 2}, {5, 2}, {1, 2}};
        const auto oriented =
            EditGeometry::orientInteriorToEndpoints(reversedInterior, from, to);
        // Oriented interior must progress monotonically in x toward `to`.
        QCOMPARE(oriented, (QVector<QPointF>{{1, 2}, {5, 2}, {9, 2}}));
        // Full assembled path is x-monotonic (no backtracking → no loop).
        QVector<QPointF> full{from};
        full += oriented;
        full += to;
        for (int i = 1; i < full.size(); ++i)
            QVERIFY(full[i].x() >= full[i - 1].x());
    }

    // replacedAt ---------------------------------------------------------
    void replacedAt_firstVertex()
    {
        const QVector<QPointF> v{{0, 0}, {1, 1}, {2, 2}};
        const auto r = EditGeometry::replacedAt(v, 0, {10, 10});
        QCOMPARE(r.size(), 3);
        QCOMPARE(r[0], QPointF(10, 10));
        QCOMPARE(r[1], QPointF(1, 1));
        QCOMPARE(r[2], QPointF(2, 2));
    }

    void replacedAt_lastVertex()
    {
        const QVector<QPointF> v{{0, 0}, {1, 1}, {2, 2}};
        const auto r = EditGeometry::replacedAt(v, 2, {99, 99});
        QCOMPARE(r[2], QPointF(99, 99));
    }

    void replacedAt_outOfRangeIsNoOp()
    {
        const QVector<QPointF> v{{0, 0}, {1, 1}};
        const auto r = EditGeometry::replacedAt(v, 5, {3, 3});
        QCOMPARE(r, v);
    }

    // insertedAt ---------------------------------------------------------
    void insertedAt_middle()
    {
        const QVector<QPointF> v{{0, 0}, {2, 2}};
        const auto r = EditGeometry::insertedAt(v, 1, {1, 1});
        QCOMPARE(r.size(), 3);
        QCOMPARE(r[0], QPointF(0, 0));
        QCOMPARE(r[1], QPointF(1, 1));
        QCOMPARE(r[2], QPointF(2, 2));
    }

    void insertedAt_atEndClamps()
    {
        const QVector<QPointF> v{{0, 0}};
        const auto r = EditGeometry::insertedAt(v, 10, {1, 1});
        QCOMPARE(r.size(), 2);
        QCOMPARE(r.last(), QPointF(1, 1));
    }

    // removedAt ----------------------------------------------------------
    void removedAt_interior()
    {
        const QVector<QPointF> v{{0, 0}, {1, 1}, {2, 2}};
        const auto r = EditGeometry::removedAt(v, 1);
        QCOMPARE(r.size(), 2);
        QCOMPARE(r[0], QPointF(0, 0));
        QCOMPARE(r[1], QPointF(2, 2));
    }

    void removedAt_tooSmall_returnsCopy()
    {
        const QVector<QPointF> v{{0, 0}, {1, 1}};
        const auto r = EditGeometry::removedAt(v, 0);
        QCOMPARE(r, v); // size-2 input must not be trimmed
    }

    // distanceToPolyline -------------------------------------------------
    void distanceToPolyline_onSegment()
    {
        const QVector<QPointF> v{{0, 0}, {10, 0}};
        int seg = -1;
        QPointF pt;
        const double d = EditGeometry::distanceToPolyline(v, {5, 3}, &seg, &pt);
        QCOMPARE(d, 3.0);
        QCOMPARE(seg, 0);
        QCOMPARE(pt, QPointF(5, 0));
    }

    void distanceToPolyline_beyondEndpoint()
    {
        const QVector<QPointF> v{{0, 0}, {10, 0}};
        const double d = EditGeometry::distanceToPolyline(v, {15, 0});
        QCOMPARE(d, 5.0);
    }

    void distanceToPolyline_empty_returnsInfinity()
    {
        const double d = EditGeometry::distanceToPolyline({}, {0, 0});
        QVERIFY(std::isinf(d));
    }

    void distanceToPolyline_picksNearestSegment()
    {
        // L-shape: horizontal then vertical
        const QVector<QPointF> v{{0, 0}, {10, 0}, {10, 10}};
        int seg = -1;
        const double d = EditGeometry::distanceToPolyline(v, {12, 5}, &seg, nullptr);
        QCOMPARE(d, 2.0);
        QCOMPARE(seg, 1); // vertical segment is closer than the horizontal one
    }

    // cleanPolyline ------------------------------------------------------
    void cleanPolyline_emptyAndSingle_unchanged()
    {
        QCOMPARE(EditGeometry::cleanPolyline({}).size(), 0);
        const QVector<QPointF> one{{1, 1}};
        QCOMPARE(EditGeometry::cleanPolyline(one), one);
    }

    void cleanPolyline_nonDegenerate_preservedExactly()
    {
        const QVector<QPointF> v{{0, 0}, {1, 0}, {1, 1}, {0, 1}};
        QCOMPARE(EditGeometry::cleanPolyline(v), v);
    }

    void cleanPolyline_collapsesConsecutiveDuplicate()
    {
        const QVector<QPointF> v{{0, 0}, {1, 0}, {1, 0}, {2, 0}};
        const QVector<QPointF> expected{{0, 0}, {1, 0}, {2, 0}};
        QCOMPARE(EditGeometry::cleanPolyline(v), expected);
    }

    void cleanPolyline_collapsesRunOfThreeOrMore()
    {
        const QVector<QPointF> v{{0, 0}, {1, 0}, {1, 0}, {1, 0}, {2, 0}};
        const QVector<QPointF> expected{{0, 0}, {1, 0}, {2, 0}};
        QCOMPARE(EditGeometry::cleanPolyline(v), expected);
    }

    void cleanPolyline_nearCoincidentWithinTol_collapses()
    {
        // Second point is 1e-9 away — well inside the 1e-6 default tolerance.
        const QVector<QPointF> v{{0, 0}, {1e-9, 0}, {1, 0}};
        const QVector<QPointF> expected{{0, 0}, {1, 0}};
        QCOMPARE(EditGeometry::cleanPolyline(v), expected);
    }

    void cleanPolyline_justOutsideTol_kept()
    {
        // 1e-3 separation is far outside the default tolerance — keep both.
        const QVector<QPointF> v{{0, 0}, {1e-3, 0}, {1, 0}};
        QCOMPARE(EditGeometry::cleanPolyline(v), v);
    }

    void cleanPolyline_collapsesToSinglePoint()
    {
        // Whole input coincident → degenerate result the caller must guard on.
        const QVector<QPointF> v{{5, 5}, {5, 5}, {5, 5}};
        const auto r = EditGeometry::cleanPolyline(v);
        QCOMPARE(r.size(), 1);
        QVERIFY(r.size() < 2);
    }

    // cleanPolygonRing ---------------------------------------------------
    void cleanPolygonRing_dropsRedundantClosingPoint()
    {
        // Square with an explicit closing vertex repeating the first.
        const QVector<QPointF> v{{0, 0}, {1, 0}, {1, 1}, {0, 1}, {0, 0}};
        const QVector<QPointF> expected{{0, 0}, {1, 0}, {1, 1}, {0, 1}};
        QCOMPARE(EditGeometry::cleanPolygonRing(v), expected);
    }

    void cleanPolygonRing_openRing_preserved()
    {
        const QVector<QPointF> v{{0, 0}, {1, 0}, {1, 1}, {0, 1}};
        QCOMPARE(EditGeometry::cleanPolygonRing(v), v);
    }

    void cleanPolygonRing_collapsesAndDeCloses()
    {
        // Duplicate interior vertex AND an explicit closing point.
        const QVector<QPointF> v{{0, 0}, {1, 0}, {1, 0}, {1, 1}, {0, 0}};
        const QVector<QPointF> expected{{0, 0}, {1, 0}, {1, 1}};
        QCOMPARE(EditGeometry::cleanPolygonRing(v), expected);
    }

    void cleanPolygonRing_degenerate_belowThreeDistinct()
    {
        // Collapses to two distinct points — caller skips a <3 ring.
        const QVector<QPointF> v{{0, 0}, {0, 0}, {1, 1}, {0, 0}};
        const auto r = EditGeometry::cleanPolygonRing(v);
        QVERIFY(r.size() < 3);
    }

    // Composite: auto-length after endpoint move ------------------------
    void autoLength_recomputeAfterEndpointMove()
    {
        // Link polyline: [(0,0), (5,0), (10,0)] — length 10 before.
        // Move the first endpoint to (0,3). New length = √((5-0)²+(0-3)²)+5
        //                                             = √34 + 5 ≈ 10.8309519
        const QVector<QPointF> before{{0, 0}, {5, 0}, {10, 0}};
        QCOMPARE(EditGeometry::polylineLength(before), 10.0);

        const auto after = EditGeometry::replacedAt(before, 0, {0, 3});
        const double len = EditGeometry::polylineLength(after);
        QVERIFY(qAbs(len - (std::sqrt(34.0) + 5.0)) < 1e-9);
    }
};

QTEST_APPLESS_MAIN(TestEditGeometry)
#include "test_editgeometry.moc"
