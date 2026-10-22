/**
 * @file      test_linkpropertyadapter.cpp
 * @brief     Slice SA round-trip tests for SWMMLinkPropertyAdapter and its
 *         five type-aware subclasses (Conduit / Pump / Orifice / Weir /
 *         Outlet). Exercises:
 *           - The `tag` getter+setter round-trip via the base-class
 *             `swmm_link_get_tag` / `_set_tag` accessors.
 *           - That every subclass advertises `tag` as a writable
 *             Q_PROPERTY (mirror of the node-side test in
 *             `test_nodepropertyadapter.cpp::summaryBlockIsReadOnlyViaMetaObject`).
 *           - That `displayLabelFor("tag")` resolves to a non-empty
 *             localized string.
 *           - That setting the same tag is idempotent w.r.t. the engine
 *             store (no spurious `changed()` emissions are expected — but
 *             we only assert the engine state, not the signal count, to
 *             match the existing adapter contract).
 *
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

#include "ui/properties/swmmlinkpropertyadapter.h"
#include "ui/properties/culvertcodes.h" // ATTRIBUTE_EDITOR_WIRING Phase 0
#include "ui/dialogs/linkcompoundeditdialog.h"

#include <openswmm/engine/openswmm_engine.h>
#include <openswmm/engine/openswmm_links.h>
#include <openswmm/engine/openswmm_nodes.h>

#include <QListWidget>
#include <QListWidgetItem>
#include <QObject>
#include <QSignalSpy>
#include <QSplitter>
#include <QTest>

#include <array>

namespace
{

// Tiny fixture: two junctions + one circular conduit between them. Every
// link kind that exists in this fixture (just the conduit) gets a tag,
// then the adapter is asked to read it back.
/**
 *
 * @brief Allocate the editing-only two-junction/one-conduit fixture.
 * @return Owned engine to destroy, or nullptr on allocation failure; object-add/
 * node-assignment statuses are unchecked. C1 connects J1 to J2 with engine defaults,
 * without explicit diameter, topology initialization, IO or simulation.
 *
 */
SWMM_Engine buildConduitFixture()
{
    SWMM_Engine e = swmm_engine_new();
    if (!e)
        return nullptr;
    swmm_node_add(e, "J1", 0); // Junction
    swmm_node_add(e, "J2", 0);
    swmm_link_add(e, "C1", 0); // 0 = Conduit
    const int li = swmm_link_index(e, "C1");
    const int j1 = swmm_node_index(e, "J1");
    const int j2 = swmm_node_index(e, "J2");
    swmm_link_set_nodes(e, li, j1, j2);
    return e;
}

// One-link fixture for any link kind. Returns the engine after creating
// L1 of the requested type connected to J1→J2. Pumps / orifices / weirs
// / outlets reuse the same shape so the per-kind adapter classes can be
// constructed against the same handle.
/**
 *
 * @brief Allocate an editing-only two-junction fixture with L1 of the requested type.
 * @param linkType Engine link enum, normally zero..four for conduit/pump/orifice/weir/outlet.
 * @return Owned engine to destroy, or nullptr on allocation failure; add/type/
 * endpoint statuses are unchecked, so a handle alone does not prove valid setup.
 * No geometry beyond defaults, input file or simulation is created.
 *
 */
SWMM_Engine buildLinkFixture(int linkType)
{
    SWMM_Engine e = swmm_engine_new();
    if (!e)
        return nullptr;
    swmm_node_add(e, "J1", 0);
    swmm_node_add(e, "J2", 0);
    swmm_link_add(e, "L1", linkType);
    const int li = swmm_link_index(e, "L1");
    const int j1 = swmm_node_index(e, "J1");
    const int j2 = swmm_node_index(e, "J2");
    swmm_link_set_nodes(e, li, j1, j2);
    return e;
}

} // namespace

class TestLinkPropertyAdapter : public QObject
{
    Q_OBJECT

private slots:

    // ====================================================================
    // Slice SA — tag round-trip on each link kind
    // ====================================================================

    /**
     *
     * @brief Require conduit tag set/overwrite/clear to round-trip and the first real change to
     * emit once.
     *
     */
    void conduitTagRoundTrip()
    {
        SWMM_Engine e = buildConduitFixture();
        QVERIFY(e);

        SWMMConduitPropertyAdapter a(e, QStringLiteral("C1"));
        QCOMPARE(a.tag(), QString{});

        QSignalSpy spy(&a, &SWMMLinkPropertyAdapter::changed);
        a.setTag(QStringLiteral("trunk-main"));
        QCOMPARE(spy.count(), 1);
        QCOMPARE(a.tag(), QStringLiteral("trunk-main"));

        // Overwriting with a different value re-reads the new string.
        a.setTag(QStringLiteral("storm-bypass"));
        QCOMPARE(a.tag(), QStringLiteral("storm-bypass"));

        // Clearing via empty string round-trips to empty.
        a.setTag(QString{});
        QCOMPARE(a.tag(), QString{});

        swmm_engine_destroy(e);
    }

    /**
     *
     * @brief Require a pump adapter tag to round-trip through its real engine link.
     *
     */
    void pumpTagRoundTrip()
    {
        SWMM_Engine e = buildLinkFixture(/*Pump=*/1);
        QVERIFY(e);

        SWMMPumpPropertyAdapter a(e, QStringLiteral("L1"));
        a.setTag(QStringLiteral("lift-1"));
        QCOMPARE(a.tag(), QStringLiteral("lift-1"));

        swmm_engine_destroy(e);
    }

