/*!
 * \file   basemapconnectionstore.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 */
#include "connections/basemapconnectionstore.h"
#include "core/basemapcrypto.h"

#include <QSettings>

// ---------------------------------------------------------------------------
// QSettings group / key constants
// ---------------------------------------------------------------------------
static constexpr char kTopGroup[]       = "BasemapConnections";
static constexpr char kXYZGroup[]       = "XYZ";
static constexpr char kWMSGroup[]       = "WMS";
static constexpr char kWCSGroup[]       = "WCS";
static constexpr char kArcGISGroup[]    = "ArcGIS";
static constexpr char kLocalRasterGroup[] = "localraster";
static constexpr char kHeaderPrefix[]   = "http-header/";
static constexpr char kPassword[]       = "password";

// ---------------------------------------------------------------------------
// Singleton
// ---------------------------------------------------------------------------

BasemapConnectionStore *BasemapConnectionStore::instance()
{
    static BasemapConnectionStore s_instance;
    return &s_instance;
}

BasemapConnectionStore::BasemapConnectionStore(QObject *parent)
    : QObject(parent)
{}

// ---------------------------------------------------------------------------
// Header helpers
// ---------------------------------------------------------------------------

void BasemapConnectionStore::writeHeaders(QSettings &s,
                                           const BasemapHttpHeaders &headers)
{
    // Remove old entries first
    s.beginGroup("http-header");
    s.remove("");
    s.endGroup();

    for (auto it = headers.cbegin(); it != headers.cend(); ++it) {
        s.setValue(QString("http-header/%1").arg(it.key()), it.value());
    }
}

BasemapHttpHeaders BasemapConnectionStore::readHeaders(QSettings &s)
{
    BasemapHttpHeaders result;
    s.beginGroup("http-header");
    const QStringList keys = s.childKeys();
    for (const QString &k : keys)
        result.insert(k, s.value(k).toString());
    s.endGroup();
    return result;
}

// ---------------------------------------------------------------------------
// XYZ
// ---------------------------------------------------------------------------

void BasemapConnectionStore::saveXYZ(const XYZConnection &conn,
                                      const BasemapAuth  &auth)
{
    QSettings s;
    s.beginGroup(kTopGroup);
    s.beginGroup(kXYZGroup);
    s.beginGroup(conn.name);

    s.setValue("urlTemplate",     conn.urlTemplate);
    s.setValue("zMin",            conn.zMin);
    s.setValue("zMax",            conn.zMax);
    s.setValue("tilePixelRatio",  conn.tilePixelRatio);
    s.setValue("axisOrder",       static_cast<int>(conn.axisOrder));
    writeHeaders(s, conn.httpHeaders);

    s.setValue("username", auth.username);
    if (!auth.password.isEmpty())
        s.setValue(kPassword, BasemapCrypto::encrypt(auth.password));
    else
        s.remove(kPassword);

    s.endGroup(); // name
    s.endGroup(); // XYZ
    s.endGroup(); // BasemapConnections

    emit connectionsChanged();
}

XYZConnection BasemapConnectionStore::loadXYZ(const QString &name) const
{
    QSettings s;
    s.beginGroup(kTopGroup);
    s.beginGroup(kXYZGroup);
    s.beginGroup(name);

    XYZConnection c;
    c.name           = name;
    c.urlTemplate    = s.value("urlTemplate").toString();
    c.zMin           = s.value("zMin", 0).toInt();
    c.zMax           = s.value("zMax", 19).toInt();
    c.tilePixelRatio = s.value("tilePixelRatio", 0).toInt();
    c.axisOrder      = static_cast<TileAxisOrder>(s.value("axisOrder", 0).toInt());
    c.httpHeaders    = readHeaders(s);

    s.endGroup();
    s.endGroup();
    s.endGroup();
    return c;
}

BasemapAuth BasemapConnectionStore::loadXYZAuth(const QString &name) const
{
    QSettings s;
    s.beginGroup(kTopGroup);
    s.beginGroup(kXYZGroup);
    s.beginGroup(name);

    BasemapAuth auth;
    auth.username = s.value("username").toString();
    const QByteArray enc = s.value(kPassword).toByteArray();
    if (!enc.isEmpty())
        auth.password = BasemapCrypto::decrypt(enc);

    s.endGroup();
    s.endGroup();
    s.endGroup();
    return auth;
}

