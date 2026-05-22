/*!
 * \file   basemapconnectionstore.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \brief  Singleton QSettings-backed registry for all saved basemap connections.
 *
 * \details
 * Passwords are stored AES-256-CBC encrypted (via `BasemapCrypto`).
 * HTTP headers are persisted under `http-header/{key}` sub-groups,
 * matching QGIS's PATH_PREFIX convention.
 *
 * QSettings organisation / application keys follow the main app (set by
 * QCoreApplication::setOrganizationName / setApplicationName).
 * The store lives under the "BasemapConnections" top-level group.
 */
#ifndef BASEMAPCONNECTIONSTORE_H
#define BASEMAPCONNECTIONSTORE_H

#include "connections/basemapconnection.h"

#include <QObject>
#include <QStringList>

/*!
 * \class BasemapConnectionStore
 * \brief Persistent, encrypted registry of saved basemap connection profiles.
 *
 * Usage:
 * \code
 *   auto *store = BasemapConnectionStore::instance();
 *   store->saveXYZ(conn, auth);
 *   auto [conn, auth] = store->loadXYZ("CartoDB Positron");
 * \endcode
 */
class BasemapConnectionStore : public QObject
{
    Q_OBJECT

public:
    static BasemapConnectionStore *instance();

    // ------ XYZ -------------------------------------------------------

    /*! \brief Saves (or updates) an XYZ connection.  Password is encrypted. */
    void saveXYZ(const XYZConnection &conn, const BasemapAuth &auth);

    /*! \brief Returns the named XYZ connection, or a default-constructed one. */
    XYZConnection loadXYZ(const QString &name) const;

    /*! \brief Returns the decrypted auth for the named XYZ connection. */
    BasemapAuth loadXYZAuth(const QString &name) const;

    /*! \brief Names of all stored XYZ connections (builtins excluded). */
    QStringList xyzConnectionNames() const;

    void removeXYZ(const QString &name);

    // ------ WMS / WMTS -----------------------------------------------

    void saveWMS(const WMSConnection &conn, const BasemapAuth &auth);
    WMSConnection loadWMS(const QString &name) const;
    BasemapAuth loadWMSAuth(const QString &name) const;
    QStringList wmsConnectionNames() const;
    void removeWMS(const QString &name);

    // ------ WCS ------------------------------------------------------

    void saveWCS(const WCSConnection &conn, const BasemapAuth &auth);
    WCSConnection loadWCS(const QString &name) const;
    BasemapAuth loadWCSAuth(const QString &name) const;
    QStringList wcsConnectionNames() const;
    void removeWCS(const QString &name);

    // ------ ArcGIS REST ----------------------------------------------

    void saveArcGIS(const ArcGISRestConnection &conn, const BasemapAuth &auth);
    ArcGISRestConnection loadArcGIS(const QString &name) const;
    BasemapAuth loadArcGISAuth(const QString &name) const;
    QStringList arcGISConnectionNames() const;
    void removeArcGIS(const QString &name);

signals:
    void connectionsChanged();

private:
    explicit BasemapConnectionStore(QObject *parent = nullptr);

    // Helpers for header map serialisation
    static void writeHeaders(class QSettings &s, const BasemapHttpHeaders &headers);
    static BasemapHttpHeaders readHeaders(class QSettings &s);
};

#endif // BASEMAPCONNECTIONSTORE_H
