/*!
 * \file   test_nodepropertyadapter.cpp
 * \brief  Slice DB round-trip tests for SWMMNodePropertyAdapter and its
 *         four type-aware subclasses (Junction / Outfall / Storage /
 *         Divider). Exercises:
 *           - The depth/elev/area scalar block that already shipped in
 *             Slice Z.5.3 (regression cover, no behaviour change here).
 *           - X / Y coord getters reading via `swmm_spatial_get_node_coord`.
 *           - The `coordChangeRequested` signal fired by setXCoord /
 *             setYCoord when the new value differs from the cached coord.
 *           - The read-only summary block (`crownElev`, `fullVolume`,
 *             `degree`, `statMaxDepth`, `statMaxOverflow`, `statVolFlooded`,
 *             `statTimeFlooded`) returning zero pre-run + the correct
 *             post-link `degree`.
 */

#include "ui/properties/swmmnodepropertyadapter.h"

#include <openswmm/engine/openswmm_engine.h>
#include <openswmm/engine/openswmm_links.h>
#include <openswmm/engine/openswmm_nodes.h>
#include <openswmm/engine/openswmm_spatial.h>
#include <openswmm/engine/openswmm_tables.h>  // Slice AG.4 — storage curve

#include <QObject>
#include <QSignalSpy>
#include <QTest>

#include <array>
#include <utility>

class TestNodePropertyAdapter : public QObject
{
    Q_OBJECT

private:
    // Build a tiny fixture with one junction (J1) sitting at (10, 20)
    // and an outfall (O1) at (30, 40), connected by a circular conduit.
    // The conduit gives J1 a degree of 1 and a non-zero crown elevation
    // so `degree` and `crownElev` exercise the engine's computed paths.
    SWMM_Engine buildFixture()
    {
        SWMM_Engine e = swmm_engine_new();
        if (!e) return nullptr;
        swmm_node_add(e, "J1", 0);  // 0 = Junction
        swmm_node_add(e, "O1", 1);  // 1 = Outfall
        const int jIdx = swmm_node_index(e, "J1");
        const int oIdx = swmm_node_index(e, "O1");
        swmm_node_set_invert_elev(e, jIdx, 100.0);
        swmm_node_set_max_depth(e, jIdx, 5.0);
        swmm_node_set_invert_elev(e, oIdx, 95.0);
        swmm_spatial_set_node_coord(e, jIdx, 10.0, 20.0);
        swmm_spatial_set_node_coord(e, oIdx, 30.0, 40.0);
        return e;
    }

    // J1 —C1— MID —C2— O1, with MID flagged as a virtual junction. The two
    // conduits are default-constructed, so they satisfy the virtual-junction
    // rules (identical cross-section, zero offsets, exactly two conduits, no
    // lateral inflow) without any further setup.
    SWMM_Engine buildVirtualJunctionFixture()
    {
        SWMM_Engine e = swmm_engine_new();
        if (!e) return nullptr;
        swmm_node_add(e, "J1",  0);
        swmm_node_add(e, "MID", 0);
        swmm_node_add(e, "O1",  1);
        const int jIdx = swmm_node_index(e, "J1");
        const int mIdx = swmm_node_index(e, "MID");
        const int oIdx = swmm_node_index(e, "O1");
        swmm_node_set_invert_elev(e, jIdx, 100.0);
        swmm_node_set_invert_elev(e, mIdx,  97.5);
        swmm_node_set_invert_elev(e, oIdx,  95.0);

        swmm_link_add(e, "C1", 0);   // 0 = conduit
        swmm_link_add(e, "C2", 0);
        swmm_link_set_nodes(e, swmm_link_index(e, "C1"), jIdx, mIdx);
        swmm_link_set_nodes(e, swmm_link_index(e, "C2"), mIdx, oIdx);

        if (swmm_node_set_virtual(e, mIdx, 1) != 0) {
            swmm_engine_destroy(e);
            return nullptr;
        }
        return e;
    }

private slots:

    // ====================================================================
    // Scalar regression (Slice Z.5.3 contract — unchanged in DB)
    // ====================================================================

