/**
 * @file      meshcommands.h
 * @brief     Undoable per-cell 2D mesh attribute edits.
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 *
 * Undoable per-cell 2D mesh attribute edits. One command covers a whole
 * selection (or a whole raster/shapefile assignment), so a bulk write is a
 * single Ctrl+Z rather than thousands.
 *
 * Every editing surface — the mesh-editing toolbar, the properties panel
 * adapter, and the Cell Data assignment dialog — pushes through
 * pushCellParamEdit() so undo behaves identically no matter where the edit
 * came from.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef OPENSWMMVIS_MAP_MESHCOMMANDS_H
#define OPENSWMMVIS_MAP_MESHCOMMANDS_H

#include "map/mapundostack.h"
#include "mesh/meshedgebc.h"
#include "mesh/meshinfil.h"
#include "mesh/meshresult.h"

#include <QByteArray>
#include <QPair>
#include <QPointer>
#include <QSignalBlocker>
#include <QString>
#include <QVariant>
#include <QVector>

class SWMM2DMeshLayer;

namespace mesh
{
/** Coalesce multi-element command notifications, including undo/redo; nestable. */
class AttributeEditBatch
{
public:
    AttributeEditBatch(SWMM2DMeshLayer *layer, qsizetype count);
    ~AttributeEditBatch();
    AttributeEditBatch(const AttributeEditBatch &) = delete;
    AttributeEditBatch &operator=(const AttributeEditBatch &) = delete;
private:
    QPointer<SWMM2DMeshLayer> m_layer;
    QSignalBlocker m_signals;
};
}

/**
 * @class MeshSetTriangleTagCommand
 * @brief Undoable descriptive tag assignment across cell indices, with parallel original tags.
 */
class MeshSetTriangleTagCommand : public MapCommand
{
public:
    /**
     * @brief Capture a shared new descriptive tag and parallel old tags for cell undo/redo.
     * @param[in] layer Optional borrowed mesh layer, guarded against QObject deletion.
     * @param[in] triangles Zero-based mesh cell indices in input order; duplicates not removed.
     * @param[in] newTag Target descriptive tag copied unchanged, including empty clear.
     * @param[in] oldTags Parallel previous tags; undo stops at shorter indices/tag vector.
     * @param[in] text Undo-menu label copied verbatim.
     * @param[in] canvas Optional borrowed raw canvas stored by MapCommand; no direct invalidation
     *   by these commands.
     * @param[in] parent Optional QUndoCommand owner, otherwise stack owns after push.
     * @note GUI-thread only; layer is guarded by QPointer and deletion makes replay a no-op.
     *   Constructor captures supplied values without applying; caller snapshots BEFORE changes.
     *   Indices must still describe same mesh topology on replay; QPointer does not protect against
     *   topology replacement. Setter failures are ignored and batch writes are not atomic. Layer
     *   apply helpers supply attribute/repaint notifications; no command-level file save or engine
     *   transaction.
     */
    MeshSetTriangleTagCommand(SWMM2DMeshLayer *layer, QVector<int> triangles, QString newTag,
                              QVector<QString> oldTags, const QString &text, MapCanvas *canvas,
                              QUndoCommand *parent = nullptr);

    /**
     * @brief Restore parallel old descriptive tags.
     * @note Null/deleted layer no-op. Stops at shorter indices/tags vector; no inferred snapshots.
     *   No per-write errors reported or batch rollback.
     */
    void undo() override;
    /**
     * @brief Write shared new tag to each stored cell.
     * @note Null/deleted layer no-op. All layer setter statuses ignored; duplicate targets are
     *   attempted repeatedly. Invoked on initial stack push as well as later redo.
     */
    void redo() override;
    /**
     * @return Mesh command family ID 44; this class supplies no mergeWith override, so edits remain
     *   separate undo steps.
     */
    int id() const override { return 44; }

private:
    QPointer<SWMM2DMeshLayer> m_layer;
    QVector<int> m_tris;
    QString m_newTag;
    QVector<QString> m_oldTags;
};

class MeshSetTriangleAttributeCommand : public MapCommand
{
public:
    /*! \param layer     Mesh layer that owns the triangles.
     *  \param key       mesh::CellParamSpec key ("mannings", "initDepth", …).
     *  \param triangles Triangle indices to write.
     *  \param newValues Parallel to \p triangles: the value to write to each.
     *  \param oldValues Parallel to \p triangles: the pre-edit value of each
     *                   (NaN = the attribute was unset).
     *  \param text      Undo-stack label.
     * @brief Capture a single numeric key and parallel old/new cell values for replay.
     * @param[in] canvas Optional borrowed raw canvas stored by MapCommand; no direct invalidation
     *   by these commands.
     * @param[in] parent Optional QUndoCommand owner, otherwise stack owns after push.
     * @note GUI-thread only; layer is guarded by QPointer and deletion makes replay a no-op.
     *   Constructor captures supplied values without applying; caller snapshots BEFORE changes.
     *   Indices must still describe same mesh topology on replay; QPointer does not protect against
     *   topology replacement. Setter failures are ignored and batch writes are not atomic. Layer
     *   apply helpers supply attribute/repaint notifications; no command-level file save or engine
     *   transaction.
     * @note Cell indices include triangles/quads. Values follow CellParamSpec project-unit
     *   conventions (Manning n dimensionless, initial depth project length, infiltration per
     *   method). Any nonfinite old/new value is replaced by registry default on replay, so undo of
     *   originally unset numeric fields materializes a default rather than restoring absence.
     *   Single infiltration-key edits materialize complete overrides and do not restore
     *   inheritance; use whole-row infiltration command for provenance.
     */
    MeshSetTriangleAttributeCommand(SWMM2DMeshLayer *layer, QByteArray key, QVector<int> triangles,
                                    QVector<double> newValues, QVector<double> oldValues,
                                    const QString &text, MapCanvas *canvas,
                                    QUndoCommand *parent = nullptr);

