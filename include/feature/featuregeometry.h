/**
 * @file      featuregeometry.h
 * @brief     Editable vector geometry for user-authored feature layers
 *            (workplans/MESH_DIALOG_TABS_AND_FEATURE_LAYERS_PLAN_2026-09-07.md §3.1).
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 *
 * Editable vector geometry for user-authored feature layers
 * (workplans/MESH_DIALOG_TABS_AND_FEATURE_LAYERS_PLAN_2026-09-07.md §3.1).
 *
 * The GUI has three incompatible geometry vocabularies today: QPolygonF (mesh
 * and SWMM sides, no hole concept), OGRGeometry (inside GISVectorLayer only,
 * never surfaced), and QPainterPath (render-only). FeatureGeometry is the one
 * value type the editing stack passes around; \ref openswmmvis::feature::FeatureGeometry::fromOGR
 * and \ref openswmmvis::feature::FeatureGeometry::toOGR are the ONLY places OGR geometry is
 * converted, so no other GUI code needs to include ogr_geometry.h.
 *
 * Rings carry an optional parallel Z array. A ring is 2D when \c z is empty
 * and 3D when \c z.size() == \c pts.size(); individual Z values may be NaN
 * (sampled outside raster coverage — see ZPolicy). Callers must treat NaN as
 * "unknown", never as zero: the mesh pipeline already distinguishes them via
 * SteinerPoint::hasZ.
 *
 * Ring validity (simple, orientation, containment) reuses the predicates
 * already written for quad regions in mesh/meshquadregion.h rather than
 * duplicating them; see \ref openswmmvis::feature::FeatureGeometry::validate.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef FEATUREGEOMETRY_H
#define FEATUREGEOMETRY_H

#include <QPointF>
#include <QPolygonF>
#include <QRectF>
#include <QString>
#include <QVector>

class OGRGeometry;
class OGRSpatialReference;

namespace openswmmvis::feature
{

/*!
 * \enum GeometryType
 * \brief Geometry kinds an editable feature layer can hold.
 *
 * A layer declares one type at creation; it decides the OGR geometry type of
 * the GeoPackage table and therefore cannot change afterwards. The Multi*
 * variants accept single-part geometries too (a one-part multi-polygon), so
 * "Add part" can promote a drawing without a table migration.
 */
enum class GeometryType {
    None = 0,
    Point,
    LineString,
    Polygon,
    MultiPoint,
    MultiLineString,
    MultiPolygon
};

/*! \brief Human label for \p t, for combo boxes and metadata rows.
 * @param[in] t Geometry kind to describe.
 * @return Translated user-facing kind label; unknown values use the None fallback.
 */
[[nodiscard]] QString geometryTypeLabel(GeometryType t);

/*! \brief Stable token for \p t, for .oswp persistence. Round-trips through
 *         \ref geometryTypeFromToken.
 * @param[in] t Geometry kind to persist.
 * @return Stable lowercase token; unknown values use the none token.
 */
[[nodiscard]] QString geometryTypeToken(GeometryType t);

/*! \brief Inverse of \ref geometryTypeToken; GeometryType::None when unknown.
 * @param[in] token Kind token trimmed and matched case-insensitively.
 * @return Recognized geometry kind, or None when unknown.
 */
[[nodiscard]] GeometryType geometryTypeFromToken(const QString &token);

/*! \brief True when \p t is one of the Multi* kinds.
 * @param[in] t Geometry kind to classify.
 * @return True only for MultiPoint, MultiLineString or MultiPolygon.
 */
[[nodiscard]] bool isMultiType(GeometryType t);

/*! \brief The Multi* kind corresponding to \p t (identity for Multi* inputs).
 * @param[in] t Single/multipart geometry kind.
 * @return Corresponding multipart kind; multipart and None/unrecognized input pass through.
 */
[[nodiscard]] GeometryType toMultiType(GeometryType t);

/*!
 * \struct Ring
 * \brief One ordered run of vertices, with optional per-vertex Z.
 *
 * Used for a polygon's exterior or interior ring, for a line part, and — with
 * a single vertex — for a point part. Rings are stored OPEN: the closing
 * vertex is implicit for polygons and is added only when converting to OGR.
 * Storing them open keeps vertex editing unambiguous (there is exactly one
 * handle per corner) and matches mesh::QuadRegion::ring, whose comment notes
 * "last == first optional".
 */
