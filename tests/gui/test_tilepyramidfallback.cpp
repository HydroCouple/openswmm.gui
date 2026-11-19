/**
 * @file      test_tilepyramidfallback.cpp
 * @brief     QtTest: TilePyramidLayer's extent-based coarser-tile fallback —
 *         exact hits, sub-rect math (dyadic and non-dyadic grids), the
 *         nearest-ancestor preference, the span-ratio cap, and clearTiles().
 *
 * Self-contained: exercises only the TilePyramidLayer base (no GDAL warp, no
 * network). A minimal concrete subclass exposes the protected cache ops.
 *
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

#include "layers/tilepyramidlayer.h"

#include <QImage>
#include <QObject>
#include <QTest>

namespace
{

class TestTileLayer : public TilePyramidLayer
{
    Q_OBJECT
public:
    /**
     *
     * @brief Construct a parentless concrete tile-cache test layer without loading data.
     * Layer value lifetime owns its cache; protected cache methods are exposed only for tests.
     *
     */
    explicit TestTileLayer() : TilePyramidLayer(nullptr) {}

    /**
     *
     * @brief Provide an inert scene hook so the cache-only fixture is concrete.
     * @param scene Borrowed possibly null scene, ignored and not retained.
     * @param extent Borrowed ignored extent.
     * @param crs Borrowed possibly null CRS, ignored and not retained.
     * No graphics items or transforms are created; not production population behavior.
     *
     */
    void populateScene(QGraphicsScene *scene, const MapExtent &extent,
                       const SpatialReferenceSystem *crs) override
    {
    }

    // Re-expose the protected surface for the test body.
    using TilePyramidLayer::TileDraw;
    using TilePyramidLayer::cacheTile;
    using TilePyramidLayer::isTileCached;
    using TilePyramidLayer::cachedTileImage;
    using TilePyramidLayer::clearTiles;
    using TilePyramidLayer::resolveTileForDraw;
};

/**
 *
 * @brief Allocate a square ARGB32 image and fill it with one packed color.
 * @param px Pixel side length forwarded to QImage; valid positive fixtures expected.
 * @param color Packed QRgb fill value.
 * @return Owned image, possibly null on invalid size/allocation failure; no file I/O.
 *
 */
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

/**
 *
 * @brief Require exact cache-key resolution to return full256-square source rect, exact flag and
 * expected pixel.
 * No fallback/network/GDAL resampling or actual scene painting.
 *
 */
void TestTilePyramidFallback::exactHitReturnsFullRect()
{
    TestTileLayer layer;
    layer.cacheTile("0/0/0", solidTile(256, qRgb(10, 20, 30)), MapExtent(0, 0, 256, 256), 0);
    QVERIFY(layer.isTileCached("0/0/0"));

    TestTileLayer::TileDraw td;
    QVERIFY(layer.resolveTileForDraw("0/0/0", MapExtent(0, 0, 256, 256), 64.0, td));
    QVERIFY(td.exact);
    QCOMPARE(td.src, QRectF(0, 0, 256, 256));
    QCOMPARE(td.img.pixel(1, 1), qRgb(10, 20, 30));
}

/**
 *
 * @brief Require missing dyadic/non-dyadic children to resolve correct parent source rectangles
 * with north-up image-y mapping.
 * Checks NE/SW128-square rects and a one-third-width child within1e-9; does not rasterize
 * the scaled target or fetch a missing tile.
 *
 */
void TestTilePyramidFallback::missingTileFallsBackToParentSubRect()
{
    TestTileLayer layer;
    // Dyadic parent at level 1 spanning (0,0)-(512,512), 256² px.
    layer.cacheTile("1/0/0", solidTile(256, qRgb(1, 2, 3)), MapExtent(0, 0, 512, 512), 1);

    // NE child (256,256)-(512,512): east half, NORTH half → src y = 0
    // (image row 0 is the extent's yMax edge).
    TestTileLayer::TileDraw td;
    QVERIFY(layer.resolveTileForDraw("0/1/1", MapExtent(256, 256, 512, 512), 64.0, td));
    QVERIFY(!td.exact);
    QCOMPARE(td.src, QRectF(128, 0, 128, 128));

    // SW child (0,0)-(256,256): west half, SOUTH half → src y = 128.
    QVERIFY(layer.resolveTileForDraw("0/0/0", MapExtent(0, 0, 256, 256), 64.0, td));
    QVERIFY(!td.exact);
    QCOMPARE(td.src, QRectF(0, 128, 128, 128));

    // Non-dyadic (WMTS-style) child one third into the parent.
    QVERIFY(layer.resolveTileForDraw("x", MapExtent(0, 256, 512.0 / 3.0, 512), 64.0, td));
    QVERIFY(!td.exact);
    QCOMPARE(td.src.x(), 0.0);
    QCOMPARE(td.src.y(), 0.0);
    QVERIFY(qAbs(td.src.width() - 256.0 / 3.0) < 1e-9);
    QCOMPARE(td.src.height(), 128.0);
}

