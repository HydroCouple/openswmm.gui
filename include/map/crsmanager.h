/**
 * @file      crsmanager.h
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     Application-wide singleton for querying the GDAL CRS database and
 *         managing recently used coordinate reference systems.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef CRSMANAGER_H
#define CRSMANAGER_H

#include <QList>
#include <QMap>
#include <QObject>
#include <QPair>
#include <QString>

class SpatialReferenceSystem;

/*!
 * \struct CRSInfo
 * \brief Lightweight record describing one entry in the GDAL CRS database.
 */
struct CRSInfo {
    QString authName; /*!< Authority, e.g. "EPSG". */
    int code = -1;    /*!< Numeric authority code. */
    QString name;     /*!< Human-readable name. */
    QString type;     /*!< CRS type string ("geographic 2D", "projected", etc.). */
    QString areaName; /*!< Area of use, e.g. "World", "USA - Florida". */
    bool deprecated = false;
};

/*!
 * \class CRSManager
 * \brief Singleton that queries the GDAL CRS database and manages CRS objects.
 * \details Provides methods to:
 *  - List all CRSes available in the GDAL database (filterable by authority / type / keyword).
 *  - Create SpatialReferenceSystem instances by authority+code or WKT/PROJ string.
 *  - Compare two SRSes for equality.
 *  - Query units for a CRS.
 *
 * Usage:
 * \code
 *   CRSManager &mgr = CRSManager::instance();
 *   QList<CRSInfo> list = mgr.queryDatabase("UTM", "EPSG", false);
 * \endcode
 */
class CRSManager : public QObject
{
    Q_OBJECT

    Q_PROPERTY(int totalCRSCount READ totalCRSCount CONSTANT)
    Q_PROPERTY(QStringList authorities READ availableAuthorities CONSTANT)

public:
    /*!
     * \brief Returns the application-wide singleton instance.
     * @return Borrowed process-lifetime static manager; first access registers GDAL, creates
     *   builtin wrappers and appends persistent recents. Create/use on application GUI thread;
     *   mutable QObject/settings/recent state has no locking.
     */
    static CRSManager &instance();

    // ----- Database queries ------------------------------------------------

    /*!
     * \brief Returns a flat list of all (or matching) CRS records from the GDAL database.
     * \param keyword          Free-text filter (matches name / code string). Empty = no filter.
     * \param authority        Authority filter, e.g. "EPSG". Empty = all authorities.
     * \param includeDeprecated Whether to include deprecated CRSes (default: false).
     * \returns List of matching CRSInfo records in GDAL database enumeration order.
     * @note Returns GDAL database enumeration order; implementation does NOT explicitly sort
     *   despite historical wording. Keyword matches lowercased name or decimal numeric code only,
     *   not authority/type/area; numeric authority text conversion can yield0. Frees GDAL list;
     *   missing/empty database returns empty and triggers one process-wide diagnostic. No query
     *   cache.
     */
    [[nodiscard]] QList<CRSInfo> queryDatabase(const QString &keyword = {},
                                               const QString &authority = {},
                                               bool includeDeprecated = false) const;

    /*!
     * \brief Returns the raw total number of CRSes in the GDAL database.
     * @return Raw GDAL database count across all authorities, including deprecated records as
     *   returned by default API; no filtering/caching or one-shot warning.0typically means
     *   unavailable/empty database.
     */
    [[nodiscard]] int totalCRSCount() const;

    /*!
     * \brief Returns the list of authority names found in the GDAL database.
     * @return Owned deduplicated sorted authority names from current GDAL database, empty on
     *   missing list. Includes authorities represented only by deprecated records; no caching.
     */
    [[nodiscard]] QStringList availableAuthorities() const;

    // ----- Factory ---------------------------------------------------------

    /*!
     * \brief Creates a CRS from an authority + code pair.
     * \param authName  e.g. "EPSG".
     * \param code      Numeric authority code.
     * \param parent    Qt parent for the new object.
     * \returns         Heap-allocated SpatialReferenceSystem, or nullptr on failure.
     *                  Caller takes ownership unless a parent is given.
     * @note Delegates null-on-parse-failure SpatialReferenceSystem factory with traditional GIS
     *   axes; parent supplied means parent ownership, otherwise caller. Does not update recents or
     *   builtin objects.
     */
    [[nodiscard]] SpatialReferenceSystem *createFromAuthCode(const QString &authName, int code,
                                                             QObject *parent = nullptr) const;

    /*!
     * \brief Creates a CRS from a WKT2 or PROJ string.
     * \param wktOrProj  WKT2 or PROJ string.
     * \param parent     Qt parent for the new object.
     * \returns          Heap-allocated SpatialReferenceSystem, or nullptr on failure.
     * @note Parent owns success when nonnull, otherwise caller; factory deletes invalid wrapper.
     *   GDAL user-input grammar broader than WKT/PROJ accepted; no recents/settings mutation.
     */
    [[nodiscard]] SpatialReferenceSystem *createFromWktOrProj(const QString &wktOrProj,
                                                              QObject *parent = nullptr) const;

