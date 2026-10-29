/**
 * @file      meshgenerator.cpp
 * @brief     2D mesh generator (workplans/MESH_TRIANGLE_ENGINE_PLAN_2026-09-30.md): a constrained
 *            Delaunay triangulation of every constraint, refined to the caller's size function and
 *            a requested minimum angle (mesh/meshcdt.h), with aligned quad strips only where
 *            features ask for them — four-sided quad regions, corridors, streets between facing
 *            break lines, conduits.
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 *
 * Mesh generator (workplans/MESH_TRIANGLE_ENGINE_PLAN_2026-09-30.md):
 *
 *  1. size function h(x) from the caller's RefineHook (or the uniform
 *     maxArea), with quad-region spacings as overrides;
 *  2. constraint polylines: domain rings, holes, breaklines, region rings,
 *     patch boundaries; Steiner points are fixed vertices; lines that cross
 *     or come closer than the refinement floor are joined at shared
 *     vertices;
 *  3. quad strips: the caller's patches (corridors), four-sided quad
 *     regions, conduit strips and strips between facing terrain break lines
 *     — each a structured patch whose boundary is fixed in the CDT; a strip
 *     that does not fit is dropped and its feature stays triangles;
 *  4. terrain break lines cut back to keep clear of all of those;
 *  5. every other constraint resampled at h(x), triangulated with the
 *     constrained-Delaunay kernel (mesh/meshcdt.h), exterior, holes and
 *     patch interiors removed, then Delaunay refinement to h(x) and the
 *     minimum angle;
 *  6. assembly: markers and tags, boundary edges, structured patches
 *     stitched by coordinate, region tags by flood fill bounded by
 *     constraints.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#include "mesh/meshgenerator.h"

#include "mesh/breaklinestrips.h"
#include "mesh/meshcdt.h"
#include "mesh/meshcellgeom.h"
#include "mesh/pslgprep.h"

#include "core/editgeometry.h"

#include <QDebug>
#include <QHash>
#include <QPainterPath>
#include <QRectF>
#include <QSet>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <functional>
#include <limits>
#include <memory>

namespace mesh
{

namespace
{
// Keep legacy console diagnostics, while giving each generator's caller the
// same messages without intercepting process-global Qt logging.
class GeneratorDiagnostic
{
public:
    explicit GeneratorDiagnostic(const std::function<void(const QString &)> &callback)
        : callback_(callback), stream_(std::make_unique<QDebug>(&message_))
    {
    }
    ~GeneratorDiagnostic() noexcept
    {
        stream_.reset();
        try {
            qInfo().noquote() << message_;
            if (callback_)
                callback_(message_);
        } catch (...) {
        }
    }
    GeneratorDiagnostic &noquote()
    {
        stream_->noquote();
        return *this;
    }
    template <class T> GeneratorDiagnostic &operator<<(const T &value)
    {
        *stream_ << value;
        return *this;
    }

private:
    const std::function<void(const QString &)> &callback_;
    QString message_;
    std::unique_ptr<QDebug> stream_;
};
}

// ── Structured patch placement ─────────────────────────────────────────────
// Patches (corridors) are topology, not hints: an overlapping, out-of-domain
// or constraint-crossing patch cannot be stitched, so it is rejected up
// front. Coordinates are taken relative to the domain so projected CRS
// offsets cannot erase small overlaps in the clipping arithmetic.
namespace
{

/**
 * @brief Copy an implicit closed ring into a winding-fill painter path.
 * @param[in] ring Borrowed finite XY polygon, unchanged; empty returns empty path.
 * @return Owned path with explicit closure. No simple-polygon validation, coordinate transform,
 * source retention or file/engine effects; may allocate path storage.
 */
QPainterPath patchRingPath(const QPolygonF &ring)
{
    QPainterPath path;
    path.setFillRule(Qt::WindingFill);
    if (!ring.isEmpty()) {
        path.addPolygon(ring);
        path.closeSubpath();
    }
    return path;
}

/**
 * @brief Sum absolute local-origin shoelace areas of a path's fill polygons.
 * @param[in] path Borrowed finite painter geometry, unchanged.
 * @return Nonnegative area in squared path units, zero for no >=3-point fill polygon. Uses Qt's
 * fill polygon decomposition, not adaptive exact area arithmetic; may allocate decomposition
 * arrays. No source mutation, coordinate conversion or IO.
 */
double patchPathArea(const QPainterPath &path)
{
    double area = 0;
    for (const QPolygonF &polygon : path.toFillPolygons()) {
        if (polygon.size() < 3)
            continue;
        const QPointF origin = polygon.first();
        double twice = 0;
        for (int i = 0; i < polygon.size(); ++i) {
            const QPointF a = polygon[i] - origin;
            const QPointF b = polygon[(i + 1) % polygon.size()] - origin;
            twice += a.x() * b.y() - a.y() * b.x();
        }
        area += std::abs(twice) / 2;
    }
    return area;
}

/**
 * @brief Copy ring vertices after subtracting a common arithmetic origin.
 * @param[in] ring Borrowed finite polygon, unchanged.
 * @param[in] origin Borrowed finite reference in same XY units, unchanged.
 * @return Owned translated polygon, preserving count/order/closure. No CRS transform, topology
 * check or source-pointer retention; allocates output.
 */
QPolygonF patchRelativeRing(const QPolygonF &ring, const QPointF &origin)
{
    QPolygonF relative;
    relative.reserve(ring.size());
    for (const QPointF &point : ring)
        relative.append(point - origin);
    return relative;
}

/**
 * @brief Compute the planar determinant of two vectors.
 * @param[in] a Borrowed finite vector, unchanged.
 * @param[in] b Borrowed compatible vector, unchanged.
 * @return Signed squared-unit cross product, no overflow/finite audit, allocation or mutation.
 */
double patchCross(const QPointF &a, const QPointF &b)
{
    return a.x() * b.y() - a.y() * b.x();
}

/**
 * @brief Test tolerant perpendicular distance and endpoint projection bounds.
 * @param[in] point Borrowed finite query in common XY frame, unchanged.
 * @param[in] a Borrowed finite segment start, unchanged.
 * @param[in] b Borrowed finite segment end, unchanged.
 * @param[in] tolerance Intended nonnegative finite XY distance, unchecked; bounds inclusive.
 * @return True within line-distance tolerance and projected endpoint extension by tolerance;
 * degenerate segment uses radial endpoint distance. No robust finite audit, allocation or
 * mutation.
 */
bool patchPointOnSegment(const QPointF &point, const QPointF &a, const QPointF &b, double tolerance)
{
    const QPointF delta = b - a;
    const double length = std::hypot(delta.x(), delta.y());
    if (!(length > 0))
        return std::hypot(point.x() - a.x(), point.y() - a.y()) <= tolerance;
    const double projection = QPointF::dotProduct(point - a, delta) / length;
    return projection >= -tolerance && projection <= length + tolerance &&
           std::abs(patchCross(delta, point - a)) <= tolerance * length;
}

/**
 * @brief Test proximity to any implicit-closure polygon boundary segment.
 * @param[in] point Borrowed finite XY query, unchanged.
 * @param[in] ring Borrowed finite polygon in same frame, unchanged; empty misses.
 * @param[in] tolerance Nonnegative finite XY distance, unchecked, inclusive boundary test.
 * @return True for any tolerant segment hit; not an interior/simplicity test. No
 * allocation/mutation.
 */
bool patchPointOnRing(const QPointF &point, const QPolygonF &ring, double tolerance)
{
    for (int i = 0; i < ring.size(); ++i)
        if (patchPointOnSegment(point, ring[i], ring[(i + 1) % ring.size()], tolerance))
            return true;
    return false;
}

// Endpoints alone miss a segment traversing a concave patch or crossing from
// boundary to boundary: split at every intersection and test each interval.
/**
 * @brief Split a segment at ring contacts and test non-boundary interior portions.
 * @param[in] a Borrowed finite segment start, unchanged.
 * @param[in] b Borrowed finite end in same XY frame, unchanged.
 * @param[in] ring Borrowed compatible implicit-closure boundary, unchanged.
 * @param[in] path Borrowed winding-fill geometry for exactly that ring/frame, unchanged.
 * @param[in] tolerance Nonnegative finite XY distance treating near-ring samples as boundary.
 * @return True when an interval midpoint is strictly path-inside and not tolerance-close to
 * boundary; degenerate segment tests its point. Ordinary double intersection cuts and collinear
 * projections, not adaptive robust geometry. Allocates cuts; no source mutation/retained
 * references or IO.
 */
bool patchSegmentEntersInterior(const QPointF &a, const QPointF &b, const QPolygonF &ring,
                                const QPainterPath &path, double tolerance)
{
    const QPointF delta = b - a;
    const double length2 = QPointF::dotProduct(delta, delta);
    if (!(length2 > 0))
        return path.contains(a) && !patchPointOnRing(a, ring, tolerance);
    QVector<double> cuts{0, 1};
    for (int i = 0; i < ring.size(); ++i) {
        const QPointF c = ring[i], d = ring[(i + 1) % ring.size()];
        const QPointF edge = d - c;
        const double denominator = patchCross(delta, edge);
        if (denominator != 0) {
            const double t = patchCross(c - a, edge) / denominator;
            const double u = patchCross(c - a, delta) / denominator;
            if (t >= 0 && t <= 1 && u >= 0 && u <= 1)
                cuts.append(t);
        } else if (patchPointOnSegment(c, a, b, tolerance) ||
                   patchPointOnSegment(d, a, b, tolerance)) {
            cuts.append(std::clamp(QPointF::dotProduct(c - a, delta) / length2, 0.0, 1.0));
            cuts.append(std::clamp(QPointF::dotProduct(d - a, delta) / length2, 0.0, 1.0));
        }
    }
    std::sort(cuts.begin(), cuts.end());
    for (int i = 1; i < cuts.size(); ++i) {
        if (!(cuts[i] > cuts[i - 1]))
            continue;
        const QPointF point = a + delta * ((cuts[i] + cuts[i - 1]) / 2);
        if (path.contains(point) && !patchPointOnRing(point, ring, tolerance))
            return true;
    }
    return false;
}

/**
 * @brief Validate explicit patch containment/overlap and required-constraint compatibility.
 * @param[in] patches Borrowed valid local-index quad patches, unchanged; each fully validated
 * before use.
 * @param[in] domains Borrowed finite outer rings, unchanged; united into one winding-fill
 * domain.
 * @param[in] holes Borrowed finite exclusion seeds, unchanged; innermost enclosing closed
 * constraint ring identifies hole, ambiguous intersecting rings around seed reject.
 * @param[in] segments Borrowed required finite polylines, unchanged; closed exact-coordinate
 * loops participate in hole ownership; any segment entering patch interior rejects.
 * @param[in] origin Borrowed common finite XY origin subtracted for stable clipping, unchanged.
 * @param[in,out] exclusionBoundaries Required nonnull borrowed vector, appends relative hole
 * rings before patch checks, not cleared; may be partial on failure. Not retained.
 * @return Empty on compatible placement, otherwise first owned diagnostic. Qt path area
 * tolerance max(1e-24,patchArea*64*epsilon) permits tiny rounding slivers, not a robust exact
 * topology proof. Allocates grids/path clipping/prior patch arrays, no original geometry
 * mutation, callback or IO.
 *
 * @details Empty on success. \p exclusionBoundaries receives the hole rings (relative to \p
 * origin) so the stitch check can exempt patch edges lying on them.
 */
QString validatePatchPlacement(const QVector<PatchMesh> &patches, const QVector<QPolygonF> &domains,
                               const QVector<QPointF> &holes,
                               const QVector<ConstraintSegment> &segments, const QPointF &origin,
                               QVector<QPolygonF> *exclusionBoundaries)
{
    QPainterPath domain;
    for (const QPolygonF &ring : domains)
        domain = domain.united(patchRingPath(patchRelativeRing(ring, origin)));
    const double span = std::max(domain.boundingRect().width(), domain.boundingRect().height());
    const double tolerance = std::max(1e-9, span * 16 * std::numeric_limits<double>::epsilon());
    // A hole seed identifies a face: with nested closed rings only the
    // innermost enclosing ring bounds it, an enclosing ring stays domain.
    QVector<QPolygonF> closedRings;
    QVector<QPainterPath> closedPaths;
    QVector<double> closedAreas;
    for (const ConstraintSegment &segment : segments) {
        if (segment.path.size() < 4 || segment.path.first().x() != segment.path.last().x() ||
            segment.path.first().y() != segment.path.last().y())
            continue;
        closedRings.append(patchRelativeRing(QPolygonF(segment.path), origin));
        closedPaths.append(patchRingPath(closedRings.last()));
        closedAreas.append(patchPathArea(closedPaths.last()));
    }
    // Bucket closed rings by bounds: with tens of thousands of footprint
    // rings, testing every seed against every ring is quadratic.
    QVector<QRectF> closedBounds;
    closedBounds.reserve(closedPaths.size());
    for (const QPainterPath &path : std::as_const(closedPaths))
        closedBounds.append(path.controlPointRect());
    QRectF closedExtent;
    for (const QRectF &b : std::as_const(closedBounds))
        closedExtent = closedExtent.isNull() ? b : closedExtent.united(b);
    const int gridSide =
        closedPaths.isEmpty() || !(closedExtent.width() > 0) || !(closedExtent.height() > 0)
            ? 1
            : std::clamp(int(std::sqrt(double(closedPaths.size()))), 1, 1024);
    const auto gridCell = [&](double v, double lo, double span) {
        return span > 0 ? std::clamp(int((v - lo) / span * gridSide), 0, gridSide - 1) : 0;
    };
    QVector<QVector<int>> grid(gridSide * gridSide);
    for (int i = 0; i < closedBounds.size(); ++i) {
        const QRectF &b = closedBounds[i];
        for (int y = gridCell(b.top(), closedExtent.top(), closedExtent.height());
             y <= gridCell(b.bottom(), closedExtent.top(), closedExtent.height()); ++y)
            for (int x = gridCell(b.left(), closedExtent.left(), closedExtent.width());
                 x <= gridCell(b.right(), closedExtent.left(), closedExtent.width()); ++x)
                grid[y * gridSide + x].append(i);
    }
    // Rings whose bounds meet \p box, ascending (the order of a full scan).
    const auto ringsNear = [&](const QRectF &box) {
        QVector<int> out;
        if (closedPaths.isEmpty() || box.right() < closedExtent.left() ||
            box.left() > closedExtent.right() || box.bottom() < closedExtent.top() ||
            box.top() > closedExtent.bottom())
            return out;
        for (int y = gridCell(box.top(), closedExtent.top(), closedExtent.height());
             y <= gridCell(box.bottom(), closedExtent.top(), closedExtent.height()); ++y)
            for (int x = gridCell(box.left(), closedExtent.left(), closedExtent.width());
                 x <= gridCell(box.right(), closedExtent.left(), closedExtent.width()); ++x)
                for (int i : grid[y * gridSide + x]) {
                    const QRectF &b = closedBounds[i];
                    if (b.left() <= box.right() && box.left() <= b.right() &&
                        b.top() <= box.bottom() && box.top() <= b.bottom())
                        out.append(i);
                }
        std::sort(out.begin(), out.end());
        out.erase(std::unique(out.begin(), out.end()), out.end());
        return out;
    };
    QSet<int> holeRings;
    for (const QPointF &hole : holes) {
        QVector<int> enclosing;
        int innermost = -1;
        for (int i : ringsNear(QRectF(hole - origin, QSizeF(0, 0)))) {
            if (!closedPaths[i].contains(hole - origin))
                continue;
            enclosing.append(i);
            if (innermost < 0 || closedAreas[i] < closedAreas[innermost])
                innermost = i;
        }
        if (innermost < 0)
            continue;
        const double areaTolerance =
            std::max(1e-24, closedAreas[innermost] * 64 * std::numeric_limits<double>::epsilon());
        for (int other : enclosing)
            if (patchPathArea(closedPaths[innermost].subtracted(closedPaths[other])) >
                areaTolerance)
                return QStringLiteral(
                    "MeshGenerator: an excluded-hole seed lies in intersecting constraint rings; "
                    "use one unambiguous closed hole boundary before adding structured patches.");
        holeRings.insert(innermost);
    }
    // Holes are not subtracted from one global domain path (quadratic in
    // the number of footprints); each patch below checks only the hole
    // rings near it, which is the same set relation:
    // patch - (domain - holes) = (patch - domain) + (patch within a hole).
    for (int index : holeRings)
        exclusionBoundaries->append(closedRings[index]);
    QVector<QPainterPath> priorPaths;
    for (int i = 0; i < patches.size(); ++i) {
        const auto fail = [&](const QString &reason) {
            return QStringLiteral("MeshGenerator: structured patch %1%2 %3")
                .arg(i + 1)
                .arg(patches[i].tag.isEmpty() ? QString()
                                              : QStringLiteral(" ('%1')").arg(patches[i].tag))
                .arg(reason);
        };
        QString error;
        const QPolygonF ordered = orderedPatchBoundary(patches[i], &error);
        if (!error.isEmpty() || ordered.size() < 3)
            return fail(error.isEmpty() ? QStringLiteral("has no valid boundary.") : error);
        const QPolygonF ring = patchRelativeRing(ordered, origin);
        const QPainterPath path = patchRingPath(ring);
        const double areaTolerance =
            std::max(1e-24, patchPathArea(path) * 64 * std::numeric_limits<double>::epsilon());
        QPainterPath outside = path.subtracted(domain);
        for (int index : ringsNear(path.controlPointRect()))
            if (holeRings.contains(index))
                outside = outside.united(path.intersected(closedPaths[index]));
        if (patchPathArea(outside) > areaTolerance)
            return fail(QStringLiteral(
                "extends outside the meshing domain; clip or resize it before generating."));
        for (int j = 0; j < priorPaths.size(); ++j)
            if (patchPathArea(path.intersected(priorPaths[j])) > areaTolerance)
                return fail(
                    QStringLiteral(
                        "overlaps structured patch %1; separate the corridors or define a conforming junction.")
                        .arg(j + 1));
        for (const QPointF &hole : holes)
            if (path.contains(hole - origin) && !patchPointOnRing(hole - origin, ring, tolerance))
                return fail(QStringLiteral(
                    "contains an excluded-hole seed; move the patch outside the hole."));
        for (const ConstraintSegment &segment : segments) {
            if (!polylineIntersectsRect(segment.path, ordered.boundingRect()))
                continue;
            for (int k = 1; k < segment.path.size(); ++k)
                if (patchSegmentEntersInterior(segment.path[k - 1] - origin,
                                               segment.path[k] - origin, ring, path, tolerance))
                    return fail(
                        QStringLiteral(
                            "crosses or contains a required constraint%1 (marker %2, edge (%3, %4)-(%5, %6)); align the boundary or split the patch.")
                            .arg(segment.tag.isEmpty() ? QString()
                                                       : QStringLiteral(" '%1'").arg(segment.tag))
                            .arg(segment.marker)
                            .arg(segment.path[k - 1].x(), 0, 'f', 2)
                            .arg(segment.path[k - 1].y(), 0, 'f', 2)
                            .arg(segment.path[k].x(), 0, 'f', 2)
                            .arg(segment.path[k].y(), 0, 'f', 2));
        }
        priorPaths.append(path);
    }
    return {};
}

/**
 * @brief Test whether every subinterval of a segment lies on one outline boundary.
 * @param[in] a Borrowed finite segment start in common relative XY frame, unchanged.
 * @param[in] b Borrowed compatible end, unchanged; zero length returns false.
 * @param[in] outlines Borrowed implicit-closure rings, unchanged; each tested independently.
 * @param[in] tolerance Nonnegative finite XY proximity distance, unchecked.
 * @return True when one ring covers every interval after splitting at nearby ring vertices; does
 * not combine separate rings to bridge gaps. Allocates cuts, no source mutation or ownership
 * retention.
 *
 * @details True when the edge a–b lies entirely on one of \p outlines, checking every
 * sub-interval so a gap between collinear segments is not bridged.
 */