    /**
     *
     * @brief Require an orifice adapter tag to round-trip through its real engine link.
     *
     */
    void orificeTagRoundTrip()
    {
        SWMM_Engine e = buildLinkFixture(/*Orifice=*/2);
        QVERIFY(e);

        SWMMOrificePropertyAdapter a(e, QStringLiteral("L1"));
        a.setTag(QStringLiteral("bottom-orif"));
        QCOMPARE(a.tag(), QStringLiteral("bottom-orif"));

        swmm_engine_destroy(e);
    }

    /**
     *
     * @brief Require a weir adapter tag to round-trip through its real engine link.
     *
     */
    void weirTagRoundTrip()
    {
        SWMM_Engine e = buildLinkFixture(/*Weir=*/3);
        QVERIFY(e);

        SWMMWeirPropertyAdapter a(e, QStringLiteral("L1"));
        a.setTag(QStringLiteral("trans-weir"));
        QCOMPARE(a.tag(), QStringLiteral("trans-weir"));

        swmm_engine_destroy(e);
    }

    /**
     *
     * @brief Require an outlet adapter tag to round-trip through its real engine link.
     *
     */
    void outletTagRoundTrip()
    {
        SWMM_Engine e = buildLinkFixture(/*Outlet=*/4);
        QVERIFY(e);

        SWMMOutletPropertyAdapter a(e, QStringLiteral("L1"));
        a.setTag(QStringLiteral("free-outlet"));
        QCOMPARE(a.tag(), QStringLiteral("free-outlet"));

        swmm_engine_destroy(e);
    }

    // ====================================================================
    // Slice SA — meta-object contract: tag is writable on all link kinds
    // ====================================================================

    /**
     *
     * @brief Require all five subclass metaobjects to expose writable tag properties.
     * The local checker borrows an adapter QObject and diagnostic C-string label and
     * records QtTest failures locally. All adapters use C1 for metadata inspection,
     * without constructing each corresponding engine link kind in this case.
     *
     */
    void tagAdvertisedOnAllLinkKinds()
    {
        SWMM_Engine e = buildConduitFixture();
        QVERIFY(e);

        auto checkWritableTag = [&](const QObject &a, const char *label) {
            const auto *mo = a.metaObject();
            const int idx = mo->indexOfProperty("tag");
            QVERIFY2(idx >= 0, qPrintable(QStringLiteral("%1 missing tag").arg(label)));
            QVERIFY2(mo->property(idx).isWritable(),
                     qPrintable(QStringLiteral("expected %1.tag writable").arg(label)));
        };

        // All five subclasses share the same base header; metaObject
        // introspection works against the conduit fixture's engine
        // handle without needing actual per-kind engine links.
        {
            SWMMConduitPropertyAdapter a(e, QStringLiteral("C1"));
            checkWritableTag(a, "Conduit");
        }
        {
            SWMMPumpPropertyAdapter a(e, QStringLiteral("C1"));
            checkWritableTag(a, "Pump");
        }
        {
            SWMMOrificePropertyAdapter a(e, QStringLiteral("C1"));
            checkWritableTag(a, "Orifice");
        }
        {
            SWMMWeirPropertyAdapter a(e, QStringLiteral("C1"));
            checkWritableTag(a, "Weir");
        }
        {
            SWMMOutletPropertyAdapter a(e, QStringLiteral("C1"));
            checkWritableTag(a, "Outlet");
        }

        swmm_engine_destroy(e);
    }

    // ====================================================================
    // Slice SA — display label resolves
    // ====================================================================

    /**
     *
     * @brief Require the conduit tag display label to be nonempty.
     *
     */
    void tagDisplayLabelResolves()
    {
        SWMM_Engine e = buildConduitFixture();
        QVERIFY(e);

        SWMMConduitPropertyAdapter a(e, QStringLiteral("C1"));
        const QString label = a.displayLabelFor(QStringLiteral("tag"));
        QVERIFY(!label.isEmpty());

        swmm_engine_destroy(e);
    }

    // ====================================================================
    // Slice SB — conduit scalar parity round-trip
    // ====================================================================

    /**
     *
     * @brief Require conduit initial flow to default zero and round-trip 2.5 in the engine
     * authored flow units.
     *
     */
    void initialFlowRoundTrips()
    {
        SWMM_Engine e = buildConduitFixture();
        QVERIFY(e);
        SWMMConduitPropertyAdapter a(e, QStringLiteral("C1"));
        QCOMPARE(a.initialFlow(), 0.0);
        a.setInitialFlow(2.5);
        QCOMPARE(a.initialFlow(), 2.5);
        swmm_engine_destroy(e);
    }

    /**
     *
     * @brief Require conduit maximum flow to default zero and round-trip 150 in authored flow
     * units.
     *
     */
    void maxFlowRoundTrips()
    {
        SWMM_Engine e = buildConduitFixture();
        QVERIFY(e);
        SWMMConduitPropertyAdapter a(e, QStringLiteral("C1"));
        QCOMPARE(a.maxFlow(), 0.0);
        a.setMaxFlow(150.0);
        QCOMPARE(a.maxFlow(), 150.0);
        swmm_engine_destroy(e);
    }

    /**
     *
     * @brief Require conduit seepage rate 0.05 to round-trip without GUI unit conversion.
     *
     */
    void seepRateRoundTrips()
    {
        SWMM_Engine e = buildConduitFixture();
        QVERIFY(e);
        SWMMConduitPropertyAdapter a(e, QStringLiteral("C1"));
        a.setSeepRate(0.05);
        QCOMPARE(a.seepRate(), 0.05);
        swmm_engine_destroy(e);
    }

