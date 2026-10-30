/**
 * @file      meshpatch.cpp
 * @brief     Structured quad patches (workplans/TRI_QUAD_MESHING_PLAN_2026-09-06.md §3.2, phase
 *            G3).
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 *
 * Structured quad patches (TRI_QUAD_MESHING_PLAN §3.2, phase G3): transfinite
 * four-sided patches and swept channel patches, in local indices. The
 * polyline-side transfinite overload and makeMappedPatch serve the quad-region
 * redesign (QUAD_MESHING_REDESIGN_PLAN_2026-09-06.md §4.2).
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#include "mesh/meshpatch.h"

#include "mesh/meshcellgeom.h"

#include <QSet>

#include <algorithm>
#include <cmath>
#include <limits>

namespace mesh
{

namespace
{

/**
 * @brief Check both Cartesian coordinates for finite numeric values.
 * @param[in] p Borrowed point, unchanged.
 * @return True iff both finite, no CRS/location validity guarantee, allocation or state
 * mutation.
 */
bool finitePt(const QPointF &p) noexcept
{
    return std::isfinite(p.x()) && std::isfinite(p.y());
}

/**
 * @brief Compute a planar vector determinant.
 * @param[in] a Borrowed vector in common XY units, unchanged.
 * @param[in] b Borrowed compatible vector, unchanged.
 * @return Signed determinant in squared units; no finite/overflow validation, allocation or
 * mutation.
 */
double cross(const QPointF &a, const QPointF &b) noexcept
{
    return a.x() * b.y() - a.y() * b.x();
}

/**
 * @brief Compute local-origin signed shoelace area with implicit ring closure.
 * @param[in] ring Borrowed Cartesian points, unchanged; finite geometry expected, no closing
 * duplicate required.
 * @return Signed squared-unit area, positive CCW, zero for empty. Local origin reduces
 * large-coordinate cancellation; does not validate simplicity, finiteness or overflow. No
 * allocation/mutation.
 *
 * @details Signed area of a closed ring (positive = CCW).
 */
double ringSignedArea(const QVector<QPointF> &ring) noexcept
{
    double s = 0.0;
    const int n = ring.size();
    if (n == 0)
        return s;
    const QPointF origin = ring.first();
    for (int i = 0; i < n; ++i)
        s += cross(ring[i] - origin, ring[(i + 1) % n] - origin);
    return 0.5 * s;
}

/**
 * @brief Require every cyclic turn to have one nonzero sign.
 * @param[in] ring Borrowed finite cyclic points, unchanged; caller ensures meaningful polygon
 * count.
 * @return False for zero/mixed turns, true otherwise (including empty loop). No independent
 * simplicity, count, finite audit or tolerant predicate; used with four-corner input checks. No
 * allocation/mutation.
 *
 * @details Every consecutive turn of the ring has the same strict sign.
 */
bool ringIsStrictlyConvex(const QVector<QPointF> &ring) noexcept
{
    const int n = ring.size();
    int sign = 0;
    for (int i = 0; i < n; ++i) {
        const QPointF &p = ring[i], &q = ring[(i + 1) % n], &r = ring[(i + 2) % n];
        const double c = cross(q - p, r - q);
        const int s = (c > 0.0) ? 1 : (c < 0.0) ? -1 : 0;
        if (s == 0 || (sign != 0 && s != sign))
            return false;
        sign = s;
    }
    return true;
}

/*! A polyline parametrised by normalised arc length t in [0,1]. */
struct ArcPolyline {
    QVector<QPointF> pts;
    QVector<double> cum; ///< cum[i] = arc length from pts[0] to pts[i].
    double length = 0.0;

    /**
     * @brief Copy a polyline and precompute cumulative Euclidean arc length.
     * @param[in] p Borrowed finite Cartesian points, unchanged; stored arrays own copies after
     * return. Empty produces length zero; repeated points can repeat cumulative values. No
     * validation, count budget, source-pointer retention, CRS transform or IO. Caller ensures
     * >=2 nondegenerate points before at().
     */
    explicit ArcPolyline(const QVector<QPointF> &p) : pts(p), cum(p.size(), 0.0)
    {
        for (int i = 1; i < pts.size(); ++i)
            cum[i] =
                cum[i - 1] + std::hypot(pts[i].x() - pts[i - 1].x(), pts[i].y() - pts[i - 1].y());
        length = pts.isEmpty() ? 0.0 : cum.last();
    }

    /**
     * @brief Interpolate the copied arc polyline at a normalized fraction with endpoint
     * clamping.
     * @param[in] t Finite fraction, <=0 returns first and >=1 last; interior expected [0,1].
     * @return Point in stored XY frame. Requires >=2 stored points for interior query and
     * consistent finite arc arrays; no count/NaN checks. Zero located interval uses lower point.
     * No allocation/mutation.
     */
    QPointF at(double t) const
    {
        if (t <= 0.0)
            return pts.first();
        if (t >= 1.0)
            return pts.last();
        const double s = t * length;
        // First vertex whose cumulative length exceeds s: segment (k-1, k).
        const int k = std::max(1, int(std::upper_bound(cum.begin(), cum.end(), s) - cum.begin()));
        const int kk = std::min(k, int(pts.size()) - 1);
        const double seg = cum[kk] - cum[kk - 1];
        const double f = seg > 0.0 ? (s - cum[kk - 1]) / seg : 0.0;
        return pts[kk - 1] + (pts[kk] - pts[kk - 1]) * f;
    }
};

/*! A broad phase over boundary edges; cells are never compared pairwise.
 *  Positive cells with cancelling interior edges and a simple boundary have
 *  winding number one inside that boundary, so they cannot overlap. */
struct BoundaryIndex {
    struct Box {
        double x0, y0, x1, y1;
        /**
         * @brief Test inclusive overlap of two axis-aligned edge bounds.
         * @param[in] b Borrowed compatible finite normalized box, unchanged.
         * @return True for overlap or touching edges/corners. No polygon test, allocation or
         * state mutation.
         */
        bool intersects(const Box &b) const
        {
            return x0 <= b.x1 && b.x0 <= x1 && y0 <= b.y1 && b.y0 <= y1;
        }
    };
    struct Node {
        Box box;
        int begin, end, left = -1, right = -1;
    };
    const QPolygonF &ring;
    QVector<Box> boxes;
    QVector<int> order;
    QVector<Node> nodes;