struct Ring {
    QVector<QPointF> pts;
    /*! Per-vertex Z. Empty ⇒ this ring is 2D. Otherwise size() == pts.size();
     *  individual entries may be NaN meaning "not sampled". */
    QVector<double> z;

    /**
     * @return True when XY storage is empty; does not inspect Z or polygon validity.
     */
    [[nodiscard]] bool isEmpty() const { return pts.isEmpty(); }
    /**
     * @return Number of stored XY vertices.
     */
    [[nodiscard]] int size() const { return pts.size(); }

    /*! True when a well-formed Z array is present (size matches, non-empty).
     *  A ring whose Z array is the wrong length is treated as 2D rather than
     *  read out of bounds.
     * @return True for nonempty XY and an equally sized Z array, even when all Z values are NaN.
 */
    [[nodiscard]] bool hasZ() const { return !pts.isEmpty() && z.size() == pts.size(); }

    /*! Z at \p i, or NaN when this ring is 2D or \p i is out of range.
     * @param[in] i Zero-based vertex index.
     * @return Stored elevation in the ring's vertical units, or NaN for absent/misaligned Z or invalid index.
 */
    [[nodiscard]] double zAt(int i) const;

    /*! Resize \c z to match \c pts, filling new entries with NaN. No-op when
     *  the ring is 2D (empty \c z stays empty).
     * @details Retains existing Z entries, appends NaN or truncates trailing values. Does not synthesize Z for a 2D ring.
 */
    void syncZLength();

    /*! Drop the Z array, making this ring 2D.
     * @brief Drop elevation storage while retaining every XY vertex.
 */
    void clearZ() { z.clear(); }

    /*! Give this ring an all-NaN Z array if it has none.
     * @details Replaces absent or misaligned Z with an all-NaN array of current XY length; already well-formed Z is preserved.
 */
    void ensureZ();

    /**
     * @brief Copy XY vertices into Qt polygon storage without Z or adding a closing point.
     * @return Owning QPolygonF with original vertex order.
     */
    [[nodiscard]] QPolygonF toPolygon() const;
    /**
     * @brief Compute the axis-aligned XY bounds without considering Z.
     * @return Bounds of stored ring vertices.
     */
    [[nodiscard]] QRectF boundingRect() const;

    /*! Signed area via the shoelace formula; positive = counter-clockwise.
     *  Zero for fewer than three vertices.
     * @return Signed planar area in squared XY coordinate units, positive for CCW; zero for fewer than three vertices. Z is ignored.
 */
    [[nodiscard]] double signedArea() const;

    /*! Total length of the open polyline through \c pts (2D, ignores Z).
     * @return Sum of distances between consecutive XY vertices; implicit polygon closing edge is excluded and fewer than two vertices yield zero.
 */
    [[nodiscard]] double length2D() const;

    /*! True when the closed ring is simple (no self-intersection) and has at
     *  least three distinct vertices. Delegates to mesh::ringIsSimple.
     * @return mesh::ringIsSimple() result for the closed XY ring; Z is ignored.
 */
    [[nodiscard]] bool isSimpleRing() const;

    /*! Reverse the vertex order (and Z alongside it) if the ring is clockwise,
     *  so it ends counter-clockwise. Delegates to the same convention as
     *  mesh::normalizeRingCCW.
     * @details May reverse XY and a well-formed Z array together; never samples or changes elevation values.
 */
    void normalizeCCW();

    /*! True when \p p lies inside the closed ring (even-odd rule).
     * @param[in] p XY query coordinate in the same frame as the ring/part.
     * @return Even-odd closed-ring membership; Z is ignored.
 */
    [[nodiscard]] bool containsPoint(const QPointF &p) const;

    /*! Insert \p p (with Z \p zVal) at index \p index, clamped to [0, size()].
     * @param[in] index Position clamped to [0,size()].
     * @param[in] p New XY in the same coordinate frame as existing vertices.
     * @param[in] zVal Elevation copied only when a nonempty Z array already exists; NaN denotes unsampled.
     * @details Inserting into a 2D ring leaves it 2D; this does not repair an already malformed Z array.
 */
    void insertVertex(int index, const QPointF &p, double zVal);

