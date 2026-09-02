/*!
 * \file   test_bulkdelete.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Coverage for SWMMModelLayer::BulkEdit and BulkEditCommand.
 *
 *         Deleting a selection used to run the full "the model changed"
 *         cascade once per object: rebuildCategoryIndex() (O(N) hash over
 *         ~1.1M entries on an all-pipes model), a link-spatial-grid rebuild
 *         per cascade link, recomputeExtentFromCaches(), and a
 *         repaintRequested()/geometryChanged() pair whose listeners each do
 *         O(model) work of their own. Measured on ww_2024.inp (103,821 nodes
 *         / 281,049 links), deleting 100 junctions cost 20.7 s, of which only
 *         4.6 s was the engine; undo cost another 16.1 s because the add path
 *         replays the same storm.
 *
 *         BulkEdit defers all of that to one rebuild at batch end.
 *
 *         This file exists because NO test previously constructed a
 *         DeleteObjectCommand at all — the four delete tests in
 *         test_selectionops.cpp all take AttributeTablePanel's headless
 *         fallback branch, so neither the command nor the macro was covered.
 *
 *         The load-bearing test here is bulkDeleteLeavesCachesCoherent().
 *         Skipping compactNodeSceneEntry() also skips the fromNodeIdx /
 *         toNodeIdx renumber it performs, and that field is authoritative
 *         data rather than a derived cache — buildGeometryCache() consumes it
 *         and does not restore it. endBulkEdit() therefore re-reads both
 *         endpoints from the engine. Without that resync every link after a
 *         deleted node draws to the WRONG endpoints, with no error anywhere.
 *
 *         Fixture: selection_trace_fixture.inp (see its [TITLE] for topology).
 *           Nodes  J1 J2 J3 J4 (junctions), O1 (outfall)
 *           Links  C1 J1->J2, C2 J2->J3, C3 J3->O1, P1 J4->J3 (pump)
 */
#include "layers/swmmmodellayer.h"
#include "map/mapundostack.h"
#include "selection/selectionmanager.h"
#include "ui/panels/attributetablepanel.h"

#include <QComboBox>
#include <QDir>
#include <QElapsedTimer>
#include <QObject>
#include <QSignalSpy>
#include <QString>
#include <QStringList>
#include <QTest>
#include <QUndoCommand>

#include <algorithm>
#include <memory>
#include <utility>

#include <openswmm/engine/openswmm_links.h>
#include <openswmm/engine/openswmm_nodes.h>
#include <openswmm/engine/openswmm_spatial.h>

namespace {

QString dataDir()
{
    return qEnvironmentVariable("SWMMVIS_GUI_TEST_DATA", QStringLiteral("."));
}

std::unique_ptr<SWMMModelLayer> openLayer()
{
    auto layer = std::make_unique<SWMMModelLayer>(
        QDir(dataDir()).filePath(QStringLiteral("selection_trace_fixture.inp")),
        nullptr);
    QList<QString> warnings, errors;
    if (!layer->loadModel(warnings, errors)) return nullptr;
    return layer;
}

//! Model-space coordinate of a node, straight from the engine — the
//! independent oracle the cache assertions are checked against.
QPointF engineNodeXY(SWMM_Engine eng, const QString &name)
{
    const QByteArray utf8 = name.toUtf8();
    const int idx = swmm_node_index(eng, utf8.constData());
    if (idx < 0) return {};
    double x = 0, y = 0;
    swmm_spatial_get_node_coord(eng, idx, &x, &y);
    return {x, y};
}

} // namespace

class TestBulkDelete : public QObject
{
    Q_OBJECT

private slots:

    // ── The guard collapses the storm ──────────────────────────────────────

    //! Pins the whole plan: N deletions, one repaint + one geometryChanged.
    void macroDeleteEmitsOneGeometryChanged()
    {
        auto layer = openLayer();
        QVERIFY(layer);

        QSignalSpy geom(layer.get(), &SWMMModelLayer::geometryChanged);
        QSignalSpy paint(layer.get(), &SWMMModelLayer::repaintRequested);

        MapUndoStack stack;
        auto *macro = new BulkEditCommand(layer.get(), QStringLiteral("Delete 3"));
        for (const QString &n : {QStringLiteral("J1"), QStringLiteral("J2"),
                                 QStringLiteral("J4")})
            new DeleteObjectCommand(layer.get(), n,
                                    DeleteObjectCommand::DeleteNode, nullptr, macro);
        stack.push(macro);

        // Three nodes deleted (plus their cascade links) — one notification.
        QCOMPARE(geom.count(), 1);
        QCOMPARE(paint.count(), 1);

        // ...and the deletions really happened, so the count above is not 1
        // because nothing ran.
        QCOMPARE(layer->categoryCount(SWMMModelLayer::CatJunctions), 1);  // J3 only
        QCOMPARE(swmm_node_index(layer->engine(), "J1"), -1);
        QCOMPARE(swmm_node_index(layer->engine(), "J2"), -1);
        QCOMPARE(swmm_node_index(layer->engine(), "J4"), -1);
    }

