/*!
 * \file   meshgenerator.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * Slice AU — Triangle wrapper. Builds the input \c triangulateio,
 * runs Shewchuk's `triangulate()`, and re-packs the output into
 * \ref mesh::MeshResult. Tag round-trip is via Triangle's marker
 * (point/segment) and region-attribute (triangle) channels.
 *
 * Quad regions (workplans/QUAD_MESHING_REDESIGN_PLAN_2026-09-06.md §3.3, §4)
 * ride on the same single CDT: Mapped / Submapped rings become internal
 * patches (hole + stitched quads, exactly like addPatch), Free /
 * TrianglesOnly rings become a locked constraint loop with a RegionMarker
 * whose attribute is -(regionIndex+1); Free regions additionally receive a
 * cross-field aligned lattice of Steiner points, no Triangle area refinement
 * inside (the -u hook returns "unconstrained" there), and after Triangle
 * their triangles are paired into quads (template lookup + blossom), cleaned
 * up and smoothed. Without quad regions the pipeline is unchanged.
 */
#include "mesh/meshgenerator.h"

#include "mesh/meshcellgeom.h"
#include "mesh/meshcrossfield.h"
#include "mesh/meshquadpoints.h"
#include "mesh/meshquadquality.h"
#include "mesh/meshsubmap.h"

#include <QDebug>
#include <QHash>
#include <QRectF>
#include <QSet>
#include <QStringList>
#include <QtMath>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <functional>

extern "C" {
#define TRILIBRARY   // needed to expose triangulate_safe() in triangle.h
#include "triangle.h"
#undef TRILIBRARY
}

