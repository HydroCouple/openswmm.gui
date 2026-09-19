/*!
 * \file   test_layertree_reorder.cpp
 * \brief  Layer-tree reordering (Slice LTR-2026-09-19): category headers,
 *         layers and sublayers reorder by drag-drop (exercised through the
 *         model's mimeData/dropMimeData contract) and by the context-menu
 *         paths, and the canvas paint order follows the tree.
 *
 * Plan: workplans/LAYER_TREE_REORDER_PLAN_2026-09-19.md. The repro that
 * motivated the slice is categoryMoveWithEmptyCategoriesPresent(): before
 * the fix the tree row among NON-EMPTY categories was used as a position
 * in the all-categories order, so with any category empty the wrong group
 * moved (usually an empty one — i.e. nothing happened).
 *
 * Runs offscreen against a real MapCanvas and real layers that need no
 * files: SWMM2DResultsLayer (category "SWMM 2D Outputs", an ISublayerHost
 * with 8 sublayers), OpenSWMMVisAnnotationLayer ("Feature Layers") and
 * TabularDataLayer ("Tables").
 */
#include <QtTest>
#include <QMimeData>
#include <QSignalSpy>

#include <algorithm>

#include "layers/annotationlayer.h"
#include "layers/swmm2dresultslayer.h"
#include "layers/tabulardatalayer.h"
#include "map/mapcanvas.h"
#include "map/mapundostack.h"
#include "render/isublayer.h"
#include "render/isublayerhost.h"
#include "ui/panels/layertreecategories.h"
#include "ui/panels/layertreepanel.h"

using openswmmvis::ui::CatFeatureLayers;
using openswmmvis::ui::CatSwmm2DOutputs;
using openswmmvis::ui::CatTables;
using openswmmvis::ui::CatCount;

class TestLayerTreeReorder : public QObject
{
    Q_OBJECT

    // Bottom→top pointer list of the canvas stack, for readable QCOMPAREs.
    static QList<OpenSWMMVisLayer *> stack(const MapCanvas &c)
    {
        return c.layers();
    }

    // QCOMPARE needs identical pointer types: widen a concrete layer.
    static OpenSWMMVisLayer *L(OpenSWMMVisLayer &l) { return &l; }

    // A category row's DisplayRole is "<name> (<layer count>)"; the count is
    // asserted through the stack comparisons, so compare on the name alone.
    static QString categoryName(const LayerTreeModel &m, int row)
    {
        const QString label = m.data(m.index(row, 0), Qt::DisplayRole).toString();
        const int paren = label.lastIndexOf(QLatin1String(" ("));
        return paren > 0 ? label.left(paren) : label;
    }

    // Drag `src` and drop it at (row, parent) exactly as QTreeView would.
    static bool dragDrop(LayerTreeModel &m, const QModelIndex &src,
                         int row, const QModelIndex &parent)
    {
        QMimeData *mime = m.mimeData({src});
        if (!mime) return false;
        const bool can = m.canDropMimeData(mime, Qt::MoveAction, row, 0, parent);
        const bool ok  = can && m.dropMimeData(mime, Qt::MoveAction, row, 0, parent);
        delete mime;
        return ok;
    }

private slots:

    // Default group order puts 2D outputs above feature layers above tables,
    // regardless of the order the layers were added in.
    void addLayerKeepsStackGroupedByCategory()
    {
        OpenSWMMVisAnnotationLayer feat1(QStringLiteral("feat1"));
        TabularDataLayer           tab1(QStringLiteral("tab1"));
        SWMM2DResultsLayer         res1(QStringLiteral("res1"));
        OpenSWMMVisAnnotationLayer feat2(QStringLiteral("feat2"));
        SWMM2DResultsLayer         res2(QStringLiteral("res2"));
        MapCanvas canvas;

        canvas.addLayer(&feat1, false);   // first layer → index 0
        canvas.addLayer(&tab1,  false);   // Tables sits BELOW Feature Layers
        canvas.addLayer(&res1,  false);   // 2D outputs go on top
        canvas.addLayer(&feat2, false);   // top of ITS group, under results
        canvas.addLayer(&res2,  false);   // top of stack

        const QList<OpenSWMMVisLayer *> expected{&tab1, &feat1, &feat2, &res1, &res2};
        QCOMPARE(stack(canvas), expected);

        LayerTreePanel panel;
        panel.setCanvas(&canvas);
        LayerTreeModel *m = panel.model();
        QCOMPARE(m->rowCount(), 3);
        QCOMPARE(categoryName(*m, 0), QStringLiteral("SWMM 2D Outputs"));
        QCOMPARE(categoryName(*m, 1), QStringLiteral("Feature Layers"));
        QCOMPARE(categoryName(*m, 2), QStringLiteral("Tables"));
        // Top of tree = top of stack within each group.
        QCOMPARE(m->layerForIndex(m->index(0, 0, m->index(0, 0))), L(res2));
        QCOMPARE(m->layerForIndex(m->index(1, 0, m->index(0, 0))), L(res1));
        QCOMPARE(m->layerForIndex(m->index(0, 0, m->index(1, 0))), L(feat2));
    }

