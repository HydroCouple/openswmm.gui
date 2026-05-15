/*!
 * \file   test_basemapconnectionstore.cpp
 * \brief  Unit tests for BasemapConnectionStore — QSettings round-trips for
 *         XYZ, WMS, and ArcGIS REST connections, including HTTP headers and
 *         encrypted credentials.
 *
 * The store uses QSettings internally with the default QCoreApplication
 * organisation/application names.  The fixture redirects both to
 * test-specific strings and removes the test group in TearDown so real
 * user settings are never touched.
 */

#include <gtest/gtest.h>

#include <QCoreApplication>
#include <QSettings>
#include <QString>

#include "connections/basemapconnection.h"
#include "connections/basemapconnectionstore.h"

// ---------------------------------------------------------------------------
// Fixture
// ---------------------------------------------------------------------------

class BasemapStoreTest : public ::testing::Test
{
protected:
    static void SetUpTestSuite()
    {
        // Create a QCoreApplication once for the whole suite so QSettings works.
        if (!QCoreApplication::instance()) {
            static int    argc = 1;
            static char   name[] = "test_basemapconnectionstore";
            static char  *argv[] = {name};
            new QCoreApplication(argc, argv);   // intentionally kept alive
        }
        QCoreApplication::setOrganizationName(QStringLiteral("OpenSWMMUnitTest"));
        QCoreApplication::setApplicationName(QStringLiteral("BasemapStoreTest"));
    }

    void TearDown() override
    {
        // Remove the entire BasemapConnections group written by this test.
        QSettings s;
        s.remove(QStringLiteral("BasemapConnections"));
        s.sync();
    }
};

// ---------------------------------------------------------------------------
// XYZ — basic round-trip
// ---------------------------------------------------------------------------

TEST_F(BasemapStoreTest, XYZRoundTrip)
{
    XYZConnection c;
    c.name           = QStringLiteral("TestOSM");
    c.urlTemplate    = QStringLiteral("https://{s}.tile.openstreetmap.org/{z}/{x}/{y}.png");
    c.zMin           = 2;
    c.zMax           = 18;
    c.tilePixelRatio = 1;
    c.axisOrder      = TileAxisOrder::ZXY;

    BasemapConnectionStore::instance()->saveXYZ(c, {});

    const XYZConnection loaded = BasemapConnectionStore::instance()->loadXYZ(c.name);
    EXPECT_EQ(loaded.name,           c.name);
    EXPECT_EQ(loaded.urlTemplate,    c.urlTemplate);
    EXPECT_EQ(loaded.zMin,           c.zMin);
    EXPECT_EQ(loaded.zMax,           c.zMax);
    EXPECT_EQ(loaded.tilePixelRatio, c.tilePixelRatio);
    EXPECT_EQ(loaded.axisOrder,      c.axisOrder);
}

TEST_F(BasemapStoreTest, XYZAxisOrderZYXRoundTrip)
{
    XYZConnection c;
    c.name        = QStringLiteral("ArcGISXYZ");
    c.urlTemplate = QStringLiteral("https://example.com/tile/{z}/{y}/{x}");
    c.axisOrder   = TileAxisOrder::ZYX;

    BasemapConnectionStore::instance()->saveXYZ(c, {});

    const XYZConnection loaded = BasemapConnectionStore::instance()->loadXYZ(c.name);
    EXPECT_EQ(loaded.axisOrder, TileAxisOrder::ZYX);
}

// ---------------------------------------------------------------------------
// XYZ — HTTP headers round-trip
// ---------------------------------------------------------------------------