namespace mesh {

// ---------------------------------------------------------------------------
// Setters — trivial passthrough for build clarity
// ---------------------------------------------------------------------------

void MeshGenerator::setDomain(const QPolygonF &p)
{
    m_domains.clear();
    if (!p.isEmpty()) m_domains.append(p);
}

void MeshGenerator::setDomains(const QVector<QPolygonF> &polys)
{
    m_domains.clear();
    for (const auto &p : polys)
        if (!p.isEmpty()) m_domains.append(p);
}

void MeshGenerator::addDomain(const QPolygonF &p)
{
    if (!p.isEmpty()) m_domains.append(p);
}
void MeshGenerator::addConstraintSegment(const ConstraintSegment &s) { m_segments.append(s); }
void MeshGenerator::addSteinerPoint(const SteinerPoint &p)    { m_steiners.append(p); }
void MeshGenerator::reserveSteinerPoints(qsizetype additional)
{
    m_steiners.reserve(m_steiners.size() + additional);
}
void MeshGenerator::addHole(const QPointF &xy)                { m_holes.append(xy); }
void MeshGenerator::addRegion(const RegionMarker &r)          { m_regions.append(r); }
void MeshGenerator::addPatch(const PatchMesh &p)              { m_patches.append(p); }
void MeshGenerator::addQuadRegion(const QuadRegion &r)        { m_quadRegions.append(r); }
void MeshGenerator::setOptions(const GenerationOptions &o)    { m_opts = o; }
void MeshGenerator::setRefineHook(const RefineHook &h)        { m_refineHook = h; }

QString MeshGenerator::tagForVertexMarker(int marker) const
{
    return m_vertexTagByMarker.value(marker);
}
QString MeshGenerator::tagForEdgeMarker(int marker) const
{
    return m_edgeTagByMarker.value(marker);
}

// ---------------------------------------------------------------------------
// Helpers — triangulateio struct lifecycle
// ---------------------------------------------------------------------------

namespace {

void zeroIO(triangulateio &t)
{
    std::memset(&t, 0, sizeof(t));
}

// Triangle allocates output arrays with malloc(); free with free()/trifree().
// We call trifree() on every output pointer we asked Triangle to populate.
void freeOutput(triangulateio &t)
{
    if (t.pointlist)             trifree(t.pointlist);
    if (t.pointattributelist)    trifree(t.pointattributelist);
    if (t.pointmarkerlist)       trifree(t.pointmarkerlist);
    if (t.trianglelist)          trifree(t.trianglelist);
    if (t.triangleattributelist) trifree(t.triangleattributelist);
    if (t.trianglearealist)      trifree(t.trianglearealist);
    if (t.neighborlist)          trifree(t.neighborlist);
    if (t.segmentlist)           trifree(t.segmentlist);
    if (t.segmentmarkerlist)     trifree(t.segmentmarkerlist);
    if (t.edgelist)              trifree(t.edgelist);
    if (t.edgemarkerlist)        trifree(t.edgemarkerlist);
    // Inputs we hand-allocated with malloc are freed by us — Triangle does
    // NOT free its inputs.
}

// Quantise points so duplicates resolve. Triangle is robust but the input
// PSLG must not contain coincident points (different markers) or zero-length
// segments. We snap to the 7th decimal — sub-mm in metric CRSes.
struct PointHasher
{
    int operator()(const QPointF &p) const noexcept
    {
        const qint64 ix = static_cast<qint64>(qRound64(p.x() * 1e7));
        const qint64 iy = static_cast<qint64>(qRound64(p.y() * 1e7));
        return qHash(ix) ^ (qHash(iy) << 1);
    }
    bool equals(const QPointF &a, const QPointF &b) const noexcept
    {
        return qRound64(a.x() * 1e7) == qRound64(b.x() * 1e7)
            && qRound64(a.y() * 1e7) == qRound64(b.y() * 1e7);
    }
};

// ── Quad regions (QUAD_MESHING_REDESIGN_PLAN §3.3) ───────────────────────────

/*! Segment marker of a Free / TrianglesOnly region ring. Reserved: far above
 *  any tag id the dialog hands out; mapped back to marker 0 / empty tag on
 *  readback so ring edges behave exactly like patch boundaries (locked,
 *  untagged). */
constexpr int kQuadRingMarker = 0x7FFF0001;

/*! A quad region that passed validation and enters the PSLG. */
struct PreparedQuadRegion
{
    int            index = -1;                   ///< Position in m_quadRegions / m_quadReports.
    QuadRegionMode mode  = QuadRegionMode::Free; ///< Resolved mode.
    double         h     = 0.0;
    QPolygonF      ring;                         ///< normalizeRingCCW(r.ring).
    QPolygonF      ringR;                        ///< resampleRing(ring, h) — Free / TrianglesOnly.
    QVector<QPolygonF> holesR;                   ///< normalizeRingCCW of every r.holes ring.
    /*! Ring is a domain outline already present in the PSLG: emit no ring
     *  segments for it (QUAD_EVERYWHERE_PLAN_2026-09-07.md §3.1). */
    bool           isBackground = false;
    /*! No explicit QuadRegion::spacing was given, so \ref h came from the size
     *  function and the lattice should follow it point by point rather than
     *  hold the single centroid sample. */
    bool           gradeFromField = false;
    QRectF         bbox;
    QString        tag;                          ///< r.tag or inherited from a dropped RegionMarker.
    // Free only
    QVector<int>          ringInputIdx;          ///< pushPoint index of every ringR vertex.
    QVector<QPointF>      seedXY;                ///< Fixed interior seeds (junction Steiners, segment vertices).
    QVector<int>          seedInputIdx;
    QSet<int>             seedSet;
    QVector<QuadTemplate> templates;             ///< Input (== Triangle output) vertex ids.
    QSet<int>             movable;               ///< Generated lattice vertices (new pushPoint indices).
    int                   droppedSteiners = 0;
};

bool inQuadRing(const PreparedQuadRegion &q, const QPointF &p)
{
    return q.bbox.contains(p) && pointInRing(q.ring, p);
}

/*! Inside the region's meshable area: within the ring and outside every hole.
 *  Distinct from inQuadRing(), which is what decides whether a fixed vertex
 *  SEEDS the lattice — a hole-ring vertex lies on the hole boundary and must
 *  still seed, so seeding keeps using the ring-only test. */
bool inQuadRegion(const PreparedQuadRegion &q, const QPointF &p)
{
    return q.bbox.contains(p) && pointInRegion(q.ring, q.holesR, p);
}

/*! Strictly inside one of the region's holes (an area the region does not mesh). */
bool pointInRegionHole(const PreparedQuadRegion &q, const QPointF &p)
{
    for (const QPolygonF &h : q.holesR)
        if (pointInRing(h, p)) return true;
    return false;
}

/*! A point strictly inside \p ring: the vertex mean when that is inside,
 *  else the first ear centroid that is. */
QPointF ringInteriorPoint(const QPolygonF &ring)
{
    const int n = ring.size();
    QPointF c;
    for (const QPointF &p : ring) c += p;
    c /= double(std::max(1, n));
    if (pointInRing(ring, c)) return c;
    for (int i = 0; i < n; ++i)
    {
        const QPointF e = (ring[(i + n - 1) % n] + ring[i] + ring[(i + 1) % n]) / 3.0;
        if (pointInRing(ring, e)) return e;
    }
    return c;
}

/*! Region-attribute seed for a background region: a point inside the ring and
 *  outside every hole. Triangle flood-fills the attribute from here, bounded by
 *  the PSLG segments, so it must not land in a hole (the fill would be discarded
 *  with the hole) nor inside a nested region (whose own seed owns that area).
 *  Falls back to the ring interior point when the scan finds nothing. */
QPointF backgroundSeedPoint(const PreparedQuadRegion &q)
{
    const QPointF c = ringInteriorPoint(q.ring);
    if (q.holesR.isEmpty() || pointInRegion(q.ring, q.holesR, c)) return c;
    const QRectF b = q.bbox;
    constexpr int kN = 64;
    double bestD = -1.0;
    QPointF best = c;
    for (int iy = 1; iy < kN; ++iy)
        for (int ix = 1; ix < kN; ++ix)
        {
            const QPointF p(b.left() + b.width() * double(ix) / kN,
                            b.top()  + b.height() * double(iy) / kN);
            if (!pointInRegion(q.ring, q.holesR, p)) continue;
            // Prefer the most interior candidate so the seed is robust.
            const double d = distanceToRings(q.ring, q.holesR, p);
            if (d > bestD) { bestD = d; best = p; }
        }
    return best;
}

int orientSign(const QPointF &a, const QPointF &b, const QPointF &c) noexcept
{
    const double v = (b.x() - a.x()) * (c.y() - a.y()) - (b.y() - a.y()) * (c.x() - a.x());
    return v > 0.0 ? 1 : (v < 0.0 ? -1 : 0);
}

/*! True when a constraint path has a vertex strictly inside the ring or an
 *  edge properly crossing a ring edge (plan §3.2: forces Mapped/Submapped
 *  to Free — a segment cannot run through a hole). */
bool segmentsCrossRing(const QVector<ConstraintSegment> &segs, const PreparedQuadRegion &q)
{
    const int n = q.ring.size();
    for (const ConstraintSegment &cs : segs)
    {
        if (cs.path.size() < 2) continue;
        if (!QPolygonF(cs.path).boundingRect().intersects(q.bbox)) continue;
        for (const QPointF &p : cs.path)
            if (inQuadRing(q, p)) return true;
        for (int i = 0; i + 1 < cs.path.size(); ++i)
        {
            const QPointF &p1 = cs.path[i], &p2 = cs.path[i + 1];
            for (int j = 0; j < n; ++j)
            {
                const QPointF &q1 = q.ring[j], &q2 = q.ring[(j + 1) % n];
                if (orientSign(p1, p2, q1) * orientSign(p1, p2, q2) < 0
                    && orientSign(q1, q2, p1) * orientSign(q1, q2, p2) < 0)
                    return true;
            }
        }
    }
    return false;
}

/*! Angle (degrees) of the longest ring edge — constant-field fallback. */
double longestEdgeAngleDeg(const QPolygonF &ring)
{
    const int n = ring.size();
    double best = -1.0, ang = 0.0;
    for (int i = 0; i < n; ++i)
    {
        const QPointF d = ring[(i + 1) % n] - ring[i];
        const double len = std::hypot(d.x(), d.y());
        if (len > best) { best = len; ang = std::atan2(d.y(), d.x()) * 180.0 / M_PI; }
    }
    return ang;
}

} // namespace

// ---------------------------------------------------------------------------
// generate
// ---------------------------------------------------------------------------

MeshResult MeshGenerator::generate() const
{
    MeshResult result;
    m_vertexTagByMarker.clear();
    m_edgeTagByMarker.clear();
    m_triangleTagByRegionId.clear();
    m_quadReports.clear();

    if (m_domains.isEmpty())
    {
        result.errorMsg = QStringLiteral("MeshGenerator: domain is empty.");
        return result;
    }

    // ── Reject non-finite input coordinates ───────────────────────────────
    // A NaN coordinate is invisible to duplicate/degeneracy screening: NaN
    // compares false against everything, so it is neither equal to nor
    // orderable against any other vertex, and qRound64(NaN * 1e7) is
    // undefined. It therefore reaches Triangle intact, and a vertex with BOTH
    // coordinates NaN kills the INITIAL DELAUNAY pass — before any segment or
    // hole processing. Measured on a plain point set with no PSLG at all
    // (switches "zQ"):
    //
    //   triangulate -> delaunay -> divconqdelaunay -> divconqrecurse
    //     -> mergehulls -> counterclockwise -> SIGSEGV
    //
    // Every orientation test against NaN returns false, so the hull-merge
    // walk never finds its stopping edge, runs off the end of the
    // triangulation and dereferences garbage. That is a hardware fault, so
    // triangulate_safe()'s setjmp cannot catch it and the process dies. A
    // single NaN coordinate is milder but still silently wrong: the vertex is
    // dropped from the output (measured: 6955 triangles where a clean run
    // gives 6972), which is arguably worse because nothing reports it.
    //
    // NaN reaches us legitimately — dtmthinner.cpp yields NaN for NoData and
    // for grid points outside the DEM footprint — and infinities arrive from
    // failed reprojections. Screen every input before Triangle sees any of it.
    {
        const auto isFinitePt = [](const QPointF &p) {
            return std::isfinite(p.x()) && std::isfinite(p.y());
        };
        qsizetype nBad = 0;
        for (const QPolygonF &dom : m_domains)
            for (const QPointF &p : dom)      if (!isFinitePt(p))     ++nBad;
        for (const SteinerPoint &sp : m_steiners)
            if (!isFinitePt(sp.xy)) ++nBad;
        for (const ConstraintSegment &cs : m_segments)
            for (const QPointF &p : cs.path)  if (!isFinitePt(p))     ++nBad;
        for (const QPointF &h : m_holes)      if (!isFinitePt(h))     ++nBad;
        for (const RegionMarker &rm : m_regions)
            if (!isFinitePt(rm.xy)) ++nBad;
        for (const PatchMesh &pm : m_patches)
            for (const QPointF &p : pm.xy)    if (!isFinitePt(p))     ++nBad;
        for (const QuadRegion &qr : m_quadRegions)
        {
            for (const QPointF &p : qr.ring)       if (!isFinitePt(p)) ++nBad;
            for (const QPointF &p : qr.alignGuide) if (!isFinitePt(p)) ++nBad;
        }

        if (nBad > 0)
        {
            result.errorMsg = QStringLiteral(
                "MeshGenerator: %1 input coordinate(s) are not finite (NaN or "
                "infinite), so meshing was not attempted. A non-finite vertex "
                "crashes Triangle's Delaunay pass outright. The usual sources "
                "are DTM NoData or out-of-footprint samples reaching the point "
                "set, and failed coordinate reprojection.").arg(nBad);
            return result;
        }
    }

    // ── Collect unique input points ───────────────────────────────────────
    // Order: domain vertices first (markers reserved for boundary tagging),
    // then Steiner points, then constraint-segment interior points. We
    // snap-and-dedupe so a Steiner that coincides with a domain vertex maps
    // to the same input index — Triangle would reject duplicates otherwise.
    // Key by quantised (qint64,qint64) instead of QPointF so we don't need
    // a qHash<QPointF> overload (Qt provides none — fuzzy equality).
    using PointKey = QPair<qint64, qint64>;
    QHash<PointKey, int /*input index*/> pointIndex;
    QVector<QPointF> points;
    QVector<int>     pointMarkers;
    // Boundary markers are 1 (assigned when we push domain vertices below).
    // Any later push with a different non-zero marker wins — that's a
    // tagged Steiner / segment endpoint coinciding with a corner, and its
    // SWMM-side tag (junction id, conduit id) is more specific than the
    // generic "boundary" label.
    constexpr int kBoundaryMarker = 1;
    {
        // Upper bound on unique input points: every domain vertex, Steiner
        // point, and constraint-path vertex (dedupe only shrinks it).
        qsizetype estPts = m_steiners.size();
        for (const QPolygonF &dom : m_domains) estPts += dom.size();
        for (const ConstraintSegment &cs : m_segments) estPts += cs.path.size();
        points.reserve(estPts);
        pointMarkers.reserve(estPts);
        pointIndex.reserve(estPts);
    }
    // Quantise the OFFSET from a reference vertex, not the absolute coordinate.
    // The key is built by scaling by 1e7, so the product has to stay inside the
    // range a double represents as an exact integer (2^53). Absolute projected
    // coordinates blow that budget: at |x| = 1e9 the key gains a step of 2, so
    // distinct points start sharing one; past |x| ~ 9.2e11 the qint64 conversion
    // overflows outright. Measured: two points 1e-7 apart at x = 1e12 produce an
    // IDENTICAL key and are silently merged, which then drops the segments
    // between them as zero-length. Quantising (xy - quantOrigin) bounds the
    // product by the domain SPAN instead, so the key stays exact for any CRS
    // (1e-7 resolution holds out to a 9e8-unit span). The reference is the first
    // domain vertex: O(1), and every input point lies within one span of it.
    const QPointF quantOrigin = m_domains.constFirst().constFirst();
    auto pushPoint = [&](const QPointF &xy, int marker) {
        const qint64 qx = qRound64((xy.x() - quantOrigin.x()) * 1e7);
        const qint64 qy = qRound64((xy.y() - quantOrigin.y()) * 1e7);
        const PointKey key(qx, qy);
        auto it = pointIndex.find(key);
        if (it != pointIndex.end())
        {
            int &existing = pointMarkers[it.value()];
            if (marker != 0 && (existing == 0 || existing == kBoundaryMarker))
                existing = marker;
            return it.value();
        }
        const int idx = points.size();
        points.append(QPointF(quantOrigin.x() + qx / 1e7,
                              quantOrigin.y() + qy / 1e7));
        pointMarkers.append(marker);
        pointIndex.insert(key, idx);
        return idx;
    };

    // 1) Outer boundary — push every domain polygon as its own closed
    //    ring of segments, all sharing the kBoundaryMarker. Multiple
    //    disjoint polygons are supported (e.g. boundary feature layer
    //    with several non-overlapping polygons, or subcatchment-union
    //    yielding a MultiPolygon). Triangle treats each ring as an
    //    independent boundary; the unmeshed exterior between rings is
    //    automatically excluded by the PSLG topology.
    QVector<QPair<int, int>> domSegments;  // (v0, v1) pairs to add to segmentlist
    {
        qsizetype estDomSegs = 0;
        for (const QPolygonF &dom : m_domains) estDomSegs += dom.size();
        domSegments.reserve(estDomSegs);
    }
    for (const QPolygonF &dom : m_domains)
    {
        const int domN = dom.size();
        if (domN < 3) continue;
        const int ringSegStart = domSegments.size();
        int firstIdx = -1, prevIdx = -1;
        for (int i = 0; i < domN; ++i)
        {
            const QPointF &p = dom[i];
            if (i == domN - 1 && i > 0
                && qFuzzyCompare(p.x() + 1, dom[0].x() + 1)
                && qFuzzyCompare(p.y() + 1, dom[0].y() + 1))
                break;  // closed polygon: skip the dup-of-first vertex.
            const int idx = pushPoint(p, kBoundaryMarker);
            if (firstIdx < 0) { firstIdx = idx; prevIdx = idx; continue; }
            // Skip zero-length segments: after quantisation two consecutive
            // vertices may map to the same index.  OGR UnaryUnion (dissolve)
            // can produce such duplicates at polygon-join points.
            if (idx == prevIdx) continue;
            domSegments.append(qMakePair(prevIdx, idx));
            prevIdx = idx;
        }
        // Ring closing segment — only when the ring already contributed ≥ 2
        // open segments (a closed ring needs ≥ 3 total). Gating on a vertex
        // count over-counted revisited vertices, letting a polygon that
        // quantised down to 2 distinct vertices emit the degenerate pair
        // (a,b),(b,a) as a "closed ring".
        if (domSegments.size() - ringSegStart >= 2
            && prevIdx >= 0 && firstIdx >= 0 && prevIdx != firstIdx)
            domSegments.append(qMakePair(prevIdx, firstIdx));
    }
    if (domSegments.isEmpty())
    {
        result.errorMsg = QStringLiteral(
            "MeshGenerator: no usable boundary polygons "
            "(every supplied polygon had < 3 vertices after vertex deduplication).");
        return result;
    }

    // ── Quad regions — PREPARE (QUAD_MESHING_REDESIGN_PLAN §3.3, §4) ─────
    // Resolve every region before any Steiner / segment point is pushed, so
    // the Steiner and segment loops below can (a) drop terrain points inside
    // Free rings and (b) record the fixed seeds that pin a Free lattice.
    // Mapped / Submapped regions become entries of the LOCAL patch list;
    // user RegionMarkers seeded inside a quad ring are removed from the LOCAL
    // region list (their tag is inherited when the region's own is empty).
    // Both local lists replace m_patches / m_regions for the rest of generate().
    QVector<PatchMesh>          patches = m_patches;
    QVector<RegionMarker>       regions = m_regions;
    QVector<PreparedQuadRegion> qregs;
    if (!m_quadRegions.isEmpty())
    {
        QHash<int, QString> userTagById;   // what the packing loop will build for m_regions
        for (const RegionMarker &rm : m_regions)
            if (!rm.tag.isEmpty()) userTagById.insert(static_cast<int>(rm.attribute), rm.tag);

        m_quadReports.resize(m_quadRegions.size());
        QVector<QuadRegion> acceptedSoFar;     // pairwise overlap check
        for (int i = 0; i < m_quadRegions.size(); ++i)
        {
            const QuadRegion &r = m_quadRegions[i];
            QuadRegionReport &rep = m_quadReports[i];
            rep.index = i;
            rep.requested = r.mode;
            rep.resolved  = r.mode;
            QStringList notes;

            PreparedQuadRegion q;
            q.index        = i;
            q.ring         = normalizeRingCCW(r.ring);
            q.bbox         = q.ring.boundingRect();
            q.isBackground = r.isBackground;
            for (const QPolygonF &hr : r.holes)
            {
                const QPolygonF n = normalizeRingCCW(hr);
                if (n.size() >= 3) q.holesR.append(n);
            }

            // Spacing h: explicit, else the size function at the centroid
            // (sqrt(2·area) matches the neighbouring triangle edge length),
            // else the option default.
            double h = r.spacing;
            if (!(h > 0.0) && m_refineHook.targetAreaAt && !q.ring.isEmpty())
            {
                const QPointF c = q.holesR.isEmpty() ? ringInteriorPoint(q.ring)
                                                     : backgroundSeedPoint(q);
                const double a = m_refineHook.targetAreaAt(c.x(), c.y());
                if (a > 0.0) h = std::sqrt(2.0 * a);
                q.gradeFromField = true;
            }
            if (!(h > 0.0)) h = m_opts.quadRegionDefaultSpacing;
            if (!(h > 0.0) || !std::isfinite(h))
            {
                rep.message = QStringLiteral("skipped: no spacing (set QuadRegion::spacing, "
                                             "a size function, or quadRegionDefaultSpacing)");
                continue;
            }
            q.h = h;
            rep.spacing = h;

            // m_holes are seed POINTS, not rings, so no hole-ring test is
            // possible here; a hole seed inside a quad ring is the caller's error.
            QuadRegion rv = r;
            rv.spacing = h;
            const QString bad = validateQuadRegion(rv, m_domains, QVector<QPolygonF>());
            if (!bad.isEmpty())
            {
                rep.message = QStringLiteral("skipped: %1").arg(bad);
                continue;
            }
            acceptedSoFar.append(rv);
            if (!validateQuadRegionsDisjoint(acceptedSoFar).isEmpty())
            {
                acceptedSoFar.removeLast();
                rep.message = QStringLiteral("skipped: overlaps an earlier quad region");
                continue;
            }

            // Tag, and user RegionMarkers seeded inside the ring.
            q.tag = r.tag;
            for (int k = regions.size() - 1; k >= 0; --k)
            {
                if (!inQuadRing(q, regions[k].xy)) continue;
                if (q.tag.isEmpty())
                {
                    q.tag = userTagById.value(static_cast<int>(regions[k].attribute));
                    if (!q.tag.isEmpty())
                        notes << QStringLiteral("inherited tag '%1' from a region marker inside the ring").arg(q.tag);
                }
                regions.removeAt(k);
            }

            // Mode. Explicit Mapped corners index the CALLER's ring; relocate
            // them on the normalised ring by coordinate.
            QuadRegionMode mode = r.mode;
            // A background region spans the whole domain: it carries holes and
            // every conduit/breakline crosses it, so the structured modes never
            // apply (Auto on a rectangular domain would otherwise pick Mapped
            // and mesh straight over the holes).
            if (q.isBackground && mode != QuadRegionMode::TrianglesOnly)
                mode = QuadRegionMode::Free;
            QVector<int> corners;
            if (mode == QuadRegionMode::Mapped && r.corners.size() == 4)
            {
                for (int c : r.corners)
                {
                    if (c < 0 || c >= r.ring.size()) break;
                    const int at = q.ring.indexOf(r.ring[c]);
                    if (at < 0) break;
                    corners.append(at);
                }
                std::sort(corners.begin(), corners.end());
                if (corners.size() != 4 || std::adjacent_find(corners.begin(), corners.end()) != corners.end())
                    corners.clear();
            }
            if (mode == QuadRegionMode::Auto)
                mode = classifyQuadRegion(q.ring, &corners);
            else if (mode == QuadRegionMode::Mapped && corners.size() != 4)
            {
                classifyQuadRegion(q.ring, &corners);
                if (corners.size() != 4)
                {
                    mode = QuadRegionMode::Free;
                    notes << QStringLiteral("Mapped needs 4 corners: fell back to Free");
                }
            }
            if ((mode == QuadRegionMode::Mapped || mode == QuadRegionMode::Submapped)
                && segmentsCrossRing(m_segments, q))
            {
                mode = QuadRegionMode::Free;
                notes << QStringLiteral("a constraint segment crosses the region: fell back to Free");
            }
            if (mode == QuadRegionMode::Submapped)
            {
                QString e;
                const PatchMesh pm = makeSubmappedPatch(q.ring, h, q.tag, &e);
                if (pm.quads.isEmpty())
                {
                    mode = QuadRegionMode::Free;
                    notes << QStringLiteral("submapping failed (%1): fell back to Free").arg(e);
                }
                else patches.append(pm);
            }
            if (mode == QuadRegionMode::Mapped)
            {
                QString e;
                const PatchMesh pm = makeMappedPatch(q.ring, corners, h, q.tag, &e);
                if (pm.quads.isEmpty())
                {
                    mode = QuadRegionMode::Free;
                    notes << QStringLiteral("mapped patch failed (%1): fell back to Free").arg(e);
                }
                else patches.append(pm);
            }
            if (mode == QuadRegionMode::Free || mode == QuadRegionMode::TrianglesOnly)
            {
                if (!q.isBackground)
                    q.ringR = resampleRing(q.ring, h);
                else
                {
                    // A background ring IS the domain outline, whose segments the
                    // PSLG has already emitted — so its vertices are never
                    // replaced, only added to. Densify each edge to the local
                    // lattice spacing (a raw 4-corner domain would otherwise give
                    // the boundary layer 4 seeds and leave the perimeter
                    // triangulated). The new vertices lie ON those segments, so
                    // Triangle subdivides them; no segment is emitted here.
                    const auto areaAt = m_refineHook.targetAreaAt;
                    auto hOn = [&](const QPointF &a, const QPointF &b) {
                        if (!q.gradeFromField || !areaAt) return h;
                        const QPointF m = (a + b) / 2.0;
                        const double ar = areaAt(m.x(), m.y());
                        return (ar > 0.0 && std::isfinite(ar)) ? std::sqrt(2.0 * ar) : h;
                    };
                    const int n = q.ring.size();
                    q.ringR.clear();
                    q.ringR.reserve(n * 2);
                    for (int i = 0; i < n; ++i)
                    {
                        const QPointF &a = q.ring[i], &b = q.ring[(i + 1) % n];
                        q.ringR.append(a);
                        const double len = std::hypot(b.x() - a.x(), b.y() - a.y());
                        const double he  = hOn(a, b);
                        const int    k   = (he > 0.0) ? int(std::floor(len / he)) : 0;
                        for (int s = 1; s <= k; ++s)
                        {
                            const double t = double(s) / double(k + 1);
                            q.ringR.append(a + (b - a) * t);
                        }
                    }
                }
            }

            q.mode = mode;
            rep.resolved = mode;
            rep.message  = notes.join(QStringLiteral("; "));
            qregs.append(q);
        }
    }
    bool anyFree = false;
    for (const PreparedQuadRegion &q : qregs)
        if (q.mode == QuadRegionMode::Free) anyFree = true;
    // A pushed point inside a Free ring that must stay fixed (junction
    // Steiner, constraint-segment vertex) seeds that region's lattice.
    auto noteFixedSeed = [&](int idx) {
        if (!anyFree) return;
        const QPointF &p = points[idx];
        for (PreparedQuadRegion &q : qregs)
        {
            if (q.mode != QuadRegionMode::Free || !inQuadRing(q, p)) continue;
            if (q.seedSet.contains(idx)) return;
            q.seedSet.insert(idx);
            q.seedXY.append(p);
            q.seedInputIdx.append(idx);
            return;
        }
    };

    // 2) Steiner points — exact-coord vertices that must appear in the mesh.
    //    Marker-0 points (terrain / aux) inside a Free quad ring are dropped
    //    (plan D5: the lattice replaces them; z is re-sampled downstream).
    //    So are marker-0 points OUTSIDE the ring but closer than h to it: a
    //    vertex inside a ring segment's diametral lens makes Triangle split
    //    that segment (its -u hook suppresses area refinement only), which
    //    dissolves every template touching the ring (measured: a 4 m terrain
    //    cloud against an h = 5 ring dropped the region from 100 % to 55 %
    //    quads). The size field grades the outside triangles to h anyway.
    for (const SteinerPoint &sp : m_steiners)
    {
        if (anyFree && sp.marker == 0)
        {
            bool drop = false;
            for (PreparedQuadRegion &q : qregs)
            {
                if (q.mode != QuadRegionMode::Free) continue;
                // Inside the lattice area, or close enough outside it to split a
                // ring segment. A point inside a HOLE is left alone: no lattice
                // is generated there, so nothing replaces it.
                if (inQuadRegion(q, sp.xy)
                    || (q.bbox.adjusted(-q.h, -q.h, q.h, q.h).contains(sp.xy)
                        && !pointInRegionHole(q, sp.xy)
                        && distanceToRings(q.ringR, q.holesR, sp.xy) < q.h))
                { ++q.droppedSteiners; drop = true; break; }
            }
            if (drop) continue;
        }
        const int idx = pushPoint(sp.xy, sp.marker);
        if (sp.marker != 0 && !sp.tag.isEmpty())
            m_vertexTagByMarker.insert(sp.marker, sp.tag);
        if (sp.marker != 0) noteFixedSeed(idx);
    }

    // 3) Constraint segments — push every polyline vertex; record segments.
    QVector<QPair<int, int>> userSegments;
    QVector<int>             userSegmentMarkers;
    {
        qsizetype estUserSegs = 0;
        for (const ConstraintSegment &cs : m_segments) estUserSegs += cs.path.size();
        userSegments.reserve(estUserSegs);
        userSegmentMarkers.reserve(estUserSegs);
    }
    for (const ConstraintSegment &cs : m_segments)
    {
        if (cs.path.size() < 2) continue;
        if (cs.marker != 0 && !cs.tag.isEmpty())
            m_edgeTagByMarker.insert(cs.marker, cs.tag);
        int prev = pushPoint(cs.path.first(), cs.marker);
        noteFixedSeed(prev);
        for (int i = 1; i < cs.path.size(); ++i)
        {
            const int curr = pushPoint(cs.path[i], cs.marker);
            noteFixedSeed(curr);
            if (curr != prev)
            {
                userSegments.append(qMakePair(prev, curr));
                userSegmentMarkers.append(cs.marker);
            }
            prev = curr;
        }
    }

    // 4) Structured patches (G3) — every boundary segment of a patch is a
    //    PSLG constraint (its vertices become Triangle input points, kept
    //    unsplit by the 'Y' switch below), and the patch interior is carved out as a hole
    //    seeded at the first quad's centroid (inside, since quads are
    //    convex). The quads themselves are stitched in after Triangle runs.
    //    `patches` = m_patches + the Mapped / Submapped quad regions.
    QVector<QPointF> holes = m_holes;
    for (const PatchMesh &pm : patches)
    {
        if (pm.quads.isEmpty()) continue;
        const QString bad = validate(pm);
        if (!bad.isEmpty())
        {
            result.errorMsg = QStringLiteral("MeshGenerator: %1").arg(bad);
            return result;
        }
        for (const QPair<int, int> &seg : pm.boundarySegments)
        {
            const int a = pushPoint(pm.xy[seg.first], 0);
            const int b = pushPoint(pm.xy[seg.second], 0);
            if (a != b)
            {
                userSegments.append(qMakePair(a, b));
                userSegmentMarkers.append(0);
            }
        }
        const MeshTriangle &q0 = pm.quads.first();
        holes.append((pm.xy[q0.v0] + pm.xy[q0.v1] + pm.xy[q0.v2] + pm.xy[q0.v3]) / 4.0);
    }

    // 5) Free / TrianglesOnly quad regions (plan §3.3, §4.4). The resampled
    //    ring is a locked constraint loop (marker kQuadRingMarker, points
    //    pushed with marker 0 so a coincident junction keeps its own marker;
    //    Triangle stamps the segment marker on the remaining ring vertices and
    //    readback maps it to 0). One RegionMarker seeded inside carries the
    //    attribute -(index+1) that identifies the region's triangles on
    //    readback. Free regions then get their cross field and the frontal
    //    lattice; the generated points are plain marker-0 Steiner points.
    //
    //    Template quads are NOT emitted as constraint segments (plan §4.4f
    //    describes that variant): pairTrianglesIntoQuads looks the two
    //    triangles of a template up by vertex triple in the Delaunay output,
    //    which is diagonal-agnostic, so the extra ~2 segments per lattice
    //    point buy nothing here.
    for (PreparedQuadRegion &q : qregs)
    {
        if (q.mode != QuadRegionMode::Free && q.mode != QuadRegionMode::TrianglesOnly) continue;
        QuadRegionReport &rep = m_quadReports[q.index];
        const QuadRegion &r = m_quadRegions[q.index];
        rep.droppedSteiners = q.droppedSteiners;

        const int nr = q.ringR.size();
        q.ringInputIdx.resize(nr);
        // marker 0 never overwrites an existing one, so a background ring's
        // vertices keep the kBoundaryMarker the domain pass gave them and we
        // simply recover their indices.
        for (int i = 0; i < nr; ++i) q.ringInputIdx[i] = pushPoint(q.ringR[i], 0);
        if (!q.isBackground)
            for (int i = 0; i < nr; ++i)
            {
                const int a = q.ringInputIdx[i], b = q.ringInputIdx[(i + 1) % nr];
                if (a == b) continue;
                userSegments.append(qMakePair(a, b));
                userSegmentMarkers.append(kQuadRingMarker);
            }
        RegionMarker rm;
        rm.xy        = q.isBackground ? backgroundSeedPoint(q) : ringInteriorPoint(q.ring);
        rm.attribute = -double(q.index + 1);
        rm.maxArea   = -1.0;
        rm.tag       = q.tag;
        regions.append(rm);
        if (q.mode != QuadRegionMode::Free) continue;

        // Cross field: constant when an alignment angle is given, otherwise
        // harmonic from the ring, every constraint path near the ring and the
        // optional guide polyline.
        CrossField field;
        if (r.hasAlignAngle)
            field.setConstant(r.alignAngleDeg);
        else
        {
            QVector<QVector<QPointF>> aligned;
            QVector<QPointF> closed = q.ringR;
            closed.append(q.ringR.first());
            aligned.append(closed);
            for (const ConstraintSegment &cs : m_segments)
                if (cs.path.size() >= 2 && QPolygonF(cs.path).boundingRect().intersects(q.bbox))
                    aligned.append(cs.path);
            if (r.alignGuide.size() >= 2) aligned.append(r.alignGuide);
            CrossField::Options fo;
            fo.pitch = q.h;
            if (!field.build(q.bbox, aligned, fo))
            {
                field.setConstant(longestEdgeAngleDeg(q.ringR));
                rep.message += (rep.message.isEmpty() ? QString() : QStringLiteral("; "))
                             + QStringLiteral("cross field solve failed: constant field along the longest ring edge");
            }
        }

        // Seeds = ring vertices (quantised, in ring order) then the fixed
        // interior points; template indices are combined indices into
        // [seeds..., generated...] and are mapped to pushPoint indices, which
        // equal Triangle's output ids for input points.
        QVector<QPointF> seeds;
        seeds.reserve(nr + q.seedXY.size());
        for (int idx : q.ringInputIdx) seeds.append(points[idx]);
        seeds += q.seedXY;
        QuadPointOptions po;
        po.h = q.h;
        // Graded lattice (QUAD_EVERYWHERE_PLAN_2026-09-07.md §3.2): follow the
        // size function wherever the caller did NOT pin a spacing. An explicit
        // QuadRegion::spacing means "this size everywhere in this region", so it
        // stays uniform; q.h remains the fallback for a bad sample.
        if (q.gradeFromField && m_refineHook.targetAreaAt)
        {
            const auto areaAt = m_refineHook.targetAreaAt;
            po.hAt = [areaAt](double x, double y) {
                const double a = areaAt(x, y);
                return a > 0.0 && std::isfinite(a) ? std::sqrt(2.0 * a) : 0.0;
            };
        }
        const QuadPointSet ps = placeQuadPoints(q.ringR, q.holesR, seeds, nr, field, po);

        QVector<int> combined = q.ringInputIdx + q.seedInputIdx;
        combined.reserve(combined.size() + ps.generated.size());
        for (const QPointF &p : ps.generated)
        {
            const int before = points.size();
            const int idx = pushPoint(p, 0);
            if (points.size() > before) q.movable.insert(idx);
            combined.append(idx);
        }
        rep.generatedPoints = ps.generated.size();
        q.templates.reserve(ps.templates.size());
        for (const QuadTemplate &t : ps.templates)
        {
            QuadTemplate o;
            for (int k = 0; k < 4; ++k)
                o.v[k] = (t.v[k] >= 0 && t.v[k] < combined.size()) ? combined[t.v[k]] : -1;
            q.templates.append(o);
        }
    }

    // ── Final PSLG validation ─────────────────────────────────────────────
    // Strip any zero-length segments (v0 == v1) that may have survived from
    // user constraint segments or from the domain boundary on degenerate input
    // (e.g., OGR UnaryUnion duplicate vertices at polygon-join points).
    // Triangle aborts with a fatal error on zero-length segments.
    {
        auto stripZeroLen = [](QVector<QPair<int,int>> &segs,
                               QVector<int>             &markers) {
            // Single-pass compaction; segs and markers stay in lockstep.
            int w = 0;
            for (int i = 0; i < segs.size(); ++i)
            {
                if (segs[i].first == segs[i].second) continue;
                segs[w]    = segs[i];
                markers[w] = markers[i];
                ++w;
            }
            segs.resize(w);
            markers.resize(w);
        };
        QVector<int> domMarkers(domSegments.size(), kBoundaryMarker);
        stripZeroLen(domSegments, domMarkers);
        stripZeroLen(userSegments, userSegmentMarkers);
    }
    if (domSegments.isEmpty())
    {
        result.errorMsg = QStringLiteral(
            "MeshGenerator: all domain boundary segments were degenerate "
            "(zero-length after vertex deduplication).");
        return result;
    }

    // ── Bound the point count against Triangle's first-block pool sizing ──
    // poolinit() sizes a pool's first block as
    //   trimalloc(itemsfirstblock * itembytes + sizeof(void*) + alignbytes)
    // The binding pool is the TRIANGLE pool, not the vertex pool: while
    // initializevertexpool() passes itemsfirstblock = invertices with
    // itembytes 32, initializetrisubpools() passes 2*invertices - 2 with
    // itembytes 72 for our switch string (2D, no point attributes, -A region
    // attributes, -p/-q always set). That is 144 bytes per input point —
    // 4.5x the vertex pool, so bounding on 32 was far too permissive and left
    // a live window between the two limits.
    //
    // The arithmetic itself is now size_t in the vendored triangle.c (both
    // operands were int and wrapped silently), so overflowing this no longer
    // corrupts the heap. The bound is kept as a fail-fast: past it the
    // triangle pool alone wants > 2 GB in ONE contiguous block, which is a
    // request worth refusing with an actionable message rather than letting
    // it become a bad_alloc — or, on Windows, a commit-limit kill.
    //
    // NOTE: derived by reading triangle.c (poolinit / initializetrisubpools),
    // NOT reproduced — provoking it needs > 2 GB of pool.
    constexpr qsizetype kMaxTrianglePoints = (2147483647 - 16) / 144;  // 14913080
    if (points.size() > kMaxTrianglePoints)
    {
        result.errorMsg = QStringLiteral(
            "MeshGenerator: %1 mesh points exceeds the %2 this triangulator "
            "can size its element pool for (it allocates ~144 bytes per input "
            "point in a single contiguous block). Reduce the terrain point "
            "density, enable thinning, or mesh a smaller extent.")
            .arg(points.size()).arg(kMaxTrianglePoints);
        return result;
    }

    // ── Pack input triangulateio ──────────────────────────────────────────
    triangulateio in{};   zeroIO(in);
    triangulateio out{};  zeroIO(out);

    // std::malloc returns NULL on failure — it does NOT throw — so writing
    // through an unchecked pointer here is a raw SIGSEGV that the pipeline's
    // bad_alloc guard cannot intercept. At the kMaxTrianglePoints bound the
    // pointlist alone is ~1 GB contiguous, which a large DEM can push past
    // the commit limit. Check every packing allocation and fail gracefully.
    auto packOom = [&]() {
        std::free(in.pointlist);      std::free(in.pointmarkerlist);
        std::free(in.segmentlist);    std::free(in.segmentmarkerlist);
        std::free(in.holelist);       std::free(in.regionlist);
        result.errorMsg = QStringLiteral(
            "MeshGenerator: out of memory while packing %1 mesh points for "
            "Triangle. Reduce the terrain point density, enable thinning, or "
            "mesh a smaller extent.").arg(points.size());
        return result;
    };

    // Points
    in.numberofpoints = points.size();
    in.pointlist      = static_cast<REAL *>(std::malloc(sizeof(REAL) * 2 * points.size()));
    in.pointmarkerlist = static_cast<int *>(std::malloc(sizeof(int) * points.size()));
    if (!in.pointlist || !in.pointmarkerlist) return packOom();
    for (int i = 0; i < points.size(); ++i)
    {
        in.pointlist[2 * i + 0] = points[i].x();
        in.pointlist[2 * i + 1] = points[i].y();
        in.pointmarkerlist[i]   = pointMarkers[i];
    }

    // Segments — boundary + user
    const int totalSeg = domSegments.size() + userSegments.size();
    in.numberofsegments = totalSeg;
    if (totalSeg > 0)
    {
        in.segmentlist       = static_cast<int *>(std::malloc(sizeof(int) * 2 * totalSeg));
        in.segmentmarkerlist = static_cast<int *>(std::malloc(sizeof(int) * totalSeg));
        if (!in.segmentlist || !in.segmentmarkerlist) return packOom();
        int s = 0;
        for (const auto &seg : domSegments)
        {
            in.segmentlist[2 * s + 0] = seg.first;
            in.segmentlist[2 * s + 1] = seg.second;
            in.segmentmarkerlist[s]   = kBoundaryMarker;
            ++s;
        }
        for (int u = 0; u < userSegments.size(); ++u)
        {
            in.segmentlist[2 * s + 0] = userSegments[u].first;
            in.segmentlist[2 * s + 1] = userSegments[u].second;
            in.segmentmarkerlist[s]   = userSegmentMarkers[u];
            ++s;
        }
    }

    // Holes (user holes + patch interiors)
    in.numberofholes = holes.size();
    if (!holes.isEmpty())
    {
        in.holelist = static_cast<REAL *>(std::malloc(sizeof(REAL) * 2 * holes.size()));
        if (!in.holelist) return packOom();
        for (int i = 0; i < holes.size(); ++i)
        {
            in.holelist[2 * i + 0] = holes[i].x();
            in.holelist[2 * i + 1] = holes[i].y();
        }
    }

    // Regions — Triangle's regionlist is an array of 4-doubles per region:
    // (x, y, attribute, max_area). Attribute is propagated to
    // triangleattributelist for each output triangle.
    in.numberofregions = regions.size();
    if (!regions.isEmpty())
    {
        in.regionlist = static_cast<REAL *>(std::malloc(sizeof(REAL) * 4 * regions.size()));
        if (!in.regionlist) return packOom();
        for (int i = 0; i < regions.size(); ++i)
        {
            in.regionlist[4 * i + 0] = regions[i].xy.x();
            in.regionlist[4 * i + 1] = regions[i].xy.y();
            in.regionlist[4 * i + 2] = regions[i].attribute;
            in.regionlist[4 * i + 3] = regions[i].maxArea > 0
                                           ? regions[i].maxArea
                                           : -1.0;
            if (!regions[i].tag.isEmpty())
                m_triangleTagByRegionId.insert(
                    static_cast<int>(regions[i].attribute), regions[i].tag);
        }
    }

    // ── Refinement hook ───────────────────────────────────────────────────
    // With Free quad regions the hook is wrapped so that targetAreaAt returns
    // "unconstrained" (0) inside every Free ring — Triangle must not insert
    // its own Steiner points into the lattice (plan §3.3) — and outside it
    // defers to the user's size function, else to the uniform maxArea (the
    // numeric 'a' switch is omitted once a size function exists, so the cap
    // has to come through the hook). -q angle refinement is unaffected: a
    // right-isosceles lattice has 45° corners, above the default minAngle.
    // Without Free regions `hook` is a plain copy of m_refineHook.
    RefineHook hook = m_refineHook;
    if (anyFree)
    {
        QVector<QPolygonF> rings;
        QVector<QRectF>    boxes;
        for (const PreparedQuadRegion &q : qregs)
            if (q.mode == QuadRegionMode::Free) { rings.append(q.ring); boxes.append(q.bbox); }
        const std::function<double(double, double)> userArea = m_refineHook.targetAreaAt;
        const double uniformArea = m_opts.maxArea;
        hook.targetAreaAt = [rings, boxes, userArea, uniformArea](double x, double y) {
            const QPointF p(x, y);
            for (int i = 0; i < rings.size(); ++i)
                if (boxes[i].contains(p) && pointInRing(rings[i], p)) return 0.0;
            if (userArea) return userArea(x, y);
            return uniformArea > 0.0 ? uniformArea : 0.0;
        };
    }

    // ── Switch string ─────────────────────────────────────────────────────
    QString sw;
    if (!m_opts.customSwitchString.isEmpty())
    {
        sw = m_opts.customSwitchString;
    }
    else
    {
        // p = read PSLG; z = zero-based; A = regional attributes per triangle.
        //
        // 'e' (output edge list) was here on the assumption that it produced
        // boundaryEdges. It does not — boundaryEdges is built from
        // out.segmentlist below, which comes from 'p'. Requesting 'e' made
        // Triangle allocate m.edges*2 ints (~3 edges per vertex) and traverse
        // the whole mesh in writeedges(), at the exact moment its memory pools
        // are still live, for an array nothing ever read.
        sw = QStringLiteral("pzA");
        if (m_opts.minAngle > 0.0)
            sw += QStringLiteral("q%1").arg(m_opts.minAngle, 0, 'f', 2);

        // A size function supersedes the uniform cap: 'u' routes every
        // refinement decision through the hook, so emitting 'a<area>' as well
        // would apply both constraints and defeat the grading.
        const bool useSizeFn = static_cast<bool>(hook.targetAreaAt);
        if (m_opts.maxArea > 0.0 && !useSizeFn)
            sw += QStringLiteral("a%1").arg(m_opts.maxArea, 0, 'f', 4);
        else if (!regions.isEmpty())
            sw += QStringLiteral("a");  // per-region area only.

        // 'u' enables the triunsuitable() hook (cancellation, progress, graded
        // sizing). It also sets Triangle's `quality` flag, so the refinement
        // pass — and therefore the hook — runs even when minAngle and maxArea
        // are both zero. That is what makes cancellation available at all.
        if (useSizeFn || hook.isCancelled || hook.onProgress)
            sw += QStringLiteral("u");
        // A structured patch's boundary is a mesh boundary (its interior is
        // a hole), and a Steiner point inserted on it would leave a hanging
        // node against the patch quads. 'Y' forbids splitting segments that
        // have a triangle on one side only — exactly the patch boundaries
        // (and the domain outline); interior breaklines may still split.
        // A Free quad ring has triangles on both sides, so 'Y' does not
        // protect it and no 'YY' is added for it (plan §4.4f).
        if (!m_opts.allowSteiner)         sw += QStringLiteral("YY");
        else if (!patches.isEmpty())      sw += QStringLiteral("Y");
        if (m_opts.conformingDelaunay)    sw += QStringLiteral("D");
        if (m_opts.maxSteinerPoints > 0)
            sw += QStringLiteral("S%1").arg(m_opts.maxSteinerPoints);
        if (m_opts.quiet)                 sw += QStringLiteral("Q");
    }
    QByteArray swBa = sw.toLatin1();

    // ── Run Triangle ──────────────────────────────────────────────────────
    // triangulate_safe() wraps triangulate() with setjmp so that any fatal
    // error inside Triangle (degenerate PSLG, out-of-memory, intersecting
    // segments) is caught via longjmp rather than calling exit(), which
    // would kill the entire process from the worker thread.
    int triErr = 0;
    bool cancelled = false;
    {
        // Scoped so the hook is uninstalled before we touch the results, and so
        // a nested/concurrent generate() on another thread is unaffected.
        const RefineHookGuard hookGuard(&hook);
        triErr    = triangulate_safe(swBa.data(), &in, &out, nullptr);
        cancelled = refineHookWasCancelled();
    }

    if (triErr != 0)
    {
        freeOutput(out);
        // Free the inputs we malloc'd above (Triangle never frees its inputs).
        std::free(in.pointlist);      std::free(in.pointmarkerlist);
        std::free(in.segmentlist);    std::free(in.segmentmarkerlist);
        std::free(in.holelist);       std::free(in.regionlist);
        result.errorMsg = QStringLiteral(
            "Triangle fatal error — check PSLG for degenerate geometry "
            "(duplicate/coincident vertices, crossing or zero-length "
            "constraint segments, boundary not forming a closed ring).");
        return result;
    }

    if (cancelled)
    {
        // Refinement was abandoned partway, so the mesh satisfies neither the
        // angle nor the area constraints. Triangle still returned normally (the
        // hook drains rather than aborts), so its pools are already freed — we
        // just discard the output instead of handing back a half-refined mesh.
        freeOutput(out);
        std::free(in.pointlist);      std::free(in.pointmarkerlist);
        std::free(in.segmentlist);    std::free(in.segmentmarkerlist);
        std::free(in.holelist);       std::free(in.regionlist);
        result.errorMsg = QStringLiteral("Cancelled during Triangle refinement.");
        return result;
    }

    // ── Copy out → MeshResult ─────────────────────────────────────────────
    result.vertices.reserve(out.numberofpoints);
    for (int i = 0; i < out.numberofpoints; ++i)
    {
        MeshVertex v;
        v.xy.setX(out.pointlist[2 * i + 0]);
        v.xy.setY(out.pointlist[2 * i + 1]);
        v.marker = out.pointmarkerlist ? out.pointmarkerlist[i] : 0;
        if (v.marker == kQuadRingMarker) v.marker = 0;   // quad ring vertex: untagged
        v.tag    = m_vertexTagByMarker.value(v.marker);
        result.vertices.append(v);
    }

    // Validate every vertex index Triangle hands back ONCE, here at the
    // source. Downstream consumers (reorderMeshHilbert, the DEM-coverage CSR
    // fill) index vertex arrays with these values without further checks; on
    // a degenerate PSLG a corrupt index would turn into an out-of-bounds
    // write there, not a clean failure.
    const int nOutPts = out.numberofpoints;
    auto badOutput = [&]() {
        result.vertices.clear();
        result.triangles.clear();
        result.boundaryEdges.clear();
        result.errorMsg = QStringLiteral(
            "MeshGenerator: Triangle returned a vertex index outside its own "
            "point list — output is corrupt (degenerate PSLG?).");
        return false;
    };
    auto validIdx = [nOutPts](int v) { return v >= 0 && v < nOutPts; };

    bool outputOk = true;
    QVector<int> regionIdOfTriangle;   // Free quad regions: attribute per output triangle
    if (anyFree) regionIdOfTriangle.reserve(out.numberoftriangles);
    result.triangles.reserve(out.numberoftriangles);
    for (int i = 0; i < out.numberoftriangles; ++i)
    {
        MeshTriangle t;
        t.v0 = out.trianglelist[3 * i + 0];
        t.v1 = out.trianglelist[3 * i + 1];
        t.v2 = out.trianglelist[3 * i + 2];
        if (!validIdx(t.v0) || !validIdx(t.v1) || !validIdx(t.v2))
        {
            outputOk = badOutput();
            break;
        }
        if (out.triangleattributelist && out.numberoftriangleattributes > 0)
        {
            const int regionId = static_cast<int>(out.triangleattributelist[i]);
            t.tag = m_triangleTagByRegionId.value(regionId);
            if (anyFree) regionIdOfTriangle.append(regionId);
        }
        result.triangles.append(t);
    }

    if (outputOk && out.segmentlist && out.numberofsegments > 0)
    {
        result.boundaryEdges.reserve(out.numberofsegments);
        for (int i = 0; i < out.numberofsegments; ++i)
        {
            MeshEdge e;
            e.v0     = out.segmentlist[2 * i + 0];
            e.v1     = out.segmentlist[2 * i + 1];
            if (!validIdx(e.v0) || !validIdx(e.v1))
            {
                outputOk = badOutput();
                break;
            }
            e.marker = out.segmentmarkerlist ? out.segmentmarkerlist[i] : 0;
            if (e.marker == kQuadRingMarker) e.marker = 0;   // quad ring edge: locked, untagged
            e.tag    = m_edgeTagByMarker.value(e.marker);
            result.boundaryEdges.append(e);
        }
    }

    // ── Free quad regions: pair, clean up, smooth (plan §4.4f–h) ──────────
    // Runs on the bare Triangle output, before patches are stitched and
    // before any vertex- or cell-indexed side table exists, so the cell
    // reorder of pairing and the vertex compaction of cleanup have nothing
    // to invalidate except the per-region id lists kept here. Pairing runs
    // for every region first (each call reorders cells; the remaining
    // regions' cell ids follow oldToNew), then cleanup + smoothing per
    // region (each call may compact vertices; the remaining regions' movable
    // sets follow vertexOldToNew).
    if (outputOk && anyFree && regionIdOfTriangle.size() == result.triangles.size())
    {
        QHash<int, int> slotOfIndex;   // m_quadRegions index → qregs slot
        for (int s = 0; s < qregs.size(); ++s) slotOfIndex.insert(qregs[s].index, s);
        QVector<QVector<int>> cellIds(qregs.size());
        for (int t = 0; t < regionIdOfTriangle.size(); ++t)
        {
            const int id = regionIdOfTriangle[t];
            if (id >= 0) continue;
            const int slot = slotOfIndex.value(-id - 1, -1);
            if (slot >= 0 && qregs[slot].mode == QuadRegionMode::Free) cellIds[slot].append(t);
        }

        QSet<QPair<int, int>> locked;
        locked.reserve(result.boundaryEdges.size());
        for (const MeshEdge &e : std::as_const(result.boundaryEdges))
            locked.insert(edgeKey(e.v0, e.v1));

        for (int s = 0; s < qregs.size(); ++s)
        {
            const PreparedQuadRegion &q = qregs[s];
            if (q.mode != QuadRegionMode::Free || cellIds[s].isEmpty()) continue;
            QuadPairingOptions po;
            po.bounds = m_opts.quadRegionBounds;
            QVector<int> oldToNew;
            const QuadPairingStats ps = pairTrianglesIntoQuads(result, cellIds[s], q.templates,
                                                               locked, po, &oldToNew);
            QuadRegionReport &rep = m_quadReports[q.index];
            rep.templateQuads = ps.templateQuads;
            rep.gapQuads      = ps.gapQuads;
            for (int j = s + 1; j < qregs.size(); ++j)
                for (int &c : cellIds[j]) c = oldToNew[c];
        }

        for (int s = 0; s < qregs.size(); ++s)
        {
            const PreparedQuadRegion &q = qregs[s];
            if (q.mode != QuadRegionMode::Free || q.movable.isEmpty()) continue;
            QVector<int> vOldToNew;
            const QuadCleanupStats cs = cleanupAndSmoothQuads(result, q.movable,
                                                              m_opts.quadCleanup, &vOldToNew);
            QuadRegionReport &rep = m_quadReports[q.index];
            rep.doubletsRemoved = cs.doubletsRemoved;
            rep.diagonalSwaps   = cs.diagonalSwaps;
            rep.verticesMoved   = cs.verticesMoved;
            for (int j = s + 1; j < qregs.size(); ++j)
            {
                QSet<int> remapped;
                for (int v : qregs[j].movable)
                    if (v >= 0 && v < vOldToNew.size() && vOldToNew[v] >= 0) remapped.insert(vOldToNew[v]);
                qregs[j].movable = remapped;
            }
        }
    }

    // ── Stitch structured patches (G3) ────────────────────────────────────
    // Patch boundary vertices were PSLG input points, so Triangle hands them
    // back with the exact (quantised) coordinates pushPoint() stored: match
    // by the same key. Interior patch vertices are new. Quads go AFTER every
    // triangle — the engine's cell order.
    if (outputOk && !patches.isEmpty())
    {
        const double eps = m_opts.patchSnapEps;
        auto keyOf = [&](const QPointF &p) {
            const double sx = (eps > 0.0) ? (p.x() - quantOrigin.x()) / eps
                                          : (p.x() - quantOrigin.x()) * 1e7;
            const double sy = (eps > 0.0) ? (p.y() - quantOrigin.y()) / eps
                                          : (p.y() - quantOrigin.y()) * 1e7;
            return PointKey(qRound64(sx), qRound64(sy));
        };
        QHash<PointKey, int> outIndex;
        outIndex.reserve(result.vertices.size());
        for (int i = 0; i < result.vertices.size(); ++i)
            outIndex.insert(keyOf(result.vertices[i].xy), i);

        for (const PatchMesh &pm : patches)
        {
            if (pm.quads.isEmpty()) continue;
            QVector<int> localToGlobal(pm.xy.size(), -1);
            for (int k = 0; k < pm.xy.size(); ++k)
            {
                const PointKey key = keyOf(pm.xy[k]);
                const auto it = outIndex.constFind(key);
                if (it != outIndex.constEnd()) { localToGlobal[k] = it.value(); continue; }
                MeshVertex v;
                v.xy = pm.xy[k];
                localToGlobal[k] = result.vertices.size();
                result.vertices.append(v);
                outIndex.insert(key, localToGlobal[k]);
            }
            for (const MeshTriangle &q : pm.quads)
            {
                MeshTriangle c = q;
                c.v0 = localToGlobal[q.v0];
                c.v1 = localToGlobal[q.v1];
                c.v2 = localToGlobal[q.v2];
                c.v3 = localToGlobal[q.v3];
                if (c.tag.isEmpty()) c.tag = pm.tag;
                result.triangles.append(c);
            }
        }
    }

    result.ok = outputOk && (out.numberoftriangles > 0);
    if (!result.ok && result.errorMsg.isEmpty())
        result.errorMsg = QStringLiteral(
            "Triangle produced 0 triangles — domain may be self-intersecting "
            "or constraint segments may cross.");

    // ── Tri-pair merge (G2) — last step ───────────────────────────────────
    // Every constrained segment (domain boundary, holes, breaklines, patch
    // boundaries) is a locked edge: a quad never straddles one.
    if (result.ok && m_opts.mergeTrianglePairs)
    {
        QSet<QPair<int, int>> locked;
        locked.reserve(result.boundaryEdges.size());
        for (const MeshEdge &e : std::as_const(result.boundaryEdges))
            locked.insert(edgeKey(e.v0, e.v1));
        mergeTrianglePairs(result, m_opts.quadMerge, locked, nullptr);
    }

    // ── Quad region reports: cells inside each ring on exit ───────────────
    // Every ring is a constraint loop (or a patch boundary), so a cell is
    // wholly inside or outside and its centroid decides membership.
    if (result.ok && !qregs.isEmpty())
    {
        QVector<QVector<double>> rect(qregs.size());
        for (const MeshTriangle &c : std::as_const(result.triangles))
        {
            const QPointF cen = cellGeom(result.vertices, c).centroid;
            for (int s = 0; s < qregs.size(); ++s)
            {
                if (!inQuadRing(qregs[s], cen)) continue;
                QuadRegionReport &rep = m_quadReports[qregs[s].index];
                if (c.isQuad())
                {
                    ++rep.quads;
                    const QuadQuality qq = quadQuality(result.vertices, c);
                    rep.minScaledJacobian = std::min(rep.minScaledJacobian, qq.scaledJacobian);
                    rect[s].append(qq.rectangularity);
                }
                else ++rep.triangles;
                break;
            }
        }
        for (int s = 0; s < qregs.size(); ++s)
        {
            QuadRegionReport &rep = m_quadReports[qregs[s].index];
            if (rep.quads == 0) { rep.minScaledJacobian = 0.0; continue; }
            QVector<double> &r = rect[s];
            std::sort(r.begin(), r.end());
            const int mid = r.size() / 2;
            rep.medianRectangularity = (r.size() % 2 == 1) ? r[mid] : 0.5 * (r[mid - 1] + r[mid]);
        }
    }

    // ── Cleanup ───────────────────────────────────────────────────────────
    std::free(in.pointlist);
    std::free(in.pointmarkerlist);
    std::free(in.segmentlist);
    std::free(in.segmentmarkerlist);
    std::free(in.holelist);
    std::free(in.regionlist);
    freeOutput(out);

    return result;
}

} // namespace mesh
