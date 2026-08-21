/*!
 * \file   test_xsectgeominline.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * Inline cross-section geom1..geom4 — the direct-edit fields surfaced
 * alongside the compound XSection dialog in both the Property Browser and
 * the Attribute Table. Covers:
 *   - the shared shape/geom helpers (openswmmvis::xsectGeomApplies /
 *     xsectGeomLabel drive the per-shape LABELS and tooltips;
 *     xsectGeomIsPickerIndex is the separate, narrower test for which
 *     slots an inline numeric editor may write);
 *   - the SWMMLinkPropertyAdapter geom getters/setters: read-modify-write
 *     of the engine xsect tuple (shape + siblings preserved), writable for
 *     every shape, and the picker-index guard that is the sole exception;
 *   - the metaobject contract (geom1..4 are writable, NOTIFY-ing
 *     Q_PROPERTYs on Conduit / Orifice / Weir, absent on Pump / Outlet).
 *
 * Standalone (mirrors test_subcatchpropertyadapter's lean link strategy):
 * pulls in only the link adapter + UnitSystem + the small User-Flags
 * surface, with SWMMModelLayer::ensureUserFlagsModel resolved by a link
 * stub. The companion test_linkpropertyadapter.cpp (which exercises the
 * full LinkCompoundEditDialog) is disabled pending a transect-picker
 * dependency split, so the inline-geom coverage lives here where it runs.
 */

#include "ui/properties/swmmlinkpropertyadapter.h"
#include "ui/properties/xsectshapegeom.h"

#include <openswmm/engine/openswmm_engine.h>
#include <openswmm/engine/openswmm_infrastructure.h>
#include <openswmm/engine/openswmm_links.h>
#include <openswmm/engine/openswmm_nodes.h>

#include <QObject>
#include <QSignalSpy>
#include <QStringList>
#include <QTest>

namespace {

// J1→J2 with a single link of the requested type (0=Conduit, 1=Pump,
// 2=Orifice, 3=Weir, 4=Outlet). The engine seeds a CIRCULAR section.
SWMM_Engine buildLinkFixture(const char *id, int linkType)
{
    SWMM_Engine e = swmm_engine_new();
    if (!e) return nullptr;
    swmm_node_add(e, "J1", 0);
    swmm_node_add(e, "J2", 0);
    swmm_link_add(e, id, linkType);
    const int li = swmm_link_index(e, id);
    swmm_link_set_nodes(e, li, swmm_node_index(e, "J1"), swmm_node_index(e, "J2"));
    return e;
}

} // namespace

class TestXsectGeomInline : public QObject
{
    Q_OBJECT

private slots:

    // -- Shared applicability helper ------------------------------------

    void applicabilityHelper()
    {
        using namespace openswmmvis;
        // CIRCULAR (0): only geom1 (Diameter).
        QVERIFY( xsectGeomApplies(0, 1));
        QVERIFY(!xsectGeomApplies(0, 2));
        QVERIFY(!xsectGeomApplies(0, 3));
        QVERIFY(!xsectGeomApplies(0, 4));
        // RECT_CLOSED (2): geom1 + geom2.
        QVERIFY( xsectGeomApplies(2, 1));
        QVERIFY( xsectGeomApplies(2, 2));
        QVERIFY(!xsectGeomApplies(2, 3));
        // TRAPEZOIDAL (4): all four (depth, bottom width, two slopes).
        for (int k = 1; k <= 4; ++k) QVERIFY(xsectGeomApplies(4, k));
        // IRREGULAR / STREET carry no named dimensions at all — geom1 is a
        // list index and the rest are unused, so nothing gets a label.
        for (int k = 1; k <= 4; ++k) {
            QVERIFY(!xsectGeomApplies(SWMM_XSECT_IRREGULAR, k));
            QVERIFY(!xsectGeomApplies(SWMM_XSECT_STREET, k));
        }
        // Shape-specific labels back the per-row/cell tooltips.
        QCOMPARE(xsectGeomLabel(0, 1), QStringLiteral("Diameter"));
        QCOMPARE(xsectGeomLabel(2, 2), QStringLiteral("Width"));
        QVERIFY(xsectGeomLabel(0, 2).isEmpty());
        // Out-of-range ordinal is empty / not applicable.
        QVERIFY(xsectGeomLabel(4, 5).isEmpty());
        QVERIFY(!xsectGeomApplies(4, 0));
    }

    // -- Adapter read-modify-write round-trip ---------------------------

