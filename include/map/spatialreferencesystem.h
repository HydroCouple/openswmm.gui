/**
 * @file      spatialreferencesystem.h
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     Qt wrapper around a GDAL OGRSpatialReference, providing CRS
 *         metadata, serialisation helpers, and transformation factory methods.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
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
    Q_PROPERTY(QString authName READ authName NOTIFY propertyChanged)
    Q_PROPERTY(int code READ code NOTIFY propertyChanged)
    Q_PROPERTY(QString description READ description WRITE setDescription NOTIFY propertyChanged)
    Q_PROPERTY(QString wkt READ toWkt NOTIFY propertyChanged)
    Q_PROPERTY(QString proj4 READ toProj4 NOTIFY propertyChanged)
    Q_PROPERTY(bool isGeographic READ isGeographic NOTIFY propertyChanged)
    Q_PROPERTY(bool isProjected READ isProjected NOTIFY propertyChanged)
    Q_PROPERTY(QString linearUnits READ linearUnitsName NOTIFY propertyChanged)

public:
    /*!
     * \brief Constructs a CRS from an EPSG code.
     * \param authName  Authority name, e.g. "EPSG" (default).
     * \param code      Numeric authority code, e.g. 4326 (WGS 84, default).
     * \param parent    Qt parent object.
     * @note Parses AUTH:code with GDAL SetFromUserInput and traditional GIS axis mapping
     *   (X/easting/lon before Y/northing/lat). Failure leaves an allocated QObject with null OGR
     *   handle and default identity; use fromAuthCode factory for null-on-invalid policy. Owns
     *   handle, does not persist or emit propertyChanged during initialization.
     */
    explicit SpatialReferenceSystem(const QString &authName = "EPSG", int code = 4326,
                                    QObject *parent = nullptr);

    /*!
     * \brief Constructs a CRS from a WKT or PROJ string.
     * \param wktOrProj  WKT or PROJ.4/PROJ string representation.
     * \param parent     Qt parent object.
     * @note Uses GDAL SetFromUserInput grammar, which accepts broader definitions than WKT/PROJ
     *   alone; traditional GIS axis mapping. Parse failure retains QObject with null handle, no
     *   signal. Parent controls QObject lifetime; handle owned exclusively.
     */
    explicit SpatialReferenceSystem(const QString &wktOrProj, QObject *parent = nullptr);

    /*!
     * \brief Copy-constructs from another SpatialReferenceSystem.
     * @param[in] other Borrowed stable source; identity/description copied and OGR handle
     *   independently cloned if present.
     * @param[in] parent Optional new QObject owner, independent of original parent.
     * @note No signals/connections/child objects copied; cloning invalid source yields invalid
     *   independent wrapper. Serialized access required if source OGR handle can be mutated.
     */
    explicit SpatialReferenceSystem(const SpatialReferenceSystem &other, QObject *parent = nullptr);

    /**
     * @brief Delete owned OGRSpatialReference; invalidate all borrowed raw pointers and metadata
     *   references.
     * @note Any transformations/readers borrowing underlying GDAL state must finish before owner
     *   destruction.
     */
    virtual ~SpatialReferenceSystem();

    // ----- Identity --------------------------------------------------------

    /*!
     * \brief Returns the authority name (e.g. "EPSG", "ESRI").
     * @return Stored uppercase authority name after successful parsing, possibly empty for
     *   local/unidentified CRS.
     */
    [[nodiscard]] QString authName() const;

    /*!
     * \brief Returns the numeric authority code, or -1 if not known.
     * @return Stored numeric root authority code, minus one if absent/invalid; nonnumeric authority
     *   text parsed by QString::toInt can become0.
     */
    [[nodiscard]] int code() const;

    /*!
     * \brief Returns a human-readable description/name for this CRS.
     * @return Custom nonempty label, otherwise GDAL CRS name, then toAuthority; empty when
     *   invalid/unidentified.
     */
    [[nodiscard]] QString description() const;

    /*!
     * \brief Sets a custom description for this CRS.
     * @param[in] description Custom display label copied without altering GDAL CRS name/authority;
     *   empty returns description() to fallback.
     * @note Equality no-op; change emits propertyChanged("description") only. Coordinate
     *   transformations/units unaffected. Owning GUI thread for QObject mutation.
     */
    void setDescription(const QString &description);

    // ----- Serialization ---------------------------------------------------

    /*!
     * \brief Exports this CRS using GDAL default WKT serialization.
     * \return GDAL WKT representation, or an empty string on failure.
     * @note Calls GDAL exportToWkt with default options; no explicit WKT2 dialect is selected, so
     *   actual dialect follows GDAL/reference defaults. Copies UTF-8 and frees GDAL allocation,
     *   invalid returns empty; no handle mutation or signals.
     */
    [[nodiscard]] QString toWkt() const;

    /*!
     * \brief Exports this CRS as a legacy PROJ.4 string.
     * \return PROJ.4 string, or an empty string on failure.
     * @note Copies legacy export string and frees GDAL allocation; invalid handle/export with no
     *   buffer yields empty. Export return code not checked separately, so caller treats empty as
     *   failure; lossy legacy representation may omit metadata.
     */
    [[nodiscard]] QString toProj4() const;

    /*!
     * \brief Returns the "AUTHORITY:CODE" string, e.g. "EPSG:4326".
     * @return Stored AUTH:code when authority exists/code>=0, Local for GDAL local CRS, otherwise
     *   empty.
     */
    [[nodiscard]] QString toAuthority() const;

    // ----- Type predicates -------------------------------------------------

    /*!
     * \brief Returns true when the CRS uses geographic (angular) coordinates.
     * @return Whether nonnull GDAL reference reports geographic; invalid false.
     */
    [[nodiscard]] bool isGeographic() const;

    /*!
     * \brief Returns true when the CRS is a projected (Cartesian) CRS.
     * @return Whether nonnull GDAL reference reports projected; invalid false.
     */
    [[nodiscard]] bool isProjected() const;

    /*!
     * \brief Returns true when this CRS equals \p other (within GDAL tolerance).
     * @param[in] other Stable borrowed wrapper compared through GDAL IsSame.
     * @return False if either handle null, including two invalid wrappers; otherwise GDAL semantic
     *   CRS comparison. Custom descriptions/QObject identity are ignored; no coordinate
     *   transformations performed.
     */
    [[nodiscard]] bool equals(const SpatialReferenceSystem &other) const;

    // ----- Units -----------------------------------------------------------

    /*!
     * \brief Returns the name of the linear unit (e.g. "metre", "US survey foot").
     *        Geographic defaults follow GDAL GetLinearUnits.
     * @return GDAL GetLinearUnits name or empty for invalid/absent name; no geographic-type guard,
     *   so geographic behavior follows GDAL defaults.
     */
    [[nodiscard]] QString linearUnitsName() const;

    /*!
     * \brief Returns the conversion factor from the linear unit to metres.
     *        Returns 1.0 for geographic CRSes.
     * @return GDAL GetLinearUnits conversion factor to metres, 1.0for null reference. Does not
     *   independently reject geographic/nonplanar/nonfinite factors; use planarLinearUnit for
     *   meshing.
     */
    [[nodiscard]] double linearUnitsToMetres() const;

    /*!
     * \brief Returns the name of the angular unit (e.g. "degree").
     *        Projected defaults follow GDAL GetAngularUnits.
     * @return GDAL GetAngularUnits name or empty invalid/absent name; projected CRS is not
     *   explicitly suppressed.
     */
    [[nodiscard]] QString angularUnitsName() const;

    /*!
     * \brief Planar linear-unit information used by the 2D mesh writer.
     * \details `metresPerUnit` is the factor that converts this CRS's XY
     *          coordinates to metres (the engine's SI contract).
     *          `usable` is true only when the CRS is projected or local AND
     *          `metresPerUnit` is finite and > 0. Geographic CRSes are
     *          unusable for planar meshing.
     */
    struct LinearUnitInfo {
        double metresPerUnit = 1.0;
        QString name;
        bool usable = false;
    };

    /*!
     * \brief Returns a LinearUnitInfo describing the planar linear unit.
     * \details Use this at every boundary that must convert between
     *          project-CRS XY and engine-SI metres (2D mesh write/read).
     * @return Value with positive finite metres-per-unit and GDAL unit name when obtainable; usable
     *   only projected/local. Geographic/invalid factors return default 1.0, empty name, unusable.
     *   No authority-code inference or coordinate conversion.
     */
    [[nodiscard]] LinearUnitInfo planarLinearUnit() const;

    // ----- GDAL interop ----------------------------------------------------

    /*!
     * \brief Returns a non-owning pointer to the underlying OGRSpatialReference.
     * \warning Caller must not delete the returned pointer.
     * @return Borrowed mutable owned GDAL pointer, nullable; do not delete. Const wrapper does not
     *   prevent raw mutation; such mutation bypasses identity metadata, signals and other caches.
     *   Serialize access and clone for independent worker ownership.
     */
    [[nodiscard]] OGRSpatialReference *ogrSpatialReference() const;

    /*!
     * \brief Creates a new OGRCoordinateTransformation from this CRS to \p target.
     * \details Caller takes ownership of the returned object; destroy it with
     *          OGRCoordinateTransformation::DestroyCT() or delete.
     * \param target  Destination CRS.
     * \returns A new transformation, or nullptr on failure.

     * @note Uses traditional GIS axis strategy installed by constructors; returns null for invalid
     *   endpoint/GDAL inability, including local CRS pairs GDAL cannot transform. Returned handle is
     *   independent caller-owned; destroy with OGRCoordinateTransformation::DestroyCT. Wrapper adds no
     *   automatic fallback/point-failure handling.
     */
    [[nodiscard]] OGRCoordinateTransformation *
    createTransformationTo(const SpatialReferenceSystem &target) const;

    // ----- Factory methods -------------------------------------------------

    /*!
     * \brief Creates a SpatialReferenceSystem from a WKT or PROJ string.
     * \param wktOrProj  WKT or PROJ string.
     * \param parent     Qt parent for the new object.
     * \returns          New SpatialReferenceSystem (caller owns it), or nullptr on failure.
     * @note Allocates using ordinary constructor, then deletes failed wrapper and returns null when
     *   OGR handle missing. Success QObject is owned by supplied parent when nonnull, otherwise
     *   caller. No registration/recent-list/settings mutation.
     */
    [[nodiscard]] static SpatialReferenceSystem *fromWktOrProj(const QString &wktOrProj,
                                                               QObject *parent = nullptr);

    /*!
     * \brief Creates a SpatialReferenceSystem from an authority code.
     * \param authName   e.g. "EPSG".
     * \param code       Numeric EPSG/authority code.
     * \param parent     Qt parent for the new object.
     * \returns          New SpatialReferenceSystem (caller owns it), or nullptr on failure.
     * @note Allocates using ordinary constructor, then deletes failed wrapper and returns null when
     *   OGR handle missing. Success QObject is owned by supplied parent when nonnull, otherwise
     *   caller. No registration/recent-list/settings mutation.
     */
    [[nodiscard]] static SpatialReferenceSystem *fromAuthCode(const QString &authName, int code,
                                                              QObject *parent = nullptr);

    /*!
     * \brief Creates a local / untitled CRS for models with no coordinate system defined.
     *        Coordinates are treated as raw Cartesian values; no reprojection is possible.
     * @param[in] parent Optional QObject owner; without parent caller owns returned allocation.
     * @return New local metre CRS named Untitled, traditional GIS axes, no authority/code. GDAL
     *   local/linear-unit setter return statuses not checked; does not imply geographic
     *   reprojection possible.
     */
    [[nodiscard]] static SpatialReferenceSystem *untitled(QObject *parent = nullptr);

    /*!
     * \brief Creates a local CRS with linear units derived from the SWMM `[MAP]` section
     *        `Units` field (e.g. "FEET" or "METERS"). Distances and the scale bar will
     *        show correct values without requiring a geographic CRS.
     * \param mapUnits  Value of the `[MAP] Units` line (case-insensitive: "FEET"/"METERS"/…).
     * \param parent    Qt parent for the new object.
     * @return New parent-owned or caller-owned local CRS. Exact case-insensitive FEET/FOOT chooses
     *   US survey foot0.3048006096012192 m; EVERY other text (including unknown/DEGREES/whitespace)
     *   falls back metre 1.0. Does not trim, validate geographic intent or infer international foot.
     */
    [[nodiscard]] static SpatialReferenceSystem *localFromMapUnits(const QString &mapUnits,
                                                                   QObject *parent = nullptr);

    // ----- Local-CS predicate ----------------------------------------------

    /*! Returns true when this is a local (non-geographic, non-projected) CRS.
     * @return Whether nonnull GDAL reference reports local; invalid false.
     */
    [[nodiscard]] bool isLocal() const;

