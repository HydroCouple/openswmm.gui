/**
 * @file      meshcellgeom.h
 * @brief     Mixed triangle/quad cell helpers (workplans/TRI_QUAD_MESHING_PLAN_2026-09-06.md,
 *            phase G1).
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   Apache-2.0
 *
 * Mixed triangle/quad cell helpers (workplans/TRI_QUAD_MESHING_PLAN_2026-09-06.md,
 * phase G1). ONE place decides how a cell's edges are numbered, how its
 * flat edge slots are addressed, and how a quad is split into the two
 * sub-triangles the engine's storage model uses — rendering, hit-testing,
 * profiles, contours and statistics all draw the same picture the solver
 * stores.
 *
 * Engine contract (plans/2D_TRI_QUAD_MESH_PLAN_2026-09-06.md):
 *   - local edge k of a cell has endpoints v[(k+1)%nv], v[(k+2)%nv]
 *     (for a triangle: the historical "edge k is opposite vertex k");
 *   - per-edge SoAs are padded to kEdgeStride = 4 slots per cell
 *     (`cell * 4 + k`; slot 3 of a triangle is unused);
 *   - a quad's storage (VFR) model is two planar sub-triangles split along
 *     the diagonal chosen from the elevation ordering of its vertices
 *     (Begnudelli & Sanders 2007, Cases 1–3): the same split is used here.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef OPENSWMMVIS_MESH_MESHCELLGEOM_H
#define OPENSWMMVIS_MESH_MESHCELLGEOM_H

#include "mesh/meshresult.h"

#include <QPointF>
#include <QVector>

#include <array>
#include <cmath>

namespace mesh
{

/*! Edge slots per cell in every flat per-edge array (== engine kMaxCellVerts). */
constexpr int kEdgeStride = 4;

/**
 * @brief Compute the flat stride-four slot for one cell-local edge.
 * @param[in] cell Nonnegative zero-based cell index; cell*4+e must fit int.
 * @param[in] e Local edge[0,3] for storage addressing; triangle slot 3 is padding, not a real
 * edge.
 * @return cell*kEdgeStride+e, unchecked arithmetic, no array access or ownership/state mutation.
 *
 * @details Flat edge slot of local edge \p e of cell \p cell.
 */
constexpr int edgeSlot(int cell, int e) noexcept
{
    return cell * kEdgeStride + e;
}
/**
 * @brief Decode the owning cell index from a stride-four flat edge slot.
 * @param[in] slot Nonnegative flat slot in the relevant array range, unchecked.
 * @return slot/4 with integer division, no array lookup or validation; noexcept read-only
 * arithmetic.
 *
 * @details Cell / local edge of a flat slot.
 */
constexpr int slotCell(int slot) noexcept
{
    return slot / kEdgeStride;
}
/**
 * @brief Decode local edge position from a stride-four flat slot.
 * @param[in] slot Nonnegative valid flat slot, unchecked.
 * @return slot%4 in[0,3]; triangle padding remains 3, not converted to an invalid edge sentinel.
 * No geometry access or state mutation.
 */
constexpr int slotLocal(int slot) noexcept
{
    return slot % kEdgeStride;
}
/**
 * @brief Calculate padded per-edge storage length for a cell count.
 * @param[in] nCells Nonnegative cell count with 4*nCells representable in int.
 * @return Four slots per cell including unused triangle padding, no allocation or bounds
 * checking.
 *
 * @details Number of flat edge slots for \p nCells cells.
 */
constexpr int edgeSlotCount(int nCells) noexcept
{
    return nCells * kEdgeStride;
}

/**
 * @brief Resolve the ordered vertex IDs of one cell-local opposite-edge convention.
 * @param[in] t Valid cyclic triangle/quad, preserved, no vertex-coordinate lookup.
 * @param[in] k Local edge in[0,t.vertexCount()), caller must not pass triangle padding3.
 * @param[out] a Borrowed reference overwritten with vertex((k+1)%nv).
 * @param[out] b Borrowed reference overwritten with vertex((k+2)%nv); caller should use distinct
 * outputs.
 * @note Retains stored orientation and index values without validation, noexcept constant work.
 * No ownership transfer, allocation or topology mutation.
 *
 * @details Endpoints (a, b) of local edge \p k of cell \p t: v[(k+1)%nv], v[(k+2)%nv].
 */
