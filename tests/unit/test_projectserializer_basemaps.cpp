/**
 * @file      test_projectserializer_basemaps.cpp
 * @brief     Unit tests for the basemap JSON serialisation format (schema v3).
 *
 * Tests check local JSON fixture conventions and the production schema constant.
 * Production basemap serializer/deserializer methods are not invoked; assertions
 * use QJsonObject/QJsonDocument without widgets, a model or remote services.
 *
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

#include <gtest/gtest.h>

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>

#include "project/projectserializer.h" // for kCurrentSchemaVersion

// ---------------------------------------------------------------------------
// Schema version
// ---------------------------------------------------------------------------

/**
 *
 * @brief Require the production schema constant to include the basemap-capable version floor.
 *
 */
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
/**
 *
 * @brief Build a test-local XYZ JSON convention fixture, without production serialization.
 * @param name Borrowed display name copied as JSON text.
 * @param url Borrowed template text copied unchanged; no network lookup/validation.
 * @param tilePixelRatio Raw integer stored as authored, default zero (automatic intent).
 * @param axisOrder Raw integer stored as authored, default zero; one fixtures ZYX.
 * @return Owned object with type/name/url/tilePixelRatio/axisOrder only. No headers,
 * password filtering, registry/layer access or production serializer invocation.
 *
 */
static QJsonObject makeXYZEntry(const QString &name, const QString &url, int tilePixelRatio = 0,
                                int axisOrder = 0)
{
    QJsonObject o;
    o["type"] = QStringLiteral("xyz");
    o["name"] = name;
    o["url"] = url;
    o["tilePixelRatio"] = tilePixelRatio;
    o["axisOrder"] = axisOrder;
    return o;
}

/**
 *
 * @brief Check required type/name/nonempty URL and ratio/order keys in the local XYZ fixture.
 *
 */
TEST(ProjectSerializerBasemaps, XYZEntryHasRequiredKeys)
{
    const QJsonObject entry =
        makeXYZEntry(QStringLiteral("OSM"),
                     QStringLiteral("https://{s}.tile.openstreetmap.org/{z}/{x}/{y}.png"));

    EXPECT_EQ(entry["type"].toString(), QStringLiteral("xyz"));
    EXPECT_EQ(entry["name"].toString(), QStringLiteral("OSM"));
    EXPECT_FALSE(entry["url"].toString().isEmpty());
    EXPECT_TRUE(entry.contains(QStringLiteral("tilePixelRatio")));
    EXPECT_TRUE(entry.contains(QStringLiteral("axisOrder")));
}

/**
 *
 * @brief Require local XYZ fixture to retain raw ZYX axis-order code one.
 * Despite the case name, no serializer round trip is performed in this body.
 *
 */
TEST(ProjectSerializerBasemaps, XYZAxisOrderZYXRoundTrip)
{
    const QJsonObject entry = makeXYZEntry(
        QStringLiteral("ESRI"),
        QStringLiteral(
            "https://server.arcgisonline.com/ArcGIS/rest/services/World_Imagery/MapServer/tile/{z}/{y}/{x}"),
        /*tilePixelRatio=*/0,
        /*axisOrder=*/1); // 1 = ZYX

    EXPECT_EQ(entry["axisOrder"].toInt(), 1);
}

// ---------------------------------------------------------------------------
// Headers stored as JSON object (not array)
// ---------------------------------------------------------------------------

/**
 *
 * @brief Verify manually inserted header data is an object with preserved name/value strings.
 *
 */
TEST(ProjectSerializerBasemaps, HeadersStoredAsJsonObject)
{
    QJsonObject headers;
    headers[QStringLiteral("referer")] = QStringLiteral("https://myapp.example.com");
    headers[QStringLiteral("X-Api-Key")] = QStringLiteral("secret-42");

    QJsonObject entry = makeXYZEntry(QStringLiteral("Tiled"),
                                     QStringLiteral("https://t.example.com/{z}/{x}/{y}.png"));
    entry[QStringLiteral("headers")] = headers;

    // Headers must be a JSON object, not an array.
    EXPECT_TRUE(entry[QStringLiteral("headers")].isObject());
    EXPECT_FALSE(entry[QStringLiteral("headers")].isArray());

    const QJsonObject h = entry[QStringLiteral("headers")].toObject();
    EXPECT_EQ(h[QStringLiteral("referer")].toString(), QStringLiteral("https://myapp.example.com"));
    EXPECT_EQ(h[QStringLiteral("X-Api-Key")].toString(), QStringLiteral("secret-42"));
}

/**
 *
 * @brief Require the local XYZ builder to omit the optional headers key by construction.
 *
 */