    //! Pins undo/redo symmetry — the add path needs the guard as much as the
    //! delete path, or Ctrl+Z is slower than the delete it reverses.
    void undoRestoresAllObjectsAndEmitsOnce()
    {
        auto layer = openLayer();
        QVERIFY(layer);
        const int nodes0 = layer->categoryCount(SWMMModelLayer::CatJunctions);
        const int cond0  = layer->categoryCount(SWMMModelLayer::CatConduits);

        MapUndoStack stack;
        auto *macro = new BulkEditCommand(layer.get(), QStringLiteral("Delete 2"));
        for (const QString &n : {QStringLiteral("J1"), QStringLiteral("J2")})
            new DeleteObjectCommand(layer.get(), n,
                                    DeleteObjectCommand::DeleteNode, nullptr, macro);
        stack.push(macro);
        QCOMPARE(layer->categoryCount(SWMMModelLayer::CatJunctions), nodes0 - 2);

        QSignalSpy geom(layer.get(), &SWMMModelLayer::geometryChanged);
        QSignalSpy paint(layer.get(), &SWMMModelLayer::repaintRequested);
        stack.undo();

        QCOMPARE(geom.count(), 1);
        QCOMPARE(paint.count(), 1);
        QCOMPARE(layer->categoryCount(SWMMModelLayer::CatJunctions), nodes0);
        QCOMPARE(layer->categoryCount(SWMMModelLayer::CatConduits), cond0);

        // Properties came back, not just the names.
        SWMM_Engine eng = layer->engine();
        const int j1 = swmm_node_index(eng, "J1");
        QVERIFY(j1 >= 0);
        double invert = 0;
        QCOMPARE(swmm_node_get_invert_elev(eng, j1, &invert), 0);
        QCOMPARE(invert, 100.0);
        QCOMPARE(engineNodeXY(eng, QStringLiteral("J1")), QPointF(0.0, 0.0));
    }

    void redoAfterUndoIsSymmetric()
    {
        auto layer = openLayer();
        QVERIFY(layer);
        const int nodes0 = layer->categoryCount(SWMMModelLayer::CatJunctions);

        MapUndoStack stack;
        auto *macro = new BulkEditCommand(layer.get(), QStringLiteral("Delete 2"));
        for (const QString &n : {QStringLiteral("J1"), QStringLiteral("J2")})
            new DeleteObjectCommand(layer.get(), n,
                                    DeleteObjectCommand::DeleteNode, nullptr, macro);
        stack.push(macro);
        stack.undo();

        QSignalSpy geom(layer.get(), &SWMMModelLayer::geometryChanged);
        stack.redo();
        QCOMPARE(geom.count(), 1);
        QCOMPARE(layer->categoryCount(SWMMModelLayer::CatJunctions), nodes0 - 2);
        QCOMPARE(swmm_node_index(layer->engine(), "J1"), -1);
    }

    // ── The silent-corruption guard ────────────────────────────────────────

