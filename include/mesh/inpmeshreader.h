/*!
 * \file   inpmeshreader.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * Read the `[2D_*]` mesh sections — `[2D_VERTICES]`, `[2D_TRIANGLES]`,
 * `[2D_VERTEX_NODE_MAP]`, `[2D_TRIANGLE_NODE_MAP]`, the per-edge
 * `[2D_BOUNDARY_CONDITIONS]` / `[2D_EDGE_CONVEYANCE]` pair and the GG0a
 * per-cell `[2D_INFILTRATION_OPTIONS]` / `[2D_INFILTRATION_DEFAULTS]` /
 * `[2D_INFILTRATION]` family — from a SWMM `.inp`, resolving the
 * `[2D_MESH_FILE]` indirection when present. Mirrors the format produced by
 * \ref mesh::InpMeshWriter.
 */
#ifndef OPENSWMMVIS_MESH_INPMESHREADER_H
#define OPENSWMMVIS_MESH_INPMESHREADER_H

#include "meshresult.h"
#include "meshedgebc.h"

#include <QString>
#include <QVector>

namespace mesh {

/*! \brief Outcome of \ref InpMeshReader::read. */
struct InpMeshReadResult
{
    MeshResult mesh;                  ///< Parsed mesh; \c mesh.ok mirrors \c hasMesh. XY are in SI metres (the engine contract); caller divides back to the project CRS for display.
    QString    sourcePath;            ///< File the mesh was parsed from (the .inp itself for inline meshes, the resolved .2dm path otherwise).
    bool       isExternal = false;    ///< True when the mesh was loaded via `[2D_MESH_FILE] FILE <path>`.
    bool       hasMesh    = false;    ///< True only when both `[2D_VERTICES]` and `[2D_TRIANGLES]` were found and parsed into a non-empty mesh.
    QString    errorMsg;              ///< Non-empty only on read failure (missing referenced .2dm, malformed sections). An .inp with no 2D sections is not an error.
    QString    warning;               ///< Non-fatal warning (e.g. legacy file missing the `;; UNITS:` header — caller may surface this).

    /*! \brief Verbatim value of the `;; UNITS:` header line (empty when
     *  absent). XY in `mesh.vertices` are left unchanged — the engine's
     *  consumer side branches on this value (SI files skip the
     *  FLOW_UNITS-based ft→m scaling). */
    QString    unitsHeader;

    /*! \brief Value of the optional `;; SOURCE_CRS: <tag>` header line. */
    QString    sourceCrsTag;

    /*! \brief Slice §V.VD.1 — per-edge boundary conditions parsed from a
     *  `[2D_BOUNDARY_CONDITIONS]` section. Flat-indexed `tri * 3 + eLocal`;
     *  resized to `n_triangles * 3` with Wall defaults when the section is
     *  missing. Empty when there is no mesh. */
    QVector<MeshEdgeBC> edgeBCs;
};

class InpMeshReader
{
public:
    /*! \brief Look up the 2D mesh associated with \p inpPath.
     *
     *  Precedence matches the engine: when `[2D_MESH_FILE]` is present and the
     *  referenced file exists, the mesh is read from that file; otherwise the
     *  reader falls back to inline `[2D_*]` sections inside the .inp itself.
     *
     *  Missing mesh data is not an error — \c hasMesh stays \c false and
     *  \c errorMsg is empty so callers can treat the absence as "no 2D layer
     *  to add". \c errorMsg is only populated when a malformed section or a
     *  broken external reference prevents parsing what was clearly intended
     *  to be a mesh.
     */
    [[nodiscard]] static InpMeshReadResult read(const QString &inpPath);
};

} // namespace mesh

#endif // OPENSWMMVIS_MESH_INPMESHREADER_H
