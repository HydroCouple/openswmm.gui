/*!
 * \file   test_arcgisrestrendering.cpp
 * \brief  Unit tests for ArcGIS REST tile layer rendering.
 *
 * Covers:
 *   - URL building for ZXY (standard OSM) and ZYX (ArcGIS REST) axis orders.
 *   - Correct row/col inversion for ArcGIS REST URLs derived by onArcGISConnect.
 *   - ESRI World Imagery builtin produces tile/{z}/{row}/{col} format.
 *   - latLonToTileXY returns correct tile coordinates for known WGS84 points.
 *   - tileBoundsWGS84 returns correct geographic bounds for known tiles.
 *   - bestZoom selects a reasonable zoom level for typical viewport/extent combos.
 *
 * The private helpers are accessed via XYZTileLayerAccessor (declared friend in
 * xyztilelayer.h) to avoid exposing them in the production API.
 */

#include "layers/xyztilelayer.h"
#include "connections/basemapconnection.h"

#include <QTest>
#include <QCoreApplication>

#include <cmath>

// ---------------------------------------------------------------------------
// Friend accessor — provides static wrappers around XYZTileLayer privates.
// ---------------------------------------------------------------------------

class XYZTileLayerAccessor
{
public:
    static QString buildUrl(const XYZTileLayer &layer, int z, int x, int y)
    {
        return layer.buildUrl(z, x, y);
    }

    static int bestZoom(const XYZTileLayer &layer,
                        const QRectF &wgs84Extent, int vpWidth)
    {
        return layer.bestZoom(wgs84Extent, vpWidth);
    }

    static void latLonToTileXY(const XYZTileLayer &layer,
                                double lat, double lon, int z,
                                int &tx, int &ty)
    {
        layer.latLonToTileXY(lat, lon, z, tx, ty);
    }

    static QRectF tileBoundsWGS84(const XYZTileLayer &layer, int z, int x, int y)
    {
        return layer.tileBoundsWGS84(z, x, y);
    }
};

// ---------------------------------------------------------------------------
// Test class
// ---------------------------------------------------------------------------

class TestArcGISRestRendering : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();

    // URL building
    void zxyUrlOrder_standardOSM();
    void zyxUrlOrder_arcGISRestDerivedTemplate();
    void zyxUrlOrder_esriWorldImageryBuiltinTemplate();
    void subdomainSubstitution();
    void noSubdomainInTemplate();

    // Tile coordinate math
    void latLonToTileXY_primeMeridian_zoom0();
    void latLonToTileXY_london_zoom2();
    void latLonToTileXY_sanFrancisco_zoom10();
    void latLonToTileXY_antimeridianEdge();

    // Tile bounds
    void tileBoundsWGS84_zoom0_wholeWorld();
    void tileBoundsWGS84_zoom1_nwQuadrant();
    void tileBoundsWGS84_zoom1_seQuadrant();

    // Best zoom selection
    void bestZoom_fullWorld_1024px();
    void bestZoom_cityView_1024px();
    void bestZoom_invalidExtent_returnsFloor();

    // Builtin connection axis orders
    void esriWorldImageryBuiltinUsesZXY();
    void builtinOpenStreetMapUsesZXY();

private:
    // Creates a layer with the given template and axis order for testing.
    // Caller owns the returned pointer.
    static XYZTileLayer *makeLayer(const QString &urlTemplate,
                                   TileAxisOrder axisOrder = TileAxisOrder::ZXY)
    {
        auto *layer = new XYZTileLayer(urlTemplate, 256);
        layer->setAxisOrder(axisOrder);
        return layer;
    }
};

// ---------------------------------------------------------------------------
// initTestCase
// ---------------------------------------------------------------------------

void TestArcGISRestRendering::initTestCase()
{
    // QCoreApplication is needed for QNetworkAccessManager (created in the
    // XYZTileLayer constructor). QTest provides it via QTEST_MAIN.
}

// ---------------------------------------------------------------------------
// URL building — ZXY (standard OSM)
// ---------------------------------------------------------------------------

/*!
 * For ZXY axis order the template placeholders map directly:
 *   {z} = zoom,  {x} = tile column,  {y} = tile row.
 */
void TestArcGISRestRendering::zxyUrlOrder_standardOSM()
{
    auto *layer = makeLayer(
        QStringLiteral("https://tiles.example.com/{z}/{x}/{y}.png"),
        TileAxisOrder::ZXY);

    // z=5, col=3, row=7  →  tile/5/3/7
    const QString url = XYZTileLayerAccessor::buildUrl(*layer, 5, 3, 7);
    QCOMPARE(url, QStringLiteral("https://tiles.example.com/5/3/7.png"));

    delete layer;
}

