/**
 * @file      inpmeshwriter.h
 * @brief     emit `[2D_VERTICES]` / `[2D_TRIANGLES]` / `[2D_VERTEX_NODE_MAP]` /
 *            `[2D_TRIANGLE_NODE_MAP]` (plus the GG0a `[2D_INFILTRATION*]` family) sections from a
 *            MeshResult + a CouplingMap, and patch them into a SWMM `.inp` file (replacing
 *            existing sections in place).
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   Apache-2.0
 *
 * Slice AU — emit `[2D_VERTICES]` / `[2D_TRIANGLES]` /
 * `[2D_VERTEX_NODE_MAP]` / `[2D_TRIANGLE_NODE_MAP]` (plus the GG0a
 * `[2D_INFILTRATION*]` family) sections from a MeshResult + a CouplingMap,
 * and patch them into a SWMM `.inp` file (replacing existing sections in
 * place). Format follows `openswmm.engine/docs/2dModelStrategy.md` §1.4–1.7
 * and, for infiltration, the engine's `InpWriter.cpp emit2DInfilSections()`.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef OPENSWMMVIS_MESH_INPMESHWRITER_H
#define OPENSWMMVIS_MESH_INPMESHWRITER_H

#include "meshresult.h"
#include "meshedgebc.h"

#include <QHash>
#include <QString>
#include <QVector>

namespace mesh
{

/*! \brief 1D↔2D coupling map produced by the meshing dialog.
 *
 *  The dialog walks the SWMM model + the meshing inputs and builds this
 *  map: which mesh vertex / triangle is associated with which SWMM node
 *  ID. This is the only cross-file glue the writer needs — the writer
 *  itself is data-agnostic.
 */
struct CouplingMap {
    /*! Vertex index (in MeshResult::vertices) → SWMM node ID. */
    QHash<int, QString> vertexToNode;
    /*! Triangle index (in MeshResult::triangles) → SWMM node ID. */
    QHash<int, QString> triangleToNode;
    /*! Per-triangle Manning's n. Triangles missing from the map use the
     *  default passed into the writer. */
    QHash<int, double> triangleMannings;
};

/*! \brief Output strategy for the four 2D mesh sections.
 *
 *  External (default) keeps the main `.inp` clean and lets multiple SWMM
 *  models share a single mesh — the user-stated preference (2026-04-26).
 *  Inline is the fallback for callers who specifically want everything in
 *  one file.
 */
enum class MeshOutputMode {
    External, ///< Write `.2dm` next to the .inp; reference via `[2D_MESH_FILE]`.
    Inline    ///< Inline the four sections directly in the `.inp`.
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
     *  project is US. When the file declares `;; UNITS: SI (m)`, the engine
     *  recognizes the header and skips that multiplication.
     */
    struct UnitInfo {
        QString
            linearUnitName;   ///< Goes into `;; UNITS:` (e.g. "metre", "US survey foot", "SI (m)").
        QString sourceCrsTag; ///< Goes into `;; SOURCE_CRS:` (e.g. "EPSG:2249").

        // User-declared (out-of-line) default ctor: makes UnitInfo a
        // non-aggregate so the `const UnitInfo & = {}` default arguments
        // below invoke this constructor instead of aggregate-initialising
        // with the in-class member initializer — which clang rejects as
        // "needed within definition of enclosing class outside of member
        // functions" when the type is nested in InpMeshWriter.
        /**
         * @brief Construct empty length-unit and source-CRS metadata strings. The out-of-line
         * constructor keeps this nested type non-aggregate so default UnitInfo arguments are
         * well-formed. Owns its QString values; no IO, unit discovery, engine query or
         * conversion.
         */
        UnitInfo();
    };

