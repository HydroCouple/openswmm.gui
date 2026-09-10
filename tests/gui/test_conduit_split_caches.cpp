/*!
 * \file   test_conduit_split_caches.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Render-cache invariants across a conduit split
 *         (workplans/HANDOFF_SPLIT_LINK_DISAPPEARS_2026-09-10.md §Verification).
 *
 *         Inserting a junction on a conduit splits it in two through
 *         InsertJunctionSplitCommand → SWMMModelLayer::applyInsertJunctionSplit
 *         → syncSplitCaches. The QSG renderer walks the render-facing SoA
 *         (m_linkVertexCount / m_linkVertexOffset / m_linkSceneFlat), not
 *         m_links, so the two must never diverge (commit 167daea was the same
 *         class of fault). tests/unit/test_junction_split_layer.cpp drives the
 *         engine only; this is the first coverage of the GUI cache path.
 *
 *         After the split (and again after undo() and after redo()):
 *           - renderLinkCount() == cachedLinkCount()
 *           - the new link resolves to the same index in the layer and the engine
 *           - both halves' polylines have >= 2 points (a dropped endpoint is the
 *             `vcount < 2` failure the renderer silently skips)
 *           - both halves join at the inserted node's coordinate
 *           - the new link is not hidden
 *         Both refreshSceneCoordsForLink branches are covered: a straight
 *         conduit (in-place rewrite) and one with an interior vertex (vertex
 *         count changes → full rebuildSceneCoords).
 */

#include "layers/swmmmodellayer.h"
#include "map/mapundostack.h"

#include <QDir>
#include <QObject>
#include <QPointF>
#include <QString>
#include <QTest>
#include <QVector>

#include <cmath>
#include <memory>

#include <openswmm/engine/openswmm_links.h>
#include <openswmm/engine/openswmm_nodes.h>