/*!
 * For ZYX axis order the {x} placeholder receives the row value and the
 * {y} placeholder receives the column value, so the output path is
 * tile/{z}/{row}/{col} — the ArcGIS REST tile endpoint convention.
 *
 * The onArcGISConnect handler derives the template as:
 *   serviceBase + "/tile/{z}/{x}/{y}"  with  axisOrder = ZYX
 *
 * This test verifies that combination produces the correct ArcGIS URL.
 */
void TestArcGISRestRendering::zyxUrlOrder_arcGISRestDerivedTemplate()
{
    const QString base = QStringLiteral(
        "https://server.arcgisonline.com/ArcGIS/rest/services/World_Imagery/MapServer");
    auto *layer = makeLayer(base + QStringLiteral("/tile/{z}/{x}/{y}"),
                            TileAxisOrder::ZYX);

    // col=512, row=341 at zoom 10
    // ZYX swap: {x} ← row=341, {y} ← col=512
    // Expected:  …/tile/10/341/512  (tile/z/row/col — ArcGIS convention)
    const QString url = XYZTileLayerAccessor::buildUrl(*layer, 10, 512, 341);
    QCOMPARE(url, base + QStringLiteral("/tile/10/341/512"));

    delete layer;
}

/*!
 * The ESRI World Imagery builtin already has {y} before {x} in its template
 * (tile/{z}/{y}/{x}) and uses ZXY axis order after the bug fix.
 * With ZXY (no swap) that produces tile/{z}/{row}/{col} — correct for ArcGIS.
 *
 * Before the fix the builtin used ZYX, which produced tile/{z}/{col}/{row}.
 * This test would FAIL on the old code, confirming the fix is load-bearing.
 */
void TestArcGISRestRendering::zyxUrlOrder_esriWorldImageryBuiltinTemplate()
{
    // Replicate the ESRI World Imagery builtin after the fix
    const XYZConnection esri = []() {
        for (const XYZConnection &b : XYZConnection::builtins())
            if (b.name == QStringLiteral("ESRI World Imagery"))
                return b;
        return XYZConnection{};
    }();

    QVERIFY2(!esri.urlTemplate.isEmpty(), "ESRI World Imagery builtin not found");

    auto *layer = makeLayer(esri.urlTemplate, esri.axisOrder);

    // col=512 (x), row=341 (y) at zoom 10
    // Template: tile/{z}/{y}/{x}  with ZXY (no swap)
    //   {y} ← row=341, {x} ← col=512  →  tile/10/341/512  ✓
    const QString url = XYZTileLayerAccessor::buildUrl(*layer, 10, 512, 341);

    // Extract just the tile-path suffix from the full URL for a clean assertion.
    QVERIFY2(url.endsWith(QStringLiteral("/tile/10/341/512")),
             qPrintable(QStringLiteral("Unexpected URL: %1").arg(url)));

    delete layer;
}

// ---------------------------------------------------------------------------
// URL building — subdomains
// ---------------------------------------------------------------------------

void TestArcGISRestRendering::subdomainSubstitution()
{
    auto *layer = makeLayer(
        QStringLiteral("https://{s}.tiles.example.com/{z}/{x}/{y}.png"),
        TileAxisOrder::ZXY);

    const QString url = XYZTileLayerAccessor::buildUrl(*layer, 3, 2, 5);
    // {s} is one of a/b/c — just verify it is replaced with something
    QVERIFY(!url.contains(QStringLiteral("{s}")));
    QVERIFY(url.contains(QStringLiteral("tiles.example.com")));
    QVERIFY(url.endsWith(QStringLiteral("/3/2/5.png")));

    delete layer;
}

void TestArcGISRestRendering::noSubdomainInTemplate()
{
    // ArcGIS REST endpoints have no {s} placeholder — must not crash.
    auto *layer = makeLayer(
        QStringLiteral("https://server.arcgisonline.com/ArcGIS/rest/services/World_Imagery/MapServer/tile/{z}/{x}/{y}"),
        TileAxisOrder::ZYX);

    const QString url = XYZTileLayerAccessor::buildUrl(*layer, 5, 10, 8);
    QVERIFY(!url.contains(QStringLiteral("{s}")));
    QVERIFY(!url.contains(QStringLiteral("{z}")));
    QVERIFY(!url.contains(QStringLiteral("{x}")));
    QVERIFY(!url.contains(QStringLiteral("{y}")));

    delete layer;
}

// ---------------------------------------------------------------------------
// latLonToTileXY
// ---------------------------------------------------------------------------

/*!
 * At zoom 0 the entire world is a single tile (0, 0).
 * Any valid coordinate must map to (tx=0, ty=0).
 */
