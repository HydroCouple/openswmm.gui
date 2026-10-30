/* VFR water geometry shared by maps, profiles and queries.
 * See workplans/2D_VFR_RENDERING_PLAN_2026-09-30.md. GPL-3.0-or-later. */

/**
 * @file      cellwatergeometry.h
 * @brief     Water surface clipping and geometry for partially wet mesh cells.
 *
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

#ifndef OPENSWMM_CELLWATERGEOMETRY_H
#define OPENSWMM_CELLWATERGEOMETRY_H

#include "layers/vertexdepthreconstruct.h"
#include <QPointF>
#include <limits>
#include <utility>

namespace CellWaterGeometry
{

enum class State : unsigned char { Invalid, Dry, Wet };
struct Surface {
    double datum = 0.0;
    double level = 0.0; // eta relative to datum, never rounded to float
    State state = State::Invalid;
    /**
     * @param[in] z Bed elevation in the surface datum/unit, normally SI metres.
     * @return level-(z-datum) only for Wet state and finite z, otherwise NaN; negative values carry a below-bed trace, not an invalid sentinel. Assumes stored datum/level are valid for Wet state.
     */
    double signedDepth(double z) const
    {
        return state == State::Wet && std::isfinite(z) ? level - (z - datum)
                                                       : std::numeric_limits<double>::quiet_NaN();
    }
};

// Reuse the established exact closures, evaluated at a local datum. Both
// halves of a quad receive ONE stage from their combined storage relation.
/**
 * @brief Recover one common cell stage from triangle/quad mean storage at a local elevation datum.
 * @param[in] cell Valid triangle/quad connectivity; positive quad depth additionally requires two positive finite-area subtriangles with readable elevation indices.
 * @param[in] meanDepth Nonnegative finite mean depth in bed vertical units, normally SI metres.
 * @param[in] z Vertex bed elevations with one datum/unit, indexed by cell vertices/subtriangles.
 * @return Owned Invalid surface for bad depth/required geometry, Dry for exactly zero depth, otherwise Wet when finite stage is recovered. Datum is minimum corner bed, level is relative stage. Dry quad input does not validate quad area/split details.
 * @note Performs no allocation/source mutation and retains no references.
 */
inline Surface reconstruct(const VertexDepthReconstruct::CellSplit &cell, double meanDepth,
                           const std::vector<double> &z)
{
    Surface out;
    if (!std::isfinite(meanDepth) || meanDepth < 0.0)
        return out;
    for (int k = 0; k < cell.vertexCount(); ++k)
        if (cell.v[k] < 0 || size_t(cell.v[k]) >= z.size() || !std::isfinite(z[cell.v[k]]))
            return out;
    out.datum = z[cell.v[0]];
    for (int k = 1; k < cell.vertexCount(); ++k)
        out.datum = std::min(out.datum, z[cell.v[k]]);
    if (meanDepth == 0.0) {
        out.state = State::Dry;
        return out;
    }
    if (cell.vertexCount() == 3) {
        out.level = VertexDepthReconstruct::cellEtaFromMeanDepth(
            meanDepth, z[cell.v[0]] - out.datum, z[cell.v[1]] - out.datum,
            z[cell.v[2]] - out.datum);
    } else {
        if (cell.nSub != 2 || !(cell.area[0] > 0) || !(cell.area[1] > 0) ||
            !std::isfinite(cell.area[0] + cell.area[1]))
            return out;
        double zs[6];
        for (int s = 0; s < 2; ++s)
            for (int k = 0; k < 3; ++k) {
                const int vi = cell.sub[s][k];
                if (vi < 0 || size_t(vi) >= z.size() || !std::isfinite(z[vi]))
                    return out;
                zs[3 * s + k] = z[vi] - out.datum;
            }
        out.level =
            VertexDepthReconstruct::quadEtaFromMeanDepth(zs, cell.area[0], cell.area[1], meanDepth);
    }
    if (std::isfinite(out.level))
        out.state = State::Wet;
    return out;
}

using CornerDepths = std::array<double, 4>;

// Presentation state is separate from stored water and the solver's dry flag.
enum class DisplayState : unsigned char { Invalid, Dry, ThinFilm, PartiallyWet, Wet };
struct VisibilityPolicy {
    double filmDepth = 0.0; // SI metres; zero shows every positive surface
    bool showThinFilms = false;
};

struct DepthSummary {
    enum Flag : unsigned char { Negative = 1, Invalid = 2 };
    double deepest = 0.0;
    unsigned char flags = 0;
};