namespace {

QString dataDir()
{
    return qEnvironmentVariable("SWMMVIS_GUI_TEST_DATA", QStringLiteral("."));
}

std::unique_ptr<SWMMModelLayer> openLayer(const QString &fixture)
{
    auto layer = std::make_unique<SWMMModelLayer>(QDir(dataDir()).filePath(fixture), nullptr);
    QList<QString> warnings, errors;
    if (!layer->loadModel(warnings, errors)) return nullptr;
    return layer;
}

bool samePoint(const QPointF &a, const QPointF &b)
{
    return std::abs(a.x() - b.x()) < 1e-6 && std::abs(a.y() - b.y()) < 1e-6;
}

/*! Every invariant the handoff lists, as one call so it can be repeated
 *  after redo / undo / redo. Returns an empty string on success. */
QString checkSplitInvariants(SWMMModelLayer *layer, const QString &origLink,
                             const QString &newLink, const QString &newNode,
                             const QString &toNode, int expectedTotalPoints)
{
    if (layer->renderLinkCount() != layer->cachedLinkCount())
        return QStringLiteral("render SoA / m_links diverged: renderLinkCount=%1 cachedLinkCount=%2")
            .arg(layer->renderLinkCount()).arg(layer->cachedLinkCount());

    const int newIdx = layer->linkIndex(newLink);
    if (newIdx < 0) return QStringLiteral("%1 not found in the layer").arg(newLink);
    const int engIdx = swmm_link_index(layer->engine(), newLink.toUtf8().constData());
    if (newIdx != engIdx)
        return QStringLiteral("%1: layer index %2 != engine index %3").arg(newLink).arg(newIdx).arg(engIdx);
    if (newIdx >= layer->renderLinkCount())
        return QStringLiteral("%1 index %2 is outside the render SoA (%3)")
            .arg(newLink).arg(newIdx).arg(layer->renderLinkCount());

    const int origIdx = layer->linkIndex(origLink);
    if (origIdx < 0) return QStringLiteral("%1 not found in the layer").arg(origLink);

    const QVector<QPointF> polyNew  = layer->cachedLinkPolyline(newIdx);
    const QVector<QPointF> polyOrig = layer->cachedLinkPolyline(origIdx);
    if (polyNew.size() < 2)
        return QStringLiteral("%1 polyline has %2 point(s) — an endpoint was dropped")
            .arg(newLink).arg(polyNew.size());
    if (polyOrig.size() < 2)
        return QStringLiteral("%1 polyline has %2 point(s) — an endpoint was dropped")
            .arg(origLink).arg(polyOrig.size());
    if (polyNew.size() + polyOrig.size() != expectedTotalPoints)
        return QStringLiteral("halves carry %1 + %2 points, expected %3 in total")
            .arg(polyOrig.size()).arg(polyNew.size()).arg(expectedTotalPoints);

    const int nodeIdx = layer->nodeIndex(newNode);
    if (nodeIdx < 0) return QStringLiteral("%1 not found in the layer").arg(newNode);
    double nx = 0.0, ny = 0.0;
    if (!layer->cachedNodeCoord(nodeIdx, &nx, &ny))
        return QStringLiteral("%1 has no cached coordinate").arg(newNode);
    const QPointF split(nx, ny);
    if (!samePoint(polyOrig.last(), split))
        return QStringLiteral("%1 does not end at the inserted node (%2,%3) but at (%4,%5)")
            .arg(origLink).arg(nx).arg(ny).arg(polyOrig.last().x()).arg(polyOrig.last().y());
    if (!samePoint(polyNew.first(), split))
        return QStringLiteral("%1 does not start at the inserted node (%2,%3) but at (%4,%5)")
            .arg(newLink).arg(nx).arg(ny).arg(polyNew.first().x()).arg(polyNew.first().y());

    double tx = 0.0, ty = 0.0;
    if (!layer->cachedNodeCoord(layer->nodeIndex(toNode), &tx, &ty))
        return QStringLiteral("%1 has no cached coordinate").arg(toNode);
    if (!samePoint(polyNew.last(), QPointF(tx, ty)))
        return QStringLiteral("%1 does not end at %2").arg(newLink, toNode);

    if (layer->hiddenObjects().contains(newLink))
        return QStringLiteral("%1 is in the hidden set").arg(newLink);
    return QString();
}

/*! The pre-split state: one link fewer, the original back on its old
 *  downstream node, and the SoA still in step with m_links. */
QString checkFusedInvariants(SWMMModelLayer *layer, const QString &origLink,
                             const QString &newLink, const QString &newNode,
                             const QString &toNode, int linkCount)
{
    if (layer->renderLinkCount() != layer->cachedLinkCount())
        return QStringLiteral("after fuse: renderLinkCount=%1 cachedLinkCount=%2")
            .arg(layer->renderLinkCount()).arg(layer->cachedLinkCount());
    if (layer->cachedLinkCount() != linkCount)
        return QStringLiteral("after fuse: %1 links, expected %2")
            .arg(layer->cachedLinkCount()).arg(linkCount);
    if (layer->linkIndex(newLink) >= 0) return QStringLiteral("%1 survived the fuse").arg(newLink);
    if (layer->nodeIndex(newNode) >= 0) return QStringLiteral("%1 survived the fuse").arg(newNode);
    const QVector<QPointF> poly = layer->cachedLinkPolyline(layer->linkIndex(origLink));
    double tx = 0.0, ty = 0.0;
    if (!layer->cachedNodeCoord(layer->nodeIndex(toNode), &tx, &ty))
        return QStringLiteral("%1 has no cached coordinate").arg(toNode);
    if (poly.size() < 2 || !samePoint(poly.last(), QPointF(tx, ty)))
        return QStringLiteral("%1 does not end at %2 after the fuse").arg(origLink, toNode);
    return QString();
}

} // namespace

class TestConduitSplitCaches : public QObject
{
    Q_OBJECT

private slots:
    void splitKeepsRenderCachesInStep_data()
    {
        QTest::addColumn<QString>("fixture");
        QTest::addColumn<QString>("link");
        QTest::addColumn<QString>("toNode");
        QTest::addColumn<double>("t");
        QTest::addColumn<int>("totalPoints");   // orig + new polyline points

        // C1 J1(0,0) → J2(1000,0), no [VERTICES]: the in-place
        // refreshSceneCoordsForLink branch. 2 + 2 points.
        QTest::newRow("straight")
            << QStringLiteral("selection_trace_fixture.inp")
            << QStringLiteral("C1") << QStringLiteral("J2") << 0.5 << 4;
        // C1 J1(0,0) → (500,50) → X1(1000,0): the interior vertex ends up on
        // one half, so its vertex count changes and refreshSceneCoordsForLink
        // falls through to a full rebuildSceneCoords(). 2 + 3 points.
        QTest::newRow("interior-vertex")
            << QStringLiteral("typed_selection_fixture.inp")
            << QStringLiteral("C1") << QStringLiteral("X1") << 0.25 << 5;
    }

