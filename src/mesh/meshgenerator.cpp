/*!
 * \file   meshgenerator.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license MIT
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

#include <cstdlib>
#include <cstring>

extern "C" {
#include "triangle.h"
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
void MeshGenerator::addHole(const QPointF &xy)                { m_holes.append(xy); }
void MeshGenerator::addRegion(const RegionMarker &r)          { m_regions.append(r); }
void MeshGenerator::setOptions(const GenerationOptions &o)    { m_opts = o; }

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
    auto pushPoint = [&](const QPointF &xy, int marker) {
        const qint64 qx = qRound64(xy.x() * 1e7);
        const qint64 qy = qRound64(xy.y() * 1e7);
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
        points.append(QPointF(qx / 1e7, qy / 1e7));
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
    for (const QPolygonF &dom : m_domains)
    {
        const int domN = dom.size();
        if (domN < 3) continue;            // can't form a closed ring
        int firstIdx = -1, prevIdx = -1;
        for (int i = 0; i < domN; ++i)
        {
            const QPointF &p = dom[i];
            if (i == domN - 1 && i > 0
                && qFuzzyCompare(p.x() + 1, dom[0].x() + 1)
                && qFuzzyCompare(p.y() + 1, dom[0].y() + 1))
                break;  // closed polygon: skip the dup-of-first vertex.
            const int idx = pushPoint(p, kBoundaryMarker);
            if (firstIdx < 0) firstIdx = idx;
            if (prevIdx >= 0)
                domSegments.append(qMakePair(prevIdx, idx));
            prevIdx = idx;
        }
        if (prevIdx >= 0 && firstIdx >= 0 && prevIdx != firstIdx)
            domSegments.append(qMakePair(prevIdx, firstIdx));
    }
    if (domSegments.isEmpty())
    {
        result.errorMsg = QStringLiteral(
            "MeshGenerator: no usable boundary polygons "
            "(every supplied polygon had < 3 vertices).");
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

    // ── Pack input triangulateio ──────────────────────────────────────────
    triangulateio in{};   zeroIO(in);
    triangulateio out{};  zeroIO(out);

    // Points
    in.numberofpoints = points.size();
    in.pointlist      = static_cast<REAL *>(std::malloc(sizeof(REAL) * 2 * points.size()));
    in.pointmarkerlist = static_cast<int *>(std::malloc(sizeof(int) * points.size()));
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
        // p = read PSLG; z = zero-based; e = output edge list (we want
        // segments → boundaryEdges); A = regional attributes per triangle.
        sw = QStringLiteral("pzeA");
        if (m_opts.minAngle > 0.0)
            sw += QStringLiteral("q%1").arg(m_opts.minAngle, 0, 'f', 2);
        if (m_opts.maxArea > 0.0)
            sw += QStringLiteral("a%1").arg(m_opts.maxArea, 0, 'f', 4);
        else if (!m_regions.isEmpty())
            sw += QStringLiteral("a");  // per-region area only.
        if (!m_opts.allowSteiner)         sw += QStringLiteral("YY");
        if (m_opts.conformingDelaunay)    sw += QStringLiteral("D");
        if (m_opts.maxSteinerPoints > 0)
            sw += QStringLiteral("S%1").arg(m_opts.maxSteinerPoints);
        if (m_opts.quiet)                 sw += QStringLiteral("Q");
    }
    QByteArray swBa = sw.toLatin1();

    // ── Run Triangle ──────────────────────────────────────────────────────
    // Triangle's `triangulate` is the canonical entry — see vendor/triangle.
    triangulate(swBa.data(), &in, &out, /*vorout=*/nullptr);

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

    result.triangles.reserve(out.numberoftriangles);
    for (int i = 0; i < out.numberoftriangles; ++i)
    {
        MeshTriangle t;
        t.v0 = out.trianglelist[3 * i + 0];
        t.v1 = out.trianglelist[3 * i + 1];
        t.v2 = out.trianglelist[3 * i + 2];
        if (out.triangleattributelist && out.numberoftriangleattributes > 0)
        {
            const int regionId = static_cast<int>(out.triangleattributelist[i]);
            t.tag = m_triangleTagByRegionId.value(regionId);
        }
        result.triangles.append(t);
    }

    if (out.segmentlist && out.numberofsegments > 0)
    {
        result.boundaryEdges.reserve(out.numberofsegments);
        for (int i = 0; i < out.numberofsegments; ++i)
        {
            MeshEdge e;
            e.v0     = out.segmentlist[2 * i + 0];
            e.v1     = out.segmentlist[2 * i + 1];
            e.marker = out.segmentmarkerlist ? out.segmentmarkerlist[i] : 0;
            e.tag    = m_edgeTagByMarker.value(e.marker);
            result.boundaryEdges.append(e);
        }
    }

    result.ok = (out.numberoftriangles > 0);
    if (!result.ok)
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
