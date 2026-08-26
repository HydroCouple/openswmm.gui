/*!
 * \file   test_mesheditingtoolbar_bc.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  The mesh-editing toolbar's BC editor: hydration, mixed-selection
 *         placeholders, and — above all — that DISPLAYING a selection never
 *         WRITES to it.
 *
 * First test to instantiate MeshEditingToolbar. Pins the fixes for the
 * bulk-assign-then-reselect defect family: the single-edge branch never
 * hydrated the BC widgets (stale timeseries shown); multi-edge hydration read
 * an arbitrary hash-ordered edge; and the unblocked stage-TS combo turned a
 * display refresh into a bulk overwrite of every selected edge.
 */
#include "layers/swmm2dmeshlayer.h"
#include "map/mapcanvas.h"
#include "map/mapundostack.h"
#include "map/meshcommands.h"
#include "mesh/meshobjectref.h"
#include "mesh/meshresult.h"
#include "selection/selectionmanager.h"
#include "ui/panels/meshattributetablemodel.h"
#include "ui/toolbars/mesheditingtoolbar.h"

#include <QComboBox>
#include <QDoubleSpinBox>
#include <QLineEdit>
#include <QTest>

using Kind = MeshAttributeTableModel::Kind;
using mesh::MeshBCTypes;

namespace {

/*! `wide` × 1 strip of unit quads, two triangles each (the
 *  test_meshcommands_vertex_edge fixture). */
mesh::MeshResult makeStrip(int wide)
{
    mesh::MeshResult m;
    for (int x = 0; x <= wide; ++x) {
        mesh::MeshVertex b; b.xy = QPointF(double(x), 0.0); m.vertices.append(b);
        mesh::MeshVertex t; t.xy = QPointF(double(x), 1.0); m.vertices.append(t);
    }
    for (int x = 0; x < wide; ++x) {
        const int b0 = x * 2, t0 = x * 2 + 1, b1 = (x + 1) * 2, t1 = (x + 1) * 2 + 1;
        mesh::MeshTriangle a; a.v0 = b0; a.v1 = b1; a.v2 = t1; m.triangles.append(a);
        mesh::MeshTriangle c; c.v0 = b0; c.v1 = t1; c.v2 = t0; m.triangles.append(c);
    }
    m.ok = true;
    return m;
}

/*! First \p n boundary slots on \p layer, in (tri, e) scan order. */
QVector<QPair<int, int>> boundarySlots(const SWMM2DMeshLayer &layer, int n)
{
    QVector<QPair<int, int>> out;
    for (int t = 0; t < layer.triangleCount() && out.size() < n; ++t)
        for (int k = 0; k < 3 && out.size() < n; ++k)
            if (layer.isBoundaryEdge(t, k)) out.append({t, k});
    return out;
}

mesh::MeshEdgeBC stageTsBC(const QString &ts)
{
    mesh::MeshEdgeBC bc;
    bc.type = MeshBCTypes::Type::SpecifiedStageTS;
    bc.tseries = ts;
    return bc;
}

/*! Everything one test needs, wired the way SWMMVis wires it. */
struct Rig {
    MapCanvas canvas;
    SWMM2DMeshLayer *layer;                 // owned by the canvas
    SelectionManager sel;
    MeshEditingToolbar tb{QStringLiteral("t")};

    QComboBox *typeCombo;
    QComboBox *stageTs;

    explicit Rig(int wide = 4)
        : layer(new SWMM2DMeshLayer(makeStrip(wide), QString()))
    {
        tb.setTimeseriesLister(
            [] { return QStringList{QStringLiteral("TS_A"),
                                    QStringLiteral("TS_B"),
                                    QStringLiteral("TS_C")}; });
        canvas.addLayer(layer, /*pushUndo=*/false);
        tb.rebindCanvas(&canvas);
        tb.rebindSelectionManager(&sel);
        typeCombo = tb.findChild<QComboBox *>(QStringLiteral("meshBcTypeCombo"));
        stageTs   = tb.findChild<QComboBox *>(QStringLiteral("meshBcStageTSCombo"));
    }

    void select(const QVector<QPair<int, int>> &edgeSlots)
    {
        QSet<SWMMObjectRef> refs;
        for (const auto &pr : edgeSlots)
            refs.insert(mesh::MeshObjectRef::edge(QString(), pr.first, pr.second));
        sel.select(refs, SelectionManager::Mode::Replace);
    }
};

} // namespace

class TestMeshEditingToolbarBc : public QObject
{
    Q_OBJECT
private slots:

