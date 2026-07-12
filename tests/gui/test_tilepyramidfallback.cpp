/*!
 * \file   test_tilepyramidfallback.cpp
 * \brief  QtTest: TilePyramidLayer's extent-based coarser-tile fallback —
 *         exact hits, sub-rect math (dyadic and non-dyadic grids), the
 *         nearest-ancestor preference, the span-ratio cap, and clearTiles().
 *
 * Self-contained: exercises only the TilePyramidLayer base (no GDAL warp, no
 * network). A minimal concrete subclass exposes the protected cache ops.
 */

#include "layers/tilepyramidlayer.h"

#include <QImage>
#include <QObject>
#include <QTest>

namespace {

class TestTileLayer : public TilePyramidLayer
{
    Q_OBJECT
public:
    explicit TestTileLayer() : TilePyramidLayer(nullptr) {}

    void populateScene(QGraphicsScene *, const MapExtent &,
                       const SpatialReferenceSystem *) override {}

    // Re-expose the protected surface for the test body.
    using TilePyramidLayer::TileDraw;
    using TilePyramidLayer::cacheTile;
    using TilePyramidLayer::isTileCached;
    using TilePyramidLayer::cachedTileImage;
    using TilePyramidLayer::clearTiles;
    using TilePyramidLayer::resolveTileForDraw;
};

QImage solidTile(int px, QRgb color)
{
    QImage img(px, px, QImage::Format_ARGB32);
    img.fill(color);
    return img;
}

} // namespace

class TestTilePyramidFallback : public QObject
{
    Q_OBJECT
private slots:
    void exactHitReturnsFullRect();
    void missingTileFallsBackToParentSubRect();
    void nearestAncestorWins();
    void spanRatioCapRejectsTooCoarse();
    void noContainingTileReturnsFalse();
    void clearTilesDropsFallback();
};

void TestTilePyramidFallback::exactHitReturnsFullRect()
{
    TestTileLayer layer;
    layer.cacheTile("0/0/0", solidTile(256, qRgb(10, 20, 30)),
                    MapExtent(0, 0, 256, 256), 0);
    QVERIFY(layer.isTileCached("0/0/0"));

    TestTileLayer::TileDraw td;
    QVERIFY(layer.resolveTileForDraw("0/0/0", MapExtent(0, 0, 256, 256),
                                     64.0, td));
    QVERIFY(td.exact);
    QCOMPARE(td.src, QRectF(0, 0, 256, 256));
    QCOMPARE(td.img.pixel(1, 1), qRgb(10, 20, 30));
}

void TestTilePyramidFallback::missingTileFallsBackToParentSubRect()
{
    TestTileLayer layer;
    // Dyadic parent at level 1 spanning (0,0)-(512,512), 256² px.
    layer.cacheTile("1/0/0", solidTile(256, qRgb(1, 2, 3)),
                    MapExtent(0, 0, 512, 512), 1);

    // NE child (256,256)-(512,512): east half, NORTH half → src y = 0
    // (image row 0 is the extent's yMax edge).
    TestTileLayer::TileDraw td;
    QVERIFY(layer.resolveTileForDraw("0/1/1", MapExtent(256, 256, 512, 512),
                                     64.0, td));
    QVERIFY(!td.exact);
    QCOMPARE(td.src, QRectF(128, 0, 128, 128));

    // SW child (0,0)-(256,256): west half, SOUTH half → src y = 128.
    QVERIFY(layer.resolveTileForDraw("0/0/0", MapExtent(0, 0, 256, 256),
                                     64.0, td));
    QVERIFY(!td.exact);
    QCOMPARE(td.src, QRectF(0, 128, 128, 128));

    // Non-dyadic (WMTS-style) child one third into the parent.
    QVERIFY(layer.resolveTileForDraw("x", MapExtent(0, 256, 512.0 / 3.0, 512),
                                     64.0, td));
    QVERIFY(!td.exact);
    QCOMPARE(td.src.x(), 0.0);
    QCOMPARE(td.src.y(), 0.0);
    QVERIFY(qAbs(td.src.width() - 256.0 / 3.0) < 1e-9);
    QCOMPARE(td.src.height(), 128.0);
}

void TestTilePyramidFallback::nearestAncestorWins()
{
    TestTileLayer layer;
    layer.cacheTile("2/0/0", solidTile(256, qRgb(200, 0, 0)),
                    MapExtent(0, 0, 1024, 1024), 2);   // grandparent
    layer.cacheTile("1/0/0", solidTile(256, qRgb(0, 200, 0)),
                    MapExtent(0, 0, 512, 512), 1);     // parent (smaller span)

    TestTileLayer::TileDraw td;
    QVERIFY(layer.resolveTileForDraw("0/0/0", MapExtent(0, 0, 256, 256),
                                     64.0, td));
    QVERIFY(!td.exact);
    QCOMPARE(td.img.pixel(1, 1), qRgb(0, 200, 0));  // parent, not grandparent
}

void TestTilePyramidFallback::spanRatioCapRejectsTooCoarse()
{
    TestTileLayer layer;
    // Ancestor 8× the child span; a cap of 4 must reject it, 64 accepts.
    layer.cacheTile("3/0/0", solidTile(256, qRgb(5, 5, 5)),
                    MapExtent(0, 0, 2048, 2048), 3);

    TestTileLayer::TileDraw td;
    QVERIFY(!layer.resolveTileForDraw("0/0/0", MapExtent(0, 0, 256, 256),
                                      4.0, td));
    QVERIFY(layer.resolveTileForDraw("0/0/0", MapExtent(0, 0, 256, 256),
                                     64.0, td));
}

void TestTilePyramidFallback::noContainingTileReturnsFalse()
{
    TestTileLayer layer;
    // Cached tile does not contain the requested extent (disjoint sibling).
    layer.cacheTile("0/9/9", solidTile(256, qRgb(7, 7, 7)),
                    MapExtent(2304, 2304, 2560, 2560), 0);

    TestTileLayer::TileDraw td;
    QVERIFY(!layer.resolveTileForDraw("0/0/0", MapExtent(0, 0, 256, 256),
                                      64.0, td));
}

void TestTilePyramidFallback::clearTilesDropsFallback()
{
    TestTileLayer layer;
    layer.cacheTile("1/0/0", solidTile(256, qRgb(1, 2, 3)),
                    MapExtent(0, 0, 512, 512), 1);

    TestTileLayer::TileDraw td;
    QVERIFY(layer.resolveTileForDraw("0/0/0", MapExtent(0, 0, 256, 256),
                                     64.0, td));
    layer.clearTiles();
    QVERIFY(!layer.isTileCached("1/0/0"));
    QVERIFY(layer.cachedTileImage("1/0/0").isNull());
    QVERIFY(!layer.resolveTileForDraw("0/0/0", MapExtent(0, 0, 256, 256),
                                      64.0, td));
}

QTEST_MAIN(TestTilePyramidFallback)
#include "test_tilepyramidfallback.moc"
