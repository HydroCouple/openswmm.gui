/**
 * @file      basemapconnectionstore.h
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @brief     Singleton QSettings-backed registry for all saved basemap connections.
 *
 * @details
 * Passwords are stored AES-256-CBC encrypted (via `BasemapCrypto`).
 * HTTP headers are persisted under `http-header/{key}` sub-groups,
 * matching QGIS's PATH_PREFIX convention.
 *
 * QSettings organisation / application keys follow the main app (set by
 * QCoreApplication::setOrganizationName / setApplicationName).
 * The store lives under the "BasemapConnections" top-level group.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
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
 *   auto conn = store->loadXYZ("CartoDB Positron");
 *   auto auth = store->loadXYZAuth("CartoDB Positron");
 * \endcode
 */
class BasemapConnectionStore : public QObject
{
    Q_OBJECT

public:
    /**
     * @brief Access the process-wide saved-profile registry.
     * @return Borrowed singleton; do not delete. Access its QObject interface on the GUI thread.
     */
    static BasemapConnectionStore *instance();

    // ------ XYZ -------------------------------------------------------

    /*! \brief Saves (or updates) an XYZ connection.  Password is encrypted.
     * @brief Persist a XYZ profile under its connection name.
     * @param[in] conn Profile copied into the corresponding QSettings subgroup; a matching name overwrites those fields.
     * @param[in] auth Username is stored as text; a nonempty password is encrypted for this machine, and an empty password removes the saved password.
     * @details Emits connectionsChanged after writing. No connection validation or QSettings/cipher failure result is exposed.
     */
    void saveXYZ(const XYZConnection &conn, const BasemapAuth &auth);

    /*! \brief Returns the named XYZ connection, using field defaults when settings are absent.
     * @brief Load a saved XYZ profile without network or file access.
     * @param[in] name Exact QSettings connection subgroup name.
     * @return Value copy with name set to the requested name; missing fields use stored-type defaults. Built-in XYZ entries are supplied separately by XYZConnection::builtins().
     */
    XYZConnection loadXYZ(const QString &name) const;

    /*! \brief Returns the decrypted auth for the named XYZ connection.
     * @brief Read saved credentials for a XYZ profile.
     * @param[in] name Exact connection subgroup name.
     * @return Username plus decrypted password; missing data or decryption failure yields an empty password.
     */
    BasemapAuth loadXYZAuth(const QString &name) const;

    /*! \brief Names of all stored XYZ connections (builtins excluded).
     * @brief Enumerate saved XYZ profile groups.
     * @return Names present in QSettings; built-in definitions are not merged into this list.
     */
    QStringList xyzConnectionNames() const;

    /**
     * @brief Remove the named XYZ settings subgroup, including saved credentials if any.
     * @param[in] name QSettings subgroup to remove; an empty name follows QSettings remove semantics for the whole protocol group.
     * @details Emits connectionsChanged even when no matching profile exists. Built-in status is not enforced here; callers enforce deletion policy.
     */
    void removeXYZ(const QString &name);

    // ------ WMS / WMTS -----------------------------------------------

    /**
     * @brief Persist a WMS/WMTS profile under its connection name.
     * @param[in] conn Profile copied into the corresponding QSettings subgroup; a matching name overwrites those fields.
     * @param[in] auth Username is stored as text; a nonempty password is encrypted for this machine, and an empty password removes the saved password.
     * @details Emits connectionsChanged after writing. No connection validation or QSettings/cipher failure result is exposed.
     */
    void saveWMS(const WMSConnection &conn, const BasemapAuth &auth);
    /**
     * @brief Load a saved WMS/WMTS profile without network or file access.
     * @param[in] name Exact QSettings connection subgroup name.
     * @return Value copy with name set to the requested name; missing fields use stored-type defaults. Built-in XYZ entries are supplied separately by XYZConnection::builtins().
     */
    WMSConnection loadWMS(const QString &name) const;
    /**
     * @brief Read saved credentials for a WMS/WMTS profile.
     * @param[in] name Exact connection subgroup name.
     * @return Username plus decrypted password; missing data or decryption failure yields an empty password.
     */
    BasemapAuth loadWMSAuth(const QString &name) const;
    /**
     * @brief Enumerate saved WMS/WMTS profile groups.
     * @return Names present in QSettings; built-in definitions are not merged into this list.
     */
    QStringList wmsConnectionNames() const;
    /**
     * @brief Remove the named WMS/WMTS settings subgroup, including saved credentials if any.
     * @param[in] name QSettings subgroup to remove; an empty name follows QSettings remove semantics for the whole protocol group.
     * @details Emits connectionsChanged even when no matching profile exists. Built-in status is not enforced here; callers enforce deletion policy.
     */
    void removeWMS(const QString &name);