    /**
     * @brief Render native geometry, coupling and infiltration sections into an owned text
     * block.
     * @param[in] mesh Borrowed authoring mesh, unchanged. Coordinates/depths/parameters retain
     * supplied units; caller validates finite geometry, indices, token-safe tags and
     * triangle-before-quad engine order.
     * @param[in] coupling Borrowed node/roughness maps, unchanged. Node identifiers/tags must be
     * whitespace-grammar-safe. Cell roughness comes from triangleMannings/defaultMannings, not
     * MeshTriangle::mannings.
     * @param[in] defaultMannings Manning roughness in the authored model convention, used where
     * the coupling map lacks a cell entry; no physical/range validation.
     * @param[in] units Borrowed UNITS/SOURCE_CRS metadata, unchanged; no reprojection or
     * numerical unit conversion. Nonempty values emit comments.
     * @return Owned native [2D_*] text, always including vertices and triangles; quads emitted
     * only when present. Empty coupling/infiltration containers omit their sections, but
     * nonempty containers with invalid/skipped rows can produce header-only sections. Uses
     * max_digits10 precision for finite doubles. No mesh.ok, index/topology, reference,
     * finite-value or text-token validation, file writes or retained input references. QHash
     * coupling row order is unspecified. Allocates complete text; caller owns subsequent
     * publication.
     *
     * @details Render the 2D mesh sections as a single text block.
     *
     * Order: `[2D_VERTICES]`, `[2D_TRIANGLES]` (triangle cells), `[2D_QUADS]` (`V1 V2 V3 V4
     * MANNINGS_N [INIT_DEPTH] [TAG]`, quad cells — emitted ONLY when the mesh holds a quad, so
     * all-triangle output is byte-identical to the pre-quad writer), `[2D_VERTEX_NODE_MAP]`,
     * `[2D_TRIANGLE_NODE_MAP]`, then the GG0a infiltration family `[2D_INFILTRATION_OPTIONS]` /
     * `[2D_INFILTRATION_DEFAULTS]` / `[2D_INFILTRATION]`. Each section starts with a
     * `;;`-prefixed header comment. Sections with no content (e.g. empty vertex_node_map, a mesh
     * with no infiltration data) are omitted entirely.
     *
     * The infiltration sections are per-cell mesh attributes, so they ride with the mesh — into
     * the `.2dm` in external mode, inline otherwise. They are deliberately NOT appended to
     * `[2D_TRIANGLES]`, whose columns are positional.
     */
    [[nodiscard]] static QString buildSectionText(const MeshResult &mesh,
                                                  const CouplingMap &coupling,
                                                  double defaultMannings = 0.035,
                                                  const UnitInfo &units = {});

    /**
     * @brief Render authored cell/local-edge boundary rows with raw variant parameters.
     * @param[in] bcs Borrowed stride-four cell-edge vector, unchanged; caller supplies all
     * slots, default triangle padding, consistent shared-edge factors and valid variant
     * values/names.
     * @return Owned section or empty for entirely default entries. Non-Wall or nonempty-group
     * rows emit; a Wall entry with only unused nonzero parameters can trigger a header without a
     * row. No mesh is available to filter triangle padding or excess cells. PARAM_2 is always
     * '*'; unused names/groups use '*'. No size, enum, range, physical-unit or identifier
     * validation and no conversion/IO.
     *
     * @details Slice §V.VD.1 — render the `[2D_BOUNDARY_CONDITIONS]` section for the per-edge BC
     * vector. Returns an empty string when every edge is default Wall without a group (avoids
     * polluting the .inp with a section that round-trips to default). Format:
     *
     * @code
     * [2D_BOUNDARY_CONDITIONS]
     * ;; TRI  EDGE  TYPE             PARAM_1        PARAM_2  GROUP
     *    12   0     NORMAL_FLOW      0.002          *        *
     *    12   1     SPECIFIED_STAGE  95.4           *        *
     *    45   2     TS_STAGE         DownstreamTS   *        Outlet
     * @endcode
     *
     * TYPE is one of WALL / NORMAL_FLOW / SPECIFIED_STAGE / TS_STAGE / SPECIFIED_FLOW / TS_FLOW
     * / RATING_CURVE (see mesh::MeshBCTypes). PARAM_1 is the type's primary parameter (slope /
     * head / TS name / flow / TS name / curve name). PARAM_2 is reserved for future extensions;
     * today always "*". GROUP is the optional named group ("*" = none).
     */
    [[nodiscard]] static QString buildBCSectionText(const QVector<MeshEdgeBC> &bcs);