inline void edgeEndpoints(const MeshTriangle &t, int k, int &a, int &b) noexcept
{
    const int nv = t.vertexCount();
    a = t.vertex((k + 1) % nv);
    b = t.vertex((k + 2) % nv);
}

/**
 * @brief Reconstruct planar specific discharge by least-squares fitting outward edge-normal
 * discharge.
 * @param[in] cell Nonnegative cell index with representable cell*4 addressing.
 * @param[in] nv Real edge count 3 or 4, excludes triangle padding.
 * @param[in] flux Required nonnull stable flat array of at least 4*(cell+1) floats,
 * outward-positive m3/s.
 * @param[in] length Required nonnull equally sized edge lengths in metres; <=1e-12 skips an
 * edge.
 * @param[in] nx Required nonnull equally sized outward unit-normal x components, dimensionless.
 * @param[in] ny Required nonnull equally sized outward unit-normal y components, dimensionless.
 * @param[out] qx Borrowed x discharge output in m2/s, assigned only after determinant passes.
 * @param[out] qy Borrowed y discharge output in m2/s, assigned only after determinant passes;
 * use distinct outputs.
 * @return False when abs(normal-matrix determinant)<1e-12 or nonfinite, outputs untouched; true
 * after solve, without a final finite-output check. Nonfinite flux/length ratios skipped, finite
 * ratios clamped +/-10 m2/s; caller must provide meaningful normals. No array ownership
 * retained, allocation or mutation, safe concurrent reads with stable inputs and disjoint
 * outputs, noexcept.
 *
 * @details RT0 least-squares specific discharge (m²/s) of one cell from its outward-positive
 * normal edge fluxes (m³/s).
 *
 * The engine's own reconstruction (SurfaceFluxCalculator::computeFaceVelocity): with \c nv
 * outward unit normals n_e and normal speeds q_e = flux_e/length_e, solve the nv×2 least-squares
 * system N·q ≈ b via the normal equations (NᵀN) q = Nᵀ b. |q_e| is clamped to 10 against
 * wet/dry-front spikes and a non-finite q_e is skipped so it cannot poison the 2×2 solve. Divide
 * by the cell depth for velocity. The per-edge arrays are the flat
 * \ref edgeSlot layout (\p cell's slots are read, nothing else).
 */
inline bool rt0CellDischarge(int cell, int nv, const float *flux, const float *length,
                             const float *nx, const float *ny, double &qx, double &qy) noexcept
{
    constexpr double kQMax = 10.0;
    double a00 = 0.0, a01 = 0.0, a11 = 0.0; // NᵀN entries
    double b0 = 0.0, b1 = 0.0;              // Nᵀ b entries
    for (int e = 0; e < nv; ++e) {
        const int idx = edgeSlot(cell, e);
        const double len = length[idx];
        if (len <= 1e-12)
            continue;
        double q = double(flux[idx]) / len;
        if (!std::isfinite(q))
            continue;
        if (q > kQMax)
            q = kQMax;
        if (q < -kQMax)
            q = -kQMax;
        const double enx = nx[idx], eny = ny[idx];
        a00 += enx * enx;
        a01 += enx * eny;
        a11 += eny * eny;
        b0 += enx * q;
        b1 += eny * q;
    }
    const double det = a00 * a11 - a01 * a01;
    // NaN-robust degeneracy gate: `abs(NaN) < eps` is false, so the inverted
    // form is required to reject the cell instead of emitting NaN.
    if (!(std::abs(det) >= 1e-12))
        return false;
    const double inv_det = 1.0 / det;
    qx = (a11 * b0 - a01 * b1) * inv_det;
    qy = (-a01 * b0 + a00 * b1) * inv_det;
    return true;
}

