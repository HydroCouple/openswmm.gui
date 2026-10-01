/*!
 * \file   meshgenerator.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * Mesh generator (workplans/MESH_OVERHAUL_PLAN_2026-09-29.md Stages 3–5):
 *
 *  1. size function h(x) from the caller's RefineHook (or the uniform
 *     maxArea), with quad-region spacings as overrides;
 *  2. constraint polylines (domain rings, holes, breaklines, region rings,
 *     patch boundaries) resampled at h(x); Steiner points are fixed vertices;
 *  3. quadtree cores — one background core in the caller's frame, one per
 *     quad region that carries its own alignment angle — sized by h(x),
 *     2:1 balanced, kept clear of every constraint (mesh/meshquadtree.h);
 *  4. the fringe between core fronts and constraints triangulated with the
 *     constrained-Delaunay kernel (mesh/meshcdt.h): exterior and holes
 *     removed, core interiors removed, light refinement, guarded smoothing
 *     and (quads mode) blossom pairing of fringe triangles;
 *  5. assembly: markers and tags, boundary edges, region tags by flood fill
 *     bounded by constraints, structured patches stitched by coordinate.
 */
#include "mesh/meshgenerator.h"

#include "mesh/meshcdt.h"
#include "mesh/meshcellgeom.h"
#include "mesh/meshquadcleanup.h"
#include "mesh/meshquadmatch.h"
#include "mesh/meshquadquality.h"
#include "mesh/meshquadtree.h"
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