bool patchEdgeOnOutline(const QPointF &a, const QPointF &b, const QVector<QPolygonF> &outlines,
                        double tolerance)
{
    for (const QPolygonF &ring : outlines) {
        QVector<double> cuts{0, 1};
        const QPointF delta = b - a;
        const double length2 = QPointF::dotProduct(delta, delta);
        if (!(length2 > 0))
            return false;
        for (const QPointF &point : ring)
            if (patchPointOnSegment(point, a, b, tolerance))
                cuts.append(std::clamp(QPointF::dotProduct(point - a, delta) / length2, 0.0, 1.0));
        std::sort(cuts.begin(), cuts.end());
        bool covered = true;
        for (int i = 1; i < cuts.size(); ++i) {
            if (!(cuts[i] > cuts[i - 1]))
                continue;
            if (!patchPointOnRing(a + delta * ((cuts[i] + cuts[i - 1]) / 2), ring, tolerance)) {
                covered = false;
                break;
            }
        }
        if (covered)
            return true;
    }
    return false;
}

} // namespace

// ── Input accumulation ─────────────────────────────────────────────────────

void MeshGenerator::setDomain(const QPolygonF &p)
{
    m_domains.clear();
    m_domains.append(p);
}

void MeshGenerator::setDomains(const QVector<QPolygonF> &polys)
{
    m_domains = polys;
}

void MeshGenerator::addDomain(const QPolygonF &p)
{
    m_domains.append(p);
}
void MeshGenerator::addConstraintSegment(const ConstraintSegment &s)
{
    m_segments.append(s);
}
void MeshGenerator::addSteinerPoint(const SteinerPoint &p)
{
    m_steiners.append(p);
}
void MeshGenerator::reserveSteinerPoints(qsizetype additional)
{
    m_steiners.reserve(m_steiners.size() + additional);
}
void MeshGenerator::addHole(const QPointF &xy)
{
    m_holes.append(xy);
}
void MeshGenerator::addRegion(const RegionMarker &r)
{
    m_regions.append(r);
}
void MeshGenerator::addPatch(const PatchMesh &p)
{
    m_patches.append(p);
}
void MeshGenerator::addQuadRegion(const QuadRegion &r)
{
    m_quadRegions.append(r);
}
void MeshGenerator::setTerrainBreaklines(const QVector<QVector<QPointF>> &lines)
{
    m_terrainLines = lines;
}
void MeshGenerator::setOptions(const GenerationOptions &o)
{
    m_opts = o;
}
void MeshGenerator::setRefineHook(const RefineHook &h)
{
    m_refineHook = h;
}

QString MeshGenerator::tagForVertexMarker(int marker) const
{
    return m_vertexTagByMarker.value(marker);
}

QString MeshGenerator::tagForEdgeMarker(int marker) const
{
    return m_edgeTagByMarker.value(marker);
}

QSet<QPair<int, int>> MeshGenerator::quadRegionMergeLocks(const MeshResult &) const
{
    return {}; // the generator never pairs triangles; nothing to protect
}

// ── Helpers ────────────────────────────────────────────────────────────────

