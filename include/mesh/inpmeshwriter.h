/*!
 * \file   inpmeshwriter.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
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
#include "meshedgebc.h"

#include <QHash>
#include <QString>
#include <QVector>

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
    /*! \brief Unit & provenance metadata written as `;;` header comments.
     *
     *  The writer does NOT perform any unit conversion — values are written
     *  in whatever unit the caller's mesh already carries (the project CRS
     *  linear unit, by convention aligned with the SWMM FLOW_UNITS).
     *  The metadata is purely descriptive so the reader / engine can
     *  branch on it.
     *
     *  Engine contract (today): `[2D_VERTICES]` XY is in the project's
     *  length unit (feet for US flow units, metres otherwise). Engine
     *  multiplies by 0.3048 in `SurfaceRouter2D::initialize` when the
     *  project is US.  When the file declares `;; UNITS: SI (m)` and the
     *  engine has been updated to honour it, the engine skips that
     *  multiplication.
     */
    struct UnitInfo
    {
        QString linearUnitName;     ///< Goes into `;; UNITS:` (e.g. "metre", "US survey foot", "SI (m)").
        QString sourceCrsTag;       ///< Goes into `;; SOURCE_CRS:` (e.g. "EPSG:2249").

        // User-declared (out-of-line) default ctor: makes UnitInfo a
        // non-aggregate so the `const UnitInfo & = {}` default arguments
        // below invoke this constructor instead of aggregate-initialising
        // with the in-class member initializer — which clang rejects as
        // "needed within definition of enclosing class outside of member
        // functions" when the type is nested in InpMeshWriter.
        UnitInfo();
    };

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
     *  \param units            optional `;; UNITS:` / `;; SOURCE_CRS:`
     *                          header metadata (purely descriptive).
     */
    [[nodiscard]] static QString buildSectionText(const MeshResult &mesh,
                                                  const CouplingMap &coupling,
                                                  double defaultMannings = 0.035,
                                                  const UnitInfo &units = {});

    /*! \brief Slice §V.VD.1 — render the `[2D_BOUNDARY_CONDITIONS]`
     *  section for the per-edge BC vector. Returns an empty string when
     *  every edge is Wall (avoids polluting the .inp with a section
     *  that round-trips to default). Format:
     *
     *      [2D_BOUNDARY_CONDITIONS]
     *      ;; TRI  EDGE  TYPE             PARAM_1        PARAM_2  GROUP
     *         12   0     NORMAL_FLOW      0.002          *        *
     *         12   1     SPECIFIED_STAGE  95.4           *        *
     *         45   2     TS_STAGE         DownstreamTS   *        Outlet
     *
     *  TYPE is one of WALL / NORMAL_FLOW / SPECIFIED_STAGE / TS_STAGE /
     *  SPECIFIED_FLOW / TS_FLOW / RATING_CURVE (see mesh::MeshBCTypes).
     *  PARAM_1 is the type's primary parameter (slope / head / TS name /
     *  flow / TS name / curve name). PARAM_2 is reserved for future
     *  extensions; today always "*". GROUP is the optional named group
     *  ("*" = none). */
    [[nodiscard]] static QString buildBCSectionText(const QVector<MeshEdgeBC> &bcs);

    /*! \brief Engine §11A — render the `[2D_EDGE_CONVEYANCE]` section.
     *
     *  Walks \p bcs (flat-indexed `tri * 3 + edge`, parallel to \p mesh)
     *  and emits one row per edge whose conveyance differs from the
     *  default 1.0. Interior edges occupy two slots that the GUI keeps in
     *  sync (see SWMM2DMeshLayer::applyMeshEdgeConveyance), so the writer
     *  canonicalises on the first encountered vertex-pair and silently
     *  drops the second half — interior edges appear exactly once.
     *  Returns an empty string when every edge is at default.
     *
     *  Format (matches the engine parser in `SectionHandlers2D.cpp`):
     *
     *      [2D_EDGE_CONVEYANCE]
     *      ;; FROM_VERTEX  TO_VERTEX  CONVEYANCE
     *         12           37         0.5
     *         12           45         0
     *
     *  CONVEYANCE is a dimensionless multiplier in [0, 1]. 1.0 (default)
     *  is unrestricted; 0.0 is a closed (impermeable) edge. */
    [[nodiscard]] static QString buildConveyanceSectionText(
        const MeshResult &mesh, const QVector<MeshEdgeBC> &bcs);

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
                                             QString *errorOut = nullptr,
                                             const UnitInfo &units = {});

    /*! \brief §V.VD.1 overload — additionally writes the
     *  `[2D_BOUNDARY_CONDITIONS]` section. Empty / all-Wall BC vector
     *  is equivalent to the non-BC overload (section is omitted). */
    [[nodiscard]] static bool writeExternal(const QString &inpPath,
                                             const QString &meshFilePath,
                                             const MeshResult &mesh,
                                             const CouplingMap &coupling,
                                             const QVector<MeshEdgeBC> &bcs,
                                             double defaultMannings,
                                             QString *errorOut = nullptr,
                                             const UnitInfo &units = {});

    /*! \brief Replace (or append) all four 2D sections in the .inp at
     *         \p inpPath. Existing 2D sections are stripped first; any
     *         existing `[2D_MESH_FILE]` block is also removed so the
     *         engine reads the freshly-inlined sections instead.
     */
    [[nodiscard]] static bool writeInline(const QString &inpPath,
                                           const MeshResult &mesh,
                                           const CouplingMap &coupling,
                                           double defaultMannings = 0.035,
                                           QString *errorOut = nullptr,
                                           const UnitInfo &units = {});

    /*! \brief §V.VD.1 overload — additionally writes the
     *  `[2D_BOUNDARY_CONDITIONS]` section inline. */
    [[nodiscard]] static bool writeInline(const QString &inpPath,
                                           const MeshResult &mesh,
                                           const CouplingMap &coupling,
                                           const QVector<MeshEdgeBC> &bcs,
                                           double defaultMannings,
                                           QString *errorOut = nullptr,
                                           const UnitInfo &units = {});

    /*! \brief Retarget the `.inp`'s `[2D_MESH_FILE]` reference at an
     *         already-existing `.2dm` without writing any mesh geometry.
     *
     *  Strips any inline `[2D_*]` mesh-data sections and any prior
     *  `[2D_MESH_FILE]` block, then injects a fresh `[2D_MESH_FILE] FILE
     *  <path>` pointing at \p meshFilePath. The stored path is relative to
     *  the `.inp` directory when the mesh is a sibling, absolute otherwise
     *  — identical to \ref writeExternal. Atomic via `QSaveFile`.
     *
     *  \param inpPath      SWMM input file to patch.
     *  \param meshFilePath Existing `.2dm` to reference (must exist on disk).
     *  \param errorOut     Set on failure.
     *  \returns true on success. */
    [[nodiscard]] static bool writeMeshFileRef(const QString &inpPath,
                                               const QString &meshFilePath,
                                               QString *errorOut = nullptr);

    /*! \brief Replace the `[2D_BOUNDARY_CONDITIONS]` and
     *         `[2D_EDGE_CONVEYANCE]` sections of \p filePath (a `.inp` or
     *         external `.2dm`) with sections built from \p bcs, leaving
     *         every other section untouched.
     *
     *  Used by the post-save external-mesh restore: the pre-write `.2dm`
     *  snapshot predates the engine's write of the current BC/conveyance
     *  edits, so restoring it would silently discard them — this re-emits
     *  the layer's per-edge state into the restored file. All-default \p bcs
     *  strips the sections without appending (reset-to-Wall round-trips).
     *  Atomic via `QSaveFile`.
     *
     *  \param filePath  File whose BC/conveyance sections are replaced.
     *  \param mesh      Mesh the flat-indexed \p bcs parallels.
     *  \param bcs       Per-edge BC state (`tri * 3 + edge`).
     *  \param errorOut  Set on failure.
     *  \returns true on success. */
    [[nodiscard]] static bool patchBCSections(const QString &filePath,
                                              const MeshResult &mesh,
                                              const QVector<MeshEdgeBC> &bcs,
                                              QString *errorOut = nullptr);

    /*! \brief Replace the `[2D_VERTICES]`, `[2D_TRIANGLES]`,
     *         `[2D_VERTEX_NODE_MAP]` and `[2D_TRIANGLE_NODE_MAP]` sections of
     *         \p filePath with sections rebuilt from the layer's editable
     *         mesh state, leaving every other section untouched.
     *
     *  The BC-patch's sibling for mesh *attributes*: the post-save
     *  external-mesh restore rolls the sidecar back to its pre-write
     *  snapshot, which predates the engine's write of the current vertex
     *  elevation / tag / coupling and triangle Manning / tag edits — without
     *  this re-emit those edits silently vanish from the saved model (and
     *  the next run reads the old elevations). The layer is authoritative
     *  for exactly the fields pushMeshEditsToEngine() pushes; a triangle
     *  whose Manning is unset (NaN) keeps the file's existing token so a
     *  generation-time default survives the rewrite.
     *
     *  Fails without touching the file when the section row counts don't
     *  match \p mesh — rows map to mesh entries by position, so a mismatch
     *  means the file holds a different mesh. Atomic via `QSaveFile`.
     *
     *  \param filePath  External mesh file (or `.inp`) to patch.
     *  \param mesh      The layer's current mesh state.
     *  \param errorOut  Set on failure.
     *  \param defaultMannings  MANNINGS_N materialized for a row that must
     *                   carry an INIT_DEPTH or TAG but has no Manning's value
     *                   on either side (columns are positional, so the later
     *                   ones cannot be written without it).
     *  \returns true on success. */
    [[nodiscard]] static bool patchAttributeSections(const QString &filePath,
                                                     const MeshResult &mesh,
                                                     QString *errorOut = nullptr,
                                                     double defaultMannings = 0.035);

    /*! \brief Strip any `[2D_MESH_FILE]` reference from the `.inp` so the
     *         engine falls back to the inline `[2D_*]` mesh sections.
     *
     *  The inverse of \ref writeMeshFileRef: removes only the
     *  `[2D_MESH_FILE]` block and leaves every other section (including the
     *  inline mesh data) untouched. Used when the user selects the inline
     *  mesh as the active 2D configuration. No-op-safe when the section is
     *  absent. Atomic via `QSaveFile`.
     *
     *  \param inpPath  SWMM input file to patch.
     *  \param errorOut Set on failure.
     *  \returns true on success. */
    [[nodiscard]] static bool clearMeshFileRef(const QString &inpPath,
                                               QString *errorOut = nullptr);

    /*! \brief Convenience dispatch on \ref MeshOutputMode. */
    [[nodiscard]] static bool write(MeshOutputMode mode,
                                     const QString &inpPath,
                                     const QString &meshFilePath,
                                     const MeshResult &mesh,
                                     const CouplingMap &coupling,
                                     double defaultMannings = 0.035,
                                     QString *errorOut = nullptr,
                                     const UnitInfo &units = {});
};

} // namespace mesh

#endif // OPENSWMMVIS_MESH_INPMESHWRITER_H