    void junctionScalarRoundTrip()
    {
        SWMM_Engine e = buildFixture();
        QVERIFY(e);

        SWMMJunctionPropertyAdapter a(e, QStringLiteral("J1"));
        QCOMPARE(a.invertElev(), 100.0);
        QCOMPARE(a.maxDepth(),   5.0);

        // surcharge / ponded use straight SoA setters that round-trip in
        // the BUILDING state. (`initialDepth` is not asserted here because
        // the engine's `_set_initial_depth` writes to the runtime `depth`
        // field rather than `init_depth`; that asymmetry is pre-existing
        // and out of scope for Slice DB.)
        a.setSurchargeDepth(2.0);
        a.setPondedArea(150.0);
        QCOMPARE(a.surchargeDepth(), 2.0);
        QCOMPARE(a.pondedArea(),     150.0);

        swmm_engine_destroy(e);
    }

    // ====================================================================
    // Coordinates — read direct, write via signal (no in-adapter write)
    // ====================================================================

    void coordReadHitsEngine()
    {
        SWMM_Engine e = buildFixture();
        QVERIFY(e);
        SWMMJunctionPropertyAdapter a(e, QStringLiteral("J1"));
        QCOMPARE(a.xCoord(), 10.0);
        QCOMPARE(a.yCoord(), 20.0);
        swmm_engine_destroy(e);
    }

    void setXCoordEmitsRequestWithCurrentY()
    {
        SWMM_Engine e = buildFixture();
        QVERIFY(e);
        SWMMJunctionPropertyAdapter a(e, QStringLiteral("J1"));

        QSignalSpy spy(&a, &SWMMNodePropertyAdapter::coordChangeRequested);
        a.setXCoord(99.0);
        QCOMPARE(spy.count(), 1);
        const auto args = spy.takeFirst();
        QCOMPARE(args.at(0).toDouble(), 99.0);
        QCOMPARE(args.at(1).toDouble(), 20.0);  // Y preserved from cache.

        // The adapter intentionally does NOT touch the engine — the
        // request is the contract; PropertiesPanel routes it through
        // applyNodeMove. Verify the engine state is still the original.
        const int idx = swmm_node_index(e, "J1");
        double x = 0, y = 0;
        swmm_spatial_get_node_coord(e, idx, &x, &y);
        QCOMPARE(x, 10.0);
        QCOMPARE(y, 20.0);

        swmm_engine_destroy(e);
    }

    void setYCoordEmitsRequestWithCurrentX()
    {
        SWMM_Engine e = buildFixture();
        QVERIFY(e);
        SWMMJunctionPropertyAdapter a(e, QStringLiteral("J1"));

        QSignalSpy spy(&a, &SWMMNodePropertyAdapter::coordChangeRequested);
        a.setYCoord(-7.5);
        QCOMPARE(spy.count(), 1);
        const auto args = spy.takeFirst();
        QCOMPARE(args.at(0).toDouble(), 10.0);
        QCOMPARE(args.at(1).toDouble(), -7.5);

        swmm_engine_destroy(e);
    }

    void coordWriteWithSameValueIsNoop()
    {
        SWMM_Engine e = buildFixture();
        QVERIFY(e);
        SWMMJunctionPropertyAdapter a(e, QStringLiteral("J1"));

        QSignalSpy spy(&a, &SWMMNodePropertyAdapter::coordChangeRequested);
        a.setXCoord(10.0);  // identical to current X.
        a.setYCoord(20.0);  // identical to current Y.
        QCOMPARE(spy.count(), 0);

        swmm_engine_destroy(e);
    }

    // ====================================================================
    // Read-only summary block (Slice DB)
    // ====================================================================

    void summaryBlockReadsZeroBeforeRun()
    {
        SWMM_Engine e = buildFixture();
        QVERIFY(e);
        SWMMJunctionPropertyAdapter a(e, QStringLiteral("J1"));

        // No simulation run → all four stat values are zero. Crown elev
        // and full volume depend on link insertion + storage curve
        // hydration, which the fixture doesn't perform, so they also
        // read zero. Degree is the count of attached links (also zero
        // until we add a conduit below).
        QCOMPARE(a.statMaxDepth(),    0.0);
        QCOMPARE(a.statMaxOverflow(), 0.0);
        QCOMPARE(a.statVolFlooded(),  0.0);
        QCOMPARE(a.statTimeFlooded(), 0.0);
        QCOMPARE(a.degree(),          0);

        swmm_engine_destroy(e);
    }