    /**
     * @brief Build a median-split bounding-volume tree over an immutable closed boundary's
     * edges.
     * @param[in] points Borrowed NONEMPTY finite open-ring vertices, stored by reference: must
     * outlive this index and remain unchanged. Each last-to-first edge gets owned bounds/order
     * metadata. Allocates recursive tree with <=8-edge leaves, splitting widest axis; counts
     * must fit int. No polygon validation or source ownership transfer. Queries require
     * immutable ring/index; no IO.
     */
    explicit BoundaryIndex(const QPolygonF &points) : ring(points)
    {
        boxes.reserve(ring.size());
        order.reserve(ring.size());
        for (int i = 0; i < ring.size(); ++i) {
            const auto &a = ring[i], &b = ring[(i + 1) % ring.size()];
            boxes.append({std::min(a.x(), b.x()), std::min(a.y(), b.y()), std::max(a.x(), b.x()),
                          std::max(a.y(), b.y())});
            order.append(i);
        }
        build(0, order.size());
    }
    /**
     * @brief Recursively partition a nonempty range of boundary-edge order indices.
     * @param[in] begin Inclusive valid order-array position.
     * @param[in] end Exclusive valid position, strictly greater than begin, unchecked.
     * @return Appended tree node index. Reorders owned edge indices by median center along
     * widest bound axis; ties use edge ID. Source ring/boxes unchanged; allocates
     * nodes/recursive stack, no retained arguments.
     */
    int build(int begin, int end)
    {
        Box box = boxes[order[begin]];
        for (int k = begin + 1; k < end; ++k) {
            const auto &b = boxes[order[k]];
            box.x0 = std::min(box.x0, b.x0);
            box.y0 = std::min(box.y0, b.y0);
            box.x1 = std::max(box.x1, b.x1);
            box.y1 = std::max(box.y1, b.y1);
        }
        const int node = nodes.size();
        nodes.append({box, begin, end});
        if (end - begin > 8) {
            const bool xAxis = box.x1 - box.x0 >= box.y1 - box.y0;
            const int mid = begin + (end - begin) / 2;
            std::nth_element(
                order.begin() + begin, order.begin() + mid, order.begin() + end, [&](int a, int b) {
                    const auto &u = boxes[a], &v = boxes[b];
                    const double uc = xAxis ? 0.5 * u.x0 + 0.5 * u.x1 : 0.5 * u.y0 + 0.5 * u.y1;
                    const double vc = xAxis ? 0.5 * v.x0 + 0.5 * v.x1 : 0.5 * v.y0 + 0.5 * v.y1;
                    return uc == vc ? a < b : uc < vc;
                });
            const int left = build(begin, mid), right = build(mid, end);
            nodes[node].left = left;
            nodes[node].right = right;
        }
        return node;
    }
    /**
     * @brief Compute three-point orientation using long-double intermediate
     * differences/products.
     * @param[in] a Borrowed finite origin point, unchanged.
     * @param[in] b Borrowed finite second point in same XY frame, unchanged.
     * @param[in] c Borrowed finite third point, unchanged.
     * @return Signed determinant in squared units, positive CCW. No adaptive exact
     * predicate/overflow audit, allocation, input retention or state mutation.
     */
    static long double orient(const QPointF &a, const QPointF &b, const QPointF &c)
    {
        return (static_cast<long double>(b.x()) - a.x()) *
                   (static_cast<long double>(c.y()) - a.y()) -
               (static_cast<long double>(b.y()) - a.y()) *
                   (static_cast<long double>(c.x()) - a.x());
    }
    /**
     * @brief Test exact collinearity and inclusive axis bounds for a point on a segment.
     * @param[in] a Borrowed finite segment start, unchanged.
     * @param[in] b Borrowed finite segment end, unchanged.
     * @param[in] p Borrowed compatible query, unchanged.
     * @return True iff long-double orientation exactly zero and coordinates within both endpoint
     * ranges. No tolerance, full geometry validation, allocation, pointer retention or source
     * mutation.
     */
    static bool onSegment(const QPointF &a, const QPointF &b, const QPointF &p)
    {
        return orient(a, b, p) == 0 && p.x() >= std::min(a.x(), b.x()) &&
               p.x() <= std::max(a.x(), b.x()) && p.y() >= std::min(a.y(), b.y()) &&
               p.y() <= std::max(a.y(), b.y());
    }
    /**
     * @brief Detect forbidden boundary-edge intersections including nonadjacent
     * touches/collinear overlap.
     * @param[in] i Valid zero-based ring edge index, unchecked.
     * @param[in] j Valid distinct ring edge index, unchecked.
     * @return True for proper crossing/contact or adjacent backtracking; prescribed shared
     * endpoint of consecutive edges alone allowed. Uses long-double orientation with exact zero
     * comparisons, not an adaptive robust predicate/tolerance. Ring finite/immutable
     * prerequisite; no allocation/mutation.
     */
    bool intersects(int i, int j) const
    {
        const int n = ring.size();
        const QPointF &a = ring[i], &b = ring[(i + 1) % n], &c = ring[j], &d = ring[(j + 1) % n];
        // Consecutive edges may share their prescribed endpoint, but may
        // not double back along each other.
        if ((i + 1) % n == j)
            return onSegment(a, b, d) || onSegment(c, d, a);
        if ((j + 1) % n == i)
            return onSegment(a, b, c) || onSegment(c, d, b);
        const long double abC = orient(a, b, c), abD = orient(a, b, d), cdA = orient(c, d, a),
                          cdB = orient(c, d, b);
        if (((abC > 0 && abD < 0) || (abC < 0 && abD > 0)) &&
            ((cdA > 0 && cdB < 0) || (cdA < 0 && cdB > 0)))
            return true;
        return (abC == 0 && onSegment(a, b, c)) || (abD == 0 && onSegment(a, b, d)) ||
               (cdA == 0 && onSegment(c, d, a)) || (cdB == 0 && onSegment(c, d, b));
    }
    /**
     * @brief Find one later-ID boundary edge conflicting with a queried edge through tree
     * pruning.
     * @param[in] edge Valid source edge index, unchecked.
     * @param[in] node Valid subtree node, default root zero; nonempty built tree required.
     * @return First found conflicting edge >edge, or -1. Tree traversal order determines first
     * result, not globally smallest edge ID. Inclusive bounds filter then detailed segment
     * check. Read-only, uses recursive stack, no source mutation or retained query references.
     */
    int conflict(int edge, int node = 0) const
    {
        const auto &n = nodes[node];
        if (!boxes[edge].intersects(n.box))
            return -1;
        if (n.left >= 0) {
            const int left = conflict(edge, n.left);
            return left >= 0 ? left : conflict(edge, n.right);
        }
        for (int k = n.begin; k < n.end; ++k) {
            const int other = order[k];
            if (other > edge && boxes[edge].intersects(boxes[other]) && intersects(edge, other))
                return other;
        }
        return -1;
    }
};

/**
 * @brief Audit swept subdivision/index capacity before allocating the final patch.
 * @param[in] p Borrowed validated swept input, unchanged; across>=1 and finite nonnegative along
 * required before division, finite nonzero centerline segments checked here.
 * @param[in,out] segmentParts Optional borrowed vector, appends each segment subdivision count
 * without clearing; failure may retain prior partial entries. Not retained.
 * @param[out] stationCount Optional borrowed final station count, assigned only on success.
 * @return Empty when vertex/cell/perimeter counts fit int, otherwise first diagnostic.
 * Near-integer length/along ratios snap only within capped coordinate-representation tolerance;
 * positive spacing otherwise rounds up. No practical memory cap, allocation exception handling
 * or source mutation.
 */
QString sweptStationCounts(const SweptPatch &p, QVector<int> *segmentParts = nullptr,
                           int *stationCount = nullptr)
{
    const qint64 indexLimit = std::numeric_limits<int>::max();
    const qint64 maxStations = indexLimit / (qint64(p.across) + 1);
    if (maxStations < 2 || p.centreline.size() > maxStations)
        return QStringLiteral(
            "Swept patch station/across counts exceed the vertex index capacity.");
    qint64 stations = 1;
    for (int i = 1; i < p.centreline.size(); ++i) {
        const auto &a = p.centreline[i - 1], &b = p.centreline[i];
        const double length = std::hypot(b.x() - a.x(), b.y() - a.y());
        if (!(length > 0) || !std::isfinite(length))
            return QStringLiteral(
                       "Swept patch segment ending at station %1 has a non-finite or zero length.")
                .arg(i);
        double parts = 1;
        if (p.along > 0) {
            const double ratio = length / p.along;
            if (!std::isfinite(ratio) || ratio > maxStations)
                return QStringLiteral(
                           "Swept patch along-spacing requests too many stations at segment %1.")
                    .arg(i - 1);
            // UTM coordinate subtraction can perturb an exact station count.
            // Snap only within a small representational tolerance; never
            // coarsen a meaningful fraction of a requested interval.
            const double coordinateScale =
                std::max({1.0, std::abs(a.x()), std::abs(a.y()), std::abs(b.x()), std::abs(b.y())});
            const double tolerance =
                std::min(1e-6, std::max(1e-9, 8 * std::numeric_limits<double>::epsilon() *
                                                  coordinateScale / p.along));
            const double rounded = std::round(ratio);
            parts =
                std::max(1.0, std::ceil(std::abs(ratio - rounded) <= tolerance ? rounded : ratio));
        }
        if (parts > maxStations - stations)
            return QStringLiteral(
                       "Swept patch station count exceeds the vertex index capacity at segment %1.")
                .arg(i - 1);
        stations += qint64(parts);
        if (segmentParts)
            segmentParts->append(int(parts));
    }
    if ((stations - 1) * p.across > indexLimit ||
        2 * (stations - 1) + 2 * qint64(p.across) > indexLimit)
        return QStringLiteral("Swept patch cell or boundary count exceeds the index capacity.");
    if (stationCount)
        *stationCount = int(stations);
    return {};
}

/**
 * @brief Check positive grid subdivisions and int-safe vertex/cell/perimeter products.
 * @param[in] n Subdivisions along one logical side, >=1 required.
 * @param[in] m Subdivisions along other side, >=1 required.
 * @return Empty on representable products, diagnostic otherwise. Uses 64-bit arithmetic; no
 * practical memory budget, mesh generation, caller-state mutation or IO.
 */
QString gridDimensions(int n, int m)
{
    if (n < 1 || m < 1)
        return QStringLiteral("Patch subdivisions must be >= 1 (n=%1, m=%2).").arg(n).arg(m);
    const qint64 limit = std::numeric_limits<int>::max();
    if ((qint64(n) + 1) * (qint64(m) + 1) > limit || qint64(n) * m > limit ||
        2 * (qint64(n) + m) > limit)
        return QStringLiteral(
                   "Patch subdivisions exceed the vertex, cell or boundary index capacity (n=%1, m=%2).")
            .arg(n)
            .arg(m);
    return {};
}

} // namespace

// ---------------------------------------------------------------------------
// Validation
// ---------------------------------------------------------------------------

/**
 * @brief Check four finite convex cyclic corners and representable subdivision products.
 * @param[in] p Borrowed structured patch, unchanged; corners may use either winding,
 * subdivisions >=1.
 * @return Empty when these prerequisites pass, first owned diagnostic otherwise. Does not
 * generate cells, inspect tag syntax, impose practical memory budget or certify final
 * interpolated arithmetic.
 */
