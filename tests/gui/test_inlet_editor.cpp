/*!
 * \file   test_inlet_editor.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * Inlets plan §2.2–2.3 (INLET_EDITOR_AND_INLET_JUNCTION_GUI_PLAN_2026-09-05,
 * Phase G2). Three concerns, all reachable without the dialog:
 *
 *  1. InletProvider ↔ InletRegistry ↔ a headless engine round-trip for every
 *     inlet type (including COMBINATION and CUSTOM) — create → saveToEngine →
 *     a fresh registry's loadFromEngine → the design compares equal, and
 *     swmm_inlet_get_design agrees field for field. This is the regression
 *     guard for the old "defaults on load" behaviour.
 *  2. Undo / redo of a parameter change through SetInletParamsCommand.
 *  3. InletSceneBuilder's dimension-callout count per type (the table in
 *     inletdrawingview.h).
 *
 * Plus the property bag's group-visibility table, which drives which rows the
 * editor shows for each type.
 */
#include "inlet/inletprovider.h"
#include "inlet/inletregistry.h"
#include "inlet/inletundocommands.h"
#include "ui/dialogs/inletpropertybag.h"
#include "ui/widgets/inletdrawingview.h"

#include <openswmm/engine/openswmm_engine.h>
#include <openswmm/engine/openswmm_infrastructure.h>

#include <QGraphicsItem>
#include <QGraphicsScene>
#include <QObject>
#include <QPalette>
#include <QTest>
#include <QUndoStack>

#include <iterator>

using openswmmvis::inlet::AddInletCommand;
using openswmmvis::inlet::DeleteInletCommand;
using openswmmvis::inlet::GrateType;
using openswmmvis::inlet::InletCurveKind;
using openswmmvis::inlet::InletDesignData;
using openswmmvis::inlet::InletProvider;
using openswmmvis::inlet::InletRegistry;
using openswmmvis::inlet::InletType;
using openswmmvis::inlet::SetInletParamsCommand;
using openswmmvis::inlet::ThroatType;
using openswmmvis::ui::InletPropertyBag;
using openswmmvis::ui::InletSceneBuilder;
using openswmmvis::ui::InletTheme;

namespace {

/*! A design with a distinct value in every slot so a dropped field shows up. */
InletDesignData sampleDesign(InletType t)
{
    InletDesignData d;
    d.type        = t;
    d.grateLength = 3.25;
    d.grateWidth  = 1.75;
    d.grateType   = GrateType::CurvedVane;
    d.openArea    = 0.62;
    d.splashVeloc = 4.5;
    d.curbLength  = 5.5;
    d.curbHeight  = 0.75;
    d.throat      = ThroatType::Inclined;
    d.slotLength  = 8.0;
    d.slotWidth   = 0.35;
    d.curveId     = QStringLiteral("CAP1");
    d.curveKind   = InletCurveKind::Diversion;
    return d;
}

bool usesGrateGroup(InletType t)
{
    return t == InletType::Grate || t == InletType::DropGrate
        || t == InletType::Combo;
}

bool usesCurbGroup(InletType t)
{
    return t == InletType::Curb || t == InletType::DropCurb
        || t == InletType::Combo;
}

/*! What sampleDesign(t) looks like after an engine round-trip.
 *
 *  swmm_inlet_set_design stores only the fields the type uses and zeroes the
 *  rest, and InletRegistry deliberately restores the provider's defaults for
 *  those inapplicable groups rather than copying the zeros back (see
 *  fromEngineDesign) — otherwise a GRATE loaded from a file could never be
 *  switched to CURB. */
InletDesignData expectedAfterRoundTrip(InletType t)
{
    const InletDesignData s = sampleDesign(t);
    InletDesignData e;              // provider defaults
    e.type = t;
    if (usesGrateGroup(t)) {
        e.grateLength = s.grateLength;
        e.grateWidth  = s.grateWidth;
        e.grateType   = s.grateType;
        e.openArea    = s.openArea;
        e.splashVeloc = s.splashVeloc;
    }
    if (usesCurbGroup(t)) {
        e.curbLength = s.curbLength;
        e.curbHeight = s.curbHeight;
        e.throat     = s.throat;
    }
    if (t == InletType::Slotted) {
        e.slotLength = s.slotLength;
        e.slotWidth  = s.slotWidth;
    }
    if (t == InletType::Custom) {
        e.curveId = s.curveId;
        // swmm_inlet_set_design keeps the given curve_kind while the curve
        // does not exist yet (CAP1 is never added here); once the curve
        // exists its own table type is the authority instead.
        e.curveKind = s.curveKind;
    }
    return e;
}

int calloutCount(QGraphicsScene *scene)
{
    int n = 0;
    const auto items = scene->items();
    for (QGraphicsItem *it : items)
        if (it->data(InletSceneBuilder::CalloutRole).isValid()) ++n;
    return n;
}

QGraphicsScene *sceneFor(InletType t, GrateType g = GrateType::PBar50)
{
    InletProvider p(QStringLiteral("D"));
    InletDesignData d = sampleDesign(t);
    d.grateType = g;
    p.setDesign(d);
    return InletSceneBuilder::build(p, /*units*/ nullptr,
                                     InletTheme::fromPalette(QPalette()));
}

} // namespace