    void roundTripPreservesShapeAndSiblings()
    {
        SWMM_Engine e = buildLinkFixture("C1", /*Conduit=*/0);
        QVERIFY(e);
        const int idx = swmm_link_index(e, "C1");
        QVERIFY(idx >= 0);
        // Seed RECT_CLOSED (shape 2): geom1=4.0 (depth), geom2=2.5 (width).
        QCOMPARE(swmm_link_set_xsect(e, idx, /*RECT_CLOSED=*/2, 4.0, 2.5, 0, 0),
                 SWMM_OK);

        SWMMConduitPropertyAdapter a(e, QStringLiteral("C1"));
        QCOMPARE(a.xsectShapeId(), 2);
        QCOMPARE(a.xsectGeom1(), 4.0);
        QCOMPARE(a.xsectGeom2(), 2.5);

        // Edit geom1 — shape + geom2 must survive the read-modify-write.
        QSignalSpy spy(&a, &SWMMLinkPropertyAdapter::changed);
        a.setXsectGeom1(5.0);
        QCOMPARE(spy.count(), 1);
        int shape = -1; double g1 = 0, g2 = 0, g3 = 0, g4 = 0;
        QCOMPARE(swmm_link_get_xsect(e, idx, &shape, &g1, &g2, &g3, &g4), SWMM_OK);
        QCOMPARE(shape, 2);
        QCOMPARE(g1, 5.0);
        QCOMPARE(g2, 2.5);   // sibling preserved

        // Edit geom2 likewise.
        a.setXsectGeom2(3.0);
        QCOMPARE(a.xsectGeom2(), 3.0);
        QCOMPARE(a.xsectGeom1(), 5.0);   // sibling preserved
        QCOMPARE(a.xsectShapeId(), 2);

        swmm_engine_destroy(e);
    }

    //! A geom the current shape doesn't consume is still a real stored
    //! number, so the write goes through and the value survives. This used
    //! to be a no-op, which meant a CIRCULAR conduit offered three dead,
    //! permanently blank geom fields and silently discarded a width typed
    //! before switching to RECT_CLOSED.
    void geomWritableEvenWhenShapeDoesNotUseIt()
    {
        SWMM_Engine e = buildLinkFixture("C1", /*Conduit=*/0);
        QVERIFY(e);
        const int idx = swmm_link_index(e, "C1");
        // CIRCULAR (shape 0): only geom1 (Diameter) is *used* by the solver.
        QCOMPARE(swmm_link_set_xsect(e, idx, /*CIRCULAR=*/0, 2.0, 0, 0, 0), SWMM_OK);

        SWMMConduitPropertyAdapter a(e, QStringLiteral("C1"));
        QSignalSpy spy(&a, &SWMMLinkPropertyAdapter::changed);

        // geom2 is unused by CIRCULAR but still writable + readable back.
        a.setXsectGeom2(9.0);
        QCOMPARE(spy.count(), 1);
        QCOMPARE(a.xsectGeom2(), 9.0);
        QCOMPARE(a.xsectShapeId(), 0);   // shape must not move

        // geom1 likewise.
        a.setXsectGeom1(3.5);
        QCOMPARE(spy.count(), 2);
        QCOMPARE(a.xsectGeom1(), 3.5);
        QCOMPARE(a.xsectGeom2(), 9.0);   // sibling preserved

        // Switching to RECT_CLOSED now finds the width already there —
        // the point of keeping the stored value.
        QCOMPARE(swmm_link_set_xsect(e, idx, /*RECT_CLOSED=*/2, 3.5, 9.0, 0, 0),
                 SWMM_OK);
        QCOMPARE(a.xsectGeom2(), 9.0);

        swmm_engine_destroy(e);
    }

    //! The one slot that must still refuse an inline numeric write: for
    //! IRREGULAR (and STREET) geom1 holds a transect-list INDEX, so a raw
    //! number would silently re-point the section at another transect.
    //! Its siblings are plain unused storage and stay writable.
    void pickerIndexGeomRejectsInlineWrite()
    {
        SWMM_Engine e = buildLinkFixture("C1", /*Conduit=*/0);
        QVERIFY(e);
        const int idx = swmm_link_index(e, "C1");
        // set_xsect(IRREGULAR, index) now validates and BINDS the transect
        // (it used to store the index as a depth and leave the reference
        // dangling — the .inp save corruption family), so the fixture needs a
        // real transect for index 0 to name.
        QCOMPARE(swmm_transect_add(e, "T1"), SWMM_OK);
        const int ti = swmm_transect_index(e, "T1");
        QCOMPARE(swmm_transect_set_roughness(e, ti, 0.04, 0.04, 0.03), SWMM_OK);
        QCOMPARE(swmm_transect_add_station(e, ti, 0.0, 10.0), SWMM_OK);
        QCOMPARE(swmm_transect_add_station(e, ti, 5.0, 5.0), SWMM_OK);
        QCOMPARE(swmm_transect_add_station(e, ti, 10.0, 10.0), SWMM_OK);
        QCOMPARE(swmm_link_set_xsect(e, idx, SWMM_XSECT_IRREGULAR, 0, 0, 0, 0),
                 SWMM_OK);

        SWMMConduitPropertyAdapter a(e, QStringLiteral("C1"));
        QSignalSpy spy(&a, &SWMMLinkPropertyAdapter::changed);
        a.setXsectGeom1(7.0);
        QCOMPARE(spy.count(), 0);
        QCOMPARE(a.xsectGeom1(), 0.0);

        // geom2 on IRREGULAR is not an index — no reason to lock it.
        a.setXsectGeom2(1.25);
        QCOMPARE(spy.count(), 1);
        QCOMPARE(a.xsectGeom2(), 1.25);

        swmm_engine_destroy(e);
    }