    //! THE critical test. Deleting SoA index 0 shifts every later node index,
    //! so any link whose endpoint index is not renumbered now resolves to the
    //! WRONG node — and nothing errors, the model just draws wrong.
    //!
    //! Falsifier: comment out syncLinkEndpointIndicesFromEngine() in
    //! endBulkEdit() and C3's polyline starts at J4 (2000,-800) instead of
    //! J3 (2000,0).
    void bulkDeleteLeavesCachesCoherent()
    {
        auto layer = openLayer();
        QVERIFY(layer);
        SWMM_Engine eng = layer->engine();

        // J1 is node index 0, and C3 (J3->O1) is downstream of it in index
        // order, so deleting J1 shifts C3's from-node index by one.
        QCOMPARE(swmm_node_index(eng, "J1"), 0);
        const QPointF j3xy = engineNodeXY(eng, QStringLiteral("J3"));
        const QPointF j4xy = engineNodeXY(eng, QStringLiteral("J4"));
        QVERIFY(j3xy != j4xy);   // else the assertion below could not fail

        MapUndoStack stack;
        auto *macro = new BulkEditCommand(layer.get(), QStringLiteral("Delete J1"));
        new DeleteObjectCommand(layer.get(), QStringLiteral("J1"),
                                DeleteObjectCommand::DeleteNode, nullptr, macro);
        stack.push(macro);

        // Counts agree with the engine after the deferred rebuild.
        QCOMPARE(layer->categoryCount(SWMMModelLayer::CatJunctions),
                 3);                                   // J2 J3 J4 (J1 gone)
        QCOMPARE(layer->categoryCount(SWMMModelLayer::CatConduits),
                 2);                                   // C1 cascaded with J1

        // Name->index maps agree with the engine, for every survivor.
        for (const char *n : {"J2", "J3", "J4", "O1"})
            QCOMPARE(layer->nodeIndex(QString::fromLatin1(n)),
                     swmm_node_index(eng, n));
        for (const char *l : {"C2", "C3", "P1"})
            QCOMPARE(layer->linkIndex(QString::fromLatin1(l)),
                     swmm_link_index(eng, l));

        // The one that matters: C3's cached polyline must still start at J3.
        // Without the endpoint resync its stale fromNodeIdx now names J4.
        const int c3 = layer->linkIndex(QStringLiteral("C3"));
        QVERIFY(c3 >= 0);
        const QVector<QPointF> poly = layer->cachedLinkPolyline(c3);
        QVERIFY(!poly.isEmpty());
        QCOMPARE(poly.first(), j3xy);
        QCOMPARE(poly.last(), engineNodeXY(eng, QStringLiteral("O1")));

        // And the pump, whose from-node J4 also shifted.
        const int p1 = layer->linkIndex(QStringLiteral("P1"));
        QVERIFY(p1 >= 0);
        const QVector<QPointF> ppoly = layer->cachedLinkPolyline(p1);
        QVERIFY(!ppoly.isEmpty());
        QCOMPARE(ppoly.first(), j4xy);
    }

    //! Same coherence check, but through undo — which re-adds nodes at the
    //! engine TAIL rather than their original index, so every link endpoint
    //! index changes again.
    void undoLeavesCachesCoherent()
    {
        auto layer = openLayer();
        QVERIFY(layer);

        MapUndoStack stack;
        auto *macro = new BulkEditCommand(layer.get(), QStringLiteral("Delete J1"));
        new DeleteObjectCommand(layer.get(), QStringLiteral("J1"),
                                DeleteObjectCommand::DeleteNode, nullptr, macro);
        stack.push(macro);
        stack.undo();

        SWMM_Engine eng = layer->engine();
        for (const char *l : {"C1", "C2", "C3", "P1"}) {
            const int li = layer->linkIndex(QString::fromLatin1(l));
            QVERIFY2(li >= 0, l);
            QCOMPARE(li, swmm_link_index(eng, l));

            // Resolve the endpoint by INDEX straight from the engine — no
            // name lookup, so the oracle shares nothing with the cache it is
            // checking.
            int fromIdx = -1;
            QCOMPARE(swmm_link_get_from_node(eng, li, &fromIdx), 0);
            double fx = 0, fy = 0;
            QCOMPARE(swmm_spatial_get_node_coord(eng, fromIdx, &fx, &fy), 0);

            const QVector<QPointF> poly = layer->cachedLinkPolyline(li);
            QVERIFY2(!poly.isEmpty(), l);
            QCOMPARE(poly.first(), QPointF(fx, fy));
        }
    }

    // ── Cascade reporting ──────────────────────────────────────────────────

    //! J2 sits between C1 (J1->J2) and C2 (J2->J3), so deleting it cascades
    //! both. Pins the cascade-name contract that step 3 re-implements on top
    //! of the engine's own SWMM_ImpactReport.
    void cascadeLinkNamesAreReported()
    {
        auto layer = openLayer();
        QVERIFY(layer);

        QStringList cascade;
        QVERIFY(layer->applyNodeDelete(QStringLiteral("J2"), &cascade));
        cascade.sort();
        QCOMPARE(cascade, QStringList({QStringLiteral("C1"), QStringLiteral("C2")}));
    }

    // ── Entry points ───────────────────────────────────────────────────────