    /**
     * @brief Render one nondefault conveyance factor per undirected real mesh edge.
     * @param[in] mesh Borrowed authoring mesh, unchanged. Coordinates/depths/parameters retain
     * supplied units; caller validates finite geometry, indices, token-safe tags and
     * triangle-before-quad engine order.
     * @param[in] bcs Borrowed stride-four cell-edge vector, unchanged; caller supplies all
     * slots, default triangle padding, consistent shared-edge factors and valid variant
     * values/names.
     * @return Owned section, empty when no supplied slot differs from 1.0. First encountered
     * nondefault slot for each endpoint pair wins; default slots do not reserve the pair. Short
     * vectors skip missing edges, and nondefault padding/excess slots can trigger a header
     * without emitted rows. Endpoint identifiers are serialized without coordinate lookup.
     * Factors are intended dimensionless [0,1], not validated here. Allocates a deduplication
     * set and text; retains no input and changes no mesh/file/engine state.
     *
     * @details Engine §11A — render the `[2D_EDGE_CONVEYANCE]` section.
     *
     * Walks \p bcs (flat-indexed `mesh::edgeSlot(cell, edge)`, parallel to
     * \p mesh; endpoints via `mesh::edgeEndpoints`, every edge of a quad included) and emits one
     * row per edge whose conveyance differs from the default 1.0. Interior edges occupy two
     * slots that the GUI keeps in sync (see SWMM2DMeshLayer::applyMeshEdgeConveyance), so the
     * writer canonicalises on the first encountered vertex-pair and silently drops the second
     * half — interior edges appear exactly once. Returns an empty string when every edge is at
     * default.
     *
     * Format (matches the engine parser in `SectionHandlers2D.cpp`):
     *
     * @code
     * [2D_EDGE_CONVEYANCE]
     * ;; FROM_VERTEX  TO_VERTEX  CONVEYANCE
     *    12           37         0.5
     *    12           45         0
     * @endcode
     *
     * CONVEYANCE is a dimensionless multiplier in [0, 1]. 1.0 (default) is unrestricted; 0.0 is
     * a closed (impermeable) edge.
     */
    [[nodiscard]] static QString buildConveyanceSectionText(const MeshResult &mesh,
                                                            const QVector<MeshEdgeBC> &bcs);

    /**
     * @brief Publish a native mesh sidecar, then retarget an existing INP to that sidecar.
     * @param[in] inpPath Borrowed existing readable INP pathname, also the anchor for stored
     * relative paths.
     * @param[in] meshFilePath Borrowed distinct sidecar pathname; empty selects the INP complete
     * base name followed by .2dm in its directory. Existing sidecar may be replaced.
     * @param[in] mesh Borrowed authoring mesh, unchanged. Coordinates/depths/parameters retain
     * supplied units; caller validates finite geometry, indices, token-safe tags and
     * triangle-before-quad engine order.
     * @param[in] coupling Borrowed node/roughness maps, unchanged. Node identifiers/tags must be
     * whitespace-grammar-safe. Cell roughness comes from triangleMannings/defaultMannings, not
     * MeshTriangle::mannings.
     * @param[in] defaultMannings Manning roughness in the authored model convention, used where
     * the coupling map lacks a cell entry; no physical/range validation.
     * @param[out] errorOut Optional borrowed diagnostic, set on checked failure; not cleared on
     * success. No pointer retained.
     * @param[in] units Borrowed UNITS/SOURCE_CRS metadata, unchanged; no reprojection or
     * numerical unit conversion. Nonempty values emit comments.
     * @return True after both commits; false for empty/invalid mesh, sidecar write, INP read or
     * INP write failure. Sidecar commits FIRST, even before reading the INP: a later failure can
     * leave the sidecar newly written while the INP is unchanged. No rollback or multi-file
     * transaction. Removes GUI-owned geometry, coupling, infiltration, BC/conveyance and old
     * reference sections from the INP, preserving other 2D configuration. Does not run
     * validateTopologyReplacement(). Stores a relative path whenever QDir can represent one,
     * including ../ paths; otherwise absolute. Whitespace/semicolon/comma paths are quoted, but
     * embedded quotes/newlines are not escaped. Caller validates grammar and distinct targets.
     * Each file commit uses QSaveFile with direct-write fallback disabled. Caller supplies
     * existing parent directories and exclusive publishing access; no engine state is changed.
     *
     * @details Default mode: write the mesh into a sibling `.2dm` file and patch the `.inp` with
     * a `[2D_MESH_FILE]` reference.
     *
     *
     * Both writes are atomic via `QSaveFile`. The `.inp`'s GUI-owned mesh-data sections are
     * stripped (if present) so a stale inline mesh doesn't shadow the external file. Path stored
     * in `[2D_MESH_FILE]` is **relative to the .inp directory** whenever representable,
     * including parent-directory paths; absolute otherwise.
     */
    [[nodiscard]] static bool writeExternal(const QString &inpPath, const QString &meshFilePath,
                                            const MeshResult &mesh, const CouplingMap &coupling,
                                            double defaultMannings = 0.035,
                                            QString *errorOut = nullptr,
                                            const UnitInfo &units = {});