    //! CUSTOM is the mixed case: geom1 is a real depth, geom2 is a
    //! shape-curve index.
    void customShapeLocksOnlyItsCurveIndex()
    {
        using namespace openswmmvis;
        QVERIFY(!xsectGeomIsPickerIndex(SWMM_XSECT_CUSTOM, 1));
        QVERIFY( xsectGeomIsPickerIndex(SWMM_XSECT_CUSTOM, 2));
        QVERIFY( xsectGeomIsPickerIndex(SWMM_XSECT_IRREGULAR, 1));
        QVERIFY( xsectGeomIsPickerIndex(SWMM_XSECT_STREET, 1));
        // Everything else is a plain stored dimension.
        QVERIFY(!xsectGeomIsPickerIndex(SWMM_XSECT_CIRCULAR, 1));
        QVERIFY(!xsectGeomIsPickerIndex(SWMM_XSECT_CIRCULAR, 2));
        QVERIFY(!xsectGeomIsPickerIndex(SWMM_XSECT_IRREGULAR, 2));
    }

    // -- Metaobject contract --------------------------------------------

    void geomsWritableOnXsectionKindsOnly()
    {
        const QStringList geoms = {
            QStringLiteral("geom1"), QStringLiteral("geom2"),
            QStringLiteral("geom3"), QStringLiteral("geom4"),
        };
        // Conduit / Orifice / Weir carry the inline geom rows…
        {
            SWMM_Engine e = buildLinkFixture("C1", /*Conduit=*/0);
            SWMMConduitPropertyAdapter a(e, QStringLiteral("C1"));
            const auto *mo = a.metaObject();
            for (const QString &p : geoms) {
                const int i = mo->indexOfProperty(p.toUtf8().constData());
                QVERIFY2(i >= 0,
                         qPrintable(QStringLiteral("conduit missing %1").arg(p)));
                QVERIFY(mo->property(i).isWritable());
                QVERIFY(mo->property(i).hasNotifySignal());
            }
            QVERIFY(!a.displayLabelFor(QStringLiteral("geom1")).isEmpty());
            swmm_engine_destroy(e);
        }
        {
            SWMM_Engine e = buildLinkFixture("L1", /*Orifice=*/2);
            SWMMOrificePropertyAdapter a(e, QStringLiteral("L1"));
            for (const QString &p : geoms)
                QVERIFY(a.metaObject()->indexOfProperty(p.toUtf8().constData()) >= 0);
            swmm_engine_destroy(e);
        }
        {
            SWMM_Engine e = buildLinkFixture("L1", /*Weir=*/3);
            SWMMWeirPropertyAdapter a(e, QStringLiteral("L1"));
            for (const QString &p : geoms)
                QVERIFY(a.metaObject()->indexOfProperty(p.toUtf8().constData()) >= 0);
            swmm_engine_destroy(e);
        }
        // …but Pump / Outlet (no Shape row) must NOT.
        {
            SWMM_Engine e = buildLinkFixture("L1", /*Pump=*/1);
            SWMMPumpPropertyAdapter a(e, QStringLiteral("L1"));
            for (const QString &p : geoms)
                QCOMPARE(a.metaObject()->indexOfProperty(p.toUtf8().constData()), -1);
            swmm_engine_destroy(e);
        }
        {
            SWMM_Engine e = buildLinkFixture("L1", /*Outlet=*/4);
            SWMMOutletPropertyAdapter a(e, QStringLiteral("L1"));
            for (const QString &p : geoms)
                QCOMPARE(a.metaObject()->indexOfProperty(p.toUtf8().constData()), -1);
            swmm_engine_destroy(e);
        }
    }
};

QTEST_MAIN(TestXsectGeomInline)
#include "test_xsectgeominline.moc"