void TestArcGISRestRendering::latLonToTileXY_primeMeridian_zoom0()
{
    auto *layer = makeLayer(QStringLiteral("https://example.com/{z}/{x}/{y}.png"));

    int tx = -1, ty = -1;
    XYZTileLayerAccessor::latLonToTileXY(*layer, 51.5, 0.0, 0, tx, ty);
    QCOMPARE(tx, 0);
    QCOMPARE(ty, 0);

    delete layer;
}

/*!
 * London (51.5°N, 0.0°E) at zoom 2.
 * col = floor((0 + 180) / 360 * 4) = 2
 * row ≈ floor((1 - log(tan(51.5°) + 1/cos(51.5°)) / π) / 2 * 4) = 1
 */
void TestArcGISRestRendering::latLonToTileXY_london_zoom2()
{
    auto *layer = makeLayer(QStringLiteral("https://example.com/{z}/{x}/{y}.png"));

    int tx = -1, ty = -1;
    XYZTileLayerAccessor::latLonToTileXY(*layer, 51.5, 0.0, 2, tx, ty);
    QCOMPARE(tx, 2);   // column 2 (central longitude)
    QCOMPARE(ty, 1);   // row 1 (northern hemisphere, upper half)

    delete layer;
}

/*!
 * San Francisco (37.77°N, 122.42°W) at zoom 10.
 * Reference values computed from the standard Web Mercator tile formula.
 */
void TestArcGISRestRendering::latLonToTileXY_sanFrancisco_zoom10()
{
    auto *layer = makeLayer(QStringLiteral("https://example.com/{z}/{x}/{y}.png"));

    int tx = -1, ty = -1;
    XYZTileLayerAccessor::latLonToTileXY(*layer, 37.7749, -122.4194, 10, tx, ty);

    // col = floor((-122.4194 + 180) / 360 * 1024) = floor(163.35) = 163
    QCOMPARE(tx, 163);
    // row computed via Mercator inverse: reference = 395
    QCOMPARE(ty, 395);

    delete layer;
}

/*!
 * Coordinates right on the anti-meridian should clamp to valid tile range.
 */
void TestArcGISRestRendering::latLonToTileXY_antimeridianEdge()
{
    auto *layer = makeLayer(QStringLiteral("https://example.com/{z}/{x}/{y}.png"));

    int tx = -1, ty = -1;
    XYZTileLayerAccessor::latLonToTileXY(*layer, 0.0, 179.9999, 1, tx, ty);

    const int maxTile = (1 << 1) - 1;  // = 1 at zoom 1
    QVERIFY(tx >= 0 && tx <= maxTile);
    QVERIFY(ty >= 0 && ty <= maxTile);

    delete layer;
}

// ---------------------------------------------------------------------------
// tileBoundsWGS84
// ---------------------------------------------------------------------------

/*!
 * Zoom 0, tile (0,0) covers the entire Web Mercator-valid world:
 *   lon: [-180, 180],  lat: [-85.05°, +85.05°]
 *
 * tileBoundsWGS84 returns QRectF(lonMin, latMin, lonSpan, latSpan).
 * Qt QRectF semantics: top() = y parameter = latMin (south);
 *                      bottom() = y + height = latMax (north).
 */
void TestArcGISRestRendering::tileBoundsWGS84_zoom0_wholeWorld()
{
    auto *layer = makeLayer(QStringLiteral("https://example.com/{z}/{x}/{y}.png"));

    const QRectF bounds = XYZTileLayerAccessor::tileBoundsWGS84(*layer, 0, 0, 0);

    QVERIFY2(qAbs(bounds.left()   - (-180.0)) < 0.01, "lonMin should be -180");
    QVERIFY2(qAbs(bounds.right()  -   180.0)  < 0.01, "lonMax should be +180");
    QVERIFY2(qAbs(bounds.top()    - (-85.05)) < 0.05, "latMin should be ~-85.05");
    QVERIFY2(qAbs(bounds.bottom() -   85.05)  < 0.05, "latMax should be ~+85.05");

    delete layer;
}

/*!
 * Zoom 1, tile (0,0): north-west quadrant.
 *   lon: [-180,  0],  lat: [  0, +85.05°]
 */
void TestArcGISRestRendering::tileBoundsWGS84_zoom1_nwQuadrant()
{
    auto *layer = makeLayer(QStringLiteral("https://example.com/{z}/{x}/{y}.png"));

    const QRectF bounds = XYZTileLayerAccessor::tileBoundsWGS84(*layer, 1, 0, 0);

    QVERIFY2(qAbs(bounds.left()   - (-180.0)) < 0.01, "lonMin should be -180");
    QVERIFY2(qAbs(bounds.right()  -     0.0)  < 0.01, "lonMax should be 0");
    QVERIFY2(qAbs(bounds.top()    -     0.0)  < 0.01, "latMin should be 0");
    QVERIFY2(bounds.bottom() > 85.0,                  "latMax should be ~+85.05");

    delete layer;
}

