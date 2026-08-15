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
 */
#include "mesh/meshgenerator.h"

#include <QDebug>
#include <QHash>
#include <QtMath>

#include <cmath>
#include <cstdlib>
#include <cstring>

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

    // 2) Steiner points — exact-coord vertices that must appear in the mesh.
    for (const SteinerPoint &sp : m_steiners)
    {
        const int idx = pushPoint(sp.xy, sp.marker);
        if (sp.marker != 0 && !sp.tag.isEmpty())
            m_vertexTagByMarker.insert(sp.marker, sp.tag);
        Q_UNUSED(idx);
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
        for (int i = 1; i < cs.path.size(); ++i)
        {
            const int curr = pushPoint(cs.path[i], cs.marker);
            if (curr != prev)
            {
                userSegments.append(qMakePair(prev, curr));
                userSegmentMarkers.append(cs.marker);
            }
            prev = curr;
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

    // Holes
    in.numberofholes = m_holes.size();
    if (!m_holes.isEmpty())
    {
        in.holelist = static_cast<REAL *>(std::malloc(sizeof(REAL) * 2 * m_holes.size()));
        if (!in.holelist) return packOom();
        for (int i = 0; i < m_holes.size(); ++i)
        {
            in.holelist[2 * i + 0] = m_holes[i].x();
            in.holelist[2 * i + 1] = m_holes[i].y();
        }
    }

    // Regions — Triangle's regionlist is an array of 4-doubles per region:
    // (x, y, attribute, max_area). Attribute is propagated to
    // triangleattributelist for each output triangle.
    in.numberofregions = m_regions.size();
    if (!m_regions.isEmpty())
    {
        in.regionlist = static_cast<REAL *>(std::malloc(sizeof(REAL) * 4 * m_regions.size()));
        if (!in.regionlist) return packOom();
        for (int i = 0; i < m_regions.size(); ++i)
        {
            in.regionlist[4 * i + 0] = m_regions[i].xy.x();
            in.regionlist[4 * i + 1] = m_regions[i].xy.y();
            in.regionlist[4 * i + 2] = m_regions[i].attribute;
            in.regionlist[4 * i + 3] = m_regions[i].maxArea > 0
                                           ? m_regions[i].maxArea
                                           : -1.0;
            if (!m_regions[i].tag.isEmpty())
                m_triangleTagByRegionId.insert(
                    static_cast<int>(m_regions[i].attribute), m_regions[i].tag);
        }
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
        const bool useSizeFn = static_cast<bool>(m_refineHook.targetAreaAt);
        if (m_opts.maxArea > 0.0 && !useSizeFn)
            sw += QStringLiteral("a%1").arg(m_opts.maxArea, 0, 'f', 4);
        else if (!m_regions.isEmpty())
            sw += QStringLiteral("a");  // per-region area only.

        // 'u' enables the triunsuitable() hook (cancellation, progress, graded
        // sizing). It also sets Triangle's `quality` flag, so the refinement
        // pass — and therefore the hook — runs even when minAngle and maxArea
        // are both zero. That is what makes cancellation available at all.
        if (useSizeFn || m_refineHook.isCancelled || m_refineHook.onProgress)
            sw += QStringLiteral("u");
        if (!m_opts.allowSteiner)         sw += QStringLiteral("YY");
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
        const RefineHookGuard hookGuard(&m_refineHook);
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
            e.tag    = m_edgeTagByMarker.value(e.marker);
            result.boundaryEdges.append(e);
        }
    }

    result.ok = outputOk && (out.numberoftriangles > 0);
    if (!result.ok && result.errorMsg.isEmpty())
        result.errorMsg = QStringLiteral(
            "Triangle produced 0 triangles — domain may be self-intersecting "
            "or constraint segments may cross.");

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