namespace
{

constexpr int kBoundaryMarker = 1;
constexpr double kEquilateral = 0.4330127018922193; // √3/4
// Refinement splits a triangle larger than the equilateral one of side
// kMeanEdge · h, which leaves the MEAN edge at h (measured: 0.82 h at
// factor 1, tests/output/mesh_triangle_engine_2026-09-30/debug/size_probe.cpp)
// — h is the target edge length the dialog promises, not an upper bound.
constexpr double kMeanEdge = 1.22;
// Refinement never splits an edge below kRefineFloor · hMin (a floor against
// cascades); constraint lines closer than that are joined before meshing.
constexpr double kRefineFloor = 0.25;
// A quad strip stops short of a corner sharper than this (its inner side
// would shrink by more than a fifth of the width there).
constexpr double kStripMaxTurnDeg = 45.0;

using EdgeKey = QPair<int, int>; // mesh::edgeKey (meshedgekey.h) builds them

/**
 * @brief Encode exact coordinate bits into a two-word identity with signed zero folded.
 * @param[in] p Borrowed XY point, unchanged; finite values expected by generator.
 * @return Pair of 64-bit double bit patterns, not a tolerance/spatial hash. No
 * allocation/mutation.
 *
 * @details Exact-coordinate key (bit pattern, -0 folded).
 */
inline QPair<qint64, qint64> coordKey(const QPointF &p)
{
    double x = p.x(), y = p.y();
    if (x == 0.0)
        x = 0.0;
    if (y == 0.0)
        y = 0.0;
    qint64 kx, ky;
    std::memcpy(&kx, &x, 8);
    std::memcpy(&ky, &y, 8);
    return {kx, ky};
}

/**
 * @brief Copy a point sequence without consecutive/closing QPointF-equal duplicates.
 * @param[in] ring Borrowed coordinates, unchanged; equality follows QPointF semantics.
 * @return Owned open sequence, possibly empty/short. Does not ensure finite/simple/CCW polygon
 * or remove nonconsecutive duplicates. Allocates output, no unit conversion or retained
 * references.
 *
 * @details Ring without its closing duplicate and without consecutive duplicates.
 */
QVector<QPointF> openRing(const QVector<QPointF> &ring, bool exactCoordinates = false)
{
    const auto same = [exactCoordinates](QPointF a, QPointF b) {
        return exactCoordinates ? a.x() == b.x() && a.y() == b.y() : a == b;
    };
    QVector<QPointF> out;
    out.reserve(ring.size());
    for (const QPointF &p : ring)
        if (out.isEmpty() || !same(out.last(), p))
            out.append(p);
    if (out.size() > 1 && same(out.first(), out.last()))
        out.removeLast();
    return out;
}

/**
 * @brief Apply planar odd-even ray parity to an implicitly closed coordinate sequence.
 * @param[in] ring Borrowed finite Cartesian points, unchanged; empty/short inputs allowed but
 * not certified.
 * @param[in] p Borrowed finite same-frame query, unchanged.
 * @return Ray-parity membership; exact boundary behavior unspecified, caller handles boundaries
 * when needed. No orientation, simplicity, robust-predicate audit, allocation or state mutation.
 */
bool pointInRing(const QVector<QPointF> &ring, const QPointF &p)
{
    bool inside = false;
    const int n = ring.size();
    for (int i = 0, j = n - 1; i < n; j = i++) {
        const QPointF &a = ring[i], &b = ring[j];
        if ((a.y() > p.y()) != (b.y() > p.y())) {
            const double xi = a.x() + (p.y() - a.y()) * (b.x() - a.x()) / (b.y() - a.y());
            if (p.x() < xi)
                inside = !inside;
        }
    }
    return inside;
}

/*! Uniform grid over ring bounding boxes, so point-in-ring queries against
 *  thousands of holes test only rings whose box holds the point. Boxes are
 *  widened by a rounding margin: a point outside one is never inside the
 *  ring by pointInRing(), so filtering cannot change any answer. Candidates
 *  are visited in ascending ring index, as a full scan would visit them. */
class RingIndex
{
public:
    /**
     * @brief Build conservative grid metadata over borrowed immutable ring arrays.
     * @param[in] rings Borrowed vector of nullable ring pointers, copied as pointers only. Every
     * nonnull ring must outlive index and remain unchanged; >=3-point finite rings participate.
     * No ownership transfer. Owns padded boxes, <=1024-per-axis cells and large-box list; boxes
     * spanning >64 cells use fallback list. Finite grid arithmetic/int counts caller
     * prerequisite; no geometry validation/mutation or IO.
     */
    explicit RingIndex(const QVector<const QVector<QPointF> *> &rings)
        : m_rings(rings), m_boxes(rings.size())
    {
        QRectF all;
        for (int i = 0; i < rings.size(); ++i) {
            if (!rings[i] || rings[i]->size() < 3)
                continue;
            double x0 = rings[i]->first().x(), x1 = x0, y0 = rings[i]->first().y(), y1 = y0;
            for (const QPointF &q : *rings[i]) {
                x0 = std::min(x0, q.x());
                x1 = std::max(x1, q.x());
                y0 = std::min(y0, q.y());
                y1 = std::max(y1, q.y());
            }
            const double m =
                1e-9 * (1.0 + std::max({std::abs(x0), std::abs(x1), std::abs(y0), std::abs(y1)}));
            m_boxes[i] = QRectF(QPointF(x0 - m, y0 - m), QPointF(x1 + m, y1 + m));
            all = all.isNull() ? m_boxes[i] : all.united(m_boxes[i]);
        }
        if (all.isNull())
            return;
        m_origin = all.topLeft();
        m_n = std::clamp(int(std::sqrt(double(rings.size()))), 1, 1024);
        m_cw = std::max(all.width() / m_n, 1e-12);
        m_ch = std::max(all.height() / m_n, 1e-12);
        m_cells.resize(qsizetype(m_n) * m_n);
        for (int i = 0; i < rings.size(); ++i) {
            if (m_boxes[i].isNull())
                continue;
            const int c0 = cx(m_boxes[i].left()), c1 = cx(m_boxes[i].right());
            const int r0 = cy(m_boxes[i].top()), r1 = cy(m_boxes[i].bottom());
            if (qint64(c1 - c0 + 1) * (r1 - r0 + 1) > 64) {
                m_large.append(i);
                continue;
            }
            for (int r = r0; r <= r1; ++r)
                for (int c = c0; c <= c1; ++c)
                    m_cells[r * m_n + c].append(i);
        }
    }
    /**
     * @brief Visit containing rings in ascending source index, stopping when callback returns
     * false.
     * @param[in] p Borrowed finite query in ring XY units, unchanged; cell conversions fit int.
     * @param[in] fn Synchronous borrowed callable receiving ring index and returning continue
     * bool. Not retained, must not mutate indexed rings/index or throw; transient geometry not
     * exposed. Conservative box filtering then odd-even ring test. Null/short rings omitted. No
     * ownership transfer, source mutation or IO; caller ensures immutable borrowed ring
     * lifetime.
     *
     * @details Calls \p fn(index) for each ring containing \p p, in ascending index order, until
     * \p fn returns false.
     */
    template <class Fn> void visitContaining(const QPointF &p, Fn &&fn) const
    {
        if (m_cells.isEmpty())
            return;
        static const QVector<int> none;
        const bool inGrid = p.x() >= m_origin.x() && p.y() >= m_origin.y() &&
                            p.x() <= m_origin.x() + m_cw * m_n &&
                            p.y() <= m_origin.y() + m_ch * m_n;
        const QVector<int> &cell = inGrid ? m_cells[cy(p.y()) * m_n + cx(p.x())] : none;
        qsizetype a = 0, b = 0;
        while (a < cell.size() || b < m_large.size()) {
            const int i = (b >= m_large.size() || (a < cell.size() && cell[a] < m_large[b]))
                              ? cell[a++]
                              : m_large[b++];
            if (m_boxes[i].contains(p) && pointInRing(*m_rings[i], p) && !fn(i))
                return;
        }
    }
    /**
     * @brief Test whether any indexed ring contains a finite point.
     * @param[in] p Borrowed common-frame XY query, unchanged.
     * @return True on first odd-even ring hit, false otherwise, boundary semantics inherited.
     * Uses no source mutation/retention; requires live immutable ring pointers/index and
     * int-safe grid arithmetic.
     */
    [[nodiscard]] bool anyContains(const QPointF &p) const
    {
        bool found = false;
        visitContaining(p, [&](int) {
            found = true;
            return false;
        });
        return found;
    }

private:
    /**
     * @brief Map an x coordinate to a clamped ring-grid column.
     * @param[in] x Finite same-frame coordinate with (x-origin)/cellWidth representable as int,
     * unchecked.
     * @return Clamped [0,m_n-1] column; requires nonempty initialized grid. No
     * allocation/mutation.
     */
    int cx(double x) const { return std::clamp(int((x - m_origin.x()) / m_cw), 0, m_n - 1); }
    /**
     * @brief Map a y coordinate to a clamped ring-grid row.
     * @param[in] y Finite same-frame coordinate with quotient representable as int, unchecked.
     * @return Clamped [0,m_n-1] row; requires initialized nonempty grid. No allocation/mutation.
     */
    int cy(double y) const { return std::clamp(int((y - m_origin.y()) / m_ch), 0, m_n - 1); }
    QVector<const QVector<QPointF> *> m_rings;
    QVector<QRectF> m_boxes;
    QVector<QVector<int>> m_cells;
    QVector<int> m_large;
    QPointF m_origin;
    double m_cw = 1, m_ch = 1;
    int m_n = 0;
};

/**
 * @brief Collect nonowning pointers to current ring vector elements.
 * @param[in] rings Borrowed ring container, unchanged; must remain alive/unreallocated and
 * immutable while returned pointers are used. Returned pointer array owns no ring geometry.
 * @return Owned pointer vector in input order; allocation only, no source mutation or lifetime
 * extension.
 */
QVector<const QVector<QPointF> *> ringPointers(const QVector<QVector<QPointF>> &rings)
{
    QVector<const QVector<QPointF> *> out;
    out.reserve(rings.size());
    for (const auto &r : rings)
        out.append(&r);
    return out;
}

/*! Uniform grid over segments for "is anything within r of p" queries. */
class SegmentGrid
{
public:
    /**
     * @brief Replace segment-grid contents with owned segment copies indexed by bounding cells.
     * @param[in] segs Borrowed finite compatible-frame endpoint pairs, unchanged; copied into
     * owned arrays.
     * @param[in] cell Positive finite intended grid pitch in XY units; <=0/NaN falls back to 1,
     * infinity is not independently rejected. Coordinate/pitch quotients and grid loops must fit
     * qint64. Resets every owner to -1 and grid, fills full segment bounding rectangles; long
     * diagonal segments can cost many buckets. Use addSplit for bounded stamping. No source
     * retention, unit conversion, geometry validation, cancellation or IO; exclude queries
     * during mutation.
     */
    void build(const QVector<QPair<QPointF, QPointF>> &segs, double cell)
    {
        m_segs = segs;
        m_owner.fill(-1, segs.size());
        m_cell = cell > 0.0 ? cell : 1.0;
        m_index.clear();
        for (int i = 0; i < m_segs.size(); ++i) {
            const QPointF &a = m_segs[i].first, &b = m_segs[i].second;
            const qint64 x0 = cellOf(std::min(a.x(), b.x())), x1 = cellOf(std::max(a.x(), b.x()));
            const qint64 y0 = cellOf(std::min(a.y(), b.y())), y1 = cellOf(std::max(a.y(), b.y()));
            for (qint64 y = y0; y <= y1; ++y)
                for (qint64 x = x0; x <= x1; ++x)
                    m_index[qMakePair(x, y)].append(i);
        }
    }
    /**
     * @brief Append one owned segment as cell-length pieces with an owner ID for filtered
     * queries.
     * @param[in] a Borrowed finite start in grid XY units, unchanged.
     * @param[in] b Borrowed compatible end, unchanged; zero-length segment allowed.
     * @param[in] owner Caller identity copied to all pieces; -1 default, not validated. Requires
     * initialized finite positive cell and representable ceil(length/cell), counts/grid loops.
     * Appends piece arrays/buckets, may allocate; no total budget, source retention, geometry
     * repair or IO.
     *
     * @details Add a segment, split into pieces no longer than the cell so each piece touches at
     * most a 2×2 block of cells (long domain edges would otherwise index their whole bounding
     * box).
     */
    void addSplit(const QPointF &a, const QPointF &b, int owner = -1)
    {
        const double len = std::hypot(b.x() - a.x(), b.y() - a.y());
        const int n = std::max(1, int(std::ceil(len / m_cell)));
        QPointF prev = a;
        for (int k = 1; k <= n; ++k) {
            const QPointF next = k == n ? b : a + (b - a) * (double(k) / n);
            const int i = m_segs.size();
            m_segs.append(qMakePair(prev, next));
            m_owner.append(owner);
            const qint64 x0 = cellOf(std::min(prev.x(), next.x())),
                         x1 = cellOf(std::max(prev.x(), next.x()));
            const qint64 y0 = cellOf(std::min(prev.y(), next.y())),
                         y1 = cellOf(std::max(prev.y(), next.y()));
            for (qint64 y = y0; y <= y1; ++y)
                for (qint64 x = x0; x <= x1; ++x)
                    m_index[qMakePair(x, y)].append(i);
            prev = next;
        }
    }
    /**
     * @brief Test strict clearance from a query segment to any eligible indexed segment.
     * @param[in] a Borrowed finite query start, unchanged.
     * @param[in] b Borrowed compatible query end, unchanged.
     * @param[in] r Positive finite XY clearance radius, grid extents representable; zero yields
     * false even for exact contact because comparison is strict squared-distance <r*r.
     * @param[in] ignore Owner ID to skip, default minimum int; matching IDs omitted without
     * validation.
     * @return True for a tested segment within strict radius, false otherwise. Duplicate bucket
     * visits allowed; no allocation, source mutation or retained references, const queries
     * require immutable grid.
     *
     * @details True when segment ab comes within r of any indexed segment (crossing or touching
     * counts as distance 0), ignoring the segments added with owner \p ignore.
     */
    [[nodiscard]] bool segmentNear(const QPointF &a, const QPointF &b, double r,
                                   int ignore = std::numeric_limits<int>::min()) const
    {
        const double r2 = r * r;
        const qint64 x0 = cellOf(std::min(a.x(), b.x()) - r),
                     x1 = cellOf(std::max(a.x(), b.x()) + r);
        const qint64 y0 = cellOf(std::min(a.y(), b.y()) - r),
                     y1 = cellOf(std::max(a.y(), b.y()) + r);
        for (qint64 y = y0; y <= y1; ++y)
            for (qint64 x = x0; x <= x1; ++x) {
                const auto it = m_index.constFind(qMakePair(x, y));
                if (it == m_index.constEnd())
                    continue;
                for (int i : it.value())
                    if (m_owner[i] != ignore &&
                        segSegDist2(a, b, m_segs[i].first, m_segs[i].second) < r2)
                        return true;
            }
        return false;
    }
    /**
     * @brief Compute squared planar segment distance, with proper crossings returning zero.
     * @param[in] a Borrowed first segment start, unchanged.
     * @param[in] b Borrowed first end, unchanged.
     * @param[in] c Borrowed second start in common finite frame, unchanged.
     * @param[in] d Borrowed second end, unchanged.
     * @return Squared XY distance via strict cross signs or minimum endpoint-to-segment
     * distance, so touches/collinear overlaps also have zero distance under ordinary arithmetic.
     * Not an adaptive robust predicate, no finite audit, allocation, state mutation or input
     * retention.
     *
     * @details Squared distance between segments ab and cd (0 when they cross).
     */
    static double segSegDist2(const QPointF &a, const QPointF &b, const QPointF &c,
                              const QPointF &d)
    {
        auto orient = [](const QPointF &p, const QPointF &q, const QPointF &r) {
            return (q.x() - p.x()) * (r.y() - p.y()) - (q.y() - p.y()) * (r.x() - p.x());
        };
        const double d1 = orient(c, d, a), d2 = orient(c, d, b), d3 = orient(a, b, c),
                     d4 = orient(a, b, d);
        if (((d1 > 0 && d2 < 0) || (d1 < 0 && d2 > 0)) &&
            ((d3 > 0 && d4 < 0) || (d3 < 0 && d4 > 0)))
            return 0.0;
        return std::min({pslg::distSqToSegment(a, c, d), pslg::distSqToSegment(b, c, d),
                         pslg::distSqToSegment(c, a, b), pslg::distSqToSegment(d, a, b)});
    }
    /*! True when some segment lies closer to \p p than the clearance its
     *  owner asks for (\p clearance(owner), at most \p rMax). */
    template <class F>
    /**
     * @brief Test point clearance against each candidate owner's individual threshold.
     * @param[in] p Borrowed finite grid-frame query, unchanged.
     * @param[in] rMax Finite nonnegative search radius in XY units, at least every clearance
     * callback result.
     * @param[in] clearance Synchronous callable taking owner ID, returning intended nonnegative
     * finite XY clearance <=rMax. May be called repeatedly for same segment/owner, not retained,
     * must not mutate grid/throw.
     * @return True for strict squared-distance hit, false otherwise. Larger callback radii can
     * be missed outside searched buckets; negative values are squared rather than validated. No
     * source mutation or IO.
     */
    [[nodiscard]] bool withinOwnClearance(const QPointF &p, double rMax, F &&clearance) const
    {
        const qint64 x0 = cellOf(p.x() - rMax), x1 = cellOf(p.x() + rMax);
        const qint64 y0 = cellOf(p.y() - rMax), y1 = cellOf(p.y() + rMax);
        for (qint64 y = y0; y <= y1; ++y)
            for (qint64 x = x0; x <= x1; ++x) {
                const auto it = m_index.constFind(qMakePair(x, y));
                if (it == m_index.constEnd())
                    continue;
                for (int i : it.value()) {
                    const double c = clearance(m_owner[i]);
                    if (pslg::distSqToSegment(p, m_segs[i].first, m_segs[i].second) < c * c)
                        return true;
                }
            }
        return false;
    }
    /**
     * @brief Invoke a callback for every segment entry in cells overlapping a rectangle.
     * @param[in] box Borrowed normalized finite grid-frame rectangle, unchanged; int64-safe cell
     * bounds required.
     * @param[in] f Synchronous callable receiving const endpoint references and owner ID; can
     * repeat the same segment through multiple buckets, not retained. References expire on grid
     * mutation/destruction; must not mutate grid or throw. No precise segment/box filtering
     * performed. Const grid access, no geometry copy/ownership transfer or IO; callback owns any
     * output allocations.
     *
     * @details Call \p f(segment endpoints a, b, owner) for every segment indexed in a cell that
     * meets \p box (a segment may come twice).
     */
    template <class F> void visitBox(const QRectF &box, F &&f) const
    {
        for (qint64 y = cellOf(box.top()); y <= cellOf(box.bottom()); ++y)
            for (qint64 x = cellOf(box.left()); x <= cellOf(box.right()); ++x) {
                const auto it = m_index.constFind(qMakePair(x, y));
                if (it == m_index.constEnd())
                    continue;
                for (int i : it.value())
                    f(m_segs[i].first, m_segs[i].second, m_owner[i]);
            }
    }
    /**
     * @brief Return nearest eligible segment distance among buckets intersecting a search
     * square.
     * @param[in] p Borrowed finite grid-frame query, unchanged.
     * @param[in] r Finite nonnegative XY search half-width with representable cell extents.
     * @param[in] ignoreA First owner ID excluded.
     * @param[in] ignoreB Second owner ID excluded, may equal first.
     * @return Minimum squared XY distance of visited segments, infinity if none. Does NOT clamp
     * result to r or prove no nearer segment outside search square; far segments sharing a
     * bucket can return distance >r. No source mutation, allocation, IO or retained pointers.
     *
     * @details Squared distance to the nearest segment not owned by \p ignoreA or
     * \p ignoreB, or +inf when none within r.
     */
    [[nodiscard]] double nearest2Ignoring(const QPointF &p, double r, int ignoreA,
                                          int ignoreB) const
    {
        double best = std::numeric_limits<double>::infinity();
        const qint64 x0 = cellOf(p.x() - r), x1 = cellOf(p.x() + r);
        const qint64 y0 = cellOf(p.y() - r), y1 = cellOf(p.y() + r);
        for (qint64 y = y0; y <= y1; ++y)
            for (qint64 x = x0; x <= x1; ++x) {
                const auto it = m_index.constFind(qMakePair(x, y));
                if (it == m_index.constEnd())
                    continue;
                for (int i : it.value())
                    if (m_owner[i] != ignoreA && m_owner[i] != ignoreB)
                        best = std::min(
                            best, pslg::distSqToSegment(p, m_segs[i].first, m_segs[i].second));
            }
        return best;
    }
    /**
     * @brief Return minimum segment distance among buckets intersecting a query-centered square.
     * @param[in] p Borrowed finite grid-frame query, unchanged.
     * @param[in] r Finite nonnegative XY search half-width, cell extents must fit int64.
     * @return Minimum visited squared XY distance or infinity without candidates, not explicitly
     * limited to r. No exact global nearest guarantee beyond searched cells; no
     * allocation/mutation/IO.
     *
     * @details Squared distance to the nearest segment, or +inf when none within r.
     */
    [[nodiscard]] double nearest2(const QPointF &p, double r) const
    {
        double best = std::numeric_limits<double>::infinity();
        const qint64 x0 = cellOf(p.x() - r), x1 = cellOf(p.x() + r);
        const qint64 y0 = cellOf(p.y() - r), y1 = cellOf(p.y() + r);
        for (qint64 y = y0; y <= y1; ++y)
            for (qint64 x = x0; x <= x1; ++x) {
                const auto it = m_index.constFind(qMakePair(x, y));
                if (it == m_index.constEnd())
                    continue;
                for (int i : it.value())
                    best =
                        std::min(best, pslg::distSqToSegment(p, m_segs[i].first, m_segs[i].second));
            }
        return best;
    }

private:
    /**
     * @brief Convert one coordinate to its signed segment-grid cell index.
     * @param[in] v Finite grid-frame ordinate whose floor(v/cell) fits qint64, unchecked.
     * @return Floor quotient; requires positive finite initialized cell. No allocation or
     * mutation.
     */
    [[nodiscard]] qint64 cellOf(double v) const { return qint64(std::floor(v / m_cell)); }
    QVector<QPair<QPointF, QPointF>> m_segs;
    QVector<int> m_owner;
    QHash<QPair<qint64, qint64>, QVector<int>> m_index;
    double m_cell = 1.0;
};

/*! A constraint polyline after preparation. */
struct Poly {
    QVector<QPointF> pts; ///< Open sequence; closed rings wrap.
    int marker = 0;
    QString tag;
    bool closed = false;
    bool resample = true; ///< Patch edges keep their stationing.
    bool isDomain = false;
    bool isHole = false;
    bool isTerrain = false; ///< Terrain break line: alignment only (no BC edge, no region barrier).
    bool fixed = false;     ///< Quad patch edge: refinement never splits it.
    bool barrier = true;    ///< Listed in boundaryEdges and bounds region tags.
    bool dropped = false;   ///< Replaced by a quad strip; not meshed.
    double stripWidth = 0.0; ///< Conduit strip width (ConstraintSegment::stripWidth).
    int regionIndex = -1;    ///< Quad region whose ring this is.
    QVector<int> cdtIds;     ///< Vertex ids after CDT build.
};

/*! A quad strip boundary edge and how far terrain lines keep from it. */
struct ClearEdge {
    QPointF a, b;
    double clear = 0.0;
};

/**
 * @brief Test strict distance between nonadjacent polyline segments with bounding-box rejection.
 * @param[in] pts Borrowed finite path vertices, unchanged; open-ring representation expected if
 * closed.
 * @param[in] closed Include implicit final edge and exempt first/last adjacency when true.
 * @param[in] r Positive finite XY clearance, unchecked; zero comparison does not flag exact
 * contact.
 * @return True for any nonadjacent pair with squared distance <r*r, false for <3 edges or no
 * hit. Quadratic pairs after bounds reject, allocates segment list, no source mutation or
 * retained references.
 *
 * @details True when two non-adjacent segments of \p pts come within \p r of each other (a
 * closed ring's last and first segments are adjacent).
 */
bool polylineSelfNear(const QVector<QPointF> &pts, bool closed, double r)
{
    const int n = pts.size();
    const int edges = closed ? n : n - 1;
    if (edges < 3)
        return false;
    QVector<QPair<QPointF, QPointF>> segs;
    for (int k = 0; k < edges; ++k)
        segs.append(qMakePair(pts[k], pts[(k + 1) % n]));
    // Pairwise with a bounding-box reject: simplified pieces are short.
    for (int i = 0; i < edges; ++i)
        for (int j = i + 2; j < edges; ++j) {
            if (closed && i == 0 && j == edges - 1)
                continue;
            const auto &A = segs[i], &B = segs[j];
            const double pad = r;
            if (std::max(A.first.x(), A.second.x()) + pad < std::min(B.first.x(), B.second.x()) ||
                std::max(B.first.x(), B.second.x()) + pad < std::min(A.first.x(), A.second.x()) ||
                std::max(A.first.y(), A.second.y()) + pad < std::min(B.first.y(), B.second.y()) ||
                std::max(B.first.y(), B.second.y()) + pad < std::min(A.first.y(), A.second.y()))
                continue;
            if (SegmentGrid::segSegDist2(A.first, A.second, B.first, B.second) < r * r)
                return true;
        }
    return false;
}

/**
 * @brief Trim/simplify dense terrain polylines into noncoupling constraints clear of other
 * inputs.
 * @param[in] lines Borrowed same-frame dense lines, unchanged; nonfinite/repeated points
 * filtered, longest lines processed first with stable ties. Closed input repeats first point.
 * @param[in] polys Borrowed existing compatible prepared constraints, unchanged; indexed as
 * obstacles.
 * @param[in] domainRings Borrowed outer rings, unchanged, immutable for local nonowning indexes.
 * @param[in] excludedRings Borrowed holes/patch interiors to avoid, unchanged.
 * @param[in] steiners Borrowed fixed points, unchanged; treated as zero-length obstacles.
 * @param[in] hMin Positive finite XY mesh floor, <=0/NaN returns empty. All grid/count ratios
 * representable.
 * @param[in] stripEdges Borrowed segment-specific clearance obstacles, unchanged; nonnegative
 * finite widths.
 * @param[in] minimumLength Positive XY accepted-run minimum; otherwise 4*hMin.
 * @return Owned prepared Poly values, empty for no inputs/no survivors. Per-line max sample
 * spacing sets cutback and simplification tolerance; then folds split, short/self-crowding runs
 * rejected and accepted paths become obstacles. Closed rings do not become holes/barriers. No
 * complete robust topology proof, total allocation budget, cancellation callbacks, source
 * mutation, engine/IO or retained pointers.
 *
 * @details Terrain break lines → constraint polylines that cannot cross or crowd anything else
 * (MESH_OVERHAUL_PHASE6B_FEATURE_CAPTURE_2026-09-30.md §2.1).
 *
 * Each dense input chain (spacing ≤ s, measured per line) is cut back to where every point lies
 * at least D = hMin + 2s + 2·dev from the other constraints, Steiner points and already accepted
 * lines (longest line first), inside the domain and outside holes and patches. Each surviving
 * run is simplified (RDP, then a minimum segment length of hMin) within dev = max(hMin/4, 0.75
 * s) — the staircase of a pixel chain is at most ~0.7 px off its line — split wherever it folds
 * back on itself (a hairpin of a two-pixel-wide response collapses to A→B→A′), and kept when it
 * is at least 4·hMin long and stays ≥ hMin/4 from every other constraint segment and from
 * itself. The separation bound makes the last check a formality; it is kept so a crossing
 * constraint can never reach the CDT.
 */
QVector<Poly> prepareTerrainLines(const QVector<QVector<QPointF>> &lines,
                                  const QVector<Poly> &polys,
                                  const QVector<QVector<QPointF>> &domainRings,
                                  const QVector<QVector<QPointF>> &excludedRings,
                                  const QVector<SteinerPoint> &steiners, double hMin,
                                  const QVector<ClearEdge> &stripEdges = {},
                                  double minimumLength = 0.0)
{
    QVector<Poly> out;
    if (lines.isEmpty() || !(hMin > 0.0))
        return out;
    // Spacing per line: one sparse line must not widen every other line's
    // cut-back.
    QVector<double> lineSeg(lines.size(), 0.0);
    for (int i = 0; i < lines.size(); ++i)
        for (int k = 1; k < lines[i].size(); ++k) {
            const double d = std::hypot(lines[i][k].x() - lines[i][k - 1].x(),
                                        lines[i][k].y() - lines[i][k - 1].y());
            if (std::isfinite(d))
                lineSeg[i] = std::max(lineSeg[i], d);
        }
    QVector<double> sorted = lineSeg;
    std::sort(sorted.begin(), sorted.end());
    const double typicalSeg = sorted[sorted.size() / 2] > 0.0 ? sorted[sorted.size() / 2] : hMin;
    const double minLen = minimumLength > 0 ? minimumLength : 4.0 * hMin;
    const double clear = 0.25 * hMin;

    SegmentGrid grid; // cell sized for the typical line; queries take any radius
    grid.build({}, hMin + 2.0 * typicalSeg + 2.0 * std::max(0.25 * hMin, 0.75 * typicalSeg));
    for (const Poly &p : polys) {
        const int n = p.pts.size();
        const int edges = p.closed ? n : n - 1;
        for (int k = 0; k < edges; ++k)
            grid.addSplit(p.pts[k], p.pts[(k + 1) % n]);
    }
    for (const SteinerPoint &sp : steiners)
        grid.addSplit(sp.xy, sp.xy);
    // Lines keep three quarters of an edge away from each quad strip edge: a
    // line closer than that would leave triangles too small for the strip's
    // fixed edge beside it.
    SegmentGrid strips;
    double stripMax = 0.0;
    for (const ClearEdge &e : stripEdges)
        stripMax = std::max(stripMax, e.clear);
    strips.build({}, std::max(stripMax, hMin));
    for (int i = 0; i < stripEdges.size(); ++i)
        strips.addSplit(stripEdges[i].a, stripEdges[i].b, i);
    auto nearStrip = [&](const QPointF &q) {
        return stripMax > 0.0 && strips.withinOwnClearance(q, stripMax, [&](int owner) {
            return stripEdges[owner].clear;
        });
    };

    const RingIndex domainIndex(ringPointers(domainRings)),
        excludedIndex(ringPointers(excludedRings));
    auto insideDomain = [&](const QPointF &q) {
        return domainIndex.anyContains(q) && !excludedIndex.anyContains(q);
    };

    QVector<int> order(lines.size());
    QVector<double> length(lines.size(), 0.0);
    for (int i = 0; i < lines.size(); ++i) {
        order[i] = i;
        length[i] = pslg::polylineLength(lines[i]);
    }
    std::stable_sort(order.begin(), order.end(),
                     [&](int a, int b) { return length[a] > length[b]; });

    // Interior angle under 30° at a vertex = the line folds back there.
    auto foldsAt = [](const QPointF &a, const QPointF &b, const QPointF &c) {
        const QPointF u = a - b, v = c - b;
        const double lu = std::hypot(u.x(), u.y()), lv = std::hypot(v.x(), v.y());
        if (!(lu > 0.0) || !(lv > 0.0))
            return false;
        return QPointF::dotProduct(u, v) / (lu * lv) > 0.8660254037844386; // cos 30°
    };
    std::function<void(QVector<QPointF>, bool)> accept = [&](QVector<QPointF> pts, bool closed) {
        // Drop consecutive duplicates (and the closing one of a ring).
        QVector<QPointF> dd;
        for (const QPointF &q : pts)
            if (dd.isEmpty() || dd.last() != q)
                dd.append(q);
        if (closed && dd.size() > 1 && dd.first() == dd.last())
            dd.removeLast();
        if (dd.size() < (closed ? 3 : 2))
            return;
        // Split a fold-back into its legs; the returning leg then fails the
        // clearance test against the first and is dropped. A folding ring is
        // dropped (rare: a loop that doubles back on itself).
        {
            const int n = dd.size();
            for (int k = closed ? 0 : 1; k < (closed ? n : n - 1); ++k)
                if (foldsAt(dd[(k + n - 1) % n], dd[k], dd[(k + 1) % n])) {
                    if (closed)
                        return;
                    accept(dd.mid(0, k + 1), false);
                    accept(dd.mid(k), false);
                    return;
                }
        }
        QVector<QPointF> path = dd;
        if (closed)
            path.append(path.first());
        if (pslg::polylineLength(path) < minLen)
            return;
        const int n = dd.size();
        const int edges = closed ? n : n - 1;
        for (int k = 0; k < edges; ++k)
            if (grid.segmentNear(dd[k], dd[(k + 1) % n], clear))
                return;
        if (polylineSelfNear(dd, closed, clear))
            return;
        Poly p;
        p.pts = dd;
        p.closed = closed;
        p.isTerrain = true;
        for (int k = 0; k < edges; ++k)
            grid.addSplit(dd[k], dd[(k + 1) % n]);
        out.append(p);
    };

    for (int li : order) {
        if (!(lineSeg[li] > 0.0))
            continue;
        const double dev = std::max(0.25 * hMin, 0.75 * lineSeg[li]);
        const double D = hMin + 2.0 * lineSeg[li] + 2.0 * dev;
        QVector<QPointF> pts;
        for (const QPointF &q : lines[li])
            if (std::isfinite(q.x()) && std::isfinite(q.y()) && (pts.isEmpty() || pts.last() != q))
                pts.append(q);
        bool closed = pts.size() >= 4 && pts.first() == pts.last();
        if (closed)
            pts.removeLast();
        if (pts.size() < 2)
            continue;
        const int n = pts.size();
        QVector<bool> blocked(n);
        int firstBlocked = -1;
        for (int k = 0; k < n; ++k) {
            blocked[k] = grid.nearest2(pts[k], D) < D * D || nearStrip(pts[k]);
            if (blocked[k] && firstBlocked < 0)
                firstBlocked = k;
        }
        if (closed && firstBlocked < 0) {
            if (!insideDomain(pts.first()))
                continue;
            QVector<QPointF> ring = pts;
            ring.append(ring.first());
            ring = pslg::simplifyRing(ring, dev);
            ring = pslg::resampleRingMinLength(ring, hMin, dev);
            accept(ring, true);
            continue;
        }
        // Open runs; a closed chain is rotated to start on a blocked point
        // so no run wraps around.
        QVector<QPointF> seq;
        QVector<bool> seqBlocked;
        if (closed) {
            for (int k = 0; k < n; ++k) {
                seq.append(pts[(firstBlocked + k) % n]);
                seqBlocked.append(blocked[(firstBlocked + k) % n]);
            }
        } else {
            seq = pts;
            seqBlocked = blocked;
        }
        int k = 0;
        while (k < seq.size()) {
            if (seqBlocked[k]) {
                ++k;
                continue;
            }
            QVector<QPointF> run;
            while (k < seq.size() && !seqBlocked[k])
                run.append(seq[k++]);
            if (run.size() < 2 || !insideDomain(run.first()))
                continue;
            run = pslg::simplifyPolyline(run, dev);
            run = pslg::resampleMinLength(run, hMin, dev);
            accept(run, false);
        }
    }
    return out;
}

/**
 * @brief Copy an arc-length interval of a finite polyline with interpolated cut endpoints.
 * @param[in] pts Borrowed finite Cartesian path, unchanged; zero-length segments skipped.
 * @param[in] s0 Finite start distance in path XY units, intended 0<=s0<s1.
 * @param[in] s1 Finite end distance within total path length, intended bounds not validated.
 * @return Owned interval points, possibly empty; exact original cut vertices retained and
 * duplicate output neighbors suppressed. No geodesic transform, source mutation/retention or IO;
 * allocates output.
 *
 * @details Portion of \p pts between arc lengths s0 < s1, endpoints interpolated (the same
 * arithmetic for the same arguments, so pieces cut at one arc length share their end point bit
 * for bit).
 */
QVector<QPointF> subPolyline(const QVector<QPointF> &pts, double s0, double s1)
{
    QVector<QPointF> out;
    double arc = 0.0;
    for (int k = 0; k + 1 < pts.size(); ++k) {
        const double len = std::hypot(pts[k + 1].x() - pts[k].x(), pts[k + 1].y() - pts[k].y());
        const double e0 = arc, e1 = arc + len;
        arc = e1;
        // A cut exactly at a vertex starts on that vertex (the piece ending
        // there ends on it too), never on an interpolation of it.
        if (!(len > 0.0) || e1 <= s0)
            continue;
        if (e0 > s1)
            break;
        if (out.isEmpty())
            out.append(s0 <= e0 ? pts[k] : pts[k] + (pts[k + 1] - pts[k]) * ((s0 - e0) / len));
        if (e1 < s1) {
            if (out.last() != pts[k + 1])
                out.append(pts[k + 1]);
        } else {
            const QPointF q =
                s1 >= e1 ? pts[k + 1] : pts[k] + (pts[k + 1] - pts[k]) * ((s1 - e0) / len);
            if (out.last() != q)
                out.append(q);
            break;
        }
    }
    return out;
}

/**
 * @brief Check a conduit end path remains outside and clear of its neighboring strip outline.
 * @param[in] piece Borrowed finite end polyline in common XY frame, unchanged.
 * @param[in] outline Borrowed simple strip polygon, unchanged.
 * @param[in] capA Borrowed first prescribed cap-center point, equality permits endpoint
 * shortening.
 * @param[in] capB Borrowed second cap center, unchanged.
 * @param[in] clear Positive finite XY clearance; touched cap portions shorten by
 * min(clear/length,0.5).
 * @return False for tested interior midpoint or boundary distance below half-clearance, true
 * otherwise, including no nonzero segments. Ordinary point/segment arithmetic, not full robust
 * path containment. Allocates ring copy, no source mutation/retained arguments or IO.
 *
 * @details A conduit's plain end beside its strip: outside the outline and at least half of \p
 * clear from it, except where it meets the strip at a cap centre.
 */
bool endClearOf(const QVector<QPointF> &piece, const QPolygonF &outline, const QPointF &capA,
                const QPointF &capB, double clear)
{
    const QVector<QPointF> ring(outline.begin(), outline.end());
    for (int k = 0; k + 1 < piece.size(); ++k) {
        const QPointF a0 = piece[k], b0 = piece[k + 1];
        const double len = std::hypot(b0.x() - a0.x(), b0.y() - a0.y());
        if (!(len > 0.0))
            continue;
        // Shorten the segment that touches a cap by the clearance at that end.
        const double t = std::min(clear / len, 0.5);
        QPointF a = a0, b = b0;
        if (a0 == capA || a0 == capB)
            a = a0 + (b0 - a0) * t;
        if (b0 == capA || b0 == capB)
            b = b0 + (a0 - b0) * t;
        if (pointInRing(ring, 0.5 * (a + b)))
            return false;
        // Half the clearance: the shortened end itself lies one clearance out
        // from the cap along a straight line.
        for (int e = 0; e < outline.size(); ++e)
            if (SegmentGrid::segSegDist2(a, b, outline[e], outline[(e + 1) % outline.size()]) <
                0.25 * clear * clear)
                return false;
    }
    return true;
}

/*! Whether a generated quad strip fits (MESH_TRIANGLE_ENGINE_PLAN D12): its
 *  ring inside the domain and outside holes, every edge at least \p clear
 *  from every other constraint and accepted strip, and no node, strip or
 *  strip corner inside it. The feature it replaces is ignored by owner. */
class StripChecker
{
public:
    /**
     * @brief Index borrowed immutable domain/hole rings and node points for generated-strip
     * placement.
     * @param[in] domainRings Borrowed outer ring arrays, must outlive checker and remain
     * immutable.
     * @param[in] holeRings Borrowed excluded ring arrays, same lifetime/frame prerequisites.
     * @param[in] steiners Borrowed node array stored by reference, must outlive checker/queries.
     * @param[in] clear Positive finite intended XY clearance; grid pitch max(4*clear,1e-9). Owns
     * local ring-index metadata/grid, no source geometry ownership transfer or validation. Later
     * addPoly/addRing populate obstacles; no engine/file access, excludes concurrent
     * mutation/query.
     */
    StripChecker(const QVector<QVector<QPointF>> &domainRings,
                 const QVector<QVector<QPointF>> &holeRings, const QVector<SteinerPoint> &steiners,
                 double clear)
        : m_domainIndex(ringPointers(domainRings)), m_holeIndex(ringPointers(holeRings)),
          m_steiners(steiners), m_clear(clear)
    {
        m_grid.build({}, std::max(4.0 * clear, 1e-9));
    }
    /**
     * @brief Append a prepared polyline's segment copies to strip-clearance obstacles.
     * @param[in] p Borrowed finite Poly, unchanged; closed wraps, open uses consecutive
     * segments.
     * @param[in] owner Caller identity for later ignore filtering, copied without validation.
     * Requires representable cell-length subdivision; may allocate, no source retention/IO or
     * ring registration.
     */
    void addPoly(const Poly &p, int owner)
    {
        const int n = p.pts.size();
        const int edges = p.closed ? n : n - 1;
        for (int k = 0; k < edges; ++k)
            m_grid.addSplit(p.pts[k], p.pts[(k + 1) % n], owner);
    }
    /**
     * @brief Register an accepted strip perimeter for boundary clearance and interior overlap
     * checks.
     * @param[in] ring Borrowed nonempty finite simple polygon, copied; implicit closure used.
     * Owns ring copy and indexed pieces with reserved owner -3. No full geometry validation,
     * source retention, coordinate conversion, IO or concurrent mutation/query support.
     */
    void addRing(const QPolygonF &ring)
    {
        for (int k = 0; k < ring.size(); ++k)
            m_grid.addSplit(ring[k], ring[(k + 1) % ring.size()], -3);
        m_rings.append(ring);
    }
    /**
     * @brief Diagnose generated-strip incompatibility with domain, holes, constraints, nodes or
     * earlier strips.
     * @param[in] ring Borrowed nonempty simple finite candidate polygon, unchanged; caller
     * validates shape first.
     * @param[in] ignore Obstacle owner ID of replaced feature to omit from clearance/interior
     * checks.
     * @return Empty when tested placement fits, first owned human-readable reason otherwise.
     * Tests vertices in domain/outside holes, every edge clearance, enclosed nodes/constraint
     * endpoints and earlier strip overlap. Inherits ordinary boundary/clearance tolerances; not
     * a complete robust geometry proof. Allocates temporary ring/candidate work through helpers,
     * no indexed-state mutation or retained inputs.
     */
    [[nodiscard]] QString misfit(const QPolygonF &ring, int ignore) const
    {
        for (const QPointF &q : ring) {
            const bool in = m_domainIndex.anyContains(q) && !m_holeIndex.anyContains(q);
            if (!in)
                return QStringLiteral(
                    "it reaches the edge of the meshing domain or a hole (keep it inside, "
                    "a minimum cell size away)");
        }
        for (int k = 0; k < ring.size(); ++k)
            if (m_grid.segmentNear(ring[k], ring[(k + 1) % ring.size()], m_clear, ignore))
                return QStringLiteral(
                    "it crosses or crowds another constraint or strip (keep a minimum cell size apart)");
        const QVector<QPointF> open(ring.begin(), ring.end());
        const QRectF box = ring.boundingRect();
        for (const SteinerPoint &sp : m_steiners)
            if (box.contains(sp.xy) && pointInRing(open, sp.xy))
                return QStringLiteral("a node lies inside it");
        // A constraint wholly inside the ring would lose its edges to the
        // strip (or overlap it, if closed).
        bool inside = false;
        m_grid.visitBox(box, [&](const QPointF &a, const QPointF &b, int owner) {
            if (inside || owner == ignore || owner == -3)
                return;
            inside = (box.contains(a) && pointInRing(open, a)) ||
                     (box.contains(b) && pointInRing(open, b));
        });
        if (inside)
            return QStringLiteral("another constraint lies inside it");
        for (const QPolygonF &r : m_rings)
            if (r.boundingRect().intersects(box) &&
                (pointInRing(open, r.first()) ||
                 pointInRing(QVector<QPointF>(r.begin(), r.end()), ring.first())))
                return QStringLiteral("it overlaps another strip");
        return {};
    }

private:
    RingIndex m_domainIndex, m_holeIndex;
    const QVector<SteinerPoint> &m_steiners;
    double m_clear;
    SegmentGrid m_grid;
    QVector<QPolygonF> m_rings;
};

/**
 * @brief Append each valid patch boundary pair as an unresampled fixed Poly constraint.
 * @param[in] pm Borrowed patch, unchanged; invalid endpoint indices skip, zero-length geometry
 * not audited.
 * @param[in] barrier Set whether appended edges appear as boundary/tag flood barriers; geometry
 * is always constrained/fixed regardless of this reporting flag.
 * @param[in,out] polys Required nonnull borrowed output vector, appended without clearing or
 * retention. No patch tag/marker propagation here, source mutation or IO; append copies
 * endpoints and may allocate.
 *
 * @details A patch's boundary segments as fixed polylines (one per segment: the stations stay
 * exactly where the patch put them).
 */
void appendPatchBoundary(const PatchMesh &pm, bool barrier, QVector<Poly> *polys)
{
    for (const auto &seg : pm.boundarySegments) {
        if (seg.first < 0 || seg.first >= pm.xy.size() || seg.second < 0 ||
            seg.second >= pm.xy.size())
            continue;
        Poly p;
        p.pts = {pm.xy[seg.first], pm.xy[seg.second]};
        p.resample = false;
        p.fixed = true;
        p.barrier = barrier;
        polys->append(p);
    }
}

/*! What joinConstraints did. */
struct JoinReport {
    int merged = 0, snapped = 0, crossings = 0;
};

/**
 * @brief Merge near ordinary vertices and insert shared near-segment/crossing points into
 * prepared lines.
 * @param[in,out] polys Borrowed mutable constraints, changed/appended in place; excludes
 * fixed/terrain/ dropped/<2-point paths. Domain vertices are pinned during merge but domain
 * segments can gain cuts. Open strip-request paths split into runs at inserted cuts; attributes
 * copied to appended pieces.
 * @param[in] anchors Borrowed finite fixed point coordinates, unchanged, initial merge
 * representatives.
 * @param[in] tol Positive finite XY join radius with int64-safe cell quotients; <=0/NaN no-op
 * report.
 * @return Counts of merges/snap cuts/proper crossings, not unique final geometry edits. Moves
 * ordinary vertices less than tol, inserts nearby point coordinates as kinks, exact orientation
 * rejects false proper crossings, near crossing locations coalesce. Collinear overlaps handled
 * only via prior point cuts, not full overlay. No transactional rollback/cancellation/memory
 * cap, engine/file access or retained references; allocates indexes/cut arrays and can increase
 * input size.
 *
 * @details Join constraint lines that meet or nearly meet, so the triangulation sees shared
 * vertices instead of crossings it cannot recover or features closer than the refinement floor
 * (which leave cells no refinement can fix): 1. a vertex within \p tol of another moves onto it
 * (anchors — pinned points — and domain vertices never move; line ends settle before interior
 * vertices); 2. a vertex within \p tol of a segment is inserted into it; 3. segments that cross
 * get the crossing point (crossings within \p tol of each other are one point). Geometry moves
 * by less than \p tol. Fixed patch edges and terrain lines are left alone (patch placement and
 * the line preparation keep those clear). A line with a strip width is cut into runs at the
 * points put into it, so its strip stops short of the other line as at a junction instead of
 * being dropped for containing it.
 */
JoinReport joinConstraints(QVector<Poly> &polys, const QVector<QPointF> &anchors, double tol,
                           bool preserveCoordinates)
{
    JoinReport rep;
    if (!(tol > 0.0))
        return rep;
    auto joins = [](const Poly &p) {
        return !p.fixed && !p.isTerrain && !p.dropped && p.pts.size() >= 2;
    };

    // ── 1. Vertices closer than tol become one ───────────────────────────
    QHash<QPair<qint64, qint64>, QVector<QPointF>> reps;
    auto keyOf = [tol](const QPointF &p) {
        return qMakePair(qint64(std::floor(p.x() / tol)), qint64(std::floor(p.y() / tol)));
    };
    auto settle = [&](QPointF &v, bool canMove) {
        canMove = canMove && !preserveCoordinates;
        const auto k = keyOf(v);
        double best = tol;
        const QPointF *to = nullptr;
        for (qint64 dy = -1; dy <= 1; ++dy)
            for (qint64 dx = -1; dx <= 1; ++dx) {
                const auto it = reps.constFind({k.first + dx, k.second + dy});
                if (it == reps.constEnd())
                    continue;
                for (const QPointF &q : it.value()) {
                    if (q == v)
                        return;
                    const double d = QLineF(v, q).length();
                    if (d < best) {
                        best = d;
                        to = &q;
                    }
                }
            }
        if (to && canMove) {
            v = *to;
            ++rep.merged;
        } else
            reps[k].append(v);
    };
    for (QPointF a : anchors)
        settle(a, false);
    for (Poly &p : polys)
        if (joins(p) && p.isDomain)
            for (QPointF &v : p.pts)
                settle(v, false);
    QVector<QVector<QPointF>> before(polys.size());
    for (int i = 0; i < polys.size(); ++i)
        if (joins(polys[i]) && !polys[i].isDomain)
            before[i] = polys[i].pts;
    for (Poly &p : polys)
        if (joins(p) && !p.isDomain && !p.closed) {
            settle(p.pts.first(), true);
            settle(p.pts.last(), true);
        }
    for (Poly &p : polys)
        if (joins(p) && !p.isDomain)
            for (int k = 0; k < p.pts.size(); ++k)
                if (p.closed || (k > 0 && k + 1 < p.pts.size()))
                    settle(p.pts[k], true);
    for (int i = 0; i < polys.size(); ++i) {
        if (before[i].isEmpty() || before[i] == polys[i].pts)
            continue;
        Poly &p = polys[i];
        QVector<QPointF> dd;
        for (const QPointF &q : std::as_const(p.pts))
            if (dd.isEmpty() || dd.last() != q)
                dd.append(q);
        if (p.closed && dd.size() > 1 && dd.first() == dd.last())
            dd.removeLast();
        // A line shorter than tol keeps its own vertices (and its slivers).
        p.pts = dd.size() >= (p.closed ? 3 : 2) ? dd : before[i];
    }

    // Segments of the joining lines in a grid of cells at least 4·tol wide;
    // a segment is stamped piece by piece so a long one indexes only the
    // cells along it.
    struct Seg {
        int poly, k;
        QPointF a, b;
    };
    QVector<Seg> segs;
    QHash<QPair<qint64, qint64>, QVector<int>> grid;
    double cell = 0.0;
    auto cellOf = [&cell](double v) { return qint64(std::floor(v / cell)); };
    auto index = [&]() {
        segs.clear();
        grid.clear();
        double total = 0.0;
        for (int i = 0; i < polys.size(); ++i) {
            const Poly &p = polys[i];
            if (!joins(p))
                continue;
            const int n = p.pts.size(), edges = p.closed ? n : n - 1;
            for (int k = 0; k < edges; ++k) {
                segs.append({i, k, p.pts[k], p.pts[(k + 1) % n]});
                total += QLineF(segs.last().a, segs.last().b).length();
            }
        }
        cell = std::max({4.0 * tol, total / 200000.0, 1e-12});
        for (int s = 0; s < segs.size(); ++s) {
            const QPointF a = segs[s].a, b = segs[s].b;
            const int n = std::max(1, int(std::ceil(QLineF(a, b).length() / cell)));
            for (int j = 0; j < n; ++j) {
                const QPointF u = a + (b - a) * (double(j) / n),
                              w = a + (b - a) * (double(j + 1) / n);
                for (qint64 y = cellOf(std::min(u.y(), w.y())); y <= cellOf(std::max(u.y(), w.y()));
                     ++y)
                    for (qint64 x = cellOf(std::min(u.x(), w.x()));
                         x <= cellOf(std::max(u.x(), w.x())); ++x) {
                        QVector<int> &v = grid[qMakePair(x, y)];
                        if (v.isEmpty() || v.last() != s)
                            v.append(s);
                    }
            }
        }
    };
    // Insert the cut points (sorted along each segment) into their lines.
    using Cuts = QHash<QPair<int, int>, QVector<QPair<double, QPointF>>>;
    auto apply = [&](const Cuts &cuts) {
        const int count = polys.size();
        for (int i = 0; i < count; ++i) {
            if (!joins(polys[i]))
                continue;
            const QVector<QPointF> pts = polys[i].pts;
            const int n = pts.size(), edges = polys[i].closed ? n : n - 1;
            QVector<QPointF> out;
            QVector<int> at; // indices in out of the points put in
            for (int k = 0; k < n; ++k) {
                out.append(pts[k]);
                if (k >= edges)
                    continue;
                auto c = cuts.value({i, k});
                if (c.isEmpty())
                    continue;
                std::sort(c.begin(), c.end(),
                          [](const auto &l, const auto &r) { return l.first < r.first; });
                for (const auto &tp : c)
                    if (out.last() != tp.second && tp.second != pts[(k + 1) % n]) {
                        at.append(out.size());
                        out.append(tp.second);
                    }
            }
            if (at.isEmpty())
                continue;
            polys[i].pts = out;
            if (polys[i].closed || !(polys[i].stripWidth > 0.0))
                continue;
            int from = 0;
            at.append(out.size() - 1);
            for (int j = 0; j < at.size(); ++j) {
                const QVector<QPointF> piece = out.mid(from, at[j] - from + 1);
                if (j == 0)
                    polys[i].pts = piece;
                else {
                    Poly q = polys[i];
                    q.pts = piece;
                    polys.append(q);
                }
                from = at[j];
            }
        }
    };

    // ── 2. A vertex within tol of a segment goes into it ─────────────────
    index();
    {
        QVector<QPointF> points;
        QSet<QPair<double, double>> seen; // exact coordinates
        for (const QPointF &a : anchors)
            if (!seen.contains({a.x(), a.y()})) {
                seen.insert({a.x(), a.y()});
                points.append(a);
            }
        for (const Seg &sg : std::as_const(segs))
            for (const QPointF &q : {sg.a, sg.b})
                if (!seen.contains({q.x(), q.y()})) {
                    seen.insert({q.x(), q.y()});
                    points.append(q);
                }
        Cuts cuts;
        const double tol2 = tol * tol;
        for (const QPointF &v : std::as_const(points)) {
            QSet<int> tried;
            for (qint64 y = cellOf(v.y() - tol); y <= cellOf(v.y() + tol); ++y)
                for (qint64 x = cellOf(v.x() - tol); x <= cellOf(v.x() + tol); ++x) {
                    const auto it = grid.constFind(qMakePair(x, y));
                    if (it == grid.constEnd())
                        continue;
                    for (int s : it.value()) {
                        if (tried.contains(s))
                            continue;
                        tried.insert(s);
                        const Seg &sg = segs[s];
                        if (v == sg.a || v == sg.b)
                            continue;
                        const QPointF d = sg.b - sg.a;
                        const double len2 = d.x() * d.x() + d.y() * d.y();
                        if (!(len2 > 0.0))
                            continue;
                        const double t =
                            ((v.x() - sg.a.x()) * d.x() + (v.y() - sg.a.y()) * d.y()) / len2;
                        if (!(t > 0.0 && t < 1.0) ||
                            (preserveCoordinates
                                 ? ConstrainedDelaunay::orientExact(sg.a, sg.b, v) != 0
                                 : pslg::distSqToSegment(v, sg.a, sg.b) >= tol2))
                            continue;
                        cuts[{sg.poly, sg.k}].append({t, v});
                        ++rep.snapped;
                    }
                }
        }
        if (!cuts.isEmpty())
            apply(cuts);
    }

    // ── 3. Crossings get a shared vertex ─────────────────────────────────
    index();
    {
        auto cross = [](const QPointF &u, const QPointF &v) {
            return u.x() * v.y() - u.y() * v.x();
        };
        Cuts cuts;
        // Three lines through one point give three crossings a rounding
        // error apart; they become one vertex rather than a cluster.
        QHash<QPair<qint64, qint64>, QPointF> crossPts;
        auto snapped = [&](const QPointF &p) {
            if (preserveCoordinates)
                return p;
            const auto k = keyOf(p);
            for (qint64 dy = -1; dy <= 1; ++dy)
                for (qint64 dx = -1; dx <= 1; ++dx) {
                    const auto it = crossPts.constFind({k.first + dx, k.second + dy});
                    if (it != crossPts.constEnd() && QLineF(it.value(), p).length() < tol)
                        return it.value();
                }
            crossPts.insert(k, p);
            return p;
        };
        QSet<QPair<int, int>> tested;
        for (auto it = grid.cbegin(); it != grid.cend(); ++it) {
            const QVector<int> &ids = it.value();
            for (int x = 0; x < ids.size(); ++x)
                for (int y = x + 1; y < ids.size(); ++y) {
                    const int s1 = std::min(ids[x], ids[y]), s2 = std::max(ids[x], ids[y]);
                    if (tested.contains({s1, s2}))
                        continue;
                    tested.insert({s1, s2});
                    const Seg &A = segs[s1], &B = segs[s2];
                    const QPointF r = A.b - A.a, q = B.b - B.a;
                    const double den = cross(r, q);
                    if (den == 0.0)
                        continue;
                    // Decide the crossing exactly: two collinear pieces of
                    // one straight line have a rounding-noise den, and t, u
                    // can then land inside (0, 1) for segments metres apart.
                    const auto straddles = [](const QPointF &a, const QPointF &b, const QPointF &c,
                                              const QPointF &d) {
                        const double oc = ConstrainedDelaunay::orientExact(a, b, c),
                                     od = ConstrainedDelaunay::orientExact(a, b, d);
                        return (oc > 0.0 && od < 0.0) || (oc < 0.0 && od > 0.0);
                    };
                    if (!straddles(A.a, A.b, B.a, B.b) || !straddles(B.a, B.b, A.a, A.b))
                        continue;
                    const double t = cross(B.a - A.a, q) / den, u = cross(B.a - A.a, r) / den;
                    // Proper crossings only: shared ends and touches are not split.
                    if (!(t > 1e-9 && t < 1.0 - 1e-9 && u > 1e-9 && u < 1.0 - 1e-9))
                        continue;
                    const QPointF p = snapped(A.a + r * t);
                    cuts[{A.poly, A.k}].append({t, p});
                    cuts[{B.poly, B.k}].append({u, p});
                    ++rep.crossings;
                }
        }
        if (!cuts.isEmpty())
            apply(cuts);
    }
    return rep;
}

/**
 * @brief Compute ordinary signed shoelace area of an implicit-closure ring.
 * @param[in] r Borrowed finite points in common XY frame, unchanged.
 * @return Signed squared-unit area, positive CCW, zero empty. Absolute-coordinate products can
 * lose small-area precision at large CRS offsets; no robust/local-origin correction, allocation
 * or mutation.
 */
double ringArea(const QVector<QPointF> &r)
{
    double a = 0.0;
    for (int i = 0, j = r.size() - 1; i < r.size(); j = i++)
        a += r[j].x() * r[i].y() - r[i].x() * r[j].y();
    return 0.5 * a;
}

/**
 * @brief Accumulate normalized domain, feature, region and fixed-patch constraints plus hole
 * ownership.
 * @param[in] domains Borrowed outer rings, unchanged; normalized rings with >=3 points appended.
 * @param[in] segments Borrowed polylines, unchanged; closed loops recognized with repeated first
 * point.
 * @param[in] regionRings Borrowed open normalized rings, same count/order as regions, unchanged.
 * @param[in] regions Borrowed quad-region metadata, unchanged; background rings do not add
 * constraints.
 * @param[in] patches Borrowed valid patches, unchanged; boundary pairs become fixed barrier
 * lines.
 * @param[in] holes Borrowed finite seeds, unchanged; innermost containing closed nondomain Poly
 * becomes hole.
 * @param[in,out] polys Required nonnull borrowed output, appended and hole flags modified, not
 * cleared.
 * @param[in,out] domainRings Required distinct nonnull output, appended normalized domain rings.
 * @param[in,out] holeRings Required distinct nonnull output, appends identified hole rings; all
 * outputs must not alias source geometry or invalidate it during accumulation, no pointers
 * retained.
 * @return False when domainRings remains empty, true otherwise. Failure can leave partial
 * output; existing output contents participate. No finite/simplicity/overlap/identity audit,
 * source mutation beyond explicit outputs, cancellation or IO; allocates copies/nonowning local
 * indexes.
 *
 * @details The constraint polylines generation starts from: domain rings, constraint segments,
 * quad region rings and patch boundaries. A hole seed makes the innermost closed ring around it
 * a hole (an enclosing region or subcatchment ring stays a region). False when no domain ring
 * has three or more vertices.
 */
bool buildConstraintPolys(const QVector<QPolygonF> &domains,
                          const QVector<ConstraintSegment> &segments,
                          const QVector<QVector<QPointF>> &regionRings,
                          const QVector<QuadRegion> &regions, const QVector<PatchMesh> &patches,
                          const QVector<QPointF> &holes, QVector<Poly> *polys,
                          QVector<QVector<QPointF>> *domainRings,
                          QVector<QVector<QPointF>> *holeRings, bool exactCoordinates = false)
{
    const auto same = [exactCoordinates](QPointF a, QPointF b) {
        return exactCoordinates ? a.x() == b.x() && a.y() == b.y() : a == b;
    };
    for (const QPolygonF &dom : domains) {
        Poly p;
        p.pts = openRing(QVector<QPointF>(dom.begin(), dom.end()), exactCoordinates);
        if (p.pts.size() < 3)
            continue;
        p.marker = kBoundaryMarker;
        p.closed = true;
        p.isDomain = true;
        domainRings->append(p.pts);
        polys->append(p);
    }
    if (domainRings->isEmpty())
        return false;
    for (const ConstraintSegment &cs : segments) {
        Poly p;
        const bool closed = cs.path.size() >= 4 && same(cs.path.first(), cs.path.last());
        p.pts = closed ? openRing(cs.path, exactCoordinates) : cs.path;
        if (!closed) {
            QVector<QPointF> dd;
            for (const QPointF &q : p.pts)
                if (dd.isEmpty() || !same(dd.last(), q))
                    dd.append(q);
            p.pts = dd;
        }
        if (p.pts.size() < 2)
            continue;
        p.marker = cs.marker;
        p.tag = cs.tag;
        p.closed = closed;
        p.stripWidth = closed ? 0.0 : cs.stripWidth;
        polys->append(p);
    }
    for (int i = 0; i < regionRings.size(); ++i) {
        if (regionRings[i].size() < 3 || regions[i].isBackground)
            continue;
        Poly p;
        p.pts = regionRings[i];
        p.closed = true;
        p.regionIndex = i;
        polys->append(p);
    }
    for (const PatchMesh &pm : patches)
        appendPatchBoundary(pm, true, polys);
    QVector<const QVector<QPointF> *> closedRings(polys->size(), nullptr);
    for (int i = 0; i < polys->size(); ++i)
        if (polys->at(i).closed && !polys->at(i).isDomain)
            closedRings[i] = &polys->at(i).pts;
    QVector<int> holePolys;
    {
        const QVector<Poly> &all = *polys;
        const RingIndex closedIndex(closedRings);
        for (const QPointF &seed : holes) {
            int best = -1;
            double bestArea = std::numeric_limits<double>::infinity();
            closedIndex.visitContaining(seed, [&](int i) {
                const double a = std::abs(ringArea(all[i].pts));
                if (a < bestArea) {
                    bestArea = a;
                    best = i;
                }
                return true;
            });
            if (best >= 0)
                holePolys.append(best);
        }
    }
    // Marked after the index is gone: it points into *polys.
    for (int i : std::as_const(holePolys))
        (*polys)[i].isHole = true;
    for (const Poly &p : std::as_const(*polys))
        if (p.isHole)
            holeRings->append(p.pts);
    return true;
}

} // namespace

