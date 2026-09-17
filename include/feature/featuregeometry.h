/*!
 * \file   featuregeometry.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * Editable vector geometry for user-authored feature layers
 * (workplans/MESH_DIALOG_TABS_AND_FEATURE_LAYERS_PLAN_2026-09-07.md §3.1).
 *
 * The GUI has three incompatible geometry vocabularies today: QPolygonF (mesh
 * and SWMM sides, no hole concept), OGRGeometry (inside GISVectorLayer only,
 * never surfaced), and QPainterPath (render-only). FeatureGeometry is the one
 * value type the editing stack passes around; \ref FeatureGeometry::fromOGR
 * and \ref FeatureGeometry::toOGR are the ONLY places OGR geometry is
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
 * duplicating them; see \ref validate.
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

namespace openswmmvis::feature {

/*!
 * \enum GeometryType
 * \brief Geometry kinds an editable feature layer can hold.
 *
 * A layer declares one type at creation; it decides the OGR geometry type of
 * the GeoPackage table and therefore cannot change afterwards. The Multi*
 * variants accept single-part geometries too (a one-part multi-polygon), so
 * "Add part" can promote a drawing without a table migration.
 */
enum class GeometryType
{
    None = 0,
    Point,
    LineString,
    Polygon,
    MultiPoint,
    MultiLineString,
    MultiPolygon
};

/*! \brief Human label for \p t, for combo boxes and metadata rows. */
[[nodiscard]] QString geometryTypeLabel(GeometryType t);

/*! \brief Stable token for \p t, for .oswp persistence. Round-trips through
 *         \ref geometryTypeFromToken. */
[[nodiscard]] QString geometryTypeToken(GeometryType t);

/*! \brief Inverse of \ref geometryTypeToken; GeometryType::None when unknown. */
[[nodiscard]] GeometryType geometryTypeFromToken(const QString &token);

/*! \brief True when \p t is one of the Multi* kinds. */
[[nodiscard]] bool isMultiType(GeometryType t);

/*! \brief The Multi* kind corresponding to \p t (identity for Multi* inputs). */
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
struct Ring
{
    QVector<QPointF> pts;
    /*! Per-vertex Z. Empty ⇒ this ring is 2D. Otherwise size() == pts.size();
     *  individual entries may be NaN meaning "not sampled". */
    QVector<double>  z;

    [[nodiscard]] bool isEmpty()  const { return pts.isEmpty(); }
    [[nodiscard]] int  size()     const { return pts.size(); }

    /*! True when a well-formed Z array is present (size matches, non-empty).
     *  A ring whose Z array is the wrong length is treated as 2D rather than
     *  read out of bounds. */
    [[nodiscard]] bool hasZ() const
    { return !pts.isEmpty() && z.size() == pts.size(); }

    /*! Z at \p i, or NaN when this ring is 2D or \p i is out of range. */
    [[nodiscard]] double zAt(int i) const;

    /*! Resize \c z to match \c pts, filling new entries with NaN. No-op when
     *  the ring is 2D (empty \c z stays empty). */
    void syncZLength();

    /*! Drop the Z array, making this ring 2D. */
    void clearZ() { z.clear(); }

    /*! Give this ring an all-NaN Z array if it has none. */
    void ensureZ();

    [[nodiscard]] QPolygonF toPolygon() const;
    [[nodiscard]] QRectF    boundingRect() const;

    /*! Signed area via the shoelace formula; positive = counter-clockwise.
     *  Zero for fewer than three vertices. */
    [[nodiscard]] double signedArea() const;

    /*! Total length of the open polyline through \c pts (2D, ignores Z). */
    [[nodiscard]] double length2D() const;

    /*! True when the closed ring is simple (no self-intersection) and has at
     *  least three distinct vertices. Delegates to mesh::ringIsSimple. */
    [[nodiscard]] bool isSimpleRing() const;

    /*! Reverse the vertex order (and Z alongside it) if the ring is clockwise,
     *  so it ends counter-clockwise. Delegates to the same convention as
     *  mesh::normalizeRingCCW. */
    void normalizeCCW();

    /*! True when \p p lies inside the closed ring (even-odd rule). */
    [[nodiscard]] bool containsPoint(const QPointF &p) const;

    /*! Insert \p p (with Z \p zVal) at index \p index, clamped to [0, size()]. */
    void insertVertex(int index, const QPointF &p, double zVal);

    /*! Remove the vertex at \p index if in range; keeps \c z aligned. */
    void removeVertex(int index);

    /*! Move the vertex at \p index to \p p, leaving its Z untouched. */
    void moveVertex(int index, const QPointF &p);