TEST_F(BasemapStoreTest, XYZHttpHeadersRoundTrip)
{
    XYZConnection c;
    c.name        = QStringLiteral("HeaderTest");
    c.urlTemplate = QStringLiteral("https://tiles.example.com/{z}/{x}/{y}.png");
    c.httpHeaders.insert(QStringLiteral("referer"),    QStringLiteral("https://myapp.example.com"));
    c.httpHeaders.insert(QStringLiteral("X-Api-Key"),  QStringLiteral("secret-key-42"));
    c.httpHeaders.insert(QStringLiteral("User-Agent"), QStringLiteral("OpenSWMMVis/1.0"));

    BasemapConnectionStore::instance()->saveXYZ(c, {});

    const XYZConnection loaded = BasemapConnectionStore::instance()->loadXYZ(c.name);
    EXPECT_EQ(loaded.httpHeaders.size(),                               3);
    EXPECT_EQ(loaded.httpHeaders.value(QStringLiteral("referer")),    QStringLiteral("https://myapp.example.com"));
    EXPECT_EQ(loaded.httpHeaders.value(QStringLiteral("X-Api-Key")), QStringLiteral("secret-key-42"));
    EXPECT_EQ(loaded.httpHeaders.value(QStringLiteral("User-Agent")),QStringLiteral("OpenSWMMVis/1.0"));
}

// ---------------------------------------------------------------------------
// XYZ — auth round-trip
// ---------------------------------------------------------------------------

TEST_F(BasemapStoreTest, XYZAuthRoundTrip)
{
    XYZConnection c;
    c.name        = QStringLiteral("SecuredTiles");
    c.urlTemplate = QStringLiteral("https://secure.tiles.example.com/{z}/{x}/{y}.png");

    BasemapAuth authIn;
    authIn.username = QStringLiteral("tileuser");
    authIn.password = QStringLiteral("tilepassword!");

    BasemapConnectionStore::instance()->saveXYZ(c, authIn);

    const BasemapAuth authOut = BasemapConnectionStore::instance()->loadXYZAuth(c.name);
    EXPECT_EQ(authOut.username, authIn.username);
    EXPECT_EQ(authOut.password, authIn.password);
}

TEST_F(BasemapStoreTest, XYZEmptyAuthRoundTrip)
{
    XYZConnection c;
    c.name        = QStringLiteral("PublicTiles");
    c.urlTemplate = QStringLiteral("https://public.tiles.example.com/{z}/{x}/{y}.png");

    BasemapConnectionStore::instance()->saveXYZ(c, {});

    const BasemapAuth authOut = BasemapConnectionStore::instance()->loadXYZAuth(c.name);
    EXPECT_TRUE(authOut.isEmpty());
}

// ---------------------------------------------------------------------------
// XYZ — list and remove
// ---------------------------------------------------------------------------

TEST_F(BasemapStoreTest, XYZNamesAndRemove)
{
    XYZConnection c1; c1.name = QStringLiteral("Alpha"); c1.urlTemplate = QStringLiteral("https://a.example.com/{z}/{x}/{y}.png");
    XYZConnection c2; c2.name = QStringLiteral("Beta");  c2.urlTemplate = QStringLiteral("https://b.example.com/{z}/{x}/{y}.png");

    BasemapConnectionStore::instance()->saveXYZ(c1, {});
    BasemapConnectionStore::instance()->saveXYZ(c2, {});

    QStringList names = BasemapConnectionStore::instance()->xyzConnectionNames();
    EXPECT_TRUE(names.contains(QStringLiteral("Alpha")));
    EXPECT_TRUE(names.contains(QStringLiteral("Beta")));

    BasemapConnectionStore::instance()->removeXYZ(QStringLiteral("Alpha"));

    names = BasemapConnectionStore::instance()->xyzConnectionNames();
    EXPECT_FALSE(names.contains(QStringLiteral("Alpha")));
    EXPECT_TRUE(names.contains(QStringLiteral("Beta")));
}

// ---------------------------------------------------------------------------
// WMS — basic round-trip
// ---------------------------------------------------------------------------