    /**
     *
     * @brief Require barrel count to default one, accept three and refuse zero/negative values
     * without changing it.
     *
     */
    void barrelsRoundTrips()
    {
        SWMM_Engine e = buildConduitFixture();
        QVERIFY(e);
        SWMMConduitPropertyAdapter a(e, QStringLiteral("C1"));
        // Engine seeds barrels at 1 on link_add; verify default + write.
        QCOMPARE(a.barrels(), 1);
        a.setBarrels(3);
        QCOMPARE(a.barrels(), 3);
        // Invalid values (<1) are rejected without mutating the engine.
        a.setBarrels(0);
        QCOMPARE(a.barrels(), 3);
        a.setBarrels(-2);
        QCOMPARE(a.barrels(), 3);
        swmm_engine_destroy(e);
    }

    /**
     *
     * @brief Require each dimensionless loss coefficient edit to preserve the other two fields of
     * the engine triple.
     *
     */
    void lossCoefficientsTriplePropagate()
    {
        // The three Q_PROPERTYs round-trip through one engine triple-write;
        // verify each slot writes only its own coefficient, leaving the
        // other two intact.
        SWMM_Engine e = buildConduitFixture();
        QVERIFY(e);
        SWMMConduitPropertyAdapter a(e, QStringLiteral("C1"));

        a.setLossInlet(0.5);
        QCOMPARE(a.lossInlet(), 0.5);
        QCOMPARE(a.lossOutlet(), 0.0);
        QCOMPARE(a.lossAvg(), 0.0);

        a.setLossOutlet(0.25);
        QCOMPARE(a.lossInlet(), 0.5);
        QCOMPARE(a.lossOutlet(), 0.25);
        QCOMPARE(a.lossAvg(), 0.0);

        a.setLossAvg(0.1);
        QCOMPARE(a.lossInlet(), 0.5);
        QCOMPARE(a.lossOutlet(), 0.25);
        QCOMPARE(a.lossAvg(), 0.1);

        // Mutating one slot must NOT touch the other two — regression for
        // the read-merge-write atomicity contract documented in §S.1 Q-S4.
        a.setLossInlet(0.7);
        QCOMPARE(a.lossInlet(), 0.7);
        QCOMPARE(a.lossOutlet(), 0.25);
        QCOMPARE(a.lossAvg(), 0.1);

        swmm_engine_destroy(e);
    }

    /**
     *
     * @brief Require conduit flow/loss/seepage/barrel meta-properties to exist and advertise
     * writable setters.
     *
     */
    void scalarPropertiesAdvertisedAsWritable()
    {
        SWMM_Engine e = buildConduitFixture();
        QVERIFY(e);
        SWMMConduitPropertyAdapter a(e, QStringLiteral("C1"));
        const auto *mo = a.metaObject();
        const QStringList editableProps = {
            QStringLiteral("initialFlow"), QStringLiteral("maxFlow"), QStringLiteral("lossInlet"),
            QStringLiteral("lossOutlet"),  QStringLiteral("lossAvg"), QStringLiteral("seepRate"),
            QStringLiteral("barrels"),
        };
        for (const QString &name : editableProps) {
            const int idx = mo->indexOfProperty(name.toUtf8().constData());
            QVERIFY2(idx >= 0, qPrintable(QStringLiteral("conduit missing %1").arg(name)));
            QVERIFY2(mo->property(idx).isWritable(),
                     qPrintable(QStringLiteral("expected %1 writable").arg(name)));
        }
        swmm_engine_destroy(e);
    }

    /**
     *
     * @brief Require each tested conduit scalar key to have a nonempty display label.
     *
     */
    void scalarDisplayLabelsResolve()
    {
        SWMM_Engine e = buildConduitFixture();
        QVERIFY(e);
        SWMMConduitPropertyAdapter a(e, QStringLiteral("C1"));
        const QStringList keys = {
            QStringLiteral("initialFlow"), QStringLiteral("maxFlow"), QStringLiteral("lossInlet"),
            QStringLiteral("lossOutlet"),  QStringLiteral("lossAvg"), QStringLiteral("seepRate"),
            QStringLiteral("barrels"),
        };
        for (const QString &k : keys) {
            QVERIFY2(!a.displayLabelFor(k).isEmpty(), qPrintable(k));
        }
        swmm_engine_destroy(e);
    }

    // ====================================================================
    // Slice SC.1 — LinkCompoundEditRef accessors + Q_PROPERTY shape
    // ====================================================================

    /**
     *
     * @brief Require XSection/culvert/inlet refs to carry correct borrowed identities, editor
     * kinds and nonempty default summaries.
     *
     */
    void xsectionRefCarriesEngineAndLinkName()
    {
        SWMM_Engine e = buildConduitFixture();
        QVERIFY(e);
        SWMMConduitPropertyAdapter a(e, QStringLiteral("C1"));

        const LinkCompoundEditRef xr = a.xsectionRef();
        QCOMPARE(xr.engine, e);
        QCOMPARE(xr.linkName, QStringLiteral("C1"));
        QCOMPARE(xr.kind, LinkCompoundEditRef::XSection);
        // Summary is computed live; for a freshly-added conduit the
        // engine seeds the shape to CIRCULAR with zero geom1 — the
        // summary must still be non-empty so the cell shows something.
        QVERIFY(!xr.summary.isEmpty());

        // ATTRIBUTE_EDITOR_WIRING Phase 0 — culvert code is now an
        // inline-combobox value type, not a compound-dialog ref.
        const CulvertCodeRef cr = a.culvertCodeRef();
        QCOMPARE(cr.engine, e);
        QCOMPARE(cr.linkName, QStringLiteral("C1"));
        QCOMPARE(cr.code, 0); // fresh conduit — no culvert code
        QVERIFY(!culvertCodeLabel(cr.code).isEmpty());

        const LinkCompoundEditRef ir = a.inletUsageRef();
        QCOMPARE(ir.kind, LinkCompoundEditRef::InletUsage);
        QVERIFY(!ir.summary.isEmpty());

        swmm_engine_destroy(e);
    }