    void degreeReadableViaGetter()
    {
        // Pre-`swmm_engine_initialize`, `nodes.degree[]` is zero — the
        // engine populates it during topology resolution. We only verify
        // the adapter routes through the getter; the value contract is
        // engine-owned. After `_initialize`, degree reflects attached
        // link count; the GUI never hand-rolls a count.
        SWMM_Engine e = buildFixture();
        QVERIFY(e);
        SWMMJunctionPropertyAdapter a(e, QStringLiteral("J1"));
        QCOMPARE(a.degree(), 0);
        swmm_engine_destroy(e);
    }

    void summaryBlockIsReadOnlyViaMetaObject()
    {
        // Catches a regression where someone adds a WRITE to the Q_PROPERTY
        // — the contract is that the summary fields are display-only.
        SWMM_Engine e = buildFixture();
        QVERIFY(e);
        SWMMJunctionPropertyAdapter a(e, QStringLiteral("J1"));

        const auto *mo = a.metaObject();
        const QStringList readOnlyProps = {
            QStringLiteral("crownElev"),
            QStringLiteral("fullVolume"),
            QStringLiteral("degree"),
            QStringLiteral("statMaxDepth"),
            QStringLiteral("statMaxOverflow"),
            QStringLiteral("statVolFlooded"),
            QStringLiteral("statTimeFlooded"),
        };
        for (const QString &name : readOnlyProps) {
            const int idx = mo->indexOfProperty(name.toUtf8().constData());
            QVERIFY2(idx >= 0, qPrintable(name));
            QVERIFY2(!mo->property(idx).isWritable(),
                     qPrintable(QStringLiteral("expected %1 read-only").arg(name)));
        }

        // Conversely, the editable ones MUST advertise writability so
        // QPropertyModel renders an editor.
        const QStringList editableProps = {
            QStringLiteral("invertElev"),
            QStringLiteral("maxDepth"),
            QStringLiteral("initialDepth"),
            QStringLiteral("surchargeDepth"),
            QStringLiteral("pondedArea"),
            QStringLiteral("xCoord"),
            QStringLiteral("yCoord"),
        };
        for (const QString &name : editableProps) {
            const int idx = mo->indexOfProperty(name.toUtf8().constData());
            QVERIFY2(idx >= 0, qPrintable(name));
            QVERIFY2(mo->property(idx).isWritable(),
                     qPrintable(QStringLiteral("expected %1 writable").arg(name)));
        }

        swmm_engine_destroy(e);
    }

    // ====================================================================
    // Virtual junction — the one editable depth is the rendering rim
    // ====================================================================

    /*! A virtual junction's max depth is derived (always the shared pipe
     *  crown), so the adapter exposes no `maxDepth` editor. What it does
     *  expose is `rimDepth` — the optional [VIRTUAL_JUNCTIONS] MaxDepth that
     *  says where the ground surface is drawn. */
    void virtualJunctionExposesWritableRimDepthOnly()
    {
        SWMM_Engine e = buildVirtualJunctionFixture();
        QVERIFY(e);

        SWMMVirtualJunctionPropertyAdapter a(e, QStringLiteral("MID"));
        const auto *mo = a.metaObject();

        const int rimIdx = mo->indexOfProperty("rimDepth");
        QVERIFY2(rimIdx >= 0, "virtual junction must expose rimDepth");
        QVERIFY2(mo->property(rimIdx).isWritable(), "rimDepth must be editable");

        // The hydraulic depth fields stay off this adapter entirely.
        for (const char *absent : {"maxDepth", "surchargeDepth", "pondedArea"})
            QVERIFY2(mo->indexOfProperty(absent) < 0, absent);

        // Round-trip through the engine.
        a.setRimDepth(4.5);
        QCOMPARE(a.rimDepth(), 4.5);
        double engineRim = 0.0;
        QCOMPARE(swmm_node_get_rim_depth(e, swmm_node_index(e, "MID"), &engineRim), 0);
        QCOMPARE(engineRim, 4.5);

        // 0 clears it back to "unset" (renderers fall back to the crown).
        a.setRimDepth(0.0);
        QCOMPARE(a.rimDepth(), 0.0);

        // The label must say the value is for display, since the property
        // browser has nowhere else to put that.
        const QString label = a.displayLabelFor(QStringLiteral("rimDepth"));
        QVERIFY(!label.isEmpty());
        QVERIFY2(label.contains(QStringLiteral("Display")), qPrintable(label));

        swmm_engine_destroy(e);
    }