QStringList BasemapConnectionStore::xyzConnectionNames() const
{
    QSettings s;
    s.beginGroup(kTopGroup);
    s.beginGroup(kXYZGroup);
    const QStringList names = s.childGroups();
    s.endGroup();
    s.endGroup();
    return names;
}

void BasemapConnectionStore::removeXYZ(const QString &name)
{
    QSettings s;
    s.beginGroup(kTopGroup);
    s.beginGroup(kXYZGroup);
    s.remove(name);
    s.endGroup();
    s.endGroup();
    emit connectionsChanged();
}

// ---------------------------------------------------------------------------
// WMS / WMTS
// ---------------------------------------------------------------------------

void BasemapConnectionStore::saveWMS(const WMSConnection &conn,
                                      const BasemapAuth  &auth)
{
    QSettings s;
    s.beginGroup(kTopGroup);
    s.beginGroup(kWMSGroup);
    s.beginGroup(conn.name);

    s.setValue("url",                   conn.url);
    s.setValue("layerName",             conn.layerName);
    s.setValue("style",                 conn.style);
    s.setValue("imageFormat",           conn.imageFormat);
    s.setValue("crs",                   conn.crs);
    s.setValue("tileMatrixSet",         conn.tileMatrixSet);
    s.setValue("dpiMode",               conn.dpiMode);
    s.setValue("tilePixelRatio",        conn.tilePixelRatio);
    s.setValue("ignoreGetMapURI",       conn.ignoreGetMapURI);
    s.setValue("ignoreAxisOrientation", conn.ignoreAxisOrientation);
    s.setValue("invertAxisOrientation", conn.invertAxisOrientation);
    s.setValue("smoothPixmapTransform", conn.smoothPixmapTransform);
    writeHeaders(s, conn.httpHeaders);

    s.setValue("username", auth.username);
    if (!auth.password.isEmpty())
        s.setValue(kPassword, BasemapCrypto::encrypt(auth.password));
    else
        s.remove(kPassword);

    s.endGroup();
    s.endGroup();
    s.endGroup();
    emit connectionsChanged();
}

WMSConnection BasemapConnectionStore::loadWMS(const QString &name) const
{
    QSettings s;
    s.beginGroup(kTopGroup);
    s.beginGroup(kWMSGroup);
    s.beginGroup(name);

    WMSConnection c;
    c.name                   = name;
    c.url                    = s.value("url").toString();
    c.layerName              = s.value("layerName").toString();
    c.style                  = s.value("style").toString();
    c.imageFormat            = s.value("imageFormat", "image/png").toString();
    c.crs                    = s.value("crs", "EPSG:3857").toString();
    c.tileMatrixSet          = s.value("tileMatrixSet").toString();
    c.dpiMode                = s.value("dpiMode", 7).toInt();
    c.tilePixelRatio         = s.value("tilePixelRatio", 0).toInt();
    c.ignoreGetMapURI        = s.value("ignoreGetMapURI", false).toBool();
    c.ignoreAxisOrientation  = s.value("ignoreAxisOrientation", false).toBool();
    c.invertAxisOrientation  = s.value("invertAxisOrientation", false).toBool();
    c.smoothPixmapTransform  = s.value("smoothPixmapTransform", true).toBool();
    c.httpHeaders            = readHeaders(s);

    s.endGroup();
    s.endGroup();
    s.endGroup();
    return c;
}

BasemapAuth BasemapConnectionStore::loadWMSAuth(const QString &name) const
{
    QSettings s;
    s.beginGroup(kTopGroup);
    s.beginGroup(kWMSGroup);
    s.beginGroup(name);

    BasemapAuth auth;
    auth.username = s.value("username").toString();
    const QByteArray enc = s.value(kPassword).toByteArray();
    if (!enc.isEmpty())
        auth.password = BasemapCrypto::decrypt(enc);

    s.endGroup();
    s.endGroup();
    s.endGroup();
    return auth;
}

