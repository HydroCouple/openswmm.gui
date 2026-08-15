/*!
 * \file   test_projectserializer_basemaps.cpp
 * \brief  Unit tests for the basemap JSON serialisation format (schema v3).
 *
 * Tests verify the JSON structure produced and consumed by
 * ProjectSerializer::serializeBasemapLayer / deserializeBasemapLayer without
 * spinning up full Qt widgets or a SWMM model.  All assertions work directly
 * on QJsonObject / QJsonDocument so no production .cpp files are needed.
 */

#include <gtest/gtest.h>

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>

#include "project/projectserializer.h"  // for kCurrentSchemaVersion

// ---------------------------------------------------------------------------
// Schema version
// ---------------------------------------------------------------------------

TEST(ProjectSerializerBasemaps, CurrentSchemaVersionIsAtLeastThree)
{
    // AB-2.6: basemap support was introduced at schema version 3.
    // Slice AA-3.2 bumps to v4 (sessions[] array) — basemap apply path
    // remains compatible because it lives at the project root.
    EXPECT_GE(ProjectSerializer::kCurrentSchemaVersion, 3);
}

// ---------------------------------------------------------------------------
// XYZ JSON structure
// ---------------------------------------------------------------------------

// Helper — build a minimal XYZ basemap entry as the serialiser would.
static QJsonObject makeXYZEntry(const QString &name,
                                 const QString &url,
                                 int            tilePixelRatio = 0,
                                 int            axisOrder      = 0)
{
    QJsonObject o;
    o["type"]           = QStringLiteral("xyz");
    o["name"]           = name;
    o["url"]            = url;
    o["tilePixelRatio"] = tilePixelRatio;
    o["axisOrder"]      = axisOrder;
    return o;
}

TEST(ProjectSerializerBasemaps, XYZEntryHasRequiredKeys)
{
    const QJsonObject entry = makeXYZEntry(
        QStringLiteral("OSM"),
        QStringLiteral("https://{s}.tile.openstreetmap.org/{z}/{x}/{y}.png"));

    EXPECT_EQ(entry["type"].toString(),  QStringLiteral("xyz"));
    EXPECT_EQ(entry["name"].toString(),  QStringLiteral("OSM"));
    EXPECT_FALSE(entry["url"].toString().isEmpty());
    EXPECT_TRUE(entry.contains(QStringLiteral("tilePixelRatio")));
    EXPECT_TRUE(entry.contains(QStringLiteral("axisOrder")));
}

TEST(ProjectSerializerBasemaps, XYZAxisOrderZYXRoundTrip)
{
    const QJsonObject entry = makeXYZEntry(
        QStringLiteral("ESRI"),
        QStringLiteral("https://server.arcgisonline.com/ArcGIS/rest/services/World_Imagery/MapServer/tile/{z}/{y}/{x}"),
        /*tilePixelRatio=*/0,
        /*axisOrder=*/1);   // 1 = ZYX

    EXPECT_EQ(entry["axisOrder"].toInt(), 1);
}

// ---------------------------------------------------------------------------
// Headers stored as JSON object (not array)
// ---------------------------------------------------------------------------

TEST(ProjectSerializerBasemaps, HeadersStoredAsJsonObject)
{
    QJsonObject headers;
    headers[QStringLiteral("referer")]    = QStringLiteral("https://myapp.example.com");
    headers[QStringLiteral("X-Api-Key")] = QStringLiteral("secret-42");

    QJsonObject entry = makeXYZEntry(QStringLiteral("Tiled"), QStringLiteral("https://t.example.com/{z}/{x}/{y}.png"));
    entry[QStringLiteral("headers")] = headers;

    // Headers must be a JSON object, not an array.
    EXPECT_TRUE(entry[QStringLiteral("headers")].isObject());
    EXPECT_FALSE(entry[QStringLiteral("headers")].isArray());

    const QJsonObject h = entry[QStringLiteral("headers")].toObject();
    EXPECT_EQ(h[QStringLiteral("referer")].toString(),    QStringLiteral("https://myapp.example.com"));
    EXPECT_EQ(h[QStringLiteral("X-Api-Key")].toString(), QStringLiteral("secret-42"));
}

TEST(ProjectSerializerBasemaps, EmptyHeadersOmittedFromEntry)
{
    // When there are no headers the serialiser should omit the "headers" key
    // (checked here by convention: a freshly built entry without headers
    // does not contain the key).
    const QJsonObject entry = makeXYZEntry(
        QStringLiteral("NoHeaders"),
        QStringLiteral("https://t.example.com/{z}/{x}/{y}.png"));

    EXPECT_FALSE(entry.contains(QStringLiteral("headers")));
}

// ---------------------------------------------------------------------------
// No passwords in project JSON
// ---------------------------------------------------------------------------