/**
 * @param[in] depths Up to four signed corner depths, normally metres.
 * @param[in] count Number of active corners, caller constrained zero through four.
 * @return Deepest nonnegative depth and flags for negative/invalid values; first nonfinite returns immediately with any earlier maximum/negative flag retained. Does not distinguish true dry state by itself.
 */
inline DepthSummary summarizeDepths(const CornerDepths &depths, int count)
{
    DepthSummary out;
    for (int k = 0; k < count; ++k) {
        const double depth = depths[k];
        if (!std::isfinite(depth)) {
            out.flags |= DepthSummary::Invalid;
            return out;
        }
        out.deepest = std::max(out.deepest, depth);
        if (depth < 0.0)
            out.flags |= DepthSummary::Negative;
    }
    return out;
}

/**
 * @param[in] depths Previously summarized signed traces; Invalid flag takes precedence.
 * @param[in] policy Finite nonnegative film threshold in the depth unit (SI metres in layer use); showThinFilms does not alter classification.
 * @return Invalid, ThinFilm at/below threshold plus four float-epsilon-scaled tolerance, otherwise PartiallyWet when any negative trace exists or Wet. This overload never returns Dry.
 */
inline DisplayState displayState(const DepthSummary &depths, const VisibilityPolicy &policy)
{
    if (depths.flags & DepthSummary::Invalid)
        return DisplayState::Invalid;
    // Source depths are floats: allow four float ulps at the threshold,
    // independent of elevation datum or cell relief, never a fixed film width.
    const double tolerance =
        4 * std::numeric_limits<float>::epsilon() * std::max(depths.deepest, policy.filmDepth);
    if (depths.deepest <= policy.filmDepth + tolerance)
        return DisplayState::ThinFilm;
    return depths.flags & DepthSummary::Negative ? DisplayState::PartiallyWet : DisplayState::Wet;
}

/**
 * @param[in] depths Signed traces in policy depth units.
 * @param[in] count Active corners zero through four.
 * @param[in] policy Display threshold settings, not solver wet/dry policy.
 * @return Classification of summarizeDepths(); no geometry or volume change occurs.
 */
inline DisplayState displayState(const CornerDepths &depths, int count,
                                 const VisibilityPolicy &policy)
{
    return displayState(summarizeDepths(depths, count), policy);
}

/**
 * @param[in] cell Triangle/quad connectivity indexing z.
 * @param[in] surface Reconstructed physical surface; Invalid/Dry state is returned directly.
 * @param[in] z Vertex bed elevations in the surface datum/unit.
 * @param[in] policy Presentation threshold in the same vertical units.
 * @return Invalid for unusable corner lookup/nonfinite trace, physical Dry when stored, or signed-corner classification otherwise. Does not mutate/reconstruct storage.
 */
inline DisplayState displayState(const VertexDepthReconstruct::CellSplit &cell,
                                 const Surface &surface, const std::vector<double> &z,
                                 const VisibilityPolicy &policy)
{
    if (surface.state == State::Invalid)
        return DisplayState::Invalid;
    if (surface.state == State::Dry)
        return DisplayState::Dry;
    CornerDepths depths{};
    for (int k = 0; k < cell.vertexCount(); ++k) {
        const int v = cell.v[k];
        if (v < 0 || size_t(v) >= z.size())
            return DisplayState::Invalid;
        depths[k] = surface.signedDepth(z[size_t(v)]);
    }
    return displayState(depths, cell.vertexCount(), policy);
}

/**
 * @param[in] state Classified display state.
 * @param[in] policy Thin-film visibility switch.
 * @return True for Wet/PartiallyWet, plus ThinFilm only when enabled; Invalid/Dry are always hidden.
 */
inline bool visible(DisplayState state, const VisibilityPolicy &policy)
{
    return state == DisplayState::Wet || state == DisplayState::PartiallyWet ||
           (state == DisplayState::ThinFilm && policy.showThinFilms);
}

// Shared edges are cached once per mesh. Each pair contains the two cell
// corners at the SAME endpoint (corner index = 4*cell + local vertex).
struct SmoothTopology {
    struct Edge {
        int a0, a1, b0, b1;
    };
    std::vector<Edge> edges;
};