QString validate(const StructuredPatch &p)
{
    if (p.corners.size() != 4)
        return QStringLiteral("Structured patch needs exactly 4 corners (got %1).")
            .arg(p.corners.size());
    for (const QPointF &c : p.corners)
        if (!finitePt(c))
            return QStringLiteral("Structured patch has a non-finite corner coordinate.");
    const QString dimensions = gridDimensions(p.n, p.m);
    if (!dimensions.isEmpty())
        return dimensions;
    if (!ringIsStrictlyConvex(p.corners))
        return QStringLiteral("Structured patch corners must form a convex quadrilateral "
                              "(a concave or self-intersecting outline folds the "
                              "transfinite map).");
    return QString();
}

/**
 * @brief Check swept input geometry and representable station/cell/boundary counts.
 * @param[in] p Borrowed patch, unchanged; >=2 finite nonduplicate consecutive centerline
 * vertices, positive finite width, across>=1 and finite nonnegative along spacing required,
 * common XY units.
 * @return Empty on input/count success, first owned diagnostic otherwise. Does not certify
 * offset folding/global overlap; those checks occur after makeSweptPatch generation. May
 * calculate counts, no mesh/source mutation, CRS transform or IO.
 */
QString validate(const SweptPatch &p)
{
    if (p.centreline.size() < 2)
        return QStringLiteral("Swept patch centreline needs at least 2 points.");
    for (int i = 0; i < p.centreline.size(); ++i)
        if (!finitePt(p.centreline[i]))
            return QStringLiteral(
                       "Swept patch centreline has a non-finite coordinate at station %1.")
                .arg(i);
    if (!(p.width > 0.0) || !std::isfinite(p.width))
        return QStringLiteral("Swept patch width must be > 0.");
    if (p.across < 1)
        return QStringLiteral("Swept patch needs at least 1 quad across.");
    if (p.along < 0.0 || !std::isfinite(p.along))
        return QStringLiteral("Swept patch along-spacing must be >= 0.");
    for (int i = 1; i < p.centreline.size(); ++i)
        if (p.centreline[i].x() == p.centreline[i - 1].x() &&
            p.centreline[i].y() == p.centreline[i - 1].y())
            return QStringLiteral("Swept patch centreline has a repeated vertex at station %1.")
                .arg(i);
    return sweptStationCounts(p);
}

/**
 * @brief Validate finite convex CCW cells, cancelling shared edges and one exact simple
 * perimeter.
 * @param[in] pm Borrowed patch, unchanged; patches with no vertices/quads or excessive counts
 * reject.
 * @param[out] boundary Optional borrowed ring, assigned only on success in open CCW order,
 * untouched failure.
 * @param[out] badVertex Optional borrowed diagnostic vertex, set on failure (-1 when
 * unavailable), untouched on success; not retained.
 * @param[out] otherVertex Optional distinct borrowed conflicting vertex, same assignment rules.
 * @return Empty on success or first owned diagnostic. Validates cell/boundary indices, winding,
 * convexity, manifold shared edges, boundary closure/coverage/intersection; ignores lines and
 * attributes. Allocates copied geometry/maps/BVH, no repair, source mutation, file/engine access
 * or retained pointers.
 */
static QString validatePatchMesh(const PatchMesh &pm, QPolygonF *boundary = nullptr,
                                 int *badVertex = nullptr, int *otherVertex = nullptr)
{
    const auto fail = [&](const QString &message, int vertex = -1, int other = -1) {
        if (badVertex)
            *badVertex = vertex;
        if (otherVertex)
            *otherVertex = other;
        return message;
    };
    if (pm.xy.isEmpty() || pm.quads.isEmpty())
        return fail(QStringLiteral("Patch must contain vertices and quadrilateral cells."));
    if (pm.xy.size() > std::numeric_limits<int>::max() ||
        pm.quads.size() > std::numeric_limits<int>::max() ||
        pm.boundarySegments.size() > std::numeric_limits<int>::max())
        return fail(QStringLiteral("Patch exceeds the vertex or cell index capacity."));
    QVector<MeshVertex> verts;
    verts.reserve(pm.xy.size());
    for (int i = 0; i < pm.xy.size(); ++i) {
        if (!finitePt(pm.xy[i]))
            return fail(QStringLiteral("Patch vertex %1 has a non-finite coordinate.").arg(i), i);
        MeshVertex v;
        v.xy = pm.xy[i];
        verts.append(v);
    }
    struct EdgeUse {
        int count = 0, direction = 0;
    };
    QHash<QPair<int, int>, EdgeUse> edges;
    const auto key = [](int a, int b) { return a < b ? qMakePair(a, b) : qMakePair(b, a); };
    for (int k = 0; k < pm.quads.size(); ++k) {
        const auto &q = pm.quads[k];
        if (!q.isQuad())
            return fail(QStringLiteral("Patch cell %1 is not a quadrilateral.").arg(k));
        for (int i = 0; i < 4; ++i)
            if (q.vertex(i) < 0 || q.vertex(i) >= pm.xy.size())
                return fail(
                    QStringLiteral("Patch cell %1 references a vertex outside the patch.").arg(k));
        if (!cellIsConvex(verts, q))
            return fail(QStringLiteral("Patch%1 quad %2 is folded or concave.")
                            .arg(pm.tag.isEmpty() ? QString() : QStringLiteral(" '%1'").arg(pm.tag))
                            .arg(k),
                        q.v0);
        const auto &a = pm.xy[q.v0], &b = pm.xy[q.v1], &c = pm.xy[q.v2], &d = pm.xy[q.v3];
        const double twiceArea = cross(b - a, c - a) + cross(c - a, d - a);
        if (!(twiceArea > 0) || !std::isfinite(twiceArea))
            return fail(
                QStringLiteral("Patch%1 quad %2 is clockwise, degenerate or has non-finite area.")
                    .arg(pm.tag.isEmpty() ? QString() : QStringLiteral(" '%1'").arg(pm.tag))
                    .arg(k),
                q.v0);
        for (int i = 0; i < 4; ++i) {
            const int from = q.vertex(i), to = q.vertex((i + 1) % 4);
            auto &edge = edges[key(from, to)];
            ++edge.count;
            edge.direction += from < to ? 1 : -1;
            if (edge.count > 2 || (edge.count == 2 && edge.direction != 0))
                return fail(
                    QStringLiteral(
                        "Patch has duplicated or inconsistently shared cell edges at vertex %1.")
                        .arg(from),
                    from);
        }
    }

    QSet<QPair<int, int>> expectedBoundary;
    for (auto it = edges.cbegin(); it != edges.cend(); ++it)
        if (it.value().count == 1)
            expectedBoundary.insert(it.key());
    if (expectedBoundary.isEmpty() || pm.boundarySegments.isEmpty())
        return fail(QStringLiteral("Patch has no closed boundary."));
    QHash<int, QVector<int>> adjacency;
    for (const auto &edge : pm.boundarySegments) {
        const int a = edge.first, b = edge.second;
        if (a < 0 || b < 0 || a >= pm.xy.size() || b >= pm.xy.size() || a == b)
            return fail(QStringLiteral("Patch boundary references an invalid vertex."));
        if (!expectedBoundary.remove(key(a, b)))
            return fail(
                QStringLiteral("Patch boundary contains an extra or repeated edge at vertex %1.")
                    .arg(a),
                a);
        adjacency[a].append(b);
        adjacency[b].append(a);
    }
    if (!expectedBoundary.isEmpty())
        return fail(QStringLiteral("Patch boundary is missing cell edges."));
    for (auto it = adjacency.cbegin(); it != adjacency.cend(); ++it)
        if (it.value().size() != 2)
            return fail(
                QStringLiteral("Patch boundary branches or is open at vertex %1.").arg(it.key()),
                it.key());

    QPolygonF ring;
    QVector<int> vertices;
    QSet<int> visited;
    const int start = pm.boundarySegments.first().first;
    int previous = -1, current = start;
    for (int i = 0; i < pm.boundarySegments.size(); ++i) {
        if (visited.contains(current))
            return fail(
                QStringLiteral(
                    "Patch boundary must form one loop without holes or disconnected pieces."),
                current);
        visited.insert(current);
        vertices.append(current);
        ring.append(pm.xy[current]);
        const auto &neighbours = adjacency[current];
        const int next = neighbours[0] == previous ? neighbours[1] : neighbours[0];
        previous = current;
        current = next;
    }
    if (current != start || visited.size() != adjacency.size())
        return fail(QStringLiteral("Patch boundary does not close into one loop."));
    const BoundaryIndex index(ring);
    for (int i = 0; i < ring.size(); ++i) {
        const int other = index.conflict(i);
        if (other >= 0)
            return fail(QStringLiteral(
                            "Patch boundary intersects or touches itself near vertices %1 and %2.")
                            .arg(vertices[i])
                            .arg(vertices[other]),
                        vertices[i], vertices[other]);
    }
    const double area = ringSignedArea(ring);
    if (!std::isfinite(area) || area == 0)
        return fail(QStringLiteral("Patch boundary has degenerate or non-finite area."));
    if (boundary) {
        if (area < 0)
            std::reverse(ring.begin(), ring.end());
        *boundary = std::move(ring);
    }
    return {};
}