/**
 *
 * @brief Require the smaller containing parent to win over a grandparent, identified by its green
 * pixel.
 * Checks nonexact result and sample color, without pinning cache iteration order or
 * external zoom-grid metadata.
 *
 */
void TestTilePyramidFallback::nearestAncestorWins()
{
    TestTileLayer layer;
    layer.cacheTile("2/0/0", solidTile(256, qRgb(200, 0, 0)), MapExtent(0, 0, 1024, 1024),
                    2); // grandparent
    layer.cacheTile("1/0/0", solidTile(256, qRgb(0, 200, 0)), MapExtent(0, 0, 512, 512),
                    1); // parent (smaller span)

    TestTileLayer::TileDraw td;
    QVERIFY(layer.resolveTileForDraw("0/0/0", MapExtent(0, 0, 256, 256), 64.0, td));
    QVERIFY(!td.exact);
    QCOMPARE(td.img.pixel(1, 1), qRgb(0, 200, 0)); // parent, not grandparent
}

/**
 *
 * @brief Require an eight-times-span ancestor to fail cap4 and succeed cap64.
 * Does not check the resolved source rectangle, boundary equality or nonfinite caps.
 *
 */
void TestTilePyramidFallback::spanRatioCapRejectsTooCoarse()
{
    TestTileLayer layer;
    // Ancestor 8× the child span; a cap of 4 must reject it, 64 accepts.
    layer.cacheTile("3/0/0", solidTile(256, qRgb(5, 5, 5)), MapExtent(0, 0, 2048, 2048), 3);

    TestTileLayer::TileDraw td;
    QVERIFY(!layer.resolveTileForDraw("0/0/0", MapExtent(0, 0, 256, 256), 4.0, td));
    QVERIFY(layer.resolveTileForDraw("0/0/0", MapExtent(0, 0, 256, 256), 64.0, td));
}

/**
 *
 * @brief Require a spatially disjoint cached sibling not to satisfy the requested extent.
 * No network fetch, cache mutation or diagnostic assertion.
 *
 */
void TestTilePyramidFallback::noContainingTileReturnsFalse()
{
    TestTileLayer layer;
    // Cached tile does not contain the requested extent (disjoint sibling).
    layer.cacheTile("0/9/9", solidTile(256, qRgb(7, 7, 7)), MapExtent(2304, 2304, 2560, 2560), 0);

    TestTileLayer::TileDraw td;
    QVERIFY(!layer.resolveTileForDraw("0/0/0", MapExtent(0, 0, 256, 256), 64.0, td));
}

/**
 *
 * @brief Require clearing the cache to remove exact image presence and the previously usable
 * parent fallback.
 * Checks missing cached image is null and later resolution false; no asynchronous tile
 * request is in flight.
 *
 */
void TestTilePyramidFallback::clearTilesDropsFallback()
{
    TestTileLayer layer;
    layer.cacheTile("1/0/0", solidTile(256, qRgb(1, 2, 3)), MapExtent(0, 0, 512, 512), 1);

    TestTileLayer::TileDraw td;
    QVERIFY(layer.resolveTileForDraw("0/0/0", MapExtent(0, 0, 256, 256), 64.0, td));
    layer.clearTiles();
    QVERIFY(!layer.isTileCached("1/0/0"));
    QVERIFY(layer.cachedTileImage("1/0/0").isNull());
    QVERIFY(!layer.resolveTileForDraw("0/0/0", MapExtent(0, 0, 256, 256), 64.0, td));
}

QTEST_MAIN(TestTilePyramidFallback)
#include "test_tilepyramidfallback.moc"
