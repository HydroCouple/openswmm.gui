/*!
 * \file   marchingtriangles.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * Slice BJ.2-lite — marching-triangles contour line extraction.
 *
 * Per triangle and per contour level, the algorithm classifies each vertex
 * as below/above the level (8 cases, 6 of which produce a single line
 * segment crossing two edges). Linear-interpolation on the two edges with
 * sign changes gives the iso-line segment endpoints.
 *
 * The triangle iterator is templated so we don't tie the algorithm to any
 * specific mesh type: callers pass a forward range of triangles plus a
 * functor that extracts (p0, p1, p2, v0, v1, v2) per triangle. This lets
 * SWMM2DMeshLayer feed its existing SceneTri cache directly, and lets the
 * follow-up result-attribute slice swap the scalar source without touching
 * the algorithm.
 *
 * Output is a flat std::vector<IsoLineSegment> in scene space, suitable
 * for QSG line-list upload (every two consecutive points = one segment).
 *
 * Out-of-scope for the lite cut (deferred to follow-ups):
 *  - filled iso-bands (polygonisation between adjacent levels)
 *  - viewport / LOD culling (passed-in triangle range is iterated whole)
 *  - per-level pen / colour overrides (caller assigns)
 *  - contour labels (waits for BI.2 LabelExpression)
 *  - raster (marching squares — separate algorithm)
 */
#ifndef OPENSWMMVIS_CONTOUR_MARCHINGTRIANGLES_H
#define OPENSWMMVIS_CONTOUR_MARCHINGTRIANGLES_H

#include <QColor>
#include <QPointF>

#include <algorithm>
#include <vector>