/**
 * @brief Cache endpoint correspondences across exactly two incident mesh-cell sides.
 * @param[in] cells Ordered triangles/quads with stable vertex identities; invalid/repeated endpoint sides are skipped.
 * @return Owned shared-edge corner slots encoded4*cell+localVertex. Boundary, nonmanifold and same-cell duplicate edges are omitted; connectivity is topological, without XY/bed/wet testing.
 * @note Allocates/sorts side records; rebuild after any connectivity/order change. No mesh references are retained.
 */
inline SmoothTopology smoothTopology(const std::vector<VertexDepthReconstruct::CellSplit> &cells)
{
    struct Side {
        int lo, hi, a, b;
    };
    std::vector<Side> sides;
    sides.reserve(cells.size() * 4);
    for (int c = 0; c < int(cells.size()); ++c) {
        const auto &cell = cells[size_t(c)];
        for (int k = 0; k < cell.vertexCount(); ++k) {
            const int next = (k + 1) % cell.vertexCount(), u = cell.v[k], v = cell.v[next];
            if (u < 0 || v < 0 || u == v)
                continue;
            sides.push_back(u < v ? Side{u, v, 4 * c + k, 4 * c + next}
                                  : Side{v, u, 4 * c + next, 4 * c + k});
        }
    }
    std::sort(sides.begin(), sides.end(), [](const Side &a, const Side &b) {
        return a.lo < b.lo || (a.lo == b.lo && a.hi < b.hi);
    });
    SmoothTopology out;
    for (size_t i = 0; i < sides.size();) {
        size_t end = i + 1;
        while (end < sides.size() && sides[end].lo == sides[i].lo && sides[end].hi == sides[i].hi)
            ++end;
        // Do not infer a connection through a non-manifold edge.
        if (end - i == 2 && sides[i].a / 4 != sides[i + 1].a / 4)
            out.edges.push_back({sides[i].a, sides[i + 1].a, sides[i].b, sides[i + 1].b});
        i = end;
    }
    return out;
}

// Volume closure for an inclined surface. The prescribed height offsets are
// relative to the area centroid; subtracting them from the bed reuses VFR,
// including the original storage diagonal of a quad. Return signed depths.
/**
 * @brief Invert inclined cell storage using offset-adjusted local beds.
 * @param[in] cell Valid connectivity/subtriangle membership; all referenced vertices must be readable and each subtriangle vertex must occur in v.
 * @param[in] meanDepth Nonnegative finite cell mean depth in the bed unit.
 * @param[in] z Finite vertex beds with one datum/unit.
 * @param[in] offsets Prescribed surface-height offsets in vertical units relative to the area centroid, one per active corner.
 * @param[out] level Recovered stage relative to the first corner's bed datum after offset adjustment.
 * @return Signed corner traces at the recovered stage; unused fourth triangle slot is zero. Caller supplies consistent positive areas/split; this low-level routine performs no bounds validation and does not mutate source storage.
 */
inline CornerDepths inclinedCorners(const VertexDepthReconstruct::CellSplit &cell, double meanDepth,
                                    const std::vector<double> &z, const CornerDepths &offsets,
                                    double &level)
{
    double bed[4]{};
    for (int k = 0; k < cell.vertexCount(); ++k)
        bed[k] = (z[size_t(cell.v[k])] - z[size_t(cell.v[0])]) - offsets[k];
    if (cell.nSub == 1) {
        level = VertexDepthReconstruct::cellEtaFromMeanDepth(meanDepth, bed[0], bed[1], bed[2]);
    } else {
        double zs[6];
        for (int s = 0; s < 2; ++s)
            for (int k = 0; k < 3; ++k) {
                const auto it = std::find(cell.v.begin(), cell.v.end(), cell.sub[s][k]);
                zs[3 * s + k] = bed[size_t(it - cell.v.begin())];
            }
        level =
            VertexDepthReconstruct::quadEtaFromMeanDepth(zs, cell.area[0], cell.area[1], meanDepth);
    }
    CornerDepths out{};
    for (int k = 0; k < cell.vertexCount(); ++k)
        out[k] = level - bed[k];
    return out;
}

// Fit shared signed depths over hydraulically connected neighborhoods.
// The helper is included here because it consumes the geometry types above.
#include "layers/neighborhoodwaterfit.h"