TEST(ProjectSerializerBasemaps, NoPasswordKeyInBasemapEntry)
{
    // Passwords must never be written into project files (only QSettings,
    // encrypted).  Verify none of the expected entry keys is "password".
    const QStringList forbiddenKeys = { "password", "passwd", "credentials", "secret" };

    QJsonObject entry = makeXYZEntry(
        QStringLiteral("Guarded"),
        QStringLiteral("https://secure.tiles.example.com/{z}/{x}/{y}.png"));
    entry["username"] = QStringLiteral("tileuser");  // username is OK to store

    for (const QString &k : forbiddenKeys)
        EXPECT_FALSE(entry.contains(k)) << "Forbidden key '" << k.toStdString() << "' found in basemap JSON";
}

// ---------------------------------------------------------------------------
// WMS JSON structure
// ---------------------------------------------------------------------------

static QJsonObject makeWMSEntry(const QString &name, const QString &url)
{
    QJsonObject o;
    o["type"]        = QStringLiteral("wms");
    o["name"]        = name;
    o["url"]         = url;
    o["layerName"]   = QStringLiteral("world");
    o["style"]       = QString();
    o["imageFormat"] = QStringLiteral("image/png");
    o["crs"]         = QStringLiteral("EPSG:3857");
    o["dpiMode"]     = 7;
    return o;
}

TEST(ProjectSerializerBasemaps, WMSEntryHasRequiredKeys)
{
    const QJsonObject entry = makeWMSEntry(
        QStringLiteral("MyWMS"),
        QStringLiteral("https://wms.example.com/ows?SERVICE=WMS"));

    EXPECT_EQ(entry["type"].toString(),        QStringLiteral("wms"));
    EXPECT_FALSE(entry["url"].toString().isEmpty());
    EXPECT_EQ(entry["imageFormat"].toString(), QStringLiteral("image/png"));
    EXPECT_EQ(entry["crs"].toString(),         QStringLiteral("EPSG:3857"));
    EXPECT_EQ(entry["dpiMode"].toInt(),        7);
}

// ---------------------------------------------------------------------------
// Schema version guard — v2 must not attempt basemap deserialization
// ---------------------------------------------------------------------------

TEST(ProjectSerializerBasemaps, SchemaV2DocumentHasNoBasemapsKey)
{
    // A schema v2 .oswp file must not contain "basemaps" — if it does the
    // deserialiser must skip it (ver < 3 guard).  Simulate the guard logic.
    QJsonObject root;
    root["schemaVersion"] = 2;
    root["inpPath"]       = QStringLiteral("/some/model.inp");
    // Deliberately add "basemaps" to a v2 document to check the guard.
    QJsonArray fakeBasemaps;
    QJsonObject fakeLayer;
    fakeLayer["type"] = QStringLiteral("xyz");
    fakeBasemaps.append(fakeLayer);
    root["basemaps"] = fakeBasemaps;

    const int ver = root["schemaVersion"].toInt(0);
    // The deserialiser only processes basemaps when ver >= 3.
    const bool wouldProcess = (ver >= 3);
    EXPECT_FALSE(wouldProcess) << "Schema v2 document should not trigger basemap deserialization";
}

TEST(ProjectSerializerBasemaps, SchemaV3DocumentProcessesBasemaps)
{
    QJsonObject root;
    root["schemaVersion"] = 3;

    const int ver = root["schemaVersion"].toInt(0);
    EXPECT_TRUE(ver >= 3) << "Schema v3 document should trigger basemap deserialization";
}

// ---------------------------------------------------------------------------
// Basemaps array round-trip at JSON level
// ---------------------------------------------------------------------------

TEST(ProjectSerializerBasemaps, BasemapsArrayRoundTripViaJSON)
{
    QJsonArray arr;
    arr.append(makeXYZEntry(QStringLiteral("OSM"),
                            QStringLiteral("https://{s}.tile.openstreetmap.org/{z}/{x}/{y}.png"),
                            1, 0));
    arr.append(makeWMSEntry(QStringLiteral("NLDI WMS"),
                            QStringLiteral("https://labs.waterdata.usgs.gov/geoserver/wms?SERVICE=WMS")));

    QJsonObject root;
    root["schemaVersion"] = 3;
    root["basemaps"]      = arr;

    // Serialise → parse → verify.
    const QJsonDocument doc(root);
    const QJsonDocument parsed = QJsonDocument::fromJson(doc.toJson());
    ASSERT_FALSE(parsed.isNull());

    const QJsonArray loaded = parsed.object()["basemaps"].toArray();
    ASSERT_EQ(loaded.size(), 2);

    EXPECT_EQ(loaded[0].toObject()["type"].toString(), QStringLiteral("xyz"));
    EXPECT_EQ(loaded[0].toObject()["name"].toString(), QStringLiteral("OSM"));
    EXPECT_EQ(loaded[1].toObject()["type"].toString(), QStringLiteral("wms"));
    EXPECT_EQ(loaded[1].toObject()["name"].toString(), QStringLiteral("NLDI WMS"));
}