    /**
     * @brief Publish sidecar/reference first, then append requested boundary/conveyance sections
     * to the INP.
     * @param[in] inpPath Borrowed readable INP target and relative-path anchor.
     * @param[in] meshFilePath Borrowed distinct sidecar target; empty chooses the default .2dm
     * name.
     * @param[in] mesh Borrowed authoring mesh, unchanged. Coordinates/depths/parameters retain
     * supplied units; caller validates finite geometry, indices, token-safe tags and
     * triangle-before-quad engine order.
     * @param[in] coupling Borrowed node/roughness maps, unchanged. Node identifiers/tags must be
     * whitespace-grammar-safe. Cell roughness comes from triangleMannings/defaultMannings, not
     * MeshTriangle::mannings.
     * @param[in] bcs Borrowed stride-four cell-edge vector, unchanged; caller supplies all
     * slots, default triangle padding, consistent shared-edge factors and valid variant
     * values/names.
     * @param[in] defaultMannings Manning roughness in the authored model convention, used where
     * the coupling map lacks a cell entry; no physical/range validation.
     * @param[out] errorOut Optional borrowed diagnostic, set on checked failure; not cleared on
     * success. No pointer retained.
     * @param[in] units Borrowed UNITS/SOURCE_CRS metadata, unchanged; no reprojection or
     * numerical unit conversion. Nonempty values emit comments.
     * @return True after base sidecar/reference commits and, when needed, a second INP commit;
     * false on any checked stage failure. Boundary/conveyance text is appended to the MAIN INP,
     * not the sidecar. Failure during the extra read/write leaves already committed
     * geometry/reference updates in place. No exact edge-vector size check, topology-replacement
     * guard or multi-file rollback. Path quoting, owned-section removal and roughness behavior
     * match the non-BC overload. Each file commit uses QSaveFile with direct-write fallback
     * disabled. Caller supplies existing parent directories and exclusive publishing access; no
     * engine state is changed.
     *
     * @details §V.VD.1 overload — additionally writes the `[2D_BOUNDARY_CONDITIONS]` section.
     * Empty / fully default BC vector is equivalent to the non-BC overload (section is omitted).
     */
    [[nodiscard]] static bool writeExternal(const QString &inpPath, const QString &meshFilePath,
                                            const MeshResult &mesh, const CouplingMap &coupling,
                                            const QVector<MeshEdgeBC> &bcs, double defaultMannings,
                                            QString *errorOut = nullptr,
                                            const UnitInfo &units = {});

    /**
     * @brief Replace GUI-owned mesh sections and any external mesh reference with an inline
     * mesh.
     * @param[in] inpPath Borrowed existing readable INP pathname to replace atomically.
     * @param[in] mesh Borrowed authoring mesh, unchanged. Coordinates/depths/parameters retain
     * supplied units; caller validates finite geometry, indices, token-safe tags and
     * triangle-before-quad engine order.
     * @param[in] coupling Borrowed node/roughness maps, unchanged. Node identifiers/tags must be
     * whitespace-grammar-safe. Cell roughness comes from triangleMannings/defaultMannings, not
     * MeshTriangle::mannings.
     * @param[in] defaultMannings Manning roughness in the authored model convention, used where
     * the coupling map lacks a cell entry; no physical/range validation.
     * @param[out] errorOut Optional borrowed diagnostic, set on checked failure; not cleared on
     * success. No pointer retained.
     * @param[in] units Borrowed UNITS/SOURCE_CRS metadata, unchanged; no reprojection or
     * numerical unit conversion. Nonempty values emit comments.
     * @return True after checked read and single commit; false for mesh.ok false, empty geometry
     * or IO failure. Strips owned geometry/coupling/infiltration/BC/conveyance/reference
     * sections; other 2D configuration and existing metadata outside stripped sections survive.
     * Does not run topology ownership guard. No failure rollback is needed across other files
     * because only this INP is published. Each file commit uses QSaveFile with direct-write
     * fallback disabled. Caller supplies existing parent directories and exclusive publishing
     * access; no engine state is changed.
     *
     * @details Replace (or append) all four 2D sections in the .inp at
     * \p inpPath. Existing 2D sections are stripped first; any existing `[2D_MESH_FILE]` block
     * is also removed so the engine reads the freshly-inlined sections instead.
     */
    [[nodiscard]] static bool writeInline(const QString &inpPath, const MeshResult &mesh,
                                          const CouplingMap &coupling,
                                          double defaultMannings = 0.035,
                                          QString *errorOut = nullptr, const UnitInfo &units = {});