/**
 * @brief Validate complete convex CCW quad patch topology and one simple matching boundary.
 * @param[in] pm Borrowed local-index patch, unchanged; vertex/cell/boundary counts must fit int.
 * @return Empty on finite coordinates, valid indices/convex positive-area cells, opposite
 * shared-edge directions and one nonintersecting/non-touching closed boundary matching outer
 * edges. Rejects holes, duplicates, remote boundary contacts and disconnected loops. Patch
 * lines/attribute values are not validated. Allocates vertex/edge/boundary index storage; no
 * source mutation, repair, transform or IO.
 */
QString validate(const PatchMesh &pm)
{
    return validatePatchMesh(pm);
}

/**
 * @brief Validate a quad patch and extract its ordered open CCW perimeter.
 * @param[in] pm Borrowed patch, unchanged; full validatePatchMesh checks apply.
 * @param[out] err Optional borrowed diagnostic, set on failure, not retained; success clearing
 * is stated below.
 * @return Owned ring without closing duplicate, empty on failure. err is assigned on EVERY call,
 * including cleared to empty on success. Segment input order/orientation is immaterial. No
 * source mutation or retained pointers; allocates validation/index/ring storage.
 */
QPolygonF orderedPatchBoundary(const PatchMesh &pm, QString *err)
{
    QPolygonF ring;
    const QString error = validatePatchMesh(pm, &ring);
    if (err)
        *err = error;
    return ring;
}

// ---------------------------------------------------------------------------
// Transfinite (Coons) patch on four straight sides
// ---------------------------------------------------------------------------

/**
 * @brief Generate a bilinear grid between four straight convex sides.
 * @param[in] p Borrowed cyclic four-corner patch, unchanged, common finite XY frame; n,m>=1
 * checked.
 * @param[out] err Optional borrowed diagnostic, set on failure, not retained; success clearing
 * is stated below.
 * @return Owned XY-only patch with local-index CCW quads/boundary and copied tag; empty on
 * checked failure. No elevations, source mutation, coordinate conversion, engine/file access or
 * retained inputs. Index capacity checks do not constitute a practical memory budget or
 * allocation-failure handling. Produces (n+1)*(m+1) vertices and n*m cells, accepts either
 * corner winding then emits CCW cells. Validates inputs and complete output; err is not cleared
 * on success. No cancellation or terrain sampling.
 */
PatchMesh makeTransfinitePatch(const StructuredPatch &p, QString *err)
{
    PatchMesh pm;
    const QString msg = validate(p);
    if (!msg.isEmpty()) {
        if (err)
            *err = msg;
        return pm;
    }

    pm.tag = p.tag;
    const int n = p.n, m = p.m;
    const QPointF &P0 = p.corners[0], &P1 = p.corners[1], &P2 = p.corners[2], &P3 = p.corners[3];

    // Coons interpolation with linear side curves: the two ruled surfaces
    // and the bilinear correction coincide, so the map is bilinear in (u,v).
    pm.xy.reserve((n + 1) * (m + 1));
    for (int j = 0; j <= m; ++j) {
        const double v = double(j) / m;
        for (int i = 0; i <= n; ++i) {
            const double u = double(i) / n;
            pm.xy.append((1 - u) * (1 - v) * P0 + u * (1 - v) * P1 + u * v * P2 + (1 - u) * v * P3);
        }
    }
    auto idx = [n](int i, int j) { return j * (n + 1) + i; };

    const bool ccw = ringSignedArea(p.corners) > 0.0;
    pm.quads.reserve(n * m);
    for (int j = 0; j < m; ++j)
        for (int i = 0; i < n; ++i) {
            MeshTriangle q;
            q.tag = p.tag;
            if (ccw) {
                q.v0 = idx(i, j);
                q.v1 = idx(i + 1, j);
                q.v2 = idx(i + 1, j + 1);
                q.v3 = idx(i, j + 1);
            } else {
                q.v0 = idx(i, j);
                q.v1 = idx(i, j + 1);
                q.v2 = idx(i + 1, j + 1);
                q.v3 = idx(i + 1, j);
            }
            pm.quads.append(q);
        }

    pm.boundarySegments.reserve(2 * (n + m));
    for (int i = 0; i < n; ++i) {
        pm.boundarySegments.append(qMakePair(idx(i, 0), idx(i + 1, 0)));
        pm.boundarySegments.append(qMakePair(idx(i, m), idx(i + 1, m)));
    }
    for (int j = 0; j < m; ++j) {
        pm.boundarySegments.append(qMakePair(idx(0, j), idx(0, j + 1)));
        pm.boundarySegments.append(qMakePair(idx(n, j), idx(n, j + 1)));
    }

    const QString bad = validate(pm);
    if (!bad.isEmpty()) {
        if (err)
            *err = bad;
        return PatchMesh();
    }
    return pm;
}

// ---------------------------------------------------------------------------
// Transfinite (Coons) patch on four polyline sides (redesign plan §4.2)
// ---------------------------------------------------------------------------

/**
 * @brief Generate a Coons grid between four ordered arc-length-parametrized polyline sides.
 * @param[in] sides Borrowed four finite >=2-point sides in cyclic forward order, unchanged.
 * Adjacent endpoints must agree within 1e-9*max(1,absolute coordinate scale); positive arc
 * lengths required.
 * @param[in] n Positive subdivisions along opposite sides 0/2, index products checked.
 * @param[in] m Positive subdivisions along opposite sides 1/3, index products checked.
 * @param[in] tag Borrowed patch/cell tag copied verbatim, no token-grammar validation.
 * @param[out] err Optional borrowed diagnostic, set on failure, not retained; success clearing
 * is stated below.
 * @return Owned XY-only patch with local-index CCW quads/boundary and copied tag; empty on
 * checked failure. No elevations, source mutation, coordinate conversion, engine/file access or
 * retained inputs. Index capacity checks do not constitute a practical memory budget or
 * allocation-failure handling. Boundary samples lie directly on supplied polyline curves;
 * interior uses Coons correction. Does not automatically preserve every intermediate side vertex
 * as a mesh station. Final full patch validation rejects folding/invalid boundary. err not
 * cleared on success. No callbacks/cancellation.
 */