/**
 * @brief Reconstruct cell velocity from edge discharge only when a usable wet depth is
 * available.
 * @param[in] cell Nonnegative valid cell index, stride-four address arithmetic must fit int.
 * @param[in] nv Number of real edges 3 or 4, no padding read.
 * @param[in] flux Nonnull stable flat outward volumetric fluxes m3/s, extent>=4*(cell+1).
 * @param[in] length Nonnull stable equal-extent lengths metres.
 * @param[in] nx Nonnull stable equal-extent outward unit-normal x components.
 * @param[in] ny Nonnull stable equal-extent outward unit-normal y components.
 * @param[in] depth Finite positive hydraulic depth in metres; below dryDepth refuses.
 * @param[in] dryDepth Dry threshold in metres; caller supplies nonnegative finite policy value,
 * threshold equality is wet, depth<=0 always refuses.
 * @param[out] vx Borrowed x velocity m/s, set zero before validation; can be nonfinite if final
 * division fails.
 * @param[out] vy Borrowed y velocity m/s, same policy, use distinct outputs.
 * @return True only for successful discharge reconstruction and both finite velocities; false
 * for invalid/ dry depth, degenerate solve or nonfinite divided values. Early refusal leaves
 * zero outputs, final nonfinite refusal does not restore zero. Arrays borrowed unchanged, no
 * allocation or physical state update.
 *
 * @details Velocity in SI m/s from outward volumetric edge flux. Never substitute specific
 * discharge for velocity, or divide by a dry/non-finite depth.
 */
inline bool cellVelocity(int cell, int nv, const float *flux, const float *length, const float *nx,
                         const float *ny, double depth, double dryDepth, double &vx,
                         double &vy) noexcept
{
    vx = vy = 0.;
    if (!std::isfinite(depth) || depth <= 0. || depth < dryDepth)
        return false;
    double qx = 0., qy = 0.;
    if (!rt0CellDischarge(cell, nv, flux, length, nx, ny, qx, qy))
        return false;
    vx = qx / depth;
    vy = qy / depth;
    return std::isfinite(vx) && std::isfinite(vy);
}

/*! \brief Per-cell derived geometry shared by every consumer. */
struct CellGeom {
    double area = 0.0;                       ///< planimetric (map units²)
    QPointF centroid;                        ///< AREA centroid (not the vertex mean for a quad)
    double zMean = 0.0;                      ///< mean of the vertex elevations
    int nSub = 1;                            ///< sub-triangles: 1 (triangle) or 2 (quad)
    std::array<std::array<int, 3>, 2> sub{}; ///< sub-triangle vertex indices
    int diagCase = 0;                        ///< B&S 2007 case (1..3) for a quad; 0 for a triangle
};

/**
 * @brief Compute triangle/quad subtriangles, positive fan area, area centroid and arithmetic
 * mean elevation.
 * @param[in] vertices Stable vertex records with valid t indices, common planar units and
 * consistent z units.
 * @param[in] t Valid nonfolded cyclic cell, preserved; quads use stable elevation ordering to
 * choose storage diagonal.
 * @return Owned geometry with area squared planar units, centroid planar units, zMean original
 * vertical units, one or two subtriangles and quad diagonal case 1..3. Zero quad area falls back
 * to vertex-mean centroid; triangle centroid always arithmetic. No bounds/finite/convexity
 * validation, invalid/folded geometry can produce meaningless positive fan area or nonfinite
 * fields. Constant local work, no heap allocation, ownership retention or mesh mutation;
 * concurrent reads safe when inputs stable.
 *
 * @details Geometry of cell \p t against \p vertices.
 *
 * A quad is split along the Begnudelli & Sanders (2007) diagonal: with the vertices ordered by
 * elevation n1 ≤ n2 ≤ n3 ≤ n4 — Case 1: n1,n4 opposite → sub-triangles (n1,n2,n4),(n1,n3,n4);
 * Case 2: n1,n4 adjacent and n2 adjacent to n1 → (n1,n2,n4),(n2,n3,n4); Case 3 →
 * (n1,n3,n4),(n2,n3,n4). The centroid is the area-weighted centroid of the two sub-triangles (a
 * property of the polygon, independent of the diagonal).
 */