namespace OpenSWMM::Contour {

/*!
 * \brief Sample a 5-stop Viridis-inspired colour ramp at `t` in [0, 1].
 *
 * Used by BJ.2-filled as the default isoband palette until Slice BB lands
 * `ColorRamp` (at which point this helper becomes a fallback for the
 * legacy-no-ramp case). Stops follow matplotlib's Viridis closely enough
 * for an engineering audience to read elevation rank correctly without
 * the colour-vision-deficiency pitfalls of jet/rainbow.
 */
inline QColor viridisAt(double t)
{
    struct Stop { double t; quint8 r, g, b; };
    static const Stop stops[] = {
        {0.00, 0x44, 0x01, 0x54},
        {0.25, 0x3b, 0x52, 0x8b},
        {0.50, 0x21, 0x91, 0x8c},
        {0.75, 0x5e, 0xc9, 0x62},
        {1.00, 0xfd, 0xe7, 0x25},
    };
    constexpr int N = int(sizeof(stops) / sizeof(*stops));
    t = std::clamp(t, 0.0, 1.0);
    int i = 0;
    while (i < N - 2 && stops[i + 1].t <= t) ++i;
    const Stop &lo = stops[i], &hi = stops[i + 1];
    const double f = (hi.t > lo.t) ? (t - lo.t) / (hi.t - lo.t) : 0.0;
    const int r = int(lo.r + f * (hi.r - lo.r) + 0.5);
    const int g = int(lo.g + f * (hi.g - lo.g) + 0.5);
    const int b = int(lo.b + f * (hi.b - lo.b) + 0.5);
    return QColor(std::clamp(r, 0, 255),
                  std::clamp(g, 0, 255),
                  std::clamp(b, 0, 255));
}

/*! \brief A single contour iso-line segment in scene space. */
struct IsoLineSegment
{
    QPointF a;
    QPointF b;
    double  level;   ///< the iso-level this segment was generated for
};

/*!
 * \brief Generate iso-line segments for every (triangle, level) pair where
 *        the level crosses the triangle's value range.
 *
 * \tparam TriRange Forward-iterable range of opaque "triangle handles". The
 *                  caller's Extract functor turns each handle into vertex
 *                  positions + per-vertex scalar values.
 * \tparam Extract  Callable with signature
 *                      void(const TriHandle &h,
 *                           QPointF &p0, QPointF &p1, QPointF &p2,
 *                           double  &v0, double  &v1, double  &v2)
 *
 * \param tris    forward range of triangles (anything with begin()/end())
 * \param levels  iso-levels (sorted ascending is convenient for the caller
 *                but not required by the algorithm)
 * \param extract per-triangle vertex/value extractor
 *
 * Complexity O(|tris| * |levels|) with an early-out per triangle when the
 * level lies outside [min(v0,v1,v2), max(v0,v1,v2)]. Typical fill rate
 * on real meshes is well under 30% so the constant factor stays small.
 */
template <typename TriRange, typename Extract>
std::vector<IsoLineSegment>
marchingTriangles(const TriRange         &tris,
                  const std::vector<double> &levels,
                  Extract                  extract)
{
    std::vector<IsoLineSegment> out;
    if (levels.empty()) return out;

    out.reserve(/* heuristic */ levels.size() * 32);

    QPointF p0, p1, p2;
    double  v0, v1, v2;

    for (const auto &t : tris) {
        extract(t, p0, p1, p2, v0, v1, v2);

        // Triangle-level early-out: clamp range so far-out levels skip
        // immediately. Computed once per triangle.
        const double vMin = std::min({v0, v1, v2});
        const double vMax = std::max({v0, v1, v2});
        if (!(vMax > vMin)) continue;   // degenerate (flat) triangle

        for (const double level : levels) {
            if (level < vMin || level > vMax) continue;

            // Classify vertices: bit i = 1 means vertex i is below `level`.
            // Cases 0 (000) and 7 (111) produce no segment.
            const int below = (v0 < level ? 1 : 0)
                            | (v1 < level ? 2 : 0)
                            | (v2 < level ? 4 : 0);
            if (below == 0 || below == 7) continue;

            // Edges are (0,1), (1,2), (0,2). An edge is "crossed" iff its
            // two endpoints have opposite below-flags. Exactly two edges
            // are crossed in every below ∈ {1,2,3,4,5,6} case.
            QPointF endpoints[2];
            int     k = 0;

            auto cross = [&](const QPointF &pa, double va,
                             const QPointF &pb, double vb) {
                const double denom = vb - va;
                if (denom == 0.0) {
                    endpoints[k++] = pa;
                    return;
                }
                const double t = (level - va) / denom;
                endpoints[k++] = QPointF(pa.x() + t * (pb.x() - pa.x()),
                                         pa.y() + t * (pb.y() - pa.y()));
            };

            const bool b0 = (below & 1);
            const bool b1 = (below & 2);
            const bool b2 = (below & 4);

            if (b0 != b1) cross(p0, v0, p1, v1);
            if (b1 != b2) cross(p1, v1, p2, v2);
            if (b2 != b0) cross(p2, v2, p0, v0);

            // k must be exactly 2 for cases 1..6. If a vertex sits exactly
            // on the level both incident edges register a "crossing" at
            // that vertex — emit the degenerate segment regardless so the
            // line topology stays closed; downstream renderers handle
            // zero-length segments gracefully (single pixel or nothing).
            if (k == 2) {
                out.push_back({endpoints[0], endpoints[1], level});
            }
        }
    }
    return out;
}

/*!
 * \brief Convenience helper that generates N evenly-spaced contour levels
 *        between vMin and vMax (exclusive of the endpoints).
 *
 * Returns an empty vector if N < 1 or the range is degenerate. Endpoints
 * are excluded because a level exactly equal to vMin or vMax produces
 * either no segments (above/below flip never happens) or coincident-edge
 * artifacts that aren't useful visually.
 */
inline std::vector<double>
evenlySpacedLevels(double vMin, double vMax, int count)
{
    std::vector<double> out;
    if (count < 1 || !(vMax > vMin)) return out;
    out.reserve(size_t(count));
    const double step = (vMax - vMin) / double(count + 1);
    for (int i = 1; i <= count; ++i)
        out.push_back(vMin + step * double(i));
    return out;
}

/*!
 * \brief Convenience helper that returns `levelCount` evenly-spaced break
 *        levels INCLUSIVE of both endpoints (vMin and vMax).
 *
 * Used by the filled-isoband path: N bands need N+1 break levels including
 * the data-range endpoints, otherwise the boundary triangles get clipped
 * away. Returns an empty vector if levelCount < 2 or the range is
 * degenerate.
 */
inline std::vector<double>
evenlySpacedLevelsInclusive(double vMin, double vMax, int levelCount)
{
    std::vector<double> out;
    if (levelCount < 2 || !(vMax > vMin)) return out;
    out.reserve(size_t(levelCount));
    const double step = (vMax - vMin) / double(levelCount - 1);
    for (int i = 0; i < levelCount; ++i)
        out.push_back(vMin + step * double(i));
    out.back() = vMax;   // guard against floating-point drift on the last sample
    return out;
}

/*! \brief One filled-isoband polygon in scene space. Convex (Sutherland-
 *  Hodgman of a triangle yields a convex polygon with ≤ 5 vertices). */
struct IsoBandPolygon
{
    std::vector<QPointF> verts;     ///< convex polygon vertices in order
    double               bandLo;    ///< lower break level
    double               bandHi;    ///< upper break level
    int                  bandIndex; ///< 0-based — which band in the level
                                    ///< sequence (drives palette sampling)
};

namespace detail {

/*!
 * \brief Sutherland-Hodgman clip of a convex polygon against the half-plane
 *        defined by `value >= L` (when keepAbove) or `value <= L` (else).
 *
 * Input polygon vertices/values are read from \p inPts / \p inVals. Output
 * is written to \p outPts / \p outVals (cleared on entry). Vertex values
 * are tracked alongside vertices so chained clips against multiple levels
 * stay numerically consistent. New vertices introduced by edge intersections
 * receive value = L exactly.
 */
inline void clipHalfplane(const std::vector<QPointF> &inPts,
                          const std::vector<double>  &inVals,
                          double                      L,
                          bool                        keepAbove,
                          std::vector<QPointF>       &outPts,
                          std::vector<double>        &outVals)
{
    outPts.clear();
    outVals.clear();
    const size_t n = inPts.size();
    if (n == 0) return;

    auto isInside = [&](double v) {
        return keepAbove ? (v >= L) : (v <= L);
    };

    for (size_t i = 0; i < n; ++i) {
        const size_t  j  = (i + 1) % n;
        const QPointF &pi = inPts[i],  &pj = inPts[j];
        const double   vi = inVals[i],  vj = inVals[j];
        const bool    inI = isInside(vi);
        const bool    inJ = isInside(vj);

        if (inI) {
            outPts.push_back(pi);
            outVals.push_back(vi);
        }
        if (inI != inJ) {
            const double denom = vj - vi;
            if (denom == 0.0) {
                outPts.push_back(pi);
                outVals.push_back(L);
            } else {
                const double t = (L - vi) / denom;
                outPts.push_back(QPointF(pi.x() + t * (pj.x() - pi.x()),
                                          pi.y() + t * (pj.y() - pi.y())));
                outVals.push_back(L);
            }
        }
    }
}

} // namespace detail

/*!
 * \brief Generate filled iso-band polygons for every (triangle, band) pair.
 *
 * \p levels is interpreted as N break levels defining N-1 bands; consecutive
 * pairs `[levels[k], levels[k+1]]` define band k. For best results pass an
 * inclusive level vector (see \ref evenlySpacedLevelsInclusive) so boundary
 * triangles aren't clipped away.
 *
 * Implementation: each triangle is clipped twice per band via two
 * Sutherland-Hodgman passes (one for `value >= L1`, one for `value <= L2`).
 * The output convex polygon has up to 5 vertices and is suitable for fan
 * triangulation by the caller.
 *
 * Complexity O(|tris| * |bands|) with an early-out per (triangle, band) when
 * the band lies entirely outside the triangle's value range.
 */
template <typename TriRange, typename Extract>
std::vector<IsoBandPolygon>
marchingTrianglesIsobands(const TriRange         &tris,
                          const std::vector<double> &levels,
                          Extract                  extract)
{
    std::vector<IsoBandPolygon> out;
    if (levels.size() < 2) return out;
    out.reserve(/* heuristic */ levels.size() * 32);

    QPointF p0, p1, p2;
    double  v0, v1, v2;

    // Scratch buffers reused across iterations
    std::vector<QPointF> a, b;
    std::vector<double>  aV, bV;
    a.reserve(8);  b.reserve(8);
    aV.reserve(8); bV.reserve(8);

    for (const auto &t : tris) {
        extract(t, p0, p1, p2, v0, v1, v2);

        const double vMin = std::min({v0, v1, v2});
        const double vMax = std::max({v0, v1, v2});
        if (!(vMax > vMin)) continue;   // degenerate triangle

        for (size_t k = 0; k + 1 < levels.size(); ++k) {
            const double L1 = levels[k];
            const double L2 = levels[k + 1];
            if (L2 < vMin || L1 > vMax) continue;
            if (!(L2 > L1)) continue;   // degenerate band

            // Initialise polygon = triangle
            a = {p0, p1, p2};
            aV = {v0, v1, v2};

            // Clip 1: keep value >= L1
            detail::clipHalfplane(a, aV, L1, /*keepAbove=*/true,  b, bV);
            // Clip 2: keep value <= L2
            detail::clipHalfplane(b, bV, L2, /*keepAbove=*/false, a, aV);

            if (a.size() >= 3) {
                IsoBandPolygon bp;
                bp.verts     = a;
                bp.bandLo    = L1;
                bp.bandHi    = L2;
                bp.bandIndex = int(k);
                out.push_back(std::move(bp));
            }
        }
    }
    return out;
}

} // namespace OpenSWMM::Contour

#endif // OPENSWMMVIS_CONTOUR_MARCHINGTRIANGLES_H