static PatchMesh transfiniteImpl(const QVector<QVector<QPointF>> &sides, int n, int m,
                                const QString &tag, QString *err, bool stationed)
{
    PatchMesh pm;
    auto fail = [&](const QString &msg) {
        if (err)
            *err = msg;
        return PatchMesh();
    };

    if (sides.size() != 4)
        return fail(
            QStringLiteral("Transfinite patch needs exactly 4 sides (got %1).").arg(sides.size()));
    const QString dimensions = gridDimensions(n, m);
    if (!dimensions.isEmpty())
        return fail(dimensions);
    double scale = 1.0;
    for (int k = 0; k < 4; ++k) {
        if (sides[k].size() < 2)
            return fail(
                QStringLiteral("Transfinite patch side %1 needs at least 2 points.").arg(k));
        for (const QPointF &p : sides[k]) {
            if (!finitePt(p))
                return fail(QStringLiteral("Transfinite patch side %1 has a non-finite coordinate.")
                                .arg(k));
            scale = std::max({scale, std::abs(p.x()), std::abs(p.y())});
        }
    }
    for (int k = 0; k < 4; ++k) {
        const QPointF &a = sides[k].last(), &b = sides[(k + 1) % 4].first();
        if (stationed ? (a.x()!=b.x() || a.y()!=b.y()) :
            (std::abs(a.x() - b.x()) > 1e-9 * scale || std::abs(a.y() - b.y()) > 1e-9 * scale))
            return fail(
                QStringLiteral("Transfinite patch side %1 does not end where side %2 starts.")
                    .arg(k)
                    .arg((k + 1) % 4));
    }

    // Side curves, all arc-length parametrised on [0,1]:
    //   B(u) = sides[0], c0 -> c1        (bottom, v = 0)
    //   R(v) = sides[1], c1 -> c2        (right,  u = 1)
    //   T(u) = sides[2] reversed, c3 -> c2   (top,  v = 1)
    //   L(v) = sides[3] reversed, c0 -> c3   (left, u = 0)
    // so that B(0)=L(0)=c0, B(1)=R(0)=c1, R(1)=T(1)=c2, T(0)=L(1)=c3.
    const ArcPolyline B(sides[0]), R(sides[1]), Tr(sides[2]), Lr(sides[3]);
    const ArcPolyline *arcs[4] = {&B, &R, &Tr, &Lr};
    for (int k = 0; k < 4; ++k)
        if (!(arcs[k]->length > 0.0))
            return fail(QStringLiteral("Transfinite patch side %1 has zero length.").arg(k));
    const QPointF P00 = sides[0].first(), P10 = sides[1].first();
    const QPointF P11 = sides[2].first(), P01 = sides[3].first();
    auto T = [&](double u) { return Tr.at(1.0 - u); };
    auto L = [&](double v) { return Lr.at(1.0 - v); };
    auto bottom=[&](int i){return stationed?sides[0][i]:B.at(double(i)/n);};
    auto top=[&](int i){return stationed?sides[2][n-i]:T(double(i)/n);};
    auto left=[&](int j){return stationed?sides[3][m-j]:L(double(j)/m);};
    auto right=[&](int j){return stationed?sides[1][j]:R.at(double(j)/m);};

    pm.tag = tag;
    pm.xy.reserve((n + 1) * (m + 1));
    for (int j = 0; j <= m; ++j) {
        const double v = double(j) / m;
        for (int i = 0; i <= n; ++i) {
            const double u = double(i) / n;
            // Boundary vertices sit exactly ON the polylines (the Coons formula
            // reproduces them only up to rounding).
            if (j == 0) {
                pm.xy.append(bottom(i));
                continue;
            }
            if (j == m) {
                pm.xy.append(top(i));
                continue;
            }
            if (i == 0) {
                pm.xy.append(left(j));
                continue;
            }
            if (i == n) {
                pm.xy.append(right(j));
                continue;
            }
            if(stationed) {
                pm.xy.append(P00+(1-v)*(bottom(i)-P00)+v*(top(i)-P00)+
                    (1-u)*(left(j)-P00)+u*(right(j)-P00)-
                    (u*(1-v)*(P10-P00)+(1-u)*v*(P01-P00)+u*v*(P11-P00)));
                continue;
            }
            pm.xy.append(
                (1 - v) * B.at(u) + v * T(u) + (1 - u) * L(v) + u * R.at(v) -
                ((1 - u) * (1 - v) * P00 + u * (1 - v) * P10 + (1 - u) * v * P01 + u * v * P11));
        }
    }
    auto idx = [n](int i, int j) { return j * (n + 1) + i; };

    // Orientation from the full boundary loop (c0 -> c1 -> c2 -> c3).
    QVector<QPointF> loop;
    for (int k = 0; k < 4; ++k)
        for (int i = 0; i + 1 < sides[k].size(); ++i)
            loop.append(sides[k][i]);
    const bool ccw = ringSignedArea(loop) > 0.0;
    pm.quads.reserve(n * m);
    for (int j = 0; j < m; ++j)
        for (int i = 0; i < n; ++i) {
            MeshTriangle q;
            q.tag = tag;
            if (ccw) {
                q.v0 = idx(i, j);
                q.v1 = idx(i + 1, j);
                q.v2 = idx(i + 1, j + 1);
                q.v3 = idx(i, j + 1);
            } else {
                q.v0 = idx(i, j);
                q.v1 = idx(i, j + 1);
                q.v2 = idx(i + 1, j + 1);
                q.v3 = idx(i + 1, j);
            }
            pm.quads.append(q);
        }

    pm.boundarySegments.reserve(2 * (n + m));
    for (int i = 0; i < n; ++i) {
        pm.boundarySegments.append(qMakePair(idx(i, 0), idx(i + 1, 0)));
        pm.boundarySegments.append(qMakePair(idx(i, m), idx(i + 1, m)));
    }
    for (int j = 0; j < m; ++j) {
        pm.boundarySegments.append(qMakePair(idx(0, j), idx(0, j + 1)));
        pm.boundarySegments.append(qMakePair(idx(n, j), idx(n, j + 1)));
    }

    const QString bad = validate(pm);
    if (!bad.isEmpty())
        return fail(bad);
    return pm;
}

PatchMesh makeTransfinitePatch(const QVector<QVector<QPointF>> &sides,int n,int m,
                              const QString &tag,QString *err)
{
    return transfiniteImpl(sides,n,m,tag,err,false);
}

PatchMesh makeTransfinitePatchAtStations(const QVector<QVector<QPointF>> &sides,
                                       const QString &tag,QString *err)
{
    if(sides.size()!=4 || sides[0].size()<2 || sides[1].size()<2 ||
       sides[0].size()!=sides[2].size() || sides[1].size()!=sides[3].size()) {
        if(err) *err=QStringLiteral("Stationed patch needs four cyclic sides with matching opposite counts.");
        return {};
    }
    return transfiniteImpl(sides,sides[0].size()-1,sides[1].size()-1,tag,err,true);
}

namespace
{
/**
 * @brief Validate mapped sides, associate optional physical axis and generate a tagged Coons
 * patch.
 * @param[in] ring Borrowed finite ring, either winding/exact closing duplicate, unchanged.
 * @param[in] corners Borrowed four strictly increasing open-ring corner positions, unchanged.
 * @param[in] hAlong Positive finite first physical/logical spacing in ring horizontal units.
 * @param[in] hAcross Positive finite second spacing in same units.
 * @param[in] alongAngleDeg Optional borrowed finite undirected degrees from +x; null preserves
 * logical side-pair assignment, nonnull associates pair by mean squared chord-axis projection.
 * Not retained.
 * @param[in] tag Borrowed copied patch/cell tag, unchanged.
 * @param[out] err Optional borrowed diagnostic, set on failure, not retained; success clearing
 * is stated below.
 * @return Owned XY-only patch with local-index CCW quads/boundary and copied tag; empty on
 * checked failure. No elevations, source mutation, coordinate conversion, engine/file access or
 * retained inputs. Index capacity checks do not constitute a practical memory budget or
 * allocation-failure handling. Clears err at entry. Rejects ambiguous axis association, invalid
 * endpoints/sides, nonfinite lengths and unsafe rounded subdivision products; delegates full
 * output validation. No callbacks/cancellation.
 */
PatchMesh makeMappedPatchImpl(const QPolygonF &ring, const QVector<int> &corners, double hAlong,
                              double hAcross, const double *alongAngleDeg, const QString &tag,
                              QString *err)
{
    if (err)
        err->clear();
    auto fail = [&](const QString &msg) {
        if (err)
            *err = msg;
        return PatchMesh();
    };
    if (!(hAlong > 0.0) || !std::isfinite(hAlong) || !(hAcross > 0.0) || !std::isfinite(hAcross))
        return fail(QStringLiteral("Mapped patch spacing must be finite and > 0."));
    if (alongAngleDeg && !std::isfinite(*alongAngleDeg))
        return fail(QStringLiteral("Mapped patch along-axis angle must be finite."));
    if (corners.size() != 4)
        return fail(
            QStringLiteral("Mapped patch needs exactly 4 corners (got %1).").arg(corners.size()));

    // Corner indices refer to the caller's ring. Both windings work; only an
    // exact closing duplicate is removed (QPointF equality is coordinate-relative).
    int nr = ring.size();
    if (nr >= 2 && ring.first().x() == ring.last().x() && ring.first().y() == ring.last().y())
        --nr;
    if (nr < 4)
        return fail(
            QStringLiteral("Mapped patch ring needs at least 4 vertices (got %1).").arg(nr));
    for (const auto &point : ring)
        if (!finitePt(point))
            return fail(QStringLiteral("Mapped patch ring has a non-finite coordinate."));
    for (int k = 0; k < 4; ++k) {
        if (corners[k] < 0 || corners[k] >= nr)
            return fail(QStringLiteral("Mapped patch corner %1 (ring index %2) is out of range.")
                            .arg(k)
                            .arg(corners[k]));
        if (k > 0 && corners[k] <= corners[k - 1])
            return fail(
                QStringLiteral("Mapped patch corners must be strictly increasing ring indices."));
    }

    // Side k walks the ring forward from corners[k] to corners[(k+1)%4].
    QVector<QVector<QPointF>> sides(4);
    double len[4] = {0.0, 0.0, 0.0, 0.0};
    for (int k = 0; k < 4; ++k) {
        int i = corners[k];
        const int end = corners[(k + 1) % 4];
        sides[k].append(ring[i]);
        do {
            const int nx = (i + 1) % nr;
            len[k] += std::hypot(ring[nx].x() - ring[i].x(), ring[nx].y() - ring[i].y());
            if (!std::isfinite(len[k]))
                return fail(QStringLiteral("Mapped patch side %1 has a non-finite length.").arg(k));
            sides[k].append(ring[nx]);
            i = nx;
        } while (i != end);
        if (!(len[k] > 0.0))
            return fail(QStringLiteral("Mapped patch side %1 has zero length.").arg(k));
    }

    double h0 = hAlong, h1 = hAcross;
    if (alongAngleDeg) {
        // Chords identify logical sides independently of intermediate vertices.
        // Squared projections are undirected and give equal weight to each side,
        // so changing the first corner, winding or side lengths cannot swap the
        // physical axes. Arc lengths below still determine subdivision counts.
        const double radians = std::remainder(*alongAngleDeg, 180.0) * (std::acos(-1.0) / 180.0);
        const QPointF axis(std::cos(radians), std::sin(radians));
        double score[2] = {0.0, 0.0};
        for (int k = 0; k < 4; ++k) {
            const QPointF delta = sides[k].last() - sides[k].first();
            const double chord = std::hypot(delta.x(), delta.y());
            if (!(chord > 0.0) || !std::isfinite(chord))
                return fail(QStringLiteral(
                                "Mapped patch side %1 has no finite distinct endpoint direction.")
                                .arg(k));
            const double projection =
                (delta.x() / chord) * axis.x() + (delta.y() / chord) * axis.y();
            score[k % 2] += 0.5 * projection * projection;
        }
        if (std::abs(score[0] - score[1]) <= 1e-10)
            return fail(QStringLiteral(
                "Mapped patch along-axis association is ambiguous between the two opposite side pairs. Choose an axis more clearly aligned with one pair."));
        if (score[1] > score[0])
            std::swap(h0, h1);
    }

    // Preserve the scalar API's nearest-integer counts, but validate before any
    // integer conversion. Half-sums avoid overflowing two individually finite
    // lengths; gridDimensions also checks vertex/cell/perimeter products.
    auto count = [](double a, double b, double spacing, int &out) {
        const double ratio = (0.5 * a + 0.5 * b) / spacing;
        if (!std::isfinite(ratio))
            return false;
        const double rounded = std::max(1.0, std::floor(ratio + 0.5));
        if (rounded > double(std::numeric_limits<int>::max() - 1))
            return false;
        out = int(rounded);
        return true;
    };
    int n = 0, m = 0;
    if (!count(len[0], len[2], h0, n) || !count(len[1], len[3], h1, m))
        return fail(QStringLiteral("Mapped patch spacing exceeds the subdivision index capacity."));
    const QString dimensions = gridDimensions(n, m);
    if (!dimensions.isEmpty())
        return fail(dimensions);
    return makeTransfinitePatch(sides, n, m, tag, err);
}
} // namespace