    void splitKeepsRenderCachesInStep()
    {
        QFETCH(QString, fixture);
        QFETCH(QString, link);
        QFETCH(QString, toNode);
        QFETCH(double, t);
        QFETCH(int, totalPoints);

        auto layer = openLayer(fixture);
        QVERIFY2(layer, qPrintable(QStringLiteral("could not load %1").arg(fixture)));
        QCOMPARE(layer->renderLinkCount(), layer->cachedLinkCount());
        const int linksBefore = layer->cachedLinkCount();

        // Names the map tool would generate (nextNodeName / nextLinkName).
        const QString newNode = QStringLiteral("JS1");
        const QString newLink = link + QStringLiteral("_B");

        MapUndoStack stack;
        stack.push(new InsertJunctionSplitCommand(layer.get(), link, t,
                                                  newNode, newLink, /*canvas=*/nullptr));
        QCOMPARE(layer->cachedLinkCount(), linksBefore + 1);
        QString err = checkSplitInvariants(layer.get(), link, newLink, newNode, toNode, totalPoints);
        QVERIFY2(err.isEmpty(), qPrintable(QStringLiteral("after split: ") + err));

        // The tool selects the inserted node right after the push, which
        // re-derives the selection / hidden flag arrays.
        layer->setSelectedElements({ { newNode, SWMMModelLayer::kKindNode } });
        err = checkSplitInvariants(layer.get(), link, newLink, newNode, toNode, totalPoints);
        QVERIFY2(err.isEmpty(), qPrintable(QStringLiteral("after selection: ") + err));

        stack.undo();
        err = checkFusedInvariants(layer.get(), link, newLink, newNode, toNode, linksBefore);
        QVERIFY2(err.isEmpty(), qPrintable(QStringLiteral("after undo: ") + err));

        stack.redo();
        QCOMPARE(layer->cachedLinkCount(), linksBefore + 1);
        err = checkSplitInvariants(layer.get(), link, newLink, newNode, toNode, totalPoints);
        QVERIFY2(err.isEmpty(), qPrintable(QStringLiteral("after redo: ") + err));

        // A second split on the same original conduit (handoff smoke step 5).
        const QString newNode2 = QStringLiteral("JS2");
        const QString newLink2 = link + QStringLiteral("_B1");
        stack.push(new InsertJunctionSplitCommand(layer.get(), link, 0.5,
                                                  newNode2, newLink2, nullptr));
        QCOMPARE(layer->cachedLinkCount(), linksBefore + 2);
        QCOMPARE(layer->renderLinkCount(), layer->cachedLinkCount());
        QVERIFY(layer->cachedLinkPolyline(layer->linkIndex(newLink2)).size() >= 2);
        QVERIFY(layer->cachedLinkPolyline(layer->linkIndex(newLink)).size() >= 2);
        QVERIFY(layer->cachedLinkPolyline(layer->linkIndex(link)).size() >= 2);
    }

    /*! The mechanism behind "the new downstream link disappears": hidden and
     *  selected state are keyed by name, a fused split leaves its name in
     *  both, the .oswp sidecar re-applies the hidden set by name without
     *  checking the object exists (the reporter's sidecar carried 18 such
     *  names, `C1_B` among them), and the tools regenerate exactly that
     *  name on the next split. The new link then inherited the dead one's
     *  hidden flag as soon as the tool's selection call rebuilt the flag
     *  arrays — drawn only by the selection pass, gone on deselect. */
    void splitDropsStaleNameState()
    {
        auto layer = openLayer(QStringLiteral("selection_trace_fixture.inp"));
        QVERIFY(layer);
        const QString link    = QStringLiteral("C1");
        const QString newNode = QStringLiteral("JS1");
        const QString newLink = QStringLiteral("C1_B");

        // Stale state under the names the split is about to generate — the
        // sidecar restore path (setObjectsVisible by name) and a selection
        // left over from the previous incarnation.
        layer->setObjectsVisible({ newLink, newNode }, /*visible=*/false);
        QVERIFY(layer->hiddenObjects().contains(newLink));
        layer->setSelectedElements({ { newLink, SWMMModelLayer::kKindLink },
                                     { QStringLiteral("J1"), SWMMModelLayer::kKindNode } });
        QVERIFY(layer->selectedElementNames().contains(newLink));

        MapUndoStack stack;
        stack.push(new InsertJunctionSplitCommand(layer.get(), link, 0.5,
                                                  newNode, newLink, nullptr));
        const int newIdx = layer->linkIndex(newLink);
        QVERIFY(newIdx >= 0);

        // Visible and unselected from the outset …
        QVERIFY(layer->isObjectVisible(newLink, SWMMModelLayer::CatConduits));
        QVERIFY(layer->isObjectVisible(newNode, SWMMModelLayer::CatJunctions));
        QVERIFY(!layer->hiddenObjects().contains(newLink));
        QVERIFY(!layer->hiddenObjects().contains(newNode));
        QVERIFY(!layer->selectedElementNames().contains(newLink));
        QVERIFY(layer->selectedElementNames().contains(QStringLiteral("J1")));   // unrelated selection kept
        QCOMPARE(layer->categoryCheckState(SWMMModelLayer::CatConduits), Qt::Checked);
        QVERIFY(!layer->renderLinkHidden(newIdx));

        // … and still after the tool selects the inserted node, which is the
        // rebuildFlagArrays() call that used to re-derive the stale hidden bit.
        layer->setSelectedElements({ { newNode, SWMMModelLayer::kKindNode } });
        QVERIFY2(!layer->renderLinkHidden(newIdx),
                 "the renderer-facing hidden flag came back for the new link");
        QVERIFY(layer->isObjectVisible(newLink, SWMMModelLayer::CatConduits));

        // Undo leaves the dead name behind (as it always did); the redo must
        // shed it again.
        stack.undo();
        QVERIFY(layer->linkIndex(newLink) < 0);
        stack.redo();
        const int againIdx = layer->linkIndex(newLink);
        QVERIFY(againIdx >= 0);
        layer->setSelectedElements({ { newNode, SWMMModelLayer::kKindNode } });
        QVERIFY(layer->isObjectVisible(newLink, SWMMModelLayer::CatConduits));
        QVERIFY(!layer->renderLinkHidden(againIdx));
    }

