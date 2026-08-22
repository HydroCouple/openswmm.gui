/*!
 * \file   meshcommands.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * Undoable per-cell 2D mesh attribute edits. One command covers a whole
 * selection (or a whole raster/shapefile assignment), so a bulk write is a
 * single Ctrl+Z rather than thousands.
 *
 * Every editing surface — the mesh-editing toolbar, the properties panel
 * adapter, and the Cell Data assignment dialog — pushes through
 * pushCellParamEdit() so undo behaves identically no matter where the edit
 * came from.
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
#include <QString>
#include <QVariant>
#include <QVector>

class SWMM2DMeshLayer;

/*!
 * \class MeshSetTriangleAttributeCommand
 * \brief Undoable write of one per-cell parameter across a set of triangles.
 * \details Stores the pre-edit value of every touched triangle (NaN for
 *          "was unset"). redo() writes the new values, undo() restores the
 *          old ones — both through the file-local `applyCellParam` helper in
 *          meshcommands.cpp, so the layer emits `attributeChanged` and every
 *          view refreshes either way. The layer is held as a QPointer: if it
 *          is closed, the command becomes inert rather than dangling.
 */
class MeshSetTriangleTagCommand : public MapCommand
{
public:
    MeshSetTriangleTagCommand(SWMM2DMeshLayer   *layer,
                              QVector<int>       triangles,
                              QString            newTag,
                              QVector<QString>   oldTags,
                              const QString     &text,
                              MapCanvas         *canvas,
                              QUndoCommand      *parent = nullptr);

    void undo() override;
    void redo() override;
    int  id()   const override { return 44; }

private:
    QPointer<SWMM2DMeshLayer> m_layer;
    QVector<int>              m_tris;
    QString                   m_newTag;
    QVector<QString>          m_oldTags;
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
     *  \param text      Undo-stack label. */
    MeshSetTriangleAttributeCommand(SWMM2DMeshLayer      *layer,
                                    QByteArray            key,
                                    QVector<int>          triangles,
                                    QVector<double>       newValues,
                                    QVector<double>       oldValues,
                                    const QString        &text,
                                    MapCanvas            *canvas,
                                    QUndoCommand         *parent = nullptr);

    void undo() override;
    void redo() override;
    int  id()   const override { return 43; }

private:
    void apply(const QVector<double> &values);

    QPointer<SWMM2DMeshLayer> m_layer;
    QByteArray                m_key;
    QVector<int>              m_tris;
    QVector<double>           m_newValues;
    QVector<double>           m_oldValues;
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
     *  \param oldAttrs  Parallel to \p vertices: pre-edit attribute snapshot. */
    MeshSetVertexAttributeCommand(SWMM2DMeshLayer          *layer,
                                  QByteArray                key,
                                  QVector<int>              vertices,
                                  QVector<mesh::MeshVertex> newAttrs,
                                  QVector<mesh::MeshVertex> oldAttrs,
                                  const QString            &text,
                                  MapCanvas                *canvas,
                                  QUndoCommand             *parent = nullptr);

    void undo() override;
    void redo() override;
    int  id()   const override { return 45; }

private:
    void apply(const QVector<mesh::MeshVertex> &attrs);

    QPointer<SWMM2DMeshLayer> m_layer;
    QByteArray                m_key;
    QVector<int>              m_verts;
    QVector<mesh::MeshVertex> m_newAttrs;
    QVector<mesh::MeshVertex> m_oldAttrs;
};

/*!
 * \class MeshSetEdgeAttributeCommand
 * \brief Undoable write of one per-edge attribute across a set of edge slots.
 * \details Slots are stored flat (`tri * 3 + edgeLocal`) and both sides of the
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
     *  \param edgeSlots  Flat edge indices (`tri * 3 + edgeLocal`).
     *  \param newBCs Parallel to \p edgeSlots: post-edit slot value.
     *  \param oldBCs Parallel to \p edgeSlots: pre-edit slot value. */
    MeshSetEdgeAttributeCommand(SWMM2DMeshLayer          *layer,
                                QByteArray                key,
                                QVector<int>              edgeSlots,
                                QVector<mesh::MeshEdgeBC> newBCs,
                                QVector<mesh::MeshEdgeBC> oldBCs,
                                const QString            &text,
                                MapCanvas                *canvas,
                                QUndoCommand             *parent = nullptr);

    void undo() override;
    void redo() override;
    int  id()   const override { return 46; }