    /*! The reported bug: after a bulk assignment, selecting one edge must
     *  show THAT edge's timeseries, not the last multi-selection's. */
    void singleEdge_rehydratesOwnTimeseries()
    {
        Rig rig;
        QVERIFY(rig.tb.activeMesh() == rig.layer);
        QVERIFY(rig.typeCombo && rig.stageTs);
        const auto bs = boundarySlots(*rig.layer, 2);
        QCOMPARE(bs.size(), 2);
        QVERIFY(mesh::pushEdgeBCEdit(rig.layer, {bs[0]},
                                     stageTsBC(QStringLiteral("TS_A")), nullptr) > 0);
        QVERIFY(mesh::pushEdgeBCEdit(rig.layer, {bs[1]},
                                     stageTsBC(QStringLiteral("TS_B")), nullptr) > 0);

        rig.select({bs[0]});
        QCOMPARE(rig.stageTs->currentText(), QStringLiteral("TS_A"));
        QCOMPARE(static_cast<MeshBCTypes::Type>(
                     rig.typeCombo->currentData().toInt()),
                 MeshBCTypes::Type::SpecifiedStageTS);

        rig.select({bs[1]});
        QCOMPARE(rig.stageTs->currentText(), QStringLiteral("TS_B"));
    }

    /*! Selection changes are display-only: no undo entries, no BC bytes
     *  change — pre-fix, the unblocked TS combo bulk-overwrote the
     *  selection with an arbitrary edge's value on mere selection. */
    void selectionChange_neverWrites()
    {
        Rig rig;
        const auto bs = boundarySlots(*rig.layer, 3);
        QCOMPARE(bs.size(), 3);
        QVERIFY(mesh::pushEdgeBCEdit(rig.layer, {bs[0]},
                                     stageTsBC(QStringLiteral("TS_A")), nullptr) > 0);
        QVERIFY(mesh::pushEdgeBCEdit(rig.layer, {bs[1]},
                                     stageTsBC(QStringLiteral("TS_B")), nullptr) > 0);
        // bs[2] stays at the default Wall.

        const int undoBefore = rig.canvas.undoStack()->count();
        const QVector<mesh::MeshEdgeBC> bcBefore = rig.layer->edgeBCs();

        rig.select({bs[0], bs[1]});
        rig.select({bs[0], bs[1], bs[2]});
        rig.select({bs[1]});
        rig.select({bs[0]});

        QCOMPARE(rig.canvas.undoStack()->count(), undoBefore);
        const QVector<mesh::MeshEdgeBC> bcAfter = rig.layer->edgeBCs();
        QCOMPARE(bcAfter.size(), bcBefore.size());
        for (int i = 0; i < bcBefore.size(); ++i) {
            QCOMPARE(bcAfter[i].type,    bcBefore[i].type);
            QCOMPARE(bcAfter[i].tseries, bcBefore[i].tseries);
            QCOMPARE(bcAfter[i].head,    bcBefore[i].head);
        }
    }

    /*! Mixed selections state their mixedness instead of impersonating an
     *  arbitrary member. */
    void mixedSelection_showsPlaceholders()
    {
        Rig rig;
        const auto bs = boundarySlots(*rig.layer, 3);
        QVERIFY(mesh::pushEdgeBCEdit(rig.layer, {bs[0]},
                                     stageTsBC(QStringLiteral("TS_A")), nullptr) > 0);
        // bs[1] stays Wall → mixed TYPE.
        rig.select({bs[0], bs[1]});
        QCOMPARE(rig.typeCombo->currentIndex(), -1);

        // Same type, different TS → placeholder, empty text.
        QVERIFY(mesh::pushEdgeBCEdit(rig.layer, {bs[1]},
                                     stageTsBC(QStringLiteral("TS_B")), nullptr) > 0);
        rig.select({bs[0], bs[1]});
        QVERIFY(rig.typeCombo->currentIndex() >= 0);
        QCOMPARE(rig.stageTs->currentText(), QString());
        QVERIFY(rig.stageTs->lineEdit());
        QCOMPARE(rig.stageTs->lineEdit()->placeholderText(),
                 QStringLiteral("<multiple>"));
    }