    /**
     * @brief Restore numeric before-values, replacing nonfinite/unset with registry defaults.
     * @note Null/deleted layer no-op. Does not restore absent numeric columns or infiltration
     *   inheritance. No per-write errors reported or batch rollback.
     */
    void undo() override;
    /**
     * @brief Apply parallel numeric after-values through key dispatch.
     * @note Null/deleted layer no-op. Nonfinite values become spec defaults; unknown/disabled keys
     *   fail silently. Iterates shorter vector. Invoked on initial stack push as well as later
     *   redo.
     */
    void redo() override;
    /**
     * @return Mesh command family ID 43; this class supplies no mergeWith override, so edits remain
     *   separate undo steps.
     */
    int id() const override { return 43; }

private:
    /**
     * @brief Apply parallel numeric cell values, substituting registry defaults for all nonfinite
     *   entries.
     * @param[in] values Key-specific cell values, interpreted in registry units; only common prefix
     *   with cell indices visited.
     * @note Null/deleted layer no-op; unknown/disabled keys or method-masked infiltration slots can
     *   be rejected silently. Each write delegates layer notifications; no direct canvas
     *   invalidation.
     */
    void apply(const QVector<double> &values);

    QPointer<SWMM2DMeshLayer> m_layer;
    QByteArray m_key;
    QVector<int> m_tris;
    QVector<double> m_newValues;
    QVector<double> m_oldValues;
};

/*!
 * \class MeshSetVertexAttributeCommand
 * \brief Undoable write of one per-vertex attribute across a set of vertices.
 * \details Stores a snapshot of every touched vertex's editable attribute set
 *          (z / tag / coupledNode / couplingCd / couplingArea) on both sides of
 *          the edit rather than the single changed field. That is what makes
 *          undo faithful for the interdependent ones: clearing a coupling also
 *          resets Cd and Area to the engine defaults, so restoring the node id
 *          alone would silently drop the user's coefficients.
 *
 *          The snapshot is applied field-by-field through the layer's apply*
 *          helpers, in an order that respects their preconditions (coupledNode
 *          before Cd/Area, which are rejected on an uncoupled vertex). Fields
 *          that did not move are no-ops inside the helpers.
 */
class MeshSetVertexAttributeCommand : public MapCommand
{
public:
    /*! \param layer     Mesh layer that owns the vertices.
     *  \param key       Attribute key ("z", "tag", "coupledNode",
     *                   "couplingCd", "couplingArea") — used for labelling
     *                   only; the snapshots carry the values.
     *  \param vertices  Vertex indices to write.
     *  \param newAttrs  Parallel to \p vertices: post-edit attribute snapshot.
     *  \param oldAttrs  Parallel to \p vertices: pre-edit attribute snapshot.
     * @brief Capture complete editable vertex attributes, preserving coupled-node coefficient and
     *   authored-flag relationships.
     * @param[in] text Undo-menu label copied verbatim.
     * @param[in] canvas Optional borrowed raw canvas stored by MapCommand; no direct invalidation
     *   by these commands.
     * @param[in] parent Optional QUndoCommand owner, otherwise stack owns after push.
     * @note GUI-thread only; layer is guarded by QPointer and deletion makes replay a no-op.
     *   Constructor captures supplied values without applying; caller snapshots BEFORE changes.
     *   Indices must still describe same mesh topology on replay; QPointer does not protect against
     *   topology replacement. Setter failures are ignored and batch writes are not atomic. Layer
     *   apply helpers supply attribute/repaint notifications; no command-level file save or engine
     *   transaction.
     * @note Writes tag, coupledNode, Cd, area, authored flags, then Z; XY/marker in snapshots are
     *   ignored. Cd dimensionless, area mesh-length units squared, Z project vertical units.
     *   Snapshot key stored but not used to select writes. Replay stops at shorter index/snapshot
     *   vector, with no deduplication.
     */
    MeshSetVertexAttributeCommand(SWMM2DMeshLayer *layer, QByteArray key, QVector<int> vertices,
                                  QVector<mesh::MeshVertex> newAttrs,
                                  QVector<mesh::MeshVertex> oldAttrs, const QString &text,
                                  MapCanvas *canvas, QUndoCommand *parent = nullptr);

