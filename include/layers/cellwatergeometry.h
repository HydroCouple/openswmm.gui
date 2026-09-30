/* VFR water geometry shared by maps, profiles and queries.
 * See workplans/2D_VFR_RENDERING_PLAN_2026-09-30.md. GPL-3.0-or-later. */
#ifndef OPENSWMM_CELLWATERGEOMETRY_H
#define OPENSWMM_CELLWATERGEOMETRY_H

#include "layers/vertexdepthreconstruct.h"
#include <QPointF>
#include <limits>

namespace CellWaterGeometry {

enum class State : unsigned char { Invalid, Dry, Wet };
struct Surface {
    double datum = 0.0;
    double level = 0.0; // eta relative to datum, never rounded to float
    State state = State::Invalid;
    double signedDepth(double z) const {
        return state == State::Wet && std::isfinite(z)
            ? level - (z - datum) : std::numeric_limits<double>::quiet_NaN();
    }
};

// Reuse the established exact closures, evaluated at a local datum. Both
// halves of a quad receive ONE stage from their combined storage relation.
inline Surface reconstruct(const VertexDepthReconstruct::CellSplit& cell,
                           double meanDepth, const std::vector<double>& z)
{
    Surface out;
    if (!std::isfinite(meanDepth) || meanDepth < 0.0) return out;
    for (int k = 0; k < cell.vertexCount(); ++k)
        if (cell.v[k] < 0 || size_t(cell.v[k]) >= z.size()
            || !std::isfinite(z[cell.v[k]])) return out;
    out.datum = z[cell.v[0]];
    for (int k = 1; k < cell.vertexCount(); ++k)
        out.datum = std::min(out.datum, z[cell.v[k]]);
    if (meanDepth == 0.0) { out.state = State::Dry; return out; }
    if (cell.vertexCount() == 3) {
        out.level = VertexDepthReconstruct::cellEtaFromMeanDepth(meanDepth,
            z[cell.v[0]] - out.datum, z[cell.v[1]] - out.datum,
            z[cell.v[2]] - out.datum);
    } else {
        if (cell.nSub != 2 || !(cell.area[0] > 0) || !(cell.area[1] > 0)
            || !std::isfinite(cell.area[0] + cell.area[1])) return out;
        double zs[6];
        for (int s = 0; s < 2; ++s)
            for (int k = 0; k < 3; ++k) {
                const int vi = cell.sub[s][k];
                if (vi < 0 || size_t(vi) >= z.size() || !std::isfinite(z[vi])) return out;
                zs[3*s+k] = z[vi] - out.datum;
            }
        out.level = VertexDepthReconstruct::quadEtaFromMeanDepth(
            zs, cell.area[0], cell.area[1], meanDepth);
    }
    if (std::isfinite(out.level)) out.state = State::Wet;
    return out;
}

using CornerDepths = std::array<double,4>;

// Shared edges are cached once per mesh. Each pair contains the two cell
// corners at the SAME endpoint (corner index = 4*cell + local vertex).
struct SmoothTopology {
    struct Edge { int a0, a1, b0, b1; };
    std::vector<Edge> edges;
};

inline SmoothTopology smoothTopology(const std::vector<VertexDepthReconstruct::CellSplit>& cells)
{
    struct Side { int lo, hi, a, b; };
    std::vector<Side> sides;
    sides.reserve(cells.size()*4);
    for (int c=0;c<int(cells.size());++c) {
        const auto& cell=cells[size_t(c)];
        for (int k=0;k<cell.vertexCount();++k) {
            const int next=(k+1)%cell.vertexCount(), u=cell.v[k], v=cell.v[next];
            if (u<0 || v<0 || u==v) continue;
            sides.push_back(u<v ? Side{u,v,4*c+k,4*c+next} : Side{v,u,4*c+next,4*c+k});
        }
    }
    std::sort(sides.begin(),sides.end(),[](const Side& a,const Side& b) {
        return a.lo<b.lo || (a.lo==b.lo && a.hi<b.hi);
    });
    SmoothTopology out;
    for (size_t i=0;i<sides.size();) {
        size_t end=i+1;
        while (end<sides.size() && sides[end].lo==sides[i].lo && sides[end].hi==sides[i].hi) ++end;
        // Do not infer a connection through a non-manifold edge.
        if (end-i==2 && sides[i].a/4!=sides[i+1].a/4)
            out.edges.push_back({sides[i].a,sides[i+1].a,sides[i].b,sides[i+1].b});
        i=end;
    }
    return out;
}

// Continuous, bounded display projection of VFR stages on each connected
// wet vertex fan. Include signed values at HIGH/DRY corners too: dropping
// those and later extrapolating a maximum stage creates the uphill artifact.
// Every mean uses depths relative to the same vertex datum, in double.
// Raw cell storage is unchanged; the smooth display is not a per-cell
// volume-conserving remap. Dry cells never receive a surface.
inline void smoothCornerDepths(const std::vector<VertexDepthReconstruct::CellSplit>& cells,
                               const std::vector<Surface>& surfaces,
                               const std::vector<double>& z,
                               const SmoothTopology& topology,
                               std::vector<CornerDepths>& out)
{
    const double nan=std::numeric_limits<double>::quiet_NaN();
    out.assign(cells.size(),CornerDepths{nan,nan,nan,nan});
    std::vector<int> parent(cells.size()*4,-1);
    std::vector<double> weight(cells.size()*4,0.0);
    auto value=[&](int corner)->double& { return out[size_t(corner/4)][corner%4]; };
    for (int c=0;c<int(cells.size()) && c<int(surfaces.size());++c) {
        const auto& cell=cells[size_t(c)];
        const double area=cell.area[0]+(cell.nSub==2 ? cell.area[1] : 0.0);
        if (surfaces[size_t(c)].state!=State::Wet || !(area>0) || !std::isfinite(area)) continue;
        for (int k=0;k<cell.vertexCount();++k) {
            const int v=cell.v[k], corner=4*c+k;
            if (v<0 || size_t(v)>=z.size()) continue;
            value(corner)=surfaces[size_t(c)].signedDepth(z[size_t(v)]);
            if (!std::isfinite(value(corner))) continue;
            parent[size_t(corner)]=corner;
            weight[size_t(corner)]=area/cell.vertexCount();
        }
    }
    auto root=[&](int i) {
        while (parent[size_t(i)]!=i) {
            parent[size_t(i)]=parent[size_t(parent[size_t(i)])];
            i=parent[size_t(i)];
        }
        return i;
    };
    auto join=[&](int a,int b) {
        a=root(a); b=root(b);
        if (a==b) return;
        if (b<a) std::swap(a,b);
        const double total=weight[size_t(a)]+weight[size_t(b)];
        value(a)+=(value(b)-value(a))*(weight[size_t(b)]/total);
        weight[size_t(a)]=total;
        parent[size_t(b)]=a;
    };
    for (const auto& e:topology.edges) {
        if (parent[size_t(e.a0)]<0 || parent[size_t(e.a1)]<0
            || parent[size_t(e.b0)]<0 || parent[size_t(e.b1)]<0) continue;
        const int c=e.a0/4, other=e.a1/4;
        const double low=std::min(z[size_t(cells[size_t(c)].v[e.a0%4])],
                                  z[size_t(cells[size_t(c)].v[e.b0%4])]);
        // Both VFR surfaces must wet a positive length of the common edge.
        if (!(surfaces[size_t(c)].signedDepth(low)>0)
            || !(surfaces[size_t(other)].signedDepth(low)>0)) continue;
        join(e.a0,e.a1); join(e.b0,e.b1);
    }
    for (int i=0;i<int(parent.size());++i)
        if (parent[size_t(i)]>=0) value(i)=value(root(i));
}

inline bool barycentric(const QPointF& p, const QPointF& a,
                        const QPointF& b, const QPointF& c,
                        std::array<double, 3>& w)
{
    const QPointF ab = b-a, ac = c-a, ap = p-a;
    const double det = ab.x()*ac.y() - ab.y()*ac.x();
    if (!std::isfinite(det) || det == 0.0) return false;
    w[1] = (ap.x()*ac.y() - ap.y()*ac.x()) / det;
    w[2] = (ab.x()*ap.y() - ab.y()*ap.x()) / det;
    w[0] = 1.0 - w[1] - w[2];
    return std::isfinite(w[0]) && std::isfinite(w[1]) && std::isfinite(w[2]);
}

// Intersect a line segment with a triangle, including paths on an edge.
// Ownership of overlapping intervals is resolved by the caller's stable IDs.
inline bool triangleInterval(const QPointF& p, const QPointF& q,
                             const QPointF& a, const QPointF& b, const QPointF& c,
                             double& lo, double& hi)
{
    std::array<double, 3> wp, wq;
    if (!barycentric(p,a,b,c,wp) || !barycentric(q,a,b,c,wq)) return false;
    lo = 0; hi = 1;
    for (int k = 0; k < 3; ++k) {
        // Snap only barycentric roundoff on an edge, not a finite-width halo.
        const double tol = 32 * std::numeric_limits<double>::epsilon()
                         * std::max({1.0, std::abs(wp[k]), std::abs(wq[k])});
        if (std::abs(wp[k]) < tol) wp[k] = 0;
        if (std::abs(wq[k]) < tol) wq[k] = 0;
        if (wp[k] < 0 && wq[k] < 0) return false;
        if (wp[k] < 0) lo = std::max(lo, wp[k] / (wp[k]-wq[k]));
        if (wq[k] < 0) hi = std::min(hi, wp[k] / (wp[k]-wq[k]));
    }
    return hi > lo;
}

// Exact wet interval; zero-depth and unknown segments do not paint water.
inline bool wetInterval(double q0, double q1, double cutoff, double& lo, double& hi)
{
    lo = 0; hi = 1;
    if (!std::isfinite(q0) || !std::isfinite(q1)
        || std::max(q0,q1) <= cutoff) return false;
    if (q0 < cutoff) lo = (q0-cutoff)/(q0-q1);
    if (q1 < cutoff) hi = (q0-cutoff)/(q0-q1);
    return hi > lo;
}

struct ClipVertex { QPointF point; double depth; };
struct WetPolygon { std::array<ClipVertex, 4> vertices; int size = 0; };
inline WetPolygon clipTriangle(const QPointF& a, const QPointF& b, const QPointF& c,
                               double qa, double qb, double qc, double cutoff)
{
    WetPolygon out;
    if (!std::isfinite(qa) || !std::isfinite(qb) || !std::isfinite(qc)
        || std::max({qa,qb,qc}) <= cutoff) return out;
    const ClipVertex v[3] = {{a,qa}, {b,qb}, {c,qc}};
    for (int i = 0; i < 3; ++i) {
        const auto& p = v[i]; const auto& q = v[(i+1)%3];
        if (p.depth >= cutoff) out.vertices[out.size++] = p;
        if ((p.depth < cutoff && q.depth > cutoff)
            || (p.depth > cutoff && q.depth < cutoff)) {
            const double t = (p.depth-cutoff)/(p.depth-q.depth);
            out.vertices[out.size++] = {p.point+t*(q.point-p.point), cutoff};
        }
    }
    return out;
}

} // namespace CellWaterGeometry
#endif