    /**
     *
     * @brief Require direct circular/closed-rectangle engine writes to appear in live XSection
     * summary text with their authored dimensions.
     *
     */
    void xsectionSummaryReflectsEngineWrite()
    {
        SWMM_Engine e = buildConduitFixture();
        QVERIFY(e);
        SWMMConduitPropertyAdapter a(e, QStringLiteral("C1"));

        // Direct engine write — the test stands in for the dialog's
        // apply-as-you-go path. CIRCULAR (shape 0), diameter 3.0 ft.
        const int idx = swmm_link_index(e, "C1");
        QVERIFY(idx >= 0);
        QCOMPARE(swmm_link_set_xsect(e, idx, /*CIRCULAR=*/0, 3.0, 0, 0, 0), SWMM_OK);

        const QString summary = a.xsectionRef().summary;
        QVERIFY(summary.contains(QStringLiteral("CIRCULAR")));
        QVERIFY(summary.contains(QStringLiteral("3")));

        // Switch to RECT_CLOSED (shape 2) with explicit width.
        QCOMPARE(swmm_link_set_xsect(e, idx, /*RECT_CLOSED=*/2, 4.0, 2.5, 0, 0), SWMM_OK);
        const QString s2 = a.xsectionRef().summary;
        QVERIFY(s2.contains(QStringLiteral("RECT_CLOSED")));
        QVERIFY(s2.contains(QStringLiteral("4")));
        QVERIFY(s2.contains(QStringLiteral("2.5")));

        swmm_engine_destroy(e);
    }

    // §S.SC.1.a (2026-05-25) — the XSection page's shape picker is now a
    // QListWidget(IconMode) inside a QSplitter, not a QComboBox. Driving
    // the list's currentRow programmatically must route through the same
    // apply-as-you-go path the combo did: engine xsect shape is rewritten
    // synchronously, geom1..4 + barrels stay at their last values.
    /**
     *
     * @brief Require selecting the trapezoidal thumbnail to synchronously update the engine shape
     * through the null-layer direct path.
     * Also checks the horizontal shape splitter is noncollapsible; the case does not
     * compare retained dimension or barrel values after the shape change.
     *
     */
    void xsectionThumbnailSelectionRoundTrip()
    {
        SWMM_Engine e = buildConduitFixture();
        QVERIFY(e);
        const int idx = swmm_link_index(e, "C1");
        QVERIFY(idx >= 0);
        // Seed a known starting state — CIRCULAR (shape 0), 2.0 ft.
        QCOMPARE(swmm_link_set_xsect(e, idx, /*CIRCULAR=*/0, 2.0, 0, 0, 0), SWMM_OK);

        LinkCompoundEditRef ref;
        ref.engine = e;
        ref.linkName = QStringLiteral("C1");
        ref.kind = LinkCompoundEditRef::XSection;
        ref.layer = nullptr; // smoke test exercises direct-engine path.

        LinkCompoundEditDialog dlg(ref);

        auto *list = dlg.findChild<QListWidget *>(QStringLiteral("xsectionShapeList"));
        QVERIFY2(list, "splitter-rewrite must expose the shape list by name");
        QVERIFY2(list->count() >= 5, "conduit shape palette must enumerate the full shape set");

        // Locate the row whose UserRole is TRAPEZOIDAL (engine id 4).
        int trapezoidalRow = -1;
        for (int i = 0; i < list->count(); ++i) {
            if (list->item(i)->data(Qt::UserRole).toInt() == 4) {
                trapezoidalRow = i;
                break;
            }
        }
        QVERIFY(trapezoidalRow >= 0);

        // Drive the new selection path; engine state must follow synchronously.
        list->setCurrentRow(trapezoidalRow);

        int shape = -1;
        double g1 = 0, g2 = 0, g3 = 0, g4 = 0;
        QCOMPARE(swmm_link_get_xsect(e, idx, &shape, &g1, &g2, &g3, &g4), SWMM_OK);
        QCOMPARE(shape, /*TRAPEZOIDAL=*/4);

        // Sanity-check the splitter itself exists and isn't collapsible —
        // guards the user-facing resize affordance against accidental
        // regressions to a static layout.
        auto *splitter = dlg.findChild<QSplitter *>(QStringLiteral("xsectionShapeSplitter"));
        QVERIFY(splitter);
        QVERIFY(!splitter->childrenCollapsible());
        QCOMPARE(splitter->orientation(), Qt::Horizontal);

        swmm_engine_destroy(e);
    }

    /**
     *
     * @brief Require conduit XSection, culvert and inlet compound meta-properties to be writable
     * and notify changes.
     *
     */
    void compoundCellsAdvertisedAsWritable()
    {
        // The `Q_PROPERTY` flags are what unlock the per-cell button
        // widget: without WRITE the QPropertyItemDelegate never enters
        // edit mode and the registered LinkCompoundEditButton creator
        // never fires. Lock the contract in metatype introspection.
        SWMM_Engine e = buildConduitFixture();
        QVERIFY(e);
        SWMMConduitPropertyAdapter a(e, QStringLiteral("C1"));
        const auto *mo = a.metaObject();
        const QStringList compoundProps = {
            QStringLiteral("xsection"),
            QStringLiteral("culvertCode"),
            QStringLiteral("inletUsage"),
        };
        for (const QString &p : compoundProps) {
            const int idx = mo->indexOfProperty(p.toUtf8().constData());
            QVERIFY2(idx >= 0, qPrintable(QStringLiteral("conduit missing %1").arg(p)));
            QVERIFY2(mo->property(idx).isWritable(),
                     qPrintable(QStringLiteral("%1 must be writable").arg(p)));
            QVERIFY2(mo->property(idx).hasNotifySignal(),
                     qPrintable(QStringLiteral("%1 must NOTIFY changed").arg(p)));
        }
        swmm_engine_destroy(e);
    }