    /**
     * @brief Restore complete before vertex attributes including authored Cd/area flags.
     * @note Null/deleted layer no-op. Preserves coupling order, not XY/marker. Stops at shorter
     *   snapshot vector. No per-write errors reported or batch rollback.
     */
    void undo() override;
    /**
     * @brief Apply complete after vertex attributes in dependency order.
     * @note Null/deleted layer no-op. Writes all editable fields regardless of key; coefficient
     *   setters may reject uncoupled vertices; failures ignored. Invoked on initial stack push as
     *   well as later redo.
     */
    void redo() override;
    /**
     * @return Mesh command family ID 45; this class supplies no mergeWith override, so edits remain
     *   separate undo steps.
     */
    int id() const override { return 45; }

private:
    /**
     * @brief Restore editable vertex snapshots in coupling-safe order, including coefficient
     *   authorship flags.
     * @param[in] attrs Full value snapshots; only tag, coupledNode, Cd/area/flags and Z used,
     *   common prefix with indices.
     * @note Layer setters called tag→node→Cd→area→authorship→Z; statuses ignored, allowing partial
     *   fields/rows. Null/deleted layer no-op; no topology/lifetime version check.
     */
    void apply(const QVector<mesh::MeshVertex> &attrs);

    QPointer<SWMM2DMeshLayer> m_layer;
    QByteArray m_key;
    QVector<int> m_verts;
    QVector<mesh::MeshVertex> m_newAttrs;
    QVector<mesh::MeshVertex> m_oldAttrs;
};

/*!
 * \class MeshSetCellCouplingsCommand
 * \brief Undoable replacement of the node→cell coupling row set
 *        ([2D_TRIANGLE_NODE_MAP]) — the Remap 1D↔2D dialog's cell half.
 */
class MeshSetCellCouplingsCommand : public MapCommand
{
public:
    /**
     * @brief Capture complete node-to-cell coupling row sets for replacement undo/redo.
     * @param[in] layer Optional borrowed mesh layer, guarded against deletion.
     * @param[in] newRows Complete target coupling list, not a delta; fields retain
     *   mesh::CellCoupling conventions.
     * @param[in] oldRows Caller-captured complete prior list, copied for undo.
     * @param[in] text Undo-menu label copied verbatim.
     * @param[in] canvas Optional borrowed raw canvas stored by MapCommand; no direct invalidation
     *   by these commands.
     * @param[in] parent Optional QUndoCommand owner, otherwise stack owns after push.
     * @note GUI-thread only; layer is guarded by QPointer and deletion makes replay a no-op.
     *   Constructor captures supplied values without applying; caller snapshots BEFORE changes.
     *   Indices must still describe same mesh topology on replay; QPointer does not protect against
     *   topology replacement. Setter failures are ignored and batch writes are not atomic. Layer
     *   apply helpers supply attribute/repaint notifications; no command-level file save or engine
     *   transaction.
     * @note Whole list passed to applyCellCouplings; no local ID/index validation or per-row
     *   snapshots are inferred.
     */
    MeshSetCellCouplingsCommand(SWMM2DMeshLayer *layer, QVector<mesh::CellCoupling> newRows,
                                QVector<mesh::CellCoupling> oldRows, const QString &text,
                                MapCanvas *canvas, QUndoCommand *parent = nullptr);

    /**
     * @brief Replace complete node-to-cell coupling list with stored original.
     * @note Null/deleted layer no-op. Return status ignored; not a merge of current rows. No
     *   per-write errors reported or batch rollback.
     */
    void undo() override;
    /**
     * @brief Replace complete node-to-cell coupling list with stored target.
     * @note Null/deleted layer no-op. Layer applies own checks/notifications; return status
     *   ignored. Invoked on initial stack push as well as later redo.
     */
    void redo() override;

private:
    QPointer<SWMM2DMeshLayer> m_layer;
    QVector<mesh::CellCoupling> m_newRows;
    QVector<mesh::CellCoupling> m_oldRows;
};

/*!
 * \class MeshSetEdgeAttributeCommand
 * \brief Undoable write of one per-edge attribute across a set of edge slots.
 * \details Slots are stored flat (`mesh::edgeSlot(cell, edgeLocal)`) and both sides of the
 *          edit hold the WHOLE MeshEdgeBC, not just the changed field: the BC
 *          fields are interdependent through `type` (a stage value is
 *          meaningless once the type flips to Wall), so a partial restore would
 *          leave the slot in a state the user never saw.
 *
 *          Conveyance dispatches to applyMeshEdgeConveyance rather than
 *          applyMeshEdgeBC in BOTH directions, so the engine's interior-edge
 *          symmetry invariant (both halves carry the same value) survives undo.
 */