    /*!
     * \brief Insert intermediate vertices so no segment is longer than
     *        \p spacing map units.
     * \details Used before Z sampling so a breakline drawn with three clicks
     *          follows the terrain instead of chording it (plan §4.3). New
     *          vertices get NaN Z when the ring is 3D — the caller samples
     *          afterwards. A \p spacing <= 0 is a no-op.
     * \return  The number of vertices inserted.
     */
    int densify(double spacing);

    [[nodiscard]] bool operator==(const Ring &o) const;
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
struct Part
{
    Ring          exterior;
    QVector<Ring> holes;     ///< Interior rings; polygon parts only.

    [[nodiscard]] bool isEmpty() const { return exterior.isEmpty(); }
    [[nodiscard]] QRectF boundingRect() const;

    /*! Ring area minus the area of every hole; absolute value, so orientation
     *  does not matter to the caller. Zero for non-polygon parts. */
    [[nodiscard]] double netArea() const;

    /*! True when \p p is inside the exterior and outside every hole — the
     *  same predicate mesh::pointInRegion applies to quad regions. */
    [[nodiscard]] bool containsPoint(const QPointF &p) const;

    /*! Apply \ref Ring::normalizeCCW to the exterior and reverse every hole so
     *  holes run clockwise — the OGR/GeoPackage convention. */
    void normalizeOrientation();

    [[nodiscard]] bool operator==(const Part &o) const;
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
    FeatureGeometry() = default;
    explicit FeatureGeometry(GeometryType type) : m_type(type) {}

    [[nodiscard]] GeometryType type() const { return m_type; }
    void setType(GeometryType t) { m_type = t; }

    [[nodiscard]] bool isEmpty() const;
    [[nodiscard]] bool isValid() const { return m_type != GeometryType::None && !isEmpty(); }

    [[nodiscard]] const QVector<Part> &parts() const { return m_parts; }
    [[nodiscard]] QVector<Part> &parts() { return m_parts; }
    [[nodiscard]] int partCount() const { return m_parts.size(); }

    void addPart(const Part &p) { m_parts.append(p); }
    void clear() { m_parts.clear(); }

    /*! Total vertex count across every ring of every part. */
    [[nodiscard]] int vertexCount() const;

    /*! True when EVERY non-empty ring carries a well-formed Z array. */
    [[nodiscard]] bool hasZ() const;

    /*! Count of vertices whose Z is NaN, across all 3D rings. Reported in the
     *  Features dock so an un-sampled breakline is visible rather than silent. */
    [[nodiscard]] int unsampledZCount() const;

    /*! Min / max of the finite Z values. Returns false when there are none. */
    [[nodiscard]] bool zRange(double &minZ, double &maxZ) const;

    [[nodiscard]] QRectF boundingRect() const;

    /*! Sum of \ref Part::netArea over every part. */
    [[nodiscard]] double area() const;

    /*! Sum of exterior-ring 2D lengths — meaningful for line types. */
    [[nodiscard]] double length() const;

    /*! Give (or drop) Z arrays on every ring to match \p on. */
    void setHasZ(bool on);

    /*! \ref Ring::densify on every ring of every part; returns the number of
     *  vertices inserted in total. */
    int densify(double spacing);

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
     */
    [[nodiscard]] bool validate(GeometryType expected, QString *reason = nullptr) const;

    // ----- OGR bridge -----------------------------------------------------

    /*!
     * \brief Build a FeatureGeometry from \p g.
     * \details Reads 2.5D coordinates when the source geometry carries them.
     *          Unsupported geometry classes (curves, geometry collections)
     *          yield a GeometryType::None result rather than throwing.
     */
    [[nodiscard]] static FeatureGeometry fromOGR(const OGRGeometry *g);

    /*!
     * \brief Build a new OGR geometry. The CALLER OWNS the result and must
     *        release it with OGRGeometryFactory::destroyGeometry.
     * \param srs  Optional; assigned to the geometry when non-null.
     * \returns    nullptr when this geometry is empty or of type None.
     */
    [[nodiscard]] OGRGeometry *toOGR(OGRSpatialReference *srs = nullptr) const;

    /*! The OGR well-known-binary type constant for \p t, with the 25D flag set
     *  when \p withZ. Used when creating the GeoPackage table. */
    [[nodiscard]] static int ogrTypeFor(GeometryType t, bool withZ);

    [[nodiscard]] bool operator==(const FeatureGeometry &o) const;
    [[nodiscard]] bool operator!=(const FeatureGeometry &o) const { return !(*this == o); }

private:
    GeometryType  m_type = GeometryType::None;
    QVector<Part> m_parts;
};

}   // namespace openswmmvis::feature

#endif // FEATUREGEOMETRY_H
