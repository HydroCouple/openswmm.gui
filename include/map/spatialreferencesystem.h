/*!
 * \file   spatialreferencesystem.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Qt wrapper around a GDAL OGRSpatialReference, providing CRS
 *         metadata, serialisation helpers, and transformation factory methods.
 */

#ifndef SPATIALREFERENCESYSTEM_H
#define SPATIALREFERENCESYSTEM_H

#include <QObject>
#include <QString>

// Forward-declare GDAL types to avoid polluting headers
class OGRSpatialReference;
class OGRCoordinateTransformation;

/*!
 * \class SpatialReferenceSystem
 * \brief Represents a coordinate reference system backed by a GDAL OGRSpatialReference.
 * \details Provides access to CRS metadata (authority, code, description, units) and
 *          factory methods for creating coordinate transformations between two CRSes.
 *          Retains ownership of the underlying OGRSpatialReference.
 */
class SpatialReferenceSystem : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString authName   READ authName   NOTIFY propertyChanged)
    Q_PROPERTY(int     code       READ code       NOTIFY propertyChanged)
    Q_PROPERTY(QString description READ description WRITE setDescription NOTIFY propertyChanged)
    Q_PROPERTY(QString wkt        READ toWkt      NOTIFY propertyChanged)
    Q_PROPERTY(QString proj4      READ toProj4    NOTIFY propertyChanged)
    Q_PROPERTY(bool    isGeographic READ isGeographic NOTIFY propertyChanged)
    Q_PROPERTY(bool    isProjected  READ isProjected  NOTIFY propertyChanged)
    Q_PROPERTY(QString linearUnits  READ linearUnitsName NOTIFY propertyChanged)