QStringList BasemapConnectionStore::wmsConnectionNames() const
{
    QSettings s;
    s.beginGroup(kTopGroup);
    s.beginGroup(kWMSGroup);
    const QStringList names = s.childGroups();
    s.endGroup();
    s.endGroup();
    return names;
}

void BasemapConnectionStore::removeWMS(const QString &name)
{
    QSettings s;
    s.beginGroup(kTopGroup);
    s.beginGroup(kWMSGroup);
    s.remove(name);
    s.endGroup();
    s.endGroup();
    emit connectionsChanged();
}

// ---------------------------------------------------------------------------
// WCS
// ---------------------------------------------------------------------------

void BasemapConnectionStore::saveWCS(const WCSConnection &conn,
                                      const BasemapAuth   &auth)
{
    QSettings s;
    s.beginGroup(kTopGroup);
    s.beginGroup(kWCSGroup);
    s.beginGroup(conn.name);

    s.setValue("url",           conn.url);
    s.setValue("version",       conn.version);
    s.setValue("coverageId",    conn.coverageId);
    s.setValue("outputCrs",     conn.outputCrs);
    s.setValue("outputFormat",  conn.outputFormat);
    s.setValue("rangeSubset",   conn.rangeSubset);
    s.setValue("interpolation", conn.interpolation);
    writeHeaders(s, conn.httpHeaders);

    s.setValue("username", auth.username);
    if (!auth.password.isEmpty())
        s.setValue(kPassword, BasemapCrypto::encrypt(auth.password));
    else
        s.remove(kPassword);

    s.endGroup();
    s.endGroup();
    s.endGroup();
    emit connectionsChanged();
}

WCSConnection BasemapConnectionStore::loadWCS(const QString &name) const
{
    QSettings s;
    s.beginGroup(kTopGroup);
    s.beginGroup(kWCSGroup);
    s.beginGroup(name);

    WCSConnection c;
    c.name          = name;
    c.url           = s.value("url").toString();
    c.version       = s.value("version", QStringLiteral("2.0.1")).toString();
    c.coverageId    = s.value("coverageId").toString();
    c.outputCrs     = s.value("outputCrs",    QStringLiteral("EPSG:4326")).toString();
    c.outputFormat  = s.value("outputFormat", QStringLiteral("image/tiff")).toString();
    c.rangeSubset   = s.value("rangeSubset").toString();
    c.interpolation = s.value("interpolation", QStringLiteral("nearest")).toString();
    c.httpHeaders   = readHeaders(s);

    s.endGroup();
    s.endGroup();
    s.endGroup();
    return c;
}

BasemapAuth BasemapConnectionStore::loadWCSAuth(const QString &name) const
{
    QSettings s;
    s.beginGroup(kTopGroup);
    s.beginGroup(kWCSGroup);
    s.beginGroup(name);

    BasemapAuth auth;
    auth.username = s.value("username").toString();
    const QByteArray enc = s.value(kPassword).toByteArray();
    if (!enc.isEmpty())
        auth.password = BasemapCrypto::decrypt(enc);

    s.endGroup();
    s.endGroup();
    s.endGroup();
    return auth;
}

QStringList BasemapConnectionStore::wcsConnectionNames() const
{
    QSettings s;
    s.beginGroup(kTopGroup);
    s.beginGroup(kWCSGroup);
    const QStringList names = s.childGroups();
    s.endGroup();
    s.endGroup();
    return names;
}

void BasemapConnectionStore::removeWCS(const QString &name)
{
    QSettings s;
    s.beginGroup(kTopGroup);
    s.beginGroup(kWCSGroup);
    s.remove(name);
    s.endGroup();
    s.endGroup();
    emit connectionsChanged();
}

// ---------------------------------------------------------------------------
// ArcGIS REST
// ---------------------------------------------------------------------------