    /*! ADDNODE_SPLIT_REDESIGN_PLAN_2026-09-10 step 2: placing a storage node
     *  or flow divider ON a conduit = split, then convert the inserted
     *  junction, then apply the type's creation defaults — one undoable
     *  step whose undo converts back and re-fuses. */
    void insertNodeSplit_storageAndDivider_data()
    {
        QTest::addColumn<QString>("link");
        QTest::addColumn<QString>("toNode");
        QTest::addColumn<QString>("node");
        QTest::addColumn<int>("nodeType");
        QTest::newRow("storage") << QStringLiteral("C2") << QStringLiteral("J3")
                                 << QStringLiteral("ST1") << 2;
        QTest::newRow("divider") << QStringLiteral("C3") << QStringLiteral("O1")
                                 << QStringLiteral("DV1") << 3;
    }

    void insertNodeSplit_storageAndDivider()
    {
        QFETCH(QString, link);
        QFETCH(QString, toNode);
        QFETCH(QString, node);
        QFETCH(int, nodeType);

        auto layer = openLayer(QStringLiteral("selection_trace_fixture.inp"));
        QVERIFY(layer);
        SWMM_Engine eng = layer->engine();
        const int linksBefore = layer->cachedLinkCount();
        const int nodesBefore = layer->cachedNodeCount();
        const QString newLink = link + QStringLiteral("_B");

        MapUndoStack stack;
        auto *cmd = new InsertNodeSplitCommand(layer.get(), link, 0.5, node, newLink,
                                               nodeType, /*canvas=*/nullptr);
        stack.push(cmd);
        QVERIFY2(cmd->retyped(), qPrintable(cmd->warnings().join(QStringLiteral("; "))));

        // Split invariants hold exactly as for a plain junction …
        QString err = checkSplitInvariants(layer.get(), link, newLink, node, toNode, 4);
        QVERIFY2(err.isEmpty(), qPrintable(err));
        // … and the node carries the requested type with its defaults.
        const int idx = swmm_node_index(eng, node.toUtf8().constData());
        QVERIFY(idx >= 0);
        int type = -1;
        QCOMPARE(swmm_node_get_type(eng, idx, &type), 0);
        QCOMPARE(type, nodeType);
        if (nodeType == 2) {
            double depth = -1.0;
            QCOMPARE(swmm_node_get_max_depth(eng, idx, &depth), 0);
            QVERIFY2(depth > 0.0, "storage creation defaults were not applied");
        }
        // The Object Browser bucket follows the converted type.
        QCOMPARE(layer->objectNameAt(nodeType == 2 ? SWMMModelLayer::CatStorage
                                                   : SWMMModelLayer::CatDividers,
                                     0), node);

        // Undo: back to a junction, then fused away.
        stack.undo();
        err = checkFusedInvariants(layer.get(), link, newLink, node, toNode, linksBefore);
        QVERIFY2(err.isEmpty(), qPrintable(QStringLiteral("after undo: ") + err));
        QCOMPARE(layer->cachedNodeCount(), nodesBefore);

        // Redo: the conversion and defaults are re-applied.
        stack.redo();
        QVERIFY(cmd->retyped());
        type = -1;
        QCOMPARE(swmm_node_get_type(eng, swmm_node_index(eng, node.toUtf8().constData()), &type), 0);
        QCOMPARE(type, nodeType);
        QCOMPARE(layer->renderLinkCount(), layer->cachedLinkCount());
        QCOMPARE(layer->cachedLinkCount(), linksBefore + 1);
    }
};

QTEST_MAIN(TestConduitSplitCaches)
#include "test_conduit_split_caches.moc"