/*!
 * Zoom 1, tile (1,1): south-east quadrant.
 *   lon: [  0, 180],  lat: [-85.05°,  0]
 */
void TestArcGISRestRendering::tileBoundsWGS84_zoom1_seQuadrant()
{
    auto *layer = makeLayer(QStringLiteral("https://example.com/{z}/{x}/{y}.png"));

    const QRectF bounds = XYZTileLayerAccessor::tileBoundsWGS84(*layer, 1, 1, 1);

    QVERIFY2(qAbs(bounds.left()   -   0.0)  < 0.01, "lonMin should be 0");
    QVERIFY2(qAbs(bounds.right()  - 180.0)  < 0.01, "lonMax should be 180");
    QVERIFY2(bounds.top() < -85.0,                   "latMin should be ~-85.05");
    QVERIFY2(qAbs(bounds.bottom()) < 0.01,           "latMax should be 0");

    delete layer;
}

// ---------------------------------------------------------------------------
// bestZoom
// ---------------------------------------------------------------------------

/*!
 * Full-world view (360° longitude span) on a 1024-px-wide canvas with
 * standard 256-px tiles.
 *   idealZ = log2((1024 * 360) / (360 * 256)) = log2(4) = 2
 * With the 0.7 rounding bias: floor(2 + 0.7) = 2.
 */
void TestArcGISRestRendering::bestZoom_fullWorld_1024px()
{
    auto *layer = makeLayer(QStringLiteral("https://example.com/{z}/{x}/{y}.png"));

    const QRectF world(-180, -85, 360, 170);
    const int z = XYZTileLayerAccessor::bestZoom(*layer, world, 1024);
    QCOMPARE(z, 2);

    delete layer;
}

/*!
 * City-scale view: 0.1° longitude span (roughly the width of a city)
 * on a 1024-px canvas — should select a zoom around 13–15.
 */
void TestArcGISRestRendering::bestZoom_cityView_1024px()
{
    auto *layer = makeLayer(QStringLiteral("https://example.com/{z}/{x}/{y}.png"));

    const QRectF cityExtent(-87.65, 41.83, 0.1, 0.05);  // central Chicago
    const int z = XYZTileLayerAccessor::bestZoom(*layer, cityExtent, 1024);
    QVERIFY2(z >= 12 && z <= 16,
             qPrintable(QStringLiteral("Expected zoom 12–16 for city view, got %1").arg(z)));

    delete layer;
}

/*!
 * Invalid extent (zero/negative size) should return a safe floor of 2.
 */
void TestArcGISRestRendering::bestZoom_invalidExtent_returnsFloor()
{
    auto *layer = makeLayer(QStringLiteral("https://example.com/{z}/{x}/{y}.png"));

    const int z = XYZTileLayerAccessor::bestZoom(*layer, QRectF(), 1024);
    QCOMPARE(z, 2);

    delete layer;
}

// ---------------------------------------------------------------------------
// Builtin connection axis orders
// ---------------------------------------------------------------------------

/*!
 * After the axis-order fix, the ESRI World Imagery builtin must use ZXY
 * (no swap), because its URL template already has {y} before {x}.
 */
void TestArcGISRestRendering::esriWorldImageryBuiltinUsesZXY()
{
    for (const XYZConnection &b : XYZConnection::builtins()) {
        if (b.name == QStringLiteral("ESRI World Imagery")) {
            QCOMPARE(b.axisOrder, TileAxisOrder::ZXY);
            // Template must have {y} before {x} (ArcGIS row/col order in the path)
            const int yPos = b.urlTemplate.indexOf(QStringLiteral("{y}"));
            const int xPos = b.urlTemplate.indexOf(QStringLiteral("{x}"));
            QVERIFY2(yPos < xPos && yPos != -1 && xPos != -1,
                     "ESRI World Imagery template should have {y} before {x}");
            return;
        }
    }
    QFAIL("ESRI World Imagery builtin not found");
}

void TestArcGISRestRendering::builtinOpenStreetMapUsesZXY()
{
    for (const XYZConnection &b : XYZConnection::builtins()) {
        if (b.name == QStringLiteral("OpenStreetMap")) {
            QCOMPARE(b.axisOrder, TileAxisOrder::ZXY);
            return;
        }
    }
    QFAIL("OpenStreetMap builtin not found");
}

// ---------------------------------------------------------------------------

QTEST_MAIN(TestArcGISRestRendering)
#include "test_arcgisrestrendering.moc"