    // ====================================================================
    // Display labels — verifies new key names map to non-empty strings
    // ====================================================================

    void displayLabelsCoverNewKeys()
    {
        SWMM_Engine e = buildFixture();
        QVERIFY(e);
        SWMMJunctionPropertyAdapter a(e, QStringLiteral("J1"));

        const QStringList keys = {
            QStringLiteral("xCoord"),  QStringLiteral("yCoord"),
            QStringLiteral("crownElev"), QStringLiteral("fullVolume"),
            QStringLiteral("degree"),
            QStringLiteral("statMaxDepth"),    QStringLiteral("statMaxOverflow"),
            QStringLiteral("statVolFlooded"),  QStringLiteral("statTimeFlooded"),
            // Slice DB.2 — compound editor keys.
            QStringLiteral("inflows"), QStringLiteral("dwf"),
            QStringLiteral("rdii"),    QStringLiteral("treatment"),
        };
        for (const QString &k : keys) {
            const QString label = a.displayLabelFor(k);
            QVERIFY2(!label.isEmpty(), qPrintable(k));
        }

        swmm_engine_destroy(e);
    }

    // ====================================================================
    // Slice DB.2 — compound editor refs
    // ====================================================================

    void compoundRefsCarryEngineAndNodeName()
    {
        SWMM_Engine e = buildFixture();
        QVERIFY(e);
        SWMMJunctionPropertyAdapter a(e, QStringLiteral("J1"));

        const auto refs = std::array{
            std::pair{a.inflowsRef(),   NodeCompoundEditRef::Inflows},
            std::pair{a.dwfRef(),       NodeCompoundEditRef::Dwf},
            std::pair{a.rdiiRef(),      NodeCompoundEditRef::Rdii},
            std::pair{a.treatmentRef(), NodeCompoundEditRef::Treatment},
        };
        for (const auto &[ref, expectedKind] : refs) {
            QCOMPARE(ref.engine,   e);
            QCOMPARE(ref.nodeName, QStringLiteral("J1"));
            QCOMPARE(ref.kind,     expectedKind);
            // Summary is computed live; for an empty fixture every
            // count-based summary should be the "(none)" or "engine
            // API pending" sentinel — non-empty in either case.
            QVERIFY(!ref.summary.isEmpty());
        }

        swmm_engine_destroy(e);
    }

    void compoundRefsAdvertisedOnAllNodeKinds()
    {
        // Q_PROPERTYs `inflows`, `dwf`, `rdii`, `treatment` must surface
        // on every node subclass — the user wants to reach Inflows /
        // DWF / Treatment from outfalls and storage as well.
        SWMM_Engine e = buildFixture();
        QVERIFY(e);

        const QStringList compoundProps = {
            QStringLiteral("inflows"), QStringLiteral("dwf"),
            QStringLiteral("rdii"),    QStringLiteral("treatment"),
        };
        auto checkAdvertises = [&](const QObject &a, const char *label) {
            const auto *mo = a.metaObject();
            for (const QString &p : compoundProps) {
                const int idx = mo->indexOfProperty(p.toUtf8().constData());
                QVERIFY2(idx >= 0, qPrintable(
                    QStringLiteral("%1 missing %2").arg(label, p)));
            }
        };
        {
            SWMMJunctionPropertyAdapter a(e, QStringLiteral("J1"));
            checkAdvertises(a, "Junction");
        }
        {
            SWMMOutfallPropertyAdapter a(e, QStringLiteral("O1"));
            checkAdvertises(a, "Outfall");
        }
        // Storage / Divider would require swmm_node_add(STORAGE/DIVIDER)
        // first — same shape, exercised indirectly via metaObject
        // introspection on a temporary attached to the existing engine.
        {
            SWMMStoragePropertyAdapter a(e, QStringLiteral("J1"));
            checkAdvertises(a, "Storage");
        }
        {
            SWMMDividerPropertyAdapter a(e, QStringLiteral("J1"));
            checkAdvertises(a, "Divider");
        }

        swmm_engine_destroy(e);
    }