/**
 * @brief Request the connected-neighborhood surface fit using common geometry inputs.
 * @param[in] cells Stable valid triangle/quad splits and areas, one per depth/surface entry.
 * @param[in] surfaces Physical reconstructed surfaces, one per cell.
 * @param[in] x,y,z Finite vertex-coordinate arrays of equal length; z/vertical values use SI metres and XY share one projected horizontal coordinate system. Cell indices must be valid.
 * @param[in] topology Shared-edge slots built for exactly this ordered mesh.
 * @param[in] depths Positive finite participating mean depths, one per cell.
 * @param[in] flux Four outward side-flux slots per cell, including padded triangle slot; finite opposite-sign/matching shared flux establishes flowing contact. Values are compared for relative consistency without unit conversion.
 * @param[in] policy Forwarded presentation policy; the current fit deliberately ignores visibility thresholds so fitting stays independent of display toggles.
 * @param[in,out] stats Optional caller-owned counters incremented/shape fields overwritten during a fit; initialize/reset externally for per-call statistics.
 * @param[in,out] workspace Optional exclusive reusable allocation scratch, reinitialized for each call rather than numerically warm-started.
 * @param[in] reuse Optional moved-by-value result buffer whose capacity can be reused; its previous values are discarded.
 * @return Owned signed corner arrays from NeighborhoodFit::reconstruct(), or empty for coordinate-length mismatch, inconsistent cell-array lengths or no supported flowing connection. No solver state is changed; caller can use the empty result to choose a fallback.
 */
inline std::vector<CornerDepths> flowingCornerDepths(
    const std::vector<VertexDepthReconstruct::CellSplit> &cells,
    const std::vector<Surface> &surfaces, const std::vector<double> &x,
    const std::vector<double> &y, const std::vector<double> &z, const SmoothTopology &topology,
    const std::vector<float> &depths, const std::vector<float> &flux, VisibilityPolicy policy = {},
    NeighborhoodFit::Stats *stats = nullptr, NeighborhoodFit::Workspace *workspace = nullptr,
    std::vector<CornerDepths> reuse = {})
{
    if (x.size() != z.size() || y.size() != z.size())
        return {};
    return NeighborhoodFit::reconstruct(cells, surfaces, x, y, z, topology, depths, flux, policy,
                                        stats, workspace, std::move(reuse));
}

// Continuous, bounded display projection of VFR stages on each connected
// wet vertex fan. Include signed values at HIGH/DRY corners too: dropping
// those and later extrapolating a maximum stage creates the uphill artifact.
// Every mean uses depths relative to the same vertex datum, in double.
// Raw cell storage is unchanged; the smooth display is not a per-cell
// volume-conserving remap. Dry cells never receive a surface.
/**
 * @brief Project visible physical stages to shared wet-fan corner traces for display.
 * @param[in] cells Ordered valid mesh splits, matching topology slots.
 * @param[in] surfaces Physical states; must cover cells when reconstructed has full cell length, otherwise the common prefix is processed.
 * @param[in] z Finite vertex beds indexed by cells, in policy vertical units (normally metres).
 * @param[in] topology Endpoint connections built for this exact mesh; indices must be in4*cells range.
 * @param[out] out Replaced with one four-slot signed-depth array per cell. Hidden/unusable corners are NaN; output must not alias reconstructed.
 * @param[in] policy Visibility threshold applied before joining and to supplied reconstructed traces.
 * @param[in] reconstructed Optional full-length neighborhood-fit output. When length equals cells.size(), copies it without averaging then hides invisible cells; otherwise builds area-weighted shared traces across overlapping positive wet-edge intervals.
 * @note Changes display output only; the averaging fallback is bounded but not per-cell volume-conserving. Negative high/dry-corner values are retained for shorelines. Caller owns all arrays and serializes output access.
 */