namespace mesh {

// ── Structured patch placement ─────────────────────────────────────────────
// Patches (corridors) are topology, not hints: an overlapping, out-of-domain
// or constraint-crossing patch cannot be stitched, so it is rejected up
// front. Coordinates are taken relative to the domain so projected CRS
// offsets cannot erase small overlaps in the clipping arithmetic.
namespace {

QPainterPath patchRingPath(const QPolygonF &ring)
{
    QPainterPath path;
    path.setFillRule(Qt::WindingFill);
    if (!ring.isEmpty()) { path.addPolygon(ring); path.closeSubpath(); }
    return path;
}

double patchPathArea(const QPainterPath &path)
{
    double area = 0;
    for (const QPolygonF &polygon : path.toFillPolygons()) {
        if (polygon.size() < 3) continue;
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

QPolygonF patchRelativeRing(const QPolygonF &ring, const QPointF &origin)
{
    QPolygonF relative;
    relative.reserve(ring.size());
    for (const QPointF &point : ring) relative.append(point - origin);
    return relative;
}

double patchCross(const QPointF &a, const QPointF &b) { return a.x() * b.y() - a.y() * b.x(); }

bool patchPointOnSegment(const QPointF &point, const QPointF &a, const QPointF &b, double tolerance)
{
    const QPointF delta = b - a;
    const double length = std::hypot(delta.x(), delta.y());
    if (!(length > 0)) return std::hypot(point.x() - a.x(), point.y() - a.y()) <= tolerance;
    const double projection = QPointF::dotProduct(point - a, delta) / length;
    return projection >= -tolerance && projection <= length + tolerance
        && std::abs(patchCross(delta, point - a)) <= tolerance * length;
}

bool patchPointOnRing(const QPointF &point, const QPolygonF &ring, double tolerance)
{
    for (int i = 0; i < ring.size(); ++i)
        if (patchPointOnSegment(point, ring[i], ring[(i + 1) % ring.size()], tolerance)) return true;
    return false;
}

// Endpoints alone miss a segment traversing a concave patch or crossing from
// boundary to boundary: split at every intersection and test each interval.
bool patchSegmentEntersInterior(const QPointF &a, const QPointF &b,
                                const QPolygonF &ring, const QPainterPath &path, double tolerance)
{
    const QPointF delta = b - a;
    const double length2 = QPointF::dotProduct(delta, delta);
    if (!(length2 > 0)) return path.contains(a) && !patchPointOnRing(a, ring, tolerance);
    QVector<double> cuts{0, 1};
    for (int i = 0; i < ring.size(); ++i) {
        const QPointF c = ring[i], d = ring[(i + 1) % ring.size()];
        const QPointF edge = d - c;
        const double denominator = patchCross(delta, edge);
        if (denominator != 0) {
            const double t = patchCross(c - a, edge) / denominator;
            const double u = patchCross(c - a, delta) / denominator;
            if (t >= 0 && t <= 1 && u >= 0 && u <= 1) cuts.append(t);
        } else if (patchPointOnSegment(c, a, b, tolerance) || patchPointOnSegment(d, a, b, tolerance)) {
            cuts.append(std::clamp(QPointF::dotProduct(c - a, delta) / length2, 0.0, 1.0));
            cuts.append(std::clamp(QPointF::dotProduct(d - a, delta) / length2, 0.0, 1.0));
        }
    }
    std::sort(cuts.begin(), cuts.end());
    for (int i = 1; i < cuts.size(); ++i) {
        if (!(cuts[i] > cuts[i - 1])) continue;
        const QPointF point = a + delta * ((cuts[i] + cuts[i - 1]) / 2);
        if (path.contains(point) && !patchPointOnRing(point, ring, tolerance)) return true;
    }
    return false;
}

/*! Empty on success. \p exclusionBoundaries receives the hole rings (relative
 *  to \p origin) so the stitch check can exempt patch edges lying on them. */
QString validatePatchPlacement(const QVector<PatchMesh> &patches,
                               const QVector<QPolygonF> &domains,
                               const QVector<QPointF> &holes,
                               const QVector<ConstraintSegment> &segments,
                               const QPointF &origin,
                               QVector<QPolygonF> *exclusionBoundaries)
{
    QPainterPath domain;
    for (const QPolygonF &ring : domains) domain = domain.united(patchRingPath(patchRelativeRing(ring, origin)));
    const double span = std::max(domain.boundingRect().width(), domain.boundingRect().height());
    const double tolerance = std::max(1e-9, span * 16 * std::numeric_limits<double>::epsilon());
    // A hole seed identifies a face: with nested closed rings only the
    // innermost enclosing ring bounds it, an enclosing ring stays domain.
    QVector<QPolygonF> closedRings;
    QVector<QPainterPath> closedPaths;
    QVector<double> closedAreas;
    for (const ConstraintSegment &segment : segments) {
        if (segment.path.size() < 4
            || segment.path.first().x() != segment.path.last().x()
            || segment.path.first().y() != segment.path.last().y()) continue;
        closedRings.append(patchRelativeRing(QPolygonF(segment.path), origin));
        closedPaths.append(patchRingPath(closedRings.last()));
        closedAreas.append(patchPathArea(closedPaths.last()));
    }
    QSet<int> holeRings;
    for (const QPointF &hole : holes) {
        QVector<int> enclosing;
        int innermost = -1;
        for (int i = 0; i < closedPaths.size(); ++i) {
            if (!closedPaths[i].contains(hole - origin)) continue;
            enclosing.append(i);
            if (innermost < 0 || closedAreas[i] < closedAreas[innermost]) innermost = i;
        }
        if (innermost < 0) continue;
        const double areaTolerance = std::max(1e-24, closedAreas[innermost] * 64 * std::numeric_limits<double>::epsilon());
        for (int other : enclosing)
            if (patchPathArea(closedPaths[innermost].subtracted(closedPaths[other])) > areaTolerance)
                return QStringLiteral("MeshGenerator: an excluded-hole seed lies in intersecting constraint rings; "
                                      "use one unambiguous closed hole boundary before adding structured patches.");
        holeRings.insert(innermost);
    }
    for (int index : holeRings) {
        exclusionBoundaries->append(closedRings[index]);
        domain = domain.subtracted(closedPaths[index]);
    }
    QVector<QPainterPath> priorPaths;
    for (int i = 0; i < patches.size(); ++i) {
        const auto fail = [&](const QString &reason) {
            return QStringLiteral("MeshGenerator: structured patch %1%2 %3")
                .arg(i + 1).arg(patches[i].tag.isEmpty() ? QString()
                    : QStringLiteral(" ('%1')").arg(patches[i].tag)).arg(reason);
        };
        QString error;
        const QPolygonF ordered = orderedPatchBoundary(patches[i], &error);
        if (!error.isEmpty() || ordered.size() < 3)
            return fail(error.isEmpty() ? QStringLiteral("has no valid boundary.") : error);
        const QPolygonF ring = patchRelativeRing(ordered, origin);
        const QPainterPath path = patchRingPath(ring);
        const double areaTolerance = std::max(1e-24, patchPathArea(path) * 64 * std::numeric_limits<double>::epsilon());
        if (patchPathArea(path.subtracted(domain)) > areaTolerance)
            return fail(QStringLiteral("extends outside the meshing domain; clip or resize it before generating."));
        for (int j = 0; j < priorPaths.size(); ++j)
            if (patchPathArea(path.intersected(priorPaths[j])) > areaTolerance)
                return fail(QStringLiteral("overlaps structured patch %1; separate the corridors or define a conforming junction.").arg(j + 1));
        for (const QPointF &hole : holes)
            if (path.contains(hole - origin) && !patchPointOnRing(hole - origin, ring, tolerance))
                return fail(QStringLiteral("contains an excluded-hole seed; move the patch outside the hole."));
        for (const ConstraintSegment &segment : segments) {
            if (!polylineIntersectsRect(segment.path, ordered.boundingRect())) continue;
            for (int k = 1; k < segment.path.size(); ++k)
                if (patchSegmentEntersInterior(segment.path[k - 1] - origin, segment.path[k] - origin, ring, path, tolerance))
                    return fail(QStringLiteral("crosses or contains a required constraint; align the boundary or split the patch."));
        }
        priorPaths.append(path);
    }
    return {};
}

/*! True when the edge a–b lies entirely on one of \p outlines, checking
 *  every sub-interval so a gap between collinear segments is not bridged. */
bool patchEdgeOnOutline(const QPointF &a, const QPointF &b, const QVector<QPolygonF> &outlines, double tolerance)
{
    for (const QPolygonF &ring : outlines) {
        QVector<double> cuts{0, 1};
        const QPointF delta = b - a;
        const double length2 = QPointF::dotProduct(delta, delta);
        if (!(length2 > 0)) return false;
        for (const QPointF &point : ring)
            if (patchPointOnSegment(point, a, b, tolerance))
                cuts.append(std::clamp(QPointF::dotProduct(point - a, delta) / length2, 0.0, 1.0));
        std::sort(cuts.begin(), cuts.end());
        bool covered = true;
        for (int i = 1; i < cuts.size(); ++i) {
            if (!(cuts[i] > cuts[i - 1])) continue;
            if (!patchPointOnRing(a + delta * ((cuts[i] + cuts[i - 1]) / 2), ring, tolerance)) { covered = false; break; }
        }
        if (covered) return true;
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

void MeshGenerator::addDomain(const QPolygonF &p)                { m_domains.append(p); }
void MeshGenerator::addConstraintSegment(const ConstraintSegment &s) { m_segments.append(s); }
void MeshGenerator::addSteinerPoint(const SteinerPoint &p)       { m_steiners.append(p); }
void MeshGenerator::reserveSteinerPoints(qsizetype additional)
{
    m_steiners.reserve(m_steiners.size() + additional);
}
void MeshGenerator::addHole(const QPointF &xy)                   { m_holes.append(xy); }
void MeshGenerator::addRegion(const RegionMarker &r)             { m_regions.append(r); }
void MeshGenerator::addPatch(const PatchMesh &p)                 { m_patches.append(p); }
void MeshGenerator::addQuadRegion(const QuadRegion &r)           { m_quadRegions.append(r); }
void MeshGenerator::setOptions(const GenerationOptions &o)       { m_opts = o; }
void MeshGenerator::setRefineHook(const RefineHook &h)           { m_refineHook = h; }

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
    return {};   // core cells are never triangle pairs; nothing to protect
}

// ── Helpers ────────────────────────────────────────────────────────────────

namespace {

constexpr int    kBoundaryMarker = 1;
constexpr double kEquilateral    = 0.4330127018922193;   // √3/4
constexpr double kClearance      = 0.5;                  // core leaves: × h from any constraint
constexpr double kRefineClearance = 0.3;                 // fringe insertions: × h from any constraint
constexpr double kFringeMinAngle = 25.0;

using EdgeKey = QPair<int, int>;   // mesh::edgeKey (meshedgekey.h) builds them

/*! Exact-coordinate key (bit pattern, -0 folded). */
inline QPair<qint64, qint64> coordKey(const QPointF &p)
{
    double x = p.x(), y = p.y();
    if (x == 0.0) x = 0.0;
    if (y == 0.0) y = 0.0;
    qint64 kx, ky;
    std::memcpy(&kx, &x, 8); std::memcpy(&ky, &y, 8);
    return {kx, ky};
}

/*! Ring without its closing duplicate and without consecutive duplicates. */
QVector<QPointF> openRing(const QVector<QPointF> &ring)
{
    QVector<QPointF> out;
    out.reserve(ring.size());
    for (const QPointF &p : ring)
        if (out.isEmpty() || out.last() != p) out.append(p);
    if (out.size() > 1 && out.first() == out.last()) out.removeLast();
    return out;
}

bool pointInRing(const QVector<QPointF> &ring, const QPointF &p)
{
    bool inside = false;
    const int n = ring.size();
    for (int i = 0, j = n - 1; i < n; j = i++)
    {
        const QPointF &a = ring[i], &b = ring[j];
        if ((a.y() > p.y()) != (b.y() > p.y()))
        {
            const double xi = a.x() + (p.y() - a.y()) * (b.x() - a.x()) / (b.y() - a.y());
            if (p.x() < xi) inside = !inside;
        }
    }
    return inside;
}

/*! Uniform grid over segments for "is anything within r of p" queries. */
class SegmentGrid
{
public:
    void build(const QVector<QPair<QPointF, QPointF>> &segs, double cell)
    {
        m_segs = segs;
        m_cell = cell > 0.0 ? cell : 1.0;
        m_index.clear();
        for (int i = 0; i < m_segs.size(); ++i)
        {
            const QPointF &a = m_segs[i].first, &b = m_segs[i].second;
            const qint64 x0 = cellOf(std::min(a.x(), b.x())), x1 = cellOf(std::max(a.x(), b.x()));
            const qint64 y0 = cellOf(std::min(a.y(), b.y())), y1 = cellOf(std::max(a.y(), b.y()));
            for (qint64 y = y0; y <= y1; ++y)
                for (qint64 x = x0; x <= x1; ++x)
                    m_index[qMakePair(x, y)].append(i);
        }
    }
    /*! Squared distance to the nearest segment, or +inf when none within r. */
    [[nodiscard]] double nearest2(const QPointF &p, double r) const
    {
        double best = std::numeric_limits<double>::infinity();
        const qint64 x0 = cellOf(p.x() - r), x1 = cellOf(p.x() + r);
        const qint64 y0 = cellOf(p.y() - r), y1 = cellOf(p.y() + r);
        for (qint64 y = y0; y <= y1; ++y)
            for (qint64 x = x0; x <= x1; ++x)
            {
                const auto it = m_index.constFind(qMakePair(x, y));
                if (it == m_index.constEnd()) continue;
                for (int i : it.value())
                    best = std::min(best, pslg::distSqToSegment(p, m_segs[i].first, m_segs[i].second));
            }
        return best;
    }
private:
    [[nodiscard]] qint64 cellOf(double v) const { return qint64(std::floor(v / m_cell)); }
    QVector<QPair<QPointF, QPointF>> m_segs;
    QHash<QPair<qint64, qint64>, QVector<int>> m_index;
    double m_cell = 1.0;
};

/*! A constraint polyline after preparation. */
struct Poly
{
    QVector<QPointF> pts;     ///< Open sequence; closed rings wrap.
    int     marker = 0;
    QString tag;
    bool    closed = false;
    bool    resample = true;  ///< Patch edges keep their stationing.
    bool    isDomain = false;
    bool    isHole = false;
    QVector<int> cdtIds;      ///< Vertex ids after CDT build.
};

} // namespace

// ── Generation ─────────────────────────────────────────────────────────────

MeshResult MeshGenerator::generate() const
{
    MeshResult result;
    m_vertexTagByMarker.clear();
    m_edgeTagByMarker.clear();
    m_triangleTagByRegionId.clear();
    m_quadReports.clear();

    auto fail = [&](const QString &msg) {
        result = MeshResult();
        result.ok = false;
        result.errorMsg = msg;
        return result;
    };
    auto cancelled = [&]() { return m_refineHook.isCancelled && m_refineHook.isCancelled(); };
    auto progress = [&](qint64 n) { if (m_refineHook.onProgress) m_refineHook.onProgress(n); };

    if (m_domains.isEmpty()) return fail(QStringLiteral("MeshGenerator: domain is empty."));

    // ── Finite input check ───────────────────────────────────────────────
    {
        const auto isFinitePt = [](const QPointF &p) { return std::isfinite(p.x()) && std::isfinite(p.y()); };
        qsizetype nBad = 0;
        for (const QPolygonF &dom : m_domains) for (const QPointF &p : dom) if (!isFinitePt(p)) ++nBad;
        for (const SteinerPoint &sp : m_steiners) if (!isFinitePt(sp.xy)) ++nBad;
        for (const ConstraintSegment &cs : m_segments) for (const QPointF &p : cs.path) if (!isFinitePt(p)) ++nBad;
        for (const QPointF &h : m_holes) if (!isFinitePt(h)) ++nBad;
        for (const RegionMarker &rm : m_regions) if (!isFinitePt(rm.xy)) ++nBad;
        for (const PatchMesh &pm : m_patches) for (const QPointF &p : pm.xy) if (!isFinitePt(p)) ++nBad;
        for (const QuadRegion &qr : m_quadRegions) for (const QPointF &p : qr.ring) if (!isFinitePt(p)) ++nBad;
        if (nBad > 0)
            return fail(QStringLiteral(
                "MeshGenerator: %1 input coordinate(s) are not finite (NaN or infinite), so meshing "
                "was not attempted. The usual sources are DTM NoData or out-of-footprint samples "
                "reaching the point set, and failed coordinate reprojection.").arg(nBad));
    }

    // ── Tag tables ───────────────────────────────────────────────────────
    for (const SteinerPoint &sp : m_steiners)
        if (sp.marker != 0 && !sp.tag.isEmpty()) m_vertexTagByMarker.insert(sp.marker, sp.tag);
    for (const ConstraintSegment &cs : m_segments)
        if (cs.marker != 0 && !cs.tag.isEmpty()) m_edgeTagByMarker.insert(cs.marker, cs.tag);
    for (const RegionMarker &rm : m_regions)
        m_triangleTagByRegionId.insert(int(rm.attribute), rm.tag);

    // ── Size function ────────────────────────────────────────────────────
    const double hUniform = m_opts.maxArea > 0.0 ? std::sqrt(m_opts.maxArea / kEquilateral) : 0.0;
    const bool haveHook = bool(m_refineHook.targetAreaAt);
    if (!haveHook && hUniform <= 0.0)
        return fail(QStringLiteral("MeshGenerator: no cell size — set maxArea or install a size function."));

    QVector<QPair<QVector<QPointF>, double>> spacingOverrides;   // quad regions with a spacing
    QVector<QVector<QPointF>> regionRings(m_quadRegions.size());
    for (int i = 0; i < m_quadRegions.size(); ++i)
    {
        regionRings[i] = openRing(QVector<QPointF>(m_quadRegions[i].ring.begin(), m_quadRegions[i].ring.end()));
        if (m_quadRegions[i].spacing > 0.0 && regionRings[i].size() >= 3)
            spacingOverrides.append(qMakePair(regionRings[i], m_quadRegions[i].spacing));
    }
    const std::function<double(double, double)> hAt = [&](double x, double y) {
        double h = hUniform;
        if (haveHook)
        {
            const double a = m_refineHook.targetAreaAt(x, y);
            if (std::isfinite(a) && a > 0.0) h = std::sqrt(a / kEquilateral);
        }
        for (const auto &ov : spacingOverrides)
            if (pointInRing(ov.first, QPointF(x, y))) h = (h > 0.0) ? std::min(h, ov.second) : ov.second;
        return h;
    };

    // ── Structured patch placement ───────────────────────────────────────
    QVector<QPolygonF> patchExclusionBoundaries;   // hole rings, relative to patchOrigin
    const QPointF patchOrigin = m_domains.first().isEmpty() ? QPointF() : m_domains.first().first();
    if (!m_patches.isEmpty())
    {
        if (!(std::isfinite(m_opts.patchSnapEps) && m_opts.patchSnapEps >= 0.0))
            return fail(QStringLiteral("MeshGenerator: patch snap tolerance is invalid or too small for the coordinate span."));
        const QString err = validatePatchPlacement(m_patches, m_domains, m_holes, m_segments,
                                                   patchOrigin, &patchExclusionBoundaries);
        if (!err.isEmpty()) return fail(err);
    }

    // ── Constraint polylines ─────────────────────────────────────────────
    QVector<Poly> polys;
    QVector<QVector<QPointF>> domainRings;
    for (const QPolygonF &dom : m_domains)
    {
        Poly p;
        p.pts = openRing(QVector<QPointF>(dom.begin(), dom.end()));
        if (p.pts.size() < 3) continue;
        p.marker = kBoundaryMarker; p.closed = true; p.isDomain = true;
        domainRings.append(p.pts);
        polys.append(p);
    }
    if (domainRings.isEmpty())
        return fail(QStringLiteral("MeshGenerator: no domain ring with 3 or more vertices."));
    for (const ConstraintSegment &cs : m_segments)
    {
        Poly p;
        const bool closed = cs.path.size() >= 4 && cs.path.first() == cs.path.last();
        p.pts = closed ? openRing(cs.path) : cs.path;
        if (!closed)
        {
            QVector<QPointF> dd;
            for (const QPointF &q : p.pts) if (dd.isEmpty() || dd.last() != q) dd.append(q);
            p.pts = dd;
        }
        if (p.pts.size() < 2) continue;
        p.marker = cs.marker; p.tag = cs.tag; p.closed = closed;
        polys.append(p);
    }
    for (int i = 0; i < regionRings.size(); ++i)
    {
        if (regionRings[i].size() < 3 || m_quadRegions[i].isBackground) continue;
        Poly p;
        p.pts = regionRings[i]; p.closed = true;
        polys.append(p);
    }
    for (const PatchMesh &pm : m_patches)
        for (const auto &seg : pm.boundarySegments)
        {
            if (seg.first < 0 || seg.first >= pm.xy.size() || seg.second < 0 || seg.second >= pm.xy.size()) continue;
            Poly p;
            p.pts = {pm.xy[seg.first], pm.xy[seg.second]};
            p.resample = false;
            polys.append(p);
        }
    // Hole rings: closed non-domain polylines that contain a hole seed.
    QVector<QVector<QPointF>> holeRings;
    for (Poly &p : polys)
    {
        if (!p.closed || p.isDomain) continue;
        for (const QPointF &seed : m_holes)
            if (pointInRing(p.pts, seed)) { p.isHole = true; break; }
        if (p.isHole) holeRings.append(p.pts);
    }

    // ── Floor size ───────────────────────────────────────────────────────
    double hMin = m_opts.minCellSize > 0.0 ? m_opts.minCellSize : 0.0;
    if (hMin <= 0.0)
    {
        double hs = std::numeric_limits<double>::infinity();
        int sampled = 0;
        for (const Poly &p : polys)
            for (const QPointF &q : p.pts)
            {
                if (sampled++ > 20000) break;
                const double h = hAt(q.x(), q.y());
                if (h > 0.0) hs = std::min(hs, h);
            }
        for (const SteinerPoint &sp : m_steiners) { const double h = hAt(sp.xy.x(), sp.xy.y()); if (h > 0.0) hs = std::min(hs, h); }
        if (!std::isfinite(hs)) hs = hUniform > 0.0 ? hUniform : 1.0;
        hMin = 0.25 * hs;
    }
    const std::function<double(double, double)> hClamped = [&](double x, double y) {
        const double h = hAt(x, y);
        return h > 0.0 ? std::max(h, hMin) : h;
    };

    // ── Resample constraints at h(x) ─────────────────────────────────────
    for (Poly &p : polys)
    {
        if (!p.resample) continue;
        QVector<QPointF> path = p.pts;
        if (p.closed) path.append(path.first());
        path = pslg::resampleAtSize(path, hClamped);
        if (p.closed) path.removeLast();
        p.pts = path;
    }
    if (cancelled()) return fail(QStringLiteral("Cancelled."));

    // ── Quadtree cores ───────────────────────────────────────────────────
    QVector<QVector<QPointF>> coreConstraints;
    for (const Poly &p : polys)
    {
        if (p.isDomain || p.isHole) continue;   // domains/holes go in as rings
        QVector<QPointF> path = p.pts;
        if (p.closed) path.append(path.first());
        coreConstraints.append(path);
    }
    for (const SteinerPoint &sp : m_steiners)
        coreConstraints.append({sp.xy, sp.xy});   // a point cuts the leaf it lands in

    QVector<QPolygonF> domainPolys, holePolys;
    for (const auto &r : domainRings) domainPolys.append(QPolygonF(r));
    for (const auto &r : holeRings) holePolys.append(QPolygonF(r));

    QRectF bbox;
    for (const QPolygonF &d : domainPolys) bbox = bbox.isValid() ? bbox.united(d.boundingRect()) : d.boundingRect();

    struct CoreJob { QVector<QPolygonF> domains, holes; QuadtreeFrame frame; int regionIndex = -1; };
    QVector<CoreJob> jobs;
    {
        CoreJob bg;
        bg.domains = domainPolys;
        bg.holes = holePolys;
        bg.frame.origin = bbox.center();
        bg.frame.angleDeg = m_opts.frameAngleDeg;
        for (int i = 0; i < m_quadRegions.size(); ++i)
        {
            const QuadRegion &qr = m_quadRegions[i];
            if (!qr.hasAlignAngle || regionRings[i].size() < 3 || qr.isBackground) continue;
            bg.holes.append(QPolygonF(regionRings[i]));
            CoreJob rj;
            rj.domains = {QPolygonF(regionRings[i])};
            for (const auto &hr : holeRings)
                if (!hr.isEmpty() && pointInRing(regionRings[i], hr.first())) rj.holes.append(QPolygonF(hr));
            rj.frame.origin = QPolygonF(regionRings[i]).boundingRect().center();
            rj.frame.angleDeg = qr.alignAngleDeg;
            rj.regionIndex = i;
            jobs.append(rj);
        }
        jobs.prepend(bg);
    }
    QVector<QuadtreeMesh> cores(jobs.size());
    qint64 coreCells = 0;
    for (int j = 0; j < jobs.size(); ++j)
    {
        QuadtreeOptions qo;
        qo.hAt = hClamped;
        qo.hMin = hMin;
        qo.clearance = kClearance;
        qo.triangles = m_opts.trianglesOnly;
        qo.progress = [&](double) { progress(coreCells); return !cancelled(); };
        if (!buildQuadtreeCore(jobs[j].domains, jobs[j].holes, coreConstraints, jobs[j].frame, qo, &cores[j]))
        {
            if (cores[j].errorMsg.contains(QStringLiteral("cancelled"))) return fail(QStringLiteral("Cancelled."));
            return fail(QStringLiteral("MeshGenerator: %1").arg(cores[j].errorMsg));
        }
        coreCells += cores[j].cells.size();
    }
    if (cancelled()) return fail(QStringLiteral("Cancelled."));

    // ── Fringe: constrained Delaunay of front + constraint vertices ──────
    QVector<QPointF> cdtPoints;
    QVector<QVector<int>> coreFrontPointIds(cores.size());   // core vertex id → cdt point index (-1 = interior)
    for (int j = 0; j < cores.size(); ++j)
    {
        QVector<int> &map = coreFrontPointIds[j];
        map.fill(-1, cores[j].vertices.size());
        for (const auto &e : cores[j].frontEdges)
            for (int v : {e.first, e.second})
                if (map[v] < 0) { map[v] = cdtPoints.size(); cdtPoints.append(cores[j].vertices[v]); }
    }
    QVector<int> polyPointStart(polys.size());
    for (int i = 0; i < polys.size(); ++i)
    {
        polyPointStart[i] = cdtPoints.size();
        for (const QPointF &q : polys[i].pts) cdtPoints.append(q);
    }
    for (const SteinerPoint &sp : m_steiners) cdtPoints.append(sp.xy);
    if (cdtPoints.size() < 3) return fail(QStringLiteral("MeshGenerator: too few points to triangulate."));

    ConstrainedDelaunay cdt;
    QVector<int> pointVertex;
    if (!cdt.build(cdtPoints, &pointVertex))
        return fail(QStringLiteral("MeshGenerator: %1").arg(cdt.errorMsg()));
    if (cancelled()) return fail(QStringLiteral("Cancelled."));

    // Constraint edges: polylines then core fronts.
    QSet<EdgeKey> constraintEdges;   // in CDT vertex ids
    for (int i = 0; i < polys.size(); ++i)
    {
        Poly &p = polys[i];
        p.cdtIds.resize(p.pts.size());
        for (int k = 0; k < p.pts.size(); ++k) p.cdtIds[k] = pointVertex[polyPointStart[i] + k];
        const int n = p.pts.size();
        const int edges = p.closed ? n : n - 1;
        for (int k = 0; k < edges; ++k)
        {
            const int a = p.cdtIds[k], b = p.cdtIds[(k + 1) % n];
            if (a == b) continue;
            if (!cdt.insertConstraint(a, b))
                return fail(QStringLiteral("MeshGenerator: constraint%1 could not be recovered — %2. "
                                           "Constraints cross each other or the domain boundary.")
                                .arg(p.tag.isEmpty() ? QString() : QStringLiteral(" '%1'").arg(p.tag), cdt.errorMsg()));
            constraintEdges.insert(edgeKey(a, b));
        }
    }
    for (int j = 0; j < cores.size(); ++j)
        for (const auto &e : cores[j].frontEdges)
        {
            const int a = pointVertex[coreFrontPointIds[j][e.first]], b = pointVertex[coreFrontPointIds[j][e.second]];
            if (a == b) continue;
            if (!cdt.insertConstraint(a, b))
                return fail(QStringLiteral("MeshGenerator: core front edge could not be recovered — %1.").arg(cdt.errorMsg()));
        }
    if (cancelled()) return fail(QStringLiteral("Cancelled."));

    cdt.removeExterior();
    for (const QPointF &seed : m_holes) cdt.removeRegionAt(seed);
    for (const PatchMesh &pm : m_patches)
        if (!pm.quads.isEmpty())
        {
            const MeshTriangle &q = pm.quads.first();
            const QPointF c = 0.25 * (pm.xy[q.v0] + pm.xy[q.v1] + pm.xy[q.v2] + pm.xy[q.v3]);
            cdt.removeRegionAt(c);
        }
    // Core interiors: one seed per connected component of kept cells.
    for (const QuadtreeMesh &core : cores)
    {
        const int nc = core.cells.size();
        if (nc == 0) continue;
        QVector<int> parent(nc);
        for (int i = 0; i < nc; ++i) parent[i] = i;
        std::function<int(int)> find = [&](int x) { while (parent[x] != x) { parent[x] = parent[parent[x]]; x = parent[x]; } return x; };
        QHash<EdgeKey, int> firstCellOfEdge;
        for (int c = 0; c < nc; ++c)
        {
            const QuadtreeCell &cell = core.cells[c];
            const int v[4] = {cell.v0, cell.v1, cell.v2, cell.v3};
            const int n = cell.v3 < 0 ? 3 : 4;
            for (int e = 0; e < n; ++e)
            {
                const EdgeKey k = edgeKey(v[e], v[(e + 1) % n]);
                auto it = firstCellOfEdge.find(k);
                if (it == firstCellOfEdge.end()) firstCellOfEdge.insert(k, c);
                else parent[find(c)] = find(it.value());
            }
        }
        QSet<int> seeded;
        for (int c = 0; c < nc; ++c)
        {
            const int root = find(c);
            if (seeded.contains(root)) continue;
            seeded.insert(root);
            const QuadtreeCell &cell = core.cells[c];
            QPointF ctr = core.vertices[cell.v0] + core.vertices[cell.v1] + core.vertices[cell.v2];
            ctr = cell.v3 < 0 ? ctr / 3.0 : (ctr + core.vertices[cell.v3]) / 4.0;
            cdt.removeRegionAt(ctr);
        }
    }
    if (cancelled()) return fail(QStringLiteral("Cancelled."));

    // Light refinement, clear of constraints.
    {
        QVector<QPair<QPointF, QPointF>> segs;
        for (const Poly &p : polys)
        {
            const int n = p.pts.size();
            const int edges = p.closed ? n : n - 1;
            for (int k = 0; k < edges; ++k) segs.append(qMakePair(p.pts[k], p.pts[(k + 1) % n]));
        }
        SegmentGrid grid;
        grid.build(segs, std::max(hMin * 4.0, 1e-9));
        const int fringeVertices = cdtPoints.size();
        // Size hint per CDT vertex: the shortest front edge at a core front
        // vertex, so the fringe never runs more than 2x coarser than the
        // core cells it touches (the grading bound across the interface).
        QVector<double> hint(cdt.vertices().size(), 0.0);
        for (int j = 0; j < cores.size(); ++j)
            for (const auto &e : cores[j].frontEdges)
            {
                const QPointF d = cores[j].vertices[e.second] - cores[j].vertices[e.first];
                const double len = std::hypot(d.x(), d.y());
                for (int v : {e.first, e.second})
                {
                    const int id = pointVertex[coreFrontPointIds[j][v]];
                    hint[id] = hint[id] > 0.0 ? std::min(hint[id], len) : len;
                }
            }
        cdt.refine(hClamped, kFringeMinAngle,
                   [&](const QPointF &c) {
                       const double h = hClamped(c.x(), c.y());
                       if (!(h > 0.0)) return false;
                       const double r = kRefineClearance * h;
                       return grid.nearest2(c, r) > r * r;
                   },
                   4 * fringeVertices + 10000, &hint);
    }
    if (cancelled()) return fail(QStringLiteral("Cancelled."));

    // ── Assembly ─────────────────────────────────────────────────────────
    // Marker per exact coordinate: Steiner markers win over the boundary marker.
    QHash<QPair<qint64, qint64>, int> markerOf;
    for (const Poly &p : polys)
        if (p.isDomain) for (const QPointF &q : p.pts) markerOf.insert(coordKey(q), kBoundaryMarker);
    for (const SteinerPoint &sp : m_steiners)
        if (sp.marker != 0) markerOf.insert(coordKey(sp.xy), sp.marker);

    QVector<int> cdtToGlobal(cdt.vertices().size(), -1);
    QHash<QPair<qint64, qint64>, int> globalOfCoord;
    auto addVertex = [&](const QPointF &xy) {
        const auto key = coordKey(xy);
        const auto it = globalOfCoord.constFind(key);
        if (it != globalOfCoord.constEnd()) return it.value();
        MeshVertex v;
        v.xy = xy;
        v.marker = markerOf.value(key, 0);
        v.tag = m_vertexTagByMarker.value(v.marker);
        const int id = result.vertices.size();
        result.vertices.append(v);
        globalOfCoord.insert(key, id);
        return id;
    };
    for (int v = 0; v < cdt.vertices().size(); ++v)
        if (!cdt.isSuperVertex(v)) cdtToGlobal[v] = addVertex(cdt.vertices()[v]);

    // Core cells.
    QVector<int> coreCellStart(cores.size());
    for (int j = 0; j < cores.size(); ++j)
    {
        const QuadtreeMesh &core = cores[j];
        QVector<int> map(core.vertices.size(), -1);
        coreCellStart[j] = result.triangles.size();
        for (const QuadtreeCell &cell : core.cells)
        {
            MeshTriangle t;
            const int v[4] = {cell.v0, cell.v1, cell.v2, cell.v3};
            int g[4] = {-1, -1, -1, -1};
            for (int k = 0; k < 4; ++k)
            {
                if (v[k] < 0) continue;
                if (map[v[k]] < 0) map[v[k]] = addVertex(core.vertices[v[k]]);
                g[k] = map[v[k]];
            }
            t.v0 = g[0]; t.v1 = g[1]; t.v2 = g[2]; t.v3 = g[3];
            if (jobs[j].regionIndex >= 0) t.tag = m_quadRegions[jobs[j].regionIndex].tag;
            result.triangles.append(t);
        }
    }
    // Fringe triangles.
    const int fringeCellStart = result.triangles.size();
    for (const auto &T : cdt.triangles())
    {
        if (!T.alive) continue;
        MeshTriangle t;
        t.v0 = cdtToGlobal[T.v[0]]; t.v1 = cdtToGlobal[T.v[1]]; t.v2 = cdtToGlobal[T.v[2]];
        if (t.v0 < 0 || t.v1 < 0 || t.v2 < 0) continue;
        result.triangles.append(t);
    }
    // Boundary edges (every constraint sub-edge, with its marker and tag).
    QSet<EdgeKey> lockedGlobal;
    for (const Poly &p : polys)
    {
        const int n = p.pts.size();
        const int edges = p.closed ? n : n - 1;
        for (int k = 0; k < edges; ++k)
        {
            const int ca = p.cdtIds[k], cb = p.cdtIds[(k + 1) % n];
            if (ca == cb) continue;
            // A vertex lying on the segment (a constraint endpoint on the
            // boundary, say) split it: list every piece as a mesh edge.
            const QVector<int> chain = cdt.constrainedChain(ca, cb);
            for (int c = 0; c + 1 < chain.size(); ++c)
            {
                const int a = cdtToGlobal[chain[c]], b = cdtToGlobal[chain[c + 1]];
                if (a < 0 || b < 0 || a == b) continue;
                MeshEdge e;
                e.v0 = a; e.v1 = b; e.marker = p.marker; e.tag = p.tag.isEmpty() ? m_edgeTagByMarker.value(p.marker) : p.tag;
                result.boundaryEdges.append(e);
                lockedGlobal.insert(edgeKey(a, b));
            }
        }
    }

    if (result.triangles.isEmpty())
        return fail(QStringLiteral("MeshGenerator: no cells were produced — the domain may be smaller than the cell size."));

    // ── Region tags: flood fill bounded by constraint edges ──────────────
    // Angle-bearing regions flood too: their own core cells carry the tag
    // already (assembly tags them per CoreJob), but the CDT fringe between
    // that core and the region ring does not — the flood fills exactly that
    // remainder (it only writes empty tags) and the ring's constraint edges
    // bound it the same as for a frameless region.
    if (!m_regions.isEmpty() || std::any_of(m_quadRegions.begin(), m_quadRegions.end(), [](const QuadRegion &q) { return !q.tag.isEmpty(); }))
    {
        QHash<EdgeKey, QVector<int>> cellsOfEdge;
        cellsOfEdge.reserve(result.triangles.size() * 2);
        for (int c = 0; c < result.triangles.size(); ++c)
        {
            const MeshTriangle &t = result.triangles[c];
            const int n = t.vertexCount();
            for (int e = 0; e < n; ++e) cellsOfEdge[edgeKey(t.vertex(e), t.vertex((e + 1) % n))].append(c);
        }
        auto cellContaining = [&](const QPointF &p) {
            for (int j = 0; j < cores.size(); ++j)
            {
                const int c = cores[j].cellAt(p);
                if (c >= 0) return coreCellStart[j] + c;
            }
            const int t = cdt.locate(p);
            if (t < 0) return -1;
            // Map the CDT triangle to its result cell by vertex triple.
            const auto &T = cdt.triangles()[t];
            const int a = cdtToGlobal[T.v[0]], b = cdtToGlobal[T.v[1]];
            for (int c : cellsOfEdge.value(edgeKey(a, b)))
                if (c >= fringeCellStart) { const MeshTriangle &mt = result.triangles[c];
                    const int cc = cdtToGlobal[T.v[2]];
                    if ((mt.v0 == cc || mt.v1 == cc || mt.v2 == cc)) return c; }
            return -1;
        };
        // One visited set across ALL floods. The locked edges are fixed, so a
        // flood fills its whole constraint-bounded component; a later flood
        // starting in an already-visited component could only write tags into
        // cells the earlier flood already tagged, i.e. nothing. Skipping it is
        // exact, and keeps many seeds in one component (713 unbounded
        // subcatchment seeds on Bellinge) at O(cells) instead of O(seeds·cells).
        QVector<bool> seen(result.triangles.size(), false);
        auto flood = [&](int start, const QString &tag) {
            if (start < 0 || tag.isEmpty() || seen[start]) return;
            QVector<int> stack{start};
            seen[start] = true;
            while (!stack.isEmpty())
            {
                const int c = stack.takeLast();
                if (result.triangles[c].tag.isEmpty()) result.triangles[c].tag = tag;
                const MeshTriangle &t = result.triangles[c];
                const int n = t.vertexCount();
                for (int e = 0; e < n; ++e)
                {
                    const EdgeKey k = edgeKey(t.vertex(e), t.vertex((e + 1) % n));
                    if (lockedGlobal.contains(k)) continue;
                    for (int o : cellsOfEdge.value(k))
                        if (!seen[o]) { seen[o] = true; stack.append(o); }
                }
            }
        };
        for (const RegionMarker &rm : m_regions)
            flood(cellContaining(rm.xy), rm.tag.isEmpty() ? m_triangleTagByRegionId.value(int(rm.attribute)) : rm.tag);
        for (int i = 0; i < m_quadRegions.size(); ++i)
        {
            const QuadRegion &qr = m_quadRegions[i];
            if (qr.tag.isEmpty() || regionRings[i].size() < 3) continue;
            const QPointF seed = EditGeometry::interiorPoint(regionRings[i]);
            flood(cellContaining(seed), qr.tag);
        }
    }

    // ── Fringe pairing and smoothing ─────────────────────────────────────
    {
        QVector<int> fringeCells;
        for (int c = fringeCellStart; c < result.triangles.size(); ++c) fringeCells.append(c);
        QSet<int> movable;
        for (int v = 0; v < cdt.vertices().size(); ++v)
            if (v >= cdtPoints.size() && !cdt.isSuperVertex(v) && cdtToGlobal[v] >= 0) movable.insert(cdtToGlobal[v]);
        if (!fringeCells.isEmpty())
        {
            // Core front edges are locked too: a pair may never straddle the core.
            QSet<EdgeKey> locked = lockedGlobal;
            for (int j = 0; j < cores.size(); ++j)
                for (const auto &e : cores[j].frontEdges)
                    locked.insert(edgeKey(cdtToGlobal[pointVertex[coreFrontPointIds[j][e.first]]],
                                          cdtToGlobal[pointVertex[coreFrontPointIds[j][e.second]]]));
            QuadQualityBounds bounds;
            bounds.minAngleDeg = 45.0; bounds.maxAngleDeg = 135.0; bounds.minScaledJacobian = 0.5; bounds.maxAspect = 3.0;
            if (!m_opts.trianglesOnly)
            {
                QuadPairingOptions po;
                po.bounds = bounds;
                QVector<int> oldToNew;
                pairTrianglesIntoQuads(result, fringeCells, {}, locked, po, &oldToNew);
            }
            if (!movable.isEmpty())
            {
                QuadCleanupOptions co;
                co.bounds = bounds;
                co.removeDoublets = false;
                co.diagonalSwaps = false;
                co.smoothingIterations = 5;
                QVector<int> vOldToNew;
                cleanupAndSmoothQuads(result, movable, co, &vOldToNew);
            }
        }
    }
    if (cancelled()) return fail(QStringLiteral("Cancelled."));

    // ── Structured patches, stitched by coordinate ───────────────────────
    // Patch boundary vertices entered the CDT unresampled, so they come
    // back with their exact coordinates; interior patch vertices are new.
    if (!m_patches.isEmpty())
    {
        const double eps = m_opts.patchSnapEps > 0.0 ? m_opts.patchSnapEps : 1e-7;
        const QPointF origin = bbox.center();
        auto keyOf = [&](const QPointF &p) {
            return QPair<qint64, qint64>(qRound64((p.x() - origin.x()) / eps), qRound64((p.y() - origin.y()) / eps));
        };
        // A tiny snap tolerance must never push qRound64 out of range.
        const auto validSnapKey = [&](const QPointF &p) {
            const double x = (p.x() - origin.x()) / eps, y = (p.y() - origin.y()) / eps;
            const double limit = std::ldexp(1.0, 63);
            return std::isfinite(x) && std::isfinite(y) && std::abs(x) < limit && std::abs(y) < limit;
        };
        for (const MeshVertex &v : std::as_const(result.vertices))
            if (!validSnapKey(v.xy))
                return fail(QStringLiteral("MeshGenerator: patch snap tolerance is invalid or too small for the coordinate span."));
        for (const PatchMesh &pm : m_patches)
            for (const QPointF &p : pm.xy)
                if (!validSnapKey(p))
                    return fail(QStringLiteral("MeshGenerator: patch snap tolerance is invalid or too small for the coordinate span."));

        QHash<QPair<qint64, qint64>, int> outIndex;
        outIndex.reserve(result.vertices.size());
        for (int i = 0; i < result.vertices.size(); ++i) outIndex.insert(keyOf(result.vertices[i].xy), i);
        QHash<EdgeKey, int> patchEdgeIncidence;
        for (const PatchMesh &pm : m_patches)
        {
            if (pm.quads.isEmpty()) continue;
            QVector<int> localToGlobal(pm.xy.size(), -1);
            for (int k = 0; k < pm.xy.size(); ++k)
            {
                const auto it = outIndex.constFind(keyOf(pm.xy[k]));
                if (it != outIndex.constEnd()) { localToGlobal[k] = it.value(); continue; }
                MeshVertex v;
                v.xy = pm.xy[k];
                localToGlobal[k] = result.vertices.size();
                result.vertices.append(v);
                outIndex.insert(keyOf(pm.xy[k]), localToGlobal[k]);
            }
            // Welding changes coordinates and can collapse vertices when the
            // snap tolerance is too coarse: check the welded geometry first.
            PatchMesh welded = pm;
            for (int k = 0; k < welded.xy.size(); ++k) welded.xy[k] = result.vertices[localToGlobal[k]].xy;
            for (const auto &edge : pm.boundarySegments)
                for (int k : {edge.first, edge.second})
                    if (welded.xy[k] != pm.xy[k])
                        return fail(QStringLiteral("MeshGenerator: patch snap tolerance moves a structured patch boundary away "
                                                   "from its carved cavity. Reduce the patch snap tolerance."));
            const QString error = validate(welded);
            if (!error.isEmpty())
                return fail(QStringLiteral("MeshGenerator: structured patch '%1' is invalid after vertex welding: %2 "
                                           "Reduce the patch snap tolerance or revise its spacing.").arg(pm.tag, error));
            for (const auto &edge : pm.boundarySegments)
                patchEdgeIncidence.insert(edgeKey(localToGlobal[edge.first], localToGlobal[edge.second]), 0);
            for (const MeshTriangle &q : pm.quads)
            {
                MeshTriangle c = q;
                c.v0 = localToGlobal[q.v0]; c.v1 = localToGlobal[q.v1];
                c.v2 = localToGlobal[q.v2]; c.v3 = localToGlobal[q.v3];
                if (c.tag.isEmpty()) c.tag = pm.tag;
                result.triangles.append(c);
            }
        }
        // Every patch boundary edge needs two incident cells (one when it
        // lies on the domain or a hole outline): exact edge identity rejects
        // unmatched subdivisions and T-junctions.
        for (const MeshTriangle &cell : std::as_const(result.triangles))
        {
            const int n = cell.isQuad() ? 4 : 3;
            for (int k = 0; k < n; ++k)
            {
                auto found = patchEdgeIncidence.find(edgeKey(cell.vertex(k), cell.vertex((k + 1) % n)));
                if (found != patchEdgeIncidence.end()) ++found.value();
            }
        }
        QVector<QPolygonF> outlines = patchExclusionBoundaries;
        for (const QPolygonF &domain : m_domains) outlines.append(patchRelativeRing(domain, patchOrigin));
        const double outlineTol = std::max(1e-9, std::max(bbox.width(), bbox.height()) * 16 * std::numeric_limits<double>::epsilon());
        for (auto it = patchEdgeIncidence.cbegin(); it != patchEdgeIncidence.cend(); ++it)
        {
            if (it.value() == 2) continue;
            const QPointF a = result.vertices[it.key().first].xy - patchOrigin;
            const QPointF b = result.vertices[it.key().second].xy - patchOrigin;
            if (it.value() == 1 && patchEdgeOnOutline(a, b, outlines, outlineTol)) continue;
            return fail(QStringLiteral("MeshGenerator: structured patch boundary has %1 incident cells instead of a conforming "
                                       "interface. Match subdivisions on touching patches and domain boundaries, or separate "
                                       "the patches.").arg(it.value()));
        }
    }

    // Triangles first, quads after (engine order).
    reorderTrianglesFirst(result);

    // ── Reports ──────────────────────────────────────────────────────────
    m_quadReports.resize(m_quadRegions.size());
    for (int i = 0; i < m_quadRegions.size(); ++i)
    {
        QuadRegionReport &r = m_quadReports[i];
        r.index = i;
        r.requested = m_quadRegions[i].mode;
        r.resolved = m_opts.trianglesOnly ? QuadRegionMode::TrianglesOnly : QuadRegionMode::Free;
        r.accepted = regionRings[i].size() >= 3;
        r.spacing = m_quadRegions[i].spacing;
        if (!r.accepted) r.message = QStringLiteral("region ring has fewer than 3 vertices");
    }

    result.ok = !result.triangles.isEmpty();
    return result;
}

} // namespace mesh