void BasemapConnectionStore::saveArcGIS(const ArcGISRestConnection &conn,
                                         const BasemapAuth          &auth)
{
    QSettings s;
    s.beginGroup(kTopGroup);
    s.beginGroup(kArcGISGroup);
    s.beginGroup(conn.name);

    s.setValue("url",               conn.url);
    s.setValue("urlPrefix",         conn.urlPrefix);
    s.setValue("contentEndpoint",   conn.contentEndpoint);
    s.setValue("communityEndpoint", conn.communityEndpoint);
    writeHeaders(s, conn.httpHeaders);

    s.setValue("username", auth.username);
    if (!auth.password.isEmpty())
        s.setValue(kPassword, BasemapCrypto::encrypt(auth.password));
    else
        s.remove(kPassword);

    s.endGroup();
    s.endGroup();
    s.endGroup();
    emit connectionsChanged();
}

ArcGISRestConnection BasemapConnectionStore::loadArcGIS(const QString &name) const
{
    QSettings s;
    s.beginGroup(kTopGroup);
    s.beginGroup(kArcGISGroup);
    s.beginGroup(name);

    ArcGISRestConnection c;
    c.name              = name;
    c.url               = s.value("url").toString();
    c.urlPrefix         = s.value("urlPrefix").toString();
    c.contentEndpoint   = s.value("contentEndpoint").toString();
    c.communityEndpoint = s.value("communityEndpoint").toString();
    c.httpHeaders       = readHeaders(s);

    s.endGroup();
    s.endGroup();
    s.endGroup();
    return c;
}

BasemapAuth BasemapConnectionStore::loadArcGISAuth(const QString &name) const
{
    QSettings s;
    s.beginGroup(kTopGroup);
    s.beginGroup(kArcGISGroup);
    s.beginGroup(name);

    BasemapAuth auth;
    auth.username = s.value("username").toString();
    const QByteArray enc = s.value(kPassword).toByteArray();
    if (!enc.isEmpty())
        auth.password = BasemapCrypto::decrypt(enc);

    s.endGroup();
    s.endGroup();
    s.endGroup();
    return auth;
}

QStringList BasemapConnectionStore::arcGISConnectionNames() const
{
    QSettings s;
    s.beginGroup(kTopGroup);
    s.beginGroup(kArcGISGroup);
    const QStringList names = s.childGroups();
    s.endGroup();
    s.endGroup();
    return names;
}

void BasemapConnectionStore::removeArcGIS(const QString &name)
{
    QSettings s;
    s.beginGroup(kTopGroup);
    s.beginGroup(kArcGISGroup);
    s.remove(name);
    s.endGroup();
    s.endGroup();
    emit connectionsChanged();
}

// ---------------------------------------------------------------------------
// Local raster
// ---------------------------------------------------------------------------

void BasemapConnectionStore::saveLocalRaster(const LocalRasterConnection &conn)
{
    QSettings s;
    s.beginGroup(kTopGroup);
    s.beginGroup(kLocalRasterGroup);
    s.beginGroup(conn.name);

    s.setValue("filePath",      conn.filePath);
    s.setValue("worldFilePath", conn.worldFilePath);
    s.setValue("crsAuthCode",   conn.crsAuthCode);

    s.endGroup(); // name
    s.endGroup(); // localraster
    s.endGroup(); // BasemapConnections

    emit connectionsChanged();
}

LocalRasterConnection BasemapConnectionStore::loadLocalRaster(const QString &name) const
{
    QSettings s;
    s.beginGroup(kTopGroup);
    s.beginGroup(kLocalRasterGroup);
    s.beginGroup(name);

    LocalRasterConnection c;
    c.name          = name;
    c.filePath      = s.value("filePath").toString();
    c.worldFilePath = s.value("worldFilePath").toString();
    c.crsAuthCode   = s.value("crsAuthCode").toString();

    s.endGroup();
    s.endGroup();
    s.endGroup();
    return c;
}

QStringList BasemapConnectionStore::localRasterConnectionNames() const
{
    QSettings s;
    s.beginGroup(kTopGroup);
    s.beginGroup(kLocalRasterGroup);
    const QStringList names = s.childGroups();
    s.endGroup();
    s.endGroup();
    return names;
}

void BasemapConnectionStore::removeLocalRaster(const QString &name)
{
    QSettings s;
    s.beginGroup(kTopGroup);
    s.beginGroup(kLocalRasterGroup);
    s.remove(name);
    s.endGroup();
    s.endGroup();
    emit connectionsChanged();
}