QVector<QVector<QPointF>> MeshGenerator::previewTerrainBreaklines() const
{
    if (m_terrainLines.isEmpty() || !(m_opts.minCellSize > 0.0) || m_domains.isEmpty())
        return m_terrainLines;
    QVector<QVector<QPointF>> regionRings(m_quadRegions.size());
    for (int i = 0; i < m_quadRegions.size(); ++i)
        regionRings[i] =
            openRing(QVector<QPointF>(m_quadRegions[i].ring.begin(), m_quadRegions[i].ring.end()), m_opts.preserveConstraintCoordinates);
    QVector<Poly> polys;
    QVector<QVector<QPointF>> domainRings, holeRings;
    if (!buildConstraintPolys(m_domains, m_segments, regionRings, m_quadRegions, m_patches, m_holes,
                              &polys, &domainRings, &holeRings, m_opts.preserveConstraintCoordinates))
        return m_terrainLines;
    if (!m_opts.allowQuads)
        for (auto &poly : polys)
            poly.stripWidth = 0.0;
    QVector<QVector<QPointF>> excluded = holeRings;
    for (const PatchMesh &pm : m_patches) {
        const QPolygonF ring = orderedPatchBoundary(pm);
        if (ring.size() >= 3)
            excluded.append(QVector<QPointF>(ring.begin(), ring.end()));
    }
    QVector<QVector<QPointF>> out;
    for (const Poly &p :
         prepareTerrainLines(m_terrainLines, polys, domainRings, excluded, m_steiners,
                             m_opts.minCellSize, {}, m_opts.terrainMinLineLength)) {
        QVector<QPointF> line = p.pts;
        if (p.closed)
            line.append(line.first());
        out.append(line);
    }
    return out;
}