    // The bug: 5 of the 8 categories are empty here. Moving "Tables" up
    // must move the TABLES group, both in the tree and in the stack.
    void categoryMoveWithEmptyCategoriesPresent()
    {
        SWMM2DResultsLayer         res(QStringLiteral("res"));
        OpenSWMMVisAnnotationLayer feat(QStringLiteral("feat"));
        TabularDataLayer           tab(QStringLiteral("tab"));
        MapCanvas canvas;
        canvas.addLayer(&res,  false);
        canvas.addLayer(&feat, false);
        canvas.addLayer(&tab,  false);
        QCOMPARE(stack(canvas), (QList<OpenSWMMVisLayer *>{&tab, &feat, &res}));

        LayerTreePanel panel;
        panel.setCanvas(&canvas);
        LayerTreeModel *m = panel.model();
        QCOMPARE(categoryName(*m, 2), QStringLiteral("Tables"));

        QVERIFY(m->reorderCategory(CatTables, 1));          // "Move Category Up"
        QCOMPARE(categoryName(*m, 0), QStringLiteral("SWMM 2D Outputs"));
        QCOMPARE(categoryName(*m, 1), QStringLiteral("Tables"));
        QCOMPARE(categoryName(*m, 2), QStringLiteral("Feature Layers"));
        QCOMPARE(stack(canvas), (QList<OpenSWMMVisLayer *>{&feat, &tab, &res}));

        // Per-project preference recorded on the canvas, Tables now just
        // before Feature Layers, with the empty groups keeping their slots.
        const QVector<int> g = canvas.layerGroupOrder();
        QCOMPARE(g.size(), int(CatCount));
        QCOMPARE(g.indexOf(CatTables) + 1, g.indexOf(CatFeatureLayers));

        // Undo restores the stack and — because the tree is derived from
        // the stack — the tree.
        canvas.undoStack()->undo();
        QCOMPARE(stack(canvas), (QList<OpenSWMMVisLayer *>{&tab, &feat, &res}));
        QCOMPARE(categoryName(*m, 2), QStringLiteral("Tables"));
        canvas.undoStack()->redo();
        QCOMPARE(categoryName(*m, 1), QStringLiteral("Tables"));

        // Bounds: no-ops return false and change nothing.
        QVERIFY(!m->reorderCategory(CatTables, 1));
        QVERIFY(!m->reorderCategory(CatTables, 3));
        QVERIFY(!m->reorderCategory(CatSwmm2DOutputs, -1));
    }

    void categoryDragDropBetweenAndOntoRows()
    {
        SWMM2DResultsLayer         res(QStringLiteral("res"));
        OpenSWMMVisAnnotationLayer feat(QStringLiteral("feat"));
        TabularDataLayer           tab(QStringLiteral("tab"));
        MapCanvas canvas;
        canvas.addLayer(&res,  false);
        canvas.addLayer(&feat, false);
        canvas.addLayer(&tab,  false);
        LayerTreePanel panel;
        panel.setCanvas(&canvas);
        LayerTreeModel *m = panel.model();

        // Drag "Tables" (row 2) and drop it BETWEEN row 0 and row 1
        // (parent invalid, row = 1).
        QVERIFY(dragDrop(*m, m->index(2, 0), 1, QModelIndex()));
        QCOMPARE(categoryName(*m, 1), QStringLiteral("Tables"));
        QCOMPARE(stack(canvas), (QList<OpenSWMMVisLayer *>{&feat, &tab, &res}));

        // Drop "SWMM 2D Outputs" (row 0) ONTO the last category row
        // ("Feature Layers"). "Onto" means "just above that row": the group
        // lands directly above Feature Layers → Tables, 2D Outputs, Features.
        QVERIFY(dragDrop(*m, m->index(0, 0), -1, m->index(2, 0)));
        QCOMPARE(categoryName(*m, 0), QStringLiteral("Tables"));
        QCOMPARE(categoryName(*m, 1), QStringLiteral("SWMM 2D Outputs"));
        QCOMPARE(categoryName(*m, 2), QStringLiteral("Feature Layers"));
        QCOMPARE(stack(canvas), (QList<OpenSWMMVisLayer *>{&feat, &res, &tab}));

        // Drop after the last row (parent invalid, row -1) moves to bottom.
        QVERIFY(dragDrop(*m, m->index(0, 0), -1, QModelIndex()));
        QCOMPARE(categoryName(*m, 2), QStringLiteral("Tables"));

        // A category can never be dropped onto a layer row.
        QMimeData *mime = m->mimeData({m->index(0, 0)});
        QVERIFY(!m->canDropMimeData(mime, Qt::MoveAction, -1, 0,
                                    m->index(0, 0, m->index(1, 0))));
        delete mime;
    }