class TestInletEditor : public QObject
{
    Q_OBJECT

private slots:

    // ── 1. Provider ↔ registry ↔ engine round-trip ──────────────────────────

    void registryRoundTrip_EveryType()
    {
        SWMM_Engine eng = swmm_engine_new();
        QVERIFY(eng != nullptr);

        static const InletType kAll[] = {
            InletType::Grate, InletType::Curb, InletType::Combo,
            InletType::Slotted, InletType::DropGrate, InletType::DropCurb,
            InletType::Custom,
        };

        // Author one design per type through the registry, then flush.
        {
            InletRegistry reg;
            for (InletType t : kAll) {
                const QString name =
                    QStringLiteral("IN%1").arg(static_cast<int>(t));
                InletProvider *p = reg.create(name);
                QVERIFY(p);
                p->setDesign(sampleDesign(t));
                p->setComments(QStringLiteral("design %1").arg(name));
            }
            QCOMPARE(reg.saveToEngine(eng), int(std::size(kAll)));
        }

        // The engine holds every field the type actually uses.
        for (InletType t : kAll) {
            const QString name = QStringLiteral("IN%1").arg(static_cast<int>(t));
            const int idx = swmm_inlet_index(eng, name.toUtf8().constData());
            QVERIFY2(idx >= 0, qPrintable(name));

            SWMM_InletDesign got{};
            QCOMPARE(swmm_inlet_get_design(eng, idx, &got), SWMM_OK);

            const InletDesignData want = sampleDesign(t);
            QCOMPARE(got.type, static_cast<int>(want.type));
            if (usesGrateGroup(t)) {
                QCOMPARE(got.grate_length, want.grateLength);
                QCOMPARE(got.grate_width,  want.grateWidth);
                QCOMPARE(got.grate_type,   static_cast<int>(want.grateType));
                QCOMPARE(got.open_area,    want.openArea);
                QCOMPARE(got.splash_veloc, want.splashVeloc);
            }
            if (usesCurbGroup(t)) {
                QCOMPARE(got.curb_length, want.curbLength);
                QCOMPARE(got.curb_height, want.curbHeight);
                QCOMPARE(got.throat,      static_cast<int>(want.throat));
            }
            if (t == InletType::Slotted) {
                QCOMPARE(got.slot_length, want.slotLength);
                QCOMPARE(got.slot_width,  want.slotWidth);
            }
            if (t == InletType::Custom)
                QCOMPARE(QString::fromUtf8(got.curve_id), want.curveId);
        }

        // A fresh registry reading the same engine reproduces the providers —
        // no more "loaded inlets show defaults".
        {
            InletRegistry reloaded;
            QCOMPARE(reloaded.loadFromEngine(eng), int(std::size(kAll)));
            for (InletType t : kAll) {
                const QString name =
                    QStringLiteral("IN%1").arg(static_cast<int>(t));
                InletProvider *p = reloaded.findByName(name);
                QVERIFY2(p, qPrintable(name));
                QVERIFY2(p->design() == expectedAfterRoundTrip(t), qPrintable(name));
                QCOMPARE(p->comments(), QStringLiteral("design %1").arg(name));
            }
        }

        swmm_engine_destroy(eng);
    }