class MeshSetEdgeAttributeCommand : public MapCommand
{
public:
    /*! \param layer  Mesh layer that owns the edges.
     *  \param key    Attribute key ("conveyance", "bcType", "head", "slope",
     *                "flow", "tseries", "curve", "group", or "bc" for a whole
     *                boundary-condition write). "conveyance" selects the
     *                mirroring write path; every other key writes the slot.
     *  \param edgeSlots  Flat edge slots (`mesh::edgeSlot(cell, edgeLocal)`).
     *  \param newBCs Parallel to \p edgeSlots: post-edit slot value.
     *  \param oldBCs Parallel to \p edgeSlots: pre-edit slot value.
     * @brief Capture complete edge-condition snapshots for single-slot or mirrored-conveyance
     *   replay.
     * @param[in] text Undo-menu label copied verbatim.
     * @param[in] canvas Optional borrowed raw canvas stored by MapCommand; no direct invalidation
     *   by these commands.
     * @param[in] parent Optional QUndoCommand owner, otherwise stack owns after push.
     * @note GUI-thread only; layer is guarded by QPointer and deletion makes replay a no-op.
     *   Constructor captures supplied values without applying; caller snapshots BEFORE changes.
     *   Indices must still describe same mesh topology on replay; QPointer does not protect against
     *   topology replacement. Setter failures are ignored and batch writes are not atomic. Layer
     *   apply helpers supply attribute/repaint notifications; no command-level file save or engine
     *   transaction.
     * @note Flat slots use mesh::kEdgeStride=4, with triangle slot 3 padding. Replay decodes
     *   cell/local edge and delegates validity checks. Conveyance key writes only its fraction to
     *   mirrored interior halves; all other keys replay whole BC including group/conveyance. Stops
     *   at shorter slots/snapshot vector. Head project vertical units, flow project flow per metre,
     *   slope dimensionless; references remain names.
     */
    MeshSetEdgeAttributeCommand(SWMM2DMeshLayer *layer, QByteArray key, QVector<int> edgeSlots,
                                QVector<mesh::MeshEdgeBC> newBCs, QVector<mesh::MeshEdgeBC> oldBCs,
                                const QString &text, MapCanvas *canvas,
                                QUndoCommand *parent = nullptr);

    /**
     * @brief Restore old whole BCs or old mirrored conveyance fractions.
     * @note Null/deleted layer no-op. Mirrored interior neighbour follows conveyance setter; all
     *   statuses ignored. No per-write errors reported or batch rollback.
     */
    void undo() override;
    /**
     * @brief Apply after BC snapshots, mirroring conveyance-only writes when selected.
     * @note Null/deleted layer no-op. Whole BC for other keys; no boundary-only filter is performed
     *   at command replay level. Invoked on initial stack push as well as later redo.
     */
    void redo() override;
    /**
     * @return Mesh command family ID 46; this class supplies no mergeWith override, so edits remain
     *   separate undo steps.
     */
    int id() const override { return 46; }

private:
    /**
     * @brief Replay edge snapshots via mirrored-conveyance or complete boundary-slot setter.
     * @param[in] bcs Parallel edge rows; common prefix with stored flat slots visited.
     * @note Conveyance key uses only row fraction, other keys whole row. Decodes slot via stride
     *   four, no local boundary-only check; layer validates cell/local-edge limits and emits
     *   notifications. Null layer no-op/statuses ignored.
     */
    void apply(const QVector<mesh::MeshEdgeBC> &bcs);

    QPointer<SWMM2DMeshLayer> m_layer;
    QByteArray m_key;
    QVector<int> m_slots;
    QVector<mesh::MeshEdgeBC> m_newBCs;
    QVector<mesh::MeshEdgeBC> m_oldBCs;
};

/*!
 * \class MeshSetTriangleInfilCommand
 * \brief Undoable write of a COMPLETE per-cell infiltration row (method plus
 *        every positional parameter plus destination) across a set of
 *        triangles — GUI plan §3.5(3), phase GG0c.
 * \details MeshSetTriangleAttributeCommand carries one key and parallel
 *          double vectors, which is right for a single-column table edit and
 *          wrong for "assign this method and its five parameters to 400
 *          selected cells": that has to be one undo action, and the
 *          parameters only mean anything together.
 *
 *          **The old state records PROVENANCE, not just values.** A cell that
 *          was inheriting from its region tag (or the '*' default) must go
 *          back to inheriting on undo — restoring a materialised override
 *          carrying identical numbers looks correct but silently detaches the
 *          cell from its region, so the next region-level edit no longer
 *          reaches it. undo() therefore branches on the stored
 *          mesh::InfilProvenance: `Override` restores the old row,
 *          `Tag` / `Star` / `None` erase the override through
 *          SWMM2DMeshLayer::clearMeshTriangleInfil.
 *
 *          The layer is held as a QPointer: if it is closed, the command
 *          becomes inert rather than dangling.
 */