inline CellGeom cellGeom(const QVector<MeshVertex> &vertices, const MeshTriangle &t)
{
    CellGeom g;
    auto triArea = [&](int a, int b, int c) {
        const QPointF &A = vertices[a].xy, &B = vertices[b].xy, &C = vertices[c].xy;
        return 0.5 *
               std::abs((B.x() - A.x()) * (C.y() - A.y()) - (C.x() - A.x()) * (B.y() - A.y()));
    };
    auto triCentroid = [&](int a, int b, int c) {
        const QPointF &A = vertices[a].xy, &B = vertices[b].xy, &C = vertices[c].xy;
        return QPointF((A.x() + B.x() + C.x()) / 3.0, (A.y() + B.y() + C.y()) / 3.0);
    };
    if (!t.isQuad()) {
        g.nSub = 1;
        g.sub[0] = {t.v0, t.v1, t.v2};
        g.area = triArea(t.v0, t.v1, t.v2);
        g.centroid = triCentroid(t.v0, t.v1, t.v2);
        g.zMean = (vertices[t.v0].z + vertices[t.v1].z + vertices[t.v2].z) / 3.0;
        return g;
    }
    const int v[4] = {t.v0, t.v1, t.v2, t.v3};
    // Elevation ordering of the cyclic positions (stable insertion sort).
    int p[4] = {0, 1, 2, 3};
    for (int i = 1; i < 4; ++i) {
        const int key = p[i];
        int j = i - 1;
        while (j >= 0 && vertices[v[p[j]]].z > vertices[v[key]].z) {
            p[j + 1] = p[j];
            --j;
        }
        p[j + 1] = key;
    }
    const int n1 = p[0], n2 = p[1], n3 = p[2], n4 = p[3];
    auto adjacent = [](int a, int b) {
        const int d = (a - b + 4) % 4;
        return d == 1 || d == 3;
    };
    int t1[3], t2[3];
    if (!adjacent(n1, n4)) {
        g.diagCase = 1;
        t1[0] = n1;
        t1[1] = n2;
        t1[2] = n4;
        t2[0] = n1;
        t2[1] = n3;
        t2[2] = n4;
    } else if (adjacent(n2, n1)) {
        g.diagCase = 2;
        t1[0] = n1;
        t1[1] = n2;
        t1[2] = n4;
        t2[0] = n2;
        t2[1] = n3;
        t2[2] = n4;
    } else {
        g.diagCase = 3;
        t1[0] = n1;
        t1[1] = n3;
        t1[2] = n4;
        t2[0] = n2;
        t2[1] = n3;
        t2[2] = n4;
    }
    g.nSub = 2;
    g.sub[0] = {v[t1[0]], v[t1[1]], v[t1[2]]};
    g.sub[1] = {v[t2[0]], v[t2[1]], v[t2[2]]};
    const double a1 = triArea(g.sub[0][0], g.sub[0][1], g.sub[0][2]);
    const double a2 = triArea(g.sub[1][0], g.sub[1][1], g.sub[1][2]);
    g.area = a1 + a2;
    const QPointF c1 = triCentroid(g.sub[0][0], g.sub[0][1], g.sub[0][2]);
    const QPointF c2 = triCentroid(g.sub[1][0], g.sub[1][1], g.sub[1][2]);
    g.centroid =
        (g.area > 0.0)
            ? QPointF((a1 * c1.x() + a2 * c2.x()) / g.area, (a1 * c1.y() + a2 * c2.y()) / g.area)
            : QPointF(
                  (vertices[t.v0].xy + vertices[t.v1].xy + vertices[t.v2].xy + vertices[t.v3].xy) /
                  4.0);
    g.zMean = (vertices[t.v0].z + vertices[t.v1].z + vertices[t.v2].z + vertices[t.v3].z) / 4.0;
    return g;
}

/**
 * @brief Compute oriented polygon area in local coordinates to reduce projected-origin
 * cancellation.
 * @param[in] vertices Stable planar coordinates in common units, every t index valid.
 * @param[in] t Cyclic triangle/quad, preserved.
 * @return Half signed cross-sum in squared coordinate units, positive for CCW, negative for CW
 * and zero for degenerate cells. No finite/overflow/index validation or coordinate scaling,
 * noexcept allocation-free read-only work.
 *
 * @details Signed area of the polygon (positive = counter-clockwise).
 */