    // ------ WCS ------------------------------------------------------

    /**
     * @brief Persist a WCS profile under its connection name.
     * @param[in] conn Profile copied into the corresponding QSettings subgroup; a matching name overwrites those fields.
     * @param[in] auth Username is stored as text; a nonempty password is encrypted for this machine, and an empty password removes the saved password.
     * @details Emits connectionsChanged after writing. No connection validation or QSettings/cipher failure result is exposed.
     */
    void saveWCS(const WCSConnection &conn, const BasemapAuth &auth);
    /**
     * @brief Load a saved WCS profile without network or file access.
     * @param[in] name Exact QSettings connection subgroup name.
     * @return Value copy with name set to the requested name; missing fields use stored-type defaults. Built-in XYZ entries are supplied separately by XYZConnection::builtins().
     */
    WCSConnection loadWCS(const QString &name) const;
    /**
     * @brief Read saved credentials for a WCS profile.
     * @param[in] name Exact connection subgroup name.
     * @return Username plus decrypted password; missing data or decryption failure yields an empty password.
     */
    BasemapAuth loadWCSAuth(const QString &name) const;
    /**
     * @brief Enumerate saved WCS profile groups.
     * @return Names present in QSettings; built-in definitions are not merged into this list.
     */
    QStringList wcsConnectionNames() const;
    /**
     * @brief Remove the named WCS settings subgroup, including saved credentials if any.
     * @param[in] name QSettings subgroup to remove; an empty name follows QSettings remove semantics for the whole protocol group.
     * @details Emits connectionsChanged even when no matching profile exists. Built-in status is not enforced here; callers enforce deletion policy.
     */
    void removeWCS(const QString &name);

    // ------ WFS ------------------------------------------------------

    /**
     * @brief Persist a WFS profile under its connection name.
     * @param[in] conn Profile copied into the corresponding QSettings subgroup; a matching name overwrites those fields.
     * @param[in] auth Username is stored as text; a nonempty password is encrypted for this machine, and an empty password removes the saved password.
     * @details Emits connectionsChanged after writing. No connection validation or QSettings/cipher failure result is exposed.
     */
    void saveWFS(const WFSConnection &conn, const BasemapAuth &auth);
    /**
     * @brief Load a saved WFS profile without network or file access.
     * @param[in] name Exact QSettings connection subgroup name.
     * @return Value copy with name set to the requested name; missing fields use stored-type defaults. Built-in XYZ entries are supplied separately by XYZConnection::builtins().
     */
    WFSConnection loadWFS(const QString &name) const;
    /**
     * @brief Read saved credentials for a WFS profile.
     * @param[in] name Exact connection subgroup name.
     * @return Username plus decrypted password; missing data or decryption failure yields an empty password.
     */
    BasemapAuth loadWFSAuth(const QString &name) const;
    /**
     * @brief Enumerate saved WFS profile groups.
     * @return Names present in QSettings; built-in definitions are not merged into this list.
     */
    QStringList wfsConnectionNames() const;
    /**
     * @brief Remove the named WFS settings subgroup, including saved credentials if any.
     * @param[in] name QSettings subgroup to remove; an empty name follows QSettings remove semantics for the whole protocol group.
     * @details Emits connectionsChanged even when no matching profile exists. Built-in status is not enforced here; callers enforce deletion policy.
     */
    void removeWFS(const QString &name);

    // ------ ArcGIS REST ----------------------------------------------