class MeshSetTriangleInfilCommand : public MapCommand
{
public:
    /*! \param layer      Mesh layer that owns the triangles.
     *  \param triangles  Triangle indices to write.
     *  \param newRow     The row every triangle in \p triangles receives.
     *  \param oldRows    Parallel to \p triangles: each cell's pre-edit
     *                    RESOLVED row.
     *  \param oldProv    Parallel to \p triangles: where that row came from.
     *                    Anything but `Override` means the cell was
     *                    inheriting and undo must erase, not rewrite.
     *  \param text       Undo-stack label.
     * @brief Capture whole infiltration overrides plus prior resolved rows/provenance for faithful
     *   inheritance undo.
     * @param[in] canvas Optional borrowed raw canvas stored by MapCommand; no direct invalidation
     *   by these commands.
     * @param[in] parent Optional QUndoCommand owner, otherwise stack owns after push.
     * @note GUI-thread only; layer is guarded by QPointer and deletion makes replay a no-op.
     *   Constructor captures supplied values without applying; caller snapshots BEFORE changes.
     *   Indices must still describe same mesh topology on replay; QPointer does not protect against
     *   topology replacement. Setter failures are ignored and batch writes are not atomic. Layer
     *   apply helpers supply attribute/repaint notifications; no command-level file save or engine
     *   transaction.
     * @note New row uses method-specific positional parameter conventions in InfilRow; no row
     *   validation here. Redo attempts every cell. Undo visits cells only while provenance
     *   available; Override needs corresponding old row, any other provenance clears override. No
     *   transactional rollback or topology/version guard.
     */
    MeshSetTriangleInfilCommand(SWMM2DMeshLayer *layer, QVector<int> triangles,
                                mesh::InfilRow newRow, QVector<mesh::InfilRow> oldRows,
                                QVector<mesh::InfilProvenance> oldProv, const QString &text,
                                MapCanvas *canvas, QUndoCommand *parent = nullptr);
    MeshSetTriangleInfilCommand(SWMM2DMeshLayer *layer, QVector<int> triangles,
                                QVector<mesh::InfilRow> newRows,
                                QVector<mesh::InfilRow> oldRows,
                                QVector<mesh::InfilProvenance> oldProv, const QString &text,
                                MapCanvas *canvas);

    /**
     * @brief Restore old override rows or clear overrides to recover tag/star/no-row inheritance.
     * @note Null/deleted layer no-op. Visits shorter cells/provenance; missing old row for Override
     *   is skipped. No per-write errors reported or batch rollback.
     */
    void undo() override;
    /**
     * @brief Write the stored uniform or per-cell complete infiltration overrides.
     * @note Null/deleted layer no-op. No local editability/row validation; layer rejects invalid
     *   cells or values as applicable, statuses ignored. Invoked on initial stack push as well as
     *   later redo.
     */
    void redo() override;
    /**
     * @return Mesh command family ID 49; this class supplies no mergeWith override, so edits remain
     *   separate undo steps.
     */
    int id() const override { return 49; }

private:
    QPointer<SWMM2DMeshLayer> m_layer;
    QVector<int> m_tris;
    mesh::InfilRow m_newRow;
    QVector<mesh::InfilRow> m_newRows;
    QVector<mesh::InfilRow> m_oldRows;
    QVector<mesh::InfilProvenance> m_oldProv;
};

/*!
 * \class MeshSetInfilDefaultsCommand
 * \brief Undoable write of one or more `[2D_INFILTRATION_DEFAULTS]` tag rows —
 *        the region-level peer of MeshSetTriangleInfilCommand.
 * \details Editing a default is how an assignment stays editable as regions
 *          instead of being frozen into N per-cell rows (engine D-I3): the row
 *          reaches every cell carrying the tag that has no override of its own,
 *          and none of them gain one.
 *
 *          The old state records, per tag, both the previous row and whether a
 *          row EXISTED — the same distinction MeshSetTriangleInfilCommand draws
 *          per cell. A tag that had no default row must go back to having none:
 *          writing the resolved numbers back would leave a row that shadows the
 *          '*' fallback, so a later edit to '*' would stop reaching the region.
 *          undo() therefore erases through SWMM2DMeshLayer::clearMeshInfilDefault
 *          for those tags and rewrites through applyMeshInfilDefault for the rest.
 *
 *          The layer is held as a QPointer: if it is closed, the command
 *          becomes inert rather than dangling.
 */
class MeshSetInfilDefaultsCommand : public MapCommand
{
public:
    /*! \param layer     Mesh layer that owns the mesh.
     *  \param tags      Region tags to write ("*" = the mesh-wide fallback).
     *  \param newRows   Parallel to \p tags: the row each tag receives.
     *  \param oldRows   Parallel to \p tags: each tag's pre-edit row (ignored
     *                   where \p oldExisted is false).
     *  \param oldExisted Parallel to \p tags: false means the tag had no
     *                   default row, so undo must ERASE rather than rewrite.
     *  \param text      Undo-stack label.
     * @brief Capture tag-default rows and existence flags so undo can erase previously absent
     *   defaults.
     * @param[in] canvas Optional borrowed raw canvas stored by MapCommand; no direct invalidation
     *   by these commands.
     * @param[in] parent Optional QUndoCommand owner, otherwise stack owns after push.
     * @note GUI-thread only; layer is guarded by QPointer and deletion makes replay a no-op.
     *   Constructor captures supplied values without applying; caller snapshots BEFORE changes.
     *   Indices must still describe same mesh topology on replay; QPointer does not protect against
     *   topology replacement. Setter failures are ignored and batch writes are not atomic. Layer
     *   apply helpers supply attribute/repaint notifications; no command-level file save or engine
     *   transaction.
     * @note Row values keep method-specific InfilRow conventions. Redo stops at shorter tag/new-row
     *   vector; undo stops at shorter tag/existence vector, requiring old row only when flag true.
     *   Does not validate/deduplicate tags or rows at constructor level.
     */
    MeshSetInfilDefaultsCommand(SWMM2DMeshLayer *layer, QVector<QString> tags,
                                QVector<mesh::InfilRow> newRows, QVector<mesh::InfilRow> oldRows,
                                QVector<bool> oldExisted, const QString &text, MapCanvas *canvas,
                                QUndoCommand *parent = nullptr);