    void registryRemove_DeletesTheEngineCopy()
    {
        SWMM_Engine eng = swmm_engine_new();
        QVERIFY(eng != nullptr);

        InletRegistry reg;
        InletProvider *gone = reg.create(QStringLiteral("GONE"));
        InletProvider *kept = reg.create(QStringLiteral("KEPT"));
        QVERIFY(gone && kept);
        gone->setDesign(sampleDesign(InletType::Grate));
        kept->setDesign(sampleDesign(InletType::Curb));
        QCOMPARE(reg.saveToEngine(eng), 2);

        reg.remove(gone);
        QCOMPARE(swmm_inlet_index(eng, "GONE"), -1);
        QVERIFY(swmm_inlet_index(eng, "KEPT") >= 0);

        // A later flush must not resurrect it.
        reg.saveToEngine(eng);
        QCOMPARE(swmm_inlet_index(eng, "GONE"), -1);

        swmm_engine_destroy(eng);
    }

    // ── 2. Undo / redo ──────────────────────────────────────────────────────

    void undoRedo_ParamsChange()
    {
        InletRegistry reg;
        InletProvider *p = reg.create(QStringLiteral("G1"));
        QVERIFY(p);
        p->setDesign(sampleDesign(InletType::Grate));

        const InletDesignData before = p->design();
        InletDesignData after = before;
        after.grateLength = 9.5;
        after.grateType   = GrateType::Generic;
        after.openArea    = 0.4;

        QUndoStack stack;
        stack.push(new SetInletParamsCommand(p, before, after));
        QCOMPARE(p->grateLength(), 9.5);
        QVERIFY(p->grateType() == GrateType::Generic);

        stack.undo();
        QVERIFY(p->design() == before);

        stack.redo();
        QVERIFY(p->design() == after);
    }

    void undoRedo_AddAndDelete()
    {
        InletRegistry reg;
        QUndoStack stack;

        stack.push(new AddInletCommand(&reg, QStringLiteral("NEW1")));
        QVERIFY(reg.findByName(QStringLiteral("NEW1")));
        stack.undo();
        QVERIFY(!reg.findByName(QStringLiteral("NEW1")));
        stack.redo();

        InletProvider *p = reg.findByName(QStringLiteral("NEW1"));
        QVERIFY(p);
        p->setDesign(sampleDesign(InletType::Slotted));
        p->setComments(QStringLiteral("slot"));

        stack.push(new DeleteInletCommand(&reg, p));
        QVERIFY(!reg.findByName(QStringLiteral("NEW1")));

        stack.undo();
        InletProvider *back = reg.findByName(QStringLiteral("NEW1"));
        QVERIFY(back);
        QVERIFY(back->design() == sampleDesign(InletType::Slotted));
        QCOMPARE(back->comments(), QStringLiteral("slot"));
    }

    // ── 3. Drawing: dimension-callout count per type ─────────────────────────

    void sceneBuilder_CalloutCountPerType()
    {
        // The table documented in inletdrawingview.h.
        struct Row { InletType type; int callouts; };
        static const Row kRows[] = {
            { InletType::Grate,     2 },   // L, W
            { InletType::DropGrate, 2 },   // L, W
            { InletType::Curb,      3 },   // L, h, throat
            { InletType::DropCurb,  2 },   // L (×4 sides), h
            { InletType::Combo,     5 },   // L grate, W, L curb, sweeper, h
            { InletType::Slotted,   2 },   // L, w
            { InletType::Custom,    2 },   // curve name, curve kind
        };

        for (const Row &r : kRows) {
            QGraphicsScene *scene = sceneFor(r.type);
            QVERIFY(scene);
            QCOMPARE(calloutCount(scene), r.callouts);
            delete scene;
        }
    }

    void sceneBuilder_GenericGrateAddsTwoCallouts()
    {
        QGraphicsScene *plain   = sceneFor(InletType::Grate, GrateType::PBar50);
        QGraphicsScene *generic = sceneFor(InletType::Grate, GrateType::Generic);
        QCOMPARE(calloutCount(generic), calloutCount(plain) + 2);
        delete plain;
        delete generic;
    }