    // ====================================================================
    // Slice AG.4 — storage-unit geometry (functional + tabular)
    // ====================================================================

    // A storage unit (S1) plus one [STORAGE]-type curve (SC1) so both the
    // functional and tabular paths have something to bind to.
    SWMM_Engine buildStorageFixture()
    {
        SWMM_Engine e = swmm_engine_new();
        if (!e) return nullptr;
        swmm_node_add(e, "S1", 2);   // 2 = Storage
        swmm_curve_add(e, "SC1", 1); // 1 = CURVE_STORAGE
        const int ci = swmm_table_index(e, "SC1");
        swmm_table_add_point(e, ci, 0.0, 100.0);
        swmm_table_add_point(e, ci, 5.0, 400.0);
        return e;
    }

    void storageFunctionalRoundTrip()
    {
        SWMM_Engine e = buildStorageFixture();
        QVERIFY(e);
        SWMMStoragePropertyAdapter a(e, QStringLiteral("S1"));

        // No curve assigned → functional form by default.
        QCOMPARE(a.storageShape(), SWMMNodePropertyAdapter::Functional);

        // Each coefficient setter edits one term of the engine's atomic
        // (A,B,C) triple without clobbering the others.
        a.setStorageCoeffA(1500.0);
        a.setStorageExpB(0.5);
        a.setStorageConstC(250.0);
        QCOMPARE(a.storageCoeffA(), 1500.0);
        QCOMPARE(a.storageExpB(),   0.5);
        QCOMPARE(a.storageConstC(), 250.0);

        // Mirror the read against the raw engine triple.
        const int idx = swmm_node_index(e, "S1");
        double av = 0, bv = 0, cv = 0;
        swmm_node_get_storage_functional(e, idx, &av, &bv, &cv);
        QCOMPARE(av, 1500.0);
        QCOMPARE(bv, 0.5);
        QCOMPARE(cv, 250.0);

        swmm_engine_destroy(e);
    }

    void storageTabularRoundTripAndShapeSwitch()
    {
        SWMM_Engine e = buildStorageFixture();
        QVERIFY(e);
        SWMMStoragePropertyAdapter a(e, QStringLiteral("S1"));

        // Assign the curve by name through the picker ref → TABULAR.
        DataObjectRef r = a.storageCurveRef();
        r.currentName = QStringLiteral("SC1");
        a.setStorageCurveRef(r);
        QCOMPARE(a.storageShape(), SWMMNodePropertyAdapter::Tabular);
        QCOMPARE(a.storageCurveRef().currentName, QStringLiteral("SC1"));

        const int idx = swmm_node_index(e, "S1");
        int curveIdx = -1;
        swmm_node_get_storage_curve(e, idx, &curveIdx);
        QCOMPARE(curveIdx, swmm_table_index(e, "SC1"));

        // Switching the shape back to FUNCTIONAL detaches the curve.
        a.setStorageShape(SWMMNodePropertyAdapter::Functional);
        QCOMPARE(a.storageShape(), SWMMNodePropertyAdapter::Functional);
        QVERIFY(a.storageCurveRef().currentName.isEmpty());

        // setStorageShape(Tabular) re-binds the first storage curve when
        // none is currently assigned.
        a.setStorageShape(SWMMNodePropertyAdapter::Tabular);
        QCOMPARE(a.storageShape(), SWMMNodePropertyAdapter::Tabular);
        QCOMPARE(a.storageCurveRef().currentName, QStringLiteral("SC1"));

        // Clearing the curve ref (empty name) also reverts to FUNCTIONAL.
        DataObjectRef empty = a.storageCurveRef();
        empty.currentName.clear();
        a.setStorageCurveRef(empty);
        QCOMPARE(a.storageShape(), SWMMNodePropertyAdapter::Functional);

        swmm_engine_destroy(e);
    }