inline void smoothCornerDepths(const std::vector<VertexDepthReconstruct::CellSplit> &cells,
                               const std::vector<Surface> &surfaces, const std::vector<double> &z,
                               const SmoothTopology &topology, std::vector<CornerDepths> &out,
                               VisibilityPolicy policy = {},
                               const std::vector<CornerDepths> &reconstructed = {})
{
    const double nan = std::numeric_limits<double>::quiet_NaN();
    if (reconstructed.size() == cells.size()) {
        // The neighborhood fit already owns the shared corner values and
        // closes its total volume. Averaging it again could change that volume
        // and join a crest which the hydraulic neighborhood deliberately split.
        out = reconstructed;
        for (size_t c = 0; c < cells.size(); ++c)
            if (!visible(displayState(cells[c], surfaces[c], z, policy), policy) ||
                !visible(displayState(out[c], cells[c].vertexCount(), policy), policy))
                out[c] = {nan, nan, nan, nan};
        return;
    }
    out.assign(cells.size(), CornerDepths{nan, nan, nan, nan});
    std::vector<int> parent(cells.size() * 4, -1);
    std::vector<double> weight(cells.size() * 4, 0.0);
    auto value = [&](int corner) -> double & { return out[size_t(corner / 4)][corner % 4]; };
    for (int c = 0; c < int(cells.size()) && c < int(surfaces.size()); ++c) {
        const auto &cell = cells[size_t(c)];
        const double area = cell.area[0] + (cell.nSub == 2 ? cell.area[1] : 0.0);
        if (!visible(displayState(cell, surfaces[size_t(c)], z, policy), policy) || !(area > 0) ||
            !std::isfinite(area))
            continue;
        for (int k = 0; k < cell.vertexCount(); ++k) {
            const int v = cell.v[k], corner = 4 * c + k;
            if (v < 0 || size_t(v) >= z.size())
                continue;
            value(corner) = surfaces[size_t(c)].signedDepth(z[size_t(v)]);
            if (!std::isfinite(value(corner)))
                continue;
            parent[size_t(corner)] = corner;
            weight[size_t(corner)] = area / cell.vertexCount();
        }
    }
    auto root = [&](int i) {
        while (parent[size_t(i)] != i) {
            parent[size_t(i)] = parent[size_t(parent[size_t(i)])];
            i = parent[size_t(i)];
        }
        return i;
    };
    auto join = [&](int a, int b) {
        a = root(a);
        b = root(b);
        if (a == b)
            return;
        if (b < a)
            std::swap(a, b);
        const double total = weight[size_t(a)] + weight[size_t(b)];
        value(a) += (value(b) - value(a)) * (weight[size_t(b)] / total);
        weight[size_t(a)] = total;
        parent[size_t(b)] = a;
    };
    for (const auto &e : topology.edges) {
        if (parent[size_t(e.a0)] < 0 || parent[size_t(e.a1)] < 0 || parent[size_t(e.b0)] < 0 ||
            parent[size_t(e.b1)] < 0)
            continue;
        // Use the unprojected traces: earlier unions must not change whether
        // a later edge connects.
        auto raw = [&](int corner) {
            const size_t c = size_t(corner / 4);
            const int k = corner % 4;
            return surfaces[c].signedDepth(z[size_t(cells[c].v[k])]);
        };
        double lo = 0, hi = 1;
        auto intersect = [&](double a, double b) {
            if (!(std::max(a, b) > 0))
                return false;
            if (a < 0)
                lo = std::max(lo, a / (a - b));
            if (b < 0)
                hi = std::min(hi, a / (a - b));
            return hi > lo;
        };
        if (!intersect(raw(e.a0), raw(e.b0)) || !intersect(raw(e.a1), raw(e.b1)))
            continue;
        join(e.a0, e.a1);
        join(e.b0, e.b1);
    }
    for (int i = 0; i < int(parent.size()); ++i)
        if (parent[size_t(i)] >= 0)
            value(i) = value(root(i));
}

/**
 * @brief Evaluate unrestricted triangle barycentric weights.
 * @param[in] p Sample point in one finite common XY coordinate system.
 * @param[in] a,b,c Triangle corners in that system.
 * @param[out] w Corner weights a/b/c; unchanged for zero/nonfinite determinant, potentially assigned nonfinite values before a later false result.
 * @return True for finite weights and nonzero finite determinant, including points outside the triangle. No containment tolerance or normalization beyond w0=1-w1-w2.
 */
inline bool barycentric(const QPointF &p, const QPointF &a, const QPointF &b, const QPointF &c,
                        std::array<double, 3> &w)
{
    const QPointF ab = b - a, ac = c - a, ap = p - a;
    const double det = ab.x() * ac.y() - ab.y() * ac.x();
    if (!std::isfinite(det) || det == 0.0)
        return false;
    w[1] = (ap.x() * ac.y() - ap.y() * ac.x()) / det;
    w[2] = (ab.x() * ap.y() - ab.y() * ap.x()) / det;
    w[0] = 1.0 - w[1] - w[2];
    return std::isfinite(w[0]) && std::isfinite(w[1]) && std::isfinite(w[2]);
}