signals:
    /*!
     * \brief Emitted when any property changes.
     * \param propertyName  Name of the changed Q_PROPERTY.
     * @note Current mutable wrapper emits only description changes through setDescription;
     *   initialization/raw OGR mutations do not emit per-property refresh.
     */
    void propertyChanged(const QString &propertyName);

private:
    /**
     * @param[in] parent Optional QObject owner.
     * @note Private null-handle construction for local factories; unnamed Qt initialization tag
     *   selects overload only, its value is unused. Initializes identity to empty/minus one.
     */
    SpatialReferenceSystem(QObject *parent, Qt::Initialization); // null-init for factory use
    /**
     * @param[in] authName Authority string combined with numeric code for GDAL input.
     * @param[in] code Numeric authority code.
     * @note Constructor-only helper allocates traditional-GIS-axis reference, parses, and deletes
     *   new handle on failure. Does not release a previous nonnull handle before assignment, so
     *   never use as general reinitializer. Success caches root metadata/name, no signals.
     */
    void initFromAuthCode(const QString &authName, int code);
    /**
     * @param[in] wktOrProj GDAL user-input definition encoded UTF-8.
     * @note Constructor-only helper allocates traditional-GIS-axis reference, parses, and deletes
     *   new handle on failure. Does not release a previous nonnull handle before assignment, so
     *   never use as general reinitializer. Success caches root metadata/name, no signals.
     */
    void initFromWktOrProj(const QString &wktOrProj);

    OGRSpatialReference *m_ogrSRS = nullptr; /*!< Owned GDAL spatial reference. */
    QString m_authName;
    int m_code = -1;
    QString m_description;
};

#endif // SPATIALREFERENCESYSTEM_H