    //! The headless branch of AttributeTablePanel::deleteObjects must batch
    //! too, so tests exercise the same path the UI does — and the terminal
    //! emission must still arrive before deleteObjects() returns (the
    //! "coalesce, never skip" rule).
    void headlessFallbackBatchesToo()
    {
        auto layer = openLayer();
        QVERIFY(layer);
        SelectionManager selMgr;
        AttributeTablePanel panel;
        panel.setProject(layer.get(), &selMgr, nullptr);
        panel.refresh();

        auto *combo = panel.findChild<QComboBox *>();
        QVERIFY(combo);
        for (int i = 0; i < combo->count(); ++i)
            if (combo->itemText(i).startsWith(QLatin1String("Junctions")))
                combo->setCurrentIndex(i);

        QSignalSpy geom(layer.get(), &SWMMModelLayer::geometryChanged);
        const int n = panel.deleteObjects({QStringLiteral("J1"),
                                           QStringLiteral("J2"),
                                           QStringLiteral("J4")});
        QCOMPARE(n, 3);
        // Already emitted by the time deleteObjects() returned — never
        // deferred past the call, only coalesced within it.
        QCOMPARE(geom.count(), 1);
        QCOMPARE(layer->categoryCount(SWMMModelLayer::CatJunctions), 1);
    }

    //! End-state contract: after a delete through the panel, the canonical
    //! selection bus must hold no ref naming a deleted object.
    //!
    //! HONEST SCOPE NOTE: this passes with AND without the explicit
    //! m_selMgr->clear() in deleteObjects() — some downstream effect of the
    //! post-delete refresh() empties the bus anyway. So it is a regression
    //! guard on the end state, NOT a pin on that call. Verified by probe:
    //! disabling the clear leaves this green.
    //!
    //! The clear is kept regardless, because dropping the canonical
    //! selection BEFORE mutating is the correct ordering, and relying on a
    //! view-bridge side effect to reach the same end state is fragile —
    //! it holds only while a panel is attached to the bus.
    void attributeTableDeleteLeavesNoPhantomRefs()
    {
        auto layer = openLayer();
        QVERIFY(layer);
        SelectionManager selMgr;
        AttributeTablePanel panel;
        panel.setProject(layer.get(), &selMgr, nullptr);
        panel.refresh();

        auto *combo = panel.findChild<QComboBox *>();
        QVERIFY(combo);
        for (int i = 0; i < combo->count(); ++i)
            if (combo->itemText(i).startsWith(QLatin1String("Junctions")))
                combo->setCurrentIndex(i);

        selMgr.select(QSet<SWMMObjectRef>{{SWMMObjectRef::Node, QStringLiteral("J1")},
                                          {SWMMObjectRef::Node, QStringLiteral("J2")}});
        QVERIFY(!selMgr.isEmpty());

        QCOMPARE(panel.deleteObjects({QStringLiteral("J1"),
                                      QStringLiteral("J2")}), 2);
        for (const SWMMObjectRef &r : selMgr.selection())
            QVERIFY2(r.name != QLatin1String("J1") && r.name != QLatin1String("J2"),
                     qPrintable(QStringLiteral("phantom ref survived: %1").arg(r.name)));
    }

    // ── Phase A2 — BatchDeleteCommand parity with the per-object path ─────