    // ====================================================================
    // Slice SC.1 extended — xsection cell on Orifice + Weir adapters.
    // Conduit-only kinds (culvertCode + inletUsage) must NOT leak onto
    // these subclasses; xsection MUST be present.
    // ====================================================================

    /**
     *
     * @brief Require orifice metadata/ref to expose XSection while excluding conduit-only culvert
     * and inlet rows.
     *
     */
    void orificeAdvertisesXsectionOnly()
    {
        SWMM_Engine e = buildLinkFixture(/*Orifice=*/2);
        QVERIFY(e);
        SWMMOrificePropertyAdapter a(e, QStringLiteral("L1"));
        const auto *mo = a.metaObject();
        QVERIFY(mo->indexOfProperty("xsection") >= 0);
        QCOMPARE(mo->indexOfProperty("culvertCode"), -1);
        QCOMPARE(mo->indexOfProperty("inletUsage"), -1);

        // Round-trip the ref accessors run without crashing on a
        // freshly-added orifice link (engine seeds shape=CIRCULAR, g1=0).
        const LinkCompoundEditRef xr = a.xsectionRef();
        QCOMPARE(xr.linkName, QStringLiteral("L1"));
        QCOMPARE(xr.kind, LinkCompoundEditRef::XSection);
        QVERIFY(!xr.summary.isEmpty());

        swmm_engine_destroy(e);
    }

    /**
     *
     * @brief Require weir metadata/ref to expose only XSection among conduit compound rows and
     * reflect an engine RECT_OPEN shape write.
     *
     */
    void weirAdvertisesXsectionOnly()
    {
        SWMM_Engine e = buildLinkFixture(/*Weir=*/3);
        QVERIFY(e);
        SWMMWeirPropertyAdapter a(e, QStringLiteral("L1"));
        const auto *mo = a.metaObject();
        QVERIFY(mo->indexOfProperty("xsection") >= 0);
        QCOMPARE(mo->indexOfProperty("culvertCode"), -1);
        QCOMPARE(mo->indexOfProperty("inletUsage"), -1);

        const LinkCompoundEditRef xr = a.xsectionRef();
        QCOMPARE(xr.kind, LinkCompoundEditRef::XSection);
        QVERIFY(!xr.summary.isEmpty());

        // Direct engine write — RECT_OPEN (shape 3) is one of the legal
        // weir xsections per legacy objprops.txt:875. Adapter summary
        // updates without any caching.
        const int idx = swmm_link_index(e, "L1");
        QCOMPARE(swmm_link_set_xsect(e, idx, /*RECT_OPEN=*/3, 2.0, 5.0, 0, 0), SWMM_OK);
        const QString s = a.xsectionRef().summary;
        QVERIFY(s.contains(QStringLiteral("RECT_OPEN")));

        swmm_engine_destroy(e);
    }

    /**
     *
     * @brief Require real pump/outlet adapter metaobjects to omit the XSection property.
     *
     */
    void pumpAndOutletStillHaveNoXsectionCell()
    {
        // The xsection cell is intentionally NOT on pumps or outlets —
        // legacy PumpProps + OutletProps have no Shape row. Guard the
        // contract so a careless future edit doesn't surface it there.
        SWMM_Engine e = buildLinkFixture(/*Pump=*/1);
        QVERIFY(e);
        {
            SWMMPumpPropertyAdapter a(e, QStringLiteral("L1"));
            QCOMPARE(a.metaObject()->indexOfProperty("xsection"), -1);
        }
        swmm_engine_destroy(e);

        e = buildLinkFixture(/*Outlet=*/4);
        QVERIFY(e);
        {
            SWMMOutletPropertyAdapter a(e, QStringLiteral("L1"));
            QCOMPARE(a.metaObject()->indexOfProperty("xsection"), -1);
        }
        swmm_engine_destroy(e);
    }

    // ====================================================================
    // Slice SD partial — orifice TYPE (SIDE / BOTTOM) round-trip via the
    // BN-LINK-02 engine accessor pair.
    // ====================================================================

    /**
     *
     * @brief Require orificeType to be a writable notifying enum with SIDE/BOTTOM keys in UI
     * order.
     *
     */
    void orificeTypeAdvertisedAsWritableEnum()
    {
        SWMM_Engine e = buildLinkFixture(/*Orifice=*/2);
        QVERIFY(e);
        SWMMOrificePropertyAdapter a(e, QStringLiteral("L1"));

        const auto *mo = a.metaObject();
        const int idx = mo->indexOfProperty("orificeType");
        QVERIFY2(idx >= 0, "orifice adapter missing orificeType");
        const QMetaProperty p = mo->property(idx);
        QVERIFY(p.isWritable());
        QVERIFY(p.hasNotifySignal());
        QVERIFY(p.isEnumType());
        // Enum keys come from SWMMLinkPropertyAdapter::OrificeType.
        QCOMPARE(QString::fromLatin1(p.enumerator().key(0)), QStringLiteral("SIDE"));
        QCOMPARE(QString::fromLatin1(p.enumerator().key(1)), QStringLiteral("BOTTOM"));

        swmm_engine_destroy(e);
    }

