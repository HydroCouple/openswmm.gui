/**
 * @file      meshresult.h
 * @brief     value types for the 2D mesh generator.
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 *
 * Slice AU — value types for the 2D mesh generator. Pure value types
 * with no Qt-meta or meshing-kernel includes — safe to depend on from
 * dialog / writer / layer code without pulling in the triangulation kernel.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef OPENSWMMVIS_MESH_MESHRESULT_H
#define OPENSWMMVIS_MESH_MESHRESULT_H

#include <QHash>
#include <QPointF>
#include <QString>
#include <QVector>

#include <limits>

#include "meshinfil.h" // InfilRow / InfilDefaultRow / InfilOptions (GG0 §3.2a)

namespace mesh
{

/*! \brief A vertex in the generated mesh. */
struct MeshVertex {
    QPointF xy;               ///< Map units (project CRS).
    double z = 0.0;           ///< Sampled DTM elevation (set by DTMSampler; 0 until then).
    int marker = 0;           ///< Input/output point marker (carries our tag id).
    QString tag;              ///< Descriptive label ([2D_VERTICES] TAG column); empty if none.
    QString coupledNode;      ///< Coupled SWMM node id ([2D_VERTEX_NODE_MAP]); empty if uncoupled.
    double couplingCd = 0.65; ///< [2D_VERTEX_NODE_MAP] CD column (engine default).
    double couplingArea =
        1.0; ///< [2D_VERTEX_NODE_MAP] AREA column, mesh length units² (engine default).
    /// False when the row does not author CD / AREA: it is written without
    /// the token and the engine applies [2D_OPTIONS] COUPLING_CD / the
    /// node's 2D coupling structure (an authored AREA forces a CD column).
    bool couplingCdSet = true;
    bool couplingAreaSet = true;
};

/*! \brief A cell in the mesh — a triangle or (since the engine's mixed
 *  tri-quad meshes, workplans/TRI_QUAD_MESHING_PLAN_2026-09-06.md) a convex
 *  quadrilateral — indices into \ref MeshResult::vertices.
 *
 *  The historical name is kept: every consumer that reads `v0/v1/v2` still
 *  compiles, and a quad simply carries a fourth vertex `v3 >= 0` (cyclic
 *  order, either orientation). Use \ref vertexCount / \ref vertex and the
 *  helpers in mesh/meshcellgeom.h (edgeEndpoints, edgeSlot, cellGeom)
 *  instead of hard-coding 3.
 *
 *  Engine contract (plans/2D_TRI_QUAD_MESH_PLAN_2026-09-06.md): cells are
 *  written triangles first, then quads (`[2D_TRIANGLES]` then `[2D_QUADS]`);
 *  local edge k has endpoints `v[(k+1)%nv]`, `v[(k+2)%nv]` — for a triangle
 *  the old "edge opposite vertex" rule. \ref MeshResult::triangles keeps the
 *  engine's cell order.
 */
struct MeshTriangle {
    int v0 = 0, v1 = 0, v2 = 0;
    int v3 = -1; ///< Fourth vertex of a quadrilateral; -1 = triangle.
    QString tag; ///< Region tag (e.g. subcatchment ID); empty if untagged.
    /// Manning's roughness ([2D_TRIANGLES] MANNINGS_N). NaN = unset, so the
    /// writer falls back to the caller's default (mesh generation uses a
    /// uniform value); a loaded mesh carries the per-triangle value.
    double mannings = std::numeric_limits<double>::quiet_NaN();
    /// Initial water depth in m ([2D_TRIANGLES] INIT_DEPTH, written between
    /// MANNINGS_N and TAG). NaN = unset (column absent on load) — treated as
    /// the engine default 0 (dry) and omitted on write unless any triangle
    /// carries a depth or a tag (TAG position must stay unambiguous).
    double initDepth = std::numeric_limits<double>::quiet_NaN();

    /**
     * @brief Classify the cell solely by whether a fourth vertex identifier is present.
     * @return True when v3 is nonnegative, false otherwise; no bounds, distinctness or geometry
     * validation. Constant read-only work, with no allocation or topology mutation.
     */
    bool isQuad() const noexcept { return v3 >= 0; }
    /**
     * @brief Report the logical corner count selected by the fourth-vertex sentinel.
     * @return Four when v3>=0, otherwise three, irrespective of the validity of stored
     * identifiers. Constant read-only work; does not inspect the vertex array.
     */
    int vertexCount() const noexcept { return v3 >= 0 ? 4 : 3; }
    /**
     * @brief Fetch one stored corner identifier without a vertex-array lookup.
     * @param[in] k Storage slot 0..3; ordinary callers use [0,vertexCount()).
     * @return Stored v0/v1/v2/v3 for slots 0/1/2/3 respectively, or -1 for all other slots. For
     * a triangle, slot 3 returns its actual negative sentinel, which need not equal -1. No
     * identifier validation, allocation or mutation.
     *
     * @details Stored vertex slot k (0..3); -1 outside that storage range.
     */
    int vertex(int k) const noexcept
    {
        switch (k) {
        case 0:
            return v0;
        case 1:
            return v1;
        case 2:
            return v2;
        case 3:
            return v3;
        default:
            return -1;
        }
    }
    /**
     * @brief Replace a stored corner identifier, leaving every other cell attribute unchanged.
     * @param[in] k Storage slot 0..3; values outside this range are ignored.
     * @param[in] v Identifier to store, not checked against mesh bounds or other corners.
     * Setting slot 3 nonnegative classifies the cell as a quad; a negative value classifies it
     * as a triangle.
     * @note Mutates this cell only; does not repair orientation, adjacency or associated mesh
     * arrays. No allocation, ownership retention or error reporting.
     */
    void setVertex(int k, int v) noexcept
    {
        switch (k) {
        case 0:
            v0 = v;
            break;
        case 1:
            v1 = v;
            break;
        case 2:
            v2 = v;
            break;
        case 3:
            v3 = v;
            break;
        default:
            break;
        }
    }
    /**
     * @brief Check whether an identifier equals a stored logical corner.
     * @param[in] v Identifier to compare, including negative values; no mesh bounds validation.
     * @return True for equality with v0, v1 or v2, or with a nonnegative v3; false otherwise.
     * Negative values in the first three slots can match, while an absent fourth corner cannot.
     * Constant read-only work, with no geometry lookup or allocation.
     */
    bool hasVertex(int v) const noexcept
    {
        return v0 == v || v1 == v || v2 == v || (v3 >= 0 && v3 == v);
    }
};

