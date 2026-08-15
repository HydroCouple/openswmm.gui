/*!
 * \file   crsmanager.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Application-wide singleton for querying the GDAL CRS database and
 *         managing recently used coordinate reference systems.
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
struct CRSInfo
{
    QString authName;       /*!< Authority, e.g. "EPSG". */
    int     code = -1;      /*!< Numeric authority code. */
    QString name;           /*!< Human-readable name. */
    QString type;           /*!< CRS type string ("geographic 2D", "projected", etc.). */
    QString areaName;       /*!< Area of use, e.g. "World", "USA - Florida". */
    bool    deprecated = false;
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

    Q_PROPERTY(int   totalCRSCount      READ totalCRSCount      CONSTANT)
    Q_PROPERTY(QStringList authorities  READ availableAuthorities CONSTANT)

public:

    /*!
     * \brief Returns the application-wide singleton instance.
     */
    static CRSManager &instance();

    // ----- Database queries ------------------------------------------------

    /*!
     * \brief Returns a flat list of all (or matching) CRS records from the GDAL database.
     * \param keyword          Free-text filter (matches name / code string). Empty = no filter.
     * \param authority        Authority filter, e.g. "EPSG". Empty = all authorities.
     * \param includeDeprecated Whether to include deprecated CRSes (default: false).
     * \returns List of matching CRSInfo records, sorted by authority then code.
     */
    [[nodiscard]] QList<CRSInfo> queryDatabase(const QString &keyword = {},
                                               const QString &authority = {},
                                               bool includeDeprecated = false) const;

    /*!
     * \brief Returns the total number of non-deprecated CRSes in the GDAL database.
     */
    [[nodiscard]] int totalCRSCount() const;

    /*!
     * \brief Returns the list of authority names found in the GDAL database.
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
     */
    [[nodiscard]] SpatialReferenceSystem *createFromAuthCode(const QString &authName,
                                                             int code,
                                                             QObject *parent = nullptr) const;

    /*!
     * \brief Creates a CRS from a WKT2 or PROJ string.
     * \param wktOrProj  WKT2 or PROJ string.
     * \param parent     Qt parent for the new object.
     * \returns          Heap-allocated SpatialReferenceSystem, or nullptr on failure.
     */
    [[nodiscard]] SpatialReferenceSystem *createFromWktOrProj(const QString &wktOrProj,
                                                              QObject *parent = nullptr) const;

    /*!
     * \brief Returns true when \p a and \p b represent the same CRS.
     */
    [[nodiscard]] bool areSameCRS(const SpatialReferenceSystem &a,
                                  const SpatialReferenceSystem &b) const;

    // ----- Commonly used CRSes ---------------------------------------------

    /*!
     * \brief Returns the WGS 84 geographic CRS (EPSG:4326).
     * \details The returned pointer is owned by CRSManager and stays valid
     *          for the lifetime of the application.
     */
    [[nodiscard]] SpatialReferenceSystem *wgs84() const;

    /*!
     * \brief Returns the Web Mercator projected CRS (EPSG:3857).
     */
    [[nodiscard]] SpatialReferenceSystem *webMercator() const;

    // ----- Recent / favourites --------------------------------------------

    /*!
     * \brief Returns authority:code pairs the user has recently used.
     */
    [[nodiscard]] QList<QPair<QString, int>> recentCRSes() const;

    /*!
     * \brief Records an authority+code pair as recently used.
     */
    void addRecentCRS(const QString &authName, int code);

    /*!
     * \brief Clears the recent-CRS list.
     */
    void clearRecentCRSes();

    /*!
     * \brief Loads recent CRS list from persistent settings.
     */
    void loadSettings();

    /*!
     * \brief Saves recent CRS list to persistent settings.
     */
    void saveSettings() const;

signals:
    /*!
     * \brief Emitted when the recent-CRS list changes.
     */
    void recentCRSesChanged();

private:
    explicit CRSManager(QObject *parent = nullptr);
    ~CRSManager() override;

    // Non-copyable singleton
    CRSManager(const CRSManager &)            = delete;
    CRSManager &operator=(const CRSManager &) = delete;

    void initBuiltins();

    SpatialReferenceSystem              *m_wgs84       = nullptr;
    SpatialReferenceSystem              *m_webMercator = nullptr;
    QList<QPair<QString, int>>           m_recentCRSes;
    static constexpr int                 MaxRecentCRSes = 20;
};

#endif // CRSMANAGER_H