    /**
     *
     * @brief Require the default BOTTOM orifice type to switch SIDE/BOTTOM through the engine
     * with one first-change signal.
     *
     */
    void orificeTypeRoundTripsThroughEngine()
    {
        SWMM_Engine e = buildLinkFixture(/*Orifice=*/2);
        QVERIFY(e);
        SWMMOrificePropertyAdapter a(e, QStringLiteral("L1"));

        // links.param1 defaults to 0.0 on link_add → engine BOTTOM →
        // adapter BOTTOM. Lock the engine contract here so a regression
        // in default-init bubbles up loudly.
        QCOMPARE(a.orificeType(), SWMMLinkPropertyAdapter::BOTTOM);

        QSignalSpy spy(&a, &SWMMLinkPropertyAdapter::changed);
        a.setOrificeType(SWMMLinkPropertyAdapter::SIDE);
        QCOMPARE(spy.count(), 1);
        QCOMPARE(a.orificeType(), SWMMLinkPropertyAdapter::SIDE);

        a.setOrificeType(SWMMLinkPropertyAdapter::BOTTOM);
        QCOMPARE(a.orificeType(), SWMMLinkPropertyAdapter::BOTTOM);

        swmm_engine_destroy(e);
    }

    /**
     *
     * @brief Require conduit and weir metaobjects to omit orificeType; other kinds are not
     * instantiated by this case.
     *
     */
    void nonOrificeAdaptersDoNotExposeOrificeType()
    {
        // The Q_PROPERTY must NOT surface on other link kinds — even
        // though the base-class enum is shared, only Orifice declares
        // the Q_PROPERTY. Without this guard, a future careless edit
        // could leak the row onto conduits / pumps / weirs / outlets.
        SWMM_Engine e = buildConduitFixture();
        QVERIFY(e);
        {
            SWMMConduitPropertyAdapter a(e, QStringLiteral("C1"));
            QCOMPARE(a.metaObject()->indexOfProperty("orificeType"), -1);
        }
        swmm_engine_destroy(e);

        e = buildLinkFixture(/*Weir=*/3);
        QVERIFY(e);
        {
            SWMMWeirPropertyAdapter a(e, QStringLiteral("L1"));
            QCOMPARE(a.metaObject()->indexOfProperty("orificeType"), -1);
        }
        swmm_engine_destroy(e);
    }

    /**
     *
     * @brief Require orificeType to resolve a nonempty display label.
     *
     */
    void orificeTypeDisplayLabelResolves()
    {
        SWMM_Engine e = buildLinkFixture(/*Orifice=*/2);
        QVERIFY(e);
        SWMMOrificePropertyAdapter a(e, QStringLiteral("L1"));
        QVERIFY(!a.displayLabelFor(QStringLiteral("orificeType")).isEmpty());
        swmm_engine_destroy(e);
    }

    // ====================================================================
    // Slice SD partial — weir TYPE (5 values) round-trip via BN-LINK-03.
    // ====================================================================

    /**
     *
     * @brief Require weirType to be a writable notifying enum with the five expected UI keys in
     * order.
     *
     */
    void weirTypeAdvertisedAsWritableEnum()
    {
        SWMM_Engine e = buildLinkFixture(/*Weir=*/3);
        QVERIFY(e);
        SWMMWeirPropertyAdapter a(e, QStringLiteral("L1"));

        const auto *mo = a.metaObject();
        const int idx = mo->indexOfProperty("weirType");
        QVERIFY2(idx >= 0, "weir adapter missing weirType");
        const QMetaProperty p = mo->property(idx);
        QVERIFY(p.isWritable());
        QVERIFY(p.hasNotifySignal());
        QVERIFY(p.isEnumType());
        // Keys must match the legacy combo order verbatim so the cell
        // renders names users have seen for decades.
        const QStringList expectedKeys = {
            QStringLiteral("TRANSVERSE"),  QStringLiteral("SIDEFLOW"), QStringLiteral("VNOTCH"),
            QStringLiteral("TRAPEZOIDAL"), QStringLiteral("ROADWAY"),
        };
        for (int i = 0; i < expectedKeys.size(); ++i) {
            QCOMPARE(QString::fromLatin1(p.enumerator().key(i)), expectedKeys.at(i));
        }
        swmm_engine_destroy(e);
    }

    /**
     *
     * @brief Require default TRANSVERSE and every five-value weir-type setter to round-trip.
     *
     */
    void weirTypeAllFiveValuesRoundTrip()
    {
        SWMM_Engine e = buildLinkFixture(/*Weir=*/3);
        QVERIFY(e);
        SWMMWeirPropertyAdapter a(e, QStringLiteral("L1"));

        // Default is TRANSVERSE (engine seeds param1 = 0.0 on link_add).
        QCOMPARE(a.weirType(), SWMMLinkPropertyAdapter::TRANSVERSE);

        for (SWMMLinkPropertyAdapter::WeirType v :
             {SWMMLinkPropertyAdapter::TRANSVERSE, SWMMLinkPropertyAdapter::SIDEFLOW,
              SWMMLinkPropertyAdapter::VNOTCH, SWMMLinkPropertyAdapter::TRAPEZOIDAL,
              SWMMLinkPropertyAdapter::ROADWAY}) {
            a.setWeirType(v);
            QCOMPARE(a.weirType(), v);
        }

        swmm_engine_destroy(e);
    }

    /**
     *
     * @brief Require conduit and orifice metaobjects to omit weirType.
     *
     */
    void weirTypeNotExposedOnOtherLinkKinds()
    {
        SWMM_Engine e = buildConduitFixture();
        QVERIFY(e);
        {
            SWMMConduitPropertyAdapter a(e, QStringLiteral("C1"));
            QCOMPARE(a.metaObject()->indexOfProperty("weirType"), -1);
        }
        swmm_engine_destroy(e);

        e = buildLinkFixture(/*Orifice=*/2);
        QVERIFY(e);
        {
            SWMMOrificePropertyAdapter a(e, QStringLiteral("L1"));
            QCOMPARE(a.metaObject()->indexOfProperty("weirType"), -1);
        }
        swmm_engine_destroy(e);
    }

