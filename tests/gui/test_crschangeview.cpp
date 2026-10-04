/*!
 * \file   test_crschangeview.cpp
 * \brief  A canvas CRS change re-frames the view in the NEW CRS.
 *
 * Before: "Re-render only", same-session undo/redo of a CRS change, and any
 * other MapCanvas::setCanvasSRS call kept the old extent — numbers that mean
 * nothing in the new CRS — so the network seemed to vanish. Now every CRS
 * change refits to the data (or, with no data layer, keeps looking at the same
 * place), and the refit is part of the CRS change's single undo step.
 */

#include "layers/swmmmodellayer.h"
#include "map/mapcanvas.h"
#include "map/mapundostack.h"
#include "map/spatialreferencesystem.h"

#include <QDir>
#include <QTest>

namespace {

QString fixturePath()
{
    return QDir(qEnvironmentVariable("SWMMVIS_GUI_TEST_DATA", QStringLiteral(".")))
        .filePath(QStringLiteral("gage_assign_rdii.inp"));
}

/*! True when \p view contains \p data entirely. */
bool frames(const MapExtent &view, const MapExtent &data)
{
    return view.isValid() && data.isValid()
           && view.xMin() <= data.xMin() && view.xMax() >= data.xMax()
           && view.yMin() <= data.yMin() && view.yMax() >= data.yMax();
}

QPointF centre(const MapExtent &e)
{
    return {0.5 * (e.xMin() + e.xMax()), 0.5 * (e.yMin() + e.yMax())};
}

} // namespace

class TestCrsChangeView : public QObject
{
    Q_OBJECT
private slots:
    void renderOnlyChangeFramesDataInNewCrs();
    void basemapOnlyKeepsLookingAtTheSamePlace();
};

void TestCrsChangeView::renderOnlyChangeFramesDataInNewCrs()
{
    MapCanvas canvas;
    canvas.resize(800, 600);
    QVERIFY(canvas.setCanvasSRSByCode(QStringLiteral("EPSG"), 26985));

    auto *layer = new SWMMModelLayer(fixturePath(), nullptr);
    QList<QString> w, e;
    QVERIFY(layer->loadModel(w, e));
    layer->setSRS(SpatialReferenceSystem::fromAuthCode(QStringLiteral("EPSG"), 26985), true);
    canvas.addLayer(layer, false);
    canvas.zoomToFullExtent(false);
    QVERIFY(frames(canvas.extent(), canvas.layerExtentInCanvasCRS(layer)));

    MapUndoStack *stack = canvas.undoStack();
    const int before = stack->count();

    // "Re-render only": just the canvas CRS changes.
    QVERIFY(canvas.setCanvasSRSByCode(QStringLiteral("EPSG"), 3857));
    QCOMPARE(stack->count(), before + 1);   // one step — the refit is part of it
    const MapExtent data3857 = canvas.layerExtentInCanvasCRS(layer);
    QVERIFY2(frames(canvas.extent(), data3857),
             "view must frame the data at its EPSG:3857 coordinates");

    stack->undo();
    QCOMPARE(canvas.canvasSRS()->toAuthority(), QStringLiteral("EPSG:26985"));
    QVERIFY(frames(canvas.extent(), canvas.layerExtentInCanvasCRS(layer)));

    stack->redo();
    QCOMPARE(canvas.canvasSRS()->toAuthority(), QStringLiteral("EPSG:3857"));
    QVERIFY(frames(canvas.extent(), canvas.layerExtentInCanvasCRS(layer)));
}

void TestCrsChangeView::basemapOnlyKeepsLookingAtTheSamePlace()
{
    MapCanvas canvas;
    canvas.resize(800, 600);
    QVERIFY(canvas.setCanvasSRSByCode(QStringLiteral("EPSG"), 4326));
    // No data layers: a CRS change must not zoom out to the world.
    const MapExtent lonlat(-77.1, 38.8, -77.0, 38.9);   // Washington, DC
    canvas.setExtent(lonlat, false);
    const QPointF c0 = centre(canvas.extent());

    QVERIFY(canvas.setCanvasSRSByCode(QStringLiteral("EPSG"), 3857));
    const MapExtent v = canvas.extent();
    QVERIFY(v.isValid());
    // DC in Web Mercator is about (-8.58e6, 4.69e6); the view stays near it
    // and at city scale, not world scale.
    const QPointF c = centre(v);
    QVERIFY2(std::abs(c.x() - (-8.58e6)) < 3.0e4 && std::abs(c.y() - 4.69e6) < 3.0e4,
             qPrintable(QStringLiteral("centre %1,%2 (was %3,%4)")
                            .arg(c.x()).arg(c.y()).arg(c0.x()).arg(c0.y())));
    QVERIFY(v.width() < 1.0e5);
}

QTEST_MAIN(TestCrsChangeView)
#include "test_crschangeview.moc"