    void layerDragDropStaysInsideItsCategory()
    {
        OpenSWMMVisAnnotationLayer a(QStringLiteral("a"));
        OpenSWMMVisAnnotationLayer b(QStringLiteral("b"));
        OpenSWMMVisAnnotationLayer c(QStringLiteral("c"));
        TabularDataLayer           tab(QStringLiteral("tab"));
        MapCanvas canvas;
        canvas.addLayer(&a, false);
        canvas.addLayer(&b, false);
        canvas.addLayer(&c, false);
        canvas.addLayer(&tab, false);
        // stack: tab a b c → tree Feature Layers: c b a ; Tables: tab

        LayerTreePanel panel;
        panel.setCanvas(&canvas);
        LayerTreeModel *m = panel.model();
        const QModelIndex feat = m->index(0, 0);
        const QModelIndex tabs = m->index(1, 0);
        QCOMPARE(m->layerForIndex(m->index(0, 0, feat)), L(c));

        // Drag "a" (row 2) and drop between row 0 and 1 → c a b.
        QVERIFY(dragDrop(*m, m->index(2, 0, feat), 1, feat));
        QCOMPARE(stack(canvas), (QList<OpenSWMMVisLayer *>{&tab, &b, &a, &c}));

        // Drop "c" (row 0) onto the category header → bottom of the group.
        QVERIFY(dragDrop(*m, m->index(0, 0, feat), -1, feat));
        QCOMPARE(stack(canvas), (QList<OpenSWMMVisLayer *>{&tab, &c, &b, &a}));

        // Cross-category is refused up front (forbidden cursor), never applied.
        QMimeData *mime = m->mimeData({m->index(0, 0, feat)});
        QVERIFY(!m->canDropMimeData(mime, Qt::MoveAction, 0, 0, tabs));
        QVERIFY(!m->dropMimeData(mime, Qt::MoveAction, 0, 0, tabs));
        delete mime;
        QCOMPARE(stack(canvas), (QList<OpenSWMMVisLayer *>{&tab, &c, &b, &a}));

        // Undo the two drops.
        canvas.undoStack()->undo();
        canvas.undoStack()->undo();
        QCOMPARE(stack(canvas), (QList<OpenSWMMVisLayer *>{&tab, &a, &b, &c}));
    }

    void sublayerDropBetweenRowsAndOntoRows()
    {
        SWMM2DResultsLayer res(QStringLiteral("res"));
        SWMM2DResultsLayer other(QStringLiteral("other"));   // used at the end
        MapCanvas canvas;
        canvas.addLayer(&res, false);
        auto *host = dynamic_cast<OpenSWMM::Render::ISublayerHost *>(&res);
        QVERIFY(host);
        const QList<OpenSWMM::Render::ISublayer *> before = host->sublayers();
        const int n = before.size();
        QVERIFY(n >= 3);

        LayerTreePanel panel;
        panel.setCanvas(&canvas);
        LayerTreeModel *m = panel.model();
        const QModelIndex layerIdx = m->index(0, 0, m->index(0, 0));
        QCOMPARE(m->rowCount(layerIdx), n);
        // Display row 0 is the TOP of the paint stack.
        QCOMPARE(m->sublayerForIndex(m->index(0, 0, layerIdx)), before.last());

        // Drag the top sub-row and drop BETWEEN rows 1 and 2 (parent = the
        // host layer row, row = 2) → it becomes display row 1.
        QVERIFY(dragDrop(*m, m->index(0, 0, layerIdx), 2, layerIdx));
        QList<OpenSWMM::Render::ISublayer *> after = host->sublayers();
        QCOMPARE(after[n - 1], before[n - 2]);
        QCOMPARE(after[n - 2], before[n - 1]);
        QCOMPARE(m->sublayerForIndex(m->index(1, 0, layerIdx)), before.last());

        // Drop the bottom sub-row ONTO the top sub-row → takes the top slot.
        QVERIFY(dragDrop(*m, m->index(n - 1, 0, layerIdx), -1,
                         m->index(0, 0, layerIdx)));
        after = host->sublayers();
        QCOMPARE(after.last(), before.first());

        // Drop after the last row (row -1, parent = host) → bottom.
        QVERIFY(dragDrop(*m, m->index(0, 0, layerIdx), -1, layerIdx));
        after = host->sublayers();
        QCOMPARE(after.first(), before.first());

        // Undoable: three undos restore the original paint order and the
        // sub-rows re-derive from it.
        canvas.undoStack()->undo();
        canvas.undoStack()->undo();
        canvas.undoStack()->undo();
        QCOMPARE(host->sublayers(), before);
        QCOMPARE(m->sublayerForIndex(m->index(0, 0, layerIdx)), before.last());

        // A sublayer can't be dropped under a different host.
        canvas.addLayer(&other, false);
        const QModelIndex otherIdx = m->indexForLayer(&other);
        const QModelIndex resIdx   = m->indexForLayer(&res);
        QVERIFY(otherIdx.isValid() && resIdx.isValid());
        QMimeData *mime = m->mimeData({m->index(0, 0, resIdx)});
        QVERIFY(!m->canDropMimeData(mime, Qt::MoveAction, 0, 0, otherIdx));
        QVERIFY(!m->canDropMimeData(mime, Qt::MoveAction, -1, 0,
                                    m->index(0, 0, otherIdx)));
        delete mime;
    }