    /**
     *
     * @brief Require weirType to resolve a nonempty display label.
     *
     */
    void weirTypeDisplayLabelResolves()
    {
        SWMM_Engine e = buildLinkFixture(/*Weir=*/3);
        QVERIFY(e);
        SWMMWeirPropertyAdapter a(e, QStringLiteral("L1"));
        QVERIFY(!a.displayLabelFor(QStringLiteral("weirType")).isEmpty());
        swmm_engine_destroy(e);
    }

    // ====================================================================
    // Slice SD partial — outlet rating type + exponent via BN-LINK-04.
    // ====================================================================

    /**
     *
     * @brief Require the outlet rating enum keys, writable coefficient/exponent and curve-picker
     * property to be advertised.
     *
     */
    void outletRatingPropertiesAdvertised()
    {
        SWMM_Engine e = buildLinkFixture(/*Outlet=*/4);
        QVERIFY(e);
        SWMMOutletPropertyAdapter a(e, QStringLiteral("L1"));
        const auto *mo = a.metaObject();

        // Rating type — enum, writable, NOTIFY.
        const int rti = mo->indexOfProperty("ratingType");
        QVERIFY2(rti >= 0, "outlet adapter missing ratingType");
        const QMetaProperty rt = mo->property(rti);
        QVERIFY(rt.isWritable());
        QVERIFY(rt.hasNotifySignal());
        QVERIFY(rt.isEnumType());
        const QStringList expected = {
            QStringLiteral("FUNCTIONAL_HEAD"),
            QStringLiteral("FUNCTIONAL_DEPTH"),
            QStringLiteral("TABULAR_HEAD"),
            QStringLiteral("TABULAR_DEPTH"),
        };
        for (int i = 0; i < expected.size(); ++i)
            QCOMPARE(QString::fromLatin1(rt.enumerator().key(i)), expected.at(i));

        // Scalar coefficient + exponent rows.
        QVERIFY(mo->indexOfProperty("coefficient") >= 0);
        QVERIFY(mo->indexOfProperty("expon") >= 0);
        QVERIFY(mo->property(mo->indexOfProperty("coefficient")).isWritable());
        QVERIFY(mo->property(mo->indexOfProperty("expon")).isWritable());

        // Tabular-curve picker via DataObjectRef.
        QVERIFY(mo->indexOfProperty("outletCurve") >= 0);

        swmm_engine_destroy(e);
    }

    /**
     *
     * @brief Require default FUNCTIONAL_HEAD and every functional/tabular head/depth
     * outlet-rating choice to round-trip.
     *
     */
    void outletRatingTypeAllFourValuesRoundTrip()
    {
        SWMM_Engine e = buildLinkFixture(/*Outlet=*/4);
        QVERIFY(e);
        SWMMOutletPropertyAdapter a(e, QStringLiteral("L1"));

        QCOMPARE(a.outletRatingType(), SWMMLinkPropertyAdapter::FUNCTIONAL_HEAD);

        for (auto v :
             {SWMMLinkPropertyAdapter::FUNCTIONAL_HEAD, SWMMLinkPropertyAdapter::FUNCTIONAL_DEPTH,
              SWMMLinkPropertyAdapter::TABULAR_HEAD, SWMMLinkPropertyAdapter::TABULAR_DEPTH}) {
            a.setOutletRatingType(v);
            QCOMPARE(a.outletRatingType(), v);
        }

        swmm_engine_destroy(e);
    }

    /**
     *
     * @brief Require outlet exponent to default zero and round-trip 0.5 then 1.75.
     *
     */
    void outletExponRoundTrips()
    {
        SWMM_Engine e = buildLinkFixture(/*Outlet=*/4);
        QVERIFY(e);
        SWMMOutletPropertyAdapter a(e, QStringLiteral("L1"));
        QCOMPARE(a.outletExpon(), 0.0);
        a.setOutletExpon(0.5);
        QCOMPARE(a.outletExpon(), 0.5);
        a.setOutletExpon(1.75);
        QCOMPARE(a.outletExpon(), 1.75);
        swmm_engine_destroy(e);
    }

    /**
     *
     * @brief Require conduit metadata to omit rating/exponent/curve and weir metadata to omit
     * rating/exponent.
     *
     */
    void outletRatingNotExposedOnOtherLinkKinds()
    {
        SWMM_Engine e = buildConduitFixture();
        QVERIFY(e);
        {
            SWMMConduitPropertyAdapter a(e, QStringLiteral("C1"));
            QCOMPARE(a.metaObject()->indexOfProperty("ratingType"), -1);
            QCOMPARE(a.metaObject()->indexOfProperty("expon"), -1);
            QCOMPARE(a.metaObject()->indexOfProperty("outletCurve"), -1);
        }
        swmm_engine_destroy(e);

        e = buildLinkFixture(/*Weir=*/3);
        QVERIFY(e);
        {
            SWMMWeirPropertyAdapter a(e, QStringLiteral("L1"));
            QCOMPARE(a.metaObject()->indexOfProperty("ratingType"), -1);
            QCOMPARE(a.metaObject()->indexOfProperty("expon"), -1);
        }
        swmm_engine_destroy(e);
    }

    /**
     *
     * @brief Require rating/coefficient/exponent/curve keys to resolve nonempty outlet labels.
     *
     */
    void outletRatingDisplayLabelsResolve()
    {
        SWMM_Engine e = buildLinkFixture(/*Outlet=*/4);
        QVERIFY(e);
        SWMMOutletPropertyAdapter a(e, QStringLiteral("L1"));
        const QStringList keys = {
            QStringLiteral("ratingType"),
            QStringLiteral("coefficient"),
            QStringLiteral("expon"),
            QStringLiteral("outletCurve"),
        };
        for (const QString &k : keys) {
            QVERIFY2(!a.displayLabelFor(k).isEmpty(), qPrintable(k));
        }
        swmm_engine_destroy(e);
    }