    /**
     * @brief Commit inline geometry, then append requested boundary/conveyance text in a second
     * commit.
     * @param[in] inpPath Borrowed existing readable INP pathname.
     * @param[in] mesh Borrowed authoring mesh, unchanged. Coordinates/depths/parameters retain
     * supplied units; caller validates finite geometry, indices, token-safe tags and
     * triangle-before-quad engine order.
     * @param[in] coupling Borrowed node/roughness maps, unchanged. Node identifiers/tags must be
     * whitespace-grammar-safe. Cell roughness comes from triangleMannings/defaultMannings, not
     * MeshTriangle::mannings.
     * @param[in] bcs Borrowed stride-four cell-edge vector, unchanged; caller supplies all
     * slots, default triangle padding, consistent shared-edge factors and valid variant
     * values/names.
     * @param[in] defaultMannings Manning roughness in the authored model convention, used where
     * the coupling map lacks a cell entry; no physical/range validation.
     * @param[out] errorOut Optional borrowed diagnostic, set on checked failure; not cleared on
     * success. No pointer retained.
     * @param[in] units Borrowed UNITS/SOURCE_CRS metadata, unchanged; no reprojection or
     * numerical unit conversion. Nonempty values emit comments.
     * @return True after base commit and any required second read/commit; false on any checked
     * failure. A failed boundary stage can leave the new inline geometry already saved without
     * requested boundary rows. No edge-vector size or full topology guard is applied; this
     * overload is not one transaction. Each file commit uses QSaveFile with direct-write
     * fallback disabled. Caller supplies existing parent directories and exclusive publishing
     * access; no engine state is changed.
     *
     * @details §V.VD.1 overload — additionally writes the `[2D_BOUNDARY_CONDITIONS]` section
     * inline.
     */
    [[nodiscard]] static bool writeInline(const QString &inpPath, const MeshResult &mesh,
                                          const CouplingMap &coupling,
                                          const QVector<MeshEdgeBC> &bcs, double defaultMannings,
                                          QString *errorOut = nullptr, const UnitInfo &units = {});

    /**
     * @brief Replace GUI-owned inline mesh sections with a reference to an existing filesystem
     * entry.
     * @param[in] inpPath Borrowed existing readable INP to publish.
     * @param[in] meshFilePath Borrowed nonempty existing target; checked for existence only, not
     * regularity, size, grammar or valid mesh contents.
     * @param[out] errorOut Optional borrowed diagnostic, set on checked failure; not cleared on
     * success. No pointer retained.
     * @return True after checked read and one INP commit, false on target/read/write failure. No
     * mesh file is written. Removes owned geometry/coupling/infiltration/BC/conveyance and old
     * reference blocks, preserving other 2D configuration. Relative paths include sibling and
     * ../ forms where representable; otherwise absolute. Embedded quotes/newlines are not
     * escaped; caller validates reference grammar. Each file commit uses QSaveFile with
     * direct-write fallback disabled. Caller supplies existing parent directories and exclusive
     * publishing access; no engine state is changed.
     *
     * @details Retarget the `.inp`'s `[2D_MESH_FILE]` reference at an already-existing `.2dm`
     * without writing any mesh geometry.
     *
     * Strips any inline `[2D_*]` mesh-data sections and any prior `[2D_MESH_FILE]` block, then
     * injects a fresh `[2D_MESH_FILE] FILE <path>` pointing at \p meshFilePath. The stored path
     * is relative to the `.inp` directory whenever representable, absolute otherwise — identical
     * to \ref writeExternal. Atomic via `QSaveFile`.
     */
    [[nodiscard]] static bool writeMeshFileRef(const QString &inpPath, const QString &meshFilePath,
                                               QString *errorOut = nullptr);

    /**
     * @brief Publish an INP reference to a final logical mesh name after checking a physical
     * staged file.
     * @param[in] inpPath Borrowed readable INP target; must retain the final model directory
     * anchor so relative references resolve correctly after adoption.
     * @param[in] logicalFinalMeshPath Borrowed nonempty logical target, not required to exist
     * yet.
     * @param[in] physicalStagedMeshPath Borrowed existing regular nonempty staged file to check;
     * contents and correspondence to the logical target are not validated.
     * @param[out] errorOut Optional borrowed diagnostic, set on checked failure; not cleared on
     * success. No pointer retained.
     * @return True after one INP commit; false for invalid staging metadata or read/write
     * failure. Removes owned inline mesh/reference sections and stores the LOGICAL path using
     * the usual relative/quoting rules. Does not publish/move the staged mesh, inspect engine
     * syntax or coordinate a multi-file save. Each file commit uses QSaveFile with direct-write
     * fallback disabled. Caller supplies existing parent directories and exclusive publishing
     * access; no engine state is changed.
     *
     * @details Reference a prepared mesh using its final logical name. The physical staged file
     * must exist, be regular and nonempty; the logical final file need not exist yet. inpPath
     * must retain the final model's directory anchor for relative references. Publishes only
     * inpPath, atomically.
     */
    [[nodiscard]] static bool writePreparedMeshFileRef(const QString &inpPath,
                                                       const QString &logicalFinalMeshPath,
                                                       const QString &physicalStagedMeshPath,
                                                       QString *errorOut = nullptr);