    void canvasMoveSublayerEmitsAndUndoes()
    {
        SWMM2DResultsLayer res(QStringLiteral("res"));
        MapCanvas canvas;
        canvas.addLayer(&res, false);
        auto *host = dynamic_cast<OpenSWMM::Render::ISublayerHost *>(&res);
        const auto before = host->sublayers();
        QSignalSpy spy(&canvas, &MapCanvas::sublayerOrderChanged);

        QVERIFY(canvas.moveSublayer(&res, 0, 1));
        QCOMPARE(spy.count(), 1);
        QCOMPARE(host->sublayers()[1], before[0]);
        canvas.undoStack()->undo();
        QCOMPARE(spy.count(), 2);
        QCOMPARE(host->sublayers(), before);

        QVERIFY(!canvas.moveSublayer(&res, 0, 0));
        QVERIFY(!canvas.moveSublayer(&res, 0, before.size()));
        OpenSWMMVisAnnotationLayer notOnCanvas(QStringLiteral("x"));
        QVERIFY(!canvas.moveSublayer(&notOnCanvas, 0, 1));
    }

    // .oswp round-trip contract: the saved keys are applied as layers
    // arrive, in any order, and re-sort the layers already present.
    void pendingLayerOrderRestoresSavedStack()
    {
        OpenSWMMVisAnnotationLayer feat(QStringLiteral("feat"));
        SWMM2DResultsLayer         res(QStringLiteral("res"));
        TabularDataLayer           tab(QStringLiteral("tab"));
        OpenSWMMVisAnnotationLayer feat2(QStringLiteral("feat2"));   // late arrival
        MapCanvas canvas;

        // The user had saved the (non-default) order tab < res < feat.
        const QStringList saved{MapCanvas::layerOrderKey(&tab),
                                MapCanvas::layerOrderKey(&res),
                                MapCanvas::layerOrderKey(&feat)};

        // Layers already present before the order is known get re-sorted.
        canvas.addLayer(&res,  false);
        canvas.addLayer(&feat, false);           // grouped default: feat below res
        QCOMPARE(stack(canvas), (QList<OpenSWMMVisLayer *>{&feat, &res}));
        canvas.setPendingLayerOrder(saved);
        QCOMPARE(stack(canvas), (QList<OpenSWMMVisLayer *>{&res, &feat}));

        // A late (async) arrival lands in its saved slot, not its group slot.
        canvas.addLayer(&tab, false);
        QCOMPARE(stack(canvas), (QList<OpenSWMMVisLayer *>{&tab, &res, &feat}));

        // An unknown layer still uses the grouped insert (top of its group).
        canvas.addLayer(&feat2, false);
        QCOMPARE(stack(canvas).last(), L(feat2));
    }

    void groupOrderValidation()
    {
        MapCanvas canvas;
        const QVector<int> def = canvas.layerGroupOrder();
        QCOMPARE(def.size(), int(CatCount));
        canvas.setLayerGroupOrder({0, 1, 2});                 // wrong size
        QCOMPARE(canvas.layerGroupOrder(), def);
        QVector<int> dup = def; dup[0] = dup[1];              // not a permutation
        canvas.setLayerGroupOrder(dup);
        QCOMPARE(canvas.layerGroupOrder(), def);
        QVector<int> rev = def; std::reverse(rev.begin(), rev.end());
        canvas.setLayerGroupOrder(rev);
        QCOMPARE(canvas.layerGroupOrder(), rev);
    }
};

QTEST_MAIN(TestLayerTreeReorder)
#include "test_layertree_reorder.moc"