    /*! Remove the vertex at \p index if in range; keeps \c z aligned.
     * @param[in] index Zero-based XY vertex to remove; invalid indexes are ignored.
     * @details Removes a corresponding Z entry when present. Does not enforce minimum geometry vertex counts.
 */
    void removeVertex(int index);

    /*! Move the vertex at \p index to \p p, leaving its Z untouched.
     * @param[in] index Zero-based vertex index; invalid indexes are ignored.
     * @param[in] p Replacement XY in the ring coordinate frame.
     * @details Stored Z is intentionally unchanged, even when XY moves away from its sampling location.
 */
    void moveVertex(int index, const QPointF &p);

    /*!
     * \brief Insert intermediate vertices so no segment is longer than
     *        \p spacing map units.
     * \details Used before Z sampling so a breakline drawn with three clicks
     *          follows the terrain instead of chording it (plan §4.3). New
     *          vertices get NaN Z when the ring is 3D — the caller samples
     *          afterwards. A \p spacing <= 0 is a no-op.
     * \return  The number of vertices inserted.
     * @param[in] spacing Finite positive maximum segment length in XY coordinate units; nonpositive values do no work.
     * @details Operates on consecutive stored vertices only; an implicit polygon closing edge is not densified. New 3D vertices carry NaN for later sampling.
     */
    int densify(double spacing);

    /**
     * @brief Compare stored geometry value identity.
     * @param[in] o Geometry value to compare without mutation.
     * @return Equal XY/part order and matching Z storage; paired NaNs compare equal and finite Z uses qFuzzyCompare.
     */
    [[nodiscard]] bool operator==(const Ring &o) const;
    /**
     * @brief Negate stored geometry value equality.
     * @param[in] o Geometry value to compare without mutation.
     * @return True exactly when operator== returns false; no geometry is modified.
     */
    [[nodiscard]] bool operator!=(const Ring &o) const { return !(*this == o); }
};

/*!
 * \struct Part
 * \brief One part of a geometry: a polygon (exterior + holes), a line run, or
 *        a single point.
 *
 * For non-polygon types \c holes is always empty and \c exterior carries the
 * vertices (one for a point).
 */
struct Part {
    Ring exterior;
    QVector<Ring> holes; ///< Interior rings; polygon parts only.

    /**
     * @return Whether this part's exterior has no vertices; holes are ignored.
     */
    [[nodiscard]] bool isEmpty() const { return exterior.isEmpty(); }
    /**
     * @brief Compute the axis-aligned XY bounds without considering Z.
     * @return Exterior bounds; holes are assumed contained.
     */
    [[nodiscard]] QRectF boundingRect() const;

    /*! Ring area minus the area of every hole; absolute value, so orientation
     *  does not matter to the caller. Zero for non-polygon parts.
     * @return max(0, abs(exterior area) - sum(abs(hole areas))) in squared XY units; zero for fewer than three exterior points, independent of geometry kind.
 */
    [[nodiscard]] double netArea() const;

    /*! True when \p p is inside the exterior and outside every hole — the
     *  same predicate mesh::pointInRegion applies to quad regions.
     * @param[in] p XY query coordinate in the same frame as the ring/part.
     * @return True inside the exterior and outside each hole; boundary behavior follows the underlying ring predicate.
 */
    [[nodiscard]] bool containsPoint(const QPointF &p) const;

    /*! Apply \ref Ring::normalizeCCW to the exterior and reverse every hole so
     *  holes run clockwise — the OGR/GeoPackage convention.
     * @brief Orient all part exteriors CCW and holes CW while keeping aligned Z order.
     * @details Reverses exterior/hole vertex sequences as needed; well-formed Z arrays reverse alongside their XY values.
 */
    void normalizeOrientation();

