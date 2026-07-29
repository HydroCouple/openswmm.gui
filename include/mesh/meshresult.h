/*!
 * \file   meshresult.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * Slice AU — value types for the 2D mesh generator. Pure value types
 * with no Qt-meta, no Triangle includes — safe to depend on from
 * dialog / writer / layer code without dragging Triangle in.
 */
#ifndef OPENSWMMVIS_MESH_MESHRESULT_H
#define OPENSWMMVIS_MESH_MESHRESULT_H

#include <QPointF>
#include <QString>
#include <QVector>

#include <limits>

namespace mesh {

/*! \brief A vertex in the generated mesh. */
struct MeshVertex
{
    QPointF xy;            ///< Map units (project CRS).
    double  z       = 0.0; ///< Sampled DTM elevation (set by DTMSampler; 0 until then).
    int     marker  = 0;   ///< Triangle's input/output point marker (carries our tag id).
    QString tag;           ///< Descriptive label ([2D_VERTICES] TAG column); empty if none.
    QString coupledNode;   ///< Coupled SWMM node id ([2D_VERTEX_NODE_MAP]); empty if uncoupled.
    double  couplingCd   = 0.65; ///< [2D_VERTEX_NODE_MAP] CD column (engine default).
    double  couplingArea = 1.0;  ///< [2D_VERTEX_NODE_MAP] AREA column, m² (engine default).
};

/*! \brief A triangle in the generated mesh, indices into \ref MeshResult::vertices. */
struct MeshTriangle
{
    int     v0 = 0, v1 = 0, v2 = 0;
    QString tag;          ///< Region tag (e.g. subcatchment ID); empty if untagged.
    /// Manning's roughness ([2D_TRIANGLES] MANNINGS_N). NaN = unset, so the
    /// writer falls back to the caller's default (mesh generation uses a
    /// uniform value); a loaded mesh carries the per-triangle value.
    double  mannings = std::numeric_limits<double>::quiet_NaN();
};

/*! \brief A boundary edge with its source-segment marker preserved. */
struct MeshEdge
{
    int v0 = 0, v1 = 0;
    int marker = 0;       ///< Source segment marker; non-zero if assigned by caller.
    QString tag;          ///< Resolved tag (e.g. conduit ID, "domain", "outflow").
};

/*! \brief A 1D node coupled to a mesh cell (centroid coupling).
 *
 * MESH_DECOUPLED_1D2D_REMAP_PLAN Part C — a triangle may carry several of
 * these (multiple nodes landing in one cell), so they are stored as rows
 * on the MeshResult rather than as fields on MeshTriangle. Written as
 * repeated [2D_TRIANGLE_NODE_MAP] rows; the engine builds one coupling
 * point per row (orifice exchange law).
 */
struct CellCoupling
{
    int     tri = -1;      ///< Triangle index into MeshResult::triangles.
    QString nodeId;        ///< SWMM node id.
    double  cd   = 0.65;   ///< Discharge coefficient (engine default).
    double  area = 2.0;    ///< Effective exchange area, m² (mapper default).
};

/*! \brief Result of \ref MeshGenerator::generate. */
struct MeshResult
{
    QVector<MeshVertex>   vertices;
    QVector<MeshTriangle> triangles;
    QVector<MeshEdge>     boundaryEdges;
    QVector<CellCoupling> cellCouplings; ///< Node→cell couplings (Part C).
    bool    ok = false;
    QString errorMsg;
};

} // namespace mesh

#endif // OPENSWMMVIS_MESH_MESHRESULT_H