/**
 * @brief Choose isotropic subdivision counts from opposite-side arc lengths and build a Coons
 * patch.
 * @param[in] ring Borrowed finite ring in either winding, unchanged; exact repeated closing
 * point allowed.
 * @param[in] corners Borrowed four strictly increasing indices in original open-ring order,
 * unchanged.
 * @param[in] h Positive finite target spacing in ring XY units; opposite mean lengths round to
 * nearest positive integer count, so h is a target rather than strict maximum gap.
 * @param[in] tag Borrowed copied patch/cell tag, unchanged.
 * @param[out] err Optional borrowed diagnostic, set on failure, not retained; success clearing
 * is stated below.
 * @return Owned XY-only patch with local-index CCW quads/boundary and copied tag; empty on
 * checked failure. No elevations, source mutation, coordinate conversion, engine/file access or
 * retained inputs. Index capacity checks do not constitute a practical memory budget or
 * allocation-failure handling. err is cleared at entry, then set on failure. Side
 * geometry/count/final patch checks apply.
 */
PatchMesh makeMappedPatch(const QPolygonF &ring, const QVector<int> &corners, double h,
                          const QString &tag, QString *err)
{
    return makeMappedPatchImpl(ring, corners, h, h, nullptr, tag, err);
}

/**
 * @brief Associate physical directional spacings with opposite logical side pairs before Coons
 * generation.
 * @param[in] ring Borrowed finite cyclic ring, either winding, unchanged; exact closing
 * duplicate allowed.
 * @param[in] corners Borrowed four strictly increasing open-ring indices, unchanged.
 * @param[in] hAlong Positive finite target spacing along the selected physical axis, ring
 * horizontal units.
 * @param[in] hAcross Positive finite orthogonal target spacing in same units.
 * @param[in] alongAngleDeg Finite undirected axis counterclockwise from +x, degrees modulo 180.
 * @param[in] tag Borrowed copied tag, unchanged.
 * @param[out] err Optional borrowed diagnostic, set on failure, not retained; success clearing
 * is stated below.
 * @return Owned XY-only patch with local-index CCW quads/boundary and copied tag; empty on
 * checked failure. No elevations, source mutation, coordinate conversion, engine/file access or
 * retained inputs. Index capacity checks do not constitute a practical memory budget or
 * allocation-failure handling. Mean squared chord-axis projections choose which side pair
 * receives hAlong; equal scores within 1e-10 reject as ambiguous. Arc lengths set
 * nearest-integer counts. err cleared at entry. Rotation of ring start/winding does not
 * intentionally exchange physical spacings; no CRS/geodesic operations.
 */
PatchMesh makeMappedPatch(const QPolygonF &ring, const QVector<int> &corners, double hAlong,
                          double hAcross, double alongAngleDeg, const QString &tag, QString *err)
{
    return makeMappedPatchImpl(ring, corners, hAlong, hAcross, &alongAngleDeg, tag, err);
}

// ---------------------------------------------------------------------------
// Swept patch along a centreline
// ---------------------------------------------------------------------------

/**
 * @brief Sweep a finite centerline with mitred offsets into a globally validated quad strip.
 * @param[in] p Borrowed centerline/positive width/across/along spacing/tag, unchanged, common XY
 * units. Original vertices retained and each segment subdivided to target along spacing when
 * positive.
 * @param[out] err Optional borrowed diagnostic, set on failure, not retained; success clearing
 * is stated below.
 * @return Owned XY-only patch with local-index CCW quads/boundary and copied tag; empty on
 * checked failure. No elevations, source mutation, coordinate conversion, engine/file access or
 * retained inputs. Index capacity checks do not constitute a practical memory budget or
 * allocation-failure handling. err cleared at entry. Checks counts before allocation, rejects
 * collapsed station geometry, reversals/excessively sharp mitres, nonfinite offsets, invalid
 * cells and remote boundary contacts. No snapping/automatic bend repair, terrain z, callbacks or
 * cancellation.
 */