public:

    /*!
     * \brief Constructs a CRS from an EPSG code.
     * \param authName  Authority name, e.g. "EPSG" (default).
     * \param code      Numeric authority code, e.g. 4326 (WGS 84, default).
     * \param parent    Qt parent object.
     */
    explicit SpatialReferenceSystem(const QString &authName = "EPSG",
                                    int code = 4326,
                                    QObject *parent = nullptr);

    /*!
     * \brief Constructs a CRS from a WKT or PROJ string.
     * \param wktOrProj  WKT or PROJ.4/PROJ string representation.
     * \param parent     Qt parent object.
     */
    explicit SpatialReferenceSystem(const QString &wktOrProj,
                                    QObject *parent = nullptr);

    /*!
     * \brief Copy-constructs from another SpatialReferenceSystem.
     */
    explicit SpatialReferenceSystem(const SpatialReferenceSystem &other,
                                    QObject *parent = nullptr);

    virtual ~SpatialReferenceSystem();

    // ----- Identity --------------------------------------------------------

    /*!
     * \brief Returns the authority name (e.g. "EPSG", "ESRI").
     */
    [[nodiscard]] QString authName() const;

    /*!
     * \brief Returns the numeric authority code, or -1 if not known.
     */
    [[nodiscard]] int code() const;

    /*!
     * \brief Returns a human-readable description/name for this CRS.
     */
    [[nodiscard]] QString description() const;

    /*!
     * \brief Sets a custom description for this CRS.
     */
    void setDescription(const QString &description);

    // ----- Serialization ---------------------------------------------------

    /*!
     * \brief Exports this CRS as a WKT2 string.
     * \return WKT2 representation, or an empty string on failure.
     */
    [[nodiscard]] QString toWkt() const;

    /*!
     * \brief Exports this CRS as a legacy PROJ.4 string.
     * \return PROJ.4 string, or an empty string on failure.
     */
    [[nodiscard]] QString toProj4() const;

    /*!
     * \brief Returns the "AUTHORITY:CODE" string, e.g. "EPSG:4326".
     */
    [[nodiscard]] QString toAuthority() const;

    // ----- Type predicates -------------------------------------------------

    /*!
     * \brief Returns true when the CRS uses geographic (angular) coordinates.
     */
    [[nodiscard]] bool isGeographic() const;

    /*!
     * \brief Returns true when the CRS is a projected (Cartesian) CRS.
     */
    [[nodiscard]] bool isProjected() const;

    /*!
     * \brief Returns true when this CRS equals \p other (within GDAL tolerance).
     */
    [[nodiscard]] bool equals(const SpatialReferenceSystem &other) const;

    // ----- Units -----------------------------------------------------------

    /*!
     * \brief Returns the name of the linear unit (e.g. "metre", "US survey foot").
     *        Returns an empty string for geographic CRSes.
     */
    [[nodiscard]] QString linearUnitsName() const;

    /*!
     * \brief Returns the conversion factor from the linear unit to metres.
     *        Returns 1.0 for geographic CRSes.
     */
    [[nodiscard]] double linearUnitsToMetres() const;

    /*!
     * \brief Returns the name of the angular unit (e.g. "degree").
     *        Returns an empty string for projected CRSes.
     */
    [[nodiscard]] QString angularUnitsName() const;

    // ----- GDAL interop ----------------------------------------------------

    /*!
     * \brief Returns a non-owning pointer to the underlying OGRSpatialReference.
     * \warning Caller must not delete the returned pointer.
     */
    [[nodiscard]] OGRSpatialReference *ogrSpatialReference() const;

    /*!
     * \brief Creates a new OGRCoordinateTransformation from this CRS to \p target.
     * \details Caller takes ownership of the returned object; destroy it with
     *          OGRCoordinateTransformation::DestroyCT() or delete.
     * \param target  Destination CRS.
     * \returns A new transformation, or nullptr on failure.
     */
    [[nodiscard]] OGRCoordinateTransformation *
    createTransformationTo(const SpatialReferenceSystem &target) const;

    // ----- Factory methods -------------------------------------------------

    /*!
     * \brief Creates a SpatialReferenceSystem from a WKT or PROJ string.
     * \param wktOrProj  WKT or PROJ string.
     * \param parent     Qt parent for the new object.
     * \returns          New SpatialReferenceSystem (caller owns it), or nullptr on failure.
     */
    [[nodiscard]] static SpatialReferenceSystem *fromWktOrProj(const QString &wktOrProj,
                                                               QObject *parent = nullptr);

    /*!
     * \brief Creates a SpatialReferenceSystem from an authority code.
     * \param authName   e.g. "EPSG".
     * \param code       Numeric EPSG/authority code.
     * \param parent     Qt parent for the new object.
     * \returns          New SpatialReferenceSystem (caller owns it), or nullptr on failure.
     */
    [[nodiscard]] static SpatialReferenceSystem *fromAuthCode(const QString &authName,
                                                              int code,
                                                              QObject *parent = nullptr);

    /*!
     * \brief Creates a local / untitled CRS for models with no coordinate system defined.
     *        Coordinates are treated as raw Cartesian values; no reprojection is possible.
     */
    [[nodiscard]] static SpatialReferenceSystem *untitled(QObject *parent = nullptr);

    /*!
     * \brief Creates a local CRS with linear units derived from the SWMM `[MAP]` section
     *        `Units` field (e.g. "FEET" or "METERS"). Distances and the scale bar will
     *        show correct values without requiring a geographic CRS.
     * \param mapUnits  Value of the `[MAP] Units` line (case-insensitive: "FEET"/"METERS"/…).
     * \param parent    Qt parent for the new object.
     */
    [[nodiscard]] static SpatialReferenceSystem *localFromMapUnits(const QString &mapUnits,
                                                                    QObject *parent = nullptr);

    // ----- Local-CS predicate ----------------------------------------------

    /*! Returns true when this is a local (non-geographic, non-projected) CRS. */
    [[nodiscard]] bool isLocal() const;

signals:
    /*!
     * \brief Emitted when any property changes.
     * \param propertyName  Name of the changed Q_PROPERTY.
     */
    void propertyChanged(const QString &propertyName);

private:
    SpatialReferenceSystem(QObject *parent, Qt::Initialization);  // null-init for factory use
    void initFromAuthCode(const QString &authName, int code);
    void initFromWktOrProj(const QString &wktOrProj);

    OGRSpatialReference *m_ogrSRS = nullptr; /*!< Owned GDAL spatial reference. */
    QString              m_authName;
    int                  m_code = -1;
    QString              m_description;
};

#endif // SPATIALREFERENCESYSTEM_H
