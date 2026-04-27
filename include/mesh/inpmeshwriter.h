/*!
 * \file   inpmeshwriter.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license MIT
 *
 * Slice AU — emit `[2D_VERTICES]` / `[2D_TRIANGLES]` /
 * `[2D_VERTEX_NODE_MAP]` / `[2D_TRIANGLE_NODE_MAP]` sections from a
 * MeshResult + a CouplingMap, and patch them into a SWMM `.inp` file
 * (replacing existing sections in place). Format follows
 * `openswmm.engine/docs/2dModelStrategy.md` §1.4–1.7.
 */
#ifndef OPENSWMMVIS_MESH_INPMESHWRITER_H
#define OPENSWMMVIS_MESH_INPMESHWRITER_H

#include "meshresult.h"

#include <QHash>
#include <QString>

namespace mesh {

/*! \brief 1D↔2D coupling map produced by the meshing dialog.
 *
 *  The dialog walks the SWMM model + the meshing inputs and builds this
 *  map: which mesh vertex / triangle is associated with which SWMM node
 *  ID. This is the only cross-file glue the writer needs — the writer
 *  itself is data-agnostic.
 */
struct CouplingMap
{
    /*! Vertex index (in MeshResult::vertices) → SWMM node ID. */
    QHash<int, QString> vertexToNode;
    /*! Triangle index (in MeshResult::triangles) → SWMM node ID. */
    QHash<int, QString> triangleToNode;
    /*! Per-triangle Manning's n. Triangles missing from the map use the
     *  default passed into the writer. */
    QHash<int, double>  triangleMannings;
};

/*! \brief Output strategy for the four 2D mesh sections.
 *
 *  External (default) keeps the main `.inp` clean and lets multiple SWMM
 *  models share a single mesh — the user-stated preference (2026-04-26).
 *  Inline is the fallback for callers who specifically want everything in
 *  one file.
 */
enum class MeshOutputMode {
    External,   ///< Write `.2dm` next to the .inp; reference via `[2D_MESH_FILE]`.
    Inline      ///< Inline the four sections directly in the `.inp`.
};

class InpMeshWriter
{
public:
    /*! \brief Render the four 2D sections as a single text block.
     *
     *  Order: `[2D_VERTICES]`, `[2D_TRIANGLES]`, `[2D_VERTEX_NODE_MAP]`,
     *  `[2D_TRIANGLE_NODE_MAP]`. Each section starts with a `;;`-prefixed
     *  header comment. Sections with no content (e.g. empty
     *  vertex_node_map) are omitted entirely.
     *
     *  \param mesh             output of MeshGenerator::generate()
     *  \param coupling         vertex/triangle → SWMM node map
     *  \param defaultMannings  used for triangles missing from
     *                          coupling.triangleMannings (default 0.035 ≈
     *                          natural channel / grass).
     */
    [[nodiscard]] static QString buildSectionText(const MeshResult &mesh,
                                                  const CouplingMap &coupling,
                                                  double defaultMannings = 0.035);

    /*! \brief Default mode: write the mesh into a sibling `.2dm` file and
     *         patch the `.inp` with a `[2D_MESH_FILE]` reference.
     *
     *  \param inpPath           SWMM input file to patch.
     *  \param meshFilePath      Output `.2dm` file. If empty, defaults to
     *                           `<inpDir>/<inpBasename>.2dm`.
     *  \param mesh, coupling, defaultMannings  as in buildSectionText.
     *  \param errorOut          set on failure.
     *
     *  Both writes are atomic via `QSaveFile`. The `.inp`'s existing
     *  `[2D_*]` sections are stripped (if present) so a stale inline mesh
     *  doesn't shadow the external file. Path stored in `[2D_MESH_FILE]`
     *  is **relative to the .inp directory** when the mesh sits beside
     *  the .inp; absolute otherwise.
     */
    [[nodiscard]] static bool writeExternal(const QString &inpPath,
                                             const QString &meshFilePath,
                                             const MeshResult &mesh,
                                             const CouplingMap &coupling,
                                             double defaultMannings = 0.035,
                                             QString *errorOut = nullptr);

    /*! \brief Replace (or append) all four 2D sections in the .inp at
     *         \p inpPath. Existing 2D sections are stripped first; any
     *         existing `[2D_MESH_FILE]` block is also removed so the
     *         engine reads the freshly-inlined sections instead.
     */
    [[nodiscard]] static bool writeInline(const QString &inpPath,
                                           const MeshResult &mesh,
                                           const CouplingMap &coupling,
                                           double defaultMannings = 0.035,
                                           QString *errorOut = nullptr);

    /*! \brief Convenience dispatch on \ref MeshOutputMode. */
    [[nodiscard]] static bool write(MeshOutputMode mode,
                                     const QString &inpPath,
                                     const QString &meshFilePath,
                                     const MeshResult &mesh,
                                     const CouplingMap &coupling,
                                     double defaultMannings = 0.035,
                                     QString *errorOut = nullptr);
};

} // namespace mesh

#endif // OPENSWMMVIS_MESH_INPMESHWRITER_H