    /**
     * @brief Read a model and reject recognized unsupported mesh-indexed assignments before
     * replacement.
     * @param[in] filePath Borrowed input/sidecar pathname, read only; whole UTF-8 content is
     * loaded.
     * @param[out] errorOut Optional borrowed diagnostic, set on checked failure; not cleared on
     * success. No pointer retained.
     * @return True when checked read succeeds and no guarded rows are found; false with
     * path/section/line diagnostic otherwise. Rejects recognized indexed velocity, quality,
     * groundwater and snow assignments, including tag scopes needing remapping.
     * Global/XY/AUTO/wildcard cases are retained where recognized. Unknown sections/syntax are
     * not exhaustively validated. No file/engine changes or retained handles.
     *
     * @details Refuse unsupported mesh-indexed velocity, quality and groundwater assignments
     * before topology replacement, including tag assignments whose meaning needs remapping.
     * Global/XY/AUTO configuration is retained. This is an ownership guard, not a complete
     * engine syntax validator. Does not modify the file.
     */
    [[nodiscard]] static bool validateTopologyReplacement(const QString &filePath,
                                                          QString *errorOut = nullptr);

    /**
     * @brief Replace the complete owned topology and attributes in one checked single-file
     * commit.
     * @param[in] filePath Borrowed existing readable target INP/native mesh pathname.
     * @param[in] mesh Borrowed authoring mesh, unchanged. Coordinates/depths/parameters retain
     * supplied units; caller validates finite geometry, indices, token-safe tags and
     * triangle-before-quad engine order.
     * @param[in] bcs Borrowed stride-four cell-edge vector, unchanged; caller supplies all
     * slots, default triangle padding, consistent shared-edge factors and valid variant
     * values/names.
     * @param[out] errorOut Optional borrowed diagnostic, set on checked failure; not cleared on
     * success. No pointer retained.
     * @param[in] defaultMannings Authored roughness fallback for cells whose mesh.mannings is
     * nonfinite; finite values, including nonpositive ones, are copied without physical
     * validation.
     * @param[in] units Optional borrowed metadata. Null preserves last existing UNITS/SOURCE_CRS
     * values; nonnull replaces UNITS even with empty text and replaces SOURCE_CRS only when
     * nonempty. No numerical conversion.
     * @return True after one commit; false for invalid/empty mesh, bcs.size()!=4*cellCount,
     * guarded indexed rows or IO failure. Old row counts need not match. Rebuilds coupling from
     * vertex coupledNode and cellCouplings; replaces geometry, infiltration, BC/conveyance and
     * removes an external reference. Canonicalizes unit/CRS metadata outside sections; preserves
     * other configuration. Caller validates geometry, field ranges, target ownership and
     * complete stride arithmetic before calling. Each file commit uses QSaveFile with
     * direct-write fallback disabled. Caller supplies existing parent directories and exclusive
     * publishing access; no engine state is changed.
     *
     * @details Replace the complete GUI-owned mesh in a prepared INP/mesh file, without
     * requiring its old topology or row counts to match. Rebuilds coupling from
     * coupledNode/cellCouplings and replaces geometry, attributes, infiltration, boundary
     * conditions and conveyance in one atomic write. Unsupported topology-indexed rows fail
     * without changing the file. Removes any old external mesh reference. Null units preserve
     * metadata; explicit units follow patchMeshSections' metadata rules.
     */
    [[nodiscard]] static bool replaceMeshSections(const QString &filePath, const MeshResult &mesh,
                                                  const QVector<MeshEdgeBC> &bcs,
                                                  QString *errorOut = nullptr,
                                                  double defaultMannings = 0.035,
                                                  const UnitInfo *units = nullptr);