PatchMesh makeSweptPatch(const SweptPatch &p, QString *err)
{
    PatchMesh pm;
    if (err)
        err->clear();
    const QString msg = validate(p);
    if (!msg.isEmpty()) {
        if (err)
            *err = msg;
        return pm;
    }
    pm.tag = p.tag;

    // Stations: the centreline vertices, each segment resampled to the
    // target along-spacing (original vertices always kept).
    QVector<int> segmentParts;
    int ns = 0;
    const QString countError = sweptStationCounts(p, &segmentParts, &ns);
    if (!countError.isEmpty()) {
        if (err)
            *err = countError;
        return pm;
    }
    QVector<QPointF> st;
    st.reserve(ns);
    st.append(p.centreline.first());
    for (int i = 1; i < p.centreline.size(); ++i) {
        const QPointF &a = p.centreline[i - 1], &b = p.centreline[i];
        const int parts = segmentParts[i - 1];
        for (int k = 1; k <= parts; ++k) {
            const QPointF point = k == parts ? b : a + (b - a) * (double(k) / parts);
            if (point.x() == st.last().x() && point.y() == st.last().y()) {
                if (err)
                    *err =
                        QStringLiteral(
                            "Swept patch stations collapse at station %1; increase the along-spacing.")
                            .arg(st.size());
                return PatchMesh();
            }
            st.append(point);
        }
    }

    // Left-hand unit normal of each segment, mitred at interior stations.
    QVector<QPointF> segN(ns - 1);
    for (int i = 0; i < ns - 1; ++i) {
        const QPointF d = st[i + 1] - st[i];
        const double len = std::hypot(d.x(), d.y());
        if (!(len > 0) || !std::isfinite(len)) {
            if (err)
                *err = QStringLiteral("Swept patch has an invalid interval at station %1.").arg(i);
            return PatchMesh();
        }
        segN[i] = QPointF(-d.y() / len, d.x() / len);
    }
    QVector<QPointF> offs(ns); // per-station offset vector for unit distance
    for (int i = 0; i < ns; ++i) {
        if (i == 0) {
            offs[i] = segN[0];
            continue;
        }
        if (i == ns - 1) {
            offs[i] = segN[ns - 2];
            continue;
        }
        const QPointF &n0 = segN[i - 1], &n1 = segN[i];
        QPointF nb = n0 + n1;
        const double lb = std::hypot(nb.x(), nb.y());
        if (lb < 1e-12) {
            if (err)
                *err = QStringLiteral("Swept patch centreline reverses on itself at station %1.")
                           .arg(i);
            return PatchMesh();
        }
        nb /= lb;
        const double c = nb.x() * n0.x() + nb.y() * n0.y(); // cos(half turn)
        if (c < 1e-6) {
            if (err)
                *err = QStringLiteral("Swept patch centreline turns too sharply at station %1.")
                           .arg(i);
            return PatchMesh();
        }
        offs[i] = nb / c; // mitre length
    }

    const int na = p.across;
    auto idx = [na](int i, int j) { return i * (na + 1) + j; };
    pm.xy.reserve(ns * (na + 1));
    for (int i = 0; i < ns; ++i)
        for (int j = 0; j <= na; ++j) {
            const double s = p.width * (double(j) / na - 0.5);
            const QPointF point = st[i] + offs[i] * s;
            if (!finitePt(point)) {
                if (err)
                    *err = QStringLiteral("Swept patch offset is non-finite at station %1.").arg(i);
                return PatchMesh();
            }
            pm.xy.append(point);
        }

    // Along the tangent then across to the left: CCW.
    pm.quads.reserve((ns - 1) * na);
    for (int i = 0; i < ns - 1; ++i)
        for (int j = 0; j < na; ++j) {
            MeshTriangle q;
            q.tag = p.tag;
            q.v0 = idx(i, j);
            q.v1 = idx(i + 1, j);
            q.v2 = idx(i + 1, j + 1);
            q.v3 = idx(i, j + 1);
            pm.quads.append(q);
        }

    pm.boundarySegments.reserve(2 * (ns - 1) + 2 * na);
    for (int i = 0; i < ns - 1; ++i) {
        pm.boundarySegments.append(qMakePair(idx(i, 0), idx(i + 1, 0)));
        pm.boundarySegments.append(qMakePair(idx(i, na), idx(i + 1, na)));
    }
    for (int j = 0; j < na; ++j) {
        pm.boundarySegments.append(qMakePair(idx(0, j), idx(0, j + 1)));
        pm.boundarySegments.append(qMakePair(idx(ns - 1, j), idx(ns - 1, j + 1)));
    }

    // Check the whole offset boundary as well as individual cells. Distant
    // stations can cross even when every local quad remains convex.
    int badVertex = -1, otherVertex = -1;
    const QString bad = validatePatchMesh(pm, nullptr, &badVertex, &otherVertex);
    if (!bad.isEmpty()) {
        if (err) {
            if (otherVertex >= 0)
                *err = QStringLiteral("Swept patch offsets conflict near stations %1 and %2: %3")
                           .arg(badVertex / (na + 1))
                           .arg(otherVertex / (na + 1))
                           .arg(bad);
            else if (badVertex >= 0)
                *err = QStringLiteral("Swept patch offset is invalid near station %1: %2")
                           .arg(badVertex / (na + 1))
                           .arg(bad);
            else
                *err = bad;
        }
        return PatchMesh();
    }
    return pm;
}

/**
 * @brief Build a conforming strip between two finite simple disjoint open banks.
 * @param[in] p Borrowed bank arrays/across/along/tag, unchanged. across>=1, finite nonnegative
 * along; each bank >=2 finite nonduplicate consecutive points and not explicitly closed. Common
 * XY units.
 * @param[out] err Optional borrowed diagnostic, set on failure, not retained; success clearing
 * is stated below.
 * @return Owned XY-only patch with local-index CCW quads/boundary and copied tag; empty on
 * checked failure. No elevations, source mutation, coordinate conversion, engine/file access or
 * retained inputs. Index capacity checks do not constitute a practical memory budget or
 * allocation-failure handling. err cleared at entry. Pairs ends by minimum total connection
 * length, rejects near ties, canonicalizes bank direction/order, and requires simple outline
 * including connectors. Unions exact normalized arc-vertex fractions so every original bend
 * survives; positive along subdivides using longer bank advance. Rejects unrepresentable
 * fractions/counts/points, folded cells and remote contacts. This is normalized arc
 * correspondence, not a surveyed station match. No source reordering or cancellation.
 */