private:
    void apply(const QVector<mesh::MeshEdgeBC> &bcs);

    QPointer<SWMM2DMeshLayer> m_layer;
    QByteArray                m_key;
    QVector<int>              m_slots;
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
 *          selected cells": that has to be one atomic action, and the
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
     *  \param text       Undo-stack label. */
    MeshSetTriangleInfilCommand(SWMM2DMeshLayer               *layer,
                                QVector<int>                   triangles,
                                mesh::InfilRow                 newRow,
                                QVector<mesh::InfilRow>        oldRows,
                                QVector<mesh::InfilProvenance> oldProv,
                                const QString                 &text,
                                MapCanvas                     *canvas,
                                QUndoCommand                  *parent = nullptr);

    void undo() override;
    void redo() override;
    int  id()   const override { return 49; }

private:
    QPointer<SWMM2DMeshLayer>      m_layer;
    QVector<int>                   m_tris;
    mesh::InfilRow                 m_newRow;
    QVector<mesh::InfilRow>        m_oldRows;
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
     *  \param text      Undo-stack label. */
    MeshSetInfilDefaultsCommand(SWMM2DMeshLayer         *layer,
                                QVector<QString>         tags,
                                QVector<mesh::InfilRow>  newRows,
                                QVector<mesh::InfilRow>  oldRows,
                                QVector<bool>            oldExisted,
                                const QString           &text,
                                MapCanvas               *canvas,
                                QUndoCommand            *parent = nullptr);

    void undo() override;
    void redo() override;
    int  id()   const override { return 50; }

private:
    QPointer<SWMM2DMeshLayer> m_layer;
    QVector<QString>          m_tags;
    QVector<mesh::InfilRow>   m_newRows;
    QVector<mesh::InfilRow>   m_oldRows;
    QVector<bool>             m_oldExisted;
};

namespace mesh {

/*!
 * \brief Snapshot the current values, then push one undoable edit writing
 *        \p value to every triangle in \p triangles.
 *
 * Triangles already holding \p value are dropped from the command, so a
 * no-op edit does not clutter the undo stack. With no undo stack available
 * the write still happens, just unundoably.
 *
 * \returns the number of triangles actually changed.
 */
int pushCellParamEdit(SWMM2DMeshLayer *layer, const QVector<int> &triangles,
                      const QByteArray &key, double value, MapCanvas *canvas);

/*!
 * \brief Per-triangle variant for assignments that write a different value to
 *        each cell (raster sampling, shapefile field joins).
 * \param triangles Indices to write; \p values is parallel to it.
 */
int pushCellParamEdits(SWMM2DMeshLayer *layer, const QVector<int> &triangles,
                       const QVector<double> &values, const QByteArray &key,
                       const QString &text, MapCanvas *canvas);

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
 * \returns the number of triangles actually changed.
 */
int pushCellInfilEdit(SWMM2DMeshLayer *layer, const QVector<int> &triangles,
                      const InfilRow &row, MapCanvas *canvas);

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
 * \returns the number of tag rows actually changed.
 */
int pushInfilDefaultsEdit(SWMM2DMeshLayer *layer,
                          const QVector<InfilDefaultRow> &rows,
                          MapCanvas *canvas);

/*!
 * \brief Undoable write of the descriptive `[2D_TRIANGLES]` TAG to every
 *        triangle in \p triangles.
 * \returns the number of triangles actually changed.
 */
int pushCellTagEdit(SWMM2DMeshLayer *layer, const QVector<int> &triangles,
                    const QString &tag, MapCanvas *canvas);

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
 * \returns the number of vertices actually changed.
 */
int pushVertexParamEdit(SWMM2DMeshLayer *layer, const QVector<int> &vertices,
                        const QByteArray &key, const QVariant &value,
                        MapCanvas *canvas);

/*!
 * \brief Undoable write of one edge attribute to every edge in \p edges
 *        (each a `(triIdx, edgeLocal)` pair).
 *
 * \param key   One of "conveyance", "bcType", "head", "slope", "flow",
 *              "tseries", "curve", "group". The BC keys apply to BOUNDARY
 *              edges only — interior slots in \p edges are skipped, mirroring
 *              the toolbar's gating — while "conveyance" applies to every edge.
 *
 * \returns the number of edge slots actually changed.
 */
int pushEdgeParamEdit(SWMM2DMeshLayer *layer,
                      const QVector<QPair<int, int>> &edges,
                      const QByteArray &key, const QVariant &value,
                      MapCanvas *canvas);

/*!
 * \brief Undoable write of a complete boundary condition (type plus whichever
 *        parameter that type carries) to every BOUNDARY edge in \p edges.
 *
 * Each slot keeps its own `group` label and `conveyance` — both are orthogonal
 * to the BC type and edited elsewhere. Interior edges are skipped.
 *
 * \returns the number of edge slots actually changed.
 */
int pushEdgeBCEdit(SWMM2DMeshLayer *layer,
                   const QVector<QPair<int, int>> &edges,
                   const MeshEdgeBC &bc, MapCanvas *canvas);

} // namespace mesh

#endif // OPENSWMMVIS_MAP_MESHCOMMANDS_H
