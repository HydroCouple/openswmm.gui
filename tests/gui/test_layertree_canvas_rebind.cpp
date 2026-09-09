/*!
 * \file   test_layertree_canvas_rebind.cpp
 * \brief  The Layers dock and the Terrain toolbar must survive the canvas
 *         they are bound to being destroyed underneath them.
 *
 * Two crash reports (2026-09-04, 2026-09-05) segfault in QObject::disconnect
 * inside LayerTreeModel::setCanvas, called from
 * SWMMVis::onActiveSubWindowChanged when an ordinary widget show/hide flipped
 * MDI activation: the model held the previous project's MapCanvas as a raw
 * pointer, that window had been closed (its canvas freed), and the rebind
 * disconnected from freed memory. Both holders are QPointers now and a
 * null → null rebind still resets the rows. This test is the reproduction:
 * on the unfixed tree the first case dies in QObject::disconnect.
 */
#include <QtTest>

#include "layers/swmm2dresultslayer.h"
#include "map/mapcanvas.h"
#include "ui/panels/layertreepanel.h"
#include "ui/toolbars/terraintoolbar.h"

class TestLayerTreeCanvasRebind : public QObject
{
    Q_OBJECT

private slots:

    void layersDockSurvivesDeletionOfTheBoundCanvas()
    {
        LayerTreePanel panel;
        auto *a = new MapCanvas;
        auto *b = new MapCanvas;

        // Any layer at all, so the model has rows that name the canvas's
        // layers (the canvas does not own it; it outlives both canvases).
        SWMM2DResultsLayer layer(QStringLiteral("2D Results"));
        b->addLayer(&layer, false);

        panel.setCanvas(a);
        panel.setCanvas(b);
        QCOMPARE(panel.model()->canvas(), b);
        QCOMPARE(panel.model()->rowCount(), 1);

        // The project window died under the dock (Welcome tab active, or a
        // null activation the main window ignores): the QPointer must go
        // null and the rows must be gone before their layers dangle.
        delete b;
        QVERIFY(panel.model()->canvas() == nullptr);
        QCOMPARE(panel.model()->rowCount(), 0);

        // The next activation flip used to disconnect() from freed memory.
        panel.setCanvas(a);
        QCOMPARE(panel.model()->canvas(), a);

        // Last project closes while bound, then the "all closed" branch
        // rebinds to null after the canvas is already gone.
        delete a;
        QVERIFY(panel.model()->canvas() == nullptr);
        panel.setCanvas(nullptr);
        QCOMPARE(panel.model()->rowCount(), 0);
    }

    void terrainToolbarSurvivesDeletionOfTheBoundCanvas()
    {
        TerrainToolbar bar(QStringLiteral("Terrain"));
        auto *a = new MapCanvas;
        auto *b = new MapCanvas;

        bar.rebindCanvas(a);
        bar.rebindCanvas(b);
        delete b;                 // dies while bound
        bar.rebindCanvas(a);      // used to disconnect() from freed memory
        delete a;
        bar.rebindCanvas(nullptr);
        QVERIFY(true);            // reaching here is the assertion
    }
};

QTEST_MAIN(TestLayerTreeCanvasRebind)
#include "test_layertree_canvas_rebind.moc"