inline double cellSignedArea(const QVector<MeshVertex> &vertices, const MeshTriangle &t) noexcept
{
    const int nv = t.vertexCount();
    const QPointF origin = vertices[t.v0].xy;
    double s = 0.0;
    for (int k = 0; k < nv; ++k) {
        // Translate before multiplication: projected map coordinates can
        // otherwise cancel the small area and even reverse its sign.
        const QPointF a = vertices[t.vertex(k)].xy - origin;
        const QPointF b = vertices[t.vertex((k + 1) % nv)].xy - origin;
        s += a.x() * b.y() - b.x() * a.y();
    }
    return 0.5 * s;
}

/**
 * @brief Check strict consistent quad turns, treating every triangle as convex without
 * inspection.
 * @param[in] vertices Stable coordinates with valid quad vertex indices when t is a quad.
 * @param[in] t Triangle or cyclic quad, preserved.
 * @return True unconditionally for triangles; for quads true only when all four computed
 * cross-products have matching nonzero sign, false for collinearity/mixed signs/NaN turn. No
 * separate finite test, so caller supplies finite representable products. No allocation or mesh
 * mutation, noexcept.
 *
 * @details Convexity test for a quad (every consecutive cross product has the same non-zero
 * sign). Triangles are trivially convex.
 */
inline bool cellIsConvex(const QVector<MeshVertex> &vertices, const MeshTriangle &t) noexcept
{
    if (!t.isQuad())
        return true;
    int sign = 0;
    for (int k = 0; k < 4; ++k) {
        const QPointF &p = vertices[t.vertex(k)].xy;
        const QPointF &q = vertices[t.vertex((k + 1) % 4)].xy;
        const QPointF &r = vertices[t.vertex((k + 2) % 4)].xy;
        const double cr = (q.x() - p.x()) * (r.y() - q.y()) - (q.y() - p.y()) * (r.x() - q.x());
        const int s = (cr > 0.0) ? 1 : (cr < 0.0) ? -1 : 0;
        if (s == 0 || (sign != 0 && s != sign))
            return false;
        sign = s;
    }
    return true;
}

/**
 * @brief Test a point against the elevation-selected cell subtriangle fan using barycentric
 * slack.
 * @param[in] vertices Stable valid referenced cell vertices in shared planar units.
 * @param[in] t Valid cyclic triangle/nonfolded quad, preserved; cellGeom defines fan and
 * diagonal.
 * @param[in] p Finite query point in same planar units, preserved.
 * @param[in] eps Dimensionless nonnegative barycentric tolerance, default 0 includes triangle
 * boundaries; negative values shrink allowable barycentric region, not validated.
 * @return True if any nondegenerate subtriangle has all barycentric coordinates>=-eps, false
 * otherwise; abs(denominator)<1e-300 skips degenerate fan element. No Euclidean-distance
 * tolerance, bounds/finite validation, allocation, ownership retention or mesh mutation;
 * noexcept.
 *
 * @details Point-in-cell test on the sub-triangle fan (barycentric per sub-triangle).
 */
inline bool cellContains(const QVector<MeshVertex> &vertices, const MeshTriangle &t,
                         const QPointF &p, double eps = 0.0) noexcept
{
    const CellGeom g = cellGeom(vertices, t);
    for (int s = 0; s < g.nSub; ++s) {
        const QPointF &a = vertices[g.sub[s][0]].xy, &b = vertices[g.sub[s][1]].xy,
                      &c = vertices[g.sub[s][2]].xy;
        const double d = (b.y() - c.y()) * (a.x() - c.x()) + (c.x() - b.x()) * (a.y() - c.y());
        if (std::abs(d) < 1e-300)
            continue;
        const double l1 =
            ((b.y() - c.y()) * (p.x() - c.x()) + (c.x() - b.x()) * (p.y() - c.y())) / d;
        const double l2 =
            ((c.y() - a.y()) * (p.x() - c.x()) + (a.x() - c.x()) * (p.y() - c.y())) / d;
        const double l3 = 1.0 - l1 - l2;
        if (l1 >= -eps && l2 >= -eps && l3 >= -eps)
            return true;
    }
    return false;
}

} // namespace mesh

#endif // OPENSWMMVIS_MESH_MESHCELLGEOM_H