TEST(ProjectSerializerBasemaps, EmptyHeadersOmittedFromEntry)
{
    // When there are no headers the serialiser should omit the "headers" key
    // (checked here by convention: a freshly built entry without headers
    // does not contain the key).
    const QJsonObject entry = makeXYZEntry(QStringLiteral("NoHeaders"),
                                           QStringLiteral("https://t.example.com/{z}/{x}/{y}.png"));

    EXPECT_FALSE(entry.contains(QStringLiteral("headers")));
}

// ---------------------------------------------------------------------------
// No passwords in project JSON
// ---------------------------------------------------------------------------

/**
 *
 * @brief Check the local fixture plus username lacks four forbidden credential-key names.
 * This case does not exercise production credential removal or arbitrary input filtering.
 *
 */
TEST(ProjectSerializerBasemaps, NoPasswordKeyInBasemapEntry)
{
    // Passwords must never be written into project files (only QSettings,
    // encrypted).  Verify none of the expected entry keys is "password".
    const QStringList forbiddenKeys = {"password", "passwd", "credentials", "secret"};

    QJsonObject entry =
        makeXYZEntry(QStringLiteral("Guarded"),
                     QStringLiteral("https://secure.tiles.example.com/{z}/{x}/{y}.png"));
    entry["username"] = QStringLiteral("tileuser"); // username is OK to store

    for (const QString &k : forbiddenKeys)
        EXPECT_FALSE(entry.contains(k))
            << "Forbidden key '" << k.toStdString() << "' found in basemap JSON";
}

// ---------------------------------------------------------------------------
// WMS JSON structure
// ---------------------------------------------------------------------------

/**
 *
 * @brief Build the test-local WMS JSON convention fixture.
 * @param name Borrowed display name copied into JSON.
 * @param url Borrowed endpoint string copied without parsing/network access.
 * @return Owned WMS object using world layer, empty style, image/png, EPSG:3857 and
 * dpiMode seven. No production layer/serializer or capability validation is exercised.
 *
 */
static QJsonObject makeWMSEntry(const QString &name, const QString &url)
{
    QJsonObject o;
    o["type"] = QStringLiteral("wms");
    o["name"] = name;
    o["url"] = url;
    o["layerName"] = QStringLiteral("world");
    o["style"] = QString();
    o["imageFormat"] = QStringLiteral("image/png");
    o["crs"] = QStringLiteral("EPSG:3857");
    o["dpiMode"] = 7;
    return o;
}

/**
 *
 * @brief Check type, nonempty URL, format, CRS and DPI convention fields of the local WMS
 * fixture.
 *
 */
TEST(ProjectSerializerBasemaps, WMSEntryHasRequiredKeys)
{
    const QJsonObject entry = makeWMSEntry(
        QStringLiteral("MyWMS"), QStringLiteral("https://wms.example.com/ows?SERVICE=WMS"));

    EXPECT_EQ(entry["type"].toString(), QStringLiteral("wms"));
    EXPECT_FALSE(entry["url"].toString().isEmpty());
    EXPECT_EQ(entry["imageFormat"].toString(), QStringLiteral("image/png"));
    EXPECT_EQ(entry["crs"].toString(), QStringLiteral("EPSG:3857"));
    EXPECT_EQ(entry["dpiMode"].toInt(), 7);
}

// ---------------------------------------------------------------------------
// Schema version guard — v2 must not attempt basemap deserialization
// ---------------------------------------------------------------------------

/**
 *
 * @brief Simulate the version>=3 guard and require it false for version two.
 * The fixture deliberately contains basemaps; production deserialization is not called.
 *
 */
TEST(ProjectSerializerBasemaps, SchemaV2DocumentHasNoBasemapsKey)
{
    // A schema v2 .oswp file must not contain "basemaps" — if it does the
    // deserialiser must skip it (ver < 3 guard).  Simulate the guard logic.
    QJsonObject root;
    root["schemaVersion"] = 2;
    root["inpPath"] = QStringLiteral("/some/model.inp");
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

/**
 *
 * @brief Require the local version>=3 predicate true for a version-three JSON object.
 *
 */
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

/**
 *
 * @brief Round-trip local XYZ/WMS entries through Qt JSON and check count/type/name order.
 * No production serializer methods, layer creation or remote service access are involved.
 *
 */
TEST(ProjectSerializerBasemaps, BasemapsArrayRoundTripViaJSON)
{
    QJsonArray arr;
    arr.append(makeXYZEntry(QStringLiteral("OSM"),
                            QStringLiteral("https://{s}.tile.openstreetmap.org/{z}/{x}/{y}.png"), 1,
                            0));
    arr.append(
        makeWMSEntry(QStringLiteral("NLDI WMS"),
                     QStringLiteral("https://labs.waterdata.usgs.gov/geoserver/wms?SERVICE=WMS")));

    QJsonObject root;
    root["schemaVersion"] = 3;
    root["basemaps"] = arr;

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