    /**
     * @brief Compare stored geometry value identity.
     * @param[in] o Geometry value to compare without mutation.
     * @return Comparison of ordered constituent values.
     */
    [[nodiscard]] bool operator==(const Part &o) const;
    /**
     * @brief Negate stored geometry value equality.
     * @param[in] o Geometry value to compare without mutation.
     * @return True exactly when operator== returns false; no geometry is modified.
     */
    [[nodiscard]] bool operator!=(const Part &o) const { return !(*this == o); }
};

/*!
 * \class FeatureGeometry
 * \brief A complete feature geometry: a type plus zero or more parts.
 *
 * Single-part types (Point, LineString, Polygon) hold exactly one entry in
 * \c parts; the accessors tolerate more and use the first. Multi* types hold
 * any number.
 */
class FeatureGeometry
{
public:
    /**
     * @brief Construct an empty untyped geometry (GeometryType::None).
     */
    FeatureGeometry() = default;
    /**
     * @brief Construct an empty geometry with a declared kind.
     * @param[in] type Stored kind; parts are added separately and no validity check occurs.
     */
    explicit FeatureGeometry(GeometryType type) : m_type(type) {}

    /**
     * @return Stored kind without structural validation.
     */
    [[nodiscard]] GeometryType type() const { return m_type; }
    /**
     * @brief Change the kind tag without converting geometry parts.
     * @param[in] t New kind; callers must validate its compatibility with existing vertices/holes.
     */
    void setType(GeometryType t) { m_type = t; }

    /**
     * @return True when no parts exist or every exterior is empty; holes alone do not make a geometry nonempty.
     */
    [[nodiscard]] bool isEmpty() const;
    /**
     * @return True for a non-None kind and nonempty exterior data; this lightweight check does not replace validate() for topology/part-count checks.
     */
    [[nodiscard]] bool isValid() const { return m_type != GeometryType::None && !isEmpty(); }

    /**
     * @return Borrowed read-only part storage, invalidated by container mutations or geometry destruction.
     */
    [[nodiscard]] const QVector<Part> &parts() const { return m_parts; }
    /**
     * @return Mutable borrowed part storage; caller preserves ring/Z/type invariants and triggers any owning-layer notifications separately.
     */
    [[nodiscard]] QVector<Part> &parts() { return m_parts; }
    /**
     * @return Number of stored parts, including empty parts.
     */
    [[nodiscard]] int partCount() const { return m_parts.size(); }

    /**
     * @brief Append a copied part without validating its compatibility with the kind.
     * @param[in] p Exterior/holes/Z value copied into owned storage.
     */
    void addPart(const Part &p) { m_parts.append(p); }
    /**
     * @brief Remove every part while retaining the geometry kind tag.
     */
    void clear() { m_parts.clear(); }

    /*! Total vertex count across every ring of every part.
     * @return Sum of XY vertices in every exterior and hole, including invalid/empty geometry storage.
 */
    [[nodiscard]] int vertexCount() const;

    /*! True when EVERY non-empty ring carries a well-formed Z array.
     * @return True only if at least one nonempty ring exists and every nonempty exterior/hole has aligned Z; empty geometry returns false.
 */
    [[nodiscard]] bool hasZ() const;

    /*! Count of vertices whose Z is NaN, across all 3D rings. Reported in the
     *  Features dock so an un-sampled breakline is visible rather than silent.
     * @return Number of NaN entries in well-formed 3D rings; 2D/misaligned Z arrays contribute zero.
 */
    [[nodiscard]] int unsampledZCount() const;

    /*! Min / max of non-NaN Z values. Returns false when there are none.
     * @param[out] minZ Lowest non-NaN elevation, written only when at least one eligible value exists.
     * @param[out] maxZ Highest non-NaN elevation under the same rule.
     * @return True for any non-NaN value in a well-formed 3D ring; infinities are retained rather than filtered. False leaves outputs unchanged.
 */
    [[nodiscard]] bool zRange(double &minZ, double &maxZ) const;

    /**
     * @brief Compute the axis-aligned XY bounds without considering Z.
     * @return Union of all part exterior bounds; empty/default for no nonempty parts.
     */
    [[nodiscard]] QRectF boundingRect() const;

    /*! Sum of \ref Part::netArea over every part.
     * @return Sum of nonnegative exterior-minus-hole areas in squared XY coordinate units; this does not branch on the stored geometry kind.
 */
    [[nodiscard]] double area() const;