    // ====================================================================
    // Slice SD partial — pump startup/shutoff (BN-LINK-05) and orifice
    // open/close rate (BN-LINK-06) round-trips.
    // ====================================================================

    /**
     *
     * @brief Require startup/shutoff depths to default zero, round-trip independently and expose
     * writable labeled pump properties.
     *
     */
    void pumpStartupShutoffRoundTrip()
    {
        SWMM_Engine e = buildLinkFixture(/*Pump=*/1);
        QVERIFY(e);
        SWMMPumpPropertyAdapter a(e, QStringLiteral("L1"));

        QCOMPARE(a.pumpStartupDepth(), 0.0);
        QCOMPARE(a.pumpShutoffDepth(), 0.0);

        a.setPumpStartupDepth(2.5);
        a.setPumpShutoffDepth(0.5);
        QCOMPARE(a.pumpStartupDepth(), 2.5);
        QCOMPARE(a.pumpShutoffDepth(), 0.5);

        const auto *mo = a.metaObject();
        QVERIFY(mo->indexOfProperty("startupDepth") >= 0);
        QVERIFY(mo->indexOfProperty("shutoffDepth") >= 0);
        QVERIFY(mo->property(mo->indexOfProperty("startupDepth")).isWritable());
        QVERIFY(mo->property(mo->indexOfProperty("shutoffDepth")).isWritable());
        QVERIFY(!a.displayLabelFor(QStringLiteral("startupDepth")).isEmpty());
        QVERIFY(!a.displayLabelFor(QStringLiteral("shutoffDepth")).isEmpty());

        swmm_engine_destroy(e);
    }

    /**
     *
     * @brief Require orifice opening/closing rate to default zero, round-trip 0.05 and expose a
     * writable labeled property.
     *
     */
    void orificeOpenCloseRateRoundTrip()
    {
        SWMM_Engine e = buildLinkFixture(/*Orifice=*/2);
        QVERIFY(e);
        SWMMOrificePropertyAdapter a(e, QStringLiteral("L1"));

        QCOMPARE(a.orificeOpenCloseRate(), 0.0);
        a.setOrificeOpenCloseRate(0.05);
        QCOMPARE(a.orificeOpenCloseRate(), 0.05);
        a.setOrificeOpenCloseRate(0.0);
        QCOMPARE(a.orificeOpenCloseRate(), 0.0);

        const auto *mo = a.metaObject();
        QVERIFY(mo->indexOfProperty("openCloseRate") >= 0);
        QVERIFY(mo->property(mo->indexOfProperty("openCloseRate")).isWritable());
        QVERIFY(!a.displayLabelFor(QStringLiteral("openCloseRate")).isEmpty());

        swmm_engine_destroy(e);
    }

    /**
     *
     * @brief Require conduit metadata to omit pump startup/shutoff and orifice opening/closing
     * rows.
     *
     */
    void pumpDepthsNotExposedOnOtherKinds()
    {
        SWMM_Engine e = buildConduitFixture();
        QVERIFY(e);
        SWMMConduitPropertyAdapter a(e, QStringLiteral("C1"));
        QCOMPARE(a.metaObject()->indexOfProperty("startupDepth"), -1);
        QCOMPARE(a.metaObject()->indexOfProperty("shutoffDepth"), -1);
        QCOMPARE(a.metaObject()->indexOfProperty("openCloseRate"), -1);
        swmm_engine_destroy(e);
    }
};

// ============================================================================
// Stub SWMMModelLayer surface — §S.SC.1.a smoke test only.
// ----------------------------------------------------------------------------
// LinkCompoundEditDialog::buildXSectionPage references three apply* methods
// on SWMMModelLayer. The test constructs the dialog with ref.layer = nullptr
// so the runtime path never enters those branches, but both branches still
// compile and the linker needs symbol bodies. Mirrors the pattern in
// test_hydrograph_models.cpp:215+.
// ============================================================================

#include "layers/swmmmodellayer.h"

/**
 *
 * @brief Satisfy the unused model-layer XSection link dependency.
 * @param index Ignored link index.
 * @param shape Ignored engine cross-section enum.
 * @param g1 Ignored first shape-dependent authored dimension.
 * @param g2 Ignored second shape-dependent authored dimension.
 * @param g3 Ignored third shape-dependent authored dimension.
 * @param g4 Ignored fourth shape-dependent authored dimension.
 * @return Always true without validation, engine writes or undo history. The
 * smoke dialog uses a null layer and never executes this production-incompatible stub.
 *
 */
bool SWMMModelLayer::applyLinkXsect(int index, int shape, double g1, double g2, double g3,
                                    double g4)
{
    return true;
}
/**
 *
 * @brief Satisfy the unused model-layer barrel-edit link dependency.
 * @param index Ignored link index.
 * @param barrels Ignored barrel count, not validated.
 * @return Always true with no model mutation or undo; not exercised by the null-layer dialog.
 *
 */
bool SWMMModelLayer::applyLinkBarrels(int index, int barrels)
{
    return true;
}
/**
 *
 * @brief Satisfy the unused model-layer culvert-edit link dependency.
 * @param index Ignored link index.
 * @param code Ignored culvert code, not validated.
 * @return Always true with no model mutation or undo; not exercised by the null-layer dialog.
 *
 */
bool SWMMModelLayer::applyLinkCulvertCode(int index, int code)
{
    return true;
}

QTEST_MAIN(TestLinkPropertyAdapter)
#include "test_linkpropertyadapter.moc"
