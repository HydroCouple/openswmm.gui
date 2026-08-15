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

#include <algorithm>
#include <cmath>

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

    // signedRingArea -----------------------------------------------------
    void signedRingArea_ccwPositive_cwNegative()
    {
        const QVector<QPointF> ccw{{0, 0}, {10, 0}, {10, 10}, {0, 10}};
        QVERIFY(EditGeometry::signedRingArea(ccw) > 0.0);
        QCOMPARE(EditGeometry::signedRingArea(ccw), 100.0);
        QVector<QPointF> cw = ccw;
        std::reverse(cw.begin(), cw.end());
        QVERIFY(EditGeometry::signedRingArea(cw) < 0.0);
    }

    void signedRingArea_degenerate_isZero()
    {
        QCOMPARE(EditGeometry::signedRingArea({{0, 0}, {1, 1}}), 0.0);
    }

    // pointInRing --------------------------------------------------------
    void pointInRing_insideAndOutside()
    {
        const QVector<QPointF> sq{{0, 0}, {10, 0}, {10, 10}, {0, 10}};
        QVERIFY(EditGeometry::pointInRing(sq, {5, 5}));
        QVERIFY(!EditGeometry::pointInRing(sq, {15, 5}));
        QVERIFY(!EditGeometry::pointInRing(sq, {-1, 5}));
    }

    // interiorPoint — the robust hole seed --------------------------------
    void interiorPoint_convexSquare_isInside()
    {
        const QVector<QPointF> sq{{0, 0}, {10, 0}, {10, 10}, {0, 10}};
        const QPointF p = EditGeometry::interiorPoint(sq);
        QVERIFY(EditGeometry::pointInRing(sq, p));
    }

    void interiorPoint_lShape_isInside_whereCentroidIsNot()
    {
        // L-shape whose VERTEX CENTROID (~1.67,1.67) lies OUTSIDE the ring —
        // the exact case the old mesh centroid-seed got wrong.
        const QVector<QPointF> l{{0, 0}, {4, 0}, {4, 1}, {1, 1}, {1, 4}, {0, 4}};
        // Demonstrate the centroid is outside.
        QPointF c(0, 0);
        for (const QPointF &v : l) c += v;
        c /= double(l.size());
        QVERIFY(!EditGeometry::pointInRing(l, c));
        // interiorPoint must nevertheless be strictly inside.
        const QPointF p = EditGeometry::interiorPoint(l);
        QVERIFY(EditGeometry::pointInRing(l, p));
    }

    void interiorPoint_cShape_isInside()
    {
        const QVector<QPointF> cs{{0, 0}, {3, 0}, {3, 1}, {1, 1},
                                  {1, 2}, {3, 2}, {3, 3}, {0, 3}};
        const QPointF p = EditGeometry::interiorPoint(cs);
        QVERIFY(EditGeometry::pointInRing(cs, p));
    }

    // netArea / containsPoint (donut) ------------------------------------
    void ringPolygon_donut_areaAndContainment()
    {
        EditGeometry::RingPolygon rp;
        rp.exterior = {{0, 0}, {10, 0}, {10, 10}, {0, 10}};
        rp.interiors.append(QVector<QPointF>{{3, 3}, {7, 3}, {7, 7}, {3, 7}});

        QCOMPARE(EditGeometry::netArea(rp), 100.0 - 16.0);
        QVERIFY(EditGeometry::containsPoint(rp, {1, 1}));   // in the ring body
        QVERIFY(!EditGeometry::containsPoint(rp, {5, 5}));  // in the hole
        QVERIFY(!EditGeometry::containsPoint(rp, {15, 5})); // outside exterior
    }

    // validateRingPolygon ------------------------------------------------
    void validate_ok_donut()
    {
        EditGeometry::RingPolygon rp;
        rp.exterior = {{0, 0}, {10, 0}, {10, 10}, {0, 10}};
        rp.interiors.append(QVector<QPointF>{{3, 3}, {7, 3}, {7, 7}, {3, 7}});
        QCOMPARE(EditGeometry::validateRingPolygon(rp),
                 EditGeometry::RingValidity::Ok);
    }

    void validate_selfIntersectingExterior()
    {
        EditGeometry::RingPolygon rp;
        rp.exterior = {{0, 0}, {2, 2}, {2, 0}, {0, 2}};  // bow-tie
        QCOMPARE(EditGeometry::validateRingPolygon(rp),
                 EditGeometry::RingValidity::SelfIntersecting);
    }

    void validate_holeOutsideExterior()
    {
        EditGeometry::RingPolygon rp;
        rp.exterior = {{0, 0}, {2, 0}, {2, 2}, {0, 2}};
        rp.interiors.append(QVector<QPointF>{{5, 5}, {6, 5}, {6, 6}, {5, 6}});
        QCOMPARE(EditGeometry::validateRingPolygon(rp),
                 EditGeometry::RingValidity::HoleOutsideExterior);
    }

    void validate_holesOverlap()
    {
        EditGeometry::RingPolygon rp;
        rp.exterior = {{0, 0}, {20, 0}, {20, 20}, {0, 20}};
        rp.interiors.append(QVector<QPointF>{{2, 2}, {8, 2}, {8, 8}, {2, 8}});
        rp.interiors.append(QVector<QPointF>{{5, 5}, {11, 5}, {11, 11}, {5, 11}});
        QCOMPARE(EditGeometry::validateRingPolygon(rp),
                 EditGeometry::RingValidity::HolesOverlap);
    }

    // normalizeRingPolygon -----------------------------------------------
    void normalize_orientsExteriorCcwAndHolesCw()
    {
        EditGeometry::RingPolygon rp;
        rp.exterior = {{0, 0}, {0, 10}, {10, 10}, {10, 0}};        // CW input
        rp.interiors.append(QVector<QPointF>{{3, 3}, {7, 3}, {7, 7}, {3, 7}}); // CCW

        const EditGeometry::RingPolygon n = EditGeometry::normalizeRingPolygon(rp);
        QVERIFY(EditGeometry::signedRingArea(n.exterior) > 0.0);   // CCW
        QCOMPARE(n.interiors.size(), qsizetype(1));
        QVERIFY(EditGeometry::signedRingArea(n.interiors[0]) < 0.0); // CW
    }

    void normalize_dropsDegenerateHoles()
    {
        EditGeometry::RingPolygon rp;
        rp.exterior = {{0, 0}, {10, 0}, {10, 10}, {0, 10}};
        rp.interiors.append(QVector<QPointF>{{3, 3}, {3, 3}, {3, 3}});  // collapses
        const EditGeometry::RingPolygon n = EditGeometry::normalizeRingPolygon(rp);
        QCOMPARE(n.interiors.size(), qsizetype(0));
    }
};

QTEST_APPLESS_MAIN(TestEditGeometry)
#include "test_editgeometry.moc"