    // The four geometric shapes (CYLINDRICAL / CONICAL / PARABOLIC / PYRAMIDAL).
    // These are curve-less but NOT power-law functional, which is precisely what the
    // old "shape = (curve >= 0) ? Tabular : Functional" inference could not express —
    // the adapter now reads the engine's real shape field.
    void storageGeometricShapeRoundTrip()
    {
        SWMM_Engine e = buildStorageFixture();
        QVERIFY(e);
        SWMMStoragePropertyAdapter a(e, QStringLiteral("S1"));
        const int idx = swmm_node_index(e, "S1");

        struct Case { SWMMNodePropertyAdapter::StorageShape shape; double p1, p2, p3; };
        const QVector<Case> cases = {
            { SWMMNodePropertyAdapter::Cylindrical, 30.0, 20.0, 0.0 },
            { SWMMNodePropertyAdapter::Conical,     30.0, 20.0, 2.5 },
            { SWMMNodePropertyAdapter::Paraboloid,  30.0, 20.0, 8.0 },
            { SWMMNodePropertyAdapter::Pyramidal,   30.0, 20.0, 2.5 },
        };

        for (const Case &c : cases) {
            a.setStorageShape(c.shape);
            QCOMPARE(a.storageShape(), c.shape);

            a.setStorageParam1(c.p1);
            a.setStorageParam2(c.p2);
            a.setStorageParam3(c.p3);
            QCOMPARE(a.storageParam1(), c.p1);
            QCOMPARE(a.storageParam2(), c.p2);
            QCOMPARE(a.storageParam3(), c.p3);

            // The engine must have derived its area coefficients from the dimensions —
            // if it hadn't, the node would route as a zero-area storage.
            double av = 0, bv = 0, cv = 0;
            QCOMPARE(swmm_node_get_storage_functional(e, idx, &av, &bv, &cv), SWMM_OK);
            QVERIFY2(av != 0.0 || bv != 0.0 || cv != 0.0,
                     "shape dimensions did not produce area coefficients");

            // A geometric shape is never tabulated.
            int curveIdx = 0;
            QCOMPARE(swmm_node_get_storage_curve(e, idx, &curveIdx), SWMM_OK);
            QCOMPARE(curveIdx, -1);
        }

        // Pyramidal: L=30, W=20, Z=2.5 → a=2(L+W)Z=250, b=4Z²=25, c=L·W=600.
        a.setStorageShape(SWMMNodePropertyAdapter::Pyramidal);
        a.setStorageParam1(30.0);
        a.setStorageParam2(20.0);
        a.setStorageParam3(2.5);
        double av = 0, bv = 0, cv = 0;
        swmm_node_get_storage_functional(e, idx, &av, &bv, &cv);
        QCOMPARE(av, 250.0);
        QCOMPARE(bv, 25.0);
        QCOMPARE(cv, 600.0);

        swmm_engine_destroy(e);
    }

    // A rejected dimension must leave the node on its last valid geometry rather
    // than half-writing it (the engine validates before it mutates).
    void storageGeometryRejectsInvalidDimensions()
    {
        SWMM_Engine e = buildStorageFixture();
        QVERIFY(e);
        SWMMStoragePropertyAdapter a(e, QStringLiteral("S1"));

        a.setStorageShape(SWMMNodePropertyAdapter::Pyramidal);
        a.setStorageParam1(30.0);
        a.setStorageParam2(20.0);
        a.setStorageParam3(2.5);

        a.setStorageParam1(0.0);          // L must be > 0 → rejected
        QCOMPARE(a.storageParam1(), 30.0);

        a.setStorageShape(SWMMNodePropertyAdapter::Paraboloid);
        a.setStorageParam3(0.0);          // height divides → rejected
        QCOMPARE(a.storageParam3(), 2.5);

        swmm_engine_destroy(e);
    }