    /**
     * @brief Replace only owned boundary/conveyance sections in one file.
     * @param[in] filePath Borrowed existing readable INP/native mesh target.
     * @param[in] mesh Borrowed authoring mesh, unchanged. Coordinates/depths/parameters retain
     * supplied units; caller validates finite geometry, indices, token-safe tags and
     * triangle-before-quad engine order.
     * @param[in] bcs Borrowed stride-four cell-edge vector, unchanged; caller supplies all
     * slots, default triangle padding, consistent shared-edge factors and valid variant
     * values/names.
     * @param[out] errorOut Optional borrowed diagnostic, set on checked failure; not cleared on
     * success. No pointer retained.
     * @return True after checked read and single commit; false on IO failure. Other section
     * bodies survive, though trailing newline layout is normalized. No bcs.size, mesh.ok,
     * physical or topology validation; callers must supply proper edge slots. Default rows
     * remove prior boundary/conveyance sections. Each file commit uses QSaveFile with
     * direct-write fallback disabled. Caller supplies existing parent directories and exclusive
     * publishing access; no engine state is changed.
     *
     * @details Replace the `[2D_BOUNDARY_CONDITIONS]` and `[2D_EDGE_CONVEYANCE]` sections of \p
     * filePath (a `.inp` or external `.2dm`) with sections built from \p bcs, leaving every
     * other section untouched.
     *
     * All-default \p bcs strips the sections without appending (reset-to-Wall round-trips).
     * Atomic via `QSaveFile`.
     */
    [[nodiscard]] static bool patchBCSections(const QString &filePath, const MeshResult &mesh,
                                              const QVector<MeshEdgeBC> &bcs,
                                              QString *errorOut = nullptr);

    /**
     * @brief Rewrite owned geometry attributes/coupling/infiltration after matching row counts.
     * @param[in] filePath Borrowed existing readable INP/native mesh target.
     * @param[in] mesh Borrowed authoring mesh, unchanged. Coordinates/depths/parameters retain
     * supplied units; caller validates finite geometry, indices, token-safe tags and
     * triangle-before-quad engine order.
     * @param[out] errorOut Optional borrowed diagnostic, set on checked failure; not cleared on
     * success. No pointer retained.
     * @param[in] defaultMannings Roughness materialized only when a row needs later depth/tag
     * columns and neither a positive finite mesh value nor a parseable old roughness token is
     * available.
     * @param[in] units Optional borrowed metadata. Null preserves last existing UNITS/SOURCE_CRS
     * values; nonnull replaces UNITS even with empty text and replaces SOURCE_CRS only when
     * nonempty. No numerical conversion.
     * @return True after exact vertex/triangle/quad row-count checks and one commit; false on
     * mismatch or IO failure. Counts establish positional correspondence only, not topology
     * identity. Positive finite mesh roughness wins; otherwise parseable original roughness
     * tokens survive verbatim. Finite mesh depth wins, otherwise old parseable depth can
     * survive. Tags/connectivity are rebuilt from mesh. Preserves BC/conveyance and external
     * reference sections. Does not check mesh.ok or full topology. Each file commit uses
     * QSaveFile with direct-write fallback disabled. Caller supplies existing parent directories
     * and exclusive publishing access; no engine state is changed.
     *
     * @details Replace the `[2D_VERTICES]`, `[2D_TRIANGLES]`, `[2D_QUADS]`,
     * `[2D_VERTEX_NODE_MAP]`, `[2D_TRIANGLE_NODE_MAP]` and `[2D_INFILTRATION_OPTIONS]` /
     * `[2D_INFILTRATION_DEFAULTS]` / `[2D_INFILTRATION]` sections of \p filePath with sections
     * rebuilt from the layer's editable mesh state, leaving every other section untouched.
     *
     * Re-emits the layer's geometry, attributes, coupling and infiltration. The layer is
     * authoritative for these editable fields; a triangle whose Manning is unset (NaN) keeps the
     * file's existing token so a generation-time default survives the rewrite.
     *
     * Fails without touching the file when the section row counts don't match \p mesh — rows map
     * to mesh entries by position, so a mismatch means the file holds a different mesh. Atomic
     * via `QSaveFile`.
     */
    [[nodiscard]] static bool patchAttributeSections(const QString &filePath,
                                                     const MeshResult &mesh,
                                                     QString *errorOut = nullptr,
                                                     double defaultMannings = 0.035,
                                                     const UnitInfo *units = nullptr);