// Intersect a line segment with a triangle, including paths on an edge.
// Ownership of overlapping intervals is resolved by the caller's stable IDs.
/**
 * @brief Clip a finite line segment to a triangle in barycentric coordinates.
 * @param[in] p,q Segment endpoints in the same finite XY system as triangle corners.
 * @param[in] a,b,c Nondegenerate triangle positions; orientation is arbitrary.
 * @param[out] lo,hi Parametric segment interval in[0,1] on success. Early degeneracy leaves these untouched; later clipping failure can leave partial bounds.
 * @return True only for positive interval length; isolated point touches are false, paths along an edge can succeed. Snaps only relative barycentric roundoff at32 double epsilons. Overlapping-triangle ownership remains the caller's responsibility.
 */
inline bool triangleInterval(const QPointF &p, const QPointF &q, const QPointF &a, const QPointF &b,
                             const QPointF &c, double &lo, double &hi)
{
    std::array<double, 3> wp, wq;
    if (!barycentric(p, a, b, c, wp) || !barycentric(q, a, b, c, wq))
        return false;
    lo = 0;
    hi = 1;
    for (int k = 0; k < 3; ++k) {
        // Snap only barycentric roundoff on an edge, not a finite-width halo.
        const double tol = 32 * std::numeric_limits<double>::epsilon() *
                           std::max({1.0, std::abs(wp[k]), std::abs(wq[k])});
        if (std::abs(wp[k]) < tol)
            wp[k] = 0;
        if (std::abs(wq[k]) < tol)
            wq[k] = 0;
        if (wp[k] < 0 && wq[k] < 0)
            return false;
        if (wp[k] < 0)
            lo = std::max(lo, wp[k] / (wp[k] - wq[k]));
        if (wq[k] < 0)
            hi = std::min(hi, wp[k] / (wp[k] - wq[k]));
    }
    return hi > lo;
}

// Exact wet interval; zero-depth and unknown segments do not paint water.
/**
 * @brief Restrict a linear signed-depth trace to values above a cutoff.
 * @param[in] q0,q1 Finite endpoint signed depths in one vertical unit.
 * @param[in] cutoff Finite threshold in that unit, normally zero.
 * @param[out] lo,hi Always initialized to[0,1], then clipped where needed; false results do not promise empty bounds.
 * @return True for positive interval length when at least one endpoint is strictly above cutoff. Nonfinite endpoint/all-below-or-equal traces return false; exact threshold endpoints may bound a positive interval.
 */
inline bool wetInterval(double q0, double q1, double cutoff, double &lo, double &hi)
{
    lo = 0;
    hi = 1;
    if (!std::isfinite(q0) || !std::isfinite(q1) || std::max(q0, q1) <= cutoff)
        return false;
    if (q0 < cutoff)
        lo = (q0 - cutoff) / (q0 - q1);
    if (q1 < cutoff)
        hi = (q0 - cutoff) / (q0 - q1);
    return hi > lo;
}

struct ClipVertex {
    QPointF point;
    double depth;
};
struct WetPolygon {
    std::array<ClipVertex, 4> vertices;
    int size = 0;
};
/**
 * @brief Clip a linear triangle depth field against a scalar cutoff.
 * @param[in] a,b,c Finite triangle XY corners in one common coordinate system.
 * @param[in] qa,qb,qc Signed corner depths in one vertical unit, paired with a/b/c.
 * @param[in] cutoff Finite clipping threshold in that unit.
 * @return Owned zero-to-four-vertex polygon in original winding order, with interpolated cutoff depths at crossings. Nonfinite depths or maximum<=cutoff returns empty. Geometry degeneracy/area and coordinate finiteness are not independently checked.
 */
inline WetPolygon clipTriangle(const QPointF &a, const QPointF &b, const QPointF &c, double qa,
                               double qb, double qc, double cutoff)
{
    WetPolygon out;
    if (!std::isfinite(qa) || !std::isfinite(qb) || !std::isfinite(qc) ||
        std::max({qa, qb, qc}) <= cutoff)
        return out;
    const ClipVertex v[3] = {{a, qa}, {b, qb}, {c, qc}};
    for (int i = 0; i < 3; ++i) {
        const auto &p = v[i];
        const auto &q = v[(i + 1) % 3];
        if (p.depth >= cutoff)
            out.vertices[out.size++] = p;
        if ((p.depth < cutoff && q.depth > cutoff) || (p.depth > cutoff && q.depth < cutoff)) {
            const double t = (p.depth - cutoff) / (p.depth - q.depth);
            out.vertices[out.size++] = {p.point + t * (q.point - p.point), cutoff};
        }
    }
    return out;
}

} // namespace CellWaterGeometry
#endif