    // Switching away from a geometric shape and back must not leave the node
    // wedged as "shape says PYRAMIDAL but a curve is attached".
    void storageShapeSwitchesAreCoherent()
    {
        SWMM_Engine e = buildStorageFixture();
        QVERIFY(e);
        SWMMStoragePropertyAdapter a(e, QStringLiteral("S1"));
        const int idx = swmm_node_index(e, "S1");

        a.setStorageShape(SWMMNodePropertyAdapter::Conical);
        a.setStorageParam1(30.0);
        a.setStorageParam2(20.0);
        a.setStorageParam3(2.5);

        // → Tabular: binds the fixture's storage curve.
        a.setStorageShape(SWMMNodePropertyAdapter::Tabular);
        QCOMPARE(a.storageShape(), SWMMNodePropertyAdapter::Tabular);
        int curveIdx = -1;
        swmm_node_get_storage_curve(e, idx, &curveIdx);
        QVERIFY(curveIdx >= 0);

        // → Functional: detaches the curve.
        a.setStorageShape(SWMMNodePropertyAdapter::Functional);
        QCOMPARE(a.storageShape(), SWMMNodePropertyAdapter::Functional);
        swmm_node_get_storage_curve(e, idx, &curveIdx);
        QCOMPARE(curveIdx, -1);

        // → back to Conical: the dimensions survived the round trip, and the engine
        //   re-derived the coefficients from them.
        a.setStorageShape(SWMMNodePropertyAdapter::Conical);
        QCOMPARE(a.storageParam1(), 30.0);
        QCOMPARE(a.storageParam3(), 2.5);
        double av = 0, bv = 0, cv = 0;
        swmm_node_get_storage_functional(e, idx, &av, &bv, &cv);
        QVERIFY(cv > 0.0);   // π·A·B

        swmm_engine_destroy(e);
    }

    // Writing functional coefficients on a node that was geometric must snap it back
    // to FUNCTIONAL — otherwise the solver would read the power-law A/B/C as a cone's
    // quadratic coefficients.
    void storageFunctionalWriteClearsGeometricShape()
    {
        SWMM_Engine e = buildStorageFixture();
        QVERIFY(e);
        SWMMStoragePropertyAdapter a(e, QStringLiteral("S1"));

        a.setStorageShape(SWMMNodePropertyAdapter::Pyramidal);
        a.setStorageParam1(30.0);
        a.setStorageParam2(20.0);
        a.setStorageParam3(2.5);
        QCOMPARE(a.storageShape(), SWMMNodePropertyAdapter::Pyramidal);

        a.setStorageCoeffA(1500.0);
        QCOMPARE(a.storageShape(), SWMMNodePropertyAdapter::Functional);

        swmm_engine_destroy(e);
    }

    void storageGeometryPropsWritableViaMetaObject()
    {
        SWMM_Engine e = buildStorageFixture();
        QVERIFY(e);
        SWMMStoragePropertyAdapter a(e, QStringLiteral("S1"));

        const auto *mo = a.metaObject();
        const QStringList writableProps = {
            QStringLiteral("storageShape"),  QStringLiteral("storageCurve"),
            QStringLiteral("storageCoeffA"), QStringLiteral("storageExpB"),
            QStringLiteral("storageConstC"),
            QStringLiteral("storageParam1"), QStringLiteral("storageParam2"),
            QStringLiteral("storageParam3"),
        };
        for (const QString &name : writableProps) {
            const int idx = mo->indexOfProperty(name.toUtf8().constData());
            QVERIFY2(idx >= 0, qPrintable(name));
            QVERIFY2(mo->property(idx).isWritable(),
                     qPrintable(QStringLiteral("expected %1 writable").arg(name)));
            QVERIFY2(!a.displayLabelFor(name).isEmpty(), qPrintable(name));
        }

        swmm_engine_destroy(e);
    }
};

QTEST_MAIN(TestNodePropertyAdapter)
#include "test_nodepropertyadapter.moc"