    /**
     * @brief Restore existing prior tag-default rows and erase rows originally absent.
     * @note Null/deleted layer no-op. Visits shorter tags/existence flags; missing old row for true
     *   flag skipped. No per-write errors reported or batch rollback.
     */
    void undo() override;
    /**
     * @brief Write parallel target region-default rows.
     * @note Null/deleted layer no-op. Stops at shorter vectors; repeated tags follow supplied
     *   order, statuses ignored. Invoked on initial stack push as well as later redo.
     */
    void redo() override;
    /**
     * @return Mesh command family ID 50; this class supplies no mergeWith override, so edits remain
     *   separate undo steps.
     */
    int id() const override { return 50; }

private:
    QPointer<SWMM2DMeshLayer> m_layer;
    QVector<QString> m_tags;
    QVector<mesh::InfilRow> m_newRows;
    QVector<mesh::InfilRow> m_oldRows;
    QVector<bool> m_oldExisted;
};

namespace mesh
{

/*!
 * \brief Snapshot the current values, then push one undoable edit writing
 *        \p value to every triangle in \p triangles.
 *
 * Triangles already holding \p value are dropped from the command, so a
 * no-op edit does not clutter the undo stack. With no undo stack available
 * the write still happens, just unundoably.
 *
 * \returns the number of filtered candidate triangle entries attempted.
 * @param[in] layer Optional borrowed mesh layer; null returns zero.
 * @param[in] canvas Optional borrowed canvas; live undo stack takes ownership of command and
 *   immediately redoes it, otherwise command redoes once then is deleted.
 * @param[in] triangles Zero-based candidate cell indices, invalid skipped.
 * @param[in] key Enabled registry key; unknown/disabled returns zero.
 * @param[in] value Shared finite target in key-specific project units; registry editor min/max are
 *   not enforced by funnel.
 * @note GUI-thread only. Return counts filtered candidate entries queued/attempted, not confirmed
 *   setter successes; duplicate indices remain unless explicitly stated. Commands do not report
 *   individual rejected writes or roll back partial results. No-op/invalid target sets push
 *   nothing. Layer emits actual edit notifications; no immediate file save.
 * @note Skips exact finite current-value matches and infiltration-ineligible cells. Nonfinite
 *   previous values snapshotted then become registry defaults on undo; single-field infiltration
 *   writes do not preserve inheritance.
 */
int pushCellParamEdit(SWMM2DMeshLayer *layer, const QVector<int> &triangles, const QByteArray &key,
                      double value, MapCanvas *canvas);

/*!
 * \brief Per-triangle variant for assignments that write a different value to
 *        each cell (raster sampling, shapefile field joins).
 * \param triangles Indices to write; \p values is parallel to it.
 * @param[in] layer Optional borrowed mesh layer; null returns zero.
 * @param[in] canvas Optional borrowed canvas; live undo stack takes ownership of command and
 *   immediately redoes it, otherwise command redoes once then is deleted.
 * @param[in] values Parallel finite targets in registry units; stops at shorter index/value vector.
 * @param[in] key Enabled registry key, exact byte key.
 * @param[in] text Undo label passed unchanged.
 * @note GUI-thread only. Return counts filtered candidate entries queued/attempted, not confirmed
 *   setter successes; duplicate indices remain unless explicitly stated. Commands do not report
 *   individual rejected writes or roll back partial results. No-op/invalid target sets push
 *   nothing. Layer emits actual edit notifications; no immediate file save.
 * @return Number of changed candidate entries, zero for null/invalid/no-op; not confirmed write
 *   count. Filters as scalar funnel, including method-specific infiltration editability but no
 *   editor min/max clamp.
 */
int pushCellParamEdits(SWMM2DMeshLayer *layer, const QVector<int> &triangles,
                       const QVector<double> &values, const QByteArray &key, const QString &text,
                       MapCanvas *canvas);

/*!
 * \brief Snapshot each cell's resolved infiltration row AND where it came
 *        from, then push one undoable edit writing \p row as a per-cell
 *        `[2D_INFILTRATION]` override on every triangle in \p triangles.
 *
 * The single funnel every infiltration editing surface uses — attribute
 * table, "Assign Infiltration to Selection…", the raster/shapefile assign
 * dialog — so a 400-cell assignment is one undo entry no matter where it came
 * from, and undo restores INHERITANCE for cells that were inheriting (see
 * MeshSetTriangleInfilCommand).
 *
 * Cells that already carry an override equal to \p row are dropped, so a
 * no-op edit does not clutter the undo stack. A cell that merely *inherits*
 * the same numbers is NOT dropped: materialising the override there is a real
 * state change (the cell stops tracking its region), and it is exactly what
 * the user asked for. With no undo stack available the write still happens,
 * just unundoably.
 *
 * \returns the number of filtered candidate triangle entries attempted.
 * @param[in] layer Optional borrowed mesh layer; null returns zero.
 * @param[in] canvas Optional borrowed canvas; live undo stack takes ownership of command and
 *   immediately redoes it, otherwise command redoes once then is deleted.
 * @param[in] triangles Candidate zero-based cells; invalid or surface-infiltration-ineligible
 *   skipped.
 * @param[in] row Complete target override in InfilRow method/parameter conventions; no local
 *   numeric validation.
 * @note GUI-thread only. Return counts filtered candidate entries queued/attempted, not confirmed
 *   setter successes; duplicate indices remain unless explicitly stated. Commands do not report
 *   individual rejected writes or roll back partial results. No-op/invalid target sets push
 *   nothing. Layer emits actual edit notifications; no immediate file save.
 * @note Exact equal existing overrides skipped, equal inherited rows retained to materialize
 *   override. Snapshots resolved old row AND provenance; undo clears newly introduced override when
 *   prior state inherited.
 */
int pushCellInfilEdit(SWMM2DMeshLayer *layer, const QVector<int> &triangles, const InfilRow &row,
                      MapCanvas *canvas);
/** Same provenance-preserving edit, with a distinct resolved target row per cell. */
int pushCellInfilEdits(SWMM2DMeshLayer *layer, const QVector<int> &triangles,
                       const QVector<InfilRow> &rows, MapCanvas *canvas);

/*!
 * \brief Snapshot each tag's existing `[2D_INFILTRATION_DEFAULTS]` row (and
 *        whether it had one at all), then push one undoable edit writing
 *        \p rows as region defaults.
 *
 * The region-level funnel every surface uses — the raster/shapefile assign
 * dialog's "Region defaults (by tag)" write target and the "apply to region
 * tag instead" route in the assign-to-selection dialog — so a multi-tag
 * assignment is one undo entry and undo restores the ABSENCE of a row for a
 * tag that had none (see MeshSetInfilDefaultsCommand).
 *
 * Rows whose tag already carries an identical default are dropped, so a no-op
 * edit does not clutter the undo stack; rows with an empty tag are rejected.
 * With no undo stack available the write still happens, just unundoably.
 *
 * \param rows Ordered; a duplicate tag later in the list wins, matching the
 *             layer's last-write-wins mutator.
 * \returns the number of filtered unique tag rows attempted.
 * @param[in] layer Optional borrowed mesh layer; null returns zero.
 * @param[in] canvas Optional borrowed canvas; live undo stack takes ownership of command and
 *   immediately redoes it, otherwise command redoes once then is deleted.
 * @note GUI-thread only. Return counts filtered candidate entries queued/attempted, not confirmed
 *   setter successes; duplicate indices remain unless explicitly stated. Commands do not report
 *   individual rejected writes or roll back partial results. No-op/invalid target sets push
 *   nothing. Layer emits actual edit notifications; no immediate file save.
 * @note Nonempty tags are exact/case-sensitive. Collapses duplicate included tags to latest row
 *   while retaining original old state; a tag initially skipped as equal can be included by later
 *   differing row. Final equality is not rechecked after collapse, so count may include final equal
 *   values. Row validity delegated to layer.
 */
int pushInfilDefaultsEdit(SWMM2DMeshLayer *layer, const QVector<InfilDefaultRow> &rows,
                          MapCanvas *canvas);

/*!
 * \brief Undoable write of the descriptive `[2D_TRIANGLES]` TAG to every
 *        triangle in \p triangles.
 * \returns the number of filtered candidate triangle entries attempted.
 * @param[in] layer Optional borrowed mesh layer; null returns zero.
 * @param[in] canvas Optional borrowed canvas; live undo stack takes ownership of command and
 *   immediately redoes it, otherwise command redoes once then is deleted.
 * @param[in] triangles Candidate cell indices; invalid/exact-equal current tags skipped.
 * @param[in] tag Descriptive tag copied unchanged, including empty clear.
 * @note GUI-thread only. Return counts filtered candidate entries queued/attempted, not confirmed
 *   setter successes; duplicate indices remain unless explicitly stated. Commands do not report
 *   individual rejected writes or roll back partial results. No-op/invalid target sets push
 *   nothing. Layer emits actual edit notifications; no immediate file save.
 * @note Stores parallel old tags; does not assign infiltration-default rows, although changing tags
 *   affects later inherited resolution.
 */
int pushCellTagEdit(SWMM2DMeshLayer *layer, const QVector<int> &triangles, const QString &tag,
                    MapCanvas *canvas);

/*!
 * \brief Snapshot the current values, then push one undoable edit writing
 *        \p value into attribute \p key on every vertex in \p vertices.
 *
 * \param key   One of "z", "tag", "coupledNode", "couplingCd",
 *              "couplingArea". Unknown keys are rejected.
 * \param value Typed to match the key (double for z / Cd / area, string for
 *              tag / coupledNode).
 *
 * Vertices already holding \p value are dropped, so a no-op edit does not
 * clutter the undo stack. With no undo stack available the write still
 * happens, just unundoably.
 *
 * \returns the number of filtered candidate vertex entries attempted.
 * @param[in] layer Optional borrowed mesh layer; null returns zero.
 * @param[in] canvas Optional borrowed canvas; live undo stack takes ownership of command and
 *   immediately redoes it, otherwise command redoes once then is deleted.
 * @param[in] vertices Candidate mesh vertex indices; invalid skipped.
 * @note GUI-thread only. Return counts filtered candidate entries queued/attempted, not confirmed
 *   setter successes; duplicate indices remain unless explicitly stated. Commands do not report
 *   individual rejected writes or roll back partial results. No-op/invalid target sets push
 *   nothing. Layer emits actual edit notifications; no immediate file save.
 * @note Z must convert to finite double in project vertical units. Tag/node use toString;
 *   coefficient/area require >0 and coupled vertex, but positive infinity is not explicitly
 *   filtered. Coefficient edit marks authored flag. Snapshots all editable attributes; same
 *   comparison uses exact doubles, so NaN fields never equal. Clearing node is delegated to replay
 *   and may reset coefficient fields through layer preconditions.
 */
int pushVertexParamEdit(SWMM2DMeshLayer *layer, const QVector<int> &vertices, const QByteArray &key,
                        const QVariant &value, MapCanvas *canvas);

/*!
 * \brief Undoable write of one edge attribute to every edge in \p edges
 *        (each a `(triIdx, edgeLocal)` pair).
 *
 * \param key   One of "conveyance", "bcType", "head", "slope", "flow",
 *              "tseries", "curve", "group". The BC keys apply to BOUNDARY
 *              edges only — interior slots in \p edges are skipped, mirroring
 *              the toolbar's gating — while "conveyance" applies to every edge.
 *
 * \returns the number of filtered candidate edge entries attempted.
 * @param[in] layer Optional borrowed mesh layer; null returns zero.
 * @param[in] canvas Optional borrowed canvas; live undo stack takes ownership of command and
 *   immediately redoes it, otherwise command redoes once then is deleted.
 * @param[in] edges Candidate cell/local-edge pairs; validates flat slot bounds, layer applies
 *   actual cell arity.
 * @param[in] value Typed value: conveyance fraction 0..1, BC enum range, project
 *   head/flow-per-metre/slope or reference/group string. Head/slope/flow toDouble has no
 *   finite/range check here.
 * @note GUI-thread only. Return counts filtered candidate entries queued/attempted, not confirmed
 *   setter successes; duplicate indices remain unless explicitly stated. Commands do not report
 *   individual rejected writes or roll back partial results. No-op/invalid target sets push
 *   nothing. Layer emits actual edit notifications; no immediate file save.
 * @note Non-conveyance keys require boundary edges; conveyance can touch interior and setter
 *   mirrors neighbour. Whole snapshot equality uses exact doubles, including ordinary NaN
 *   inequality. Unknown keys rejected.
 */
int pushEdgeParamEdit(SWMM2DMeshLayer *layer, const QVector<QPair<int, int>> &edges,
                      const QByteArray &key, const QVariant &value, MapCanvas *canvas);

/*!
 * \brief Undoable write of a complete boundary condition (type plus whichever
 *        parameter that type carries) to every BOUNDARY edge in \p edges.
 *
 * Each slot keeps its own `group` label and `conveyance` — both are orthogonal
 * to the BC type and edited elsewhere. Interior edges are skipped.
 *
 * \returns the number of filtered candidate edge entries attempted.
 * @param[in] layer Optional borrowed mesh layer; null returns zero.
 * @param[in] canvas Optional borrowed canvas; live undo stack takes ownership of command and
 *   immediately redoes it, otherwise command redoes once then is deleted.
 * @param[in] edges Candidate cell/local-edge pairs; invalid/nonboundary skipped.
 * @param[in] bc Complete target BC copied, except each edge keeps own group and conveyance. Head
 *   project vertical units, slope dimensionless, flow project flow per metre; no local numeric/type
 *   validation.
 * @note GUI-thread only. Return counts filtered candidate entries queued/attempted, not confirmed
 *   setter successes; duplicate indices remain unless explicitly stated. Commands do not report
 *   individual rejected writes or roll back partial results. No-op/invalid target sets push
 *   nothing. Layer emits actual edit notifications; no immediate file save.
 * @note Skips exact full-row equality after preserving orthogonal fields, otherwise stores full
 *   old/new rows. Current boundary test/slot checks determine eligible targets.
 */
int pushEdgeBCEdit(SWMM2DMeshLayer *layer, const QVector<QPair<int, int>> &edges,
                   const MeshEdgeBC &bc, MapCanvas *canvas);

} // namespace mesh

#endif // OPENSWMMVIS_MAP_MESHCOMMANDS_H