    /*! Sum of exterior-ring 2D lengths — meaningful for line types.
     * @return Sum of open exterior polyline lengths in XY coordinate units; holes, implicit closing edges and Z are excluded.
 */
    [[nodiscard]] double length() const;

    /*! Give (or drop) Z arrays on every ring to match \p on.
     * @param[in] on True ensures aligned Z arrays (new values NaN), false clears every exterior/hole Z array.
 */
    void setHasZ(bool on);

    /*! \ref Ring::densify on every ring of every part; returns the number of
     *  vertices inserted in total.
     * @param[in] spacing Finite positive maximum consecutive segment length in XY units, forwarded to Ring::densify().
     * @return Total inserted vertices across all exteriors and holes.
 */
    int densify(double spacing);

    /**
     * @brief Normalize every part exterior counterclockwise and every hole clockwise, preserving aligned Z order.
     */
    void normalizeOrientation();

    /*!
     * \brief Structural check against \p expected.
     * \param expected  The layer's declared type.
     * \param reason    Set to a user-facing message when the result is false.
     * \details Rejects: wrong part count for a single-part type, empty rings,
     *          polygons with fewer than three vertices, self-intersecting
     *          rings, holes not contained by their exterior, and holes that
     *          overlap one another. These are the same rules
     *          QUAD_MESHING_REDESIGN_PLAN §5 states for a drawn quad region,
     *          so a polygon that passes here is admissible as a region ring.
     * @return True after expected-kind structural/topological checks, clearing reason; false stores the first diagnostic when reason is non-null.
     * @details Validates structure according to expected, rather than enforcing equality with the stored type tag. Uses XY geometry only and does not check all coordinates/Z for finiteness.
     */
    [[nodiscard]] bool validate(GeometryType expected, QString *reason = nullptr) const;

    // ----- OGR bridge -----------------------------------------------------

    /*!
     * \brief Build a FeatureGeometry from \p g.
     * \details Reads 2.5D coordinates when the source geometry carries them.
     *          Unsupported geometry classes (curves, geometry collections)
     *          yield a GeometryType::None result rather than throwing.
     * @param[in] g Borrowed OGR geometry, possibly nullptr; source ownership is unchanged.
     * @return Owning value copy with open polygon rings and aligned Z where present, or None for null/unsupported geometry.
     */
    [[nodiscard]] static FeatureGeometry fromOGR(const OGRGeometry *g);

    /*!
     * \brief Build a new OGR geometry. The CALLER OWNS the result and must
     *        release it with OGRGeometryFactory::destroyGeometry.
     * \param srs  Optional; assigned to the geometry when non-null.
     * \returns    nullptr when this geometry is empty or of type None.
     * @details Uses stored kind/parts and adds explicit polygon closure in OGR. Does not run validate() first; callers own validity checks. Assigning a non-null SRS uses OGR reference-count semantics.
     */
    [[nodiscard]] OGRGeometry *toOGR(OGRSpatialReference *srs = nullptr) const;

    /*! The OGR well-known-binary type constant for \p t, with the 25D flag set
     *  when \p withZ. Used when creating the GeoPackage table.
     * @param[in] t GUI geometry kind.
     * @param[in] withZ Whether to set the OGR Z flag.
     * @return OGR geometry type constant, with unsupported/None mapped to wkbUnknown.
 */
    [[nodiscard]] static int ogrTypeFor(GeometryType t, bool withZ);

    /**
     * @brief Compare stored geometry value identity.
     * @param[in] o Geometry value to compare without mutation.
     * @return Comparison of ordered constituent values and type tag.
     */
    [[nodiscard]] bool operator==(const FeatureGeometry &o) const;
    /**
     * @brief Negate stored geometry value equality.
     * @param[in] o Geometry value to compare without mutation.
     * @return True exactly when operator== returns false; no geometry is modified.
     */
    [[nodiscard]] bool operator!=(const FeatureGeometry &o) const { return !(*this == o); }

private:
    GeometryType m_type = GeometryType::None;
    QVector<Part> m_parts;
};

} // namespace openswmmvis::feature

#endif // FEATUREGEOMETRY_H