    /*! A real bulk edit is exactly ONE undoable command, every selected
     *  edge takes the new value, and undo restores every original — the
     *  re-entrancy falsifier: pre-fix, the per-slot attributeChanged fired
     *  a refresh whose unblocked combo pushed a NESTED command that
     *  rewrote already-updated edges with the old value. */
    void bulkAssign_isOneCommand_allEdgesGetNewValue()
    {
        Rig rig;
        const auto bs = boundarySlots(*rig.layer, 3);
        QVERIFY(mesh::pushEdgeBCEdit(rig.layer, {bs[0]},
                                     stageTsBC(QStringLiteral("TS_A")), nullptr) > 0);
        QVERIFY(mesh::pushEdgeBCEdit(rig.layer, {bs[1]},
                                     stageTsBC(QStringLiteral("TS_B")), nullptr) > 0);
        QVERIFY(mesh::pushEdgeBCEdit(rig.layer, {bs[2]},
                                     stageTsBC(QStringLiteral("TS_C")), nullptr) > 0);

        rig.select({bs[0], bs[1], bs[2]});
        const int undoBefore = rig.canvas.undoStack()->count();

        // The user's gesture: pick TS_B in the (visible, unblocked) combo.
        rig.stageTs->setCurrentText(QStringLiteral("TS_B"));

        QCOMPARE(rig.canvas.undoStack()->count(), undoBefore + 1);
        const auto &bcs = rig.layer->edgeBCs();
        for (const auto &pr : bs)
            QCOMPARE(bcs[pr.first * 3 + pr.second].tseries,
                     QStringLiteral("TS_B"));

        rig.canvas.undoStack()->undo();
        QCOMPARE(rig.layer->edgeBCs()[bs[0].first * 3 + bs[0].second].tseries,
                 QStringLiteral("TS_A"));
        QCOMPARE(rig.layer->edgeBCs()[bs[1].first * 3 + bs[1].second].tseries,
                 QStringLiteral("TS_B"));
        QCOMPARE(rig.layer->edgeBCs()[bs[2].first * 3 + bs[2].second].tseries,
                 QStringLiteral("TS_C"));
    }

    /*! CLAUDE.md §5.1: the toolbar and the Attribute Table read one model
     *  and must agree per edge. */
    void toolbarAndAttributeTable_agree()
    {
        Rig rig;
        const auto bs = boundarySlots(*rig.layer, 3);
        // Type first (a Wall-typed selection would correctly commit Walls
        // and ignore the TS text), then the user's TS pick.
        for (const auto &pr : bs)
            QVERIFY(mesh::pushEdgeBCEdit(rig.layer, {pr},
                                         stageTsBC(QStringLiteral("TS_A")),
                                         nullptr) > 0);
        rig.select({bs[0], bs[1], bs[2]});
        rig.stageTs->setCurrentText(QStringLiteral("TS_B"));

        MeshAttributeTableModel model;
        model.setSource(rig.layer, Kind::Edge);
        int tsCol = -1;   // Edges tab: key "tseries", header "Time Series"
        for (int c = 0; c < model.columnCount(); ++c)
            if (model.headerData(c, Qt::Horizontal).toString()
                    .compare(QStringLiteral("Time Series"),
                             Qt::CaseInsensitive) == 0)
                { tsCol = c; break; }
        QVERIFY2(tsCol >= 0, "no timeseries column in the Edges tab");

        for (const auto &pr : bs) {
            const int row = model.rowForRef(
                mesh::MeshObjectRef::edge(QString(), pr.first, pr.second));
            QVERIFY(row >= 0);
            QCOMPARE(model.data(model.index(row, tsCol)).toString(),
                     QStringLiteral("TS_B"));
            rig.select({pr});
            QCOMPARE(rig.stageTs->currentText(), QStringLiteral("TS_B"));
        }
    }

    /*! With a mixed-type display (combo index -1), a param edit must not
     *  commit — currentData() would decode as Wall and silently wall off
     *  the whole selection. */
    void typeFlip_withInvalidCombo_isNoop()
    {
        Rig rig;
        const auto bs = boundarySlots(*rig.layer, 2);
        QVERIFY(mesh::pushEdgeBCEdit(rig.layer, {bs[0]},
                                     stageTsBC(QStringLiteral("TS_A")), nullptr) > 0);
        rig.select({bs[0], bs[1]});           // StageTS + Wall → mixed
        QCOMPARE(rig.typeCombo->currentIndex(), -1);

        const int undoBefore = rig.canvas.undoStack()->count();
        const QVector<mesh::MeshEdgeBC> bcBefore = rig.layer->edgeBCs();
        rig.stageTs->setCurrentText(QStringLiteral("TS_C"));   // unblocked

        QCOMPARE(rig.canvas.undoStack()->count(), undoBefore);
        for (int i = 0; i < bcBefore.size(); ++i)
            QCOMPARE(rig.layer->edgeBCs()[i].type, bcBefore[i].type);
    }
};

QTEST_MAIN(TestMeshEditingToolbarBc)
#include "test_mesheditingtoolbar_bc.moc"