TEST_F(BasemapStoreTest, WMSRoundTrip)
{
    WMSConnection c;
    c.name                  = QStringLiteral("TestWMS");
    c.url                   = QStringLiteral("https://wms.example.com/ows?SERVICE=WMS");
    c.layerName             = QStringLiteral("world_boundaries");
    c.style                 = QStringLiteral("default");
    c.imageFormat           = QStringLiteral("image/jpeg");
    c.crs                   = QStringLiteral("EPSG:4326");
    c.dpiMode               = 4;
    c.tilePixelRatio        = 2;
    c.ignoreGetMapURI       = true;
    c.ignoreAxisOrientation = false;
    c.invertAxisOrientation = true;
    c.smoothPixmapTransform = false;

    BasemapConnectionStore::instance()->saveWMS(c, {});

    const WMSConnection loaded = BasemapConnectionStore::instance()->loadWMS(c.name);
    EXPECT_EQ(loaded.name,                  c.name);
    EXPECT_EQ(loaded.url,                   c.url);
    EXPECT_EQ(loaded.layerName,             c.layerName);
    EXPECT_EQ(loaded.style,                 c.style);
    EXPECT_EQ(loaded.imageFormat,           c.imageFormat);
    EXPECT_EQ(loaded.crs,                   c.crs);
    EXPECT_EQ(loaded.dpiMode,               c.dpiMode);
    EXPECT_EQ(loaded.tilePixelRatio,        c.tilePixelRatio);
    EXPECT_EQ(loaded.ignoreGetMapURI,       c.ignoreGetMapURI);
    EXPECT_EQ(loaded.ignoreAxisOrientation, c.ignoreAxisOrientation);
    EXPECT_EQ(loaded.invertAxisOrientation, c.invertAxisOrientation);
    EXPECT_EQ(loaded.smoothPixmapTransform, c.smoothPixmapTransform);
}

TEST_F(BasemapStoreTest, WMSAuthRoundTrip)
{
    WMSConnection c;
    c.name = QStringLiteral("SecuredWMS");
    c.url  = QStringLiteral("https://secure.wms.example.com/ows");

    BasemapAuth authIn;
    authIn.username = QStringLiteral("wmsuser");
    authIn.password = QStringLiteral("wmspass#2026");

    BasemapConnectionStore::instance()->saveWMS(c, authIn);

    const BasemapAuth authOut = BasemapConnectionStore::instance()->loadWMSAuth(c.name);
    EXPECT_EQ(authOut.username, authIn.username);
    EXPECT_EQ(authOut.password, authIn.password);
}

// ---------------------------------------------------------------------------
// ArcGIS REST — basic round-trip
// ---------------------------------------------------------------------------

TEST_F(BasemapStoreTest, ArcGISRoundTrip)
{
    ArcGISRestConnection c;
    c.name              = QStringLiteral("TestArcGIS");
    c.url               = QStringLiteral("https://server.arcgisonline.com/ArcGIS/rest/services/World_Imagery");
    c.urlPrefix         = QStringLiteral("https://proxy.example.com/");
    c.contentEndpoint   = QStringLiteral("https://portal.example.com/arcgis/sharing/rest/content");
    c.communityEndpoint = QStringLiteral("https://portal.example.com/arcgis/sharing/rest/community");
    c.httpHeaders.insert(QStringLiteral("referer"), QStringLiteral("https://myapp.example.com"));

    BasemapConnectionStore::instance()->saveArcGIS(c, {});

    const ArcGISRestConnection loaded = BasemapConnectionStore::instance()->loadArcGIS(c.name);
    EXPECT_EQ(loaded.name,              c.name);
    EXPECT_EQ(loaded.url,               c.url);
    EXPECT_EQ(loaded.urlPrefix,         c.urlPrefix);
    EXPECT_EQ(loaded.contentEndpoint,   c.contentEndpoint);
    EXPECT_EQ(loaded.communityEndpoint, c.communityEndpoint);
    EXPECT_EQ(loaded.httpHeaders.value(QStringLiteral("referer")),
              QStringLiteral("https://myapp.example.com"));
}

TEST_F(BasemapStoreTest, ArcGISNamesAndRemove)
{
    ArcGISRestConnection c;
    c.name = QStringLiteral("ToRemove");
    c.url  = QStringLiteral("https://server.arcgisonline.com/ArcGIS/rest/services/World_Topo_Map");

    BasemapConnectionStore::instance()->saveArcGIS(c, {});
    EXPECT_TRUE(BasemapConnectionStore::instance()->arcGISConnectionNames().contains(c.name));

    BasemapConnectionStore::instance()->removeArcGIS(c.name);
    EXPECT_FALSE(BasemapConnectionStore::instance()->arcGISConnectionNames().contains(c.name));
}