    /**
     * @brief Persist a ArcGIS REST profile under its connection name.
     * @param[in] conn Profile copied into the corresponding QSettings subgroup; a matching name overwrites those fields.
     * @param[in] auth Username is stored as text; a nonempty password is encrypted for this machine, and an empty password removes the saved password.
     * @details Emits connectionsChanged after writing. No connection validation or QSettings/cipher failure result is exposed.
     */
    void saveArcGIS(const ArcGISRestConnection &conn, const BasemapAuth &auth);
    /**
     * @brief Load a saved ArcGIS REST profile without network or file access.
     * @param[in] name Exact QSettings connection subgroup name.
     * @return Value copy with name set to the requested name; missing fields use stored-type defaults. Built-in XYZ entries are supplied separately by XYZConnection::builtins().
     */
    ArcGISRestConnection loadArcGIS(const QString &name) const;
    /**
     * @brief Read saved credentials for a ArcGIS REST profile.
     * @param[in] name Exact connection subgroup name.
     * @return Username plus decrypted password; missing data or decryption failure yields an empty password.
     */
    BasemapAuth loadArcGISAuth(const QString &name) const;
    /**
     * @brief Enumerate saved ArcGIS REST profile groups.
     * @return Names present in QSettings; built-in definitions are not merged into this list.
     */
    QStringList arcGISConnectionNames() const;
    /**
     * @brief Remove the named ArcGIS REST settings subgroup, including saved credentials if any.
     * @param[in] name QSettings subgroup to remove; an empty name follows QSettings remove semantics for the whole protocol group.
     * @details Emits connectionsChanged even when no matching profile exists. Built-in status is not enforced here; callers enforce deletion policy.
     */
    void removeArcGIS(const QString &name);

    // ------ Local raster (no auth — local files have none) -----------

    /**
     * @brief Persist a local raster profile under its connection name.
     * @param[in] conn Profile copied into the corresponding QSettings subgroup; a matching name overwrites those fields.
     * @details Emits connectionsChanged after writing. No connection validation or QSettings/cipher failure result is exposed.
     */
    void saveLocalRaster(const LocalRasterConnection &conn);
    /**
     * @brief Load a saved local raster profile without network or file access.
     * @param[in] name Exact QSettings connection subgroup name.
     * @return Value copy with name set to the requested name; missing fields use stored-type defaults. Built-in XYZ entries are supplied separately by XYZConnection::builtins().
     */
    LocalRasterConnection loadLocalRaster(const QString &name) const;
    /**
     * @brief Enumerate saved local raster profile groups.
     * @return Names present in QSettings; built-in definitions are not merged into this list.
     */
    QStringList localRasterConnectionNames() const;
    /**
     * @brief Remove the named local raster settings subgroup, including saved credentials if any.
     * @param[in] name QSettings subgroup to remove; an empty name follows QSettings remove semantics for the whole protocol group.
     * @details Emits connectionsChanged even when no matching profile exists. Built-in status is not enforced here; callers enforce deletion policy.
     */
    void removeLocalRaster(const QString &name);

signals:
    /**
     * @brief Notify observers after any save/remove call, without a persistence-success guarantee.
     */
    void connectionsChanged();

private:
    /**
     * @brief Construct the QObject registry facade; profiles are loaded on demand.
     * @param[in] parent Optional Qt owner; singleton construction uses nullptr.
     */
    explicit BasemapConnectionStore(QObject *parent = nullptr);

    // Helpers for header map serialisation
    /**
     * @brief Replace the complete http-header subgroup in the current settings group.
     * @param[in,out] s Settings positioned at a connection group; the group position is restored before return.
     * @param[in] headers Header names and values copied verbatim into child keys.
     */
    static void writeHeaders(class QSettings &s, const BasemapHttpHeaders &headers);
    /**
     * @brief Read HTTP headers from the current connection settings group.
     * @param[in,out] s Settings whose current group is restored before return.
     * @return Value map of direct child keys under http-header; empty when absent.
     */
    static BasemapHttpHeaders readHeaders(class QSettings &s);
};

#endif // BASEMAPCONNECTIONSTORE_H