/*! \brief Alias: a "triangle" row is any cell (triangle or quad). */
using MeshCell = MeshTriangle;

/*! \brief A boundary edge with its source-segment marker preserved. */
struct MeshEdge {
    int v0 = 0, v1 = 0;
    int marker = 0; ///< Source segment marker; non-zero if assigned by caller.
    QString tag;    ///< Resolved tag (e.g. conduit ID, "domain", "outflow").
};

/*! \brief A 1D node coupled to a mesh cell (centroid coupling).
 *
 * MESH_DECOUPLED_1D2D_REMAP_PLAN Part C — a triangle may carry several of
 * these (multiple nodes landing in one cell), so they are stored as rows
 * on the MeshResult rather than as fields on MeshTriangle. Written as
 * repeated [2D_TRIANGLE_NODE_MAP] rows; the engine builds one coupling
 * point per row (orifice exchange law).
 */
struct CellCoupling {
    int tri = -1;        ///< Triangle index into MeshResult::triangles.
    QString nodeId;      ///< SWMM node id.
    double cd = 0.65;    ///< Discharge coefficient (engine default).
    double area = 2.0;   ///< Effective exchange area, mesh length units² (mapper default).
    bool cdSet = true;   ///< False: row written without CD (see MeshVertex::couplingCdSet).
    bool areaSet = true; ///< False: row written without AREA.
};

/*! \brief Result of \ref MeshGenerator::generate. */
struct MeshResult {
    QVector<MeshVertex> vertices;
    QVector<MeshTriangle> triangles; ///< Cells: triangles first, then quads (engine order).
    QVector<MeshEdge> boundaryEdges;
    QVector<CellCoupling> cellCouplings; ///< Node→cell couplings (Part C).

    /*! Per-cell infiltration (INTEGRATED2D_GW_GUI_PLAN §3.2a, phase GG0).
     *
     *  Follows the CellCoupling precedent above — sparse/inherited rows live
     *  on MeshResult rather than bloating the per-triangle POD. Resolution is
     *  `infilOverrides > tag row in infilDefaults > '*' row > none`
     *  (engine D-I3); use mesh::resolveInfil() rather than reading these
     *  directly, so the inherited-vs-overridden distinction is preserved.
     *
     *  Parameters are in PROJECT units — the same numbers a user types into
     *  [INFILTRATION]. The GUI performs no unit conversion.
     *
     *  Written as [2D_INFILTRATION_DEFAULTS] / [2D_INFILTRATION]; these
     *  sections follow the mesh (external .2dm when one is in use, else
     *  inline). They must NOT be appended to [2D_TRIANGLES], whose columns
     *  are positional.
     */
    // Engine-resolved authoring preview; absent means unavailable, not zero.
    QVector<double> surfaceMeshWeatherPercent, surfaceSourcePerviousPercent;
    QVector<int> surfaceOwnershipStatus;
    QVector<int> infiltrationOwner, infiltrationConflict;
    QStringList infiltrationSource;
    QVector<InfilDefaultRow> infilDefaults; ///< Tag rows; '*' = mesh-wide fallback.
    QHash<int, InfilRow> infilOverrides;    ///< Sparse, keyed by triangle index.
    InfilOptions infilOptions;              ///< [2D_INFILTRATION_OPTIONS].

    bool ok = false;
    QString errorMsg;

    /**
     * @brief Count cells whose fourth identifier classifies them as quadrilaterals.
     * @return Number of entries with v3>=0, zero for an empty array. The count must fit int.
     * Linear read-only traversal of triangles, independent of storage ordering, ok or geometry
     * validity; no allocation, mesh mutation or cached-count update.
     *
     * @details Number of quadrilateral cells (O(n)).
     */
    int quadCount() const noexcept
    {
        int n = 0;
        for (const MeshTriangle &t : triangles)
            n += t.isQuad() ? 1 : 0;
        return n;
    }
    /**
     * @brief Determine whether any stored cell has a nonnegative fourth identifier.
     * @return True at the first quad-classified cell, false for an empty array or all triangles.
     * Linear worst-case read-only traversal with early exit; no geometry validation or cached
     * state update.
     */
    bool hasQuads() const noexcept
    {
        for (const MeshTriangle &t : triangles)
            if (t.isQuad())
                return true;
        return false;
    }
};

} // namespace mesh

#endif // OPENSWMMVIS_MESH_MESHRESULT_H