// ── Generation ─────────────────────────────────────────────────────────────

MeshResult MeshGenerator::generate() const
{
    m_leakedPatches.clear();
    for (int attempt = 0;; ++attempt) {
        QPair<qint64, qint64> leaked{std::numeric_limits<qint64>::min(), 0};
        MeshResult r = generateOnce(&leaked);
        if (leaked.first == std::numeric_limits<qint64>::min() || attempt >= 64)
            return r;
        m_leakedPatches.insert(leaked);
        GeneratorDiagnostic(m_refineHook.onDiagnostic).noquote()
            << QStringLiteral(
                   "[Mesh] a structured patch's ring is not watertight; regenerating with it "
                   "as triangles (%1 excluded so far)")
                   .arg(m_leakedPatches.size());
    }
}

MeshResult MeshGenerator::generateOnce(QPair<qint64, qint64> *leaked) const
{
    MeshResult result;
    m_acceptedTerrainLines.clear();
    m_vertexTagByMarker.clear();
    m_edgeTagByMarker.clear();
    m_triangleTagByRegionId.clear();
    m_quadReports.clear();
    m_stats = GenerationStats();

    auto fail = [&](const QString &msg) {
        result = MeshResult();
        result.ok = false;
        result.errorMsg = msg;
        return result;
    };
    auto cancelled = [&]() { return m_refineHook.isCancelled && m_refineHook.isCancelled(); };
    auto progress = [&](qint64 n) {
        if (m_refineHook.onProgress)
            m_refineHook.onProgress(n);
    };

    if (m_domains.isEmpty())
        return fail(QStringLiteral("MeshGenerator: domain is empty."));
    if (!m_opts.allowQuads)
        for (const PatchMesh &patch : m_patches)
            if (!patch.quads.isEmpty())
                return fail(QStringLiteral(
                    "MeshGenerator: triangles-only mode conflicts with an explicit quad patch. "
                    "Remove the patch or enable quadrilaterals."));

    // ── Finite input check ───────────────────────────────────────────────
    {
        const auto isFinitePt = [](const QPointF &p) {
            return std::isfinite(p.x()) && std::isfinite(p.y());
        };
        qsizetype nBad = 0;
        for (const QPolygonF &dom : m_domains)
            for (const QPointF &p : dom)
                if (!isFinitePt(p))
                    ++nBad;
        for (const SteinerPoint &sp : m_steiners)
            if (!isFinitePt(sp.xy))
                ++nBad;
        for (const ConstraintSegment &cs : m_segments)
            for (const QPointF &p : cs.path)
                if (!isFinitePt(p))
                    ++nBad;
        for (const QPointF &h : m_holes)
            if (!isFinitePt(h))
                ++nBad;
        for (const RegionMarker &rm : m_regions)
            if (!isFinitePt(rm.xy))
                ++nBad;
        for (const PatchMesh &pm : m_patches)
            for (const QPointF &p : pm.xy)
                if (!isFinitePt(p))
                    ++nBad;
        for (const QuadRegion &qr : m_quadRegions)
            for (const QPointF &p : qr.ring)
                if (!isFinitePt(p))
                    ++nBad;
        if (nBad > 0)
            return fail(
                QStringLiteral(
                    "MeshGenerator: %1 input coordinate(s) are not finite (NaN or infinite), so meshing "
                    "was not attempted. The usual sources are DTM NoData or out-of-footprint samples "
                    "reaching the point set, and failed coordinate reprojection.")
                    .arg(nBad));
    }

    // ── Tag tables ───────────────────────────────────────────────────────
    for (const SteinerPoint &sp : m_steiners)
        if (sp.marker != 0 && !sp.tag.isEmpty())
            m_vertexTagByMarker.insert(sp.marker, sp.tag);
    for (const ConstraintSegment &cs : m_segments)
        if (cs.marker != 0 && !cs.tag.isEmpty())
            m_edgeTagByMarker.insert(cs.marker, cs.tag);
    for (const RegionMarker &rm : m_regions)
        m_triangleTagByRegionId.insert(int(rm.attribute), rm.tag);

    // ── Size function ────────────────────────────────────────────────────
    const double hUniform = m_opts.maxArea > 0.0 ? std::sqrt(m_opts.maxArea / kEquilateral) : 0.0;
    const bool haveHook = bool(m_refineHook.targetAreaAt);

    QVector<QPair<QVector<QPointF>, double>> spacingOverrides; // quad regions with a spacing
    QVector<QRectF> spacingOverrideBounds;                     // quick reject for hAt
    QVector<QVector<QPointF>> regionRings(m_quadRegions.size());
    for (int i = 0; i < m_quadRegions.size(); ++i) {
        regionRings[i] =
            openRing(QVector<QPointF>(m_quadRegions[i].ring.begin(), m_quadRegions[i].ring.end()), m_opts.preserveConstraintCoordinates);
        if (m_quadRegions[i].spacing > 0.0 && regionRings[i].size() >= 3) {
            spacingOverrides.append(qMakePair(regionRings[i], m_quadRegions[i].spacing));
            spacingOverrideBounds.append(QPolygonF(regionRings[i]).boundingRect());
        }
    }
    // Bucket the override boxes so a size query (millions during seeding and
    // refinement) tests only the few regions near it, not all of them.
    QRectF overrideExtent;
    for (const QRectF &b : std::as_const(spacingOverrideBounds))
        overrideExtent = overrideExtent.isNull() ? b : overrideExtent.united(b);
    const int overrideSide =
        spacingOverrides.isEmpty()
            ? 0
            : std::clamp(int(std::sqrt(double(spacingOverrides.size())) * 2), 1, 512);
    QVector<QVector<int>> overrideGrid(overrideSide * overrideSide);
    const auto overrideCell = [&](double v, double lo, double span) {
        return span > 0 ? std::clamp(int((v - lo) / span * overrideSide), 0, overrideSide - 1) : 0;
    };
    for (int k = 0; k < spacingOverrideBounds.size(); ++k) {
        const QRectF &b = spacingOverrideBounds[k];
        for (int y = overrideCell(b.top(), overrideExtent.top(), overrideExtent.height());
             y <= overrideCell(b.bottom(), overrideExtent.top(), overrideExtent.height()); ++y)
            for (int x = overrideCell(b.left(), overrideExtent.left(), overrideExtent.width());
                 x <= overrideCell(b.right(), overrideExtent.left(), overrideExtent.width()); ++x)
                overrideGrid[y * overrideSide + x].append(k);
    }
    const std::function<double(double, double)> hAt = [&](double x, double y) {
        double h = hUniform;
        if (haveHook) {
            const double a = m_refineHook.targetAreaAt(x, y);
            h = std::isfinite(a) && a > 0.0 ? std::sqrt(a / kEquilateral) : 0.0;
        }
        if (overrideSide == 0 || x < overrideExtent.left() || x > overrideExtent.right() ||
            y < overrideExtent.top() || y > overrideExtent.bottom())
            return h;
        for (const int k :
             overrideGrid[overrideCell(y, overrideExtent.top(), overrideExtent.height()) *
                              overrideSide +
                          overrideCell(x, overrideExtent.left(), overrideExtent.width())]) {
            const QRectF &b = spacingOverrideBounds[k];
            if (x < b.left() || x > b.right() || y < b.top() || y > b.bottom())
                continue;
            const auto &ov = spacingOverrides[k];
            if (pointInRing(ov.first, QPointF(x, y)))
                h = (h > 0.0) ? std::min(h, ov.second) : ov.second;
        }
        return h;
    };

    // ── Structured patch placement ───────────────────────────────────────
    QVector<QPolygonF> patchExclusionBoundaries; // hole rings, relative to patchOrigin
    const QPointF patchOrigin = m_domains.first().isEmpty() ? QPointF() : m_domains.first().first();
    if (!m_patches.isEmpty()) {
        if (!(std::isfinite(m_opts.patchSnapEps) && m_opts.patchSnapEps >= 0.0))
            return fail(QStringLiteral(
                "MeshGenerator: patch snap tolerance is invalid or too small for the coordinate span."));
        const QString err = validatePatchPlacement(m_patches, m_domains, m_holes, m_segments,
                                                   patchOrigin, &patchExclusionBoundaries);
        if (!err.isEmpty())
            return fail(err);
    }

    // ── Constraint polylines ─────────────────────────────────────────────
    QVector<Poly> polys;
    QVector<QVector<QPointF>> domainRings, holeRings;
    if (!buildConstraintPolys(m_domains, m_segments, regionRings, m_quadRegions, m_patches, m_holes,
                              &polys, &domainRings, &holeRings, m_opts.preserveConstraintCoordinates))
        return fail(QStringLiteral("MeshGenerator: no domain ring with 3 or more vertices."));
    if (!m_opts.allowQuads)
        for (auto &poly : polys)
            poly.stripWidth = 0.0;

    // ── Floor size ───────────────────────────────────────────────────────
    double hMin = m_opts.minCellSize > 0.0 ? m_opts.minCellSize : 0.0;
    if (hMin <= 0.0) {
        double hs = std::numeric_limits<double>::infinity();
        int sampled = 0;
        for (const Poly &p : polys)
            for (const QPointF &q : p.pts) {
                if (sampled++ > 20000)
                    break;
                const double h = hAt(q.x(), q.y());
                if (h > 0.0)
                    hs = std::min(hs, h);
            }
        for (const SteinerPoint &sp : m_steiners) {
            const double h = hAt(sp.xy.x(), sp.xy.y());
            if (h > 0.0)
                hs = std::min(hs, h);
        }
        if (!std::isfinite(hs))
            hs = hUniform > 0.0 ? hUniform : 1.0;
        hMin = 0.25 * hs;
    }
    const std::function<double(double, double)> hClamped = [&](double x, double y) {
        const double h = hAt(x, y);
        return h > 0.0 ? std::max(h, hMin) : h;
    };
    // Identity of a patch across regeneration attempts: its outline centroid.
    auto patchKey = [&](const QPolygonF &outline) {
        double cx = 0, cy = 0;
        for (const QPointF &q : outline) {
            cx += q.x();
            cy += q.y();
        }
        const double n = std::max<qsizetype>(1, outline.size()),
                     quantum = std::max(1e-9, 1e-6 * std::max(hMin, 1e-6));
        return qMakePair(qint64(std::llround(cx / n / quantum)),
                         qint64(std::llround(cy / n / quantum)));
    };
    // Lines that cross or come closer than the refinement floor share
    // vertices from here on (pipes crossing in plan, a node beside its pipe,
    // two alignments through one manhole digitised apart).
    {
        QVector<QPointF> anchors;
        anchors.reserve(m_steiners.size());
        for (const SteinerPoint &sp : m_steiners)
            anchors.append(sp.xy);
        double tol = kRefineFloor * hMin;
        // Channel section edges carry prescribed elevation breaks. Generic
        // proximity cleanup must not weld a narrow bank crest to its toe.
        if (m_opts.channelSpacing > 0.0)
            tol = std::min(tol, 0.1 * m_opts.channelSpacing);
        else
            for (const auto &poly : polys)
                if (poly.tag.startsWith(QLatin1String("channel:")))
                    for (int i = 1; i < poly.pts.size(); ++i) {
                        const double length = QLineF(poly.pts[i - 1], poly.pts[i]).length();
                        if (length > 0)
                            tol = std::min(tol, length * 0.1);
                    }
        const JoinReport jr =
            joinConstraints(polys, anchors, tol, m_opts.preserveConstraintCoordinates);
        if (jr.merged + jr.snapped + jr.crossings > 0)
            GeneratorDiagnostic(m_refineHook.onDiagnostic).noquote()
                << QStringLiteral(
                       "[Mesh] constraint lines joined within %1: %2 vertices merged, %3 put on a "
                       "line beside them, %4 crossings split")
                       .arg(tol)
                       .arg(jr.merged)
                       .arg(jr.snapped)
                       .arg(jr.crossings);
    }
    // Joining can move a small hole's ring past its seed, and a seed outside
    // its ring would flood-remove the surrounding mesh. Re-seat every seed
    // that no hole ring contains inside the nearest hole ring.
    QVector<QPointF> holeSeeds = m_holes;
    {
        QVector<const QVector<QPointF> *> holeRingPtrs;
        for (const Poly &p : std::as_const(polys))
            if (p.isHole && !p.dropped && p.pts.size() >= 3)
                holeRingPtrs.append(&p.pts);
        const RingIndex holeIndex(holeRingPtrs);
        int reseated = 0;
        for (QPointF &seed : holeSeeds) {
            if (holeIndex.anyContains(seed))
                continue;
            const QVector<QPointF> *nearest = nullptr;
            double best = std::numeric_limits<double>::infinity();
            for (const QVector<QPointF> *ring : std::as_const(holeRingPtrs)) {
                const int n = ring->size();
                for (int k = 0; k < n; ++k) {
                    const double d =
                        std::sqrt(pslg::distSqToSegment(seed, (*ring)[k], (*ring)[(k + 1) % n]));
                    if (d < best) {
                        best = d;
                        nearest = ring;
                    }
                }
            }
            if (!nearest || best > 4.0 * kRefineFloor * hMin + 1e-9)
                continue;
            // Centroid of an ear: convex and empty of other ring vertices.
            const QVector<QPointF> &r = *nearest;
            const int n = r.size();
            double area2 = 0;
            for (int k = 0; k < n; ++k)
                area2 += r[k].x() * r[(k + 1) % n].y() - r[(k + 1) % n].x() * r[k].y();
            for (int k = 0; k < n; ++k) {
                const QPointF &a = r[(k + n - 1) % n], &b = r[k], &c = r[(k + 1) % n];
                const double turn =
                    (b.x() - a.x()) * (c.y() - b.y()) - (b.y() - a.y()) * (c.x() - b.x());
                if (!(turn * area2 > 0))
                    continue;
                const QPolygonF ear({a, b, c});
                bool empty = true;
                for (int j = 0; j < n && empty; ++j)
                    if (j != k && j != (k + 1) % n && j != (k + n - 1) % n &&
                        ear.containsPoint(r[j], Qt::OddEvenFill))
                        empty = false;
                if (!empty)
                    continue;
                seed = (a + b + c) / 3.0;
                ++reseated;
                break;
            }
        }
        if (reseated)
            GeneratorDiagnostic(m_refineHook.onDiagnostic).noquote()
                << QStringLiteral(
                       "[Mesh] %1 hole seed(s) re-seated inside their ring after joining")
                       .arg(reseated);
    }
    // A strip cannot turn a sharp corner (its inner side folds into slivers):
    // a line with a strip width is cut into runs where it turns by more than
    // kStripMaxTurnDeg, each run stopping short of the corner as at a junction.
    for (int pi = 0, count = int(polys.size()); pi < count; ++pi) {
        if (!(polys[pi].stripWidth > 0.0) || polys[pi].closed || polys[pi].dropped)
            continue;
        const QVector<QPointF> pts = polys[pi].pts;
        QVector<int> at;
        for (int k = 1; k + 1 < pts.size(); ++k) {
            const QPointF u = pts[k] - pts[k - 1], v = pts[k + 1] - pts[k];
            const double turn =
                std::atan2(std::abs(u.x() * v.y() - u.y() * v.x()), u.x() * v.x() + u.y() * v.y()) *
                180.0 / M_PI;
            if (turn > kStripMaxTurnDeg)
                at.append(k);
        }
        if (at.isEmpty())
            continue;
        at.append(pts.size() - 1);
        int from = 0;
        for (int j = 0; j < at.size(); ++j) {
            const QVector<QPointF> piece = pts.mid(from, at[j] - from + 1);
            if (j == 0)
                polys[pi].pts = piece;
            else {
                Poly q = polys[pi];
                q.pts = piece;
                polys.append(q);
            }
            from = at[j];
        }
    }

    // ── Quad strips (MESH_TRIANGLE_ENGINE_PLAN_2026-09-30.md D12) ─────────
    // User patches first (validated above), then the ones built here: four-
    // sided quad regions and conduit strips. A generated strip that does not
    // fit is dropped and its feature stays a triangle mesh.
    QVector<PatchMesh> patches = m_patches;
    QVector<QPolygonF> patchRings;
    QVector<ClearEdge> stripEdges; // every strip boundary edge, for the terrain lines
    auto addStripEdges = [&](const PatchMesh &pm) {
        for (const auto &seg : pm.boundarySegments) {
            const QPointF a = pm.xy.value(seg.first), b = pm.xy.value(seg.second);
            stripEdges.append({a, b, 0.75 * std::hypot(b.x() - a.x(), b.y() - a.y())});
        }
    };
    for (const PatchMesh &pm : m_patches) {
        patchRings.append(orderedPatchBoundary(pm));
        addStripEdges(pm);
    }
    // Other constraints near a strip bound its edge lengths (see minSizeOn).
    SegmentGrid polyGrid;
    polyGrid.build({}, std::max(4.0 * hMin, 1e-9));
    for (int i = 0; i < polys.size(); ++i) {
        const int n = polys[i].pts.size();
        const int edges = polys[i].closed ? n : n - 1;
        for (int k = 0; k < edges; ++k)
            polyGrid.addSplit(polys[i].pts[k], polys[i].pts[(k + 1) % n], i);
    }
    for (const SteinerPoint &sp : m_steiners)
        polyGrid.addSplit(sp.xy, sp.xy, -1);
    // Distance from a path to the nearest other constraint (ignoring the
    // features the strip is built from), searched up to \p r.
    // \p ignorePoly is a poly index (polyGrid's owners); \p extra is a grid
    // with its own owners, \p extraIgnoreA / \p extraIgnoreB among them.
    constexpr int kNone = std::numeric_limits<int>::min();
    auto clearanceOf = [&](const QVector<QPointF> &path, double r, int ignorePoly,
                           const SegmentGrid *extra, int extraIgnoreA, int extraIgnoreB) {
        double d2 = r * r;
        for (int k = 0; k + 1 < path.size(); ++k)
            for (int f = 0; f <= 4; ++f) {
                const QPointF q = path[k] + (path[k + 1] - path[k]) * (f / 4.0);
                d2 = std::min(d2, polyGrid.nearest2Ignoring(q, r, ignorePoly, ignorePoly));
                if (extra)
                    d2 = std::min(d2, extra->nearest2Ignoring(q, r, extraIgnoreA, extraIgnoreB));
            }
        return std::sqrt(d2);
    };
    StripChecker checker(domainRings, holeRings, m_steiners, hMin);
    for (int i = 0; i < polys.size(); ++i)
        checker.addPoly(polys[i], i);
    for (const QPolygonF &r : std::as_const(patchRings))
        checker.addRing(r);

    // Smallest size along a strip's boundary: a strip's edges are fixed, so
    // they may not be longer than the size the triangles beside them need —
    // or those triangles could never reach the angle bound.
    auto minSizeOn = [&](const QVector<QPointF> &path, bool closed) {
        double h = std::numeric_limits<double>::infinity();
        const int n = path.size(), edges = closed ? n : n - 1;
        for (int k = 0; k < edges; ++k)
            for (int f = 0; f < 4; ++f) {
                const QPointF q = path[k] + (path[(k + 1) % n] - path[k]) * (f / 4.0);
                const double hq = hClamped(q.x(), q.y());
                if (hq > 0.0)
                    h = std::min(h, hq);
            }
        if (!closed && n > 0) {
            const double hq = hClamped(path.last().x(), path.last().y());
            if (hq > 0.0)
                h = std::min(h, hq);
        }
        return std::isfinite(h) ? h : hMin;
    };

    m_quadReports.resize(m_quadRegions.size());
    for (int i = 0; i < m_quadRegions.size(); ++i) {
        QuadRegionReport &r = m_quadReports[i];
        r.index = i;
        r.requested = m_quadRegions[i].mode;
        r.resolved = QuadRegionMode::TrianglesOnly;
        r.accepted = regionRings[i].size() >= 3;
        r.spacing = m_quadRegions[i].spacing;
        if (!r.accepted)
            r.message = QStringLiteral("region ring has fewer than 3 vertices");
    }
    for (int pi = 0; pi < polys.size(); ++pi) {
        const int ri = polys[pi].regionIndex;
        if (ri < 0 || polys[pi].isHole)
            continue;
        const QuadRegion &qr = m_quadRegions[ri];
        QuadRegionReport &rep = m_quadReports[ri];
        if (!m_opts.allowQuads || qr.mode == QuadRegionMode::TrianglesOnly) {
            rep.message = m_opts.allowQuads ? QStringLiteral("triangles requested")
                                            : QStringLiteral("global triangles-only mode");
            continue;
        }
        const QPolygonF ring = normalizeRingCCW(QPolygonF(regionRings[ri]));
        QVector<int> corners;
        if (classifyQuadRegion(ring, &corners) != QuadRegionMode::Mapped || corners.size() != 4) {
            rep.message = QStringLiteral(
                "not four-sided, so triangles inside (split it into four-sided pieces for quads)");
            continue;
        }
        QString err;
        const double h = qr.spacing > 0.0
                             ? qr.spacing
                             : minSizeOn(QVector<QPointF>(ring.begin(), ring.end()), true);
        PatchMesh pm = qr.directionalSpacing ? makeMappedPatch(ring, corners, qr.hAlong, qr.hAcross,
                                                               qr.mappedAlongAngleDeg, qr.tag, &err)
                                             : makeMappedPatch(ring, corners, h, qr.tag, &err);
        QPolygonF outline;
        if (err.isEmpty())
            outline = orderedPatchBoundary(pm, &err);
        if (err.isEmpty())
            err = checker.misfit(outline, pi);
        if (!err.isEmpty()) {
            rep.message = QStringLiteral("no quads: %1").arg(err);
            ++m_stats.stripsDropped;
            continue;
        }
        if (m_leakedPatches.contains(patchKey(outline))) {
            rep.message = QStringLiteral("ring not watertight");
            ++m_stats.stripsDropped;
            continue;
        }
        polys[pi].dropped = true;
        appendPatchBoundary(pm, true, &polys);
        checker.addRing(outline);
        patchRings.append(outline);
        addStripEdges(pm);
        patches.append(pm);
        rep.resolved = QuadRegionMode::Mapped;
        ++m_stats.regionPatches;
    }
    // Conduit strips: the line runs along the middle row. Each end is left as
    // a plain constrained line one width long — two, four when a neighbour
    // leaves the junction at an acute angle — so strips never meet there.
    for (int pi = 0; pi < polys.size(); ++pi) {
        if (!(polys[pi].stripWidth > 0.0) || polys[pi].dropped)
            continue;
        const Poly src = polys[pi];
        const double w = src.stripWidth, L = pslg::polylineLength(src.pts);
        PatchMesh pm;
        QPolygonF outline;
        SweptPatch sp;
        double cut = 0.0;
        QString err = QStringLiteral("it is shorter than three widths");
        for (const double c : {w, 2.0 * w, 4.0 * w}) {
            if (!(L - 2.0 * c >= w))
                break;
            const QVector<QPointF> centre = subPolyline(src.pts, c, L - c);
            const QPointF mid = centre[centre.size() / 2];
            const double requested = hClamped(mid.x(), mid.y());
            const double hMid = requested > 0 ? requested : std::max(hMin, w);
            // Stations no longer than the room beside the strip either. The
            // spacing below never exceeds 2w / across <= w, so room beyond 2w
            // changes nothing; searching out to the local size (up to the
            // largest cell size, kilometres) made every strip scan the whole
            // neighbourhood.
            const double room =
                clearanceOf(centre, std::min(hMid, 2.0 * w) + 0.5 * w, pi, nullptr, kNone, kNone) -
                0.5 * w;
            const double hLine =
                std::max(std::min(minSizeOn(centre, false), std::max(room, hMin)), 1e-12);
            sp = SweptPatch();
            sp.centreline = centre;
            sp.width = w;
            // Even rows (the line on the middle one), cells no more than twice
            // as long as wide either way, stations at the line's size.
            sp.across = std::max(2, 2 * int(std::lround(w / (2.0 * hMid))));
            sp.across = std::max(sp.across, 2 * int(std::ceil(w / (4.0 * hLine))));
            sp.along = std::min(hLine, 2.0 * w / sp.across);
            // Every station interval near the spacing: a vertex just short of
            // the cut would leave a sliver row of quads.
            sp.centreline = pslg::resampleMinLength(centre, 0.5 * sp.along, 0.05 * w);
            err.clear();
            pm = makeSweptPatch(sp, &err);
            if (err.isEmpty())
                outline = orderedPatchBoundary(pm, &err);
            if (err.isEmpty())
                err = checker.misfit(outline, pi);
            // The plain ends must stay outside the strip and clear of it but
            // where they meet its cap (a line that bends back would cross it).
            if (err.isEmpty())
                for (const QVector<QPointF> &piece :
                     {subPolyline(src.pts, 0.0, c), subPolyline(src.pts, L - c, L)})
                    if (!endClearOf(piece, outline, centre.first(), centre.last(), hMin)) {
                        err = QStringLiteral("its end bends back across the strip");
                        break;
                    }
            if (err.isEmpty()) {
                cut = c;
                break;
            }
        }
        if (err.isEmpty() && m_leakedPatches.contains(patchKey(outline)))
            err = QStringLiteral("ring not watertight");
        if (!err.isEmpty()) {
            GeneratorDiagnostic(m_refineHook.onDiagnostic).noquote()
                << QStringLiteral("[Mesh] conduit strip%1 dropped: %2")
                       .arg(src.tag.isEmpty() ? QString() : QStringLiteral(" '%1'").arg(src.tag),
                            err);
            ++m_stats.stripsDropped;
            continue;
        }
        const int na = sp.across, stations = pm.xy.size() / (na + 1);
        PatchLine line;
        line.marker = src.marker;
        line.tag = src.tag;
        for (int s = 0; s < stations; ++s)
            line.path.append(s * (na + 1) + na / 2);
        pm.lines.append(line);
        polys[pi].dropped = true;
        for (const QVector<QPointF> &piece :
             {subPolyline(src.pts, 0.0, cut), subPolyline(src.pts, L - cut, L)}) {
            if (piece.size() < 2)
                continue;
            Poly p = src;
            p.pts = piece;
            p.stripWidth = 0.0;
            checker.addPoly(p, polys.size());
            polys.append(p);
        }
        appendPatchBoundary(pm, false, &polys);
        checker.addRing(outline);
        patchRings.append(outline);
        addStripEdges(pm);
        patches.append(pm);
        ++m_stats.conduitStrips;
    }

    // ── Terrain break lines (Phase 6b) ───────────────────────────────────
    // After the hole rings and strips are known (a terrain loop never
    // becomes a hole; lines keep clear of strips) and before resampling.
    auto activePolys = [&]() {
        QVector<Poly> out;
        for (const Poly &p : std::as_const(polys))
            if (!p.dropped)
                out.append(p);
        return out;
    };
    QVector<Poly> terrain;
    if (!m_terrainLines.isEmpty()) {
        QVector<QVector<QPointF>> excluded = holeRings;
        for (const QPolygonF &r : std::as_const(patchRings))
            if (r.size() >= 3)
                excluded.append(QVector<QPointF>(r.begin(), r.end()));
        terrain = prepareTerrainLines(m_terrainLines, activePolys(), domainRings, excluded,
                                      m_steiners, hMin, stripEdges, m_opts.terrainMinLineLength);
        // Streets and ditches: facing lines with lower ground between them.
        if (m_opts.allowQuads && m_opts.quadsBetweenBreaklines && m_refineHook.elevationAt &&
            !terrain.isEmpty()) {
            QVector<QVector<QPointF>> lines;
            for (const Poly &p : std::as_const(terrain)) {
                QVector<QPointF> path = p.pts;
                if (p.closed)
                    path.append(path.first());
                lines.append(path);
            }
            QVector<QPair<QPointF, QPointF>> obstacles;
            for (const Poly &p : activePolys()) {
                const int n = p.pts.size();
                const int edges = p.closed ? n : n - 1;
                for (int k = 0; k < edges; ++k)
                    obstacles.append(qMakePair(p.pts[k], p.pts[(k + 1) % n]));
            }
            for (const QPolygonF &r : std::as_const(patchRings))
                for (int k = 0; k < r.size(); ++k)
                    obstacles.append(qMakePair(r[k], r[(k + 1) % r.size()]));
            // A node (a manhole in the street) splits the strip around it
            // rather than keeping the whole street out: a small cross at
            // each node blocks the rays that pass it.
            for (const SteinerPoint &sp : m_steiners) {
                obstacles.append(qMakePair(sp.xy - QPointF(hMin, 0.0), sp.xy + QPointF(hMin, 0.0)));
                obstacles.append(qMakePair(sp.xy - QPointF(0.0, hMin), sp.xy + QPointF(0.0, hMin)));
            }
            SegmentGrid terrainGrid;
            terrainGrid.build({}, std::max(4.0 * hMin, 1e-9));
            for (int i = 0; i < lines.size(); ++i)
                for (int k = 0; k + 1 < lines[i].size(); ++k)
                    terrainGrid.addSplit(lines[i][k], lines[i][k + 1], i);
            FacingPairOptions fo;
            fo.minWidth = 2.0 * hMin;
            fo.maxWidth =
                hUniform > 0.0 ? std::max(10.0 * hUniform, 4.0 * fo.minWidth) : 40.0 * hMin;
            fo.station = hMin;
            fo.elevationAt = m_refineHook.elevationAt;
            int accepted = 0;
            for (const FacingPair &fp : findFacingPairs(lines, obstacles, fo)) {
                if (cancelled())
                    return fail(QStringLiteral("Cancelled."));
                const QPointF mid =
                    0.5 * (fp.bankA[fp.bankA.size() / 2] + fp.bankB[fp.bankB.size() / 2]);
                const double requested = hClamped(mid.x(), mid.y());
                const double hMid = requested > 0 ? requested : std::max(hMin, fp.width);
                double hBank = std::max(
                    std::min(minSizeOn(fp.bankA, false), minSizeOn(fp.bankB, false)), 1e-12);
                hBank = std::min(
                    hBank, std::max(hMin, std::min(clearanceOf(fp.bankA, hBank, kNone, &terrainGrid,
                                                               fp.lineA, fp.lineB),
                                                   clearanceOf(fp.bankB, hBank, kNone, &terrainGrid,
                                                               fp.lineA, fp.lineB))));
                // The end caps run across the street: their pieces must fit the
                // size there too (either pairing of the bank ends).
                double hEnds = std::numeric_limits<double>::infinity();
                for (const QVector<QPointF> &cap :
                     {QVector<QPointF>{fp.bankA.first(), fp.bankB.first()},
                      QVector<QPointF>{fp.bankA.last(), fp.bankB.last()},
                      QVector<QPointF>{fp.bankA.first(), fp.bankB.last()},
                      QVector<QPointF>{fp.bankA.last(), fp.bankB.first()}}) {
                    const double hc = minSizeOn(cap, false);
                    hEnds = std::min({hEnds, hc,
                                      std::max(hMin, clearanceOf(cap, hc, kNone, &terrainGrid,
                                                                 fp.lineA, fp.lineB))});
                }
                hEnds = std::max(hEnds, 1e-12);
                BankPairPatch bp;
                bp.bankA = fp.bankA;
                bp.bankB = fp.bankB;
                // At least two rows, so the street's cross-fall shows; cells no
                // more than twice as long as wide either way.
                bp.across = std::max(2, int(std::lround(fp.width / hMid)));
                bp.across = std::max({bp.across, int(std::ceil(fp.width / hEnds)),
                                      int(std::ceil(fp.width / (2.0 * hBank)))});
                bp.along = std::min(hBank, 2.0 * fp.width / bp.across);
                QString err;
                const PatchMesh pm = makeBankPairPatch(bp, &err);
                QPolygonF outline;
                if (err.isEmpty())
                    outline = orderedPatchBoundary(pm, &err);
                if (err.isEmpty())
                    err = checker.misfit(outline, std::numeric_limits<int>::min());
                if (err.isEmpty() && m_leakedPatches.contains(patchKey(outline)))
                    err = QStringLiteral("ring not watertight");
                if (!err.isEmpty()) {
                    ++m_stats.stripsDropped;
                    continue;
                }
                appendPatchBoundary(pm, false, &polys);
                checker.addRing(outline);
                patchRings.append(outline);
                addStripEdges(pm);
                excluded.append(QVector<QPointF>(outline.begin(), outline.end()));
                patches.append(pm);
                ++accepted;
            }
            m_stats.breaklineStrips = accepted;
            // The curbs under a strip are its sides now: prepare the lines
            // again so what remains keeps clear of the strips.
            if (accepted > 0)
                terrain =
                    prepareTerrainLines(m_terrainLines, activePolys(), domainRings, excluded,
                                        m_steiners, hMin, stripEdges, m_opts.terrainMinLineLength);
        }
        for (const Poly &p : std::as_const(terrain)) {
            QVector<QPointF> kept = p.pts;
            if (p.closed)
                kept.append(kept.first());
            m_acceptedTerrainLines.append(kept);
            Poly t = p;
            t.barrier = false;
            polys.append(t);
        }
    }
    if (cancelled())
        return fail(QStringLiteral("Cancelled."));

    // ── Resample constraints at h(x) ─────────────────────────────────────
    for (Poly &p : polys) {
        if (!p.resample || p.dropped)
            continue;
        QVector<QPointF> path = p.pts;
        if (p.closed)
            path.append(path.first());
        path = pslg::resampleAtSize(path, hClamped);
        if (p.closed)
            path.removeLast();
        p.pts = path;
    }
    if (cancelled())
        return fail(QStringLiteral("Cancelled."));

    // ── Constrained Delaunay triangulation ───────────────────────────────
    QVector<QPointF> cdtPoints;
    QVector<int> polyPointStart(polys.size(), -1);
    for (int i = 0; i < polys.size(); ++i) {
        if (polys[i].dropped)
            continue;
        polyPointStart[i] = cdtPoints.size();
        for (const QPointF &q : polys[i].pts)
            cdtPoints.append(q);
    }
    for (const SteinerPoint &sp : m_steiners)
        cdtPoints.append(sp.xy);
    if (cdtPoints.size() < 3)
        return fail(QStringLiteral("MeshGenerator: too few points to triangulate."));

    qint64 fixedCells = 0;
    for (const auto &patch : std::as_const(patches))
        fixedCells += patch.quads.size();
    if (fixedCells >= m_opts.maxCells)
        return fail(QStringLiteral(
            "Structured feature cells exhaust the cell budget. Increase the budget or the feature spacing."));
    ConstrainedDelaunay cdt;
    QVector<int> pointVertex;
    if (!cdt.build(cdtPoints, &pointVertex))
        return fail(QStringLiteral("MeshGenerator: %1").arg(cdt.errorMsg()));
    if (cancelled())
        return fail(QStringLiteral("Cancelled."));

    if (m_opts.diagnosticProvenance) {
        cdt.enableProvenance();
        for (int i = 0; i < polys.size(); ++i)
            if (!polys[i].dropped) {
                const auto &p = polys[i];
                const quint32 origin = p.isDomain           ? ConstrainedDelaunay::Domain
                                       : p.isHole           ? ConstrainedDelaunay::Hole
                                       : p.isTerrain        ? ConstrainedDelaunay::Breakline
                                       : p.regionIndex >= 0 ? ConstrainedDelaunay::Region
                                       : p.fixed            ? ConstrainedDelaunay::Patch
                                                            : ConstrainedDelaunay::Line;
                for (int k = 0; k < p.pts.size(); ++k)
                    cdt.addVertexOrigin(pointVertex[polyPointStart[i] + k], origin);
            }
        const int firstSteiner = cdtPoints.size() - m_steiners.size();
        for (int i = firstSteiner; i < pointVertex.size(); ++i)
            cdt.addVertexOrigin(pointVertex[i], ConstrainedDelaunay::Node);
    }
    for (int i = 0; i < polys.size(); ++i) {
        Poly &p = polys[i];
        if (p.dropped)
            continue;
        p.cdtIds.resize(p.pts.size());
        for (int k = 0; k < p.pts.size(); ++k)
            p.cdtIds[k] = pointVertex[polyPointStart[i] + k];
        const int n = p.pts.size();
        const int edges = p.closed ? n : n - 1;
        for (int k = 0; k < edges; ++k) {
            const int a = p.cdtIds[k], b = p.cdtIds[(k + 1) % n];
            if (a == b)
                continue;
            if (!cdt.insertConstraint(a, b)) {
                const QPointF pa = p.pts[k], pb = p.pts[(k + 1) % n];
                const auto kindOf = [](const Poly &q) {
                    return q.isDomain ? QStringLiteral("domain ring")
                           : q.isHole ? QStringLiteral("hole ring")
                           : q.closed ? QStringLiteral("closed ring")
                                      : QStringLiteral("line");
                };
                // Name the already inserted constraint this edge crosses.
                QString crossedBy;
                for (int j = 0; j <= i && crossedBy.isEmpty(); ++j) {
                    const Poly &o = polys[j];
                    if (o.dropped)
                        continue;
                    const int on = o.pts.size(), oEdges = o.closed ? on : on - 1;
                    for (int m = 0; m < oEdges && crossedBy.isEmpty(); ++m) {
                        if (j == i && std::abs(m - k) <= 1)
                            continue;
                        const QPointF qa = o.pts[m], qb = o.pts[(m + 1) % on];
                        const auto opposite = [](QPointF u, QPointF v, QPointF c, QPointF d) {
                            const double oc = ConstrainedDelaunay::orientExact(u, v, c),
                                         od = ConstrainedDelaunay::orientExact(u, v, d);
                            return (oc > 0.0 && od < 0.0) || (oc < 0.0 && od > 0.0);
                        };
                        if (opposite(pa, pb, qa, qb) && opposite(qa, qb, pa, pb))
                            crossedBy =
                                QStringLiteral(
                                    "; crosses %1%2 edge %8 of %9 (%3, %4)-(%5, %6), marker %7")
                                    .arg(kindOf(o))
                                    .arg(o.tag.isEmpty() ? QString()
                                                         : QStringLiteral(" '%1'").arg(o.tag))
                                    .arg(qa.x(), 0, 'f', 2)
                                    .arg(qa.y(), 0, 'f', 2)
                                    .arg(qb.x(), 0, 'f', 2)
                                    .arg(qb.y(), 0, 'f', 2)
                                    .arg(o.marker)
                                    .arg(m)
                                    .arg(oEdges);
                    }
                }
                return fail(
                    QStringLiteral("MeshGenerator: constraint%1 could not be recovered — %2. "
                                   "Constraints cross each other or the domain boundary. "
                                   "(%3 edge %10 of %11 (%4, %5)-(%6, %7), marker %8%9)")
                        .arg(p.tag.isEmpty() ? QString() : QStringLiteral(" '%1'").arg(p.tag),
                             cdt.errorMsg(), kindOf(p))
                        .arg(pa.x(), 0, 'f', 2)
                        .arg(pa.y(), 0, 'f', 2)
                        .arg(pb.x(), 0, 'f', 2)
                        .arg(pb.y(), 0, 'f', 2)
                        .arg(p.marker)
                        .arg(crossedBy)
                        .arg(k)
                        .arg(edges));
            }
        }
    }
    // Fixed after every constraint is in, so a later constraint's vertex on a
    // patch edge is already part of its chain.
    for (const Poly &p : std::as_const(polys)) {
        if (p.dropped || !p.fixed)
            continue;
        const int n = p.pts.size();
        const int edges = p.closed ? n : n - 1;
        for (int k = 0; k < edges; ++k)
            cdt.setFixedConstraint(p.cdtIds[k], p.cdtIds[(k + 1) % n]);
    }
    if (cancelled())
        return fail(QStringLiteral("Cancelled."));

    cdt.removeExterior();
    {
        int bigHoles = 0;
        for (const QPointF &seed : std::as_const(holeSeeds)) {
            const int removed = cdt.removeRegionAt(seed);
            // A building hole holds only its ring vertices; thousands of
            // removed triangles mean its seed or ring let the flood out.
            if (removed > 5000 && ++bigHoles <= 10)
                GeneratorDiagnostic(m_refineHook.onDiagnostic).noquote()
                    << QStringLiteral("[Mesh][hole-diag] seed (%1, %2) removed %3 triangles")
                           .arg(seed.x(), 0, 'f', 2)
                           .arg(seed.y(), 0, 'f', 2)
                           .arg(removed);
        }
        if (bigHoles)
            GeneratorDiagnostic(m_refineHook.onDiagnostic).noquote()
                << QStringLiteral("[Mesh][hole-diag] %1 hole removals were unexpectedly large")
                       .arg(bigHoles);
    }
    for (int pi = 0; pi < patches.size(); ++pi)
        if (const PatchMesh &pm = patches[pi]; !pm.quads.isEmpty()) {
            const MeshTriangle &q = pm.quads.first();
            const QPointF c = 0.25 * (pm.xy[q.v0] + pm.xy[q.v1] + pm.xy[q.v2] + pm.xy[q.v3]);
            // A patch interior holds only its ring vertices: about ring - 2
            // triangles. Removing far more means its ring leaked into the
            // surrounding mesh; regenerate with this patch as triangles.
            const int removed = cdt.removeRegionAt(c);
            if (leaked && pi < patchRings.size() && removed > 2 * patchRings[pi].size() + 8) {
                *leaked = patchKey(patchRings[pi]);
                return fail(
                    QStringLiteral(
                        "MeshGenerator: structured patch ring is not watertight (removal took %1 triangles)")
                        .arg(removed));
            }
        }
    if (cancelled())
        return fail(QStringLiteral("Cancelled."));

    // ── Lattice seeding (D-R6) ───────────────────────────────────────────
    // One hexagonal lattice per size band [s, 2s), s = kMeanEdge·hMin·2^k:
    // a point is kept where the local target edge falls in its band, inside
    // the domain and holes, and at least half a spacing from every
    // constraint. Refinement then only fills band transitions and the
    // neighbourhood of constraints, so open areas stay near-equilateral.
    if (m_opts.latticeSeeding && hMin > 0.0 &&
        (haveHook || hUniform > 0.0 || !spacingOverrides.isEmpty())) {
        QRectF bbox;
        for (const auto &r : domainRings)
            bbox = bbox.isValid() ? bbox.united(QPolygonF(r).boundingRect())
                                  : QPolygonF(r).boundingRect();
        const RingIndex inDomain(ringPointers(domainRings)), inHole(ringPointers(holeRings));
        SegmentGrid near;
        near.build({}, 4.0 * kMeanEdge * hMin);
        for (const Poly &p : polys) {
            if (p.dropped || p.pts.size() < 2)
                continue;
            const int n = p.pts.size(), edges = p.closed ? n : n - 1;
            for (int k = 0; k < edges; ++k)
                near.addSplit(p.pts[k], p.pts[(k + 1) % n]);
        }
        for (const SteinerPoint &sp : m_steiners)
            near.addSplit(sp.xy, sp.xy);
        qint64 seeded = 0;
        const double s0 = kMeanEdge * hMin;
        for (double s = s0; s < 2.0 * std::max(bbox.width(), bbox.height()); s *= 2.0) {
            const double dy = s * std::sqrt(3.0) / 2.0;
            int row = 0;
            for (double y = bbox.top() + 0.5 * dy; y < bbox.bottom(); y += dy, ++row) {
                if (cancelled())
                    return fail(QStringLiteral("Cancelled."));
                for (double x = bbox.left() + ((row & 1) ? 0.5 * s : 0.0) + 0.25 * s;
                     x < bbox.right(); x += s) {
                    const double h = kMeanEdge * hClamped(x, y);
                    if (!(h >= s) || (h >= 2.0 * s))
                        continue;
                    const QPointF q(x, y);
                    if (!inDomain.anyContains(q) || inHole.anyContains(q))
                        continue;
                    if (near.segmentNear(q, q, 0.5 * s))
                        continue;
                    const int before = cdt.vertices().size();
                    const int v = cdt.insertPoint(q);
                    if (v >= 0)
                        ++seeded;
                    if (v >= before)
                        cdt.addVertexOrigin(v, ConstrainedDelaunay::Lattice);
                }
            }
        }
        GeneratorDiagnostic(m_refineHook.onDiagnostic).noquote()
            << QStringLiteral("[Mesh] lattice seeding: %1 vertices").arg(seeded);
    }
    if (cancelled())
        return fail(QStringLiteral("Cancelled."));

    // ── Quality refinement ───────────────────────────────────────────────
    {
        // Safety cap from the expected cell count: ∫ dA / ((√3/4) h²) over
        // the domain on a 128 × 128 sample, 8× over, plus the inputs.
        QRectF bbox;
        for (const auto &r : domainRings)
            bbox = bbox.isValid() ? bbox.united(QPolygonF(r).boundingRect())
                                  : QPolygonF(r).boundingRect();
        double expected = 0.0;
        const int ns = 128;
        const double dx = bbox.width() / ns, dy = bbox.height() / ns;
        for (int iy = 0; iy < ns; ++iy)
            for (int ix = 0; ix < ns; ++ix) {
                const QPointF c(bbox.left() + (ix + 0.5) * dx, bbox.top() + (iy + 0.5) * dy);
                bool in = false;
                for (const auto &r : domainRings)
                    if (pointInRing(r, c)) {
                        in = true;
                        break;
                    }
                if (!in)
                    continue;
                const double h = kMeanEdge * hClamped(c.x(), c.y());
                if (h > 0.0)
                    expected += dx * dy / (kEquilateral * h * h);
            }
        ConstrainedDelaunay::QualityOptions qo;
        qo.hAt = [&](double x, double y) { return kMeanEdge * hClamped(x, y); };
        qo.minAngleDeg = std::clamp(m_opts.minAngleDeg, 0.0, 34.0);
        qo.maxInsertions = int(std::min(2.0e9, 8.0 * expected + 20.0 * cdtPoints.size() + 10000.0));
        qo.minEdge =
            kRefineFloor * hMin; // a floor against cascades, well under the minimum cell size
        qo.prioritizeQuality = m_opts.prioritizeQuality;
        qo.maxTriangles = std::max(1, int(m_opts.maxCells - fixedCells));
        qo.terrainError = m_refineHook.terrainError;
        qo.terrainErrorBound = m_refineHook.terrainErrorBound;
        qo.terrainElevationAt = m_refineHook.terrainElevationAt;
        qo.terrainTolerance = m_refineHook.terrainTolerance;
        qo.terrainWorstFirst = m_refineHook.terrainWorstFirst;
        qo.terrainFinalCheck = m_refineHook.terrainFinalCheck;
        qo.smoothingPasses = m_opts.smoothingPasses;
        qo.coarsenPasses = m_opts.coarsenPasses;
        // A region that resolves to triangles still participates in local
        // sizing and tagging; it is not a fixed quad patch. Protecting every
        // requested region suppressed all coarsening when a triangular region
        // covered the domain (including global triangles-only mode).
        QVector<QVector<QPointF>> protectedRings;
        if (qo.coarsenPasses > 0)
            for (int i = 0; i < m_quadReports.size(); ++i)
                if (m_quadReports[i].resolved == QuadRegionMode::Mapped)
                    protectedRings.append(regionRings[i]);
        const RingIndex protectedRegions(ringPointers(protectedRings));
        if (!protectedRings.isEmpty())
            qo.protectedLocation = [&](const QPointF &p) {
                return protectedRegions.anyContains(p);
            };
        qo.terrainMinSpacing = hMin;
        if (m_refineHook.onDetailedProgress)
            qo.onProgress = [&](GenerationProgress state) {
                state.quads = fixedCells;
                m_refineHook.onDetailedProgress(state);
            };
        // Terrain may require far more cells than the coarse size estimate.
        // The explicit cell budget remains the hard resource limit.
        if (qo.terrainError)
            qo.maxInsertions = std::max(qo.maxInsertions, qo.maxTriangles);
        qo.cancelled = [&]() {
            progress(cdt.vertices().size());
            return cancelled();
        };
        const auto rep = cdt.refineQuality(qo);
        if (rep.cancelled || cancelled())
            return fail(QStringLiteral("Cancelled."));
        m_stats.refineInserted = rep.inserted;
        m_stats.coarsenRemoved = rep.coarsenRemoved;
        m_stats.coarsenRejectedAngle = rep.coarsenRejectedAngle;
        m_stats.coarsenRejectedSize = rep.coarsenRejectedSize;
        m_stats.coarsenRejectedTerrain = rep.coarsenRejectedTerrain;
        m_stats.coarsenRejectedEdge = rep.coarsenRejectedEdge;
        m_stats.coarsenAttempts = rep.coarsenAttempts;
        m_stats.coarsenHeightRejects = rep.coarsenHeightRejects;
        m_stats.coarsenBoundAccepts = rep.coarsenBoundAccepts;
        m_stats.coarsenExactQueries = rep.coarsenExactQueries;
        m_stats.coarsenSkippedTerrain = rep.coarsenSkippedTerrain;
        m_stats.coarsenSeconds = rep.coarsenSeconds;
        m_stats.sizeInserted = rep.sizeInsertions;
        m_stats.qualityInserted = rep.qualityInsertions;
        m_stats.terrainInserted = rep.terrainInsertions;
        m_stats.segmentSplits = rep.segmentSplits;
        m_stats.terrainUnresolved = rep.terrainUnresolved;
        m_stats.terrainUnknown = rep.terrainUnknown;
        m_stats.maxTerrainError = rep.maxTerrainError;
        // What the guarantee leaves out, counted: triangles under the bound
        // that are not the small-input-angle exemption (they rest on strip
        // edges, or on inputs closer than the floor).
        const double sinMin = std::sin(qo.minAngleDeg * M_PI / 180.0);
        for (int t = 0; t < cdt.triangles().size(); ++t) {
            const auto &T = cdt.triangles()[t];
            if (!T.alive)
                continue;
            const QPointF &A = cdt.vertices()[T.v[0]], &B = cdt.vertices()[T.v[1]],
                          &C = cdt.vertices()[T.v[2]];
            const double cross =
                (B.x() - A.x()) * (C.y() - A.y()) - (B.y() - A.y()) * (C.x() - A.x());
            const double la = std::hypot(C.x() - B.x(), C.y() - B.y()),
                         lb = std::hypot(A.x() - C.x(), A.y() - C.y()),
                         lc = std::hypot(B.x() - A.x(), B.y() - A.y());
            const double sinWorst =
                std::min({cross / (lb * lc), cross / (la * lc), cross / (la * lb)});
            if (sinWorst < sinMin - 1e-12 && !cdt.smallAngleExempt(t))
                ++m_stats.trianglesBelowAngle;
        }
        m_stats.refineCapped = rep.capped;
        if (rep.capped)
            qWarning() << "[Mesh] refinement stopped at its safety cap (" << qo.maxInsertions
                       << " vertices) — some triangles may miss the size or angle bound.";
    }

    if (m_opts.diagnosticProvenance) {
        auto label = [](quint32 mask) {
            QStringList labels;
            const char *names[] = {"domain",      "hole",      "line",    "patch",
                                   "input_point", "breakline", "region",  "lattice",
                                   "size",        "angle",     "terrain", "segment_split"};
            for (int k = 0; k < 12; ++k)
                if (mask & (1u << k))
                    labels.append(QLatin1String(names[k]));
            return labels.isEmpty() ? QStringLiteral("unclassified") : labels.join('+');
        };
        QJsonObject vertices, edges, below;
        QSet<int> active;
        const auto &origins = cdt.vertexOrigins();
        auto count = [](QJsonObject &table, const QString &key) {
            table[key] = table.value(key).toInt() + 1;
        };
        for (const auto &T : cdt.triangles())
            if (T.alive) {
                for (int v : T.v)
                    active.insert(v);
                int shortest = 0;
                double length = std::numeric_limits<double>::infinity();
                for (int k = 0; k < 3; ++k) {
                    const auto delta =
                        cdt.vertices()[T.v[(k + 1) % 3]] - cdt.vertices()[T.v[(k + 2) % 3]];
                    const double d = QPointF::dotProduct(delta, delta);
                    if (d < length) {
                        length = d;
                        shortest = k;
                    }
                }
                const QString key =
                    label(origins[T.v[(shortest + 1) % 3]] | origins[T.v[(shortest + 2) % 3]]);
                count(edges, key);
                if (length < hMin * hMin)
                    count(below, key);
            }
        for (int v : active)
            count(vertices, label(origins[v]));
        m_stats.provenance = {
            {"liveVerticesByOrigin", vertices},
            {"shortestEdgeEndpointOrigins", edges},
            {"belowMinimumEndpointOrigins", below},
            {"scope",
             QStringLiteral(
                 "CDT before assembly; endpoint association, not causal attribution; structured patch interiors excluded")}};
    }

    // ── Assembly ─────────────────────────────────────────────────────────
    // Marker per exact coordinate: Steiner markers win over the boundary marker.
    QHash<QPair<qint64, qint64>, int> markerOf;
    for (const Poly &p : std::as_const(polys))
        if (p.isDomain)
            for (const QPointF &q : p.pts)
                markerOf.insert(coordKey(q), kBoundaryMarker);
    for (const SteinerPoint &sp : m_steiners)
        if (sp.marker != 0)
            markerOf.insert(coordKey(sp.xy), sp.marker);

    QVector<int> cdtToGlobal(cdt.vertices().size(), -1);
    QHash<QPair<qint64, qint64>, int> globalOfCoord;
    auto addVertex = [&](const QPointF &xy,
                         double elevation = std::numeric_limits<double>::quiet_NaN()) {
        const auto key = coordKey(xy);
        const auto it = globalOfCoord.constFind(key);
        if (it != globalOfCoord.constEnd())
            return it.value();
        MeshVertex v;
        v.xy = xy;
        if (m_refineHook.terrainElevationAt)
            v.z = std::isfinite(elevation) ? elevation
                                           : m_refineHook.terrainElevationAt(xy.x(), xy.y());
        v.marker = markerOf.value(key, 0);
        v.tag = m_vertexTagByMarker.value(v.marker);
        const int id = result.vertices.size();
        result.vertices.append(v);
        globalOfCoord.insert(key, id);
        return id;
    };
    // Vertex elevations in space-filling-curve order, not insertion order:
    // the hook reads a DEM tile cache, and refinement order touches tiles at
    // random across the raster (on a large DEM every lookup was a disk read).
    // The hook is a function of position, so the values are unchanged.
    QVector<double> vertexZ(cdt.vertices().size(), std::numeric_limits<double>::quiet_NaN());
    for (int v = 0; v < cdt.terrainElevations().size() && v < vertexZ.size(); ++v)
        vertexZ[v] = cdt.terrainElevations()[v];
    if (m_refineHook.terrainElevationAt) {
        QVector<int> pending;
        double sx0 = std::numeric_limits<double>::infinity(), sx1 = -sx0, sy0 = sx0, sy1 = -sx0;
        for (int v = 0; v < cdt.vertices().size(); ++v) {
            if (cdt.isSuperVertex(v) || !cdt.isVertexActive(v) || std::isfinite(vertexZ[v]))
                continue;
            pending.append(v);
            const QPointF &q = cdt.vertices()[v];
            sx0 = std::min(sx0, q.x());
            sx1 = std::max(sx1, q.x());
            sy0 = std::min(sy0, q.y());
            sy1 = std::max(sy1, q.y());
        }
        // Explicit bounds: QRectF::united() drops zero-size rectangles.
        const QRectF span =
            pending.isEmpty() ? QRectF() : QRectF(QPointF(sx0, sy0), QPointF(sx1, sy1));
        if (span.width() > 0 && span.height() > 0) {
            QVector<quint64> code(cdt.vertices().size(), 0);
            for (int v : std::as_const(pending)) {
                const QPointF &q = cdt.vertices()[v];
                const auto cell = [](double t) {
                    return quint32(std::clamp(t, 0.0, 1.0) * 65535.0);
                };
                const quint32 x = cell((q.x() - span.left()) / span.width()),
                              y = cell((q.y() - span.top()) / span.height());
                quint64 m = 0;
                for (int bit = 0; bit < 16; ++bit)
                    m |= quint64((x >> bit) & 1u) << (2 * bit) | quint64((y >> bit) & 1u)
                                                                     << (2 * bit + 1);
                code[v] = m;
            }
            std::stable_sort(pending.begin(), pending.end(),
                             [&](int a, int b) { return code[a] < code[b]; });
        }
        for (int v : std::as_const(pending))
            vertexZ[v] =
                m_refineHook.terrainElevationAt(cdt.vertices()[v].x(), cdt.vertices()[v].y());
    }
    for (int v = 0; v < cdt.vertices().size(); ++v)
        if (!cdt.isSuperVertex(v))
            cdtToGlobal[v] = addVertex(cdt.vertices()[v], vertexZ[v]);

    QVector<int> cellOfCdtTriangle(cdt.triangles().size(), -1);
    for (int ti = 0; ti < cdt.triangles().size(); ++ti) {
        const auto &T = cdt.triangles()[ti];
        if (!T.alive)
            continue;
        MeshTriangle t;
        t.v0 = cdtToGlobal[T.v[0]];
        t.v1 = cdtToGlobal[T.v[1]];
        t.v2 = cdtToGlobal[T.v[2]];
        if (t.v0 < 0 || t.v1 < 0 || t.v2 < 0)
            continue;
        cellOfCdtTriangle[ti] = result.triangles.size();
        result.triangles.append(t);
    }
    // Boundary edges: every sub-edge of a barrier constraint, with its marker
    // and tag. Terrain break lines and strip sides are alignment only.
    QSet<EdgeKey> floodLocked;
    for (const Poly &p : std::as_const(polys)) {
        if (p.dropped || (!p.barrier && !p.isDomain))
            continue;
        const int n = p.pts.size();
        const int edges = p.closed ? n : n - 1;
        for (int k = 0; k < edges; ++k) {
            const int ca = p.cdtIds[k], cb = p.cdtIds[(k + 1) % n];
            if (ca == cb)
                continue;
            // Refinement and vertices lying on the segment split it: list
            // every piece as a mesh edge.
            const QVector<int> chain = cdt.constrainedChain(ca, cb);
            for (int c = 0; c + 1 < chain.size(); ++c) {
                const int a = cdtToGlobal[chain[c]], b = cdtToGlobal[chain[c + 1]];
                if (a < 0 || b < 0 || a == b)
                    continue;
                if (p.isDomain)
                    for (int v : {a, b})
                        if (result.vertices[v].marker == 0)
                            result.vertices[v].marker = kBoundaryMarker;
                MeshEdge e;
                e.v0 = a;
                e.v1 = b;
                e.marker = p.marker;
                e.tag = p.tag.isEmpty() ? m_edgeTagByMarker.value(p.marker) : p.tag;
                result.boundaryEdges.append(e);
                floodLocked.insert(edgeKey(a, b));
            }
        }
    }
    const int triangleCells = result.triangles.size();
    if (cancelled())
        return fail(QStringLiteral("Cancelled."));

    // ── Structured patches, stitched by coordinate ───────────────────────
    // Patch boundary vertices entered the CDT unresampled and fixed, so they
    // come back with their exact coordinates; interior patch vertices are new.
    if (!patches.isEmpty()) {
        QRectF bbox;
        for (const auto &r : domainRings)
            bbox = bbox.isValid() ? bbox.united(QPolygonF(r).boundingRect())
                                  : QPolygonF(r).boundingRect();
        const double eps = m_opts.patchSnapEps > 0.0 ? m_opts.patchSnapEps : 1e-7;
        const QPointF origin = bbox.center();
        auto keyOf = [&](const QPointF &p) {
            return QPair<qint64, qint64>(qRound64((p.x() - origin.x()) / eps),
                                         qRound64((p.y() - origin.y()) / eps));
        };
        // A tiny snap tolerance must never push qRound64 out of range.
        const auto validSnapKey = [&](const QPointF &p) {
            const double x = (p.x() - origin.x()) / eps, y = (p.y() - origin.y()) / eps;
            const double limit = std::ldexp(1.0, 63);
            return std::isfinite(x) && std::isfinite(y) && std::abs(x) < limit &&
                   std::abs(y) < limit;
        };
        for (const MeshVertex &v : std::as_const(result.vertices))
            if (!validSnapKey(v.xy))
                return fail(QStringLiteral(
                    "MeshGenerator: patch snap tolerance is invalid or too small for the coordinate span."));
        for (const PatchMesh &pm : std::as_const(patches))
            for (const QPointF &p : pm.xy)
                if (!validSnapKey(p))
                    return fail(QStringLiteral(
                        "MeshGenerator: patch snap tolerance is invalid or too small for the coordinate span."));

        QHash<QPair<qint64, qint64>, int> outIndex;
        outIndex.reserve(result.vertices.size());
        for (int i = 0; i < result.vertices.size(); ++i)
            outIndex.insert(keyOf(result.vertices[i].xy), i);
        QHash<EdgeKey, int> patchEdgeIncidence;
        for (const PatchMesh &pm : std::as_const(patches)) {
            if (pm.quads.isEmpty())
                continue;
            QVector<int> localToGlobal(pm.xy.size(), -1);
            for (int k = 0; k < pm.xy.size(); ++k) {
                const auto it = outIndex.constFind(keyOf(pm.xy[k]));
                if (it != outIndex.constEnd()) {
                    localToGlobal[k] = it.value();
                    continue;
                }
                MeshVertex v;
                v.xy = pm.xy[k];
                if (m_refineHook.terrainElevationAt)
                    v.z = m_refineHook.terrainElevationAt(v.xy.x(), v.xy.y());
                localToGlobal[k] = result.vertices.size();
                result.vertices.append(v);
                outIndex.insert(keyOf(pm.xy[k]), localToGlobal[k]);
            }
            // Welding changes coordinates and can collapse vertices when the
            // snap tolerance is too coarse: check the welded geometry first.
            PatchMesh welded = pm;
            for (int k = 0; k < welded.xy.size(); ++k)
                welded.xy[k] = result.vertices[localToGlobal[k]].xy;
            for (const auto &edge : pm.boundarySegments)
                for (int k : {edge.first, edge.second})
                    if (welded.xy[k] != pm.xy[k])
                        return fail(QStringLiteral(
                            "MeshGenerator: patch snap tolerance moves a structured patch boundary away "
                            "from its carved cavity. Reduce the patch snap tolerance."));
            const QString error = validate(welded);
            if (!error.isEmpty())
                return fail(
                    QStringLiteral(
                        "MeshGenerator: structured patch '%1' is invalid after vertex welding: %2 "
                        "Reduce the patch snap tolerance or revise its spacing.")
                        .arg(pm.tag, error));
            for (const auto &edge : pm.boundarySegments)
                patchEdgeIncidence.insert(
                    edgeKey(localToGlobal[edge.first], localToGlobal[edge.second]), 0);
            for (const MeshTriangle &q : pm.quads) {
                MeshTriangle c = q;
                c.v0 = localToGlobal[q.v0];
                c.v1 = localToGlobal[q.v1];
                c.v2 = localToGlobal[q.v2];
                c.v3 = localToGlobal[q.v3];
                if (c.tag.isEmpty())
                    c.tag = pm.tag;
                result.triangles.append(c);
            }
            // Interior lines (a conduit along its strip): coupling edges.
            for (const PatchLine &line : pm.lines)
                for (int k = 0; k + 1 < line.path.size(); ++k) {
                    const int a = localToGlobal.value(line.path[k], -1),
                              b = localToGlobal.value(line.path[k + 1], -1);
                    if (a < 0 || b < 0 || a == b)
                        continue;
                    MeshEdge e;
                    e.v0 = a;
                    e.v1 = b;
                    e.marker = line.marker;
                    e.tag = line.tag.isEmpty() ? m_edgeTagByMarker.value(line.marker) : line.tag;
                    result.boundaryEdges.append(e);
                    floodLocked.insert(edgeKey(a, b));
                }
        }
        // Every patch boundary edge needs two incident cells (one when it
        // lies on the domain or a hole outline): exact edge identity rejects
        // unmatched subdivisions and T-junctions.
        for (const MeshTriangle &cell : std::as_const(result.triangles)) {
            const int n = cell.isQuad() ? 4 : 3;
            for (int k = 0; k < n; ++k) {
                auto found =
                    patchEdgeIncidence.find(edgeKey(cell.vertex(k), cell.vertex((k + 1) % n)));
                if (found != patchEdgeIncidence.end())
                    ++found.value();
            }
        }
        QVector<QPolygonF> outlines = patchExclusionBoundaries;
        for (const QPolygonF &domain : m_domains)
            outlines.append(patchRelativeRing(domain, patchOrigin));
        const double outlineTol = std::max(1e-9, std::max(bbox.width(), bbox.height()) * 16 *
                                                     std::numeric_limits<double>::epsilon());
        for (auto it = patchEdgeIncidence.cbegin(); it != patchEdgeIncidence.cend(); ++it) {
            if (it.value() == 2)
                continue;
            const QPointF a = result.vertices[it.key().first].xy - patchOrigin;
            const QPointF b = result.vertices[it.key().second].xy - patchOrigin;
            if (it.value() == 1 && patchEdgeOnOutline(a, b, outlines, outlineTol))
                continue;
            const QPointF at = 0.5 * (a + b) + patchOrigin;
            // Diagnose: a vertex on the edge means a T-junction; none means
            // the neighbouring cell was removed (a leaking region removal).
            QString cause = QStringLiteral("the neighbouring cell is missing");
            const QPointF d = b - a;
            const double len2 = d.x() * d.x() + d.y() * d.y();
            for (int vi = 0; vi < result.vertices.size() && len2 > 0; ++vi) {
                if (vi == it.key().first || vi == it.key().second)
                    continue;
                const QPointF q = result.vertices[vi].xy - patchOrigin;
                const double t = ((q.x() - a.x()) * d.x() + (q.y() - a.y()) * d.y()) / len2;
                if (t <= 1e-9 || t >= 1 - 1e-9)
                    continue;
                const QPointF foot = a + d * t;
                if (std::hypot(q.x() - foot.x(), q.y() - foot.y()) < 1e-6 * std::sqrt(len2)) {
                    cause = QStringLiteral("T-junction at vertex (%1, %2)")
                                .arg(q.x() + patchOrigin.x(), 0, 'f', 2)
                                .arg(q.y() + patchOrigin.y(), 0, 'f', 2);
                    break;
                }
            }
            // Diagnostics: the cells around the edge's two ends.
            for (int ci = 0; ci < result.triangles.size(); ++ci) {
                const MeshTriangle &cell = result.triangles[ci];
                const int nv = cell.vertexCount();
                bool touches = false;
                for (int k = 0; k < nv; ++k)
                    touches = touches || cell.vertex(k) == it.key().first ||
                              cell.vertex(k) == it.key().second;
                if (!touches)
                    continue;
                QStringList pts;
                for (int k = 0; k < nv; ++k)
                    pts << QStringLiteral("%1:%2 %3")
                               .arg(cell.vertex(k))
                               .arg(result.vertices[cell.vertex(k)].xy.x(), 0, 'f', 3)
                               .arg(result.vertices[cell.vertex(k)].xy.y(), 0, 'f', 3);
                GeneratorDiagnostic(m_refineHook.onDiagnostic).noquote()
                    << QStringLiteral("[Mesh][patch-diag] cell %1 (%2): %3")
                           .arg(ci)
                           .arg(nv == 4 ? "quad" : "tri")
                           .arg(pts.join(", "));
            }
            GeneratorDiagnostic(m_refineHook.onDiagnostic).noquote()
                << QStringLiteral("[Mesh][patch-diag] edge %1-%2")
                       .arg(it.key().first)
                       .arg(it.key().second);
            // Diagnostics: every constraint with a vertex within 40 units.
            for (const Poly &pp : std::as_const(polys)) {
                bool nearHere = false;
                for (const QPointF &q : pp.pts)
                    if (std::hypot(q.x() - at.x(), q.y() - at.y()) < 40.0) {
                        nearHere = true;
                        break;
                    }
                if (!nearHere)
                    continue;
                QStringList pts;
                for (const QPointF &q : pp.pts)
                    pts << QStringLiteral("%1 %2").arg(q.x(), 0, 'f', 2).arg(q.y(), 0, 'f', 2);
                GeneratorDiagnostic(m_refineHook.onDiagnostic).noquote()
                    << QStringLiteral("[Mesh][patch-diag] %1%2%3%4%5 marker %6 tag '%7': %8")
                           .arg(pp.isDomain ? "domain " : "")
                           .arg(pp.isHole ? "hole " : "")
                           .arg(pp.isTerrain ? "terrain " : "")
                           .arg(pp.fixed ? "fixed " : "")
                           .arg(pp.dropped ? "dropped " : "")
                           .arg(pp.marker)
                           .arg(pp.tag)
                           .arg(pts.join(", "));
            }
            return fail(
                QStringLiteral(
                    "MeshGenerator: structured patch boundary has %1 incident cells instead of a conforming "
                    "interface near (%2, %3): %4. Match subdivisions on touching patches and domain "
                    "boundaries, or separate the patches.")
                    .arg(it.value())
                    .arg(at.x(), 0, 'f', 2)
                    .arg(at.y(), 0, 'f', 2)
                    .arg(cause));
        }
    }
    if (result.triangles.isEmpty())
        return fail(QStringLiteral(
            "MeshGenerator: no cells were produced — the domain may be smaller than the cell size."));

    // ── Region tags: flood fill bounded by constraint edges ──────────────
    // After the patches are stitched so strip cells take the tag of the
    // region they cross; cells that carry a tag already keep it.
    if (!m_regions.isEmpty() || std::any_of(m_quadRegions.begin(), m_quadRegions.end(),
                                            [](const QuadRegion &q) { return !q.tag.isEmpty(); })) {
        QHash<EdgeKey, QVector<int>> cellsOfEdge;
        cellsOfEdge.reserve(result.triangles.size() * 2);
        for (int c = 0; c < result.triangles.size(); ++c) {
            const MeshTriangle &t = result.triangles[c];
            const int n = t.vertexCount();
            for (int e = 0; e < n; ++e)
                cellsOfEdge[edgeKey(t.vertex(e), t.vertex((e + 1) % n))].append(c);
        }
        auto cellContaining = [&](const QPointF &p) {
            const int t = cdt.locate(p);
            if (t >= 0)
                return cellOfCdtTriangle[t];
            for (int c = triangleCells; c < result.triangles.size(); ++c) {
                const MeshTriangle &q = result.triangles[c];
                QVector<QPointF> ring;
                for (int k = 0; k < q.vertexCount(); ++k)
                    ring.append(result.vertices[q.vertex(k)].xy);
                if (pointInRing(ring, p))
                    return c;
            }
            return -1;
        };
        // One visited set across ALL floods (Phase 7 finding). The locked
        // edges are fixed, so a flood fills its whole constraint-bounded
        // component; a later flood starting in an already-visited component
        // could only write tags into cells the earlier flood already tagged,
        // i.e. nothing. Skipping it is exact, and keeps many seeds in one
        // component (713 unbounded subcatchment seeds on Bellinge) at
        // O(cells) instead of O(seeds·cells).
        QVector<bool> seen(result.triangles.size(), false);
        auto flood = [&](int start, const QString &tag) {
            if (start < 0 || tag.isEmpty() || seen[start])
                return;
            QVector<int> stack{start};
            seen[start] = true;
            while (!stack.isEmpty()) {
                const int c = stack.takeLast();
                if (result.triangles[c].tag.isEmpty())
                    result.triangles[c].tag = tag;
                const MeshTriangle &t = result.triangles[c];
                const int n = t.vertexCount();
                for (int e = 0; e < n; ++e) {
                    const EdgeKey k = edgeKey(t.vertex(e), t.vertex((e + 1) % n));
                    if (floodLocked.contains(k))
                        continue;
                    for (int o : cellsOfEdge.value(k))
                        if (!seen[o]) {
                            seen[o] = true;
                            stack.append(o);
                        }
                }
            }
        };
        for (const RegionMarker &rm : m_regions)
            flood(cellContaining(rm.xy),
                  rm.tag.isEmpty() ? m_triangleTagByRegionId.value(int(rm.attribute)) : rm.tag);
        for (int i = 0; i < m_quadRegions.size(); ++i) {
            const QuadRegion &qr = m_quadRegions[i];
            if (qr.tag.isEmpty() || regionRings[i].size() < 3)
                continue;
            const QPointF seed = EditGeometry::interiorPoint(regionRings[i]);
            flood(cellContaining(seed), qr.tag);
        }
    }

    // Engine order — triangles first, quads after — holds by construction:
    // patch quads are appended after every triangle.

    result.ok = !result.triangles.isEmpty();
    return result;
}

} // namespace mesh