PatchMesh makeBankPairPatch(const BankPairPatch &p, QString *err)
{
    if (err)
        err->clear();
    const auto fail = [&](const QString &message) {
        if (err)
            *err = message;
        return PatchMesh();
    };
    if (p.across < 1)
        return fail(QStringLiteral("Bank-pair across count must be positive."));
    if (!std::isfinite(p.along) || p.along < 0)
        return fail(QStringLiteral("Bank-pair along spacing must be finite and nonnegative."));
    const qint64 maxStations = std::numeric_limits<int>::max() / (qint64(p.across) + 1);
    if (maxStations < 2 || p.bankA.size() > maxStations || p.bankB.size() > maxStations)
        return fail(
            QStringLiteral("Bank-pair station/across counts exceed the vertex index capacity."));
    const auto same = [](const QPointF &a, const QPointF &b) {
        return a.x() == b.x() && a.y() == b.y();
    };
    const auto less = [](const QPointF &a, const QPointF &b) {
        return a.x() < b.x() || (a.x() == b.x() && a.y() < b.y());
    };
    double coordinateScale = 1;
    int bankNumber = 0;
    for (const auto *bank : {&p.bankA, &p.bankB}) {
        ++bankNumber;
        if (bank->size() < 2)
            return fail(QStringLiteral("Bank %1 needs at least two vertices.").arg(bankNumber));
        for (int i = 0; i < bank->size(); ++i) {
            const auto &point = bank->at(i);
            if (!finitePt(point))
                return fail(QStringLiteral("Bank %1 has a non-finite vertex at %2.")
                                .arg(bankNumber)
                                .arg(i));
            coordinateScale = std::max({coordinateScale, std::abs(point.x()), std::abs(point.y())});
            if (i > 0 && same(point, bank->at(i - 1)))
                return fail(QStringLiteral("Bank %1 has duplicate consecutive vertices at %2.")
                                .arg(bankNumber)
                                .arg(i));
        }
        if (same(bank->first(), bank->last()))
            return fail(QStringLiteral("Bank %1 is closed; two open bank lines are required.")
                            .arg(bankNumber));
    }

    QVector<QPointF> bankA = p.bankA, bankB = p.bankB;
    const auto distance = [](const QPointF &a, const QPointF &b) {
        return std::hypot(a.x() - b.x(), a.y() - b.y());
    };
    // Half-sums avoid overflow when adding individually representable lengths.
    const double direct =
        0.5 * distance(bankA.first(), bankB.first()) + 0.5 * distance(bankA.last(), bankB.last());
    const double reverse =
        0.5 * distance(bankA.first(), bankB.last()) + 0.5 * distance(bankA.last(), bankB.first());
    if (!std::isfinite(direct) || !std::isfinite(reverse))
        return fail(QStringLiteral("Bank endpoint connection lengths are non-finite."));
    const double pairingTolerance =
        64 * std::numeric_limits<double>::epsilon() * std::max(direct, reverse);
    if (std::abs(direct - reverse) <= pairingTolerance)
        return fail(QStringLiteral(
            "Bank endpoint pairing is ambiguous; use bank lines with clearly corresponding ends."));
    if (reverse < direct)
        std::reverse(bankB.begin(), bankB.end());

    // Canonical endpoint pairs make reversed/swapped input use the same arc
    // accumulation order. This is an ordering rule, not a world-space tolerance.
    const auto orderedPair = [&](QPointF a, QPointF b) {
        if (less(b, a))
            std::swap(a, b);
        return qMakePair(a, b);
    };
    const auto start = orderedPair(bankA.first(), bankB.first());
    const auto end = orderedPair(bankA.last(), bankB.last());
    if (less(end.first, start.first) ||
        (same(end.first, start.first) && less(end.second, start.second))) {
        std::reverse(bankA.begin(), bankA.end());
        std::reverse(bankB.begin(), bankB.end());
    }
    if (less(bankB.first(), bankA.first()))
        std::swap(bankA, bankB);

    // The bank edges and the two endpoint connectors must form one simple
    // boundary before subdivision. Reuse the indexed inclusive-contact check;
    // no all-segment or all-cell quadratic scan is introduced.
    QPolygonF outline;
    outline.reserve(bankA.size() + bankB.size());
    for (const auto &point : bankA)
        outline.append(point);
    for (auto it = bankB.crbegin(); it != bankB.crend(); ++it)
        outline.append(*it);
    for (int i = 0; i < outline.size(); ++i)
        if (same(outline[i], outline[(i + 1) % outline.size()]))
            return fail(QStringLiteral("Bank boundaries touch at an endpoint."));
    const BoundaryIndex index(outline);
    for (int edge = 0; edge < outline.size(); ++edge) {
        const int other = index.conflict(edge);
        if (other >= 0)
            return fail(
                QStringLiteral(
                    "Bank boundaries or endpoint connectors touch, overlap or intersect at segments %1 and %2.")
                    .arg(edge)
                    .arg(other));
    }
    const double boundaryArea = ringSignedArea(outline);
    if (boundaryArea == 0 || !std::isfinite(boundaryArea))
        return fail(QStringLiteral("Bank boundary has zero or non-finite area."));
    const bool ccw = boundaryArea > 0;

    struct BankArc {
        const QVector<QPointF> *points;
        QVector<double> fractions;
        double length = 0;
        /* @brief Retain a borrowed immutable canonical bank for local arc correspondence.
         * @param[in] bank Nonempty finite bank; must outlive this local helper, not owned.
         * No validation or allocation here; prepare() establishes the normalized ladder.
         */
        explicit BankArc(const QVector<QPointF> &bank) : points(&bank) {}
        /* @brief Accumulate strictly representable bank arc intervals then normalize fractions.
         * @param[in] number One-based bank number used only in diagnostics.
         * @return Empty success, diagnostic on collapsed/nonfinite interval; failure retains partial
         * fractions/length. Single-use fresh helper, finite >=2-point bank required; allocates ladder.
         */
        QString prepare(int number)
        {
            fractions.reserve(points->size());
            fractions.append(0);
            for (int i = 1; i < points->size(); ++i) {
                const auto delta = points->at(i) - points->at(i - 1);
                const double segment = std::hypot(delta.x(), delta.y());
                const double next = length + segment;
                if (!(segment > 0) || !std::isfinite(next) || !(next > length))
                    return QStringLiteral(
                               "Bank %1 has an unrepresentable arc interval at vertex %2.")
                        .arg(number)
                        .arg(i);
                length = next;
                fractions.append(length);
            }
            for (int i = 1; i < fractions.size(); ++i) {
                fractions[i] /= length;
                if (!(fractions[i] > fractions[i - 1]))
                    return QStringLiteral("Bank %1 normalized stations collapse at vertex %2.")
                        .arg(number)
                        .arg(i);
            }
            fractions.last() = 1;
            return {};
        }
        /* @brief Interpolate a prepared bank at an exact or bracketed normalized station.
         * @param[in] fraction Finite [0,1] normalized arc coordinate, intended bounds unchecked.
         * @return Bank-frame point; exact fractions preserve original vertices, above range clamps
         * last. Requires successful prepare and immutable bank; no allocation or mutation.
         */
        QPointF at(double fraction) const
        {
            const auto it = std::lower_bound(fractions.cbegin(), fractions.cend(), fraction);
            if (it == fractions.cend())
                return points->last();
            const int right = int(it - fractions.cbegin());
            if (*it == fraction || right == 0)
                return points->at(right);
            const double t =
                (fraction - fractions[right - 1]) / (fractions[right] - fractions[right - 1]);
            return points->at(right - 1) + (points->at(right) - points->at(right - 1)) * t;
        }
    };
    BankArc arcA(bankA), arcB(bankB);
    QString error = arcA.prepare(1);
    if (error.isEmpty())
        error = arcB.prepare(2);
    if (!error.isEmpty())
        return fail(error);

    // Exact fraction equality alone coalesces stations. Near fractions must
    // remain distinct to preserve both bends; unrepresentable resulting cells
    // are rejected instead of silently dropping a bank vertex.
    QVector<double> breaks;
    int a = 0, b = 0;
    while (a < arcA.fractions.size() || b < arcB.fractions.size()) {
        const double fa = a < arcA.fractions.size() ? arcA.fractions[a] : 2;
        const double fb = b < arcB.fractions.size() ? arcB.fractions[b] : 2;
        const double fraction = std::min(fa, fb);
        if (fa == fraction)
            ++a;
        if (fb == fraction)
            ++b;
        if (breaks.size() >= maxStations)
            return fail(
                QStringLiteral("Bank-pair station union exceeds the vertex index capacity."));
        breaks.append(fraction);
    }
    QVector<int> parts;
    parts.reserve(breaks.size() - 1);
    qint64 stationCount = 1;
    for (int interval = 0; interval + 1 < breaks.size(); ++interval) {
        double subdivisions = 1;
        if (p.along > 0) {
            const double advance =
                (breaks[interval + 1] - breaks[interval]) * std::max(arcA.length, arcB.length);
            const double ratio = advance / p.along;
            if (!std::isfinite(ratio) || ratio > maxStations)
                return fail(
                    QStringLiteral(
                        "Bank-pair along spacing exceeds station index capacity at interval %1.")
                        .arg(interval));
            const double tolerance =
                std::min(1e-6, std::max(1e-9, 8 * std::numeric_limits<double>::epsilon() *
                                                  coordinateScale / p.along));
            const double rounded = std::round(ratio);
            subdivisions =
                std::max(1.0, std::ceil(std::abs(ratio - rounded) <= tolerance ? rounded : ratio));
        }
        if (subdivisions > maxStations - stationCount)
            return fail(
                QStringLiteral("Bank-pair station count exceeds index capacity at interval %1.")
                    .arg(interval));
        stationCount += qint64(subdivisions);
        parts.append(int(subdivisions));
    }
    error = gridDimensions(int(stationCount - 1), p.across);
    if (!error.isEmpty())
        return fail(error);

    PatchMesh pm;
    pm.tag = p.tag;
    const int ns = int(stationCount), na = p.across;
    pm.xy.reserve(ns * (na + 1));
    double previousFraction = -1;
    const auto appendStation = [&](double fraction, int station) -> QString {
        if (!(fraction > previousFraction))
            return QStringLiteral(
                       "Bank-pair normalized station %1 collapses at the requested spacing.")
                .arg(station);
        const QPointF aPoint = arcA.at(fraction), bPoint = arcB.at(fraction);
        for (int across = 0; across <= na; ++across) {
            // Keep both physical banks exact, including every original bend.
            const QPointF point = across == 0 ? aPoint
                                  : across == na
                                      ? bPoint
                                      : aPoint + (bPoint - aPoint) * (double(across) / na);
            if (!finitePt(point))
                return QStringLiteral("Bank-pair interpolation is non-finite at station %1.")
                    .arg(station);
            pm.xy.append(point);
        }
        previousFraction = fraction;
        return {};
    };
    error = appendStation(0, 0);
    if (!error.isEmpty())
        return fail(error);
    int station = 0;
    for (int interval = 0; interval < parts.size(); ++interval) {
        const double first = breaks[interval], last = breaks[interval + 1];
        for (int part = 1; part <= parts[interval]; ++part) {
            // Preserve each original union station bit-for-bit; only interior
            // interval stations are computed by interpolation.
            const double fraction = part == parts[interval]
                                        ? last
                                        : first + (last - first) * (double(part) / parts[interval]);
            error = appendStation(fraction, ++station);
            if (!error.isEmpty())
                return fail(error);
        }
    }
    const auto vertex = [na](int i, int j) { return i * (na + 1) + j; };
    pm.quads.reserve((ns - 1) * na);
    for (int i = 0; i + 1 < ns; ++i)
        for (int j = 0; j < na; ++j) {
            MeshTriangle quad;
            quad.tag = p.tag;
            quad.v0 = vertex(i, j);
            quad.v1 = ccw ? vertex(i + 1, j) : vertex(i, j + 1);
            quad.v2 = vertex(i + 1, j + 1);
            quad.v3 = ccw ? vertex(i, j + 1) : vertex(i + 1, j);
            pm.quads.append(quad);
        }
    pm.boundarySegments.reserve(2 * (ns - 1) + 2 * na);
    for (int i = 0; i + 1 < ns; ++i) {
        pm.boundarySegments.append(qMakePair(vertex(i, 0), vertex(i + 1, 0)));
        pm.boundarySegments.append(qMakePair(vertex(i, na), vertex(i + 1, na)));
    }
    for (int j = 0; j < na; ++j) {
        pm.boundarySegments.append(qMakePair(vertex(0, j), vertex(0, j + 1)));
        pm.boundarySegments.append(qMakePair(vertex(ns - 1, j), vertex(ns - 1, j + 1)));
    }
    int badVertex = -1, otherVertex = -1;
    error = validatePatchMesh(pm, nullptr, &badVertex, &otherVertex);
    if (!error.isEmpty()) {
        if (otherVertex >= 0)
            return fail(QStringLiteral("Bank-pair cells conflict near stations %1 and %2: %3")
                            .arg(badVertex / (na + 1))
                            .arg(otherVertex / (na + 1))
                            .arg(error));
        if (badVertex >= 0)
            return fail(QStringLiteral("Bank-pair cell is invalid near station %1: %2")
                            .arg(badVertex / (na + 1))
                            .arg(error));
        return fail(QStringLiteral("Bank-pair patch is invalid: %1").arg(error));
    }
    return pm;
}

} // namespace mesh
