/**
 * @file      inpmeshreader.h
 * @brief     Read the `[2D_*]` mesh sections — `[2D_VERTICES]`, `[2D_TRIANGLES]`, `[2D_QUADS]`
 *            (mixed meshes: quads appended after the triangles, cell index = n_triangles + j; a
 *            triangle row after a quad row is an error), `[2D_VERTEX_NODE_MAP]`,
 *            `[2D_TRIANGLE_NODE_MAP]`, the per-edge `[2D_BOUNDARY_CONDITIONS]` /
 *            `[2D_EDGE_CONVEYANCE]` pair and the GG0a per-cell `[2D_INFILTRATION_OPTIONS]` /
 *            `[2D_INFILTRATION_DEFAULTS]` / `[2D_INFILTRATION]` family — from a SWMM `.inp`,
 *            resolving the `[2D_MESH_FILE]` indirection when present.
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   Apache-2.0
 *
 * Read the `[2D_*]` mesh sections — `[2D_VERTICES]`, `[2D_TRIANGLES]`,
 * `[2D_QUADS]` (mixed meshes: quads appended after the triangles, cell
 * index = n_triangles + j; a triangle row after a quad row is an error),
 * `[2D_VERTEX_NODE_MAP]`, `[2D_TRIANGLE_NODE_MAP]`, the per-edge
 * `[2D_BOUNDARY_CONDITIONS]` / `[2D_EDGE_CONVEYANCE]` pair and the GG0a
 * per-cell `[2D_INFILTRATION_OPTIONS]` / `[2D_INFILTRATION_DEFAULTS]` /
 * `[2D_INFILTRATION]` family — from a SWMM `.inp`, resolving the
 * `[2D_MESH_FILE]` indirection when present. Mirrors the format produced by
 * @ref mesh::InpMeshWriter.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef OPENSWMMVIS_MESH_INPMESHREADER_H
#define OPENSWMMVIS_MESH_INPMESHREADER_H

#include "meshresult.h"
#include "meshedgebc.h"

#include <QString>
#include <QVector>

namespace mesh
{

/*! \brief Outcome of \ref InpMeshReader::read. */
struct InpMeshReadResult {
    MeshResult
        mesh; ///< Parsed mesh; \c mesh.ok mirrors \c hasMesh. XY/Z remain verbatim authored units; this reader performs no SI conversion.
    QString
        sourcePath; ///< File the mesh was parsed from (the .inp itself for inline meshes, the resolved .2dm path otherwise).
    bool isExternal = false; ///< True when the mesh was loaded via `[2D_MESH_FILE] FILE path`.
    bool hasMesh =
        false; ///< True only when `[2D_VERTICES]` and `[2D_TRIANGLES]` (or `[2D_QUADS]`) were found and parsed into a non-empty mesh.
    QString
        errorMsg; ///< Non-empty only on read failure (missing referenced .2dm, malformed sections). An .inp with no 2D sections is not an error.
    QString
        warning; ///< Non-fatal warning (e.g. legacy file missing the `;; UNITS:` header — caller may surface this).

    /*! \brief Verbatim value of the `;; UNITS:` header line (empty when
     *  absent). XY in `mesh.vertices` are left unchanged — the engine's
     *  consumer side branches on this value (SI files skip the
     *  FLOW_UNITS-based ft→m scaling). */
    QString unitsHeader;

    /*! \brief Value of the optional `;; SOURCE_CRS: tag` header line. */
    QString sourceCrsTag;

    /*! \brief Slice §V.VD.1 — per-edge boundary conditions parsed from a
     *  `[2D_BOUNDARY_CONDITIONS]` section. Flat-indexed
     *  `mesh::edgeSlot(cell, eLocal)` (stride `mesh::kEdgeStride`; slot 3 of
     *  a triangle is unused padding); sized `mesh::edgeSlotCount(n_cells)`
     *  with Wall defaults when the section is missing. A row whose EDGE
     *  exceeds its cell's edge count (2 for a triangle, 3 for a quad) is
     *  dropped. Empty when there is no mesh. */
    QVector<MeshEdgeBC> edgeBCs;
};

/**
 * @brief Recognize the retained metric-mesh header vocabulary without converting geometry.
 * @param[in] unitsHeader Borrowed header value, trimmed and compared case-insensitively,
 * unchanged.
 * @return True only for SI (m), m, metre, metres, meter or meters; false for
 * empty/unknown/imperial text. Internal whitespace/spelling is not normalized. This flag alone
 * does not prove engine scaling: callers also consider FLOW_UNITS and lifecycle. No engine
 * query, IO, coordinate conversion or state mutation.
 *
 * @details True when \p unitsHeader (the verbatim `;; UNITS:` value) declares a metric mesh,
 * i.e. one the engine will NOT rescale.
 *
 * Mirrors the engine's `prescan2DUnitsHeader` keyword set exactly — "SI (m)", "m", "metre(s)",
 * "meter(s)", case-insensitive. Anything else, including an absent or explicitly imperial
 * header, is not metric. Callers that need to know whether the engine converted the mesh to SI
 * (issue #155) must apply this together with FLOW_UNITS: the engine scales only when FLOW_UNITS
 * is US-customary AND this returns false.
 */
[[nodiscard]] bool unitsHeaderIsSI(const QString &unitsHeader);

class InpMeshReader
{
public:
    /**
     * @brief Read inline/native-sidecar mesh data and resolve edge/infiltration overlays into
     * owned values.
     * @param[in] inpPath Borrowed SWMM input pathname, unchanged; QFile reads whole UTF-8 text
     * read only. Relative FILE references resolve against its directory; native [2D_*] sidecar
     * grammar is expected, not SMS cards even when both extensions are .2dm.
     * @return Owned result with hasMesh/mesh.ok true only for seen nonempty vertices and
     * triangle/quad cells. Malformed recognized rows or unreadable input/reference set errorMsg;
     * absent/incomplete/empty mesh sections can yield no mesh without error. Parses inline
     * first, so inline syntax errors can fail even when an external reference exists. Any
     * nonempty external reference is authoritative: unreadable file errors, readable file
     * without complete geometry returns no mesh, with no inline fallback/nested recursion.
     * Geometry xy/z, initial depths, boundary scalars and infiltration parameters remain
     * verbatim authored units; this reader performs no SI conversion, reprojection, full
     * finite/topology/index validation or engine reference resolution. External nonempty
     * units/source-CRS headers override inline values. External nonempty infiltration
     * default/override containers and positive infiltration step win; empty/ nonpositive fields
     * inherit inline data. Edges start Wall/default conveyance, external then inline BC rows
     * overlay, followed by external then inline endpoint-conveyance rows. Missing-units warning
     * is currently emitted only on the inline success path. Full failure geometry is not
     * exposed, metadata can already be populated; returned values own their storage. Allocates
     * whole text/sections/maps/results, no source writes, callbacks, engine mutations or
     * retained caller references.
     *
     * @details Look up the 2D mesh associated with \p inpPath.
     *
     * A nonempty external mesh reference selects that sidecar exclusively; unreadable references
     * are errors and readable references without complete geometry return no mesh. Inline mesh
     * data is selected only when there is no nonempty reference.
     *
     * Missing mesh data is not an error — \c hasMesh stays \c false and
     * \c errorMsg is empty so callers can treat the absence as "no 2D layer to add". \c errorMsg
     * is only populated when a malformed section or a broken external reference prevents parsing
     * what was clearly intended to be a mesh.
     */
    [[nodiscard]] static InpMeshReadResult read(const QString &inpPath);
};

} // namespace mesh

#endif // OPENSWMMVIS_MESH_INPMESHREADER_H