    /*!
     * \brief Returns true when \p a and \p b represent the same CRS.
     * @param[in] a Stable borrowed first wrapper.
     * @param[in] b Stable borrowed second wrapper.
     * @return GDAL semantic equals result, false if either invalid even both invalid; custom
     *   labels/QObject identity ignored.
     */
    [[nodiscard]] bool areSameCRS(const SpatialReferenceSystem &a,
                                  const SpatialReferenceSystem &b) const;

    // ----- Commonly used CRSes ---------------------------------------------

    /*!
     * \brief Returns the WGS 84 geographic CRS (EPSG:4326).
     * \details The returned pointer is owned by CRSManager and stays valid
     *          for the lifetime of the application.
     * @return Borrowed mutable manager-owned EPSG:4326 wrapper, valid until manager destruction.
     *   Constructor allocation is nonnull even if GDAL parse failed; inspect ogrSpatialReference
     *   before use. Do not delete/reparent or mutate shared CRS unexpectedly.
     */
    [[nodiscard]] SpatialReferenceSystem *wgs84() const;

    /*!
     * \brief Returns the Web Mercator projected CRS (EPSG:3857).
     * @return Borrowed mutable manager-owned EPSG:3857 wrapper, valid until manager destruction.
     *   Constructor allocation is nonnull even if GDAL parse failed; inspect ogrSpatialReference
     *   before use. Do not delete/reparent or mutate shared CRS unexpectedly.
     */
    [[nodiscard]] SpatialReferenceSystem *webMercator() const;

    // ----- Recent / favourites --------------------------------------------

    /*!
     * \brief Returns authority:code pairs the user has recently used.
     * @return Owned most-recent-first raw authority/code pairs. Loaded state may exceed 20 or contain
     *   duplicates because loadSettings appends; no validation/database availability guaranteed.
     */
    [[nodiscard]] QList<QPair<QString, int>> recentCRSes() const;

    /*!
     * \brief Records an authority+code pair as recently used.
     * @param[in] authName Raw authority string; case retained and exact pair equality used.
     * @param[in] code Raw numeric code, copied without positivity/database validation.
     * @note Removes exact duplicate pair, prepends, trims to 20 and always emits recentCRSesChanged,
     *   even if already first. Does not save settings immediately.
     */
    void addRecentCRS(const QString &authName, int code);

    /*!
     * \brief Clears the recent-CRS list.
     * @note Clears list and emits recentCRSesChanged even if empty; does not persist until
     *   saveSettings/destruction.
     */
    void clearRecentCRSes();

    /*!
     * \brief Loads recent CRS list from persistent settings.
     * @note Reads CRSManager/RecentCRSes/crs QSettings array and APPENDS entries with nonempty
     *   authority/code > 0 to existing list. No clearing, dedup, 20 limit, signal or status/error
     *   handling; repeated calls duplicate valid stored rows.
     */
    void loadSettings();

    /*!
     * \brief Saves recent CRS list to persistent settings.
     * @note Writes current full recent array to default QSettings, including duplicates/invalid
     *   in-memory pairs; no explicit sync/status/error reporting. Does not emit or change list.
     */
    void saveSettings() const;

signals:
    /*!
     * \brief Emitted when the recent-CRS list changes.
     * @note Emitted unconditionally by add/clear only; loadSettings appends silently. Signal
     *   implies in-memory list mutation policy, not persistence success.
     */
    void recentCRSesChanged();

private:
    /**
     * @param[in] parent Optional QObject owner; singleton normally uses null.
     * @note Registers GDAL, builtin EPSG wrappers and loads recents synchronously. No locking; call
     *   before consumers need shared CRS objects.
     */
    explicit CRSManager(QObject *parent = nullptr);
    /**
     * @brief Save recent settings and delete both owned builtin CRS wrappers; borrowed pointers
     *   become invalid.
     * @note Settings errors are not surfaced, destruction must occur after dependent
     *   transformations/views finish.
     */
    ~CRSManager() override;

    // Non-copyable singleton
    /**
     * @brief Copy construction disabled to preserve singleton identity and unique builtin
     *   ownership.
     */
    CRSManager(const CRSManager &) = delete;
    /**
     * @brief Copy assignment disabled; manager state cannot replace singleton ownership.
     */
    CRSManager &operator=(const CRSManager &) = delete;

    /**
     * @brief Allocate manager-owned WGS84/WebMercator wrappers with traditional GIS axes.
     * @note No parse validation or replacement cleanup; constructor-only helper, never call twice
     *   on initialized builtin pointers.
     */
    void initBuiltins();

    SpatialReferenceSystem *m_wgs84 = nullptr;
    SpatialReferenceSystem *m_webMercator = nullptr;
    QList<QPair<QString, int>> m_recentCRSes;
    static constexpr int MaxRecentCRSes = 20;
};

#endif // CRSMANAGER_H