    /**
     * @brief Patch matched mesh attributes and complete boundary state in one atomic file
     * update.
     * @param[in] filePath Borrowed existing readable INP/native mesh target.
     * @param[in] mesh Borrowed authoring mesh, unchanged. Coordinates/depths/parameters retain
     * supplied units; caller validates finite geometry, indices, token-safe tags and
     * triangle-before-quad engine order.
     * @param[in] bcs Borrowed stride-four cell-edge vector, unchanged; caller supplies all
     * slots, default triangle padding, consistent shared-edge factors and valid variant
     * values/names.
     * @param[out] errorOut Optional borrowed diagnostic, set on checked failure; not cleared on
     * success. No pointer retained.
     * @param[in] defaultMannings Positional-column fallback roughness, using
     * patchAttributeSections rules.
     * @param[in] units Optional borrowed metadata. Null preserves last existing UNITS/SOURCE_CRS
     * values; nonnull replaces UNITS even with empty text and replaces SOURCE_CRS only when
     * nonempty. No numerical conversion.
     * @return True after exact edge-vector and vertex/triangle/quad row-count checks plus one
     * commit; false on mismatch/IO failure with this target unchanged. Uses
     * patchAttributeSections attribute preservation rules, then rebuilds BC/conveyance in memory
     * before publication. No full geometry, identity, physical field validation or
     * topology-replacement guard. Other files are outside this guarantee. Each file commit uses
     * QSaveFile with direct-write fallback disabled. Caller supplies existing parent directories
     * and exclusive publishing access; no engine state is changed.
     *
     * @details Replace mesh attributes and BC/conveyance sections together.
     *
     * Uses the same payload and metadata rules as patchAttributeSections and patchBCSections,
     * with one checked read and one atomic QSaveFile commit. Row counts and the complete
     * edge-slot vector must match. Validation or write failure leaves this file unchanged; other
     * files involved in a project Save are outside this single-file guarantee.
     */
    [[nodiscard]] static bool patchMeshSections(const QString &filePath, const MeshResult &mesh,
                                                const QVector<MeshEdgeBC> &bcs,
                                                QString *errorOut = nullptr,
                                                double defaultMannings = 0.035,
                                                const UnitInfo *units = nullptr);

    /**
     * @brief Strip recognized external-reference blocks while preserving inline mesh and other
     * sections.
     * @param[in] inpPath Borrowed existing readable INP pathname.
     * @param[out] errorOut Optional borrowed diagnostic, set on checked failure; not cleared on
     * success. No pointer retained.
     * @return True after checked read and one commit; false on IO failure. Even an absent block
     * performs a write and may normalize final newlines. Recognizes trimmed bracket-only section
     * headers; trailing header comments are not stripped before recognition. Does not
     * inspect/publish an external file. Each file commit uses QSaveFile with direct-write
     * fallback disabled. Caller supplies existing parent directories and exclusive publishing
     * access; no engine state is changed.
     *
     * @details Strip any `[2D_MESH_FILE]` reference from the `.inp` so the engine falls back to
     * the inline `[2D_*]` mesh sections.
     *
     * The inverse of \ref writeMeshFileRef . removes only the `[2D_MESH_FILE]` block and leaves
     * every other section (including the inline mesh data) untouched. Used when the user selects
     * the inline mesh as the active 2D configuration. No-op-safe when the section is absent.
     * Atomic via `QSaveFile`.
     */
    [[nodiscard]] static bool clearMeshFileRef(const QString &inpPath, QString *errorOut = nullptr);

    /**
     * @brief Dispatch to the geometry-only external or inline publication strategy.
     * @param[in] mode External or Inline; unknown enum values return false without setting
     * errorOut.
     * @param[in] inpPath Borrowed existing readable INP target.
     * @param[in] meshFilePath Borrowed sidecar target used only for External; empty selects the
     * default.
     * @param[in] mesh Borrowed authoring mesh, unchanged. Coordinates/depths/parameters retain
     * supplied units; caller validates finite geometry, indices, token-safe tags and
     * triangle-before-quad engine order.
     * @param[in] coupling Borrowed node/roughness maps, unchanged. Node identifiers/tags must be
     * whitespace-grammar-safe. Cell roughness comes from triangleMannings/defaultMannings, not
     * MeshTriangle::mannings.
     * @param[in] defaultMannings Manning roughness in the authored model convention, used where
     * the coupling map lacks a cell entry; no physical/range validation.
     * @param[out] errorOut Optional borrowed diagnostic, set on checked failure; not cleared on
     * success. No pointer retained.
     * @param[in] units Borrowed UNITS/SOURCE_CRS metadata, unchanged; no reprojection or
     * numerical unit conversion. Nonempty values emit comments.
     * @return Selected writer result, or false for unsupported mode. External can leave a
     * sidecar committed after later failure; no BC overload or topology ownership guard is
     * invoked. No retained inputs.
     *
     * @details Convenience dispatch on \ref MeshOutputMode.
     */
    [[nodiscard]] static bool write(MeshOutputMode mode, const QString &inpPath,
                                    const QString &meshFilePath, const MeshResult &mesh,
                                    const CouplingMap &coupling, double defaultMannings = 0.035,
                                    QString *errorOut = nullptr, const UnitInfo &units = {});
};

} // namespace mesh

#endif // OPENSWMMVIS_MESH_INPMESHWRITER_H
