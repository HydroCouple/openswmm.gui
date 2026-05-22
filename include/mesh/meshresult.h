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

namespace mesh {

/*! \brief A vertex in the generated mesh. */
struct MeshVertex
{
    QPointF xy;            ///< Map units (project CRS).
    double  z       = 0.0; ///< Sampled DTM elevation (set by DTMSampler; 0 until then).
    int     marker  = 0;   ///< Triangle's input/output point marker (carries our tag id).
    QString tag;           ///< Resolved string (e.g. SWMM node ID); empty if untagged.
};

/*! \brief A triangle in the generated mesh, indices into \ref MeshResult::vertices. */
struct MeshTriangle
{
    int     v0 = 0, v1 = 0, v2 = 0;
    QString tag;          ///< Region tag (e.g. subcatchment ID); empty if untagged.
};

/*! \brief A boundary edge with its source-segment marker preserved. */
struct MeshEdge
{
    int v0 = 0, v1 = 0;
    int marker = 0;       ///< Source segment marker; non-zero if assigned by caller.
    QString tag;          ///< Resolved tag (e.g. conduit ID, "domain", "outflow").
};

/*! \brief Result of \ref MeshGenerator::generate. */
struct MeshResult
{
    QVector<MeshVertex>   vertices;
    QVector<MeshTriangle> triangles;
    QVector<MeshEdge>     boundaryEdges;
    bool    ok = false;
    QString errorMsg;
};

} // namespace mesh

#endif // OPENSWMMVIS_MESH_MESHRESULT_H