    void sceneBuilder_CalloutTextCarriesTheProviderValue()
    {
        InletProvider p(QStringLiteral("D"));
        InletDesignData d = sampleDesign(InletType::Grate);
        d.grateLength = 2.0;
        p.setDesign(d);

        QGraphicsScene *scene =
            InletSceneBuilder::build(p, nullptr, InletTheme::fromPalette(QPalette()));
        bool found = false;
        const auto items = scene->items();
        for (QGraphicsItem *it : items) {
            const QVariant v = it->data(InletSceneBuilder::CalloutRole);
            if (v.isValid() && v.toString() == QStringLiteral("L = 2.00 ft"))
                found = true;
        }
        QVERIFY2(found, "expected a 'L = 2.00 ft' dimension callout");
        delete scene;
    }

    // ── Property-bag group visibility ────────────────────────────────────────

    void propertyBag_GroupVisibilityMatchesTheLegacyTable()
    {
        using G = InletPropertyBag;
        QCOMPARE(int(G::groupsFor(InletType::Grate)),     int(G::GrateGroup));
        QCOMPARE(int(G::groupsFor(InletType::DropGrate)), int(G::GrateGroup));
        QCOMPARE(int(G::groupsFor(InletType::Curb)),      int(G::CurbGroup));
        QCOMPARE(int(G::groupsFor(InletType::DropCurb)),  int(G::CurbGroup));
        QCOMPARE(int(G::groupsFor(InletType::Combo)),
                 int(G::GrateGroup | G::CurbGroup));
        QCOMPARE(int(G::groupsFor(InletType::Slotted)),   int(G::SlottedGroup));
        QCOMPARE(int(G::groupsFor(InletType::Custom)),    int(G::CustomGroup));
    }

    void propertyBag_GenericOnlyAndDropCurbExceptions()
    {
        InletProvider p(QStringLiteral("D"));
        InletPropertyBag bag;

        // Open fraction / splash-over velocity only apply to a GENERIC grate.
        InletDesignData d = sampleDesign(InletType::Grate);
        d.grateType = GrateType::PBar50;
        p.setDesign(d);
        bag.bind(&p);
        QVERIFY(bag.isPropertyVisible(QStringLiteral("grateLength")));
        QVERIFY(!bag.isPropertyVisible(QStringLiteral("openFraction")));
        QVERIFY(!bag.isPropertyVisible(QStringLiteral("splashVelocity")));

        d.grateType = GrateType::Generic;
        p.setDesign(d);
        QVERIFY(bag.isPropertyVisible(QStringLiteral("openFraction")));
        QVERIFY(bag.isPropertyVisible(QStringLiteral("splashVelocity")));

        // The throat angle does not apply to a DROP CURB.
        p.setDesign(sampleDesign(InletType::Curb));
        QVERIFY(bag.isPropertyVisible(QStringLiteral("throatAngle")));
        p.setDesign(sampleDesign(InletType::DropCurb));
        QVERIFY(bag.isPropertyVisible(QStringLiteral("curbHeight")));
        QVERIFY(!bag.isPropertyVisible(QStringLiteral("throatAngle")));
    }

    void propertyBag_EditPushesTheWholeDesign()
    {
        InletProvider p(QStringLiteral("D"));
        p.setDesign(sampleDesign(InletType::Grate));

        InletPropertyBag bag;
        bag.bind(&p);
        bag.setGrateLength(7.5);
        QCOMPARE(p.grateLength(), 7.5);
        // Untouched fields survive the whole-design write.
        QCOMPARE(p.curbHeight(), sampleDesign(InletType::Grate).curbHeight);

        // With a commit handler installed the provider is NOT written directly;
        // the host owns the write (that is where the undo command is pushed).
        InletDesignData seenBefore, seenAfter;
        int calls = 0;
        bag.setCommitHandler([&](const InletDesignData &b, const InletDesignData &a) {
            seenBefore = b; seenAfter = a; ++calls;
        });
        bag.setGrateWidth(4.25);
        QCOMPARE(calls, 1);
        QCOMPARE(seenBefore.grateWidth, sampleDesign(InletType::Grate).grateWidth);
        QCOMPARE(seenAfter.grateWidth, 4.25);
        QCOMPARE(p.grateWidth(), sampleDesign(InletType::Grate).grateWidth);
    }
};

QTEST_MAIN(TestInletEditor)
#include "test_inlet_editor.moc"