    //! The batched delete (one swmm_*_delete_many per kind) must land on
    //! exactly the state the per-object reference path produces, and its
    //! undo must restore exactly what per-object undo restores.
    void batchDeleteMatchesPerObjectReference()
    {
        auto batchLayer = openLayer();
        auto refLayer   = openLayer();
        QVERIFY(batchLayer);
        QVERIFY(refLayer);

        const QStringList victims{QStringLiteral("J1"), QStringLiteral("J2"),
                                  QStringLiteral("J4")};

        MapUndoStack batchStack;
        QList<BatchDeleteCommand::Target> targets;
        for (const QString &n : victims)
            targets.append({n, DeleteObjectCommand::DeleteNode});
        QSignalSpy geom(batchLayer.get(), &SWMMModelLayer::geometryChanged);
        batchStack.push(new BatchDeleteCommand(batchLayer.get(), targets,
                                               nullptr,
                                               QStringLiteral("Batch")));
        // The batch keeps the one-notification contract.
        QCOMPARE(geom.count(), 1);

        MapUndoStack refStack;
        auto *refMacro = new BulkEditCommand(refLayer.get(),
                                             QStringLiteral("Ref"));
        for (const QString &n : victims)
            new DeleteObjectCommand(refLayer.get(), n,
                                    DeleteObjectCommand::DeleteNode, nullptr,
                                    refMacro);
        refStack.push(refMacro);

        const auto sameState = [](SWMMModelLayer *a, SWMMModelLayer *b) {
            const int nn = swmm_node_count(a->engine());
            if (nn != swmm_node_count(b->engine())) return false;
            for (int i = 0; i < nn; ++i)
                if (QString::fromUtf8(swmm_node_id(a->engine(), i))
                    != QString::fromUtf8(swmm_node_id(b->engine(), i)))
                    return false;
            const int nl = swmm_link_count(a->engine());
            if (nl != swmm_link_count(b->engine())) return false;
            for (int i = 0; i < nl; ++i)
                if (QString::fromUtf8(swmm_link_id(a->engine(), i))
                    != QString::fromUtf8(swmm_link_id(b->engine(), i)))
                    return false;
            // The SoA mirrors must agree with their engines too.
            if (a->cachedNodeCount() != nn || b->cachedNodeCount() != nn)
                return false;
            if (a->cachedLinkCount() != nl || b->cachedLinkCount() != nl)
                return false;
            return true;
        };

        QVERIFY2(sameState(batchLayer.get(), refLayer.get()),
                 "post-delete state diverged from the per-object reference");

        // Undo restores identically on both paths…
        batchStack.undo();
        refStack.undo();
        QVERIFY2(sameState(batchLayer.get(), refLayer.get()),
                 "post-undo state diverged from the per-object reference");
        QCOMPARE(swmm_node_index(batchLayer->engine(), "J1") >= 0, true);

        // …and redo re-deletes identically.
        batchStack.redo();
        refStack.redo();
        QVERIFY2(sameState(batchLayer.get(), refLayer.get()),
                 "post-redo state diverged from the per-object reference");
        QCOMPARE(swmm_node_index(batchLayer->engine(), "J1"), -1);
    }

    // ── Profiling probe (perf-plan Phase 0; skipped unless env-gated) ──────

    //! Bulk-delete baseline on a REAL model: set SWMM_PROFILE_INP=<path.inp>
    //! (the profileExternalModel idiom) and optionally
    //! SWMM_PROFILE_DELETE_N=<count> (default 1000). Reports snapshot vs
    //! execute+close ms; enable QT_LOGGING_RULES="openswmm.bulkdelete=true"
    //! for the endBulkEdit sub-splits. Never runs in CI.
    void profileBulkDelete()
    {
        const QString inp = qEnvironmentVariable("SWMM_PROFILE_INP");
        if (inp.isEmpty())
            QSKIP("set SWMM_PROFILE_INP=<path.inp> to run the bulk-delete profile");

        auto layer = std::make_unique<SWMMModelLayer>(inp, nullptr);
        QList<QString> warnings, errors;
        QVERIFY2(layer->loadModel(warnings, errors),
                 qPrintable(errors.join(QStringLiteral("; "))));

        const int junctions =
            layer->categoryCount(SWMMModelLayer::CatJunctions);
        const int wanted =
            qEnvironmentVariable("SWMM_PROFILE_DELETE_N",
                                 QStringLiteral("1000")).toInt();
        const int k = std::min(wanted, junctions);
        QVERIFY2(k > 0, "profile model has no junctions to delete");

        QStringList names;
        names.reserve(k);
        for (int i = 0; i < k; ++i)
            names.append(layer->objectNameAt(SWMMModelLayer::CatJunctions, i));

        QElapsedTimer timer;
        timer.start();
        MapUndoStack stack;
        QList<BatchDeleteCommand::Target> targets;
        targets.reserve(names.size());
        for (const QString &n : std::as_const(names))
            targets.append({n, DeleteObjectCommand::DeleteNode});
        auto *macro = new BatchDeleteCommand(layer.get(), targets, nullptr,
                                             QStringLiteral("Profile Delete"));
        const qint64 snapshotMs = timer.elapsed();
        stack.push(macro);
        const qint64 totalMs = timer.elapsed();

        qInfo().nospace() << "[profileBulkDelete] " << k << " node(s) of "
                          << junctions << " — snapshots " << snapshotMs
                          << " ms, execute+close " << (totalMs - snapshotMs)
                          << " ms, total " << totalMs << " ms";
    }
};

QTEST_MAIN(TestBulkDelete)
#include "test_bulkdelete.moc"
